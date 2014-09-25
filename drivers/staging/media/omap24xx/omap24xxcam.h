/*
 * drivers/media/platform/omap24xxcam.h
 *
 * Copyright (C) 2004 MontaVista Software, Inc.
 * Copyright (C) 2004 Texas Instruments.
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *
 * Based on code from Andy Lowe <source@mvista.com>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef OMAP24XXCAM_H
#define OMAP24XXCAM_H

#include <media/videobuf-dma-sg.h>
#include <media/v4l2-device.h>
#include "v4l2-int-device.h"

/*
 *
 * General driver related definitions.
 *
 */

#define CAM_NAME				"omap24xxcam"

#define CAM_MCLK				96000000

/* number of bytes transferred per DMA request */
#define DMA_THRESHOLD				32

/*
 * NUM_CAMDMA_CHANNELS is the number of logical channels provided by
 * the camera DMA controller.
 */
#define NUM_CAMDMA_CHANNELS			4

/*
 * NUM_SG_DMA is the number of scatter-gather DMA transfers that can
 * be queued. (We don't have any overlay sglists now.)
 */
#define NUM_SG_DMA				(VIDEO_MAX_FRAME)

/*
 *
 * Register definitions.
 *
 */

/* subsystem register block offsets */
#define CC_REG_OFFSET				0x00000400
#define CAMDMA_REG_OFFSET			0x00000800
#define CAMMMU_REG_OFFSET			0x00000C00

/* define camera subsystem register offsets */
#define CAM_REVISION				0x000
#define CAM_SYSCONFIG				0x010
#define CAM_SYSSTATUS				0x014
#define CAM_IRQSTATUS				0x018
#define CAM_GPO					0x040
#define CAM_GPI					0x050

/* define camera core register offsets */
#define CC_REVISION				0x000
#define CC_SYSCONFIG				0x010
#define CC_SYSSTATUS				0x014
#define CC_IRQSTATUS				0x018
#define CC_IRQENABLE				0x01C
#define CC_CTRL					0x040
#define CC_CTRL_DMA				0x044
#define CC_CTRL_XCLK				0x048
#define CC_FIFODATA				0x04C
#define CC_TEST					0x050
#define CC_GENPAR				0x054
#define CC_CCPFSCR				0x058
#define CC_CCPFECR				0x05C
#define CC_CCPLSCR				0x060
#define CC_CCPLECR				0x064
#define CC_CCPDFR				0x068

/* define camera dma register offsets */
#define CAMDMA_REVISION				0x000
#define CAMDMA_IRQSTATUS_L0			0x008
#define CAMDMA_IRQSTATUS_L1			0x00C
#define CAMDMA_IRQSTATUS_L2			0x010
#define CAMDMA_IRQSTATUS_L3			0x014
#define CAMDMA_IRQENABLE_L0			0x018
#define CAMDMA_IRQENABLE_L1			0x01C
#define CAMDMA_IRQENABLE_L2			0x020
#define CAMDMA_IRQENABLE_L3			0x024
#define CAMDMA_SYSSTATUS			0x028
#define CAMDMA_OCP_SYSCONFIG			0x02C
#define CAMDMA_CAPS_0				0x064
#define CAMDMA_CAPS_2				0x06C
#define CAMDMA_CAPS_3				0x070
#define CAMDMA_CAPS_4				0x074
#define CAMDMA_GCR				0x078
#define CAMDMA_CCR(n)				(0x080 + (n)*0x60)
#define CAMDMA_CLNK_CTRL(n)			(0x084 + (n)*0x60)
#define CAMDMA_CICR(n)				(0x088 + (n)*0x60)
#define CAMDMA_CSR(n)				(0x08C + (n)*0x60)
#define CAMDMA_CSDP(n)				(0x090 + (n)*0x60)
#define CAMDMA_CEN(n)				(0x094 + (n)*0x60)
#define CAMDMA_CFN(n)				(0x098 + (n)*0x60)
#define CAMDMA_CSSA(n)				(0x09C + (n)*0x60)
#define CAMDMA_CDSA(n)				(0x0A0 + (n)*0x60)
#define CAMDMA_CSEI(n)				(0x0A4 + (n)*0x60)
#define CAMDMA_CSFI(n)				(0x0A8 + (n)*0x60)
#define CAMDMA_CDEI(n)				(0x0AC + (n)*0x60)
#define CAMDMA_CDFI(n)				(0x0B0 + (n)*0x60)
#define CAMDMA_CSAC(n)				(0x0B4 + (n)*0x60)
#define CAMDMA_CDAC(n)				(0x0B8 + (n)*0x60)
#define CAMDMA_CCEN(n)				(0x0BC + (n)*0x60)
#define CAMDMA_CCFN(n)				(0x0C0 + (n)*0x60)
#define CAMDMA_COLOR(n)				(0x0C4 + (n)*0x60)

