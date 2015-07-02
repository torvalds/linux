/*
 * Driver for IBM Power 842 compression accelerator
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Authors: Robert Jennings <rcj@linux.vnet.ibm.com>
 *          Seth Jennings <sjenning@linux.vnet.ibm.com>
 */

#include <asm/vio.h>

#include "nx-842.h"
#include "nx_csbcpb.h" /* struct nx_csbcpb */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Jennings <rcj@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("842 H/W Compression driver for IBM Power processors");

static struct nx842_constraints nx842_pseries_constraints = {
	.alignment =	DDE_BUFFER_ALIGN,
	.multiple =	DDE_BUFFER_LAST_MULT,
	.minimum =	DDE_BUFFER_LAST_MULT,
	.maximum =	PAGE_SIZE, /* dynamic, max_sync_size */
};

static int check_constraints(unsigned long buf, unsigned int *len, bool in)
{
	if (!IS_ALIGNED(buf, nx842_pseries_constraints.alignment)) {
		pr_debug("%s buffer 0x%lx not aligned to 0x%x\n",
			 in ? "input" : "output", buf,
			 nx842_pseries_constraints.alignment);
		return -EINVAL;
	}
	if (*len % nx842_pseries_constraints.multiple) {
		pr_debug("%s buffer len 0x%x not multiple of 0x%x\n",
			 in ? "input" : "output", *len,
			 nx842_pseries_constraints.multiple);
		if (in)
			return -EINVAL;
		*len = round_down(*len, nx842_pseries_constraints.multiple);
	}
	if (*len < nx842_pseries_constraints.minimum) {
		pr_debug("%s buffer len 0x%x under minimum 0x%x\n",
			 in ? "input" : "output", *len,
			 nx842_pseries_constraints.minimum);
		return -EINVAL;
	}
	if (*len > nx842_pseries_constraints.maximum) {
		pr_debug("%s buffer len 0x%x over maximum 0x%x\n",
			 in ? "input" : "output", *len,
			 nx842_pseries_constraints.maximum);
		if (in)
			return -EINVAL;
		*len = nx842_pseries_constraints.maximum;
	}
	return 0;
}

/* I assume we need to align the CSB? */
#define WORKMEM_ALIGN	(256)

struct nx842_workmem {
	/* scatterlist */
	char slin[4096];
	char slout[4096];
	/* coprocessor status/parameter block */
	struct nx_csbcpb csbcpb;

	char padding[WORKMEM_ALIGN];
} __aligned(WORKMEM_ALIGN);

/* Macros for fields within nx_csbcpb */
/* Check the valid bit within the csbcpb valid field */
#define NX842_CSBCBP_VALID_CHK(x) (x & BIT_MASK(7))

/* CE macros operate on the completion_extension field bits in the csbcpb.
 * CE0 0=full completion, 1=partial completion
 * CE1 0=CE0 indicates completion, 1=termination (output may be modified)
 * CE2 0=processed_bytes is source bytes, 1=processed_bytes is target bytes */
#define NX842_CSBCPB_CE0(x)	(x & BIT_MASK(7))
#define NX842_CSBCPB_CE1(x)	(x & BIT_MASK(6))
#define NX842_CSBCPB_CE2(x)	(x & BIT_MASK(5))

/* The NX unit accepts data only on 4K page boundaries */
#define NX842_HW_PAGE_SIZE	(4096)
#define NX842_HW_PAGE_MASK	(~(NX842_HW_PAGE_SIZE-1))

enum nx842_status {
	UNAVAILABLE,
	AVAILABLE
};

struct ibm_nx842_counters {
	atomic64_t comp_complete;
	atomic64_t comp_failed;
	atomic64_t decomp_complete;
	atomic64_t decomp_failed;
	atomic64_t swdecomp;
	atomic64_t comp_times[32];
	atomic64_t decomp_times[32];
};

static struct nx842_devdata {
	struct vio_dev *vdev;
	struct device *dev;
	struct ibm_nx842_counters *counters;
	unsigned int max_sg_len;
	unsigned int max_sync_size;
	unsigned int max_sync_sg;
	enum nx842_status status;
} __rcu *devdata;
static DEFINE_SPINLOCK(devdata_mutex);

#define NX842_COUNTER_INC(_x) \
static inline void nx842_inc_##_x( \
	const struct nx842_devdata *dev) { \
	if (dev) \
		atomic64_inc(&dev->counters->_x); \
}
NX842_COUNTER_INC(comp_complete);
NX842_COUNTER_INC(comp_failed);
NX842_COUNTER_INC(decomp_complete);
NX842_COUNTER_INC(decomp_failed);
NX842_COUNTER_INC(swdecomp);

#define NX842_HIST_SLOTS 16

static void ibm_nx842_incr_hist(atomic64_t *times, unsigned int time)
{
	int bucket = fls(time);

	if (bucket)
		bucket = min((NX842_HIST_SLOTS - 1), bucket - 1);

	atomic64_inc(&times[bucket]);
}

