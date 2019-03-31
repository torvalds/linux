/*
 * Algorithm testing framework and tests.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) 2007 Nokia Siemens Networks
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 * Copyright (c) 2019 Google LLC
 *
 * Updated RFC4106 AES-GCM testing.
 *    Authors: Aidan O'Mahony (aidan.o.mahony@intel.com)
 *             Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Tadeusz Struk (tadeusz.struk@intel.com)
 *    Copyright (c) 2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/aead.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/err.h>
#include <linux/fips.h>
#include <linux/module.h>
#include <linux/once.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <crypto/rng.h>
#include <crypto/drbg.h>
#include <crypto/akcipher.h>
#include <crypto/kpp.h>
#include <crypto/acompress.h>
#include <crypto/internal/simd.h>

#include "internal.h"

static bool notests;
module_param(notests, bool, 0644);
MODULE_PARM_DESC(notests, "disable crypto self-tests");

static bool panic_on_fail;
module_param(panic_on_fail, bool, 0444);

#ifdef CONFIG_CRYPTO_MANAGER_EXTRA_TESTS
static bool noextratests;
module_param(noextratests, bool, 0644);
MODULE_PARM_DESC(noextratests, "disable expensive crypto self-tests");

static unsigned int fuzz_iterations = 100;
module_param(fuzz_iterations, uint, 0644);
MODULE_PARM_DESC(fuzz_iterations, "number of fuzz test iterations");

DEFINE_PER_CPU(bool, crypto_simd_disabled_for_test);
EXPORT_PER_CPU_SYMBOL_GPL(crypto_simd_disabled_for_test);
#endif

#ifdef CONFIG_CRYPTO_MANAGER_DISABLE_TESTS

/* a perfect nop */
int alg_test(const char *driver, const char *alg, u32 type, u32 mask)
{
	return 0;
}

#else

#include "testmgr.h"

/*
 * Need slab memory for testing (size in number of pages).
 */
#define XBUFSIZE	8

/*
* Used by test_cipher()
*/
#define ENCRYPT 1
#define DECRYPT 0

struct aead_test_suite {
	const struct aead_testvec *vecs;
	unsigned int count;
};

struct cipher_test_suite {
	const struct cipher_testvec *vecs;
	unsigned int count;
};

struct comp_test_suite {
	struct {
		const struct comp_testvec *vecs;
		unsigned int count;
	} comp, decomp;
};

struct hash_test_suite {
	const struct hash_testvec *vecs;
	unsigned int count;
};

struct cprng_test_suite {
	const struct cprng_testvec *vecs;
	unsigned int count;
};

struct drbg_test_suite {
	const struct drbg_testvec *vecs;
	unsigned int count;
};

struct akcipher_test_suite {
	const struct akcipher_testvec *vecs;
	unsigned int count;
};

struct kpp_test_suite {
	const struct kpp_testvec *vecs;
	unsigned int count;
};

struct alg_test_desc {
	const char *alg;
	int (*test)(const struct alg_test_desc *desc, const char *driver,
		    u32 type, u32 mask);
	int fips_allowed;	/* set if alg is allowed in fips mode */

	union {
		struct aead_test_suite aead;
		struct cipher_test_suite cipher;
		struct comp_test_suite comp;
		struct hash_test_suite hash;
		struct cprng_test_suite cprng;
		struct drbg_test_suite drbg;
		struct akcipher_test_suite akcipher;
		struct kpp_test_suite kpp;
	} suite;
};

static void hexdump(unsigned char *buf, unsigned int len)
{
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
			16, 1,
			buf, len, false);
}

static int __testmgr_alloc_buf(char *buf[XBUFSIZE], int order)
{
	int i;

	for (i = 0; i < XBUFSIZE; i++) {
		buf[i] = (char *)__get_free_pages(GFP_KERNEL, order);
		if (!buf[i])
			goto err_free_buf;
	}

	return 0;

err_free_buf:
	while (i-- > 0)
		free_pages((unsigned long)buf[i], order);

	return -ENOMEM;
}

static int testmgr_alloc_buf(char *buf[XBUFSIZE])
{
	return __testmgr_alloc_buf(buf, 0);
}

static void __testmgr_free_buf(char *buf[XBUFSIZE], int order)
{
	int i;

	for (i = 0; i < XBUFSIZE; i++)
		free_pages((unsigned long)buf[i], order);
}

static void testmgr_free_buf(char *buf[XBUFSIZE])
{
	__testmgr_free_buf(buf, 0);
}

#define TESTMGR_POISON_BYTE	0xfe
#define TESTMGR_POISON_LEN	16

static inline void testmgr_poison(void *addr, size_t len)
{
	memset(addr, TESTMGR_POISON_BYTE, len);
}

/* Is the memory region still fully poisoned? */
static inline bool testmgr_is_poison(const void *addr, size_t len)
{
	return memchr_inv(addr, TESTMGR_POISON_BYTE, len) == NULL;
}

/* flush type for hash algorithms */
enum flush_type {
	/* merge with update of previous buffer(s) */
	FLUSH_TYPE_NONE = 0,

	/* update with previous buffer(s) before doing this one */
	FLUSH_TYPE_FLUSH,

	/* likewise, but also export and re-import the intermediate state */
	FLUSH_TYPE_REIMPORT,
};

/* finalization function for hash algorithms */
enum finalization_type {
	FINALIZATION_TYPE_FINAL,	/* use final() */
	FINALIZATION_TYPE_FINUP,	/* use finup() */
	FINALIZATION_TYPE_DIGEST,	/* use digest() */
};

#define TEST_SG_TOTAL	10000

/**
 * struct test_sg_division - description of a scatterlist entry
 *
 * This struct describes one entry of a scatterlist being constructed to check a
 * crypto test vector.
 *
 * @proportion_of_total: length of this chunk relative to the total length,
 *			 given as a proportion out of TEST_SG_TOTAL so that it
 *			 scales to fit any test vector
 * @offset: byte offset into a 2-page buffer at which this chunk will start
 * @offset_relative_to_alignmask: if true, add the algorithm's alignmask to the
 *				  @offset
 * @flush_type: for hashes, whether an update() should be done now vs.
 *		continuing to accumulate data
 * @nosimd: if doing the pending update(), do it with SIMD disabled?
 */
struct test_sg_division {
	unsigned int proportion_of_total;
	unsigned int offset;
	bool offset_relative_to_alignmask;
	enum flush_type flush_type;
	bool nosimd;
};

/**
 * struct testvec_config - configuration for testing a crypto test vector
 *
 * This struct describes the data layout and other parameters with which each
 * crypto test vector can be tested.
 *
 * @name: name of this config, logged for debugging purposes if a test fails
 * @inplace: operate on the data in-place, if applicable for the algorithm type?
 * @req_flags: extra request_flags, e.g. CRYPTO_TFM_REQ_MAY_SLEEP
 * @src_divs: description of how to arrange the source scatterlist
 * @dst_divs: description of how to arrange the dst scatterlist, if applicable
 *	      for the algorithm type.  Defaults to @src_divs if unset.
 * @iv_offset: misalignment of the IV in the range [0..MAX_ALGAPI_ALIGNMASK+1],
 *	       where 0 is aligned to a 2*(MAX_ALGAPI_ALIGNMASK+1) byte boundary
 * @iv_offset_relative_to_alignmask: if true, add the algorithm's alignmask to
 *				     the @iv_offset
 * @finalization_type: what finalization function to use for hashes
 * @nosimd: execute with SIMD disabled?  Requires !CRYPTO_TFM_REQ_MAY_SLEEP.
 */
struct testvec_config {
	const char *name;
	bool inplace;
	u32 req_flags;
	struct test_sg_division src_divs[XBUFSIZE];
	struct test_sg_division dst_divs[XBUFSIZE];
	unsigned int iv_offset;
	bool iv_offset_relative_to_alignmask;
	enum finalization_type finalization_type;
	bool nosimd;
};

#define TESTVEC_CONFIG_NAMELEN	192

/*
 * The following are the lists of testvec_configs to test for each algorithm
 * type when the basic crypto self-tests are enabled, i.e. when
 * CONFIG_CRYPTO_MANAGER_DISABLE_TESTS is unset.  They aim to provide good test
 * coverage, while keeping the test time much shorter than the full fuzz tests
 * so that the basic tests can be enabled in a wider range of circumstances.
 */

/* Configs for skciphers and aeads */
static const struct testvec_config default_cipher_testvec_configs[] = {
	{
		.name = "in-place",
		.inplace = true,
		.src_divs = { { .proportion_of_total = 10000 } },
	}, {
		.name = "out-of-place",
		.src_divs = { { .proportion_of_total = 10000 } },
	}, {
		.name = "unaligned buffer, offset=1",
		.src_divs = { { .proportion_of_total = 10000, .offset = 1 } },
		.iv_offset = 1,
	}, {
		.name = "buffer aligned only to alignmask",
		.src_divs = {
			{
				.proportion_of_total = 10000,
				.offset = 1,
				.offset_relative_to_alignmask = true,
			},
		},
		.iv_offset = 1,
		.iv_offset_relative_to_alignmask = true,
	}, {
		.name = "two even aligned splits",
		.src_divs = {
			{ .proportion_of_total = 5000 },
			{ .proportion_of_total = 5000 },
		},
	}, {
		.name = "uneven misaligned splits, may sleep",
		.req_flags = CRYPTO_TFM_REQ_MAY_SLEEP,
		.src_divs = {
			{ .proportion_of_total = 1900, .offset = 33 },
			{ .proportion_of_total = 3300, .offset = 7  },
			{ .proportion_of_total = 4800, .offset = 18 },
		},
		.iv_offset = 3,
	}, {
		.name = "misaligned splits crossing pages, inplace",
		.inplace = true,
		.src_divs = {
			{
				.proportion_of_total = 7500,
				.offset = PAGE_SIZE - 32
			}, {
				.proportion_of_total = 2500,
				.offset = PAGE_SIZE - 7
			},
		},
	}
};

static const struct testvec_config default_hash_testvec_configs[] = {
	{
		.name = "init+update+final aligned buffer",
		.src_divs = { { .proportion_of_total = 10000 } },
		.finalization_type = FINALIZATION_TYPE_FINAL,
	}, {
		.name = "init+finup aligned buffer",
		.src_divs = { { .proportion_of_total = 10000 } },
		.finalization_type = FINALIZATION_TYPE_FINUP,
	}, {
		.name = "digest aligned buffer",
		.src_divs = { { .proportion_of_total = 10000 } },
		.finalization_type = FINALIZATION_TYPE_DIGEST,
	}, {
		.name = "init+update+final misaligned buffer",
		.src_divs = { { .proportion_of_total = 10000, .offset = 1 } },
		.finalization_type = FINALIZATION_TYPE_FINAL,
	}, {
		.name = "digest buffer aligned only to alignmask",
		.src_divs = {
			{
				.proportion_of_total = 10000,
				.offset = 1,
				.offset_relative_to_alignmask = true,
			},
		},
		.finalization_type = FINALIZATION_TYPE_DIGEST,
	}, {
		.name = "init+update+update+final two even splits",
		.src_divs = {
			{ .proportion_of_total = 5000 },
			{
				.proportion_of_total = 5000,
				.flush_type = FLUSH_TYPE_FLUSH,
			},
		},
		.finalization_type = FINALIZATION_TYPE_FINAL,
	}, {
		.name = "digest uneven misaligned splits, may sleep",
		.req_flags = CRYPTO_TFM_REQ_MAY_SLEEP,
		.src_divs = {
			{ .proportion_of_total = 1900, .offset = 33 },
			{ .proportion_of_total = 3300, .offset = 7  },
			{ .proportion_of_total = 4800, .offset = 18 },
		},
		.finalization_type = FINALIZATION_TYPE_DIGEST,
	}, {
		.name = "digest misaligned splits crossing pages",
		.src_divs = {
			{
				.proportion_of_total = 7500,
				.offset = PAGE_SIZE - 32,
			}, {
				.proportion_of_total = 2500,
				.offset = PAGE_SIZE - 7,
			},
		},
		.finalization_type = FINALIZATION_TYPE_DIGEST,
	}, {
		.name = "import/export",
		.src_divs = {
			{
				.proportion_of_total = 6500,
				.flush_type = FLUSH_TYPE_REIMPORT,
			}, {
				.proportion_of_total = 3500,
				.flush_type = FLUSH_TYPE_REIMPORT,
			},
		},
		.finalization_type = FINALIZATION_TYPE_FINAL,
	}
};

static unsigned int count_test_sg_divisions(const struct test_sg_division *divs)
{
	unsigned int remaining = TEST_SG_TOTAL;
	unsigned int ndivs = 0;

	do {
		remaining -= divs[ndivs++].proportion_of_total;
	} while (remaining);

	return ndivs;
}

#define SGDIVS_HAVE_FLUSHES	BIT(0)
#define SGDIVS_HAVE_NOSIMD	BIT(1)

static bool valid_sg_divisions(const struct test_sg_division *divs,
			       unsigned int count, int *flags_ret)
{
	unsigned int total = 0;
	unsigned int i;

	for (i = 0; i < count && total != TEST_SG_TOTAL; i++) {
		if (divs[i].proportion_of_total <= 0 ||
		    divs[i].proportion_of_total > TEST_SG_TOTAL - total)
			return false;
		total += divs[i].proportion_of_total;
		if (divs[i].flush_type != FLUSH_TYPE_NONE)
			*flags_ret |= SGDIVS_HAVE_FLUSHES;
		if (divs[i].nosimd)
			*flags_ret |= SGDIVS_HAVE_NOSIMD;
	}
	return total == TEST_SG_TOTAL &&
		memchr_inv(&divs[i], 0, (count - i) * sizeof(divs[0])) == NULL;
}

