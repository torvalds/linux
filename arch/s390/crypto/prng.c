/*
 * Copyright IBM Corp. 2006, 2015
 * Author(s): Jan Glauber <jan.glauber@de.ibm.com>
 *	      Harald Freudenberger <freude@de.ibm.com>
 * Driver for the s390 pseudo random number generator
 */

#define KMSG_COMPONENT "prng"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/fs.h>
#include <linux/fips.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/cpufeature.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>

#include <asm/debug.h>
#include <linux/uaccess.h>
#include <asm/timex.h>
#include <asm/cpacf.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 PRNG interface");


#define PRNG_MODE_AUTO	  0
#define PRNG_MODE_TDES	  1
#define PRNG_MODE_SHA512  2

static unsigned int prng_mode = PRNG_MODE_AUTO;
module_param_named(mode, prng_mode, int, 0);
MODULE_PARM_DESC(prng_mode, "PRNG mode: 0 - auto, 1 - TDES, 2 - SHA512");


#define PRNG_CHUNKSIZE_TDES_MIN   8
#define PRNG_CHUNKSIZE_TDES_MAX   (64*1024)
#define PRNG_CHUNKSIZE_SHA512_MIN 64
#define PRNG_CHUNKSIZE_SHA512_MAX (64*1024)

static unsigned int prng_chunk_size = 256;
module_param_named(chunksize, prng_chunk_size, int, 0);
MODULE_PARM_DESC(prng_chunk_size, "PRNG read chunk size in bytes");


#define PRNG_RESEED_LIMIT_TDES		 4096
#define PRNG_RESEED_LIMIT_TDES_LOWER	 4096
#define PRNG_RESEED_LIMIT_SHA512       100000
#define PRNG_RESEED_LIMIT_SHA512_LOWER	10000

static unsigned int prng_reseed_limit;
module_param_named(reseed_limit, prng_reseed_limit, int, 0);
MODULE_PARM_DESC(prng_reseed_limit, "PRNG reseed limit");


/*
 * Any one who considers arithmetical methods of producing random digits is,
 * of course, in a state of sin. -- John von Neumann
 */

static int prng_errorflag;

#define PRNG_GEN_ENTROPY_FAILED  1
#define PRNG_SELFTEST_FAILED	 2
#define PRNG_INSTANTIATE_FAILED  3
#define PRNG_SEED_FAILED	 4
#define PRNG_RESEED_FAILED	 5
#define PRNG_GEN_FAILED		 6

struct prng_ws_s {
	u8  parm_block[32];
	u32 reseed_counter;
	u64 byte_counter;
};

struct ppno_ws_s {
	u32 res;
	u32 reseed_counter;
	u64 stream_bytes;
	u8  V[112];
	u8  C[112];
};

struct prng_data_s {
	struct mutex mutex;
	union {
		struct prng_ws_s prngws;
		struct ppno_ws_s ppnows;
	};
	u8 *buf;
	u32 rest;
	u8 *prev;
};

static struct prng_data_s *prng_data;

/* initial parameter block for tdes mode, copied from libica */
static const u8 initial_parm_block[32] __initconst = {
	0x0F, 0x2B, 0x8E, 0x63, 0x8C, 0x8E, 0xD2, 0x52,
	0x64, 0xB7, 0xA0, 0x7B, 0x75, 0x28, 0xB8, 0xF4,
	0x75, 0x5F, 0xD2, 0xA6, 0x8D, 0x97, 0x11, 0xFF,
	0x49, 0xD8, 0x23, 0xF3, 0x7E, 0x21, 0xEC, 0xA0 };


/*** helper functions ***/

/*
 * generate_entropy:
 * This algorithm produces 64 bytes of entropy data based on 1024
 * individual stckf() invocations assuming that each stckf() value
 * contributes 0.25 bits of entropy. So the caller gets 256 bit
 * entropy per 64 byte or 4 bits entropy per byte.
 */
