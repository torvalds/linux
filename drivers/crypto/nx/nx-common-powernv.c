// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for IBM PowerNV compression accelerator
 *
 * Copyright (C) 2015 Dan Streetman, IBM Corp
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "nx-842.h"

#include <linux/timer.h>

#include <asm/prom.h>
#include <asm/icswx.h>
#include <asm/vas.h>
#include <asm/reg.h>
#include <asm/opal-api.h>
#include <asm/opal.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
MODULE_DESCRIPTION("H/W Compression driver for IBM PowerNV processors");
MODULE_ALIAS_CRYPTO("842");
MODULE_ALIAS_CRYPTO("842-nx");

#define WORKMEM_ALIGN	(CRB_ALIGN)
#define CSB_WAIT_MAX	(5000) /* ms */
#define VAS_RETRIES	(10)

struct nx842_workmem {
	/* Below fields must be properly aligned */
	struct coprocessor_request_block crb; /* CRB_ALIGN align */
	struct data_descriptor_entry ddl_in[DDL_LEN_MAX]; /* DDE_ALIGN align */
	struct data_descriptor_entry ddl_out[DDL_LEN_MAX]; /* DDE_ALIGN align */
	/* Above fields must be properly aligned */

	ktime_t start;

	char padding[WORKMEM_ALIGN]; /* unused, to allow alignment */
} __packed __aligned(WORKMEM_ALIGN);

struct nx_coproc {
	unsigned int chip_id;
	unsigned int ct;	/* Can be 842 or GZIP high/normal*/
	unsigned int ci;	/* Coprocessor instance, used with icswx */
	struct {
		struct vas_window *rxwin;
		int id;
	} vas;
	struct list_head list;
};

/*
 * Send the request to NX engine on the chip for the corresponding CPU
 * where the process is executing. Use with VAS function.
 */
static DEFINE_PER_CPU(struct vas_window *, cpu_txwin);

/* no cpu hotplug on powernv, so this list never changes after init */
static LIST_HEAD(nx_coprocs);
static unsigned int nx842_ct;	/* used in icswx function */

/*
 * Using same values as in skiboot or coprocessor type representing
 * in NX workbook.
 */
#define NX_CT_GZIP	(2)	/* on P9 and later */
#define NX_CT_842	(3)

static int (*nx842_powernv_exec)(const unsigned char *in,
				unsigned int inlen, unsigned char *out,
				unsigned int *outlenp, void *workmem, int fc);

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

	while (!(READ_ONCE(csb->flags) & CSB_V)) {
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
	case CSB_CC_EXCEED_BYTE_COUNT:	/* P9 or later */
		/*
		 * DDE byte count exceeds the limit specified in Maximum
		 * byte count register.
		 */
		CSB_ERR(csb, "DDE byte count exceeds the limit");
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
	case CSB_CC_INVALID_CRB:	/* P9 or later */
		/* shouldn't happen, we setup CRB correctly */
		CSB_ERR(csb, "Invalid CRB");
		return -EINVAL;
	case CSB_CC_INVALID_DDE:	/* P9 or later */
		/*
		 * shouldn't happen, setup_direct/indirect_dde creates
		 * DDE right
		 */
		CSB_ERR(csb, "Invalid DDE");
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
		CSB_ERR(csb, "CRB sequence number error");
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
	case CSB_CC_HW_EXPIRED_TIMER:	/* P9 or later */
		CSB_ERR(csb, "Job did not finish within allowed time");
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
	pr_debug_ratelimited("Processed %u bytes in %lu us\n",
			     be32_to_cpu(csb->count),
			     (unsigned long)ktime_us_delta(now, start));

	return 0;
}

static int nx842_config_crb(const unsigned char *in, unsigned int inlen,
			unsigned char *out, unsigned int outlen,
			struct nx842_workmem *wmem)
{
	struct coprocessor_request_block *crb;
	struct coprocessor_status_block *csb;
	u64 csb_addr;
	int ret;

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

	/* set up CRB's CSB addr */
	csb_addr = nx842_get_pa(csb) & CRB_CSB_ADDRESS;
	csb_addr |= CRB_CSB_AT; /* Addrs are phys */
	crb->csb_addr = cpu_to_be64(csb_addr);

	return 0;
}