/* NX unit operation flags */
#define NX842_OP_COMPRESS	0x0
#define NX842_OP_CRC		0x1
#define NX842_OP_DECOMPRESS	0x2
#define NX842_OP_COMPRESS_CRC   (NX842_OP_COMPRESS | NX842_OP_CRC)
#define NX842_OP_DECOMPRESS_CRC (NX842_OP_DECOMPRESS | NX842_OP_CRC)
#define NX842_OP_ASYNC		(1<<23)
#define NX842_OP_NOTIFY		(1<<22)
#define NX842_OP_NOTIFY_INT(x)	((x & 0xff)<<8)

static unsigned long nx842_get_desired_dma(struct vio_dev *viodev)
{
	/* No use of DMA mappings within the driver. */
	return 0;
}

struct nx842_slentry {
	__be64 ptr; /* Real address (use __pa()) */
	__be64 len;
};

/* pHyp scatterlist entry */
struct nx842_scatterlist {
	int entry_nr; /* number of slentries */
	struct nx842_slentry *entries; /* ptr to array of slentries */
};

/* Does not include sizeof(entry_nr) in the size */
static inline unsigned long nx842_get_scatterlist_size(
				struct nx842_scatterlist *sl)
{
	return sl->entry_nr * sizeof(struct nx842_slentry);
}

static int nx842_build_scatterlist(unsigned long buf, int len,
			struct nx842_scatterlist *sl)
{
	unsigned long entrylen;
	struct nx842_slentry *entry;

	sl->entry_nr = 0;

	entry = sl->entries;
	while (len) {
		entry->ptr = cpu_to_be64(nx842_get_pa((void *)buf));
		entrylen = min_t(int, len,
				 LEN_ON_SIZE(buf, NX842_HW_PAGE_SIZE));
		entry->len = cpu_to_be64(entrylen);

		len -= entrylen;
		buf += entrylen;

		sl->entry_nr++;
		entry++;
	}

	return 0;
}

static int nx842_validate_result(struct device *dev,
	struct cop_status_block *csb)
{
	/* The csb must be valid after returning from vio_h_cop_sync */
	if (!NX842_CSBCBP_VALID_CHK(csb->valid)) {
		dev_err(dev, "%s: cspcbp not valid upon completion.\n",
				__func__);
		dev_dbg(dev, "valid:0x%02x cs:0x%02x cc:0x%02x ce:0x%02x\n",
				csb->valid,
				csb->crb_seq_number,
				csb->completion_code,
				csb->completion_extension);
		dev_dbg(dev, "processed_bytes:%d address:0x%016lx\n",
				be32_to_cpu(csb->processed_byte_count),
				(unsigned long)be64_to_cpu(csb->address));
		return -EIO;
	}

	/* Check return values from the hardware in the CSB */
	switch (csb->completion_code) {
	case 0:	/* Completed without error */
		break;
	case 64: /* Target bytes > Source bytes during compression */
	case 13: /* Output buffer too small */
		dev_dbg(dev, "%s: Compression output larger than input\n",
					__func__);
		return -ENOSPC;
	case 66: /* Input data contains an illegal template field */
	case 67: /* Template indicates data past the end of the input stream */
		dev_dbg(dev, "%s: Bad data for decompression (code:%d)\n",
					__func__, csb->completion_code);
		return -EINVAL;
	default:
		dev_dbg(dev, "%s: Unspecified error (code:%d)\n",
					__func__, csb->completion_code);
		return -EIO;
	}

	/* Hardware sanity check */
	if (!NX842_CSBCPB_CE2(csb->completion_extension)) {
		dev_err(dev, "%s: No error returned by hardware, but "
				"data returned is unusable, contact support.\n"
				"(Additional info: csbcbp->processed bytes "
				"does not specify processed bytes for the "
				"target buffer.)\n", __func__);
		return -EIO;
	}

	return 0;
}

/**
 * nx842_pseries_compress - Compress data using the 842 algorithm
 *
 * Compression provide by the NX842 coprocessor on IBM Power systems.
 * The input buffer is compressed and the result is stored in the
 * provided output buffer.
 *
 * Upon return from this function @outlen contains the length of the
 * compressed data.  If there is an error then @outlen will be 0 and an
 * error will be specified by the return code from this function.
 *
 * @in: Pointer to input buffer
 * @inlen: Length of input buffer
 * @out: Pointer to output buffer
 * @outlen: Length of output buffer
 * @wrkmem: ptr to buffer for working memory, size determined by
 *          nx842_pseries_driver.workmem_size
 *
 * Returns:
 *   0		Success, output of length @outlen stored in the buffer at @out
 *   -ENOMEM	Unable to allocate internal buffers
 *   -ENOSPC	Output buffer is to small
 *   -EIO	Internal error
 *   -ENODEV	Hardware unavailable
 */
