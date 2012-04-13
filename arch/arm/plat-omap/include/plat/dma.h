/*
 *  arch/arm/plat-omap/include/mach/dma.h
 *
 *  Copyright (C) 2003 Nokia Corporation
 *  Author: Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/platform_device.h>

/*
 * TODO: These dma channel defines should go away once all
 * the omap drivers hwmod adapted.
 */

/* Move omap4 specific defines to dma-44xx.h */
#include "dma-44xx.h"

/* DMA channels for omap1 */
#define OMAP_DMA_NO_DEVICE		0
#define OMAP_DMA_MCSI1_TX		1
#define OMAP_DMA_MCSI1_RX		2
#define OMAP_DMA_I2C_RX			3
#define OMAP_DMA_I2C_TX			4
#define OMAP_DMA_EXT_NDMA_REQ		5
#define OMAP_DMA_EXT_NDMA_REQ2		6
#define OMAP_DMA_UWIRE_TX		7
#define OMAP_DMA_MCBSP1_TX		8
#define OMAP_DMA_MCBSP1_RX		9
#define OMAP_DMA_MCBSP3_TX		10
#define OMAP_DMA_MCBSP3_RX		11
#define OMAP_DMA_UART1_TX		12
#define OMAP_DMA_UART1_RX		13
#define OMAP_DMA_UART2_TX		14
#define OMAP_DMA_UART2_RX		15
#define OMAP_DMA_MCBSP2_TX		16
#define OMAP_DMA_MCBSP2_RX		17
#define OMAP_DMA_UART3_TX		18
#define OMAP_DMA_UART3_RX		19
#define OMAP_DMA_CAMERA_IF_RX		20
#define OMAP_DMA_MMC_TX			21
#define OMAP_DMA_MMC_RX			22
#define OMAP_DMA_NAND			23
#define OMAP_DMA_IRQ_LCD_LINE		24
#define OMAP_DMA_MEMORY_STICK		25
#define OMAP_DMA_USB_W2FC_RX0		26
#define OMAP_DMA_USB_W2FC_RX1		27
#define OMAP_DMA_USB_W2FC_RX2		28
#define OMAP_DMA_USB_W2FC_TX0		29
#define OMAP_DMA_USB_W2FC_TX1		30
#define OMAP_DMA_USB_W2FC_TX2		31

/* These are only for 1610 */
#define OMAP_DMA_CRYPTO_DES_IN		32
#define OMAP_DMA_SPI_TX			33
#define OMAP_DMA_SPI_RX			34
#define OMAP_DMA_CRYPTO_HASH		35
#define OMAP_DMA_CCP_ATTN		36
#define OMAP_DMA_CCP_FIFO_NOT_EMPTY	37
#define OMAP_DMA_CMT_APE_TX_CHAN_0	38
#define OMAP_DMA_CMT_APE_RV_CHAN_0	39
#define OMAP_DMA_CMT_APE_TX_CHAN_1	40
#define OMAP_DMA_CMT_APE_RV_CHAN_1	41
#define OMAP_DMA_CMT_APE_TX_CHAN_2	42
#define OMAP_DMA_CMT_APE_RV_CHAN_2	43
#define OMAP_DMA_CMT_APE_TX_CHAN_3	44
#define OMAP_DMA_CMT_APE_RV_CHAN_3	45
#define OMAP_DMA_CMT_APE_TX_CHAN_4	46
#define OMAP_DMA_CMT_APE_RV_CHAN_4	47
#define OMAP_DMA_CMT_APE_TX_CHAN_5	48
#define OMAP_DMA_CMT_APE_RV_CHAN_5	49
#define OMAP_DMA_CMT_APE_TX_CHAN_6	50
#define OMAP_DMA_CMT_APE_RV_CHAN_6	51
#define OMAP_DMA_CMT_APE_TX_CHAN_7	52
#define OMAP_DMA_CMT_APE_RV_CHAN_7	53
#define OMAP_DMA_MMC2_TX		54
#define OMAP_DMA_MMC2_RX		55
#define OMAP_DMA_CRYPTO_DES_OUT		56

