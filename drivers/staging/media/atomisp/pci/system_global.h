/* SPDX-License-Identifier: GPL-2.0 */
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *    (c) 2020 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
 */

#ifndef __SYSTEM_GLOBAL_H_INCLUDED__
#define __SYSTEM_GLOBAL_H_INCLUDED__

/*
 * Create a list of HAS and IS properties that defines the system
 * Those are common for both ISP2400 and ISP2401
 *
 * The configuration assumes the following
 * - The system is hetereogeneous; Multiple cells and devices classes
 * - The cell and device instances are homogeneous, each device type
 *   belongs to the same class
 * - Device instances supporting a subset of the class capabilities are
 *   allowed
 *
 * We could manage different device classes through the enumerated
 * lists (C) or the use of classes (C++), but that is presently not
 * fully supported
 *
 * N.B. the 3 input formatters are of 2 different classess
 */

#define HAS_MMU_VERSION_2
#define HAS_DMA_VERSION_2
#define HAS_GDC_VERSION_2
#define HAS_VAMEM_VERSION_2
#define HAS_HMEM_VERSION_1
#define HAS_BAMEM_VERSION_2
#define HAS_IRQ_VERSION_2
#define HAS_IRQ_MAP_VERSION_2
#define HAS_INPUT_FORMATTER_VERSION_2
#define HAS_INPUT_SYSTEM_VERSION_2
#define HAS_BUFFERED_SENSOR
#define HAS_FIFO_MONITORS_VERSION_2
#define HAS_GP_DEVICE_VERSION_2
#define HAS_GPIO_VERSION_1
#define HAS_TIMED_CTRL_VERSION_1
#define HAS_RX_VERSION_2

/* per-frame parameter handling support */
#define SH_CSS_ENABLE_PER_FRAME_PARAMS

#define DMA_DDR_TO_VAMEM_WORKAROUND
#define DMA_DDR_TO_HMEM_WORKAROUND

/*
 * The longest allowed (uninteruptible) bus transfer, does not
 * take stalling into account
 */
#define HIVE_ISP_MAX_BURST_LENGTH	1024

/*
 * Maximum allowed burst length in words for the ISP DMA
 * This value is set to 2 to prevent the ISP DMA from blocking
 * the bus for too long; as the input system can only buffer
 * 2 lines on Moorefield and Cherrytrail, the input system buffers
 * may overflow if blocked for too long (BZ 2726).
 */
#define ISP2400_DMA_MAX_BURST_LENGTH	128
#define ISP2401_DMA_MAX_BURST_LENGTH	2

#ifdef ISP2401
#  include "isp2401_system_global.h"
#else
#  include "isp2400_system_global.h"
#endif

#include <hive_isp_css_defs.h>
#include <type_support.h>

/* This interface is deprecated */
#include "hive_types.h"

/*
 * Semi global. "HRT" is accessible from SP, but the HRT types do not fully apply
 */
#define HRT_VADDRESS_WIDTH	32

#define SIZEOF_HRT_REG		(HRT_DATA_WIDTH >> 3)
#define HIVE_ISP_CTRL_DATA_BYTES (HIVE_ISP_CTRL_DATA_WIDTH / 8)

/* The main bus connecting all devices */
#define HRT_BUS_WIDTH		HIVE_ISP_CTRL_DATA_WIDTH
#define HRT_BUS_BYTES		HIVE_ISP_CTRL_DATA_BYTES

typedef u32			hrt_bus_align_t;

/*
 * Enumerate the devices, device access through the API is by ID,
 * through the DLI by address. The enumerator terminators are used
 * to size the wiring arrays and as an exception value.
 */
typedef enum {
	DDR0_ID = 0,
	N_DDR_ID
} ddr_ID_t;

typedef enum {
	ISP0_ID = 0,
	N_ISP_ID
} isp_ID_t;

typedef enum {
	SP0_ID = 0,
	N_SP_ID
} sp_ID_t;

typedef enum {
	MMU0_ID = 0,
	MMU1_ID,
	N_MMU_ID
} mmu_ID_t;

typedef enum {
	DMA0_ID = 0,
	N_DMA_ID
} dma_ID_t;

typedef enum {
	GDC0_ID = 0,
	GDC1_ID,
	N_GDC_ID
} gdc_ID_t;

/* this extra define is needed because we want to use it also
   in the preprocessor, and that doesn't work with enums.
 */
#define N_GDC_ID_CPP 2

typedef enum {
	VAMEM0_ID = 0,
	VAMEM1_ID,
	VAMEM2_ID,
	N_VAMEM_ID
} vamem_ID_t;

typedef enum {
	BAMEM0_ID = 0,
	N_BAMEM_ID
} bamem_ID_t;

typedef enum {
	HMEM0_ID = 0,
	N_HMEM_ID
} hmem_ID_t;

typedef enum {
	IRQ0_ID = 0,	/* GP IRQ block */
	IRQ1_ID,	/* Input formatter */
	IRQ2_ID,	/* input system */
	IRQ3_ID,	/* input selector */
	N_IRQ_ID
} irq_ID_t;

