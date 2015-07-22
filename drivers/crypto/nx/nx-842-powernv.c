/*
 * Driver for IBM PowerNV 842 compression accelerator
 *
 * Copyright (C) 2015 Dan Streetman, IBM Corp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "nx-842.h"

#include <linux/timer.h>

#include <asm/prom.h>
#include <asm/icswx.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
MODULE_DESCRIPTION("842 H/W Compression driver for IBM PowerNV processors");
MODULE_ALIAS_CRYPTO("842");
MODULE_ALIAS_CRYPTO("842-nx");

#define WORKMEM_ALIGN	(CRB_ALIGN)
#define CSB_WAIT_MAX	(5000) /* ms */

struct nx842_workmem {
	/* Below fields must be properly aligned */
	struct coprocessor_request_block crb; /* CRB_ALIGN align */
	struct data_descriptor_entry ddl_in[DDL_LEN_MAX]; /* DDE_ALIGN align */
	struct data_descriptor_entry ddl_out[DDL_LEN_MAX]; /* DDE_ALIGN align */
	/* Above fields must be properly aligned */

	ktime_t start;

	char padding[WORKMEM_ALIGN]; /* unused, to allow alignment */
} __packed __aligned(WORKMEM_ALIGN);

struct nx842_coproc {
	unsigned int chip_id;
	unsigned int ct;
	unsigned int ci;
	struct list_head list;
};

/* no cpu hotplug on powernv, so this list never changes after init */
static LIST_HEAD(nx842_coprocs);
static unsigned int nx842_ct;

/**
 * setup_indirect_dde - Setup an indirect DDE
 *
 * The DDE is setup with the the DDE count, byte count, and address of
 * first direct DDE in the list.
 */
static void setup_indirect_dde(struct data_descriptor_entry *dde,
			       struct data_descriptor_entry *ddl,
			       unsigned int dde_count, unsigned int byte_count)
{
	dde->flags = 0;
	dde->count = dde_count;
	dde->index = 0;
	dde->length = cpu_to_be32(byte_count);
	dde->address = cpu_to_be64(nx842_get_pa(ddl));
}

/**
 * setup_direct_dde - Setup single DDE from buffer
 *
 * The DDE is setup with the buffer and length.  The buffer must be properly
 * aligned.  The used length is returned.
 * Returns:
 *   N    Successfully set up DDE with N bytes
 */
static unsigned int setup_direct_dde(struct data_descriptor_entry *dde,
				     unsigned long pa, unsigned int len)
{
	unsigned int l = min_t(unsigned int, len, LEN_ON_PAGE(pa));

	dde->flags = 0;
	dde->count = 0;
	dde->index = 0;
	dde->length = cpu_to_be32(l);
	dde->address = cpu_to_be64(pa);

	return l;
}

/**
 * setup_ddl - Setup DDL from buffer
 *
 * Returns:
 *   0		Successfully set up DDL
 */
static int setup_ddl(struct data_descriptor_entry *dde,
		     struct data_descriptor_entry *ddl,
		     unsigned char *buf, unsigned int len,
		     bool in)
{
	unsigned long pa = nx842_get_pa(buf);
	int i, ret, total_len = len;

	if (!IS_ALIGNED(pa, DDE_BUFFER_ALIGN)) {
		pr_debug("%s buffer pa 0x%lx not 0x%x-byte aligned\n",
			 in ? "input" : "output", pa, DDE_BUFFER_ALIGN);
		return -EINVAL;
	}

	/* only need to check last mult; since buffer must be
	 * DDE_BUFFER_ALIGN aligned, and that is a multiple of
	 * DDE_BUFFER_SIZE_MULT, and pre-last page DDE buffers
	 * are guaranteed a multiple of DDE_BUFFER_SIZE_MULT.
	 */
	if (len % DDE_BUFFER_LAST_MULT) {
		pr_debug("%s buffer len 0x%x not a multiple of 0x%x\n",
			 in ? "input" : "output", len, DDE_BUFFER_LAST_MULT);
		if (in)
			return -EINVAL;
		len = round_down(len, DDE_BUFFER_LAST_MULT);
	}

	/* use a single direct DDE */
	if (len <= LEN_ON_PAGE(pa)) {
		ret = setup_direct_dde(dde, pa, len);
		WARN_ON(ret < len);
		return 0;
	}

	/* use the DDL */
	for (i = 0; i < DDL_LEN_MAX && len > 0; i++) {
		ret = setup_direct_dde(&ddl[i], pa, len);
		buf += ret;
		len -= ret;
		pa = nx842_get_pa(buf);
	}

	if (len > 0) {
		pr_debug("0x%x total %s bytes 0x%x too many for DDL.\n",
			 total_len, in ? "input" : "output", len);
		if (in)
			return -EMSGSIZE;
		total_len -= len;
	}
	setup_indirect_dde(dde, ddl, i, total_len);

	return 0;
}

