// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/pm_runtime.h>
#include <linux/ratelimit.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/ioport.h>
#include <linux/clk.h>
#include <linux/clk/qcom.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/pm_wakeup.h>
#include <linux/pm_qos.h>
#include <linux/power_supply.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/interconnect.h>
#include <linux/irq.h>
#include <linux/extcon.h>
#include <linux/reset.h>
#include <linux/usb/dwc3-msm.h>
#include <linux/usb/role.h>
#include <linux/usb/redriver.h>
#include <linux/usb/composite.h>
#include <linux/soc/qcom/wcd939x-i2c.h>
#include <linux/usb/repeater.h>

#include "core.h"
#include "gadget.h"
#include "debug.h"
#include "xhci.h"
#include "debug-ipc.h"

#define NUM_LOG_PAGES   12

/* dload specific suppot */
#define PID_MAGIC_ID		0x71432909
#define SERIAL_NUM_MAGIC_ID	0x61945374
#define SERIAL_NUMBER_LENGTH	128

/* time out to wait for USB cable status notification (in ms)*/
#define SM_INIT_TIMEOUT 30000

#define DWC3_GUCTL1_IP_GAP_ADD_ON(n)	((n) << 21)
#define DWC3_GUCTL1_IP_GAP_ADD_ON_MASK	DWC3_GUCTL1_IP_GAP_ADD_ON(7)
#define DWC3_GUCTL1_L1_SUSP_THRLD_EN_FOR_HOST	BIT(8)
#define DWC3_GUCTL3_USB20_RETRY_DISABLE	BIT(16)
#define DWC3_GUSB3PIPECTL_DISRXDETU3	BIT(22)

#define DWC3_LLUCTL	0xd024
/* Force Gen1 speed on Gen2 link */
#define DWC3_LLUCTL_FORCE_GEN1	BIT(10)

#define DWC31_LINK_GDBGLTSSM	0xd050
#define DWC3_GDBGLTSSM_LINKSTATE_MASK	(0xF << 22)

#define DWC31_LINK_LU3LFPSRXTIM(n)	(0xd010 + ((n) * 0x80))
#define GEN2_U3_EXIT_RSP_RX_CLK(n)	((n) << 16)
#define GEN2_U3_EXIT_RSP_RX_CLK_MASK	GEN2_U3_EXIT_RSP_RX_CLK(0xff)
#define GEN1_U3_EXIT_RSP_RX_CLK(n)	(n)
#define GEN1_U3_EXIT_RSP_RX_CLK_MASK	GEN1_U3_EXIT_RSP_RX_CLK(0xff)

/* AHB2PHY register offsets */
#define PERIPH_SS_AHB2PHY_TOP_CFG 0x10

/* AHB2PHY read/write waite value */
#define ONE_READ_WRITE_WAIT 0x11

/* XHCI registers */
#define USB3_HCSPARAMS1		(0x4)
#define USB3_PORTSC		(0x420)
#define USB3_PORTPMSC_20	(0x424)

/**
 *  USB QSCRATCH Hardware registers
 *
 */
#define QSCRATCH_REG_OFFSET	(0x000F8800)
#define QSCRATCH_GENERAL_CFG	(QSCRATCH_REG_OFFSET + 0x08)
#define CGCTL_REG		(QSCRATCH_REG_OFFSET + 0x28)
#define PWR_EVNT_IRQ_STAT_REG    (QSCRATCH_REG_OFFSET + 0x58)
#define PWR_EVNT_IRQ_MASK_REG    (QSCRATCH_REG_OFFSET + 0x5C)
#define EXTRA_INP_REG		(QSCRATCH_REG_OFFSET + 0x1e4)

#define PWR_EVNT_POWERDOWN_IN_P3_MASK		BIT(2)
#define PWR_EVNT_POWERDOWN_OUT_P3_MASK		BIT(3)
#define PWR_EVNT_LPM_IN_L2_MASK			BIT(4)
#define PWR_EVNT_LPM_OUT_L2_MASK		BIT(5)
#define PWR_EVNT_LPM_OUT_RX_ELECIDLE_IRQ_MASK	BIT(12)
#define PWR_EVNT_LPM_OUT_L1_MASK		BIT(13)

#define EXTRA_INP_SS_DISABLE	BIT(5)

/* QSCRATCH_GENERAL_CFG register bit offset */
#define PIPE_UTMI_CLK_SEL	BIT(0)
#define PIPE3_PHYSTATUS_SW	BIT(3)
#define PIPE_UTMI_CLK_DIS	BIT(8)

#define HS_PHY_CTRL_REG		(QSCRATCH_REG_OFFSET + 0x10)
#define UTMI_OTG_VBUS_VALID	BIT(20)
#define SW_SESSVLD_SEL		BIT(28)

#define SS_PHY_CTRL_REG		(QSCRATCH_REG_OFFSET + 0x30)
#define LANE0_PWR_PRESENT	BIT(24)

/* USB DBM Hardware registers */
#define DBM_REG_OFFSET		0xF8000

/* DBM_GEN_CFG */
#define DBM_EN_USB3		0x00000001

/* DBM_EP_CFG */
#define DBM_EN_EP		0x00000001
#define USB3_EPNUM		0x0000003E
#define DBM_BAM_PIPE_NUM	0x000000C0
#define DBM_PRODUCER		0x00000100
#define DBM_DISABLE_WB		0x00000200
#define DBM_INT_RAM_ACC		0x00000400

/* DBM_DATA_FIFO_SIZE */
#define DBM_DATA_FIFO_SIZE_MASK	0x0000ffff

/* DBM_GEVNTSIZ */
#define DBM_GEVNTSIZ_MASK	0x0000ffff

/* DBM_DBG_CNFG */
#define DBM_ENABLE_IOC_MASK	0x0000000f

/* DBM_SOFT_RESET */
#define DBM_SFT_RST_EP0		0x00000001
#define DBM_SFT_RST_EP1		0x00000002
#define DBM_SFT_RST_EP2		0x00000004
#define DBM_SFT_RST_EP3		0x00000008
#define DBM_SFT_RST_EPS_MASK	0x0000000F
#define DBM_SFT_RST_MASK	0x80000000
#define DBM_EN_MASK		0x00000002

/* DBM TRB configurations */
#define DBM_TRB_BIT		0x80000000
#define DBM_TRB_DATA_SRC	0x40000000
#define DBM_TRB_DMA		0x20000000
#define DBM_TRB_EP_NUM(ep)	(ep<<24)

/* GSI related registers */
#define GSI_TRB_ADDR_BIT_53_MASK	(1 << 21)
#define GSI_TRB_ADDR_BIT_55_MASK	(1 << 23)

#define	GSI_GENERAL_CFG_REG(reg)	(QSCRATCH_REG_OFFSET + \
						reg[GENERAL_CFG_REG])
#define	GSI_RESTART_DBL_PNTR_MASK	BIT(20)
#define	GSI_CLK_EN_MASK			BIT(12)
#define	BLOCK_GSI_WR_GO_MASK		BIT(1)
#define	GSI_EN_MASK			BIT(0)

#define GSI_DBL_ADDR_L(reg, n)		(QSCRATCH_REG_OFFSET + \
						reg[DBL_ADDR_L] + (n*4))
#define GSI_DBL_ADDR_H(reg, n)		(QSCRATCH_REG_OFFSET + \
						reg[DBL_ADDR_H] + (n*4))
#define GSI_RING_BASE_ADDR_L(reg, n)	(QSCRATCH_REG_OFFSET + \
						reg[RING_BASE_ADDR_L] + (n*4))
#define GSI_RING_BASE_ADDR_H(reg, n)	(QSCRATCH_REG_OFFSET + \
						reg[RING_BASE_ADDR_H] + (n*4))

#define	GSI_IF_STS(reg)			(QSCRATCH_REG_OFFSET + reg[IF_STS])
#define	GSI_WR_CTRL_STATE_MASK	BIT(15)

#define DWC3_GEVNTCOUNT_EVNTINTRPTMASK		(1 << 31)
#define DWC3_GEVNTADRHI_EVNTADRHI_GSI_EN(n)	(n << 22)
#define DWC3_GEVNTADRHI_EVNTADRHI_GSI_IDX(n)	(n << 16)
#define DWC3_GEVENT_TYPE_GSI			0x3

/* BAM pipe mask */
#define MSM_PIPE_ID_MASK	(0x1F)

/* EBC/LPC Configuration */
#define LPC_SCAN_MASK		(QSCRATCH_REG_OFFSET + 0x200)
#define LPC_REG			(QSCRATCH_REG_OFFSET + 0x204)

#define LPC_SPEED_INDICATOR	BIT(0)
#define LPC_SSP_MODE		BIT(1)
#define LPC_BUS_CLK_EN		BIT(12)

#define USB30_MODE_SEL_REG	(QSCRATCH_REG_OFFSET + 0x210)
#define USB30_QDSS_MODE_SEL	BIT(0)

#define USB30_QDSS_CONFIG_REG	(QSCRATCH_REG_OFFSET + 0x214)

#define DWC3_DEPCFG_EBC_MODE		BIT(15)

#define DWC3_DEPCFG_RETRY		BIT(15)
#define DWC3_DEPCFG_TRB_WB		BIT(14)

/* USB repeater */
#define USB_REPEATER_V1		0x1

enum dbm_reg {
	DBM_EP_CFG,
	DBM_DATA_FIFO,
	DBM_DATA_FIFO_SIZE,
	DBM_DATA_FIFO_EN,
	DBM_GEVNTADR,
	DBM_GEVNTSIZ,
	DBM_DBG_CNFG,
	DBM_HW_TRB0_EP,
	DBM_HW_TRB1_EP,
	DBM_HW_TRB2_EP,
	DBM_HW_TRB3_EP,
	DBM_PIPE_CFG,
	DBM_DISABLE_UPDXFER,
	DBM_SOFT_RESET,
	DBM_GEN_CFG,
	DBM_GEVNTADR_LSB,
	DBM_GEVNTADR_MSB,
	DBM_DATA_FIFO_LSB,
	DBM_DATA_FIFO_MSB,
	DBM_DATA_FIFO_ADDR_EN,
	DBM_DATA_FIFO_SIZE_EN,
};

struct dbm_reg_data {
	u32 offset;
	unsigned int ep_mult;
};

#define DBM_1_4_NUM_EP		4
#define DBM_1_5_NUM_EP		8

static const struct dbm_reg_data dbm_1_4_regtable[] = {
	[DBM_EP_CFG]		= { 0x0000, 0x4 },
	[DBM_DATA_FIFO]		= { 0x0010, 0x4 },
	[DBM_DATA_FIFO_SIZE]	= { 0x0020, 0x4 },
	[DBM_DATA_FIFO_EN]	= { 0x0030, 0x0 },
	[DBM_GEVNTADR]		= { 0x0034, 0x0 },
	[DBM_GEVNTSIZ]		= { 0x0038, 0x0 },
	[DBM_DBG_CNFG]		= { 0x003C, 0x0 },
	[DBM_HW_TRB0_EP]	= { 0x0040, 0x4 },
	[DBM_HW_TRB1_EP]	= { 0x0050, 0x4 },
	[DBM_HW_TRB2_EP]	= { 0x0060, 0x4 },
	[DBM_HW_TRB3_EP]	= { 0x0070, 0x4 },
	[DBM_PIPE_CFG]		= { 0x0080, 0x0 },
	[DBM_SOFT_RESET]	= { 0x0084, 0x0 },
	[DBM_GEN_CFG]		= { 0x0088, 0x0 },
	[DBM_GEVNTADR_LSB]	= { 0x0098, 0x0 },
	[DBM_GEVNTADR_MSB]	= { 0x009C, 0x0 },
	[DBM_DATA_FIFO_LSB]	= { 0x00A0, 0x8 },
	[DBM_DATA_FIFO_MSB]	= { 0x00A4, 0x8 },
};

static const struct dbm_reg_data dbm_1_5_regtable[] = {
	[DBM_EP_CFG]		= { 0x0000, 0x4 },
	[DBM_DATA_FIFO]		= { 0x0280, 0x4 },
	[DBM_DATA_FIFO_SIZE]	= { 0x0080, 0x4 },
	[DBM_DATA_FIFO_EN]	= { 0x026C, 0x0 },
	[DBM_GEVNTADR]		= { 0x0270, 0x0 },
	[DBM_GEVNTSIZ]		= { 0x0268, 0x0 },
	[DBM_DBG_CNFG]		= { 0x0208, 0x0 },
	[DBM_HW_TRB0_EP]	= { 0x0220, 0x4 },
	[DBM_HW_TRB1_EP]	= { 0x0230, 0x4 },
	[DBM_HW_TRB2_EP]	= { 0x0240, 0x4 },
	[DBM_HW_TRB3_EP]	= { 0x0250, 0x4 },
	[DBM_PIPE_CFG]		= { 0x0274, 0x0 },
	[DBM_DISABLE_UPDXFER]	= { 0x0298, 0x0 },
	[DBM_SOFT_RESET]	= { 0x020C, 0x0 },
	[DBM_GEN_CFG]		= { 0x0210, 0x0 },
	[DBM_GEVNTADR_LSB]	= { 0x0260, 0x0 },
	[DBM_GEVNTADR_MSB]	= { 0x0264, 0x0 },
	[DBM_DATA_FIFO_LSB]	= { 0x0100, 0x8 },
	[DBM_DATA_FIFO_MSB]	= { 0x0104, 0x8 },
	[DBM_DATA_FIFO_ADDR_EN]	= { 0x0200, 0x0 },
	[DBM_DATA_FIFO_SIZE_EN]	= { 0x0204, 0x0 },
};

enum usb_gsi_reg {
	GENERAL_CFG_REG,
	DBL_ADDR_L,
	DBL_ADDR_H,
	RING_BASE_ADDR_L,
	RING_BASE_ADDR_H,
	IF_STS,
	GSI_REG_MAX,
};

struct dload_struct {
	u32	pid;
	char	serial_number[SERIAL_NUMBER_LENGTH];
	u32	pid_magic;
	u32	serial_magic;
};

struct dwc3_hw_ep {
	struct dwc3_ep		*dep;
	enum usb_hw_ep_mode	mode;
	struct dwc3_trb		*ebc_trb_pool;
	u8 dbm_ep_num;
	int num_trbs;

	unsigned long flags;
#define DWC3_MSM_HW_EP_TRANSFER_STARTED BIT(0)
};

struct dwc3_msm_req_complete {
	struct list_head list_item;
	struct usb_request *req;
	void (*orig_complete)(struct usb_ep *ep,
			      struct usb_request *req);
};

enum dwc3_drd_state {
	DRD_STATE_UNDEFINED = 0,

	DRD_STATE_IDLE,
	DRD_STATE_PERIPHERAL,
	DRD_STATE_PERIPHERAL_SUSPEND,

	DRD_STATE_HOST,
};

static const char *const state_names[] = {
	[DRD_STATE_UNDEFINED] = "undefined",
	[DRD_STATE_IDLE] = "idle",
	[DRD_STATE_PERIPHERAL] = "peripheral",
	[DRD_STATE_PERIPHERAL_SUSPEND] = "peripheral_suspend",
	[DRD_STATE_HOST] = "host",
};

static const char *const usb_dr_modes[] = {
	[USB_DR_MODE_UNKNOWN]		= "",
	[USB_DR_MODE_HOST]		= "host",
	[USB_DR_MODE_PERIPHERAL]	= "peripheral",
	[USB_DR_MODE_OTG]		= "otg",
};

enum dp_lane {
	DP_NONE = 0,
	DP_2_LANE = 2,
	DP_4_LANE = 4,
};

static const char *dwc3_drd_state_string(enum dwc3_drd_state state)
{
	if (state < 0 || state >= ARRAY_SIZE(state_names))
		return "UNKNOWN";

	return state_names[state];
}

enum dwc3_id_state {
	DWC3_ID_GROUND = 0,
	DWC3_ID_FLOAT,
};

enum msm_usb_irq {
	HS_PHY_IRQ,
	PWR_EVNT_IRQ,
	DP_HS_PHY_IRQ,
	DM_HS_PHY_IRQ,
	SS_PHY_IRQ,
	USB_MAX_IRQ
};

enum icc_paths {
	USB_DDR,
	USB_IPA,
	DDR_USB,
	USB_MAX_PATH
};

enum bus_vote {
	BUS_VOTE_NONE,
	BUS_VOTE_NOMINAL,
	BUS_VOTE_SVS,
	BUS_VOTE_MIN,
	BUS_VOTE_MAX
};

static const char * const icc_path_names[] = {
	"usb-ddr", "usb-ipa", "ddr-usb",
};

static struct {
	u32 avg, peak;
} bus_vote_values[BUS_VOTE_MAX][3] = {
	/* usb_ddr avg/peak, usb_ipa avg/peak, apps_usb avg/peak */
	[BUS_VOTE_NONE]    = { {0, 0}, {0, 0}, {0, 0} },
	[BUS_VOTE_NOMINAL] = { {1000000, 1250000}, {0, 2400}, {0, 40000}, },
	[BUS_VOTE_SVS]     = { {240000, 700000}, {0, 2400}, {0, 40000}, },
	[BUS_VOTE_MIN]     = { {1, 1}, {1, 1}, {1, 1}, },
};

struct usb_irq_info {
	const char	*name;
	unsigned long	irq_type;
	bool		required;
};

static const struct usb_irq_info usb_irq_info[USB_MAX_IRQ] = {
	{ "hs_phy_irq",
	  IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQ_TYPE_LEVEL_HIGH |
		 IRQF_EARLY_RESUME,
	  false,
	},
	{ "pwr_event_irq",
	  IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQ_TYPE_LEVEL_HIGH |
		 IRQF_EARLY_RESUME,
	  true,
	},
	{ "dp_hs_phy_irq",
	  IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_EARLY_RESUME,
	  false,
	},
	{ "dm_hs_phy_irq",
	  IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_EARLY_RESUME,
	  false,
	},
	{ "ss_phy_irq",
	  IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQ_TYPE_LEVEL_HIGH |
		 IRQF_EARLY_RESUME,
	  false,
	},
};

struct usb_irq {
	int irq;
	bool enable;
};

static const char * const gsi_op_strings[] = {
	"EP_CONFIG", "START_XFER", "STORE_DBL_INFO",
	"ENABLE_GSI", "UPDATE_XFER", "RING_DB",
	"END_XFER", "GET_CH_INFO", "GET_XFER_IDX", "PREPARE_TRBS",
	"FREE_TRBS", "SET_CLR_BLOCK_DBL", "CHECK_FOR_SUSP",
	"EP_DISABLE" };

static const char * const usb_role_strings[] = {
	"NONE",
	"HOST",
	"DEVICE"
};

static const char *const speed_names[] = {
	[USB_SPEED_UNKNOWN] = "UNKNOWN",
	[USB_SPEED_LOW] = "low-speed",
	[USB_SPEED_FULL] = "full-speed",
	[USB_SPEED_HIGH] = "high-speed",
	[USB_SPEED_WIRELESS] = "wireless",
	[USB_SPEED_SUPER] = "super-speed",
	[USB_SPEED_SUPER_PLUS] = "super-speed-plus",
};

struct dwc3_msm;

struct extcon_nb {
	struct extcon_dev	*edev;
	struct dwc3_msm		*mdwc;
	int			idx;
	struct notifier_block	vbus_nb;
	struct notifier_block	id_nb;
	bool is_eud;
};

/* Input bits to state machine (mdwc->inputs) */

#define ID			0
#define B_SESS_VLD		1
#define B_SUSPEND		2
#define WAIT_FOR_LPM		3
#define CONN_DONE		4

/* reduce interval which allow device enter perf mode quickly for KPI test */
#define PM_QOS_DEFAULT_SAMPLE_MS	100
/* better choose high speed data which will cover super speed, ignore low/full */
#define PM_QOS_DEFAULT_SAMPLE_THRESHOLD	200

/* below setting will be used after device enters perf mode */
#define PM_QOS_PERF_SAMPLE_MS	2000
#define PM_QOS_PERF_SAMPLE_THRESHOLD	400

struct dwc3_msm {
	struct device *dev;
	void __iomem *base;
	void __iomem *tcsr_dyn_en_dis;
	void __iomem *ahb2phy_base;
	phys_addr_t reg_phys;
	struct platform_device	*dwc3;
	struct dma_iommu_mapping *iommu_map;
	const struct usb_ep_ops *original_ep_ops[DWC3_ENDPOINTS_NUM];
	struct list_head req_complete_list;
	struct clk		*xo_clk;
	struct clk		*core_clk;
	long			core_clk_rate;
	long			core_clk_rate_hs;
	long			core_clk_rate_disconnected;
	struct clk		*iface_clk;
	struct clk		*sleep_clk;
	struct clk		*utmi_clk;
	unsigned int		utmi_clk_rate;
	struct clk		*utmi_clk_src;
	struct clk		*bus_aggr_clk;
	struct clk		*noc_aggr_clk;
	struct clk		*cfg_ahb_clk;
	struct reset_control	*core_reset;
	struct regulator	*dwc3_gdsc;

	struct usb_phy		*hs_phy, *ss_phy;
	struct usb_redriver	*redriver;

	const struct dbm_reg_data *dbm_reg_table;
	int			dbm_num_eps;
	bool			dbm_is_1p4;

	bool			resume_pending;
	atomic_t                pm_suspended;
	struct usb_irq		wakeup_irq[USB_MAX_IRQ];
	int			core_irq;
	unsigned int		irq_cnt;
	struct work_struct	resume_work;
	struct work_struct	restart_usb_work;
	bool			in_restart;
	struct workqueue_struct *dwc3_wq;
	struct workqueue_struct *sm_usb_wq;
	struct work_struct	sm_work;
	unsigned long		inputs;
	enum dwc3_drd_state	drd_state;
	enum usb_dr_mode	dr_mode;
	enum bus_vote		default_bus_vote;
	enum bus_vote		override_bus_vote;
	struct icc_path		*icc_paths[3];
	bool			in_host_mode;
	bool			in_device_mode;
	enum usb_device_speed	max_rh_port_speed;
	bool			perf_mode;
	bool			check_eud_state;
	bool			vbus_active;
	bool			eud_active;
	bool			suspend;
	bool			use_pdc_interrupts;
	enum dwc3_id_state	id_state;
	bool			use_pwr_event_for_wakeup;
	bool			host_poweroff_in_pm_suspend;
	bool			disable_host_ssphy_powerdown;
	unsigned long		lpm_flags;
	unsigned int		vbus_draw;
#define MDWC3_SS_PHY_SUSPEND		BIT(0)
#define MDWC3_ASYNC_IRQ_WAKE_CAPABILITY	BIT(1)
#define MDWC3_POWER_COLLAPSE		BIT(2)

	struct extcon_nb	*extcon;
	int			ext_idx;
	struct notifier_block	host_nb;

	u32			ip;
	atomic_t                in_p3;
	atomic_t		in_lpm;
	unsigned int		lpm_to_suspend_delay;
	struct dev_pm_ops	*dwc3_pm_ops;
	struct dev_pm_ops	*xhci_pm_ops;

	u32			num_gsi_event_buffers;
	struct dwc3_event_buffer **gsi_ev_buff;
	int pm_qos_latency;
	struct pm_qos_request pm_qos_req_dma;
	struct delayed_work perf_vote_work;
	struct mutex suspend_resume_mutex;
	struct mutex role_switch_mutex;

	enum usb_device_speed override_usb_speed;
	enum usb_device_speed	max_hw_supp_speed;
	u32			*gsi_reg;
	int			gsi_reg_offset_cnt;

	struct notifier_block	dpdm_nb;
	struct regulator	*dpdm_reg;

	u64			dummy_gsi_db;
	dma_addr_t		dummy_gsi_db_dma;

	struct usb_role_switch *role_switch;
	struct usb_role_switch *dwc3_drd_sw;
	bool			ss_release_called;
	int			orientation_override;

#define MAX_ERROR_RECOVERY_TRIES	3
	bool			err_evt_seen;
	int			retries_on_error;

	void            *dwc_ipc_log_ctxt;
	void            *dwc_dma_ipc_log_ctxt;

	struct dwc3_hw_ep	hw_eps[DWC3_ENDPOINTS_NUM];
	phys_addr_t		ebc_desc_addr;
	bool			dis_sending_cm_l1_quirk;
	bool			use_eusb2_phy;
	bool			force_gen1;
	bool			cached_dis_u1_entry_quirk;
	bool			cached_dis_u2_entry_quirk;
	int			refcnt_dp_usb;
	enum dp_lane		dp_state;

	int			orientation_gpio;
	bool			has_orientation_gpio;

	bool			wcd_usbss;
	bool			dynamic_disable;

	struct dentry *dbg_dir;
#define PM_QOS_REQ_DYNAMIC	0
#define PM_QOS_REQ_PERF		1
#define PM_QOS_REQ_DEFAULT	2
	u8 qos_req_state;
#define PM_QOS_REC_MAX_RECORD	50
	bool qos_rec_start;
	u8 qos_rec_index;
	u32 qos_rec_irq[PM_QOS_REC_MAX_RECORD];
};

#define USB_HSPHY_3P3_VOL_MIN		3050000 /* uV */
#define USB_HSPHY_3P3_VOL_MAX		3300000 /* uV */
#define USB_HSPHY_3P3_HPM_LOAD		16000	/* uA */

#define USB_HSPHY_1P8_VOL_MIN		1800000 /* uV */
#define USB_HSPHY_1P8_VOL_MAX		1800000 /* uV */
#define USB_HSPHY_1P8_HPM_LOAD		19000	/* uA */

#define USB_SSPHY_1P8_VOL_MIN		1800000 /* uV */
#define USB_SSPHY_1P8_VOL_MAX		1800000 /* uV */
#define USB_SSPHY_1P8_HPM_LOAD		23000	/* uA */

/* unfortunately, dwc3 core doesn't manage multiple dwc3 instances for trace */
void *dwc_trace_ipc_log_ctxt;

static struct dload_struct __iomem *diag_dload;

static void dwc3_pwr_event_handler(struct dwc3_msm *mdwc);

static inline void dwc3_msm_ep_writel(void __iomem *base, u32 offset, u32 value)
{
	writel_relaxed(value, base + offset - DWC3_GLOBALS_REGS_START);

	/* Ensure writes to DWC3 ep registers are completed */
	mb();
}

static inline u32 dwc3_msm_ep_readl(void __iomem *base, u32 offset)
{
	u32 value;

	/*
	 * We requested the mem region starting from the Globals address
	 * space, see dwc3_probe in core.c.
	 * However, the offsets are given starting from xHCI address space.
	 */
	value = readl_relaxed(base + offset - DWC3_GLOBALS_REGS_START);
	return value;
}

static inline dma_addr_t dwc3_trb_dma_offset(struct dwc3_ep *dep,
		struct dwc3_trb *trb)
{
	u32 offset = (char *) trb - (char *) dep->trb_pool;

	return dep->trb_pool_dma + offset;
}

static int dwc3_alloc_trb_pool(struct dwc3_ep *dep)
{
	struct dwc3		*dwc = dep->dwc;

	if (dep->trb_pool)
		return 0;

	dep->trb_pool = dma_alloc_coherent(dwc->sysdev,
			sizeof(struct dwc3_trb) * DWC3_TRB_NUM,
			&dep->trb_pool_dma, GFP_KERNEL);
	if (!dep->trb_pool) {
		dev_err(dep->dwc->dev, "failed to allocate trb pool for %s\n",
				dep->name);
		return -ENOMEM;
	}

	return 0;
}

static void dwc3_free_trb_pool(struct dwc3_ep *dep)
{
	struct dwc3		*dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	/*
	 * Clean up ep ring to avoid getting xferInProgress due to stale trbs
	 * with HWO bit set from previous composition when update transfer cmd
	 * is issued.
	 */
	if (dep->number > 1 && dep->trb_pool && dep->trb_pool_dma) {
		memset(&dep->trb_pool[0], 0,
			sizeof(struct dwc3_trb) * DWC3_TRB_NUM);
		dbg_event(dep->number, "Clr_TRB", 0);
		dev_info(dwc->dev, "Clr_TRB ring of %s\n", dep->name);

		dma_free_coherent(dwc->sysdev,
				sizeof(struct dwc3_trb) * DWC3_TRB_NUM,
				dep->trb_pool, dep->trb_pool_dma);
		dep->trb_pool = NULL;
		dep->trb_pool_dma = 0;
	}
}

static enum usb_device_speed dwc3_msm_get_max_speed(struct dwc3_msm *mdwc)
{
	struct dwc3 *dwc;

	if (!mdwc->dwc3)
		return mdwc->override_usb_speed;

	dwc = platform_get_drvdata(mdwc->dwc3);
	return dwc->maximum_speed;
}

static unsigned int dwc3_msm_set_max_speed(struct dwc3_msm *mdwc, enum usb_device_speed spd)
{
	struct dwc3 *dwc;

	if (spd == USB_SPEED_UNKNOWN || spd >= mdwc->max_hw_supp_speed)
		spd = mdwc->max_hw_supp_speed;

	if (!mdwc->dwc3) {
		mdwc->override_usb_speed = spd;
		return 0;
	}

	dwc = platform_get_drvdata(mdwc->dwc3);
	dwc->maximum_speed = spd;

	return 0;
}

/**
 *
 * Read register with debug info.
 *
 * @base - DWC3 base virtual address.
 * @offset - register offset.
 *
 * @return u32
 */
static inline u32 dwc3_msm_read_reg(void __iomem *base, u32 offset)
{
	u32 val = ioread32(base + offset);
	return val;
}

/**
 * Read register masked field with debug info.
 *
 * @base - DWC3 base virtual address.
 * @offset - register offset.
 * @mask - register bitmask.
 *
 * @return u32
 */
static inline u32 dwc3_msm_read_reg_field(void __iomem *base,
					  u32 offset,
					  const u32 mask)
{
	u32 shift = __ffs(mask);
	u32 val = ioread32(base + offset);

	val &= mask;		/* clear other bits */
	val >>= shift;
	return val;
}

/**
 *
 * Write register with debug info.
 *
 * @base - DWC3 base virtual address.
 * @offset - register offset.
 * @val - value to write.
 *
 */
static inline void dwc3_msm_write_reg(void __iomem *base, u32 offset, u32 val)
{
	iowrite32(val, base + offset);
}

/**
 * Write register masked field with debug info.
 *
 * @base - DWC3 base virtual address.
 * @offset - register offset.
 * @mask - register bitmask.
 * @val - value to write.
 *
 */
static inline void dwc3_msm_write_reg_field(void __iomem *base, u32 offset,
					    const u32 mask, u32 val)
{
	u32 shift = __ffs(mask);
	u32 tmp = ioread32(base + offset);

	tmp &= ~mask;		/* clear written bits */
	val = tmp | (val << shift);
	iowrite32(val, base + offset);

	/* Read back to make sure that previous write goes through */
	ioread32(base + offset);
}

/**
 * dwc3_core_calc_tx_fifo_size - calculates the txfifo size value
 * @dwc: pointer to the DWC3 context
 * @nfifos: number of fifos to calculate for
 *
 * Calculates the size value based on the equation below:
 *
 * fifo_size = mult * ((max_packet + mdwidth)/mdwidth + 1) + 1
 *
 * The max packet size is set to 1024, as the txfifo requirements mainly apply
 * to super speed USB use cases.  However, it is safe to overestimate the fifo
 * allocations for other scenarios, i.e. high speed USB.
 */
static int dwc3_core_calc_tx_fifo_size(struct dwc3 *dwc, int mult)
{
	int max_packet = 1024;
	int fifo_size;
	int mdwidth;

	mdwidth = dwc3_mdwidth(dwc);

	/* MDWIDTH is represented in bits, we need it in bytes */
	mdwidth >>= 3;

	fifo_size = mult * ((max_packet + mdwidth) / mdwidth) + 1;
	return fifo_size;
}

/*
 * dwc3_core_resize_tx_fifos - reallocate fifo spaces for current use-case
 * @dwc: pointer to our context structure
 *
 * This function will a best effort FIFO allocation in order
 * to improve FIFO usage and throughput, while still allowing
 * us to enable as many endpoints as possible.
 *
 * Keep in mind that this operation will be highly dependent
 * on the configured size for RAM1 - which contains TxFifo -,
 * the amount of endpoints enabled on coreConsultant tool, and
 * the width of the Master Bus.
 *
 * In general, FIFO depths are represented with the following equation:
 *
 * fifo_size = mult * ((max_packet + mdwidth)/mdwidth + 1) + 1
 *
 * Conversions can be done to the equation to derive the number of packets that
 * will fit to a particular FIFO size value.
 */
static int dwc3_core_resize_tx_fifos(struct dwc3_ep *dep)
{
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	int fifo_0_start;
	int ram1_depth;
	int fifo_size;
	int min_depth;
	int num_in_ep;
	int remaining;
	int num_fifos = 1;
	int fifo;
	int tmp;

	if (!dwc->do_fifo_resize)
		return 0;

	/* resize IN endpoints except ep0 */
	if (!usb_endpoint_dir_in(dep->endpoint.desc) || dep->number <= 1)
		return 0;

	ram1_depth = DWC3_RAM1_DEPTH(dwc->hwparams.hwparams7);

	if ((dep->endpoint.maxburst > 1 &&
	     usb_endpoint_xfer_bulk(dep->endpoint.desc)) ||
	    usb_endpoint_xfer_isoc(dep->endpoint.desc))
		num_fifos = 3;

	if (dep->endpoint.maxburst > 6 &&
	    usb_endpoint_xfer_bulk(dep->endpoint.desc) && DWC3_IP_IS(DWC31))
		num_fifos = dwc->tx_fifo_resize_max_num;

	/* FIFO size for a single buffer */
	fifo = dwc3_core_calc_tx_fifo_size(dwc, 1);

	/* Calculate the number of remaining EPs w/o any FIFO */
	num_in_ep = dwc->max_cfg_eps;
	num_in_ep -= dwc->num_ep_resized;

	/* Reserve at least one FIFO for the number of IN EPs */
	min_depth = num_in_ep * (fifo + 1);
	remaining = ram1_depth - min_depth - dwc->last_fifo_depth;
	remaining = max_t(int, 0, remaining);
	/*
	 * We've already reserved 1 FIFO per EP, so check what we can fit in
	 * addition to it.  If there is not enough remaining space, allocate
	 * all the remaining space to the EP.
	 */
	fifo_size = (num_fifos - 1) * fifo;
	if (remaining < fifo_size)
		fifo_size = remaining;

	fifo_size += fifo;
	/* Last increment according to the TX FIFO size equation */
	fifo_size++;

	/* Check if TXFIFOs start at non-zero addr */
	tmp = dwc3_msm_read_reg(mdwc->base, DWC3_GTXFIFOSIZ(0));
	fifo_0_start = DWC3_GTXFIFOSIZ_TXFSTADDR(tmp);

	fifo_size |= (fifo_0_start + (dwc->last_fifo_depth << 16));
	if (DWC3_IP_IS(DWC3))
		dwc->last_fifo_depth += DWC3_GTXFIFOSIZ_TXFDEP(fifo_size);
	else
		dwc->last_fifo_depth += DWC31_GTXFIFOSIZ_TXFDEP(fifo_size);

	/* Check fifo size allocation doesn't exceed available RAM size. */
	if (dwc->last_fifo_depth >= ram1_depth) {
		dev_err(dwc->dev, "Fifosize(%d) > RAM size(%d) %s depth:%d\n",
			dwc->last_fifo_depth, ram1_depth,
			dep->endpoint.name, fifo_size);
		if (DWC3_IP_IS(DWC3))
			fifo_size = DWC3_GTXFIFOSIZ_TXFDEP(fifo_size);
		else
			fifo_size = DWC31_GTXFIFOSIZ_TXFDEP(fifo_size);

		dwc->last_fifo_depth -= fifo_size;
		return -ENOMEM;
	}

	dwc3_msm_write_reg(mdwc->base, DWC3_GTXFIFOSIZ(dep->number >> 1), fifo_size);
	dwc->num_ep_resized++;

	return 0;
}