typedef enum {
	FIFO_MONITOR0_ID = 0,
	N_FIFO_MONITOR_ID
} fifo_monitor_ID_t;

typedef enum {
	GP_DEVICE0_ID = 0,
	N_GP_DEVICE_ID
} gp_device_ID_t;

typedef enum {
	GP_TIMER0_ID = 0,
	GP_TIMER1_ID,
	GP_TIMER2_ID,
	GP_TIMER3_ID,
	GP_TIMER4_ID,
	GP_TIMER5_ID,
	GP_TIMER6_ID,
	GP_TIMER7_ID,
	N_GP_TIMER_ID
} gp_timer_ID_t;

typedef enum {
	GPIO0_ID = 0,
	N_GPIO_ID
} gpio_ID_t;

typedef enum {
	TIMED_CTRL0_ID = 0,
	N_TIMED_CTRL_ID
} timed_ctrl_ID_t;

typedef enum {
	INPUT_FORMATTER0_ID = 0,
	INPUT_FORMATTER1_ID,
	INPUT_FORMATTER2_ID,
	INPUT_FORMATTER3_ID,
	N_INPUT_FORMATTER_ID
} input_formatter_ID_t;

/* The IF RST is outside the IF */
#define INPUT_FORMATTER0_SRST_OFFSET	0x0824
#define INPUT_FORMATTER1_SRST_OFFSET	0x0624
#define INPUT_FORMATTER2_SRST_OFFSET	0x0424
#define INPUT_FORMATTER3_SRST_OFFSET	0x0224

#define INPUT_FORMATTER0_SRST_MASK		0x0001
#define INPUT_FORMATTER1_SRST_MASK		0x0002
#define INPUT_FORMATTER2_SRST_MASK		0x0004
#define INPUT_FORMATTER3_SRST_MASK		0x0008

typedef enum {
	INPUT_SYSTEM0_ID = 0,
	N_INPUT_SYSTEM_ID
} input_system_ID_t;

typedef enum {
	RX0_ID = 0,
	N_RX_ID
} rx_ID_t;

enum mipi_port_id {
	MIPI_PORT0_ID = 0,
	MIPI_PORT1_ID,
	MIPI_PORT2_ID,
	N_MIPI_PORT_ID
};

#define	N_RX_CHANNEL_ID		4

/* Generic port enumeration with an internal port type ID */
typedef enum {
	CSI_PORT0_ID = 0,
	CSI_PORT1_ID,
	CSI_PORT2_ID,
	TPG_PORT0_ID,
	PRBS_PORT0_ID,
	FIFO_PORT0_ID,
	MEMORY_PORT0_ID,
	N_INPUT_PORT_ID
} input_port_ID_t;

typedef enum {
	CAPTURE_UNIT0_ID = 0,
	CAPTURE_UNIT1_ID,
	CAPTURE_UNIT2_ID,
	ACQUISITION_UNIT0_ID,
	DMA_UNIT0_ID,
	CTRL_UNIT0_ID,
	GPREGS_UNIT0_ID,
	FIFO_UNIT0_ID,
	IRQ_UNIT0_ID,
	N_SUB_SYSTEM_ID
} sub_system_ID_t;

#define	N_CAPTURE_UNIT_ID		3
#define	N_ACQUISITION_UNIT_ID		1
#define	N_CTRL_UNIT_ID			1


enum ia_css_isp_memories {
	IA_CSS_ISP_PMEM0 = 0,
	IA_CSS_ISP_DMEM0,
	IA_CSS_ISP_VMEM0,
	IA_CSS_ISP_VAMEM0,
	IA_CSS_ISP_VAMEM1,
	IA_CSS_ISP_VAMEM2,
	IA_CSS_ISP_HMEM0,
	IA_CSS_SP_DMEM0,
	IA_CSS_DDR,
	N_IA_CSS_MEMORIES
};

#define IA_CSS_NUM_MEMORIES 9
/* For driver compatibility */
#define N_IA_CSS_ISP_MEMORIES   IA_CSS_NUM_MEMORIES
#define IA_CSS_NUM_ISP_MEMORIES IA_CSS_NUM_MEMORIES

/*
 * ISP2401 specific enums
 */

typedef enum {
	ISYS_IRQ0_ID = 0,	/* port a */
	ISYS_IRQ1_ID,	/* port b */
	ISYS_IRQ2_ID,	/* port c */
	N_ISYS_IRQ_ID
} isys_irq_ID_t;


/*
 * Input-buffer Controller.
 */
typedef enum {
	IBUF_CTRL0_ID = 0,	/* map to ISYS2401_IBUF_CNTRL_A */
	IBUF_CTRL1_ID,		/* map to ISYS2401_IBUF_CNTRL_B */
	IBUF_CTRL2_ID,		/* map ISYS2401_IBUF_CNTRL_C */
	N_IBUF_CTRL_ID
} ibuf_ctrl_ID_t;
/* end of Input-buffer Controller */