/* define camera mmu register offsets */
#define CAMMMU_REVISION				0x000
#define CAMMMU_SYSCONFIG			0x010
#define CAMMMU_SYSSTATUS			0x014
#define CAMMMU_IRQSTATUS			0x018
#define CAMMMU_IRQENABLE			0x01C
#define CAMMMU_WALKING_ST			0x040
#define CAMMMU_CNTL				0x044
#define CAMMMU_FAULT_AD				0x048
#define CAMMMU_TTB				0x04C
#define CAMMMU_LOCK				0x050
#define CAMMMU_LD_TLB				0x054
#define CAMMMU_CAM				0x058
#define CAMMMU_RAM				0x05C
#define CAMMMU_GFLUSH				0x060
#define CAMMMU_FLUSH_ENTRY			0x064
#define CAMMMU_READ_CAM				0x068
#define CAMMMU_READ_RAM				0x06C
#define CAMMMU_EMU_FAULT_AD			0x070

/* Define bit fields within selected registers */
#define CAM_REVISION_MAJOR			(15 << 4)
#define CAM_REVISION_MAJOR_SHIFT		4
#define CAM_REVISION_MINOR			(15 << 0)
#define CAM_REVISION_MINOR_SHIFT		0

#define CAM_SYSCONFIG_SOFTRESET			(1 <<  1)
#define CAM_SYSCONFIG_AUTOIDLE			(1 <<  0)

#define CAM_SYSSTATUS_RESETDONE			(1 <<  0)

#define CAM_IRQSTATUS_CC_IRQ			(1 <<  4)
#define CAM_IRQSTATUS_MMU_IRQ			(1 <<  3)
#define CAM_IRQSTATUS_DMA_IRQ2			(1 <<  2)
#define CAM_IRQSTATUS_DMA_IRQ1			(1 <<  1)
#define CAM_IRQSTATUS_DMA_IRQ0			(1 <<  0)

#define CAM_GPO_CAM_S_P_EN			(1 <<  1)
#define CAM_GPO_CAM_CCP_MODE			(1 <<  0)

#define CAM_GPI_CC_DMA_REQ1			(1 << 24)
#define CAP_GPI_CC_DMA_REQ0			(1 << 23)
#define CAP_GPI_CAM_MSTANDBY			(1 << 21)
#define CAP_GPI_CAM_WAIT			(1 << 20)
#define CAP_GPI_CAM_S_DATA			(1 << 17)
#define CAP_GPI_CAM_S_CLK			(1 << 16)
#define CAP_GPI_CAM_P_DATA			(0xFFF << 3)
#define CAP_GPI_CAM_P_DATA_SHIFT		3
#define CAP_GPI_CAM_P_VS			(1 <<  2)
#define CAP_GPI_CAM_P_HS			(1 <<  1)
#define CAP_GPI_CAM_P_CLK			(1 <<  0)

#define CC_REVISION_MAJOR			(15 << 4)
#define CC_REVISION_MAJOR_SHIFT			4
#define CC_REVISION_MINOR			(15 << 0)
#define CC_REVISION_MINOR_SHIFT			0

#define CC_SYSCONFIG_SIDLEMODE			(3 <<  3)
#define CC_SYSCONFIG_SIDLEMODE_FIDLE		(0 <<  3)
#define CC_SYSCONFIG_SIDLEMODE_NIDLE		(1 <<  3)
#define CC_SYSCONFIG_SOFTRESET			(1 <<  1)
#define CC_SYSCONFIG_AUTOIDLE			(1 <<  0)

#define CC_SYSSTATUS_RESETDONE			(1 <<  0)

