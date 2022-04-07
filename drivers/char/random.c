// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright (C) 2017-2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
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
 * various pieces of data are hashed. Some of that data is then "credited" as
 * having a certain number of bits of entropy. When enough bits of entropy are
 * available, the hash is finalized and handed as a key to a stream cipher that
 * expands it indefinitely for various consumers. This key is periodically
 * refreshed as the various entropy collectors, described below, add data to the
 * input pool and credit it. There is currently no Fortuna-like scheduler
 * involved, which can lead to malicious entropy sources causing a premature
 * reseed, and the entropy estimates are, at best, conservative guesses.
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
#include <linux/genhd.h>
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
#include <crypto/chacha.h>
#include <crypto/blake2s.h>
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
 * crng_init =  0 --> Uninitialized
 *		1 --> Initialized
 *		2 --> Initialized from input_pool
 *
 * crng_init is protected by base_crng->lock, and only increases
 * its value (from 0->1->2).
 */
static int crng_init = 0;
#define crng_ready() (likely(crng_init > 1))
/* Various types of waiters for crng_init->2 transition. */
static DECLARE_WAIT_QUEUE_HEAD(crng_init_wait);
static struct fasync_struct *fasync;
static DEFINE_SPINLOCK(random_ready_chain_lock);
static RAW_NOTIFIER_HEAD(random_ready_chain);

/* Control how we warn userspace. */
static struct ratelimit_state unseeded_warning =
	RATELIMIT_STATE_INIT("warn_unseeded_randomness", HZ, 3);
static struct ratelimit_state urandom_warning =
	RATELIMIT_STATE_INIT("warn_urandom_randomness", HZ, 3);
static int ratelimit_disable __read_mostly;
module_param_named(ratelimit_disable, ratelimit_disable, int, 0644);
MODULE_PARM_DESC(ratelimit_disable, "Disable random ratelimit suppression");

/*
 * Returns whether or not the input pool has been seeded and thus guaranteed
 * to supply cryptographically secure random numbers. This applies to: the
 * /dev/urandom device, the get_random_bytes function, and the get_random_{u32,
 * ,u64,int,long} family of functions.
 *
 * Returns: true if the input pool has been seeded.
 *          false if the input pool has not been seeded.
 */
bool rng_is_initialized(void)
{
	return crng_ready();
}
EXPORT_SYMBOL(rng_is_initialized);

/* Used by wait_for_random_bytes(), and considered an entropy collector, below. */
static void try_to_generate_entropy(void);

/*
 * Wait for the input pool to be seeded and thus guaranteed to supply
 * cryptographically secure random numbers. This applies to: the /dev/urandom
 * device, the get_random_bytes function, and the get_random_{u32,u64,int,long}
 * family of functions. Using any of these functions without first calling
 * this function forfeits the guarantee of security.
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
 * Add a callback function that will be invoked when the input
 * pool is initialised.
 *
 * returns: 0 if callback is successfully added
 *	    -EALREADY if pool is already initialised (callback not called)
 */
int register_random_ready_notifier(struct notifier_block *nb)
{
	unsigned long flags;
	int ret = -EALREADY;

	if (crng_ready())
		return ret;

	spin_lock_irqsave(&random_ready_chain_lock, flags);
	if (!crng_ready())
		ret = raw_notifier_chain_register(&random_ready_chain, nb);
	spin_unlock_irqrestore(&random_ready_chain_lock, flags);
	return ret;
}
EXPORT_SYMBOL(register_random_ready_notifier);

/*
 * Delete a previously registered readiness callback function.
 */
int unregister_random_ready_notifier(struct notifier_block *nb)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&random_ready_chain_lock, flags);
	ret = raw_notifier_chain_unregister(&random_ready_chain, nb);
	spin_unlock_irqrestore(&random_ready_chain_lock, flags);
	return ret;
}
EXPORT_SYMBOL(unregister_random_ready_notifier);

static void process_random_ready_list(void)
{
	unsigned long flags;

	spin_lock_irqsave(&random_ready_chain_lock, flags);
	raw_notifier_call_chain(&random_ready_chain, 0, NULL);
	spin_unlock_irqrestore(&random_ready_chain_lock, flags);
}

#define warn_unseeded_randomness(previous) \
	_warn_unseeded_randomness(__func__, (void *)_RET_IP_, (previous))

static void _warn_unseeded_randomness(const char *func_name, void *caller, void **previous)
{
#ifdef CONFIG_WARN_ALL_UNSEEDED_RANDOM
	const bool print_once = false;
#else
	static bool print_once __read_mostly;
#endif

	if (print_once || crng_ready() ||
	    (previous && (caller == READ_ONCE(*previous))))
		return;
	WRITE_ONCE(*previous, caller);
#ifndef CONFIG_WARN_ALL_UNSEEDED_RANDOM
	print_once = true;
#endif
	if (__ratelimit(&unseeded_warning))
		printk_deferred(KERN_NOTICE "random: %s called from %pS with crng_init=%d\n",
				func_name, caller, crng_init);
}


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
 *	void get_random_bytes(void *buf, size_t nbytes)
 *	u32 get_random_u32()
 *	u64 get_random_u64()
 *	unsigned int get_random_int()
 *	unsigned long get_random_long()
 *
 * These interfaces will return the requested number of random bytes
 * into the given buffer or as a return value. This is equivalent to
 * a read from /dev/urandom. The u32, u64, int, and long family of
 * functions may be higher performance for one-off random integers,
 * because they do a bit of buffering and do not invoke reseeding
 * until the buffer is emptied.
 *
 *********************************************************************/

