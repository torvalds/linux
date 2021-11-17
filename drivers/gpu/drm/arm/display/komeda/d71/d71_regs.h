/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _D71_REG_H_
#define _D71_REG_H_

/* Common block registers offset */
#define BLK_BLOCK_INFO		0x000
#define BLK_PIPELINE_INFO	0x004
#define BLK_MAX_LINE_SIZE	0x008
#define BLK_VALID_INPUT_ID0	0x020
#define BLK_OUTPUT_ID0		0x060
#define BLK_INPUT_ID0		0x080
#define BLK_IRQ_RAW_STATUS	0x0A0
#define BLK_IRQ_CLEAR		0x0A4
#define BLK_IRQ_MASK		0x0A8
#define BLK_IRQ_STATUS		0x0AC
#define BLK_STATUS		0x0B0
#define BLK_INFO		0x0C0
#define BLK_CONTROL		0x0D0
#define BLK_SIZE		0x0D4
#define BLK_IN_SIZE		0x0E0

#define BLK_P0_PTR_LOW		0x100
#define BLK_P0_PTR_HIGH		0x104
#define BLK_P0_STRIDE		0x108
#define BLK_P1_PTR_LOW		0x110
#define BLK_P1_PTR_HIGH		0x114
#define BLK_P1_STRIDE		0x118
#define BLK_P2_PTR_LOW		0x120
#define BLK_P2_PTR_HIGH		0x124

#define BLOCK_INFO_N_SUBBLKS(x)	((x) & 0x000F)
#define BLOCK_INFO_BLK_ID(x)	(((x) & 0x00F0) >> 4)
#define BLOCK_INFO_BLK_TYPE(x)	(((x) & 0xFF00) >> 8)
#define BLOCK_INFO_INPUT_ID(x)	((x) & 0xFFF0)
#define BLOCK_INFO_TYPE_ID(x)	(((x) & 0x0FF0) >> 4)

#define PIPELINE_INFO_N_OUTPUTS(x)	((x) & 0x000F)
#define PIPELINE_INFO_N_VALID_INPUTS(x)	(((x) & 0x0F00) >> 8)

/* Common block control register bits */
#define BLK_CTRL_EN		BIT(0)
/* Common size macro */
#define HV_SIZE(h, v)		(((h) & 0x1FFF) + (((v) & 0x1FFF) << 16))
#define HV_OFFSET(h, v)		(((h) & 0xFFF) + (((v) & 0xFFF) << 16))
#define HV_CROP(h, v)		(((h) & 0xFFF) + (((v) & 0xFFF) << 16))

/* AD_CONTROL register */
#define AD_CONTROL		0x160

/* AD_CONTROL register bits */
#define AD_AEN			BIT(0)
#define AD_YT			BIT(1)
#define AD_BS			BIT(2)
#define AD_WB			BIT(3)
#define AD_TH			BIT(4)

/* Global Control Unit */
#define GLB_ARCH_ID		0x000
#define GLB_CORE_ID		0x004
#define GLB_CORE_INFO		0x008
#define GLB_IRQ_STATUS		0x010

#define GCU_CONFIG_VALID0	0x0D4
#define GCU_CONFIG_VALID1	0x0D8

/* GCU_CONTROL_BITS */
#define GCU_CONTROL_MODE(x)	((x) & 0x7)
#define GCU_CONTROL_SRST	BIT(16)

/* GCU_CONFIGURATION registers */
#define GCU_CONFIGURATION_ID0	0x100
#define GCU_CONFIGURATION_ID1	0x104

/* GCU configuration */
#define GCU_MAX_LINE_SIZE(x)	((x) & 0xFFFF)
#define GCU_MAX_NUM_LINES(x)	((x) >> 16)
#define GCU_NUM_RICH_LAYERS(x)	((x) & 0x7)
#define GCU_NUM_PIPELINES(x)	(((x) >> 3) & 0x7)
#define GCU_NUM_SCALERS(x)	(((x) >> 6) & 0x7)
#define GCU_DISPLAY_SPLIT_EN(x)	(((x) >> 16) & 0x1)
#define GCU_DISPLAY_TBU_EN(x)	(((x) >> 17) & 0x1)

