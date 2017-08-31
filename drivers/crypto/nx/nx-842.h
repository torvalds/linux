
#ifndef __NX_842_H__
#define __NX_842_H__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/ratelimit.h>

/* Restrictions on Data Descriptor List (DDL) and Entry (DDE) buffers
 *
 * From NX P8 workbook, sec 4.9.1 "842 details"
 *   Each DDE buffer is 128 byte aligned
 *   Each DDE buffer size is a multiple of 32 bytes (except the last)
 *   The last DDE buffer size is a multiple of 8 bytes
 */
#define DDE_BUFFER_ALIGN	(128)
#define DDE_BUFFER_SIZE_MULT	(32)
#define DDE_BUFFER_LAST_MULT	(8)

/* Arbitrary DDL length limit
 * Allows max buffer size of MAX-1 to MAX pages
 * (depending on alignment)
 */
#define DDL_LEN_MAX		(17)

/* CCW 842 CI/FC masks
 * NX P8 workbook, section 4.3.1, figure 4-6
 * "CI/FC Boundary by NX CT type"
 */
#define CCW_CI_842		(0x00003ff8)
#define CCW_FC_842		(0x00000007)

/* CCW Function Codes (FC) for 842
 * NX P8 workbook, section 4.9, table 4-28
 * "Function Code Definitions for 842 Memory Compression"
 */
#define CCW_FC_842_COMP_NOCRC	(0)
#define CCW_FC_842_COMP_CRC	(1)
#define CCW_FC_842_DECOMP_NOCRC	(2)
#define CCW_FC_842_DECOMP_CRC	(3)
#define CCW_FC_842_MOVE		(4)

/* CSB CC Error Types for 842
 * NX P8 workbook, section 4.10.3, table 4-30
 * "Reported Error Types Summary Table"
 */
/* These are all duplicates of existing codes defined in icswx.h. */
#define CSB_CC_TRANSLATION_DUP1	(80)
#define CSB_CC_TRANSLATION_DUP2	(82)
#define CSB_CC_TRANSLATION_DUP3	(84)
#define CSB_CC_TRANSLATION_DUP4	(86)
#define CSB_CC_TRANSLATION_DUP5	(92)
#define CSB_CC_TRANSLATION_DUP6	(94)
#define CSB_CC_PROTECTION_DUP1	(81)
#define CSB_CC_PROTECTION_DUP2	(83)
#define CSB_CC_PROTECTION_DUP3	(85)
#define CSB_CC_PROTECTION_DUP4	(87)
#define CSB_CC_PROTECTION_DUP5	(93)
#define CSB_CC_PROTECTION_DUP6	(95)
#define CSB_CC_RD_EXTERNAL_DUP1	(89)
#define CSB_CC_RD_EXTERNAL_DUP2	(90)
#define CSB_CC_RD_EXTERNAL_DUP3	(91)
/* These are specific to NX */
/* 842 codes */
#define CSB_CC_TPBC_GT_SPBC	(64) /* no error, but >1 comp ratio */
#define CSB_CC_CRC_MISMATCH	(65) /* decomp crc mismatch */
#define CSB_CC_TEMPL_INVALID	(66) /* decomp invalid template value */
#define CSB_CC_TEMPL_OVERFLOW	(67) /* decomp template shows data after end */
/* sym crypt codes */
#define CSB_CC_DECRYPT_OVERFLOW	(64)
/* asym crypt codes */
#define CSB_CC_MINV_OVERFLOW	(128)
/*
 * HW error - Job did not finish in the maximum time allowed.
 * Job terminated.
 */
#define CSB_CC_HW_EXPIRED_TIMER		(224)
/* These are reserved for hypervisor use */
#define CSB_CC_HYP_RESERVE_START	(240)
#define CSB_CC_HYP_RESERVE_END		(253)
#define CSB_CC_HYP_RESERVE_P9_END	(251)
/* No valid interrupt server (P9 or later). */
#define CSB_CC_HYP_RESERVE_NO_INTR_SERVER	(252)
#define CSB_CC_HYP_NO_HW		(254)
#define CSB_CC_HYP_HANG_ABORTED		(255)