#define CSB_ERR(csb, msg, ...)					\
	pr_err("ERROR: " msg " : %02x %02x %02x %02x %08x\n",	\
	       ##__VA_ARGS__, (csb)->flags,			\
	       (csb)->cs, (csb)->cc, (csb)->ce,			\
	       be32_to_cpu((csb)->count))

#define CSB_ERR_ADDR(csb, msg, ...)				\
	CSB_ERR(csb, msg " at %lx", ##__VA_ARGS__,		\
		(unsigned long)be64_to_cpu((csb)->address))

/**
 * wait_for_csb
 */
static int wait_for_csb(struct nx842_workmem *wmem,
			struct coprocessor_status_block *csb)
{
	ktime_t start = wmem->start, now = ktime_get();
	ktime_t timeout = ktime_add_ms(start, CSB_WAIT_MAX);

	while (!(ACCESS_ONCE(csb->flags) & CSB_V)) {
		cpu_relax();
		now = ktime_get();
		if (ktime_after(now, timeout))
			break;
	}

	/* hw has updated csb and output buffer */
	barrier();

	/* check CSB flags */
	if (!(csb->flags & CSB_V)) {
		CSB_ERR(csb, "CSB still not valid after %ld us, giving up",
			(long)ktime_us_delta(now, start));
		return -ETIMEDOUT;
	}
	if (csb->flags & CSB_F) {
		CSB_ERR(csb, "Invalid CSB format");
		return -EPROTO;
	}
	if (csb->flags & CSB_CH) {
		CSB_ERR(csb, "Invalid CSB chaining state");
		return -EPROTO;
	}

	/* verify CSB completion sequence is 0 */
	if (csb->cs) {
		CSB_ERR(csb, "Invalid CSB completion sequence");
		return -EPROTO;
	}