static int generate_entropy(u8 *ebuf, size_t nbytes)
{
	int n, ret = 0;
	u8 *pg, *h, hash[64];

	/* allocate 2 pages */
	pg = (u8 *) __get_free_pages(GFP_KERNEL, 1);
	if (!pg) {
		prng_errorflag = PRNG_GEN_ENTROPY_FAILED;
		return -ENOMEM;
	}

	while (nbytes) {
		/* fill pages with urandom bytes */
		get_random_bytes(pg, 2*PAGE_SIZE);
		/* exor pages with 1024 stckf values */
		for (n = 0; n < 2 * PAGE_SIZE / sizeof(u64); n++) {
			u64 *p = ((u64 *)pg) + n;
			*p ^= get_tod_clock_fast();
		}
		n = (nbytes < sizeof(hash)) ? nbytes : sizeof(hash);
		if (n < sizeof(hash))
			h = hash;
		else
			h = ebuf;
		/* hash over the filled pages */
		cpacf_kimd(CPACF_KIMD_SHA_512, h, pg, 2*PAGE_SIZE);
		if (n < sizeof(hash))
			memcpy(ebuf, hash, n);
		ret += n;
		ebuf += n;
		nbytes -= n;
	}

	free_pages((unsigned long)pg, 1);
	return ret;
}


/*** tdes functions ***/

static void prng_tdes_add_entropy(void)
{
	__u64 entropy[4];
	unsigned int i;

	for (i = 0; i < 16; i++) {
		cpacf_kmc(CPACF_KMC_PRNG, prng_data->prngws.parm_block,
			  (char *) entropy, (char *) entropy,
			  sizeof(entropy));
		memcpy(prng_data->prngws.parm_block, entropy, sizeof(entropy));
	}
}


static void prng_tdes_seed(int nbytes)
{
	char buf[16];
	int i = 0;

	BUG_ON(nbytes > sizeof(buf));

	get_random_bytes(buf, nbytes);

	/* Add the entropy */
	while (nbytes >= 8) {
		*((__u64 *)prng_data->prngws.parm_block) ^= *((__u64 *)(buf+i));
		prng_tdes_add_entropy();
		i += 8;
		nbytes -= 8;
	}
	prng_tdes_add_entropy();
	prng_data->prngws.reseed_counter = 0;
}


static int __init prng_tdes_instantiate(void)
{
	int datalen;

	pr_debug("prng runs in TDES mode with "
		 "chunksize=%d and reseed_limit=%u\n",
		 prng_chunk_size, prng_reseed_limit);

	/* memory allocation, prng_data struct init, mutex init */
	datalen = sizeof(struct prng_data_s) + prng_chunk_size;
	prng_data = kzalloc(datalen, GFP_KERNEL);
	if (!prng_data) {
		prng_errorflag = PRNG_INSTANTIATE_FAILED;
		return -ENOMEM;
	}
	mutex_init(&prng_data->mutex);
	prng_data->buf = ((u8 *)prng_data) + sizeof(struct prng_data_s);
	memcpy(prng_data->prngws.parm_block, initial_parm_block, 32);

	/* initialize the PRNG, add 128 bits of entropy */
	prng_tdes_seed(16);

	return 0;
}


static void prng_tdes_deinstantiate(void)
{
	pr_debug("The prng module stopped "
		 "after running in triple DES mode\n");
	kzfree(prng_data);
}


/*** sha512 functions ***/