/**
 * nx842_exec_icswx - compress/decompress data using the 842 algorithm
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
static int nx842_exec_icswx(const unsigned char *in, unsigned int inlen,
				  unsigned char *out, unsigned int *outlenp,
				  void *workmem, int fc)
{
	struct coprocessor_request_block *crb;
	struct coprocessor_status_block *csb;
	struct nx842_workmem *wmem;
	int ret;
	u32 ccw;
	unsigned int outlen = *outlenp;

	wmem = PTR_ALIGN(workmem, WORKMEM_ALIGN);

	*outlenp = 0;

	/* shoudn't happen, we don't load without a coproc */
	if (!nx842_ct) {
		pr_err_ratelimited("coprocessor CT is 0");
		return -ENODEV;
	}

	ret = nx842_config_crb(in, inlen, out, outlen, wmem);
	if (ret)
		return ret;

	crb = &wmem->crb;
	csb = &crb->csb;

	/* set up CCW */
	ccw = 0;
	ccw = SET_FIELD(CCW_CT, ccw, nx842_ct);
	ccw = SET_FIELD(CCW_CI_842, ccw, 0); /* use 0 for hw auto-selection */
	ccw = SET_FIELD(CCW_FC_842, ccw, fc);

	wmem->start = ktime_get();

	/* do ICSWX */
	ret = icswx(cpu_to_be32(ccw), crb);

	pr_debug_ratelimited("icswx CR %x ccw %x crb->ccw %x\n", ret,
			     (unsigned int)ccw,
			     (unsigned int)be32_to_cpu(crb->ccw));

	/*
	 * NX842 coprocessor sets 3rd bit in CR register with XER[S0].
	 * XER[S0] is the integer summary overflow bit which is nothing
	 * to do NX. Since this bit can be set with other return values,
	 * mask this bit.
	 */
	ret &= ~ICSWX_XERS0;

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
	}

	if (!ret)
		*outlenp = be32_to_cpu(csb->count);

	return ret;
}

/**
 * nx842_exec_vas - compress/decompress data using the 842 algorithm
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
 *   0		Success, output of length @outlenp stored in the buffer
 *		at @out
 *   -ENODEV	Hardware unavailable
 *   -ENOSPC	Output buffer is to small
 *   -EMSGSIZE	Input buffer too large
 *   -EINVAL	buffer constraints do not fix nx842_constraints
 *   -EPROTO	hardware error during operation
 *   -ETIMEDOUT	hardware did not complete operation in reasonable time
 *   -EINTR	operation was aborted
 */
