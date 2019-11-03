// SPDX-License-Identifier: GPL-2.0-only
/*
 * VPDMA helper library
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include "vpdma.h"
#include "vpdma_priv.h"

#define VPDMA_FIRMWARE	"vpdma-1b8.bin"

const struct vpdma_data_format vpdma_yuv_fmts[] = {
	[VPDMA_DATA_FMT_Y444] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_Y444,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_Y422] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_Y422,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_Y420] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_Y420,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_C444] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_C444,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_C422] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_C422,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_C420] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_C420,
		.depth		= 4,
	},
	[VPDMA_DATA_FMT_CB420] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_CB420,
		.depth		= 4,
	},
	[VPDMA_DATA_FMT_YCR422] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_YCR422,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_YC444] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_YC444,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_CRY422] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_CRY422,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_CBY422] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_CBY422,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_YCB422] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_YCB422,
		.depth		= 16,
	},
};
EXPORT_SYMBOL(vpdma_yuv_fmts);

const struct vpdma_data_format vpdma_rgb_fmts[] = {
	[VPDMA_DATA_FMT_RGB565] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_RGB16_565,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ARGB16_1555] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ARGB_1555,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ARGB16] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ARGB_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_RGBA16_5551] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_RGBA_5551,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_RGBA16] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_RGBA_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ARGB24] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ARGB24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_RGB24] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_RGB24_888,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_ARGB32] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ARGB32_8888,
		.depth		= 32,
	},
	[VPDMA_DATA_FMT_RGBA24] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_RGBA24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_RGBA32] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_RGBA32_8888,
		.depth		= 32,
	},
	[VPDMA_DATA_FMT_BGR565] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_BGR16_565,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ABGR16_1555] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ABGR_1555,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ABGR16] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ABGR_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_BGRA16_5551] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_BGRA_5551,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_BGRA16] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_BGRA_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ABGR24] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ABGR24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_BGR24] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_BGR24_888,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_ABGR32] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_ABGR32_8888,
		.depth		= 32,
	},
	[VPDMA_DATA_FMT_BGRA24] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_BGRA24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_BGRA32] = {
		.type		= VPDMA_DATA_FMT_TYPE_RGB,
		.data_type	= DATA_TYPE_BGRA32_8888,
		.depth		= 32,
	},
};
EXPORT_SYMBOL(vpdma_rgb_fmts);

/*
 * To handle RAW format we are re-using the CBY422
 * vpdma data type so that we use the vpdma to re-order
 * the incoming bytes, as the parser assumes that the
 * first byte presented on the bus is the MSB of a 2
 * bytes value.
 * RAW8 handles from 1 to 8 bits
 * RAW16 handles from 9 to 16 bits
 */
const struct vpdma_data_format vpdma_raw_fmts[] = {
	[VPDMA_DATA_FMT_RAW8] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_CBY422,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_RAW16] = {
		.type		= VPDMA_DATA_FMT_TYPE_YUV,
		.data_type	= DATA_TYPE_CBY422,
		.depth		= 16,
	},
};
EXPORT_SYMBOL(vpdma_raw_fmts);

const struct vpdma_data_format vpdma_misc_fmts[] = {
	[VPDMA_DATA_FMT_MV] = {
		.type		= VPDMA_DATA_FMT_TYPE_MISC,
		.data_type	= DATA_TYPE_MV,
		.depth		= 4,
	},
};
EXPORT_SYMBOL(vpdma_misc_fmts);

struct vpdma_channel_info {
	int num;		/* VPDMA channel number */
	int cstat_offset;	/* client CSTAT register offset */
};

static const struct vpdma_channel_info chan_info[] = {
	[VPE_CHAN_LUMA1_IN] = {
		.num		= VPE_CHAN_NUM_LUMA1_IN,
		.cstat_offset	= VPDMA_DEI_LUMA1_CSTAT,
	},
	[VPE_CHAN_CHROMA1_IN] = {
		.num		= VPE_CHAN_NUM_CHROMA1_IN,
		.cstat_offset	= VPDMA_DEI_CHROMA1_CSTAT,
	},
	[VPE_CHAN_LUMA2_IN] = {
		.num		= VPE_CHAN_NUM_LUMA2_IN,
		.cstat_offset	= VPDMA_DEI_LUMA2_CSTAT,
	},
	[VPE_CHAN_CHROMA2_IN] = {
		.num		= VPE_CHAN_NUM_CHROMA2_IN,
		.cstat_offset	= VPDMA_DEI_CHROMA2_CSTAT,
	},
	[VPE_CHAN_LUMA3_IN] = {
		.num		= VPE_CHAN_NUM_LUMA3_IN,
		.cstat_offset	= VPDMA_DEI_LUMA3_CSTAT,
	},
	[VPE_CHAN_CHROMA3_IN] = {
		.num		= VPE_CHAN_NUM_CHROMA3_IN,
		.cstat_offset	= VPDMA_DEI_CHROMA3_CSTAT,
	},
	[VPE_CHAN_MV_IN] = {
		.num		= VPE_CHAN_NUM_MV_IN,
		.cstat_offset	= VPDMA_DEI_MV_IN_CSTAT,
	},
	[VPE_CHAN_MV_OUT] = {
		.num		= VPE_CHAN_NUM_MV_OUT,
		.cstat_offset	= VPDMA_DEI_MV_OUT_CSTAT,
	},
	[VPE_CHAN_LUMA_OUT] = {
		.num		= VPE_CHAN_NUM_LUMA_OUT,
		.cstat_offset	= VPDMA_VIP_UP_Y_CSTAT,
	},
	[VPE_CHAN_CHROMA_OUT] = {
		.num		= VPE_CHAN_NUM_CHROMA_OUT,
		.cstat_offset	= VPDMA_VIP_UP_UV_CSTAT,
	},
	[VPE_CHAN_RGB_OUT] = {
		.num		= VPE_CHAN_NUM_RGB_OUT,
		.cstat_offset	= VPDMA_VIP_UP_Y_CSTAT,
	},
};