static int __init prng_sha512_selftest(void)
{
	/* NIST DRBG testvector for Hash Drbg, Sha-512, Count #0 */
	static const u8 seed[] __initconst = {
		0x6b, 0x50, 0xa7, 0xd8, 0xf8, 0xa5, 0x5d, 0x7a,
		0x3d, 0xf8, 0xbb, 0x40, 0xbc, 0xc3, 0xb7, 0x22,
		0xd8, 0x70, 0x8d, 0xe6, 0x7f, 0xda, 0x01, 0x0b,
		0x03, 0xc4, 0xc8, 0x4d, 0x72, 0x09, 0x6f, 0x8c,
		0x3e, 0xc6, 0x49, 0xcc, 0x62, 0x56, 0xd9, 0xfa,
		0x31, 0xdb, 0x7a, 0x29, 0x04, 0xaa, 0xf0, 0x25 };
	static const u8 V0[] __initconst = {
		0x00, 0xad, 0xe3, 0x6f, 0x9a, 0x01, 0xc7, 0x76,
		0x61, 0x34, 0x35, 0xf5, 0x4e, 0x24, 0x74, 0x22,
		0x21, 0x9a, 0x29, 0x89, 0xc7, 0x93, 0x2e, 0x60,
		0x1e, 0xe8, 0x14, 0x24, 0x8d, 0xd5, 0x03, 0xf1,
		0x65, 0x5d, 0x08, 0x22, 0x72, 0xd5, 0xad, 0x95,
		0xe1, 0x23, 0x1e, 0x8a, 0xa7, 0x13, 0xd9, 0x2b,
		0x5e, 0xbc, 0xbb, 0x80, 0xab, 0x8d, 0xe5, 0x79,
		0xab, 0x5b, 0x47, 0x4e, 0xdd, 0xee, 0x6b, 0x03,
		0x8f, 0x0f, 0x5c, 0x5e, 0xa9, 0x1a, 0x83, 0xdd,
		0xd3, 0x88, 0xb2, 0x75, 0x4b, 0xce, 0x83, 0x36,
		0x57, 0x4b, 0xf1, 0x5c, 0xca, 0x7e, 0x09, 0xc0,
		0xd3, 0x89, 0xc6, 0xe0, 0xda, 0xc4, 0x81, 0x7e,
		0x5b, 0xf9, 0xe1, 0x01, 0xc1, 0x92, 0x05, 0xea,
		0xf5, 0x2f, 0xc6, 0xc6, 0xc7, 0x8f, 0xbc, 0xf4 };
	static const u8 C0[] __initconst = {
		0x00, 0xf4, 0xa3, 0xe5, 0xa0, 0x72, 0x63, 0x95,
		0xc6, 0x4f, 0x48, 0xd0, 0x8b, 0x5b, 0x5f, 0x8e,
		0x6b, 0x96, 0x1f, 0x16, 0xed, 0xbc, 0x66, 0x94,
		0x45, 0x31, 0xd7, 0x47, 0x73, 0x22, 0xa5, 0x86,
		0xce, 0xc0, 0x4c, 0xac, 0x63, 0xb8, 0x39, 0x50,
		0xbf, 0xe6, 0x59, 0x6c, 0x38, 0x58, 0x99, 0x1f,
		0x27, 0xa7, 0x9d, 0x71, 0x2a, 0xb3, 0x7b, 0xf9,
		0xfb, 0x17, 0x86, 0xaa, 0x99, 0x81, 0xaa, 0x43,
		0xe4, 0x37, 0xd3, 0x1e, 0x6e, 0xe5, 0xe6, 0xee,
		0xc2, 0xed, 0x95, 0x4f, 0x53, 0x0e, 0x46, 0x8a,
		0xcc, 0x45, 0xa5, 0xdb, 0x69, 0x0d, 0x81, 0xc9,
		0x32, 0x92, 0xbc, 0x8f, 0x33, 0xe6, 0xf6, 0x09,
		0x7c, 0x8e, 0x05, 0x19, 0x0d, 0xf1, 0xb6, 0xcc,
		0xf3, 0x02, 0x21, 0x90, 0x25, 0xec, 0xed, 0x0e };
	static const u8 random[] __initconst = {
		0x95, 0xb7, 0xf1, 0x7e, 0x98, 0x02, 0xd3, 0x57,
		0x73, 0x92, 0xc6, 0xa9, 0xc0, 0x80, 0x83, 0xb6,
		0x7d, 0xd1, 0x29, 0x22, 0x65, 0xb5, 0xf4, 0x2d,
		0x23, 0x7f, 0x1c, 0x55, 0xbb, 0x9b, 0x10, 0xbf,
		0xcf, 0xd8, 0x2c, 0x77, 0xa3, 0x78, 0xb8, 0x26,
		0x6a, 0x00, 0x99, 0x14, 0x3b, 0x3c, 0x2d, 0x64,
		0x61, 0x1e, 0xee, 0xb6, 0x9a, 0xcd, 0xc0, 0x55,
		0x95, 0x7c, 0x13, 0x9e, 0x8b, 0x19, 0x0c, 0x7a,
		0x06, 0x95, 0x5f, 0x2c, 0x79, 0x7c, 0x27, 0x78,
		0xde, 0x94, 0x03, 0x96, 0xa5, 0x01, 0xf4, 0x0e,
		0x91, 0x39, 0x6a, 0xcf, 0x8d, 0x7e, 0x45, 0xeb,
		0xdb, 0xb5, 0x3b, 0xbf, 0x8c, 0x97, 0x52, 0x30,
		0xd2, 0xf0, 0xff, 0x91, 0x06, 0xc7, 0x61, 0x19,
		0xae, 0x49, 0x8e, 0x7f, 0xbc, 0x03, 0xd9, 0x0f,
		0x8e, 0x4c, 0x51, 0x62, 0x7a, 0xed, 0x5c, 0x8d,
		0x42, 0x63, 0xd5, 0xd2, 0xb9, 0x78, 0x87, 0x3a,
		0x0d, 0xe5, 0x96, 0xee, 0x6d, 0xc7, 0xf7, 0xc2,
		0x9e, 0x37, 0xee, 0xe8, 0xb3, 0x4c, 0x90, 0xdd,
		0x1c, 0xf6, 0xa9, 0xdd, 0xb2, 0x2b, 0x4c, 0xbd,
		0x08, 0x6b, 0x14, 0xb3, 0x5d, 0xe9, 0x3d, 0xa2,
		0xd5, 0xcb, 0x18, 0x06, 0x69, 0x8c, 0xbd, 0x7b,
		0xbb, 0x67, 0xbf, 0xe3, 0xd3, 0x1f, 0xd2, 0xd1,
		0xdb, 0xd2, 0xa1, 0xe0, 0x58, 0xa3, 0xeb, 0x99,
		0xd7, 0xe5, 0x1f, 0x1a, 0x93, 0x8e, 0xed, 0x5e,
		0x1c, 0x1d, 0xe2, 0x3a, 0x6b, 0x43, 0x45, 0xd3,
		0x19, 0x14, 0x09, 0xf9, 0x2f, 0x39, 0xb3, 0x67,
		0x0d, 0x8d, 0xbf, 0xb6, 0x35, 0xd8, 0xe6, 0xa3,
		0x69, 0x32, 0xd8, 0x10, 0x33, 0xd1, 0x44, 0x8d,
		0x63, 0xb4, 0x03, 0xdd, 0xf8, 0x8e, 0x12, 0x1b,
		0x6e, 0x81, 0x9a, 0xc3, 0x81, 0x22, 0x6c, 0x13,
		0x21, 0xe4, 0xb0, 0x86, 0x44, 0xf6, 0x72, 0x7c,
		0x36, 0x8c, 0x5a, 0x9f, 0x7a, 0x4b, 0x3e, 0xe2 };

	u8 buf[sizeof(random)];
	struct ppno_ws_s ws;

	memset(&ws, 0, sizeof(ws));

	/* initial seed */
	cpacf_ppno(CPACF_PPNO_SHA512_DRNG_SEED,
		   &ws, NULL, 0, seed, sizeof(seed));

	/* check working states V and C */
	if (memcmp(ws.V, V0, sizeof(V0)) != 0
	    || memcmp(ws.C, C0, sizeof(C0)) != 0) {
		pr_err("The prng self test state test "
		       "for the SHA-512 mode failed\n");
		prng_errorflag = PRNG_SELFTEST_FAILED;
		return -EIO;
	}

	/* generate random bytes */
	cpacf_ppno(CPACF_PPNO_SHA512_DRNG_GEN,
		   &ws, buf, sizeof(buf), NULL, 0);
	cpacf_ppno(CPACF_PPNO_SHA512_DRNG_GEN,
		   &ws, buf, sizeof(buf), NULL, 0);

	/* check against expected data */
	if (memcmp(buf, random, sizeof(random)) != 0) {
		pr_err("The prng self test data test "
		       "for the SHA-512 mode failed\n");
		prng_errorflag = PRNG_SELFTEST_FAILED;
		return -EIO;
	}

	return 0;
}


