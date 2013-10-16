/*
 * VPDMA helper library
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
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

#include "vpdma.h"
#include "vpdma_priv.h"

#define VPDMA_FIRMWARE	"vpdma-1b8.bin"

const struct vpdma_data_format vpdma_yuv_fmts[] = {
	[VPDMA_DATA_FMT_Y444] = {
		.data_type	= DATA_TYPE_Y444,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_Y422] = {
		.data_type	= DATA_TYPE_Y422,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_Y420] = {
		.data_type	= DATA_TYPE_Y420,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_C444] = {
		.data_type	= DATA_TYPE_C444,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_C422] = {
		.data_type	= DATA_TYPE_C422,
		.depth		= 8,
	},
	[VPDMA_DATA_FMT_C420] = {
		.data_type	= DATA_TYPE_C420,
		.depth		= 4,
	},
	[VPDMA_DATA_FMT_YC422] = {
		.data_type	= DATA_TYPE_YC422,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_YC444] = {
		.data_type	= DATA_TYPE_YC444,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_CY422] = {
		.data_type	= DATA_TYPE_CY422,
		.depth		= 16,
	},
};

const struct vpdma_data_format vpdma_rgb_fmts[] = {
	[VPDMA_DATA_FMT_RGB565] = {
		.data_type	= DATA_TYPE_RGB16_565,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ARGB16_1555] = {
		.data_type	= DATA_TYPE_ARGB_1555,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ARGB16] = {
		.data_type	= DATA_TYPE_ARGB_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_RGBA16_5551] = {
		.data_type	= DATA_TYPE_RGBA_5551,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_RGBA16] = {
		.data_type	= DATA_TYPE_RGBA_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ARGB24] = {
		.data_type	= DATA_TYPE_ARGB24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_RGB24] = {
		.data_type	= DATA_TYPE_RGB24_888,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_ARGB32] = {
		.data_type	= DATA_TYPE_ARGB32_8888,
		.depth		= 32,
	},
	[VPDMA_DATA_FMT_RGBA24] = {
		.data_type	= DATA_TYPE_RGBA24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_RGBA32] = {
		.data_type	= DATA_TYPE_RGBA32_8888,
		.depth		= 32,
	},
	[VPDMA_DATA_FMT_BGR565] = {
		.data_type	= DATA_TYPE_BGR16_565,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ABGR16_1555] = {
		.data_type	= DATA_TYPE_ABGR_1555,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ABGR16] = {
		.data_type	= DATA_TYPE_ABGR_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_BGRA16_5551] = {
		.data_type	= DATA_TYPE_BGRA_5551,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_BGRA16] = {
		.data_type	= DATA_TYPE_BGRA_4444,
		.depth		= 16,
	},
	[VPDMA_DATA_FMT_ABGR24] = {
		.data_type	= DATA_TYPE_ABGR24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_BGR24] = {
		.data_type	= DATA_TYPE_BGR24_888,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_ABGR32] = {
		.data_type	= DATA_TYPE_ABGR32_8888,
		.depth		= 32,
	},
	[VPDMA_DATA_FMT_BGRA24] = {
		.data_type	= DATA_TYPE_BGRA24_6666,
		.depth		= 24,
	},
	[VPDMA_DATA_FMT_BGRA32] = {
		.data_type	= DATA_TYPE_BGRA32_8888,
		.depth		= 32,
	},
};

const struct vpdma_data_format vpdma_misc_fmts[] = {
	[VPDMA_DATA_FMT_MV] = {
		.data_type	= DATA_TYPE_MV,
		.depth		= 4,
	},
};

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

	WARN_ON((u32) buf->addr & VPDMA_DESC_ALIGN);

	return 0;
}

void vpdma_free_desc_buf(struct vpdma_buf *buf)
{
	WARN_ON(buf->mapped);
	kfree(buf->addr);
	buf->addr = NULL;
	buf->size = 0;
}

/*
 * map descriptor/payload DMA buffer, enabling DMA access
 */