static int nx842_pseries_compress(const unsigned char *in, unsigned int inlen,
				  unsigned char *out, unsigned int *outlen,
				  void *wmem)
{
	struct nx842_devdata *local_devdata;
	struct device *dev = NULL;
	struct nx842_workmem *workmem;
	struct nx842_scatterlist slin, slout;
	struct nx_csbcpb *csbcpb;
	int ret = 0, max_sync_size;
	unsigned long inbuf, outbuf;
	struct vio_pfo_op op = {
		.done = NULL,
		.handle = 0,
		.timeout = 0,
	};
	unsigned long start = get_tb();

	inbuf = (unsigned long)in;
	if (check_constraints(inbuf, &inlen, true))
		return -EINVAL;

	outbuf = (unsigned long)out;
	if (check_constraints(outbuf, outlen, false))
		return -EINVAL;

	rcu_read_lock();
	local_devdata = rcu_dereference(devdata);
	if (!local_devdata || !local_devdata->dev) {
		rcu_read_unlock();
		return -ENODEV;
	}
	max_sync_size = local_devdata->max_sync_size;
	dev = local_devdata->dev;

	/* Init scatterlist */
	workmem = PTR_ALIGN(wmem, WORKMEM_ALIGN);
	slin.entries = (struct nx842_slentry *)workmem->slin;
	slout.entries = (struct nx842_slentry *)workmem->slout;

	/* Init operation */
	op.flags = NX842_OP_COMPRESS;
	csbcpb = &workmem->csbcpb;
	memset(csbcpb, 0, sizeof(*csbcpb));
	op.csbcpb = nx842_get_pa(csbcpb);

	if ((inbuf & NX842_HW_PAGE_MASK) ==
	    ((inbuf + inlen - 1) & NX842_HW_PAGE_MASK)) {
		/* Create direct DDE */
		op.in = nx842_get_pa((void *)inbuf);
		op.inlen = inlen;
	} else {
		/* Create indirect DDE (scatterlist) */
		nx842_build_scatterlist(inbuf, inlen, &slin);
		op.in = nx842_get_pa(slin.entries);
		op.inlen = -nx842_get_scatterlist_size(&slin);
	}

	if ((outbuf & NX842_HW_PAGE_MASK) ==
	    ((outbuf + *outlen - 1) & NX842_HW_PAGE_MASK)) {
		/* Create direct DDE */
		op.out = nx842_get_pa((void *)outbuf);
		op.outlen = *outlen;
	} else {
		/* Create indirect DDE (scatterlist) */
		nx842_build_scatterlist(outbuf, *outlen, &slout);
		op.out = nx842_get_pa(slout.entries);
		op.outlen = -nx842_get_scatterlist_size(&slout);
	}

	dev_dbg(dev, "%s: op.in %lx op.inlen %ld op.out %lx op.outlen %ld\n",
		__func__, (unsigned long)op.in, (long)op.inlen,
		(unsigned long)op.out, (long)op.outlen);

	/* Send request to pHyp */
	ret = vio_h_cop_sync(local_devdata->vdev, &op);

	/* Check for pHyp error */
	if (ret) {
		dev_dbg(dev, "%s: vio_h_cop_sync error (ret=%d, hret=%ld)\n",
			__func__, ret, op.hcall_err);
		ret = -EIO;
		goto unlock;
	}

	/* Check for hardware error */
	ret = nx842_validate_result(dev, &csbcpb->csb);
	if (ret)
		goto unlock;

	*outlen = be32_to_cpu(csbcpb->csb.processed_byte_count);
	dev_dbg(dev, "%s: processed_bytes=%d\n", __func__, *outlen);

unlock:
	if (ret)
		nx842_inc_comp_failed(local_devdata);
	else {
		nx842_inc_comp_complete(local_devdata);
		ibm_nx842_incr_hist(local_devdata->counters->comp_times,
			(get_tb() - start) / tb_ticks_per_usec);
	}
	rcu_read_unlock();
	return ret;
}

/**
 * nx842_pseries_decompress - Decompress data using the 842 algorithm
 *
 * Decompression provide by the NX842 coprocessor on IBM Power systems.
 * The input buffer is decompressed and the result is stored in the
 * provided output buffer.  The size allocated to the output buffer is
 * provided by the caller of this function in @outlen.  Upon return from
 * this function @outlen contains the length of the decompressed data.
 * If there is an error then @outlen will be 0 and an error will be
 * specified by the return code from this function.
 *
 * @in: Pointer to input buffer
 * @inlen: Length of input buffer
 * @out: Pointer to output buffer
 * @outlen: Length of output buffer
 * @wrkmem: ptr to buffer for working memory, size determined by
 *          nx842_pseries_driver.workmem_size
 *
 * Returns:
 *   0		Success, output of length @outlen stored in the buffer at @out
 *   -ENODEV	Hardware decompression device is unavailable
 *   -ENOMEM	Unable to allocate internal buffers
 *   -ENOSPC	Output buffer is to small
 *   -EINVAL	Bad input data encountered when attempting decompress
 *   -EIO	Internal error
 */
