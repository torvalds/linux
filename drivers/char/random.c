/*
 * random.c -- A strong random number generator
 *
 * Copyright (C) 2017-2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * Copyright Matt Mackall <mpm@selenic.com>, 2003, 2004, 2005
 *
 * Copyright Theodore Ts'o, 1994, 1995, 1996, 1997, 1998, 1999.  All
 * rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
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
 * The primary kernel interface is
 *
 *	void get_random_bytes(void *buf, int nbytes);
 *
 * This interface will return the requested number of random bytes,
 * and place it in the requested buffer.  This is equivalent to a
 * read from /dev/urandom.
 *
 * For less critical applications, there are the functions:
 *
 *	u32 get_random_u32()
 *	u64 get_random_u64()
 *	unsigned int get_random_int()
 *	unsigned long get_random_long()
 *
 * These are produced by a cryptographic RNG seeded from get_random_bytes,
 * and so do not deplete the entropy pool as much.  These are recommended
 * for most in-kernel operations *if the result is going to be stored in
 * the kernel*.
 *
 * Specifically, the get_random_int() family do not attempt to do
 * "anti-backtracking".  If you capture the state of the kernel (e.g.
 * by snapshotting the VM), you can figure out previous get_random_int()
 * return values.  But if the value is stored in the kernel anyway,
 * this is not a problem.
 *
 * It *is* safe to expose get_random_int() output to attackers (e.g. as
 * network cookies); given outputs 1..n, it's not feasible to predict
 * outputs 0 or n+1.  The only concern is an attacker who breaks into
 * the kernel later; the get_random_int() engine is not reseeded as
 * often as the get_random_bytes() one.
 *
 * get_random_bytes() is needed for keys that need to stay secret after
 * they are erased from the kernel.  For example, any key that will
 * be wrapped and stored encrypted.  And session encryption keys: we'd
 * like to know that after the session is closed and the keys erased,
 * the plaintext is unrecoverable to someone who recorded the ciphertext.
 *
 * But for network ports/cookies, stack canaries, PRNG seeds, address
 * space layout randomization, session *authentication* keys, or other
 * applications where the sensitive data is stored in the kernel in
 * plaintext for as long as it's sensitive, the get_random_int() family
 * is just fine.
 *
 * Consider ASLR.  We want to keep the address space secret from an
 * outside attacker while the process is running, but once the address
 * space is torn down, it's of no use to an attacker any more.  And it's
 * stored in kernel data structures as long as it's alive, so worrying
 * about an attacker's ability to extrapolate it from the get_random_int()
 * CRNG is silly.
 *
 * Even some cryptographic keys are safe to generate with get_random_int().
 * In particular, keys for SipHash are generally fine.  Here, knowledge
 * of the key authorizes you to do something to a kernel object (inject
 * packets to a network connection, or flood a hash table), and the
 * key is stored with the object being protected.  Once it goes away,
 * we no longer care if anyone knows the key.
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
 *	void add_device_randomness(const void *buf, unsigned int size);
 *	void add_input_randomness(unsigned int type, unsigned int code,
 *                                unsigned int value);
 *	void add_interrupt_randomness(int irq);
 *	void add_disk_randomness(struct gendisk *disk);
 *	void add_hwgenerator_randomness(const char *buffer, size_t count,
 *					size_t entropy);
 *	void add_bootloader_randomness(const void *buf, unsigned int size);
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
#include <crypto/chacha.h>
#include <crypto/blake2s.h>

#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/io.h>

#define CREATE_TRACE_POINTS
#include <trace/events/random.h>

/* #define ADD_INTERRUPT_BENCH */

enum {
	POOL_BITS = BLAKE2S_HASH_SIZE * 8,
	POOL_MIN_BITS = POOL_BITS /* No point in settling for less. */
};

/*
 * Static global variables
 */
static DECLARE_WAIT_QUEUE_HEAD(random_write_wait);
static struct fasync_struct *fasync;

static DEFINE_SPINLOCK(random_ready_list_lock);
static LIST_HEAD(random_ready_list);

struct crng_state {
	u32 state[16];
	unsigned long init_time;
	spinlock_t lock;
};

