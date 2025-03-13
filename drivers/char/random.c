// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright (C) 2017-2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright Matt Mackall <mpm@selenic.com>, 2003, 2004, 2005
 * Copyright Theodore Ts'o, 1994, 1995, 1996, 1997, 1998, 1999. All rights reserved.
 *
 * This driver produces cryptographically secure pseudorandom data. It is divided
 * into roughly six sections, each with a section header:
 *
 *   - Initialization and readiness waiting.
 *   - Fast key erasure RNG, the "crng".
 *   - Entropy accumulation and extraction routines.
 *   - Entropy collection routines.
 *   - Userspace reader/writer interfaces.
 *   - Sysctl interface.
 *
 * The high level overview is that there is one input pool, into which
 * various pieces of data are hashed. Prior to initialization, some of that
 * data is then "credited" as having a certain number of bits of entropy.
 * When enough bits of entropy are available, the hash is finalized and
 * handed as a key to a stream cipher that expands it indefinitely for
 * various consumers. This key is periodically refreshed as the various
 * entropy collectors, described below, add data to the input pool.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/utsname.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/nodemask.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/ratelimit.h>
#include <linux/syscalls.h>
#include <linux/completion.h>
#include <linux/uuid.h>
#include <linux/uaccess.h>
#include <linux/suspend.h>
#include <linux/siphash.h>
#include <linux/sched/isolation.h>
#include <crypto/chacha.h>
#include <crypto/blake2s.h>
#ifdef CONFIG_VDSO_GETRANDOM
#include <vdso/getrandom.h>
#include <vdso/datapage.h>
#include <vdso/vsyscall.h>
#endif
#include <asm/archrandom.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/io.h>

/*********************************************************************
 *
 * Initialization and readiness waiting.
 *
 * Much of the RNG infrastructure is devoted to various dependencies
 * being able to wait until the RNG has collected enough entropy and
 * is ready for safe consumption.
 *
 *********************************************************************/

/*
 * crng_init is protected by base_crng->lock, and only increases
 * its value (from empty->early->ready).
 */
static enum {
	CRNG_EMPTY = 0, /* Little to no entropy collected */
	CRNG_EARLY = 1, /* At least POOL_EARLY_BITS collected */
	CRNG_READY = 2  /* Fully initialized with POOL_READY_BITS collected */
} crng_init __read_mostly = CRNG_EMPTY;
static DEFINE_STATIC_KEY_FALSE(crng_is_ready);
#define crng_ready() (static_branch_likely(&crng_is_ready) || crng_init >= CRNG_READY)
/* Various types of waiters for crng_init->CRNG_READY transition. */
static DECLARE_WAIT_QUEUE_HEAD(crng_init_wait);
static struct fasync_struct *fasync;
static ATOMIC_NOTIFIER_HEAD(random_ready_notifier);

/* Control how we warn userspace. */
static struct ratelimit_state urandom_warning =
	RATELIMIT_STATE_INIT_FLAGS("urandom_warning", HZ, 3, RATELIMIT_MSG_ON_RELEASE);
static int ratelimit_disable __read_mostly =
	IS_ENABLED(CONFIG_WARN_ALL_UNSEEDED_RANDOM);
module_param_named(ratelimit_disable, ratelimit_disable, int, 0644);
MODULE_PARM_DESC(ratelimit_disable, "Disable random ratelimit suppression");

/*
 * Returns whether or not the input pool has been seeded and thus guaranteed
 * to supply cryptographically secure random numbers. This applies to: the
 * /dev/urandom device, the get_random_bytes function, and the get_random_{u8,
 * u16,u32,u64,long} family of functions.
 *
 * Returns: true if the input pool has been seeded.
 *          false if the input pool has not been seeded.
 */
bool rng_is_initialized(void)
{
	return crng_ready();
}
EXPORT_SYMBOL(rng_is_initialized);

static void __cold crng_set_ready(struct work_struct *work)
{
	static_branch_enable(&crng_is_ready);
}

/* Used by wait_for_random_bytes(), and considered an entropy collector, below. */
static void try_to_generate_entropy(void);

/*
 * Wait for the input pool to be seeded and thus guaranteed to supply
 * cryptographically secure random numbers. This applies to: the /dev/urandom
 * device, the get_random_bytes function, and the get_random_{u8,u16,u32,u64,
 * long} family of functions. Using any of these functions without first
 * calling this function forfeits the guarantee of security.
 *
 * Returns: 0 if the input pool has been seeded.
 *          -ERESTARTSYS if the function was interrupted by a signal.
 */
int wait_for_random_bytes(void)
{
	while (!crng_ready()) {
		int ret;

		try_to_generate_entropy();
		ret = wait_event_interruptible_timeout(crng_init_wait, crng_ready(), HZ);
		if (ret)
			return ret > 0 ? 0 : ret;
	}
	return 0;
}
EXPORT_SYMBOL(wait_for_random_bytes);

/*
 * Add a callback function that will be invoked when the crng is initialised,
 * or immediately if it already has been. Only use this is you are absolutely
 * sure it is required. Most users should instead be able to test
 * `rng_is_initialized()` on demand, or make use of `get_random_bytes_wait()`.
 */
int __cold execute_with_initialized_rng(struct notifier_block *nb)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&random_ready_notifier.lock, flags);
	if (crng_ready())
		nb->notifier_call(nb, 0, NULL);
	else
		ret = raw_notifier_chain_register((struct raw_notifier_head *)&random_ready_notifier.head, nb);
	spin_unlock_irqrestore(&random_ready_notifier.lock, flags);
	return ret;
}

#define warn_unseeded_randomness() \
	if (IS_ENABLED(CONFIG_WARN_ALL_UNSEEDED_RANDOM) && !crng_ready()) \
		printk_deferred(KERN_NOTICE "random: %s called from %pS with crng_init=%d\n", \
				__func__, (void *)_RET_IP_, crng_init)


/*********************************************************************
 *
 * Fast key erasure RNG, the "crng".
 *
 * These functions expand entropy from the entropy extractor into
 * long streams for external consumption using the "fast key erasure"
 * RNG described at <https://blog.cr.yp.to/20170723-random.html>.
 *
 * There are a few exported interfaces for use by other drivers:
 *
 *	void get_random_bytes(void *buf, size_t len)
 *	u8 get_random_u8()
 *	u16 get_random_u16()
 *	u32 get_random_u32()
 *	u32 get_random_u32_below(u32 ceil)
 *	u32 get_random_u32_above(u32 floor)
 *	u32 get_random_u32_inclusive(u32 floor, u32 ceil)
 *	u64 get_random_u64()
 *	unsigned long get_random_long()
 *
 * These interfaces will return the requested number of random bytes
 * into the given buffer or as a return value. This is equivalent to
 * a read from /dev/urandom. The u8, u16, u32, u64, long family of
 * functions may be higher performance for one-off random integers,
 * because they do a bit of buffering and do not invoke reseeding
 * until the buffer is emptied.
 *
 *********************************************************************/

enum {
	CRNG_RESEED_START_INTERVAL = HZ,
	CRNG_RESEED_INTERVAL = 60 * HZ
};

static struct {
	u8 key[CHACHA_KEY_SIZE] __aligned(__alignof__(long));
	unsigned long generation;
	spinlock_t lock;
} base_crng = {
	.lock = __SPIN_LOCK_UNLOCKED(base_crng.lock)
};

struct crng {
	u8 key[CHACHA_KEY_SIZE];
	unsigned long generation;
	local_lock_t lock;
};