/*
 * Check whether the given testvec_config is valid.  This isn't strictly needed
 * since every testvec_config should be valid, but check anyway so that people
 * don't unknowingly add broken configs that don't do what they wanted.
 */
static bool valid_testvec_config(const struct testvec_config *cfg)
{
	int flags = 0;

	if (cfg->name == NULL)
		return false;

	if (!valid_sg_divisions(cfg->src_divs, ARRAY_SIZE(cfg->src_divs),
				&flags))
		return false;

	if (cfg->dst_divs[0].proportion_of_total) {
		if (!valid_sg_divisions(cfg->dst_divs,
					ARRAY_SIZE(cfg->dst_divs), &flags))
			return false;
	} else {
		if (memchr_inv(cfg->dst_divs, 0, sizeof(cfg->dst_divs)))
			return false;
		/* defaults to dst_divs=src_divs */
	}

	if (cfg->iv_offset +
	    (cfg->iv_offset_relative_to_alignmask ? MAX_ALGAPI_ALIGNMASK : 0) >
	    MAX_ALGAPI_ALIGNMASK + 1)
		return false;

	if ((flags & (SGDIVS_HAVE_FLUSHES | SGDIVS_HAVE_NOSIMD)) &&
	    cfg->finalization_type == FINALIZATION_TYPE_DIGEST)
		return false;

	if ((cfg->nosimd || (flags & SGDIVS_HAVE_NOSIMD)) &&
	    (cfg->req_flags & CRYPTO_TFM_REQ_MAY_SLEEP))
		return false;

	return true;
}

struct test_sglist {
	char *bufs[XBUFSIZE];
	struct scatterlist sgl[XBUFSIZE];
	struct scatterlist sgl_saved[XBUFSIZE];
	struct scatterlist *sgl_ptr;
	unsigned int nents;
};

static int init_test_sglist(struct test_sglist *tsgl)
{
	return __testmgr_alloc_buf(tsgl->bufs, 1 /* two pages per buffer */);
}

static void destroy_test_sglist(struct test_sglist *tsgl)
{
	return __testmgr_free_buf(tsgl->bufs, 1 /* two pages per buffer */);
}

/**
 * build_test_sglist() - build a scatterlist for a crypto test
 *
 * @tsgl: the scatterlist to build.  @tsgl->bufs[] contains an array of 2-page
 *	  buffers which the scatterlist @tsgl->sgl[] will be made to point into.
 * @divs: the layout specification on which the scatterlist will be based
 * @alignmask: the algorithm's alignmask
 * @total_len: the total length of the scatterlist to build in bytes
 * @data: if non-NULL, the buffers will be filled with this data until it ends.
 *	  Otherwise the buffers will be poisoned.  In both cases, some bytes
 *	  past the end of each buffer will be poisoned to help detect overruns.
 * @out_divs: if non-NULL, the test_sg_division to which each scatterlist entry
 *	      corresponds will be returned here.  This will match @divs except
 *	      that divisions resolving to a length of 0 are omitted as they are
 *	      not included in the scatterlist.
 *
 * Return: 0 or a -errno value
 */
static int build_test_sglist(struct test_sglist *tsgl,
			     const struct test_sg_division *divs,
			     const unsigned int alignmask,
			     const unsigned int total_len,
			     struct iov_iter *data,
			     const struct test_sg_division *out_divs[XBUFSIZE])
{
	struct {
		const struct test_sg_division *div;
		size_t length;
	} partitions[XBUFSIZE];
	const unsigned int ndivs = count_test_sg_divisions(divs);
	unsigned int len_remaining = total_len;
	unsigned int i;

	BUILD_BUG_ON(ARRAY_SIZE(partitions) != ARRAY_SIZE(tsgl->sgl));
	if (WARN_ON(ndivs > ARRAY_SIZE(partitions)))
		return -EINVAL;

	/* Calculate the (div, length) pairs */
	tsgl->nents = 0;
	for (i = 0; i < ndivs; i++) {
		unsigned int len_this_sg =
			min(len_remaining,
			    (total_len * divs[i].proportion_of_total +
			     TEST_SG_TOTAL / 2) / TEST_SG_TOTAL);

		if (len_this_sg != 0) {
			partitions[tsgl->nents].div = &divs[i];
			partitions[tsgl->nents].length = len_this_sg;
			tsgl->nents++;
			len_remaining -= len_this_sg;
		}
	}
	if (tsgl->nents == 0) {
		partitions[tsgl->nents].div = &divs[0];
		partitions[tsgl->nents].length = 0;
		tsgl->nents++;
	}
	partitions[tsgl->nents - 1].length += len_remaining;

	/* Set up the sgl entries and fill the data or poison */
	sg_init_table(tsgl->sgl, tsgl->nents);
	for (i = 0; i < tsgl->nents; i++) {
		unsigned int offset = partitions[i].div->offset;
		void *addr;

		if (partitions[i].div->offset_relative_to_alignmask)
			offset += alignmask;

		while (offset + partitions[i].length + TESTMGR_POISON_LEN >
		       2 * PAGE_SIZE) {
			if (WARN_ON(offset <= 0))
				return -EINVAL;
			offset /= 2;
		}

		addr = &tsgl->bufs[i][offset];
		sg_set_buf(&tsgl->sgl[i], addr, partitions[i].length);

		if (out_divs)
			out_divs[i] = partitions[i].div;

		if (data) {
			size_t copy_len, copied;

			copy_len = min(partitions[i].length, data->count);
			copied = copy_from_iter(addr, copy_len, data);
			if (WARN_ON(copied != copy_len))
				return -EINVAL;
			testmgr_poison(addr + copy_len, partitions[i].length +
				       TESTMGR_POISON_LEN - copy_len);
		} else {
			testmgr_poison(addr, partitions[i].length +
				       TESTMGR_POISON_LEN);
		}
	}

	sg_mark_end(&tsgl->sgl[tsgl->nents - 1]);
	tsgl->sgl_ptr = tsgl->sgl;
	memcpy(tsgl->sgl_saved, tsgl->sgl, tsgl->nents * sizeof(tsgl->sgl[0]));
	return 0;
}

/*
 * Verify that a scatterlist crypto operation produced the correct output.
 *
 * @tsgl: scatterlist containing the actual output
 * @expected_output: buffer containing the expected output
 * @len_to_check: length of @expected_output in bytes
 * @unchecked_prefix_len: number of ignored bytes in @tsgl prior to real result
 * @check_poison: verify that the poison bytes after each chunk are intact?
 *
 * Return: 0 if correct, -EINVAL if incorrect, -EOVERFLOW if buffer overrun.
 */
static int verify_correct_output(const struct test_sglist *tsgl,
				 const char *expected_output,
				 unsigned int len_to_check,
				 unsigned int unchecked_prefix_len,
				 bool check_poison)
{
	unsigned int i;

	for (i = 0; i < tsgl->nents; i++) {
		struct scatterlist *sg = &tsgl->sgl_ptr[i];
		unsigned int len = sg->length;
		unsigned int offset = sg->offset;
		const char *actual_output;

		if (unchecked_prefix_len) {
			if (unchecked_prefix_len >= len) {
				unchecked_prefix_len -= len;
				continue;
			}
			offset += unchecked_prefix_len;
			len -= unchecked_prefix_len;
			unchecked_prefix_len = 0;
		}
		len = min(len, len_to_check);
		actual_output = page_address(sg_page(sg)) + offset;
		if (memcmp(expected_output, actual_output, len) != 0)
			return -EINVAL;
		if (check_poison &&
		    !testmgr_is_poison(actual_output + len, TESTMGR_POISON_LEN))
			return -EOVERFLOW;
		len_to_check -= len;
		expected_output += len;
	}
	if (WARN_ON(len_to_check != 0))
		return -EINVAL;
	return 0;
}

static bool is_test_sglist_corrupted(const struct test_sglist *tsgl)
{
	unsigned int i;

	for (i = 0; i < tsgl->nents; i++) {
		if (tsgl->sgl[i].page_link != tsgl->sgl_saved[i].page_link)
			return true;
		if (tsgl->sgl[i].offset != tsgl->sgl_saved[i].offset)
			return true;
		if (tsgl->sgl[i].length != tsgl->sgl_saved[i].length)
			return true;
	}
	return false;
}

struct cipher_test_sglists {
	struct test_sglist src;
	struct test_sglist dst;
};

static struct cipher_test_sglists *alloc_cipher_test_sglists(void)
{
	struct cipher_test_sglists *tsgls;

	tsgls = kmalloc(sizeof(*tsgls), GFP_KERNEL);
	if (!tsgls)
		return NULL;

	if (init_test_sglist(&tsgls->src) != 0)
		goto fail_kfree;
	if (init_test_sglist(&tsgls->dst) != 0)
		goto fail_destroy_src;

	return tsgls;

fail_destroy_src:
	destroy_test_sglist(&tsgls->src);
fail_kfree:
	kfree(tsgls);
	return NULL;
}

static void free_cipher_test_sglists(struct cipher_test_sglists *tsgls)
{
	if (tsgls) {
		destroy_test_sglist(&tsgls->src);
		destroy_test_sglist(&tsgls->dst);
		kfree(tsgls);
	}
}

/* Build the src and dst scatterlists for an skcipher or AEAD test */
static int build_cipher_test_sglists(struct cipher_test_sglists *tsgls,
				     const struct testvec_config *cfg,
				     unsigned int alignmask,
				     unsigned int src_total_len,
				     unsigned int dst_total_len,
				     const struct kvec *inputs,
				     unsigned int nr_inputs)
{
	struct iov_iter input;
	int err;

	iov_iter_kvec(&input, WRITE, inputs, nr_inputs, src_total_len);
	err = build_test_sglist(&tsgls->src, cfg->src_divs, alignmask,
				cfg->inplace ?
					max(dst_total_len, src_total_len) :
					src_total_len,
				&input, NULL);
	if (err)
		return err;

	if (cfg->inplace) {
		tsgls->dst.sgl_ptr = tsgls->src.sgl;
		tsgls->dst.nents = tsgls->src.nents;
		return 0;
	}
	return build_test_sglist(&tsgls->dst,
				 cfg->dst_divs[0].proportion_of_total ?
					cfg->dst_divs : cfg->src_divs,
				 alignmask, dst_total_len, NULL, NULL);
}

#ifdef CONFIG_CRYPTO_MANAGER_EXTRA_TESTS
static char *generate_random_sgl_divisions(struct test_sg_division *divs,
					   size_t max_divs, char *p, char *end,
					   bool gen_flushes, u32 req_flags)
{
	struct test_sg_division *div = divs;
	unsigned int remaining = TEST_SG_TOTAL;

	do {
		unsigned int this_len;
		const char *flushtype_str;

		if (div == &divs[max_divs - 1] || prandom_u32() % 2 == 0)
			this_len = remaining;
		else
			this_len = 1 + (prandom_u32() % remaining);
		div->proportion_of_total = this_len;

		if (prandom_u32() % 4 == 0)
			div->offset = (PAGE_SIZE - 128) + (prandom_u32() % 128);
		else if (prandom_u32() % 2 == 0)
			div->offset = prandom_u32() % 32;
		else
			div->offset = prandom_u32() % PAGE_SIZE;
		if (prandom_u32() % 8 == 0)
			div->offset_relative_to_alignmask = true;

		div->flush_type = FLUSH_TYPE_NONE;
		if (gen_flushes) {
			switch (prandom_u32() % 4) {
			case 0:
				div->flush_type = FLUSH_TYPE_REIMPORT;
				break;
			case 1:
				div->flush_type = FLUSH_TYPE_FLUSH;
				break;
			}
		}

		if (div->flush_type != FLUSH_TYPE_NONE &&
		    !(req_flags & CRYPTO_TFM_REQ_MAY_SLEEP) &&
		    prandom_u32() % 2 == 0)
			div->nosimd = true;

		switch (div->flush_type) {
		case FLUSH_TYPE_FLUSH:
			if (div->nosimd)
				flushtype_str = "<flush,nosimd>";
			else
				flushtype_str = "<flush>";
			break;
		case FLUSH_TYPE_REIMPORT:
			if (div->nosimd)
				flushtype_str = "<reimport,nosimd>";
			else
				flushtype_str = "<reimport>";
			break;
		default:
			flushtype_str = "";
			break;
		}

		BUILD_BUG_ON(TEST_SG_TOTAL != 10000); /* for "%u.%u%%" */
		p += scnprintf(p, end - p, "%s%u.%u%%@%s+%u%s", flushtype_str,
			       this_len / 100, this_len % 100,
			       div->offset_relative_to_alignmask ?
					"alignmask" : "",
			       div->offset, this_len == remaining ? "" : ", ");
		remaining -= this_len;
		div++;
	} while (remaining);

	return p;
}