/* DMA channels for 24xx */
#define OMAP24XX_DMA_NO_DEVICE		0
#define OMAP24XX_DMA_XTI_DMA		1	/* S_DMA_0 */
#define OMAP24XX_DMA_EXT_DMAREQ0	2	/* S_DMA_1 */
#define OMAP24XX_DMA_EXT_DMAREQ1	3	/* S_DMA_2 */
#define OMAP24XX_DMA_GPMC		4	/* S_DMA_3 */
#define OMAP24XX_DMA_GFX		5	/* S_DMA_4 */
#define OMAP24XX_DMA_DSS		6	/* S_DMA_5 */
#define OMAP242X_DMA_VLYNQ_TX		7	/* S_DMA_6 */
#define OMAP24XX_DMA_EXT_DMAREQ2	7	/* S_DMA_6 */
#define OMAP24XX_DMA_CWT		8	/* S_DMA_7 */
#define OMAP24XX_DMA_AES_TX		9	/* S_DMA_8 */
#define OMAP24XX_DMA_AES_RX		10	/* S_DMA_9 */
#define OMAP24XX_DMA_DES_TX		11	/* S_DMA_10 */
#define OMAP24XX_DMA_DES_RX		12	/* S_DMA_11 */
#define OMAP24XX_DMA_SHA1MD5_RX		13	/* S_DMA_12 */
#define OMAP34XX_DMA_SHA2MD5_RX		13	/* S_DMA_12 */
#define OMAP242X_DMA_EXT_DMAREQ2	14	/* S_DMA_13 */
#define OMAP242X_DMA_EXT_DMAREQ3	15	/* S_DMA_14 */
#define OMAP242X_DMA_EXT_DMAREQ4	16	/* S_DMA_15 */
#define OMAP242X_DMA_EAC_AC_RD		17	/* S_DMA_16 */
#define OMAP242X_DMA_EAC_AC_WR		18	/* S_DMA_17 */
#define OMAP242X_DMA_EAC_MD_UL_RD	19	/* S_DMA_18 */
#define OMAP242X_DMA_EAC_MD_UL_WR	20	/* S_DMA_19 */
#define OMAP242X_DMA_EAC_MD_DL_RD	21	/* S_DMA_20 */
#define OMAP242X_DMA_EAC_MD_DL_WR	22	/* S_DMA_21 */
#define OMAP242X_DMA_EAC_BT_UL_RD	23	/* S_DMA_22 */
#define OMAP242X_DMA_EAC_BT_UL_WR	24	/* S_DMA_23 */
#define OMAP242X_DMA_EAC_BT_DL_RD	25	/* S_DMA_24 */
#define OMAP242X_DMA_EAC_BT_DL_WR	26	/* S_DMA_25 */
#define OMAP243X_DMA_EXT_DMAREQ3	14	/* S_DMA_13 */
#define OMAP24XX_DMA_SPI3_TX0		15	/* S_DMA_14 */
#define OMAP24XX_DMA_SPI3_RX0		16	/* S_DMA_15 */
#define OMAP24XX_DMA_MCBSP3_TX		17	/* S_DMA_16 */
#define OMAP24XX_DMA_MCBSP3_RX		18	/* S_DMA_17 */
#define OMAP24XX_DMA_MCBSP4_TX		19	/* S_DMA_18 */
#define OMAP24XX_DMA_MCBSP4_RX		20	/* S_DMA_19 */
#define OMAP24XX_DMA_MCBSP5_TX		21	/* S_DMA_20 */
#define OMAP24XX_DMA_MCBSP5_RX		22	/* S_DMA_21 */
#define OMAP24XX_DMA_SPI3_TX1		23	/* S_DMA_22 */
#define OMAP24XX_DMA_SPI3_RX1		24	/* S_DMA_23 */
#define OMAP243X_DMA_EXT_DMAREQ4	25	/* S_DMA_24 */
#define OMAP243X_DMA_EXT_DMAREQ5	26	/* S_DMA_25 */
#define OMAP34XX_DMA_I2C3_TX		25	/* S_DMA_24 */
#define OMAP34XX_DMA_I2C3_RX		26	/* S_DMA_25 */
#define OMAP24XX_DMA_I2C1_TX		27	/* S_DMA_26 */
#define OMAP24XX_DMA_I2C1_RX		28	/* S_DMA_27 */
#define OMAP24XX_DMA_I2C2_TX		29	/* S_DMA_28 */
#define OMAP24XX_DMA_I2C2_RX		30	/* S_DMA_29 */
#define OMAP24XX_DMA_MCBSP1_TX		31	/* S_DMA_30 */
#define OMAP24XX_DMA_MCBSP1_RX		32	/* S_DMA_31 */
#define OMAP24XX_DMA_MCBSP2_TX		33	/* S_DMA_32 */
#define OMAP24XX_DMA_MCBSP2_RX		34	/* S_DMA_33 */
#define OMAP24XX_DMA_SPI1_TX0		35	/* S_DMA_34 */
#define OMAP24XX_DMA_SPI1_RX0		36	/* S_DMA_35 */
#define OMAP24XX_DMA_SPI1_TX1		37	/* S_DMA_36 */
#define OMAP24XX_DMA_SPI1_RX1		38	/* S_DMA_37 */
#define OMAP24XX_DMA_SPI1_TX2		39	/* S_DMA_38 */
#define OMAP24XX_DMA_SPI1_RX2		40	/* S_DMA_39 */
#define OMAP24XX_DMA_SPI1_TX3		41	/* S_DMA_40 */
#define OMAP24XX_DMA_SPI1_RX3		42	/* S_DMA_41 */
#define OMAP24XX_DMA_SPI2_TX0		43	/* S_DMA_42 */
#define OMAP24XX_DMA_SPI2_RX0		44	/* S_DMA_43 */
#define OMAP24XX_DMA_SPI2_TX1		45	/* S_DMA_44 */
#define OMAP24XX_DMA_SPI2_RX1		46	/* S_DMA_45 */
#define OMAP24XX_DMA_MMC2_TX		47	/* S_DMA_46 */
#define OMAP24XX_DMA_MMC2_RX		48	/* S_DMA_47 */
#define OMAP24XX_DMA_UART1_TX		49	/* S_DMA_48 */
#define OMAP24XX_DMA_UART1_RX		50	/* S_DMA_49 */
#define OMAP24XX_DMA_UART2_TX		51	/* S_DMA_50 */
#define OMAP24XX_DMA_UART2_RX		52	/* S_DMA_51 */
#define OMAP24XX_DMA_UART3_TX		53	/* S_DMA_52 */
#define OMAP24XX_DMA_UART3_RX		54	/* S_DMA_53 */
#define OMAP24XX_DMA_USB_W2FC_TX0	55	/* S_DMA_54 */
#define OMAP24XX_DMA_USB_W2FC_RX0	56	/* S_DMA_55 */
#define OMAP24XX_DMA_USB_W2FC_TX1	57	/* S_DMA_56 */
#define OMAP24XX_DMA_USB_W2FC_RX1	58	/* S_DMA_57 */
#define OMAP24XX_DMA_USB_W2FC_TX2	59	/* S_DMA_58 */
#define OMAP24XX_DMA_USB_W2FC_RX2	60	/* S_DMA_59 */
#define OMAP24XX_DMA_MMC1_TX		61	/* S_DMA_60 */
#define OMAP24XX_DMA_MMC1_RX		62	/* S_DMA_61 */
#define OMAP24XX_DMA_MS			63	/* S_DMA_62 */
#define OMAP242X_DMA_EXT_DMAREQ5	64	/* S_DMA_63 */
#define OMAP243X_DMA_EXT_DMAREQ6	64	/* S_DMA_63 */
#define OMAP34XX_DMA_EXT_DMAREQ3	64	/* S_DMA_63 */
#define OMAP34XX_DMA_AES2_TX		65	/* S_DMA_64 */
#define OMAP34XX_DMA_AES2_RX		66	/* S_DMA_65 */
#define OMAP34XX_DMA_DES2_TX		67	/* S_DMA_66 */
#define OMAP34XX_DMA_DES2_RX		68	/* S_DMA_67 */
#define OMAP34XX_DMA_SHA1MD5_RX		69	/* S_DMA_68 */
#define OMAP34XX_DMA_SPI4_TX0		70	/* S_DMA_69 */
#define OMAP34XX_DMA_SPI4_RX0		71	/* S_DMA_70 */
#define OMAP34XX_DSS_DMA0		72	/* S_DMA_71 */
#define OMAP34XX_DSS_DMA1		73	/* S_DMA_72 */
#define OMAP34XX_DSS_DMA2		74	/* S_DMA_73 */
#define OMAP34XX_DSS_DMA3		75	/* S_DMA_74 */
#define OMAP34XX_DMA_MMC3_TX		77	/* S_DMA_76 */
#define OMAP34XX_DMA_MMC3_RX		78	/* S_DMA_77 */
#define OMAP34XX_DMA_USIM_TX		79	/* S_DMA_78 */
#define OMAP34XX_DMA_USIM_RX		80	/* S_DMA_79 */