static DEFINE_PER_CPU(struct crng, crngs) = {
	.generation = ULONG_MAX,
	.lock = INIT_LOCAL_LOCK(crngs.lock),
};

/*
 * Return the interval until the next reseeding, which is normally
 * CRNG_RESEED_INTERVAL, but during early boot, it is at an interval
 * proportional to the uptime.
 */
static unsigned int crng_reseed_interval(void)
{
	static bool early_boot = true;

	if (unlikely(READ_ONCE(early_boot))) {
		time64_t uptime = ktime_get_seconds();
		if (uptime >= CRNG_RESEED_INTERVAL / HZ * 2)
			WRITE_ONCE(early_boot, false);
		else
			return max_t(unsigned int, CRNG_RESEED_START_INTERVAL,
				     (unsigned int)uptime / 2 * HZ);
	}
	return CRNG_RESEED_INTERVAL;
}

/* Used by crng_reseed() and crng_make_state() to extract a new seed from the input pool. */
static void extract_entropy(void *buf, size_t len);

/* This extracts a new crng key from the input pool. */
static void crng_reseed(struct work_struct *work)
{
	static DECLARE_DELAYED_WORK(next_reseed, crng_reseed);
	unsigned long flags;
	unsigned long next_gen;
	u8 key[CHACHA_KEY_SIZE];

	/* Immediately schedule the next reseeding, so that it fires sooner rather than later. */
	if (likely(system_unbound_wq))
		queue_delayed_work(system_unbound_wq, &next_reseed, crng_reseed_interval());

	extract_entropy(key, sizeof(key));

	/*
	 * We copy the new key into the base_crng, overwriting the old one,
	 * and update the generation counter. We avoid hitting ULONG_MAX,
	 * because the per-cpu crngs are initialized to ULONG_MAX, so this
	 * forces new CPUs that come online to always initialize.
	 */
	spin_lock_irqsave(&base_crng.lock, flags);
	memcpy(base_crng.key, key, sizeof(base_crng.key));
	next_gen = base_crng.generation + 1;
	if (next_gen == ULONG_MAX)
		++next_gen;
	WRITE_ONCE(base_crng.generation, next_gen);
#ifdef CONFIG_VDSO_GETRANDOM
	/* base_crng.generation's invalid value is ULONG_MAX, while
	 * vdso_k_rng_data->generation's invalid value is 0, so add one to the
	 * former to arrive at the latter. Use smp_store_release so that this
	 * is ordered with the write above to base_crng.generation. Pairs with
	 * the smp_rmb() before the syscall in the vDSO code.
	 *
	 * Cast to unsigned long for 32-bit architectures, since atomic 64-bit
	 * operations are not supported on those architectures. This is safe
	 * because base_crng.generation is a 32-bit value. On big-endian
	 * architectures it will be stored in the upper 32 bits, but that's okay
	 * because the vDSO side only checks whether the value changed, without
	 * actually using or interpreting the value.
	 */
	smp_store_release((unsigned long *)&vdso_k_rng_data->generation, next_gen + 1);
#endif
	if (!static_branch_likely(&crng_is_ready))
		crng_init = CRNG_READY;
	spin_unlock_irqrestore(&base_crng.lock, flags);
	memzero_explicit(key, sizeof(key));
}

/*
 * This generates a ChaCha block using the provided key, and then
 * immediately overwrites that key with half the block. It returns
 * the resultant ChaCha state to the user, along with the second
 * half of the block containing 32 bytes of random data that may
 * be used; random_data_len may not be greater than 32.
 *
 * The returned ChaCha state contains within it a copy of the old
 * key value, at index 4, so the state should always be zeroed out
 * immediately after using in order to maintain forward secrecy.
 * If the state cannot be erased in a timely manner, then it is
 * safer to set the random_data parameter to &chacha_state[4] so
 * that this function overwrites it before returning.
 */
static void crng_fast_key_erasure(u8 key[CHACHA_KEY_SIZE],
				  u32 chacha_state[CHACHA_STATE_WORDS],
				  u8 *random_data, size_t random_data_len)
{
	u8 first_block[CHACHA_BLOCK_SIZE];

	BUG_ON(random_data_len > 32);

	chacha_init_consts(chacha_state);
	memcpy(&chacha_state[4], key, CHACHA_KEY_SIZE);
	memset(&chacha_state[12], 0, sizeof(u32) * 4);
	chacha20_block(chacha_state, first_block);

	memcpy(key, first_block, CHACHA_KEY_SIZE);
	memcpy(random_data, first_block + CHACHA_KEY_SIZE, random_data_len);
	memzero_explicit(first_block, sizeof(first_block));
}

/*
 * This function returns a ChaCha state that you may use for generating
 * random data. It also returns up to 32 bytes on its own of random data
 * that may be used; random_data_len may not be greater than 32.
 */
static void crng_make_state(u32 chacha_state[CHACHA_STATE_WORDS],
			    u8 *random_data, size_t random_data_len)
{
	unsigned long flags;
	struct crng *crng;

	BUG_ON(random_data_len > 32);

	/*
	 * For the fast path, we check whether we're ready, unlocked first, and
	 * then re-check once locked later. In the case where we're really not
	 * ready, we do fast key erasure with the base_crng directly, extracting
	 * when crng_init is CRNG_EMPTY.
	 */
	if (!crng_ready()) {
		bool ready;

		spin_lock_irqsave(&base_crng.lock, flags);
		ready = crng_ready();
		if (!ready) {
			if (crng_init == CRNG_EMPTY)
				extract_entropy(base_crng.key, sizeof(base_crng.key));
			crng_fast_key_erasure(base_crng.key, chacha_state,
					      random_data, random_data_len);
		}
		spin_unlock_irqrestore(&base_crng.lock, flags);
		if (!ready)
			return;
	}

	local_lock_irqsave(&crngs.lock, flags);
	crng = raw_cpu_ptr(&crngs);

	/*
	 * If our per-cpu crng is older than the base_crng, then it means
	 * somebody reseeded the base_crng. In that case, we do fast key
	 * erasure on the base_crng, and use its output as the new key
	 * for our per-cpu crng. This brings us up to date with base_crng.
	 */
	if (unlikely(crng->generation != READ_ONCE(base_crng.generation))) {
		spin_lock(&base_crng.lock);
		crng_fast_key_erasure(base_crng.key, chacha_state,
				      crng->key, sizeof(crng->key));
		crng->generation = base_crng.generation;
		spin_unlock(&base_crng.lock);
	}

	/*
	 * Finally, when we've made it this far, our per-cpu crng has an up
	 * to date key, and we can do fast key erasure with it to produce
	 * some random data and a ChaCha state for the caller. All other
	 * branches of this function are "unlikely", so most of the time we
	 * should wind up here immediately.
	 */
	crng_fast_key_erasure(crng->key, chacha_state, random_data, random_data_len);
	local_unlock_irqrestore(&crngs.lock, flags);
}

static void _get_random_bytes(void *buf, size_t len)
{
	u32 chacha_state[CHACHA_STATE_WORDS];
	u8 tmp[CHACHA_BLOCK_SIZE];
	size_t first_block_len;

	if (!len)
		return;

	first_block_len = min_t(size_t, 32, len);
	crng_make_state(chacha_state, buf, first_block_len);
	len -= first_block_len;
	buf += first_block_len;

	while (len) {
		if (len < CHACHA_BLOCK_SIZE) {
			chacha20_block(chacha_state, tmp);
			memcpy(buf, tmp, len);
			memzero_explicit(tmp, sizeof(tmp));
			break;
		}

		chacha20_block(chacha_state, buf);
		if (unlikely(chacha_state[12] == 0))
			++chacha_state[13];
		len -= CHACHA_BLOCK_SIZE;
		buf += CHACHA_BLOCK_SIZE;
	}

	memzero_explicit(chacha_state, sizeof(chacha_state));
}