#define CC_IRQSTATUS_FS_IRQ			(1 << 19)
#define CC_IRQSTATUS_LE_IRQ			(1 << 18)
#define CC_IRQSTATUS_LS_IRQ			(1 << 17)
#define CC_IRQSTATUS_FE_IRQ			(1 << 16)
#define CC_IRQSTATUS_FW_ERR_IRQ			(1 << 10)
#define CC_IRQSTATUS_FSC_ERR_IRQ		(1 <<  9)
#define CC_IRQSTATUS_SSC_ERR_IRQ		(1 <<  8)
#define CC_IRQSTATUS_FIFO_NOEMPTY_IRQ		(1 <<  4)
#define CC_IRQSTATUS_FIFO_FULL_IRQ		(1 <<  3)
#define CC_IRQSTATUS_FIFO_THR_IRQ		(1 <<  2)
#define CC_IRQSTATUS_FIFO_OF_IRQ		(1 <<  1)
#define CC_IRQSTATUS_FIFO_UF_IRQ		(1 <<  0)

#define CC_IRQENABLE_FS_IRQ			(1 << 19)
#define CC_IRQENABLE_LE_IRQ			(1 << 18)
#define CC_IRQENABLE_LS_IRQ			(1 << 17)
#define CC_IRQENABLE_FE_IRQ			(1 << 16)
#define CC_IRQENABLE_FW_ERR_IRQ			(1 << 10)
#define CC_IRQENABLE_FSC_ERR_IRQ		(1 <<  9)
#define CC_IRQENABLE_SSC_ERR_IRQ		(1 <<  8)
#define CC_IRQENABLE_FIFO_NOEMPTY_IRQ		(1 <<  4)
#define CC_IRQENABLE_FIFO_FULL_IRQ		(1 <<  3)
#define CC_IRQENABLE_FIFO_THR_IRQ		(1 <<  2)
#define CC_IRQENABLE_FIFO_OF_IRQ		(1 <<  1)
#define CC_IRQENABLE_FIFO_UF_IRQ		(1 <<  0)

#define CC_CTRL_CC_ONE_SHOT			(1 << 20)
#define CC_CTRL_CC_IF_SYNCHRO			(1 << 19)
#define CC_CTRL_CC_RST				(1 << 18)
#define CC_CTRL_CC_FRAME_TRIG			(1 << 17)
#define CC_CTRL_CC_EN				(1 << 16)
#define CC_CTRL_NOBT_SYNCHRO			(1 << 13)
#define CC_CTRL_BT_CORRECT			(1 << 12)
#define CC_CTRL_PAR_ORDERCAM			(1 << 11)
#define CC_CTRL_PAR_CLK_POL			(1 << 10)
#define CC_CTRL_NOBT_HS_POL			(1 <<  9)
#define CC_CTRL_NOBT_VS_POL			(1 <<  8)
#define CC_CTRL_PAR_MODE			(7 <<  1)
#define CC_CTRL_PAR_MODE_SHIFT			1
#define CC_CTRL_PAR_MODE_NOBT8			(0 <<  1)
#define CC_CTRL_PAR_MODE_NOBT10			(1 <<  1)
#define CC_CTRL_PAR_MODE_NOBT12			(2 <<  1)
#define CC_CTRL_PAR_MODE_BT8			(4 <<  1)
#define CC_CTRL_PAR_MODE_BT10			(5 <<  1)
#define CC_CTRL_PAR_MODE_FIFOTEST		(7 <<  1)
#define CC_CTRL_CCP_MODE			(1 <<  0)

#define CC_CTRL_DMA_EN				(1 <<  8)
#define CC_CTRL_DMA_FIFO_THRESHOLD		(0x7F << 0)
#define CC_CTRL_DMA_FIFO_THRESHOLD_SHIFT	0

#define CC_CTRL_XCLK_DIV			(0x1F << 0)
#define CC_CTRL_XCLK_DIV_SHIFT			0
#define CC_CTRL_XCLK_DIV_STABLE_LOW		(0 <<  0)
#define CC_CTRL_XCLK_DIV_STABLE_HIGH		(1 <<  0)
#define CC_CTRL_XCLK_DIV_BYPASS			(31 << 0)