#define OMAP36XX_DMA_UART4_TX		81	/* S_DMA_80 */
#define OMAP36XX_DMA_UART4_RX		82	/* S_DMA_81 */

/* Only for AM35xx */
#define AM35XX_DMA_UART4_TX		54
#define AM35XX_DMA_UART4_RX		55

/*----------------------------------------------------------------------------*/

#define OMAP1_DMA_TOUT_IRQ		(1 << 0)
#define OMAP_DMA_DROP_IRQ		(1 << 1)
#define OMAP_DMA_HALF_IRQ		(1 << 2)
#define OMAP_DMA_FRAME_IRQ		(1 << 3)
#define OMAP_DMA_LAST_IRQ		(1 << 4)
#define OMAP_DMA_BLOCK_IRQ		(1 << 5)
#define OMAP1_DMA_SYNC_IRQ		(1 << 6)
#define OMAP2_DMA_PKT_IRQ		(1 << 7)
#define OMAP2_DMA_TRANS_ERR_IRQ		(1 << 8)
#define OMAP2_DMA_SECURE_ERR_IRQ	(1 << 9)
#define OMAP2_DMA_SUPERVISOR_ERR_IRQ	(1 << 10)
#define OMAP2_DMA_MISALIGNED_ERR_IRQ	(1 << 11)

