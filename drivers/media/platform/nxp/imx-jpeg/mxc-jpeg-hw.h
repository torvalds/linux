/* SPDX-License-Identifier: GPL-2.0 */
/*
 * i.MX8QXP/i.MX8QM JPEG encoder/decoder v4l2 driver
 *
 * Copyright 2018-2019 NXP
 */

#ifndef _MXC_JPEG_HW_H
#define _MXC_JPEG_HW_H

/* JPEG Decoder/Encoder Wrapper Register Map */
#define GLB_CTRL			0x0
#define COM_STATUS			0x4
#define BUF_BASE0			0x14
#define BUF_BASE1			0x18
#define LINE_PITCH			0x1C
#define STM_BUFBASE			0x20
#define STM_BUFSIZE			0x24
#define IMGSIZE				0x28
#define STM_CTRL			0x2C

/* CAST JPEG-Decoder/Encoder Status Register Map (read-only)*/
#define CAST_STATUS0			0x100
#define CAST_STATUS1			0x104
#define CAST_STATUS2			0x108
#define CAST_STATUS3			0x10c
#define CAST_STATUS4			0x110
#define CAST_STATUS5			0x114
#define CAST_STATUS6			0x118
#define CAST_STATUS7			0x11c
#define CAST_STATUS8			0x120
#define CAST_STATUS9			0x124
#define CAST_STATUS10			0x128
#define CAST_STATUS11			0x12c
#define CAST_STATUS12			0x130
#define CAST_STATUS13			0x134
/* the following are for encoder only */
#define CAST_STATUS14		0x138
#define CAST_STATUS15		0x13c
#define CAST_STATUS16		0x140
#define CAST_STATUS17		0x144
#define CAST_STATUS18		0x148
#define CAST_STATUS19		0x14c

/* CAST JPEG-Decoder Control Register Map (write-only) */
#define CAST_CTRL			CAST_STATUS13

/* CAST JPEG-Encoder Control Register Map (write-only) */
#define CAST_MODE			CAST_STATUS0
#define CAST_CFG_MODE			CAST_STATUS1
#define CAST_QUALITY			CAST_STATUS2
#define CAST_RSVD			CAST_STATUS3
#define CAST_REC_REGS_SEL		CAST_STATUS4
#define CAST_LUMTH			CAST_STATUS5
#define CAST_CHRTH			CAST_STATUS6
#define CAST_NOMFRSIZE_LO		CAST_STATUS16
#define CAST_NOMFRSIZE_HI		CAST_STATUS17
#define CAST_OFBSIZE_LO			CAST_STATUS18
#define CAST_OFBSIZE_HI			CAST_STATUS19

#define MXC_MAX_SLOTS	1 /* TODO use all 4 slots*/
/* JPEG-Decoder Wrapper Slot Registers 0..3 */
#define SLOT_BASE			0x10000
#define SLOT_STATUS			0x0
#define SLOT_IRQ_EN			0x4
#define SLOT_BUF_PTR			0x8
#define SLOT_CUR_DESCPT_PTR		0xC
#define SLOT_NXT_DESCPT_PTR		0x10
#define MXC_SLOT_OFFSET(slot, offset)	((SLOT_BASE * ((slot) + 1)) + (offset))

/* GLB_CTRL fields */
#define GLB_CTRL_JPG_EN					0x1
#define GLB_CTRL_SFT_RST				(0x1 << 1)
#define GLB_CTRL_DEC_GO					(0x1 << 2)
#define GLB_CTRL_L_ENDIAN(le)				((le) << 3)
#define GLB_CTRL_SLOT_EN(slot)				(0x1 << ((slot) + 4))

/* COM_STAUS fields */
#define COM_STATUS_DEC_ONGOING(r)		(((r) & (1 << 31)) >> 31)
#define COM_STATUS_CUR_SLOT(r)			(((r) & (0x3 << 29)) >> 29)

