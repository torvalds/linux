/*
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

#ifndef _TI_VPDMA_PRIV_H_
#define _TI_VPDMA_PRIV_H_

/*
 * VPDMA Register offsets
 */

/* Top level */
#define VPDMA_PID		0x00
#define VPDMA_LIST_ADDR		0x04
#define VPDMA_LIST_ATTR		0x08
#define VPDMA_LIST_STAT_SYNC	0x0c
#define VPDMA_BG_RGB		0x18
#define VPDMA_BG_YUV		0x1c
#define VPDMA_SETUP		0x30
#define VPDMA_MAX_SIZE1		0x34
#define VPDMA_MAX_SIZE2		0x38
#define VPDMA_MAX_SIZE3		0x3c

/* Interrupts */
#define VPDMA_INT_CHAN_STAT(grp)	(0x40 + grp * 8)
#define VPDMA_INT_CHAN_MASK(grp)	(VPDMA_INT_CHAN_STAT(grp) + 4)
#define VPDMA_INT_CLIENT0_STAT		0x78
#define VPDMA_INT_CLIENT0_MASK		0x7c
#define VPDMA_INT_CLIENT1_STAT		0x80
#define VPDMA_INT_CLIENT1_MASK		0x84
#define VPDMA_INT_LIST0_STAT		0x88
#define VPDMA_INT_LIST0_MASK		0x8c

#define VPDMA_PERFMON(i)		(0x200 + i * 4)

/* VPE specific client registers */
#define VPDMA_DEI_CHROMA1_CSTAT		0x0300
#define VPDMA_DEI_LUMA1_CSTAT		0x0304
#define VPDMA_DEI_LUMA2_CSTAT		0x0308
#define VPDMA_DEI_CHROMA2_CSTAT		0x030c
#define VPDMA_DEI_LUMA3_CSTAT		0x0310
#define VPDMA_DEI_CHROMA3_CSTAT		0x0314
#define VPDMA_DEI_MV_IN_CSTAT		0x0330
#define VPDMA_DEI_MV_OUT_CSTAT		0x033c
#define VPDMA_VIP_UP_Y_CSTAT		0x0390
#define VPDMA_VIP_UP_UV_CSTAT		0x0394
#define VPDMA_VPI_CTL_CSTAT		0x03d0

/* Reg field info for VPDMA_CLIENT_CSTAT registers */
#define VPDMA_CSTAT_LINE_MODE_MASK	0x03
#define VPDMA_CSTAT_LINE_MODE_SHIFT	8
#define VPDMA_CSTAT_FRAME_START_MASK	0xf
#define VPDMA_CSTAT_FRAME_START_SHIFT	10

#define VPDMA_LIST_NUM_MASK		0x07
#define VPDMA_LIST_NUM_SHFT		24
#define VPDMA_LIST_STOP_SHFT		20
#define VPDMA_LIST_RDY_MASK		0x01
#define VPDMA_LIST_RDY_SHFT		19
#define VPDMA_LIST_TYPE_MASK		0x03
#define VPDMA_LIST_TYPE_SHFT		16
#define VPDMA_LIST_SIZE_MASK		0xffff

/* VPDMA data type values for data formats */
#define DATA_TYPE_Y444				0x0
#define DATA_TYPE_Y422				0x1
#define DATA_TYPE_Y420				0x2
#define DATA_TYPE_C444				0x4
#define DATA_TYPE_C422				0x5
#define DATA_TYPE_C420				0x6
#define DATA_TYPE_YC422				0x7
#define DATA_TYPE_YC444				0x8
#define DATA_TYPE_CY422				0x27

#define DATA_TYPE_RGB16_565			0x0
#define DATA_TYPE_ARGB_1555			0x1
#define DATA_TYPE_ARGB_4444			0x2
#define DATA_TYPE_RGBA_5551			0x3
#define DATA_TYPE_RGBA_4444			0x4
#define DATA_TYPE_ARGB24_6666			0x5
#define DATA_TYPE_RGB24_888			0x6
#define DATA_TYPE_ARGB32_8888			0x7
#define DATA_TYPE_RGBA24_6666			0x8
#define DATA_TYPE_RGBA32_8888			0x9
#define DATA_TYPE_BGR16_565			0x10
#define DATA_TYPE_ABGR_1555			0x11
#define DATA_TYPE_ABGR_4444			0x12
#define DATA_TYPE_BGRA_5551			0x13
#define DATA_TYPE_BGRA_4444			0x14
#define DATA_TYPE_ABGR24_6666			0x15
#define DATA_TYPE_BGR24_888			0x16
#define DATA_TYPE_ABGR32_8888			0x17
#define DATA_TYPE_BGRA24_6666			0x18
#define DATA_TYPE_BGRA32_8888			0x19