/**
 * Write DBM register masked field with debug info.
 *
 * @dbm - DBM specific data
 * @reg - DBM register, used to look up the offset value
 * @ep - endpoint number
 * @mask - register bitmask.
 * @val - value to write.
 */
static inline void msm_dbm_write_ep_reg_field(struct dwc3_msm *mdwc,
					      enum dbm_reg reg, int ep,
					      const u32 mask, u32 val)
{
	u32 offset = DBM_REG_OFFSET + mdwc->dbm_reg_table[reg].offset +
			(mdwc->dbm_reg_table[reg].ep_mult * ep);
	dwc3_msm_write_reg_field(mdwc->base, offset, mask, val);
}

#define msm_dbm_write_reg_field(d, r, m, v) \
	msm_dbm_write_ep_reg_field(d, r, 0, m, v)

/**
 * Read DBM register with debug info.
 *
 * @dbm - DBM specific data
 * @reg - DBM register, used to look up the offset value
 * @ep - endpoint number
 *
 * @return u32
 */
static inline u32 msm_dbm_read_ep_reg(struct dwc3_msm *mdwc,
				      enum dbm_reg reg, int ep)
{
	u32 offset = DBM_REG_OFFSET + mdwc->dbm_reg_table[reg].offset +
			(mdwc->dbm_reg_table[reg].ep_mult * ep);
	return dwc3_msm_read_reg(mdwc->base, offset);
}

#define msm_dbm_read_reg(d, r) msm_dbm_read_ep_reg(d, r, 0)

/**
 * Write DBM register with debug info.
 *
 * @dbm - DBM specific data
 * @reg - DBM register, used to look up the offset value
 * @ep - endpoint number
 */
static inline void msm_dbm_write_ep_reg(struct dwc3_msm *mdwc, enum dbm_reg reg,
					int ep, u32 val)
{
	u32 offset = DBM_REG_OFFSET + mdwc->dbm_reg_table[reg].offset +
			(mdwc->dbm_reg_table[reg].ep_mult * ep);
	dwc3_msm_write_reg(mdwc->base, offset, val);
}

#define msm_dbm_write_reg(d, r, v) msm_dbm_write_ep_reg(d, r, 0, v)

/**
 * Return DBM EP number according to usb endpoint number.
 */
static int find_matching_dbm_ep(struct dwc3_msm *mdwc, u8 usb_ep)
{
	if (mdwc->hw_eps[usb_ep].mode == USB_EP_BAM)
		return mdwc->hw_eps[usb_ep].dbm_ep_num;

	dev_dbg(mdwc->dev, "%s: No DBM EP matches USB EP %d\n",
			__func__, usb_ep);
	return -ENODEV; /* Not found */
}

/**
 * Return number of configured DBM endpoints.
 */
static int dbm_get_num_of_eps_configured(struct dwc3_msm *mdwc)
{
	int i;
	int count = 0;

	for (i = 0; i < DWC3_ENDPOINTS_NUM; i++)
		if (mdwc->hw_eps[i].mode == USB_EP_BAM)
			count++;

	return count;
}

static bool dwc3_msm_is_ss_rhport_connected(struct dwc3_msm *mdwc)
{
	int i, num_ports;
	u32 reg;

	reg = dwc3_msm_read_reg(mdwc->base, USB3_HCSPARAMS1);
	num_ports = HCS_MAX_PORTS(reg);

	for (i = 0; i < num_ports; i++) {
		reg = dwc3_msm_read_reg(mdwc->base, USB3_PORTSC + i*0x10);
		if ((reg & PORT_CONNECT) && DEV_SUPERSPEED_ANY(reg))
			return true;
	}

	return false;
}

static bool dwc3_msm_is_host_superspeed(struct dwc3_msm *mdwc)
{
	int i, num_ports;
	u32 reg;

	reg = dwc3_msm_read_reg(mdwc->base, USB3_HCSPARAMS1);
	num_ports = HCS_MAX_PORTS(reg);

	for (i = 0; i < num_ports; i++) {
		reg = dwc3_msm_read_reg(mdwc->base, USB3_PORTSC + i*0x10);
		if ((reg & PORT_PE) && DEV_SUPERSPEED_ANY(reg))
			return true;
	}

	return false;
}

static inline bool dwc3_msm_is_dev_superspeed(struct dwc3_msm *mdwc)
{
	u8 speed;

	speed = dwc3_msm_read_reg(mdwc->base, DWC3_DSTS) & DWC3_DSTS_CONNECTSPD;
	if ((speed == DWC3_DSTS_SUPERSPEED) ||
			(speed == DWC3_DSTS_SUPERSPEED_PLUS))
		return true;

	return false;
}

static inline bool dwc3_msm_is_superspeed(struct dwc3_msm *mdwc)
{
	if (mdwc->in_host_mode)
		return dwc3_msm_is_host_superspeed(mdwc);

	return dwc3_msm_is_dev_superspeed(mdwc);
}

static int dwc3_msm_dbm_disable_updxfer(struct dwc3 *dwc, u8 usb_ep)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	u32 data;
	int dbm_ep;

	dbm_ep = find_matching_dbm_ep(mdwc, usb_ep);
	if (dbm_ep < 0)
		return dbm_ep;

	data = msm_dbm_read_reg(mdwc, DBM_DISABLE_UPDXFER);
	data |= (0x1 << dbm_ep);
	msm_dbm_write_reg(mdwc, DBM_DISABLE_UPDXFER, data);

	return 0;
}

static int dwc3_core_send_gadget_ep_cmd(struct dwc3_ep *dep, unsigned int cmd,
		struct dwc3_gadget_ep_cmd_params *params)
{
	const struct usb_endpoint_descriptor *desc = dep->endpoint.desc;
	struct dwc3		*dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	u32			timeout = 5000;
	u32			saved_config = 0;
	u32			reg;

	int			cmd_status = 0;
	int			ret = -EINVAL;

	/*
	 * When operating in USB 2.0 speeds (HS/FS), if GUSB2PHYCFG.ENBLSLPM or
	 * GUSB2PHYCFG.SUSPHY is set, it must be cleared before issuing an
	 * endpoint command.
	 *
	 * Save and clear both GUSB2PHYCFG.ENBLSLPM and GUSB2PHYCFG.SUSPHY
	 * settings. Restore them after the command is completed.
	 *
	 * DWC_usb3 3.30a and DWC_usb31 1.90a programming guide section 3.2.2
	 */
	if (dwc->gadget->speed <= USB_SPEED_HIGH) {
		reg = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
		if (unlikely(reg & DWC3_GUSB2PHYCFG_SUSPHY)) {
			saved_config |= DWC3_GUSB2PHYCFG_SUSPHY;
			reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
		}

		if (reg & DWC3_GUSB2PHYCFG_ENBLSLPM) {
			saved_config |= DWC3_GUSB2PHYCFG_ENBLSLPM;
			reg &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;
		}

		if (saved_config)
			dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0), reg);
	}

	dwc3_msm_ep_writel(dep->regs, DWC3_DEPCMDPAR0, params->param0);
	dwc3_msm_ep_writel(dep->regs, DWC3_DEPCMDPAR1, params->param1);
	dwc3_msm_ep_writel(dep->regs, DWC3_DEPCMDPAR2, params->param2);

	/*
	 * Synopsys Databook 2.60a states in section 6.3.2.5.6 of that if we're
	 * not relying on XferNotReady, we can make use of a special "No
	 * Response Update Transfer" command where we should clear both CmdAct
	 * and CmdIOC bits.
	 *
	 * With this, we don't need to wait for command completion and can
	 * straight away issue further commands to the endpoint.
	 *
	 * NOTICE: We're making an assumption that control endpoints will never
	 * make use of Update Transfer command. This is a safe assumption
	 * because we can never have more than one request at a time with
	 * Control Endpoints. If anybody changes that assumption, this chunk
	 * needs to be updated accordingly.
	 */
	if (DWC3_DEPCMD_CMD(cmd) == DWC3_DEPCMD_UPDATETRANSFER &&
			!usb_endpoint_xfer_isoc(desc))
		cmd &= ~(DWC3_DEPCMD_CMDIOC | DWC3_DEPCMD_CMDACT);
	else
		cmd |= DWC3_DEPCMD_CMDACT;

	dwc3_msm_ep_writel(dep->regs, DWC3_DEPCMD, cmd);
	do {
		reg = dwc3_msm_ep_readl(dep->regs, DWC3_DEPCMD);
		if (!(reg & DWC3_DEPCMD_CMDACT)) {
			cmd_status = DWC3_DEPCMD_STATUS(reg);

			switch (cmd_status) {
			case 0:
				ret = 0;
				break;
			case DEPEVT_TRANSFER_NO_RESOURCE:
				dev_WARN(dwc->dev, "No resource for %s\n",
					 dep->name);
				ret = -EINVAL;
				break;
			case DEPEVT_TRANSFER_BUS_EXPIRY:
				/*
				 * SW issues START TRANSFER command to
				 * isochronous ep with future frame interval. If
				 * future interval time has already passed when
				 * core receives the command, it will respond
				 * with an error status of 'Bus Expiry'.
				 *
				 * Instead of always returning -EINVAL, let's
				 * give a hint to the gadget driver that this is
				 * the case by returning -EAGAIN.
				 */
				ret = -EAGAIN;
				break;
			default:
				dev_WARN(dwc->dev, "UNKNOWN cmd status\n");
			}

			break;
		}
	} while (--timeout);

	if (timeout == 0) {
		ret = -ETIMEDOUT;
		cmd_status = -ETIMEDOUT;
	}

	if (DWC3_DEPCMD_CMD(cmd) == DWC3_DEPCMD_STARTTRANSFER) {
		if (ret == 0)
			mdwc->hw_eps[dep->number].flags |=
						DWC3_MSM_HW_EP_TRANSFER_STARTED;

		if (ret != -ETIMEDOUT) {
			u32 res_id;

			res_id = dwc3_msm_ep_readl(dep->regs, DWC3_DEPCMD);
			dep->resource_index = DWC3_DEPCMD_GET_RSC_IDX(res_id);
		}
	}

	if (saved_config) {
		reg = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
		reg |= saved_config;
		dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0), reg);
	}

	return ret;
}

static void dwc3_core_stop_active_transfer(struct dwc3_ep *dep, bool force)
{
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_gadget_ep_cmd_params params;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	u32 cmd;
	int ret;

	/*
	 * Do not allow endxfer if no transfer was started, or if the
	 * delayed ep stop is set.  If delayed ep stop is set, the
	 * endxfer is in progress by DWC3 gadget, and waiting for the
	 * SETUP stage to occur.
	 */
	if (!(mdwc->hw_eps[dep->number].flags &
			DWC3_MSM_HW_EP_TRANSFER_STARTED) ||
		(dep->flags & DWC3_EP_DELAY_STOP))
		return;

	dwc3_msm_notify_event(dwc, DWC3_CONTROLLER_NOTIFY_DISABLE_UPDXFER,
			dep->number);

	/*
	 * NOTICE: We are violating what the Databook says about the
	 * EndTransfer command. Ideally we would _always_ wait for the
	 * EndTransfer Command Completion IRQ, but that's causing too
	 * much trouble synchronizing between us and gadget driver.
	 *
	 * We have discussed this with the IP Provider and it was
	 * suggested to giveback all requests here.
	 *
	 * Note also that a similar handling was tested by Synopsys
	 * (thanks a lot Paul) and nothing bad has come out of it.
	 * In short, what we're doing is issuing EndTransfer with
	 * CMDIOC bit set and delay kicking transfer until the
	 * EndTransfer command had completed.
	 *
	 * As of IP version 3.10a of the DWC_usb3 IP, the controller
	 * supports a mode to work around the above limitation. The
	 * software can poll the CMDACT bit in the DEPCMD register
	 * after issuing a EndTransfer command. This mode is enabled
	 * by writing GUCTL2[14]. This polling is already done in the
	 * dwc3_core_send_gadget_ep_cmd() function so if the mode is
	 * enabled, the EndTransfer command will have completed upon
	 * returning from this function.
	 *
	 * This mode is NOT available on the DWC_usb31 IP.
	 */

	cmd = DWC3_DEPCMD_ENDTRANSFER;
	cmd |= force ? DWC3_DEPCMD_HIPRI_FORCERM : 0;
	cmd |= DWC3_DEPCMD_PARAM(dep->resource_index);
	memset(&params, 0, sizeof(params));
	ret = dwc3_core_send_gadget_ep_cmd(dep, cmd, &params);
	WARN_ON_ONCE(ret);
	if (ret == -ETIMEDOUT && dep->dwc->ep0state != EP0_SETUP_PHASE) {
		/*
		 * Set DWC3_EP_DELAY_STOP and DWC3_EP_TRANSFER_STARTED
		 * together, as endxfer will need to be handed over to DWC3
		 * ep0 when it moves back to the SETUP phase.  If the transfer
		 * started flag is not set, then the endxfer on GSI ep is
		 * treated as a no-op.
		 */
		dep->flags |= DWC3_EP_DELAY_STOP | DWC3_EP_TRANSFER_STARTED;
		dbg_event(0xFF, "core ENDXFER", ret);
		goto out;
	}
	dep->resource_index = 0;

	if (DWC3_IP_IS(DWC31) || DWC3_VER_IS_PRIOR(DWC3, 310A))
		udelay(100);
	/*
	 * The END_TRANSFER command will cause the controller to generate a
	 * NoStream Event, and it's not due to the host DP NoStream rejection.
	 * Ignore the next NoStream event.
	 */
	if (dep->stream_capable)
		dep->flags |= DWC3_EP_IGNORE_NEXT_NOSTREAM;
out:
	mdwc->hw_eps[dep->number].flags &= ~DWC3_MSM_HW_EP_TRANSFER_STARTED;
}

int dwc3_core_stop_hw_active_transfers(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	int i;

	for (i = 0; i < DWC3_ENDPOINTS_NUM; i++)
		if (mdwc->hw_eps[i].mode == USB_EP_GSI)
			dwc3_core_stop_active_transfer(dwc->eps[i], true);

	return 0;
}

#if IS_ENABLED(CONFIG_USB_DWC3_GADGET) || IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE)
/**
 * Configure the DBM with the BAM's data fifo.
 * This function is called by the USB BAM Driver
 * upon initialization.
 *
 * @ep - pointer to usb endpoint.
 * @addr - address of data fifo.
 * @size - size of data fifo.
 *
 */
int msm_data_fifo_config(struct usb_ep *ep, unsigned long addr,
			 u32 size, u8 dbm_ep)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	u32 lo = lower_32_bits(addr);
	u32 hi = upper_32_bits(addr);

	if (dbm_ep >= DBM_1_5_NUM_EP) {
		dev_err(mdwc->dev, "Invalid DBM EP num:%d\n", dbm_ep);
		return -EINVAL;
	}

	mdwc->hw_eps[dep->number].dbm_ep_num = dbm_ep;

	if (!mdwc->dbm_is_1p4 || sizeof(addr) > sizeof(u32)) {
		msm_dbm_write_ep_reg(mdwc, DBM_DATA_FIFO_LSB, dbm_ep, lo);
		msm_dbm_write_ep_reg(mdwc, DBM_DATA_FIFO_MSB, dbm_ep, hi);
	} else {
		msm_dbm_write_ep_reg(mdwc, DBM_DATA_FIFO, dbm_ep, addr);
	}

	msm_dbm_write_ep_reg_field(mdwc, DBM_DATA_FIFO_SIZE, dbm_ep,
		DBM_DATA_FIFO_SIZE_MASK, size);

	return 0;
}
EXPORT_SYMBOL(msm_data_fifo_config);

static int dbm_ep_unconfig(struct dwc3_msm *mdwc, u8 usb_ep);

/**
 * Configure the DBM with the USB3 core event buffer.
 * This function is called by the SNPS UDC upon initialization.
 *
 * @addr - address of the event buffer.
 * @size - size of the event buffer.
 *
 */
static int dbm_event_buffer_config(struct dwc3_msm *mdwc, u32 addr_lo,
				   u32 addr_hi, int size)
{
	dev_dbg(mdwc->dev, "Configuring event buffer\n");

	if (size < 0) {
		dev_err(mdwc->dev, "Invalid size %d\n", size);
		return -EINVAL;
	}

	/* In case event buffer is already configured, Do nothing. */
	if (msm_dbm_read_reg(mdwc, DBM_GEVNTSIZ))
		return 0;

	if (!mdwc->dbm_is_1p4 || sizeof(phys_addr_t) > sizeof(u32)) {
		msm_dbm_write_reg(mdwc, DBM_GEVNTADR_LSB, addr_lo);
		msm_dbm_write_reg(mdwc, DBM_GEVNTADR_MSB, addr_hi);
	} else {
		msm_dbm_write_reg(mdwc, DBM_GEVNTADR, addr_lo);
	}

	msm_dbm_write_reg_field(mdwc, DBM_GEVNTSIZ, DBM_GEVNTSIZ_MASK, size);

	return 0;
}

/**
 * Cleanups for msm endpoint on request complete.
 *
 * Also call original request complete.
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - pointer to usb_request instance.
 *
 * @return int - 0 on success, negative on error.
 */
static void dwc3_msm_req_complete_func(struct usb_ep *ep,
				       struct usb_request *request)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_msm_req_complete *req_complete = NULL;

	/* Find original request complete function and remove it from list */
	list_for_each_entry(req_complete, &mdwc->req_complete_list, list_item) {
		if (req_complete->req == request)
			break;
	}
	if (!req_complete || req_complete->req != request) {
		dev_err(dep->dwc->dev, "%s: could not find the request\n",
					__func__);
		return;
	}
	list_del(&req_complete->list_item);

	/*
	 * Release another one TRB to the pool since DBM queue took 2 TRBs
	 * (normal and link), and the dwc3/gadget.c :: dwc3_gadget_giveback
	 * released only one.
	 */
	dep->trb_dequeue++;

	/* Unconfigure dbm ep */
	dbm_ep_unconfig(mdwc, dep->number);

	/*
	 * If this is the last endpoint we unconfigured, than reset also
	 * the event buffers; unless unconfiguring the ep due to lpm,
	 * in which case the event buffer only gets reset during the
	 * block reset.
	 */
	if (dbm_get_num_of_eps_configured(mdwc) == 0)
		dbm_event_buffer_config(mdwc, 0, 0, 0);

	/*
	 * Call original complete function, notice that dwc->lock is already
	 * taken by the caller of this function (dwc3_gadget_giveback()).
	 */
	request->complete = req_complete->orig_complete;
	if (request->complete)
		request->complete(ep, request);

	kfree(req_complete);
}

/**
 * Reset the DBM endpoint which is linked to the given USB endpoint.
 * This function is called by the function driver upon events
 * such as transfer aborting, USB re-enumeration and USB
 * disconnection.
 *
 * @usb_ep - pointer to usb_ep instance.
 *
 * @return int - 0 on success, negative on error.
 */
int msm_dwc3_reset_dbm_ep(struct usb_ep *ep)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	int dbm_ep;

	dbm_ep = find_matching_dbm_ep(mdwc, dep->number);
	if (dbm_ep < 0)
		return dbm_ep;

	dev_dbg(mdwc->dev, "Resetting endpoint %d, DBM ep %d\n", dep->number,
			dbm_ep);

	/* Reset the dbm endpoint */
	msm_dbm_write_reg_field(mdwc, DBM_SOFT_RESET,
			DBM_SFT_RST_EPS_MASK & 1 << dbm_ep, 1);

	/*
	 * The necessary delay between asserting and deasserting the dbm ep
	 * reset is based on the number of active endpoints. If there is more
	 * than one endpoint, a 1 msec delay is required. Otherwise, a shorter
	 * delay will suffice.
	 */
	if (dbm_get_num_of_eps_configured(mdwc) > 1)
		usleep_range(1000, 1200);
	else
		udelay(10);

	msm_dbm_write_reg_field(mdwc, DBM_SOFT_RESET,
			DBM_SFT_RST_EPS_MASK & 1 << dbm_ep, 0);

	return 0;
}
EXPORT_SYMBOL(msm_dwc3_reset_dbm_ep);

static int __dwc3_msm_ebc_ep_queue(struct dwc3_ep *dep, struct dwc3_request *req)
{
	struct dwc3_gadget_ep_cmd_params params;
	u32 cmd, param1;
	int ret = 0;

	req->status = DWC3_REQUEST_STATUS_STARTED;
	list_add_tail(&req->list, &dep->started_list);
	if (dep->direction)
		param1 = 0x0;
	else
		param1 = 0x200;

	/* Now start the transfer */
	memset(&params, 0, sizeof(params));
	params.param0 = 0x8000; /* TDAddr High */
	params.param1 = param1; /* DAddr Low */

	cmd = DWC3_DEPCMD_STARTTRANSFER;
	ret = dwc3_core_send_gadget_ep_cmd(dep, cmd, &params);
	if (ret < 0) {
		dev_dbg(dep->dwc->dev,
			"%s: failed to send STARTTRANSFER command\n",
			__func__);

		list_del(&req->list);
		return ret;
	}

	return ret;
}

/**
 * Helper function.
 * See the header of the dwc3_msm_ep_queue function.
 *
 * @dwc3_ep - pointer to dwc3_ep instance.
 * @req - pointer to dwc3_request instance.
 *
 * @return int - 0 on success, negative on error.
 */
static int __dwc3_msm_dbm_ep_queue(struct dwc3_ep *dep, struct dwc3_request *req)
{
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_trb *trb;
	struct dwc3_trb *trb_link;
	struct dwc3_gadget_ep_cmd_params params;
	u32 cmd;
	int ret = 0, size;

	/* We push the request to the dep->started_list list to indicate that
	 * this request is issued with start transfer. The request will be out
	 * from this list in 2 cases. The first is that the transfer will be
	 * completed (not if the transfer is endless using a circular TRBs with
	 * link TRB). The second case is an option to do stop stransfer, this
	 * can be initiated by the function driver when calling dequeue.
	 */
	req->status = DWC3_REQUEST_STATUS_STARTED;
	list_add_tail(&req->list, &dep->started_list);

	size = dwc3_msm_read_reg(mdwc->base, DWC3_GEVNTSIZ(0));
	dbm_event_buffer_config(mdwc,
		dwc3_msm_read_reg(mdwc->base, DWC3_GEVNTADRLO(0)),
		dwc3_msm_read_reg(mdwc->base, DWC3_GEVNTADRHI(0)),
		DWC3_GEVNTSIZ_SIZE(size));

	/* First, prepare a normal TRB, point to the fake buffer */
	trb = &dep->trb_pool[dep->trb_enqueue];
	if (++dep->trb_enqueue == (DWC3_TRB_NUM - 1))
		dep->trb_enqueue = 0;
	memset(trb, 0, sizeof(*trb));

	req->trb = trb;
	req->num_trbs++;
	trb->bph = DBM_TRB_BIT | DBM_TRB_DMA | DBM_TRB_EP_NUM(dep->number);
	trb->size = DWC3_TRB_SIZE_LENGTH(req->request.length);
	trb->ctrl = DWC3_TRBCTL_NORMAL | DWC3_TRB_CTRL_HWO |
		DWC3_TRB_CTRL_CHN | (req->direction ? 0 : DWC3_TRB_CTRL_CSP);
	req->trb_dma = dwc3_trb_dma_offset(dep, trb);

	/* Second, prepare a Link TRB that points to the first TRB*/
	trb_link = &dep->trb_pool[dep->trb_enqueue];
	if (++dep->trb_enqueue == (DWC3_TRB_NUM - 1))
		dep->trb_enqueue = 0;
	memset(trb_link, 0, sizeof(*trb_link));

	trb_link->bpl = lower_32_bits(req->trb_dma);
	trb_link->bph = upper_32_bits(req->trb_dma) | DBM_TRB_BIT |
			DBM_TRB_DMA | DBM_TRB_EP_NUM(dep->number);
	trb_link->size = 0;
	trb_link->ctrl = DWC3_TRBCTL_LINK_TRB | DWC3_TRB_CTRL_HWO;

	/*
	 * Now start the transfer
	 */
	memset(&params, 0, sizeof(params));
	params.param0 = upper_32_bits(req->trb_dma); /* TDAddr High */
	params.param1 = lower_32_bits(req->trb_dma); /* DAddr Low */

	/* DBM requires IOC to be set */
	cmd = DWC3_DEPCMD_STARTTRANSFER | DWC3_DEPCMD_CMDIOC;
	ret = dwc3_core_send_gadget_ep_cmd(dep, cmd, &params);
	if (ret < 0) {
		dev_dbg(dep->dwc->dev,
			"%s: failed to send STARTTRANSFER command\n",
			__func__);

		list_del(&req->list);
		return ret;
	}

	msm_dbm_write_reg(mdwc, DBM_GEN_CFG,
				dwc3_msm_is_superspeed(mdwc) ? 1 : 0);

	return ret;
}

/**
 * Queue a usb request to the DBM endpoint.
 * This function should be called after the endpoint
 * was enabled by the ep_enable.
 *
 * This function prepares special structure of TRBs which
 * is familiar with the DBM HW, so it will possible to use
 * this endpoint in DBM mode.
 *
 * The TRBs prepared by this function, is one normal TRB
 * which point to a fake buffer, followed by a link TRB
 * that points to the first TRB.
 *
 * The API of this function follow the regular API of
 * usb_ep_queue (see usb_ep_ops in include/linuk/usb/gadget.h).
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - pointer to usb_request instance.
 * @gfp_flags - possible flags.
 *
 * @return int - 0 on success, negative on error.
 */
static int dwc3_msm_ep_queue(struct usb_ep *ep,
			     struct usb_request *request, gfp_t gfp_flags)
{
	struct dwc3_request *req = to_dwc3_request(request);
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_msm_req_complete *req_complete;
	unsigned long flags;
	int ret = 0;

	/*
	 * We must obtain the lock of the dwc3 core driver,
	 * including disabling interrupts, so we will be sure
	 * that we are the only ones that configure the HW device
	 * core and ensure that we queuing the request will finish
	 * as soon as possible so we will release back the lock.
	 */
	spin_lock_irqsave(&dwc->lock, flags);
	if (!dep->endpoint.desc) {
		dev_err(mdwc->dev,
			"%s: trying to queue request %pK to disabled ep %s\n",
			__func__, request, ep->name);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EPERM;
	}

	if (!mdwc->original_ep_ops[dep->number]) {
		dev_err(mdwc->dev,
			"ep [%s,%d] was unconfigured as msm endpoint\n",
			ep->name, dep->number);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EINVAL;
	}

	if (!request) {
		dev_err(mdwc->dev, "%s: request is NULL\n", __func__);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EINVAL;
	}

	/* HW restriction regarding TRB size (8KB) */
	if (mdwc->hw_eps[dep->number].mode == USB_EP_BAM && req->request.length < 0x2000) {
		dev_err(mdwc->dev, "%s: Min TRB size is 8KB\n", __func__);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EINVAL;
	}

	if (dep->number == 0 || dep->number == 1) {
		dev_err(mdwc->dev,
			"%s: trying to queue dbm request %pK to ep %s\n",
			__func__, request, ep->name);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EPERM;
	}

	if (dep->trb_dequeue != dep->trb_enqueue
					|| !list_empty(&dep->pending_list)
					|| !list_empty(&dep->started_list)) {
		dev_err(mdwc->dev,
			"%s: trying to queue dbm request %pK tp ep %s\n",
			__func__, request, ep->name);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EPERM;
	}
	dep->trb_dequeue = 0;
	dep->trb_enqueue = 0;

	/*
	 * Override req->complete function, but before doing that,
	 * store it's original pointer in the req_complete_list.
	 */
	req_complete = kzalloc(sizeof(*req_complete), gfp_flags);

	if (!req_complete) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -ENOMEM;
	}

	req_complete->req = request;
	req_complete->orig_complete = request->complete;
	list_add_tail(&req_complete->list_item, &mdwc->req_complete_list);
	request->complete = dwc3_msm_req_complete_func;

	dev_vdbg(dwc->dev, "%s: queuing request %pK to ep %s length %d\n",
			__func__, request, ep->name, request->length);

	if (mdwc->hw_eps[dep->number].mode == USB_EP_EBC)
		ret = __dwc3_msm_ebc_ep_queue(dep, req);
	else
		ret = __dwc3_msm_dbm_ep_queue(dep, req);
	if (ret < 0) {
		dev_err(mdwc->dev,
			"error %d after queuing %s req\n", ret,
			mdwc->hw_eps[dep->number].mode == USB_EP_EBC ? "ebc" : "dbm");
		goto err;
	}

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;

err:
	spin_unlock_irqrestore(&dwc->lock, flags);
	kfree(req_complete);
	return ret;
}

/**
 * dwc3_msm_depcfg_params - Set depcfg parameters for MSM eps
 * @ep: Endpoint being configured
 * @params: depcmd param being passed to the controller
 *
 * Initializes the dwc3_gadget_ep_cmd_params structure being passed as part of
 * the depcfg command.  This API is explicitly used for initializing the params
 * for MSM specific HW endpoints.
 *
 * Supported EP types:
 * - USB GSI
 * - USB BAM
 * - USB EBC
 */
static void dwc3_msm_depcfg_params(struct usb_ep *ep, struct dwc3_gadget_ep_cmd_params *params)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	const struct usb_endpoint_descriptor *desc = ep->desc;
	const struct usb_ss_ep_comp_descriptor *comp_desc = ep->comp_desc;

	params->param0 = DWC3_DEPCFG_EP_TYPE(usb_endpoint_type(desc))
		| DWC3_DEPCFG_MAX_PACKET_SIZE(usb_endpoint_maxp(desc));

	/* Burst size is only needed in SuperSpeed mode */
	if (dwc->gadget->speed >= USB_SPEED_SUPER) {
		u32 burst = dep->endpoint.maxburst;

		params->param0 |= DWC3_DEPCFG_BURST_SIZE(burst - 1);
	}

	if (usb_ss_max_streams(comp_desc) && usb_endpoint_xfer_bulk(desc)) {
		params->param1 |= DWC3_DEPCFG_STREAM_CAPABLE
					| DWC3_DEPCFG_STREAM_EVENT_EN;
		dep->stream_capable = true;
	}

	/* Set EP number */
	params->param1 |= DWC3_DEPCFG_EP_NUMBER(dep->number);
	if (dep->direction)
		params->param0 |= DWC3_DEPCFG_FIFO_NUMBER(dep->number >> 1);

	params->param0 |= DWC3_DEPCFG_ACTION_INIT;

	if (mdwc->hw_eps[dep->number].mode == USB_EP_GSI) {
		/* Enable XferInProgress and XferComplete Interrupts */
		params->param1 |= DWC3_DEPCFG_XFER_COMPLETE_EN;
		params->param1 |= DWC3_DEPCFG_XFER_IN_PROGRESS_EN;
		params->param1 |= DWC3_DEPCFG_FIFO_ERROR_EN;
	} else if (mdwc->hw_eps[dep->number].mode == USB_EP_EBC) {
		params->param1 |= DWC3_DEPCFG_RETRY | DWC3_DEPCFG_TRB_WB;
		params->param0 |= DWC3_DEPCFG_EBC_MODE;
	}
}

static int dwc3_msm_set_ep_config(struct dwc3_ep *dep, unsigned int action)
{
	const struct usb_ss_ep_comp_descriptor *comp_desc;
	const struct usb_endpoint_descriptor *desc;
	struct dwc3_gadget_ep_cmd_params params;
	struct usb_ep *ep = &dep->endpoint;

	comp_desc = dep->endpoint.comp_desc;
	desc = dep->endpoint.desc;

	memset(&params, 0x00, sizeof(params));
	dwc3_msm_depcfg_params(ep, &params);

	return dwc3_core_send_gadget_ep_cmd(dep, DWC3_DEPCMD_SETEPCONFIG, &params);
}

static int __dwc3_msm_ep_enable(struct dwc3_ep *dep, unsigned int action)
{
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	const struct usb_endpoint_descriptor *desc = dep->endpoint.desc;
	u32 reg;
	int ret;

	ret = dwc3_msm_set_ep_config(dep, action);
	if (ret) {
		dev_err(dwc->dev, "set_ep_config() failed for %s\n", dep->name);
		return ret;
	}

	if (!(dep->flags & DWC3_EP_ENABLED)) {
		struct dwc3_trb	*trb_st_hw;
		struct dwc3_trb	*trb_link;

		dwc3_core_resize_tx_fifos(dep);

		dep->type = usb_endpoint_type(desc);
		dep->flags |= DWC3_EP_ENABLED;

		reg = dwc3_msm_read_reg(mdwc->base, DWC3_DALEPENA);
		reg |= DWC3_DALEPENA_EP(dep->number);
		dwc3_msm_write_reg(mdwc->base, DWC3_DALEPENA, reg);

		/* Initialize the TRB ring */
		dep->trb_dequeue = 0;
		dep->trb_enqueue = 0;
		memset(dep->trb_pool, 0,
		       sizeof(struct dwc3_trb) * DWC3_TRB_NUM);

		/* Link TRB. The HWO bit is never reset */
		trb_st_hw = &dep->trb_pool[0];

		trb_link = &dep->trb_pool[DWC3_TRB_NUM - 1];
		trb_link->bpl = lower_32_bits(dwc3_trb_dma_offset(dep, trb_st_hw));
		trb_link->bph = upper_32_bits(dwc3_trb_dma_offset(dep, trb_st_hw));
		trb_link->ctrl |= DWC3_TRBCTL_LINK_TRB;
		trb_link->ctrl |= DWC3_TRB_CTRL_HWO;
	}

	return 0;
}