/* Generate a random testvec_config for fuzz testing */
static void generate_random_testvec_config(struct testvec_config *cfg,
					   char *name, size_t max_namelen)
{
	char *p = name;
	char * const end = name + max_namelen;

	memset(cfg, 0, sizeof(*cfg));

	cfg->name = name;

	p += scnprintf(p, end - p, "random:");

	if (prandom_u32() % 2 == 0) {
		cfg->inplace = true;
		p += scnprintf(p, end - p, " inplace");
	}

	if (prandom_u32() % 2 == 0) {
		cfg->req_flags |= CRYPTO_TFM_REQ_MAY_SLEEP;
		p += scnprintf(p, end - p, " may_sleep");
	}

	switch (prandom_u32() % 4) {
	case 0:
		cfg->finalization_type = FINALIZATION_TYPE_FINAL;
		p += scnprintf(p, end - p, " use_final");
		break;
	case 1:
		cfg->finalization_type = FINALIZATION_TYPE_FINUP;
		p += scnprintf(p, end - p, " use_finup");
		break;
	default:
		cfg->finalization_type = FINALIZATION_TYPE_DIGEST;
		p += scnprintf(p, end - p, " use_digest");
		break;
	}

	if (!(cfg->req_flags & CRYPTO_TFM_REQ_MAY_SLEEP) &&
	    prandom_u32() % 2 == 0) {
		cfg->nosimd = true;
		p += scnprintf(p, end - p, " nosimd");
	}

	p += scnprintf(p, end - p, " src_divs=[");
	p = generate_random_sgl_divisions(cfg->src_divs,
					  ARRAY_SIZE(cfg->src_divs), p, end,
					  (cfg->finalization_type !=
					   FINALIZATION_TYPE_DIGEST),
					  cfg->req_flags);
	p += scnprintf(p, end - p, "]");

	if (!cfg->inplace && prandom_u32() % 2 == 0) {
		p += scnprintf(p, end - p, " dst_divs=[");
		p = generate_random_sgl_divisions(cfg->dst_divs,
						  ARRAY_SIZE(cfg->dst_divs),
						  p, end, false,
						  cfg->req_flags);
		p += scnprintf(p, end - p, "]");
	}

	if (prandom_u32() % 2 == 0) {
		cfg->iv_offset = 1 + (prandom_u32() % MAX_ALGAPI_ALIGNMASK);
		p += scnprintf(p, end - p, " iv_offset=%u", cfg->iv_offset);
	}

	WARN_ON_ONCE(!valid_testvec_config(cfg));
}

static void crypto_disable_simd_for_test(void)
{
	preempt_disable();
	__this_cpu_write(crypto_simd_disabled_for_test, true);
}

static void crypto_reenable_simd_for_test(void)
{
	__this_cpu_write(crypto_simd_disabled_for_test, false);
	preempt_enable();
}
#else /* !CONFIG_CRYPTO_MANAGER_EXTRA_TESTS */
static void crypto_disable_simd_for_test(void)
{
}

static void crypto_reenable_simd_for_test(void)
{
}
#endif /* !CONFIG_CRYPTO_MANAGER_EXTRA_TESTS */

static int do_ahash_op(int (*op)(struct ahash_request *req),
		       struct ahash_request *req,
		       struct crypto_wait *wait, bool nosimd)
{
	int err;

	if (nosimd)
		crypto_disable_simd_for_test();

	err = op(req);

	if (nosimd)
		crypto_reenable_simd_for_test();

	return crypto_wait_req(err, wait);
}

static int check_nonfinal_hash_op(const char *op, int err,
				  u8 *result, unsigned int digestsize,
				  const char *driver, unsigned int vec_num,
				  const struct testvec_config *cfg)
{
	if (err) {
		pr_err("alg: hash: %s %s() failed with err %d on test vector %u, cfg=\"%s\"\n",
		       driver, op, err, vec_num, cfg->name);
		return err;
	}
	if (!testmgr_is_poison(result, digestsize)) {
		pr_err("alg: hash: %s %s() used result buffer on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return -EINVAL;
	}
	return 0;
}

static int test_hash_vec_cfg(const char *driver,
			     const struct hash_testvec *vec,
			     unsigned int vec_num,
			     const struct testvec_config *cfg,
			     struct ahash_request *req,
			     struct test_sglist *tsgl,
			     u8 *hashstate)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	const unsigned int alignmask = crypto_ahash_alignmask(tfm);
	const unsigned int digestsize = crypto_ahash_digestsize(tfm);
	const unsigned int statesize = crypto_ahash_statesize(tfm);
	const u32 req_flags = CRYPTO_TFM_REQ_MAY_BACKLOG | cfg->req_flags;
	const struct test_sg_division *divs[XBUFSIZE];
	DECLARE_CRYPTO_WAIT(wait);
	struct kvec _input;
	struct iov_iter input;
	unsigned int i;
	struct scatterlist *pending_sgl;
	unsigned int pending_len;
	u8 result[HASH_MAX_DIGESTSIZE + TESTMGR_POISON_LEN];
	int err;

	/* Set the key, if specified */
	if (vec->ksize) {
		err = crypto_ahash_setkey(tfm, vec->key, vec->ksize);
		if (err) {
			pr_err("alg: hash: %s setkey failed with err %d on test vector %u; flags=%#x\n",
			       driver, err, vec_num,
			       crypto_ahash_get_flags(tfm));
			return err;
		}
	}

	/* Build the scatterlist for the source data */
	_input.iov_base = (void *)vec->plaintext;
	_input.iov_len = vec->psize;
	iov_iter_kvec(&input, WRITE, &_input, 1, vec->psize);
	err = build_test_sglist(tsgl, cfg->src_divs, alignmask, vec->psize,
				&input, divs);
	if (err) {
		pr_err("alg: hash: %s: error preparing scatterlist for test vector %u, cfg=\"%s\"\n",
		       driver, vec_num, cfg->name);
		return err;
	}

	/* Do the actual hashing */

	testmgr_poison(req->__ctx, crypto_ahash_reqsize(tfm));
	testmgr_poison(result, digestsize + TESTMGR_POISON_LEN);

	if (cfg->finalization_type == FINALIZATION_TYPE_DIGEST) {
		/* Just using digest() */
		ahash_request_set_callback(req, req_flags, crypto_req_done,
					   &wait);
		ahash_request_set_crypt(req, tsgl->sgl, result, vec->psize);
		err = do_ahash_op(crypto_ahash_digest, req, &wait, cfg->nosimd);
		if (err) {
			pr_err("alg: hash: %s digest() failed with err %d on test vector %u, cfg=\"%s\"\n",
			       driver, err, vec_num, cfg->name);
			return err;
		}
		goto result_ready;
	}

	/* Using init(), zero or more update(), then final() or finup() */

	ahash_request_set_callback(req, req_flags, crypto_req_done, &wait);
	ahash_request_set_crypt(req, NULL, result, 0);
	err = do_ahash_op(crypto_ahash_init, req, &wait, cfg->nosimd);
	err = check_nonfinal_hash_op("init", err, result, digestsize,
				     driver, vec_num, cfg);
	if (err)
		return err;

	pending_sgl = NULL;
	pending_len = 0;
	for (i = 0; i < tsgl->nents; i++) {
		if (divs[i]->flush_type != FLUSH_TYPE_NONE &&
		    pending_sgl != NULL) {
			/* update() with the pending data */
			ahash_request_set_callback(req, req_flags,
						   crypto_req_done, &wait);
			ahash_request_set_crypt(req, pending_sgl, result,
						pending_len);
			err = do_ahash_op(crypto_ahash_update, req, &wait,
					  divs[i]->nosimd);
			err = check_nonfinal_hash_op("update", err,
						     result, digestsize,
						     driver, vec_num, cfg);
			if (err)
				return err;
			pending_sgl = NULL;
			pending_len = 0;
		}
		if (divs[i]->flush_type == FLUSH_TYPE_REIMPORT) {
			/* Test ->export() and ->import() */
			testmgr_poison(hashstate + statesize,
				       TESTMGR_POISON_LEN);
			err = crypto_ahash_export(req, hashstate);
			err = check_nonfinal_hash_op("export", err,
						     result, digestsize,
						     driver, vec_num, cfg);
			if (err)
				return err;
			if (!testmgr_is_poison(hashstate + statesize,
					       TESTMGR_POISON_LEN)) {
				pr_err("alg: hash: %s export() overran state buffer on test vector %u, cfg=\"%s\"\n",
				       driver, vec_num, cfg->name);
				return -EOVERFLOW;
			}

			testmgr_poison(req->__ctx, crypto_ahash_reqsize(tfm));
			err = crypto_ahash_import(req, hashstate);
			err = check_nonfinal_hash_op("import", err,
						     result, digestsize,
						     driver, vec_num, cfg);
			if (err)
				return err;
		}
		if (pending_sgl == NULL)
			pending_sgl = &tsgl->sgl[i];
		pending_len += tsgl->sgl[i].length;
	}

	ahash_request_set_callback(req, req_flags, crypto_req_done, &wait);
	ahash_request_set_crypt(req, pending_sgl, result, pending_len);
	if (cfg->finalization_type == FINALIZATION_TYPE_FINAL) {
		/* finish with update() and final() */
		err = do_ahash_op(crypto_ahash_update, req, &wait, cfg->nosimd);
		err = check_nonfinal_hash_op("update", err, result, digestsize,
					     driver, vec_num, cfg);
		if (err)
			return err;
		err = do_ahash_op(crypto_ahash_final, req, &wait, cfg->nosimd);
		if (err) {
			pr_err("alg: hash: %s final() failed with err %d on test vector %u, cfg=\"%s\"\n",
			       driver, err, vec_num, cfg->name);
			return err;
		}
	} else {
		/* finish with finup() */
		err = do_ahash_op(crypto_ahash_finup, req, &wait, cfg->nosimd);
		if (err) {
			pr_err("alg: hash: %s finup() failed with err %d on test vector %u, cfg=\"%s\"\n",
			       driver, err, vec_num, cfg->name);
			return err;
		}
	}

result_ready:
	/* Check that the algorithm produced the correct digest */
	if (memcmp(result, vec->digest, digestsize) != 0) {
		pr_err("alg: hash: %s test failed (wrong result) on test vector %u, cfg=\"%s\"\n",
		       driver, vec_num, cfg->name);
		return -EINVAL;
	}
	if (!testmgr_is_poison(&result[digestsize], TESTMGR_POISON_LEN)) {
		pr_err("alg: hash: %s overran result buffer on test vector %u, cfg=\"%s\"\n",
		       driver, vec_num, cfg->name);
		return -EOVERFLOW;
	}

	return 0;
}

static int test_hash_vec(const char *driver, const struct hash_testvec *vec,
			 unsigned int vec_num, struct ahash_request *req,
			 struct test_sglist *tsgl, u8 *hashstate)
{
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(default_hash_testvec_configs); i++) {
		err = test_hash_vec_cfg(driver, vec, vec_num,
					&default_hash_testvec_configs[i],
					req, tsgl, hashstate);
		if (err)
			return err;
	}

#ifdef CONFIG_CRYPTO_MANAGER_EXTRA_TESTS
	if (!noextratests) {
		struct testvec_config cfg;
		char cfgname[TESTVEC_CONFIG_NAMELEN];

		for (i = 0; i < fuzz_iterations; i++) {
			generate_random_testvec_config(&cfg, cfgname,
						       sizeof(cfgname));
			err = test_hash_vec_cfg(driver, vec, vec_num, &cfg,
						req, tsgl, hashstate);
			if (err)
				return err;
		}
	}
#endif
	return 0;
}

static int __alg_test_hash(const struct hash_testvec *vecs,
			   unsigned int num_vecs, const char *driver,
			   u32 type, u32 mask)
{
	struct crypto_ahash *tfm;
	struct ahash_request *req = NULL;
	struct test_sglist *tsgl = NULL;
	u8 *hashstate = NULL;
	unsigned int i;
	int err;

	tfm = crypto_alloc_ahash(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: hash: failed to allocate transform for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: hash: failed to allocate request for %s\n",
		       driver);
		err = -ENOMEM;
		goto out;
	}

	tsgl = kmalloc(sizeof(*tsgl), GFP_KERNEL);
	if (!tsgl || init_test_sglist(tsgl) != 0) {
		pr_err("alg: hash: failed to allocate test buffers for %s\n",
		       driver);
		kfree(tsgl);
		tsgl = NULL;
		err = -ENOMEM;
		goto out;
	}

	hashstate = kmalloc(crypto_ahash_statesize(tfm) + TESTMGR_POISON_LEN,
			    GFP_KERNEL);
	if (!hashstate) {
		pr_err("alg: hash: failed to allocate hash state buffer for %s\n",
		       driver);
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_vecs; i++) {
		err = test_hash_vec(driver, &vecs[i], i, req, tsgl, hashstate);
		if (err)
			goto out;
	}
	err = 0;
out:
	kfree(hashstate);
	if (tsgl) {
		destroy_test_sglist(tsgl);
		kfree(tsgl);
	}
	ahash_request_free(req);
	crypto_free_ahash(tfm);
	return err;
}

static int alg_test_hash(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	const struct hash_testvec *template = desc->suite.hash.vecs;
	unsigned int tcount = desc->suite.hash.count;
	unsigned int nr_unkeyed, nr_keyed;
	int err;

	/*
	 * For OPTIONAL_KEY algorithms, we have to do all the unkeyed tests
	 * first, before setting a key on the tfm.  To make this easier, we
	 * require that the unkeyed test vectors (if any) are listed first.
	 */

	for (nr_unkeyed = 0; nr_unkeyed < tcount; nr_unkeyed++) {
		if (template[nr_unkeyed].ksize)
			break;
	}
	for (nr_keyed = 0; nr_unkeyed + nr_keyed < tcount; nr_keyed++) {
		if (!template[nr_unkeyed + nr_keyed].ksize) {
			pr_err("alg: hash: test vectors for %s out of order, "
			       "unkeyed ones must come first\n", desc->alg);
			return -EINVAL;
		}
	}

	err = 0;
	if (nr_unkeyed) {
		err = __alg_test_hash(template, nr_unkeyed, driver, type, mask);
		template += nr_unkeyed;
	}

	if (!err && nr_keyed)
		err = __alg_test_hash(template, nr_keyed, driver, type, mask);

	return err;
}

