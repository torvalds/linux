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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nx842.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <asm/page.h>
#include <asm/vio.h>

#include "nx_csbcpb.h" /* struct nx_csbcpb */

#define MODULE_NAME "nx-compress"
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Jennings <rcj@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("842 H/W Compression driver for IBM Power processors");

#define SHIFT_4K 12
#define SHIFT_64K 16
#define SIZE_4K (1UL << SHIFT_4K)
#define SIZE_64K (1UL << SHIFT_64K)

/* IO buffer must be 128 byte aligned */
#define IO_BUFFER_ALIGN 128

struct nx842_header {
	int blocks_nr; /* number of compressed blocks */
	int offset; /* offset of the first block (from beginning of header) */
	int sizes[0]; /* size of compressed blocks */
};

static inline int nx842_header_size(const struct nx842_header *hdr)
{
	return sizeof(struct nx842_header) +
			hdr->blocks_nr * sizeof(hdr->sizes[0]);
}

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
#define NX842_HW_PAGE_SHIFT	SHIFT_4K
#define NX842_HW_PAGE_SIZE	(ASM_CONST(1) << NX842_HW_PAGE_SHIFT)
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
	unsigned long ptr; /* Real address (use __pa()) */
	unsigned long len;
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

static inline unsigned long nx842_get_pa(void *addr)
{
	if (is_vmalloc_addr(addr))
		return page_to_phys(vmalloc_to_page(addr))
		       + offset_in_page(addr);
	else
		return __pa(addr);
}

static int nx842_build_scatterlist(unsigned long buf, int len,
			struct nx842_scatterlist *sl)
{
	unsigned long nextpage;
	struct nx842_slentry *entry;

	sl->entry_nr = 0;

	entry = sl->entries;
	while (len) {
		entry->ptr = nx842_get_pa((void *)buf);
		nextpage = ALIGN(buf + 1, NX842_HW_PAGE_SIZE);
		if (nextpage < buf + len) {
			/* we aren't at the end yet */
			if (IS_ALIGNED(buf, NX842_HW_PAGE_SIZE))
				/* we are in the middle (or beginning) */
				entry->len = NX842_HW_PAGE_SIZE;
			else
				/* we are at the beginning */
				entry->len = nextpage - buf;
		} else {
			/* at the end */
			entry->len = len;
		}

		len -= entry->len;
		buf += entry->len;
		sl->entry_nr++;
		entry++;
	}

	return 0;
}

/*
 * Working memory for software decompression
 */
struct sw842_fifo {
	union {
		char f8[256][8];
		char f4[512][4];
	};
	char f2[256][2];
	unsigned char f84_full;
	unsigned char f2_full;
	unsigned char f8_count;
	unsigned char f2_count;
	unsigned int f4_count;
};

/*
 * Working memory for crypto API
 */
struct nx842_workmem {
	char bounce[PAGE_SIZE]; /* bounce buffer for decompression input */
	union {
		/* hardware working memory */
		struct {
			/* scatterlist */
			char slin[SIZE_4K];
			char slout[SIZE_4K];
			/* coprocessor status/parameter block */
			struct nx_csbcpb csbcpb;
		};
		/* software working memory */
		struct sw842_fifo swfifo; /* software decompression fifo */
	};
};

int nx842_get_workmem_size(void)
{
	return sizeof(struct nx842_workmem) + NX842_HW_PAGE_SIZE;
}
EXPORT_SYMBOL_GPL(nx842_get_workmem_size);

int nx842_get_workmem_size_aligned(void)
{
	return sizeof(struct nx842_workmem);
}
EXPORT_SYMBOL_GPL(nx842_get_workmem_size_aligned);

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
				csb->processed_byte_count,
				(unsigned long)csb->address);
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
 * nx842_compress - Compress data using the 842 algorithm
 *
 * Compression provide by the NX842 coprocessor on IBM Power systems.
 * The input buffer is compressed and the result is stored in the
 * provided output buffer.
 *
 * Upon return from this function @outlen contains the length of the
 * compressed data.  If there is an error then @outlen will be 0 and an
 * error will be specified by the return code from this function.
 *
 * @in: Pointer to input buffer, must be page aligned
 * @inlen: Length of input buffer, must be PAGE_SIZE
 * @out: Pointer to output buffer
 * @outlen: Length of output buffer
 * @wrkmem: ptr to buffer for working memory, size determined by
 *          nx842_get_workmem_size()
 *
 * Returns:
 *   0		Success, output of length @outlen stored in the buffer at @out
 *   -ENOMEM	Unable to allocate internal buffers
 *   -ENOSPC	Output buffer is to small
 *   -EMSGSIZE	XXX Difficult to describe this limitation
 *   -EIO	Internal error
 *   -ENODEV	Hardware unavailable
 */