static int dwc3_msm_ep_enable(struct usb_ep *ep,
			      const struct usb_endpoint_descriptor *desc)
{
	struct dwc3_ep *dep;
	struct dwc3 *dwc;
	struct dwc3_msm *mdwc;
	unsigned long flags;
	int ret;

	if (!ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT) {
		pr_debug("dwc3: invalid parameters\n");
		return -EINVAL;
	}

	if (!desc->wMaxPacketSize) {
		pr_debug("dwc3: missing wMaxPacketSize\n");
		return -EINVAL;
	}

	dep = to_dwc3_ep(ep);
	dwc = dep->dwc;
	mdwc = dev_get_drvdata(dwc->dev->parent);

	if (dev_WARN_ONCE(dwc->dev, dep->flags & DWC3_EP_ENABLED,
					"%s is already enabled\n",
					dep->name))
		return 0;

	if (pm_runtime_suspended(dwc->sysdev)) {
		dev_err(dwc->dev, "fail ep_enable %s device is into LPM\n",
					dep->name);
		return -EINVAL;
	}

	spin_lock_irqsave(&dwc->lock, flags);
	ret = __dwc3_msm_ep_enable(dep, DWC3_DEPCFG_ACTION_INIT);
	dbg_event(dep->number, "ENABLE", ret);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

/**
 * Returns XferRscIndex for the EP. This is stored at StartXfer GSI EP OP
 *
 * @usb_ep - pointer to usb_ep instance.
 *
 * @return int - XferRscIndex
 */
static inline int gsi_get_xfer_index(struct usb_ep *ep)
{
	struct dwc3_ep			*dep = to_dwc3_ep(ep);

	return dep->resource_index;
}

/**
 * Fills up the GSI channel information needed in call to IPA driver
 * for GSI channel creation.
 *
 * @usb_ep - pointer to usb_ep instance.
 * @ch_info - output parameter with requested channel info
 */
static void gsi_get_channel_info(struct usb_ep *ep,
			struct gsi_channel_info *ch_info)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	int last_trb_index = 0;
	struct dwc3	*dwc = dep->dwc;
	struct usb_gsi_request *request = ch_info->ch_req;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	/* Provide physical USB addresses for DEPCMD and GEVENTCNT registers */
	ch_info->depcmd_low_addr = (u32)(mdwc->reg_phys +
				DWC3_DEP_BASE(dep->number) + DWC3_DEPCMD);

	ch_info->depcmd_hi_addr = 0;

	ch_info->xfer_ring_base_addr = dwc3_trb_dma_offset(dep,
							&dep->trb_pool[0]);
	/* Convert to multipled of 1KB */
	ch_info->const_buffer_size = request->buf_len/1024;

	/* IN direction */
	if (dep->direction) {
		/*
		 * Multiply by size of each TRB for xfer_ring_len in bytes.
		 * 2n + 2 TRBs as per GSI h/w requirement. n Xfer TRBs + 1
		 * extra Xfer TRB followed by n ZLP TRBs + 1 LINK TRB.
		 */
		ch_info->xfer_ring_len = (2 * request->num_bufs + 2) * 0x10;
		last_trb_index = 2 * request->num_bufs + 2;
	} else { /* OUT direction */
		/*
		 * Multiply by size of each TRB for xfer_ring_len in bytes.
		 * n + 1 TRBs as per GSI h/w requirement. n Xfer TRBs + 1
		 * LINK TRB.
		 */
		ch_info->xfer_ring_len = (request->num_bufs + 2) * 0x10;
		last_trb_index = request->num_bufs + 2;
	}

	/* Store last 16 bits of LINK TRB address as per GSI hw requirement */
	ch_info->last_trb_addr = (dwc3_trb_dma_offset(dep,
			&dep->trb_pool[last_trb_index - 1]) & 0x0000FFFF);
	ch_info->gevntcount_low_addr = (u32)(mdwc->reg_phys +
			DWC3_GEVNTCOUNT(request->ep_intr_num));
	ch_info->gevntcount_hi_addr = 0;

	dev_dbg(dwc->dev,
	"depcmd_laddr=%x last_trb_addr=%x gevtcnt_laddr=%x gevtcnt_haddr=%x",
		ch_info->depcmd_low_addr, ch_info->last_trb_addr,
		ch_info->gevntcount_low_addr, ch_info->gevntcount_hi_addr);
}

/**
 * Perform StartXfer on GSI EP. Stores XferRscIndex.
 *
 * @usb_ep - pointer to usb_ep instance.
 *
 * @return int - 0 on success
 */
static int gsi_startxfer_for_ep(struct usb_ep *ep, struct usb_gsi_request *req)
{
	int ret;
	struct dwc3_gadget_ep_cmd_params params;
	u32				cmd;
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3	*dwc = dep->dwc;

	memset(&params, 0, sizeof(params));
	params.param0 = GSI_TRB_ADDR_BIT_53_MASK | GSI_TRB_ADDR_BIT_55_MASK;
	params.param0 |= upper_32_bits(dwc3_trb_dma_offset(dep,
						&dep->trb_pool[0])) & 0xffff;
	params.param0 |= (req->ep_intr_num << 16);
	params.param1 = lower_32_bits(dwc3_trb_dma_offset(dep,
						&dep->trb_pool[0]));
	cmd = DWC3_DEPCMD_STARTTRANSFER;
	cmd |= DWC3_DEPCMD_PARAM(0);
	ret = dwc3_core_send_gadget_ep_cmd(dep, cmd, &params);

	if (ret < 0)
		dev_dbg(dwc->dev, "Fail StrtXfr on GSI EP#%d\n", dep->number);
	dev_dbg(dwc->dev, "XferRsc = %x", dep->resource_index);
	return ret;
}

/**
 * Store Ring Base and Doorbell Address for GSI EP
 * for GSI channel creation.
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - USB GSI request to get Doorbell address obtained from IPA driver
 */
static void gsi_store_ringbase_dbl_info(struct usb_ep *ep,
			struct usb_gsi_request *request)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3	*dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	int n = request->ep_intr_num - 1;

	dwc3_msm_write_reg(mdwc->base, GSI_RING_BASE_ADDR_L(mdwc->gsi_reg, n),
		lower_32_bits(dwc3_trb_dma_offset(dep, &dep->trb_pool[0])));
	dwc3_msm_write_reg(mdwc->base, GSI_RING_BASE_ADDR_H(mdwc->gsi_reg, n),
		upper_32_bits(dwc3_trb_dma_offset(dep, &dep->trb_pool[0])));

	dev_dbg(mdwc->dev, "Ring Base Addr %d: %x (LSB) %x (MSB)\n", n,
		lower_32_bits(dwc3_trb_dma_offset(dep, &dep->trb_pool[0])),
		upper_32_bits(dwc3_trb_dma_offset(dep, &dep->trb_pool[0])));

	if (!request->mapped_db_reg_phs_addr_lsb) {
		request->mapped_db_reg_phs_addr_lsb =
			dma_map_resource(dwc->sysdev,
				(phys_addr_t)request->db_reg_phs_addr_lsb,
				PAGE_SIZE, DMA_BIDIRECTIONAL, 0);
		if (dma_mapping_error(dwc->sysdev,
				request->mapped_db_reg_phs_addr_lsb))
			dev_err(mdwc->dev, "mapping error for db_reg_phs_addr_lsb\n");
	}

	dev_dbg(mdwc->dev, "ep:%s dbl_addr_lsb:%x mapped_dbl_addr_lsb:%llx\n",
		ep->name, request->db_reg_phs_addr_lsb,
		(unsigned long long)request->mapped_db_reg_phs_addr_lsb);

	dbg_log_string("ep:%s dbl_addr_lsb:%x mapped_addr:%llx\n",
		ep->name, request->db_reg_phs_addr_lsb,
		(unsigned long long)request->mapped_db_reg_phs_addr_lsb);

	/*
	 * Replace dummy doorbell address with real one as IPA connection
	 * is setup now and GSI must be ready to handle doorbell updates.
	 */
	dwc3_msm_write_reg(mdwc->base, GSI_DBL_ADDR_H(mdwc->gsi_reg, n),
		upper_32_bits(request->mapped_db_reg_phs_addr_lsb));

	dwc3_msm_write_reg(mdwc->base, GSI_DBL_ADDR_L(mdwc->gsi_reg, n),
		lower_32_bits(request->mapped_db_reg_phs_addr_lsb));

	dev_dbg(mdwc->dev, "GSI DB Addr %d: %pad\n", n,
		&request->mapped_db_reg_phs_addr_lsb);
}

/**
 * Rings Doorbell for GSI Channel
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - pointer to GSI request. This is used to pass in the
 * address of the GSI doorbell obtained from IPA driver
 */
static void gsi_ring_db(struct usb_ep *ep, struct usb_gsi_request *request)
{
	void __iomem *gsi_dbl_address_lsb;
	void __iomem *gsi_dbl_address_msb;
	dma_addr_t trb_dma;
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3	*dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	int num_trbs = (dep->direction) ? (2 * (request->num_bufs) + 2)
					: (request->num_bufs + 2);

	gsi_dbl_address_lsb = ioremap(request->db_reg_phs_addr_lsb,
				sizeof(u32));
	if (!gsi_dbl_address_lsb) {
		dev_err(mdwc->dev, "Failed to map GSI DBL address LSB 0x%x\n",
				request->db_reg_phs_addr_lsb);
		return;
	}

	gsi_dbl_address_msb = ioremap(request->db_reg_phs_addr_msb,
				sizeof(u32));
	if (!gsi_dbl_address_msb) {
		dev_err(mdwc->dev, "Failed to map GSI DBL address MSB 0x%x\n",
				request->db_reg_phs_addr_msb);
		iounmap(gsi_dbl_address_lsb);
		return;
	}

	trb_dma = dwc3_trb_dma_offset(dep, &dep->trb_pool[num_trbs-1]);
	dev_dbg(mdwc->dev, "Writing link TRB addr: %pad to %pK (lsb:%x msb:%x) for ep:%s\n",
		&trb_dma, gsi_dbl_address_lsb, request->db_reg_phs_addr_lsb,
		request->db_reg_phs_addr_msb, ep->name);

	dbg_log_string("ep:%s link TRB addr:%pad db_lsb:%pad db_msb:%pad\n",
		ep->name, &trb_dma, &request->db_reg_phs_addr_lsb,
		&request->db_reg_phs_addr_msb);

	writel_relaxed(lower_32_bits(trb_dma), gsi_dbl_address_lsb);
	writel_relaxed(upper_32_bits(trb_dma), gsi_dbl_address_msb);

	iounmap(gsi_dbl_address_lsb);
	iounmap(gsi_dbl_address_msb);
}

/**
 * Sets HWO bit for TRBs and performs UpdateXfer for OUT EP.
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - pointer to GSI request. Used to determine num of TRBs for OUT EP.
 *
 * @return int - 0 on success
 */
static int gsi_updatexfer_for_ep(struct usb_ep *ep,
					struct usb_gsi_request *request)
{
	int i;
	int ret;
	u32				cmd;
	int num_trbs = request->num_bufs + 1;
	struct dwc3_trb *trb;
	struct dwc3_gadget_ep_cmd_params params;
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;

	for (i = 0; i < num_trbs - 1; i++) {
		trb = &dep->trb_pool[i];
		trb->ctrl |= DWC3_TRB_CTRL_HWO;
	}

	memset(&params, 0, sizeof(params));
	cmd = DWC3_DEPCMD_UPDATETRANSFER;
	cmd |= DWC3_DEPCMD_PARAM(dep->resource_index);
	ret = dwc3_core_send_gadget_ep_cmd(dep, cmd, &params);
	if (ret < 0)
		dev_dbg(dwc->dev, "UpdateXfr fail on GSI EP#%d\n", dep->number);
	return ret;
}

/**
 * Allocates Buffers and TRBs. Configures TRBs for GSI EPs.
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - pointer to GSI request.
 *
 * @return int - 0 on success
 */
static int gsi_prepare_trbs(struct usb_ep *ep, struct usb_gsi_request *req)
{
	int i = 0;
	size_t len;
	dma_addr_t buffer_addr;
	dma_addr_t trb0_dma;
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3		*dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_trb *trb;
	int num_trbs = (dep->direction) ? (2 * (req->num_bufs) + 2)
					: (req->num_bufs + 2);
	struct scatterlist *sg;
	struct sg_table *sgt;

	/* Allocate TRB buffers */

	len = req->buf_len * req->num_bufs;
	req->buf_base_addr = dma_alloc_coherent(dwc->sysdev, len, &req->dma,
					GFP_KERNEL);
	if (!req->buf_base_addr)
		return -ENOMEM;

	dma_get_sgtable(dwc->sysdev, &req->sgt_data_buff, req->buf_base_addr,
			req->dma, len);

	buffer_addr = req->dma;
	dbg_log_string("TRB buffer_addr = %pad buf_len = %zu\n", &buffer_addr,
				req->buf_len);

	/* Allocate and configure TRBs */
	dep->trb_pool = dma_alloc_coherent(dwc->sysdev,
				num_trbs * sizeof(struct dwc3_trb),
				&dep->trb_pool_dma, GFP_KERNEL);

	if (!dep->trb_pool)
		goto free_trb_buffer;

	memset(dep->trb_pool, 0, num_trbs * sizeof(struct dwc3_trb));

	trb0_dma = dwc3_trb_dma_offset(dep, &dep->trb_pool[0]);

	mdwc->hw_eps[dep->number].num_trbs = num_trbs;
	dma_get_sgtable(dwc->sysdev, &req->sgt_trb_xfer_ring, dep->trb_pool,
		dep->trb_pool_dma, num_trbs * sizeof(struct dwc3_trb));

	sgt = &req->sgt_trb_xfer_ring;
	dev_dbg(dwc->dev, "%s(): trb_pool:%pK trb_pool_dma:%lx\n",
		__func__, dep->trb_pool, (unsigned long)dep->trb_pool_dma);

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		dev_dbg(dwc->dev,
			"%i: page_link:%lx offset:%x length:%x address:%lx\n",
			i, sg->page_link, sg->offset, sg->length,
			(unsigned long)sg->dma_address);

	/* IN direction */
	if (dep->direction) {
		trb = &dep->trb_pool[0];
		/* Set up first n+1 TRBs for ZLPs */
		for (i = 0; i < req->num_bufs + 1; i++, trb++)
			trb->ctrl = DWC3_TRBCTL_NORMAL | DWC3_TRB_CTRL_IOC;

		/* Setup next n TRBs pointing to valid buffers */
		for (; i < num_trbs - 1; i++, trb++) {
			trb->bpl = lower_32_bits(buffer_addr);
			trb->bph = upper_32_bits(buffer_addr);
			trb->ctrl = DWC3_TRBCTL_NORMAL | DWC3_TRB_CTRL_IOC;
			buffer_addr += req->buf_len;
		}

		/* Set up the Link TRB at the end */
		trb->bpl = lower_32_bits(trb0_dma);
		trb->bph = upper_32_bits(trb0_dma) & 0xffff;
		trb->bph |= (1 << 23) | (1 << 21) | (req->ep_intr_num << 16);
		trb->ctrl = DWC3_TRBCTL_LINK_TRB | DWC3_TRB_CTRL_HWO;
	} else { /* OUT direction */
		/* Start ring with LINK TRB pointing to second entry */
		trb = &dep->trb_pool[0];
		trb->bpl = lower_32_bits(trb0_dma + sizeof(*trb));
		trb->bph = upper_32_bits(trb0_dma + sizeof(*trb));
		trb->ctrl = DWC3_TRBCTL_LINK_TRB;

		/* Setup next n-1 TRBs pointing to valid buffers */
		for (i = 1, trb++; i < num_trbs - 1; i++, trb++) {
			trb->bpl = lower_32_bits(buffer_addr);
			trb->bph = upper_32_bits(buffer_addr);
			trb->size = req->buf_len;
			buffer_addr += req->buf_len;
			trb->ctrl = DWC3_TRBCTL_NORMAL | DWC3_TRB_CTRL_IOC
				| DWC3_TRB_CTRL_CSP | DWC3_TRB_CTRL_ISP_IMI;
		}

		/* Set up the Link TRB at the end */
		trb->bpl = lower_32_bits(trb0_dma);
		trb->bph = upper_32_bits(trb0_dma) & 0xffff;
		trb->bph |= (1 << 23) | (1 << 21) | (req->ep_intr_num << 16);
		trb->ctrl = DWC3_TRBCTL_LINK_TRB | DWC3_TRB_CTRL_HWO;
	}

	dev_dbg(dwc->dev, "%s: Initialized TRB Ring for %s\n",
					__func__, dep->name);

	return 0;

free_trb_buffer:
	dma_free_coherent(dwc->sysdev, len, req->buf_base_addr, req->dma);
	req->buf_base_addr = NULL;
	sg_free_table(&req->sgt_data_buff);
	return -ENOMEM;
}

/**
 * Frees TRBs and buffers for GSI EPs.
 *
 * @usb_ep - pointer to usb_ep instance.
 *
 */
static void gsi_free_trbs(struct usb_ep *ep, struct usb_gsi_request *req)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	if (mdwc->hw_eps[dep->number].mode != USB_EP_GSI)
		return;

	/*  Free TRBs and TRB pool for EP */
	if (dep->trb_pool_dma) {
		dma_free_coherent(dwc->sysdev,
			mdwc->hw_eps[dep->number].num_trbs * sizeof(struct dwc3_trb),
			dep->trb_pool,
			dep->trb_pool_dma);
		dep->trb_pool = NULL;
		dep->trb_pool_dma = 0;
	}
	sg_free_table(&req->sgt_trb_xfer_ring);

	/* free TRB buffers */
	dma_free_coherent(dwc->sysdev, req->buf_len * req->num_bufs,
		req->buf_base_addr, req->dma);
	req->buf_base_addr = NULL;
	sg_free_table(&req->sgt_data_buff);
}
/**
 * Configures GSI EPs. For GSI EPs we need to set interrupter numbers.
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - pointer to GSI request.
 */
static void gsi_configure_ep(struct usb_ep *ep, struct usb_gsi_request *request)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3		*dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_gadget_ep_cmd_params params;
	const struct usb_endpoint_descriptor *desc = ep->desc;
	const struct usb_ss_ep_comp_descriptor *comp_desc = ep->comp_desc;
	int n = request->ep_intr_num - 1;
	u32 reg;

	/* setup dummy doorbell as IPA connection isn't setup yet */
	dwc3_msm_write_reg(mdwc->base, GSI_DBL_ADDR_H(mdwc->gsi_reg, n),
			upper_32_bits(mdwc->dummy_gsi_db_dma));
	dwc3_msm_write_reg(mdwc->base, GSI_DBL_ADDR_L(mdwc->gsi_reg, n),
			lower_32_bits(mdwc->dummy_gsi_db_dma));
	dev_dbg(mdwc->dev, "Dummy DB Addr %pK: %llx\n",
		&mdwc->dummy_gsi_db, mdwc->dummy_gsi_db_dma);

	memset(&params, 0x00, sizeof(params));

	dwc3_msm_depcfg_params(ep, &params);

	/* Set interrupter number for GSI endpoints */
	params.param1 |= DWC3_DEPCFG_INT_NUM(request->ep_intr_num);

	dev_dbg(mdwc->dev, "Set EP config to params = %x %x %x, for %s\n",
	params.param0, params.param1, params.param2, dep->name);

	dwc3_core_send_gadget_ep_cmd(dep, DWC3_DEPCMD_SETEPCONFIG, &params);

	if (!(dep->flags & DWC3_EP_ENABLED)) {
		dwc3_core_resize_tx_fifos(dep);
		dep->endpoint.desc = desc;
		dep->endpoint.comp_desc = comp_desc;
		dep->type = usb_endpoint_type(desc);
		dep->flags |= DWC3_EP_ENABLED;
		reg = dwc3_msm_read_reg(mdwc->base, DWC3_DALEPENA);
		reg |= DWC3_DALEPENA_EP(dep->number);
		dwc3_msm_write_reg(mdwc->base, DWC3_DALEPENA, reg);
	}

}

/**
 * Enables USB wrapper for GSI
 *
 * @usb_ep - pointer to usb_ep instance.
 */
static void gsi_enable(struct usb_ep *ep)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	dwc3_msm_write_reg_field(mdwc->base, GSI_GENERAL_CFG_REG(mdwc->gsi_reg),
		GSI_CLK_EN_MASK, 1);
	dwc3_msm_write_reg_field(mdwc->base, GSI_GENERAL_CFG_REG(mdwc->gsi_reg),
		GSI_RESTART_DBL_PNTR_MASK, 1);
	dwc3_msm_write_reg_field(mdwc->base, GSI_GENERAL_CFG_REG(mdwc->gsi_reg),
		GSI_RESTART_DBL_PNTR_MASK, 0);
	dev_dbg(mdwc->dev, "%s: Enable GSI\n", __func__);
	dwc3_msm_write_reg_field(mdwc->base, GSI_GENERAL_CFG_REG(mdwc->gsi_reg),
		GSI_EN_MASK, 1);
}

/**
 * Block or allow doorbell towards GSI
 *
 * @usb_ep - pointer to usb_ep instance.
 * @request - pointer to GSI request. In this case num_bufs is used as a bool
 * to set or clear the doorbell bit
 */
static void gsi_set_clear_dbell(struct usb_ep *ep,
					bool block_db)
{

	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	dwc3_msm_write_reg_field(mdwc->base, GSI_GENERAL_CFG_REG(mdwc->gsi_reg),
		BLOCK_GSI_WR_GO_MASK, block_db);
}

/**
 * Performs necessary checks before stopping GSI channels
 *
 * @usb_ep - pointer to usb_ep instance to access DWC3 regs
 */
static bool gsi_check_ready_to_suspend(struct dwc3_msm *mdwc)
{
	u32	timeout = 1500;

	while (dwc3_msm_read_reg_field(mdwc->base,
		GSI_IF_STS(mdwc->gsi_reg), GSI_WR_CTRL_STATE_MASK)) {
		if (!timeout--) {
			dev_err(mdwc->dev,
			"Unable to suspend GSI ch. WR_CTRL_STATE != 0\n");
			return false;
		}
	}

	return true;
}

static inline const char *gsi_op_to_string(unsigned int op)
{
	if (op < ARRAY_SIZE(gsi_op_strings))
		return gsi_op_strings[op];

	return "Invalid";
}

/**
 * Performs GSI operations or GSI EP related operations.
 *
 * @usb_ep - pointer to usb_ep instance.
 * @op_data - pointer to opcode related data.
 * @op - GSI related or GSI EP related op code.
 *
 * @return int - 0 on success, negative on error.
 * Also returns XferRscIdx for GSI_EP_OP_GET_XFER_IDX.
 */
int usb_gsi_ep_op(struct usb_ep *ep, void *op_data, enum gsi_ep_op op)
{
	u32 ret = 0;
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct usb_gsi_request *request;
	struct gsi_channel_info *ch_info;
	bool block_db;
	unsigned long flags;

	dbg_log_string("%s(%d):%s", ep->name, dep->number >> 1,
			gsi_op_to_string(op));

	switch (op) {
	case GSI_EP_OP_PREPARE_TRBS:
		if (!dwc->pullups_connected) {
			dbg_log_string("No Pullup\n");
			return -ESHUTDOWN;
		}

		dwc3_free_trb_pool(dep);
		request = (struct usb_gsi_request *)op_data;
		ret = gsi_prepare_trbs(ep, request);
		break;
	case GSI_EP_OP_FREE_TRBS:
		request = (struct usb_gsi_request *)op_data;
		gsi_free_trbs(ep, request);
		dwc3_alloc_trb_pool(dep);
		break;
	case GSI_EP_OP_CONFIG:
		if (!dwc->pullups_connected) {
			dbg_log_string("No Pullup\n");
			return -ESHUTDOWN;
		}

		request = (struct usb_gsi_request *)op_data;
		spin_lock_irqsave(&dwc->lock, flags);
		gsi_configure_ep(ep, request);
		spin_unlock_irqrestore(&dwc->lock, flags);
		break;
	case GSI_EP_OP_STARTXFER:
		if (!dwc->pullups_connected) {
			dbg_log_string("No Pullup\n");
			return -ESHUTDOWN;
		}

		request = (struct usb_gsi_request *)op_data;
		spin_lock_irqsave(&dwc->lock, flags);
		ret = gsi_startxfer_for_ep(ep, request);
		spin_unlock_irqrestore(&dwc->lock, flags);
		break;
	case GSI_EP_OP_GET_XFER_IDX:
		ret = gsi_get_xfer_index(ep);
		break;
	case GSI_EP_OP_STORE_DBL_INFO:
		request = (struct usb_gsi_request *)op_data;
		gsi_store_ringbase_dbl_info(ep, request);
		break;
	case GSI_EP_OP_ENABLE_GSI:
		if (!dwc->pullups_connected) {
			dbg_log_string("No Pullup\n");
			return -ESHUTDOWN;
		}

		gsi_enable(ep);
		break;
	case GSI_EP_OP_GET_CH_INFO:
		ch_info = (struct gsi_channel_info *)op_data;
		gsi_get_channel_info(ep, ch_info);
		break;
	case GSI_EP_OP_RING_DB:
		if (!dwc->pullups_connected) {
			dbg_log_string("No Pullup\n");
			return -ESHUTDOWN;
		}

		request = (struct usb_gsi_request *)op_data;
		gsi_ring_db(ep, request);
		break;
	case GSI_EP_OP_UPDATEXFER:
		request = (struct usb_gsi_request *)op_data;
		spin_lock_irqsave(&dwc->lock, flags);
		ret = gsi_updatexfer_for_ep(ep, request);
		spin_unlock_irqrestore(&dwc->lock, flags);
		break;
	case GSI_EP_OP_ENDXFER:
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_core_stop_active_transfer(dep, true);
		spin_unlock_irqrestore(&dwc->lock, flags);
		break;
	case GSI_EP_OP_SET_CLR_BLOCK_DBL:
		block_db = *((bool *)op_data);
		gsi_set_clear_dbell(ep, block_db);
		break;
	case GSI_EP_OP_CHECK_FOR_SUSPEND:
		ret = gsi_check_ready_to_suspend(mdwc);
		break;
	case GSI_EP_OP_DISABLE:
		/*
		 * Explicitly stop active transfers, as DWC3 core no longer
		 * knows about the DEP flags for GSI based EPs.
		 */
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_core_stop_active_transfer(dep, true);
		spin_unlock_irqrestore(&dwc->lock, flags);
		ret = ep->ops->disable(ep);
		break;
	default:
		dev_err(mdwc->dev, "%s: Invalid opcode GSI EP\n", __func__);
	}

	return ret;
}
EXPORT_SYMBOL(usb_gsi_ep_op);

/**
 * Configure a USB DBM ep to work in BAM mode.
 *
 * @usb_ep - USB physical EP number.
 * @producer - producer/consumer.
 * @disable_wb - disable write back to system memory.
 * @internal_mem - use internal USB memory for data fifo.
 * @ioc - enable interrupt on completion.
 *
 * @return int - DBM ep number.
 */
static int dbm_ep_config(struct dwc3_msm *mdwc, u8 usb_ep, u8 bam_pipe,
		bool producer, bool disable_wb, bool internal_mem, bool ioc)
{
	int dbm_ep;
	u32 ep_cfg;
	u32 data;

	dev_dbg(mdwc->dev, "Configuring DBM ep\n");

	dbm_ep = find_matching_dbm_ep(mdwc, usb_ep);
	if (dbm_ep < 0)
		return dbm_ep;

	/* Due to HW issue, EP 7 can be set as IN EP only */
	if (!mdwc->dbm_is_1p4 && dbm_ep == 7 && producer) {
		pr_err("last DBM EP can't be OUT EP\n");
		return -ENODEV;
	}

	/* Set ioc bit for dbm_ep if needed */
	msm_dbm_write_reg_field(mdwc, DBM_DBG_CNFG,
		DBM_ENABLE_IOC_MASK & 1 << dbm_ep, ioc ? 1 : 0);

	ep_cfg = (producer ? DBM_PRODUCER : 0) |
		(disable_wb ? DBM_DISABLE_WB : 0) |
		(internal_mem ? DBM_INT_RAM_ACC : 0);

	msm_dbm_write_ep_reg_field(mdwc, DBM_EP_CFG, dbm_ep,
		DBM_PRODUCER | DBM_DISABLE_WB | DBM_INT_RAM_ACC, ep_cfg >> 8);

	msm_dbm_write_ep_reg_field(mdwc, DBM_EP_CFG, dbm_ep, USB3_EPNUM,
		usb_ep);

	if (mdwc->dbm_is_1p4) {
		msm_dbm_write_ep_reg_field(mdwc, DBM_EP_CFG, dbm_ep,
				DBM_BAM_PIPE_NUM, bam_pipe);
		msm_dbm_write_reg_field(mdwc, DBM_PIPE_CFG, 0x000000ff, 0xe4);
	}

	msm_dbm_write_ep_reg_field(mdwc, DBM_EP_CFG, dbm_ep, DBM_EN_EP, 1);

	data = msm_dbm_read_reg(mdwc, DBM_DISABLE_UPDXFER);
	data &= ~(0x1 << dbm_ep);
	msm_dbm_write_reg(mdwc, DBM_DISABLE_UPDXFER, data);

	return dbm_ep;
}

static int msm_ep_clear_ebc_trbs(struct usb_ep *ep)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_hw_ep *edep;

	edep = &mdwc->hw_eps[dep->number];
	if (edep->ebc_trb_pool) {
		memunmap(edep->ebc_trb_pool);
		edep->ebc_trb_pool = NULL;
	}

	return 0;
}

static int msm_ep_setup_ebc_trbs(struct usb_ep *ep, struct usb_request *req)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_hw_ep *edep;
	struct dwc3_trb *trb;
	u32 desc_offset = 0, scan_offset = 0x4000, phys_base;
	int i, num_trbs;

	if (!mdwc->ebc_desc_addr) {
		dev_err(mdwc->dev, "%s: ebc_desc_addr not specified\n", __func__);
		return -EINVAL;
	}

	if (!dep->direction) {
		desc_offset = 0x200;
		scan_offset = 0x8000;
	}

	edep = &mdwc->hw_eps[dep->number];
	phys_base = mdwc->ebc_desc_addr + desc_offset;
	num_trbs = req->length / EBC_TRB_SIZE;
	mdwc->hw_eps[dep->number].num_trbs = num_trbs;
	edep->ebc_trb_pool = memremap(phys_base,
				      num_trbs * sizeof(struct dwc3_trb),
				      MEMREMAP_WT);
	if (!edep->ebc_trb_pool)
		return -ENOMEM;

	for (i = 0; i < num_trbs; i++) {
		trb = &edep->ebc_trb_pool[i];
		memset(trb, 0, sizeof(*trb));

		/* Setup n TRBs pointing to valid buffers */
		trb->bpl = scan_offset;
		trb->bph = 0x8000;
		trb->size = EBC_TRB_SIZE;
		trb->ctrl = DWC3_TRBCTL_NORMAL | DWC3_TRB_CTRL_CHN |
				DWC3_TRB_CTRL_HWO;
		if (i == (num_trbs-1)) {
			trb->bpl = desc_offset;
			trb->bph = 0x8000;
			trb->size = 0;
			trb->ctrl = DWC3_TRBCTL_LINK_TRB | DWC3_TRB_CTRL_HWO;
		}
		scan_offset += trb->size;
	}

	return 0;
}

static int ebc_ep_config(struct usb_ep *ep, struct usb_request *request)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	u32 reg, ep_num;
	int ret;

	reg = dwc3_msm_read_reg(mdwc->base, LPC_REG);

	switch (dwc3_msm_read_reg(mdwc->base, DWC3_DSTS) & DWC3_DSTS_CONNECTSPD) {
	case DWC3_DSTS_SUPERSPEED_PLUS:
		reg |= LPC_SSP_MODE;
		break;
	case DWC3_DSTS_SUPERSPEED:
		reg |= LPC_SPEED_INDICATOR;
		break;
	default:
		reg &= ~(LPC_SSP_MODE | LPC_SPEED_INDICATOR);
		break;
	}

	dwc3_msm_write_reg(mdwc->base, LPC_REG, reg);
	ret = msm_ep_setup_ebc_trbs(ep, request);
	if (ret < 0) {
		dev_err(mdwc->dev, "error %d setting up ebc trbs\n", ret);
		return ret;
	}

	ep_num = !dep->direction ? dep->number + 15 :
				   dep->number >> 1;
	reg = dwc3_msm_read_reg(mdwc->base, LPC_SCAN_MASK);
	reg |= BIT(ep_num);
	dwc3_msm_write_reg(mdwc->base, LPC_SCAN_MASK, reg);

	reg = dwc3_msm_read_reg(mdwc->base, LPC_REG);
	reg |= LPC_BUS_CLK_EN;
	dwc3_msm_write_reg(mdwc->base, LPC_REG, reg);

	reg = dwc3_msm_read_reg(mdwc->base, USB30_MODE_SEL_REG);
	reg |= USB30_QDSS_MODE_SEL;
	dwc3_msm_write_reg(mdwc->base, USB30_MODE_SEL_REG, reg);

	return 0;
}

/**
 * Configure MSM endpoint.
 * This function do specific configurations
 * to an endpoint which need specific implementaion
 * in the MSM architecture.
 *
 * This function should be called by usb function/class
 * layer which need a support from the specific MSM HW
 * which wrap the USB3 core. (like EBC or DBM specific endpoints)
 *
 * @ep - a pointer to some usb_ep instance
 *
 * @return int - 0 on success, negetive on error.
 */
int msm_ep_config(struct usb_ep *ep, struct usb_request *request, u32 bam_opts)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);

	if (mdwc->hw_eps[dep->number].mode == USB_EP_EBC) {
		ret = ebc_ep_config(ep, request);
		if (ret < 0) {
			dev_err(mdwc->dev,
				"error %d after calling ebc_ep_config\n", ret);
			spin_unlock_irqrestore(&dwc->lock, flags);
			return ret;
		}
	} else {
		/* Configure the DBM endpoint if required. */
		ret = dbm_ep_config(mdwc, dep->number,
				bam_opts & MSM_PIPE_ID_MASK,
				bam_opts & MSM_PRODUCER,
				bam_opts & MSM_DISABLE_WB,
				bam_opts & MSM_INTERNAL_MEM,
				bam_opts & MSM_ETD_IOC);
		if (ret < 0) {
			dev_err(mdwc->dev,
				"error %d after calling dbm_ep_config\n", ret);
			spin_unlock_irqrestore(&dwc->lock, flags);
			return ret;
		}
	}
	mdwc->hw_eps[dep->number].dep = dep;
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}
EXPORT_SYMBOL(msm_ep_config);

static int dbm_ep_unconfig(struct dwc3_msm *mdwc, u8 usb_ep)
{
	int dbm_ep;
	u32 data;

	dev_dbg(mdwc->dev, "Unconfiguring DB ep\n");

	dbm_ep = find_matching_dbm_ep(mdwc, usb_ep);
	if (dbm_ep < 0)
		return dbm_ep;

	mdwc->hw_eps[usb_ep].dbm_ep_num = 0;
	data = msm_dbm_read_ep_reg(mdwc, DBM_EP_CFG, dbm_ep);
	data &= (~0x1);
	msm_dbm_write_ep_reg(mdwc, DBM_EP_CFG, dbm_ep, data);

	/*
	 * ep_soft_reset is not required during disconnect as pipe reset on
	 * next connect will take care of the same.
	 */
	return 0;
}

/**
 * Un-configure MSM endpoint.
 * Tear down configurations done in the
 * dwc3_msm_ep_config function.
 *
 * @ep - a pointer to some usb_ep instance
 *
 * @return int - 0 on success, negative on error.
 */