/*
 * This returns random bytes in arbitrary quantities. The quality of the
 * random bytes is good as /dev/urandom. In order to ensure that the
 * randomness provided by this function is okay, the function
 * wait_for_random_bytes() should be called and return 0 at least once
 * at any point prior.
 */
void get_random_bytes(void *buf, size_t len)
{
	warn_unseeded_randomness();
	_get_random_bytes(buf, len);
}
EXPORT_SYMBOL(get_random_bytes);

static ssize_t get_random_bytes_user(struct iov_iter *iter)
{
	u32 chacha_state[CHACHA_STATE_WORDS];
	u8 block[CHACHA_BLOCK_SIZE];
	size_t ret = 0, copied;

	if (unlikely(!iov_iter_count(iter)))
		return 0;

	/*
	 * Immediately overwrite the ChaCha key at index 4 with random
	 * bytes, in case userspace causes copy_to_iter() below to sleep
	 * forever, so that we still retain forward secrecy in that case.
	 */
	crng_make_state(chacha_state, (u8 *)&chacha_state[4], CHACHA_KEY_SIZE);
	/*
	 * However, if we're doing a read of len <= 32, we don't need to
	 * use chacha_state after, so we can simply return those bytes to
	 * the user directly.
	 */
	if (iov_iter_count(iter) <= CHACHA_KEY_SIZE) {
		ret = copy_to_iter(&chacha_state[4], CHACHA_KEY_SIZE, iter);
		goto out_zero_chacha;
	}

	for (;;) {
		chacha20_block(chacha_state, block);
		if (unlikely(chacha_state[12] == 0))
			++chacha_state[13];

		copied = copy_to_iter(block, sizeof(block), iter);
		ret += copied;
		if (!iov_iter_count(iter) || copied != sizeof(block))
			break;

		BUILD_BUG_ON(PAGE_SIZE % sizeof(block) != 0);
		if (ret % PAGE_SIZE == 0) {
			if (signal_pending(current))
				break;
			cond_resched();
		}
	}

	memzero_explicit(block, sizeof(block));
out_zero_chacha:
	memzero_explicit(chacha_state, sizeof(chacha_state));
	return ret ? ret : -EFAULT;
}

/*
 * Batched entropy returns random integers. The quality of the random
 * number is good as /dev/urandom. In order to ensure that the randomness
 * provided by this function is okay, the function wait_for_random_bytes()
 * should be called and return 0 at least once at any point prior.
 */

#define DEFINE_BATCHED_ENTROPY(type)						\
struct batch_ ##type {								\
	/*									\
	 * We make this 1.5x a ChaCha block, so that we get the			\
	 * remaining 32 bytes from fast key erasure, plus one full		\
	 * block from the detached ChaCha state. We can increase		\
	 * the size of this later if needed so long as we keep the		\
	 * formula of (integer_blocks + 0.5) * CHACHA_BLOCK_SIZE.		\
	 */									\
	type entropy[CHACHA_BLOCK_SIZE * 3 / (2 * sizeof(type))];		\
	local_lock_t lock;							\
	unsigned long generation;						\
	unsigned int position;							\
};										\
										\