#define CC_TEST_FIFO_RD_POINTER			(0xFF << 24)
#define CC_TEST_FIFO_RD_POINTER_SHIFT		24
#define CC_TEST_FIFO_WR_POINTER			(0xFF << 16)
#define CC_TEST_FIFO_WR_POINTER_SHIFT		16
#define CC_TEST_FIFO_LEVEL			(0xFF <<  8)
#define CC_TEST_FIFO_LEVEL_SHIFT		8
#define CC_TEST_FIFO_LEVEL_PEAK			(0xFF <<  0)
#define CC_TEST_FIFO_LEVEL_PEAK_SHIFT		0

#define CC_GENPAR_FIFO_DEPTH			(7 <<  0)
#define CC_GENPAR_FIFO_DEPTH_SHIFT		0

#define CC_CCPDFR_ALPHA				(0xFF <<  8)
#define CC_CCPDFR_ALPHA_SHIFT			8
#define CC_CCPDFR_DATAFORMAT			(15 <<  0)
#define CC_CCPDFR_DATAFORMAT_SHIFT		0
#define CC_CCPDFR_DATAFORMAT_YUV422BE		(0 <<  0)
#define CC_CCPDFR_DATAFORMAT_YUV422		(1 <<  0)
#define CC_CCPDFR_DATAFORMAT_YUV420		(2 <<  0)
#define CC_CCPDFR_DATAFORMAT_RGB444		(4 <<  0)
#define CC_CCPDFR_DATAFORMAT_RGB565		(5 <<  0)
#define CC_CCPDFR_DATAFORMAT_RGB888NDE		(6 <<  0)
#define CC_CCPDFR_DATAFORMAT_RGB888		(7 <<  0)
#define CC_CCPDFR_DATAFORMAT_RAW8NDE		(8 <<  0)
#define CC_CCPDFR_DATAFORMAT_RAW8		(9 <<  0)
#define CC_CCPDFR_DATAFORMAT_RAW10NDE		(10 <<  0)
#define CC_CCPDFR_DATAFORMAT_RAW10		(11 <<  0)
#define CC_CCPDFR_DATAFORMAT_RAW12NDE		(12 <<  0)
#define CC_CCPDFR_DATAFORMAT_RAW12		(13 <<  0)
#define CC_CCPDFR_DATAFORMAT_JPEG8		(15 <<  0)

#define CAMDMA_REVISION_MAJOR			(15 << 4)
#define CAMDMA_REVISION_MAJOR_SHIFT		4
#define CAMDMA_REVISION_MINOR			(15 << 0)
#define CAMDMA_REVISION_MINOR_SHIFT		0

#define CAMDMA_OCP_SYSCONFIG_MIDLEMODE		(3 << 12)
#define CAMDMA_OCP_SYSCONFIG_MIDLEMODE_FSTANDBY	(0 << 12)
#define CAMDMA_OCP_SYSCONFIG_MIDLEMODE_NSTANDBY	(1 << 12)
#define CAMDMA_OCP_SYSCONFIG_MIDLEMODE_SSTANDBY	(2 << 12)
#define CAMDMA_OCP_SYSCONFIG_FUNC_CLOCK		(1 <<  9)
#define CAMDMA_OCP_SYSCONFIG_OCP_CLOCK		(1 <<  8)
#define CAMDMA_OCP_SYSCONFIG_EMUFREE		(1 <<  5)
#define CAMDMA_OCP_SYSCONFIG_SIDLEMODE		(3 <<  3)
#define CAMDMA_OCP_SYSCONFIG_SIDLEMODE_FIDLE	(0 <<  3)
#define CAMDMA_OCP_SYSCONFIG_SIDLEMODE_NIDLE	(1 <<  3)
#define CAMDMA_OCP_SYSCONFIG_SIDLEMODE_SIDLE	(2 <<  3)
#define CAMDMA_OCP_SYSCONFIG_SOFTRESET		(1 <<  1)
#define CAMDMA_OCP_SYSCONFIG_AUTOIDLE		(1 <<  0)

#define CAMDMA_SYSSTATUS_RESETDONE		(1 <<  0)

#define CAMDMA_GCR_ARBITRATION_RATE		(0xFF << 16)
#define CAMDMA_GCR_ARBITRATION_RATE_SHIFT	16
#define CAMDMA_GCR_MAX_CHANNEL_FIFO_DEPTH	(0xFF << 0)
#define CAMDMA_GCR_MAX_CHANNEL_FIFO_DEPTH_SHIFT	0