static u32 read_reg(struct vpdma_data *vpdma, int offset)
{
	return ioread32(vpdma->base + offset);
}

static void write_reg(struct vpdma_data *vpdma, int offset, u32 value)
{
	iowrite32(value, vpdma->base + offset);
}

static int read_field_reg(struct vpdma_data *vpdma, int offset,
		u32 mask, int shift)
{
	return (read_reg(vpdma, offset) & (mask << shift)) >> shift;
}

static void write_field_reg(struct vpdma_data *vpdma, int offset, u32 field,
		u32 mask, int shift)
{
	u32 val = read_reg(vpdma, offset);

	val &= ~(mask << shift);
	val |= (field & mask) << shift;

	write_reg(vpdma, offset, val);
}

void vpdma_dump_regs(struct vpdma_data *vpdma)
{
	struct device *dev = &vpdma->pdev->dev;

#define DUMPREG(r) dev_dbg(dev, "%-35s %08x\n", #r, read_reg(vpdma, VPDMA_##r))

	dev_dbg(dev, "VPDMA Registers:\n");

	DUMPREG(PID);
	DUMPREG(LIST_ADDR);
	DUMPREG(LIST_ATTR);
	DUMPREG(LIST_STAT_SYNC);
	DUMPREG(BG_RGB);
	DUMPREG(BG_YUV);
	DUMPREG(SETUP);
	DUMPREG(MAX_SIZE1);
	DUMPREG(MAX_SIZE2);
	DUMPREG(MAX_SIZE3);

	/*
	 * dumping registers of only group0 and group3, because VPE channels
	 * lie within group0 and group3 registers
	 */
	DUMPREG(INT_CHAN_STAT(0));
	DUMPREG(INT_CHAN_MASK(0));
	DUMPREG(INT_CHAN_STAT(3));
	DUMPREG(INT_CHAN_MASK(3));
	DUMPREG(INT_CLIENT0_STAT);
	DUMPREG(INT_CLIENT0_MASK);
	DUMPREG(INT_CLIENT1_STAT);
	DUMPREG(INT_CLIENT1_MASK);
	DUMPREG(INT_LIST0_STAT);
	DUMPREG(INT_LIST0_MASK);

	/*
	 * these are registers specific to VPE clients, we can make this
	 * function dump client registers specific to VPE or VIP based on
	 * who is using it
	 */
	DUMPREG(DEI_CHROMA1_CSTAT);
	DUMPREG(DEI_LUMA1_CSTAT);
	DUMPREG(DEI_CHROMA2_CSTAT);
	DUMPREG(DEI_LUMA2_CSTAT);
	DUMPREG(DEI_CHROMA3_CSTAT);
	DUMPREG(DEI_LUMA3_CSTAT);
	DUMPREG(DEI_MV_IN_CSTAT);
	DUMPREG(DEI_MV_OUT_CSTAT);
	DUMPREG(VIP_UP_Y_CSTAT);
	DUMPREG(VIP_UP_UV_CSTAT);
	DUMPREG(VPI_CTL_CSTAT);
}
EXPORT_SYMBOL(vpdma_dump_regs);

/*
 * Allocate a DMA buffer
 */
int vpdma_alloc_desc_buf(struct vpdma_buf *buf, size_t size)
{
	buf->size = size;
	buf->mapped = false;
	buf->addr = kzalloc(size, GFP_KERNEL);
	if (!buf->addr)
		return -ENOMEM;

	WARN_ON(((unsigned long)buf->addr & VPDMA_DESC_ALIGN) != 0);

	return 0;
}
EXPORT_SYMBOL(vpdma_alloc_desc_buf);

void vpdma_free_desc_buf(struct vpdma_buf *buf)
{
	WARN_ON(buf->mapped);
	kfree(buf->addr);
	buf->addr = NULL;
	buf->size = 0;
}
EXPORT_SYMBOL(vpdma_free_desc_buf);