static int __init prng_sha512_instantiate(void)
{
	int ret, datalen;
	u8 seed[64 + 32 + 16];

	pr_debug("prng runs in SHA-512 mode "
		 "with chunksize=%d and reseed_limit=%u\n",
		 prng_chunk_size, prng_reseed_limit);

	/* memory allocation, prng_data struct init, mutex init */
	datalen = sizeof(struct prng_data_s) + prng_chunk_size;
	if (fips_enabled)
		datalen += prng_chunk_size;
	prng_data = kzalloc(datalen, GFP_KERNEL);
	if (!prng_data) {
		prng_errorflag = PRNG_INSTANTIATE_FAILED;
		return -ENOMEM;
	}
	mutex_init(&prng_data->mutex);
	prng_data->buf = ((u8 *)prng_data) + sizeof(struct prng_data_s);

	/* selftest */
	ret = prng_sha512_selftest();
	if (ret)
		goto outfree;

	/* generate initial seed bytestring, with 256 + 128 bits entropy */
	ret = generate_entropy(seed, 64 + 32);
	if (ret != 64 + 32)
		goto outfree;
	/* followed by 16 bytes of unique nonce */
	get_tod_clock_ext(seed + 64 + 32);

	/* initial seed of the ppno drng */
	cpacf_ppno(CPACF_PPNO_SHA512_DRNG_SEED,
		   &prng_data->ppnows, NULL, 0, seed, sizeof(seed));

	/* if fips mode is enabled, generate a first block of random
	   bytes for the FIPS 140-2 Conditional Self Test */
	if (fips_enabled) {
		prng_data->prev = prng_data->buf + prng_chunk_size;
		cpacf_ppno(CPACF_PPNO_SHA512_DRNG_GEN,
			   &prng_data->ppnows,
			   prng_data->prev, prng_chunk_size, NULL, 0);
	}

	return 0;

outfree:
	kfree(prng_data);
	return ret;
}


