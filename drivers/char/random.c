// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright (C) 2017-2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright Matt Mackall <mpm@selenic.com>, 2003, 2004, 2005
 * Copyright Theodore Ts'o, 1994, 1995, 1996, 1997, 1998, 1999.  All
 * rights reserved.
 */

/*
 * Exported interfaces ---- output
 * ===============================
 *
 * There are four exported interfaces; two for use within the kernel,
 * and two for use from userspace.
 *
 * Exported interfaces ---- userspace output
 * -----------------------------------------
 *
 * The userspace interfaces are two character devices /dev/random and
 * /dev/urandom.  /dev/random is suitable for use when very high
 * quality randomness is desired (for example, for key generation or
 * one-time pads), as it will only return a maximum of the number of
 * bits of randomness (as estimated by the random number generator)
 * contained in the entropy pool.
 *
 * The /dev/urandom device does not have this limit, and will return
 * as many bytes as are requested.  As more and more random bytes are
 * requested without giving time for the entropy pool to recharge,
 * this will result in random numbers that are merely cryptographically
 * strong.  For many applications, however, this is acceptable.
 *
 * Exported interfaces ---- kernel output
 * --------------------------------------
 *
 * The primary kernel interfaces are:
 *
 *	void get_random_bytes(void *buf, size_t nbytes);
 *	u32 get_random_u32()
 *	u64 get_random_u64()
 *	unsigned int get_random_int()
 *	unsigned long get_random_long()
 *
 * These interfaces will return the requested number of random bytes
 * into the given buffer or as a return value. This is equivalent to a
 * read from /dev/urandom. The get_random_{u32,u64,int,long}() family
 * of functions may be higher performance for one-off random integers,
 * because they do a bit of buffering.
 *
 * prandom_u32()
 * -------------
 *
 * For even weaker applications, see the pseudorandom generator
 * prandom_u32(), prandom_max(), and prandom_bytes().  If the random
 * numbers aren't security-critical at all, these are *far* cheaper.
 * Useful for self-tests, random error simulation, randomized backoffs,
 * and any other application where you trust that nobody is trying to
 * maliciously mess with you by guessing the "random" numbers.
 *
 * Exported interfaces ---- input
 * ==============================
 *
 * The current exported interfaces for gathering environmental noise
 * from the devices are:
 *
 *	void add_device_randomness(const void *buf, size_t size);
 *	void add_input_randomness(unsigned int type, unsigned int code,
 *                                unsigned int value);
 *	void add_interrupt_randomness(int irq);
 *	void add_disk_randomness(struct gendisk *disk);
 *	void add_hwgenerator_randomness(const void *buffer, size_t count,
 *					size_t entropy);
 *	void add_bootloader_randomness(const void *buf, size_t size);
 *
 * add_device_randomness() is for adding data to the random pool that
 * is likely to differ between two devices (or possibly even per boot).
 * This would be things like MAC addresses or serial numbers, or the
 * read-out of the RTC. This does *not* add any actual entropy to the
 * pool, but it initializes the pool to different values for devices
 * that might otherwise be identical and have very little entropy
 * available to them (particularly common in the embedded world).
 *
 * add_input_randomness() uses the input layer interrupt timing, as well as
 * the event type information from the hardware.
 *
 * add_interrupt_randomness() uses the interrupt timing as random
 * inputs to the entropy pool. Using the cycle counters and the irq source
 * as inputs, it feeds the randomness roughly once a second.
 *
 * add_disk_randomness() uses what amounts to the seek time of block
 * layer request events, on a per-disk_devt basis, as input to the
 * entropy pool. Note that high-speed solid state drives with very low
 * seek times do not make for good sources of entropy, as their seek
 * times are usually fairly consistent.
 *
 * All of these routines try to estimate how many bits of randomness a
 * particular randomness source.  They do this by keeping track of the
 * first and second order deltas of the event timings.
 *
 * add_hwgenerator_randomness() is for true hardware RNGs, and will credit
 * entropy as specified by the caller. If the entropy pool is full it will
 * block until more entropy is needed.
 *
 * add_bootloader_randomness() is the same as add_hwgenerator_randomness() or
 * add_device_randomness(), depending on whether or not the configuration
 * option CONFIG_RANDOM_TRUST_BOOTLOADER is set.
 *
 * Ensuring unpredictability at system startup
 * ============================================
 *
 * When any operating system starts up, it will go through a sequence
 * of actions that are fairly predictable by an adversary, especially
 * if the start-up does not involve interaction with a human operator.
 * This reduces the actual number of bits of unpredictability in the
 * entropy pool below the value in entropy_count.  In order to
 * counteract this effect, it helps to carry information in the
 * entropy pool across shut-downs and start-ups.  To do this, put the
 * following lines an appropriate script which is run during the boot
 * sequence:
 *
 *	echo "Initializing random number generator..."
 *	random_seed=/var/run/random-seed
 *	# Carry a random seed from start-up to start-up
 *	# Load and then save the whole entropy pool
 *	if [ -f $random_seed ]; then
 *		cat $random_seed >/dev/urandom
 *	else
 *		touch $random_seed
 *	fi
 *	chmod 600 $random_seed
 *	dd if=/dev/urandom of=$random_seed count=1 bs=512
 *
 * and the following lines in an appropriate script which is run as
 * the system is shutdown:
 *
 *	# Carry a random seed from shut-down to start-up
 *	# Save the whole entropy pool
 *	echo "Saving random seed..."
 *	random_seed=/var/run/random-seed
 *	touch $random_seed
 *	chmod 600 $random_seed
 *	dd if=/dev/urandom of=$random_seed count=1 bs=512
 *
 * For example, on most modern systems using the System V init
 * scripts, such code fragments would be found in
 * /etc/rc.d/init.d/random.  On older Linux systems, the correct script
 * location might be in /etc/rcb.d/rc.local or /etc/rc.d/rc.0.
 *
 * Effectively, these commands cause the contents of the entropy pool
 * to be saved at shut-down time and reloaded into the entropy pool at
 * start-up.  (The 'dd' in the addition to the bootup script is to
 * make sure that /etc/random-seed is different for every start-up,
 * even if the system crashes without executing rc.0.)  Even with
 * complete knowledge of the start-up activities, predicting the state
 * of the entropy pool requires knowledge of the previous history of
 * the system.
 *
 * Configuring the /dev/random driver under Linux
 * ==============================================
 *
 * The /dev/random driver under Linux uses minor numbers 8 and 9 of
 * the /dev/mem major number (#1).  So if your system does not have
 * /dev/random and /dev/urandom created already, they can be created
 * by using the commands:
 *
 *	mknod /dev/random c 1 8
 *	mknod /dev/urandom c 1 9
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
static DEFINE_SPINLOCK(random_ready_list_lock);
static LIST_HEAD(random_ready_list);

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
	if (likely(crng_ready()))
		return 0;

	do {
		int ret;
		ret = wait_event_interruptible_timeout(crng_init_wait, crng_ready(), HZ);
		if (ret)
			return ret > 0 ? 0 : ret;

		try_to_generate_entropy();
	} while (!crng_ready());

	return 0;
}
EXPORT_SYMBOL(wait_for_random_bytes);

/*
 * Add a callback function that will be invoked when the input
 * pool is initialised.
 *
 * returns: 0 if callback is successfully added
 *	    -EALREADY if pool is already initialised (callback not called)
 *	    -ENOENT if module for callback is not alive
 */