#define CAMDMA_CCR_SEL_SRC_DST_SYNC		(1 << 24)
#define CAMDMA_CCR_PREFETCH			(1 << 23)
#define CAMDMA_CCR_SUPERVISOR			(1 << 22)
#define CAMDMA_CCR_SECURE			(1 << 21)
#define CAMDMA_CCR_BS				(1 << 18)
#define CAMDMA_CCR_TRANSPARENT_COPY_ENABLE	(1 << 17)
#define CAMDMA_CCR_CONSTANT_FILL_ENABLE		(1 << 16)
#define CAMDMA_CCR_DST_AMODE			(3 << 14)
#define CAMDMA_CCR_DST_AMODE_CONST_ADDR		(0 << 14)
#define CAMDMA_CCR_DST_AMODE_POST_INC		(1 << 14)
#define CAMDMA_CCR_DST_AMODE_SGL_IDX		(2 << 14)
#define CAMDMA_CCR_DST_AMODE_DBL_IDX		(3 << 14)
#define CAMDMA_CCR_SRC_AMODE			(3 << 12)
#define CAMDMA_CCR_SRC_AMODE_CONST_ADDR		(0 << 12)
#define CAMDMA_CCR_SRC_AMODE_POST_INC		(1 << 12)
#define CAMDMA_CCR_SRC_AMODE_SGL_IDX		(2 << 12)
#define CAMDMA_CCR_SRC_AMODE_DBL_IDX		(3 << 12)
#define CAMDMA_CCR_WR_ACTIVE			(1 << 10)
#define CAMDMA_CCR_RD_ACTIVE			(1 <<  9)
#define CAMDMA_CCR_SUSPEND_SENSITIVE		(1 <<  8)
#define CAMDMA_CCR_ENABLE			(1 <<  7)
#define CAMDMA_CCR_PRIO				(1 <<  6)
#define CAMDMA_CCR_FS				(1 <<  5)
#define CAMDMA_CCR_SYNCHRO			((3 << 19) | (31 << 0))
#define CAMDMA_CCR_SYNCHRO_CAMERA		0x01

#define CAMDMA_CLNK_CTRL_ENABLE_LNK		(1 << 15)
#define CAMDMA_CLNK_CTRL_NEXTLCH_ID		(0x1F << 0)
#define CAMDMA_CLNK_CTRL_NEXTLCH_ID_SHIFT	0

#define CAMDMA_CICR_MISALIGNED_ERR_IE		(1 << 11)
#define CAMDMA_CICR_SUPERVISOR_ERR_IE		(1 << 10)
#define CAMDMA_CICR_SECURE_ERR_IE		(1 <<  9)
#define CAMDMA_CICR_TRANS_ERR_IE		(1 <<  8)
#define CAMDMA_CICR_PACKET_IE			(1 <<  7)
#define CAMDMA_CICR_BLOCK_IE			(1 <<  5)
#define CAMDMA_CICR_LAST_IE			(1 <<  4)
#define CAMDMA_CICR_FRAME_IE			(1 <<  3)
#define CAMDMA_CICR_HALF_IE			(1 <<  2)
#define CAMDMA_CICR_DROP_IE			(1 <<  1)

#define CAMDMA_CSR_MISALIGNED_ERR		(1 << 11)
#define CAMDMA_CSR_SUPERVISOR_ERR		(1 << 10)
#define CAMDMA_CSR_SECURE_ERR			(1 <<  9)
#define CAMDMA_CSR_TRANS_ERR			(1 <<  8)
#define CAMDMA_CSR_PACKET			(1 <<  7)
#define CAMDMA_CSR_SYNC				(1 <<  6)
#define CAMDMA_CSR_BLOCK			(1 <<  5)
#define CAMDMA_CSR_LAST				(1 <<  4)
#define CAMDMA_CSR_FRAME			(1 <<  3)
#define CAMDMA_CSR_HALF				(1 <<  2)
#define CAMDMA_CSR_DROP				(1 <<  1)