static int nx842_pseries_decompress(const unsigned char *in, unsigned int inlen,
				    unsigned char *out, unsigned int *outlen,
				    void *wmem)
{
	struct nx842_devdata *local_devdata;
	struct device *dev = NULL;
	struct nx842_workmem *workmem;
	struct nx842_scatterlist slin, slout;
	struct nx_csbcpb *csbcpb;
	int ret = 0, max_sync_size;
	unsigned long inbuf, outbuf;
	struct vio_pfo_op op = {
		.done = NULL,
		.handle = 0,
		.timeout = 0,
	};
	unsigned long start = get_tb();

	/* Ensure page alignment and size */
	inbuf = (unsigned long)in;
	if (check_constraints(inbuf, &inlen, true))
		return -EINVAL;

	outbuf = (unsigned long)out;
	if (check_constraints(outbuf, outlen, false))
		return -EINVAL;

	rcu_read_lock();
	local_devdata = rcu_dereference(devdata);
	if (!local_devdata || !local_devdata->dev) {
		rcu_read_unlock();
		return -ENODEV;
	}
	max_sync_size = local_devdata->max_sync_size;
	dev = local_devdata->dev;

	workmem = PTR_ALIGN(wmem, WORKMEM_ALIGN);

	/* Init scatterlist */
	slin.entries = (struct nx842_slentry *)workmem->slin;
	slout.entries = (struct nx842_slentry *)workmem->slout;

	/* Init operation */
	op.flags = NX842_OP_DECOMPRESS;
	csbcpb = &workmem->csbcpb;
	memset(csbcpb, 0, sizeof(*csbcpb));
	op.csbcpb = nx842_get_pa(csbcpb);

	if ((inbuf & NX842_HW_PAGE_MASK) ==
	    ((inbuf + inlen - 1) & NX842_HW_PAGE_MASK)) {
		/* Create direct DDE */
		op.in = nx842_get_pa((void *)inbuf);
		op.inlen = inlen;
	} else {
		/* Create indirect DDE (scatterlist) */
		nx842_build_scatterlist(inbuf, inlen, &slin);
		op.in = nx842_get_pa(slin.entries);
		op.inlen = -nx842_get_scatterlist_size(&slin);
	}

	if ((outbuf & NX842_HW_PAGE_MASK) ==
	    ((outbuf + *outlen - 1) & NX842_HW_PAGE_MASK)) {
		/* Create direct DDE */
		op.out = nx842_get_pa((void *)outbuf);
		op.outlen = *outlen;
	} else {
		/* Create indirect DDE (scatterlist) */
		nx842_build_scatterlist(outbuf, *outlen, &slout);
		op.out = nx842_get_pa(slout.entries);
		op.outlen = -nx842_get_scatterlist_size(&slout);
	}

	dev_dbg(dev, "%s: op.in %lx op.inlen %ld op.out %lx op.outlen %ld\n",
		__func__, (unsigned long)op.in, (long)op.inlen,
		(unsigned long)op.out, (long)op.outlen);

	/* Send request to pHyp */
	ret = vio_h_cop_sync(local_devdata->vdev, &op);

	/* Check for pHyp error */
	if (ret) {
		dev_dbg(dev, "%s: vio_h_cop_sync error (ret=%d, hret=%ld)\n",
			__func__, ret, op.hcall_err);
		goto unlock;
	}

	/* Check for hardware error */
	ret = nx842_validate_result(dev, &csbcpb->csb);
	if (ret)
		goto unlock;

	*outlen = be32_to_cpu(csbcpb->csb.processed_byte_count);

unlock:
	if (ret)
		/* decompress fail */
		nx842_inc_decomp_failed(local_devdata);
	else {
		nx842_inc_decomp_complete(local_devdata);
		ibm_nx842_incr_hist(local_devdata->counters->decomp_times,
			(get_tb() - start) / tb_ticks_per_usec);
	}

	rcu_read_unlock();
	return ret;
}

/**
 * nx842_OF_set_defaults -- Set default (disabled) values for devdata
 *
 * @devdata - struct nx842_devdata to update
 *
 * Returns:
 *  0 on success
 *  -ENOENT if @devdata ptr is NULL
 */
static int nx842_OF_set_defaults(struct nx842_devdata *devdata)
{
	if (devdata) {
		devdata->max_sync_size = 0;
		devdata->max_sync_sg = 0;
		devdata->max_sg_len = 0;
		devdata->status = UNAVAILABLE;
		return 0;
	} else
		return -ENOENT;
}

/**
 * nx842_OF_upd_status -- Update the device info from OF status prop
 *
 * The status property indicates if the accelerator is enabled.  If the
 * device is in the OF tree it indicates that the hardware is present.
 * The status field indicates if the device is enabled when the status
 * is 'okay'.  Otherwise the device driver will be disabled.
 *
 * @devdata - struct nx842_devdata to update
 * @prop - struct property point containing the maxsyncop for the update
 *
 * Returns:
 *  0 - Device is available
 *  -ENODEV - Device is not available
 */
static int nx842_OF_upd_status(struct nx842_devdata *devdata,
					struct property *prop) {
	int ret = 0;
	const char *status = (const char *)prop->value;

	if (!strncmp(status, "okay", (size_t)prop->length)) {
		devdata->status = AVAILABLE;
	} else {
		/*
		 * Caller will log that the device is disabled, so only
		 * output if there is an unexpected status.
		 */
		if (strncmp(status, "disabled", (size_t)prop->length)) {
			dev_info(devdata->dev, "%s: status '%s' is not 'okay'\n",
				__func__, status);
		}
		devdata->status = UNAVAILABLE;
		ret = -ENODEV;
	}

	return ret;
}