int add_random_ready_callback(struct random_ready_callback *rdy)
{
	struct module *owner;
	unsigned long flags;
	int err = -EALREADY;

	if (crng_ready())
		return err;

	owner = rdy->owner;
	if (!try_module_get(owner))
		return -ENOENT;

	spin_lock_irqsave(&random_ready_list_lock, flags);
	if (crng_ready())
		goto out;

	owner = NULL;

	list_add(&rdy->list, &random_ready_list);
	err = 0;

out:
	spin_unlock_irqrestore(&random_ready_list_lock, flags);

	module_put(owner);

	return err;
}
EXPORT_SYMBOL(add_random_ready_callback);

/*
 * Delete a previously registered readiness callback function.
 */
void del_random_ready_callback(struct random_ready_callback *rdy)
{
	unsigned long flags;
	struct module *owner = NULL;

	spin_lock_irqsave(&random_ready_list_lock, flags);
	if (!list_empty(&rdy->list)) {
		list_del_init(&rdy->list);
		owner = rdy->owner;
	}
	spin_unlock_irqrestore(&random_ready_list_lock, flags);

	module_put(owner);
}
EXPORT_SYMBOL(del_random_ready_callback);

static void process_random_ready_list(void)
{
	unsigned long flags;
	struct random_ready_callback *rdy, *tmp;

	spin_lock_irqsave(&random_ready_list_lock, flags);
	list_for_each_entry_safe(rdy, tmp, &random_ready_list, list) {
		struct module *owner = rdy->owner;

		list_del_init(&rdy->list);
		rdy->func(rdy);
		module_put(owner);
	}
	spin_unlock_irqrestore(&random_ready_list_lock, flags);
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


enum {
	POOL_BITS = BLAKE2S_HASH_SIZE * 8,
	POOL_MIN_BITS = POOL_BITS /* No point in settling for less. */
};

/*
 * Static global variables
 */
static DECLARE_WAIT_QUEUE_HEAD(random_write_wait);

static int crng_init_cnt = 0;

/**********************************************************************
 *
 * OS independent entropy store.   Here are the functions which handle
 * storing entropy in an entropy pool.
 *
 **********************************************************************/

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

static void extract_entropy(void *buf, size_t nbytes);
static bool drain_entropy(void *buf, size_t nbytes);

static void crng_reseed(void);

/*
 * This function adds bytes into the entropy "pool".  It does not
 * update the entropy estimate.  The caller should call
 * credit_entropy_bits if this is appropriate.
 */
static void _mix_pool_bytes(const void *in, size_t nbytes)
{
	blake2s_update(&input_pool.hash, in, nbytes);
}

static void mix_pool_bytes(const void *in, size_t nbytes)
{
	unsigned long flags;

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(in, nbytes);
	spin_unlock_irqrestore(&input_pool.lock, flags);
}

struct fast_pool {
	union {
		u32 pool32[4];
		u64 pool64[2];
	};
	unsigned long last;
	u16 reg_idx;
	u8 count;
};

/*
 * This is a fast mixing routine used by the interrupt randomness
 * collector.  It's hardcoded for an 128 bit pool and assumes that any
 * locks that might be needed are taken by the caller.
 */
static void fast_mix(u32 pool[4])
{
	u32 a = pool[0],	b = pool[1];
	u32 c = pool[2],	d = pool[3];

	a += b;			c += d;
	b = rol32(b, 6);	d = rol32(d, 27);
	d ^= a;			b ^= c;

	a += b;			c += d;
	b = rol32(b, 16);	d = rol32(d, 14);
	d ^= a;			b ^= c;

	a += b;			c += d;
	b = rol32(b, 6);	d = rol32(d, 27);
	d ^= a;			b ^= c;

	a += b;			c += d;
	b = rol32(b, 16);	d = rol32(d, 14);
	d ^= a;			b ^= c;

	pool[0] = a;  pool[1] = b;
	pool[2] = c;  pool[3] = d;
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

	if (crng_init < 2 && entropy_count >= POOL_MIN_BITS)
		crng_reseed();
}

/*********************************************************************
 *
 * CRNG using CHACHA20
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

/*
 * crng_fast_load() can be called by code in the interrupt service
 * path.  So we can't afford to dilly-dally. Returns the number of
 * bytes processed from cp.
 */
static size_t crng_fast_load(const void *cp, size_t len)
{
	unsigned long flags;
	const u8 *src = (const u8 *)cp;
	size_t ret = 0;

	if (!spin_trylock_irqsave(&base_crng.lock, flags))
		return 0;
	if (crng_init != 0) {
		spin_unlock_irqrestore(&base_crng.lock, flags);
		return 0;
	}
	while (len > 0 && crng_init_cnt < CRNG_INIT_CNT_THRESH) {
		base_crng.key[crng_init_cnt % sizeof(base_crng.key)] ^= *src;
		src++; crng_init_cnt++; len--; ret++;
	}
	if (crng_init_cnt >= CRNG_INIT_CNT_THRESH) {
		++base_crng.generation;
		crng_init = 1;
	}
	spin_unlock_irqrestore(&base_crng.lock, flags);
	if (crng_init == 1)
		pr_notice("fast init done\n");
	return ret;
}

/*
 * crng_slow_load() is called by add_device_randomness, which has two
 * attributes.  (1) We can't trust the buffer passed to it is
 * guaranteed to be unpredictable (so it might not have any entropy at
 * all), and (2) it doesn't have the performance constraints of
 * crng_fast_load().
 *
 * So, we simply hash the contents in with the current key. Finally,
 * we do *not* advance crng_init_cnt since buffer we may get may be
 * something like a fixed DMI table (for example), which might very
 * well be unique to the machine, but is otherwise unvarying.
 */
static void crng_slow_load(const void *cp, size_t len)
{
	unsigned long flags;
	struct blake2s_state hash;

	blake2s_init(&hash, sizeof(base_crng.key));

	if (!spin_trylock_irqsave(&base_crng.lock, flags))
		return;
	if (crng_init != 0) {
		spin_unlock_irqrestore(&base_crng.lock, flags);
		return;
	}

	blake2s_update(&hash, base_crng.key, sizeof(base_crng.key));
	blake2s_update(&hash, cp, len);
	blake2s_final(&hash, base_crng.key);

	spin_unlock_irqrestore(&base_crng.lock, flags);
}

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
	if (crng_init < 2) {
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
 * The general form here is based on a "fast key erasure RNG" from
 * <https://blog.cr.yp.to/20170723-random.html>. It generates a ChaCha
 * block using the provided key, and then immediately overwites that
 * key with half the block. It returns the resultant ChaCha state to the
 * user, along with the second half of the block containing 32 bytes of
 * random data that may be used; random_data_len may not be greater than
 * 32.
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
	 * ready, we do fast key erasure with the base_crng directly, because
	 * this is what crng_{fast,slow}_load mutate during early init.
	 */
	if (unlikely(!crng_ready())) {
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
	 * If the base_crng is more than 5 minutes old, we reseed, which
	 * in turn bumps the generation counter that we check below.
	 */
	if (unlikely(time_after(jiffies, READ_ONCE(base_crng.birth) + CRNG_RESEED_INTERVAL)))
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

static ssize_t get_random_bytes_user(void __user *buf, size_t nbytes)
{
	bool large_request = nbytes > 256;
	ssize_t ret = 0;
	size_t len;
	u32 chacha_state[CHACHA_STATE_WORDS];
	u8 output[CHACHA_BLOCK_SIZE];

	if (!nbytes)
		return 0;

	len = min_t(size_t, 32, nbytes);
	crng_make_state(chacha_state, output, len);

	if (copy_to_user(buf, output, len))
		return -EFAULT;
	nbytes -= len;
	buf += len;
	ret += len;

	while (nbytes) {
		if (large_request && need_resched()) {
			if (signal_pending(current))
				break;
			schedule();
		}

		chacha20_block(chacha_state, output);
		if (unlikely(chacha_state[12] == 0))
			++chacha_state[13];

		len = min_t(size_t, nbytes, CHACHA_BLOCK_SIZE);
		if (copy_to_user(buf, output, len)) {
			ret = -EFAULT;
			break;
		}

		nbytes -= len;
		buf += len;
		ret += len;
	}

	memzero_explicit(chacha_state, sizeof(chacha_state));
	memzero_explicit(output, sizeof(output));
	return ret;
}

/*********************************************************************
 *
 * Entropy input management
 *
 *********************************************************************/

/* There is one of these per entropy source */
struct timer_rand_state {
	cycles_t last_time;
	long last_delta, last_delta2;
};

#define INIT_TIMER_RAND_STATE { INITIAL_JIFFIES, };

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
	unsigned long time = random_get_entropy() ^ jiffies;
	unsigned long flags;

	if (!crng_ready() && size)
		crng_slow_load(buf, size);

	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(buf, size);
	_mix_pool_bytes(&time, sizeof(time));
	spin_unlock_irqrestore(&input_pool.lock, flags);
}
EXPORT_SYMBOL(add_device_randomness);

static struct timer_rand_state input_timer_state = INIT_TIMER_RAND_STATE;

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays.  It uses the timer_rand_state structure to make an estimate
 * of how many bits of entropy this call has added to the pool.
 *
 * The number "num" is also added to the pool - it should somehow describe
 * the type of event which just happened.  This is currently 0-255 for
 * keyboard scan codes, and 256 upwards for interrupts.
 *
 */
static void add_timer_randomness(struct timer_rand_state *state, unsigned int num)
{
	struct {
		long jiffies;
		unsigned int cycles;
		unsigned int num;
	} sample;
	long delta, delta2, delta3;

	sample.jiffies = jiffies;
	sample.cycles = random_get_entropy();
	sample.num = num;
	mix_pool_bytes(&sample, sizeof(sample));

	/*
	 * Calculate number of bits of randomness we probably added.
	 * We take into account the first, second and third-order deltas
	 * in order to make our estimate.
	 */
	delta = sample.jiffies - READ_ONCE(state->last_time);
	WRITE_ONCE(state->last_time, sample.jiffies);

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

	/* ignore autorepeat and the like */
	if (value == last_value)
		return;

	last_value = value;
	add_timer_randomness(&input_timer_state,
			     (type << 4) ^ code ^ (code >> 4) ^ value);
}
EXPORT_SYMBOL_GPL(add_input_randomness);

static DEFINE_PER_CPU(struct fast_pool, irq_randomness);

static u32 get_reg(struct fast_pool *f, struct pt_regs *regs)
{
	u32 *ptr = (u32 *)regs;
	unsigned int idx;

	if (regs == NULL)
		return 0;
	idx = READ_ONCE(f->reg_idx);
	if (idx >= sizeof(struct pt_regs) / sizeof(u32))
		idx = 0;
	ptr += idx++;
	WRITE_ONCE(f->reg_idx, idx);
	return *ptr;
}

void add_interrupt_randomness(int irq)
{
	struct fast_pool *fast_pool = this_cpu_ptr(&irq_randomness);
	struct pt_regs *regs = get_irq_regs();
	unsigned long now = jiffies;
	cycles_t cycles = random_get_entropy();

	if (cycles == 0)
		cycles = get_reg(fast_pool, regs);

	if (sizeof(cycles) == 8)
		fast_pool->pool64[0] ^= cycles ^ rol64(now, 32) ^ irq;
	else {
		fast_pool->pool32[0] ^= cycles ^ irq;
		fast_pool->pool32[1] ^= now;
	}

	if (sizeof(unsigned long) == 8)
		fast_pool->pool64[1] ^= regs ? instruction_pointer(regs) : _RET_IP_;
	else {
		fast_pool->pool32[2] ^= regs ? instruction_pointer(regs) : _RET_IP_;
		fast_pool->pool32[3] ^= get_reg(fast_pool, regs);
	}

	fast_mix(fast_pool->pool32);
	++fast_pool->count;

	if (unlikely(crng_init == 0)) {
		if (fast_pool->count >= 64 &&
		    crng_fast_load(fast_pool->pool32, sizeof(fast_pool->pool32)) > 0) {
			fast_pool->count = 0;
			fast_pool->last = now;
			if (spin_trylock(&input_pool.lock)) {
				_mix_pool_bytes(&fast_pool->pool32, sizeof(fast_pool->pool32));
				spin_unlock(&input_pool.lock);
			}
		}
		return;
	}

	if ((fast_pool->count < 64) && !time_after(now, fast_pool->last + HZ))
		return;

	if (!spin_trylock(&input_pool.lock))
		return;

	fast_pool->last = now;
	_mix_pool_bytes(&fast_pool->pool32, sizeof(fast_pool->pool32));
	spin_unlock(&input_pool.lock);

	fast_pool->count = 0;

	/* award one bit for the contents of the fast pool */
	credit_entropy_bits(1);
}
EXPORT_SYMBOL_GPL(add_interrupt_randomness);

#ifdef CONFIG_BLOCK
void add_disk_randomness(struct gendisk *disk)
{
	if (!disk || !disk->random)
		return;
	/* first major is 1, so we get >= 0x200 here */
	add_timer_randomness(disk->random, 0x100 + disk_devt(disk));
}
EXPORT_SYMBOL_GPL(add_disk_randomness);
#endif

/*********************************************************************
 *
 * Entropy extraction routines
 *
 *********************************************************************/

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

void get_random_bytes(void *buf, size_t nbytes)
{
	static void *previous;

	warn_unseeded_randomness(&previous);
	_get_random_bytes(buf, nbytes);
}
EXPORT_SYMBOL(get_random_bytes);

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
		unsigned long now;
		struct timer_list timer;
	} stack;

	stack.now = random_get_entropy();

	/* Slow counter - or none. Don't even bother */
	if (stack.now == random_get_entropy())
		return;

	timer_setup_on_stack(&stack.timer, entropy_timer, 0);
	while (!crng_ready()) {
		if (!timer_pending(&stack.timer))
			mod_timer(&stack.timer, jiffies + 1);
		mix_pool_bytes(&stack.now, sizeof(stack.now));
		schedule();
		stack.now = random_get_entropy();
	}

	del_timer_sync(&stack.timer);
	destroy_timer_on_stack(&stack.timer);
	mix_pool_bytes(&stack.now, sizeof(stack.now));
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

static bool trust_cpu __ro_after_init = IS_ENABLED(CONFIG_RANDOM_TRUST_CPU);
static int __init parse_trust_cpu(char *arg)
{
	return kstrtobool(arg, &trust_cpu);
}
early_param("random.trust_cpu", parse_trust_cpu);

/*
 * Note that setup_arch() may call add_device_randomness()
 * long before we get here. This allows seeding of the pools
 * with some platform dependent data very early in the boot
 * process. But it limits our options here. We must use
 * statically allocated structures that already have all
 * initializations complete at compile time. We should also
 * take care not to overwrite the precious per platform data
 * we were given.
 */
int __init rand_initialize(void)
{
	size_t i;
	ktime_t now = ktime_get_real();
	bool arch_init = true;
	unsigned long rv;

	for (i = 0; i < BLAKE2S_BLOCK_SIZE; i += sizeof(rv)) {
		if (!arch_get_random_seed_long_early(&rv) &&
		    !arch_get_random_long_early(&rv)) {
			rv = random_get_entropy();
			arch_init = false;
		}
		mix_pool_bytes(&rv, sizeof(rv));
	}
	mix_pool_bytes(&now, sizeof(now));
	mix_pool_bytes(utsname(), sizeof(*(utsname())));

	extract_entropy(base_crng.key, sizeof(base_crng.key));
	++base_crng.generation;

	if (arch_init && trust_cpu && crng_init < 2) {
		crng_init = 2;
		pr_notice("crng init done (trusting CPU's manufacturer)\n");
	}

	if (ratelimit_disable) {
		urandom_warning.interval = 0;
		unseeded_warning.interval = 0;
	}
	return 0;
}

#ifdef CONFIG_BLOCK
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

static long random_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int size, ent_count;
	int __user *p = (int __user *)arg;
	int retval;

	switch (cmd) {
	case RNDGETENTCNT:
		/* inherently racy, no point locking */
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
		if (xchg(&input_pool.entropy_count, 0)) {
			wake_up_interruptible(&random_write_wait);
			kill_fasync(&fasync, SIGIO, POLL_OUT);
		}
		return 0;
	case RNDRESEEDCRNG:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (crng_init < 2)
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

/********************************************************************
 *
 * Sysctl interface
 *
 ********************************************************************/

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static int random_min_urandom_seed = 60;
static int random_write_wakeup_bits = POOL_MIN_BITS;
static int sysctl_poolsize = POOL_BITS;
static char sysctl_bootid[16];

/*
 * This function is used to return both the bootid UUID, and random
 * UUID.  The difference is in whether table->data is NULL; if it is,
 * then a new UUID is generated and returned to the user.
 *
 * If the user accesses this via the proc interface, the UUID will be
 * returned as an ASCII string in the standard UUID format; if via the
 * sysctl system call, as 16 bytes of binary data.
 */
static int proc_do_uuid(struct ctl_table *table, int write, void *buffer,
			size_t *lenp, loff_t *ppos)
{
	struct ctl_table fake_table;
	unsigned char buf[64], tmp_uuid[16], *uuid;

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

	sprintf(buf, "%pU", uuid);

	fake_table.data = buf;
	fake_table.maxlen = sizeof(buf);

	return proc_dostring(&fake_table, write, buffer, lenp, ppos);
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
		.data		= &random_write_wakeup_bits,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "urandom_min_reseed_secs",
		.data		= &random_min_urandom_seed,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "boot_id",
		.data		= &sysctl_bootid,
		.maxlen		= 16,
		.mode		= 0444,
		.proc_handler	= proc_do_uuid,
	},
	{
		.procname	= "uuid",
		.maxlen		= 16,
		.mode		= 0444,
		.proc_handler	= proc_do_uuid,
	},
	{ }
};
#endif	/* CONFIG_SYSCTL */

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

/*
 * Get a random word for internal kernel use only. The quality of the random
 * number is good as /dev/urandom. In order to ensure that the randomness
 * provided by this function is okay, the function wait_for_random_bytes()
 * should be called and return 0 at least once at any point prior.
 */
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

/* Interface for in-kernel drivers of true hardware RNGs.
 * Those devices may produce endless random bits and will be throttled
 * when our pool is full.
 */
void add_hwgenerator_randomness(const void *buffer, size_t count,
				size_t entropy)
{
	if (unlikely(crng_init == 0)) {
		size_t ret = crng_fast_load(buffer, count);
		mix_pool_bytes(buffer, ret);
		count -= ret;
		buffer += ret;
		if (!count || crng_init == 0)
			return;
	}

	/* Throttle writing if we're above the trickle threshold.
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

/* Handle random seed passed by bootloader.
 * If the seed is trustworthy, it would be regarded as hardware RNGs. Otherwise
 * it would be regarded as device data.
 * The decision is controlled by CONFIG_RANDOM_TRUST_BOOTLOADER.
 */
void add_bootloader_randomness(const void *buf, size_t size)
{
	if (IS_ENABLED(CONFIG_RANDOM_TRUST_BOOTLOADER))
		add_hwgenerator_randomness(buf, size, size * 8);
	else
		add_device_randomness(buf, size);
}
EXPORT_SYMBOL_GPL(add_bootloader_randomness);
