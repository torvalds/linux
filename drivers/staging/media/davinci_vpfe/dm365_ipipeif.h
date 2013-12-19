/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _DAVINCI_VPFE_DM365_IPIPEIF_H
#define _DAVINCI_VPFE_DM365_IPIPEIF_H

#include <linux/platform_device.h>

#include <media/davinci/vpss.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "dm365_ipipeif_user.h"
#include "vpfe_video.h"

/* IPIPE base specific types */
enum ipipeif_data_shift {
	IPIPEIF_BITS15_2 = 0,
	IPIPEIF_BITS14_1 = 1,
	IPIPEIF_BITS13_0 = 2,
	IPIPEIF_BITS12_0 = 3,
	IPIPEIF_BITS11_0 = 4,
	IPIPEIF_BITS10_0 = 5,
	IPIPEIF_BITS9_0 = 6,
};

enum ipipeif_clkdiv {
	IPIPEIF_DIVIDE_HALF = 0,
	IPIPEIF_DIVIDE_THIRD = 1,
	IPIPEIF_DIVIDE_FOURTH = 2,
	IPIPEIF_DIVIDE_FIFTH = 3,
	IPIPEIF_DIVIDE_SIXTH = 4,
	IPIPEIF_DIVIDE_EIGHTH = 5,
	IPIPEIF_DIVIDE_SIXTEENTH = 6,
	IPIPEIF_DIVIDE_THIRTY = 7,
};

enum ipipeif_pack_mode  {
	IPIPEIF_PACK_16_BIT = 0,
	IPIPEIF_PACK_8_BIT = 1,
};

enum ipipeif_5_1_pack_mode  {
	IPIPEIF_5_1_PACK_16_BIT = 0,
	IPIPEIF_5_1_PACK_8_BIT = 1,
	IPIPEIF_5_1_PACK_8_BIT_A_LAW = 2,
	IPIPEIF_5_1_PACK_12_BIT = 3
};

enum  ipipeif_input_source {
	IPIPEIF_CCDC = 0,
	IPIPEIF_SDRAM_RAW = 1,
	IPIPEIF_CCDC_DARKFM = 2,
	IPIPEIF_SDRAM_YUV = 3,
};

enum ipipeif_ialaw {
	IPIPEIF_ALAW_OFF = 0,
	IPIPEIF_ALAW_ON = 1,
};

enum  ipipeif_input_src1 {
	IPIPEIF_SRC1_PARALLEL_PORT = 0,
	IPIPEIF_SRC1_SDRAM_RAW = 1,
	IPIPEIF_SRC1_ISIF_DARKFM = 2,
	IPIPEIF_SRC1_SDRAM_YUV = 3,
};

enum ipipeif_dfs_dir {
	IPIPEIF_PORT_MINUS_SDRAM = 0,
	IPIPEIF_SDRAM_MINUS_PORT = 1,
};

enum ipipeif_chroma_phase {
	IPIPEIF_CBCR_Y = 0,
	IPIPEIF_Y_CBCR = 1,
};

enum ipipeif_dpcm_type {
	IPIPEIF_DPCM_8BIT_10BIT = 0,
	IPIPEIF_DPCM_8BIT_12BIT = 1,
};

/* data shift for IPIPE 5.1 */
enum ipipeif_5_1_data_shift {
	IPIPEIF_5_1_BITS11_0 = 0,
	IPIPEIF_5_1_BITS10_0 = 1,
	IPIPEIF_5_1_BITS9_0 = 2,
	IPIPEIF_5_1_BITS8_0 = 3,
	IPIPEIF_5_1_BITS7_0 = 4,
	IPIPEIF_5_1_BITS15_4 = 5,
};

#define IPIPEIF_PAD_SINK      0
#define IPIPEIF_PAD_SOURCE    1

#define IPIPEIF_NUM_PADS	2

enum ipipeif_input_entity {
	IPIPEIF_INPUT_NONE = 0,
	IPIPEIF_INPUT_ISIF = 1,
	IPIPEIF_INPUT_MEMORY = 2,
};

enum ipipeif_output_entity {
	IPIPEIF_OUTPUT_NONE = 0,
	IPIPEIF_OUTPUT_IPIPE = 1,
	IPIPEIF_OUTPUT_RESIZER = 2,
};

struct vpfe_ipipeif_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[IPIPEIF_NUM_PADS];
	struct v4l2_mbus_framefmt formats[IPIPEIF_NUM_PADS];
	enum ipipeif_input_entity input;
	unsigned int output;
	struct vpfe_video_device video_in;
	struct v4l2_ctrl_handler ctrls;
	void *__iomem ipipeif_base_addr;
	struct ipipeif_params config;
	int dpcm_predictor;
	int gain;
};