	/* check CSB Completion Code */
	switch (csb->cc) {
	/* no error */
	case CSB_CC_SUCCESS:
		break;
	case CSB_CC_TPBC_GT_SPBC:
		/* not an error, but the compressed data is
		 * larger than the uncompressed data :(
		 */
		break;

	/* input data errors */
	case CSB_CC_OPERAND_OVERLAP:
		/* input and output buffers overlap */
		CSB_ERR(csb, "Operand Overlap error");
		return -EINVAL;
	case CSB_CC_INVALID_OPERAND:
		CSB_ERR(csb, "Invalid operand");
		return -EINVAL;
	case CSB_CC_NOSPC:
		/* output buffer too small */
		return -ENOSPC;
	case CSB_CC_ABORT:
		CSB_ERR(csb, "Function aborted");
		return -EINTR;
	case CSB_CC_CRC_MISMATCH:
		CSB_ERR(csb, "CRC mismatch");
		return -EINVAL;
	case CSB_CC_TEMPL_INVALID:
		CSB_ERR(csb, "Compressed data template invalid");
		return -EINVAL;
	case CSB_CC_TEMPL_OVERFLOW:
		CSB_ERR(csb, "Compressed data template shows data past end");
		return -EINVAL;

	/* these should not happen */
	case CSB_CC_INVALID_ALIGN:
		/* setup_ddl should have detected this */
		CSB_ERR_ADDR(csb, "Invalid alignment");
		return -EINVAL;
	case CSB_CC_DATA_LENGTH:
		/* setup_ddl should have detected this */
		CSB_ERR(csb, "Invalid data length");
		return -EINVAL;
	case CSB_CC_WR_TRANSLATION:
	case CSB_CC_TRANSLATION:
	case CSB_CC_TRANSLATION_DUP1:
	case CSB_CC_TRANSLATION_DUP2:
	case CSB_CC_TRANSLATION_DUP3:
	case CSB_CC_TRANSLATION_DUP4:
	case CSB_CC_TRANSLATION_DUP5:
	case CSB_CC_TRANSLATION_DUP6:
		/* should not happen, we use physical addrs */
		CSB_ERR_ADDR(csb, "Translation error");
		return -EPROTO;
	case CSB_CC_WR_PROTECTION:
	case CSB_CC_PROTECTION:
	case CSB_CC_PROTECTION_DUP1:
	case CSB_CC_PROTECTION_DUP2:
	case CSB_CC_PROTECTION_DUP3:
	case CSB_CC_PROTECTION_DUP4:
	case CSB_CC_PROTECTION_DUP5:
	case CSB_CC_PROTECTION_DUP6:
		/* should not happen, we use physical addrs */
		CSB_ERR_ADDR(csb, "Protection error");
		return -EPROTO;
	case CSB_CC_PRIVILEGE:
		/* shouldn't happen, we're in HYP mode */
		CSB_ERR(csb, "Insufficient Privilege error");
		return -EPROTO;
	case CSB_CC_EXCESSIVE_DDE:
		/* shouldn't happen, setup_ddl doesn't use many dde's */
		CSB_ERR(csb, "Too many DDEs in DDL");
		return -EINVAL;
	case CSB_CC_TRANSPORT:
		/* shouldn't happen, we setup CRB correctly */
		CSB_ERR(csb, "Invalid CRB");
		return -EINVAL;
	case CSB_CC_SEGMENTED_DDL:
		/* shouldn't happen, setup_ddl creates DDL right */
		CSB_ERR(csb, "Segmented DDL error");
		return -EINVAL;
	case CSB_CC_DDE_OVERFLOW:
		/* shouldn't happen, setup_ddl creates DDL right */
		CSB_ERR(csb, "DDE overflow error");
		return -EINVAL;
	case CSB_CC_SESSION:
		/* should not happen with ICSWX */
		CSB_ERR(csb, "Session violation error");
		return -EPROTO;
	case CSB_CC_CHAIN:
		/* should not happen, we don't use chained CRBs */
		CSB_ERR(csb, "Chained CRB error");
		return -EPROTO;
	case CSB_CC_SEQUENCE:
		/* should not happen, we don't use chained CRBs */
		CSB_ERR(csb, "CRB seqeunce number error");
		return -EPROTO;
	case CSB_CC_UNKNOWN_CODE:
		CSB_ERR(csb, "Unknown subfunction code");
		return -EPROTO;

	/* hardware errors */
	case CSB_CC_RD_EXTERNAL:
	case CSB_CC_RD_EXTERNAL_DUP1:
	case CSB_CC_RD_EXTERNAL_DUP2:
	case CSB_CC_RD_EXTERNAL_DUP3:
		CSB_ERR_ADDR(csb, "Read error outside coprocessor");
		return -EPROTO;
	case CSB_CC_WR_EXTERNAL:
		CSB_ERR_ADDR(csb, "Write error outside coprocessor");
		return -EPROTO;
	case CSB_CC_INTERNAL:
		CSB_ERR(csb, "Internal error in coprocessor");
		return -EPROTO;
	case CSB_CC_PROVISION:
		CSB_ERR(csb, "Storage provision error");
		return -EPROTO;
	case CSB_CC_HW:
		CSB_ERR(csb, "Correctable hardware error");
		return -EPROTO;

	default:
		CSB_ERR(csb, "Invalid CC %d", csb->cc);
		return -EPROTO;
	}