/*
 * map descriptor/payload DMA buffer, enabling DMA access
 */
int vpdma_map_desc_buf(struct vpdma_data *vpdma, struct vpdma_buf *buf)
{
	struct device *dev = &vpdma->pdev->dev;

	WARN_ON(buf->mapped);
	buf->dma_addr = dma_map_single(dev, buf->addr, buf->size,
				DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, buf->dma_addr)) {
		dev_err(dev, "failed to map buffer\n");
		return -EINVAL;
	}

	buf->mapped = true;

	return 0;
}
EXPORT_SYMBOL(vpdma_map_desc_buf);

/*
 * unmap descriptor/payload DMA buffer, disabling DMA access and
 * allowing the main processor to access the data
 */
void vpdma_unmap_desc_buf(struct vpdma_data *vpdma, struct vpdma_buf *buf)
{
	struct device *dev = &vpdma->pdev->dev;

	if (buf->mapped)
		dma_unmap_single(dev, buf->dma_addr, buf->size,
				DMA_BIDIRECTIONAL);

	buf->mapped = false;
}
EXPORT_SYMBOL(vpdma_unmap_desc_buf);

/*
 * Cleanup all pending descriptors of a list
 * First, stop the current list being processed.
 * If the VPDMA was busy, this step makes vpdma to accept post lists.
 * To cleanup the internal FSM, post abort list descriptor for all the
 * channels from @channels array of size @size.
 */
int vpdma_list_cleanup(struct vpdma_data *vpdma, int list_num,
		int *channels, int size)
{
	struct vpdma_desc_list abort_list;
	int i, ret, timeout = 500;

	write_reg(vpdma, VPDMA_LIST_ATTR,
			(list_num << VPDMA_LIST_NUM_SHFT) |
			(1 << VPDMA_LIST_STOP_SHFT));

	if (size <= 0 || !channels)
		return 0;

	ret = vpdma_create_desc_list(&abort_list,
		size * sizeof(struct vpdma_dtd), VPDMA_LIST_TYPE_NORMAL);
	if (ret)
		return ret;

	for (i = 0; i < size; i++)
		vpdma_add_abort_channel_ctd(&abort_list, channels[i]);

	ret = vpdma_map_desc_buf(vpdma, &abort_list.buf);
	if (ret)
		goto free_desc;
	ret = vpdma_submit_descs(vpdma, &abort_list, list_num);
	if (ret)
		goto unmap_desc;

	while (vpdma_list_busy(vpdma, list_num) && --timeout)
		;

	if (timeout == 0) {
		dev_err(&vpdma->pdev->dev, "Timed out cleaning up VPDMA list\n");
		ret = -EBUSY;
	}

unmap_desc:
	vpdma_unmap_desc_buf(vpdma, &abort_list.buf);
free_desc:
	vpdma_free_desc_buf(&abort_list.buf);

	return ret;
}
EXPORT_SYMBOL(vpdma_list_cleanup);

/*
 * create a descriptor list, the user of this list will append configuration,
 * control and data descriptors to this list, this list will be submitted to
 * VPDMA. VPDMA's list parser will go through each descriptor and perform the
 * required DMA operations
 */
int vpdma_create_desc_list(struct vpdma_desc_list *list, size_t size, int type)
{
	int r;

	r = vpdma_alloc_desc_buf(&list->buf, size);
	if (r)
		return r;

	list->next = list->buf.addr;

	list->type = type;

	return 0;
}
EXPORT_SYMBOL(vpdma_create_desc_list);

/*
 * once a descriptor list is parsed by VPDMA, we reset the list by emptying it,
 * to allow new descriptors to be added to the list.
 */
void vpdma_reset_desc_list(struct vpdma_desc_list *list)
{
	list->next = list->buf.addr;
}
EXPORT_SYMBOL(vpdma_reset_desc_list);

/*
 * free the buffer allocated for the VPDMA descriptor list, this should be
 * called when the user doesn't want to use VPDMA any more.
 */
void vpdma_free_desc_list(struct vpdma_desc_list *list)
{
	vpdma_free_desc_buf(&list->buf);

	list->next = NULL;
}
EXPORT_SYMBOL(vpdma_free_desc_list);

bool vpdma_list_busy(struct vpdma_data *vpdma, int list_num)
{
	return read_reg(vpdma, VPDMA_LIST_STAT_SYNC) & BIT(list_num + 16);
}
EXPORT_SYMBOL(vpdma_list_busy);

/*
 * submit a list of DMA descriptors to the VPE VPDMA, do not wait for completion
 */
int vpdma_submit_descs(struct vpdma_data *vpdma,
			struct vpdma_desc_list *list, int list_num)
{
	int list_size;
	unsigned long flags;

	if (vpdma_list_busy(vpdma, list_num))
		return -EBUSY;

	/* 16-byte granularity */
	list_size = (list->next - list->buf.addr) >> 4;