#define DATA_TYPE_MV				0x3

/* VPDMA channel numbers(only VPE channels for now) */
#define	VPE_CHAN_NUM_LUMA1_IN		0
#define	VPE_CHAN_NUM_CHROMA1_IN		1
#define	VPE_CHAN_NUM_LUMA2_IN		2
#define	VPE_CHAN_NUM_CHROMA2_IN		3
#define	VPE_CHAN_NUM_LUMA3_IN		4
#define	VPE_CHAN_NUM_CHROMA3_IN		5
#define	VPE_CHAN_NUM_MV_IN		12
#define	VPE_CHAN_NUM_MV_OUT		15
#define	VPE_CHAN_NUM_LUMA_OUT		102
#define	VPE_CHAN_NUM_CHROMA_OUT		103
#define	VPE_CHAN_NUM_RGB_OUT		106

/*
 * a VPDMA address data block payload for a configuration descriptor needs to
 * have each sub block length as a multiple of 16 bytes. Therefore, the overall
 * size of the payload also needs to be a multiple of 16 bytes. The sub block
 * lengths should be ensured to be aligned by the VPDMA user.
 */
#define VPDMA_ADB_SIZE_ALIGN		0x0f

/*
 * data transfer descriptor
 */
struct vpdma_dtd {
	u32			type_ctl_stride;
	union {
		u32		xfer_length_height;
		u32		w1;
	};
	dma_addr_t		start_addr;
	u32			pkt_ctl;
	union {
		u32		frame_width_height;	/* inbound */
		dma_addr_t	desc_write_addr;	/* outbound */
	};
	union {
		u32		start_h_v;		/* inbound */
		u32		max_width_height;	/* outbound */
	};
	u32			client_attr0;
	u32			client_attr1;
};

/* Data Transfer Descriptor specifics */
#define DTD_NO_NOTIFY		0
#define DTD_NOTIFY		1

#define DTD_PKT_TYPE		0xa
#define DTD_DIR_IN		0
#define DTD_DIR_OUT		1

/* type_ctl_stride */
#define DTD_DATA_TYPE_MASK	0x3f
#define DTD_DATA_TYPE_SHFT	26
#define DTD_NOTIFY_MASK		0x01
#define DTD_NOTIFY_SHFT		25
#define DTD_FIELD_MASK		0x01
#define DTD_FIELD_SHFT		24
#define DTD_1D_MASK		0x01
#define DTD_1D_SHFT		23
#define DTD_EVEN_LINE_SKIP_MASK	0x01
#define DTD_EVEN_LINE_SKIP_SHFT	20
#define DTD_ODD_LINE_SKIP_MASK	0x01
#define DTD_ODD_LINE_SKIP_SHFT	16
#define DTD_LINE_STRIDE_MASK	0xffff
#define DTD_LINE_STRIDE_SHFT	0

/* xfer_length_height */
#define DTD_LINE_LENGTH_MASK	0xffff
#define DTD_LINE_LENGTH_SHFT	16
#define DTD_XFER_HEIGHT_MASK	0xffff
#define DTD_XFER_HEIGHT_SHFT	0

/* pkt_ctl */
#define DTD_PKT_TYPE_MASK	0x1f
#define DTD_PKT_TYPE_SHFT	27
#define DTD_MODE_MASK		0x01
#define DTD_MODE_SHFT		26
#define DTD_DIR_MASK		0x01
#define DTD_DIR_SHFT		25
#define DTD_CHAN_MASK		0x01ff
#define DTD_CHAN_SHFT		16
#define DTD_PRI_MASK		0x0f
#define DTD_PRI_SHFT		9
#define DTD_NEXT_CHAN_MASK	0x01ff
#define DTD_NEXT_CHAN_SHFT	0