static DEFINE_PER_CPU(struct batch_ ##type, batched_entropy_ ##type) = {	\
	.lock = INIT_LOCAL_LOCK(batched_entropy_ ##type.lock),			\
	.position = UINT_MAX							\
};										\
										\
type get_random_ ##type(void)							\
{										\
	type ret;								\
	unsigned long flags;							\
	struct batch_ ##type *batch;						\
	unsigned long next_gen;							\
										\
	warn_unseeded_randomness();						\
										\
	if  (!crng_ready()) {							\
		_get_random_bytes(&ret, sizeof(ret));				\
		return ret;							\
	}									\
										\
	local_lock_irqsave(&batched_entropy_ ##type.lock, flags);		\
	batch = raw_cpu_ptr(&batched_entropy_##type);				\
										\
	next_gen = READ_ONCE(base_crng.generation);				\
	if (batch->position >= ARRAY_SIZE(batch->entropy) ||			\
	    next_gen != batch->generation) {					\
		_get_random_bytes(batch->entropy, sizeof(batch->entropy));	\
		batch->position = 0;						\
		batch->generation = next_gen;					\
	}									\
										\
	ret = batch->entropy[batch->position];					\
	batch->entropy[batch->position] = 0;					\
	++batch->position;							\
	local_unlock_irqrestore(&batched_entropy_ ##type.lock, flags);		\
	return ret;								\
}										\
EXPORT_SYMBOL(get_random_ ##type);

DEFINE_BATCHED_ENTROPY(u8)
DEFINE_BATCHED_ENTROPY(u16)
DEFINE_BATCHED_ENTROPY(u32)
DEFINE_BATCHED_ENTROPY(u64)

u32 __get_random_u32_below(u32 ceil)
{
	/*
	 * This is the slow path for variable ceil. It is still fast, most of
	 * the time, by doing traditional reciprocal multiplication and
	 * opportunistically comparing the lower half to ceil itself, before
	 * falling back to computing a larger bound, and then rejecting samples
	 * whose lower half would indicate a range indivisible by ceil. The use
	 * of `-ceil % ceil` is analogous to `2^32 % ceil`, but is computable
	 * in 32-bits.
	 */
	u32 rand = get_random_u32();
	u64 mult;

	/*
	 * This function is technically undefined for ceil == 0, and in fact
	 * for the non-underscored constant version in the header, we build bug
	 * on that. But for the non-constant case, it's convenient to have that
	 * evaluate to being a straight call to get_random_u32(), so that
	 * get_random_u32_inclusive() can work over its whole range without
	 * undefined behavior.
	 */
	if (unlikely(!ceil))
		return rand;

	mult = (u64)ceil * rand;
	if (unlikely((u32)mult < ceil)) {
		u32 bound = -ceil % ceil;
		while (unlikely((u32)mult < bound))
			mult = (u64)ceil * get_random_u32();
	}
	return mult >> 32;
}
EXPORT_SYMBOL(__get_random_u32_below);

#ifdef CONFIG_SMP
/*
 * This function is called when the CPU is coming up, with entry
 * CPUHP_RANDOM_PREPARE, which comes before CPUHP_WORKQUEUE_PREP.
 */
int __cold random_prepare_cpu(unsigned int cpu)
{
	/*
	 * When the cpu comes back online, immediately invalidate both
	 * the per-cpu crng and all batches, so that we serve fresh
	 * randomness.
	 */
	per_cpu_ptr(&crngs, cpu)->generation = ULONG_MAX;
	per_cpu_ptr(&batched_entropy_u8, cpu)->position = UINT_MAX;
	per_cpu_ptr(&batched_entropy_u16, cpu)->position = UINT_MAX;
	per_cpu_ptr(&batched_entropy_u32, cpu)->position = UINT_MAX;
	per_cpu_ptr(&batched_entropy_u64, cpu)->position = UINT_MAX;
	return 0;
}
#endif


/**********************************************************************
 *
 * Entropy accumulation and extraction routines.
 *
 * Callers may add entropy via:
 *
 *     static void mix_pool_bytes(const void *buf, size_t len)
 *
 * After which, if added entropy should be credited:
 *
 *     static void credit_init_bits(size_t bits)
 *
 * Finally, extract entropy via:
 *
 *     static void extract_entropy(void *buf, size_t len)
 *
 **********************************************************************/

enum {
	POOL_BITS = BLAKE2S_HASH_SIZE * 8,
	POOL_READY_BITS = POOL_BITS, /* When crng_init->CRNG_READY */
	POOL_EARLY_BITS = POOL_READY_BITS / 2 /* When crng_init->CRNG_EARLY */
};

static struct {
	struct blake2s_state hash;
	spinlock_t lock;
	unsigned int init_bits;
} input_pool = {
	.hash.h = { BLAKE2S_IV0 ^ (0x01010000 | BLAKE2S_HASH_SIZE),
		    BLAKE2S_IV1, BLAKE2S_IV2, BLAKE2S_IV3, BLAKE2S_IV4,
		    BLAKE2S_IV5, BLAKE2S_IV6, BLAKE2S_IV7 },
	.hash.outlen = BLAKE2S_HASH_SIZE,
	.lock = __SPIN_LOCK_UNLOCKED(input_pool.lock),
};

static void _mix_pool_bytes(const void *buf, size_t len)
{
	blake2s_update(&input_pool.hash, buf, len);
}

/*
 * This function adds bytes into the input pool. It does not
 * update the initialization bit counter; the caller should call
 * credit_init_bits if this is appropriate.
 */
static void mix_pool_bytes(const void *buf, size_t len)
{
	unsigned long flags;

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(buf, len);
	spin_unlock_irqrestore(&input_pool.lock, flags);
}

/*
 * This is an HKDF-like construction for using the hashed collected entropy
 * as a PRF key, that's then expanded block-by-block.
 */
static void extract_entropy(void *buf, size_t len)
{
	unsigned long flags;
	u8 seed[BLAKE2S_HASH_SIZE], next_key[BLAKE2S_HASH_SIZE];
	struct {
		unsigned long rdseed[32 / sizeof(long)];
		size_t counter;
	} block;
	size_t i, longs;

	for (i = 0; i < ARRAY_SIZE(block.rdseed);) {
		longs = arch_get_random_seed_longs(&block.rdseed[i], ARRAY_SIZE(block.rdseed) - i);
		if (longs) {
			i += longs;
			continue;
		}
		longs = arch_get_random_longs(&block.rdseed[i], ARRAY_SIZE(block.rdseed) - i);
		if (longs) {
			i += longs;
			continue;
		}
		block.rdseed[i++] = random_get_entropy();
	}

	spin_lock_irqsave(&input_pool.lock, flags);

	/* seed = HASHPRF(last_key, entropy_input) */
	blake2s_final(&input_pool.hash, seed);

	/* next_key = HASHPRF(seed, RDSEED || 0) */
	block.counter = 0;
	blake2s(next_key, (u8 *)&block, seed, sizeof(next_key), sizeof(block), sizeof(seed));
	blake2s_init_key(&input_pool.hash, BLAKE2S_HASH_SIZE, next_key, sizeof(next_key));

	spin_unlock_irqrestore(&input_pool.lock, flags);
	memzero_explicit(next_key, sizeof(next_key));

	while (len) {
		i = min_t(size_t, len, BLAKE2S_HASH_SIZE);
		/* output = HASHPRF(seed, RDSEED || ++counter) */
		++block.counter;
		blake2s(buf, (u8 *)&block, seed, i, sizeof(block), sizeof(seed));
		len -= i;
		buf += i;
	}

	memzero_explicit(seed, sizeof(seed));
	memzero_explicit(&block, sizeof(block));
}

#define credit_init_bits(bits) if (!crng_ready()) _credit_init_bits(bits)

static void __cold _credit_init_bits(size_t bits)
{
	static DECLARE_WORK(set_ready, crng_set_ready);
	unsigned int new, orig, add;
	unsigned long flags;
	int m;

	if (!bits)
		return;

	add = min_t(size_t, bits, POOL_BITS);

	orig = READ_ONCE(input_pool.init_bits);
	do {
		new = min_t(unsigned int, POOL_BITS, orig + add);
	} while (!try_cmpxchg(&input_pool.init_bits, &orig, new));

	if (orig < POOL_READY_BITS && new >= POOL_READY_BITS) {
		crng_reseed(NULL); /* Sets crng_init to CRNG_READY under base_crng.lock. */
		if (static_key_initialized && system_unbound_wq)
			queue_work(system_unbound_wq, &set_ready);
		atomic_notifier_call_chain(&random_ready_notifier, 0, NULL);
#ifdef CONFIG_VDSO_GETRANDOM
		WRITE_ONCE(vdso_k_rng_data->is_ready, true);
#endif
		wake_up_interruptible(&crng_init_wait);
		kill_fasync(&fasync, SIGIO, POLL_IN);
		pr_notice("crng init done\n");
		m = ratelimit_state_get_miss(&urandom_warning);
		if (m)
			pr_notice("%d urandom warning(s) missed due to ratelimiting\n", m);
	} else if (orig < POOL_EARLY_BITS && new >= POOL_EARLY_BITS) {
		spin_lock_irqsave(&base_crng.lock, flags);
		/* Check if crng_init is CRNG_EMPTY, to avoid race with crng_reseed(). */
		if (crng_init == CRNG_EMPTY) {
			extract_entropy(base_crng.key, sizeof(base_crng.key));
			crng_init = CRNG_EARLY;
		}
		spin_unlock_irqrestore(&base_crng.lock, flags);
	}
}


/**********************************************************************
 *
 * Entropy collection routines.
 *
 * The following exported functions are used for pushing entropy into
 * the above entropy accumulation routines:
 *
 *	void add_device_randomness(const void *buf, size_t len);
 *	void add_hwgenerator_randomness(const void *buf, size_t len, size_t entropy, bool sleep_after);
 *	void add_bootloader_randomness(const void *buf, size_t len);
 *	void add_vmfork_randomness(const void *unique_vm_id, size_t len);
 *	void add_interrupt_randomness(int irq);
 *	void add_input_randomness(unsigned int type, unsigned int code, unsigned int value);
 *	void add_disk_randomness(struct gendisk *disk);
 *
 * add_device_randomness() adds data to the input pool that
 * is likely to differ between two devices (or possibly even per boot).
 * This would be things like MAC addresses or serial numbers, or the
 * read-out of the RTC. This does *not* credit any actual entropy to
 * the pool, but it initializes the pool to different values for devices
 * that might otherwise be identical and have very little entropy
 * available to them (particularly common in the embedded world).
 *
 * add_hwgenerator_randomness() is for true hardware RNGs, and will credit
 * entropy as specified by the caller. If the entropy pool is full it will
 * block until more entropy is needed.
 *
 * add_bootloader_randomness() is called by bootloader drivers, such as EFI
 * and device tree, and credits its input depending on whether or not the
 * command line option 'random.trust_bootloader'.
 *
 * add_vmfork_randomness() adds a unique (but not necessarily secret) ID
 * representing the current instance of a VM to the pool, without crediting,
 * and then force-reseeds the crng so that it takes effect immediately.
 *
 * add_interrupt_randomness() uses the interrupt timing as random
 * inputs to the entropy pool. Using the cycle counters and the irq source
 * as inputs, it feeds the input pool roughly once a second or after 64
 * interrupts, crediting 1 bit of entropy for whichever comes first.
 *
 * add_input_randomness() uses the input layer interrupt timing, as well
 * as the event type information from the hardware.
 *
 * add_disk_randomness() uses what amounts to the seek time of block
 * layer request events, on a per-disk_devt basis, as input to the
 * entropy pool. Note that high-speed solid state drives with very low
 * seek times do not make for good sources of entropy, as their seek
 * times are usually fairly consistent.
 *
 * The last two routines try to estimate how many bits of entropy
 * to credit. They do this by keeping track of the first and second
 * order deltas of the event timings.
 *
 **********************************************************************/

static bool trust_cpu __initdata = true;
static bool trust_bootloader __initdata = true;
static int __init parse_trust_cpu(char *arg)
{
	return kstrtobool(arg, &trust_cpu);
}
static int __init parse_trust_bootloader(char *arg)
{
	return kstrtobool(arg, &trust_bootloader);
}
early_param("random.trust_cpu", parse_trust_cpu);
early_param("random.trust_bootloader", parse_trust_bootloader);

static int random_pm_notification(struct notifier_block *nb, unsigned long action, void *data)
{
	unsigned long flags, entropy = random_get_entropy();

	/*
	 * Encode a representation of how long the system has been suspended,
	 * in a way that is distinct from prior system suspends.
	 */
	ktime_t stamps[] = { ktime_get(), ktime_get_boottime(), ktime_get_real() };

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(&action, sizeof(action));
	_mix_pool_bytes(stamps, sizeof(stamps));
	_mix_pool_bytes(&entropy, sizeof(entropy));
	spin_unlock_irqrestore(&input_pool.lock, flags);

	if (crng_ready() && (action == PM_RESTORE_PREPARE ||
	    (action == PM_POST_SUSPEND && !IS_ENABLED(CONFIG_PM_AUTOSLEEP) &&
	     !IS_ENABLED(CONFIG_PM_USERSPACE_AUTOSLEEP)))) {
		crng_reseed(NULL);
		pr_notice("crng reseeded on system resumption\n");
	}
	return 0;
}

static struct notifier_block pm_notifier = { .notifier_call = random_pm_notification };

/*
 * This is called extremely early, before time keeping functionality is
 * available, but arch randomness is. Interrupts are not yet enabled.
 */
void __init random_init_early(const char *command_line)
{
	unsigned long entropy[BLAKE2S_BLOCK_SIZE / sizeof(long)];
	size_t i, longs, arch_bits;

#if defined(LATENT_ENTROPY_PLUGIN)
	static const u8 compiletime_seed[BLAKE2S_BLOCK_SIZE] __initconst __latent_entropy;
	_mix_pool_bytes(compiletime_seed, sizeof(compiletime_seed));
#endif

	for (i = 0, arch_bits = sizeof(entropy) * 8; i < ARRAY_SIZE(entropy);) {
		longs = arch_get_random_seed_longs(entropy, ARRAY_SIZE(entropy) - i);
		if (longs) {
			_mix_pool_bytes(entropy, sizeof(*entropy) * longs);
			i += longs;
			continue;
		}
		longs = arch_get_random_longs(entropy, ARRAY_SIZE(entropy) - i);
		if (longs) {
			_mix_pool_bytes(entropy, sizeof(*entropy) * longs);
			i += longs;
			continue;
		}
		arch_bits -= sizeof(*entropy) * 8;
		++i;
	}

	_mix_pool_bytes(init_utsname(), sizeof(*(init_utsname())));
	_mix_pool_bytes(command_line, strlen(command_line));

	/* Reseed if already seeded by earlier phases. */
	if (crng_ready())
		crng_reseed(NULL);
	else if (trust_cpu)
		_credit_init_bits(arch_bits);
}

/*
 * This is called a little bit after the prior function, and now there is
 * access to timestamps counters. Interrupts are not yet enabled.
 */
void __init random_init(void)
{
	unsigned long entropy = random_get_entropy();
	ktime_t now = ktime_get_real();

	_mix_pool_bytes(&now, sizeof(now));
	_mix_pool_bytes(&entropy, sizeof(entropy));
	add_latent_entropy();

	/*
	 * If we were initialized by the cpu or bootloader before jump labels
	 * or workqueues are initialized, then we should enable the static
	 * branch here, where it's guaranteed that these have been initialized.
	 */
	if (!static_branch_likely(&crng_is_ready) && crng_init >= CRNG_READY)
		crng_set_ready(NULL);

	/* Reseed if already seeded by earlier phases. */
	if (crng_ready())
		crng_reseed(NULL);

	WARN_ON(register_pm_notifier(&pm_notifier));

	WARN(!entropy, "Missing cycle counter and fallback timer; RNG "
		       "entropy collection will consequently suffer.");
}

/*
 * Add device- or boot-specific data to the input pool to help
 * initialize it.
 *
 * None of this adds any entropy; it is meant to avoid the problem of
 * the entropy pool having similar initial state across largely
 * identical devices.
 */
void add_device_randomness(const void *buf, size_t len)
{
	unsigned long entropy = random_get_entropy();
	unsigned long flags;

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(&entropy, sizeof(entropy));
	_mix_pool_bytes(buf, len);
	spin_unlock_irqrestore(&input_pool.lock, flags);
}
EXPORT_SYMBOL(add_device_randomness);

/*
 * Interface for in-kernel drivers of true hardware RNGs. Those devices
 * may produce endless random bits, so this function will sleep for
 * some amount of time after, if the sleep_after parameter is true.
 */
void add_hwgenerator_randomness(const void *buf, size_t len, size_t entropy, bool sleep_after)
{
	mix_pool_bytes(buf, len);
	credit_init_bits(entropy);

	/*
	 * Throttle writing to once every reseed interval, unless we're not yet
	 * initialized or no entropy is credited.
	 */
	if (sleep_after && !kthread_should_stop() && (crng_ready() || !entropy))
		schedule_timeout_interruptible(crng_reseed_interval());
}
EXPORT_SYMBOL_GPL(add_hwgenerator_randomness);

/*
 * Handle random seed passed by bootloader, and credit it depending
 * on the command line option 'random.trust_bootloader'.
 */
void __init add_bootloader_randomness(const void *buf, size_t len)
{
	mix_pool_bytes(buf, len);
	if (trust_bootloader)
		credit_init_bits(len * 8);
}

#if IS_ENABLED(CONFIG_VMGENID)
static BLOCKING_NOTIFIER_HEAD(vmfork_chain);

/*
 * Handle a new unique VM ID, which is unique, not secret, so we
 * don't credit it, but we do immediately force a reseed after so
 * that it's used by the crng posthaste.
 */
void __cold add_vmfork_randomness(const void *unique_vm_id, size_t len)
{
	add_device_randomness(unique_vm_id, len);
	if (crng_ready()) {
		crng_reseed(NULL);
		pr_notice("crng reseeded due to virtual machine fork\n");
	}
	blocking_notifier_call_chain(&vmfork_chain, 0, NULL);
}
#if IS_MODULE(CONFIG_VMGENID)
EXPORT_SYMBOL_GPL(add_vmfork_randomness);
#endif

int __cold register_random_vmfork_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&vmfork_chain, nb);
}
EXPORT_SYMBOL_GPL(register_random_vmfork_notifier);

int __cold unregister_random_vmfork_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&vmfork_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_random_vmfork_notifier);
#endif

struct fast_pool {
	unsigned long pool[4];
	unsigned long last;
	unsigned int count;
	struct timer_list mix;
};

static void mix_interrupt_randomness(struct timer_list *work);

static DEFINE_PER_CPU(struct fast_pool, irq_randomness) = {
#ifdef CONFIG_64BIT
#define FASTMIX_PERM SIPHASH_PERMUTATION
	.pool = { SIPHASH_CONST_0, SIPHASH_CONST_1, SIPHASH_CONST_2, SIPHASH_CONST_3 },
#else
#define FASTMIX_PERM HSIPHASH_PERMUTATION
	.pool = { HSIPHASH_CONST_0, HSIPHASH_CONST_1, HSIPHASH_CONST_2, HSIPHASH_CONST_3 },
#endif
	.mix = __TIMER_INITIALIZER(mix_interrupt_randomness, 0)
};

/*
 * This is [Half]SipHash-1-x, starting from an empty key. Because
 * the key is fixed, it assumes that its inputs are non-malicious,
 * and therefore this has no security on its own. s represents the
 * four-word SipHash state, while v represents a two-word input.
 */
static void fast_mix(unsigned long s[4], unsigned long v1, unsigned long v2)
{
	s[3] ^= v1;
	FASTMIX_PERM(s[0], s[1], s[2], s[3]);
	s[0] ^= v1;
	s[3] ^= v2;
	FASTMIX_PERM(s[0], s[1], s[2], s[3]);
	s[0] ^= v2;
}

#ifdef CONFIG_SMP
/*
 * This function is called when the CPU has just come online, with
 * entry CPUHP_AP_RANDOM_ONLINE, just after CPUHP_AP_WORKQUEUE_ONLINE.
 */
int __cold random_online_cpu(unsigned int cpu)
{
	/*
	 * During CPU shutdown and before CPU onlining, add_interrupt_
	 * randomness() may schedule mix_interrupt_randomness(), and
	 * set the MIX_INFLIGHT flag. However, because the worker can
	 * be scheduled on a different CPU during this period, that
	 * flag will never be cleared. For that reason, we zero out
	 * the flag here, which runs just after workqueues are onlined
	 * for the CPU again. This also has the effect of setting the
	 * irq randomness count to zero so that new accumulated irqs
	 * are fresh.
	 */
	per_cpu_ptr(&irq_randomness, cpu)->count = 0;
	return 0;
}
#endif

static void mix_interrupt_randomness(struct timer_list *work)
{
	struct fast_pool *fast_pool = container_of(work, struct fast_pool, mix);
	/*
	 * The size of the copied stack pool is explicitly 2 longs so that we
	 * only ever ingest half of the siphash output each time, retaining
	 * the other half as the next "key" that carries over. The entropy is
	 * supposed to be sufficiently dispersed between bits so on average
	 * we don't wind up "losing" some.
	 */
	unsigned long pool[2];
	unsigned int count;

	/* Check to see if we're running on the wrong CPU due to hotplug. */
	local_irq_disable();
	if (fast_pool != this_cpu_ptr(&irq_randomness)) {
		local_irq_enable();
		return;
	}

	/*
	 * Copy the pool to the stack so that the mixer always has a
	 * consistent view, before we reenable irqs again.
	 */
	memcpy(pool, fast_pool->pool, sizeof(pool));
	count = fast_pool->count;
	fast_pool->count = 0;
	fast_pool->last = jiffies;
	local_irq_enable();

	mix_pool_bytes(pool, sizeof(pool));
	credit_init_bits(clamp_t(unsigned int, (count & U16_MAX) / 64, 1, sizeof(pool) * 8));

	memzero_explicit(pool, sizeof(pool));
}

void add_interrupt_randomness(int irq)
{
	enum { MIX_INFLIGHT = 1U << 31 };
	unsigned long entropy = random_get_entropy();
	struct fast_pool *fast_pool = this_cpu_ptr(&irq_randomness);
	struct pt_regs *regs = get_irq_regs();
	unsigned int new_count;

	fast_mix(fast_pool->pool, entropy,
		 (regs ? instruction_pointer(regs) : _RET_IP_) ^ swab(irq));
	new_count = ++fast_pool->count;

	if (new_count & MIX_INFLIGHT)
		return;

	if (new_count < 1024 && !time_is_before_jiffies(fast_pool->last + HZ))
		return;

	fast_pool->count |= MIX_INFLIGHT;
	if (!timer_pending(&fast_pool->mix)) {
		fast_pool->mix.expires = jiffies;
		add_timer_on(&fast_pool->mix, raw_smp_processor_id());
	}
}
EXPORT_SYMBOL_GPL(add_interrupt_randomness);

/* There is one of these per entropy source */
struct timer_rand_state {
	unsigned long last_time;
	long last_delta, last_delta2;
};

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays. It uses the timer_rand_state structure to make an estimate
 * of how many bits of entropy this call has added to the pool. The
 * value "num" is also added to the pool; it should somehow describe
 * the type of event that just happened.
 */
static void add_timer_randomness(struct timer_rand_state *state, unsigned int num)
{
	unsigned long entropy = random_get_entropy(), now = jiffies, flags;
	long delta, delta2, delta3;
	unsigned int bits;

	/*
	 * If we're in a hard IRQ, add_interrupt_randomness() will be called
	 * sometime after, so mix into the fast pool.
	 */
	if (in_hardirq()) {
		fast_mix(this_cpu_ptr(&irq_randomness)->pool, entropy, num);
	} else {
		spin_lock_irqsave(&input_pool.lock, flags);
		_mix_pool_bytes(&entropy, sizeof(entropy));
		_mix_pool_bytes(&num, sizeof(num));
		spin_unlock_irqrestore(&input_pool.lock, flags);
	}

	if (crng_ready())
		return;

	/*
	 * Calculate number of bits of randomness we probably added.
	 * We take into account the first, second and third-order deltas
	 * in order to make our estimate.
	 */
	delta = now - READ_ONCE(state->last_time);
	WRITE_ONCE(state->last_time, now);

	delta2 = delta - READ_ONCE(state->last_delta);
	WRITE_ONCE(state->last_delta, delta);

	delta3 = delta2 - READ_ONCE(state->last_delta2);
	WRITE_ONCE(state->last_delta2, delta2);

	if (delta < 0)
		delta = -delta;
	if (delta2 < 0)
		delta2 = -delta2;
	if (delta3 < 0)
		delta3 = -delta3;
	if (delta > delta2)
		delta = delta2;
	if (delta > delta3)
		delta = delta3;

	/*
	 * delta is now minimum absolute delta. Round down by 1 bit
	 * on general principles, and limit entropy estimate to 11 bits.
	 */
	bits = min(fls(delta >> 1), 11);

	/*
	 * As mentioned above, if we're in a hard IRQ, add_interrupt_randomness()
	 * will run after this, which uses a different crediting scheme of 1 bit
	 * per every 64 interrupts. In order to let that function do accounting
	 * close to the one in this function, we credit a full 64/64 bit per bit,
	 * and then subtract one to account for the extra one added.
	 */
	if (in_hardirq())
		this_cpu_ptr(&irq_randomness)->count += max(1u, bits * 64) - 1;
	else
		_credit_init_bits(bits);
}

void add_input_randomness(unsigned int type, unsigned int code, unsigned int value)
{
	static unsigned char last_value;
	static struct timer_rand_state input_timer_state = { INITIAL_JIFFIES };

	/* Ignore autorepeat and the like. */
	if (value == last_value)
		return;

	last_value = value;
	add_timer_randomness(&input_timer_state,
			     (type << 4) ^ code ^ (code >> 4) ^ value);
}
EXPORT_SYMBOL_GPL(add_input_randomness);

#ifdef CONFIG_BLOCK
void add_disk_randomness(struct gendisk *disk)
{
	if (!disk || !disk->random)
		return;
	/* First major is 1, so we get >= 0x200 here. */
	add_timer_randomness(disk->random, 0x100 + disk_devt(disk));
}
EXPORT_SYMBOL_GPL(add_disk_randomness);

void __cold rand_initialize_disk(struct gendisk *disk)
{
	struct timer_rand_state *state;

	/*
	 * If kzalloc returns null, we just won't use that entropy
	 * source.
	 */
	state = kzalloc(sizeof(struct timer_rand_state), GFP_KERNEL);
	if (state) {
		state->last_time = INITIAL_JIFFIES;
		disk->random = state;
	}
}
#endif

struct entropy_timer_state {
	unsigned long entropy;
	struct timer_list timer;
	atomic_t samples;
	unsigned int samples_per_bit;
};

/*
 * Each time the timer fires, we expect that we got an unpredictable jump in
 * the cycle counter. Even if the timer is running on another CPU, the timer
 * activity will be touching the stack of the CPU that is generating entropy.
 *
 * Note that we don't re-arm the timer in the timer itself - we are happy to be
 * scheduled away, since that just makes the load more complex, but we do not
 * want the timer to keep ticking unless the entropy loop is running.
 *
 * So the re-arming always happens in the entropy loop itself.
 */
static void __cold entropy_timer(struct timer_list *timer)
{
	struct entropy_timer_state *state = container_of(timer, struct entropy_timer_state, timer);
	unsigned long entropy = random_get_entropy();

	mix_pool_bytes(&entropy, sizeof(entropy));
	if (atomic_inc_return(&state->samples) % state->samples_per_bit == 0)
		credit_init_bits(1);
}

/*
 * If we have an actual cycle counter, see if we can generate enough entropy
 * with timing noise.
 */
static void __cold try_to_generate_entropy(void)
{
	enum { NUM_TRIAL_SAMPLES = 8192, MAX_SAMPLES_PER_BIT = HZ / 15 };
	u8 stack_bytes[sizeof(struct entropy_timer_state) + SMP_CACHE_BYTES - 1];
	struct entropy_timer_state *stack = PTR_ALIGN((void *)stack_bytes, SMP_CACHE_BYTES);
	unsigned int i, num_different = 0;
	unsigned long last = random_get_entropy();
	int cpu = -1;

	for (i = 0; i < NUM_TRIAL_SAMPLES - 1; ++i) {
		stack->entropy = random_get_entropy();
		if (stack->entropy != last)
			++num_different;
		last = stack->entropy;
	}
	stack->samples_per_bit = DIV_ROUND_UP(NUM_TRIAL_SAMPLES, num_different + 1);
	if (stack->samples_per_bit > MAX_SAMPLES_PER_BIT)
		return;

	atomic_set(&stack->samples, 0);
	timer_setup_on_stack(&stack->timer, entropy_timer, 0);
	while (!crng_ready() && !signal_pending(current)) {
		/*
		 * Check !timer_pending() and then ensure that any previous callback has finished
		 * executing by checking try_to_del_timer_sync(), before queueing the next one.
		 */
		if (!timer_pending(&stack->timer) && try_to_del_timer_sync(&stack->timer) >= 0) {
			struct cpumask timer_cpus;
			unsigned int num_cpus;

			/*
			 * Preemption must be disabled here, both to read the current CPU number
			 * and to avoid scheduling a timer on a dead CPU.
			 */
			preempt_disable();

			/* Only schedule callbacks on timer CPUs that are online. */
			cpumask_and(&timer_cpus, housekeeping_cpumask(HK_TYPE_TIMER), cpu_online_mask);
			num_cpus = cpumask_weight(&timer_cpus);
			/* In very bizarre case of misconfiguration, fallback to all online. */
			if (unlikely(num_cpus == 0)) {
				timer_cpus = *cpu_online_mask;
				num_cpus = cpumask_weight(&timer_cpus);
			}

			/* Basic CPU round-robin, which avoids the current CPU. */
			do {
				cpu = cpumask_next(cpu, &timer_cpus);
				if (cpu >= nr_cpu_ids)
					cpu = cpumask_first(&timer_cpus);
			} while (cpu == smp_processor_id() && num_cpus > 1);

			/* Expiring the timer at `jiffies` means it's the next tick. */
			stack->timer.expires = jiffies;

			add_timer_on(&stack->timer, cpu);

			preempt_enable();
		}
		mix_pool_bytes(&stack->entropy, sizeof(stack->entropy));
		schedule();
		stack->entropy = random_get_entropy();
	}
	mix_pool_bytes(&stack->entropy, sizeof(stack->entropy));

	timer_delete_sync(&stack->timer);
	destroy_timer_on_stack(&stack->timer);
}


/**********************************************************************
 *
 * Userspace reader/writer interfaces.
 *
 * getrandom(2) is the primary modern interface into the RNG and should
 * be used in preference to anything else.
 *
 * Reading from /dev/random has the same functionality as calling
 * getrandom(2) with flags=0. In earlier versions, however, it had
 * vastly different semantics and should therefore be avoided, to
 * prevent backwards compatibility issues.
 *
 * Reading from /dev/urandom has the same functionality as calling
 * getrandom(2) with flags=GRND_INSECURE. Because it does not block
 * waiting for the RNG to be ready, it should not be used.
 *
 * Writing to either /dev/random or /dev/urandom adds entropy to
 * the input pool but does not credit it.
 *
 * Polling on /dev/random indicates when the RNG is initialized, on
 * the read side, and when it wants new entropy, on the write side.
 *
 * Both /dev/random and /dev/urandom have the same set of ioctls for
 * adding entropy, getting the entropy count, zeroing the count, and
 * reseeding the crng.
 *
 **********************************************************************/

SYSCALL_DEFINE3(getrandom, char __user *, ubuf, size_t, len, unsigned int, flags)
{
	struct iov_iter iter;
	int ret;

	if (flags & ~(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE))
		return -EINVAL;

	/*
	 * Requesting insecure and blocking randomness at the same time makes
	 * no sense.
	 */
	if ((flags & (GRND_INSECURE | GRND_RANDOM)) == (GRND_INSECURE | GRND_RANDOM))
		return -EINVAL;

	if (!crng_ready() && !(flags & GRND_INSECURE)) {
		if (flags & GRND_NONBLOCK)
			return -EAGAIN;
		ret = wait_for_random_bytes();
		if (unlikely(ret))
			return ret;
	}

	ret = import_ubuf(ITER_DEST, ubuf, len, &iter);
	if (unlikely(ret))
		return ret;
	return get_random_bytes_user(&iter);
}

static __poll_t random_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &crng_init_wait, wait);
	return crng_ready() ? EPOLLIN | EPOLLRDNORM : EPOLLOUT | EPOLLWRNORM;
}