	spin_lock_irqsave(&vpdma->lock, flags);
	write_reg(vpdma, VPDMA_LIST_ADDR, (u32) list->buf.dma_addr);

	write_reg(vpdma, VPDMA_LIST_ATTR,
			(list_num << VPDMA_LIST_NUM_SHFT) |
			(list->type << VPDMA_LIST_TYPE_SHFT) |
			list_size);
	spin_unlock_irqrestore(&vpdma->lock, flags);

	return 0;
}
EXPORT_SYMBOL(vpdma_submit_descs);

static void dump_dtd(struct vpdma_dtd *dtd);

void vpdma_update_dma_addr(struct vpdma_data *vpdma,
	struct vpdma_desc_list *list, dma_addr_t dma_addr,
	void *write_dtd, int drop, int idx)
{
	struct vpdma_dtd *dtd = list->buf.addr;
	dma_addr_t write_desc_addr;
	int offset;

	dtd += idx;
	vpdma_unmap_desc_buf(vpdma, &list->buf);

	dtd->start_addr = dma_addr;

	/* Calculate write address from the offset of write_dtd from start
	 * of the list->buf
	 */
	offset = (void *)write_dtd - list->buf.addr;
	write_desc_addr = list->buf.dma_addr + offset;

	if (drop)
		dtd->desc_write_addr = dtd_desc_write_addr(write_desc_addr,
							   1, 1, 0);
	else
		dtd->desc_write_addr = dtd_desc_write_addr(write_desc_addr,
							   1, 0, 0);

	vpdma_map_desc_buf(vpdma, &list->buf);

	dump_dtd(dtd);
}
EXPORT_SYMBOL(vpdma_update_dma_addr);

void vpdma_set_max_size(struct vpdma_data *vpdma, int reg_addr,
			u32 width, u32 height)
{
	if (reg_addr != VPDMA_MAX_SIZE1 && reg_addr != VPDMA_MAX_SIZE2 &&
	    reg_addr != VPDMA_MAX_SIZE3)
		reg_addr = VPDMA_MAX_SIZE1;

	write_field_reg(vpdma, reg_addr, width - 1,
			VPDMA_MAX_SIZE_WIDTH_MASK, VPDMA_MAX_SIZE_WIDTH_SHFT);

	write_field_reg(vpdma, reg_addr, height - 1,
			VPDMA_MAX_SIZE_HEIGHT_MASK, VPDMA_MAX_SIZE_HEIGHT_SHFT);

}
EXPORT_SYMBOL(vpdma_set_max_size);

static void dump_cfd(struct vpdma_cfd *cfd)
{
	int class;

	class = cfd_get_class(cfd);

	pr_debug("config descriptor of payload class: %s\n",
		class == CFD_CLS_BLOCK ? "simple block" :
		"address data block");

	if (class == CFD_CLS_BLOCK)
		pr_debug("word0: dst_addr_offset = 0x%08x\n",
			cfd->dest_addr_offset);

	if (class == CFD_CLS_BLOCK)
		pr_debug("word1: num_data_wrds = %d\n", cfd->block_len);

	pr_debug("word2: payload_addr = 0x%08x\n", cfd->payload_addr);

	pr_debug("word3: pkt_type = %d, direct = %d, class = %d, dest = %d, payload_len = %d\n",
		 cfd_get_pkt_type(cfd),
		 cfd_get_direct(cfd), class, cfd_get_dest(cfd),
		 cfd_get_payload_len(cfd));
}

/*
 * append a configuration descriptor to the given descriptor list, where the
 * payload is in the form of a simple data block specified in the descriptor
 * header, this is used to upload scaler coefficients to the scaler module
 */
void vpdma_add_cfd_block(struct vpdma_desc_list *list, int client,
		struct vpdma_buf *blk, u32 dest_offset)
{
	struct vpdma_cfd *cfd;
	int len = blk->size;

	WARN_ON(blk->dma_addr & VPDMA_DESC_ALIGN);

	cfd = list->next;
	WARN_ON((void *)(cfd + 1) > (list->buf.addr + list->buf.size));

	cfd->dest_addr_offset = dest_offset;
	cfd->block_len = len;
	cfd->payload_addr = (u32) blk->dma_addr;
	cfd->ctl_payload_len = cfd_pkt_payload_len(CFD_INDIRECT, CFD_CLS_BLOCK,
				client, len >> 4);

	list->next = cfd + 1;

	dump_cfd(cfd);
}
EXPORT_SYMBOL(vpdma_add_cfd_block);

/*
 * append a configuration descriptor to the given descriptor list, where the
 * payload is in the address data block format, this is used to a configure a
 * discontiguous set of MMRs
 */
void vpdma_add_cfd_adb(struct vpdma_desc_list *list, int client,
		struct vpdma_buf *adb)
{
	struct vpdma_cfd *cfd;
	unsigned int len = adb->size;