int nx842_compress(const unsigned char *in, unsigned int inlen,
		       unsigned char *out, unsigned int *outlen, void *wmem)
{
	struct nx842_header *hdr;
	struct nx842_devdata *local_devdata;
	struct device *dev = NULL;
	struct nx842_workmem *workmem;
	struct nx842_scatterlist slin, slout;
	struct nx_csbcpb *csbcpb;
	int ret = 0, max_sync_size, i, bytesleft, size, hdrsize;
	unsigned long inbuf, outbuf, padding;
	struct vio_pfo_op op = {
		.done = NULL,
		.handle = 0,
		.timeout = 0,
	};
	unsigned long start_time = get_tb();

	/*
	 * Make sure input buffer is 64k page aligned.  This is assumed since
	 * this driver is designed for page compression only (for now).  This
	 * is very nice since we can now use direct DDE(s) for the input and
	 * the alignment is guaranteed.
	*/
	inbuf = (unsigned long)in;
	if (!IS_ALIGNED(inbuf, PAGE_SIZE) || inlen != PAGE_SIZE)
		return -EINVAL;

	rcu_read_lock();
	local_devdata = rcu_dereference(devdata);
	if (!local_devdata || !local_devdata->dev) {
		rcu_read_unlock();
		return -ENODEV;
	}
	max_sync_size = local_devdata->max_sync_size;
	dev = local_devdata->dev;

	/* Create the header */
	hdr = (struct nx842_header *)out;
	hdr->blocks_nr = PAGE_SIZE / max_sync_size;
	hdrsize = nx842_header_size(hdr);
	outbuf = (unsigned long)out + hdrsize;
	bytesleft = *outlen - hdrsize;

	/* Init scatterlist */
	workmem = (struct nx842_workmem *)ALIGN((unsigned long)wmem,
		NX842_HW_PAGE_SIZE);
	slin.entries = (struct nx842_slentry *)workmem->slin;
	slout.entries = (struct nx842_slentry *)workmem->slout;

	/* Init operation */
	op.flags = NX842_OP_COMPRESS;
	csbcpb = &workmem->csbcpb;
	memset(csbcpb, 0, sizeof(*csbcpb));
	op.csbcpb = nx842_get_pa(csbcpb);
	op.out = nx842_get_pa(slout.entries);

	for (i = 0; i < hdr->blocks_nr; i++) {
		/*
		 * Aligning the output blocks to 128 bytes does waste space,
		 * but it prevents the need for bounce buffers and memory
		 * copies.  It also simplifies the code a lot.  In the worst
		 * case (64k page, 4k max_sync_size), you lose up to
		 * (128*16)/64k = ~3% the compression factor. For 64k
		 * max_sync_size, the loss would be at most 128/64k = ~0.2%.
		 */
		padding = ALIGN(outbuf, IO_BUFFER_ALIGN) - outbuf;
		outbuf += padding;
		bytesleft -= padding;
		if (i == 0)
			/* save offset into first block in header */
			hdr->offset = padding + hdrsize;

		if (bytesleft <= 0) {
			ret = -ENOSPC;
			goto unlock;
		}

		/*
		 * NOTE: If the default max_sync_size is changed from 4k
		 * to 64k, remove the "likely" case below, since a
		 * scatterlist will always be needed.
		 */
		if (likely(max_sync_size == NX842_HW_PAGE_SIZE)) {
			/* Create direct DDE */
			op.in = nx842_get_pa((void *)inbuf);
			op.inlen = max_sync_size;

		} else {
			/* Create indirect DDE (scatterlist) */
			nx842_build_scatterlist(inbuf, max_sync_size, &slin);
			op.in = nx842_get_pa(slin.entries);
			op.inlen = -nx842_get_scatterlist_size(&slin);
		}

		/*
		 * If max_sync_size != NX842_HW_PAGE_SIZE, an indirect
		 * DDE is required for the outbuf.
		 * If max_sync_size == NX842_HW_PAGE_SIZE, outbuf must
		 * also be page aligned (1 in 128/4k=32 chance) in order
		 * to use a direct DDE.
		 * This is unlikely, just use an indirect DDE always.
		 */
		nx842_build_scatterlist(outbuf,
			min(bytesleft, max_sync_size), &slout);
		/* op.out set before loop */
		op.outlen = -nx842_get_scatterlist_size(&slout);

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
		if (ret && ret != -ENOSPC)
			goto unlock;

		/* Handle incompressible data */
		if (unlikely(ret == -ENOSPC)) {
			if (bytesleft < max_sync_size) {
				/*
				 * Not enough space left in the output buffer
				 * to store uncompressed block
				 */
				goto unlock;
			} else {
				/* Store incompressible block */
				memcpy((void *)outbuf, (void *)inbuf,
					max_sync_size);
				hdr->sizes[i] = -max_sync_size;
				outbuf += max_sync_size;
				bytesleft -= max_sync_size;
				/* Reset ret, incompressible data handled */
				ret = 0;
			}
		} else {
			/* Normal case, compression was successful */
			size = csbcpb->csb.processed_byte_count;
			dev_dbg(dev, "%s: processed_bytes=%d\n",
				__func__, size);
			hdr->sizes[i] = size;
			outbuf += size;
			bytesleft -= size;
		}

		inbuf += max_sync_size;
	}

	*outlen = (unsigned int)(outbuf - (unsigned long)out);

unlock:
	if (ret)
		nx842_inc_comp_failed(local_devdata);
	else {
		nx842_inc_comp_complete(local_devdata);
		ibm_nx842_incr_hist(local_devdata->counters->comp_times,
			(get_tb() - start_time) / tb_ticks_per_usec);
	}
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(nx842_compress);

static int sw842_decompress(const unsigned char *, int, unsigned char *, int *,
			const void *);

/**
 * nx842_decompress - Decompress data using the 842 algorithm
 *
 * Decompression provide by the NX842 coprocessor on IBM Power systems.
 * The input buffer is decompressed and the result is stored in the
 * provided output buffer.  The size allocated to the output buffer is
 * provided by the caller of this function in @outlen.  Upon return from
 * this function @outlen contains the length of the decompressed data.
 * If there is an error then @outlen will be 0 and an error will be
 * specified by the return code from this function.
 *
 * @in: Pointer to input buffer, will use bounce buffer if not 128 byte
 *      aligned
 * @inlen: Length of input buffer
 * @out: Pointer to output buffer, must be page aligned
 * @outlen: Length of output buffer, must be PAGE_SIZE
 * @wrkmem: ptr to buffer for working memory, size determined by
 *          nx842_get_workmem_size()
 *
 * Returns:
 *   0		Success, output of length @outlen stored in the buffer at @out
 *   -ENODEV	Hardware decompression device is unavailable
 *   -ENOMEM	Unable to allocate internal buffers
 *   -ENOSPC	Output buffer is to small
 *   -EINVAL	Bad input data encountered when attempting decompress
 *   -EIO	Internal error
 */
int nx842_decompress(const unsigned char *in, unsigned int inlen,
			 unsigned char *out, unsigned int *outlen, void *wmem)
{
	struct nx842_header *hdr;
	struct nx842_devdata *local_devdata;
	struct device *dev = NULL;
	struct nx842_workmem *workmem;
	struct nx842_scatterlist slin, slout;
	struct nx_csbcpb *csbcpb;
	int ret = 0, i, size, max_sync_size;
	unsigned long inbuf, outbuf;
	struct vio_pfo_op op = {
		.done = NULL,
		.handle = 0,
		.timeout = 0,
	};
	unsigned long start_time = get_tb();

	/* Ensure page alignment and size */
	outbuf = (unsigned long)out;
	if (!IS_ALIGNED(outbuf, PAGE_SIZE) || *outlen != PAGE_SIZE)
		return -EINVAL;

	rcu_read_lock();
	local_devdata = rcu_dereference(devdata);
	if (local_devdata)
		dev = local_devdata->dev;

	/* Get header */
	hdr = (struct nx842_header *)in;

	workmem = (struct nx842_workmem *)ALIGN((unsigned long)wmem,
		NX842_HW_PAGE_SIZE);

	inbuf = (unsigned long)in + hdr->offset;
	if (likely(!IS_ALIGNED(inbuf, IO_BUFFER_ALIGN))) {
		/* Copy block(s) into bounce buffer for alignment */
		memcpy(workmem->bounce, in + hdr->offset, inlen - hdr->offset);
		inbuf = (unsigned long)workmem->bounce;
	}

	/* Init scatterlist */
	slin.entries = (struct nx842_slentry *)workmem->slin;
	slout.entries = (struct nx842_slentry *)workmem->slout;

	/* Init operation */
	op.flags = NX842_OP_DECOMPRESS;
	csbcpb = &workmem->csbcpb;
	memset(csbcpb, 0, sizeof(*csbcpb));
	op.csbcpb = nx842_get_pa(csbcpb);

	/*
	 * max_sync_size may have changed since compression,
	 * so we can't read it from the device info. We need
	 * to derive it from hdr->blocks_nr.
	 */
	max_sync_size = PAGE_SIZE / hdr->blocks_nr;

	for (i = 0; i < hdr->blocks_nr; i++) {
		/* Skip padding */
		inbuf = ALIGN(inbuf, IO_BUFFER_ALIGN);

		if (hdr->sizes[i] < 0) {
			/* Negative sizes indicate uncompressed data blocks */
			size = abs(hdr->sizes[i]);
			memcpy((void *)outbuf, (void *)inbuf, size);
			outbuf += size;
			inbuf += size;
			continue;
		}

		if (!dev)
			goto sw;

		/*
		 * The better the compression, the more likely the "likely"
		 * case becomes.
		 */
		if (likely((inbuf & NX842_HW_PAGE_MASK) ==
			((inbuf + hdr->sizes[i] - 1) & NX842_HW_PAGE_MASK))) {
			/* Create direct DDE */
			op.in = nx842_get_pa((void *)inbuf);
			op.inlen = hdr->sizes[i];
		} else {
			/* Create indirect DDE (scatterlist) */
			nx842_build_scatterlist(inbuf, hdr->sizes[i] , &slin);
			op.in = nx842_get_pa(slin.entries);
			op.inlen = -nx842_get_scatterlist_size(&slin);
		}

		/*
		 * NOTE: If the default max_sync_size is changed from 4k
		 * to 64k, remove the "likely" case below, since a
		 * scatterlist will always be needed.
		 */
		if (likely(max_sync_size == NX842_HW_PAGE_SIZE)) {
			/* Create direct DDE */
			op.out = nx842_get_pa((void *)outbuf);
			op.outlen = max_sync_size;
		} else {
			/* Create indirect DDE (scatterlist) */
			nx842_build_scatterlist(outbuf, max_sync_size, &slout);
			op.out = nx842_get_pa(slout.entries);
			op.outlen = -nx842_get_scatterlist_size(&slout);
		}

		/* Send request to pHyp */
		ret = vio_h_cop_sync(local_devdata->vdev, &op);

		/* Check for pHyp error */
		if (ret) {
			dev_dbg(dev, "%s: vio_h_cop_sync error (ret=%d, hret=%ld)\n",
				__func__, ret, op.hcall_err);
			dev = NULL;
			goto sw;
		}

		/* Check for hardware error */
		ret = nx842_validate_result(dev, &csbcpb->csb);
		if (ret) {
			dev = NULL;
			goto sw;
		}

		/* HW decompression success */
		inbuf += hdr->sizes[i];
		outbuf += csbcpb->csb.processed_byte_count;
		continue;

sw:
		/* software decompression */
		size = max_sync_size;
		ret = sw842_decompress(
			(unsigned char *)inbuf, hdr->sizes[i],
			(unsigned char *)outbuf, &size, wmem);
		if (ret)
			pr_debug("%s: sw842_decompress failed with %d\n",
				__func__, ret);

		if (ret) {
			if (ret != -ENOSPC && ret != -EINVAL &&
					ret != -EMSGSIZE)
				ret = -EIO;
			goto unlock;
		}

		/* SW decompression success */
		inbuf += hdr->sizes[i];
		outbuf += size;
	}

	*outlen = (unsigned int)(outbuf - (unsigned long)out);

unlock:
	if (ret)
		/* decompress fail */
		nx842_inc_decomp_failed(local_devdata);
	else {
		if (!dev)
			/* software decompress */
			nx842_inc_swdecomp(local_devdata);
		nx842_inc_decomp_complete(local_devdata);
		ibm_nx842_incr_hist(local_devdata->counters->decomp_times,
			(get_tb() - start_time) / tb_ticks_per_usec);
	}

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(nx842_decompress);

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
 *  -EINVAL - Device is not available
 */
static int nx842_OF_upd_status(struct nx842_devdata *devdata,
					struct property *prop) {
	int ret = 0;
	const char *status = (const char *)prop->value;

	if (!strncmp(status, "okay", (size_t)prop->length)) {
		devdata->status = AVAILABLE;
	} else {
		dev_info(devdata->dev, "%s: status '%s' is not 'okay'\n",
				__func__, status);
		devdata->status = UNAVAILABLE;
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
	const int *maxsglen = prop->value;

	if (prop->length != sizeof(*maxsglen)) {
		dev_err(devdata->dev, "%s: unexpected format for ibm,max-sg-len property\n", __func__);
		dev_dbg(devdata->dev, "%s: ibm,max-sg-len is %d bytes long, expected %lu bytes\n", __func__,
				prop->length, sizeof(*maxsglen));
		ret = -EINVAL;
	} else {
		devdata->max_sg_len = (unsigned int)min(*maxsglen,
				(int)NX842_HW_PAGE_SIZE);
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
	const struct maxsynccop_t {
		int comp_elements;
		int comp_data_limit;
		int comp_sg_limit;
		int decomp_elements;
		int decomp_data_limit;
		int decomp_sg_limit;
	} *maxsynccop;

	if (prop->length != sizeof(*maxsynccop)) {
		dev_err(devdata->dev, "%s: unexpected format for ibm,max-sync-cop property\n", __func__);
		dev_dbg(devdata->dev, "%s: ibm,max-sync-cop is %d bytes long, expected %lu bytes\n", __func__, prop->length,
				sizeof(*maxsynccop));
		ret = -EINVAL;
		goto out;
	}

	maxsynccop = (const struct maxsynccop_t *)prop->value;

	/* Use one limit rather than separate limits for compression and
	 * decompression. Set a maximum for this so as not to exceed the
	 * size that the header can support and round the value down to
	 * the hardware page size (4K) */
	devdata->max_sync_size =
			(unsigned int)min(maxsynccop->comp_data_limit,
					maxsynccop->decomp_data_limit);

	devdata->max_sync_size = min_t(unsigned int, devdata->max_sync_size,
					SIZE_64K);

	if (devdata->max_sync_size < SIZE_4K) {
		dev_err(devdata->dev, "%s: hardware max data size (%u) is "
				"less than the driver minimum, unable to use "
				"the hardware device\n",
				__func__, devdata->max_sync_size);
		ret = -EINVAL;
		goto out;
	}

	devdata->max_sync_sg = (unsigned int)min(maxsynccop->comp_sg_limit,
						maxsynccop->decomp_sg_limit);
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

	/* Set ptr to new property if provided */
	if (new_prop) {
		/* Single property */
		if (!strncmp(new_prop->name, "status", new_prop->length)) {
			status = new_prop;

		} else if (!strncmp(new_prop->name, "ibm,max-sg-len",
					new_prop->length)) {
			maxsglen = new_prop;

		} else if (!strncmp(new_prop->name, "ibm,max-sync-cop",
					new_prop->length)) {
			maxsyncop = new_prop;

		} else {
			/*
			 * Skip the update, the property being updated
			 * has no impact.
			 */
			goto out;
		}
	}

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
			     void *update)
{
	struct of_prop_reconfig *upd = update;
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

static struct vio_device_id nx842_driver_ids[] = {
	{"ibm,compression-v1", "ibm,compression"},
	{"", ""},
};

static struct vio_driver nx842_driver = {
	.name = MODULE_NAME,
	.probe = nx842_probe,
	.remove = nx842_remove,
	.get_desired_dma = nx842_get_desired_dma,
	.id_table = nx842_driver_ids,
};

static int __init nx842_init(void)
{
	struct nx842_devdata *new_devdata;
	pr_info("Registering IBM Power 842 compression driver\n");

	RCU_INIT_POINTER(devdata, NULL);
	new_devdata = kzalloc(sizeof(*new_devdata), GFP_KERNEL);
	if (!new_devdata) {
		pr_err("Could not allocate memory for device data\n");
		return -ENOMEM;
	}
	new_devdata->status = UNAVAILABLE;
	RCU_INIT_POINTER(devdata, new_devdata);

	return vio_register_driver(&nx842_driver);
}

module_init(nx842_init);

static void __exit nx842_exit(void)
{
	struct nx842_devdata *old_devdata;
	unsigned long flags;

	pr_info("Exiting IBM Power 842 compression driver\n");
	spin_lock_irqsave(&devdata_mutex, flags);
	old_devdata = rcu_dereference_check(devdata,
			lockdep_is_held(&devdata_mutex));
	RCU_INIT_POINTER(devdata, NULL);
	spin_unlock_irqrestore(&devdata_mutex, flags);
	synchronize_rcu();
	if (old_devdata)
		dev_set_drvdata(old_devdata->dev, NULL);
	kfree(old_devdata);
	vio_unregister_driver(&nx842_driver);
}

module_exit(nx842_exit);

/*********************************
 * 842 software decompressor
*********************************/
typedef int (*sw842_template_op)(const char **, int *, unsigned char **,
						struct sw842_fifo *);

static int sw842_data8(const char **, int *, unsigned char **,
						struct sw842_fifo *);
static int sw842_data4(const char **, int *, unsigned char **,
						struct sw842_fifo *);
static int sw842_data2(const char **, int *, unsigned char **,
						struct sw842_fifo *);
static int sw842_ptr8(const char **, int *, unsigned char **,
						struct sw842_fifo *);
static int sw842_ptr4(const char **, int *, unsigned char **,
						struct sw842_fifo *);
static int sw842_ptr2(const char **, int *, unsigned char **,
						struct sw842_fifo *);

/* special templates */
#define SW842_TMPL_REPEAT 0x1B
#define SW842_TMPL_ZEROS 0x1C
#define SW842_TMPL_EOF 0x1E

static sw842_template_op sw842_tmpl_ops[26][4] = {
	{ sw842_data8, NULL}, /* 0 (00000) */
	{ sw842_data4, sw842_data2, sw842_ptr2,  NULL},
	{ sw842_data4, sw842_ptr2,  sw842_data2, NULL},
	{ sw842_data4, sw842_ptr2,  sw842_ptr2,  NULL},
	{ sw842_data4, sw842_ptr4,  NULL},
	{ sw842_data2, sw842_ptr2,  sw842_data4, NULL},
	{ sw842_data2, sw842_ptr2,  sw842_data2, sw842_ptr2},
	{ sw842_data2, sw842_ptr2,  sw842_ptr2,  sw842_data2},
	{ sw842_data2, sw842_ptr2,  sw842_ptr2,  sw842_ptr2,},
	{ sw842_data2, sw842_ptr2,  sw842_ptr4,  NULL},
	{ sw842_ptr2,  sw842_data2, sw842_data4, NULL}, /* 10 (01010) */
	{ sw842_ptr2,  sw842_data4, sw842_ptr2,  NULL},
	{ sw842_ptr2,  sw842_data2, sw842_ptr2,  sw842_data2},
	{ sw842_ptr2,  sw842_data2, sw842_ptr2,  sw842_ptr2},
	{ sw842_ptr2,  sw842_data2, sw842_ptr4,  NULL},
	{ sw842_ptr2,  sw842_ptr2,  sw842_data4, NULL},
	{ sw842_ptr2,  sw842_ptr2,  sw842_data2, sw842_ptr2},
	{ sw842_ptr2,  sw842_ptr2,  sw842_ptr2,  sw842_data2},
	{ sw842_ptr2,  sw842_ptr2,  sw842_ptr2,  sw842_ptr2},
	{ sw842_ptr2,  sw842_ptr2,  sw842_ptr4,  NULL},
	{ sw842_ptr4,  sw842_data4, NULL}, /* 20 (10100) */
	{ sw842_ptr4,  sw842_data2, sw842_ptr2,  NULL},
	{ sw842_ptr4,  sw842_ptr2,  sw842_data2, NULL},
	{ sw842_ptr4,  sw842_ptr2,  sw842_ptr2,  NULL},
	{ sw842_ptr4,  sw842_ptr4,  NULL},
	{ sw842_ptr8,  NULL}
};

/* Software decompress helpers */

static uint8_t sw842_get_byte(const char *buf, int bit)
{
	uint8_t tmpl;
	uint16_t tmp;
	tmp = htons(*(uint16_t *)(buf));
	tmp = (uint16_t)(tmp << bit);
	tmp = ntohs(tmp);
	memcpy(&tmpl, &tmp, 1);
	return tmpl;
}

static uint8_t sw842_get_template(const char **buf, int *bit)
{
	uint8_t byte;
	byte = sw842_get_byte(*buf, *bit);
	byte = byte >> 3;
	byte &= 0x1F;
	*buf += (*bit + 5) / 8;
	*bit = (*bit + 5) % 8;
	return byte;
}

/* repeat_count happens to be 5-bit too (like the template) */
static uint8_t sw842_get_repeat_count(const char **buf, int *bit)
{
	uint8_t byte;
	byte = sw842_get_byte(*buf, *bit);
	byte = byte >> 2;
	byte &= 0x3F;
	*buf += (*bit + 6) / 8;
	*bit = (*bit + 6) % 8;
	return byte;
}

static uint8_t sw842_get_ptr2(const char **buf, int *bit)
{
	uint8_t ptr;
	ptr = sw842_get_byte(*buf, *bit);
	(*buf)++;
	return ptr;
}

static uint16_t sw842_get_ptr4(const char **buf, int *bit,
		struct sw842_fifo *fifo)
{
	uint16_t ptr;
	ptr = htons(*(uint16_t *)(*buf));
	ptr = (uint16_t)(ptr << *bit);
	ptr = ptr >> 7;
	ptr &= 0x01FF;
	*buf += (*bit + 9) / 8;
	*bit = (*bit + 9) % 8;
	return ptr;
}

static uint8_t sw842_get_ptr8(const char **buf, int *bit,
		struct sw842_fifo *fifo)
{
	return sw842_get_ptr2(buf, bit);
}

/* Software decompress template ops */

static int sw842_data8(const char **inbuf, int *inbit,
		unsigned char **outbuf, struct sw842_fifo *fifo)
{
	int ret;

	ret = sw842_data4(inbuf, inbit, outbuf, fifo);
	if (ret)
		return ret;
	ret = sw842_data4(inbuf, inbit, outbuf, fifo);
	return ret;
}

static int sw842_data4(const char **inbuf, int *inbit,
		unsigned char **outbuf, struct sw842_fifo *fifo)
{
	int ret;

	ret = sw842_data2(inbuf, inbit, outbuf, fifo);
	if (ret)
		return ret;
	ret = sw842_data2(inbuf, inbit, outbuf, fifo);
	return ret;
}

static int sw842_data2(const char **inbuf, int *inbit,
		unsigned char **outbuf, struct sw842_fifo *fifo)
{
	**outbuf = sw842_get_byte(*inbuf, *inbit);
	(*inbuf)++;
	(*outbuf)++;
	**outbuf = sw842_get_byte(*inbuf, *inbit);
	(*inbuf)++;
	(*outbuf)++;
	return 0;
}

static int sw842_ptr8(const char **inbuf, int *inbit,
		unsigned char **outbuf, struct sw842_fifo *fifo)
{
	uint8_t ptr;
	ptr = sw842_get_ptr8(inbuf, inbit, fifo);
	if (!fifo->f84_full && (ptr >= fifo->f8_count))
		return 1;
	memcpy(*outbuf, fifo->f8[ptr], 8);
	*outbuf += 8;
	return 0;
}

static int sw842_ptr4(const char **inbuf, int *inbit,
		unsigned char **outbuf, struct sw842_fifo *fifo)
{
	uint16_t ptr;
	ptr = sw842_get_ptr4(inbuf, inbit, fifo);
	if (!fifo->f84_full && (ptr >= fifo->f4_count))
		return 1;
	memcpy(*outbuf, fifo->f4[ptr], 4);
	*outbuf += 4;
	return 0;
}

static int sw842_ptr2(const char **inbuf, int *inbit,
		unsigned char **outbuf, struct sw842_fifo *fifo)
{
	uint8_t ptr;
	ptr = sw842_get_ptr2(inbuf, inbit);
	if (!fifo->f2_full && (ptr >= fifo->f2_count))
		return 1;
	memcpy(*outbuf, fifo->f2[ptr], 2);
	*outbuf += 2;
	return 0;
}

static void sw842_copy_to_fifo(const char *buf, struct sw842_fifo *fifo)
{
	unsigned char initial_f2count = fifo->f2_count;

	memcpy(fifo->f8[fifo->f8_count], buf, 8);
	fifo->f4_count += 2;
	fifo->f8_count += 1;

	if (!fifo->f84_full && fifo->f4_count >= 512) {
		fifo->f84_full = 1;
		fifo->f4_count /= 512;
	}

	memcpy(fifo->f2[fifo->f2_count++], buf, 2);
	memcpy(fifo->f2[fifo->f2_count++], buf + 2, 2);
	memcpy(fifo->f2[fifo->f2_count++], buf + 4, 2);
	memcpy(fifo->f2[fifo->f2_count++], buf + 6, 2);
	if (fifo->f2_count < initial_f2count)
		fifo->f2_full = 1;
}

static int sw842_decompress(const unsigned char *src, int srclen,
			unsigned char *dst, int *destlen,
			const void *wrkmem)
{
	uint8_t tmpl;
	const char *inbuf;
	int inbit = 0;
	unsigned char *outbuf, *outbuf_end, *origbuf, *prevbuf;
	const char *inbuf_end;
	sw842_template_op op;
	int opindex;
	int i, repeat_count;
	struct sw842_fifo *fifo;
	int ret = 0;

	fifo = &((struct nx842_workmem *)(wrkmem))->swfifo;
	memset(fifo, 0, sizeof(*fifo));

	origbuf = NULL;
	inbuf = src;
	inbuf_end = src + srclen;
	outbuf = dst;
	outbuf_end = dst + *destlen;

	while ((tmpl = sw842_get_template(&inbuf, &inbit)) != SW842_TMPL_EOF) {
		if (inbuf >= inbuf_end) {
			ret = -EINVAL;
			goto out;
		}

		opindex = 0;
		prevbuf = origbuf;
		origbuf = outbuf;
		switch (tmpl) {
		case SW842_TMPL_REPEAT:
			if (prevbuf == NULL) {
				ret = -EINVAL;
				goto out;
			}

			repeat_count = sw842_get_repeat_count(&inbuf,
								&inbit) + 1;

			/* Did the repeat count advance past the end of input */
			if (inbuf > inbuf_end) {
				ret = -EINVAL;
				goto out;
			}

			for (i = 0; i < repeat_count; i++) {
				/* Would this overflow the output buffer */
				if ((outbuf + 8) > outbuf_end) {
					ret = -ENOSPC;
					goto out;
				}

				memcpy(outbuf, prevbuf, 8);
				sw842_copy_to_fifo(outbuf, fifo);
				outbuf += 8;
			}
			break;

		case SW842_TMPL_ZEROS:
			/* Would this overflow the output buffer */
			if ((outbuf + 8) > outbuf_end) {
				ret = -ENOSPC;
				goto out;
			}

			memset(outbuf, 0, 8);
			sw842_copy_to_fifo(outbuf, fifo);
			outbuf += 8;
			break;

		default:
			if (tmpl > 25) {
				ret = -EINVAL;
				goto out;
			}

			/* Does this go past the end of the input buffer */
			if ((inbuf + 2) > inbuf_end) {
				ret = -EINVAL;
				goto out;
			}

			/* Would this overflow the output buffer */
			if ((outbuf + 8) > outbuf_end) {
				ret = -ENOSPC;
				goto out;
			}

			while (opindex < 4 &&
				(op = sw842_tmpl_ops[tmpl][opindex++])
					!= NULL) {
				ret = (*op)(&inbuf, &inbit, &outbuf, fifo);
				if (ret) {
					ret = -EINVAL;
					goto out;
				}
				sw842_copy_to_fifo(origbuf, fifo);
			}
		}
	}

out:
	if (!ret)
		*destlen = (unsigned int)(outbuf - dst);
	else
		*destlen = 0;

	return ret;
}