/* GCU opmode */
#define INACTIVE_MODE		0
#define TBU_CONNECT_MODE	1
#define TBU_DISCONNECT_MODE	2
#define DO0_ACTIVE_MODE		3
#define DO1_ACTIVE_MODE		4
#define DO01_ACTIVE_MODE	5

/* GLB_IRQ_STATUS bits */
#define GLB_IRQ_STATUS_GCU	BIT(0)
#define GLB_IRQ_STATUS_LPU0	BIT(8)
#define GLB_IRQ_STATUS_LPU1	BIT(9)
#define GLB_IRQ_STATUS_ATU0	BIT(10)
#define GLB_IRQ_STATUS_ATU1	BIT(11)
#define GLB_IRQ_STATUS_ATU2	BIT(12)
#define GLB_IRQ_STATUS_ATU3	BIT(13)
#define GLB_IRQ_STATUS_CU0	BIT(16)
#define GLB_IRQ_STATUS_CU1	BIT(17)
#define GLB_IRQ_STATUS_DOU0	BIT(24)
#define GLB_IRQ_STATUS_DOU1	BIT(25)

#define GLB_IRQ_STATUS_PIPE0	(GLB_IRQ_STATUS_LPU0 |\
				 GLB_IRQ_STATUS_ATU0 |\
				 GLB_IRQ_STATUS_ATU1 |\
				 GLB_IRQ_STATUS_CU0 |\
				 GLB_IRQ_STATUS_DOU0)

#define GLB_IRQ_STATUS_PIPE1	(GLB_IRQ_STATUS_LPU1 |\
				 GLB_IRQ_STATUS_ATU2 |\
				 GLB_IRQ_STATUS_ATU3 |\
				 GLB_IRQ_STATUS_CU1 |\
				 GLB_IRQ_STATUS_DOU1)

#define GLB_IRQ_STATUS_ATU	(GLB_IRQ_STATUS_ATU0 |\
				 GLB_IRQ_STATUS_ATU1 |\
				 GLB_IRQ_STATUS_ATU2 |\
				 GLB_IRQ_STATUS_ATU3)

/* GCU_IRQ_BITS */
#define GCU_IRQ_CVAL0		BIT(0)
#define GCU_IRQ_CVAL1		BIT(1)
#define GCU_IRQ_MODE		BIT(4)
#define GCU_IRQ_ERR		BIT(11)

/* GCU_STATUS_BITS */
#define GCU_STATUS_MODE(x)	((x) & 0x7)
#define GCU_STATUS_MERR		BIT(4)
#define GCU_STATUS_TCS0		BIT(8)
#define GCU_STATUS_TCS1		BIT(9)
#define GCU_STATUS_ACTIVE	BIT(31)

/* GCU_CONFIG_VALIDx BITS */
#define GCU_CONFIG_CVAL		BIT(0)

/* PERIPHERAL registers */
#define PERIPH_MAX_LINE_SIZE	BIT(0)
#define PERIPH_NUM_RICH_LAYERS	BIT(4)
#define PERIPH_SPLIT_EN		BIT(8)
#define PERIPH_TBU_EN		BIT(12)
#define PERIPH_AFBC_DMA_EN	BIT(16)
#define PERIPH_CONFIGURATION_ID	0x1D4

/* LPU register */
#define LPU_TBU_STATUS		0x0B4
#define LPU_RAXI_CONTROL	0x0D0
#define LPU_WAXI_CONTROL	0x0D4
#define LPU_TBU_CONTROL		0x0D8

/* LPU_xAXI_CONTROL_BITS */
#define TO_RAXI_AOUTSTDCAPB(x)	(x)
#define TO_RAXI_BOUTSTDCAPB(x)	((x) << 8)
#define TO_RAXI_BEN(x)		((x) << 15)
#define TO_xAXI_BURSTLEN(x)	((x) << 16)
#define TO_xAXI_AxQOS(x)	((x) << 24)
#define TO_xAXI_ORD(x)		((x) << 31)
#define TO_WAXI_OUTSTDCAPB(x)	(x)