	WARN_ON(len & VPDMA_ADB_SIZE_ALIGN);
	WARN_ON(adb->dma_addr & VPDMA_DESC_ALIGN);

	cfd = list->next;
	BUG_ON((void *)(cfd + 1) > (list->buf.addr + list->buf.size));

	cfd->w0 = 0;
	cfd->w1 = 0;
	cfd->payload_addr = (u32) adb->dma_addr;
	cfd->ctl_payload_len = cfd_pkt_payload_len(CFD_INDIRECT, CFD_CLS_ADB,
				client, len >> 4);

	list->next = cfd + 1;

	dump_cfd(cfd);
};
EXPORT_SYMBOL(vpdma_add_cfd_adb);

/*
 * control descriptor format change based on what type of control descriptor it
 * is, we only use 'sync on channel' control descriptors for now, so assume it's
 * that
 */
static void dump_ctd(struct vpdma_ctd *ctd)
{
	pr_debug("control descriptor\n");

	pr_debug("word3: pkt_type = %d, source = %d, ctl_type = %d\n",
		ctd_get_pkt_type(ctd), ctd_get_source(ctd), ctd_get_ctl(ctd));
}

/*
 * append a 'sync on channel' type control descriptor to the given descriptor
 * list, this descriptor stalls the VPDMA list till the time DMA is completed
 * on the specified channel
 */
void vpdma_add_sync_on_channel_ctd(struct vpdma_desc_list *list,
		enum vpdma_channel chan)
{
	struct vpdma_ctd *ctd;

	ctd = list->next;
	WARN_ON((void *)(ctd + 1) > (list->buf.addr + list->buf.size));

	ctd->w0 = 0;
	ctd->w1 = 0;
	ctd->w2 = 0;
	ctd->type_source_ctl = ctd_type_source_ctl(chan_info[chan].num,
				CTD_TYPE_SYNC_ON_CHANNEL);

	list->next = ctd + 1;

	dump_ctd(ctd);
}
EXPORT_SYMBOL(vpdma_add_sync_on_channel_ctd);

/*
 * append an 'abort_channel' type control descriptor to the given descriptor
 * list, this descriptor aborts any DMA transaction happening using the
 * specified channel
 */
void vpdma_add_abort_channel_ctd(struct vpdma_desc_list *list,
		int chan_num)
{
	struct vpdma_ctd *ctd;

	ctd = list->next;
	WARN_ON((void *)(ctd + 1) > (list->buf.addr + list->buf.size));

	ctd->w0 = 0;
	ctd->w1 = 0;
	ctd->w2 = 0;
	ctd->type_source_ctl = ctd_type_source_ctl(chan_num,
				CTD_TYPE_ABORT_CHANNEL);

	list->next = ctd + 1;

	dump_ctd(ctd);
}
EXPORT_SYMBOL(vpdma_add_abort_channel_ctd);

static void dump_dtd(struct vpdma_dtd *dtd)
{
	int dir, chan;

	dir = dtd_get_dir(dtd);
	chan = dtd_get_chan(dtd);

	pr_debug("%s data transfer descriptor for channel %d\n",
		dir == DTD_DIR_OUT ? "outbound" : "inbound", chan);

	pr_debug("word0: data_type = %d, notify = %d, field = %d, 1D = %d, even_ln_skp = %d, odd_ln_skp = %d, line_stride = %d\n",
		dtd_get_data_type(dtd), dtd_get_notify(dtd), dtd_get_field(dtd),
		dtd_get_1d(dtd), dtd_get_even_line_skip(dtd),
		dtd_get_odd_line_skip(dtd), dtd_get_line_stride(dtd));

	if (dir == DTD_DIR_IN)
		pr_debug("word1: line_length = %d, xfer_height = %d\n",
			dtd_get_line_length(dtd), dtd_get_xfer_height(dtd));

	pr_debug("word2: start_addr = %x\n", dtd->start_addr);

	pr_debug("word3: pkt_type = %d, mode = %d, dir = %d, chan = %d, pri = %d, next_chan = %d\n",
		 dtd_get_pkt_type(dtd),
		 dtd_get_mode(dtd), dir, chan, dtd_get_priority(dtd),
		 dtd_get_next_chan(dtd));

	if (dir == DTD_DIR_IN)
		pr_debug("word4: frame_width = %d, frame_height = %d\n",
			dtd_get_frame_width(dtd), dtd_get_frame_height(dtd));
	else
		pr_debug("word4: desc_write_addr = 0x%08x, write_desc = %d, drp_data = %d, use_desc_reg = %d\n",
			dtd_get_desc_write_addr(dtd), dtd_get_write_desc(dtd),
			dtd_get_drop_data(dtd), dtd_get_use_desc(dtd));

	if (dir == DTD_DIR_IN)
		pr_debug("word5: hor_start = %d, ver_start = %d\n",
			dtd_get_h_start(dtd), dtd_get_v_start(dtd));
	else
		pr_debug("word5: max_width %d, max_height %d\n",
			dtd_get_max_width(dtd), dtd_get_max_height(dtd));

	pr_debug("word6: client specific attr0 = 0x%08x\n", dtd->client_attr0);
	pr_debug("word7: client specific attr1 = 0x%08x\n", dtd->client_attr1);
}