static ssize_t write_pool_user(struct iov_iter *iter)
{
	u8 block[BLAKE2S_BLOCK_SIZE];
	ssize_t ret = 0;
	size_t copied;

	if (unlikely(!iov_iter_count(iter)))
		return 0;

	for (;;) {
		copied = copy_from_iter(block, sizeof(block), iter);
		ret += copied;
		mix_pool_bytes(block, copied);
		if (!iov_iter_count(iter) || copied != sizeof(block))
			break;

		BUILD_BUG_ON(PAGE_SIZE % sizeof(block) != 0);
		if (ret % PAGE_SIZE == 0) {
			if (signal_pending(current))
				break;
			cond_resched();
		}
	}

	memzero_explicit(block, sizeof(block));
	return ret ? ret : -EFAULT;
}

static ssize_t random_write_iter(struct kiocb *kiocb, struct iov_iter *iter)
{
	return write_pool_user(iter);
}

static ssize_t urandom_read_iter(struct kiocb *kiocb, struct iov_iter *iter)
{
	static int maxwarn = 10;

	/*
	 * Opportunistically attempt to initialize the RNG on platforms that
	 * have fast cycle counters, but don't (for now) require it to succeed.
	 */
	if (!crng_ready())
		try_to_generate_entropy();

	if (!crng_ready()) {
		if (!ratelimit_disable && maxwarn <= 0)
			ratelimit_state_inc_miss(&urandom_warning);
		else if (ratelimit_disable || __ratelimit(&urandom_warning)) {
			--maxwarn;
			pr_notice("%s: uninitialized urandom read (%zu bytes read)\n",
				  current->comm, iov_iter_count(iter));
		}
	}

	return get_random_bytes_user(iter);
}