/**
 * nx842_OF_upd_maxsglen -- Update the device info from OF maxsglen prop
 *
 * Definition of the 'ibm,max-sg-len' OF property:
 *  This field indicates the maximum byte length of a scatter list
 *  for the platform facility. It is a single cell encoded as with encode-int.
 *
 * Example:
 *  # od -x ibm,max-sg-len
 *  0000000 0000 0ff0
 *
 *  In this example, the maximum byte length of a scatter list is
 *  0x0ff0 (4,080).
 *
 * @devdata - struct nx842_devdata to update
 * @prop - struct property point containing the maxsyncop for the update
 *
 * Returns:
 *  0 on success
 *  -EINVAL on failure
 */
static int nx842_OF_upd_maxsglen(struct nx842_devdata *devdata,
					struct property *prop) {
	int ret = 0;
	const unsigned int maxsglen = of_read_number(prop->value, 1);

	if (prop->length != sizeof(maxsglen)) {
		dev_err(devdata->dev, "%s: unexpected format for ibm,max-sg-len property\n", __func__);
		dev_dbg(devdata->dev, "%s: ibm,max-sg-len is %d bytes long, expected %lu bytes\n", __func__,
				prop->length, sizeof(maxsglen));
		ret = -EINVAL;
	} else {
		devdata->max_sg_len = min_t(unsigned int,
					    maxsglen, NX842_HW_PAGE_SIZE);
	}

	return ret;
}

/**
 * nx842_OF_upd_maxsyncop -- Update the device info from OF maxsyncop prop
 *
 * Definition of the 'ibm,max-sync-cop' OF property:
 *  Two series of cells.  The first series of cells represents the maximums
 *  that can be synchronously compressed. The second series of cells
 *  represents the maximums that can be synchronously decompressed.
 *  1. The first cell in each series contains the count of the number of
 *     data length, scatter list elements pairs that follow – each being
 *     of the form
 *    a. One cell data byte length
 *    b. One cell total number of scatter list elements
 *
 * Example:
 *  # od -x ibm,max-sync-cop
 *  0000000 0000 0001 0000 1000 0000 01fe 0000 0001
 *  0000020 0000 1000 0000 01fe
 *
 *  In this example, compression supports 0x1000 (4,096) data byte length
 *  and 0x1fe (510) total scatter list elements.  Decompression supports
 *  0x1000 (4,096) data byte length and 0x1f3 (510) total scatter list
 *  elements.
 *
 * @devdata - struct nx842_devdata to update
 * @prop - struct property point containing the maxsyncop for the update
 *
 * Returns:
 *  0 on success
 *  -EINVAL on failure
 */
