/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>

#include "solo6010.h"
#include "solo6010-osd-font.h"

#define CAPTURE_MAX_BANDWIDTH		32	// D1 4channel (D1 == 4)
#define OSG_BUFFER_SIZE			1024

#define VI_PROG_HSIZE			(1280 - 16)
#define VI_PROG_VSIZE			(1024 - 16)

static void solo_capture_config(struct solo6010_dev *solo_dev)
{
	int i, j;
	unsigned long height;
	unsigned long width;
	unsigned char *buf;

	solo_reg_write(solo_dev, SOLO_CAP_BASE,
		       SOLO_CAP_MAX_PAGE(SOLO_CAP_EXT_MAX_PAGE *
					 solo_dev->nr_chans) |
		       SOLO_CAP_BASE_ADDR(SOLO_CAP_EXT_ADDR(solo_dev) >> 16));
	solo_reg_write(solo_dev, SOLO_CAP_BTW,
		       (1 << 17) | SOLO_CAP_PROG_BANDWIDTH(2) |
		       SOLO_CAP_MAX_BANDWIDTH(CAPTURE_MAX_BANDWIDTH));

	/* Set scale 1, 9 dimension */
	width = solo_dev->video_hsize;
	height = solo_dev->video_vsize;
	solo_reg_write(solo_dev, SOLO_DIM_SCALE1,
		       SOLO_DIM_H_MB_NUM(width / 16) |
		       SOLO_DIM_V_MB_NUM_FRAME(height / 8) |
		       SOLO_DIM_V_MB_NUM_FIELD(height / 16));

	/* Set scale 2, 10 dimension */
	width = solo_dev->video_hsize / 2;
	height = solo_dev->video_vsize;
	solo_reg_write(solo_dev, SOLO_DIM_SCALE2,
		       SOLO_DIM_H_MB_NUM(width / 16) |
		       SOLO_DIM_V_MB_NUM_FRAME(height / 8) |
		       SOLO_DIM_V_MB_NUM_FIELD(height / 16));

	/* Set scale 3, 11 dimension */
	width = solo_dev->video_hsize / 2;
	height = solo_dev->video_vsize / 2;
	solo_reg_write(solo_dev, SOLO_DIM_SCALE3,
		       SOLO_DIM_H_MB_NUM(width / 16) |
		       SOLO_DIM_V_MB_NUM_FRAME(height / 8) |
		       SOLO_DIM_V_MB_NUM_FIELD(height / 16));

	/* Set scale 4, 12 dimension */
	width = solo_dev->video_hsize / 3;
	height = solo_dev->video_vsize / 3;
	solo_reg_write(solo_dev, SOLO_DIM_SCALE4,
		       SOLO_DIM_H_MB_NUM(width / 16) |
		       SOLO_DIM_V_MB_NUM_FRAME(height / 8) |
		       SOLO_DIM_V_MB_NUM_FIELD(height / 16));

	/* Set scale 5, 13 dimension */
	width = solo_dev->video_hsize / 4;
	height = solo_dev->video_vsize / 2;
	solo_reg_write(solo_dev, SOLO_DIM_SCALE5,
		       SOLO_DIM_H_MB_NUM(width / 16) |
		       SOLO_DIM_V_MB_NUM_FRAME(height / 8) |
		       SOLO_DIM_V_MB_NUM_FIELD(height / 16));

	/* Progressive */
	width = VI_PROG_HSIZE;
	height = VI_PROG_VSIZE;
	solo_reg_write(solo_dev, SOLO_DIM_PROG,
		       SOLO_DIM_H_MB_NUM(width / 16) |
		       SOLO_DIM_V_MB_NUM_FRAME(height / 16) |
		       SOLO_DIM_V_MB_NUM_FIELD(height / 16));

	/* Clear OSD */
	solo_reg_write(solo_dev, SOLO_VE_OSD_CH, 0);
	solo_reg_write(solo_dev, SOLO_VE_OSD_BASE,
		       SOLO_EOSD_EXT_ADDR(solo_dev) >> 16);
	solo_reg_write(solo_dev, SOLO_VE_OSD_CLR,
		       0xF0 << 16 | 0x80 << 8 | 0x80);
	solo_reg_write(solo_dev, SOLO_VE_OSD_OPT, 0);

	/* Clear OSG buffer */
	buf = kzalloc(OSG_BUFFER_SIZE, GFP_KERNEL);
	if (!buf)
		return;

	for (i = 0; i < solo_dev->nr_chans; i++) {
		for (j = 0; j < SOLO_EOSD_EXT_SIZE; j += OSG_BUFFER_SIZE) {
			solo_p2m_dma(solo_dev, SOLO_P2M_DMA_ID_MP4E, 1, buf,
				     SOLO_EOSD_EXT_ADDR(solo_dev) +
				     (i * SOLO_EOSD_EXT_SIZE) + j,
				     OSG_BUFFER_SIZE);
		}
	}
	kfree(buf);
}