static ssize_t random_read_iter(struct kiocb *kiocb, struct iov_iter *iter)
{
	int ret;

	if (!crng_ready() &&
	    ((kiocb->ki_flags & (IOCB_NOWAIT | IOCB_NOIO)) ||
	     (kiocb->ki_filp->f_flags & O_NONBLOCK)))
		return -EAGAIN;

	ret = wait_for_random_bytes();
	if (ret != 0)
		return ret;
	return get_random_bytes_user(iter);
}

static long random_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int __user *p = (int __user *)arg;
	int ent_count;

	switch (cmd) {
	case RNDGETENTCNT:
		/* Inherently racy, no point locking. */
		if (put_user(input_pool.init_bits, p))
			return -EFAULT;
		return 0;
	case RNDADDTOENTCNT:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (get_user(ent_count, p))
			return -EFAULT;
		if (ent_count < 0)
			return -EINVAL;
		credit_init_bits(ent_count);
		return 0;
	case RNDADDENTROPY: {
		struct iov_iter iter;
		ssize_t ret;
		int len;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (get_user(ent_count, p++))
			return -EFAULT;
		if (ent_count < 0)
			return -EINVAL;
		if (get_user(len, p++))
			return -EFAULT;
		ret = import_ubuf(ITER_SOURCE, p, len, &iter);
		if (unlikely(ret))
			return ret;
		ret = write_pool_user(&iter);
		if (unlikely(ret < 0))
			return ret;
		/* Since we're crediting, enforce that it was all written into the pool. */
		if (unlikely(ret != len))
			return -EFAULT;
		credit_init_bits(ent_count);
		return 0;
	}
	case RNDZAPENTCNT:
	case RNDCLEARPOOL:
		/* No longer has any effect. */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		return 0;
	case RNDRESEEDCRNG:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (!crng_ready())
			return -ENODATA;
		crng_reseed(NULL);
		return 0;
	default:
		return -EINVAL;
	}
}