static void prng_sha512_deinstantiate(void)
{
	pr_debug("The prng module stopped after running in SHA-512 mode\n");
	kzfree(prng_data);
}


static int prng_sha512_reseed(void)
{
	int ret;
	u8 seed[64];

	/* fetch 256 bits of fresh entropy */
	ret = generate_entropy(seed, sizeof(seed));
	if (ret != sizeof(seed))
		return ret;

	/* do a reseed of the ppno drng with this bytestring */
	cpacf_ppno(CPACF_PPNO_SHA512_DRNG_SEED,
		   &prng_data->ppnows, NULL, 0, seed, sizeof(seed));

	return 0;
}


static int prng_sha512_generate(u8 *buf, size_t nbytes)
{
	int ret;

	/* reseed needed ? */
	if (prng_data->ppnows.reseed_counter > prng_reseed_limit) {
		ret = prng_sha512_reseed();
		if (ret)
			return ret;
	}

	/* PPNO generate */
	cpacf_ppno(CPACF_PPNO_SHA512_DRNG_GEN,
		   &prng_data->ppnows, buf, nbytes, NULL, 0);

	/* FIPS 140-2 Conditional Self Test */
	if (fips_enabled) {
		if (!memcmp(prng_data->prev, buf, nbytes)) {
			prng_errorflag = PRNG_GEN_FAILED;
			return -EILSEQ;
		}
		memcpy(prng_data->prev, buf, nbytes);
	}

	return nbytes;
}


/*** file io functions ***/

static int prng_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}