#define OMAP_DMA_CCR_EN			(1 << 7)
#define OMAP_DMA_CCR_RD_ACTIVE		(1 << 9)
#define OMAP_DMA_CCR_WR_ACTIVE		(1 << 10)
#define OMAP_DMA_CCR_SEL_SRC_DST_SYNC	(1 << 24)
#define OMAP_DMA_CCR_BUFFERING_DISABLE	(1 << 25)

#define OMAP_DMA_DATA_TYPE_S8		0x00
#define OMAP_DMA_DATA_TYPE_S16		0x01
#define OMAP_DMA_DATA_TYPE_S32		0x02

#define OMAP_DMA_SYNC_ELEMENT		0x00
#define OMAP_DMA_SYNC_FRAME		0x01
#define OMAP_DMA_SYNC_BLOCK		0x02
#define OMAP_DMA_SYNC_PACKET		0x03

#define OMAP_DMA_DST_SYNC_PREFETCH	0x02
#define OMAP_DMA_SRC_SYNC		0x01
#define OMAP_DMA_DST_SYNC		0x00

#define OMAP_DMA_PORT_EMIFF		0x00
#define OMAP_DMA_PORT_EMIFS		0x01
#define OMAP_DMA_PORT_OCP_T1		0x02
#define OMAP_DMA_PORT_TIPB		0x03
#define OMAP_DMA_PORT_OCP_T2		0x04
#define OMAP_DMA_PORT_MPUI		0x05

#define OMAP_DMA_AMODE_CONSTANT		0x00
#define OMAP_DMA_AMODE_POST_INC		0x01
#define OMAP_DMA_AMODE_SINGLE_IDX	0x02
#define OMAP_DMA_AMODE_DOUBLE_IDX	0x03