/*
 * append an outbound data transfer descriptor to the given descriptor list,
 * this sets up a 'client to memory' VPDMA transfer for the given VPDMA channel
 *
 * @list: vpdma desc list to which we add this descriptor
 * @width: width of the image in pixels in memory
 * @c_rect: compose params of output image
 * @fmt: vpdma data format of the buffer
 * dma_addr: dma address as seen by VPDMA
 * max_width: enum for maximum width of data transfer
 * max_height: enum for maximum height of data transfer
 * chan: VPDMA channel
 * flags: VPDMA flags to configure some descriptor fields
 */
void vpdma_add_out_dtd(struct vpdma_desc_list *list, int width,
		int stride, const struct v4l2_rect *c_rect,
		const struct vpdma_data_format *fmt, dma_addr_t dma_addr,
		int max_w, int max_h, enum vpdma_channel chan, u32 flags)
{
	vpdma_rawchan_add_out_dtd(list, width, stride, c_rect, fmt, dma_addr,
				  max_w, max_h, chan_info[chan].num, flags);
}
EXPORT_SYMBOL(vpdma_add_out_dtd);

void vpdma_rawchan_add_out_dtd(struct vpdma_desc_list *list, int width,
		int stride, const struct v4l2_rect *c_rect,
		const struct vpdma_data_format *fmt, dma_addr_t dma_addr,
		int max_w, int max_h, int raw_vpdma_chan, u32 flags)
{
	int priority = 0;
	int field = 0;
	int notify = 1;
	int channel, next_chan;
	struct v4l2_rect rect = *c_rect;
	int depth = fmt->depth;
	struct vpdma_dtd *dtd;

	channel = next_chan = raw_vpdma_chan;

	if (fmt->type == VPDMA_DATA_FMT_TYPE_YUV &&
	    (fmt->data_type == DATA_TYPE_C420 ||
	     fmt->data_type == DATA_TYPE_CB420)) {
		rect.height >>= 1;
		rect.top >>= 1;
		depth = 8;
	}

	dma_addr += rect.top * stride + (rect.left * depth >> 3);

	dtd = list->next;
	WARN_ON((void *)(dtd + 1) > (list->buf.addr + list->buf.size));

	dtd->type_ctl_stride = dtd_type_ctl_stride(fmt->data_type,
					notify,
					field,
					!!(flags & VPDMA_DATA_FRAME_1D),
					!!(flags & VPDMA_DATA_EVEN_LINE_SKIP),
					!!(flags & VPDMA_DATA_ODD_LINE_SKIP),
					stride);
	dtd->w1 = 0;
	dtd->start_addr = (u32) dma_addr;
	dtd->pkt_ctl = dtd_pkt_ctl(!!(flags & VPDMA_DATA_MODE_TILED),
				DTD_DIR_OUT, channel, priority, next_chan);
	dtd->desc_write_addr = dtd_desc_write_addr(0, 0, 0, 0);
	dtd->max_width_height = dtd_max_width_height(max_w, max_h);
	dtd->client_attr0 = 0;
	dtd->client_attr1 = 0;

	list->next = dtd + 1;

	dump_dtd(dtd);
}
EXPORT_SYMBOL(vpdma_rawchan_add_out_dtd);

/*
 * append an inbound data transfer descriptor to the given descriptor list,
 * this sets up a 'memory to client' VPDMA transfer for the given VPDMA channel
 *
 * @list: vpdma desc list to which we add this descriptor
 * @width: width of the image in pixels in memory(not the cropped width)
 * @c_rect: crop params of input image
 * @fmt: vpdma data format of the buffer
 * dma_addr: dma address as seen by VPDMA
 * chan: VPDMA channel
 * field: top or bottom field info of the input image
 * flags: VPDMA flags to configure some descriptor fields
 * frame_width/height: the complete width/height of the image presented to the
 *			client (this makes sense when multiple channels are
 *			connected to the same client, forming a larger frame)
 * start_h, start_v: position where the given channel starts providing pixel
 *			data to the client (makes sense when multiple channels
 *			contribute to the client)
 */