static ssize_t prng_tdes_read(struct file *file, char __user *ubuf,
			      size_t nbytes, loff_t *ppos)
{
	int chunk, n, ret = 0;

	/* lock prng_data struct */
	if (mutex_lock_interruptible(&prng_data->mutex))
		return -ERESTARTSYS;

	while (nbytes) {
		if (need_resched()) {
			if (signal_pending(current)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				break;
			}
			/* give mutex free before calling schedule() */
			mutex_unlock(&prng_data->mutex);
			schedule();
			/* occopy mutex again */
			if (mutex_lock_interruptible(&prng_data->mutex)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				return ret;
			}
		}

		/*
		 * we lose some random bytes if an attacker issues
		 * reads < 8 bytes, but we don't care
		 */
		chunk = min_t(int, nbytes, prng_chunk_size);

		/* PRNG only likes multiples of 8 bytes */
		n = (chunk + 7) & -8;

		if (prng_data->prngws.reseed_counter > prng_reseed_limit)
			prng_tdes_seed(8);

		/* if the CPU supports PRNG stckf is present too */
		*((unsigned long long *)prng_data->buf) = get_tod_clock_fast();

		/*
		 * Beside the STCKF the input for the TDES-EDE is the output
		 * of the last operation. We differ here from X9.17 since we
		 * only store one timestamp into the buffer. Padding the whole
		 * buffer with timestamps does not improve security, since
		 * successive stckf have nearly constant offsets.
		 * If an attacker knows the first timestamp it would be
		 * trivial to guess the additional values. One timestamp
		 * is therefore enough and still guarantees unique input values.
		 *
		 * Note: you can still get strict X9.17 conformity by setting
		 * prng_chunk_size to 8 bytes.
		 */
		cpacf_kmc(CPACF_KMC_PRNG, prng_data->prngws.parm_block,
			  prng_data->buf, prng_data->buf, n);

		prng_data->prngws.byte_counter += n;
		prng_data->prngws.reseed_counter += n;

		if (copy_to_user(ubuf, prng_data->buf, chunk)) {
			ret = -EFAULT;
			break;
		}

		nbytes -= chunk;
		ret += chunk;
		ubuf += chunk;
	}

	/* unlock prng_data struct */
	mutex_unlock(&prng_data->mutex);

	return ret;
}


static ssize_t prng_sha512_read(struct file *file, char __user *ubuf,
				size_t nbytes, loff_t *ppos)
{
	int n, ret = 0;
	u8 *p;

	/* if errorflag is set do nothing and return 'broken pipe' */
	if (prng_errorflag)
		return -EPIPE;

	/* lock prng_data struct */
	if (mutex_lock_interruptible(&prng_data->mutex))
		return -ERESTARTSYS;

	while (nbytes) {
		if (need_resched()) {
			if (signal_pending(current)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				break;
			}
			/* give mutex free before calling schedule() */
			mutex_unlock(&prng_data->mutex);
			schedule();
			/* occopy mutex again */
			if (mutex_lock_interruptible(&prng_data->mutex)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				return ret;
			}
		}
		if (prng_data->rest) {
			/* push left over random bytes from the previous read */
			p = prng_data->buf + prng_chunk_size - prng_data->rest;
			n = (nbytes < prng_data->rest) ?
				nbytes : prng_data->rest;
			prng_data->rest -= n;
		} else {
			/* generate one chunk of random bytes into read buf */
			p = prng_data->buf;
			n = prng_sha512_generate(p, prng_chunk_size);
			if (n < 0) {
				ret = n;
				break;
			}
			if (nbytes < prng_chunk_size) {
				n = nbytes;
				prng_data->rest = prng_chunk_size - n;
			} else {
				n = prng_chunk_size;
				prng_data->rest = 0;
			}
		}
		if (copy_to_user(ubuf, p, n)) {
			ret = -EFAULT;
			break;
		}
		ubuf += n;
		nbytes -= n;
		ret += n;
	}

	/* unlock prng_data struct */
	mutex_unlock(&prng_data->mutex);

	return ret;
}


/*** sysfs stuff ***/