#define RAXI_AOUTSTDCAPB_MASK	0x7F
#define RAXI_BOUTSTDCAPB_MASK	0x7F00
#define RAXI_BEN_MASK		BIT(15)
#define xAXI_BURSTLEN_MASK	0x3F0000
#define xAXI_AxQOS_MASK		0xF000000
#define xAXI_ORD_MASK		BIT(31)
#define WAXI_OUTSTDCAPB_MASK	0x3F

/* LPU_TBU_CONTROL BITS */
#define TO_TBU_DOUTSTDCAPB(x)	(x)
#define TBU_DOUTSTDCAPB_MASK	0x3F

/* LPU_IRQ_BITS */
#define LPU_IRQ_OVR		BIT(9)
#define LPU_IRQ_IBSY		BIT(10)
#define LPU_IRQ_ERR		BIT(11)
#define LPU_IRQ_EOW		BIT(12)
#define LPU_IRQ_PL0		BIT(13)

/* LPU_STATUS_BITS */
#define LPU_STATUS_AXIED(x)	((x) & 0xF)
#define LPU_STATUS_AXIE		BIT(4)
#define LPU_STATUS_AXIRP	BIT(5)
#define LPU_STATUS_AXIWP	BIT(6)
#define LPU_STATUS_FEMPTY	BIT(11)
#define LPU_STATUS_FFULL	BIT(14)
#define LPU_STATUS_ACE0		BIT(16)
#define LPU_STATUS_ACE1		BIT(17)
#define LPU_STATUS_ACE2		BIT(18)
#define LPU_STATUS_ACE3		BIT(19)
#define LPU_STATUS_ACTIVE	BIT(31)

#define AXIEID_MASK		0xF
#define AXIE_MASK		LPU_STATUS_AXIE
#define AXIRP_MASK		LPU_STATUS_AXIRP
#define AXIWP_MASK		LPU_STATUS_AXIWP

#define FROM_AXIEID(reg)	((reg) & AXIEID_MASK)
#define TO_AXIE(x)		((x) << 4)
#define FROM_AXIRP(reg)		(((reg) & AXIRP_MASK) >> 5)
#define FROM_AXIWP(reg)		(((reg) & AXIWP_MASK) >> 6)

/* LPU_TBU_STATUS_BITS */
#define LPU_TBU_STATUS_TCF	BIT(1)
#define LPU_TBU_STATUS_TTNG	BIT(2)
#define LPU_TBU_STATUS_TITR	BIT(8)
#define LPU_TBU_STATUS_TEMR	BIT(16)
#define LPU_TBU_STATUS_TTF	BIT(31)

/* LPU_TBU_CONTROL BITS */
#define LPU_TBU_CTRL_TLBPEN	BIT(16)

/* CROSSBAR CONTROL BITS */
#define CBU_INPUT_CTRL_EN	BIT(0)
#define CBU_NUM_INPUT_IDS	5
#define CBU_NUM_OUTPUT_IDS	5

/* CU register */
#define CU_BG_COLOR		0x0DC
#define CU_INPUT0_SIZE		0x0E0
#define CU_INPUT0_OFFSET	0x0E4
#define CU_INPUT0_CONTROL	0x0E8
#define CU_INPUT1_SIZE		0x0F0
#define CU_INPUT1_OFFSET	0x0F4
#define CU_INPUT1_CONTROL	0x0F8
#define CU_INPUT2_SIZE		0x100
#define CU_INPUT2_OFFSET	0x104
#define CU_INPUT2_CONTROL	0x108
#define CU_INPUT3_SIZE		0x110
#define CU_INPUT3_OFFSET	0x114
#define CU_INPUT3_CONTROL	0x118
#define CU_INPUT4_SIZE		0x120
#define CU_INPUT4_OFFSET	0x124
#define CU_INPUT4_CONTROL	0x128

#define CU_PER_INPUT_REGS	4

#define CU_NUM_INPUT_IDS	5
#define CU_NUM_OUTPUT_IDS	1

/* CU control register bits */
#define CU_CTRL_COPROC		BIT(0)

/* CU_IRQ_BITS */
#define CU_IRQ_OVR		BIT(9)
#define CU_IRQ_ERR		BIT(11)

/* CU_STATUS_BITS */
#define CU_STATUS_CPE		BIT(0)
#define CU_STATUS_ZME		BIT(1)
#define CU_STATUS_CFGE		BIT(2)
#define CU_STATUS_ACTIVE	BIT(31)