int vpdma_map_desc_buf(struct vpdma_data *vpdma, struct vpdma_buf *buf)
{
	struct device *dev = &vpdma->pdev->dev;

	WARN_ON(buf->mapped);
	buf->dma_addr = dma_map_single(dev, buf->addr, buf->size,
				DMA_TO_DEVICE);
	if (dma_mapping_error(dev, buf->dma_addr)) {
		dev_err(dev, "failed to map buffer\n");
		return -EINVAL;
	}

	buf->mapped = true;

	return 0;
}

/*
 * unmap descriptor/payload DMA buffer, disabling DMA access and
 * allowing the main processor to acces the data
 */
void vpdma_unmap_desc_buf(struct vpdma_data *vpdma, struct vpdma_buf *buf)
{
	struct device *dev = &vpdma->pdev->dev;

	if (buf->mapped)
		dma_unmap_single(dev, buf->dma_addr, buf->size, DMA_TO_DEVICE);

	buf->mapped = false;
}

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

/*
 * once a descriptor list is parsed by VPDMA, we reset the list by emptying it,
 * to allow new descriptors to be added to the list.
 */
void vpdma_reset_desc_list(struct vpdma_desc_list *list)
{
	list->next = list->buf.addr;
}

/*
 * free the buffer allocated fot the VPDMA descriptor list, this should be
 * called when the user doesn't want to use VPDMA any more.
 */
void vpdma_free_desc_list(struct vpdma_desc_list *list)
{
	vpdma_free_desc_buf(&list->buf);

	list->next = NULL;
}

static bool vpdma_list_busy(struct vpdma_data *vpdma, int list_num)
{
	return read_reg(vpdma, VPDMA_LIST_STAT_SYNC) & BIT(list_num + 16);
}

/*
 * submit a list of DMA descriptors to the VPE VPDMA, do not wait for completion
 */
int vpdma_submit_descs(struct vpdma_data *vpdma, struct vpdma_desc_list *list)
{
	/* we always use the first list */
	int list_num = 0;
	int list_size;

	if (vpdma_list_busy(vpdma, list_num))
		return -EBUSY;

	/* 16-byte granularity */
	list_size = (list->next - list->buf.addr) >> 4;

	write_reg(vpdma, VPDMA_LIST_ADDR, (u32) list->buf.dma_addr);

	write_reg(vpdma, VPDMA_LIST_ATTR,
			(list_num << VPDMA_LIST_NUM_SHFT) |
			(list->type << VPDMA_LIST_TYPE_SHFT) |
			list_size);

	return 0;
}

/* set or clear the mask for list complete interrupt */
void vpdma_enable_list_complete_irq(struct vpdma_data *vpdma, int list_num,
		bool enable)
{
	u32 val;

	val = read_reg(vpdma, VPDMA_INT_LIST0_MASK);
	if (enable)
		val |= (1 << (list_num * 2));
	else
		val &= ~(1 << (list_num * 2));
	write_reg(vpdma, VPDMA_INT_LIST0_MASK, val);
}

/* clear previosuly occured list intterupts in the LIST_STAT register */
void vpdma_clear_list_stat(struct vpdma_data *vpdma)
{
	write_reg(vpdma, VPDMA_INT_LIST0_STAT,
		read_reg(vpdma, VPDMA_INT_LIST0_STAT));
}

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
		vpdma->ready = true;
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

	vpdma->ready = true;

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

struct vpdma_data *vpdma_create(struct platform_device *pdev)
{
	struct resource *res;
	struct vpdma_data *vpdma;
	int r;

	dev_dbg(&pdev->dev, "vpdma_create\n");

	vpdma = devm_kzalloc(&pdev->dev, sizeof(*vpdma), GFP_KERNEL);
	if (!vpdma) {
		dev_err(&pdev->dev, "couldn't alloc vpdma_dev\n");
		return ERR_PTR(-ENOMEM);
	}

	vpdma->pdev = pdev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vpdma");
	if (res == NULL) {
		dev_err(&pdev->dev, "missing platform resources data\n");
		return ERR_PTR(-ENODEV);
	}

	vpdma->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!vpdma->base) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return ERR_PTR(-ENOMEM);
	}

	r = vpdma_load_firmware(vpdma);
	if (r) {
		pr_err("failed to load firmware %s\n", VPDMA_FIRMWARE);
		return ERR_PTR(r);
	}

	return vpdma;
}
MODULE_FIRMWARE(VPDMA_FIRMWARE);