/* frame_width_height */
#define DTD_FRAME_WIDTH_MASK	0xffff
#define DTD_FRAME_WIDTH_SHFT	16
#define DTD_FRAME_HEIGHT_MASK	0xffff
#define DTD_FRAME_HEIGHT_SHFT	0

/* start_h_v */
#define DTD_H_START_MASK	0xffff
#define DTD_H_START_SHFT	16
#define DTD_V_START_MASK	0xffff
#define DTD_V_START_SHFT	0

#define DTD_DESC_START_SHIFT	5
#define DTD_WRITE_DESC_MASK	0x01
#define DTD_WRITE_DESC_SHIFT	2
#define DTD_DROP_DATA_MASK	0x01
#define DTD_DROP_DATA_SHIFT	1
#define DTD_USE_DESC_MASK	0x01
#define DTD_USE_DESC_SHIFT	0

/* max_width_height */
#define DTD_MAX_WIDTH_MASK	0x07
#define DTD_MAX_WIDTH_SHFT	4
#define DTD_MAX_HEIGHT_MASK	0x07
#define DTD_MAX_HEIGHT_SHFT	0

/* max width configurations */
 /* unlimited width */
#define	MAX_OUT_WIDTH_UNLIMITED		0
/* as specified in max_size1 reg */
#define MAX_OUT_WIDTH_REG1		1
/* as specified in max_size2 reg */
#define MAX_OUT_WIDTH_REG2		2
/* as specified in max_size3 reg */
#define	MAX_OUT_WIDTH_REG3		3
/* maximum of 352 pixels as width */
#define MAX_OUT_WIDTH_352		4
/* maximum of 768 pixels as width */
#define	MAX_OUT_WIDTH_768		5
/* maximum of 1280 pixels width */
#define	MAX_OUT_WIDTH_1280		6
/* maximum of 1920 pixels as width */
#define	MAX_OUT_WIDTH_1920		7

/* max height configurations */
 /* unlimited height */
#define	MAX_OUT_HEIGHT_UNLIMITED	0
/* as specified in max_size1 reg */
#define MAX_OUT_HEIGHT_REG1		1
/* as specified in max_size2 reg */
#define MAX_OUT_HEIGHT_REG2		2
/* as specified in max_size3 reg */
#define	MAX_OUT_HEIGHT_REG3		3
/* maximum of 288 lines as height */
#define MAX_OUT_HEIGHT_288		4
/* maximum of 576 lines as height */
#define	MAX_OUT_HEIGHT_576		5
/* maximum of 720 lines as height */
#define	MAX_OUT_HEIGHT_720		6
/* maximum of 1080 lines as height */
#define	MAX_OUT_HEIGHT_1080		7

static inline u32 dtd_type_ctl_stride(int type, bool notify, int field,
			bool one_d, bool even_line_skip, bool odd_line_skip,
			int line_stride)
{
	return (type << DTD_DATA_TYPE_SHFT) | (notify << DTD_NOTIFY_SHFT) |
		(field << DTD_FIELD_SHFT) | (one_d << DTD_1D_SHFT) |
		(even_line_skip << DTD_EVEN_LINE_SKIP_SHFT) |
		(odd_line_skip << DTD_ODD_LINE_SKIP_SHFT) |
		line_stride;
}

static inline u32 dtd_xfer_length_height(int line_length, int xfer_height)
{
	return (line_length << DTD_LINE_LENGTH_SHFT) | xfer_height;
}

static inline u32 dtd_pkt_ctl(bool mode, bool dir, int chan, int pri,
			int next_chan)
{
	return (DTD_PKT_TYPE << DTD_PKT_TYPE_SHFT) | (mode << DTD_MODE_SHFT) |
		(dir << DTD_DIR_SHFT) | (chan << DTD_CHAN_SHFT) |
		(pri << DTD_PRI_SHFT) | next_chan;
}

static inline u32 dtd_frame_width_height(int width, int height)
{
	return (width << DTD_FRAME_WIDTH_SHFT) | height;
}