/* IPIPEIF Register Offsets from the base address */
#define IPIPEIF_ENABLE			0x00
#define IPIPEIF_CFG1			0x04
#define IPIPEIF_PPLN			0x08
#define IPIPEIF_LPFR			0x0c
#define IPIPEIF_HNUM			0x10
#define IPIPEIF_VNUM			0x14
#define IPIPEIF_ADDRU			0x18
#define IPIPEIF_ADDRL			0x1c
#define IPIPEIF_ADOFS			0x20
#define IPIPEIF_RSZ			0x24
#define IPIPEIF_GAIN			0x28

/* Below registers are available only on IPIPE 5.1 */
#define IPIPEIF_DPCM			0x2c
#define IPIPEIF_CFG2			0x30
#define IPIPEIF_INIRSZ			0x34
#define IPIPEIF_OCLIP			0x38
#define IPIPEIF_DTUDF			0x3c
#define IPIPEIF_CLKDIV			0x40
#define IPIPEIF_DPC1			0x44
#define IPIPEIF_DPC2			0x48
#define IPIPEIF_DFSGVL			0x4c
#define IPIPEIF_DFSGTH			0x50
#define IPIPEIF_RSZ3A			0x54
#define IPIPEIF_INIRSZ3A		0x58
#define IPIPEIF_RSZ_MIN			16
#define IPIPEIF_RSZ_MAX			112
#define IPIPEIF_RSZ_CONST		16
#define SETBIT(reg, bit)   (reg = ((reg) | ((0x00000001)<<(bit))))
#define RESETBIT(reg, bit) (reg = ((reg) & (~(0x00000001<<(bit)))))

#define IPIPEIF_ADOFS_LSB_MASK		0x1ff
#define IPIPEIF_ADOFS_LSB_SHIFT		5
#define IPIPEIF_ADOFS_MSB_MASK		0x200
#define IPIPEIF_ADDRU_MASK		0x7ff
#define IPIPEIF_ADDRL_SHIFT		5
#define IPIPEIF_ADDRL_MASK		0xffff
#define IPIPEIF_ADDRU_SHIFT		21
#define IPIPEIF_ADDRMSB_SHIFT		31
#define IPIPEIF_ADDRMSB_LEFT_SHIFT	10

/* CFG1 Masks and shifts */
#define ONESHOT_SHIFT			0
#define DECIM_SHIFT			1
#define INPSRC_SHIFT			2
#define CLKDIV_SHIFT			4
#define AVGFILT_SHIFT			7
#define PACK8IN_SHIFT			8
#define IALAW_SHIFT			9
#define CLKSEL_SHIFT			10
#define DATASFT_SHIFT			11
#define INPSRC1_SHIFT			14

/* DPC2 */
#define IPIPEIF_DPC2_EN_SHIFT		12
#define IPIPEIF_DPC2_THR_MASK		0xfff
/* Applicable for IPIPE 5.1 */
#define IPIPEIF_DF_GAIN_EN_SHIFT	10
#define IPIPEIF_DF_GAIN_MASK		0x3ff
#define IPIPEIF_DF_GAIN_THR_MASK	0xfff
/* DPCM */
#define IPIPEIF_DPCM_BITS_SHIFT		2
#define IPIPEIF_DPCM_PRED_SHIFT		1
/* CFG2 */
#define IPIPEIF_CFG2_HDPOL_SHIFT	1
#define IPIPEIF_CFG2_VDPOL_SHIFT	2
#define IPIPEIF_CFG2_YUV8_SHIFT		6
#define IPIPEIF_CFG2_YUV16_SHIFT	3
#define IPIPEIF_CFG2_YUV8P_SHIFT	7

/* INIRSZ */
#define IPIPEIF_INIRSZ_ALNSYNC_SHIFT	13
#define IPIPEIF_INIRSZ_MASK		0x1fff

/* CLKDIV */
#define IPIPEIF_CLKDIV_M_SHIFT		8

void vpfe_ipipeif_enable(struct vpfe_device *vpfe_dev);
void vpfe_ipipeif_ss_buffer_isr(struct vpfe_ipipeif_device *ipipeif);
int vpfe_ipipeif_decimation_enabled(struct vpfe_device *vpfe_dev);
int vpfe_ipipeif_get_rsz(struct vpfe_device *vpfe_dev);
void vpfe_ipipeif_cleanup(struct vpfe_ipipeif_device *ipipeif,
			  struct platform_device *pdev);
int vpfe_ipipeif_init(struct vpfe_ipipeif_device *ipipeif,
		      struct platform_device *pdev);
int vpfe_ipipeif_register_entities(struct vpfe_ipipeif_device *ipipeif,
				   struct v4l2_device *vdev);
void vpfe_ipipeif_unregister_entities(struct vpfe_ipipeif_device *ipipeif);

#endif		/* _DAVINCI_VPFE_DM365_IPIPEIF_H */