void vpdma_add_in_dtd(struct vpdma_desc_list *list, int width,
		int stride, const struct v4l2_rect *c_rect,
		const struct vpdma_data_format *fmt, dma_addr_t dma_addr,
		enum vpdma_channel chan, int field, u32 flags, int frame_width,
		int frame_height, int start_h, int start_v)
{
	int priority = 0;
	int notify = 1;
	int depth = fmt->depth;
	int channel, next_chan;
	struct v4l2_rect rect = *c_rect;
	struct vpdma_dtd *dtd;

	channel = next_chan = chan_info[chan].num;

	if (fmt->type == VPDMA_DATA_FMT_TYPE_YUV &&
	    (fmt->data_type == DATA_TYPE_C420 ||
	     fmt->data_type == DATA_TYPE_CB420)) {
		rect.height >>= 1;
		rect.top >>= 1;
		depth = 8;
	}

	dma_addr += rect.top * stride + (rect.left * depth >> 3);

	dtd = list->next;
	WARN_ON((void *)(dtd + 1) > (list->buf.addr + list->buf.size));

	dtd->type_ctl_stride = dtd_type_ctl_stride(fmt->data_type,
					notify,
					field,
					!!(flags & VPDMA_DATA_FRAME_1D),
					!!(flags & VPDMA_DATA_EVEN_LINE_SKIP),
					!!(flags & VPDMA_DATA_ODD_LINE_SKIP),
					stride);

	dtd->xfer_length_height = dtd_xfer_length_height(rect.width,
					rect.height);
	dtd->start_addr = (u32) dma_addr;
	dtd->pkt_ctl = dtd_pkt_ctl(!!(flags & VPDMA_DATA_MODE_TILED),
				DTD_DIR_IN, channel, priority, next_chan);
	dtd->frame_width_height = dtd_frame_width_height(frame_width,
					frame_height);
	dtd->start_h_v = dtd_start_h_v(start_h, start_v);
	dtd->client_attr0 = 0;
	dtd->client_attr1 = 0;

	list->next = dtd + 1;

	dump_dtd(dtd);
}
EXPORT_SYMBOL(vpdma_add_in_dtd);

int vpdma_hwlist_alloc(struct vpdma_data *vpdma, void *priv)
{
	int i, list_num = -1;
	unsigned long flags;

	spin_lock_irqsave(&vpdma->lock, flags);
	for (i = 0; i < VPDMA_MAX_NUM_LIST &&
	    vpdma->hwlist_used[i] == true; i++)
		;

	if (i < VPDMA_MAX_NUM_LIST) {
		list_num = i;
		vpdma->hwlist_used[i] = true;
		vpdma->hwlist_priv[i] = priv;
	}
	spin_unlock_irqrestore(&vpdma->lock, flags);

	return list_num;
}
EXPORT_SYMBOL(vpdma_hwlist_alloc);

void *vpdma_hwlist_get_priv(struct vpdma_data *vpdma, int list_num)
{
	if (!vpdma || list_num >= VPDMA_MAX_NUM_LIST)
		return NULL;

	return vpdma->hwlist_priv[list_num];
}
EXPORT_SYMBOL(vpdma_hwlist_get_priv);

void *vpdma_hwlist_release(struct vpdma_data *vpdma, int list_num)
{
	void *priv;
	unsigned long flags;

	spin_lock_irqsave(&vpdma->lock, flags);
	vpdma->hwlist_used[list_num] = false;
	priv = vpdma->hwlist_priv;
	spin_unlock_irqrestore(&vpdma->lock, flags);

	return priv;
}
EXPORT_SYMBOL(vpdma_hwlist_release);

/* set or clear the mask for list complete interrupt */
void vpdma_enable_list_complete_irq(struct vpdma_data *vpdma, int irq_num,
		int list_num, bool enable)
{
	u32 reg_addr = VPDMA_INT_LIST0_MASK + VPDMA_INTX_OFFSET * irq_num;
	u32 val;

	val = read_reg(vpdma, reg_addr);
	if (enable)
		val |= (1 << (list_num * 2));
	else
		val &= ~(1 << (list_num * 2));
	write_reg(vpdma, reg_addr, val);
}
EXPORT_SYMBOL(vpdma_enable_list_complete_irq);

/* get the LIST_STAT register */
unsigned int vpdma_get_list_stat(struct vpdma_data *vpdma, int irq_num)
{
	u32 reg_addr = VPDMA_INT_LIST0_STAT + VPDMA_INTX_OFFSET * irq_num;

	return read_reg(vpdma, reg_addr);
}
EXPORT_SYMBOL(vpdma_get_list_stat);

/* get the LIST_MASK register */
unsigned int vpdma_get_list_mask(struct vpdma_data *vpdma, int irq_num)
{
	u32 reg_addr = VPDMA_INT_LIST0_MASK + VPDMA_INTX_OFFSET * irq_num;

	return read_reg(vpdma, reg_addr);
}
EXPORT_SYMBOL(vpdma_get_list_mask);

/* clear previously occurred list interrupts in the LIST_STAT register */
void vpdma_clear_list_stat(struct vpdma_data *vpdma, int irq_num,
			   int list_num)
{
	u32 reg_addr = VPDMA_INT_LIST0_STAT + VPDMA_INTX_OFFSET * irq_num;

	write_reg(vpdma, reg_addr, 3 << (list_num * 2));
}
EXPORT_SYMBOL(vpdma_clear_list_stat);