static int nx842_OF_upd_maxsyncop(struct nx842_devdata *devdata,
					struct property *prop) {
	int ret = 0;
	unsigned int comp_data_limit, decomp_data_limit;
	unsigned int comp_sg_limit, decomp_sg_limit;
	const struct maxsynccop_t {
		__be32 comp_elements;
		__be32 comp_data_limit;
		__be32 comp_sg_limit;
		__be32 decomp_elements;
		__be32 decomp_data_limit;
		__be32 decomp_sg_limit;
	} *maxsynccop;

	if (prop->length != sizeof(*maxsynccop)) {
		dev_err(devdata->dev, "%s: unexpected format for ibm,max-sync-cop property\n", __func__);
		dev_dbg(devdata->dev, "%s: ibm,max-sync-cop is %d bytes long, expected %lu bytes\n", __func__, prop->length,
				sizeof(*maxsynccop));
		ret = -EINVAL;
		goto out;
	}

	maxsynccop = (const struct maxsynccop_t *)prop->value;
	comp_data_limit = be32_to_cpu(maxsynccop->comp_data_limit);
	comp_sg_limit = be32_to_cpu(maxsynccop->comp_sg_limit);
	decomp_data_limit = be32_to_cpu(maxsynccop->decomp_data_limit);
	decomp_sg_limit = be32_to_cpu(maxsynccop->decomp_sg_limit);

	/* Use one limit rather than separate limits for compression and
	 * decompression. Set a maximum for this so as not to exceed the
	 * size that the header can support and round the value down to
	 * the hardware page size (4K) */
	devdata->max_sync_size = min(comp_data_limit, decomp_data_limit);

	devdata->max_sync_size = min_t(unsigned int, devdata->max_sync_size,
					65536);

	if (devdata->max_sync_size < 4096) {
		dev_err(devdata->dev, "%s: hardware max data size (%u) is "
				"less than the driver minimum, unable to use "
				"the hardware device\n",
				__func__, devdata->max_sync_size);
		ret = -EINVAL;
		goto out;
	}

	nx842_pseries_constraints.maximum = devdata->max_sync_size;

	devdata->max_sync_sg = min(comp_sg_limit, decomp_sg_limit);
	if (devdata->max_sync_sg < 1) {
		dev_err(devdata->dev, "%s: hardware max sg size (%u) is "
				"less than the driver minimum, unable to use "
				"the hardware device\n",
				__func__, devdata->max_sync_sg);
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

/**
 *
 * nx842_OF_upd -- Handle OF properties updates for the device.
 *
 * Set all properties from the OF tree.  Optionally, a new property
 * can be provided by the @new_prop pointer to overwrite an existing value.
 * The device will remain disabled until all values are valid, this function
 * will return an error for updates unless all values are valid.
 *
 * @new_prop: If not NULL, this property is being updated.  If NULL, update
 *  all properties from the current values in the OF tree.
 *
 * Returns:
 *  0 - Success
 *  -ENOMEM - Could not allocate memory for new devdata structure
 *  -EINVAL - property value not found, new_prop is not a recognized
 *	property for the device or property value is not valid.
 *  -ENODEV - Device is not available
 */
static int nx842_OF_upd(struct property *new_prop)
{
	struct nx842_devdata *old_devdata = NULL;
	struct nx842_devdata *new_devdata = NULL;
	struct device_node *of_node = NULL;
	struct property *status = NULL;
	struct property *maxsglen = NULL;
	struct property *maxsyncop = NULL;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&devdata_mutex, flags);
	old_devdata = rcu_dereference_check(devdata,
			lockdep_is_held(&devdata_mutex));
	if (old_devdata)
		of_node = old_devdata->dev->of_node;

	if (!old_devdata || !of_node) {
		pr_err("%s: device is not available\n", __func__);
		spin_unlock_irqrestore(&devdata_mutex, flags);
		return -ENODEV;
	}

	new_devdata = kzalloc(sizeof(*new_devdata), GFP_NOFS);
	if (!new_devdata) {
		dev_err(old_devdata->dev, "%s: Could not allocate memory for device data\n", __func__);
		ret = -ENOMEM;
		goto error_out;
	}

	memcpy(new_devdata, old_devdata, sizeof(*old_devdata));
	new_devdata->counters = old_devdata->counters;

	/* Set ptrs for existing properties */
	status = of_find_property(of_node, "status", NULL);
	maxsglen = of_find_property(of_node, "ibm,max-sg-len", NULL);
	maxsyncop = of_find_property(of_node, "ibm,max-sync-cop", NULL);
	if (!status || !maxsglen || !maxsyncop) {
		dev_err(old_devdata->dev, "%s: Could not locate device properties\n", __func__);
		ret = -EINVAL;
		goto error_out;
	}

	/*
	 * If this is a property update, there are only certain properties that
	 * we care about. Bail if it isn't in the below list
	 */
	if (new_prop && (strncmp(new_prop->name, "status", new_prop->length) ||
		         strncmp(new_prop->name, "ibm,max-sg-len", new_prop->length) ||
		         strncmp(new_prop->name, "ibm,max-sync-cop", new_prop->length)))
		goto out;

	/* Perform property updates */
	ret = nx842_OF_upd_status(new_devdata, status);
	if (ret)
		goto error_out;

	ret = nx842_OF_upd_maxsglen(new_devdata, maxsglen);
	if (ret)
		goto error_out;

	ret = nx842_OF_upd_maxsyncop(new_devdata, maxsyncop);
	if (ret)
		goto error_out;

out:
	dev_info(old_devdata->dev, "%s: max_sync_size new:%u old:%u\n",
			__func__, new_devdata->max_sync_size,
			old_devdata->max_sync_size);
	dev_info(old_devdata->dev, "%s: max_sync_sg new:%u old:%u\n",
			__func__, new_devdata->max_sync_sg,
			old_devdata->max_sync_sg);
	dev_info(old_devdata->dev, "%s: max_sg_len new:%u old:%u\n",
			__func__, new_devdata->max_sg_len,
			old_devdata->max_sg_len);

	rcu_assign_pointer(devdata, new_devdata);
	spin_unlock_irqrestore(&devdata_mutex, flags);
	synchronize_rcu();
	dev_set_drvdata(new_devdata->dev, new_devdata);
	kfree(old_devdata);
	return 0;

error_out:
	if (new_devdata) {
		dev_info(old_devdata->dev, "%s: device disabled\n", __func__);
		nx842_OF_set_defaults(new_devdata);
		rcu_assign_pointer(devdata, new_devdata);
		spin_unlock_irqrestore(&devdata_mutex, flags);
		synchronize_rcu();
		dev_set_drvdata(new_devdata->dev, new_devdata);
		kfree(old_devdata);
	} else {
		dev_err(old_devdata->dev, "%s: could not update driver from hardware\n", __func__);
		spin_unlock_irqrestore(&devdata_mutex, flags);
	}

	if (!ret)
		ret = -EINVAL;
	return ret;
}

/**
 * nx842_OF_notifier - Process updates to OF properties for the device
 *
 * @np: notifier block
 * @action: notifier action
 * @update: struct pSeries_reconfig_prop_update pointer if action is
 *	PSERIES_UPDATE_PROPERTY
 *
 * Returns:
 *	NOTIFY_OK on success
 *	NOTIFY_BAD encoded with error number on failure, use
 *		notifier_to_errno() to decode this value
 */
static int nx842_OF_notifier(struct notifier_block *np, unsigned long action,
			     void *data)
{
	struct of_reconfig_data *upd = data;
	struct nx842_devdata *local_devdata;
	struct device_node *node = NULL;

	rcu_read_lock();
	local_devdata = rcu_dereference(devdata);
	if (local_devdata)
		node = local_devdata->dev->of_node;

	if (local_devdata &&
			action == OF_RECONFIG_UPDATE_PROPERTY &&
			!strcmp(upd->dn->name, node->name)) {
		rcu_read_unlock();
		nx842_OF_upd(upd->prop);
	} else
		rcu_read_unlock();

	return NOTIFY_OK;
}

static struct notifier_block nx842_of_nb = {
	.notifier_call = nx842_OF_notifier,
};

#define nx842_counter_read(_name)					\
static ssize_t nx842_##_name##_show(struct device *dev,		\
		struct device_attribute *attr,				\
		char *buf) {						\
	struct nx842_devdata *local_devdata;			\
	int p = 0;							\
	rcu_read_lock();						\
	local_devdata = rcu_dereference(devdata);			\
	if (local_devdata)						\
		p = snprintf(buf, PAGE_SIZE, "%ld\n",			\
		       atomic64_read(&local_devdata->counters->_name));	\
	rcu_read_unlock();						\
	return p;							\
}