static int test_aead_vec_cfg(const char *driver, int enc,
			     const struct aead_testvec *vec,
			     unsigned int vec_num,
			     const struct testvec_config *cfg,
			     struct aead_request *req,
			     struct cipher_test_sglists *tsgls)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const unsigned int alignmask = crypto_aead_alignmask(tfm);
	const unsigned int ivsize = crypto_aead_ivsize(tfm);
	const unsigned int authsize = vec->clen - vec->plen;
	const u32 req_flags = CRYPTO_TFM_REQ_MAY_BACKLOG | cfg->req_flags;
	const char *op = enc ? "encryption" : "decryption";
	DECLARE_CRYPTO_WAIT(wait);
	u8 _iv[3 * (MAX_ALGAPI_ALIGNMASK + 1) + MAX_IVLEN];
	u8 *iv = PTR_ALIGN(&_iv[0], 2 * (MAX_ALGAPI_ALIGNMASK + 1)) +
		 cfg->iv_offset +
		 (cfg->iv_offset_relative_to_alignmask ? alignmask : 0);
	struct kvec input[2];
	int err;

	/* Set the key */
	if (vec->wk)
		crypto_aead_set_flags(tfm, CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);
	else
		crypto_aead_clear_flags(tfm, CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);
	err = crypto_aead_setkey(tfm, vec->key, vec->klen);
	if (err) {
		if (vec->fail) /* expectedly failed to set key? */
			return 0;
		pr_err("alg: aead: %s setkey failed with err %d on test vector %u; flags=%#x\n",
		       driver, err, vec_num, crypto_aead_get_flags(tfm));
		return err;
	}
	if (vec->fail) {
		pr_err("alg: aead: %s setkey unexpectedly succeeded on test vector %u\n",
		       driver, vec_num);
		return -EINVAL;
	}

	/* Set the authentication tag size */
	err = crypto_aead_setauthsize(tfm, authsize);
	if (err) {
		pr_err("alg: aead: %s setauthsize failed with err %d on test vector %u\n",
		       driver, err, vec_num);
		return err;
	}

	/* The IV must be copied to a buffer, as the algorithm may modify it */
	if (WARN_ON(ivsize > MAX_IVLEN))
		return -EINVAL;
	if (vec->iv)
		memcpy(iv, vec->iv, ivsize);
	else
		memset(iv, 0, ivsize);

	/* Build the src/dst scatterlists */
	input[0].iov_base = (void *)vec->assoc;
	input[0].iov_len = vec->alen;
	input[1].iov_base = enc ? (void *)vec->ptext : (void *)vec->ctext;
	input[1].iov_len = enc ? vec->plen : vec->clen;
	err = build_cipher_test_sglists(tsgls, cfg, alignmask,
					vec->alen + (enc ? vec->plen :
						     vec->clen),
					vec->alen + (enc ? vec->clen :
						     vec->plen),
					input, 2);
	if (err) {
		pr_err("alg: aead: %s %s: error preparing scatterlists for test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return err;
	}

	/* Do the actual encryption or decryption */
	testmgr_poison(req->__ctx, crypto_aead_reqsize(tfm));
	aead_request_set_callback(req, req_flags, crypto_req_done, &wait);
	aead_request_set_crypt(req, tsgls->src.sgl_ptr, tsgls->dst.sgl_ptr,
			       enc ? vec->plen : vec->clen, iv);
	aead_request_set_ad(req, vec->alen);
	if (cfg->nosimd)
		crypto_disable_simd_for_test();
	err = enc ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req);
	if (cfg->nosimd)
		crypto_reenable_simd_for_test();
	err = crypto_wait_req(err, &wait);
	if (err) {
		if (err == -EBADMSG && vec->novrfy)
			return 0;
		pr_err("alg: aead: %s %s failed with err %d on test vector %u, cfg=\"%s\"\n",
		       driver, op, err, vec_num, cfg->name);
		return err;
	}
	if (vec->novrfy) {
		pr_err("alg: aead: %s %s unexpectedly succeeded on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return -EINVAL;
	}

	/* Check that the algorithm didn't overwrite things it shouldn't have */
	if (req->cryptlen != (enc ? vec->plen : vec->clen) ||
	    req->assoclen != vec->alen ||
	    req->iv != iv ||
	    req->src != tsgls->src.sgl_ptr ||
	    req->dst != tsgls->dst.sgl_ptr ||
	    crypto_aead_reqtfm(req) != tfm ||
	    req->base.complete != crypto_req_done ||
	    req->base.flags != req_flags ||
	    req->base.data != &wait) {
		pr_err("alg: aead: %s %s corrupted request struct on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		if (req->cryptlen != (enc ? vec->plen : vec->clen))
			pr_err("alg: aead: changed 'req->cryptlen'\n");
		if (req->assoclen != vec->alen)
			pr_err("alg: aead: changed 'req->assoclen'\n");
		if (req->iv != iv)
			pr_err("alg: aead: changed 'req->iv'\n");
		if (req->src != tsgls->src.sgl_ptr)
			pr_err("alg: aead: changed 'req->src'\n");
		if (req->dst != tsgls->dst.sgl_ptr)
			pr_err("alg: aead: changed 'req->dst'\n");
		if (crypto_aead_reqtfm(req) != tfm)
			pr_err("alg: aead: changed 'req->base.tfm'\n");
		if (req->base.complete != crypto_req_done)
			pr_err("alg: aead: changed 'req->base.complete'\n");
		if (req->base.flags != req_flags)
			pr_err("alg: aead: changed 'req->base.flags'\n");
		if (req->base.data != &wait)
			pr_err("alg: aead: changed 'req->base.data'\n");
		return -EINVAL;
	}
	if (is_test_sglist_corrupted(&tsgls->src)) {
		pr_err("alg: aead: %s %s corrupted src sgl on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return -EINVAL;
	}
	if (tsgls->dst.sgl_ptr != tsgls->src.sgl &&
	    is_test_sglist_corrupted(&tsgls->dst)) {
		pr_err("alg: aead: %s %s corrupted dst sgl on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return -EINVAL;
	}

	/* Check for the correct output (ciphertext or plaintext) */
	err = verify_correct_output(&tsgls->dst, enc ? vec->ctext : vec->ptext,
				    enc ? vec->clen : vec->plen,
				    vec->alen, enc || !cfg->inplace);
	if (err == -EOVERFLOW) {
		pr_err("alg: aead: %s %s overran dst buffer on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return err;
	}
	if (err) {
		pr_err("alg: aead: %s %s test failed (wrong result) on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return err;
	}

	return 0;
}

static int test_aead_vec(const char *driver, int enc,
			 const struct aead_testvec *vec, unsigned int vec_num,
			 struct aead_request *req,
			 struct cipher_test_sglists *tsgls)
{
	unsigned int i;
	int err;

	if (enc && vec->novrfy)
		return 0;

	for (i = 0; i < ARRAY_SIZE(default_cipher_testvec_configs); i++) {
		err = test_aead_vec_cfg(driver, enc, vec, vec_num,
					&default_cipher_testvec_configs[i],
					req, tsgls);
		if (err)
			return err;
	}

#ifdef CONFIG_CRYPTO_MANAGER_EXTRA_TESTS
	if (!noextratests) {
		struct testvec_config cfg;
		char cfgname[TESTVEC_CONFIG_NAMELEN];

		for (i = 0; i < fuzz_iterations; i++) {
			generate_random_testvec_config(&cfg, cfgname,
						       sizeof(cfgname));
			err = test_aead_vec_cfg(driver, enc, vec, vec_num,
						&cfg, req, tsgls);
			if (err)
				return err;
		}
	}
#endif
	return 0;
}

static int test_aead(const char *driver, int enc,
		     const struct aead_test_suite *suite,
		     struct aead_request *req,
		     struct cipher_test_sglists *tsgls)
{
	unsigned int i;
	int err;

	for (i = 0; i < suite->count; i++) {
		err = test_aead_vec(driver, enc, &suite->vecs[i], i, req,
				    tsgls);
		if (err)
			return err;
	}
	return 0;
}

static int alg_test_aead(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	const struct aead_test_suite *suite = &desc->suite.aead;
	struct crypto_aead *tfm;
	struct aead_request *req = NULL;
	struct cipher_test_sglists *tsgls = NULL;
	int err;

	if (suite->count <= 0) {
		pr_err("alg: aead: empty test suite for %s\n", driver);
		return -EINVAL;
	}

	tfm = crypto_alloc_aead(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: aead: failed to allocate transform for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: aead: failed to allocate request for %s\n",
		       driver);
		err = -ENOMEM;
		goto out;
	}

	tsgls = alloc_cipher_test_sglists();
	if (!tsgls) {
		pr_err("alg: aead: failed to allocate test buffers for %s\n",
		       driver);
		err = -ENOMEM;
		goto out;
	}

	err = test_aead(driver, ENCRYPT, suite, req, tsgls);
	if (err)
		goto out;

	err = test_aead(driver, DECRYPT, suite, req, tsgls);
out:
	free_cipher_test_sglists(tsgls);
	aead_request_free(req);
	crypto_free_aead(tfm);
	return err;
}

static int test_cipher(struct crypto_cipher *tfm, int enc,
		       const struct cipher_testvec *template,
		       unsigned int tcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_cipher_tfm(tfm));
	unsigned int i, j, k;
	char *q;
	const char *e;
	const char *input, *result;
	void *data;
	char *xbuf[XBUFSIZE];
	int ret = -ENOMEM;

	if (testmgr_alloc_buf(xbuf))
		goto out_nobuf;

	if (enc == ENCRYPT)
	        e = "encryption";
	else
		e = "decryption";

	j = 0;
	for (i = 0; i < tcount; i++) {

		if (fips_enabled && template[i].fips_skip)
			continue;

		input  = enc ? template[i].ptext : template[i].ctext;
		result = enc ? template[i].ctext : template[i].ptext;
		j++;

		ret = -EINVAL;
		if (WARN_ON(template[i].len > PAGE_SIZE))
			goto out;

		data = xbuf[0];
		memcpy(data, input, template[i].len);

		crypto_cipher_clear_flags(tfm, ~0);
		if (template[i].wk)
			crypto_cipher_set_flags(tfm, CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);

		ret = crypto_cipher_setkey(tfm, template[i].key,
					   template[i].klen);
		if (template[i].fail == !ret) {
			printk(KERN_ERR "alg: cipher: setkey failed "
			       "on test %d for %s: flags=%x\n", j,
			       algo, crypto_cipher_get_flags(tfm));
			goto out;
		} else if (ret)
			continue;

		for (k = 0; k < template[i].len;
		     k += crypto_cipher_blocksize(tfm)) {
			if (enc)
				crypto_cipher_encrypt_one(tfm, data + k,
							  data + k);
			else
				crypto_cipher_decrypt_one(tfm, data + k,
							  data + k);
		}

		q = data;
		if (memcmp(q, result, template[i].len)) {
			printk(KERN_ERR "alg: cipher: Test %d failed "
			       "on %s for %s\n", j, e, algo);
			hexdump(q, template[i].len);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	testmgr_free_buf(xbuf);
out_nobuf:
	return ret;
}

static int test_skcipher_vec_cfg(const char *driver, int enc,
				 const struct cipher_testvec *vec,
				 unsigned int vec_num,
				 const struct testvec_config *cfg,
				 struct skcipher_request *req,
				 struct cipher_test_sglists *tsgls)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const unsigned int alignmask = crypto_skcipher_alignmask(tfm);
	const unsigned int ivsize = crypto_skcipher_ivsize(tfm);
	const u32 req_flags = CRYPTO_TFM_REQ_MAY_BACKLOG | cfg->req_flags;
	const char *op = enc ? "encryption" : "decryption";
	DECLARE_CRYPTO_WAIT(wait);
	u8 _iv[3 * (MAX_ALGAPI_ALIGNMASK + 1) + MAX_IVLEN];
	u8 *iv = PTR_ALIGN(&_iv[0], 2 * (MAX_ALGAPI_ALIGNMASK + 1)) +
		 cfg->iv_offset +
		 (cfg->iv_offset_relative_to_alignmask ? alignmask : 0);
	struct kvec input;
	int err;

	/* Set the key */
	if (vec->wk)
		crypto_skcipher_set_flags(tfm, CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);
	else
		crypto_skcipher_clear_flags(tfm,
					    CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);
	err = crypto_skcipher_setkey(tfm, vec->key, vec->klen);
	if (err) {
		if (vec->fail) /* expectedly failed to set key? */
			return 0;
		pr_err("alg: skcipher: %s setkey failed with err %d on test vector %u; flags=%#x\n",
		       driver, err, vec_num, crypto_skcipher_get_flags(tfm));
		return err;
	}
	if (vec->fail) {
		pr_err("alg: skcipher: %s setkey unexpectedly succeeded on test vector %u\n",
		       driver, vec_num);
		return -EINVAL;
	}

	/* The IV must be copied to a buffer, as the algorithm may modify it */
	if (ivsize) {
		if (WARN_ON(ivsize > MAX_IVLEN))
			return -EINVAL;
		if (vec->generates_iv && !enc)
			memcpy(iv, vec->iv_out, ivsize);
		else if (vec->iv)
			memcpy(iv, vec->iv, ivsize);
		else
			memset(iv, 0, ivsize);
	} else {
		if (vec->generates_iv) {
			pr_err("alg: skcipher: %s has ivsize=0 but test vector %u generates IV!\n",
			       driver, vec_num);
			return -EINVAL;
		}
		iv = NULL;
	}

	/* Build the src/dst scatterlists */
	input.iov_base = enc ? (void *)vec->ptext : (void *)vec->ctext;
	input.iov_len = vec->len;
	err = build_cipher_test_sglists(tsgls, cfg, alignmask,
					vec->len, vec->len, &input, 1);
	if (err) {
		pr_err("alg: skcipher: %s %s: error preparing scatterlists for test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return err;
	}

	/* Do the actual encryption or decryption */
	testmgr_poison(req->__ctx, crypto_skcipher_reqsize(tfm));
	skcipher_request_set_callback(req, req_flags, crypto_req_done, &wait);
	skcipher_request_set_crypt(req, tsgls->src.sgl_ptr, tsgls->dst.sgl_ptr,
				   vec->len, iv);
	if (cfg->nosimd)
		crypto_disable_simd_for_test();
	err = enc ? crypto_skcipher_encrypt(req) : crypto_skcipher_decrypt(req);
	if (cfg->nosimd)
		crypto_reenable_simd_for_test();
	err = crypto_wait_req(err, &wait);
	if (err) {
		pr_err("alg: skcipher: %s %s failed with err %d on test vector %u, cfg=\"%s\"\n",
		       driver, op, err, vec_num, cfg->name);
		return err;
	}

	/* Check that the algorithm didn't overwrite things it shouldn't have */
	if (req->cryptlen != vec->len ||
	    req->iv != iv ||
	    req->src != tsgls->src.sgl_ptr ||
	    req->dst != tsgls->dst.sgl_ptr ||
	    crypto_skcipher_reqtfm(req) != tfm ||
	    req->base.complete != crypto_req_done ||
	    req->base.flags != req_flags ||
	    req->base.data != &wait) {
		pr_err("alg: skcipher: %s %s corrupted request struct on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		if (req->cryptlen != vec->len)
			pr_err("alg: skcipher: changed 'req->cryptlen'\n");
		if (req->iv != iv)
			pr_err("alg: skcipher: changed 'req->iv'\n");
		if (req->src != tsgls->src.sgl_ptr)
			pr_err("alg: skcipher: changed 'req->src'\n");
		if (req->dst != tsgls->dst.sgl_ptr)
			pr_err("alg: skcipher: changed 'req->dst'\n");
		if (crypto_skcipher_reqtfm(req) != tfm)
			pr_err("alg: skcipher: changed 'req->base.tfm'\n");
		if (req->base.complete != crypto_req_done)
			pr_err("alg: skcipher: changed 'req->base.complete'\n");
		if (req->base.flags != req_flags)
			pr_err("alg: skcipher: changed 'req->base.flags'\n");
		if (req->base.data != &wait)
			pr_err("alg: skcipher: changed 'req->base.data'\n");
		return -EINVAL;
	}
	if (is_test_sglist_corrupted(&tsgls->src)) {
		pr_err("alg: skcipher: %s %s corrupted src sgl on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return -EINVAL;
	}
	if (tsgls->dst.sgl_ptr != tsgls->src.sgl &&
	    is_test_sglist_corrupted(&tsgls->dst)) {
		pr_err("alg: skcipher: %s %s corrupted dst sgl on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return -EINVAL;
	}

	/* Check for the correct output (ciphertext or plaintext) */
	err = verify_correct_output(&tsgls->dst, enc ? vec->ctext : vec->ptext,
				    vec->len, 0, true);
	if (err == -EOVERFLOW) {
		pr_err("alg: skcipher: %s %s overran dst buffer on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return err;
	}
	if (err) {
		pr_err("alg: skcipher: %s %s test failed (wrong result) on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		return err;
	}

	/* If applicable, check that the algorithm generated the correct IV */
	if (vec->iv_out && memcmp(iv, vec->iv_out, ivsize) != 0) {
		pr_err("alg: skcipher: %s %s test failed (wrong output IV) on test vector %u, cfg=\"%s\"\n",
		       driver, op, vec_num, cfg->name);
		hexdump(iv, ivsize);
		return -EINVAL;
	}

	return 0;
}

static int test_skcipher_vec(const char *driver, int enc,
			     const struct cipher_testvec *vec,
			     unsigned int vec_num,
			     struct skcipher_request *req,
			     struct cipher_test_sglists *tsgls)
{
	unsigned int i;
	int err;

	if (fips_enabled && vec->fips_skip)
		return 0;

	for (i = 0; i < ARRAY_SIZE(default_cipher_testvec_configs); i++) {
		err = test_skcipher_vec_cfg(driver, enc, vec, vec_num,
					    &default_cipher_testvec_configs[i],
					    req, tsgls);
		if (err)
			return err;
	}

#ifdef CONFIG_CRYPTO_MANAGER_EXTRA_TESTS
	if (!noextratests) {
		struct testvec_config cfg;
		char cfgname[TESTVEC_CONFIG_NAMELEN];

		for (i = 0; i < fuzz_iterations; i++) {
			generate_random_testvec_config(&cfg, cfgname,
						       sizeof(cfgname));
			err = test_skcipher_vec_cfg(driver, enc, vec, vec_num,
						    &cfg, req, tsgls);
			if (err)
				return err;
		}
	}
#endif
	return 0;
}

static int test_skcipher(const char *driver, int enc,
			 const struct cipher_test_suite *suite,
			 struct skcipher_request *req,
			 struct cipher_test_sglists *tsgls)
{
	unsigned int i;
	int err;

	for (i = 0; i < suite->count; i++) {
		err = test_skcipher_vec(driver, enc, &suite->vecs[i], i, req,
					tsgls);
		if (err)
			return err;
	}
	return 0;
}

static int alg_test_skcipher(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	const struct cipher_test_suite *suite = &desc->suite.cipher;
	struct crypto_skcipher *tfm;
	struct skcipher_request *req = NULL;
	struct cipher_test_sglists *tsgls = NULL;
	int err;

	if (suite->count <= 0) {
		pr_err("alg: skcipher: empty test suite for %s\n", driver);
		return -EINVAL;
	}

	tfm = crypto_alloc_skcipher(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: skcipher: failed to allocate transform for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: skcipher: failed to allocate request for %s\n",
		       driver);
		err = -ENOMEM;
		goto out;
	}

	tsgls = alloc_cipher_test_sglists();
	if (!tsgls) {
		pr_err("alg: skcipher: failed to allocate test buffers for %s\n",
		       driver);
		err = -ENOMEM;
		goto out;
	}

	err = test_skcipher(driver, ENCRYPT, suite, req, tsgls);
	if (err)
		goto out;

	err = test_skcipher(driver, DECRYPT, suite, req, tsgls);
out:
	free_cipher_test_sglists(tsgls);
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	return err;
}

static int test_comp(struct crypto_comp *tfm,
		     const struct comp_testvec *ctemplate,
		     const struct comp_testvec *dtemplate,
		     int ctcount, int dtcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_comp_tfm(tfm));
	char *output, *decomp_output;
	unsigned int i;
	int ret;

	output = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	decomp_output = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!decomp_output) {
		kfree(output);
		return -ENOMEM;
	}

	for (i = 0; i < ctcount; i++) {
		int ilen;
		unsigned int dlen = COMP_BUF_SIZE;

		memset(output, 0, COMP_BUF_SIZE);
		memset(decomp_output, 0, COMP_BUF_SIZE);

		ilen = ctemplate[i].inlen;
		ret = crypto_comp_compress(tfm, ctemplate[i].input,
					   ilen, output, &dlen);
		if (ret) {
			printk(KERN_ERR "alg: comp: compression failed "
			       "on test %d for %s: ret=%d\n", i + 1, algo,
			       -ret);
			goto out;
		}

		ilen = dlen;
		dlen = COMP_BUF_SIZE;
		ret = crypto_comp_decompress(tfm, output,
					     ilen, decomp_output, &dlen);
		if (ret) {
			pr_err("alg: comp: compression failed: decompress: on test %d for %s failed: ret=%d\n",
			       i + 1, algo, -ret);
			goto out;
		}

		if (dlen != ctemplate[i].inlen) {
			printk(KERN_ERR "alg: comp: Compression test %d "
			       "failed for %s: output len = %d\n", i + 1, algo,
			       dlen);
			ret = -EINVAL;
			goto out;
		}

		if (memcmp(decomp_output, ctemplate[i].input,
			   ctemplate[i].inlen)) {
			pr_err("alg: comp: compression failed: output differs: on test %d for %s\n",
			       i + 1, algo);
			hexdump(decomp_output, dlen);
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < dtcount; i++) {
		int ilen;
		unsigned int dlen = COMP_BUF_SIZE;

		memset(decomp_output, 0, COMP_BUF_SIZE);

		ilen = dtemplate[i].inlen;
		ret = crypto_comp_decompress(tfm, dtemplate[i].input,
					     ilen, decomp_output, &dlen);
		if (ret) {
			printk(KERN_ERR "alg: comp: decompression failed "
			       "on test %d for %s: ret=%d\n", i + 1, algo,
			       -ret);
			goto out;
		}

		if (dlen != dtemplate[i].outlen) {
			printk(KERN_ERR "alg: comp: Decompression test %d "
			       "failed for %s: output len = %d\n", i + 1, algo,
			       dlen);
			ret = -EINVAL;
			goto out;
		}

		if (memcmp(decomp_output, dtemplate[i].output, dlen)) {
			printk(KERN_ERR "alg: comp: Decompression test %d "
			       "failed for %s\n", i + 1, algo);
			hexdump(decomp_output, dlen);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	kfree(decomp_output);
	kfree(output);
	return ret;
}

static int test_acomp(struct crypto_acomp *tfm,
			      const struct comp_testvec *ctemplate,
		      const struct comp_testvec *dtemplate,
		      int ctcount, int dtcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_acomp_tfm(tfm));
	unsigned int i;
	char *output, *decomp_out;
	int ret;
	struct scatterlist src, dst;
	struct acomp_req *req;
	struct crypto_wait wait;

	output = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	decomp_out = kmalloc(COMP_BUF_SIZE, GFP_KERNEL);
	if (!decomp_out) {
		kfree(output);
		return -ENOMEM;
	}

	for (i = 0; i < ctcount; i++) {
		unsigned int dlen = COMP_BUF_SIZE;
		int ilen = ctemplate[i].inlen;
		void *input_vec;

		input_vec = kmemdup(ctemplate[i].input, ilen, GFP_KERNEL);
		if (!input_vec) {
			ret = -ENOMEM;
			goto out;
		}

		memset(output, 0, dlen);
		crypto_init_wait(&wait);
		sg_init_one(&src, input_vec, ilen);
		sg_init_one(&dst, output, dlen);

		req = acomp_request_alloc(tfm);
		if (!req) {
			pr_err("alg: acomp: request alloc failed for %s\n",
			       algo);
			kfree(input_vec);
			ret = -ENOMEM;
			goto out;
		}

		acomp_request_set_params(req, &src, &dst, ilen, dlen);
		acomp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					   crypto_req_done, &wait);

		ret = crypto_wait_req(crypto_acomp_compress(req), &wait);
		if (ret) {
			pr_err("alg: acomp: compression failed on test %d for %s: ret=%d\n",
			       i + 1, algo, -ret);
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		ilen = req->dlen;
		dlen = COMP_BUF_SIZE;
		sg_init_one(&src, output, ilen);
		sg_init_one(&dst, decomp_out, dlen);
		crypto_init_wait(&wait);
		acomp_request_set_params(req, &src, &dst, ilen, dlen);

		ret = crypto_wait_req(crypto_acomp_decompress(req), &wait);
		if (ret) {
			pr_err("alg: acomp: compression failed on test %d for %s: ret=%d\n",
			       i + 1, algo, -ret);
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (req->dlen != ctemplate[i].inlen) {
			pr_err("alg: acomp: Compression test %d failed for %s: output len = %d\n",
			       i + 1, algo, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (memcmp(input_vec, decomp_out, req->dlen)) {
			pr_err("alg: acomp: Compression test %d failed for %s\n",
			       i + 1, algo);
			hexdump(output, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		kfree(input_vec);
		acomp_request_free(req);
	}

	for (i = 0; i < dtcount; i++) {
		unsigned int dlen = COMP_BUF_SIZE;
		int ilen = dtemplate[i].inlen;
		void *input_vec;

		input_vec = kmemdup(dtemplate[i].input, ilen, GFP_KERNEL);
		if (!input_vec) {
			ret = -ENOMEM;
			goto out;
		}

		memset(output, 0, dlen);
		crypto_init_wait(&wait);
		sg_init_one(&src, input_vec, ilen);
		sg_init_one(&dst, output, dlen);

		req = acomp_request_alloc(tfm);
		if (!req) {
			pr_err("alg: acomp: request alloc failed for %s\n",
			       algo);
			kfree(input_vec);
			ret = -ENOMEM;
			goto out;
		}

		acomp_request_set_params(req, &src, &dst, ilen, dlen);
		acomp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					   crypto_req_done, &wait);

		ret = crypto_wait_req(crypto_acomp_decompress(req), &wait);
		if (ret) {
			pr_err("alg: acomp: decompression failed on test %d for %s: ret=%d\n",
			       i + 1, algo, -ret);
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (req->dlen != dtemplate[i].outlen) {
			pr_err("alg: acomp: Decompression test %d failed for %s: output len = %d\n",
			       i + 1, algo, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		if (memcmp(output, dtemplate[i].output, req->dlen)) {
			pr_err("alg: acomp: Decompression test %d failed for %s\n",
			       i + 1, algo);
			hexdump(output, req->dlen);
			ret = -EINVAL;
			kfree(input_vec);
			acomp_request_free(req);
			goto out;
		}

		kfree(input_vec);
		acomp_request_free(req);
	}

	ret = 0;

out:
	kfree(decomp_out);
	kfree(output);
	return ret;
}

static int test_cprng(struct crypto_rng *tfm,
		      const struct cprng_testvec *template,
		      unsigned int tcount)
{
	const char *algo = crypto_tfm_alg_driver_name(crypto_rng_tfm(tfm));
	int err = 0, i, j, seedsize;
	u8 *seed;
	char result[32];

	seedsize = crypto_rng_seedsize(tfm);

	seed = kmalloc(seedsize, GFP_KERNEL);
	if (!seed) {
		printk(KERN_ERR "alg: cprng: Failed to allocate seed space "
		       "for %s\n", algo);
		return -ENOMEM;
	}

	for (i = 0; i < tcount; i++) {
		memset(result, 0, 32);

		memcpy(seed, template[i].v, template[i].vlen);
		memcpy(seed + template[i].vlen, template[i].key,
		       template[i].klen);
		memcpy(seed + template[i].vlen + template[i].klen,
		       template[i].dt, template[i].dtlen);

		err = crypto_rng_reset(tfm, seed, seedsize);
		if (err) {
			printk(KERN_ERR "alg: cprng: Failed to reset rng "
			       "for %s\n", algo);
			goto out;
		}

		for (j = 0; j < template[i].loops; j++) {
			err = crypto_rng_get_bytes(tfm, result,
						   template[i].rlen);
			if (err < 0) {
				printk(KERN_ERR "alg: cprng: Failed to obtain "
				       "the correct amount of random data for "
				       "%s (requested %d)\n", algo,
				       template[i].rlen);
				goto out;
			}
		}

		err = memcmp(result, template[i].result,
			     template[i].rlen);
		if (err) {
			printk(KERN_ERR "alg: cprng: Test %d failed for %s\n",
			       i, algo);
			hexdump(result, template[i].rlen);
			err = -EINVAL;
			goto out;
		}
	}

out:
	kfree(seed);
	return err;
}

static int alg_test_cipher(const struct alg_test_desc *desc,
			   const char *driver, u32 type, u32 mask)
{
	const struct cipher_test_suite *suite = &desc->suite.cipher;
	struct crypto_cipher *tfm;
	int err;

	tfm = crypto_alloc_cipher(driver, type, mask);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "alg: cipher: Failed to load transform for "
		       "%s: %ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = test_cipher(tfm, ENCRYPT, suite->vecs, suite->count);
	if (!err)
		err = test_cipher(tfm, DECRYPT, suite->vecs, suite->count);

	crypto_free_cipher(tfm);
	return err;
}

static int alg_test_comp(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	struct crypto_comp *comp;
	struct crypto_acomp *acomp;
	int err;
	u32 algo_type = type & CRYPTO_ALG_TYPE_ACOMPRESS_MASK;

	if (algo_type == CRYPTO_ALG_TYPE_ACOMPRESS) {
		acomp = crypto_alloc_acomp(driver, type, mask);
		if (IS_ERR(acomp)) {
			pr_err("alg: acomp: Failed to load transform for %s: %ld\n",
			       driver, PTR_ERR(acomp));
			return PTR_ERR(acomp);
		}
		err = test_acomp(acomp, desc->suite.comp.comp.vecs,
				 desc->suite.comp.decomp.vecs,
				 desc->suite.comp.comp.count,
				 desc->suite.comp.decomp.count);
		crypto_free_acomp(acomp);
	} else {
		comp = crypto_alloc_comp(driver, type, mask);
		if (IS_ERR(comp)) {
			pr_err("alg: comp: Failed to load transform for %s: %ld\n",
			       driver, PTR_ERR(comp));
			return PTR_ERR(comp);
		}

		err = test_comp(comp, desc->suite.comp.comp.vecs,
				desc->suite.comp.decomp.vecs,
				desc->suite.comp.comp.count,
				desc->suite.comp.decomp.count);

		crypto_free_comp(comp);
	}
	return err;
}

static int alg_test_crc32c(const struct alg_test_desc *desc,
			   const char *driver, u32 type, u32 mask)
{
	struct crypto_shash *tfm;
	__le32 val;
	int err;

	err = alg_test_hash(desc, driver, type, mask);
	if (err)
		return err;

	tfm = crypto_alloc_shash(driver, type, mask);
	if (IS_ERR(tfm)) {
		if (PTR_ERR(tfm) == -ENOENT) {
			/*
			 * This crc32c implementation is only available through
			 * ahash API, not the shash API, so the remaining part
			 * of the test is not applicable to it.
			 */
			return 0;
		}
		printk(KERN_ERR "alg: crc32c: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	do {
		SHASH_DESC_ON_STACK(shash, tfm);
		u32 *ctx = (u32 *)shash_desc_ctx(shash);

		shash->tfm = tfm;
		shash->flags = 0;

		*ctx = 420553207;
		err = crypto_shash_final(shash, (u8 *)&val);
		if (err) {
			printk(KERN_ERR "alg: crc32c: Operation failed for "
			       "%s: %d\n", driver, err);
			break;
		}

		if (val != cpu_to_le32(~420553207)) {
			pr_err("alg: crc32c: Test failed for %s: %u\n",
			       driver, le32_to_cpu(val));
			err = -EINVAL;
		}
	} while (0);

	crypto_free_shash(tfm);

	return err;
}

static int alg_test_cprng(const struct alg_test_desc *desc, const char *driver,
			  u32 type, u32 mask)
{
	struct crypto_rng *rng;
	int err;

	rng = crypto_alloc_rng(driver, type, mask);
	if (IS_ERR(rng)) {
		printk(KERN_ERR "alg: cprng: Failed to load transform for %s: "
		       "%ld\n", driver, PTR_ERR(rng));
		return PTR_ERR(rng);
	}

	err = test_cprng(rng, desc->suite.cprng.vecs, desc->suite.cprng.count);

	crypto_free_rng(rng);

	return err;
}


static int drbg_cavs_test(const struct drbg_testvec *test, int pr,
			  const char *driver, u32 type, u32 mask)
{
	int ret = -EAGAIN;
	struct crypto_rng *drng;
	struct drbg_test_data test_data;
	struct drbg_string addtl, pers, testentropy;
	unsigned char *buf = kzalloc(test->expectedlen, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	drng = crypto_alloc_rng(driver, type, mask);
	if (IS_ERR(drng)) {
		printk(KERN_ERR "alg: drbg: could not allocate DRNG handle for "
		       "%s\n", driver);
		kzfree(buf);
		return -ENOMEM;
	}

	test_data.testentropy = &testentropy;
	drbg_string_fill(&testentropy, test->entropy, test->entropylen);
	drbg_string_fill(&pers, test->pers, test->perslen);
	ret = crypto_drbg_reset_test(drng, &pers, &test_data);
	if (ret) {
		printk(KERN_ERR "alg: drbg: Failed to reset rng\n");
		goto outbuf;
	}

	drbg_string_fill(&addtl, test->addtla, test->addtllen);
	if (pr) {
		drbg_string_fill(&testentropy, test->entpra, test->entprlen);
		ret = crypto_drbg_get_bytes_addtl_test(drng,
			buf, test->expectedlen, &addtl,	&test_data);
	} else {
		ret = crypto_drbg_get_bytes_addtl(drng,
			buf, test->expectedlen, &addtl);
	}
	if (ret < 0) {
		printk(KERN_ERR "alg: drbg: could not obtain random data for "
		       "driver %s\n", driver);
		goto outbuf;
	}

	drbg_string_fill(&addtl, test->addtlb, test->addtllen);
	if (pr) {
		drbg_string_fill(&testentropy, test->entprb, test->entprlen);
		ret = crypto_drbg_get_bytes_addtl_test(drng,
			buf, test->expectedlen, &addtl, &test_data);
	} else {
		ret = crypto_drbg_get_bytes_addtl(drng,
			buf, test->expectedlen, &addtl);
	}
	if (ret < 0) {
		printk(KERN_ERR "alg: drbg: could not obtain random data for "
		       "driver %s\n", driver);
		goto outbuf;
	}

	ret = memcmp(test->expected, buf, test->expectedlen);

outbuf:
	crypto_free_rng(drng);
	kzfree(buf);
	return ret;
}


static int alg_test_drbg(const struct alg_test_desc *desc, const char *driver,
			 u32 type, u32 mask)
{
	int err = 0;
	int pr = 0;
	int i = 0;
	const struct drbg_testvec *template = desc->suite.drbg.vecs;
	unsigned int tcount = desc->suite.drbg.count;

	if (0 == memcmp(driver, "drbg_pr_", 8))
		pr = 1;

	for (i = 0; i < tcount; i++) {
		err = drbg_cavs_test(&template[i], pr, driver, type, mask);
		if (err) {
			printk(KERN_ERR "alg: drbg: Test %d failed for %s\n",
			       i, driver);
			err = -EINVAL;
			break;
		}
	}
	return err;

}

static int do_test_kpp(struct crypto_kpp *tfm, const struct kpp_testvec *vec,
		       const char *alg)
{
	struct kpp_request *req;
	void *input_buf = NULL;
	void *output_buf = NULL;
	void *a_public = NULL;
	void *a_ss = NULL;
	void *shared_secret = NULL;
	struct crypto_wait wait;
	unsigned int out_len_max;
	int err = -ENOMEM;
	struct scatterlist src, dst;

	req = kpp_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return err;

	crypto_init_wait(&wait);

	err = crypto_kpp_set_secret(tfm, vec->secret, vec->secret_size);
	if (err < 0)
		goto free_req;

	out_len_max = crypto_kpp_maxsize(tfm);
	output_buf = kzalloc(out_len_max, GFP_KERNEL);
	if (!output_buf) {
		err = -ENOMEM;
		goto free_req;
	}

	/* Use appropriate parameter as base */
	kpp_request_set_input(req, NULL, 0);
	sg_init_one(&dst, output_buf, out_len_max);
	kpp_request_set_output(req, &dst, out_len_max);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	/* Compute party A's public key */
	err = crypto_wait_req(crypto_kpp_generate_public_key(req), &wait);
	if (err) {
		pr_err("alg: %s: Party A: generate public key test failed. err %d\n",
		       alg, err);
		goto free_output;
	}

	if (vec->genkey) {
		/* Save party A's public key */
		a_public = kmemdup(sg_virt(req->dst), out_len_max, GFP_KERNEL);
		if (!a_public) {
			err = -ENOMEM;
			goto free_output;
		}
	} else {
		/* Verify calculated public key */
		if (memcmp(vec->expected_a_public, sg_virt(req->dst),
			   vec->expected_a_public_size)) {
			pr_err("alg: %s: Party A: generate public key test failed. Invalid output\n",
			       alg);
			err = -EINVAL;
			goto free_output;
		}
	}

	/* Calculate shared secret key by using counter part (b) public key. */
	input_buf = kmemdup(vec->b_public, vec->b_public_size, GFP_KERNEL);
	if (!input_buf) {
		err = -ENOMEM;
		goto free_output;
	}

	sg_init_one(&src, input_buf, vec->b_public_size);
	sg_init_one(&dst, output_buf, out_len_max);
	kpp_request_set_input(req, &src, vec->b_public_size);
	kpp_request_set_output(req, &dst, out_len_max);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);
	err = crypto_wait_req(crypto_kpp_compute_shared_secret(req), &wait);
	if (err) {
		pr_err("alg: %s: Party A: compute shared secret test failed. err %d\n",
		       alg, err);
		goto free_all;
	}

	if (vec->genkey) {
		/* Save the shared secret obtained by party A */
		a_ss = kmemdup(sg_virt(req->dst), vec->expected_ss_size, GFP_KERNEL);
		if (!a_ss) {
			err = -ENOMEM;
			goto free_all;
		}

		/*
		 * Calculate party B's shared secret by using party A's
		 * public key.
		 */
		err = crypto_kpp_set_secret(tfm, vec->b_secret,
					    vec->b_secret_size);
		if (err < 0)
			goto free_all;

		sg_init_one(&src, a_public, vec->expected_a_public_size);
		sg_init_one(&dst, output_buf, out_len_max);
		kpp_request_set_input(req, &src, vec->expected_a_public_size);
		kpp_request_set_output(req, &dst, out_len_max);
		kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					 crypto_req_done, &wait);
		err = crypto_wait_req(crypto_kpp_compute_shared_secret(req),
				      &wait);
		if (err) {
			pr_err("alg: %s: Party B: compute shared secret failed. err %d\n",
			       alg, err);
			goto free_all;
		}

		shared_secret = a_ss;
	} else {
		shared_secret = (void *)vec->expected_ss;
	}

	/*
	 * verify shared secret from which the user will derive
	 * secret key by executing whatever hash it has chosen
	 */
	if (memcmp(shared_secret, sg_virt(req->dst),
		   vec->expected_ss_size)) {
		pr_err("alg: %s: compute shared secret test failed. Invalid output\n",
		       alg);
		err = -EINVAL;
	}

free_all:
	kfree(a_ss);
	kfree(input_buf);
free_output:
	kfree(a_public);
	kfree(output_buf);
free_req:
	kpp_request_free(req);
	return err;
}

static int test_kpp(struct crypto_kpp *tfm, const char *alg,
		    const struct kpp_testvec *vecs, unsigned int tcount)
{
	int ret, i;

	for (i = 0; i < tcount; i++) {
		ret = do_test_kpp(tfm, vecs++, alg);
		if (ret) {
			pr_err("alg: %s: test failed on vector %d, err=%d\n",
			       alg, i + 1, ret);
			return ret;
		}
	}
	return 0;
}

static int alg_test_kpp(const struct alg_test_desc *desc, const char *driver,
			u32 type, u32 mask)
{
	struct crypto_kpp *tfm;
	int err = 0;

	tfm = crypto_alloc_kpp(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: kpp: Failed to load tfm for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}
	if (desc->suite.kpp.vecs)
		err = test_kpp(tfm, desc->alg, desc->suite.kpp.vecs,
			       desc->suite.kpp.count);

	crypto_free_kpp(tfm);
	return err;
}

static int test_akcipher_one(struct crypto_akcipher *tfm,
			     const struct akcipher_testvec *vecs)
{
	char *xbuf[XBUFSIZE];
	struct akcipher_request *req;
	void *outbuf_enc = NULL;
	void *outbuf_dec = NULL;
	struct crypto_wait wait;
	unsigned int out_len_max, out_len = 0;
	int err = -ENOMEM;
	struct scatterlist src, dst, src_tab[2];
	const char *m, *c;
	unsigned int m_size, c_size;
	const char *op;

	if (testmgr_alloc_buf(xbuf))
		return err;

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto free_xbuf;

	crypto_init_wait(&wait);

	if (vecs->public_key_vec)
		err = crypto_akcipher_set_pub_key(tfm, vecs->key,
						  vecs->key_len);
	else
		err = crypto_akcipher_set_priv_key(tfm, vecs->key,
						   vecs->key_len);
	if (err)
		goto free_req;

	err = -ENOMEM;
	out_len_max = crypto_akcipher_maxsize(tfm);

	/*
	 * First run test which do not require a private key, such as
	 * encrypt or verify.
	 */
	outbuf_enc = kzalloc(out_len_max, GFP_KERNEL);
	if (!outbuf_enc)
		goto free_req;

	if (!vecs->siggen_sigver_test) {
		m = vecs->m;
		m_size = vecs->m_size;
		c = vecs->c;
		c_size = vecs->c_size;
		op = "encrypt";
	} else {
		/* Swap args so we could keep plaintext (digest)
		 * in vecs->m, and cooked signature in vecs->c.
		 */
		m = vecs->c; /* signature */
		m_size = vecs->c_size;
		c = vecs->m; /* digest */
		c_size = vecs->m_size;
		op = "verify";
	}

	if (WARN_ON(m_size > PAGE_SIZE))
		goto free_all;
	memcpy(xbuf[0], m, m_size);

	sg_init_table(src_tab, 2);
	sg_set_buf(&src_tab[0], xbuf[0], 8);
	sg_set_buf(&src_tab[1], xbuf[0] + 8, m_size - 8);
	sg_init_one(&dst, outbuf_enc, out_len_max);
	akcipher_request_set_crypt(req, src_tab, &dst, m_size,
				   out_len_max);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      crypto_req_done, &wait);

	err = crypto_wait_req(vecs->siggen_sigver_test ?
			      /* Run asymmetric signature verification */
			      crypto_akcipher_verify(req) :
			      /* Run asymmetric encrypt */
			      crypto_akcipher_encrypt(req), &wait);
	if (err) {
		pr_err("alg: akcipher: %s test failed. err %d\n", op, err);
		goto free_all;
	}
	if (req->dst_len != c_size) {
		pr_err("alg: akcipher: %s test failed. Invalid output len\n",
		       op);
		err = -EINVAL;
		goto free_all;
	}
	/* verify that encrypted message is equal to expected */
	if (memcmp(c, outbuf_enc, c_size)) {
		pr_err("alg: akcipher: %s test failed. Invalid output\n", op);
		hexdump(outbuf_enc, c_size);
		err = -EINVAL;
		goto free_all;
	}

	/*
	 * Don't invoke (decrypt or sign) test which require a private key
	 * for vectors with only a public key.
	 */
	if (vecs->public_key_vec) {
		err = 0;
		goto free_all;
	}
	outbuf_dec = kzalloc(out_len_max, GFP_KERNEL);
	if (!outbuf_dec) {
		err = -ENOMEM;
		goto free_all;
	}

	op = vecs->siggen_sigver_test ? "sign" : "decrypt";
	if (WARN_ON(c_size > PAGE_SIZE))
		goto free_all;
	memcpy(xbuf[0], c, c_size);

	sg_init_one(&src, xbuf[0], c_size);
	sg_init_one(&dst, outbuf_dec, out_len_max);
	crypto_init_wait(&wait);
	akcipher_request_set_crypt(req, &src, &dst, c_size, out_len_max);

	err = crypto_wait_req(vecs->siggen_sigver_test ?
			      /* Run asymmetric signature generation */
			      crypto_akcipher_sign(req) :
			      /* Run asymmetric decrypt */
			      crypto_akcipher_decrypt(req), &wait);
	if (err) {
		pr_err("alg: akcipher: %s test failed. err %d\n", op, err);
		goto free_all;
	}
	out_len = req->dst_len;
	if (out_len < m_size) {
		pr_err("alg: akcipher: %s test failed. Invalid output len %u\n",
		       op, out_len);
		err = -EINVAL;
		goto free_all;
	}
	/* verify that decrypted message is equal to the original msg */
	if (memchr_inv(outbuf_dec, 0, out_len - m_size) ||
	    memcmp(m, outbuf_dec + out_len - m_size, m_size)) {
		pr_err("alg: akcipher: %s test failed. Invalid output\n", op);
		hexdump(outbuf_dec, out_len);
		err = -EINVAL;
	}
free_all:
	kfree(outbuf_dec);
	kfree(outbuf_enc);
free_req:
	akcipher_request_free(req);
free_xbuf:
	testmgr_free_buf(xbuf);
	return err;
}

static int test_akcipher(struct crypto_akcipher *tfm, const char *alg,
			 const struct akcipher_testvec *vecs,
			 unsigned int tcount)
{
	const char *algo =
		crypto_tfm_alg_driver_name(crypto_akcipher_tfm(tfm));
	int ret, i;

	for (i = 0; i < tcount; i++) {
		ret = test_akcipher_one(tfm, vecs++);
		if (!ret)
			continue;

		pr_err("alg: akcipher: test %d failed for %s, err=%d\n",
		       i + 1, algo, ret);
		return ret;
	}
	return 0;
}

static int alg_test_akcipher(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	struct crypto_akcipher *tfm;
	int err = 0;

	tfm = crypto_alloc_akcipher(driver, type, mask);
	if (IS_ERR(tfm)) {
		pr_err("alg: akcipher: Failed to load tfm for %s: %ld\n",
		       driver, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}
	if (desc->suite.akcipher.vecs)
		err = test_akcipher(tfm, desc->alg, desc->suite.akcipher.vecs,
				    desc->suite.akcipher.count);

	crypto_free_akcipher(tfm);
	return err;
}

static int alg_test_null(const struct alg_test_desc *desc,
			     const char *driver, u32 type, u32 mask)
{
	return 0;
}

#define __VECS(tv)	{ .vecs = tv, .count = ARRAY_SIZE(tv) }

/* Please keep this list sorted by algorithm name. */
static const struct alg_test_desc alg_test_descs[] = {
	{
		.alg = "adiantum(xchacha12,aes)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(adiantum_xchacha12_aes_tv_template)
		},
	}, {
		.alg = "adiantum(xchacha20,aes)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(adiantum_xchacha20_aes_tv_template)
		},
	}, {
		.alg = "aegis128",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(aegis128_tv_template)
		}
	}, {
		.alg = "aegis128l",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(aegis128l_tv_template)
		}
	}, {
		.alg = "aegis256",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(aegis256_tv_template)
		}
	}, {
		.alg = "ansi_cprng",
		.test = alg_test_cprng,
		.suite = {
			.cprng = __VECS(ansi_cprng_aes_tv_template)
		}
	}, {
		.alg = "authenc(hmac(md5),ecb(cipher_null))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_md5_ecb_cipher_null_tv_template)
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(hmac_sha1_aes_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_sha1_des_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha1),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(hmac_sha1_des3_ede_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha1),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha1),ecb(cipher_null))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_sha1_ecb_cipher_null_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha1),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha224),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_sha224_des_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha224),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(hmac_sha224_des3_ede_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(hmac_sha256_aes_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_sha256_des_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha256),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(hmac_sha256_des3_ede_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha256),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha256),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha384),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_sha384_des_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha384),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(hmac_sha384_des3_ede_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha384),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha384),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha512),cbc(aes))",
		.fips_allowed = 1,
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_sha512_aes_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha512),cbc(des))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(hmac_sha512_des_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha512),cbc(des3_ede))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(hmac_sha512_des3_ede_cbc_tv_temp)
		}
	}, {
		.alg = "authenc(hmac(sha512),ctr(aes))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "authenc(hmac(sha512),rfc3686(ctr(aes)))",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "cbc(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_cbc_tv_template)
		},
	}, {
		.alg = "cbc(anubis)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(anubis_cbc_tv_template)
		},
	}, {
		.alg = "cbc(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(bf_cbc_tv_template)
		},
	}, {
		.alg = "cbc(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_cbc_tv_template)
		},
	}, {
		.alg = "cbc(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast5_cbc_tv_template)
		},
	}, {
		.alg = "cbc(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_cbc_tv_template)
		},
	}, {
		.alg = "cbc(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(des_cbc_tv_template)
		},
	}, {
		.alg = "cbc(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(des3_ede_cbc_tv_template)
		},
	}, {
		/* Same as cbc(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "cbc(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "cbc(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_cbc_tv_template)
		},
	}, {
		.alg = "cbc(sm4)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(sm4_cbc_tv_template)
		}
	}, {
		.alg = "cbc(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_cbc_tv_template)
		},
	}, {
		.alg = "cbcmac(aes)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(aes_cbcmac_tv_template)
		}
	}, {
		.alg = "ccm(aes)",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(aes_ccm_tv_template)
		}
	}, {
		.alg = "cfb(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_cfb_tv_template)
		},
	}, {
		.alg = "chacha20",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(chacha20_tv_template)
		},
	}, {
		.alg = "cmac(aes)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(aes_cmac128_tv_template)
		}
	}, {
		.alg = "cmac(des3_ede)",
		.fips_allowed = 1,
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(des3_ede_cmac64_tv_template)
		}
	}, {
		.alg = "compress_null",
		.test = alg_test_null,
	}, {
		.alg = "crc32",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(crc32_tv_template)
		}
	}, {
		.alg = "crc32c",
		.test = alg_test_crc32c,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(crc32c_tv_template)
		}
	}, {
		.alg = "crct10dif",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(crct10dif_tv_template)
		}
	}, {
		.alg = "ctr(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_ctr_tv_template)
		}
	}, {
		.alg = "ctr(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(bf_ctr_tv_template)
		}
	}, {
		.alg = "ctr(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_ctr_tv_template)
		}
	}, {
		.alg = "ctr(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast5_ctr_tv_template)
		}
	}, {
		.alg = "ctr(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_ctr_tv_template)
		}
	}, {
		.alg = "ctr(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(des_ctr_tv_template)
		}
	}, {
		.alg = "ctr(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(des3_ede_ctr_tv_template)
		}
	}, {
		/* Same as ctr(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "ctr(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ctr(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_ctr_tv_template)
		}
	}, {
		.alg = "ctr(sm4)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(sm4_ctr_tv_template)
		}
	}, {
		.alg = "ctr(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_ctr_tv_template)
		}
	}, {
		.alg = "cts(cbc(aes))",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(cts_mode_tv_template)
		}
	}, {
		.alg = "deflate",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(deflate_comp_tv_template),
				.decomp = __VECS(deflate_decomp_tv_template)
			}
		}
	}, {
		.alg = "dh",
		.test = alg_test_kpp,
		.fips_allowed = 1,
		.suite = {
			.kpp = __VECS(dh_tv_template)
		}
	}, {
		.alg = "digest_null",
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_ctr_aes128",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_ctr_aes128_tv_template)
		}
	}, {
		.alg = "drbg_nopr_ctr_aes192",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_ctr_aes192_tv_template)
		}
	}, {
		.alg = "drbg_nopr_ctr_aes256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_ctr_aes256_tv_template)
		}
	}, {
		/*
		 * There is no need to specifically test the DRBG with every
		 * backend cipher -- covered by drbg_nopr_hmac_sha256 test
		 */
		.alg = "drbg_nopr_hmac_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_hmac_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_hmac_sha256_tv_template)
		}
	}, {
		/* covered by drbg_nopr_hmac_sha256 test */
		.alg = "drbg_nopr_hmac_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_hmac_sha512",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "drbg_nopr_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_nopr_sha256_tv_template)
		}
	}, {
		/* covered by drbg_nopr_sha256 test */
		.alg = "drbg_nopr_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_nopr_sha512",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_ctr_aes128",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_pr_ctr_aes128_tv_template)
		}
	}, {
		/* covered by drbg_pr_ctr_aes128 test */
		.alg = "drbg_pr_ctr_aes192",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_ctr_aes256",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_hmac_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_hmac_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_pr_hmac_sha256_tv_template)
		}
	}, {
		/* covered by drbg_pr_hmac_sha256 test */
		.alg = "drbg_pr_hmac_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_hmac_sha512",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "drbg_pr_sha1",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_sha256",
		.test = alg_test_drbg,
		.fips_allowed = 1,
		.suite = {
			.drbg = __VECS(drbg_pr_sha256_tv_template)
		}
	}, {
		/* covered by drbg_pr_sha256 test */
		.alg = "drbg_pr_sha384",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "drbg_pr_sha512",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "ecb(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_tv_template)
		}
	}, {
		.alg = "ecb(anubis)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(anubis_tv_template)
		}
	}, {
		.alg = "ecb(arc4)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(arc4_tv_template)
		}
	}, {
		.alg = "ecb(blowfish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(bf_tv_template)
		}
	}, {
		.alg = "ecb(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_tv_template)
		}
	}, {
		.alg = "ecb(cast5)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast5_tv_template)
		}
	}, {
		.alg = "ecb(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_tv_template)
		}
	}, {
		.alg = "ecb(cipher_null)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ecb(des)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(des_tv_template)
		}
	}, {
		.alg = "ecb(des3_ede)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(des3_ede_tv_template)
		}
	}, {
		.alg = "ecb(fcrypt)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = {
				.vecs = fcrypt_pcbc_tv_template,
				.count = 1
			}
		}
	}, {
		.alg = "ecb(khazad)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(khazad_tv_template)
		}
	}, {
		/* Same as ecb(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "ecb(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "ecb(seed)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(seed_tv_template)
		}
	}, {
		.alg = "ecb(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_tv_template)
		}
	}, {
		.alg = "ecb(sm4)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(sm4_tv_template)
		}
	}, {
		.alg = "ecb(tea)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tea_tv_template)
		}
	}, {
		.alg = "ecb(tnepres)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tnepres_tv_template)
		}
	}, {
		.alg = "ecb(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_tv_template)
		}
	}, {
		.alg = "ecb(xeta)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(xeta_tv_template)
		}
	}, {
		.alg = "ecb(xtea)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(xtea_tv_template)
		}
	}, {
		.alg = "ecdh",
		.test = alg_test_kpp,
		.fips_allowed = 1,
		.suite = {
			.kpp = __VECS(ecdh_tv_template)
		}
	}, {
		.alg = "gcm(aes)",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(aes_gcm_tv_template)
		}
	}, {
		.alg = "ghash",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(ghash_tv_template)
		}
	}, {
		.alg = "hmac(md5)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_md5_tv_template)
		}
	}, {
		.alg = "hmac(rmd128)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_rmd128_tv_template)
		}
	}, {
		.alg = "hmac(rmd160)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_rmd160_tv_template)
		}
	}, {
		.alg = "hmac(sha1)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha1_tv_template)
		}
	}, {
		.alg = "hmac(sha224)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha224_tv_template)
		}
	}, {
		.alg = "hmac(sha256)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha256_tv_template)
		}
	}, {
		.alg = "hmac(sha3-224)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_224_tv_template)
		}
	}, {
		.alg = "hmac(sha3-256)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_256_tv_template)
		}
	}, {
		.alg = "hmac(sha3-384)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_384_tv_template)
		}
	}, {
		.alg = "hmac(sha3-512)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha3_512_tv_template)
		}
	}, {
		.alg = "hmac(sha384)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha384_tv_template)
		}
	}, {
		.alg = "hmac(sha512)",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(hmac_sha512_tv_template)
		}
	}, {
		.alg = "hmac(streebog256)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_streebog256_tv_template)
		}
	}, {
		.alg = "hmac(streebog512)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(hmac_streebog512_tv_template)
		}
	}, {
		.alg = "jitterentropy_rng",
		.fips_allowed = 1,
		.test = alg_test_null,
	}, {
		.alg = "kw(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_kw_tv_template)
		}
	}, {
		.alg = "lrw(aes)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(aes_lrw_tv_template)
		}
	}, {
		.alg = "lrw(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_lrw_tv_template)
		}
	}, {
		.alg = "lrw(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_lrw_tv_template)
		}
	}, {
		.alg = "lrw(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_lrw_tv_template)
		}
	}, {
		.alg = "lrw(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_lrw_tv_template)
		}
	}, {
		.alg = "lz4",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(lz4_comp_tv_template),
				.decomp = __VECS(lz4_decomp_tv_template)
			}
		}
	}, {
		.alg = "lz4hc",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(lz4hc_comp_tv_template),
				.decomp = __VECS(lz4hc_decomp_tv_template)
			}
		}
	}, {
		.alg = "lzo",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(lzo_comp_tv_template),
				.decomp = __VECS(lzo_decomp_tv_template)
			}
		}
	}, {
		.alg = "md4",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(md4_tv_template)
		}
	}, {
		.alg = "md5",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(md5_tv_template)
		}
	}, {
		.alg = "michael_mic",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(michael_mic_tv_template)
		}
	}, {
		.alg = "morus1280",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(morus1280_tv_template)
		}
	}, {
		.alg = "morus640",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(morus640_tv_template)
		}
	}, {
		.alg = "nhpoly1305",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(nhpoly1305_tv_template)
		}
	}, {
		.alg = "ofb(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_ofb_tv_template)
		}
	}, {
		/* Same as ofb(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "ofb(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "pcbc(fcrypt)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(fcrypt_pcbc_tv_template)
		}
	}, {
		.alg = "pkcs1pad(rsa,sha224)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "pkcs1pad(rsa,sha256)",
		.test = alg_test_akcipher,
		.fips_allowed = 1,
		.suite = {
			.akcipher = __VECS(pkcs1pad_rsa_tv_template)
		}
	}, {
		.alg = "pkcs1pad(rsa,sha384)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "pkcs1pad(rsa,sha512)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "poly1305",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(poly1305_tv_template)
		}
	}, {
		.alg = "rfc3686(ctr(aes))",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_ctr_rfc3686_tv_template)
		}
	}, {
		.alg = "rfc4106(gcm(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(aes_gcm_rfc4106_tv_template)
		}
	}, {
		.alg = "rfc4309(ccm(aes))",
		.test = alg_test_aead,
		.fips_allowed = 1,
		.suite = {
			.aead = __VECS(aes_ccm_rfc4309_tv_template)
		}
	}, {
		.alg = "rfc4543(gcm(aes))",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(aes_gcm_rfc4543_tv_template)
		}
	}, {
		.alg = "rfc7539(chacha20,poly1305)",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(rfc7539_tv_template)
		}
	}, {
		.alg = "rfc7539esp(chacha20,poly1305)",
		.test = alg_test_aead,
		.suite = {
			.aead = __VECS(rfc7539esp_tv_template)
		}
	}, {
		.alg = "rmd128",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd128_tv_template)
		}
	}, {
		.alg = "rmd160",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd160_tv_template)
		}
	}, {
		.alg = "rmd256",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd256_tv_template)
		}
	}, {
		.alg = "rmd320",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(rmd320_tv_template)
		}
	}, {
		.alg = "rsa",
		.test = alg_test_akcipher,
		.fips_allowed = 1,
		.suite = {
			.akcipher = __VECS(rsa_tv_template)
		}
	}, {
		.alg = "salsa20",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(salsa20_stream_tv_template)
		}
	}, {
		.alg = "sha1",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha1_tv_template)
		}
	}, {
		.alg = "sha224",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha224_tv_template)
		}
	}, {
		.alg = "sha256",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha256_tv_template)
		}
	}, {
		.alg = "sha3-224",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_224_tv_template)
		}
	}, {
		.alg = "sha3-256",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_256_tv_template)
		}
	}, {
		.alg = "sha3-384",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_384_tv_template)
		}
	}, {
		.alg = "sha3-512",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha3_512_tv_template)
		}
	}, {
		.alg = "sha384",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha384_tv_template)
		}
	}, {
		.alg = "sha512",
		.test = alg_test_hash,
		.fips_allowed = 1,
		.suite = {
			.hash = __VECS(sha512_tv_template)
		}
	}, {
		.alg = "sm3",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(sm3_tv_template)
		}
	}, {
		.alg = "streebog256",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(streebog256_tv_template)
		}
	}, {
		.alg = "streebog512",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(streebog512_tv_template)
		}
	}, {
		.alg = "tgr128",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(tgr128_tv_template)
		}
	}, {
		.alg = "tgr160",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(tgr160_tv_template)
		}
	}, {
		.alg = "tgr192",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(tgr192_tv_template)
		}
	}, {
		.alg = "vmac64(aes)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(vmac64_aes_tv_template)
		}
	}, {
		.alg = "wp256",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(wp256_tv_template)
		}
	}, {
		.alg = "wp384",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(wp384_tv_template)
		}
	}, {
		.alg = "wp512",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(wp512_tv_template)
		}
	}, {
		.alg = "xcbc(aes)",
		.test = alg_test_hash,
		.suite = {
			.hash = __VECS(aes_xcbc128_tv_template)
		}
	}, {
		.alg = "xchacha12",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(xchacha12_tv_template)
		},
	}, {
		.alg = "xchacha20",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(xchacha20_tv_template)
		},
	}, {
		.alg = "xts(aes)",
		.test = alg_test_skcipher,
		.fips_allowed = 1,
		.suite = {
			.cipher = __VECS(aes_xts_tv_template)
		}
	}, {
		.alg = "xts(camellia)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(camellia_xts_tv_template)
		}
	}, {
		.alg = "xts(cast6)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(cast6_xts_tv_template)
		}
	}, {
		/* Same as xts(aes) except the key is stored in
		 * hardware secure memory which we reference by index
		 */
		.alg = "xts(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "xts(serpent)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(serpent_xts_tv_template)
		}
	}, {
		.alg = "xts(twofish)",
		.test = alg_test_skcipher,
		.suite = {
			.cipher = __VECS(tf_xts_tv_template)
		}
	}, {
		.alg = "xts4096(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "xts512(paes)",
		.test = alg_test_null,
		.fips_allowed = 1,
	}, {
		.alg = "zlib-deflate",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(zlib_deflate_comp_tv_template),
				.decomp = __VECS(zlib_deflate_decomp_tv_template)
			}
		}
	}, {
		.alg = "zstd",
		.test = alg_test_comp,
		.fips_allowed = 1,
		.suite = {
			.comp = {
				.comp = __VECS(zstd_comp_tv_template),
				.decomp = __VECS(zstd_decomp_tv_template)
			}
		}
	}
};