int solo_osd_print(struct solo_enc_dev *solo_enc)
{
	struct solo6010_dev *solo_dev = solo_enc->solo_dev;
	char *str = solo_enc->osd_text;
	u8 *buf;
	u32 reg = solo_reg_read(solo_dev, SOLO_VE_OSD_CH);
	int len = strlen(str);
	int i, j;
	int x = 1, y = 1;

	if (len == 0) {
		reg &= ~(1 << solo_enc->ch);
		solo_reg_write(solo_dev, SOLO_VE_OSD_CH, reg);
		return 0;
	}

	buf = kzalloc(SOLO_EOSD_EXT_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		for (j = 0; j < 16; j++) {
			buf[(j*2) + (i%2) + ((x + (i/2)) * 32) + (y * 2048)] =
				(solo_osd_font[(str[i] * 4) + (j / 4)]
					>> ((3 - (j % 4)) * 8)) & 0xff;
		}
	}

	solo_p2m_dma(solo_dev, 0, 1, buf, SOLO_EOSD_EXT_ADDR(solo_dev) +
		     (solo_enc->ch * SOLO_EOSD_EXT_SIZE), SOLO_EOSD_EXT_SIZE);
        reg |= (1 << solo_enc->ch);
        solo_reg_write(solo_dev, SOLO_VE_OSD_CH, reg);

	kfree(buf);

	return 0;
}

static void solo_jpeg_config(struct solo6010_dev *solo_dev)
{
	solo_reg_write(solo_dev, SOLO_VE_JPEG_QP_TBL,
		       (2 << 24) | (2 << 16) | (2 << 8) | (2 << 0));
	solo_reg_write(solo_dev, SOLO_VE_JPEG_QP_CH_L, 0);
	solo_reg_write(solo_dev, SOLO_VE_JPEG_QP_CH_H, 0);
	solo_reg_write(solo_dev, SOLO_VE_JPEG_CFG,
		(SOLO_JPEG_EXT_SIZE(solo_dev) & 0xffff0000) |
		((SOLO_JPEG_EXT_ADDR(solo_dev) >> 16) & 0x0000ffff));
	solo_reg_write(solo_dev, SOLO_VE_JPEG_CTRL, 0xffffffff);
}

static void solo_mp4e_config(struct solo6010_dev *solo_dev)
{
	int i;

	/* We can only use VE_INTR_CTRL(0) if we want to support mjpeg */
	solo_reg_write(solo_dev, SOLO_VE_CFG0,
		       SOLO_VE_INTR_CTRL(0) |
		       SOLO_VE_BLOCK_SIZE(SOLO_MP4E_EXT_SIZE(solo_dev) >> 16) |
		       SOLO_VE_BLOCK_BASE(SOLO_MP4E_EXT_ADDR(solo_dev) >> 16));

	solo_reg_write(solo_dev, SOLO_VE_CFG1,
		       SOLO_VE_BYTE_ALIGN(2) |
		       SOLO_VE_INSERT_INDEX | SOLO_VE_MOTION_MODE(0));

	solo_reg_write(solo_dev, SOLO_VE_WMRK_POLY, 0);
	solo_reg_write(solo_dev, SOLO_VE_VMRK_INIT_KEY, 0);
	solo_reg_write(solo_dev, SOLO_VE_WMRK_STRL, 0);
	solo_reg_write(solo_dev, SOLO_VE_ENCRYP_POLY, 0);
	solo_reg_write(solo_dev, SOLO_VE_ENCRYP_INIT, 0);

	solo_reg_write(solo_dev, SOLO_VE_ATTR,
		       SOLO_VE_LITTLE_ENDIAN |
		       SOLO_COMP_ATTR_FCODE(1) |
		       SOLO_COMP_TIME_INC(0) |
		       SOLO_COMP_TIME_WIDTH(15) |
		       SOLO_DCT_INTERVAL(36 / 4));

	for (i = 0; i < solo_dev->nr_chans; i++)
		solo_reg_write(solo_dev, SOLO_VE_CH_REF_BASE(i),
			       (SOLO_EREF_EXT_ADDR(solo_dev) +
			       (i * SOLO_EREF_EXT_SIZE)) >> 16);
}

int solo_enc_init(struct solo6010_dev *solo_dev)
{
	int i;

	solo_capture_config(solo_dev);
	solo_mp4e_config(solo_dev);
	solo_jpeg_config(solo_dev);

	for (i = 0; i < solo_dev->nr_chans; i++) {
		solo_reg_write(solo_dev, SOLO_CAP_CH_SCALE(i), 0);
		solo_reg_write(solo_dev, SOLO_CAP_CH_COMP_ENA_E(i), 0);
	}

	solo6010_irq_on(solo_dev, SOLO_IRQ_ENCODER);

	return 0;
}

void solo_enc_exit(struct solo6010_dev *solo_dev)
{
	int i;

	solo6010_irq_off(solo_dev, SOLO_IRQ_ENCODER);

	for (i = 0; i < solo_dev->nr_chans; i++) {
		solo_reg_write(solo_dev, SOLO_CAP_CH_SCALE(i), 0);
		solo_reg_write(solo_dev, SOLO_CAP_CH_COMP_ENA_E(i), 0);
	}
}