#define DMA_DEFAULT_FIFO_DEPTH		0x10
#define DMA_DEFAULT_ARB_RATE		0x01
/* Pass THREAD_RESERVE ORed with THREAD_FIFO for tparams */
#define DMA_THREAD_RESERVE_NORM		(0x00 << 12) /* Def */
#define DMA_THREAD_RESERVE_ONET		(0x01 << 12)
#define DMA_THREAD_RESERVE_TWOT		(0x02 << 12)
#define DMA_THREAD_RESERVE_THREET	(0x03 << 12)
#define DMA_THREAD_FIFO_NONE		(0x00 << 14) /* Def */
#define DMA_THREAD_FIFO_75		(0x01 << 14)
#define DMA_THREAD_FIFO_25		(0x02 << 14)
#define DMA_THREAD_FIFO_50		(0x03 << 14)

/* DMA4_OCP_SYSCONFIG bits */
#define DMA_SYSCONFIG_MIDLEMODE_MASK		(3 << 12)
#define DMA_SYSCONFIG_CLOCKACTIVITY_MASK	(3 << 8)
#define DMA_SYSCONFIG_EMUFREE			(1 << 5)
#define DMA_SYSCONFIG_SIDLEMODE_MASK		(3 << 3)
#define DMA_SYSCONFIG_SOFTRESET			(1 << 2)
#define DMA_SYSCONFIG_AUTOIDLE			(1 << 0)

#define DMA_SYSCONFIG_MIDLEMODE(n)		((n) << 12)
#define DMA_SYSCONFIG_SIDLEMODE(n)		((n) << 3)

#define DMA_IDLEMODE_SMARTIDLE			0x2
#define DMA_IDLEMODE_NO_IDLE			0x1
#define DMA_IDLEMODE_FORCE_IDLE			0x0

/* Chaining modes*/
#ifndef CONFIG_ARCH_OMAP1
#define OMAP_DMA_STATIC_CHAIN		0x1
#define OMAP_DMA_DYNAMIC_CHAIN		0x2
#define OMAP_DMA_CHAIN_ACTIVE		0x1
#define OMAP_DMA_CHAIN_INACTIVE		0x0
#endif

#define DMA_CH_PRIO_HIGH		0x1
#define DMA_CH_PRIO_LOW			0x0 /* Def */

/* Errata handling */
#define IS_DMA_ERRATA(id)		(errata & (id))
#define SET_DMA_ERRATA(id)		(errata |= (id))

#define DMA_ERRATA_IFRAME_BUFFERING	BIT(0x0)
#define DMA_ERRATA_PARALLEL_CHANNELS	BIT(0x1)
#define DMA_ERRATA_i378			BIT(0x2)
#define DMA_ERRATA_i541			BIT(0x3)
#define DMA_ERRATA_i88			BIT(0x4)
#define DMA_ERRATA_3_3			BIT(0x5)
#define DMA_ROMCODE_BUG			BIT(0x6)

/* Attributes for OMAP DMA Contrller */
#define DMA_LINKED_LCH			BIT(0x0)
#define GLOBAL_PRIORITY			BIT(0x1)
#define RESERVE_CHANNEL			BIT(0x2)
#define IS_CSSA_32			BIT(0x3)
#define IS_CDSA_32			BIT(0x4)
#define IS_RW_PRIORITY			BIT(0x5)
#define ENABLE_1510_MODE		BIT(0x6)
#define SRC_PORT			BIT(0x7)
#define DST_PORT			BIT(0x8)
#define SRC_INDEX			BIT(0x9)
#define DST_INDEX			BIT(0xA)
#define IS_BURST_ONLY4			BIT(0xB)
#define CLEAR_CSR_ON_READ		BIT(0xC)
#define IS_WORD_16			BIT(0xD)

enum omap_reg_offsets {

GCR,		GSCR,		GRST1,		HW_ID,
PCH2_ID,	PCH0_ID,	PCH1_ID,	PCHG_ID,
PCHD_ID,	CAPS_0,		CAPS_1,		CAPS_2,
CAPS_3,		CAPS_4,		PCH2_SR,	PCH0_SR,
PCH1_SR,	PCHD_SR,	REVISION,	IRQSTATUS_L0,
IRQSTATUS_L1,	IRQSTATUS_L2,	IRQSTATUS_L3,	IRQENABLE_L0,
IRQENABLE_L1,	IRQENABLE_L2,	IRQENABLE_L3,	SYSSTATUS,
OCP_SYSCONFIG,

/* omap1+ specific */
CPC, CCR2, LCH_CTRL,

/* Common registers for all omap's */
CSDP,		CCR,		CICR,		CSR,
CEN,		CFN,		CSFI,		CSEI,
CSAC,		CDAC,		CDEI,
CDFI,		CLNK_CTRL,

/* Channel specific registers */
CSSA,		CDSA,		COLOR,
CCEN,		CCFN,

/* omap3630 and omap4 specific */
CDP,		CNDP,		CCDN,

};