enum {
	CRNG_RESEED_INTERVAL = 300 * HZ,
	CRNG_INIT_CNT_THRESH = 2 * CHACHA_KEY_SIZE
};

static struct {
	u8 key[CHACHA_KEY_SIZE] __aligned(__alignof__(long));
	unsigned long birth;
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

/* Used by crng_reseed() to extract a new seed from the input pool. */
static bool drain_entropy(void *buf, size_t nbytes);

/*
 * This extracts a new crng key from the input pool, but only if there is a
 * sufficient amount of entropy available, in order to mitigate bruteforcing
 * of newly added bits.
 */
static void crng_reseed(void)
{
	unsigned long flags;
	unsigned long next_gen;
	u8 key[CHACHA_KEY_SIZE];
	bool finalize_init = false;

	/* Only reseed if we can, to prevent brute forcing a small amount of new bits. */
	if (!drain_entropy(key, sizeof(key)))
		return;

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
	WRITE_ONCE(base_crng.birth, jiffies);
	if (!crng_ready()) {
		crng_init = 2;
		finalize_init = true;
	}
	spin_unlock_irqrestore(&base_crng.lock, flags);
	memzero_explicit(key, sizeof(key));
	if (finalize_init) {
		process_random_ready_list();
		wake_up_interruptible(&crng_init_wait);
		kill_fasync(&fasync, SIGIO, POLL_IN);
		pr_notice("crng init done\n");
		if (unseeded_warning.missed) {
			pr_notice("%d get_random_xx warning(s) missed due to ratelimiting\n",
				  unseeded_warning.missed);
			unseeded_warning.missed = 0;
		}
		if (urandom_warning.missed) {
			pr_notice("%d urandom warning(s) missed due to ratelimiting\n",
				  urandom_warning.missed);
			urandom_warning.missed = 0;
		}
	}
}

/*
 * This generates a ChaCha block using the provided key, and then
 * immediately overwites that key with half the block. It returns
 * the resultant ChaCha state to the user, along with the second
 * half of the block containing 32 bytes of random data that may
 * be used; random_data_len may not be greater than 32.
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
 * Return whether the crng seed is considered to be sufficiently
 * old that a reseeding might be attempted. This happens if the last
 * reseeding was CRNG_RESEED_INTERVAL ago, or during early boot, at
 * an interval proportional to the uptime.
 */
static bool crng_has_old_seed(void)
{
	static bool early_boot = true;
	unsigned long interval = CRNG_RESEED_INTERVAL;

	if (unlikely(READ_ONCE(early_boot))) {
		time64_t uptime = ktime_get_seconds();
		if (uptime >= CRNG_RESEED_INTERVAL / HZ * 2)
			WRITE_ONCE(early_boot, false);
		else
			interval = max_t(unsigned int, 5 * HZ,
					 (unsigned int)uptime / 2 * HZ);
	}
	return time_after(jiffies, READ_ONCE(base_crng.birth) + interval);
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
	 * ready, we do fast key erasure with the base_crng directly, because
	 * this is what crng_pre_init_inject() mutates during early init.
	 */
	if (!crng_ready()) {
		bool ready;

		spin_lock_irqsave(&base_crng.lock, flags);
		ready = crng_ready();
		if (!ready)
			crng_fast_key_erasure(base_crng.key, chacha_state,
					      random_data, random_data_len);
		spin_unlock_irqrestore(&base_crng.lock, flags);
		if (!ready)
			return;
	}

	/*
	 * If the base_crng is old enough, we try to reseed, which in turn
	 * bumps the generation counter that we check below.
	 */
	if (unlikely(crng_has_old_seed()))
		crng_reseed();

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

/*
 * This function is for crng_init == 0 only. It loads entropy directly
 * into the crng's key, without going through the input pool. It is,
 * generally speaking, not very safe, but we use this only at early
 * boot time when it's better to have something there rather than
 * nothing.
 *
 * If account is set, then the crng_init_cnt counter is incremented.
 * This shouldn't be set by functions like add_device_randomness(),
 * where we can't trust the buffer passed to it is guaranteed to be
 * unpredictable (so it might not have any entropy at all).
 */
static void crng_pre_init_inject(const void *input, size_t len, bool account)
{
	static int crng_init_cnt = 0;
	struct blake2s_state hash;
	unsigned long flags;

	blake2s_init(&hash, sizeof(base_crng.key));

	spin_lock_irqsave(&base_crng.lock, flags);
	if (crng_init != 0) {
		spin_unlock_irqrestore(&base_crng.lock, flags);
		return;
	}

	blake2s_update(&hash, base_crng.key, sizeof(base_crng.key));
	blake2s_update(&hash, input, len);
	blake2s_final(&hash, base_crng.key);

	if (account) {
		crng_init_cnt += min_t(size_t, len, CRNG_INIT_CNT_THRESH - crng_init_cnt);
		if (crng_init_cnt >= CRNG_INIT_CNT_THRESH) {
			++base_crng.generation;
			crng_init = 1;
		}
	}

	spin_unlock_irqrestore(&base_crng.lock, flags);

	if (crng_init == 1)
		pr_notice("fast init done\n");
}

static void _get_random_bytes(void *buf, size_t nbytes)
{
	u32 chacha_state[CHACHA_STATE_WORDS];
	u8 tmp[CHACHA_BLOCK_SIZE];
	size_t len;

	if (!nbytes)
		return;

	len = min_t(size_t, 32, nbytes);
	crng_make_state(chacha_state, buf, len);
	nbytes -= len;
	buf += len;

	while (nbytes) {
		if (nbytes < CHACHA_BLOCK_SIZE) {
			chacha20_block(chacha_state, tmp);
			memcpy(buf, tmp, nbytes);
			memzero_explicit(tmp, sizeof(tmp));
			break;
		}

		chacha20_block(chacha_state, buf);
		if (unlikely(chacha_state[12] == 0))
			++chacha_state[13];
		nbytes -= CHACHA_BLOCK_SIZE;
		buf += CHACHA_BLOCK_SIZE;
	}

	memzero_explicit(chacha_state, sizeof(chacha_state));
}

/*
 * This function is the exported kernel interface.  It returns some
 * number of good random numbers, suitable for key generation, seeding
 * TCP sequence numbers, etc.  It does not rely on the hardware random
 * number generator.  For random bytes direct from the hardware RNG
 * (when available), use get_random_bytes_arch(). In order to ensure
 * that the randomness provided by this function is okay, the function
 * wait_for_random_bytes() should be called and return 0 at least once
 * at any point prior.
 */
void get_random_bytes(void *buf, size_t nbytes)
{
	static void *previous;

	warn_unseeded_randomness(&previous);
	_get_random_bytes(buf, nbytes);
}
EXPORT_SYMBOL(get_random_bytes);

static ssize_t get_random_bytes_user(void __user *buf, size_t nbytes)
{
	size_t len, left, ret = 0;
	u32 chacha_state[CHACHA_STATE_WORDS];
	u8 output[CHACHA_BLOCK_SIZE];

	if (!nbytes)
		return 0;

	/*
	 * Immediately overwrite the ChaCha key at index 4 with random
	 * bytes, in case userspace causes copy_to_user() below to sleep
	 * forever, so that we still retain forward secrecy in that case.
	 */
	crng_make_state(chacha_state, (u8 *)&chacha_state[4], CHACHA_KEY_SIZE);
	/*
	 * However, if we're doing a read of len <= 32, we don't need to
	 * use chacha_state after, so we can simply return those bytes to
	 * the user directly.
	 */
	if (nbytes <= CHACHA_KEY_SIZE) {
		ret = nbytes - copy_to_user(buf, &chacha_state[4], nbytes);
		goto out_zero_chacha;
	}

	for (;;) {
		chacha20_block(chacha_state, output);
		if (unlikely(chacha_state[12] == 0))
			++chacha_state[13];

		len = min_t(size_t, nbytes, CHACHA_BLOCK_SIZE);
		left = copy_to_user(buf, output, len);
		if (left) {
			ret += len - left;
			break;
		}

		buf += len;
		ret += len;
		nbytes -= len;
		if (!nbytes)
			break;

		BUILD_BUG_ON(PAGE_SIZE % CHACHA_BLOCK_SIZE != 0);
		if (ret % PAGE_SIZE == 0) {
			if (signal_pending(current))
				break;
			cond_resched();
		}
	}

	memzero_explicit(output, sizeof(output));
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
struct batched_entropy {
	union {
		/*
		 * We make this 1.5x a ChaCha block, so that we get the
		 * remaining 32 bytes from fast key erasure, plus one full
		 * block from the detached ChaCha state. We can increase
		 * the size of this later if needed so long as we keep the
		 * formula of (integer_blocks + 0.5) * CHACHA_BLOCK_SIZE.
		 */
		u64 entropy_u64[CHACHA_BLOCK_SIZE * 3 / (2 * sizeof(u64))];
		u32 entropy_u32[CHACHA_BLOCK_SIZE * 3 / (2 * sizeof(u32))];
	};
	local_lock_t lock;
	unsigned long generation;
	unsigned int position;
};


static DEFINE_PER_CPU(struct batched_entropy, batched_entropy_u64) = {
	.lock = INIT_LOCAL_LOCK(batched_entropy_u64.lock),
	.position = UINT_MAX
};

u64 get_random_u64(void)
{
	u64 ret;
	unsigned long flags;
	struct batched_entropy *batch;
	static void *previous;
	unsigned long next_gen;

	warn_unseeded_randomness(&previous);

	local_lock_irqsave(&batched_entropy_u64.lock, flags);
	batch = raw_cpu_ptr(&batched_entropy_u64);

	next_gen = READ_ONCE(base_crng.generation);
	if (batch->position >= ARRAY_SIZE(batch->entropy_u64) ||
	    next_gen != batch->generation) {
		_get_random_bytes(batch->entropy_u64, sizeof(batch->entropy_u64));
		batch->position = 0;
		batch->generation = next_gen;
	}

	ret = batch->entropy_u64[batch->position];
	batch->entropy_u64[batch->position] = 0;
	++batch->position;
	local_unlock_irqrestore(&batched_entropy_u64.lock, flags);
	return ret;
}
EXPORT_SYMBOL(get_random_u64);

static DEFINE_PER_CPU(struct batched_entropy, batched_entropy_u32) = {
	.lock = INIT_LOCAL_LOCK(batched_entropy_u32.lock),
	.position = UINT_MAX
};

u32 get_random_u32(void)
{
	u32 ret;
	unsigned long flags;
	struct batched_entropy *batch;
	static void *previous;
	unsigned long next_gen;

	warn_unseeded_randomness(&previous);

	local_lock_irqsave(&batched_entropy_u32.lock, flags);
	batch = raw_cpu_ptr(&batched_entropy_u32);

	next_gen = READ_ONCE(base_crng.generation);
	if (batch->position >= ARRAY_SIZE(batch->entropy_u32) ||
	    next_gen != batch->generation) {
		_get_random_bytes(batch->entropy_u32, sizeof(batch->entropy_u32));
		batch->position = 0;
		batch->generation = next_gen;
	}

	ret = batch->entropy_u32[batch->position];
	batch->entropy_u32[batch->position] = 0;
	++batch->position;
	local_unlock_irqrestore(&batched_entropy_u32.lock, flags);
	return ret;
}
EXPORT_SYMBOL(get_random_u32);

#ifdef CONFIG_SMP
/*
 * This function is called when the CPU is coming up, with entry
 * CPUHP_RANDOM_PREPARE, which comes before CPUHP_WORKQUEUE_PREP.
 */
int random_prepare_cpu(unsigned int cpu)
{
	/*
	 * When the cpu comes back online, immediately invalidate both
	 * the per-cpu crng and all batches, so that we serve fresh
	 * randomness.
	 */
	per_cpu_ptr(&crngs, cpu)->generation = ULONG_MAX;
	per_cpu_ptr(&batched_entropy_u32, cpu)->position = UINT_MAX;
	per_cpu_ptr(&batched_entropy_u64, cpu)->position = UINT_MAX;
	return 0;
}
#endif

/**
 * randomize_page - Generate a random, page aligned address
 * @start:	The smallest acceptable address the caller will take.
 * @range:	The size of the area, starting at @start, within which the
 *		random address must fall.
 *
 * If @start + @range would overflow, @range is capped.
 *
 * NOTE: Historical use of randomize_range, which this replaces, presumed that
 * @start was already page aligned.  We now align it regardless.
 *
 * Return: A page aligned address within [start, start + range).  On error,
 * @start is returned.
 */
unsigned long randomize_page(unsigned long start, unsigned long range)
{
	if (!PAGE_ALIGNED(start)) {
		range -= PAGE_ALIGN(start) - start;
		start = PAGE_ALIGN(start);
	}

	if (start > ULONG_MAX - range)
		range = ULONG_MAX - start;

	range >>= PAGE_SHIFT;

	if (range == 0)
		return start;

	return start + (get_random_long() % range << PAGE_SHIFT);
}

/*
 * This function will use the architecture-specific hardware random
 * number generator if it is available. It is not recommended for
 * use. Use get_random_bytes() instead. It returns the number of
 * bytes filled in.
 */
size_t __must_check get_random_bytes_arch(void *buf, size_t nbytes)
{
	size_t left = nbytes;
	u8 *p = buf;

	while (left) {
		unsigned long v;
		size_t chunk = min_t(size_t, left, sizeof(unsigned long));

		if (!arch_get_random_long(&v))
			break;

		memcpy(p, &v, chunk);
		p += chunk;
		left -= chunk;
	}

	return nbytes - left;
}
EXPORT_SYMBOL(get_random_bytes_arch);


/**********************************************************************
 *
 * Entropy accumulation and extraction routines.
 *
 * Callers may add entropy via:
 *
 *     static void mix_pool_bytes(const void *in, size_t nbytes)
 *
 * After which, if added entropy should be credited:
 *
 *     static void credit_entropy_bits(size_t nbits)
 *
 * Finally, extract entropy via these two, with the latter one
 * setting the entropy count to zero and extracting only if there
 * is POOL_MIN_BITS entropy credited prior:
 *
 *     static void extract_entropy(void *buf, size_t nbytes)
 *     static bool drain_entropy(void *buf, size_t nbytes)
 *
 **********************************************************************/

enum {
	POOL_BITS = BLAKE2S_HASH_SIZE * 8,
	POOL_MIN_BITS = POOL_BITS /* No point in settling for less. */
};

/* For notifying userspace should write into /dev/random. */
static DECLARE_WAIT_QUEUE_HEAD(random_write_wait);

static struct {
	struct blake2s_state hash;
	spinlock_t lock;
	unsigned int entropy_count;
} input_pool = {
	.hash.h = { BLAKE2S_IV0 ^ (0x01010000 | BLAKE2S_HASH_SIZE),
		    BLAKE2S_IV1, BLAKE2S_IV2, BLAKE2S_IV3, BLAKE2S_IV4,
		    BLAKE2S_IV5, BLAKE2S_IV6, BLAKE2S_IV7 },
	.hash.outlen = BLAKE2S_HASH_SIZE,
	.lock = __SPIN_LOCK_UNLOCKED(input_pool.lock),
};

static void _mix_pool_bytes(const void *in, size_t nbytes)
{
	blake2s_update(&input_pool.hash, in, nbytes);
}

/*
 * This function adds bytes into the entropy "pool".  It does not
 * update the entropy estimate.  The caller should call
 * credit_entropy_bits if this is appropriate.
 */
static void mix_pool_bytes(const void *in, size_t nbytes)
{
	unsigned long flags;

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(in, nbytes);
	spin_unlock_irqrestore(&input_pool.lock, flags);
}

static void credit_entropy_bits(size_t nbits)
{
	unsigned int entropy_count, orig, add;

	if (!nbits)
		return;

	add = min_t(size_t, nbits, POOL_BITS);

	do {
		orig = READ_ONCE(input_pool.entropy_count);
		entropy_count = min_t(unsigned int, POOL_BITS, orig + add);
	} while (cmpxchg(&input_pool.entropy_count, orig, entropy_count) != orig);

	if (!crng_ready() && entropy_count >= POOL_MIN_BITS)
		crng_reseed();
}

/*
 * This is an HKDF-like construction for using the hashed collected entropy
 * as a PRF key, that's then expanded block-by-block.
 */
static void extract_entropy(void *buf, size_t nbytes)
{
	unsigned long flags;
	u8 seed[BLAKE2S_HASH_SIZE], next_key[BLAKE2S_HASH_SIZE];
	struct {
		unsigned long rdseed[32 / sizeof(long)];
		size_t counter;
	} block;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(block.rdseed); ++i) {
		if (!arch_get_random_seed_long(&block.rdseed[i]) &&
		    !arch_get_random_long(&block.rdseed[i]))
			block.rdseed[i] = random_get_entropy();
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

	while (nbytes) {
		i = min_t(size_t, nbytes, BLAKE2S_HASH_SIZE);
		/* output = HASHPRF(seed, RDSEED || ++counter) */
		++block.counter;
		blake2s(buf, (u8 *)&block, seed, i, sizeof(block), sizeof(seed));
		nbytes -= i;
		buf += i;
	}

	memzero_explicit(seed, sizeof(seed));
	memzero_explicit(&block, sizeof(block));
}

/*
 * First we make sure we have POOL_MIN_BITS of entropy in the pool, and then we
 * set the entropy count to zero (but don't actually touch any data). Only then
 * can we extract a new key with extract_entropy().
 */
static bool drain_entropy(void *buf, size_t nbytes)
{
	unsigned int entropy_count;
	do {
		entropy_count = READ_ONCE(input_pool.entropy_count);
		if (entropy_count < POOL_MIN_BITS)
			return false;
	} while (cmpxchg(&input_pool.entropy_count, entropy_count, 0) != entropy_count);
	extract_entropy(buf, nbytes);
	wake_up_interruptible(&random_write_wait);
	kill_fasync(&fasync, SIGIO, POLL_OUT);
	return true;
}


/**********************************************************************
 *
 * Entropy collection routines.
 *
 * The following exported functions are used for pushing entropy into
 * the above entropy accumulation routines:
 *
 *	void add_device_randomness(const void *buf, size_t size);
 *	void add_input_randomness(unsigned int type, unsigned int code,
 *	                          unsigned int value);
 *	void add_disk_randomness(struct gendisk *disk);
 *	void add_hwgenerator_randomness(const void *buffer, size_t count,
 *					size_t entropy);
 *	void add_bootloader_randomness(const void *buf, size_t size);
 *	void add_interrupt_randomness(int irq);
 *
 * add_device_randomness() adds data to the input pool that
 * is likely to differ between two devices (or possibly even per boot).
 * This would be things like MAC addresses or serial numbers, or the
 * read-out of the RTC. This does *not* credit any actual entropy to
 * the pool, but it initializes the pool to different values for devices
 * that might otherwise be identical and have very little entropy
 * available to them (particularly common in the embedded world).
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
 * The above two routines try to estimate how many bits of entropy
 * to credit. They do this by keeping track of the first and second
 * order deltas of the event timings.
 *
 * add_hwgenerator_randomness() is for true hardware RNGs, and will credit
 * entropy as specified by the caller. If the entropy pool is full it will
 * block until more entropy is needed.
 *
 * add_bootloader_randomness() is the same as add_hwgenerator_randomness() or
 * add_device_randomness(), depending on whether or not the configuration
 * option CONFIG_RANDOM_TRUST_BOOTLOADER is set.
 *
 * add_interrupt_randomness() uses the interrupt timing as random
 * inputs to the entropy pool. Using the cycle counters and the irq source
 * as inputs, it feeds the input pool roughly once a second or after 64
 * interrupts, crediting 1 bit of entropy for whichever comes first.
 *
 **********************************************************************/

static bool trust_cpu __ro_after_init = IS_ENABLED(CONFIG_RANDOM_TRUST_CPU);
static bool trust_bootloader __ro_after_init = IS_ENABLED(CONFIG_RANDOM_TRUST_BOOTLOADER);
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

/*
 * The first collection of entropy occurs at system boot while interrupts
 * are still turned off. Here we push in RDSEED, a timestamp, and utsname().
 * Depending on the above configuration knob, RDSEED may be considered
 * sufficient for initialization. Note that much earlier setup may already
 * have pushed entropy into the input pool by the time we get here.
 */
int __init rand_initialize(void)
{
	size_t i;
	ktime_t now = ktime_get_real();
	bool arch_init = true;
	unsigned long rv;

#if defined(LATENT_ENTROPY_PLUGIN)
	static const u8 compiletime_seed[BLAKE2S_BLOCK_SIZE] __initconst __latent_entropy;
	_mix_pool_bytes(compiletime_seed, sizeof(compiletime_seed));
#endif

	for (i = 0; i < BLAKE2S_BLOCK_SIZE; i += sizeof(rv)) {
		if (!arch_get_random_seed_long_early(&rv) &&
		    !arch_get_random_long_early(&rv)) {
			rv = random_get_entropy();
			arch_init = false;
		}
		_mix_pool_bytes(&rv, sizeof(rv));
	}
	_mix_pool_bytes(&now, sizeof(now));
	_mix_pool_bytes(utsname(), sizeof(*(utsname())));

	extract_entropy(base_crng.key, sizeof(base_crng.key));
	++base_crng.generation;

	if (arch_init && trust_cpu && !crng_ready()) {
		crng_init = 2;
		pr_notice("crng init done (trusting CPU's manufacturer)\n");
	}

	if (ratelimit_disable) {
		urandom_warning.interval = 0;
		unseeded_warning.interval = 0;
	}
	return 0;
}

/*
 * Add device- or boot-specific data to the input pool to help
 * initialize it.
 *
 * None of this adds any entropy; it is meant to avoid the problem of
 * the entropy pool having similar initial state across largely
 * identical devices.
 */
void add_device_randomness(const void *buf, size_t size)
{
	cycles_t cycles = random_get_entropy();
	unsigned long flags, now = jiffies;

	if (crng_init == 0 && size)
		crng_pre_init_inject(buf, size, false);

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(&cycles, sizeof(cycles));
	_mix_pool_bytes(&now, sizeof(now));
	_mix_pool_bytes(buf, size);
	spin_unlock_irqrestore(&input_pool.lock, flags);
}
EXPORT_SYMBOL(add_device_randomness);

/* There is one of these per entropy source */
struct timer_rand_state {
	unsigned long last_time;
	long last_delta, last_delta2;
};

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays.  It uses the timer_rand_state structure to make an estimate
 * of how many bits of entropy this call has added to the pool.
 *
 * The number "num" is also added to the pool - it should somehow describe
 * the type of event which just happened.  This is currently 0-255 for
 * keyboard scan codes, and 256 upwards for interrupts.
 */
static void add_timer_randomness(struct timer_rand_state *state, unsigned int num)
{
	cycles_t cycles = random_get_entropy();
	unsigned long flags, now = jiffies;
	long delta, delta2, delta3;

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(&cycles, sizeof(cycles));
	_mix_pool_bytes(&now, sizeof(now));
	_mix_pool_bytes(&num, sizeof(num));
	spin_unlock_irqrestore(&input_pool.lock, flags);

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
	 * delta is now minimum absolute delta.
	 * Round down by 1 bit on general principles,
	 * and limit entropy estimate to 12 bits.
	 */
	credit_entropy_bits(min_t(unsigned int, fls(delta >> 1), 11));
}

void add_input_randomness(unsigned int type, unsigned int code,
			  unsigned int value)
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

void rand_initialize_disk(struct gendisk *disk)
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

/*
 * Interface for in-kernel drivers of true hardware RNGs.
 * Those devices may produce endless random bits and will be throttled
 * when our pool is full.
 */
void add_hwgenerator_randomness(const void *buffer, size_t count,
				size_t entropy)
{
	if (unlikely(crng_init == 0 && entropy < POOL_MIN_BITS)) {
		crng_pre_init_inject(buffer, count, true);
		mix_pool_bytes(buffer, count);
		return;
	}

	/*
	 * Throttle writing if we're above the trickle threshold.
	 * We'll be woken up again once below POOL_MIN_BITS, when
	 * the calling thread is about to terminate, or once
	 * CRNG_RESEED_INTERVAL has elapsed.
	 */
	wait_event_interruptible_timeout(random_write_wait,
			!system_wq || kthread_should_stop() ||
			input_pool.entropy_count < POOL_MIN_BITS,
			CRNG_RESEED_INTERVAL);
	mix_pool_bytes(buffer, count);
	credit_entropy_bits(entropy);
}
EXPORT_SYMBOL_GPL(add_hwgenerator_randomness);

/*
 * Handle random seed passed by bootloader.
 * If the seed is trustworthy, it would be regarded as hardware RNGs. Otherwise
 * it would be regarded as device data.
 * The decision is controlled by CONFIG_RANDOM_TRUST_BOOTLOADER.
 */
void add_bootloader_randomness(const void *buf, size_t size)
{
	if (trust_bootloader)
		add_hwgenerator_randomness(buf, size, size * 8);
	else
		add_device_randomness(buf, size);
}
EXPORT_SYMBOL_GPL(add_bootloader_randomness);

struct fast_pool {
	struct work_struct mix;
	unsigned long pool[4];
	unsigned long last;
	unsigned int count;
	u16 reg_idx;
};

static DEFINE_PER_CPU(struct fast_pool, irq_randomness) = {
#ifdef CONFIG_64BIT
	/* SipHash constants */
	.pool = { 0x736f6d6570736575UL, 0x646f72616e646f6dUL,
		  0x6c7967656e657261UL, 0x7465646279746573UL }
#else
	/* HalfSipHash constants */
	.pool = { 0, 0, 0x6c796765U, 0x74656462U }
#endif
};

/*
 * This is [Half]SipHash-1-x, starting from an empty key. Because
 * the key is fixed, it assumes that its inputs are non-malicious,
 * and therefore this has no security on its own. s represents the
 * 128 or 256-bit SipHash state, while v represents a 128-bit input.
 */
static void fast_mix(unsigned long s[4], const unsigned long *v)
{
	size_t i;

	for (i = 0; i < 16 / sizeof(long); ++i) {
		s[3] ^= v[i];
#ifdef CONFIG_64BIT
		s[0] += s[1]; s[1] = rol64(s[1], 13); s[1] ^= s[0]; s[0] = rol64(s[0], 32);
		s[2] += s[3]; s[3] = rol64(s[3], 16); s[3] ^= s[2];
		s[0] += s[3]; s[3] = rol64(s[3], 21); s[3] ^= s[0];
		s[2] += s[1]; s[1] = rol64(s[1], 17); s[1] ^= s[2]; s[2] = rol64(s[2], 32);
#else
		s[0] += s[1]; s[1] = rol32(s[1],  5); s[1] ^= s[0]; s[0] = rol32(s[0], 16);
		s[2] += s[3]; s[3] = rol32(s[3],  8); s[3] ^= s[2];
		s[0] += s[3]; s[3] = rol32(s[3],  7); s[3] ^= s[0];
		s[2] += s[1]; s[1] = rol32(s[1], 13); s[1] ^= s[2]; s[2] = rol32(s[2], 16);
#endif
		s[0] ^= v[i];
	}
}

#ifdef CONFIG_SMP
/*
 * This function is called when the CPU has just come online, with
 * entry CPUHP_AP_RANDOM_ONLINE, just after CPUHP_AP_WORKQUEUE_ONLINE.
 */
int random_online_cpu(unsigned int cpu)
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

static unsigned long get_reg(struct fast_pool *f, struct pt_regs *regs)
{
	unsigned long *ptr = (unsigned long *)regs;
	unsigned int idx;

	if (regs == NULL)
		return 0;
	idx = READ_ONCE(f->reg_idx);
	if (idx >= sizeof(struct pt_regs) / sizeof(unsigned long))
		idx = 0;
	ptr += idx++;
	WRITE_ONCE(f->reg_idx, idx);
	return *ptr;
}

static void mix_interrupt_randomness(struct work_struct *work)
{
	struct fast_pool *fast_pool = container_of(work, struct fast_pool, mix);
	/*
	 * The size of the copied stack pool is explicitly 16 bytes so that we
	 * tax mix_pool_byte()'s compression function the same amount on all
	 * platforms. This means on 64-bit we copy half the pool into this,
	 * while on 32-bit we copy all of it. The entropy is supposed to be
	 * sufficiently dispersed between bits that in the sponge-like
	 * half case, on average we don't wind up "losing" some.
	 */
	u8 pool[16];

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
	fast_pool->count = 0;
	fast_pool->last = jiffies;
	local_irq_enable();

	if (unlikely(crng_init == 0)) {
		crng_pre_init_inject(pool, sizeof(pool), true);
		mix_pool_bytes(pool, sizeof(pool));
	} else {
		mix_pool_bytes(pool, sizeof(pool));
		credit_entropy_bits(1);
	}

	memzero_explicit(pool, sizeof(pool));
}

void add_interrupt_randomness(int irq)
{
	enum { MIX_INFLIGHT = 1U << 31 };
	cycles_t cycles = random_get_entropy();
	unsigned long now = jiffies;
	struct fast_pool *fast_pool = this_cpu_ptr(&irq_randomness);
	struct pt_regs *regs = get_irq_regs();
	unsigned int new_count;
	union {
		u32 u32[4];
		u64 u64[2];
		unsigned long longs[16 / sizeof(long)];
	} irq_data;

	if (cycles == 0)
		cycles = get_reg(fast_pool, regs);

	if (sizeof(cycles) == 8)
		irq_data.u64[0] = cycles ^ rol64(now, 32) ^ irq;
	else {
		irq_data.u32[0] = cycles ^ irq;
		irq_data.u32[1] = now;
	}

	if (sizeof(unsigned long) == 8)
		irq_data.u64[1] = regs ? instruction_pointer(regs) : _RET_IP_;
	else {
		irq_data.u32[2] = regs ? instruction_pointer(regs) : _RET_IP_;
		irq_data.u32[3] = get_reg(fast_pool, regs);
	}

	fast_mix(fast_pool->pool, irq_data.longs);
	new_count = ++fast_pool->count;

	if (new_count & MIX_INFLIGHT)
		return;

	if (new_count < 64 && (!time_after(now, fast_pool->last + HZ) ||
			       unlikely(crng_init == 0)))
		return;

	if (unlikely(!fast_pool->mix.func))
		INIT_WORK(&fast_pool->mix, mix_interrupt_randomness);
	fast_pool->count |= MIX_INFLIGHT;
	queue_work_on(raw_smp_processor_id(), system_highpri_wq, &fast_pool->mix);
}
EXPORT_SYMBOL_GPL(add_interrupt_randomness);

/*
 * Each time the timer fires, we expect that we got an unpredictable
 * jump in the cycle counter. Even if the timer is running on another
 * CPU, the timer activity will be touching the stack of the CPU that is
 * generating entropy..
 *
 * Note that we don't re-arm the timer in the timer itself - we are
 * happy to be scheduled away, since that just makes the load more
 * complex, but we do not want the timer to keep ticking unless the
 * entropy loop is running.
 *
 * So the re-arming always happens in the entropy loop itself.
 */
static void entropy_timer(struct timer_list *t)
{
	credit_entropy_bits(1);
}

/*
 * If we have an actual cycle counter, see if we can
 * generate enough entropy with timing noise
 */
static void try_to_generate_entropy(void)
{
	struct {
		cycles_t cycles;
		struct timer_list timer;
	} stack;

	stack.cycles = random_get_entropy();

	/* Slow counter - or none. Don't even bother */
	if (stack.cycles == random_get_entropy())
		return;

	timer_setup_on_stack(&stack.timer, entropy_timer, 0);
	while (!crng_ready() && !signal_pending(current)) {
		if (!timer_pending(&stack.timer))
			mod_timer(&stack.timer, jiffies + 1);
		mix_pool_bytes(&stack.cycles, sizeof(stack.cycles));
		schedule();
		stack.cycles = random_get_entropy();
	}

	del_timer_sync(&stack.timer);
	destroy_timer_on_stack(&stack.timer);
	mix_pool_bytes(&stack.cycles, sizeof(stack.cycles));
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

SYSCALL_DEFINE3(getrandom, char __user *, buf, size_t, count, unsigned int,
		flags)
{
	if (flags & ~(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE))
		return -EINVAL;

	/*
	 * Requesting insecure and blocking randomness at the same time makes
	 * no sense.
	 */
	if ((flags & (GRND_INSECURE | GRND_RANDOM)) == (GRND_INSECURE | GRND_RANDOM))
		return -EINVAL;

	if (count > INT_MAX)
		count = INT_MAX;

	if (!(flags & GRND_INSECURE) && !crng_ready()) {
		int ret;

		if (flags & GRND_NONBLOCK)
			return -EAGAIN;
		ret = wait_for_random_bytes();
		if (unlikely(ret))
			return ret;
	}
	return get_random_bytes_user(buf, count);
}

static __poll_t random_poll(struct file *file, poll_table *wait)
{
	__poll_t mask;

	poll_wait(file, &crng_init_wait, wait);
	poll_wait(file, &random_write_wait, wait);
	mask = 0;
	if (crng_ready())
		mask |= EPOLLIN | EPOLLRDNORM;
	if (input_pool.entropy_count < POOL_MIN_BITS)
		mask |= EPOLLOUT | EPOLLWRNORM;
	return mask;
}

static int write_pool(const char __user *ubuf, size_t count)
{
	size_t len;
	int ret = 0;
	u8 block[BLAKE2S_BLOCK_SIZE];

	while (count) {
		len = min(count, sizeof(block));
		if (copy_from_user(block, ubuf, len)) {
			ret = -EFAULT;
			goto out;
		}
		count -= len;
		ubuf += len;
		mix_pool_bytes(block, len);
		cond_resched();
	}

out:
	memzero_explicit(block, sizeof(block));
	return ret;
}

static ssize_t random_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	int ret;

	ret = write_pool(buffer, count);
	if (ret)
		return ret;

	return (ssize_t)count;
}

static ssize_t urandom_read(struct file *file, char __user *buf, size_t nbytes,
			    loff_t *ppos)
{
	static int maxwarn = 10;

	if (!crng_ready() && maxwarn > 0) {
		maxwarn--;
		if (__ratelimit(&urandom_warning))
			pr_notice("%s: uninitialized urandom read (%zd bytes read)\n",
				  current->comm, nbytes);
	}

	return get_random_bytes_user(buf, nbytes);
}

static ssize_t random_read(struct file *file, char __user *buf, size_t nbytes,
			   loff_t *ppos)
{
	int ret;

	ret = wait_for_random_bytes();
	if (ret != 0)
		return ret;
	return get_random_bytes_user(buf, nbytes);
}

static long random_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int size, ent_count;
	int __user *p = (int __user *)arg;
	int retval;

	switch (cmd) {
	case RNDGETENTCNT:
		/* Inherently racy, no point locking. */
		if (put_user(input_pool.entropy_count, p))
			return -EFAULT;
		return 0;
	case RNDADDTOENTCNT:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (get_user(ent_count, p))
			return -EFAULT;
		if (ent_count < 0)
			return -EINVAL;
		credit_entropy_bits(ent_count);
		return 0;
	case RNDADDENTROPY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (get_user(ent_count, p++))
			return -EFAULT;
		if (ent_count < 0)
			return -EINVAL;
		if (get_user(size, p++))
			return -EFAULT;
		retval = write_pool((const char __user *)p, size);
		if (retval < 0)
			return retval;
		credit_entropy_bits(ent_count);
		return 0;
	case RNDZAPENTCNT:
	case RNDCLEARPOOL:
		/*
		 * Clear the entropy pool counters. We no longer clear
		 * the entropy pool, as that's silly.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (xchg(&input_pool.entropy_count, 0) >= POOL_MIN_BITS) {
			wake_up_interruptible(&random_write_wait);
			kill_fasync(&fasync, SIGIO, POLL_OUT);
		}
		return 0;
	case RNDRESEEDCRNG:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (!crng_ready())
			return -ENODATA;
		crng_reseed();
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
	.read = random_read,
	.write = random_write,
	.poll = random_poll,
	.unlocked_ioctl = random_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.fasync = random_fasync,
	.llseek = noop_llseek,
};

const struct file_operations urandom_fops = {
	.read = urandom_read,
	.write = random_write,
	.unlocked_ioctl = random_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.fasync = random_fasync,
	.llseek = noop_llseek,
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
 *   more entropy, tied to the POOL_MIN_BITS constant. It is writable
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
static int sysctl_random_write_wakeup_bits = POOL_MIN_BITS;
static int sysctl_poolsize = POOL_BITS;
static u8 sysctl_bootid[UUID_SIZE];

/*
 * This function is used to return both the bootid UUID, and random
 * UUID. The difference is in whether table->data is NULL; if it is,
 * then a new UUID is generated and returned to the user.
 */
static int proc_do_uuid(struct ctl_table *table, int write, void *buffer,
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
	return proc_dostring(&fake_table, 0, buffer, lenp, ppos);
}

/* The same as proc_dointvec, but writes don't change anything. */
static int proc_do_rointvec(struct ctl_table *table, int write, void *buffer,
			    size_t *lenp, loff_t *ppos)
{
	return write ? 0 : proc_dointvec(table, 0, buffer, lenp, ppos);
}

extern struct ctl_table random_table[];
struct ctl_table random_table[] = {
	{
		.procname	= "poolsize",
		.data		= &sysctl_poolsize,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "entropy_avail",
		.data		= &input_pool.entropy_count,
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
	{ }
};
#endif	/* CONFIG_SYSCTL */