static int random_fasync(int fd, struct file *filp, int on)
{
	return fasync_helper(fd, filp, on, &fasync);
}

const struct file_operations random_fops = {
	.read_iter = random_read_iter,
	.write_iter = random_write_iter,
	.poll = random_poll,
	.unlocked_ioctl = random_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.fasync = random_fasync,
	.llseek = noop_llseek,
	.splice_read = copy_splice_read,
	.splice_write = iter_file_splice_write,
};

const struct file_operations urandom_fops = {
	.read_iter = urandom_read_iter,
	.write_iter = random_write_iter,
	.unlocked_ioctl = random_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.fasync = random_fasync,
	.llseek = noop_llseek,
	.splice_read = copy_splice_read,
	.splice_write = iter_file_splice_write,
};


/********************************************************************
 *
 * Sysctl interface.
 *
 * These are partly unused legacy knobs with dummy values to not break
 * userspace and partly still useful things. They are usually accessible
 * in /proc/sys/kernel/random/ and are as follows:
 *
 * - boot_id - a UUID representing the current boot.
 *
 * - uuid - a random UUID, different each time the file is read.
 *
 * - poolsize - the number of bits of entropy that the input pool can
 *   hold, tied to the POOL_BITS constant.
 *
 * - entropy_avail - the number of bits of entropy currently in the
 *   input pool. Always <= poolsize.
 *
 * - write_wakeup_threshold - the amount of entropy in the input pool
 *   below which write polls to /dev/random will unblock, requesting
 *   more entropy, tied to the POOL_READY_BITS constant. It is writable
 *   to avoid breaking old userspaces, but writing to it does not
 *   change any behavior of the RNG.
 *
 * - urandom_min_reseed_secs - fixed to the value CRNG_RESEED_INTERVAL.
 *   It is writable to avoid breaking old userspaces, but writing
 *   to it does not change any behavior of the RNG.
 *
 ********************************************************************/

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static int sysctl_random_min_urandom_seed = CRNG_RESEED_INTERVAL / HZ;
static int sysctl_random_write_wakeup_bits = POOL_READY_BITS;
static int sysctl_poolsize = POOL_BITS;
static u8 sysctl_bootid[UUID_SIZE];