int msm_ep_unconfig(struct usb_ep *ep)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	unsigned long flags;
	u32 reg, ep_num;

	spin_lock_irqsave(&dwc->lock, flags);
	if (mdwc->hw_eps[dep->number].mode == USB_EP_EBC) {
		ep_num = !dep->direction ? dep->number + 15 :
					   dep->number >> 1;
		reg = dwc3_msm_read_reg(mdwc->base, LPC_SCAN_MASK);
		reg &= ~BIT(ep_num);
		dwc3_msm_write_reg(mdwc->base, LPC_SCAN_MASK, reg);

		dwc3_msm_write_reg(mdwc->base, LPC_SCAN_MASK, 0);
		reg = dwc3_msm_read_reg(mdwc->base, LPC_REG);
		reg &= ~LPC_BUS_CLK_EN;

		dwc3_msm_write_reg(mdwc->base, LPC_REG, reg);
		msm_ep_clear_ebc_trbs(ep);
	} else {
		if (dep->trb_dequeue == dep->trb_enqueue &&
		    list_empty(&dep->pending_list) &&
		    list_empty(&dep->started_list)) {
			dev_dbg(mdwc->dev,
				"%s: request is not queued, disable DBM ep for ep %s\n",
				__func__, ep->name);
			/* Unconfigure dbm ep */
			dbm_ep_unconfig(mdwc, dep->number);


			/*
			 * If this is the last endpoint we unconfigured, than reset also
			 * the event buffers; unless unconfiguring the ep due to lpm,
			 * in which case the event buffer only gets reset during the
			 * block reset.
			 */
			if (dbm_get_num_of_eps_configured(mdwc) == 0)
				dbm_event_buffer_config(mdwc, 0, 0, 0);
		}
	}

	mdwc->hw_eps[dep->number].dep = 0;
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}
EXPORT_SYMBOL(msm_ep_unconfig);

/**
 * msm_ep_clear_ops - Restore default endpoint operations
 * @ep: The endpoint to restore
 *
 * Resets the usb endpoint operations to the default callbacks previously saved
 * when calling msm_ep_update_ops.
 */
int msm_ep_clear_ops(struct usb_ep *ep)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct usb_ep_ops *old_ep_ops;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);

	/* Restore original ep ops */
	if (!mdwc->original_ep_ops[dep->number]) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		dev_err(mdwc->dev,
			"ep [%s,%d] was not configured as msm endpoint\n",
			ep->name, dep->number);
		return -EINVAL;
	}
	old_ep_ops = (struct usb_ep_ops *)ep->ops;
	ep->ops = mdwc->original_ep_ops[dep->number];
	mdwc->original_ep_ops[dep->number] = NULL;
	kfree(old_ep_ops);

	spin_unlock_irqrestore(&dwc->lock, flags);
	return 0;
}
EXPORT_SYMBOL(msm_ep_clear_ops);

/**
 * msm_ep_update_ops - Override default USB ep ops w/ MSM specific ops
 * @ep: The endpoint to override
 *
 * Replaces the default endpoint operations with MSM specific operations for
 * handling HW based endpoints, such as DBM or EBC eps.  This does not depend
 * on calling msm_ep_config beforehand.
 */
int msm_ep_update_ops(struct usb_ep *ep)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct usb_ep_ops *new_ep_ops;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);

	/* Save original ep ops for future restore*/
	if (mdwc->original_ep_ops[dep->number]) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		dev_err(mdwc->dev,
			"ep [%s,%d] already configured as msm endpoint\n",
			ep->name, dep->number);
		return -EPERM;
	}
	mdwc->original_ep_ops[dep->number] = ep->ops;

	/* Set new usb ops as we like */
	new_ep_ops = kzalloc(sizeof(struct usb_ep_ops), GFP_ATOMIC);
	if (!new_ep_ops) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -ENOMEM;
	}

	(*new_ep_ops) = (*ep->ops);
	new_ep_ops->queue = dwc3_msm_ep_queue;
	new_ep_ops->enable = dwc3_msm_ep_enable;

	ep->ops = new_ep_ops;

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}
EXPORT_SYMBOL(msm_ep_update_ops);

int msm_ep_set_mode(struct usb_ep *ep, enum usb_hw_ep_mode mode)
{
	struct dwc3_ep *dep = to_dwc3_ep(ep);
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	/* Reset MSM HW ep parameters for subsequent uses */
	if (mode == USB_EP_NONE)
		memset(&mdwc->hw_eps[dep->number], 0,
			sizeof(mdwc->hw_eps[dep->number]));

	mdwc->hw_eps[dep->number].mode = mode;

	return 0;
}
EXPORT_SYMBOL(msm_ep_set_mode);

#endif /* (CONFIG_USB_DWC3_GADGET) || (CONFIG_USB_DWC3_DUAL_ROLE) */

static void dwc3_resume_work(struct work_struct *w);

static void dwc3_restart_usb_work(struct work_struct *w)
{
	struct dwc3_msm *mdwc = container_of(w, struct dwc3_msm,
						restart_usb_work);
	unsigned int timeout = 50;

	if (atomic_read(&mdwc->in_lpm) || mdwc->dr_mode != USB_DR_MODE_OTG) {
		dev_dbg(mdwc->dev, "%s failed!!!\n", __func__);
		return;
	}

	/* guard against concurrent VBUS handling */
	mdwc->in_restart = true;

	if (!mdwc->vbus_active) {
		dev_dbg(mdwc->dev, "%s bailing out in disconnect\n", __func__);
		mdwc->err_evt_seen = false;
		mdwc->in_restart = false;
		return;
	}

	dbg_event(0xFF, "RestartUSB", 0);
	/* Reset active USB connection */
	dwc3_resume_work(&mdwc->resume_work);

	/* Make sure disconnect is processed before sending connect */
	while (--timeout && !pm_runtime_suspended(mdwc->dev))
		msleep(20);

	if (!timeout) {
		dev_dbg(mdwc->dev,
			"Not in LPM after disconnect, forcing suspend...\n");
		dbg_event(0xFF, "ReStart:RT SUSP",
			atomic_read(&mdwc->dev->power.usage_count));
		pm_runtime_suspend(mdwc->dev);
	}

	mdwc->in_restart = false;
	/* Force reconnect only if cable is still connected */
	if (mdwc->vbus_active)
		dwc3_resume_work(&mdwc->resume_work);

	mdwc->err_evt_seen = false;
	flush_work(&mdwc->sm_work);
}

/*
 * Config Global Distributed Switch Controller (GDSC)
 * to support controller power collapse
 */
static int dwc3_msm_config_gdsc(struct dwc3_msm *mdwc, int on)
{
	int ret;

	if (IS_ERR_OR_NULL(mdwc->dwc3_gdsc))
		return -EPERM;

	if (on) {
		ret = regulator_enable(mdwc->dwc3_gdsc);
		if (ret) {
			dev_err(mdwc->dev, "unable to enable usb3 gdsc\n");
			return ret;
		}

		qcom_clk_set_flags(mdwc->core_clk, CLKFLAG_RETAIN_MEM);
	} else {
		qcom_clk_set_flags(mdwc->core_clk, CLKFLAG_NORETAIN_MEM);
		ret = regulator_disable(mdwc->dwc3_gdsc);
		if (ret) {
			dev_err(mdwc->dev, "unable to disable usb3 gdsc\n");
			return ret;
		}
	}

	return ret;
}

static int dwc3_msm_link_clk_reset(struct dwc3_msm *mdwc, bool assert)
{
	int ret = 0;

	if (assert) {
		disable_irq(mdwc->wakeup_irq[PWR_EVNT_IRQ].irq);
		/* Using asynchronous block reset to the hardware */
		dev_dbg(mdwc->dev, "block_reset ASSERT\n");
		clk_disable_unprepare(mdwc->utmi_clk);
		clk_disable_unprepare(mdwc->sleep_clk);
		clk_disable_unprepare(mdwc->core_clk);
		clk_disable_unprepare(mdwc->iface_clk);
		ret = reset_control_assert(mdwc->core_reset);
		if (ret)
			dev_err(mdwc->dev, "dwc3 core_reset assert failed\n");
	} else {
		dev_dbg(mdwc->dev, "block_reset DEASSERT\n");
		ret = reset_control_deassert(mdwc->core_reset);
		if (ret)
			dev_err(mdwc->dev, "dwc3 core_reset deassert failed\n");
		ndelay(200);
		clk_prepare_enable(mdwc->iface_clk);
		clk_prepare_enable(mdwc->core_clk);
		clk_prepare_enable(mdwc->sleep_clk);
		clk_prepare_enable(mdwc->utmi_clk);
		enable_irq(mdwc->wakeup_irq[PWR_EVNT_IRQ].irq);
	}

	return ret;
}

static void dwc3_gsi_event_buf_alloc(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_event_buffer *evt;
	int i;

	if (!mdwc->num_gsi_event_buffers)
		return;

	mdwc->gsi_ev_buff = devm_kzalloc(dwc->dev,
		sizeof(*dwc->ev_buf) * mdwc->num_gsi_event_buffers,
		GFP_KERNEL);
	if (!mdwc->gsi_ev_buff) {
		dev_err(dwc->dev, "can't allocate gsi_ev_buff\n");
		return;
	}

	for (i = 0; i < mdwc->num_gsi_event_buffers; i++) {

		evt = devm_kzalloc(dwc->dev, sizeof(*evt), GFP_KERNEL);
		if (!evt)
			return;
		evt->dwc	= dwc;
		evt->length	= DWC3_EVENT_BUFFERS_SIZE;
		evt->buf	= dma_alloc_coherent(dwc->sysdev,
					DWC3_EVENT_BUFFERS_SIZE,
					&evt->dma, GFP_KERNEL);
		if (!evt->buf) {
			dev_err(dwc->dev,
				"can't allocate gsi_evt_buf(%d)\n", i);
			return;
		}
		mdwc->gsi_ev_buff[i] = evt;
	}
	/*
	 * Set-up dummy buffer to use as doorbell while IPA GSI
	 * connection is in progress.
	 */
	mdwc->dummy_gsi_db_dma = dma_map_single(dwc->sysdev,
					&mdwc->dummy_gsi_db,
					sizeof(mdwc->dummy_gsi_db),
					DMA_FROM_DEVICE);

	if (dma_mapping_error(dwc->sysdev, mdwc->dummy_gsi_db_dma)) {
		dev_err(dwc->dev, "failed to map dummy doorbell buffer\n");
		mdwc->dummy_gsi_db_dma = (dma_addr_t)NULL;
	}
}

static void dwc3_msm_switch_utmi(struct dwc3_msm *mdwc, int enable)
{
	u32 reg;

	dwc3_msm_write_reg(mdwc->base, QSCRATCH_GENERAL_CFG,
		dwc3_msm_read_reg(mdwc->base,
		QSCRATCH_GENERAL_CFG)
		| PIPE_UTMI_CLK_DIS);

	udelay(5);

	reg = dwc3_msm_read_reg(mdwc->base, QSCRATCH_GENERAL_CFG);
	if (enable)
		reg |= (PIPE_UTMI_CLK_SEL | PIPE3_PHYSTATUS_SW);
	else
		reg &= ~(PIPE_UTMI_CLK_SEL | PIPE3_PHYSTATUS_SW);
	dwc3_msm_write_reg(mdwc->base, QSCRATCH_GENERAL_CFG, reg);

	udelay(5);

	dwc3_msm_write_reg(mdwc->base, QSCRATCH_GENERAL_CFG,
		dwc3_msm_read_reg(mdwc->base,
		QSCRATCH_GENERAL_CFG)
		& ~PIPE_UTMI_CLK_DIS);
}

static void dwc3_msm_set_clk_sel(struct dwc3_msm *mdwc)
{
	/*
	 * Below sequence is used when controller is working without
	 * having ssphy and only USB high/full speed is supported.
	 */
	if (dwc3_msm_get_max_speed(mdwc) == USB_SPEED_HIGH ||
				dwc3_msm_get_max_speed(mdwc) == USB_SPEED_FULL)
		dwc3_msm_switch_utmi(mdwc, 1);
}

static void *gadget_get_drvdata(struct usb_gadget *g)
{
	return g->ep0->driver_data;
}

static int is_diag_enabled(struct usb_composite_dev *cdev)
{
	struct usb_configuration	*c;

	list_for_each_entry(c, &cdev->configs, list) {
		struct usb_function *f;

		f = c->interface[0];
		if (!strcmp(f->fi->group.cg_item.ci_name, "ffs.diag"))
			return 1;
	}

	return 0;
}

static void dwc3_msm_update_imem_pid(struct dwc3 *dwc)
{
	struct usb_composite_dev	*cdev = NULL;
	struct dload_struct		local_diag_dload = { 0 };

	if (!diag_dload || !dwc->gadget) {
		pr_err("%s: diag_dload mem region not defined\n", __func__);
		return;
	}

	cdev = gadget_get_drvdata(dwc->gadget);

	if (cdev->desc.idVendor == 0x05c6 && is_diag_enabled(cdev)) {
		struct usb_gadget_string_container *uc;
		struct usb_string *s;

		local_diag_dload.pid = cdev->desc.idProduct;
		local_diag_dload.pid_magic = PID_MAGIC_ID;
		local_diag_dload.serial_magic = SERIAL_NUM_MAGIC_ID;

		list_for_each_entry(uc, &cdev->gstrings, list) {
			struct usb_gadget_strings **table;

			table = (struct usb_gadget_strings **)uc->stash;
			if (!table) {
				pr_err("%s: can't update dload cookie\n", __func__);
				return;
			}

			for (s = (*table)->strings; s && s->s; s++) {
				if (s->id == cdev->desc.iSerialNumber) {
					strscpy(local_diag_dload.serial_number, s->s,
						SERIAL_NUMBER_LENGTH);
					break;
				}
			}
		}
		memcpy_toio(diag_dload, &local_diag_dload, sizeof(local_diag_dload));
	}
}

static void mdwc3_usb2_phy_soft_reset(struct dwc3_msm *mdwc)
{
	u32 val;

	val = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
	dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0),
				val | DWC3_GUSB2PHYCFG_PHYSOFTRST);
	udelay(20);
	val = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
	val &= ~DWC3_GUSB2PHYCFG_PHYSOFTRST;
	dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0), val);
}

static void mdwc3_update_u1u2_value(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	static bool read_u1u2;

	/* cache DT based initial value once */
	if (!read_u1u2) {
		mdwc->cached_dis_u1_entry_quirk = dwc->dis_u1_entry_quirk;
		mdwc->cached_dis_u2_entry_quirk = dwc->dis_u2_entry_quirk;
		read_u1u2 = true;
		dbg_log_string("cached_dt_param: u1_disable:%d u2_disable:%d\n",
			mdwc->cached_dis_u1_entry_quirk, mdwc->cached_dis_u2_entry_quirk);
	}

	/* Enable u1u2 for USB super speed (gen1) only with quirks enable it */
	if (dwc->speed == DWC3_DSTS_SUPERSPEED) {
		dwc->dis_u1_entry_quirk = mdwc->cached_dis_u1_entry_quirk;
		dwc->dis_u2_entry_quirk = mdwc->cached_dis_u2_entry_quirk;
	} else {
		dwc->dis_u1_entry_quirk = true;
		dwc->dis_u2_entry_quirk = true;
	}

	dbg_log_string("speed:%d u1:%s u2:%s\n",
		dwc->speed, dwc->dis_u1_entry_quirk ? "disabled" : "enabled",
		dwc->dis_u2_entry_quirk ? "disabled" : "enabled");
}

int dwc3_msm_get_repeater_ver(struct dwc3_msm *mdwc)
{
	struct usb_repeater *ur = NULL;
	int ver;

	ur = devm_usb_get_repeater_by_phandle(mdwc->hs_phy->dev, "usb-repeater", 0);
	if (IS_ERR(ur))
		return -ENODEV;

	ver = usb_repeater_get_version(ur);
	usb_put_repeater(ur);

	return ver;
}

static void handle_gsi_buffer_setup_event(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_event_buffer *evt;
	int i;

	if (!mdwc->gsi_ev_buff)
		return;

	for (i = 0; i < mdwc->num_gsi_event_buffers; i++) {
		evt = mdwc->gsi_ev_buff[i];
		if (!evt)
			break;

		dev_dbg(mdwc->dev, "Evt buf %pK dma %08llx length %d\n",
			evt->buf, (unsigned long long) evt->dma,
			evt->length);
		memset(evt->buf, 0, evt->length);
		evt->lpos = 0;
		/*
		 * Primary event buffer is programmed with registers
		 * DWC3_GEVNT*(0). Hence use DWC3_GEVNT*(i+1) to
		 * program USB GSI related event buffer with DWC3
		 * controller.
		 */
		dwc3_msm_write_reg(mdwc->base, DWC3_GEVNTADRLO((i+1)),
			lower_32_bits(evt->dma));
		dwc3_msm_write_reg(mdwc->base, DWC3_GEVNTADRHI((i+1)),
			(upper_32_bits(evt->dma) & 0xffff) |
			DWC3_GEVNTADRHI_EVNTADRHI_GSI_EN(
			DWC3_GEVENT_TYPE_GSI) |
			DWC3_GEVNTADRHI_EVNTADRHI_GSI_IDX((i+1)));
		dwc3_msm_write_reg(mdwc->base, DWC3_GEVNTSIZ((i+1)),
			DWC3_GEVNTCOUNT_EVNTINTRPTMASK |
			((evt->length) & 0xffff));
		dwc3_msm_write_reg(mdwc->base,
				DWC3_GEVNTCOUNT((i+1)), 0);
	}
}

static void handle_gsi_buffer_cleanup_event(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_event_buffer *evt;
	int i;

	if (!mdwc->gsi_ev_buff)
		return;

	for (i = 0; i < mdwc->num_gsi_event_buffers; i++) {
		evt = mdwc->gsi_ev_buff[i];
		evt->lpos = 0;
		/*
		 * Primary event buffer is programmed with registers
		 * DWC3_GEVNT*(0). Hence use DWC3_GEVNT*(i+1) to
		 * program USB GSI related event buffer with DWC3
		 * controller.
		 */
		dwc3_msm_write_reg(mdwc->base,
				DWC3_GEVNTADRLO((i+1)), 0);
		dwc3_msm_write_reg(mdwc->base,
				DWC3_GEVNTADRHI((i+1)), 0);
		dwc3_msm_write_reg(mdwc->base, DWC3_GEVNTSIZ((i+1)),
				DWC3_GEVNTSIZ_INTMASK |
				DWC3_GEVNTSIZ_SIZE((i+1)));
		dwc3_msm_write_reg(mdwc->base,
				DWC3_GEVNTCOUNT((i+1)), 0);
	}
}

static void handle_gsi_buffer_free_event(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	struct dwc3_event_buffer *evt;
	int i;

	if (!mdwc->gsi_ev_buff)
		return;

	for (i = 0; i < mdwc->num_gsi_event_buffers; i++) {
		evt = mdwc->gsi_ev_buff[i];
		if (evt)
			dma_free_coherent(dwc->sysdev, evt->length,
						evt->buf, evt->dma);
	}
	if (mdwc->dummy_gsi_db_dma) {
		dma_unmap_single(dwc->sysdev, mdwc->dummy_gsi_db_dma,
				 sizeof(mdwc->dummy_gsi_db),
				 DMA_FROM_DEVICE);
		mdwc->dummy_gsi_db_dma = (dma_addr_t)NULL;
	}
}

static void handle_gsi_buffer_clear_event(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	u32 reg;
	int i;

	for (i = 0; i < mdwc->num_gsi_event_buffers; i++) {
		reg = dwc3_msm_read_reg(mdwc->base,
				DWC3_GEVNTCOUNT((i+1)));
		reg &= DWC3_GEVNTCOUNT_MASK;
		dwc3_msm_write_reg(mdwc->base,
				DWC3_GEVNTCOUNT((i+1)), reg);
		dbg_log_string("remaining EVNTCOUNT(%d)=%d", i+1, reg);
	}
}

static void handle_gsi_clear_db(struct dwc3 *dwc)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	if (mdwc->gsi_reg) {
		dwc3_msm_write_reg_field(mdwc->base,
			GSI_GENERAL_CFG_REG(mdwc->gsi_reg),
			BLOCK_GSI_WR_GO_MASK, true);
		dwc3_msm_write_reg_field(mdwc->base,
			GSI_GENERAL_CFG_REG(mdwc->gsi_reg),
			GSI_EN_MASK, 0);
	}
}

void dwc3_msm_notify_event(struct dwc3 *dwc,
		enum dwc3_notify_event event, unsigned int value)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);
	u32 reg;

	switch (event) {
	case DWC3_CONTROLLER_ERROR_EVENT:
		dev_info(mdwc->dev,
			"DWC3_CONTROLLER_ERROR_EVENT received\n");

		dwc3_msm_write_reg(mdwc->base, DWC3_DEVTEN, 0x00);

		/* prevent core from generating interrupts until recovery */
		reg = dwc3_msm_read_reg(mdwc->base, DWC3_GCTL);
		reg |= DWC3_GCTL_CORESOFTRESET;
		dwc3_msm_write_reg(mdwc->base, DWC3_GCTL, reg);

		/*
		 * If core could not recover after MAX_ERROR_RECOVERY_TRIES
		 * skip the restart USB work and keep the core in softreset
		 * state
		 */
		if (mdwc->retries_on_error < MAX_ERROR_RECOVERY_TRIES)
			schedule_work(&mdwc->restart_usb_work);
		break;
	case DWC3_CONTROLLER_CONNDONE_EVENT:
		dev_dbg(mdwc->dev, "DWC3_CONTROLLER_CONNDONE_EVENT received\n");

		/*
		 * SW WA for CV9 RESET DEVICE TEST(TD 9.23) compliance failure.
		 * Visit eUSB2 phy driver for more details.
		 */
		WARN_ON(mdwc->hs_phy->flags & PHY_HOST_MODE);
		if (mdwc->use_eusb2_phy &&
				(dwc->gadget->speed >= USB_SPEED_SUPER)) {
			usb_phy_notify_connect(mdwc->hs_phy, dwc->gadget->speed);

			/*
			 * Certain USB repeater HW revisions will have a fix on
			 * silicon.  Limit the controller PHY soft reset to the
			 * version which requires it.
			 */
			if (dwc3_msm_get_repeater_ver(mdwc) == USB_REPEATER_V1) {
				udelay(20);
				/*
				 * Perform usb2 phy soft reset as given
				 * workaround
				 */
				mdwc3_usb2_phy_soft_reset(mdwc);
			}
		}

		/*
		 * Add power event if the dbm indicates coming out of L1 by
		 * interrupt
		 */
		if (!mdwc->dbm_is_1p4)
			dwc3_msm_write_reg_field(mdwc->base,
					PWR_EVNT_IRQ_MASK_REG,
					PWR_EVNT_LPM_OUT_L1_MASK, 1);

		atomic_set(&mdwc->in_lpm, 0);
		mdwc3_update_u1u2_value(dwc);
		set_bit(CONN_DONE, &mdwc->inputs);
		queue_work(mdwc->sm_usb_wq, &mdwc->sm_work);
		break;
	case DWC3_GSI_EVT_BUF_ALLOC:
		dev_dbg(mdwc->dev, "DWC3_GSI_EVT_BUF_ALLOC\n");
		dwc3_gsi_event_buf_alloc(dwc);
		break;
	case DWC3_CONTROLLER_PULLUP_ENTER:
		dev_dbg(mdwc->dev, "DWC3_CONTROLLER_PULLUP_ENTER %d\n", value);
		/* ignore pullup when role switch from device to host */
		if (mdwc->vbus_active)
			usb_redriver_gadget_pullup_enter(mdwc->redriver, value);
		break;
	case DWC3_CONTROLLER_PULLUP_EXIT:
		dev_dbg(mdwc->dev, "DWC3_CONTROLLER_PULLUP_EXIT %d\n", value);
		/* ignore pullup when role switch from device to host */
		if (mdwc->vbus_active)
			usb_redriver_gadget_pullup_exit(mdwc->redriver, value);
		break;
	case DWC3_GSI_EVT_BUF_SETUP:
		dev_dbg(mdwc->dev, "DWC3_GSI_EVT_BUF_SETUP\n");
		handle_gsi_buffer_setup_event(dwc);
		break;
	case DWC3_GSI_EVT_BUF_CLEANUP:
		dev_dbg(mdwc->dev, "DWC3_GSI_EVT_BUF_CLEANUP\n");
		handle_gsi_buffer_cleanup_event(dwc);
		break;
	case DWC3_GSI_EVT_BUF_FREE:
		dev_dbg(mdwc->dev, "DWC3_GSI_EVT_BUF_FREE\n");
		handle_gsi_buffer_free_event(dwc);
		break;
	case DWC3_GSI_EVT_BUF_CLEAR:
		dev_dbg(mdwc->dev, "DWC3_GSI_EVT_BUF_CLEAR\n");
		handle_gsi_buffer_clear_event(dwc);
		break;
	case DWC3_CONTROLLER_NOTIFY_DISABLE_UPDXFER:
		dwc3_msm_dbm_disable_updxfer(dwc, value);
		break;
	case DWC3_CONTROLLER_NOTIFY_CLEAR_DB:
		dev_dbg(mdwc->dev, "DWC3_CONTROLLER_NOTIFY_CLEAR_DB\n");
		handle_gsi_clear_db(dwc);
		break;
	case DWC3_IMEM_UPDATE_PID:
		dwc3_msm_update_imem_pid(dwc);
		break;
	default:
		dev_dbg(mdwc->dev, "unknown dwc3 event\n");
		break;
	}
}

static void dwc3_msm_block_reset(struct dwc3_msm *mdwc, bool core_reset)
{
	int ret  = 0;

	if (core_reset) {
		ret = dwc3_msm_link_clk_reset(mdwc, 1);
		if (ret)
			return;

		usleep_range(1000, 1200);
		ret = dwc3_msm_link_clk_reset(mdwc, 0);
		if (ret)
			return;

		usleep_range(10000, 12000);
	}

	/* Reset the DBM */
	msm_dbm_write_reg_field(mdwc, DBM_SOFT_RESET, DBM_SFT_RST_MASK, 1);
	usleep_range(1000, 1200);
	msm_dbm_write_reg_field(mdwc, DBM_SOFT_RESET, DBM_SFT_RST_MASK, 0);

	/* enable DBM */
	dwc3_msm_write_reg_field(mdwc->base, QSCRATCH_GENERAL_CFG,
		DBM_EN_MASK, 0x1);
	if (!mdwc->dbm_is_1p4) {
		msm_dbm_write_reg(mdwc, DBM_DATA_FIFO_ADDR_EN, 0xFF);
		msm_dbm_write_reg(mdwc, DBM_DATA_FIFO_SIZE_EN, 0xFF);
	}
}

static void mdwc3_dis_sending_cm_l1(struct dwc3_msm *mdwc)
{
	u32 val;

	val = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
	dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0),
				val | DWC3_GUSB2PHYCFG_SUSPHY);
}

static void dwc3_en_sleep_mode(struct dwc3_msm *mdwc)
{
	u32 reg;

	reg = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
	reg |= DWC3_GUSB2PHYCFG_ENBLSLPM;
	dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0), reg);

	reg = dwc3_msm_read_reg(mdwc->base, DWC3_GUCTL1);
	reg |= DWC3_GUCTL1_L1_SUSP_THRLD_EN_FOR_HOST;
	dwc3_msm_write_reg(mdwc->base, DWC3_GUCTL1, reg);
}

static void dwc3_dis_sleep_mode(struct dwc3_msm *mdwc)
{
	u32 reg;

	reg = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
	reg &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;
	dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0), reg);

	reg = dwc3_msm_read_reg(mdwc->base, DWC3_GUCTL1);
	reg &= ~DWC3_GUCTL1_L1_SUSP_THRLD_EN_FOR_HOST;
	dwc3_msm_write_reg(mdwc->base, DWC3_GUCTL1, reg);
}

/* Force Gen1 speed on Gen2 controller if required */
static void dwc3_force_gen1(struct dwc3_msm *mdwc)
{
	struct dwc3 *dwc = platform_get_drvdata(mdwc->dwc3);

	if (mdwc->force_gen1 && DWC3_IP_IS(DWC31))
		dwc3_msm_write_reg_field(mdwc->base, DWC3_LLUCTL, DWC3_LLUCTL_FORCE_GEN1, 1);
}

static int dwc3_msm_power_collapse_por(struct dwc3_msm *mdwc)
{
	struct dwc3 *dwc = NULL;
	u32 val;

	if (!mdwc->dwc3)
		return 0;

	dwc = platform_get_drvdata(mdwc->dwc3);

	if (mdwc->dynamic_disable)
		return -EINVAL;

	/*
	 * Reading a value on the TCSR_USB_INT_DYN_EN_DIS register deviating
	 * from 0xF means that TZ caused TCSR to disable USB Interface
	 */
	if (mdwc->tcsr_dyn_en_dis) {
		val = dwc3_msm_read_reg(mdwc->tcsr_dyn_en_dis, 0);
		if (val != 0xF)
			return -EINVAL;
	}

	/*
	 * Reading from GSNPSID (known read-only register) which is expected
	 * to show a non-zero value if controller was turned on properly.
	 */
	val = dwc3_msm_read_reg(mdwc->base, DWC3_GSNPSID);
	if (DWC3_GSNPS_ID(val) == 0)
		return -EINVAL;

	/* Get initial P3 status and enable IN_P3 event */
	if (mdwc->ip == DWC31_IP)
		val = dwc3_msm_read_reg_field(mdwc->base,
			DWC31_LINK_GDBGLTSSM,
			DWC3_GDBGLTSSM_LINKSTATE_MASK);
	else
		val = dwc3_msm_read_reg_field(mdwc->base,
			DWC3_GDBGLTSSM, DWC3_GDBGLTSSM_LINKSTATE_MASK);
	atomic_set(&mdwc->in_p3, val == DWC3_LINK_STATE_U3);
	dwc3_msm_write_reg_field(mdwc->base, PWR_EVNT_IRQ_MASK_REG,
				PWR_EVNT_POWERDOWN_IN_P3_MASK, 1);

	/* Set the core in host mode if it was in host mode during pm_suspend */
	if (mdwc->in_host_mode) {
		dwc3_msm_write_reg_field(mdwc->base, DWC3_GCTL,
				DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_OTG),
				DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_HOST));
		if (!dwc->dis_enblslpm_quirk)
			dwc3_en_sleep_mode(mdwc);

		if (mdwc->dis_sending_cm_l1_quirk)
			mdwc3_dis_sending_cm_l1(mdwc);
	}

	dwc3_force_gen1(mdwc);
	return 0;
}

static int dwc3_msm_prepare_suspend(struct dwc3_msm *mdwc, bool ignore_p3_state)
{
	unsigned long timeout;
	u32 reg = 0;

	if (!mdwc->in_host_mode && !mdwc->in_device_mode)
		return 0;

	if (!ignore_p3_state && (dwc3_msm_is_superspeed(mdwc) &&
					!mdwc->in_restart)) {
		if (!atomic_read(&mdwc->in_p3)) {
			dev_err(mdwc->dev, "Not in P3,aborting LPM sequence\n");
			return -EBUSY;
		}
	}

	/* Clear previous L2 events */
	dwc3_msm_write_reg(mdwc->base, PWR_EVNT_IRQ_STAT_REG,
		PWR_EVNT_LPM_IN_L2_MASK | PWR_EVNT_LPM_OUT_L2_MASK);

	/* Prepare HSPHY for suspend */
	reg = dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0));
	dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0),
		reg | DWC3_GUSB2PHYCFG_ENBLSLPM | DWC3_GUSB2PHYCFG_SUSPHY);

	/* Wait for PHY to go into L2 */
	timeout = jiffies + msecs_to_jiffies(5);
	while (!time_after(jiffies, timeout)) {
		reg = dwc3_msm_read_reg(mdwc->base, PWR_EVNT_IRQ_STAT_REG);
		if (reg & PWR_EVNT_LPM_IN_L2_MASK)
			break;
	}
	if (!(reg & PWR_EVNT_LPM_IN_L2_MASK))
		dev_err(mdwc->dev, "could not transition HS PHY to L2\n");

	/* Clear L2 event bit */
	dwc3_msm_write_reg(mdwc->base, PWR_EVNT_IRQ_STAT_REG,
		PWR_EVNT_LPM_IN_L2_MASK);

	return 0;
}

static void dwc3_set_phy_speed_flags(struct dwc3_msm *mdwc)
{
	struct dwc3 *dwc;
	int i, num_ports;
	u32 reg;

	if (!mdwc->dwc3)
		return;

	dwc = platform_get_drvdata(mdwc->dwc3);

	mdwc->hs_phy->flags &= ~(PHY_HSFS_MODE | PHY_LS_MODE);
	if (mdwc->in_host_mode) {
		reg = dwc3_msm_read_reg(mdwc->base, USB3_HCSPARAMS1);
		num_ports = HCS_MAX_PORTS(reg);
		for (i = 0; i < num_ports; i++) {
			reg = dwc3_msm_read_reg(mdwc->base,
					USB3_PORTSC + i*0x10);
			if (reg & PORT_CONNECT) {
				if (DEV_HIGHSPEED(reg) || DEV_FULLSPEED(reg))
					mdwc->hs_phy->flags |= PHY_HSFS_MODE;
				else if (DEV_LOWSPEED(reg))
					mdwc->hs_phy->flags |= PHY_LS_MODE;
			}
		}
	} else if (mdwc->drd_state == DRD_STATE_PERIPHERAL_SUSPEND) {
		if (dwc->gadget->speed == USB_SPEED_HIGH ||
			dwc->gadget->speed == USB_SPEED_FULL)
			mdwc->hs_phy->flags |= PHY_HSFS_MODE;
		else if (dwc->gadget->speed == USB_SPEED_LOW)
			mdwc->hs_phy->flags |= PHY_LS_MODE;
	}
}

static void dwc3_msm_orientation_gpio_init(struct dwc3_msm *mdwc)
{
	struct device *dev = mdwc->dev;
	int rc;

	mdwc->orientation_gpio = of_get_gpio(dev->of_node, 0);
	if (!gpio_is_valid(mdwc->orientation_gpio)) {
		dev_err(dev, "Failed to get gpio\n");
		return;
	}

	rc = devm_gpio_request_one(dev, mdwc->orientation_gpio,
				   GPIOF_IN, "dwc3-msm-orientation");
	if (rc < 0) {
		dev_err(dev, "Failed to request gpio\n");
		mdwc->orientation_gpio = -EINVAL;
		return;
	}

	mdwc->has_orientation_gpio = true;
}