static inline u32 dtd_desc_write_addr(unsigned int addr, bool write_desc,
			bool drop_data, bool use_desc)
{
	return (addr << DTD_DESC_START_SHIFT) |
		(write_desc << DTD_WRITE_DESC_SHIFT) |
		(drop_data << DTD_DROP_DATA_SHIFT) |
		use_desc;
}

static inline u32 dtd_start_h_v(int h_start, int v_start)
{
	return (h_start << DTD_H_START_SHFT) | v_start;
}

static inline u32 dtd_max_width_height(int max_width, int max_height)
{
	return (max_width << DTD_MAX_WIDTH_SHFT) | max_height;
}

static inline int dtd_get_data_type(struct vpdma_dtd *dtd)
{
	return dtd->type_ctl_stride >> DTD_DATA_TYPE_SHFT;
}

static inline bool dtd_get_notify(struct vpdma_dtd *dtd)
{
	return (dtd->type_ctl_stride >> DTD_NOTIFY_SHFT) & DTD_NOTIFY_MASK;
}

static inline int dtd_get_field(struct vpdma_dtd *dtd)
{
	return (dtd->type_ctl_stride >> DTD_FIELD_SHFT) & DTD_FIELD_MASK;
}

static inline bool dtd_get_1d(struct vpdma_dtd *dtd)
{
	return (dtd->type_ctl_stride >> DTD_1D_SHFT) & DTD_1D_MASK;
}

static inline bool dtd_get_even_line_skip(struct vpdma_dtd *dtd)
{
	return (dtd->type_ctl_stride >> DTD_EVEN_LINE_SKIP_SHFT)
		& DTD_EVEN_LINE_SKIP_MASK;
}

static inline bool dtd_get_odd_line_skip(struct vpdma_dtd *dtd)
{
	return (dtd->type_ctl_stride >> DTD_ODD_LINE_SKIP_SHFT)
		& DTD_ODD_LINE_SKIP_MASK;
}

static inline int dtd_get_line_stride(struct vpdma_dtd *dtd)
{
	return dtd->type_ctl_stride & DTD_LINE_STRIDE_MASK;
}

static inline int dtd_get_line_length(struct vpdma_dtd *dtd)
{
	return dtd->xfer_length_height >> DTD_LINE_LENGTH_SHFT;
}

static inline int dtd_get_xfer_height(struct vpdma_dtd *dtd)
{
	return dtd->xfer_length_height & DTD_XFER_HEIGHT_MASK;
}

static inline int dtd_get_pkt_type(struct vpdma_dtd *dtd)
{
	return dtd->pkt_ctl >> DTD_PKT_TYPE_SHFT;
}

static inline bool dtd_get_mode(struct vpdma_dtd *dtd)
{
	return (dtd->pkt_ctl >> DTD_MODE_SHFT) & DTD_MODE_MASK;
}

static inline bool dtd_get_dir(struct vpdma_dtd *dtd)
{
	return (dtd->pkt_ctl >> DTD_DIR_SHFT) & DTD_DIR_MASK;
}

static inline int dtd_get_chan(struct vpdma_dtd *dtd)
{
	return (dtd->pkt_ctl >> DTD_CHAN_SHFT) & DTD_CHAN_MASK;
}

static inline int dtd_get_priority(struct vpdma_dtd *dtd)
{
	return (dtd->pkt_ctl >> DTD_PRI_SHFT) & DTD_PRI_MASK;
}

static inline int dtd_get_next_chan(struct vpdma_dtd *dtd)
{
	return (dtd->pkt_ctl >> DTD_NEXT_CHAN_SHFT) & DTD_NEXT_CHAN_MASK;
}

static inline int dtd_get_frame_width(struct vpdma_dtd *dtd)
{
	return dtd->frame_width_height >> DTD_FRAME_WIDTH_SHFT;
}

static inline int dtd_get_frame_height(struct vpdma_dtd *dtd)
{
	return dtd->frame_width_height & DTD_FRAME_HEIGHT_MASK;
}

static inline int dtd_get_desc_write_addr(struct vpdma_dtd *dtd)
{
	return dtd->desc_write_addr >> DTD_DESC_START_SHIFT;
}