/* CU input control register bits */
#define CU_INPUT_CTRL_EN	BIT(0)
#define CU_INPUT_CTRL_PAD	BIT(1)
#define CU_INPUT_CTRL_PMUL	BIT(2)
#define CU_INPUT_CTRL_ALPHA(x)	(((x) & 0xFF) << 8)

/* DOU register */

/* DOU_IRQ_BITS */
#define DOU_IRQ_UND		BIT(8)
#define DOU_IRQ_ERR		BIT(11)
#define DOU_IRQ_PL0		BIT(13)
#define DOU_IRQ_PL1		BIT(14)

/* DOU_STATUS_BITS */
#define DOU_STATUS_DRIFTTO	BIT(0)
#define DOU_STATUS_FRAMETO	BIT(1)
#define DOU_STATUS_TETO		BIT(2)
#define DOU_STATUS_CSCE		BIT(8)
#define DOU_STATUS_ACTIVE	BIT(31)

/* Layer registers */
#define LAYER_INFO		0x0C0
#define LAYER_R_CONTROL		0x0D4
#define LAYER_FMT		0x0D8
#define LAYER_LT_COEFFTAB	0x0DC
#define LAYER_PALPHA		0x0E4

#define LAYER_YUV_RGB_COEFF0	0x130

#define LAYER_AD_H_CROP		0x164
#define LAYER_AD_V_CROP		0x168

#define LAYER_RGB_RGB_COEFF0	0x170

/* L_CONTROL_BITS */
#define L_EN			BIT(0)
#define L_IT			BIT(4)
#define L_R2R			BIT(5)
#define L_FT			BIT(6)
#define L_ROT(x)		(((x) & 3) << 8)
#define L_HFLIP			BIT(10)
#define L_VFLIP			BIT(11)
#define L_TBU_EN		BIT(16)
#define L_A_RCACHE(x)		(((x) & 0xF) << 28)
#define L_ROT_R0		0
#define L_ROT_R90		1
#define L_ROT_R180		2
#define L_ROT_R270		3

/* LAYER_R_CONTROL BITS */
#define LR_CHI422_BILINEAR	0
#define LR_CHI422_REPLICATION	1
#define LR_CHI420_JPEG		(0 << 2)
#define LR_CHI420_MPEG		(1 << 2)

#define L_ITSEL(x)		((x) & 0xFFF)
#define L_FTSEL(x)		(((x) & 0xFFF) << 16)

#define LAYER_PER_PLANE_REGS	4

/* Layer_WR registers */
#define LAYER_WR_PROG_LINE	0x0D4
#define LAYER_WR_FORMAT		0x0D8

/* Layer_WR control bits */
#define LW_OFM			BIT(4)
#define LW_LALPHA(x)		(((x) & 0xFF) << 8)
#define LW_A_WCACHE(x)		(((x) & 0xF) << 28)
#define LW_TBU_EN		BIT(16)

#define AxCACHE_MASK		0xF0000000

/* Layer AXI R/W cache setting */
#define AxCACHE_B		BIT(0)	/* Bufferable */
#define AxCACHE_M		BIT(1)	/* Modifiable */
#define AxCACHE_RA		BIT(2)	/* Read-Allocate */
#define AxCACHE_WA		BIT(3)	/* Write-Allocate */

/* Layer info bits */
#define L_INFO_RF		BIT(0)
#define L_INFO_CM		BIT(1)
#define L_INFO_ABUF_SIZE(x)	(((x) >> 4) & 0x7)
#define L_INFO_YUV_MAX_LINESZ(x)	(((x) >> 16) & 0xFFFF)

/* Scaler registers */
#define SC_COEFFTAB		0x0DC
#define SC_OUT_SIZE		0x0E4
#define SC_H_CROP		0x0E8
#define SC_V_CROP		0x0EC
#define SC_H_INIT_PH		0x0F0
#define SC_H_DELTA_PH		0x0F4
#define SC_V_INIT_PH		0x0F8
#define SC_V_DELTA_PH		0x0FC
#define SC_ENH_LIMITS		0x130
#define SC_ENH_COEFF0		0x134

#define SC_MAX_ENH_COEFF	9