#define NX842DEV_COUNTER_ATTR_RO(_name)					\
	nx842_counter_read(_name);					\
	static struct device_attribute dev_attr_##_name = __ATTR(_name,	\
						0444,			\
						nx842_##_name##_show,\
						NULL);

NX842DEV_COUNTER_ATTR_RO(comp_complete);
NX842DEV_COUNTER_ATTR_RO(comp_failed);
NX842DEV_COUNTER_ATTR_RO(decomp_complete);
NX842DEV_COUNTER_ATTR_RO(decomp_failed);
NX842DEV_COUNTER_ATTR_RO(swdecomp);

static ssize_t nx842_timehist_show(struct device *,
		struct device_attribute *, char *);

static struct device_attribute dev_attr_comp_times = __ATTR(comp_times, 0444,
		nx842_timehist_show, NULL);
static struct device_attribute dev_attr_decomp_times = __ATTR(decomp_times,
		0444, nx842_timehist_show, NULL);

static ssize_t nx842_timehist_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	char *p = buf;
	struct nx842_devdata *local_devdata;
	atomic64_t *times;
	int bytes_remain = PAGE_SIZE;
	int bytes;
	int i;

	rcu_read_lock();
	local_devdata = rcu_dereference(devdata);
	if (!local_devdata) {
		rcu_read_unlock();
		return 0;
	}

	if (attr == &dev_attr_comp_times)
		times = local_devdata->counters->comp_times;
	else if (attr == &dev_attr_decomp_times)
		times = local_devdata->counters->decomp_times;
	else {
		rcu_read_unlock();
		return 0;
	}

	for (i = 0; i < (NX842_HIST_SLOTS - 2); i++) {
		bytes = snprintf(p, bytes_remain, "%u-%uus:\t%ld\n",
			       i ? (2<<(i-1)) : 0, (2<<i)-1,
			       atomic64_read(&times[i]));
		bytes_remain -= bytes;
		p += bytes;
	}
	/* The last bucket holds everything over
	 * 2<<(NX842_HIST_SLOTS - 2) us */
	bytes = snprintf(p, bytes_remain, "%uus - :\t%ld\n",
			2<<(NX842_HIST_SLOTS - 2),
			atomic64_read(&times[(NX842_HIST_SLOTS - 1)]));
	p += bytes;

	rcu_read_unlock();
	return p - buf;
}

static struct attribute *nx842_sysfs_entries[] = {
	&dev_attr_comp_complete.attr,
	&dev_attr_comp_failed.attr,
	&dev_attr_decomp_complete.attr,
	&dev_attr_decomp_failed.attr,
	&dev_attr_swdecomp.attr,
	&dev_attr_comp_times.attr,
	&dev_attr_decomp_times.attr,
	NULL,
};

static struct attribute_group nx842_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = nx842_sysfs_entries,
};

static struct nx842_driver nx842_pseries_driver = {
	.name =		KBUILD_MODNAME,
	.owner =	THIS_MODULE,
	.workmem_size =	sizeof(struct nx842_workmem),
	.constraints =	&nx842_pseries_constraints,
	.compress =	nx842_pseries_compress,
	.decompress =	nx842_pseries_decompress,
};