	/* check Completion Extension state */
	if (csb->ce & CSB_CE_TERMINATION) {
		CSB_ERR(csb, "CSB request was terminated");
		return -EPROTO;
	}
	if (csb->ce & CSB_CE_INCOMPLETE) {
		CSB_ERR(csb, "CSB request not complete");
		return -EPROTO;
	}
	if (!(csb->ce & CSB_CE_TPBC)) {
		CSB_ERR(csb, "TPBC not provided, unknown target length");
		return -EPROTO;
	}

	/* successful completion */
	pr_debug_ratelimited("Processed %u bytes in %lu us\n", csb->count,
			     (unsigned long)ktime_us_delta(now, start));

	return 0;
}

/**
 * nx842_powernv_function - compress/decompress data using the 842 algorithm
 *
 * (De)compression provided by the NX842 coprocessor on IBM PowerNV systems.
 * This compresses or decompresses the provided input buffer into the provided
 * output buffer.
 *
 * Upon return from this function @outlen contains the length of the
 * output data.  If there is an error then @outlen will be 0 and an
 * error will be specified by the return code from this function.
 *
 * The @workmem buffer should only be used by one function call at a time.
 *
 * @in: input buffer pointer
 * @inlen: input buffer size
 * @out: output buffer pointer
 * @outlenp: output buffer size pointer
 * @workmem: working memory buffer pointer, size determined by
 *           nx842_powernv_driver.workmem_size
 * @fc: function code, see CCW Function Codes in nx-842.h
 *
 * Returns:
 *   0		Success, output of length @outlenp stored in the buffer at @out
 *   -ENODEV	Hardware unavailable
 *   -ENOSPC	Output buffer is to small
 *   -EMSGSIZE	Input buffer too large
 *   -EINVAL	buffer constraints do not fix nx842_constraints
 *   -EPROTO	hardware error during operation
 *   -ETIMEDOUT	hardware did not complete operation in reasonable time
 *   -EINTR	operation was aborted
 */
static int nx842_powernv_function(const unsigned char *in, unsigned int inlen,
				  unsigned char *out, unsigned int *outlenp,
				  void *workmem, int fc)
{
	struct coprocessor_request_block *crb;
	struct coprocessor_status_block *csb;
	struct nx842_workmem *wmem;
	int ret;
	u64 csb_addr;
	u32 ccw;
	unsigned int outlen = *outlenp;

	wmem = PTR_ALIGN(workmem, WORKMEM_ALIGN);

	*outlenp = 0;

	/* shoudn't happen, we don't load without a coproc */
	if (!nx842_ct) {
		pr_err_ratelimited("coprocessor CT is 0");
		return -ENODEV;
	}

	crb = &wmem->crb;
	csb = &crb->csb;

	/* Clear any previous values */
	memset(crb, 0, sizeof(*crb));

	/* set up DDLs */
	ret = setup_ddl(&crb->source, wmem->ddl_in,
			(unsigned char *)in, inlen, true);
	if (ret)
		return ret;
	ret = setup_ddl(&crb->target, wmem->ddl_out,
			out, outlen, false);
	if (ret)
		return ret;

	/* set up CCW */
	ccw = 0;
	ccw = SET_FIELD(ccw, CCW_CT, nx842_ct);
	ccw = SET_FIELD(ccw, CCW_CI_842, 0); /* use 0 for hw auto-selection */
	ccw = SET_FIELD(ccw, CCW_FC_842, fc);

	/* set up CRB's CSB addr */
	csb_addr = nx842_get_pa(csb) & CRB_CSB_ADDRESS;
	csb_addr |= CRB_CSB_AT; /* Addrs are phys */
	crb->csb_addr = cpu_to_be64(csb_addr);

	wmem->start = ktime_get();

	/* do ICSWX */
	ret = icswx(cpu_to_be32(ccw), crb);

	pr_debug_ratelimited("icswx CR %x ccw %x crb->ccw %x\n", ret,
			     (unsigned int)ccw,
			     (unsigned int)be32_to_cpu(crb->ccw));

	switch (ret) {
	case ICSWX_INITIATED:
		ret = wait_for_csb(wmem, csb);
		break;
	case ICSWX_BUSY:
		pr_debug_ratelimited("842 Coprocessor busy\n");
		ret = -EBUSY;
		break;
	case ICSWX_REJECTED:
		pr_err_ratelimited("ICSWX rejected\n");
		ret = -EPROTO;
		break;
	default:
		pr_err_ratelimited("Invalid ICSWX return code %x\n", ret);
		ret = -EPROTO;
		break;
	}

	if (!ret)
		*outlenp = be32_to_cpu(csb->count);

	return ret;
}