static int nx842_exec_vas(const unsigned char *in, unsigned int inlen,
				  unsigned char *out, unsigned int *outlenp,
				  void *workmem, int fc)
{
	struct coprocessor_request_block *crb;
	struct coprocessor_status_block *csb;
	struct nx842_workmem *wmem;
	struct vas_window *txwin;
	int ret, i = 0;
	u32 ccw;
	unsigned int outlen = *outlenp;

	wmem = PTR_ALIGN(workmem, WORKMEM_ALIGN);

	*outlenp = 0;

	crb = &wmem->crb;
	csb = &crb->csb;

	ret = nx842_config_crb(in, inlen, out, outlen, wmem);
	if (ret)
		return ret;

	ccw = 0;
	ccw = SET_FIELD(CCW_FC_842, ccw, fc);
	crb->ccw = cpu_to_be32(ccw);

	do {
		wmem->start = ktime_get();
		preempt_disable();
		txwin = this_cpu_read(cpu_txwin);

		/*
		 * VAS copy CRB into L2 cache. Refer <asm/vas.h>.
		 * @crb and @offset.
		 */
		vas_copy_crb(crb, 0);

		/*
		 * VAS paste previously copied CRB to NX.
		 * @txwin, @offset and @last (must be true).
		 */
		ret = vas_paste_crb(txwin, 0, 1);
		preempt_enable();
		/*
		 * Retry copy/paste function for VAS failures.
		 */
	} while (ret && (i++ < VAS_RETRIES));

	if (ret) {
		pr_err_ratelimited("VAS copy/paste failed\n");
		return ret;
	}

	ret = wait_for_csb(wmem, csb);
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
 * Returns: see @nx842_powernv_exec()
 */
static int nx842_powernv_compress(const unsigned char *in, unsigned int inlen,
				  unsigned char *out, unsigned int *outlenp,
				  void *wmem)
{
	return nx842_powernv_exec(in, inlen, out, outlenp,
				      wmem, CCW_FC_842_COMP_CRC);
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
 * Returns: see @nx842_powernv_exec()
 */
static int nx842_powernv_decompress(const unsigned char *in, unsigned int inlen,
				    unsigned char *out, unsigned int *outlenp,
				    void *wmem)
{
	return nx842_powernv_exec(in, inlen, out, outlenp,
				      wmem, CCW_FC_842_DECOMP_CRC);
}

static inline void nx_add_coprocs_list(struct nx_coproc *coproc,
					int chipid)
{
	coproc->chip_id = chipid;
	INIT_LIST_HEAD(&coproc->list);
	list_add(&coproc->list, &nx_coprocs);
}

static struct vas_window *nx_alloc_txwin(struct nx_coproc *coproc)
{
	struct vas_window *txwin = NULL;
	struct vas_tx_win_attr txattr;

	/*
	 * Kernel requests will be high priority. So open send
	 * windows only for high priority RxFIFO entries.
	 */
	vas_init_tx_win_attr(&txattr, coproc->ct);
	txattr.lpid = 0;	/* lpid is 0 for kernel requests */

	/*
	 * Open a VAS send window which is used to send request to NX.
	 */
	txwin = vas_tx_win_open(coproc->vas.id, coproc->ct, &txattr);
	if (IS_ERR(txwin))
		pr_err("ibm,nx-842: Can not open TX window: %ld\n",
				PTR_ERR(txwin));

	return txwin;
}

/*
 * Identify chip ID for each CPU, open send wndow for the corresponding NX
 * engine and save txwin in percpu cpu_txwin.
 * cpu_txwin is used in copy/paste operation for each compression /
 * decompression request.
 */
static int nx_open_percpu_txwins(void)
{
	struct nx_coproc *coproc, *n;
	unsigned int i, chip_id;

	for_each_possible_cpu(i) {
		struct vas_window *txwin = NULL;

		chip_id = cpu_to_chip_id(i);

		list_for_each_entry_safe(coproc, n, &nx_coprocs, list) {
			/*
			 * Kernel requests use only high priority FIFOs. So
			 * open send windows for these FIFOs.
			 * GZIP is not supported in kernel right now.
			 */

			if (coproc->ct != VAS_COP_TYPE_842_HIPRI)
				continue;

			if (coproc->chip_id == chip_id) {
				txwin = nx_alloc_txwin(coproc);
				if (IS_ERR(txwin))
					return PTR_ERR(txwin);

				per_cpu(cpu_txwin, i) = txwin;
				break;
			}
		}

		if (!per_cpu(cpu_txwin, i)) {
			/* shouldn't happen, Each chip will have NX engine */
			pr_err("NX engine is not available for CPU %d\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static int __init nx_set_ct(struct nx_coproc *coproc, const char *priority,
				int high, int normal)
{
	if (!strcmp(priority, "High"))
		coproc->ct = high;
	else if (!strcmp(priority, "Normal"))
		coproc->ct = normal;
	else {
		pr_err("Invalid RxFIFO priority value\n");
		return -EINVAL;
	}

	return 0;
}

static int __init vas_cfg_coproc_info(struct device_node *dn, int chip_id,
					int vasid, int type, int *ct)
{
	struct vas_window *rxwin = NULL;
	struct vas_rx_win_attr rxattr;
	u32 lpid, pid, tid, fifo_size;
	struct nx_coproc *coproc;
	u64 rx_fifo;
	const char *priority;
	int ret;

	ret = of_property_read_u64(dn, "rx-fifo-address", &rx_fifo);
	if (ret) {
		pr_err("Missing rx-fifo-address property\n");
		return ret;
	}

	ret = of_property_read_u32(dn, "rx-fifo-size", &fifo_size);
	if (ret) {
		pr_err("Missing rx-fifo-size property\n");
		return ret;
	}

	ret = of_property_read_u32(dn, "lpid", &lpid);
	if (ret) {
		pr_err("Missing lpid property\n");
		return ret;
	}

	ret = of_property_read_u32(dn, "pid", &pid);
	if (ret) {
		pr_err("Missing pid property\n");
		return ret;
	}

	ret = of_property_read_u32(dn, "tid", &tid);
	if (ret) {
		pr_err("Missing tid property\n");
		return ret;
	}

	ret = of_property_read_string(dn, "priority", &priority);
	if (ret) {
		pr_err("Missing priority property\n");
		return ret;
	}

	coproc = kzalloc(sizeof(*coproc), GFP_KERNEL);
	if (!coproc)
		return -ENOMEM;

	if (type == NX_CT_842)
		ret = nx_set_ct(coproc, priority, VAS_COP_TYPE_842_HIPRI,
			VAS_COP_TYPE_842);
	else if (type == NX_CT_GZIP)
		ret = nx_set_ct(coproc, priority, VAS_COP_TYPE_GZIP_HIPRI,
				VAS_COP_TYPE_GZIP);

	if (ret)
		goto err_out;

	vas_init_rx_win_attr(&rxattr, coproc->ct);
	rxattr.rx_fifo = (void *)rx_fifo;
	rxattr.rx_fifo_size = fifo_size;
	rxattr.lnotify_lpid = lpid;
	rxattr.lnotify_pid = pid;
	rxattr.lnotify_tid = tid;
	/*
	 * Maximum RX window credits can not be more than #CRBs in
	 * RxFIFO. Otherwise, can get checkstop if RxFIFO overruns.
	 */
	rxattr.wcreds_max = fifo_size / CRB_SIZE;

	/*
	 * Open a VAS receice window which is used to configure RxFIFO
	 * for NX.
	 */
	rxwin = vas_rx_win_open(vasid, coproc->ct, &rxattr);
	if (IS_ERR(rxwin)) {
		ret = PTR_ERR(rxwin);
		pr_err("setting RxFIFO with VAS failed: %d\n",
			ret);
		goto err_out;
	}

	coproc->vas.rxwin = rxwin;
	coproc->vas.id = vasid;
	nx_add_coprocs_list(coproc, chip_id);

	/*
	 * (lpid, pid, tid) combination has to be unique for each
	 * coprocessor instance in the system. So to make it
	 * unique, skiboot uses coprocessor type such as 842 or
	 * GZIP for pid and provides this value to kernel in pid
	 * device-tree property.
	 */
	*ct = pid;

	return 0;

err_out:
	kfree(coproc);
	return ret;
}

static int __init nx_coproc_init(int chip_id, int ct_842, int ct_gzip)
{
	int ret = 0;

	if (opal_check_token(OPAL_NX_COPROC_INIT)) {
		ret = opal_nx_coproc_init(chip_id, ct_842);

		if (!ret)
			ret = opal_nx_coproc_init(chip_id, ct_gzip);

		if (ret) {
			ret = opal_error_code(ret);
			pr_err("Failed to initialize NX for chip(%d): %d\n",
				chip_id, ret);
		}
	} else
		pr_warn("Firmware doesn't support NX initialization\n");

	return ret;
}

static int __init find_nx_device_tree(struct device_node *dn, int chip_id,
					int vasid, int type, char *devname,
					int *ct)
{
	int ret = 0;

	if (of_device_is_compatible(dn, devname)) {
		ret  = vas_cfg_coproc_info(dn, chip_id, vasid, type, ct);
		if (ret)
			of_node_put(dn);
	}

	return ret;
}

static int __init nx_powernv_probe_vas(struct device_node *pn)
{
	int chip_id, vasid, ret = 0;
	int ct_842 = 0, ct_gzip = 0;
	struct device_node *dn;

	chip_id = of_get_ibm_chip_id(pn);
	if (chip_id < 0) {
		pr_err("ibm,chip-id missing\n");
		return -EINVAL;
	}

	vasid = chip_to_vas_id(chip_id);
	if (vasid < 0) {
		pr_err("Unable to map chip_id %d to vasid\n", chip_id);
		return -EINVAL;
	}

	for_each_child_of_node(pn, dn) {
		ret = find_nx_device_tree(dn, chip_id, vasid, NX_CT_842,
					"ibm,p9-nx-842", &ct_842);

		if (!ret)
			ret = find_nx_device_tree(dn, chip_id, vasid,
				NX_CT_GZIP, "ibm,p9-nx-gzip", &ct_gzip);

		if (ret) {
			of_node_put(dn);
			return ret;
		}
	}

	if (!ct_842 || !ct_gzip) {
		pr_err("NX FIFO nodes are missing\n");
		return -EINVAL;
	}

	/*
	 * Initialize NX instance for both high and normal priority FIFOs.
	 */
	ret = nx_coproc_init(chip_id, ct_842, ct_gzip);

	return ret;
}

static int __init nx842_powernv_probe(struct device_node *dn)
{
	struct nx_coproc *coproc;
	unsigned int ct, ci;
	int chip_id;

	chip_id = of_get_ibm_chip_id(dn);
	if (chip_id < 0) {
		pr_err("ibm,chip-id missing\n");
		return -EINVAL;
	}

	if (of_property_read_u32(dn, "ibm,842-coprocessor-type", &ct)) {
		pr_err("ibm,842-coprocessor-type missing\n");
		return -EINVAL;
	}

	if (of_property_read_u32(dn, "ibm,842-coprocessor-instance", &ci)) {
		pr_err("ibm,842-coprocessor-instance missing\n");
		return -EINVAL;
	}

	coproc = kzalloc(sizeof(*coproc), GFP_KERNEL);
	if (!coproc)
		return -ENOMEM;

	coproc->ct = ct;
	coproc->ci = ci;
	nx_add_coprocs_list(coproc, chip_id);

	pr_info("coprocessor found on chip %d, CT %d CI %d\n", chip_id, ct, ci);

	if (!nx842_ct)
		nx842_ct = ct;
	else if (nx842_ct != ct)
		pr_err("NX842 chip %d, CT %d != first found CT %d\n",
		       chip_id, ct, nx842_ct);

	return 0;
}

static void nx_delete_coprocs(void)
{
	struct nx_coproc *coproc, *n;
	struct vas_window *txwin;
	int i;

	/*
	 * close percpu txwins that are opened for the corresponding coproc.
	 */
	for_each_possible_cpu(i) {
		txwin = per_cpu(cpu_txwin, i);
		if (txwin)
			vas_win_close(txwin);

		per_cpu(cpu_txwin, i) = NULL;
	}

	list_for_each_entry_safe(coproc, n, &nx_coprocs, list) {
		if (coproc->vas.rxwin)
			vas_win_close(coproc->vas.rxwin);

		list_del(&coproc->list);
		kfree(coproc);
	}
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

static __init int nx_compress_powernv_init(void)
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

	for_each_compatible_node(dn, NULL, "ibm,power9-nx") {
		ret = nx_powernv_probe_vas(dn);
		if (ret) {
			nx_delete_coprocs();
			of_node_put(dn);
			return ret;
		}
	}

	if (list_empty(&nx_coprocs)) {
		for_each_compatible_node(dn, NULL, "ibm,power-nx")
			nx842_powernv_probe(dn);

		if (!nx842_ct)
			return -ENODEV;

		nx842_powernv_exec = nx842_exec_icswx;
	} else {
		/*
		 * Register VAS user space API for NX GZIP so
		 * that user space can use GZIP engine.
		 * Using high FIFO priority for kernel requests and
		 * normal FIFO priority is assigned for userspace.
		 * 842 compression is supported only in kernel.
		 */
		ret = vas_register_coproc_api(THIS_MODULE, VAS_COP_TYPE_GZIP,
						"nx-gzip");

		/*
		 * GZIP is not supported in kernel right now.
		 * So open tx windows only for 842.
		 */
		if (!ret)
			ret = nx_open_percpu_txwins();

		if (ret) {
			nx_delete_coprocs();
			return ret;
		}

		nx842_powernv_exec = nx842_exec_vas;
	}

	ret = crypto_register_alg(&nx842_powernv_alg);
	if (ret) {
		nx_delete_coprocs();
		return ret;
	}

	return 0;
}
module_init(nx_compress_powernv_init);

static void __exit nx_compress_powernv_exit(void)
{
	/*
	 * GZIP engine is supported only in power9 or later and nx842_ct
	 * is used on power8 (icswx).
	 * VAS API for NX GZIP is registered during init for user space
	 * use. So delete this API use for GZIP engine.
	 */
	if (!nx842_ct)
		vas_unregister_coproc_api();

	crypto_unregister_alg(&nx842_powernv_alg);

	nx_delete_coprocs();
}
module_exit(nx_compress_powernv_exit);