enum omap_dma_burst_mode {
	OMAP_DMA_DATA_BURST_DIS = 0,
	OMAP_DMA_DATA_BURST_4,
	OMAP_DMA_DATA_BURST_8,
	OMAP_DMA_DATA_BURST_16,
};

enum end_type {
	OMAP_DMA_LITTLE_ENDIAN = 0,
	OMAP_DMA_BIG_ENDIAN
};

enum omap_dma_color_mode {
	OMAP_DMA_COLOR_DIS = 0,
	OMAP_DMA_CONSTANT_FILL,
	OMAP_DMA_TRANSPARENT_COPY
};

enum omap_dma_write_mode {
	OMAP_DMA_WRITE_NON_POSTED = 0,
	OMAP_DMA_WRITE_POSTED,
	OMAP_DMA_WRITE_LAST_NON_POSTED
};

enum omap_dma_channel_mode {
	OMAP_DMA_LCH_2D = 0,
	OMAP_DMA_LCH_G,
	OMAP_DMA_LCH_P,
	OMAP_DMA_LCH_PD
};

struct omap_dma_channel_params {
	int data_type;		/* data type 8,16,32 */
	int elem_count;		/* number of elements in a frame */
	int frame_count;	/* number of frames in a element */

	int src_port;		/* Only on OMAP1 REVISIT: Is this needed? */
	int src_amode;		/* constant, post increment, indexed,
					double indexed */
	unsigned long src_start;	/* source address : physical */
	int src_ei;		/* source element index */
	int src_fi;		/* source frame index */

	int dst_port;		/* Only on OMAP1 REVISIT: Is this needed? */
	int dst_amode;		/* constant, post increment, indexed,
					double indexed */
	unsigned long dst_start;	/* source address : physical */
	int dst_ei;		/* source element index */
	int dst_fi;		/* source frame index */

	int trigger;		/* trigger attached if the channel is
					synchronized */
	int sync_mode;		/* sycn on element, frame , block or packet */
	int src_or_dst_synch;	/* source synch(1) or destination synch(0) */

	int ie;			/* interrupt enabled */

	unsigned char read_prio;/* read priority */
	unsigned char write_prio;/* write priority */

#ifndef CONFIG_ARCH_OMAP1
	enum omap_dma_burst_mode burst_mode; /* Burst mode 4/8/16 words */
#endif
};

struct omap_dma_lch {
	int next_lch;
	int dev_id;
	u16 saved_csr;
	u16 enabled_irqs;
	const char *dev_name;
	void (*callback)(int lch, u16 ch_status, void *data);
	void *data;
	long flags;
	/* required for Dynamic chaining */
	int prev_linked_ch;
	int next_linked_ch;
	int state;
	int chain_id;
	int status;
};

struct omap_dma_dev_attr {
	u32 dev_caps;
	u16 lch_count;
	u16 chan_count;
	struct omap_dma_lch *chan;
};

/* System DMA platform data structure */
struct omap_system_dma_plat_info {
	struct omap_dma_dev_attr *dma_attr;
	u32 errata;
	void (*disable_irq_lch)(int lch);
	void (*show_dma_caps)(void);
	void (*clear_lch_regs)(int lch);
	void (*clear_dma)(int lch);
	void (*dma_write)(u32 val, int reg, int lch);
	u32 (*dma_read)(int reg, int lch);
};

extern void __init omap_init_consistent_dma_size(void);
extern void omap_set_dma_priority(int lch, int dst_port, int priority);
extern int omap_request_dma(int dev_id, const char *dev_name,
			void (*callback)(int lch, u16 ch_status, void *data),
			void *data, int *dma_ch);