static const struct file_operations prng_sha512_fops = {
	.owner		= THIS_MODULE,
	.open		= &prng_open,
	.release	= NULL,
	.read		= &prng_sha512_read,
	.llseek		= noop_llseek,
};
static const struct file_operations prng_tdes_fops = {
	.owner		= THIS_MODULE,
	.open		= &prng_open,
	.release	= NULL,
	.read		= &prng_tdes_read,
	.llseek		= noop_llseek,
};

static struct miscdevice prng_sha512_dev = {
	.name	= "prandom",
	.minor	= MISC_DYNAMIC_MINOR,
	.mode	= 0644,
	.fops	= &prng_sha512_fops,
};
static struct miscdevice prng_tdes_dev = {
	.name	= "prandom",
	.minor	= MISC_DYNAMIC_MINOR,
	.mode	= 0644,
	.fops	= &prng_tdes_fops,
};


/* chunksize attribute (ro) */
static ssize_t prng_chunksize_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", prng_chunk_size);
}
static DEVICE_ATTR(chunksize, 0444, prng_chunksize_show, NULL);

/* counter attribute (ro) */
static ssize_t prng_counter_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	u64 counter;

	if (mutex_lock_interruptible(&prng_data->mutex))
		return -ERESTARTSYS;
	if (prng_mode == PRNG_MODE_SHA512)
		counter = prng_data->ppnows.stream_bytes;
	else
		counter = prng_data->prngws.byte_counter;
	mutex_unlock(&prng_data->mutex);

	return snprintf(buf, PAGE_SIZE, "%llu\n", counter);
}
static DEVICE_ATTR(byte_counter, 0444, prng_counter_show, NULL);

/* errorflag attribute (ro) */
static ssize_t prng_errorflag_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", prng_errorflag);
}
static DEVICE_ATTR(errorflag, 0444, prng_errorflag_show, NULL);

/* mode attribute (ro) */
static ssize_t prng_mode_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	if (prng_mode == PRNG_MODE_TDES)
		return snprintf(buf, PAGE_SIZE, "TDES\n");
	else
		return snprintf(buf, PAGE_SIZE, "SHA512\n");
}
static DEVICE_ATTR(mode, 0444, prng_mode_show, NULL);

/* reseed attribute (w) */
static ssize_t prng_reseed_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	if (mutex_lock_interruptible(&prng_data->mutex))
		return -ERESTARTSYS;
	prng_sha512_reseed();
	mutex_unlock(&prng_data->mutex);

	return count;
}
static DEVICE_ATTR(reseed, 0200, NULL, prng_reseed_store);

/* reseed limit attribute (rw) */
static ssize_t prng_reseed_limit_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", prng_reseed_limit);
}
static ssize_t prng_reseed_limit_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	unsigned limit;

	if (sscanf(buf, "%u\n", &limit) != 1)
		return -EINVAL;

	if (prng_mode == PRNG_MODE_SHA512) {
		if (limit < PRNG_RESEED_LIMIT_SHA512_LOWER)
			return -EINVAL;
	} else {
		if (limit < PRNG_RESEED_LIMIT_TDES_LOWER)
			return -EINVAL;
	}

	prng_reseed_limit = limit;

	return count;
}
static DEVICE_ATTR(reseed_limit, 0644,
		   prng_reseed_limit_show, prng_reseed_limit_store);

/* strength attribute (ro) */
static ssize_t prng_strength_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	return snprintf(buf, PAGE_SIZE, "256\n");
}
static DEVICE_ATTR(strength, 0444, prng_strength_show, NULL);

static struct attribute *prng_sha512_dev_attrs[] = {
	&dev_attr_errorflag.attr,
	&dev_attr_chunksize.attr,
	&dev_attr_byte_counter.attr,
	&dev_attr_mode.attr,
	&dev_attr_reseed.attr,
	&dev_attr_reseed_limit.attr,
	&dev_attr_strength.attr,
	NULL
};
static struct attribute *prng_tdes_dev_attrs[] = {
	&dev_attr_chunksize.attr,
	&dev_attr_byte_counter.attr,
	&dev_attr_mode.attr,
	NULL
};