static void alg_check_test_descs_order(void)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(alg_test_descs); i++) {
		int diff = strcmp(alg_test_descs[i - 1].alg,
				  alg_test_descs[i].alg);

		if (WARN_ON(diff > 0)) {
			pr_warn("testmgr: alg_test_descs entries in wrong order: '%s' before '%s'\n",
				alg_test_descs[i - 1].alg,
				alg_test_descs[i].alg);
		}

		if (WARN_ON(diff == 0)) {
			pr_warn("testmgr: duplicate alg_test_descs entry: '%s'\n",
				alg_test_descs[i].alg);
		}
	}
}

static void alg_check_testvec_configs(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(default_cipher_testvec_configs); i++)
		WARN_ON(!valid_testvec_config(
				&default_cipher_testvec_configs[i]));

	for (i = 0; i < ARRAY_SIZE(default_hash_testvec_configs); i++)
		WARN_ON(!valid_testvec_config(
				&default_hash_testvec_configs[i]));
}

static void testmgr_onetime_init(void)
{
	alg_check_test_descs_order();
	alg_check_testvec_configs();

#ifdef CONFIG_CRYPTO_MANAGER_EXTRA_TESTS
	pr_warn("alg: extra crypto tests enabled.  This is intended for developer use only.\n");
#endif
}