static void dwc3_set_ssphy_orientation_flag(struct dwc3_msm *mdwc)
{
	union extcon_property_value val;
	struct extcon_dev *edev = NULL;
	unsigned int extcon_id;
	int ret, orientation;

	mdwc->ss_phy->flags &= ~(PHY_LANE_A | PHY_LANE_B);

	if (mdwc->orientation_override) {
		mdwc->ss_phy->flags |= mdwc->orientation_override;
	} else if (mdwc->has_orientation_gpio) {
		orientation = gpio_get_value(mdwc->orientation_gpio);
		if (orientation == 0)
			mdwc->ss_phy->flags |= PHY_LANE_A;
		else
			mdwc->ss_phy->flags |= PHY_LANE_B;
	} else {
		if (mdwc->extcon && mdwc->vbus_active && !mdwc->in_restart) {
			extcon_id = EXTCON_USB;
			edev = mdwc->extcon[mdwc->ext_idx].edev;
		} else if (mdwc->extcon && mdwc->id_state == DWC3_ID_GROUND) {
			extcon_id = EXTCON_USB_HOST;
			edev = mdwc->extcon[mdwc->ext_idx].edev;
		}

		if (edev && extcon_get_state(edev, extcon_id)) {
			ret = extcon_get_property(edev, extcon_id,
					EXTCON_PROP_USB_TYPEC_POLARITY, &val);
			if (ret == 0)
				mdwc->ss_phy->flags |= val.intval ?
						PHY_LANE_B : PHY_LANE_A;
		}
	}

	dbg_event(0xFF, "ss_flag", mdwc->ss_phy->flags);
}

static void msm_dwc3_perf_vote_enable(struct dwc3_msm *mdwc, bool enable);

static void configure_usb_wakeup_interrupt(struct dwc3_msm *mdwc,
	struct usb_irq *uirq, unsigned int polarity, bool enable)
{
	if (uirq && enable && !uirq->enable) {
		dbg_event(0xFF, "PDC_IRQ_EN", uirq->irq);
		dbg_event(0xFF, "PDC_IRQ_POL", polarity);
		/* clear any pending interrupt */
		irq_set_irqchip_state(uirq->irq, IRQCHIP_STATE_PENDING, 0);
		irq_set_irq_type(uirq->irq, polarity);
		enable_irq_wake(uirq->irq);
		enable_irq(uirq->irq);
		uirq->enable = true;
	}

	if (uirq && !enable && uirq->enable) {
		dbg_event(0xFF, "PDC_IRQ_DIS", uirq->irq);
		disable_irq_wake(uirq->irq);
		disable_irq_nosync(uirq->irq);
		uirq->enable = false;
	}
}

static void enable_usb_pdc_interrupt(struct dwc3_msm *mdwc, bool enable)
{
	if (!enable)
		goto disable_usb_irq;

	if (mdwc->hs_phy->flags & PHY_LS_MODE) {
		/*
		 * According to eUSB2 spec, eDP line will be pulled high for remote
		 * wakeup scenario for LS connected device in host mode. On disconnect
		 * during bus-suspend case, irrespective of the speed of the connected
		 * device, both eDM and eDP line will be pulled high (XeSE1).
		 */
		if (mdwc->use_eusb2_phy)
			configure_usb_wakeup_interrupt(mdwc,
				&mdwc->wakeup_irq[DP_HS_PHY_IRQ],
				IRQ_TYPE_EDGE_RISING, enable);
		else
			configure_usb_wakeup_interrupt(mdwc,
				&mdwc->wakeup_irq[DM_HS_PHY_IRQ],
				IRQ_TYPE_EDGE_FALLING, enable);

	} else if (mdwc->hs_phy->flags & PHY_HSFS_MODE) {
		/*
		 * According to eUSB2 spec, eDM line will be pulled high for remote
		 * wakeup scenario for HS/FS connected device in host mode. On disconnect
		 * during bus-suspend case, irrespective of the speed of the connected
		 * device, both eDM and eDP line will be pulled high (XeSE1).
		 */
		if (mdwc->use_eusb2_phy)
			configure_usb_wakeup_interrupt(mdwc,
				&mdwc->wakeup_irq[DM_HS_PHY_IRQ],
				IRQ_TYPE_EDGE_RISING, enable);
		else
			configure_usb_wakeup_interrupt(mdwc,
				&mdwc->wakeup_irq[DP_HS_PHY_IRQ],
				IRQ_TYPE_EDGE_FALLING, enable);

	} else {
		/* When in host mode, with no device connected, set the HS
		 * to level high triggered.  This is to ensure device connection
		 * is seen, if device pulls up DP before the suspend routine
		 * configures the PDC IRQs, leading it to miss the rising edge.
		 */
		configure_usb_wakeup_interrupt(mdwc,
			&mdwc->wakeup_irq[DP_HS_PHY_IRQ],
			mdwc->in_host_mode ?
			(IRQF_TRIGGER_HIGH | IRQ_TYPE_LEVEL_HIGH) :
			IRQ_TYPE_EDGE_RISING, true);
		configure_usb_wakeup_interrupt(mdwc,
			&mdwc->wakeup_irq[DM_HS_PHY_IRQ],
			mdwc->in_host_mode ?
			(IRQF_TRIGGER_HIGH | IRQ_TYPE_LEVEL_HIGH) :
			IRQ_TYPE_EDGE_RISING, true);
	}

	configure_usb_wakeup_interrupt(mdwc,
		&mdwc->wakeup_irq[SS_PHY_IRQ],
		IRQF_TRIGGER_HIGH | IRQ_TYPE_LEVEL_HIGH, enable);
	return;

disable_usb_irq:
	configure_usb_wakeup_interrupt(mdwc,
			&mdwc->wakeup_irq[DP_HS_PHY_IRQ], 0, enable);
	configure_usb_wakeup_interrupt(mdwc,
			&mdwc->wakeup_irq[DM_HS_PHY_IRQ], 0, enable);
	configure_usb_wakeup_interrupt(mdwc,
			&mdwc->wakeup_irq[SS_PHY_IRQ], 0, enable);
}

static void configure_nonpdc_usb_interrupt(struct dwc3_msm *mdwc,
		struct usb_irq *uirq, bool enable)
{
	if (uirq && enable && !uirq->enable) {
		dbg_event(0xFF, "IRQ_EN", uirq->irq);
		enable_irq_wake(uirq->irq);
		enable_irq(uirq->irq);
		uirq->enable = true;
	}

	if (uirq && !enable && uirq->enable) {
		dbg_event(0xFF, "IRQ_DIS", uirq->irq);
		disable_irq_wake(uirq->irq);
		disable_irq_nosync(uirq->irq);
		uirq->enable = false;
	}
}

static void dwc3_msm_set_ss_pwr_events(struct dwc3_msm *mdwc, bool on)
{
	u32 irq_mask, irq_stat;

	irq_stat = dwc3_msm_read_reg(mdwc->base, PWR_EVNT_IRQ_STAT_REG);

	/* clear pending interrupts */
	dwc3_msm_write_reg(mdwc->base, PWR_EVNT_IRQ_STAT_REG, irq_stat);

	irq_mask = dwc3_msm_read_reg(mdwc->base, PWR_EVNT_IRQ_MASK_REG);

	if (on)
		irq_mask |= (PWR_EVNT_POWERDOWN_OUT_P3_MASK |
					PWR_EVNT_LPM_OUT_RX_ELECIDLE_IRQ_MASK);
	else
		irq_mask &= ~(PWR_EVNT_POWERDOWN_OUT_P3_MASK |
					PWR_EVNT_LPM_OUT_RX_ELECIDLE_IRQ_MASK);

	dwc3_msm_write_reg(mdwc->base, PWR_EVNT_IRQ_MASK_REG, irq_mask);
}

static int dwc3_msm_update_bus_bw(struct dwc3_msm *mdwc, enum bus_vote bv)
{
	int i, ret = 0;
	unsigned int bv_index = mdwc->override_bus_vote ?: bv;

	dbg_event(0xFF, "bus_vote_start", bv);

	/* On some platforms SVS does not have separate vote.
	 * Vote for nominal if svs usecase does not exist.
	 * If the request is to set the bus_vote to _NONE,
	 * set it to _NONE irrespective of the requested vote
	 * from userspace.
	 */
	if (bv_index >= BUS_VOTE_MAX)
		bv_index = BUS_VOTE_MAX - 1;
	else if (bv_index < BUS_VOTE_NONE)
		bv_index = BUS_VOTE_NONE;

	for (i = 0; i < ARRAY_SIZE(mdwc->icc_paths); i++) {
		ret = icc_set_bw(mdwc->icc_paths[i],
				bus_vote_values[bv_index][i].avg,
				bus_vote_values[bv_index][i].peak);
		if (ret)
			dev_err(mdwc->dev, "bus bw voting path:%s bv:%d failed %d\n",
					icc_path_names[i], bv_index, ret);
	}

	dbg_event(0xFF, "bus_vote_end", bv_index);

	return ret;
}

/**
 * dwc3_clk_enable_disable - helper function for enabling or disabling clocks
 * in dwc3_msm_resume() or dwc3_msm_suspend respectively.
 *
 * @mdwc: Pointer to the mdwc structure.
 * @enable: enable/disable clocks
 *
 * Returns 0 on success otherwise negative errno.
 */
static int dwc3_clk_enable_disable(struct dwc3_msm *mdwc, bool enable, bool toggle_sleep)
{
	int ret = 0;
	long core_clk_rate;

	if (!enable)
		goto disable_bus_aggr_clk;

	/* Vote for TCXO while waking up USB HSPHY */
	ret = clk_prepare_enable(mdwc->xo_clk);
	if (ret < 0) {
		dev_err(mdwc->dev, "%s failed to vote TCXO buffer%d\n",
						__func__, ret);
		return ret;
	}
	if (toggle_sleep) {
		ret = clk_prepare_enable(mdwc->sleep_clk);
		if (ret < 0) {
			dev_err(mdwc->dev, "%s: sleep_clk enable failed\n",
						__func__);
			goto disable_xo_clk;
		}
	}

	/*
	 * Enable clocks
	 * Turned ON iface_clk before core_clk due to FSM depedency.
	 */
	ret = clk_prepare_enable(mdwc->iface_clk);
	if (ret < 0) {
		dev_err(mdwc->dev, "%s: iface_clk enable failed\n", __func__);
		goto disable_sleep_clk;
	}
	ret = clk_prepare_enable(mdwc->noc_aggr_clk);
	if (ret < 0) {
		dev_err(mdwc->dev, "%s: noc_aggr_clk enable failed\n", __func__);
		goto disable_iface_clk;
	}

	core_clk_rate = mdwc->core_clk_rate_disconnected;
	if (mdwc->in_host_mode && mdwc->max_rh_port_speed == USB_SPEED_HIGH)
		core_clk_rate = mdwc->core_clk_rate_hs;
	else if (!(mdwc->lpm_flags & MDWC3_POWER_COLLAPSE))
		core_clk_rate = mdwc->core_clk_rate;

	dev_dbg(mdwc->dev, "%s: set core clk rate %ld\n", __func__,
		core_clk_rate);
	clk_set_rate(mdwc->core_clk, core_clk_rate);
	ret = clk_prepare_enable(mdwc->core_clk);
	if (ret < 0) {
		dev_err(mdwc->dev, "%s: core_clk enable failed\n", __func__);
		goto disable_noc_aggr_clk;
	}
	ret = clk_prepare_enable(mdwc->utmi_clk);
	if (ret < 0) {
		dev_err(mdwc->dev, "%s: utmi_clk enable failed\n", __func__);
		goto disable_core_clk;
	}
	ret = clk_prepare_enable(mdwc->bus_aggr_clk);
	if (ret < 0) {
		dev_err(mdwc->dev, "%s: bus_aggr_clk enable failed\n", __func__);
		goto disable_utmi_clk;
	}

	return 0;

	/* Disable clocks */
disable_bus_aggr_clk:
	clk_disable_unprepare(mdwc->bus_aggr_clk);
disable_utmi_clk:
	clk_disable_unprepare(mdwc->utmi_clk);
disable_core_clk:
	clk_set_rate(mdwc->core_clk, 19200000);
	clk_disable_unprepare(mdwc->core_clk);
disable_noc_aggr_clk:
	clk_disable_unprepare(mdwc->noc_aggr_clk);
disable_iface_clk:
	/*
	 * Disable iface_clk only after core_clk as core_clk has FSM
	 * depedency on iface_clk. Hence iface_clk should be turned off
	 * after core_clk is turned off.
	 */
	clk_disable_unprepare(mdwc->iface_clk);
disable_sleep_clk:
	if (toggle_sleep)
		clk_disable_unprepare(mdwc->sleep_clk);
disable_xo_clk:
	/* USB PHY no more requires TCXO */
	clk_disable_unprepare(mdwc->xo_clk);

	return ret;
}

static int dwc3_msm_suspend(struct dwc3_msm *mdwc, bool force_power_collapse)
{
	int ret;
	struct dwc3 *dwc = NULL;
	struct dwc3_event_buffer *evt;
	struct usb_irq *uirq;
	bool can_suspend_ssphy, no_active_ss;

	if (mdwc->dwc3)
		dwc = platform_get_drvdata(mdwc->dwc3);

	mutex_lock(&mdwc->suspend_resume_mutex);
	if (atomic_read(&mdwc->in_lpm)) {
		dev_dbg(mdwc->dev, "%s: Already suspended\n", __func__);
		mutex_unlock(&mdwc->suspend_resume_mutex);
		return 0;
	}

	msm_dwc3_perf_vote_enable(mdwc, false);
	if (dwc) {
		if (!mdwc->in_host_mode) {
			evt = dwc->ev_buf;
			if ((evt->flags & DWC3_EVENT_PENDING)) {
				dev_dbg(mdwc->dev,
					"%s: %d device events pending, abort suspend\n",
					__func__, evt->count / 4);
				mutex_unlock(&mdwc->suspend_resume_mutex);
				return -EBUSY;
			}
		}

		/*
		 * Check if device is not in CONFIGURED state
		 * then check controller state of L2 and break
		 * LPM sequence. Check this for device bus suspend case.
		 */
		if ((mdwc->dr_mode == USB_DR_MODE_OTG &&
				mdwc->drd_state == DRD_STATE_PERIPHERAL_SUSPEND) &&
				(dwc->gadget->state != USB_STATE_CONFIGURED)) {
			pr_err("%s(): Trying to go in LPM with state:%d\n",
						__func__, dwc->gadget->state);
			pr_err("%s(): LPM is not performed.\n", __func__);
			mutex_unlock(&mdwc->suspend_resume_mutex);
			return -EBUSY;
		}
	}

	if (!mdwc->vbus_active && mdwc->dr_mode == USB_DR_MODE_OTG &&
		mdwc->drd_state == DRD_STATE_PERIPHERAL) {
		/*
		 * In some cases, the pm_runtime_suspend may be called by
		 * usb_bam when there is pending lpm flag. However, if this is
		 * done when cable was disconnected and otg state has not
		 * yet changed to IDLE, then it means OTG state machine
		 * is running and we race against it. So cancel LPM for now,
		 * and OTG state machine will go for LPM later, after completing
		 * transition to IDLE state.
		 */
		dev_dbg(mdwc->dev,
			"%s: cable disconnected while not in idle otg state\n",
			__func__);
		mutex_unlock(&mdwc->suspend_resume_mutex);
		return -EBUSY;
	}



	ret = dwc3_msm_prepare_suspend(mdwc, force_power_collapse);
	if (ret) {
		mutex_unlock(&mdwc->suspend_resume_mutex);
		return ret;
	}

	/* disable power event irq, hs and ss phy irq is used as wake up src */
	disable_irq(mdwc->wakeup_irq[PWR_EVNT_IRQ].irq);

	dwc3_set_phy_speed_flags(mdwc);
	/* Suspend HS PHY */
	usb_phy_set_suspend(mdwc->hs_phy, 1);

	/*
	 * Synopsys Superspeed PHY does not support ss_phy_irq, so to detect
	 * any wakeup events in host mode PHY cannot be suspended.
	 * This Superspeed PHY can be suspended only in the following cases:
	 *	1. The core is not in host mode
	 *	2. A Highspeed device is connected but not a Superspeed device
	 */
	no_active_ss = (!mdwc->in_host_mode) || (mdwc->in_host_mode &&
		((mdwc->hs_phy->flags & (PHY_HSFS_MODE | PHY_LS_MODE)) &&
			!dwc3_msm_is_superspeed(mdwc)));
	can_suspend_ssphy = dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER &&
			(!mdwc->use_pwr_event_for_wakeup || no_active_ss);
	/* Suspend SS PHY */
	if (can_suspend_ssphy) {
		if (mdwc->in_host_mode) {
			u32 reg = dwc3_msm_read_reg(mdwc->base,
					DWC3_GUSB3PIPECTL(0));

			reg |= DWC3_GUSB3PIPECTL_DISRXDETU3;
			dwc3_msm_write_reg(mdwc->base, DWC3_GUSB3PIPECTL(0),
					reg);
		}
		/* indicate phy about SS mode */
		if (dwc3_msm_is_superspeed(mdwc))
			mdwc->ss_phy->flags |= DEVICE_IN_SS_MODE;
		usb_phy_set_suspend(mdwc->ss_phy, 1);
		mdwc->lpm_flags |= MDWC3_SS_PHY_SUSPEND;
	} else if (mdwc->use_pwr_event_for_wakeup) {
		dwc3_msm_set_ss_pwr_events(mdwc, true);
		enable_irq(mdwc->wakeup_irq[PWR_EVNT_IRQ].irq);
	}

	/* make sure above writes are completed before turning off clocks */
	wmb();

	if (!(mdwc->in_host_mode || mdwc->in_device_mode) ||
	      mdwc->in_restart || force_power_collapse)
		mdwc->lpm_flags |= MDWC3_POWER_COLLAPSE;

	/* Disable clocks */
	dwc3_clk_enable_disable(mdwc, false, mdwc->lpm_flags & MDWC3_POWER_COLLAPSE);

	/* Perform controller power collapse */
	if (mdwc->lpm_flags & MDWC3_POWER_COLLAPSE) {
		dev_dbg(mdwc->dev, "%s: power collapse\n", __func__);
		dwc3_msm_config_gdsc(mdwc, 0);
	}

	dwc3_msm_update_bus_bw(mdwc, BUS_VOTE_NONE);

	/*
	 * release wakeup source with timeout to defer system suspend to
	 * handle case where on USB cable disconnect, SUSPEND and DISCONNECT
	 * event is received.
	 */
	if (mdwc->lpm_to_suspend_delay) {
		dev_dbg(mdwc->dev, "defer suspend with %d(msecs)\n",
					mdwc->lpm_to_suspend_delay);
		pm_wakeup_event(mdwc->dev, mdwc->lpm_to_suspend_delay);
	} else {
		pm_relax(mdwc->dev);
	}

	atomic_set(&mdwc->in_lpm, 1);

	/*
	 * with DCP or during cable disconnect, we dont require wakeup
	 * using HS_PHY_IRQ or SS_PHY_IRQ. Hence enable wakeup only in
	 * case of host bus suspend and device bus suspend.
	 */
	if (mdwc->in_device_mode || mdwc->in_host_mode) {
		if (mdwc->use_pdc_interrupts) {
			enable_usb_pdc_interrupt(mdwc, true);
		} else {
			uirq = &mdwc->wakeup_irq[HS_PHY_IRQ];
			configure_nonpdc_usb_interrupt(mdwc, uirq, true);
			uirq = &mdwc->wakeup_irq[SS_PHY_IRQ];
			configure_nonpdc_usb_interrupt(mdwc, uirq, true);
		}
		mdwc->lpm_flags |= MDWC3_ASYNC_IRQ_WAKE_CAPABILITY;
	}

	dev_info(mdwc->dev, "DWC3 in low power mode\n");
	dbg_event(0xFF, "Ctl Sus", atomic_read(&mdwc->in_lpm));

	/* kick_sm if it is waiting for lpm sequence to finish */
	if (test_and_clear_bit(WAIT_FOR_LPM, &mdwc->inputs))
		queue_work(mdwc->sm_usb_wq, &mdwc->sm_work);

	mutex_unlock(&mdwc->suspend_resume_mutex);

	return 0;
}

static int dwc3_msm_resume(struct dwc3_msm *mdwc)
{
	int ret;
	struct dwc3 *dwc = NULL;
	struct usb_irq *uirq;

	if (mdwc->dwc3)
		dwc = platform_get_drvdata(mdwc->dwc3);

	dev_dbg(mdwc->dev, "%s: exiting lpm\n", __func__);

	/*
	 * If h/w exited LPM without any events, ensure
	 * h/w is reset before processing any new events.
	 */
	if (!mdwc->vbus_active && mdwc->id_state)
		set_bit(WAIT_FOR_LPM, &mdwc->inputs);

	mutex_lock(&mdwc->suspend_resume_mutex);
	if (!atomic_read(&mdwc->in_lpm)) {
		dev_dbg(mdwc->dev, "%s: Already resumed\n", __func__);
		mutex_unlock(&mdwc->suspend_resume_mutex);
		return 0;
	}

	pm_stay_awake(mdwc->dev);

	if (mdwc->in_host_mode && mdwc->max_rh_port_speed == USB_SPEED_HIGH)
		dwc3_msm_update_bus_bw(mdwc, BUS_VOTE_SVS);
	else
		dwc3_msm_update_bus_bw(mdwc, mdwc->default_bus_vote);

	/* Restore controller power collapse */
	if (mdwc->lpm_flags & MDWC3_POWER_COLLAPSE) {
		dev_dbg(mdwc->dev, "%s: exit power collapse\n", __func__);
		ret = dwc3_msm_config_gdsc(mdwc, 1);
		if (ret < 0)
			goto error;
		ret = reset_control_assert(mdwc->core_reset);
		if (ret)
			dev_err(mdwc->dev, "%s:core_reset assert failed\n",
					__func__);
		/* HW requires a short delay for reset to take place properly */
		usleep_range(1000, 1200);
		ret = reset_control_deassert(mdwc->core_reset);
		if (ret)
			dev_err(mdwc->dev, "%s:core_reset deassert failed\n",
					__func__);
	}

	ret = dwc3_clk_enable_disable(mdwc, true, mdwc->lpm_flags & MDWC3_POWER_COLLAPSE);
	if (ret < 0) {
		/* Perform controller power collapse */
		if (mdwc->lpm_flags & MDWC3_POWER_COLLAPSE)
			dwc3_msm_config_gdsc(mdwc, 0);
		goto error;
	}

	/*
	 * Disable any wakeup events that were enabled if pwr_event_irq
	 * is used as wakeup interrupt.
	 */
	if (mdwc->use_pwr_event_for_wakeup &&
			!(mdwc->lpm_flags & MDWC3_SS_PHY_SUSPEND)) {
		disable_irq_nosync(mdwc->wakeup_irq[PWR_EVNT_IRQ].irq);
		dwc3_msm_set_ss_pwr_events(mdwc, false);
	}

	/* Resume SS PHY */
	if (dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER &&
			mdwc->lpm_flags & MDWC3_SS_PHY_SUSPEND) {
		dwc3_set_ssphy_orientation_flag(mdwc);
		if (!mdwc->in_host_mode || mdwc->disable_host_ssphy_powerdown ||
			(mdwc->in_host_mode && mdwc->max_rh_port_speed != USB_SPEED_HIGH))
			usb_phy_set_suspend(mdwc->ss_phy, 0);

		mdwc->ss_phy->flags &= ~DEVICE_IN_SS_MODE;
		mdwc->lpm_flags &= ~MDWC3_SS_PHY_SUSPEND;

		if (mdwc->in_host_mode) {
			u32 reg = dwc3_msm_read_reg(mdwc->base,
					DWC3_GUSB3PIPECTL(0));

			reg &= ~DWC3_GUSB3PIPECTL_DISRXDETU3;
			dwc3_msm_write_reg(mdwc->base, DWC3_GUSB3PIPECTL(0),
					reg);
		}
	}

	mdwc->hs_phy->flags &= ~(PHY_HSFS_MODE | PHY_LS_MODE);
	/* Resume HS PHY */
	usb_phy_set_suspend(mdwc->hs_phy, 0);

	/* Recover from controller power collapse */
	if (mdwc->lpm_flags & MDWC3_POWER_COLLAPSE) {
		dev_dbg(mdwc->dev, "%s: exit power collapse\n", __func__);

		ret = dwc3_msm_power_collapse_por(mdwc);
		if (ret < 0) {
			dev_err(mdwc->dev, "%s: Controller was not turned on properly\n",
						__func__);
			dwc3_clk_enable_disable(mdwc, false,
				mdwc->lpm_flags & MDWC3_POWER_COLLAPSE);
			dwc3_msm_config_gdsc(mdwc, 0);
			goto error;
		}

		mdwc->lpm_flags &= ~MDWC3_POWER_COLLAPSE;
	}

	atomic_set(&mdwc->in_lpm, 0);

	/* enable power evt irq for IN P3 detection */
	enable_irq(mdwc->wakeup_irq[PWR_EVNT_IRQ].irq);

	/* Disable HSPHY auto suspend */
	dwc3_msm_write_reg(mdwc->base, DWC3_GUSB2PHYCFG(0),
		dwc3_msm_read_reg(mdwc->base, DWC3_GUSB2PHYCFG(0)) &
				~DWC3_GUSB2PHYCFG_SUSPHY);

	/* Disable wakeup capable for HS_PHY IRQ & SS_PHY_IRQ if enabled */
	if (mdwc->lpm_flags & MDWC3_ASYNC_IRQ_WAKE_CAPABILITY) {
		if (mdwc->use_pdc_interrupts) {
			enable_usb_pdc_interrupt(mdwc, false);
		} else {
			uirq = &mdwc->wakeup_irq[HS_PHY_IRQ];
			configure_nonpdc_usb_interrupt(mdwc, uirq, false);
			uirq = &mdwc->wakeup_irq[SS_PHY_IRQ];
			configure_nonpdc_usb_interrupt(mdwc, uirq, false);
		}
		mdwc->lpm_flags &= ~MDWC3_ASYNC_IRQ_WAKE_CAPABILITY;
	}

	dev_info(mdwc->dev, "DWC3 exited from low power mode\n");

	/*
	 * Handle other power events that could not have been handled during
	 * Low Power Mode
	 */
	dwc3_pwr_event_handler(mdwc);

	msm_dwc3_perf_vote_enable(mdwc, true);

	dwc3_msm_set_clk_sel(mdwc);

	dbg_event(0xFF, "Ctl Res", atomic_read(&mdwc->in_lpm));
	mutex_unlock(&mdwc->suspend_resume_mutex);

	return 0;

error:
	dwc3_msm_update_bus_bw(mdwc, BUS_VOTE_NONE);
	pm_relax(mdwc->dev);
	clear_bit(WAIT_FOR_LPM, &mdwc->inputs);
	mutex_unlock(&mdwc->suspend_resume_mutex);
	return ret;
}

/**
 * dwc3_ext_event_notify - callback to handle events from external transceiver
 *
 * Returns 0 on success
 */
static void dwc3_ext_event_notify(struct dwc3_msm *mdwc)
{
	/* Flush processing any pending events before handling new ones */
	flush_work(&mdwc->sm_work);

	if (mdwc->dynamic_disable && (mdwc->vbus_active ||
			(mdwc->id_state == DWC3_ID_GROUND))) {
		dev_err(mdwc->dev, "%s: Event not allowed\n", __func__);
		return;
	}

	dbg_log_string("enter: mdwc->inputs:%lx hs_phy_flags:%x\n",
				mdwc->inputs, mdwc->hs_phy->flags);
	if (mdwc->id_state == DWC3_ID_FLOAT) {
		dbg_log_string("XCVR: ID set\n");
		set_bit(ID, &mdwc->inputs);
	} else {
		dbg_log_string("XCVR: ID clear\n");
		clear_bit(ID, &mdwc->inputs);
	}

	if (mdwc->vbus_active && !mdwc->in_restart) {
		if (mdwc->hs_phy->flags & EUD_SPOOF_DISCONNECT) {
			dbg_log_string("XCVR: BSV clear\n");
			clear_bit(B_SESS_VLD, &mdwc->inputs);
		} else {
			dbg_log_string("XCVR: BSV set\n");
			set_bit(B_SESS_VLD, &mdwc->inputs);
		}
	} else {
		dbg_log_string("XCVR: BSV clear\n");
		clear_bit(B_SESS_VLD, &mdwc->inputs);
	}

	if (mdwc->suspend) {
		dbg_log_string("XCVR: SUSP set\n");
		set_bit(B_SUSPEND, &mdwc->inputs);
	} else {
		dbg_log_string("XCVR: SUSP clear\n");
		clear_bit(B_SUSPEND, &mdwc->inputs);
	}

	if (mdwc->check_eud_state && mdwc->vbus_active) {
		mdwc->hs_phy->flags &=
			~(EUD_SPOOF_CONNECT | EUD_SPOOF_DISCONNECT);
		dbg_log_string("eud: state:%d active:%d hs_phy_flags:0x%x\n",
			mdwc->check_eud_state, mdwc->eud_active,
			mdwc->hs_phy->flags);
		if (mdwc->eud_active) {
			mdwc->hs_phy->flags |= EUD_SPOOF_CONNECT;
			dbg_log_string("EUD: XCVR: BSV set\n");
			set_bit(B_SESS_VLD, &mdwc->inputs);
		} else {
			mdwc->hs_phy->flags |= EUD_SPOOF_DISCONNECT;
			dbg_log_string("EUD: XCVR: BSV clear\n");
			clear_bit(B_SESS_VLD, &mdwc->inputs);
		}

		mdwc->check_eud_state = false;
	}


	dbg_log_string("eud: state:%d active:%d hs_phy_flags:0x%x\n",
		mdwc->check_eud_state, mdwc->eud_active, mdwc->hs_phy->flags);

	/* handle case of USB cable disconnect after USB spoof disconnect */
	if (!mdwc->vbus_active &&
			(mdwc->hs_phy->flags & EUD_SPOOF_DISCONNECT)) {
		mdwc->hs_phy->flags &= ~EUD_SPOOF_DISCONNECT;
		mdwc->hs_phy->flags |= PHY_SUS_OVERRIDE;
		usb_phy_set_suspend(mdwc->hs_phy, 1);
		mdwc->hs_phy->flags &= ~PHY_SUS_OVERRIDE;
		return;
	}

	dbg_log_string("exit: mdwc->inputs:%lx\n", mdwc->inputs);
	queue_work(mdwc->sm_usb_wq, &mdwc->sm_work);
}

static void dwc3_resume_work(struct work_struct *w)
{
	struct dwc3_msm *mdwc = container_of(w, struct dwc3_msm, resume_work);
	struct dwc3 *dwc = NULL;
	union extcon_property_value val;
	unsigned int extcon_id;
	struct extcon_dev *edev = NULL;
	const char *edev_name;
	int ret = 0;

	if (mdwc->dwc3)
		dwc = platform_get_drvdata(mdwc->dwc3);

	dev_dbg(mdwc->dev, "%s: dwc3 resume work\n", __func__);
	dbg_log_string("resume_work: ext_idx:%d\n", mdwc->ext_idx);
	if (mdwc->extcon && mdwc->vbus_active && !mdwc->in_restart) {
		extcon_id = EXTCON_USB;
		edev = mdwc->extcon[mdwc->ext_idx].edev;
	} else if (mdwc->extcon && mdwc->id_state == DWC3_ID_GROUND) {
		extcon_id = EXTCON_USB_HOST;
		edev = mdwc->extcon[mdwc->ext_idx].edev;
	}

	if (edev) {
		edev_name = extcon_get_edev_name(edev);
		dbg_log_string("edev:%s\n", edev_name);
		/* Skip querying speed and cc_state for EUD edev */
		if (mdwc->extcon[mdwc->ext_idx].is_eud)
			goto skip_update;
	}

	/*
	 * Do not override speed for consistency as if not present, then
	 * there is a chance w/ 4LN DP USB data disable case for the DCFG
	 * programmed w/ SSUSB w/o QMP PHY initialized.  Functionally
	 * DP mode will still operate as should.
	 */
	if (!mdwc->ss_release_called)
		dwc3_msm_set_max_speed(mdwc, mdwc->max_hw_supp_speed);
	if (edev && extcon_get_state(edev, extcon_id)) {
		ret = extcon_get_property(edev, extcon_id,
				EXTCON_PROP_USB_SS, &val);

		if (!ret && val.intval == 0)
			dwc3_msm_set_max_speed(mdwc, USB_SPEED_HIGH);
	}

	if (dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER)
		dwc3_set_ssphy_orientation_flag(mdwc);

skip_update:
	dbg_log_string("max_speed:%d hw_supp_speed:%d override_speed:%d",
		dwc3_msm_get_max_speed(mdwc), mdwc->max_hw_supp_speed,
		mdwc->override_usb_speed);
	if (mdwc->override_usb_speed &&
			mdwc->override_usb_speed <= dwc3_msm_get_max_speed(mdwc)) {
		dwc3_msm_set_max_speed(mdwc, mdwc->override_usb_speed);
	}

	dbg_event(0xFF, "speed", dwc3_msm_get_max_speed(mdwc));

	/*
	 * Skip scheduling sm work if no work is pending. When boot-up
	 * with USB cable connected, usb state m/c is skipped to avoid
	 * any changes to dp/dm lines. As PM supsend and resume can
	 * happen while charger is connected, scheduling sm work during
	 * pm resume will reset the controller and phy which might impact
	 * dp/dm lines (and charging voltage).
	 */
	if (mdwc->drd_state == DRD_STATE_UNDEFINED &&
		!edev && !mdwc->resume_pending)
		return;
	/*
	 * exit LPM first to meet resume timeline from device side.
	 * resume_pending flag would prevent calling
	 * dwc3_msm_resume() in case we are here due to system
	 * wide resume without usb cable connected. This flag is set
	 * only in case of power event irq in lpm.
	 */
	if (mdwc->resume_pending) {
		dwc3_msm_resume(mdwc);
		mdwc->resume_pending = false;
	}

	if (atomic_read(&mdwc->pm_suspended)) {
		dbg_event(0xFF, "RWrk PMSus", 0);
		/* let pm resume kick in resume work later */
		return;
	}
	dwc3_ext_event_notify(mdwc);
}