static inline bool dtd_get_write_desc(struct vpdma_dtd *dtd)
{
	return (dtd->desc_write_addr >> DTD_WRITE_DESC_SHIFT) &
							DTD_WRITE_DESC_MASK;
}

static inline bool dtd_get_drop_data(struct vpdma_dtd *dtd)
{
	return (dtd->desc_write_addr >> DTD_DROP_DATA_SHIFT) &
							DTD_DROP_DATA_MASK;
}

static inline bool dtd_get_use_desc(struct vpdma_dtd *dtd)
{
	return dtd->desc_write_addr & DTD_USE_DESC_MASK;
}

static inline int dtd_get_h_start(struct vpdma_dtd *dtd)
{
	return dtd->start_h_v >> DTD_H_START_SHFT;
}

static inline int dtd_get_v_start(struct vpdma_dtd *dtd)
{
	return dtd->start_h_v & DTD_V_START_MASK;
}

static inline int dtd_get_max_width(struct vpdma_dtd *dtd)
{
	return (dtd->max_width_height >> DTD_MAX_WIDTH_SHFT) &
							DTD_MAX_WIDTH_MASK;
}

static inline int dtd_get_max_height(struct vpdma_dtd *dtd)
{
	return (dtd->max_width_height >> DTD_MAX_HEIGHT_SHFT) &
							DTD_MAX_HEIGHT_MASK;
}

/*
 * configuration descriptor
 */
struct vpdma_cfd {
	union {
		u32	dest_addr_offset;
		u32	w0;
	};
	union {
		u32	block_len;		/* in words */
		u32	w1;
	};
	u32		payload_addr;
	u32		ctl_payload_len;	/* in words */
};

/* Configuration descriptor specifics */

#define CFD_PKT_TYPE		0xb

#define CFD_DIRECT		1
#define CFD_INDIRECT		0
#define CFD_CLS_ADB		0
#define CFD_CLS_BLOCK		1

/* block_len */
#define CFD__BLOCK_LEN_MASK	0xffff
#define CFD__BLOCK_LEN_SHFT	0

/* ctl_payload_len */
#define CFD_PKT_TYPE_MASK	0x1f
#define CFD_PKT_TYPE_SHFT	27
#define CFD_DIRECT_MASK		0x01
#define CFD_DIRECT_SHFT		26
#define CFD_CLASS_MASK		0x03
#define CFD_CLASS_SHFT		24
#define CFD_DEST_MASK		0xff
#define CFD_DEST_SHFT		16
#define CFD_PAYLOAD_LEN_MASK	0xffff
#define CFD_PAYLOAD_LEN_SHFT	0

static inline u32 cfd_pkt_payload_len(bool direct, int cls, int dest,
		int payload_len)
{
	return (CFD_PKT_TYPE << CFD_PKT_TYPE_SHFT) |
		(direct << CFD_DIRECT_SHFT) |
		(cls << CFD_CLASS_SHFT) |
		(dest << CFD_DEST_SHFT) |
		payload_len;
}

static inline int cfd_get_pkt_type(struct vpdma_cfd *cfd)
{
	return cfd->ctl_payload_len >> CFD_PKT_TYPE_SHFT;
}

static inline bool cfd_get_direct(struct vpdma_cfd *cfd)
{
	return (cfd->ctl_payload_len >> CFD_DIRECT_SHFT) & CFD_DIRECT_MASK;
}

static inline bool cfd_get_class(struct vpdma_cfd *cfd)
{
	return (cfd->ctl_payload_len >> CFD_CLASS_SHFT) & CFD_CLASS_MASK;
}

static inline int cfd_get_dest(struct vpdma_cfd *cfd)
{
	return (cfd->ctl_payload_len >> CFD_DEST_SHFT) & CFD_DEST_MASK;
}

static inline int cfd_get_payload_len(struct vpdma_cfd *cfd)
{
	return cfd->ctl_payload_len & CFD_PAYLOAD_LEN_MASK;
}

/*
 * control descriptor
 */
struct vpdma_ctd {
	union {
		u32	timer_value;
		u32	list_addr;
		u32	w0;
	};
	union {
		u32	pixel_line_count;
		u32	list_size;
		u32	w1;
	};
	union {
		u32	event;
		u32	fid_ctl;
		u32	w2;
	};
	u32		type_source_ctl;
};