/**
 * nx842_powernv_compress - Compress data using the 842 algorithm
 *
 * Compression provided by the NX842 coprocessor on IBM PowerNV systems.
 * The input buffer is compressed and the result is stored in the
 * provided output buffer.
 *
 * Upon return from this function @outlen contains the length of the
 * compressed data.  If there is an error then @outlen will be 0 and an
 * error will be specified by the return code from this function.
 *
 * @in: input buffer pointer
 * @inlen: input buffer size
 * @out: output buffer pointer
 * @outlenp: output buffer size pointer
 * @workmem: working memory buffer pointer, size determined by
 *           nx842_powernv_driver.workmem_size
 *
 * Returns: see @nx842_powernv_function()
 */
static int nx842_powernv_compress(const unsigned char *in, unsigned int inlen,
				  unsigned char *out, unsigned int *outlenp,
				  void *wmem)
{
	return nx842_powernv_function(in, inlen, out, outlenp,
				      wmem, CCW_FC_842_COMP_NOCRC);
}

/**
 * nx842_powernv_decompress - Decompress data using the 842 algorithm
 *
 * Decompression provided by the NX842 coprocessor on IBM PowerNV systems.
 * The input buffer is decompressed and the result is stored in the
 * provided output buffer.
 *
 * Upon return from this function @outlen contains the length of the
 * decompressed data.  If there is an error then @outlen will be 0 and an
 * error will be specified by the return code from this function.
 *
 * @in: input buffer pointer
 * @inlen: input buffer size
 * @out: output buffer pointer
 * @outlenp: output buffer size pointer
 * @workmem: working memory buffer pointer, size determined by
 *           nx842_powernv_driver.workmem_size
 *
 * Returns: see @nx842_powernv_function()
 */
static int nx842_powernv_decompress(const unsigned char *in, unsigned int inlen,
				    unsigned char *out, unsigned int *outlenp,
				    void *wmem)
{
	return nx842_powernv_function(in, inlen, out, outlenp,
				      wmem, CCW_FC_842_DECOMP_NOCRC);
}