#define CAMDMA_CSDP_SRC_ENDIANNESS		(1 << 21)
#define CAMDMA_CSDP_SRC_ENDIANNESS_LOCK		(1 << 20)
#define CAMDMA_CSDP_DST_ENDIANNESS		(1 << 19)
#define CAMDMA_CSDP_DST_ENDIANNESS_LOCK		(1 << 18)
#define CAMDMA_CSDP_WRITE_MODE			(3 << 16)
#define CAMDMA_CSDP_WRITE_MODE_WRNP		(0 << 16)
#define CAMDMA_CSDP_WRITE_MODE_POSTED		(1 << 16)
#define CAMDMA_CSDP_WRITE_MODE_POSTED_LAST_WRNP	(2 << 16)
#define CAMDMA_CSDP_DST_BURST_EN		(3 << 14)
#define CAMDMA_CSDP_DST_BURST_EN_1		(0 << 14)
#define CAMDMA_CSDP_DST_BURST_EN_16		(1 << 14)
#define CAMDMA_CSDP_DST_BURST_EN_32		(2 << 14)
#define CAMDMA_CSDP_DST_BURST_EN_64		(3 << 14)
#define CAMDMA_CSDP_DST_PACKED			(1 << 13)
#define CAMDMA_CSDP_WR_ADD_TRSLT		(15 << 9)
#define CAMDMA_CSDP_WR_ADD_TRSLT_ENABLE_MREQADD	(3 <<  9)
#define CAMDMA_CSDP_SRC_BURST_EN		(3 <<  7)
#define CAMDMA_CSDP_SRC_BURST_EN_1		(0 <<  7)
#define CAMDMA_CSDP_SRC_BURST_EN_16		(1 <<  7)
#define CAMDMA_CSDP_SRC_BURST_EN_32		(2 <<  7)
#define CAMDMA_CSDP_SRC_BURST_EN_64		(3 <<  7)
#define CAMDMA_CSDP_SRC_PACKED			(1 <<  6)
#define CAMDMA_CSDP_RD_ADD_TRSLT		(15 << 2)
#define CAMDMA_CSDP_RD_ADD_TRSLT_ENABLE_MREQADD	(3 <<  2)
#define CAMDMA_CSDP_DATA_TYPE			(3 <<  0)
#define CAMDMA_CSDP_DATA_TYPE_8BITS		(0 <<  0)
#define CAMDMA_CSDP_DATA_TYPE_16BITS		(1 <<  0)
#define CAMDMA_CSDP_DATA_TYPE_32BITS		(2 <<  0)

#define CAMMMU_SYSCONFIG_AUTOIDLE		(1 <<  0)

/*
 *
 * Declarations.
 *
 */

/* forward declarations */
struct omap24xxcam_sgdma;
struct omap24xxcam_dma;

typedef void (*sgdma_callback_t)(struct omap24xxcam_sgdma *cam,
				 u32 status, void *arg);
typedef void (*dma_callback_t)(struct omap24xxcam_dma *cam,
			       u32 status, void *arg);

struct channel_state {
	dma_callback_t callback;
	void *arg;
};

/* sgdma state for each of the possible videobuf_buffers + 2 overlays */
struct sgdma_state {
	const struct scatterlist *sglist;
	int sglen;		 /* number of sglist entries */
	int next_sglist;	 /* index of next sglist entry to process */
	unsigned int bytes_read; /* number of bytes read */
	unsigned int len;        /* total length of sglist (excluding
				  * bytes due to page alignment) */
	int queued_sglist;	 /* number of sglist entries queued for DMA */
	u32 csr;		 /* DMA return code */
	sgdma_callback_t callback;
	void *arg;
};

/* physical DMA channel management */
struct omap24xxcam_dma {
	spinlock_t lock;	/* Lock for the whole structure. */

	void __iomem *base;	/* base address for dma controller */

	/* While dma_stop!=0, an attempt to start a new DMA transfer will
	 * fail.
	 */
	atomic_t dma_stop;
	int free_dmach;		/* number of dma channels free */
	int next_dmach;		/* index of next dma channel to use */
	struct channel_state ch_state[NUM_CAMDMA_CHANNELS];
};

/* scatter-gather DMA (scatterlist stuff) management */
struct omap24xxcam_sgdma {
	struct omap24xxcam_dma dma;