static struct crng_state primary_crng = {
	.lock = __SPIN_LOCK_UNLOCKED(primary_crng.lock),
	.state[0] = CHACHA_CONSTANT_EXPA,
	.state[1] = CHACHA_CONSTANT_ND_3,
	.state[2] = CHACHA_CONSTANT_2_BY,
	.state[3] = CHACHA_CONSTANT_TE_K,
};

/*
 * crng_init =  0 --> Uninitialized
 *		1 --> Initialized
 *		2 --> Initialized from input_pool
 *
 * crng_init is protected by primary_crng->lock, and only increases
 * its value (from 0->1->2).
 */
static int crng_init = 0;
static bool crng_need_final_init = false;
#define crng_ready() (likely(crng_init > 1))
static int crng_init_cnt = 0;
static unsigned long crng_global_init_time = 0;
#define CRNG_INIT_CNT_THRESH (2 * CHACHA_KEY_SIZE)
static void _extract_crng(struct crng_state *crng, u8 out[CHACHA_BLOCK_SIZE]);
static void _crng_backtrack_protect(struct crng_state *crng,
				    u8 tmp[CHACHA_BLOCK_SIZE], int used);
static void process_random_ready_list(void);
static void _get_random_bytes(void *buf, int nbytes);

static struct ratelimit_state unseeded_warning =
	RATELIMIT_STATE_INIT("warn_unseeded_randomness", HZ, 3);
static struct ratelimit_state urandom_warning =
	RATELIMIT_STATE_INIT("warn_urandom_randomness", HZ, 3);

static int ratelimit_disable __read_mostly;

module_param_named(ratelimit_disable, ratelimit_disable, int, 0644);
MODULE_PARM_DESC(ratelimit_disable, "Disable random ratelimit suppression");

/**********************************************************************
 *
 * OS independent entropy store.   Here are the functions which handle
 * storing entropy in an entropy pool.
 *
 **********************************************************************/

static struct {
	struct blake2s_state hash;
	spinlock_t lock;
	int entropy_count;
} input_pool = {
	.hash.h = { BLAKE2S_IV0 ^ (0x01010000 | BLAKE2S_HASH_SIZE),
		    BLAKE2S_IV1, BLAKE2S_IV2, BLAKE2S_IV3, BLAKE2S_IV4,
		    BLAKE2S_IV5, BLAKE2S_IV6, BLAKE2S_IV7 },
	.hash.outlen = BLAKE2S_HASH_SIZE,
	.lock = __SPIN_LOCK_UNLOCKED(input_pool.lock),
};

static void extract_entropy(void *buf, size_t nbytes);

static void crng_reseed(struct crng_state *crng);

/*
 * This function adds bytes into the entropy "pool".  It does not
 * update the entropy estimate.  The caller should call
 * credit_entropy_bits if this is appropriate.
 */
static void _mix_pool_bytes(const void *in, int nbytes)
{
	blake2s_update(&input_pool.hash, in, nbytes);
}

static void __mix_pool_bytes(const void *in, int nbytes)
{
	trace_mix_pool_bytes_nolock(nbytes, _RET_IP_);
	_mix_pool_bytes(in, nbytes);
}

static void mix_pool_bytes(const void *in, int nbytes)
{
	unsigned long flags;

	trace_mix_pool_bytes(nbytes, _RET_IP_);
	spin_lock_irqsave(&input_pool.lock, flags);
	_mix_pool_bytes(in, nbytes);
	spin_unlock_irqrestore(&input_pool.lock, flags);
}

struct fast_pool {
	u32 pool[4];
	unsigned long last;
	u16 reg_idx;
	u8 count;
};

/*
 * This is a fast mixing routine used by the interrupt randomness
 * collector.  It's hardcoded for an 128 bit pool and assumes that any
 * locks that might be needed are taken by the caller.
 */
static void fast_mix(struct fast_pool *f)
{
	u32 a = f->pool[0],	b = f->pool[1];
	u32 c = f->pool[2],	d = f->pool[3];

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

	f->pool[0] = a;  f->pool[1] = b;
	f->pool[2] = c;  f->pool[3] = d;
	f->count++;
}

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