/* SC_CTRL_BITS */
#define SC_CTRL_SCL		BIT(0)
#define SC_CTRL_LS		BIT(1)
#define SC_CTRL_AP		BIT(4)
#define SC_CTRL_IENH		BIT(8)
#define SC_CTRL_RGBSM		BIT(16)
#define SC_CTRL_ASM		BIT(17)

#define SC_VTSEL(vtal)		((vtal) << 16)

#define SC_NUM_INPUTS_IDS	1
#define SC_NUM_OUTPUTS_IDS	1

#define MG_NUM_INPUTS_IDS	2
#define MG_NUM_OUTPUTS_IDS	1

/* Merger registers */
#define MG_INPUT_ID0		BLK_INPUT_ID0
#define MG_INPUT_ID1		(MG_INPUT_ID0 + 4)
#define MG_SIZE			BLK_SIZE

/* Splitter registers */
#define SP_OVERLAP_SIZE		0xD8

/* Backend registers */
#define BS_INFO			0x0C0
#define BS_PROG_LINE		0x0D4
#define BS_PREFETCH_LINE	0x0D8
#define BS_BG_COLOR		0x0DC
#define BS_ACTIVESIZE		0x0E0
#define BS_HINTERVALS		0x0E4
#define BS_VINTERVALS		0x0E8
#define BS_SYNC			0x0EC
#define BS_DRIFT_TO		0x100
#define BS_FRAME_TO		0x104
#define BS_TE_TO		0x108
#define BS_T0_INTERVAL		0x110
#define BS_T1_INTERVAL		0x114
#define BS_T2_INTERVAL		0x118
#define BS_CRC0_LOW		0x120
#define BS_CRC0_HIGH		0x124
#define BS_CRC1_LOW		0x128
#define BS_CRC1_HIGH		0x12C
#define BS_USER			0x130

/* BS control register bits */
#define BS_CTRL_EN		BIT(0)
#define BS_CTRL_VM		BIT(1)
#define BS_CTRL_BM		BIT(2)
#define BS_CTRL_HMASK		BIT(4)
#define BS_CTRL_VD		BIT(5)
#define BS_CTRL_TE		BIT(8)
#define BS_CTRL_TS		BIT(9)
#define BS_CTRL_TM		BIT(12)
#define BS_CTRL_DL		BIT(16)
#define BS_CTRL_SBS		BIT(17)
#define BS_CTRL_CRC		BIT(18)
#define BS_CTRL_PM		BIT(20)

/* BS active size/intervals */
#define BS_H_INTVALS(hfp, hbp)	(((hfp) & 0xFFF) + (((hbp) & 0x3FF) << 16))
#define BS_V_INTVALS(vfp, vbp)  (((vfp) & 0x3FFF) + (((vbp) & 0xFF) << 16))

/* BS_SYNC bits */
#define BS_SYNC_HSW(x)		((x) & 0x3FF)
#define BS_SYNC_HSP		BIT(12)
#define BS_SYNC_VSW(x)		(((x) & 0xFF) << 16)
#define BS_SYNC_VSP		BIT(28)

#define BS_NUM_INPUT_IDS	0
#define BS_NUM_OUTPUT_IDS	0

/* Image process registers */
#define IPS_DEPTH		0x0D8
#define IPS_RGB_RGB_COEFF0	0x130
#define IPS_RGB_YUV_COEFF0	0x170

#define IPS_DEPTH_MARK		0xF

/* IPS control register bits */
#define IPS_CTRL_RGB		BIT(0)
#define IPS_CTRL_FT		BIT(4)
#define IPS_CTRL_YUV		BIT(8)
#define IPS_CTRL_CHD422		BIT(9)
#define IPS_CTRL_CHD420		BIT(10)
#define IPS_CTRL_LPF		BIT(11)
#define IPS_CTRL_DITH		BIT(12)
#define IPS_CTRL_CLAMP		BIT(16)
#define IPS_CTRL_SBS		BIT(17)

/* IPS info register bits */
#define IPS_INFO_CHD420		BIT(10)

#define IPS_NUM_INPUT_IDS	2
#define IPS_NUM_OUTPUT_IDS	1

/* FT_COEFF block registers */
#define FT_COEFF0		0x80
#define GLB_IT_COEFF		0x80