static void dwc3_pwr_event_handler(struct dwc3_msm *mdwc)
{
	struct dwc3 *dwc = NULL;
	u32 irq_stat, irq_clear = 0;

	if (!mdwc->dwc3)
		return;
	dwc = platform_get_drvdata(mdwc->dwc3);

	irq_stat = dwc3_msm_read_reg(mdwc->base, PWR_EVNT_IRQ_STAT_REG);
	dev_dbg(mdwc->dev, "%s irq_stat=%X\n", __func__, irq_stat);

	/* Check for P3 events */
	if ((irq_stat & PWR_EVNT_POWERDOWN_OUT_P3_MASK) &&
			(irq_stat & PWR_EVNT_POWERDOWN_IN_P3_MASK)) {
		u32 ls;

		/* Can't tell if entered or exit P3, so check LINKSTATE */
		if (!DWC3_IP_IS(DWC3))
			ls = dwc3_msm_read_reg_field(mdwc->base,
				DWC31_LINK_GDBGLTSSM,
				DWC3_GDBGLTSSM_LINKSTATE_MASK);
		else
			ls = dwc3_msm_read_reg_field(mdwc->base,
				DWC3_GDBGLTSSM, DWC3_GDBGLTSSM_LINKSTATE_MASK);
		dev_dbg(mdwc->dev, "%s link state = 0x%04x\n", __func__, ls);
		atomic_set(&mdwc->in_p3, ls == DWC3_LINK_STATE_U3);

		irq_stat &= ~(PWR_EVNT_POWERDOWN_OUT_P3_MASK |
				PWR_EVNT_POWERDOWN_IN_P3_MASK);
		irq_clear |= (PWR_EVNT_POWERDOWN_OUT_P3_MASK |
				PWR_EVNT_POWERDOWN_IN_P3_MASK);
	} else if (irq_stat & PWR_EVNT_POWERDOWN_OUT_P3_MASK) {
		atomic_set(&mdwc->in_p3, 0);
		irq_stat &= ~PWR_EVNT_POWERDOWN_OUT_P3_MASK;
		irq_clear |= PWR_EVNT_POWERDOWN_OUT_P3_MASK;
	} else if (irq_stat & PWR_EVNT_POWERDOWN_IN_P3_MASK) {
		atomic_set(&mdwc->in_p3, 1);
		irq_stat &= ~PWR_EVNT_POWERDOWN_IN_P3_MASK;
		irq_clear |= PWR_EVNT_POWERDOWN_IN_P3_MASK;
	}

	/* Handle exit from L1 events */
	if (irq_stat & PWR_EVNT_LPM_OUT_L1_MASK) {
		dev_dbg(mdwc->dev, "%s: handling PWR_EVNT_LPM_OUT_L1_MASK\n",
				__func__);
		if (!mdwc->in_host_mode) {
			if (usb_gadget_wakeup(dwc->gadget))
				dev_err(mdwc->dev, "%s failed to take dwc out of L1\n",
					__func__);
		}
		irq_stat &= ~PWR_EVNT_LPM_OUT_L1_MASK;
		irq_clear |= PWR_EVNT_LPM_OUT_L1_MASK;
	}

	/* Unhandled events */
	if (irq_stat)
		dev_dbg(mdwc->dev, "%s: unexpected PWR_EVNT, irq_stat=%X\n",
			__func__, irq_stat);

	dwc3_msm_write_reg(mdwc->base, PWR_EVNT_IRQ_STAT_REG, irq_clear);
}

static irqreturn_t msm_dwc3_pwr_irq_thread(int irq, void *_mdwc)
{
	struct dwc3_msm *mdwc = _mdwc;

	if (atomic_read(&mdwc->in_lpm))
		dwc3_resume_work(&mdwc->resume_work);
	else
		dwc3_pwr_event_handler(mdwc);

	dbg_event(0xFF, "PWR IRQ", atomic_read(&mdwc->in_lpm));
	return IRQ_HANDLED;
}

static irqreturn_t msm_dwc3_pwr_irq(int irq, void *data)
{
	struct dwc3_msm *mdwc = data;

	dev_dbg(mdwc->dev, "%s received %d\n", __func__, irq);
	/*
	 * When in Low Power Mode, can't read PWR_EVNT_IRQ_STAT_REG to acertain
	 * which interrupts have been triggered, as the clocks are disabled.
	 * Resume controller by waking up pwr event irq thread.After re-enabling
	 * clocks, dwc3_msm_resume will call dwc3_pwr_event_handler to handle
	 * all other power events.
	 */
	if (atomic_read(&mdwc->in_lpm)) {
		/* set this to call dwc3_msm_resume() */
		mdwc->resume_pending = true;
		return IRQ_WAKE_THREAD;
	}

	dwc3_pwr_event_handler(mdwc);
	return IRQ_HANDLED;
}

static void dwc3_otg_sm_work(struct work_struct *w);

static int dwc3_msm_get_clk_gdsc(struct dwc3_msm *mdwc)
{
	int ret;

	mdwc->dwc3_gdsc = devm_regulator_get(mdwc->dev, "USB3_GDSC");
	if (IS_ERR(mdwc->dwc3_gdsc)) {
		if (PTR_ERR(mdwc->dwc3_gdsc) == -EPROBE_DEFER)
			return PTR_ERR(mdwc->dwc3_gdsc);
		mdwc->dwc3_gdsc = NULL;
	}

	mdwc->xo_clk = devm_clk_get(mdwc->dev, "xo");
	if (IS_ERR(mdwc->xo_clk))
		mdwc->xo_clk = NULL;
	clk_set_rate(mdwc->xo_clk, 19200000);

	mdwc->iface_clk = devm_clk_get(mdwc->dev, "iface_clk");
	if (IS_ERR(mdwc->iface_clk)) {
		dev_err(mdwc->dev, "failed to get iface_clk\n");
		ret = PTR_ERR(mdwc->iface_clk);
		return ret;
	}

	/*
	 * DWC3 Core requires its CORE CLK (aka master / bus clk) to
	 * run at 125Mhz in SSUSB mode and >60MHZ for HSUSB mode.
	 * On newer platform it can run at 150MHz as well.
	 */
	mdwc->core_clk = devm_clk_get(mdwc->dev, "core_clk");
	if (IS_ERR(mdwc->core_clk)) {
		dev_err(mdwc->dev, "failed to get core_clk\n");
		ret = PTR_ERR(mdwc->core_clk);
		return ret;
	}

	mdwc->core_reset = devm_reset_control_get(mdwc->dev, "core_reset");
	if (IS_ERR(mdwc->core_reset)) {
		dev_err(mdwc->dev, "failed to get core_reset\n");
		return PTR_ERR(mdwc->core_reset);
	}

	if (of_property_read_u32(mdwc->dev->of_node, "qcom,core-clk-rate",
				(u32 *)&mdwc->core_clk_rate)) {
		dev_err(mdwc->dev, "USB core-clk-rate is not present\n");
		return -EINVAL;
	}

	mdwc->core_clk_rate = clk_round_rate(mdwc->core_clk,
							mdwc->core_clk_rate);
	dev_dbg(mdwc->dev, "USB core frequency = %ld\n",
						mdwc->core_clk_rate);

	if (of_property_read_u32(mdwc->dev->of_node, "qcom,core-clk-rate-hs",
				(u32 *)&mdwc->core_clk_rate_hs)) {
		dev_dbg(mdwc->dev, "USB core-clk-rate-hs is not present\n");
		mdwc->core_clk_rate_hs = mdwc->core_clk_rate;
	}

	if (of_property_read_u32(mdwc->dev->of_node, "qcom,core-clk-rate-disconnected",
				(u32 *)&mdwc->core_clk_rate_disconnected)) {
		dev_dbg(mdwc->dev, "USB core-clk-rate-disconnected is not present\n");
		mdwc->core_clk_rate_disconnected = mdwc->core_clk_rate;
	}

	ret = clk_set_rate(mdwc->core_clk, mdwc->core_clk_rate_disconnected);
	if (ret)
		dev_err(mdwc->dev, "fail to set core_clk freq:%d\n", ret);

	mdwc->sleep_clk = devm_clk_get(mdwc->dev, "sleep_clk");
	if (IS_ERR(mdwc->sleep_clk)) {
		dev_err(mdwc->dev, "failed to get sleep_clk\n");
		ret = PTR_ERR(mdwc->sleep_clk);
		return ret;
	}

	clk_set_rate(mdwc->sleep_clk, 32000);
	mdwc->utmi_clk_rate = 19200000;
	mdwc->utmi_clk = devm_clk_get(mdwc->dev, "utmi_clk");
	if (IS_ERR(mdwc->utmi_clk)) {
		dev_err(mdwc->dev, "failed to get utmi_clk\n");
		ret = PTR_ERR(mdwc->utmi_clk);
		return ret;
	}

	clk_set_rate(mdwc->utmi_clk, mdwc->utmi_clk_rate);
	mdwc->bus_aggr_clk = devm_clk_get(mdwc->dev, "bus_aggr_clk");
	if (IS_ERR(mdwc->bus_aggr_clk))
		mdwc->bus_aggr_clk = NULL;

	mdwc->noc_aggr_clk = devm_clk_get(mdwc->dev, "noc_aggr_clk");
	if (IS_ERR(mdwc->noc_aggr_clk))
		mdwc->noc_aggr_clk = NULL;

	if (of_property_match_string(mdwc->dev->of_node,
				"clock-names", "cfg_ahb_clk") >= 0) {
		mdwc->cfg_ahb_clk = devm_clk_get(mdwc->dev, "cfg_ahb_clk");
		if (IS_ERR(mdwc->cfg_ahb_clk)) {
			ret = PTR_ERR(mdwc->cfg_ahb_clk);
			mdwc->cfg_ahb_clk = NULL;
			if (ret != -EPROBE_DEFER)
				dev_err(mdwc->dev,
					"failed to get cfg_ahb_clk ret %d\n",
					ret);
			return ret;
		}
	}

	return 0;
}

static int dwc3_msm_id_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct dwc3 *dwc;
	struct extcon_dev *edev = ptr;
	struct extcon_nb *enb = container_of(nb, struct extcon_nb, id_nb);
	struct dwc3_msm *mdwc = enb->mdwc;
	enum dwc3_id_state id;

	if (!edev || !mdwc)
		return NOTIFY_DONE;

	dwc = platform_get_drvdata(mdwc->dwc3);

	dbg_event(0xFF, "extcon idx", enb->idx);

	id = event ? DWC3_ID_GROUND : DWC3_ID_FLOAT;

	if (mdwc->id_state == id)
		return NOTIFY_DONE;

	mdwc->ext_idx = enb->idx;

	dev_dbg(mdwc->dev, "host:%ld (id:%d) event received\n", event, id);

	mdwc->id_state = id;
	dbg_event(0xFF, "id_state", mdwc->id_state);
	queue_work(mdwc->dwc3_wq, &mdwc->resume_work);

	return NOTIFY_DONE;
}


static int dwc3_msm_vbus_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct dwc3 *dwc = NULL;
	struct extcon_dev *edev = ptr;
	struct extcon_nb *enb = container_of(nb, struct extcon_nb, vbus_nb);
	struct dwc3_msm *mdwc = enb->mdwc;
	const char *edev_name;

	if (!edev || !mdwc)
		return NOTIFY_DONE;

	if (mdwc->dwc3)
		dwc = platform_get_drvdata(mdwc->dwc3);

	dbg_event(0xFF, "extcon idx", enb->idx);
	dev_dbg(mdwc->dev, "vbus:%ld event received\n", event);
	edev_name = extcon_get_edev_name(edev);
	dbg_log_string("edev:%s\n", edev_name);

	/* detect USB spoof disconnect/connect notification with EUD device */
	if (mdwc->extcon[enb->idx].is_eud) {
		int spoof;

		spoof = extcon_get_state(edev, EXTCON_JIG);

		if (mdwc->eud_active == event)
			return NOTIFY_DONE;

		mdwc->eud_active = event;

		/*
		 * In case EUD is enabled by the module param, if DWC3 is
		 * operating in SS, ignore any connect/disconnect changes.  This
		 * allows for the link to be uninterrupted, as EUD affects the
		 * HS path.  Only listen for if there are spoof
		 * connect/disconnect commands.
		 */
		if (dwc && dwc->gadget->speed >= USB_SPEED_SUPER) {
			if (mdwc->eud_active)
				wcd_usbss_dpdm_switch_update(true, true);
			else
				wcd_usbss_dpdm_switch_update(false, false);
			if (!spoof)
				return NOTIFY_DONE;
		}

		mdwc->check_eud_state = true;
	} else {
		if (mdwc->vbus_active == event)
			return NOTIFY_DONE;
		mdwc->vbus_active = event;
	}

	mdwc->ext_idx = enb->idx;
	if (mdwc->dr_mode == USB_DR_MODE_OTG && !mdwc->in_restart)
		queue_work(mdwc->dwc3_wq, &mdwc->resume_work);

	return NOTIFY_DONE;
}

static int dwc3_msm_extcon_is_valid_source(struct dwc3_msm *mdwc)
{
	struct device_node *node = mdwc->dev->of_node;
	int idx;
	int count;

	count = of_count_phandle_with_args(node, "extcon", NULL);
	if (count < 0) {
		dev_err(mdwc->dev, "of_count_phandle_with_args failed\n");
		return 0;
	}

	for (idx = 0; idx < count; idx++) {
		if (!mdwc->extcon[idx].is_eud)
			return 1;
	}

	return 0;
}

static int dwc3_msm_extcon_register(struct dwc3_msm *mdwc)
{
	struct device_node *node = mdwc->dev->of_node;
	struct extcon_dev *edev;
	int idx, extcon_cnt, ret = 0;
	bool check_vbus_state, check_id_state, phandle_found = false;
	char *eud_str;
	const char *edev_name;

	extcon_cnt = of_count_phandle_with_args(node, "extcon", NULL);
	if (extcon_cnt < 0) {
		dev_err(mdwc->dev, "of_count_phandle_with_args failed\n");
		return -ENODEV;
	}

	mdwc->extcon = devm_kcalloc(mdwc->dev, extcon_cnt,
					sizeof(*mdwc->extcon), GFP_KERNEL);
	if (!mdwc->extcon)
		return -ENOMEM;

	for (idx = 0; idx < extcon_cnt; idx++) {
		edev = extcon_get_edev_by_phandle(mdwc->dev, idx);
		if (IS_ERR(edev) && PTR_ERR(edev) != -ENODEV)
			return PTR_ERR(edev);

		if (IS_ERR_OR_NULL(edev))
			continue;

		check_vbus_state = check_id_state = true;
		phandle_found = true;

		mdwc->extcon[idx].mdwc = mdwc;
		mdwc->extcon[idx].edev = edev;
		mdwc->extcon[idx].idx = idx;

		edev_name = extcon_get_edev_name(edev);
		eud_str = strnstr(edev_name, "eud", strlen(edev_name));
		if (eud_str)
			mdwc->extcon[idx].is_eud = true;

		mdwc->extcon[idx].vbus_nb.notifier_call =
						dwc3_msm_vbus_notifier;
		ret = extcon_register_notifier(edev, EXTCON_USB,
						&mdwc->extcon[idx].vbus_nb);
		if (ret < 0)
			check_vbus_state = false;

		mdwc->extcon[idx].id_nb.notifier_call = dwc3_msm_id_notifier;
		ret = extcon_register_notifier(edev, EXTCON_USB_HOST,
						&mdwc->extcon[idx].id_nb);
		if (ret < 0)
			check_id_state = false;

		/* Update initial VBUS/ID state */
		if (check_vbus_state && extcon_get_state(edev, EXTCON_USB))
			dwc3_msm_vbus_notifier(&mdwc->extcon[idx].vbus_nb,
						true, edev);
		else  if (check_id_state &&
				extcon_get_state(edev, EXTCON_USB_HOST))
			dwc3_msm_id_notifier(&mdwc->extcon[idx].id_nb,
						true, edev);
	}

	if (!phandle_found) {
		dev_err(mdwc->dev, "no extcon device found\n");
		return -ENODEV;
	}

	return 0;
}

static inline const char *dwc3_msm_usb_role_string(enum usb_role role)
{
	if (role < ARRAY_SIZE(usb_role_strings))
		return usb_role_strings[role];

	return "Invalid";
}

static bool dwc3_msm_role_allowed(struct dwc3_msm *mdwc, enum usb_role role)
{
	dev_dbg(mdwc->dev, "%s: dr_mode=%s role_requested=%s\n",
			__func__, usb_dr_modes[mdwc->dr_mode],
			dwc3_msm_usb_role_string(role));

	if (role == USB_ROLE_HOST && mdwc->dr_mode == USB_DR_MODE_PERIPHERAL)
		return false;

	if (role == USB_ROLE_DEVICE && mdwc->dr_mode == USB_DR_MODE_HOST)
		return false;

	return true;
}

static enum usb_role dwc3_msm_get_role(struct dwc3_msm *mdwc)
{
	enum usb_role role;

	if (mdwc->vbus_active)
		role = USB_ROLE_DEVICE;
	else if (mdwc->id_state == DWC3_ID_GROUND)
		role = USB_ROLE_HOST;
	else
		role = USB_ROLE_NONE;

	return role;
}

static enum usb_role dwc3_msm_usb_role_switch_get_role(struct usb_role_switch *sw)
{
	struct dwc3_msm *mdwc = usb_role_switch_get_drvdata(sw);
	enum usb_role role;

	role = dwc3_msm_get_role(mdwc);
	dbg_log_string("get_role:%s\n", dwc3_msm_usb_role_string(role));

	return role;
}

static int dwc3_msm_set_role(struct dwc3_msm *mdwc, enum usb_role role)
{
	enum usb_role cur_role;

	if (!dwc3_msm_role_allowed(mdwc, role))
		return -EINVAL;

	mutex_lock(&mdwc->role_switch_mutex);
	cur_role = dwc3_msm_get_role(mdwc);

	dbg_log_string("cur_role:%s new_role:%s refcnt:%d\n", dwc3_msm_usb_role_string(cur_role),
				dwc3_msm_usb_role_string(role), mdwc->refcnt_dp_usb);

	/*
	 * For boot up without USB cable connected case, don't check
	 * previous role value to allow resetting USB controller and
	 * PHYs.
	 */
	if (mdwc->drd_state != DRD_STATE_UNDEFINED && cur_role == role) {
		dbg_log_string("no USB role change");
		mutex_unlock(&mdwc->role_switch_mutex);
		return 0;
	}

	switch (role) {
	case USB_ROLE_HOST:
		mdwc->vbus_active = false;
		mdwc->id_state = DWC3_ID_GROUND;
		dbg_log_string("refcnt:%d start host mode\n", mdwc->refcnt_dp_usb);
		mdwc->refcnt_dp_usb++;
		break;

	case USB_ROLE_DEVICE:
		mdwc->vbus_active = true;
		mdwc->id_state = DWC3_ID_FLOAT;
		dbg_log_string("refcnt:%d reset refcnt_dp_usb\n", mdwc->refcnt_dp_usb);
		mdwc->refcnt_dp_usb = 0;
		break;

	case USB_ROLE_NONE:
		if (mdwc->dp_state != DP_NONE) {
			mdwc->refcnt_dp_usb--;
			dbg_log_string("DP (%d)session active, refcnt:%d\n",
					mdwc->dp_state, mdwc->refcnt_dp_usb);
			mutex_unlock(&mdwc->role_switch_mutex);
			return 0;
		}

		mdwc->vbus_active = false;
		mdwc->id_state = DWC3_ID_FLOAT;
		mdwc->refcnt_dp_usb = 0;
		break;
	}
	dbg_log_string("new_role:%s refcnt:%d\n",
		dwc3_msm_usb_role_string(role), mdwc->refcnt_dp_usb);
	mutex_unlock(&mdwc->role_switch_mutex);

	dwc3_ext_event_notify(mdwc);
	return 0;
}

static int dwc3_msm_usb_role_switch_set_role(struct usb_role_switch *sw, enum usb_role role)
{
	struct dwc3_msm *mdwc = usb_role_switch_get_drvdata(sw);

	return dwc3_msm_set_role(mdwc, role);
}

static ssize_t orientation_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);

	if (mdwc->orientation_override == PHY_LANE_A)
		return scnprintf(buf, PAGE_SIZE, "A\n");
	if (mdwc->orientation_override == PHY_LANE_B)
		return scnprintf(buf, PAGE_SIZE, "B\n");

	return scnprintf(buf, PAGE_SIZE, "none\n");
}

static ssize_t orientation_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);

	if (sysfs_streq(buf, "A"))
		mdwc->orientation_override = PHY_LANE_A;
	else if (sysfs_streq(buf, "B"))
		mdwc->orientation_override = PHY_LANE_B;
	else
		mdwc->orientation_override = 0;

	return count;
}

static DEVICE_ATTR_RW(orientation);

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	enum usb_role cur_role;

	cur_role = dwc3_msm_get_role(mdwc);
	if (cur_role == USB_ROLE_DEVICE)
		return scnprintf(buf, PAGE_SIZE, "peripheral\n");
	if (cur_role == USB_ROLE_HOST)
		return scnprintf(buf, PAGE_SIZE, "host\n");

	return scnprintf(buf, PAGE_SIZE, "none\n");
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	enum usb_role role = USB_ROLE_NONE;
	int ret;

	if (sysfs_streq(buf, "peripheral"))
		role = USB_ROLE_DEVICE;
	else if (sysfs_streq(buf, "host"))
		role = USB_ROLE_HOST;

	dbg_log_string("mode_request:%s\n", dwc3_msm_usb_role_string(role));
	ret = dwc3_msm_set_role(mdwc, role);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(mode);
static void msm_dwc3_perf_vote_work(struct work_struct *w);

/* This node only shows max speed supported dwc3 and it should be
 * same as what is reported in udc/core.c max_speed node. For current
 * operating gadget speed, query current_speed node which is implemented
 * by udc/core.c
 */
static ssize_t speed_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			usb_speed_string(dwc3_msm_get_max_speed(mdwc)));
}

static ssize_t speed_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	enum usb_device_speed req_speed = USB_SPEED_UNKNOWN;

	/* no speed change in host mode */
	if (!test_bit(ID, &mdwc->inputs))
		return -EPERM;

	/* DEVSPD can only have values SS(0x4), HS(0x0) and FS(0x1).
	 * per 3.20a data book. Allow only these settings. Note that,
	 * xhci does not support full-speed only mode.
	 */
	if (sysfs_streq(buf, "full"))
		req_speed = USB_SPEED_FULL;
	else if (sysfs_streq(buf, "high"))
		req_speed = USB_SPEED_HIGH;
	else if (sysfs_streq(buf, "super"))
		req_speed = USB_SPEED_SUPER;
	else if (sysfs_streq(buf, "ssp"))
		req_speed = USB_SPEED_SUPER_PLUS;
	else
		return -EINVAL;

	/* restart usb only works for device mode. Perform manual cable
	 * plug in/out for host mode restart.
	 */
	if (req_speed != dwc3_msm_get_max_speed(mdwc) &&
			req_speed <= mdwc->max_hw_supp_speed) {
		mdwc->override_usb_speed = req_speed;
		schedule_work(&mdwc->restart_usb_work);
	} else if (req_speed >= mdwc->max_hw_supp_speed) {
		mdwc->override_usb_speed = 0;
	}

	return count;
}
static DEVICE_ATTR_RW(speed);

static ssize_t bus_vote_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);

	if (mdwc->override_bus_vote == BUS_VOTE_MIN)
		return scnprintf(buf, PAGE_SIZE, "%s\n",
			"Fixed bus vote: min");
	else if (mdwc->override_bus_vote == BUS_VOTE_MAX)
		return scnprintf(buf, PAGE_SIZE, "%s\n",
			"Fixed bus vote: max");
	else
		return scnprintf(buf, PAGE_SIZE, "%s\n",
			"Do not have fixed bus vote");
}

static ssize_t bus_vote_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	bool bv_fixed = false;
	enum bus_vote bv;

	if (sysfs_streq(buf, "min")) {
		bv_fixed = true;
		mdwc->override_bus_vote = BUS_VOTE_MIN;
	} else if (sysfs_streq(buf, "max")) {
		bv_fixed = true;
		mdwc->override_bus_vote = BUS_VOTE_MAX;
	} else if (sysfs_streq(buf, "cancel")) {
		bv_fixed = false;
		mdwc->override_bus_vote = BUS_VOTE_NONE;
	} else {
		dev_err(dev, "min/max/cancel only.\n");
		return -EINVAL;
	}

	/* Update bus vote value only when not suspend */
	if (!atomic_read(&mdwc->in_lpm)) {
		if (bv_fixed)
			bv = mdwc->override_bus_vote;
		else if (mdwc->in_host_mode
			&& (mdwc->max_rh_port_speed == USB_SPEED_HIGH))
			bv = BUS_VOTE_SVS;
		else
			bv = mdwc->default_bus_vote;

		dwc3_msm_update_bus_bw(mdwc, bv);
	}

	return count;
}
static DEVICE_ATTR_RW(bus_vote);

static ssize_t xhci_test_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc;
	enum usb_role cur_role;
	u32 reg;

	if (mdwc->dwc3 == NULL)
		return count;

	dwc = platform_get_drvdata(mdwc->dwc3);
	cur_role = dwc3_msm_get_role(mdwc);
	if (cur_role != USB_ROLE_HOST) {
		dev_err(dev, "USB is not in host mode\n");
		return count;
	}

	pm_runtime_resume(&dwc->xhci->dev);
	pm_runtime_forbid(&dwc->xhci->dev);
	reg = dwc3_msm_read_reg(mdwc->base, USB3_PORTPMSC_20);
	dev_info(dev, "USB PORTPMSC val:%x\n", reg);
	reg |= USB_TEST_PACKET << PORT_TEST_MODE_SHIFT;
	dev_info(dev, "writing %x to USB PORTPMSC\n", reg);
	dwc3_msm_write_reg(mdwc->base, USB3_PORTPMSC_20, reg);
	return count;
}
static DEVICE_ATTR_WO(xhci_test);

static ssize_t dynamic_disable_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	bool disable;

	strtobool(buf, &disable);
	if (disable) {
		if (mdwc->dynamic_disable) {
			dbg_log_string("USB already disabled\n");
			return count;
		}

		flush_work(&mdwc->sm_work);
		set_bit(ID, &mdwc->inputs);
		clear_bit(B_SESS_VLD, &mdwc->inputs);
		queue_work(mdwc->sm_usb_wq, &mdwc->sm_work);
		flush_work(&mdwc->sm_work);
		while (test_bit(WAIT_FOR_LPM, &mdwc->inputs))
			msleep(20);
		mdwc->dynamic_disable = true;
		dbg_log_string("Dynamic USB disable\n");
	} else {
		if (!mdwc->dynamic_disable) {
			dbg_log_string("USB already enabled\n");
			return count;
		}
		if (mdwc->tcsr_dyn_en_dis) {
			if (dwc3_msm_read_reg(mdwc->tcsr_dyn_en_dis, 0) != 0xF) {
				dbg_log_string("Unable to enable USB\n");
				return count;
			}
		}

		mdwc->dynamic_disable = false;
		pm_runtime_disable(mdwc->dev);
		pm_runtime_set_suspended(mdwc->dev);
		pm_runtime_enable(mdwc->dev);
		dwc3_ext_event_notify(mdwc);
		flush_work(&mdwc->sm_work);
		dbg_log_string("Dynamic USB enable\n");
	}

	return count;
}
static DEVICE_ATTR_WO(dynamic_disable);

static struct attribute *dwc3_msm_attrs[] = {
	&dev_attr_orientation.attr,
	&dev_attr_mode.attr,
	&dev_attr_speed.attr,
	&dev_attr_bus_vote.attr,
	&dev_attr_xhci_test.attr,
	&dev_attr_dynamic_disable.attr,
	NULL
};
ATTRIBUTE_GROUPS(dwc3_msm);

static int dwc_dpdm_cb(struct notifier_block *nb, unsigned long evt, void *p)
{
	struct dwc3_msm *mdwc = container_of(nb, struct dwc3_msm, dpdm_nb);

	switch (evt) {
	case REGULATOR_EVENT_ENABLE:
		dev_dbg(mdwc->dev, "%s: enable state:%s\n", __func__,
				dwc3_drd_state_string(mdwc->drd_state));
		break;
	case REGULATOR_EVENT_DISABLE:
		dev_dbg(mdwc->dev, "%s: disable state:%s\n", __func__,
				dwc3_drd_state_string(mdwc->drd_state));
		if (mdwc->drd_state == DRD_STATE_UNDEFINED)
			queue_work(mdwc->sm_usb_wq, &mdwc->sm_work);
		break;
	default:
		dev_dbg(mdwc->dev, "%s: unknown event state:%s\n", __func__,
				dwc3_drd_state_string(mdwc->drd_state));
		break;
	}

	return NOTIFY_OK;
}

static void dwc3_init_dbm(struct dwc3_msm *mdwc)
{
	const char *dbm_ver;
	int ret;

	ret = of_property_read_string(mdwc->dev->of_node, "qcom,dbm-version",
			&dbm_ver);
	if (!ret && !strcmp(dbm_ver, "1.4")) {
		mdwc->dbm_reg_table = dbm_1_4_regtable;
		mdwc->dbm_num_eps = DBM_1_4_NUM_EP;
		mdwc->dbm_is_1p4 = true;
	} else {
		/* default to v1.5 register layout */
		mdwc->dbm_reg_table = dbm_1_5_regtable;
		mdwc->dbm_num_eps = DBM_1_5_NUM_EP;
	}
}

static int dwc3_start_stop_host(struct dwc3_msm *mdwc, bool start)
{
	if (start) {
		dbg_log_string("start host mode");
		mdwc->id_state = DWC3_ID_GROUND;
		mdwc->vbus_active = false;
	} else {
		dbg_log_string("stop_host_mode started");
		mdwc->id_state = DWC3_ID_FLOAT;
		mdwc->vbus_active = false;
	}

	dwc3_ext_event_notify(mdwc);

	if (!start) {
		/*
		 * Block runtime PM during draining of the WQ, as if RPM suspend
		 * occurs, it can not queue work to sm_usb_wq. (as only chain
		 * queuing is allowed)
		 */
		pm_runtime_get(&mdwc->dwc3->dev);

		flush_work(&mdwc->resume_work);
		flush_workqueue(mdwc->sm_usb_wq);

		pm_runtime_put(&mdwc->dwc3->dev);
		while (test_bit(WAIT_FOR_LPM, &mdwc->inputs))
			msleep(20);

		dbg_log_string("stop_host_mode completed");
		if (mdwc->id_state == DWC3_ID_GROUND)
			return -EBUSY;
	}

	return 0;
}

static int dwc3_start_stop_device(struct dwc3_msm *mdwc, bool start)
{
	if (start) {
		dbg_log_string("start device mode");
		mdwc->id_state = DWC3_ID_FLOAT;
		mdwc->vbus_active = true;
	} else {
		dbg_log_string("stop device mode");
		mdwc->id_state = DWC3_ID_FLOAT;
		mdwc->vbus_active = false;
	}

	dwc3_ext_event_notify(mdwc);

	if (!start) {
		/*
		 * Block runtime PM during draining of the WQ, as if RPM suspend
		 * occurs, it can not queue work to sm_usb_wq. (as only chain
		 * queuing is allowed)
		 */
		pm_runtime_get(&mdwc->dwc3->dev);

		flush_work(&mdwc->resume_work);
		flush_workqueue(mdwc->sm_usb_wq);

		pm_runtime_put(&mdwc->dwc3->dev);

		while (test_bit(WAIT_FOR_LPM, &mdwc->inputs))
			msleep(20);

		dbg_log_string("stop_device_mode completed");
		if (mdwc->vbus_active)
			return -EBUSY;
	}

	return 0;
}

static void dwc3_msm_clear_dp_only_params(struct dwc3_msm *mdwc)
{
	dbg_log_string("resetting params for USB ss\n");
	mdwc->ss_release_called = false;
	mdwc->ss_phy->flags &= ~PHY_DP_MODE;
	dwc3_msm_set_max_speed(mdwc, USB_SPEED_UNKNOWN);

	usb_redriver_notify_disconnect(mdwc->redriver);
}

static void dwc3_msm_set_dp_only_params(struct dwc3_msm *mdwc)
{
	usb_redriver_release_lanes(mdwc->redriver, mdwc->ss_phy->flags & PHY_LANE_A ?
					ORIENTATION_CC1 : ORIENTATION_CC2, 4);

	/* restart USB host mode into high speed */
	mdwc->ss_release_called = true;
	dwc3_msm_set_max_speed(mdwc, USB_SPEED_HIGH);
	mdwc->ss_phy->flags |= PHY_DP_MODE;
}

int dwc3_msm_set_dp_mode(struct device *dev, bool dp_connected, int lanes)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	int ret = 0;

	if (!mdwc || !mdwc->dwc3) {
		dev_err(dev, "dwc3-msm is not initialized yet.\n");
		return -EAGAIN;
	}

	dev_dbg(dev, "lanes %d, connect %d\n", lanes, dp_connected);
	/* flush any pending work */
	flush_work(&mdwc->resume_work);
	flush_workqueue(mdwc->sm_usb_wq);

	dbg_log_string("DP: cur_state:%d new_state:%d lanes:%d\n",
				mdwc->dp_state, dp_connected, lanes);
	if (dp_connected && (lanes == mdwc->dp_state)) {
		dbg_log_string("DP lane already configured\n");
		return 0;
	}

	if (!dp_connected) {
		dbg_log_string("DP not connected, refcnt:%d\n", mdwc->refcnt_dp_usb);

		mutex_lock(&mdwc->role_switch_mutex);
		mdwc->ss_release_called = false;
		/*
		 * Special case for HOST mode, as we need to ensure that the DWC3
		 * max speed is set before moving back into gadget/device mode.
		 * This is because, dwc3_gadget_init() will set the max speed
		 * for the USB gadget driver.
		 */
		mdwc->refcnt_dp_usb--;
		mdwc->dp_state = DP_NONE;
		if (mdwc->drd_state == DRD_STATE_HOST) {
			if (!mdwc->refcnt_dp_usb)
				dwc3_start_stop_host(mdwc, false);
		} else {
			dwc3_msm_clear_dp_only_params(mdwc);
		}

		mdwc->ss_phy->flags &= ~PHY_USB_DP_CONCURRENT_MODE;
		mutex_unlock(&mdwc->role_switch_mutex);
		return 0;
	}

	dbg_log_string("Set DP lanes:%d refcnt:%d\n", lanes, mdwc->refcnt_dp_usb);

	if (lanes == 2) {
		mutex_lock(&mdwc->role_switch_mutex);
		mdwc->dp_state = DP_2_LANE;
		mdwc->refcnt_dp_usb++;
		mutex_unlock(&mdwc->role_switch_mutex);
		usb_redriver_release_lanes(mdwc->redriver, mdwc->ss_phy->flags & PHY_LANE_A ?
					ORIENTATION_CC1 : ORIENTATION_CC2, 2);
		pm_runtime_get_sync(&mdwc->dwc3->dev);
		mdwc->ss_phy->flags |= PHY_USB_DP_CONCURRENT_MODE;
		pm_runtime_put_sync(&mdwc->dwc3->dev);
		dbg_log_string("Set DP 2 lanes: success, refcnt:%d\n", mdwc->refcnt_dp_usb);
		return 0;
	}

	/* flush any pending work */
	flush_work(&mdwc->resume_work);
	flush_workqueue(mdwc->sm_usb_wq);

	mutex_lock(&mdwc->role_switch_mutex);
	/* 4 lanes handling */
	if (mdwc->id_state == DWC3_ID_GROUND) {
		/* stop USB host mode */
		ret = dwc3_start_stop_host(mdwc, false);
		if (ret)
			goto exit;

		dwc3_msm_set_dp_only_params(mdwc);
		dwc3_start_stop_host(mdwc, true);
	} else if (mdwc->vbus_active) {
		/* stop USB device mode */
		ret = dwc3_start_stop_device(mdwc, false);
		if (ret)
			goto exit;

		dwc3_msm_set_dp_only_params(mdwc);
		dwc3_start_stop_device(mdwc, true);
	} else {
		while (test_bit(WAIT_FOR_LPM, &mdwc->inputs))
			msleep(20);

		dbg_log_string("USB is not active.\n");
		dwc3_msm_set_dp_only_params(mdwc);
	}

	if (mdwc->dp_state != DP_2_LANE)
		mdwc->refcnt_dp_usb++;

	mdwc->dp_state = DP_4_LANE;