static int __init nx842_probe(struct vio_dev *viodev,
				  const struct vio_device_id *id)
{
	struct nx842_devdata *old_devdata, *new_devdata = NULL;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&devdata_mutex, flags);
	old_devdata = rcu_dereference_check(devdata,
			lockdep_is_held(&devdata_mutex));

	if (old_devdata && old_devdata->vdev != NULL) {
		dev_err(&viodev->dev, "%s: Attempt to register more than one instance of the hardware\n", __func__);
		ret = -1;
		goto error_unlock;
	}

	dev_set_drvdata(&viodev->dev, NULL);

	new_devdata = kzalloc(sizeof(*new_devdata), GFP_NOFS);
	if (!new_devdata) {
		dev_err(&viodev->dev, "%s: Could not allocate memory for device data\n", __func__);
		ret = -ENOMEM;
		goto error_unlock;
	}

	new_devdata->counters = kzalloc(sizeof(*new_devdata->counters),
			GFP_NOFS);
	if (!new_devdata->counters) {
		dev_err(&viodev->dev, "%s: Could not allocate memory for performance counters\n", __func__);
		ret = -ENOMEM;
		goto error_unlock;
	}

	new_devdata->vdev = viodev;
	new_devdata->dev = &viodev->dev;
	nx842_OF_set_defaults(new_devdata);

	rcu_assign_pointer(devdata, new_devdata);
	spin_unlock_irqrestore(&devdata_mutex, flags);
	synchronize_rcu();
	kfree(old_devdata);

	of_reconfig_notifier_register(&nx842_of_nb);

	ret = nx842_OF_upd(NULL);
	if (ret && ret != -ENODEV) {
		dev_err(&viodev->dev, "could not parse device tree. %d\n", ret);
		ret = -1;
		goto error;
	}

	rcu_read_lock();
	dev_set_drvdata(&viodev->dev, rcu_dereference(devdata));
	rcu_read_unlock();

	if (sysfs_create_group(&viodev->dev.kobj, &nx842_attribute_group)) {
		dev_err(&viodev->dev, "could not create sysfs device attributes\n");
		ret = -1;
		goto error;
	}

	return 0;

error_unlock:
	spin_unlock_irqrestore(&devdata_mutex, flags);
	if (new_devdata)
		kfree(new_devdata->counters);
	kfree(new_devdata);
error:
	return ret;
}

static int __exit nx842_remove(struct vio_dev *viodev)
{
	struct nx842_devdata *old_devdata;
	unsigned long flags;

	pr_info("Removing IBM Power 842 compression device\n");
	sysfs_remove_group(&viodev->dev.kobj, &nx842_attribute_group);

	spin_lock_irqsave(&devdata_mutex, flags);
	old_devdata = rcu_dereference_check(devdata,
			lockdep_is_held(&devdata_mutex));
	of_reconfig_notifier_unregister(&nx842_of_nb);
	RCU_INIT_POINTER(devdata, NULL);
	spin_unlock_irqrestore(&devdata_mutex, flags);
	synchronize_rcu();
	dev_set_drvdata(&viodev->dev, NULL);
	if (old_devdata)
		kfree(old_devdata->counters);
	kfree(old_devdata);

	return 0;
}

static struct vio_device_id nx842_vio_driver_ids[] = {
	{"ibm,compression-v1", "ibm,compression"},
	{"", ""},
};

static struct vio_driver nx842_vio_driver = {
	.name = KBUILD_MODNAME,
	.probe = nx842_probe,
	.remove = __exit_p(nx842_remove),
	.get_desired_dma = nx842_get_desired_dma,
	.id_table = nx842_vio_driver_ids,
};

static int __init nx842_pseries_init(void)
{
	struct nx842_devdata *new_devdata;
	int ret;

	pr_info("Registering IBM Power 842 compression driver\n");

	if (!of_find_compatible_node(NULL, NULL, "ibm,compression"))
		return -ENODEV;

	RCU_INIT_POINTER(devdata, NULL);
	new_devdata = kzalloc(sizeof(*new_devdata), GFP_KERNEL);
	if (!new_devdata) {
		pr_err("Could not allocate memory for device data\n");
		return -ENOMEM;
	}
	new_devdata->status = UNAVAILABLE;
	RCU_INIT_POINTER(devdata, new_devdata);

	ret = vio_register_driver(&nx842_vio_driver);
	if (ret) {
		pr_err("Could not register VIO driver %d\n", ret);

		kfree(new_devdata);
		return ret;
	}

	if (!nx842_platform_driver_set(&nx842_pseries_driver)) {
		vio_unregister_driver(&nx842_vio_driver);
		kfree(new_devdata);
		return -EEXIST;
	}

	return 0;
}

module_init(nx842_pseries_init);

static void __exit nx842_pseries_exit(void)
{
	struct nx842_devdata *old_devdata;
	unsigned long flags;

	pr_info("Exiting IBM Power 842 compression driver\n");
	nx842_platform_driver_unset(&nx842_pseries_driver);
	spin_lock_irqsave(&devdata_mutex, flags);
	old_devdata = rcu_dereference_check(devdata,
			lockdep_is_held(&devdata_mutex));
	RCU_INIT_POINTER(devdata, NULL);
	spin_unlock_irqrestore(&devdata_mutex, flags);
	synchronize_rcu();
	if (old_devdata && old_devdata->dev)
		dev_set_drvdata(old_devdata->dev, NULL);
	kfree(old_devdata);
	vio_unregister_driver(&nx842_vio_driver);
}

module_exit(nx842_pseries_exit);