static struct attribute_group prng_sha512_dev_attr_group = {
	.attrs = prng_sha512_dev_attrs
};
static struct attribute_group prng_tdes_dev_attr_group = {
	.attrs = prng_tdes_dev_attrs
};


/*** module init and exit ***/

static int __init prng_init(void)
{
	int ret;

	/* check if the CPU has a PRNG */
	if (!cpacf_query_func(CPACF_KMC, CPACF_KMC_PRNG))
		return -EOPNOTSUPP;

	/* choose prng mode */
	if (prng_mode != PRNG_MODE_TDES) {
		/* check for MSA5 support for PPNO operations */
		if (!cpacf_query_func(CPACF_PPNO, CPACF_PPNO_SHA512_DRNG_GEN)) {
			if (prng_mode == PRNG_MODE_SHA512) {
				pr_err("The prng module cannot "
				       "start in SHA-512 mode\n");
				return -EOPNOTSUPP;
			}
			prng_mode = PRNG_MODE_TDES;
		} else
			prng_mode = PRNG_MODE_SHA512;
	}

	if (prng_mode == PRNG_MODE_SHA512) {

		/* SHA512 mode */

		if (prng_chunk_size < PRNG_CHUNKSIZE_SHA512_MIN
		    || prng_chunk_size > PRNG_CHUNKSIZE_SHA512_MAX)
			return -EINVAL;
		prng_chunk_size = (prng_chunk_size + 0x3f) & ~0x3f;

		if (prng_reseed_limit == 0)
			prng_reseed_limit = PRNG_RESEED_LIMIT_SHA512;
		else if (prng_reseed_limit < PRNG_RESEED_LIMIT_SHA512_LOWER)
			return -EINVAL;

		ret = prng_sha512_instantiate();
		if (ret)
			goto out;

		ret = misc_register(&prng_sha512_dev);
		if (ret) {
			prng_sha512_deinstantiate();
			goto out;
		}
		ret = sysfs_create_group(&prng_sha512_dev.this_device->kobj,
					 &prng_sha512_dev_attr_group);
		if (ret) {
			misc_deregister(&prng_sha512_dev);
			prng_sha512_deinstantiate();
			goto out;
		}

	} else {

		/* TDES mode */

		if (prng_chunk_size < PRNG_CHUNKSIZE_TDES_MIN
		    || prng_chunk_size > PRNG_CHUNKSIZE_TDES_MAX)
			return -EINVAL;
		prng_chunk_size = (prng_chunk_size + 0x07) & ~0x07;

		if (prng_reseed_limit == 0)
			prng_reseed_limit = PRNG_RESEED_LIMIT_TDES;
		else if (prng_reseed_limit < PRNG_RESEED_LIMIT_TDES_LOWER)
			return -EINVAL;

		ret = prng_tdes_instantiate();
		if (ret)
			goto out;

		ret = misc_register(&prng_tdes_dev);
		if (ret) {
			prng_tdes_deinstantiate();
			goto out;
		}
		ret = sysfs_create_group(&prng_tdes_dev.this_device->kobj,
					 &prng_tdes_dev_attr_group);
		if (ret) {
			misc_deregister(&prng_tdes_dev);
			prng_tdes_deinstantiate();
			goto out;
		}

	}

out:
	return ret;
}


static void __exit prng_exit(void)
{
	if (prng_mode == PRNG_MODE_SHA512) {
		sysfs_remove_group(&prng_sha512_dev.this_device->kobj,
				   &prng_sha512_dev_attr_group);
		misc_deregister(&prng_sha512_dev);
		prng_sha512_deinstantiate();
	} else {
		sysfs_remove_group(&prng_tdes_dev.this_device->kobj,
				   &prng_tdes_dev_attr_group);
		misc_deregister(&prng_tdes_dev);
		prng_tdes_deinstantiate();
	}
}

module_cpu_feature_match(MSA, prng_init);
module_exit(prng_exit);