/*
 * This function is used to return both the bootid UUID, and random
 * UUID. The difference is in whether table->data is NULL; if it is,
 * then a new UUID is generated and returned to the user.
 */
static int proc_do_uuid(const struct ctl_table *table, int write, void *buf,
			size_t *lenp, loff_t *ppos)
{
	u8 tmp_uuid[UUID_SIZE], *uuid;
	char uuid_string[UUID_STRING_LEN + 1];
	struct ctl_table fake_table = {
		.data = uuid_string,
		.maxlen = UUID_STRING_LEN
	};

	if (write)
		return -EPERM;

	uuid = table->data;
	if (!uuid) {
		uuid = tmp_uuid;
		generate_random_uuid(uuid);
	} else {
		static DEFINE_SPINLOCK(bootid_spinlock);

		spin_lock(&bootid_spinlock);
		if (!uuid[8])
			generate_random_uuid(uuid);
		spin_unlock(&bootid_spinlock);
	}

	snprintf(uuid_string, sizeof(uuid_string), "%pU", uuid);
	return proc_dostring(&fake_table, 0, buf, lenp, ppos);
}

/* The same as proc_dointvec, but writes don't change anything. */
static int proc_do_rointvec(const struct ctl_table *table, int write, void *buf,
			    size_t *lenp, loff_t *ppos)
{
	return write ? 0 : proc_dointvec(table, 0, buf, lenp, ppos);
}

static const struct ctl_table random_table[] = {
	{
		.procname	= "poolsize",
		.data		= &sysctl_poolsize,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "entropy_avail",
		.data		= &input_pool.init_bits,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "write_wakeup_threshold",
		.data		= &sysctl_random_write_wakeup_bits,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_rointvec,
	},
	{
		.procname	= "urandom_min_reseed_secs",
		.data		= &sysctl_random_min_urandom_seed,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_rointvec,
	},
	{
		.procname	= "boot_id",
		.data		= &sysctl_bootid,
		.mode		= 0444,
		.proc_handler	= proc_do_uuid,
	},
	{
		.procname	= "uuid",
		.mode		= 0444,
		.proc_handler	= proc_do_uuid,
	},
};

/*
 * random_init() is called before sysctl_init(),
 * so we cannot call register_sysctl_init() in random_init()
 */
static int __init random_sysctls_init(void)
{
	register_sysctl_init("kernel/random", random_table);
	return 0;
}
device_initcall(random_sysctls_init);
#endif