static int __init nx842_powernv_probe(struct device_node *dn)
{
	struct nx842_coproc *coproc;
	struct property *ct_prop, *ci_prop;
	unsigned int ct, ci;
	int chip_id;

	chip_id = of_get_ibm_chip_id(dn);
	if (chip_id < 0) {
		pr_err("ibm,chip-id missing\n");
		return -EINVAL;
	}
	ct_prop = of_find_property(dn, "ibm,842-coprocessor-type", NULL);
	if (!ct_prop) {
		pr_err("ibm,842-coprocessor-type missing\n");
		return -EINVAL;
	}
	ct = be32_to_cpu(*(unsigned int *)ct_prop->value);
	ci_prop = of_find_property(dn, "ibm,842-coprocessor-instance", NULL);
	if (!ci_prop) {
		pr_err("ibm,842-coprocessor-instance missing\n");
		return -EINVAL;
	}
	ci = be32_to_cpu(*(unsigned int *)ci_prop->value);

	coproc = kmalloc(sizeof(*coproc), GFP_KERNEL);
	if (!coproc)
		return -ENOMEM;

	coproc->chip_id = chip_id;
	coproc->ct = ct;
	coproc->ci = ci;
	INIT_LIST_HEAD(&coproc->list);
	list_add(&coproc->list, &nx842_coprocs);

	pr_info("coprocessor found on chip %d, CT %d CI %d\n", chip_id, ct, ci);

	if (!nx842_ct)
		nx842_ct = ct;
	else if (nx842_ct != ct)
		pr_err("NX842 chip %d, CT %d != first found CT %d\n",
		       chip_id, ct, nx842_ct);

	return 0;
}

static struct nx842_constraints nx842_powernv_constraints = {
	.alignment =	DDE_BUFFER_ALIGN,
	.multiple =	DDE_BUFFER_LAST_MULT,
	.minimum =	DDE_BUFFER_LAST_MULT,
	.maximum =	(DDL_LEN_MAX - 1) * PAGE_SIZE,
};

static struct nx842_driver nx842_powernv_driver = {
	.name =		KBUILD_MODNAME,
	.owner =	THIS_MODULE,
	.workmem_size =	sizeof(struct nx842_workmem),
	.constraints =	&nx842_powernv_constraints,
	.compress =	nx842_powernv_compress,
	.decompress =	nx842_powernv_decompress,
};

static int nx842_powernv_crypto_init(struct crypto_tfm *tfm)
{
	return nx842_crypto_init(tfm, &nx842_powernv_driver);
}

static struct crypto_alg nx842_powernv_alg = {
	.cra_name		= "842",
	.cra_driver_name	= "842-nx",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct nx842_crypto_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= nx842_powernv_crypto_init,
	.cra_exit		= nx842_crypto_exit,
	.cra_u			= { .compress = {
	.coa_compress		= nx842_crypto_compress,
	.coa_decompress		= nx842_crypto_decompress } }
};

static __init int nx842_powernv_init(void)
{
	struct device_node *dn;
	int ret;

	/* verify workmem size/align restrictions */
	BUILD_BUG_ON(WORKMEM_ALIGN % CRB_ALIGN);
	BUILD_BUG_ON(CRB_ALIGN % DDE_ALIGN);
	BUILD_BUG_ON(CRB_SIZE % DDE_ALIGN);
	/* verify buffer size/align restrictions */
	BUILD_BUG_ON(PAGE_SIZE % DDE_BUFFER_ALIGN);
	BUILD_BUG_ON(DDE_BUFFER_ALIGN % DDE_BUFFER_SIZE_MULT);
	BUILD_BUG_ON(DDE_BUFFER_SIZE_MULT % DDE_BUFFER_LAST_MULT);

	for_each_compatible_node(dn, NULL, "ibm,power-nx")
		nx842_powernv_probe(dn);

	if (!nx842_ct)
		return -ENODEV;

	ret = crypto_register_alg(&nx842_powernv_alg);
	if (ret) {
		struct nx842_coproc *coproc, *n;

		list_for_each_entry_safe(coproc, n, &nx842_coprocs, list) {
			list_del(&coproc->list);
			kfree(coproc);
		}

		return ret;
	}

	return 0;
}
module_init(nx842_powernv_init);

static void __exit nx842_powernv_exit(void)
{
	struct nx842_coproc *coproc, *n;

	crypto_unregister_alg(&nx842_powernv_alg);

	list_for_each_entry_safe(coproc, n, &nx842_coprocs, list) {
		list_del(&coproc->list);
		kfree(coproc);
	}
}
module_exit(nx842_powernv_exit);