static void credit_entropy_bits(int nbits)
{
	int entropy_count, orig;

	if (nbits <= 0)
		return;

	nbits = min(nbits, POOL_BITS);

	do {
		orig = READ_ONCE(input_pool.entropy_count);
		entropy_count = min(POOL_BITS, orig + nbits);
	} while (cmpxchg(&input_pool.entropy_count, orig, entropy_count) != orig);

	trace_credit_entropy_bits(nbits, entropy_count, _RET_IP_);

	if (crng_init < 2 && entropy_count >= POOL_MIN_BITS)
		crng_reseed(&primary_crng);
}

/*********************************************************************
 *
 * CRNG using CHACHA20
 *
 *********************************************************************/

#define CRNG_RESEED_INTERVAL (300 * HZ)

static DECLARE_WAIT_QUEUE_HEAD(crng_init_wait);

/*
 * Hack to deal with crazy userspace progams when they are all trying
 * to access /dev/urandom in parallel.  The programs are almost
 * certainly doing something terribly wrong, but we'll work around
 * their brain damage.
 */
static struct crng_state **crng_node_pool __read_mostly;

static void invalidate_batched_entropy(void);
static void numa_crng_init(void);

static bool trust_cpu __ro_after_init = IS_ENABLED(CONFIG_RANDOM_TRUST_CPU);
static int __init parse_trust_cpu(char *arg)
{
	return kstrtobool(arg, &trust_cpu);
}
early_param("random.trust_cpu", parse_trust_cpu);

static bool crng_init_try_arch(struct crng_state *crng)
{
	int i;
	bool arch_init = true;
	unsigned long rv;

	for (i = 4; i < 16; i++) {
		if (!arch_get_random_seed_long(&rv) &&
		    !arch_get_random_long(&rv)) {
			rv = random_get_entropy();
			arch_init = false;
		}
		crng->state[i] ^= rv;
	}

	return arch_init;
}

static bool __init crng_init_try_arch_early(void)
{
	int i;
	bool arch_init = true;
	unsigned long rv;

	for (i = 4; i < 16; i++) {
		if (!arch_get_random_seed_long_early(&rv) &&
		    !arch_get_random_long_early(&rv)) {
			rv = random_get_entropy();
			arch_init = false;
		}
		primary_crng.state[i] ^= rv;
	}

	return arch_init;
}

static void crng_initialize_secondary(struct crng_state *crng)
{
	chacha_init_consts(crng->state);
	_get_random_bytes(&crng->state[4], sizeof(u32) * 12);
	crng_init_try_arch(crng);
	crng->init_time = jiffies - CRNG_RESEED_INTERVAL - 1;
}

static void __init crng_initialize_primary(void)
{
	extract_entropy(&primary_crng.state[4], sizeof(u32) * 12);
	if (crng_init_try_arch_early() && trust_cpu && crng_init < 2) {
		invalidate_batched_entropy();
		numa_crng_init();
		crng_init = 2;
		pr_notice("crng init done (trusting CPU's manufacturer)\n");
	}
	primary_crng.init_time = jiffies - CRNG_RESEED_INTERVAL - 1;
}