/* STM_CTRL fields */
#define STM_CTRL_PIXEL_PRECISION		(0x1 << 2)
#define STM_CTRL_IMAGE_FORMAT(img_fmt)		((img_fmt) << 3)
#define STM_CTRL_IMAGE_FORMAT_MASK		(0xF << 3)
#define STM_CTRL_BITBUF_PTR_CLR(clr)		((clr) << 7)
#define STM_CTRL_AUTO_START(go)			((go) << 8)
#define STM_CTRL_CONFIG_MOD(mod)		((mod) << 9)

/* SLOT_STATUS fields for slots 0..3 */
#define SLOT_STATUS_FRMDONE			(0x1 << 3)
#define SLOT_STATUS_ENC_CONFIG_ERR		(0x1 << 8)

/* SLOT_IRQ_EN fields TBD */

#define MXC_NXT_DESCPT_EN			0x1
#define MXC_DEC_EXIT_IDLE_MODE			0x4

/* JPEG-Decoder Wrapper - STM_CTRL Register Fields */
#define MXC_PIXEL_PRECISION(precision) ((precision) / 8 << 2)
enum mxc_jpeg_image_format {
	MXC_JPEG_INVALID = -1,
	MXC_JPEG_YUV420 = 0x0, /* 2 Plannar, Y=1st plane UV=2nd plane */
	MXC_JPEG_YUV422 = 0x1, /* 1 Plannar, YUYV sequence */
	MXC_JPEG_BGR	= 0x2, /* BGR packed format */
	MXC_JPEG_YUV444	= 0x3, /* 1 Plannar, YUVYUV sequence */
	MXC_JPEG_GRAY = 0x4, /* Y8 or Y12 or Single Component */
	MXC_JPEG_RESERVED = 0x5,
	MXC_JPEG_ABGR	= 0x6,
};

#include "mxc-jpeg.h"
void print_descriptor_info(struct device *dev, struct mxc_jpeg_desc *desc);
void print_cast_status(struct device *dev, void __iomem *reg,
		       unsigned int mode);
void print_wrapper_info(struct device *dev, void __iomem *reg);
void mxc_jpeg_sw_reset(void __iomem *reg);
int mxc_jpeg_enable(void __iomem *reg);
void wait_frmdone(struct device *dev, void __iomem *reg);
void mxc_jpeg_enc_mode_conf(struct device *dev, void __iomem *reg, u8 extseq);
void mxc_jpeg_enc_mode_go(struct device *dev, void __iomem *reg, u8 extseq);
void mxc_jpeg_enc_set_quality(struct device *dev, void __iomem *reg, u8 quality);
void mxc_jpeg_dec_mode_go(struct device *dev, void __iomem *reg);
int mxc_jpeg_get_slot(void __iomem *reg);
u32 mxc_jpeg_get_offset(void __iomem *reg, int slot);
void mxc_jpeg_enable_slot(void __iomem *reg, int slot);
void mxc_jpeg_set_l_endian(void __iomem *reg, int le);
void mxc_jpeg_enable_irq(void __iomem *reg, int slot);
void mxc_jpeg_disable_irq(void __iomem *reg, int slot);
int mxc_jpeg_set_input(void __iomem *reg, u32 in_buf, u32 bufsize);
int mxc_jpeg_set_output(void __iomem *reg, u16 out_pitch, u32 out_buf,
			u16 w, u16 h);
void mxc_jpeg_set_config_mode(void __iomem *reg, int config_mode);
int mxc_jpeg_set_params(struct mxc_jpeg_desc *desc,  u32 bufsize, u16
			out_pitch, u32 format);
void mxc_jpeg_set_bufsize(struct mxc_jpeg_desc *desc,  u32 bufsize);
void mxc_jpeg_set_res(struct mxc_jpeg_desc *desc, u16 w, u16 h);
void mxc_jpeg_set_line_pitch(struct mxc_jpeg_desc *desc, u32 line_pitch);
void mxc_jpeg_set_desc(u32 desc, void __iomem *reg, int slot);
void mxc_jpeg_clr_desc(void __iomem *reg, int slot);
void mxc_jpeg_set_regs_from_desc(struct mxc_jpeg_desc *desc,
				 void __iomem *reg);
#endif