/* control descriptor types */
#define CTD_TYPE_SYNC_ON_CLIENT		0
#define CTD_TYPE_SYNC_ON_LIST		1
#define CTD_TYPE_SYNC_ON_EXT		2
#define CTD_TYPE_SYNC_ON_LM_TIMER	3
#define CTD_TYPE_SYNC_ON_CHANNEL	4
#define CTD_TYPE_CHNG_CLIENT_IRQ	5
#define CTD_TYPE_SEND_IRQ		6
#define CTD_TYPE_RELOAD_LIST		7
#define CTD_TYPE_ABORT_CHANNEL		8

#define CTD_PKT_TYPE		0xc

/* timer_value */
#define CTD_TIMER_VALUE_MASK	0xffff
#define CTD_TIMER_VALUE_SHFT	0

/* pixel_line_count */
#define CTD_PIXEL_COUNT_MASK	0xffff
#define CTD_PIXEL_COUNT_SHFT	16
#define CTD_LINE_COUNT_MASK	0xffff
#define CTD_LINE_COUNT_SHFT	0

/* list_size */
#define CTD_LIST_SIZE_MASK	0xffff
#define CTD_LIST_SIZE_SHFT	0

/* event */
#define CTD_EVENT_MASK		0x0f
#define CTD_EVENT_SHFT		0

/* fid_ctl */
#define CTD_FID2_MASK		0x03
#define CTD_FID2_SHFT		4
#define CTD_FID1_MASK		0x03
#define CTD_FID1_SHFT		2
#define CTD_FID0_MASK		0x03
#define CTD_FID0_SHFT		0

/* type_source_ctl */
#define CTD_PKT_TYPE_MASK	0x1f
#define CTD_PKT_TYPE_SHFT	27
#define CTD_SOURCE_MASK		0xff
#define CTD_SOURCE_SHFT		16
#define CTD_CONTROL_MASK	0x0f
#define CTD_CONTROL_SHFT	0

static inline u32 ctd_pixel_line_count(int pixel_count, int line_count)
{
	return (pixel_count << CTD_PIXEL_COUNT_SHFT) | line_count;
}

static inline u32 ctd_set_fid_ctl(int fid0, int fid1, int fid2)
{
	return (fid2 << CTD_FID2_SHFT) | (fid1 << CTD_FID1_SHFT) | fid0;
}

static inline u32 ctd_type_source_ctl(int source, int control)
{
	return (CTD_PKT_TYPE << CTD_PKT_TYPE_SHFT) |
		(source << CTD_SOURCE_SHFT) | control;
}

static inline u32 ctd_get_pixel_count(struct vpdma_ctd *ctd)
{
	return ctd->pixel_line_count >> CTD_PIXEL_COUNT_SHFT;
}

static inline int ctd_get_line_count(struct vpdma_ctd *ctd)
{
	return ctd->pixel_line_count & CTD_LINE_COUNT_MASK;
}

static inline int ctd_get_event(struct vpdma_ctd *ctd)
{
	return ctd->event & CTD_EVENT_MASK;
}

static inline int ctd_get_fid2_ctl(struct vpdma_ctd *ctd)
{
	return (ctd->fid_ctl >> CTD_FID2_SHFT) & CTD_FID2_MASK;
}

static inline int ctd_get_fid1_ctl(struct vpdma_ctd *ctd)
{
	return (ctd->fid_ctl >> CTD_FID1_SHFT) & CTD_FID1_MASK;
}

static inline int ctd_get_fid0_ctl(struct vpdma_ctd *ctd)
{
	return ctd->fid_ctl & CTD_FID2_MASK;
}

static inline int ctd_get_pkt_type(struct vpdma_ctd *ctd)
{
	return ctd->type_source_ctl >> CTD_PKT_TYPE_SHFT;
}

static inline int ctd_get_source(struct vpdma_ctd *ctd)
{
	return (ctd->type_source_ctl >> CTD_SOURCE_SHFT) & CTD_SOURCE_MASK;
}

static inline int ctd_get_ctl(struct vpdma_ctd *ctd)
{
	return ctd->type_source_ctl & CTD_CONTROL_MASK;
}

#endif