static void crng_finalize_init(void)
{
	if (!system_wq) {
		/* We can't call numa_crng_init until we have workqueues,
		 * so mark this for processing later. */
		crng_need_final_init = true;
		return;
	}

	invalidate_batched_entropy();
	numa_crng_init();
	crng_init = 2;
	crng_need_final_init = false;
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

static void do_numa_crng_init(struct work_struct *work)
{
	int i;
	struct crng_state *crng;
	struct crng_state **pool;

	pool = kcalloc(nr_node_ids, sizeof(*pool), GFP_KERNEL | __GFP_NOFAIL);
	for_each_online_node(i) {
		crng = kmalloc_node(sizeof(struct crng_state),
				    GFP_KERNEL | __GFP_NOFAIL, i);
		spin_lock_init(&crng->lock);
		crng_initialize_secondary(crng);
		pool[i] = crng;
	}
	/* pairs with READ_ONCE() in select_crng() */
	if (cmpxchg_release(&crng_node_pool, NULL, pool) != NULL) {
		for_each_node(i)
			kfree(pool[i]);
		kfree(pool);
	}
}

static DECLARE_WORK(numa_crng_init_work, do_numa_crng_init);

static void numa_crng_init(void)
{
	if (IS_ENABLED(CONFIG_NUMA))
		schedule_work(&numa_crng_init_work);
}

static struct crng_state *select_crng(void)
{
	if (IS_ENABLED(CONFIG_NUMA)) {
		struct crng_state **pool;
		int nid = numa_node_id();

		/* pairs with cmpxchg_release() in do_numa_crng_init() */
		pool = READ_ONCE(crng_node_pool);
		if (pool && pool[nid])
			return pool[nid];
	}

	return &primary_crng;
}

/*
 * crng_fast_load() can be called by code in the interrupt service
 * path.  So we can't afford to dilly-dally. Returns the number of
 * bytes processed from cp.
 */
static size_t crng_fast_load(const u8 *cp, size_t len)
{
	unsigned long flags;
	u8 *p;
	size_t ret = 0;

	if (!spin_trylock_irqsave(&primary_crng.lock, flags))
		return 0;
	if (crng_init != 0) {
		spin_unlock_irqrestore(&primary_crng.lock, flags);
		return 0;
	}
	p = (u8 *)&primary_crng.state[4];
	while (len > 0 && crng_init_cnt < CRNG_INIT_CNT_THRESH) {
		p[crng_init_cnt % CHACHA_KEY_SIZE] ^= *cp;
		cp++; crng_init_cnt++; len--; ret++;
	}
	if (crng_init_cnt >= CRNG_INIT_CNT_THRESH) {
		invalidate_batched_entropy();
		crng_init = 1;
	}
	spin_unlock_irqrestore(&primary_crng.lock, flags);
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
 * So we do something more comprehensive which is guaranteed to touch
 * all of the primary_crng's state, and which uses a LFSR with a
 * period of 255 as part of the mixing algorithm.  Finally, we do
 * *not* advance crng_init_cnt since buffer we may get may be something
 * like a fixed DMI table (for example), which might very well be
 * unique to the machine, but is otherwise unvarying.
 */
static int crng_slow_load(const u8 *cp, size_t len)
{
	unsigned long flags;
	static u8 lfsr = 1;
	u8 tmp;
	unsigned int i, max = CHACHA_KEY_SIZE;
	const u8 *src_buf = cp;
	u8 *dest_buf = (u8 *)&primary_crng.state[4];

	if (!spin_trylock_irqsave(&primary_crng.lock, flags))
		return 0;
	if (crng_init != 0) {
		spin_unlock_irqrestore(&primary_crng.lock, flags);
		return 0;
	}
	if (len > max)
		max = len;

	for (i = 0; i < max; i++) {
		tmp = lfsr;
		lfsr >>= 1;
		if (tmp & 1)
			lfsr ^= 0xE1;
		tmp = dest_buf[i % CHACHA_KEY_SIZE];
		dest_buf[i % CHACHA_KEY_SIZE] ^= src_buf[i % len] ^ lfsr;
		lfsr += (tmp << 3) | (tmp >> 5);
	}
	spin_unlock_irqrestore(&primary_crng.lock, flags);
	return 1;
}

static void crng_reseed(struct crng_state *crng)
{
	unsigned long flags;
	int i;
	union {
		u8 block[CHACHA_BLOCK_SIZE];
		u32 key[8];
	} buf;

	if (crng == &primary_crng) {
		int entropy_count;
		do {
			entropy_count = READ_ONCE(input_pool.entropy_count);
			if (entropy_count < POOL_MIN_BITS)
				return;
		} while (cmpxchg(&input_pool.entropy_count, entropy_count, 0) != entropy_count);
		extract_entropy(buf.key, sizeof(buf.key));
		wake_up_interruptible(&random_write_wait);
		kill_fasync(&fasync, SIGIO, POLL_OUT);
	} else {
		_extract_crng(&primary_crng, buf.block);
		_crng_backtrack_protect(&primary_crng, buf.block,
					CHACHA_KEY_SIZE);
	}
	spin_lock_irqsave(&crng->lock, flags);
	for (i = 0; i < 8; i++) {
		unsigned long rv;
		if (!arch_get_random_seed_long(&rv) &&
		    !arch_get_random_long(&rv))
			rv = random_get_entropy();
		crng->state[i + 4] ^= buf.key[i] ^ rv;
	}
	memzero_explicit(&buf, sizeof(buf));
	WRITE_ONCE(crng->init_time, jiffies);
	spin_unlock_irqrestore(&crng->lock, flags);
	if (crng == &primary_crng && crng_init < 2)
		crng_finalize_init();
}

static void _extract_crng(struct crng_state *crng, u8 out[CHACHA_BLOCK_SIZE])
{
	unsigned long flags, init_time;

	if (crng_ready()) {
		init_time = READ_ONCE(crng->init_time);
		if (time_after(READ_ONCE(crng_global_init_time), init_time) ||
		    time_after(jiffies, init_time + CRNG_RESEED_INTERVAL))
			crng_reseed(crng);
	}
	spin_lock_irqsave(&crng->lock, flags);
	chacha20_block(&crng->state[0], out);
	if (crng->state[12] == 0)
		crng->state[13]++;
	spin_unlock_irqrestore(&crng->lock, flags);
}

static void extract_crng(u8 out[CHACHA_BLOCK_SIZE])
{
	_extract_crng(select_crng(), out);
}

/*
 * Use the leftover bytes from the CRNG block output (if there is
 * enough) to mutate the CRNG key to provide backtracking protection.
 */
static void _crng_backtrack_protect(struct crng_state *crng,
				    u8 tmp[CHACHA_BLOCK_SIZE], int used)
{
	unsigned long flags;
	u32 *s, *d;
	int i;

	used = round_up(used, sizeof(u32));
	if (used + CHACHA_KEY_SIZE > CHACHA_BLOCK_SIZE) {
		extract_crng(tmp);
		used = 0;
	}
	spin_lock_irqsave(&crng->lock, flags);
	s = (u32 *)&tmp[used];
	d = &crng->state[4];
	for (i = 0; i < 8; i++)
		*d++ ^= *s++;
	spin_unlock_irqrestore(&crng->lock, flags);
}

static void crng_backtrack_protect(u8 tmp[CHACHA_BLOCK_SIZE], int used)
{
	_crng_backtrack_protect(select_crng(), tmp, used);
}

static ssize_t extract_crng_user(void __user *buf, size_t nbytes)
{
	ssize_t ret = 0, i = CHACHA_BLOCK_SIZE;
	u8 tmp[CHACHA_BLOCK_SIZE] __aligned(4);
	int large_request = (nbytes > 256);

	while (nbytes) {
		if (large_request && need_resched()) {
			if (signal_pending(current)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				break;
			}
			schedule();
		}

		extract_crng(tmp);
		i = min_t(int, nbytes, CHACHA_BLOCK_SIZE);
		if (copy_to_user(buf, tmp, i)) {
			ret = -EFAULT;
			break;
		}

		nbytes -= i;
		buf += i;
		ret += i;
	}
	crng_backtrack_protect(tmp, i);

	/* Wipe data just written to memory */
	memzero_explicit(tmp, sizeof(tmp));

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
void add_device_randomness(const void *buf, unsigned int size)
{
	unsigned long time = random_get_entropy() ^ jiffies;
	unsigned long flags;

	if (!crng_ready() && size)
		crng_slow_load(buf, size);

	trace_add_device_randomness(size, _RET_IP_);
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
static void add_timer_randomness(struct timer_rand_state *state, unsigned num)
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
	credit_entropy_bits(min_t(int, fls(delta >> 1), 11));
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
	trace_add_input_randomness(input_pool.entropy_count);
}
EXPORT_SYMBOL_GPL(add_input_randomness);

static DEFINE_PER_CPU(struct fast_pool, irq_randomness);

#ifdef ADD_INTERRUPT_BENCH
static unsigned long avg_cycles, avg_deviation;

#define AVG_SHIFT 8 /* Exponential average factor k=1/256 */
#define FIXED_1_2 (1 << (AVG_SHIFT - 1))

static void add_interrupt_bench(cycles_t start)
{
	long delta = random_get_entropy() - start;

	/* Use a weighted moving average */
	delta = delta - ((avg_cycles + FIXED_1_2) >> AVG_SHIFT);
	avg_cycles += delta;
	/* And average deviation */
	delta = abs(delta) - ((avg_deviation + FIXED_1_2) >> AVG_SHIFT);
	avg_deviation += delta;
}
#else
#define add_interrupt_bench(x)
#endif

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
	u32 c_high, j_high;
	u64 ip;

	if (cycles == 0)
		cycles = get_reg(fast_pool, regs);
	c_high = (sizeof(cycles) > 4) ? cycles >> 32 : 0;
	j_high = (sizeof(now) > 4) ? now >> 32 : 0;
	fast_pool->pool[0] ^= cycles ^ j_high ^ irq;
	fast_pool->pool[1] ^= now ^ c_high;
	ip = regs ? instruction_pointer(regs) : _RET_IP_;
	fast_pool->pool[2] ^= ip;
	fast_pool->pool[3] ^=
		(sizeof(ip) > 4) ? ip >> 32 : get_reg(fast_pool, regs);

	fast_mix(fast_pool);
	add_interrupt_bench(cycles);

	if (unlikely(crng_init == 0)) {
		if ((fast_pool->count >= 64) &&
		    crng_fast_load((u8 *)fast_pool->pool, sizeof(fast_pool->pool)) > 0) {
			fast_pool->count = 0;
			fast_pool->last = now;
		}
		return;
	}

	if ((fast_pool->count < 64) && !time_after(now, fast_pool->last + HZ))
		return;

	if (!spin_trylock(&input_pool.lock))
		return;

	fast_pool->last = now;
	__mix_pool_bytes(&fast_pool->pool, sizeof(fast_pool->pool));
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
	trace_add_disk_randomness(disk_devt(disk), input_pool.entropy_count);
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
		unsigned long rdrand[32 / sizeof(long)];
		size_t counter;
	} block;
	size_t i;

	trace_extract_entropy(nbytes, input_pool.entropy_count);

	for (i = 0; i < ARRAY_SIZE(block.rdrand); ++i) {
		if (!arch_get_random_long(&block.rdrand[i]))
			block.rdrand[i] = random_get_entropy();
	}

	spin_lock_irqsave(&input_pool.lock, flags);

	/* seed = HASHPRF(last_key, entropy_input) */
	blake2s_final(&input_pool.hash, seed);

	/* next_key = HASHPRF(seed, RDRAND || 0) */
	block.counter = 0;
	blake2s(next_key, (u8 *)&block, seed, sizeof(next_key), sizeof(block), sizeof(seed));
	blake2s_init_key(&input_pool.hash, BLAKE2S_HASH_SIZE, next_key, sizeof(next_key));

	spin_unlock_irqrestore(&input_pool.lock, flags);
	memzero_explicit(next_key, sizeof(next_key));

	while (nbytes) {
		i = min_t(size_t, nbytes, BLAKE2S_HASH_SIZE);
		/* output = HASHPRF(seed, RDRAND || ++counter) */
		++block.counter;
		blake2s(buf, (u8 *)&block, seed, i, sizeof(block), sizeof(seed));
		nbytes -= i;
		buf += i;
	}

	memzero_explicit(seed, sizeof(seed));
	memzero_explicit(&block, sizeof(block));
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
static void _get_random_bytes(void *buf, int nbytes)
{
	u8 tmp[CHACHA_BLOCK_SIZE] __aligned(4);

	trace_get_random_bytes(nbytes, _RET_IP_);

	while (nbytes >= CHACHA_BLOCK_SIZE) {
		extract_crng(buf);
		buf += CHACHA_BLOCK_SIZE;
		nbytes -= CHACHA_BLOCK_SIZE;
	}

	if (nbytes > 0) {
		extract_crng(tmp);
		memcpy(buf, tmp, nbytes);
		crng_backtrack_protect(tmp, nbytes);
	} else
		crng_backtrack_protect(tmp, CHACHA_BLOCK_SIZE);
	memzero_explicit(tmp, sizeof(tmp));
}

void get_random_bytes(void *buf, int nbytes)
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
 * Wait for the urandom pool to be seeded and thus guaranteed to supply
 * cryptographically secure random numbers. This applies to: the /dev/urandom
 * device, the get_random_bytes function, and the get_random_{u32,u64,int,long}
 * family of functions. Using any of these functions without first calling
 * this function forfeits the guarantee of security.
 *
 * Returns: 0 if the urandom pool has been seeded.
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
 * Returns whether or not the urandom pool has been seeded and thus guaranteed
 * to supply cryptographically secure random numbers. This applies to: the
 * /dev/urandom device, the get_random_bytes function, and the get_random_{u32,
 * ,u64,int,long} family of functions.
 *
 * Returns: true if the urandom pool has been seeded.
 *          false if the urandom pool has not been seeded.
 */
bool rng_is_initialized(void)
{
	return crng_ready();
}
EXPORT_SYMBOL(rng_is_initialized);

/*
 * Add a callback function that will be invoked when the nonblocking
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

/*
 * This function will use the architecture-specific hardware random
 * number generator if it is available.  The arch-specific hw RNG will
 * almost certainly be faster than what we can do in software, but it
 * is impossible to verify that it is implemented securely (as
 * opposed, to, say, the AES encryption of a sequence number using a
 * key known by the NSA).  So it's useful if we need the speed, but
 * only if we're willing to trust the hardware manufacturer not to
 * have put in a back door.
 *
 * Return number of bytes filled in.
 */
int __must_check get_random_bytes_arch(void *buf, int nbytes)
{
	int left = nbytes;
	u8 *p = buf;

	trace_get_random_bytes_arch(left, _RET_IP_);
	while (left) {
		unsigned long v;
		int chunk = min_t(int, left, sizeof(unsigned long));

		if (!arch_get_random_long(&v))
			break;

		memcpy(p, &v, chunk);
		p += chunk;
		left -= chunk;
	}

	return nbytes - left;
}
EXPORT_SYMBOL(get_random_bytes_arch);

/*
 * init_std_data - initialize pool with system data
 *
 * This function clears the pool's entropy count and mixes some system
 * data into the pool to prepare it for use. The pool is not cleared
 * as that can only decrease the entropy in the pool.
 */
static void __init init_std_data(void)
{
	int i;
	ktime_t now = ktime_get_real();
	unsigned long rv;

	mix_pool_bytes(&now, sizeof(now));
	for (i = BLAKE2S_BLOCK_SIZE; i > 0; i -= sizeof(rv)) {
		if (!arch_get_random_seed_long(&rv) &&
		    !arch_get_random_long(&rv))
			rv = random_get_entropy();
		mix_pool_bytes(&rv, sizeof(rv));
	}
	mix_pool_bytes(utsname(), sizeof(*(utsname())));
}

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
	init_std_data();
	if (crng_need_final_init)
		crng_finalize_init();
	crng_initialize_primary();
	crng_global_init_time = jiffies;
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

static ssize_t urandom_read_nowarn(struct file *file, char __user *buf,
				   size_t nbytes, loff_t *ppos)
{
	int ret;

	nbytes = min_t(size_t, nbytes, INT_MAX >> 6);
	ret = extract_crng_user(buf, nbytes);
	trace_urandom_read(8 * nbytes, 0, input_pool.entropy_count);
	return ret;
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

	return urandom_read_nowarn(file, buf, nbytes, ppos);
}

static ssize_t random_read(struct file *file, char __user *buf, size_t nbytes,
			   loff_t *ppos)
{
	int ret;

	ret = wait_for_random_bytes();
	if (ret != 0)
		return ret;
	return urandom_read_nowarn(file, buf, nbytes, ppos);
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

static int write_pool(const char __user *buffer, size_t count)
{
	size_t bytes;
	u32 t, buf[16];
	const char __user *p = buffer;

	while (count > 0) {
		int b, i = 0;

		bytes = min(count, sizeof(buf));
		if (copy_from_user(&buf, p, bytes))
			return -EFAULT;

		for (b = bytes; b > 0; b -= sizeof(u32), i++) {
			if (!arch_get_random_int(&t))
				break;
			buf[i] ^= t;
		}

		count -= bytes;
		p += bytes;

		mix_pool_bytes(buf, bytes);
		cond_resched();
	}

	return 0;
}

static ssize_t random_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	size_t ret;

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
		crng_reseed(&primary_crng);
		WRITE_ONCE(crng_global_init_time, jiffies - 1);
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
	int ret;

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
		if (flags & GRND_NONBLOCK)
			return -EAGAIN;
		ret = wait_for_random_bytes();
		if (unlikely(ret))
			return ret;
	}
	return urandom_read_nowarn(NULL, buf, count, NULL);
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
#ifdef ADD_INTERRUPT_BENCH
	{
		.procname	= "add_interrupt_avg_cycles",
		.data		= &avg_cycles,
		.maxlen		= sizeof(avg_cycles),
		.mode		= 0444,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "add_interrupt_avg_deviation",
		.data		= &avg_deviation,
		.maxlen		= sizeof(avg_deviation),
		.mode		= 0444,
		.proc_handler	= proc_doulongvec_minmax,
	},
#endif
	{ }
};
#endif	/* CONFIG_SYSCTL */

static atomic_t batch_generation = ATOMIC_INIT(0);

struct batched_entropy {
	union {
		u64 entropy_u64[CHACHA_BLOCK_SIZE / sizeof(u64)];
		u32 entropy_u32[CHACHA_BLOCK_SIZE / sizeof(u32)];
	};
	local_lock_t lock;
	unsigned int position;
	int generation;
};

/*
 * Get a random word for internal kernel use only. The quality of the random
 * number is good as /dev/urandom, but there is no backtrack protection, with
 * the goal of being quite fast and not depleting entropy. In order to ensure
 * that the randomness provided by this function is okay, the function
 * wait_for_random_bytes() should be called and return 0 at least once at any
 * point prior.
 */
static DEFINE_PER_CPU(struct batched_entropy, batched_entropy_u64) = {
	.lock = INIT_LOCAL_LOCK(batched_entropy_u64.lock)
};

u64 get_random_u64(void)
{
	u64 ret;
	unsigned long flags;
	struct batched_entropy *batch;
	static void *previous;
	int next_gen;

	warn_unseeded_randomness(&previous);

	local_lock_irqsave(&batched_entropy_u64.lock, flags);
	batch = raw_cpu_ptr(&batched_entropy_u64);

	next_gen = atomic_read(&batch_generation);
	if (batch->position % ARRAY_SIZE(batch->entropy_u64) == 0 ||
	    next_gen != batch->generation) {
		extract_crng((u8 *)batch->entropy_u64);
		batch->position = 0;
		batch->generation = next_gen;
	}

	ret = batch->entropy_u64[batch->position++];
	local_unlock_irqrestore(&batched_entropy_u64.lock, flags);
	return ret;
}
EXPORT_SYMBOL(get_random_u64);

static DEFINE_PER_CPU(struct batched_entropy, batched_entropy_u32) = {
	.lock = INIT_LOCAL_LOCK(batched_entropy_u32.lock)
};

u32 get_random_u32(void)
{
	u32 ret;
	unsigned long flags;
	struct batched_entropy *batch;
	static void *previous;
	int next_gen;

	warn_unseeded_randomness(&previous);

	local_lock_irqsave(&batched_entropy_u32.lock, flags);
	batch = raw_cpu_ptr(&batched_entropy_u32);

	next_gen = atomic_read(&batch_generation);
	if (batch->position % ARRAY_SIZE(batch->entropy_u32) == 0 ||
	    next_gen != batch->generation) {
		extract_crng((u8 *)batch->entropy_u32);
		batch->position = 0;
		batch->generation = next_gen;
	}

	ret = batch->entropy_u32[batch->position++];
	local_unlock_irqrestore(&batched_entropy_u32.lock, flags);
	return ret;
}
EXPORT_SYMBOL(get_random_u32);

/* It's important to invalidate all potential batched entropy that might
 * be stored before the crng is initialized, which we can do lazily by
 * bumping the generation counter.
 */
static void invalidate_batched_entropy(void)
{
	atomic_inc(&batch_generation);
}

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
void add_hwgenerator_randomness(const char *buffer, size_t count,
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
void add_bootloader_randomness(const void *buf, unsigned int size)
{
	if (IS_ENABLED(CONFIG_RANDOM_TRUST_BOOTLOADER))
		add_hwgenerator_randomness(buf, size, size * 8);
	else
		add_device_randomness(buf, size);
}
EXPORT_SYMBOL_GPL(add_bootloader_randomness);