extern void omap_enable_dma_irq(int ch, u16 irq_bits);
extern void omap_disable_dma_irq(int ch, u16 irq_bits);
extern void omap_free_dma(int ch);
extern void omap_start_dma(int lch);
extern void omap_stop_dma(int lch);
extern void omap_set_dma_transfer_params(int lch, int data_type,
					 int elem_count, int frame_count,
					 int sync_mode,
					 int dma_trigger, int src_or_dst_synch);
extern void omap_set_dma_color_mode(int lch, enum omap_dma_color_mode mode,
				    u32 color);
extern void omap_set_dma_write_mode(int lch, enum omap_dma_write_mode mode);
extern void omap_set_dma_channel_mode(int lch, enum omap_dma_channel_mode mode);

extern void omap_set_dma_src_params(int lch, int src_port, int src_amode,
				    unsigned long src_start,
				    int src_ei, int src_fi);
extern void omap_set_dma_src_index(int lch, int eidx, int fidx);
extern void omap_set_dma_src_data_pack(int lch, int enable);
extern void omap_set_dma_src_burst_mode(int lch,
					enum omap_dma_burst_mode burst_mode);

extern void omap_set_dma_dest_params(int lch, int dest_port, int dest_amode,
				     unsigned long dest_start,
				     int dst_ei, int dst_fi);
extern void omap_set_dma_dest_index(int lch, int eidx, int fidx);
extern void omap_set_dma_dest_data_pack(int lch, int enable);
extern void omap_set_dma_dest_burst_mode(int lch,
					 enum omap_dma_burst_mode burst_mode);

extern void omap_set_dma_params(int lch,
				struct omap_dma_channel_params *params);

extern void omap_dma_link_lch(int lch_head, int lch_queue);
extern void omap_dma_unlink_lch(int lch_head, int lch_queue);

extern int omap_set_dma_callback(int lch,
			void (*callback)(int lch, u16 ch_status, void *data),
			void *data);
extern dma_addr_t omap_get_dma_src_pos(int lch);
extern dma_addr_t omap_get_dma_dst_pos(int lch);
extern void omap_clear_dma(int lch);
extern int omap_get_dma_active_status(int lch);
extern int omap_dma_running(void);
extern void omap_dma_set_global_params(int arb_rate, int max_fifo_depth,
				       int tparams);
extern int omap_dma_set_prio_lch(int lch, unsigned char read_prio,
				 unsigned char write_prio);
extern void omap_set_dma_dst_endian_type(int lch, enum end_type etype);
extern void omap_set_dma_src_endian_type(int lch, enum end_type etype);
extern int omap_get_dma_index(int lch, int *ei, int *fi);

void omap_dma_global_context_save(void);
void omap_dma_global_context_restore(void);

extern void omap_dma_disable_irq(int lch);

/* Chaining APIs */
#ifndef CONFIG_ARCH_OMAP1
extern int omap_request_dma_chain(int dev_id, const char *dev_name,
				  void (*callback) (int lch, u16 ch_status,
						    void *data),
				  int *chain_id, int no_of_chans,
				  int chain_mode,
				  struct omap_dma_channel_params params);
extern int omap_free_dma_chain(int chain_id);
extern int omap_dma_chain_a_transfer(int chain_id, int src_start,
				     int dest_start, int elem_count,
				     int frame_count, void *callbk_data);
extern int omap_start_dma_chain_transfers(int chain_id);
extern int omap_stop_dma_chain_transfers(int chain_id);
extern int omap_get_dma_chain_index(int chain_id, int *ei, int *fi);
extern int omap_get_dma_chain_dst_pos(int chain_id);
extern int omap_get_dma_chain_src_pos(int chain_id);

extern int omap_modify_dma_chain_params(int chain_id,
					struct omap_dma_channel_params params);
extern int omap_dma_chain_status(int chain_id);
#endif

#if defined(CONFIG_ARCH_OMAP1) && defined(CONFIG_FB_OMAP)
#include <mach/lcd_dma.h>
#else
static inline int omap_lcd_dma_running(void)
{
	return 0;
}
#endif

#endif /* __ASM_ARCH_DMA_H */