/* GLB_SC_COEFF registers */
#define GLB_SC_COEFF_ADDR	0x0080
#define GLB_SC_COEFF_DATA	0x0084
#define GLB_LT_COEFF_DATA	0x0080

#define GLB_SC_COEFF_MAX_NUM	1024
#define GLB_LT_COEFF_NUM	65
/* GLB_SC_ADDR */
#define SC_COEFF_R_ADDR		BIT(18)
#define SC_COEFF_G_ADDR		BIT(17)
#define SC_COEFF_B_ADDR		BIT(16)

#define SC_COEFF_DATA(x, y)	(((y) & 0xFFFF) | (((x) & 0xFFFF) << 16))

enum d71_blk_type {
	D71_BLK_TYPE_GCU		= 0x00,
	D71_BLK_TYPE_LPU		= 0x01,
	D71_BLK_TYPE_CU			= 0x02,
	D71_BLK_TYPE_DOU		= 0x03,
	D71_BLK_TYPE_AEU		= 0x04,
	D71_BLK_TYPE_GLB_LT_COEFF	= 0x05,
	D71_BLK_TYPE_GLB_SCL_COEFF	= 0x06, /* SH/SV scaler coeff */
	D71_BLK_TYPE_GLB_SC_COEFF	= 0x07,
	D71_BLK_TYPE_PERIPH		= 0x08,
	D71_BLK_TYPE_LPU_TRUSTED	= 0x09,
	D71_BLK_TYPE_AEU_TRUSTED	= 0x0A,
	D71_BLK_TYPE_LPU_LAYER		= 0x10,
	D71_BLK_TYPE_LPU_WB_LAYER	= 0x11,
	D71_BLK_TYPE_CU_SPLITTER	= 0x20,
	D71_BLK_TYPE_CU_SCALER		= 0x21,
	D71_BLK_TYPE_CU_MERGER		= 0x22,
	D71_BLK_TYPE_DOU_IPS		= 0x30,
	D71_BLK_TYPE_DOU_BS		= 0x31,
	D71_BLK_TYPE_DOU_FT_COEFF	= 0x32,
	D71_BLK_TYPE_AEU_DS		= 0x40,
	D71_BLK_TYPE_AEU_AES		= 0x41,
	D71_BLK_TYPE_RESERVED		= 0xFF
};

/* Constant of components */
#define D71_MAX_PIPELINE		2
#define D71_PIPELINE_MAX_SCALERS	2
#define D71_PIPELINE_MAX_LAYERS		4

#define D71_MAX_GLB_IT_COEFF		3
#define D71_MAX_GLB_SCL_COEFF		4

#define D71_MAX_LAYERS_PER_LPU		4
#define D71_BLOCK_MAX_INPUT		9
#define D71_BLOCK_MAX_OUTPUT		5
#define D71_MAX_SC_PER_CU		2

#define D71_BLOCK_OFFSET_PERIPH		0xFE00
#define D71_BLOCK_SIZE			0x0200

#define D71_DEFAULT_PREPRETCH_LINE	5
#define D71_BUS_WIDTH_16_BYTES		16

#define D71_SC_MAX_UPSCALING		64
#define D71_SC_MAX_DOWNSCALING		6
#define D71_SC_SPLIT_OVERLAP		8
#define D71_SC_ENH_SPLIT_OVERLAP	1

#define D71_MG_MIN_MERGED_SIZE		4
#define D71_MG_MAX_MERGED_HSIZE		4032
#define D71_MG_MAX_MERGED_VSIZE		4096

#define D71_PALPHA_DEF_MAP		0xFFAA5500
#define D71_LAYER_CONTROL_DEFAULT	0x30000000
#define D71_WB_LAYER_CONTROL_DEFAULT	0x3000FF00
#define D71_BS_CONTROL_DEFAULT		0x00000002

struct block_header {
	u32 block_info;
	u32 pipeline_info;
	u32 input_ids[D71_BLOCK_MAX_INPUT];
	u32 output_ids[D71_BLOCK_MAX_OUTPUT];
};

static inline u32 get_block_type(struct block_header *blk)
{
	return BLOCK_INFO_BLK_TYPE(blk->block_info);
}

#endif /* !_D71_REG_H_ */