exit:
	dbg_log_string("Set DP 4 lanes: %d refcnt:%d\n", ret, mdwc->refcnt_dp_usb);
	mutex_unlock(&mdwc->role_switch_mutex);
	return ret;
}
EXPORT_SYMBOL(dwc3_msm_set_dp_mode);

int dwc3_msm_release_ss_lane(struct device *dev)
{
	return dwc3_msm_set_dp_mode(dev, true, 4);
}
EXPORT_SYMBOL(dwc3_msm_release_ss_lane);

static int qos_rec_irq_read(struct seq_file *s, void *p)
{
	struct dwc3_msm *mdwc = s->private;
	int i;

	for (i = 0; i < PM_QOS_REC_MAX_RECORD; i++)
		seq_printf(s, "%d ", mdwc->qos_rec_irq[i]);

	seq_puts(s, "\n");

	return 0;
}

static int qos_rec_irq_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, qos_rec_irq_read, inode->i_private);
}

static const struct file_operations qos_rec_irq_ops = {
	.open	= qos_rec_irq_open,
	.read	= seq_read,
};

static void dwc3_msm_debug_init(struct dwc3_msm *mdwc)
{
	char ipc_log_ctx_name[40];

	mdwc->dwc_ipc_log_ctxt = ipc_log_context_create(NUM_LOG_PAGES,
					dev_name(mdwc->dev), 0);
	if (!mdwc->dwc_ipc_log_ctxt)
		dev_err(mdwc->dev, "Error getting ipc_log_ctxt\n");

	snprintf(ipc_log_ctx_name, sizeof(ipc_log_ctx_name),
				"%s.ep_events", dev_name(mdwc->dev));
	mdwc->dwc_dma_ipc_log_ctxt = ipc_log_context_create(2 * NUM_LOG_PAGES,
					ipc_log_ctx_name, 0);
	if (!mdwc->dwc_dma_ipc_log_ctxt)
		dev_err(mdwc->dev, "Error getting ipc_log_ctxt for ep_events\n");

	snprintf(ipc_log_ctx_name, sizeof(ipc_log_ctx_name),
				"%s.trace", dev_name(mdwc->dev));
	dwc_trace_ipc_log_ctxt = ipc_log_context_create(3 * NUM_LOG_PAGES,
					ipc_log_ctx_name, 0);
	if (!dwc_trace_ipc_log_ctxt)
		dev_err(mdwc->dev, "Error getting trace_ipc_log_ctxt for ep_events\n");

	mdwc->dbg_dir = debugfs_create_dir(dev_name(mdwc->dev), NULL);
	if (!mdwc->dbg_dir)
		return;
	debugfs_create_u8("qos_req_state", 0644, mdwc->dbg_dir, &mdwc->qos_req_state);
	debugfs_create_bool("qos_rec_start", 0644, mdwc->dbg_dir, &mdwc->qos_rec_start);
	debugfs_create_u8("qos_rec_index", 0644, mdwc->dbg_dir, &mdwc->qos_rec_index);
	debugfs_create_file("qos_rec_irq", 0444, mdwc->dbg_dir, mdwc, &qos_rec_irq_ops);
}

static void dwc3_msm_debug_exit(struct dwc3_msm *mdwc)
{
	ipc_log_context_destroy(mdwc->dwc_ipc_log_ctxt);
	ipc_log_context_destroy(mdwc->dwc_dma_ipc_log_ctxt);
	debugfs_remove_recursive(mdwc->dbg_dir);
}

static void dwc3_host_complete(struct device *dev);
static int dwc3_host_prepare(struct device *dev);
static int dwc3_core_prepare(struct device *dev);
static void dwc3_core_complete(struct device *dev);

static void dwc3_msm_override_pm_ops(struct device *dev, struct dev_pm_ops *pm_ops,
					bool is_host)
{
	if (!dev->driver || !dev->driver->pm) {
		dev_err(dev, "can't override PM OPs\n");
		return;
	}

	(*pm_ops) = (*dev->driver->pm);
	pm_ops->prepare = is_host ? dwc3_host_prepare : dwc3_core_prepare;
	pm_ops->complete = is_host ? dwc3_host_complete : dwc3_core_complete;
	dev->driver->pm = pm_ops;
}

static int dwc3_msm_core_init(struct dwc3_msm *mdwc)
{
	struct device_node *node = mdwc->dev->of_node, *dwc3_node;
	struct dwc3	*dwc;
	int ret = 0;
	u32 val;

	if (mdwc->dwc3)
		return 0;

	ret = usb_phy_init(mdwc->hs_phy);
	if (ret) {
		dev_err(mdwc->dev, "failed to init HS PHY\n");
		goto err;
	}

	if (dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER) {
		ret = usb_phy_init(mdwc->ss_phy);
		if (ret) {
			dev_err(mdwc->dev, "failed to init SS PHY\n");
			goto err;
		}
	}

	/* Assumes dwc3 is the first DT child of dwc3-msm */
	dwc3_node = of_get_next_available_child(node, NULL);
	if (!dwc3_node) {
		dev_err(mdwc->dev, "failed to find dwc3 child\n");
		ret = -ENODEV;
		goto err;
	}

	ret = of_platform_populate(node, NULL, NULL, mdwc->dev);
	if (ret) {
		dev_err(mdwc->dev,
				"failed to add create dwc3 core\n");
		of_node_put(dwc3_node);
		goto err;
	}

	mdwc->dwc3 = of_find_device_by_node(dwc3_node);
	of_node_put(dwc3_node);
	if (!mdwc->dwc3) {
		dev_err(mdwc->dev, "failed to get dwc3 platform device\n");
		ret = -ENODEV;
		goto depopulate;
	}

	dwc = platform_get_drvdata(mdwc->dwc3);
	if (!dwc) {
		dev_err(mdwc->dev, "Failed to get dwc3 device\n");
		mdwc->dwc3 = NULL;
		ret = -ENODEV;
		goto depopulate;
	}

	if (mdwc->override_usb_speed &&
		mdwc->override_usb_speed <= dwc3_msm_get_max_speed(mdwc)) {
		dwc3_msm_set_max_speed(mdwc, mdwc->override_usb_speed);
	}

	/*
	 * On platforms with SS PHY that do not support ss_phy_irq for wakeup
	 * events, use pwr_event_irq for wakeup events in superspeed mode.
	 */
	mdwc->use_pwr_event_for_wakeup = dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER
					&& !mdwc->wakeup_irq[SS_PHY_IRQ].irq;

	if (of_property_read_bool(node, "qcom,host-poweroff-in-pm-suspend")) {
		mdwc->host_poweroff_in_pm_suspend = true;
		dev_info(mdwc->dev, "%s: Core power collapse on host PM suspend\n",
								__func__);
	}

	mdwc->dwc3_drd_sw = usb_role_switch_find_by_fwnode(dev_fwnode(dwc->dev));
	if (IS_ERR(mdwc->dwc3_drd_sw)) {
		dev_err(mdwc->dev, "failed to find dwc3 drd role switch\n");
		ret = PTR_ERR(mdwc->dwc3_drd_sw);
		goto depopulate;
	}

	mdwc->dwc3_pm_ops = kzalloc(sizeof(struct dev_pm_ops), GFP_ATOMIC);
	if (!mdwc->dwc3_pm_ops)
		goto depopulate;

	dwc3_msm_override_pm_ops(dwc->dev, mdwc->dwc3_pm_ops, false);

	mdwc->xhci_pm_ops = kzalloc(sizeof(struct dev_pm_ops), GFP_ATOMIC);
	if (!mdwc->xhci_pm_ops)
		goto free_dwc_pm_ops;

	val = dwc3_msm_read_reg(mdwc->base, DWC3_GSNPSID);
	mdwc->ip = DWC3_GSNPS_ID(val);

	dwc3_force_gen1(mdwc);

	dwc3_msm_notify_event(dwc, DWC3_GSI_EVT_BUF_ALLOC, 0);
	pm_runtime_set_autosuspend_delay(dwc->dev, 0);
	pm_runtime_allow(dwc->dev);

	return 0;

free_dwc_pm_ops:
	kfree(mdwc->dwc3_pm_ops);

depopulate:
	of_platform_depopulate(mdwc->dev);

err:
	return ret;
}

static int dwc3_msm_parse_core_params(struct dwc3_msm *mdwc, struct device_node *dwc3_node)
{
	struct device_node *phy_node;
	int ret;
	const char *prop_string;

	ret = of_property_read_string(dwc3_node, "maximum-speed", &prop_string);
	if (!ret)
		ret = match_string(speed_names, ARRAY_SIZE(speed_names), prop_string);
	mdwc->max_hw_supp_speed = (ret < 0) ? USB_SPEED_UNKNOWN : ret;
	dwc3_msm_set_max_speed(mdwc, mdwc->max_hw_supp_speed);

	ret = of_property_read_string(dwc3_node, "dr_mode", &prop_string);
	if (!ret)
		ret = match_string(usb_dr_modes, ARRAY_SIZE(usb_dr_modes), prop_string);
	mdwc->dr_mode = (ret < 0) ? USB_DR_MODE_UNKNOWN : ret;

	mdwc->core_irq = of_irq_get(dwc3_node, 0);

	phy_node = of_parse_phandle(dwc3_node, "usb-phy", 0);
	mdwc->hs_phy = devm_usb_get_phy_by_node(mdwc->dev, phy_node, NULL);
	if (IS_ERR(mdwc->hs_phy)) {
		dev_err(mdwc->dev, "unable to get hsphy device\n");
		ret = PTR_ERR(mdwc->hs_phy);
		return ret;
	}

	phy_node = of_parse_phandle(dwc3_node, "usb-phy", 1);
	mdwc->ss_phy = devm_usb_get_phy_by_node(mdwc->dev, phy_node, NULL);
	if (IS_ERR(mdwc->ss_phy)) {
		dev_err(mdwc->dev, "unable to get ssphy device\n");
		ret = PTR_ERR(mdwc->ss_phy);
		return ret;
	}

	return ret;
}

static int dwc3_msm_interconnect_vote_populate(struct dwc3_msm *mdwc)
{
	int ret_nom = 0, i = 0, j = 0, count = 0;
	int ret_svs = 0, ret = 0;
	u32 *vv_nom, *vv_svs;

	count = of_property_count_strings(mdwc->dev->of_node,
						"interconnect-names");
	if (count < 0) {
		dev_err(mdwc->dev, "No interconnects found.\n");
		return -EINVAL;
	}

	/* 2 signifies the two types of values avg & peak */
	vv_nom = kzalloc(count * 2 * sizeof(*vv_nom), GFP_KERNEL);
	if (!vv_nom)
		return -ENOMEM;

	vv_svs = kzalloc(count * 2 * sizeof(*vv_svs), GFP_KERNEL);
	if (!vv_svs)
		return -ENOMEM;

	/* of_property_read_u32_array returns 0 on success */
	ret_nom = of_property_read_u32_array(mdwc->dev->of_node,
				"qcom,interconnect-values-nom",
					vv_nom, count * 2);
	if (ret_nom) {
		dev_err(mdwc->dev, "Nominal values not found.\n");
		ret = ret_nom;
		goto icc_err;
	}

	ret_svs = of_property_read_u32_array(mdwc->dev->of_node,
				"qcom,interconnect-values-svs",
					vv_svs, count * 2);
	if (ret_svs) {
		dev_err(mdwc->dev, "Svs values not found.\n");
		ret = ret_svs;
		goto icc_err;
	}

	for (i = USB_DDR; i < count && i < USB_MAX_PATH; i++) {
		/* Updating votes NOMINAL */
		bus_vote_values[BUS_VOTE_NOMINAL][i].avg
						= vv_nom[j];
		bus_vote_values[BUS_VOTE_NOMINAL][i].peak
						= vv_nom[j+1];
		/* Updating votes SVS */
		bus_vote_values[BUS_VOTE_SVS][i].avg
						= vv_svs[j];
		bus_vote_values[BUS_VOTE_SVS][i].peak
						= vv_svs[j+1];
		j += 2;
	}
icc_err:
	/* freeing the temporary resource */
	kfree(vv_nom);
	kfree(vv_svs);

	return ret;
}

static int dwc3_msm_parse_params(struct dwc3_msm *mdwc, struct device_node *node)
{
	struct device_node *diag_node, *wcd_node;
	struct device	*dev = mdwc->dev;
	int ret, size = 0, i;

	of_property_read_u32(node, "qcom,num-gsi-evt-buffs",
				&mdwc->num_gsi_event_buffers);

	if (mdwc->num_gsi_event_buffers) {
		of_get_property(node, "qcom,gsi-reg-offset", &size);
		if (size) {
			mdwc->gsi_reg = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!mdwc->gsi_reg)
				return -ENOMEM;

			mdwc->gsi_reg_offset_cnt =
					(size / sizeof(*mdwc->gsi_reg));
			if (mdwc->gsi_reg_offset_cnt != GSI_REG_MAX) {
				dev_err(dev, "invalid reg offset count\n");
				return -EINVAL;
			}

			of_property_read_u32_array(dev->of_node,
				"qcom,gsi-reg-offset", mdwc->gsi_reg,
				mdwc->gsi_reg_offset_cnt);
		} else {
			dev_err(dev, "err provide qcom,gsi-reg-offset\n");
			return -EINVAL;
		}
	}

	mdwc->use_pdc_interrupts = of_property_read_bool(node,
				"qcom,use-pdc-interrupts");

	mdwc->use_eusb2_phy = of_property_read_bool(node, "qcom,use-eusb2-phy");
	mdwc->disable_host_ssphy_powerdown = of_property_read_bool(node,
				"qcom,disable-host-ssphy-powerdown");

	mdwc->dis_sending_cm_l1_quirk = of_property_read_bool(node,
				"qcom,dis-sending-cm-l1-quirk");

	ret = dwc3_msm_interconnect_vote_populate(mdwc);
	if (ret)
		dev_err(dev, "Using default bus votes\n");

	/* use default as nominal bus voting */
	mdwc->default_bus_vote = BUS_VOTE_NOMINAL;
	of_property_read_u32(node, "qcom,default-bus-vote",
			&mdwc->default_bus_vote);

	if (mdwc->default_bus_vote >= BUS_VOTE_MAX)
		mdwc->default_bus_vote = BUS_VOTE_MAX - 1;
	else if (mdwc->default_bus_vote < BUS_VOTE_NONE)
		mdwc->default_bus_vote = BUS_VOTE_NONE;

	for (i = 0; i < ARRAY_SIZE(mdwc->icc_paths); i++) {
		mdwc->icc_paths[i] = of_icc_get(dev, icc_path_names[i]);
		if (IS_ERR(mdwc->icc_paths[i]))
			mdwc->icc_paths[i] = NULL;
	}

	ret = of_property_read_u32(node, "qcom,pm-qos-latency",
				&mdwc->pm_qos_latency);
	if (ret) {
		dev_dbg(dev, "setting pm-qos-latency to zero.\n");
		mdwc->pm_qos_latency = 0;
	}

	mdwc->force_gen1 = of_property_read_bool(node, "qcom,force-gen1");

	diag_node = of_find_compatible_node(NULL, NULL, "qcom,msm-imem-diag-dload");
	if (!diag_node)
		pr_warn("diag: failed to find diag_dload imem node\n");

	diag_dload  = diag_node ? of_iomap(diag_node, 0) : NULL;
	of_node_put(diag_node);

	wcd_node = of_parse_phandle(node, "qcom,wcd_usbss", 0);
	if (of_device_is_available(wcd_node))
		mdwc->wcd_usbss = true;
	of_node_put(wcd_node);

	return 0;
}

static int dwc3_msm_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node, *dwc3_node;
	struct device	*dev = &pdev->dev;
	struct dwc3_msm *mdwc;
	struct resource *res;
	int ret = 0, i;
	u32 val;

	mdwc = devm_kzalloc(&pdev->dev, sizeof(*mdwc), GFP_KERNEL);
	if (!mdwc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mdwc);
	mdwc->dev = &pdev->dev;

	INIT_LIST_HEAD(&mdwc->req_complete_list);
	INIT_WORK(&mdwc->resume_work, dwc3_resume_work);
	INIT_WORK(&mdwc->restart_usb_work, dwc3_restart_usb_work);
	INIT_WORK(&mdwc->sm_work, dwc3_otg_sm_work);
	INIT_DELAYED_WORK(&mdwc->perf_vote_work, msm_dwc3_perf_vote_work);

	dwc3_msm_debug_init(mdwc);

	mdwc->dwc3_wq = alloc_ordered_workqueue("dwc3_wq", 0);
	if (!mdwc->dwc3_wq) {
		pr_err("%s: Unable to create workqueue dwc3_wq\n", __func__);
		return -ENOMEM;
	}

	/*
	 * Create an ordered freezable workqueue for sm_work so that it gets
	 * scheduled only after pm_resume has happened completely. This helps
	 * in avoiding race conditions between xhci_plat_resume and
	 * xhci_runtime_resume and also between hcd disconnect and xhci_resume.
	 */
	mdwc->sm_usb_wq = alloc_ordered_workqueue("k_sm_usb", WQ_FREEZABLE);
	if (!mdwc->sm_usb_wq) {
		destroy_workqueue(mdwc->dwc3_wq);
		return -ENOMEM;
	}

	/* redriver may not probe, check it at start here */
	mdwc->redriver = usb_get_redriver_by_phandle(node, "ssusb_redriver", 0);
	if (IS_ERR(mdwc->redriver)) {
		ret = PTR_ERR(mdwc->redriver);
		mdwc->redriver = NULL;
		goto err;
	}

	dwc3_msm_orientation_gpio_init(mdwc);

	/* Get all clks and gdsc reference */
	ret = dwc3_msm_get_clk_gdsc(mdwc);
	if (ret) {
		dev_err(&pdev->dev, "error getting clock or gdsc.\n");
		goto err;
	}

	mdwc->id_state = DWC3_ID_FLOAT;
	set_bit(ID, &mdwc->inputs);

	ret = of_property_read_u32(node, "qcom,lpm-to-suspend-delay-ms",
				&mdwc->lpm_to_suspend_delay);
	if (ret) {
		dev_dbg(&pdev->dev, "setting lpm_to_suspend_delay to zero.\n");
		mdwc->lpm_to_suspend_delay = 0;
	}

	for (i = 0; i < USB_MAX_IRQ; i++) {
		mdwc->wakeup_irq[i].irq = platform_get_irq_byname(pdev,
					usb_irq_info[i].name);
		if (mdwc->wakeup_irq[i].irq < 0) {
			/* pwr_evnt_irq is only mandatory irq */
			if (usb_irq_info[i].required) {
				dev_err(&pdev->dev, "get_irq for %s failed\n\n",
						usb_irq_info[i].name);
				ret = -EINVAL;
				goto err;
			}
			mdwc->wakeup_irq[i].irq = 0;
		} else {
			irq_set_status_flags(mdwc->wakeup_irq[i].irq,
						IRQ_NOAUTOEN);

			ret = devm_request_threaded_irq(&pdev->dev,
					mdwc->wakeup_irq[i].irq,
					msm_dwc3_pwr_irq,
					msm_dwc3_pwr_irq_thread,
					usb_irq_info[i].irq_type,
					usb_irq_info[i].name, mdwc);
			if (ret) {
				dev_err(&pdev->dev, "irq req %s failed: %d\n\n",
						usb_irq_info[i].name, ret);
				goto err;
			}
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core_base");
	if (!res) {
		dev_err(&pdev->dev, "missing memory base resource\n");
		ret = -ENODEV;
		goto err;
	}

	mdwc->reg_phys = res->start;
	mdwc->base = devm_ioremap(&pdev->dev, res->start,
			resource_size(res));
	if (!mdwc->base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENODEV;
		goto err;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tcsr_dyn_en_dis");
	if (res) {
		mdwc->tcsr_dyn_en_dis = devm_ioremap(&pdev->dev, res->start,
			resource_size(res));
		if (!mdwc->tcsr_dyn_en_dis) {
			dev_err(&pdev->dev, "ioremap for tcsr_dyn_en_dis failed\n");
			ret = -ENODEV;
			goto err;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"ahb2phy_base");
	if (res) {
		mdwc->ahb2phy_base = devm_ioremap(&pdev->dev,
					res->start, resource_size(res));
		if (IS_ERR_OR_NULL(mdwc->ahb2phy_base)) {
			dev_err(dev, "couldn't find ahb2phy_base addr.\n");
			mdwc->ahb2phy_base = NULL;
		} else {
			/*
			 * On some targets cfg_ahb_clk depends upon usb gdsc
			 * regulator. If cfg_ahb_clk is enabled without
			 * turning on usb gdsc regulator clk is stuck off.
			 */
			dwc3_msm_config_gdsc(mdwc, 1);
			clk_prepare_enable(mdwc->cfg_ahb_clk);
			/* Configure AHB2PHY for one wait state read/write*/
			val = readl_relaxed(mdwc->ahb2phy_base +
					PERIPH_SS_AHB2PHY_TOP_CFG);
			if (val != ONE_READ_WRITE_WAIT) {
				writel_relaxed(ONE_READ_WRITE_WAIT,
					mdwc->ahb2phy_base +
					PERIPH_SS_AHB2PHY_TOP_CFG);
				/* complete above write before using USB PHY */
				mb();
			}
			clk_disable_unprepare(mdwc->cfg_ahb_clk);
			dwc3_msm_config_gdsc(mdwc, 0);
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ebc_desc");
	if (res)
		mdwc->ebc_desc_addr = res->start;

	dwc3_init_dbm(mdwc);

	/* Add power event if the dbm indicates coming out of L1 by interrupt */
	if (!mdwc->dbm_is_1p4) {
		if (!mdwc->wakeup_irq[PWR_EVNT_IRQ].irq) {
			dev_err(&pdev->dev,
				"need pwr_event_irq exiting L1\n");
			ret = -EINVAL;
			goto err;
		}
	}

	ret = dwc3_msm_parse_params(mdwc, node);
	if (ret < 0)
		goto err;

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64))) {
		dev_err(&pdev->dev, "setting DMA mask to 64 failed.\n");
		if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32))) {
			dev_err(&pdev->dev, "setting DMA mask to 32 failed.\n");
			ret = -EOPNOTSUPP;
			goto err;
		}
	}

	/* Assumes dwc3 is the first DT child of dwc3-msm */
	dwc3_node = of_get_next_available_child(node, NULL);
	if (!dwc3_node) {
		dev_err(&pdev->dev, "failed to find dwc3 child\n");
		ret = -ENODEV;
		goto err;
	}

	ret = dwc3_msm_parse_core_params(mdwc, dwc3_node);
	of_node_put(dwc3_node);
	if (ret < 0)
		goto err;

	/*
	 * Clocks and regulators will not be turned on until the first time
	 * runtime PM resume is called. This is to allow for booting up with
	 * charger already connected so as not to disturb PHY line states.
	 */
	mdwc->lpm_flags = MDWC3_POWER_COLLAPSE | MDWC3_SS_PHY_SUSPEND;
	atomic_set(&mdwc->in_lpm, 1);
	pm_runtime_set_autosuspend_delay(mdwc->dev, 1000);
	pm_runtime_use_autosuspend(mdwc->dev);
	device_init_wakeup(mdwc->dev, 1);

	if (of_property_read_bool(node, "qcom,disable-dev-mode-pm"))
		pm_runtime_get_noresume(mdwc->dev);

	mutex_init(&mdwc->suspend_resume_mutex);
	mutex_init(&mdwc->role_switch_mutex);

	if (of_property_read_bool(node, "usb-role-switch")) {
		struct usb_role_switch_desc role_desc = {
			.set = dwc3_msm_usb_role_switch_set_role,
			.get = dwc3_msm_usb_role_switch_get_role,
			.driver_data = mdwc,
			.allow_userspace_control = true,
		};

		role_desc.fwnode = dev_fwnode(&pdev->dev);
		mdwc->role_switch = usb_role_switch_register(mdwc->dev,
								&role_desc);
		if (IS_ERR(mdwc->role_switch)) {
			ret = PTR_ERR(mdwc->role_switch);
			goto put_dwc3;
		}
	}

	if (of_property_read_bool(node, "extcon")) {
		ret = dwc3_msm_extcon_register(mdwc);
		if (ret)
			goto put_dwc3;

		/*
		 * dpdm regulator will be turned on to perform apsd
		 * (automatic power source detection). dpdm regulator is
		 * used to float (or high-z) dp/dm lines. Do not reset
		 * controller/phy if regulator is turned on.
		 * if dpdm is not present controller can be reset
		 * as this controller may not be used for charger detection.
		 */
		mdwc->dpdm_reg = devm_regulator_get_optional(&pdev->dev,
				"dpdm");
		if (IS_ERR(mdwc->dpdm_reg)) {
			dev_dbg(mdwc->dev, "assume cable is not connected\n");
			mdwc->dpdm_reg = NULL;
		}

		if (!mdwc->vbus_active && mdwc->dpdm_reg &&
				regulator_is_enabled(mdwc->dpdm_reg)) {
			mdwc->dpdm_nb.notifier_call = dwc_dpdm_cb;
			regulator_register_notifier(mdwc->dpdm_reg,
					&mdwc->dpdm_nb);
		} else {
			if (!mdwc->role_switch)
				queue_work(mdwc->sm_usb_wq, &mdwc->sm_work);
		}
	}

	if (!mdwc->role_switch && (!mdwc->extcon ||
			!dwc3_msm_extcon_is_valid_source(mdwc))) {
		switch (mdwc->dr_mode) {
		case USB_DR_MODE_OTG:
			if (of_property_read_bool(node,
						"qcom,default-mode-host")) {
				dev_dbg(mdwc->dev, "%s: start host mode\n",
								__func__);
				mdwc->id_state = DWC3_ID_GROUND;
			} else if (of_property_read_bool(node,
						"qcom,default-mode-none")) {
				dev_dbg(mdwc->dev, "%s: stay in none mode\n",
								__func__);
			} else {
				dev_dbg(mdwc->dev, "%s: start peripheral mode\n",
								__func__);
				mdwc->vbus_active = true;
			}
			break;
		case USB_DR_MODE_HOST:
			mdwc->id_state = DWC3_ID_GROUND;
			break;
		case USB_DR_MODE_PERIPHERAL:
			fallthrough;
		default:
			mdwc->vbus_active = true;
			break;
		}

		dwc3_ext_event_notify(mdwc);
	}

	return 0;

put_dwc3:
	usb_role_switch_unregister(mdwc->role_switch);
	for (i = 0; i < ARRAY_SIZE(mdwc->icc_paths); i++)
		icc_put(mdwc->icc_paths[i]);

err:
	destroy_workqueue(mdwc->sm_usb_wq);
	destroy_workqueue(mdwc->dwc3_wq);
	usb_put_redriver(mdwc->redriver);
	return ret;
}

static int dwc3_msm_remove(struct platform_device *pdev)
{
	struct dwc3_msm	*mdwc = platform_get_drvdata(pdev);
	struct dwc3 *dwc = platform_get_drvdata(mdwc->dwc3);
	int i, ret_pm;

	if (diag_dload)
		iounmap(diag_dload);
	usb_role_switch_unregister(mdwc->role_switch);

	if (mdwc->dpdm_nb.notifier_call) {
		regulator_unregister_notifier(mdwc->dpdm_reg, &mdwc->dpdm_nb);
		mdwc->dpdm_nb.notifier_call = NULL;
	}

	/*
	 * In case of system suspend, pm_runtime_get_sync fails.
	 * Hence turn ON the clocks manually.
	 */
	ret_pm = pm_runtime_get_sync(mdwc->dev);
	dbg_event(0xFF, "Remov gsyn", ret_pm);
	if (ret_pm < 0) {
		dev_err(mdwc->dev,
			"pm_runtime_get_sync failed with %d\n", ret_pm);
		clk_prepare_enable(mdwc->noc_aggr_clk);
		clk_prepare_enable(mdwc->utmi_clk);
		clk_prepare_enable(mdwc->core_clk);
		clk_prepare_enable(mdwc->iface_clk);
		clk_prepare_enable(mdwc->sleep_clk);
		clk_prepare_enable(mdwc->bus_aggr_clk);
		clk_prepare_enable(mdwc->xo_clk);
	}

	msm_dwc3_perf_vote_enable(mdwc, false);
	cancel_work_sync(&mdwc->sm_work);

	if (mdwc->hs_phy)
		mdwc->hs_phy->flags &= ~PHY_HOST_MODE;
	dwc3_msm_notify_event(dwc, DWC3_GSI_EVT_BUF_FREE, 0);
	platform_device_put(mdwc->dwc3);
	of_platform_depopulate(&pdev->dev);

	usb_put_redriver(mdwc->redriver);

	dbg_event(0xFF, "Remov put", 0);
	pm_runtime_disable(mdwc->dev);
	pm_runtime_barrier(mdwc->dev);
	pm_runtime_put_sync(mdwc->dev);
	pm_runtime_set_suspended(mdwc->dev);
	device_wakeup_disable(mdwc->dev);

	for (i = 0; i < ARRAY_SIZE(mdwc->icc_paths); i++)
		icc_put(mdwc->icc_paths[i]);

	if (mdwc->wakeup_irq[HS_PHY_IRQ].irq)
		disable_irq(mdwc->wakeup_irq[HS_PHY_IRQ].irq);
	if (mdwc->wakeup_irq[DP_HS_PHY_IRQ].irq)
		disable_irq(mdwc->wakeup_irq[DP_HS_PHY_IRQ].irq);
	if (mdwc->wakeup_irq[DM_HS_PHY_IRQ].irq)
		disable_irq(mdwc->wakeup_irq[DM_HS_PHY_IRQ].irq);
	if (mdwc->wakeup_irq[SS_PHY_IRQ].irq)
		disable_irq(mdwc->wakeup_irq[SS_PHY_IRQ].irq);
	disable_irq(mdwc->wakeup_irq[PWR_EVNT_IRQ].irq);

	clk_disable_unprepare(mdwc->utmi_clk);
	clk_set_rate(mdwc->core_clk, 19200000);
	clk_disable_unprepare(mdwc->core_clk);
	clk_disable_unprepare(mdwc->iface_clk);
	clk_disable_unprepare(mdwc->sleep_clk);
	clk_disable_unprepare(mdwc->xo_clk);

	dwc3_msm_config_gdsc(mdwc, 0);

	destroy_workqueue(mdwc->sm_usb_wq);
	destroy_workqueue(mdwc->dwc3_wq);

	dwc3_msm_debug_exit(mdwc);

	kfree(mdwc->xhci_pm_ops);
	kfree(mdwc->dwc3_pm_ops);

	return 0;
}

static int dwc3_msm_host_ss_powerdown(struct dwc3_msm *mdwc)
{
	u32 reg;

	if (mdwc->disable_host_ssphy_powerdown ||
		dwc3_msm_get_max_speed(mdwc) < USB_SPEED_SUPER)
		return 0;

	reg = dwc3_msm_read_reg(mdwc->base, EXTRA_INP_REG);
	reg |= EXTRA_INP_SS_DISABLE;
	dwc3_msm_write_reg(mdwc->base, EXTRA_INP_REG, reg);
	dwc3_msm_switch_utmi(mdwc, 1);

	usb_phy_notify_disconnect(mdwc->ss_phy,
					USB_SPEED_SUPER);
	usb_phy_set_suspend(mdwc->ss_phy, 1);

	return 0;
}

static int dwc3_msm_host_ss_powerup(struct dwc3_msm *mdwc)
{
	u32 reg;

	dbg_log_string("start: speed:%d\n", dwc3_msm_get_max_speed(mdwc));
	if (!mdwc->in_host_mode ||
		mdwc->disable_host_ssphy_powerdown ||
		dwc3_msm_get_max_speed(mdwc) < USB_SPEED_SUPER)
		return 0;

	usb_phy_set_suspend(mdwc->ss_phy, 0);
	usb_phy_notify_connect(mdwc->ss_phy,
					USB_SPEED_SUPER);

	dwc3_msm_switch_utmi(mdwc, 0);
	reg = dwc3_msm_read_reg(mdwc->base, EXTRA_INP_REG);
	reg &= ~EXTRA_INP_SS_DISABLE;
	dwc3_msm_write_reg(mdwc->base, EXTRA_INP_REG, reg);

	return 0;
}

static int usb_audio_pre_reset(struct usb_interface *intf)
{
	return 0;
}

static int usb_audio_post_reset(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);

	dev_info(&udev->dev, "USB bus reset recovery triggered\n");
	/* Only notify to recover on devices connected to RH */
	if (!udev->parent->parent)
		kobject_uevent(&intf->dev.kobj, KOBJ_CHANGE);

	return 0;
}

static void dwc3_msm_override_audio_drv_ops(struct device *dev)
{
	struct usb_driver *usb_drv;

	if (!dev->driver) {
		dev_err(dev, "can't override USB audio OPs\n");
		return;
	}

	usb_drv = to_usb_driver(dev->driver);
	usb_drv->pre_reset = usb_audio_pre_reset;
	usb_drv->post_reset = usb_audio_post_reset;
}

/*
 * dwc3_msm_update_interfaces - override USB SND driver
 *
 * Intention of this is to add the pre and post USB bus reset callbacks
 * to the generic USB SND driver.  This allows for the DWC3 MSM to
 * generate a kernel uevent to the USB HAL for it to trigger a recovery
 * for USB audio use cases.
 *
 * One use case is that the USB HAL utilizes the roothub's authorized
 * sysfs to trigger a device disconnect/connect.  This allows for the
 * userspace entities to detect an audio device removal, so it can stop
 * the current session.
 */
static void dwc3_msm_update_interfaces(struct usb_device *udev)
{
	int i;

	for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = udev->actconfig->interface[i];
		struct usb_interface_descriptor *desc = NULL;

		desc = &intf->altsetting->desc;
		if (desc->bInterfaceClass == USB_CLASS_AUDIO)
			dwc3_msm_override_audio_drv_ops(&intf->dev);
	}
}

