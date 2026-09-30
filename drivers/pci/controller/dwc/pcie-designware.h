/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#ifndef _PCIE_DESIGNWARE_H
#define _PCIE_DESIGNWARE_H

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dma/edma.h>
#include <linux/gpio/consumer.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/reset.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

#include "../../pci.h"

/* DWC PCIe IP-core versions (native support since v4.70a) */
#define DW_PCIE_VER_365A		0x3336352a
#define DW_PCIE_VER_460A		0x3436302a
#define DW_PCIE_VER_470A		0x3437302a
#define DW_PCIE_VER_480A		0x3438302a
#define DW_PCIE_VER_490A		0x3439302a
#define DW_PCIE_VER_500A		0x3530302a
#define DW_PCIE_VER_510A		0x3531302a
#define DW_PCIE_VER_520A		0x3532302a
#define DW_PCIE_VER_540A		0x3534302a
#define DW_PCIE_VER_562A		0x3536322a

#define __dw_pcie_ver_cmp(_pci, _ver, _op) \
	((_pci)->version _op DW_PCIE_VER_ ## _ver)

#define dw_pcie_ver_is(_pci, _ver) __dw_pcie_ver_cmp(_pci, _ver, ==)

#define dw_pcie_ver_is_ge(_pci, _ver) __dw_pcie_ver_cmp(_pci, _ver, >=)

#define dw_pcie_ver_type_is(_pci, _ver, _type) \
	(__dw_pcie_ver_cmp(_pci, _ver, ==) && \
	 __dw_pcie_ver_cmp(_pci, TYPE_ ## _type, ==))

#define dw_pcie_ver_type_is_ge(_pci, _ver, _type) \
	(__dw_pcie_ver_cmp(_pci, _ver, ==) && \
	 __dw_pcie_ver_cmp(_pci, TYPE_ ## _type, >=))

/* DWC PCIe controller capabilities */
#define DW_PCIE_CAP_REQ_RES		0
#define DW_PCIE_CAP_IATU_UNROLL		1
#define DW_PCIE_CAP_CDM_CHECK		2

#define dw_pcie_cap_is(_pci, _cap) \
	test_bit(DW_PCIE_CAP_ ## _cap, &(_pci)->caps)

#define dw_pcie_cap_set(_pci, _cap) \
	set_bit(DW_PCIE_CAP_ ## _cap, &(_pci)->caps)

/* Parameters for the waiting for iATU enabled routine */
#define LINK_WAIT_MAX_IATU_RETRIES	5
#define LINK_WAIT_IATU			9

/* Synopsys-specific PCIe configuration registers */
#define PORT_FORCE_LINK			0x708		/* Port Force Link */
/* Bit[7:0] LINK_NUM: Link Number. Not used for endpoint */
#define  PORT_FORCE_LINK_NUM_MASK	GENMASK(7, 0)	/* Link Number. Not used for endpoint */
#define  PORT_FORCE_LINK_DDFS		BIT(23)		/* do deskew for sris */

#define PORT_AFAC			0x70C		/* Ack Frequency and L0-L1 ASPM Control */
/* Ack the number of Fast Training Sequence mask */
#define  PORT_AFAC_N_FTS_MASK		GENMASK(15, 8)
/* Ack the number of Fast Training Sequence */
#define  PORT_AFAC_N_FTS(n)		FIELD_PREP(PORT_AFAC_N_FTS_MASK, n)
#define  PORT_AFAC_CC_N_FTS_MASK	GENMASK(23, 16)	/* Common Clock N_FTS mask */
/* Common Clock N_FTS */
#define  PORT_AFAC_CC_N_FTS(n)		FIELD_PREP(PORT_AFAC_CC_N_FTS_MASK, n)
#define  PORT_AFAC_ENTER_ASPM		BIT(30)		/* ASPM L1 Entry Control */
#define  PORT_AFAC_L0S_ELM		GENMASK(26, 24)	/* L0s Entrance Latency mask */
#define  PORT_AFAC_L1_ELM		GENMASK(29, 27)	/* L1 Entrance Latency mask */

#define PORT_LINK_CTRL			0x710		/* Port Link Control Register */
#define  PORT_LINK_CTRL_DLE		BIT(5)		/* DLL Link Enable */
#define  PORT_LINK_CTRL_FLM		BIT(7)		/* Fast Link Mode */
#define  PORT_LINK_CTRL_LC_MASK		GENMASK(21, 16)	/* Link Mode Enable (LINK_CAPABLE) mask */
/* Link Mode Enable (LINK_CAPABLE) */
#define  PORT_LINK_CTRL_LC(n)		FIELD_PREP(PORT_LINK_CTRL_LC_MASK, n)
#define  PORT_LINK_CTRL_LC_1_LANES	PORT_LINK_CTRL_LC(0x1)	/* Link Mode 1 lane */
#define  PORT_LINK_CTRL_LC_2_LANES	PORT_LINK_CTRL_LC(0x3)	/* Link Mode 2 lane */
#define  PORT_LINK_CTRL_LC_4_LANES	PORT_LINK_CTRL_LC(0x7)	/* Link Mode 4 lane */
#define  PORT_LINK_CTRL_LC_8_LANES	PORT_LINK_CTRL_LC(0xf)	/* Link Mode 8 lane */
#define  PORT_LINK_CTRL_LC_16_LANES	PORT_LINK_CTRL_LC(0x1f)	/* Link Mode 16 lane */

#define PORT_LANE_SKEW			0x714		/* Lane Skew */
#define  PORT_LANE_SKEW_INSERT_MASK	GENMASK(23, 0)

/*
 * TIMER_CTRL_MAX_FUNC_NUM: Timer Control and Max Function Number
 * Register.
 *
 * This register holds the ack frequency, latency, replay, fast link
 * scaling timers, and max function number values.
 *
 * Bit[30:29] FAST_LINK_SCALING_FACTOR: Fast Link Timer Scaling Factor.
 *   0x0 (SF_1024): Scaling Factor is 1024 (1ms is 1us).
 *     When the LTSSM is in Config or L12 Entry State, 1ms
 *     timer is 2us, 2ms timer is 4us and 3ms timer is 6us.
 *   0x1 (SF_256): Scaling Factor is 256 (1ms is 4us)
 *   0x2 (SF_64): Scaling Factor is 64 (1ms is 16us)
 *   0x3 (SF_16): Scaling Factor is 16 (1ms is 64us)
 */
#define TIMER_CTRL_MAX_FUNC_NUM		0x718
#define  PORT_FLT_SF_MASK		GENMASK(30, 29)
#define  PORT_FLT_SF_VAL_1024		0x0
#define  PORT_FLT_SF_VAL_256		0x1
#define  PORT_FLT_SF_VAL_64		0x2
#define  PORT_FLT_SF_VAL_16		0x3

#define PORT_LINK_DEBUG0		0x728		/* Port Link Debug Register 0 */
#define  PORT_LINK_DEBUG0_LTSSM_MASK	0x3f		/* LTSSM current state mask */
#define PORT_LINK_DEBUG1		0x72C		/* Port Link Debug Register 1 */
/* LTSSM reports PHY link up or LTSSM is in Loopback */
#define  PORT_LINK_DEBUG1_LINK_UP		BIT(4)
#define  PORT_LINK_DEBUG1_LINK_IN_TRAINING	BIT(29) /* LTSSM performing link training */

#define LINK_WIDTH_SPEED_CTRL		0x80C		/* Link Width and Speed Change Control */
#define  N_FTS_MASK			GENMASK(7, 0)
#define  SPEED_CHANGE			BIT(17)
#define  LINK_WIDTH_MASK		GENMASK(12, 8)
#define  LINK_WIDTH(n)			FIELD_PREP(LINK_WIDTH_MASK, n)
#define  LINK_WIDTH_1_LANES		LINK_WIDTH(0x1)
#define  LINK_WIDTH_2_LANES		LINK_WIDTH(0x2)
#define  LINK_WIDTH_4_LANES		LINK_WIDTH(0x4)
#define  LINK_WIDTH_8_LANES		LINK_WIDTH(0x8)

/* Integrated MSI Reception Module Lower Address */
#define PCIE_MSI_ADDR_LO		0x820
/* Integrated MSI Reception Module Upper Address */
#define PCIE_MSI_ADDR_HI		0x824
/* Integrated MSI Reception Module Interrupt0 Enable */
#define PCIE_MSI_INTR0_ENABLE		0x828
/* Integrated MSI Reception Module Interrupt0 Mask */
#define PCIE_MSI_INTR0_MASK		0x82C
/* Integrated MSI Reception Module Interrupt0 Status */
#define PCIE_MSI_INTR0_STATUS		0x830

#define GEN3_CTRL			0x890		/* Gen3 Control Register */
/* Gen3 Receiver Impedance ZRX-DC Not Compliant */
#define  GEN3_CTRL_GEN3_ZRXDC_NONCOMPL	BIT(0)
#define  GEN3_CTRL_EQ_PHASE_2_3		BIT(9)		/* Equalization Phase 2 and Phase 3 */
#define  GEN3_CTRL_RXEQ_RGRDLESS_RXTS	BIT(13)
#define  GEN3_CTRL_GEN3_EQ_DISABLE	BIT(16)		/* Equalization Disable */
#define  GEN3_CTRL_RATE_SHADOW_SEL_MASK	GENMASK(25, 24)	/* Rate Shadow Select mask */

#define GEN3_EQ_CTRL			0x8A8		/* Gen3 EQ Control */
#define  GEN3_EQ_CTRL_FB_MODE		GENMASK(3, 0)	/* Feedback Mode */
#define  GEN3_EQ_CTRL_PHASE23_EXIT_MODE	BIT(4)		/* Behavior After 24 ms Timeout */
#define  GEN3_EQ_CTRL_PSET_REQ_VEC	GENMASK(23, 8)	/* Preset Request Vector */
/* Include or not the FOM feedback from the initial preset */
#define  GEN3_EQ_CTRL_FOM_INC_INITIAL_EVAL	BIT(24)

/* Gen3 EQ Direction Change Feedback Mode Control */
#define GEN3_EQ_FB_MODE_DIR_CHANGE		0x8AC
#define  GEN3_EQ_FMDC_T_MIN_PHASE23		GENMASK(4, 0)
#define  GEN3_EQ_FMDC_N_EVALS			GENMASK(9, 5)
#define  GEN3_EQ_FMDC_MAX_PRE_CURSOR_DELTA	GENMASK(13, 10)
#define  GEN3_EQ_FMDC_MAX_POST_CURSOR_DELTA	GENMASK(17, 14)

#define MISC_CONTROL_1			0x8BC		/* DBI Read-Only Write Enable */
#define  DBI_RO_WR_EN			BIT(0)		/* Write to RO Registers Using DBI */

#define COHERENCY_CTRL1				0x8E0	/* Cache Coherency Control Register 1 */
/* Boundary Lower Address For Memory Type */
#define  CFG_MEMTYPE_BOUNDARY_LOW_ADDR_MASK	GENMASK(31, 2)
#define  CFG_MEMTYPE_VALUE			BIT(0)	/* Sets the memory type */

#define COHERENCY_CTRL2				0x8E4	/* Cache Coherency Control Register 2 */
#define COHERENCY_CTRL3				0x8E8	/* Cache Coherency Control Register 3 */

#define MULTI_LANE_CTRL			0x8C0	/* UpConfigure Multi-lane Control */
#define  MULTI_LANE_CTRL_UPCFG_SUPPORT	BIT(7)	/* Upconfigure Support */

#define PCIE_VERSION_NUMBER		0x8F8	/* PCIe Controller IIP Release Version Number */
#define PCIE_VERSION_TYPE		0x8FC	/* PCIe Controller IIP Release Version Type */

/*
 * iATU inbound and outbound windows CSRs. Before the IP-core v4.80a each
 * iATU region CSRs had been indirectly accessible by means of the dedicated
 * viewport selector. The iATU/eDMA CSRs space was re-designed in DWC PCIe
 * v4.80a in a way so the viewport was unrolled into the directly accessible
 * iATU/eDMA CSRs space.
 */
#define IATU_VIEWPORT			0x900	/* iATU Index */
#define ATU_REGION_DIR_IB		BIT(31)
#define ATU_REGION_DIR_OB		0
#define ATU_VIEWPORT_BASE		0x904
#define ATU_UNROLL_BASE(dir, index) \
	(((index) << 9) | ((dir == ATU_REGION_DIR_IB) ? BIT(8) : 0))
#define ATU_VIEWPORT_SIZE		0x2C
#define ATU_REGION_CTRL1		0x000
#define  ATU_INCREASE_REGION_SIZE	BIT(13)
#define  ATU_TD				BIT(8)
#define ATU_FUNC_NUM(pf)		((pf) << 20)
#define ATU_REGION_CTRL2		0x004
#define  ATU_ENABLE			BIT(31)
#define  ATU_BAR_MODE_ENABLE		BIT(30)
#define  ATU_CFG_SHIFT_MODE_ENABLE	BIT(28)
#define  ATU_DMA_BYPASS			BIT(27)
#define  ATU_INHIBIT_PAYLOAD		BIT(22)
#define  ATU_FUNC_NUM_MATCH_EN		BIT(19)
#define ATU_LOWER_BASE			0x008
#define ATU_UPPER_BASE			0x00C
#define ATU_LIMIT			0x010
#define ATU_LOWER_TARGET		0x014
#define ATU_BUS(x)			FIELD_PREP(GENMASK(31, 24), x)
#define ATU_DEV(x)			FIELD_PREP(GENMASK(23, 19), x)
#define ATU_FUNC(x)			FIELD_PREP(GENMASK(18, 16), x)
#define ATU_UPPER_TARGET		0x018
#define ATU_UPPER_LIMIT			0x020

#define MSIX_DOORBELL			0x948
#define  MSIX_DOORBELL_PF_MASK		GENMASK(28, 24)

/*
 * eDMA CSRs. DW PCIe IP-core v4.70a and older had the eDMA registers accessible
 * over the Port Logic registers space. Afterwards the unrolled mapping was
 * introduced so eDMA and iATU could be accessed via a dedicated registers
 * space.
 */
#define PCIE_DMA_VIEWPORT_BASE		0x970
#define PCIE_DMA_UNROLL_BASE		0x80000
#define PCIE_DMA_CTRL			0x008
#define PCIE_DMA_NUM_WR_CHAN		GENMASK(3, 0)
#define PCIE_DMA_NUM_RD_CHAN		GENMASK(19, 16)

#define PCIE_PL_CHK_REG_CONTROL_STATUS			0xB20
#define PCIE_PL_CHK_REG_CHK_REG_START			BIT(0)
#define PCIE_PL_CHK_REG_CHK_REG_CONTINUOUS		BIT(1)
#define PCIE_PL_CHK_REG_CHK_REG_COMPARISON_ERROR	BIT(16)
#define PCIE_PL_CHK_REG_CHK_REG_LOGIC_ERROR		BIT(17)
#define PCIE_PL_CHK_REG_CHK_REG_COMPLETE		BIT(18)

#define PCIE_PL_CHK_REG_ERR_ADDR			0xB28

/*
 * 16.0 GT/s (Gen 4) lane margining register definitions
 */
#define GEN4_LANE_MARGINING_1_OFF		0xB80
#define MARGINING_MAX_VOLTAGE_OFFSET		GENMASK(29, 24)
#define MARGINING_NUM_VOLTAGE_STEPS		GENMASK(22, 16)
#define MARGINING_MAX_TIMING_OFFSET		GENMASK(13, 8)
#define MARGINING_NUM_TIMING_STEPS		GENMASK(5, 0)

#define GEN4_LANE_MARGINING_2_OFF		0xB84
#define MARGINING_IND_ERROR_SAMPLER		BIT(28)
#define MARGINING_SAMPLE_REPORTING_METHOD	BIT(27)
#define MARGINING_IND_LEFT_RIGHT_TIMING		BIT(26)
#define MARGINING_IND_UP_DOWN_VOLTAGE		BIT(25)
#define MARGINING_VOLTAGE_SUPPORTED		BIT(24)
#define MARGINING_MAXLANES			GENMASK(20, 16)
#define MARGINING_SAMPLE_RATE_TIMING		GENMASK(13, 8)
#define MARGINING_SAMPLE_RATE_VOLTAGE		GENMASK(5, 0)

/*
 * RAS-DES register definitions
 */
#define RAS_DES_EVENT_COUNTER_CONTROL	0x8
#define EVENT_COUNTER_ALL_CLEAR		0x3
#define EVENT_COUNTER_ENABLE_ALL	0x7
#define EVENT_COUNTER_ENABLE_MASK	GENMASK(4, 2)
#define EVENT_COUNTER_EVENT_SEL_MASK	GENMASK(23, 16)
#define EVENT_COUNTER_GROUP_SEL_MASK	GENMASK(27, 24)
#define EVENT_COUNTER_EVENT_Tx_L0S	0x2
#define EVENT_COUNTER_EVENT_Rx_L0S	0x3
#define EVENT_COUNTER_EVENT_L1		0x5
#define EVENT_COUNTER_EVENT_L1_1	0x7
#define EVENT_COUNTER_EVENT_L1_2	0x8
#define EVENT_COUNTER_GROUP_5		0x5

#define RAS_DES_EVENT_COUNTER_DATA	0xc

/* PTM register definitions */
#define PTM_RES_REQ_CTRL		0x8
#define PTM_RES_CCONTEXT_VALID		BIT(0)
#define PTM_REQ_AUTO_UPDATE_ENABLED	BIT(0)
#define PTM_REQ_START_UPDATE		BIT(1)

#define PTM_LOCAL_LSB			0x10
#define PTM_LOCAL_MSB			0x14
#define PTM_T1_T2_LSB			0x18
#define PTM_T1_T2_MSB			0x1c
#define PTM_T3_T4_LSB			0x28
#define PTM_T3_T4_MSB			0x2c
#define PTM_MASTER_LSB			0x38
#define PTM_MASTER_MSB			0x3c

/*
 * The default address offset between dbi_base and atu_base. Root controller
 * drivers are not required to initialize atu_base if the offset matches this
 * default; the driver core automatically derives atu_base from dbi_base using
 * this offset, if atu_base not set.
 */
#define DEFAULT_DBI_ATU_OFFSET (0x3 << 20)
#define DEFAULT_DBI_DMA_OFFSET PCIE_DMA_UNROLL_BASE

#define MAX_MSI_IRQS			256
#define MAX_MSI_IRQS_PER_CTRL		32
#define MAX_MSI_CTRLS			(MAX_MSI_IRQS / MAX_MSI_IRQS_PER_CTRL)
#define MSI_REG_CTRL_BLOCK_SIZE		12
#define MSI_DEF_NUM_VECTORS		32

/* Maximum number of inbound/outbound iATUs */
#define MAX_IATU_IN			256
#define MAX_IATU_OUT			256

/* Default eDMA LLP memory size */
#define DMA_LLP_MEM_SIZE		PAGE_SIZE

/* Common struct pci_epc_feature bits among DWC EP glue drivers */
#define DWC_EPC_COMMON_FEATURES		.dynamic_inbound_mapping = true, \
					.subrange_mapping = true

struct dw_pcie;
struct dw_pcie_rp;
struct dw_pcie_ep;

enum dw_pcie_device_mode {
	DW_PCIE_UNKNOWN_TYPE,
	DW_PCIE_EP_TYPE,
	DW_PCIE_LEG_EP_TYPE,
	DW_PCIE_RC_TYPE,
};

enum dw_pcie_app_clk {
	DW_PCIE_DBI_CLK,
	DW_PCIE_MSTR_CLK,
	DW_PCIE_SLV_CLK,
	DW_PCIE_NUM_APP_CLKS
};

enum dw_pcie_core_clk {
	DW_PCIE_PIPE_CLK,
	DW_PCIE_CORE_CLK,
	DW_PCIE_AUX_CLK,
	DW_PCIE_REF_CLK,
	DW_PCIE_NUM_CORE_CLKS
};

enum dw_pcie_app_rst {
	DW_PCIE_DBI_RST,
	DW_PCIE_MSTR_RST,
	DW_PCIE_SLV_RST,
	DW_PCIE_NUM_APP_RSTS
};

enum dw_pcie_core_rst {
	DW_PCIE_NON_STICKY_RST,
	DW_PCIE_STICKY_RST,
	DW_PCIE_CORE_RST,
	DW_PCIE_PIPE_RST,
	DW_PCIE_PHY_RST,
	DW_PCIE_HOT_RST,
	DW_PCIE_PWR_RST,
	DW_PCIE_NUM_CORE_RSTS
};

enum dw_pcie_ltssm {
	/* Need to align with PORT_LINK_DEBUG0 bits 0:5 */
	DW_PCIE_LTSSM_DETECT_QUIET = 0x0,
	DW_PCIE_LTSSM_DETECT_ACT = 0x1,
	DW_PCIE_LTSSM_POLL_ACTIVE = 0x2,
	DW_PCIE_LTSSM_POLL_COMPLIANCE = 0x3,
	DW_PCIE_LTSSM_POLL_CONFIG = 0x4,
	DW_PCIE_LTSSM_PRE_DETECT_QUIET = 0x5,
	DW_PCIE_LTSSM_DETECT_WAIT = 0x6,
	DW_PCIE_LTSSM_CFG_LINKWD_START = 0x7,
	DW_PCIE_LTSSM_CFG_LINKWD_ACEPT = 0x8,
	DW_PCIE_LTSSM_CFG_LANENUM_WAI = 0x9,
	DW_PCIE_LTSSM_CFG_LANENUM_ACEPT = 0xa,
	DW_PCIE_LTSSM_CFG_COMPLETE = 0xb,
	DW_PCIE_LTSSM_CFG_IDLE = 0xc,
	DW_PCIE_LTSSM_RCVRY_LOCK = 0xd,
	DW_PCIE_LTSSM_RCVRY_SPEED = 0xe,
	DW_PCIE_LTSSM_RCVRY_RCVRCFG = 0xf,
	DW_PCIE_LTSSM_RCVRY_IDLE = 0x10,
	DW_PCIE_LTSSM_L0 = 0x11,
	DW_PCIE_LTSSM_L0S = 0x12,
	DW_PCIE_LTSSM_L123_SEND_EIDLE = 0x13,
	DW_PCIE_LTSSM_L1_IDLE = 0x14,
	DW_PCIE_LTSSM_L2_IDLE = 0x15,
	DW_PCIE_LTSSM_L2_WAKE = 0x16,
	DW_PCIE_LTSSM_DISABLED_ENTRY = 0x17,
	DW_PCIE_LTSSM_DISABLED_IDLE = 0x18,
	DW_PCIE_LTSSM_DISABLED = 0x19,
	DW_PCIE_LTSSM_LPBK_ENTRY = 0x1a,
	DW_PCIE_LTSSM_LPBK_ACTIVE = 0x1b,
	DW_PCIE_LTSSM_LPBK_EXIT = 0x1c,
	DW_PCIE_LTSSM_LPBK_EXIT_TIMEOUT = 0x1d,
	DW_PCIE_LTSSM_HOT_RESET_ENTRY = 0x1e,
	DW_PCIE_LTSSM_HOT_RESET = 0x1f,
	DW_PCIE_LTSSM_RCVRY_EQ0 = 0x20,
	DW_PCIE_LTSSM_RCVRY_EQ1 = 0x21,
	DW_PCIE_LTSSM_RCVRY_EQ2 = 0x22,
	DW_PCIE_LTSSM_RCVRY_EQ3 = 0x23,

	/* Vendor glue drivers provide pseudo L1 substates from get_ltssm() */
	DW_PCIE_LTSSM_L1_1 = 0x141,
	DW_PCIE_LTSSM_L1_2 = 0x142,

	DW_PCIE_LTSSM_UNKNOWN = 0xFFFFFFFF,
};

struct dw_pcie_ob_atu_cfg {
	int index;
	int type;
	u8 func_no;
	u8 code;
	u8 routing;
	u32 ctrl2;
	u64 parent_bus_addr;
	u64 pci_addr;
	u64 size;
};

struct dw_pcie_host_ops {
	int (*init)(struct dw_pcie_rp *pp);
	void (*deinit)(struct dw_pcie_rp *pp);
	void (*post_init)(struct dw_pcie_rp *pp);
	int (*msi_init)(struct dw_pcie_rp *pp);
	void (*pme_turn_off)(struct dw_pcie_rp *pp);
};

struct dw_pcie_rp {
	bool			use_imsi_rx:1;
	bool			keep_rp_msi_en:1;
	bool			cfg0_io_shared:1;
	u64			cfg0_base;
	void __iomem		*va_cfg0_base;
	u32			cfg0_size;
	resource_size_t		io_base;
	phys_addr_t		io_bus_addr;
	u32			io_size;
	int			irq;
	const struct dw_pcie_host_ops *ops;
	int			msi_irq[MAX_MSI_CTRLS];
	struct irq_domain	*msi_irq_domain;
	dma_addr_t		msi_data;
	struct irq_chip		*msi_irq_chip;
	u32			num_vectors;
	u32			irq_mask[MAX_MSI_CTRLS];
	struct pci_host_bridge  *bridge;
	raw_spinlock_t		lock;
	DECLARE_BITMAP(msi_irq_in_use, MAX_MSI_IRQS);
	bool			use_atu_msg;
	int			msg_atu_index;
	struct resource		*msg_res;
	struct pci_eq_presets	presets;
	struct pci_config_window *cfg;
	bool			ecam_enabled;
	bool			native_ecam;
	bool                    skip_l23_ready;
	bool			skip_pwrctrl_off;
};

struct dw_pcie_ep_ops {
	int	(*pre_init)(struct dw_pcie_ep *ep);
	int	(*init)(struct dw_pcie_ep *ep);
	int	(*raise_irq)(struct dw_pcie_ep *ep, u8 func_no,
			     unsigned int type, u16 interrupt_num);
	const struct pci_epc_features* (*get_features)(struct dw_pcie_ep *ep);
	/*
	 * Provide a method to implement the different func config space
	 * access for different platform, if different func have different
	 * offset, return the offset of func. if use write a register way
	 * return a 0, and implement code in callback function of platform
	 * driver.
	 */
	unsigned int (*get_dbi_offset)(struct dw_pcie_ep *ep, u8 func_no);
	unsigned int (*get_dbi2_offset)(struct dw_pcie_ep *ep, u8 func_no);
};

struct dw_pcie_ep_func {
	struct list_head	list;
	u8			func_no;
	u8			msi_cap;	/* MSI capability offset */
	u8			msix_cap;	/* MSI-X capability offset */
	u8			bar_to_atu[PCI_STD_NUM_BARS];
	struct pci_epf_bar	*epf_bar[PCI_STD_NUM_BARS];

	/* Only for Address Match Mode inbound iATU */
	u32			*ib_atu_indexes[PCI_STD_NUM_BARS];
	unsigned int		num_ib_atu_indexes[PCI_STD_NUM_BARS];
};

struct dw_pcie_ep {
	struct pci_epc		*epc;
	struct list_head	func_list;
	const struct dw_pcie_ep_ops *ops;
	phys_addr_t		phys_base;
	size_t			addr_size;
	size_t			page_size;
	phys_addr_t		*outbound_addr;
	unsigned long		*ib_window_map;
	unsigned long		*ob_window_map;
	void __iomem		*msi_mem;
	phys_addr_t		msi_mem_phys;

	/* MSI outbound iATU state */
	bool			msi_iatu_mapped;
	size_t			msi_iatu_mapped_offset;
	u64			msi_msg_addr;
	size_t			msi_map_size;
};

struct dw_pcie_ops {
	u64	(*cpu_addr_fixup)(struct dw_pcie *pcie, u64 cpu_addr);
	u32	(*read_dbi)(struct dw_pcie *pcie, void __iomem *base, u32 reg,
			    size_t size);
	void	(*write_dbi)(struct dw_pcie *pcie, void __iomem *base, u32 reg,
			     size_t size, u32 val);
	void    (*write_dbi2)(struct dw_pcie *pcie, void __iomem *base, u32 reg,
			      size_t size, u32 val);
	bool	(*link_up)(struct dw_pcie *pcie);
	enum dw_pcie_ltssm (*get_ltssm)(struct dw_pcie *pcie);
	int	(*start_link)(struct dw_pcie *pcie);
	void	(*stop_link)(struct dw_pcie *pcie);
};

struct debugfs_info {
	struct dentry		*debug_dir;
	void			*rasdes_info;
};

struct dw_pcie {
	struct device		*dev;
	void __iomem		*dbi_base;
	resource_size_t		dbi_phys_addr;
	void __iomem		*dbi_base2;
	void __iomem		*atu_base;
	void __iomem		*elbi_base;
	resource_size_t		atu_phys_addr;
	size_t			atu_size;
	resource_size_t		parent_bus_offset;
	u32			num_ib_windows;
	u32			num_ob_windows;
	u32			region_align;
	u64			region_limit;
	struct dw_pcie_rp	pp;
	struct dw_pcie_ep	ep;
	const struct dw_pcie_ops *ops;
	u32			version;
	u32			type;
	unsigned long		caps;
	int			num_lanes;
	int			max_link_speed;
	u8			n_fts[2];
	struct dw_edma_chip	edma;
	phys_addr_t		edma_reg_phys;
	resource_size_t		edma_reg_size;
	bool			l1ss_support;	/* L1 PM Substates support */
	struct clk_bulk_data	app_clks[DW_PCIE_NUM_APP_CLKS];
	struct clk_bulk_data	core_clks[DW_PCIE_NUM_CORE_CLKS];
	struct reset_control_bulk_data	app_rsts[DW_PCIE_NUM_APP_RSTS];
	struct reset_control_bulk_data	core_rsts[DW_PCIE_NUM_CORE_RSTS];
	struct gpio_desc		*pe_rst;
	bool			suspended;
	struct debugfs_info	*debugfs;
	enum			dw_pcie_device_mode mode;
	u16			ptm_vsec_offset;
	struct pci_ptm_debugfs	*ptm_debugfs;

	/*
	 * If iATU input addresses are offset from CPU physical addresses,
	 * we previously required .cpu_addr_fixup() to convert them.  We
	 * now rely on the devicetree instead.  If .cpu_addr_fixup()
	 * exists, we compare its results with devicetree.
	 *
	 * If .cpu_addr_fixup() does not exist, we assume the offset is
	 * zero and warn if devicetree claims otherwise.  If we know all
	 * devicetrees correctly describe the offset, set
	 * use_parent_dt_ranges to true to avoid this warning.
	 */
	bool			use_parent_dt_ranges;
};

#define to_dw_pcie_from_pp(port) container_of((port), struct dw_pcie, pp)

#define to_dw_pcie_from_ep(endpoint)   \
		container_of((endpoint), struct dw_pcie, ep)

int dw_pcie_get_resources(struct dw_pcie *pci);

void dw_pcie_version_detect(struct dw_pcie *pci);

u8 dw_pcie_find_capability(struct dw_pcie *pci, u8 cap);
u16 dw_pcie_find_ext_capability(struct dw_pcie *pci, u8 cap);
void dw_pcie_remove_capability(struct dw_pcie *pci, u8 cap);
void dw_pcie_remove_ext_capability(struct dw_pcie *pci, u8 cap);
u16 dw_pcie_find_rasdes_capability(struct dw_pcie *pci);
u16 dw_pcie_find_ptm_capability(struct dw_pcie *pci);

int dw_pcie_read(void __iomem *addr, int size, u32 *val);
int dw_pcie_write(void __iomem *addr, int size, u32 val);

u32 dw_pcie_read_dbi(struct dw_pcie *pci, u32 reg, size_t size);
void dw_pcie_write_dbi(struct dw_pcie *pci, u32 reg, size_t size, u32 val);
void dw_pcie_write_dbi2(struct dw_pcie *pci, u32 reg, size_t size, u32 val);
bool dw_pcie_link_up(struct dw_pcie *pci);
void dw_pcie_upconfig_setup(struct dw_pcie *pci);
int dw_pcie_wait_for_link(struct dw_pcie *pci);
int dw_pcie_link_get_max_link_width(struct dw_pcie *pci);
int dw_pcie_prog_outbound_atu(struct dw_pcie *pci,
			      const struct dw_pcie_ob_atu_cfg *atu);
int dw_pcie_prog_inbound_atu(struct dw_pcie *pci, int index, int type,
			     u64 parent_bus_addr, u64 pci_addr, u64 size);
int dw_pcie_prog_ep_inbound_atu(struct dw_pcie *pci, u8 func_no, int index,
				int type, u64 parent_bus_addr,
				u8 bar, size_t size);
void dw_pcie_disable_atu(struct dw_pcie *pci, u32 dir, int index);
void dw_pcie_hide_unsupported_l1ss(struct dw_pcie *pci);
void dw_pcie_program_t_power_on(struct dw_pcie *pci, u32 t_power_on);
void dw_pcie_setup(struct dw_pcie *pci);
void dw_pcie_iatu_detect(struct dw_pcie *pci);
int dw_pcie_edma_detect(struct dw_pcie *pci);
void dw_pcie_edma_remove(struct dw_pcie *pci);
resource_size_t dw_pcie_parent_bus_offset(struct dw_pcie *pci,
					  const char *reg_name,
					  resource_size_t cpu_phy_addr);

static inline void dw_pcie_writel_dbi(struct dw_pcie *pci, u32 reg, u32 val)
{
	dw_pcie_write_dbi(pci, reg, 0x4, val);
}

static inline u32 dw_pcie_readl_dbi(struct dw_pcie *pci, u32 reg)
{
	return dw_pcie_read_dbi(pci, reg, 0x4);
}

static inline void dw_pcie_writew_dbi(struct dw_pcie *pci, u32 reg, u16 val)
{
	dw_pcie_write_dbi(pci, reg, 0x2, val);
}

static inline u16 dw_pcie_readw_dbi(struct dw_pcie *pci, u32 reg)
{
	return dw_pcie_read_dbi(pci, reg, 0x2);
}

static inline void dw_pcie_writeb_dbi(struct dw_pcie *pci, u32 reg, u8 val)
{
	dw_pcie_write_dbi(pci, reg, 0x1, val);
}

static inline u8 dw_pcie_readb_dbi(struct dw_pcie *pci, u32 reg)
{
	return dw_pcie_read_dbi(pci, reg, 0x1);
}

static inline void dw_pcie_writel_dbi2(struct dw_pcie *pci, u32 reg, u32 val)
{
	dw_pcie_write_dbi2(pci, reg, 0x4, val);
}

static inline int dw_pcie_read_cfg_byte(struct dw_pcie *pci, int where,
					u8 *val)
{
	*val = dw_pcie_readb_dbi(pci, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline int dw_pcie_read_cfg_word(struct dw_pcie *pci, int where,
					u16 *val)
{
	*val = dw_pcie_readw_dbi(pci, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline int dw_pcie_read_cfg_dword(struct dw_pcie *pci, int where,
					 u32 *val)
{
	*val = dw_pcie_readl_dbi(pci, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline unsigned int dw_pcie_ep_get_dbi_offset(struct dw_pcie_ep *ep,
						     u8 func_no)
{
	unsigned int dbi_offset = 0;

	if (ep->ops->get_dbi_offset)
		dbi_offset = ep->ops->get_dbi_offset(ep, func_no);

	return dbi_offset;
}

static inline u32 dw_pcie_ep_read_dbi(struct dw_pcie_ep *ep, u8 func_no,
				      u32 reg, size_t size)
{
	unsigned int offset = dw_pcie_ep_get_dbi_offset(ep, func_no);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	return dw_pcie_read_dbi(pci, offset + reg, size);
}

static inline void dw_pcie_ep_write_dbi(struct dw_pcie_ep *ep, u8 func_no,
					u32 reg, size_t size, u32 val)
{
	unsigned int offset = dw_pcie_ep_get_dbi_offset(ep, func_no);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	dw_pcie_write_dbi(pci, offset + reg, size, val);
}

static inline void dw_pcie_ep_writel_dbi(struct dw_pcie_ep *ep, u8 func_no,
					 u32 reg, u32 val)
{
	dw_pcie_ep_write_dbi(ep, func_no, reg, 0x4, val);
}

static inline u32 dw_pcie_ep_readl_dbi(struct dw_pcie_ep *ep, u8 func_no,
				       u32 reg)
{
	return dw_pcie_ep_read_dbi(ep, func_no, reg, 0x4);
}

static inline void dw_pcie_ep_writew_dbi(struct dw_pcie_ep *ep, u8 func_no,
					 u32 reg, u16 val)
{
	dw_pcie_ep_write_dbi(ep, func_no, reg, 0x2, val);
}

static inline u16 dw_pcie_ep_readw_dbi(struct dw_pcie_ep *ep, u8 func_no,
				       u32 reg)
{
	return dw_pcie_ep_read_dbi(ep, func_no, reg, 0x2);
}

static inline void dw_pcie_ep_writeb_dbi(struct dw_pcie_ep *ep, u8 func_no,
					 u32 reg, u8 val)
{
	dw_pcie_ep_write_dbi(ep, func_no, reg, 0x1, val);
}

static inline u8 dw_pcie_ep_readb_dbi(struct dw_pcie_ep *ep, u8 func_no,
				      u32 reg)
{
	return dw_pcie_ep_read_dbi(ep, func_no, reg, 0x1);
}

static inline int dw_pcie_ep_read_cfg_byte(struct dw_pcie_ep *ep, u8 func_no,
					   int where, u8 *val)
{
	*val = dw_pcie_ep_readb_dbi(ep, func_no, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline int dw_pcie_ep_read_cfg_word(struct dw_pcie_ep *ep, u8 func_no,
					   int where, u16 *val)
{
	*val = dw_pcie_ep_readw_dbi(ep, func_no, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline int dw_pcie_ep_read_cfg_dword(struct dw_pcie_ep *ep, u8 func_no,
					    int where, u32 *val)
{
	*val = dw_pcie_ep_readl_dbi(ep, func_no, where);
	return PCIBIOS_SUCCESSFUL;
}

static inline unsigned int dw_pcie_ep_get_dbi2_offset(struct dw_pcie_ep *ep,
						      u8 func_no)
{
	unsigned int dbi2_offset = 0;

	if (ep->ops->get_dbi2_offset)
		dbi2_offset = ep->ops->get_dbi2_offset(ep, func_no);
	else if (ep->ops->get_dbi_offset)     /* for backward compatibility */
		dbi2_offset = ep->ops->get_dbi_offset(ep, func_no);

	return dbi2_offset;
}

static inline void dw_pcie_ep_write_dbi2(struct dw_pcie_ep *ep, u8 func_no,
					 u32 reg, size_t size, u32 val)
{
	unsigned int offset = dw_pcie_ep_get_dbi2_offset(ep, func_no);
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	dw_pcie_write_dbi2(pci, offset + reg, size, val);
}

static inline void dw_pcie_ep_writel_dbi2(struct dw_pcie_ep *ep, u8 func_no,
					  u32 reg, u32 val)
{
	dw_pcie_ep_write_dbi2(ep, func_no, reg, 0x4, val);
}

static inline void dw_pcie_dbi_ro_wr_en(struct dw_pcie *pci)
{
	u32 reg;
	u32 val;

	reg = MISC_CONTROL_1;
	val = dw_pcie_readl_dbi(pci, reg);
	val |= DBI_RO_WR_EN;
	dw_pcie_writel_dbi(pci, reg, val);
}

static inline void dw_pcie_dbi_ro_wr_dis(struct dw_pcie *pci)
{
	u32 reg;
	u32 val;

	reg = MISC_CONTROL_1;
	val = dw_pcie_readl_dbi(pci, reg);
	val &= ~DBI_RO_WR_EN;
	dw_pcie_writel_dbi(pci, reg, val);
}

static inline int dw_pcie_start_link(struct dw_pcie *pci)
{
	if (pci->ops && pci->ops->start_link)
		return pci->ops->start_link(pci);

	return 0;
}

static inline void dw_pcie_stop_link(struct dw_pcie *pci)
{
	if (pci->ops && pci->ops->stop_link)
		pci->ops->stop_link(pci);
}

static inline enum dw_pcie_ltssm dw_pcie_get_ltssm(struct dw_pcie *pci)
{
	u32 val;

	if (pci->ops && pci->ops->get_ltssm)
		return pci->ops->get_ltssm(pci);

	val = dw_pcie_readl_dbi(pci, PORT_LINK_DEBUG0);

	return (enum dw_pcie_ltssm)FIELD_GET(PORT_LINK_DEBUG0_LTSSM_MASK, val);
}

const char *dw_pcie_ltssm_status_string(enum dw_pcie_ltssm ltssm);

#ifdef CONFIG_PCIE_DW_HOST
int dw_pcie_suspend_noirq(struct dw_pcie *pci);
int dw_pcie_resume_noirq(struct dw_pcie *pci);
void dw_handle_msi_irq(struct dw_pcie_rp *pp);
void dw_pcie_msi_init(struct dw_pcie_rp *pp);
int dw_pcie_msi_host_init(struct dw_pcie_rp *pp);
void dw_pcie_free_msi(struct dw_pcie_rp *pp);
int dw_pcie_setup_rc(struct dw_pcie_rp *pp);
int dw_pcie_host_init(struct dw_pcie_rp *pp);
void dw_pcie_host_deinit(struct dw_pcie_rp *pp);
int dw_pcie_allocate_msi_domains(struct dw_pcie_rp *pp);
void __iomem *dw_pcie_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn,
				       int where);
#else
static inline int dw_pcie_suspend_noirq(struct dw_pcie *pci)
{
	return 0;
}

static inline int dw_pcie_resume_noirq(struct dw_pcie *pci)
{
	return 0;
}

static inline void dw_handle_msi_irq(struct dw_pcie_rp *pp) { }

static inline void dw_pcie_msi_init(struct dw_pcie_rp *pp)
{ }

static inline int dw_pcie_msi_host_init(struct dw_pcie_rp *pp)
{
	return -ENODEV;
}

static inline void dw_pcie_free_msi(struct dw_pcie_rp *pp)
{ }

static inline int dw_pcie_setup_rc(struct dw_pcie_rp *pp)
{
	return 0;
}

static inline int dw_pcie_host_init(struct dw_pcie_rp *pp)
{
	return 0;
}

static inline void dw_pcie_host_deinit(struct dw_pcie_rp *pp)
{
}

static inline int dw_pcie_allocate_msi_domains(struct dw_pcie_rp *pp)
{
	return 0;
}
static inline void __iomem *dw_pcie_own_conf_map_bus(struct pci_bus *bus,
						     unsigned int devfn,
						     int where)
{
	return NULL;
}
#endif

#ifdef CONFIG_PCIE_DW_EP
void dw_pcie_ep_linkup(struct dw_pcie_ep *ep);
void dw_pcie_ep_linkdown(struct dw_pcie_ep *ep);
int dw_pcie_ep_init(struct dw_pcie_ep *ep);
int dw_pcie_ep_init_registers(struct dw_pcie_ep *ep);
void dw_pcie_ep_deinit(struct dw_pcie_ep *ep);
void dw_pcie_ep_cleanup(struct dw_pcie_ep *ep);
int dw_pcie_ep_raise_intx_irq(struct dw_pcie_ep *ep, u8 func_no);
int dw_pcie_ep_raise_msi_irq(struct dw_pcie_ep *ep, u8 func_no,
			     u8 interrupt_num);
int dw_pcie_ep_raise_msix_irq(struct dw_pcie_ep *ep, u8 func_no,
			     u16 interrupt_num);
int dw_pcie_ep_raise_msix_irq_doorbell(struct dw_pcie_ep *ep, u8 func_no,
				       u16 interrupt_num);
void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar);
struct dw_pcie_ep_func *
dw_pcie_ep_get_func_from_ep(struct dw_pcie_ep *ep, u8 func_no);
#else
static inline void dw_pcie_ep_linkup(struct dw_pcie_ep *ep)
{
}

static inline void dw_pcie_ep_linkdown(struct dw_pcie_ep *ep)
{
}

static inline int dw_pcie_ep_init(struct dw_pcie_ep *ep)
{
	return 0;
}

static inline int dw_pcie_ep_init_registers(struct dw_pcie_ep *ep)
{
	return 0;
}

static inline void dw_pcie_ep_deinit(struct dw_pcie_ep *ep)
{
}

static inline void dw_pcie_ep_cleanup(struct dw_pcie_ep *ep)
{
}

static inline int dw_pcie_ep_raise_intx_irq(struct dw_pcie_ep *ep, u8 func_no)
{
	return 0;
}

static inline int dw_pcie_ep_raise_msi_irq(struct dw_pcie_ep *ep, u8 func_no,
					   u8 interrupt_num)
{
	return 0;
}

static inline int dw_pcie_ep_raise_msix_irq(struct dw_pcie_ep *ep, u8 func_no,
					   u16 interrupt_num)
{
	return 0;
}

static inline int dw_pcie_ep_raise_msix_irq_doorbell(struct dw_pcie_ep *ep,
						     u8 func_no,
						     u16 interrupt_num)
{
	return 0;
}

static inline void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar)
{
}

static inline struct dw_pcie_ep_func *
dw_pcie_ep_get_func_from_ep(struct dw_pcie_ep *ep, u8 func_no)
{
	return NULL;
}
#endif

#ifdef CONFIG_PCIE_DW_DEBUGFS
void dwc_pcie_debugfs_init(struct dw_pcie *pci, enum dw_pcie_device_mode mode);
void dwc_pcie_debugfs_deinit(struct dw_pcie *pci);
#else
static inline void dwc_pcie_debugfs_init(struct dw_pcie *pci,
					 enum dw_pcie_device_mode mode)
{
}
static inline void dwc_pcie_debugfs_deinit(struct dw_pcie *pci)
{
}
#endif

#endif /* _PCIE_DESIGNWARE_H */