/*
 * Stream2MMIO.
 */
typedef enum {
	STREAM2MMIO0_ID = 0,	/* map to ISYS2401_S2M_A */
	STREAM2MMIO1_ID,	/* map to ISYS2401_S2M_B */
	STREAM2MMIO2_ID,	/* map to ISYS2401_S2M_C */
	N_STREAM2MMIO_ID
} stream2mmio_ID_t;

typedef enum {
	/*
	 * Stream2MMIO 0 has 8 SIDs that are indexed by
	 * [STREAM2MMIO_SID0_ID...STREAM2MMIO_SID7_ID].
	 *
	 * Stream2MMIO 1 has 4 SIDs that are indexed by
	 * [STREAM2MMIO_SID0_ID...TREAM2MMIO_SID3_ID].
	 *
	 * Stream2MMIO 2 has 4 SIDs that are indexed by
	 * [STREAM2MMIO_SID0_ID...STREAM2MMIO_SID3_ID].
	 */
	STREAM2MMIO_SID0_ID = 0,
	STREAM2MMIO_SID1_ID,
	STREAM2MMIO_SID2_ID,
	STREAM2MMIO_SID3_ID,
	STREAM2MMIO_SID4_ID,
	STREAM2MMIO_SID5_ID,
	STREAM2MMIO_SID6_ID,
	STREAM2MMIO_SID7_ID,
	N_STREAM2MMIO_SID_ID
} stream2mmio_sid_ID_t;
/* end of Stream2MMIO */

/**
 * Input System 2401: CSI-MIPI recevier.
 */
typedef enum {
	CSI_RX_BACKEND0_ID = 0,	/* map to ISYS2401_MIPI_BE_A */
	CSI_RX_BACKEND1_ID,		/* map to ISYS2401_MIPI_BE_B */
	CSI_RX_BACKEND2_ID,		/* map to ISYS2401_MIPI_BE_C */
	N_CSI_RX_BACKEND_ID
} csi_rx_backend_ID_t;

typedef enum {
	CSI_RX_FRONTEND0_ID = 0,	/* map to ISYS2401_CSI_RX_A */
	CSI_RX_FRONTEND1_ID,		/* map to ISYS2401_CSI_RX_B */
	CSI_RX_FRONTEND2_ID,		/* map to ISYS2401_CSI_RX_C */
#define N_CSI_RX_FRONTEND_ID (CSI_RX_FRONTEND2_ID + 1)
} csi_rx_frontend_ID_t;

typedef enum {
	CSI_RX_DLANE0_ID = 0,		/* map to DLANE0 in CSI RX */
	CSI_RX_DLANE1_ID,		/* map to DLANE1 in CSI RX */
	CSI_RX_DLANE2_ID,		/* map to DLANE2 in CSI RX */
	CSI_RX_DLANE3_ID,		/* map to DLANE3 in CSI RX */
	N_CSI_RX_DLANE_ID
} csi_rx_fe_dlane_ID_t;
/* end of CSI-MIPI receiver */

typedef enum {
	ISYS2401_DMA0_ID = 0,
	N_ISYS2401_DMA_ID
} isys2401_dma_ID_t;

/**
 * Pixel-generator. ("system_global.h")
 */
typedef enum {
	PIXELGEN0_ID = 0,
	PIXELGEN1_ID,
	PIXELGEN2_ID,
	N_PIXELGEN_ID
} pixelgen_ID_t;
/* end of pixel-generator. ("system_global.h") */

typedef enum {
	INPUT_SYSTEM_CSI_PORT0_ID = 0,
	INPUT_SYSTEM_CSI_PORT1_ID,
	INPUT_SYSTEM_CSI_PORT2_ID,

	INPUT_SYSTEM_PIXELGEN_PORT0_ID,
	INPUT_SYSTEM_PIXELGEN_PORT1_ID,
	INPUT_SYSTEM_PIXELGEN_PORT2_ID,

	N_INPUT_SYSTEM_INPUT_PORT_ID
} input_system_input_port_ID_t;

#define N_INPUT_SYSTEM_CSI_PORT	3

typedef enum {
	ISYS2401_DMA_CHANNEL_0 = 0,
	ISYS2401_DMA_CHANNEL_1,
	ISYS2401_DMA_CHANNEL_2,
	ISYS2401_DMA_CHANNEL_3,
	ISYS2401_DMA_CHANNEL_4,
	ISYS2401_DMA_CHANNEL_5,
	ISYS2401_DMA_CHANNEL_6,
	ISYS2401_DMA_CHANNEL_7,
	ISYS2401_DMA_CHANNEL_8,
	ISYS2401_DMA_CHANNEL_9,
	ISYS2401_DMA_CHANNEL_10,
	ISYS2401_DMA_CHANNEL_11,
	N_ISYS2401_DMA_CHANNEL
} isys2401_dma_channel;

#endif /* __SYSTEM_GLOBAL_H_INCLUDED__ */