static int alg_find_test(const char *alg)
{
	int start = 0;
	int end = ARRAY_SIZE(alg_test_descs);

	while (start < end) {
		int i = (start + end) / 2;
		int diff = strcmp(alg_test_descs[i].alg, alg);

		if (diff > 0) {
			end = i;
			continue;
		}

		if (diff < 0) {
			start = i + 1;
			continue;
		}

		return i;
	}

	return -1;
}

int alg_test(const char *driver, const char *alg, u32 type, u32 mask)
{
	int i;
	int j;
	int rc;

	if (!fips_enabled && notests) {
		printk_once(KERN_INFO "alg: self-tests disabled\n");
		return 0;
	}

	DO_ONCE(testmgr_onetime_init);

	if ((type & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_CIPHER) {
		char nalg[CRYPTO_MAX_ALG_NAME];

		if (snprintf(nalg, sizeof(nalg), "ecb(%s)", alg) >=
		    sizeof(nalg))
			return -ENAMETOOLONG;

		i = alg_find_test(nalg);
		if (i < 0)
			goto notest;

		if (fips_enabled && !alg_test_descs[i].fips_allowed)
			goto non_fips_alg;

		rc = alg_test_cipher(alg_test_descs + i, driver, type, mask);
		goto test_done;
	}

	i = alg_find_test(alg);
	j = alg_find_test(driver);
	if (i < 0 && j < 0)
		goto notest;

	if (fips_enabled && ((i >= 0 && !alg_test_descs[i].fips_allowed) ||
			     (j >= 0 && !alg_test_descs[j].fips_allowed)))
		goto non_fips_alg;

	rc = 0;
	if (i >= 0)
		rc |= alg_test_descs[i].test(alg_test_descs + i, driver,
					     type, mask);
	if (j >= 0 && j != i)
		rc |= alg_test_descs[j].test(alg_test_descs + j, driver,
					     type, mask);

test_done:
	if (rc && (fips_enabled || panic_on_fail))
		panic("alg: self-tests for %s (%s) failed in %s mode!\n",
		      driver, alg, fips_enabled ? "fips" : "panic_on_fail");

	if (fips_enabled && !rc)
		pr_info("alg: self-tests for %s (%s) passed\n", driver, alg);

	return rc;

notest:
	printk(KERN_INFO "alg: No test for %s (%s)\n", alg, driver);
	return 0;
non_fips_alg:
	return -EINVAL;
}

#endif /* CONFIG_CRYPTO_MANAGER_DISABLE_TESTS */

EXPORT_SYMBOL_GPL(alg_test);