	spinlock_t lock;	/* Lock for the fields below. */
	int free_sgdma;		/* number of free sg dma slots */
	int next_sgdma;		/* index of next sg dma slot to use */
	struct sgdma_state sg_state[NUM_SG_DMA];

	/* Reset timer data */
	struct timer_list reset_timer;
};

/* per-device data structure */
struct omap24xxcam_device {
	/*** mutex  ***/
	/*
	 * mutex serialises access to this structure. Also camera
	 * opening and releasing is synchronised by this.
	 */
	struct mutex mutex;

	struct v4l2_device v4l2_dev;

	/*** general driver state information ***/
	atomic_t users;
	/*
	 * Lock to serialise core enabling and disabling and access to
	 * sgdma_in_queue.
	 */
	spinlock_t core_enable_disable_lock;
	/*
	 * Number or sgdma requests in scatter-gather queue, protected
	 * by the lock above.
	 */
	int sgdma_in_queue;
	/*
	 * Sensor interface parameters: interface type, CC_CTRL
	 * register value and interface specific data.
	 */
	int if_type;
	union {
		struct parallel {
			u32 xclk;
		} bt656;
	} if_u;
	u32 cc_ctrl;

	/*** subsystem structures ***/
	struct omap24xxcam_sgdma sgdma;

	/*** hardware resources ***/
	unsigned int irq;
	void __iomem *mmio_base;
	unsigned long mmio_base_phys;
	unsigned long mmio_size;

	/*** interfaces and device ***/
	struct v4l2_int_device *sdev;
	struct device *dev;
	struct video_device *vfd;

	/*** camera and sensor reset related stuff ***/
	struct work_struct sensor_reset_work;
	/*
	 * We're in the middle of a reset. Don't enable core if this
	 * is non-zero! This exists to help decisionmaking in a case
	 * where videobuf_qbuf is called while we are in the middle of
	 * a reset.
	 */
	atomic_t in_reset;
	/*
	 * Non-zero if we don't want any resets for now. Used to
	 * prevent reset work to run when we're about to stop
	 * streaming.
	 */
	atomic_t reset_disable;

	/*** video device parameters ***/
	int capture_mem;

	/*** camera module clocks ***/
	struct clk *fck;
	struct clk *ick;

	/*** capture data ***/
	/* file handle, if streaming is on */
	struct file *streaming;
};

/* Per-file handle data. */
struct omap24xxcam_fh {
	spinlock_t vbq_lock; /* spinlock for the videobuf queue */
	struct videobuf_queue vbq;
	struct v4l2_pix_format pix; /* serialise pix by vbq->lock */
	atomic_t field_count; /* field counter for videobuf_buffer */
	/* accessing cam here doesn't need serialisation: it's constant */
	struct omap24xxcam_device *cam;
};

/*
 *
 * Register I/O functions.
 *
 */

static inline u32 omap24xxcam_reg_in(u32 __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline u32 omap24xxcam_reg_out(u32 __iomem *base, u32 offset,
					  u32 val)
{
	writel(val, base + offset);
	return val;
}

static inline u32 omap24xxcam_reg_merge(u32 __iomem *base, u32 offset,
					    u32 val, u32 mask)
{
	u32 __iomem *addr = base + offset;
	u32 new_val = (readl(addr) & ~mask) | (val & mask);

	writel(new_val, addr);
	return new_val;
}

/*
 *
 * Function prototypes.
 *
 */

/* dma prototypes */

void omap24xxcam_dma_hwinit(struct omap24xxcam_dma *dma);
void omap24xxcam_dma_isr(struct omap24xxcam_dma *dma);

/* sgdma prototypes */

void omap24xxcam_sgdma_process(struct omap24xxcam_sgdma *sgdma);
int omap24xxcam_sgdma_queue(struct omap24xxcam_sgdma *sgdma,
			    const struct scatterlist *sglist, int sglen,
			    int len, sgdma_callback_t callback, void *arg);
void omap24xxcam_sgdma_sync(struct omap24xxcam_sgdma *sgdma);
void omap24xxcam_sgdma_init(struct omap24xxcam_sgdma *sgdma,
			    void __iomem *base,
			    void (*reset_callback)(unsigned long data),
			    unsigned long reset_callback_data);
void omap24xxcam_sgdma_exit(struct omap24xxcam_sgdma *sgdma);

#endif