static int dwc3_msm_host_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct dwc3_msm *mdwc = container_of(nb, struct dwc3_msm, host_nb);
	struct dwc3 *dwc = platform_get_drvdata(mdwc->dwc3);
	struct usb_device *udev = ptr;

	if (event != USB_DEVICE_ADD && event != USB_DEVICE_REMOVE)
		return NOTIFY_DONE;

	/*
	 * Regardless of where the device is in the host tree, the USB generic
	 * device's PM resume/suspend should be skipped.  Otherwise, the USB
	 * generic device will end up with failures during PM resume, as XHCI
	 * relies on the DWC3 MSM to issue the PM runtime resume to wake up
	 * the entire host device chain.
	 */
	if (event == USB_DEVICE_ADD)
		dev_pm_syscore_device(&udev->dev, true);
	/*
	 * For direct-attach devices, new udev is direct child of root hub
	 * i.e. dwc -> xhci -> root_hub -> udev
	 * root_hub's udev->parent==NULL, so traverse struct device hierarchy
	 */
	if (udev->parent && !udev->parent->parent &&
			udev->dev.parent->parent == &dwc->xhci->dev) {
		if (event == USB_DEVICE_ADD) {
			if (!dwc3_msm_is_ss_rhport_connected(mdwc)) {
				/*
				 * Core clock rate can be reduced only if root
				 * hub SS port is not enabled/connected.
				 */
				clk_set_rate(mdwc->core_clk,
				mdwc->core_clk_rate_hs);
				dev_dbg(mdwc->dev,
					"set hs core clk rate %ld\n",
					mdwc->core_clk_rate_hs);
				mdwc->max_rh_port_speed = USB_SPEED_HIGH;
				dwc3_msm_update_bus_bw(mdwc, BUS_VOTE_SVS);
				dwc3_msm_host_ss_powerdown(mdwc);

				if (mdwc->wcd_usbss)
					wcd_usbss_dpdm_switch_update(true,
							udev->speed == USB_SPEED_HIGH);
				dwc3_msm_update_interfaces(udev);
			} else {
				if (mdwc->max_rh_port_speed < USB_SPEED_SUPER)
					dwc3_msm_host_ss_powerup(mdwc);
				mdwc->max_rh_port_speed = USB_SPEED_SUPER;
			}
		} else {
			/* set rate back to default core clk rate */
			clk_set_rate(mdwc->core_clk, mdwc->core_clk_rate);
			dev_dbg(mdwc->dev, "set core clk rate %ld\n",
				mdwc->core_clk_rate);
			mdwc->max_rh_port_speed = USB_SPEED_UNKNOWN;
			dwc3_msm_update_bus_bw(mdwc, mdwc->default_bus_vote);
			dwc3_msm_host_ss_powerup(mdwc);

			if (udev->parent->speed >= USB_SPEED_SUPER)
				usb_redriver_host_powercycle(mdwc->redriver);

			if (mdwc->wcd_usbss)
				wcd_usbss_dpdm_switch_update(true, true);
		}
	} else if (!udev->parent) {
		/* USB root hub device */
		if (event == USB_DEVICE_ADD) {
			pm_runtime_use_autosuspend(&udev->dev);
			pm_runtime_set_autosuspend_delay(&udev->dev, 1000);
		}
	}

	return NOTIFY_DONE;
}

static void msm_dwc3_perf_vote_update(struct dwc3_msm *mdwc, bool perf_mode)
{
	int latency = mdwc->pm_qos_latency;

	if ((mdwc->perf_mode == perf_mode) || !latency)
		return;

	if (perf_mode)
		cpu_latency_qos_update_request(&mdwc->pm_qos_req_dma, latency);
	else
		cpu_latency_qos_update_request(&mdwc->pm_qos_req_dma,
						PM_QOS_DEFAULT_VALUE);

	mdwc->perf_mode = perf_mode;
	pr_debug("%s: latency updated to: %d\n", __func__,
			perf_mode ? latency : PM_QOS_DEFAULT_VALUE);
}

static void msm_dwc3_perf_vote_work(struct work_struct *w)
{
	struct dwc3_msm *mdwc = container_of(w, struct dwc3_msm,
						perf_vote_work.work);
	struct irq_desc *irq_desc = irq_to_desc(mdwc->core_irq);
	unsigned int new = irq_desc->tot_count;
	unsigned int count = new - mdwc->irq_cnt;
	unsigned int threshold = PM_QOS_DEFAULT_SAMPLE_THRESHOLD;
	unsigned long delay = PM_QOS_DEFAULT_SAMPLE_MS;
	bool in_perf_mode = false;

	if (mdwc->qos_rec_start && mdwc->qos_rec_index < PM_QOS_REC_MAX_RECORD)
		mdwc->qos_rec_irq[mdwc->qos_rec_index++] = count;

	if (mdwc->perf_mode)
		threshold = PM_QOS_PERF_SAMPLE_THRESHOLD;

	if (mdwc->qos_req_state == PM_QOS_REQ_PERF ||
	    (mdwc->qos_req_state == PM_QOS_REQ_DYNAMIC && count >= threshold))
		in_perf_mode = true;

	pr_debug("%s: in_perf_mode:%u, interrupts in last sample:%u\n",
		 __func__, in_perf_mode, count);

	mdwc->irq_cnt = new;
	msm_dwc3_perf_vote_update(mdwc, in_perf_mode);

	/*
	 * in PM_QOS_REQ_DEFAULT and PM_QOS_REQ_PERF, both delay is 100ms,
	 * it will compare irq differences.
	 */
	if (mdwc->qos_req_state == PM_QOS_REQ_DYNAMIC && in_perf_mode)
		delay = PM_QOS_PERF_SAMPLE_MS;

	schedule_delayed_work(&mdwc->perf_vote_work, msecs_to_jiffies(delay));
}

static void msm_dwc3_perf_vote_enable(struct dwc3_msm *mdwc, bool enable)
{
	struct irq_desc *irq_desc = irq_to_desc(mdwc->core_irq);

	if (enable) {
		/* make sure when enable work, save a valid start irq count */
		mdwc->irq_cnt = irq_desc->tot_count;

		/* start default mode intially */
		mdwc->perf_mode = false;
		cpu_latency_qos_add_request(&mdwc->pm_qos_req_dma,
					    PM_QOS_DEFAULT_VALUE);
		schedule_delayed_work(&mdwc->perf_vote_work,
				msecs_to_jiffies(PM_QOS_DEFAULT_SAMPLE_MS));
	} else {
		cancel_delayed_work_sync(&mdwc->perf_vote_work);
		msm_dwc3_perf_vote_update(mdwc, false);
		cpu_latency_qos_remove_request(&mdwc->pm_qos_req_dma);
	}
}

/**
 * dwc3_otg_start_host -  helper function for starting/stopping the host
 * controller driver.
 *
 * @mdwc: Pointer to the dwc3_msm structure.
 * @on: start / stop the host controller driver.
 *
 * Returns 0 on success otherwise negative errno.
 */
static int dwc3_otg_start_host(struct dwc3_msm *mdwc, int on)
{
	int ret = 0;
	struct dwc3 *dwc = platform_get_drvdata(mdwc->dwc3);

	if (on) {
		dev_dbg(mdwc->dev, "%s: turn on host\n", __func__);
		mdwc->hs_phy->flags |= PHY_HOST_MODE;
		dbg_event(0xFF, "hs_phy_flag:", mdwc->hs_phy->flags);

		if (mdwc->wcd_usbss)
			wcd_usbss_switch_update(WCD_USBSS_USB, WCD_USBSS_CABLE_CONNECT);

		ret = pm_runtime_resume_and_get(mdwc->dev);
		if (ret < 0) {
			dev_err(mdwc->dev, "%s: pm_runtime_resume_and_get failed\n", __func__);
			mdwc->hs_phy->flags &= ~PHY_HOST_MODE;
			pm_runtime_set_suspended(mdwc->dev);
			return ret;
		}
		dbg_event(0xFF, "StrtHost gync",
			atomic_read(&mdwc->dev->power.usage_count));
		clk_set_rate(mdwc->core_clk, mdwc->core_clk_rate);

		usb_redriver_notify_connect(mdwc->redriver,
				mdwc->ss_phy->flags & PHY_LANE_A ?
					ORIENTATION_CC1 : ORIENTATION_CC2);
		if (dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER) {
			mdwc->ss_phy->flags |= PHY_HOST_MODE;
			usb_phy_notify_connect(mdwc->ss_phy,
						USB_SPEED_SUPER);
		}

		usb_phy_notify_connect(mdwc->hs_phy, USB_SPEED_HIGH);
		mdwc->host_nb.notifier_call = dwc3_msm_host_notifier;
		usb_register_notify(&mdwc->host_nb);

		if (!dwc->dis_enblslpm_quirk)
			dwc3_en_sleep_mode(mdwc);

		if (mdwc->dis_sending_cm_l1_quirk)
			mdwc3_dis_sending_cm_l1(mdwc);

		/*
		 * Increase Inter-packet delay by 1 UTMI clock cycle (EL_23).
		 *
		 * STAR 9001346572: Host: When a Single USB 2.0 Endpoint Receives NAKs Continuously,
		 * Host Stops Transfers to Other Endpoints. When an active endpoint that is not
		 * currently cached in the host controller is chosen to be cached to the same cache
		 * index as the endpoint that receives NAK, The endpoint that receives the NAK
		 * responses would be in continuous retry mode that would prevent it from getting
		 * evicted out of the host controller cache. This would prevent the new endpoint to
		 * get into the endpoint cache and therefore service to this endpoint is not done.
		 * The workaround is to disable lower layer LSP retrying the USB2.0 NAKed transfer.
		 * Forcing this to LSP upper layer allows next EP to evict the stuck EP from cache.
		 */
		if (DWC3_VER_IS_WITHIN(DWC31, 170A, ANY)) {
			dwc3_msm_write_reg_field(mdwc->base, DWC3_GUCTL1,
				DWC3_GUCTL1_IP_GAP_ADD_ON_MASK, 1);

			dwc3_msm_write_reg_field(mdwc->base, DWC3_GUCTL3,
				DWC3_GUCTL3_USB20_RETRY_DISABLE, 1);
		}

		usb_role_switch_set_role(mdwc->dwc3_drd_sw, USB_ROLE_HOST);
		if (dwc->dr_mode == USB_DR_MODE_OTG)
			flush_work(&dwc->drd_work);
		dwc3_msm_override_pm_ops(&dwc->xhci->dev, mdwc->xhci_pm_ops, true);
		mdwc->in_host_mode = true;
		pm_runtime_use_autosuspend(&dwc->xhci->dev);
		pm_runtime_set_autosuspend_delay(&dwc->xhci->dev, 0);
		pm_runtime_allow(&dwc->xhci->dev);
		pm_runtime_mark_last_busy(&dwc->xhci->dev);

		dwc3_msm_write_reg_field(mdwc->base, DWC3_GUSB3PIPECTL(0),
				DWC3_GUSB3PIPECTL_SUSPHY, 1);

		/* Reduce the U3 exit handshake timer from 8us to approximately
		 * 300ns to avoid lfps handshake interoperability issues
		 */
		if (DWC3_VER_IS(DWC31, 170A)) {
			dwc3_msm_write_reg_field(mdwc->base,
					DWC31_LINK_LU3LFPSRXTIM(0),
					GEN2_U3_EXIT_RSP_RX_CLK_MASK, 6);
			dwc3_msm_write_reg_field(mdwc->base,
					DWC31_LINK_LU3LFPSRXTIM(0),
					GEN1_U3_EXIT_RSP_RX_CLK_MASK, 5);
			dev_dbg(mdwc->dev, "LU3:%08x\n",
				dwc3_msm_read_reg(mdwc->base,
					DWC31_LINK_LU3LFPSRXTIM(0)));
		}

		/* xHCI should have incremented child count as necessary */
		dbg_event(0xFF, "StrtHost psync",
			atomic_read(&mdwc->dev->power.usage_count));
		pm_runtime_mark_last_busy(mdwc->dev);
		pm_runtime_put_sync_autosuspend(mdwc->dev);
	} else {
		dev_dbg(mdwc->dev, "%s: turn off host\n", __func__);

		ret = pm_runtime_resume_and_get(&mdwc->dwc3->dev);
		if (ret < 0) {
			dev_err(mdwc->dev, "%s: pm_runtime_resume_and_get failed\n", __func__);
			pm_runtime_set_suspended(&mdwc->dwc3->dev);
			pm_runtime_set_suspended(mdwc->dev);
			return ret;
		}
		dbg_event(0xFF, "StopHost gsync",
			atomic_read(&mdwc->dev->power.usage_count));

		/*
		 * Able to set max speed back to UNKNOWN as clk mux still set to
		 * UTMI during dwc3_msm_resume().  Need to ensure max speed is
		 * reset before dwc3_gadget_init() is called.  Otherwise, USB
		 * gadget will be set to HS only.
		 */
		mdwc->in_host_mode = false;

		if (!mdwc->ss_release_called) {
			dwc3_msm_host_ss_powerup(mdwc);
			dwc3_msm_clear_dp_only_params(mdwc);
		}

		usb_role_switch_set_role(mdwc->dwc3_drd_sw, USB_ROLE_DEVICE);
		if (dwc->dr_mode == USB_DR_MODE_OTG)
			flush_work(&dwc->drd_work);

		usb_phy_notify_disconnect(mdwc->hs_phy, USB_SPEED_HIGH);
		if (mdwc->ss_phy->flags & PHY_HOST_MODE) {
			usb_phy_notify_disconnect(mdwc->ss_phy,
					USB_SPEED_SUPER);
			mdwc->ss_phy->flags &= ~PHY_HOST_MODE;
		}
		usb_redriver_notify_disconnect(mdwc->redriver);

		mdwc->hs_phy->flags &= ~PHY_HOST_MODE;
		usb_unregister_notify(&mdwc->host_nb);

		dwc3_msm_write_reg_field(mdwc->base, DWC3_GUSB3PIPECTL(0),
				DWC3_GUSB3PIPECTL_SUSPHY, 0);

		/* wait for LPM, to ensure h/w is reset after stop_host */
		set_bit(WAIT_FOR_LPM, &mdwc->inputs);

		pm_runtime_put_sync_suspend(&mdwc->dwc3->dev);
		dbg_event(0xFF, "StopHost psync",
			atomic_read(&mdwc->dev->power.usage_count));

		if (mdwc->wcd_usbss)
			wcd_usbss_switch_update(WCD_USBSS_USB, WCD_USBSS_CABLE_DISCONNECT);
	}

	return 0;
}

static void dwc3_override_vbus_status(struct dwc3_msm *mdwc, bool vbus_present)
{
	/* Update OTG VBUS Valid from HSPHY to controller */
	dwc3_msm_write_reg_field(mdwc->base, HS_PHY_CTRL_REG,
			UTMI_OTG_VBUS_VALID, !!vbus_present);

	/* Update VBUS Valid from SSPHY to controller */
	if (vbus_present) {
		/* Update only if Super Speed is supported */
		if (dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER)
			dwc3_msm_write_reg_field(mdwc->base, SS_PHY_CTRL_REG,
				LANE0_PWR_PRESENT, 1);
	} else {
		dwc3_msm_write_reg_field(mdwc->base, SS_PHY_CTRL_REG,
			LANE0_PWR_PRESENT, 0);
	}
}

/**
 * dwc3_otg_start_peripheral -  bind/unbind the peripheral controller.
 *
 * @mdwc: Pointer to the dwc3_msm structure.
 * @on:   Turn ON/OFF the gadget.
 *
 * Returns 0 on success otherwise negative errno.
 */
static int dwc3_otg_start_peripheral(struct dwc3_msm *mdwc, int on)
{
	struct dwc3 *dwc = platform_get_drvdata(mdwc->dwc3);
	int timeout = 1000;
	int ret;

	ret = pm_runtime_resume_and_get(mdwc->dev);
	if (ret < 0) {
		dev_err(mdwc->dev, "%s: pm_runtime_resume_and_get failed\n", __func__);
		pm_runtime_set_suspended(mdwc->dev);
		return ret;
	}
	dbg_event(0xFF, "StrtGdgt gsync",
		atomic_read(&mdwc->dev->power.usage_count));

	if (on) {
		dev_dbg(mdwc->dev, "%s: turn on gadget\n", __func__);

		if (mdwc->wcd_usbss)
			wcd_usbss_switch_update(WCD_USBSS_USB, WCD_USBSS_CABLE_CONNECT);

		pm_runtime_get_sync(&mdwc->dwc3->dev);
		/*
		 * Ensure DWC3 DRD switch routine is complete before continuing.
		 * The DWC3 DRD sequence will execute a global and core soft
		 * reset during mode switching.  DWC3 MSM needs to avoid setting
		 * up the GSI related resources until that is completed.
		 */
		if (dwc->dr_mode == USB_DR_MODE_OTG)
			flush_work(&dwc->drd_work);
		dwc3_msm_notify_event(dwc, DWC3_GSI_EVT_BUF_SETUP, 0);

		dwc3_override_vbus_status(mdwc, true);

		usb_redriver_notify_connect(mdwc->redriver,
				mdwc->ss_phy->flags & PHY_LANE_A ?
					ORIENTATION_CC1 : ORIENTATION_CC2);
		if (dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER)
			usb_phy_notify_connect(mdwc->ss_phy, USB_SPEED_SUPER);

		usb_phy_notify_connect(mdwc->hs_phy, USB_SPEED_HIGH);

		/*
		 * Core reset is not required during start peripheral. Only
		 * DBM reset is required, hence perform only DBM reset here.
		 */
		dwc3_msm_block_reset(mdwc, false);
		dwc3_dis_sleep_mode(mdwc);
		mdwc->in_device_mode = true;

		/* Reduce the U3 exit handshake timer from 8us to approximately
		 * 300ns to avoid lfps handshake interoperability issues
		 */
		if (DWC3_VER_IS(DWC31, 170A)) {
			dwc3_msm_write_reg_field(mdwc->base,
					DWC31_LINK_LU3LFPSRXTIM(0),
					GEN2_U3_EXIT_RSP_RX_CLK_MASK, 6);
			dwc3_msm_write_reg_field(mdwc->base,
					DWC31_LINK_LU3LFPSRXTIM(0),
					GEN1_U3_EXIT_RSP_RX_CLK_MASK, 5);
			dev_dbg(mdwc->dev, "LU3:%08x\n",
				dwc3_msm_read_reg(mdwc->base,
					DWC31_LINK_LU3LFPSRXTIM(0)));
		}

		usb_role_switch_set_role(mdwc->dwc3_drd_sw, USB_ROLE_DEVICE);
		clk_set_rate(mdwc->core_clk, mdwc->core_clk_rate);
	} else {
		dev_dbg(mdwc->dev, "%s: turn off gadget\n", __func__);

		dwc3_override_vbus_status(mdwc, false);
		mdwc->in_device_mode = false;
		usb_phy_notify_disconnect(mdwc->hs_phy, USB_SPEED_HIGH);
		usb_phy_set_power(mdwc->hs_phy, 0);
		usb_phy_notify_disconnect(mdwc->ss_phy, USB_SPEED_SUPER);
		usb_redriver_notify_disconnect(mdwc->redriver);

		dwc3_msm_notify_event(dwc, DWC3_GSI_EVT_BUF_CLEAR, 0);
		dwc3_override_vbus_status(mdwc, false);
		dwc3_msm_write_reg_field(mdwc->base, DWC3_GUSB3PIPECTL(0),
				DWC3_GUSB3PIPECTL_SUSPHY, 0);

		/*
		 * DWC3 core runtime PM may return an error during the put sync
		 * and nothing else can trigger idle after this point.  If EBUSY
		 * is detected (i.e. dwc->connected == TRUE) then wait for the
		 * connected flag to turn FALSE (set to false during disconnect
		 * or pullup disable), and retry suspend again.
		 */
		ret = pm_runtime_put_sync(&mdwc->dwc3->dev);
		if (ret == -EBUSY) {
			while (--timeout && dwc->connected)
				msleep(20);
			dbg_event(0xFF, "StopGdgt connected", dwc->connected);
			pm_runtime_suspend(&mdwc->dwc3->dev);
		}

		/* wait for LPM, to ensure h/w is reset after stop_peripheral */
		set_bit(WAIT_FOR_LPM, &mdwc->inputs);

		if (mdwc->wcd_usbss)
			wcd_usbss_switch_update(WCD_USBSS_USB, WCD_USBSS_CABLE_DISCONNECT);
	}

	pm_runtime_put_sync(mdwc->dev);
	dbg_event(0xFF, "StopGdgt psync",
		atomic_read(&mdwc->dev->power.usage_count));

	return 0;
}

/**
 * dwc3_otg_sm_work - workqueue function.
 *
 * @w: Pointer to the dwc3 otg workqueue
 *
 * NOTE: After any change in drd_state, we must reschdule the state machine.
 */
static void dwc3_otg_sm_work(struct work_struct *w)
{
	struct dwc3_msm *mdwc = container_of(w, struct dwc3_msm, sm_work);
	struct dwc3 *dwc = NULL;
	bool work = false;
	int ret = 0;
	const char *state;

	if (mdwc->dwc3)
		dwc = platform_get_drvdata(mdwc->dwc3);

	if (!dwc && mdwc->drd_state != DRD_STATE_UNDEFINED) {
		dev_err(mdwc->dev, "dwc is NULL.\n");
		return;
	}

	state = dwc3_drd_state_string(mdwc->drd_state);
	dev_dbg(mdwc->dev, "%s state\n", state);
	dbg_event(0xFF, state, 0);

	/* Check OTG state */
	switch (mdwc->drd_state) {
	case DRD_STATE_UNDEFINED:
		if (mdwc->dpdm_nb.notifier_call) {
			regulator_unregister_notifier(mdwc->dpdm_reg,
					&mdwc->dpdm_nb);
			mdwc->dpdm_nb.notifier_call = NULL;
		}

		pm_runtime_enable(mdwc->dev);
		ret = pm_runtime_resume_and_get(mdwc->dev);
		if (ret < 0) {
			dev_err(mdwc->dev, "%s: pm_runtime_resume_and_get failed\n", __func__);
			pm_runtime_set_suspended(mdwc->dev);
			break;
		}
		ret = dwc3_msm_core_init(mdwc);
		if (ret) {
			dbg_event(0xFF, "core_init failed", ret);
			pm_runtime_put_sync_suspend(mdwc->dev);
			pm_runtime_disable(mdwc->dev);
			work = true;
			break;
		}

		mdwc->drd_state = DRD_STATE_IDLE;

		/* put controller and phy in suspend if no cable connected */
		if (test_bit(ID, &mdwc->inputs) &&
				!test_bit(B_SESS_VLD, &mdwc->inputs)) {
			dbg_event(0xFF, "undef_id_!bsv", 0);
			/*
			 * might not suspend immediately if dwc child has a
			 * pending autosuspend timer
			 */
			pm_runtime_put_sync(mdwc->dev);
			dbg_event(0xFF, "Undef NoUSB",
				atomic_read(&mdwc->dev->power.usage_count));
			break;
		}

		/*
		 * decrement runtime PM refcount as start_peripheral() and
		 * start_host() below each call get_sync() again
		 */
		pm_runtime_put_noidle(mdwc->dev);
		dbg_event(0xFF, "Exit UNDEF", 0);
		fallthrough;
	case DRD_STATE_IDLE:
		if (test_bit(WAIT_FOR_LPM, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "still not in lpm, wait.\n");
			dbg_event(0xFF, "WAIT_FOR_LPM", 0);
			break;
		}

		if (!test_bit(ID, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "!id\n");
			mdwc->drd_state = DRD_STATE_HOST;

			ret = dwc3_otg_start_host(mdwc, 1);
			if (ret) {
				dev_err(mdwc->dev, "unable to start host\n");
				mdwc->drd_state = DRD_STATE_IDLE;
				goto ret;
			}
		} else if (test_bit(B_SESS_VLD, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "b_sess_vld\n");
			/*
			 * Increment pm usage count upon cable connect. Count
			 * is decremented in DRD_STATE_PERIPHERAL state on
			 * cable disconnect or in bus suspend.
			 */
			ret = pm_runtime_resume_and_get(mdwc->dev);
			if (ret < 0) {
				dev_err(mdwc->dev, "%s: pm_runtime_resume_and_get failed\n",
						__func__);
				pm_runtime_set_suspended(mdwc->dev);
				break;
			}
			dbg_event(0xFF, "BIDLE gsync",
				atomic_read(&mdwc->dev->power.usage_count));
			dwc3_otg_start_peripheral(mdwc, 1);
			mdwc->drd_state = DRD_STATE_PERIPHERAL;
			work = true;
		}
		break;

	case DRD_STATE_PERIPHERAL:
		if (!test_bit(B_SESS_VLD, &mdwc->inputs) ||
				!test_bit(ID, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "!id || !bsv\n");
			mdwc->drd_state = DRD_STATE_IDLE;
			dwc3_otg_start_peripheral(mdwc, 0);
			/*
			 * Decrement pm usage count upon cable disconnect
			 * which was incremented upon cable connect in
			 * DRD_STATE_IDLE state
			 */
			pm_runtime_put_sync_suspend(mdwc->dev);
			dbg_event(0xFF, "!BSV psync",
				atomic_read(&mdwc->dev->power.usage_count));
			work = true;
		} else if (test_bit(B_SUSPEND, &mdwc->inputs) &&
			test_bit(B_SESS_VLD, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "BPER bsv && susp\n");
			mdwc->drd_state = DRD_STATE_PERIPHERAL_SUSPEND;
			/*
			 * Decrement pm usage count upon bus suspend.
			 * Count was incremented either upon cable
			 * connect in DRD_STATE_IDLE or host
			 * initiated resume after bus suspend in
			 * DRD_STATE_PERIPHERAL_SUSPEND state
			 */
			pm_runtime_mark_last_busy(mdwc->dev);
			pm_runtime_put_autosuspend(mdwc->dev);
			dbg_event(0xFF, "SUSP put",
				atomic_read(&mdwc->dev->power.usage_count));
		} else if (test_and_clear_bit(CONN_DONE, &mdwc->inputs) && mdwc->wcd_usbss) {
			if (dwc->gadget->speed >= USB_SPEED_SUPER && !mdwc->eud_active)
				wcd_usbss_dpdm_switch_update(false, false);
			else
				wcd_usbss_dpdm_switch_update(true,
					dwc->gadget->speed == USB_SPEED_HIGH ||
					mdwc->eud_active);
		}
		break;

	case DRD_STATE_PERIPHERAL_SUSPEND:
		if (!test_bit(B_SESS_VLD, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "BSUSP: !bsv\n");
			mdwc->drd_state = DRD_STATE_IDLE;
			dwc3_otg_start_peripheral(mdwc, 0);
		} else if (!test_bit(B_SUSPEND, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "BSUSP !susp\n");
			mdwc->drd_state = DRD_STATE_PERIPHERAL;
			/*
			 * Increment pm usage count upon host
			 * initiated resume. Count was decremented
			 * upon bus suspend in
			 * DRD_STATE_PERIPHERAL state.
			 */
			ret = pm_runtime_resume_and_get(mdwc->dev);
			if (ret < 0) {
				dev_err(mdwc->dev, "%s: pm_runtime_resume_and_get failed\n"
						, __func__);
				pm_runtime_set_suspended(mdwc->dev);
				break;
			}
			dbg_event(0xFF, "!SUSP gsync",
				atomic_read(&mdwc->dev->power.usage_count));
		}
		break;

	case DRD_STATE_HOST:
		if (test_bit(ID, &mdwc->inputs)) {
			dev_dbg(mdwc->dev, "id\n");
			dwc3_otg_start_host(mdwc, 0);
			mdwc->drd_state = DRD_STATE_IDLE;
			work = true;
		} else if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST) {
			dev_dbg(mdwc->dev, "still in a_host state. Resuming root hub.\n");
			dbg_event(0xFF, "XHCIResume", 0);
			ret = pm_runtime_resume(&dwc->xhci->dev);
			if (ret > 0) {
				dbg_event(0xFF, "RT active",
					hrtimer_active(&dwc->xhci->dev.power.suspend_timer));
				pm_request_idle(&dwc->xhci->dev);
			}
		}
		break;

	default:
		dev_err(mdwc->dev, "%s: invalid otg-state\n", __func__);

	}

	if (work)
		queue_work(mdwc->sm_usb_wq, &mdwc->sm_work);

ret:
	return;
}

#ifdef CONFIG_PM_SLEEP
static int dwc3_msm_pm_suspend(struct device *dev)
{
	int ret = 0;
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc = NULL;

	if (mdwc->dwc3) {
		dwc = platform_get_drvdata(mdwc->dwc3);

		dev_dbg(dev, "dwc3-msm PM suspend\n");
		dbg_event(0xFF, "PM Sus", 0);

		/*
		 * Check if pm_suspend can proceed irrespective of runtimePM state of
		 * host.
		 */
		if (!mdwc->host_poweroff_in_pm_suspend || !mdwc->in_host_mode) {
			if (!atomic_read(&mdwc->in_lpm)) {
				dev_err(mdwc->dev, "Abort PM suspend!! (USB is outside LPM)\n");
				return -EBUSY;
			}

			atomic_set(&mdwc->pm_suspended, 1);

			return 0;
		}

		/*
		 * PHYs also need to be power collapsed, so call notify_disconnect
		 * before suspend to ensure it.
		 */
		usb_phy_notify_disconnect(mdwc->hs_phy, USB_SPEED_HIGH);
		mdwc->hs_phy->flags &= ~PHY_HOST_MODE;
		usb_phy_notify_disconnect(mdwc->ss_phy, USB_SPEED_SUPER);
		mdwc->ss_phy->flags &= ~PHY_HOST_MODE;
	}

	/*
	 * Power collapse the core. Hence call dwc3_msm_suspend with
	 * 'force_power_collapse' set to 'true'.
	 */
	ret = dwc3_msm_suspend(mdwc, true);
	if (!ret)
		atomic_set(&mdwc->pm_suspended, 1);

	return ret;
}

static int dwc3_msm_pm_resume(struct device *dev)
{
	int ret;
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);

	dev_dbg(dev, "dwc3-msm PM resume\n");
	dbg_event(0xFF, "PM Res", 0);

	atomic_set(&mdwc->pm_suspended, 0);

	/* Let DWC3 core complete determine if resume is needed */
	if (!mdwc->in_host_mode)
		return 0;

	/* Resume dwc to avoid unclocked access by xhci_plat_resume */
	ret = dwc3_msm_resume(mdwc);
	if (ret) {
		dev_err(mdwc->dev, "%s: dwc3_msm_resume failed\n", __func__);
		atomic_set(&mdwc->pm_suspended, 1);
		return ret;
	}
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	if (mdwc->host_poweroff_in_pm_suspend && mdwc->in_host_mode) {
		/* Restore PHY flags if hibernated in host mode */
		mdwc->hs_phy->flags |= PHY_HOST_MODE;
		usb_phy_notify_connect(mdwc->hs_phy, USB_SPEED_HIGH);
		if (dwc3_msm_get_max_speed(mdwc) >= USB_SPEED_SUPER) {
			mdwc->ss_phy->flags |= PHY_HOST_MODE;
			usb_phy_notify_connect(mdwc->ss_phy,
						USB_SPEED_SUPER);
		}
	}
	/* kick in otg state machine */
	queue_work(mdwc->dwc3_wq, &mdwc->resume_work);

	return 0;
}

static void dwc3_host_complete(struct device *dev)
{
	int ret = 0;

	if (dev->power.direct_complete) {
		ret = pm_runtime_resume(dev);
		if (ret < 0) {
			dev_err(dev, "failed to runtime resume, ret %d\n", ret);
			return;
		}
	}
}

static int dwc3_host_prepare(struct device *dev)
{
	/*
	 * It is recommended to use the PM prepare callback to handle situations
	 * where the device is already runtime suspended, in order to avoid
	 * executing the PM suspend callback (duplicate suspend).  When the
	 * prepare callback returns a positive value, the PM core will set the
	 * direct_complete parameter to true, and avoid calling the PM suspend
	 * and PM resume callbacks, and allowing the driver to issue a resume
	 * using PM runtime instead. (within the complete() callback)
	 */
	if (pm_runtime_enabled(dev) && pm_runtime_suspended(dev))
		return 1;

	return 0;
}

static int dwc3_core_prepare(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	dbg_event(0xFF, "Core PM prepare", pm_runtime_suspended(dev));
	/*
	 * It is recommended to use the PM prepare callback to handle situations
	 * where the device is already runtime suspended, in order to avoid
	 * executing the PM suspend callback (duplicate suspend).  When the
	 * prepare callback returns a positive value, the PM core will set the
	 * direct_complete parameter to true, and avoid calling the PM suspend
	 * and PM resume callbacks, and allowing the driver to issue a resume
	 * using PM runtime instead. (within the complete() callback)
	 */
	if (pm_runtime_enabled(dev) && pm_runtime_suspended(dev))
		return 1;

	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_DEVICE &&
			!pm_runtime_suspended(dev)) {
		dev_info(dev, "%s: peripheral mode still active\n", __func__);
		return -EBUSY;
	}

	return 0;
}

static void dwc3_core_complete(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct dwc3_msm *mdwc = dev_get_drvdata(dwc->dev->parent);

	/*
	 * In the PM devices documentation, while leaving system suspend when
	 * the device is in the RPM suspended state, it is recommended to use
	 * the direct_complete flag to determine if an explicit runtime resume
	 * needs to be executed. However, in DWC3 MSM case, we can allow changes
	 * to cable status, or XHCI status to wake up the DWC3 core.
	 */
	dbg_event(0xFF, "Core PM complete", dev->power.direct_complete);
}
#endif

#ifdef CONFIG_PM
static int dwc3_msm_runtime_idle(struct device *dev)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);

	dev_dbg(dev, "DWC3-msm runtime idle\n");
	dbg_event(0xFF, "RT Idle", 0);

	return 0;
}

static int dwc3_msm_runtime_suspend(struct device *dev)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc = NULL;

	if (mdwc->dwc3)
		dwc = platform_get_drvdata(mdwc->dwc3);

	dev_dbg(dev, "DWC3-msm runtime suspend\n");
	dbg_event(0xFF, "RT Sus", 0);

	if (dwc)
		device_init_wakeup(dwc->dev, false);

	return dwc3_msm_suspend(mdwc, false);
}

static int dwc3_msm_runtime_resume(struct device *dev)
{
	struct dwc3_msm *mdwc = dev_get_drvdata(dev);

	dev_dbg(dev, "DWC3-msm runtime resume\n");
	dbg_event(0xFF, "RT Res", 0);

	return dwc3_msm_resume(mdwc);
}
#endif

static const struct dev_pm_ops dwc3_msm_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_msm_pm_suspend, dwc3_msm_pm_resume)
	SET_RUNTIME_PM_OPS(dwc3_msm_runtime_suspend, dwc3_msm_runtime_resume,
				dwc3_msm_runtime_idle)
};

static const struct of_device_id of_dwc3_matach[] = {
	{
		.compatible = "qcom,dwc-usb3-msm",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_dwc3_matach);

static struct platform_driver dwc3_msm_driver = {
	.probe		= dwc3_msm_probe,
	.remove		= dwc3_msm_remove,
	.driver		= {
		.name	= "msm-dwc3",
		.pm	= &dwc3_msm_dev_pm_ops,
		.of_match_table	= of_dwc3_matach,
		.dev_groups =	dwc3_msm_groups,
	},
};

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 MSM Glue Layer");
MODULE_SOFTDEP("pre: phy-generic phy-msm-snps-hs phy-msm-ssusb-qmp eud");

static int dwc3_msm_init(void)
{
	dwc3_msm_kretprobe_init();
	return platform_driver_register(&dwc3_msm_driver);
}
module_init(dwc3_msm_init);

static void __exit dwc3_msm_exit(void)
{
	platform_driver_unregister(&dwc3_msm_driver);
	dwc3_msm_kretprobe_exit();
}
module_exit(dwc3_msm_exit);