/* CCB Completion Modes (CM) for 842
 * NX P8 workbook, section 4.3, figure 4-5
 * "CRB Details - Normal Cop_Req (CL=00, C=1)"
 */
#define CCB_CM_EXTRA_WRITE	(CCB_CM0_ALL_COMPLETIONS & CCB_CM12_STORE)
#define CCB_CM_INTERRUPT	(CCB_CM0_ALL_COMPLETIONS & CCB_CM12_INTERRUPT)

#define LEN_ON_SIZE(pa, size)	((size) - ((pa) & ((size) - 1)))
#define LEN_ON_PAGE(pa)		LEN_ON_SIZE(pa, PAGE_SIZE)

static inline unsigned long nx842_get_pa(void *addr)
{
	if (!is_vmalloc_addr(addr))
		return __pa(addr);

	return page_to_phys(vmalloc_to_page(addr)) + offset_in_page(addr);
}

/**
 * This provides the driver's constraints.  Different nx842 implementations
 * may have varying requirements.  The constraints are:
 *   @alignment:	All buffers should be aligned to this
 *   @multiple:		All buffer lengths should be a multiple of this
 *   @minimum:		Buffer lengths must not be less than this amount
 *   @maximum:		Buffer lengths must not be more than this amount
 *
 * The constraints apply to all buffers and lengths, both input and output,
 * for both compression and decompression, except for the minimum which
 * only applies to compression input and decompression output; the
 * compressed data can be less than the minimum constraint.  It can be
 * assumed that compressed data will always adhere to the multiple
 * constraint.
 *
 * The driver may succeed even if these constraints are violated;
 * however the driver can return failure or suffer reduced performance
 * if any constraint is not met.
 */
struct nx842_constraints {
	int alignment;
	int multiple;
	int minimum;
	int maximum;
};

struct nx842_driver {
	char *name;
	struct module *owner;
	size_t workmem_size;

	struct nx842_constraints *constraints;

	int (*compress)(const unsigned char *in, unsigned int in_len,
			unsigned char *out, unsigned int *out_len,
			void *wrkmem);
	int (*decompress)(const unsigned char *in, unsigned int in_len,
			  unsigned char *out, unsigned int *out_len,
			  void *wrkmem);
};

struct nx842_crypto_header_group {
	__be16 padding;			/* unused bytes at start of group */
	__be32 compressed_length;	/* compressed bytes in group */
	__be32 uncompressed_length;	/* bytes after decompression */
} __packed;

struct nx842_crypto_header {
	__be16 magic;		/* NX842_CRYPTO_MAGIC */
	__be16 ignore;		/* decompressed end bytes to ignore */
	u8 groups;		/* total groups in this header */
	struct nx842_crypto_header_group group[];
} __packed;

#define NX842_CRYPTO_GROUP_MAX	(0x20)

struct nx842_crypto_ctx {
	spinlock_t lock;

	u8 *wmem;
	u8 *sbounce, *dbounce;

	struct nx842_crypto_header header;
	struct nx842_crypto_header_group group[NX842_CRYPTO_GROUP_MAX];

	struct nx842_driver *driver;
};

int nx842_crypto_init(struct crypto_tfm *tfm, struct nx842_driver *driver);
void nx842_crypto_exit(struct crypto_tfm *tfm);
int nx842_crypto_compress(struct crypto_tfm *tfm,
			  const u8 *src, unsigned int slen,
			  u8 *dst, unsigned int *dlen);
int nx842_crypto_decompress(struct crypto_tfm *tfm,
			    const u8 *src, unsigned int slen,
			    u8 *dst, unsigned int *dlen);

#endif /* __NX_842_H__ */