void vpdma_set_bg_color(struct vpdma_data *vpdma,
		struct vpdma_data_format *fmt, u32 color)
{
	if (fmt->type == VPDMA_DATA_FMT_TYPE_RGB)
		write_reg(vpdma, VPDMA_BG_RGB, color);
	else if (fmt->type == VPDMA_DATA_FMT_TYPE_YUV)
		write_reg(vpdma, VPDMA_BG_YUV, color);
}
EXPORT_SYMBOL(vpdma_set_bg_color);

/*
 * configures the output mode of the line buffer for the given client, the
 * line buffer content can either be mirrored(each line repeated twice) or
 * passed to the client as is
 */
void vpdma_set_line_mode(struct vpdma_data *vpdma, int line_mode,
		enum vpdma_channel chan)
{
	int client_cstat = chan_info[chan].cstat_offset;

	write_field_reg(vpdma, client_cstat, line_mode,
		VPDMA_CSTAT_LINE_MODE_MASK, VPDMA_CSTAT_LINE_MODE_SHIFT);
}
EXPORT_SYMBOL(vpdma_set_line_mode);

/*
 * configures the event which should trigger VPDMA transfer for the given
 * client
 */
void vpdma_set_frame_start_event(struct vpdma_data *vpdma,
		enum vpdma_frame_start_event fs_event,
		enum vpdma_channel chan)
{
	int client_cstat = chan_info[chan].cstat_offset;

	write_field_reg(vpdma, client_cstat, fs_event,
		VPDMA_CSTAT_FRAME_START_MASK, VPDMA_CSTAT_FRAME_START_SHIFT);
}
EXPORT_SYMBOL(vpdma_set_frame_start_event);

static void vpdma_firmware_cb(const struct firmware *f, void *context)
{
	struct vpdma_data *vpdma = context;
	struct vpdma_buf fw_dma_buf;
	int i, r;

	dev_dbg(&vpdma->pdev->dev, "firmware callback\n");

	if (!f || !f->data) {
		dev_err(&vpdma->pdev->dev, "couldn't get firmware\n");
		return;
	}

	/* already initialized */
	if (read_field_reg(vpdma, VPDMA_LIST_ATTR, VPDMA_LIST_RDY_MASK,
			VPDMA_LIST_RDY_SHFT)) {
		vpdma->cb(vpdma->pdev);
		return;
	}

	r = vpdma_alloc_desc_buf(&fw_dma_buf, f->size);
	if (r) {
		dev_err(&vpdma->pdev->dev,
			"failed to allocate dma buffer for firmware\n");
		goto rel_fw;
	}

	memcpy(fw_dma_buf.addr, f->data, f->size);

	vpdma_map_desc_buf(vpdma, &fw_dma_buf);

	write_reg(vpdma, VPDMA_LIST_ADDR, (u32) fw_dma_buf.dma_addr);

	for (i = 0; i < 100; i++) {		/* max 1 second */
		msleep_interruptible(10);

		if (read_field_reg(vpdma, VPDMA_LIST_ATTR, VPDMA_LIST_RDY_MASK,
				VPDMA_LIST_RDY_SHFT))
			break;
	}

	if (i == 100) {
		dev_err(&vpdma->pdev->dev, "firmware upload failed\n");
		goto free_buf;
	}

	vpdma->cb(vpdma->pdev);

free_buf:
	vpdma_unmap_desc_buf(vpdma, &fw_dma_buf);

	vpdma_free_desc_buf(&fw_dma_buf);
rel_fw:
	release_firmware(f);
}

static int vpdma_load_firmware(struct vpdma_data *vpdma)
{
	int r;
	struct device *dev = &vpdma->pdev->dev;

	r = request_firmware_nowait(THIS_MODULE, 1,
		(const char *) VPDMA_FIRMWARE, dev, GFP_KERNEL, vpdma,
		vpdma_firmware_cb);
	if (r) {
		dev_err(dev, "firmware not available %s\n", VPDMA_FIRMWARE);
		return r;
	} else {
		dev_info(dev, "loading firmware %s\n", VPDMA_FIRMWARE);
	}

	return 0;
}

int vpdma_create(struct platform_device *pdev, struct vpdma_data *vpdma,
		void (*cb)(struct platform_device *pdev))
{
	struct resource *res;
	int r;

	dev_dbg(&pdev->dev, "vpdma_create\n");

	vpdma->pdev = pdev;
	vpdma->cb = cb;
	spin_lock_init(&vpdma->lock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vpdma");
	if (res == NULL) {
		dev_err(&pdev->dev, "missing platform resources data\n");
		return -ENODEV;
	}

	vpdma->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!vpdma->base) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return -ENOMEM;
	}

	r = vpdma_load_firmware(vpdma);
	if (r) {
		pr_err("failed to load firmware %s\n", VPDMA_FIRMWARE);
		return r;
	}

	return 0;
}
EXPORT_SYMBOL(vpdma_create);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_FIRMWARE(VPDMA_FIRMWARE);
MODULE_LICENSE("GPL v2");
