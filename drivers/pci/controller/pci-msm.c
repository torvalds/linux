// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <dt-bindings/regulator/qcom,rpmh-regulator-levels.h>
#include <dt-bindings/interconnect/qcom,icc.h>
#include <linux/aer.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/crc8.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ipc_logging.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msm_pcie.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/rpmsg.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/clk/qcom.h>
#include <soc/qcom/crm.h>
#include <linux/pinctrl/qcom-pinctrl.h>
#include <soc/qcom/pcie-pdc.h>
#include <linux/random.h>

#include "../pci.h"

#define PCIE_VENDOR_ID_QCOM (0x17cb)

#define PCIE20_PARF_DBI_BASE_ADDR (0x350)
#define PCIE20_PARF_SLV_ADDR_SPACE_SIZE (0x358)

#define PCIE_GEN3_PRESET_DEFAULT (0x55555555)
#define PCIE_GEN3_SPCIE_CAP (0x0154)
#define PCIE_GEN3_GEN2_CTRL (0x080c)
#define PCIE_GEN3_RELATED (0x0890)
#define PCIE_GEN3_RELATED_RATE_SHADOW_SEL_MASK (BIT(25) | BIT(24))
/* 0 - Gen3, 1 - Gen4 */
#define PCIE_GEN3_RELATED_RATE_SHADOW_SEL(x) ((x) - PCI_EXP_LNKCAP_SLS_8_0GB)

#define PCIE_GEN3_EQ_CONTROL (0x08a8)
#define PCIE_GEN3_EQ_PSET_REQ_VEC_MASK (GENMASK(23, 8))

#define PCIE_GEN3_EQ_FB_MODE_DIR_CHANGE (0x08ac)
#define PCIE_GEN3_EQ_FMDC_T_MIN_PHASE23_MASK (0x1f)

#define PCIE_GEN3_MISC_CONTROL (0x08bc)

#define PCIE_PL_16GT_CAP (0x168)

#define PCIE20_PARF_SYS_CTRL (0x00)
#define PCIE20_PARF_PM_CTRL (0x20)
#define PCIE20_PARF_PM_STTS (0x24)
#define PCIE20_PARF_PHY_CTRL (0x40)
#define PCIE20_PARF_TEST_BUS (0xe4)
#define PCIE20_PARF_MHI_CLOCK_RESET_CTRL (0x174)
#define PCIE20_PARF_AXI_MSTR_RD_ADDR_HALT (0x1a4)
#define PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT (0x1a8)

#define PCIE20_PCIE_PARF_AXI_MSTR_WR_NS_BDF_HALT (0x4a0)
#define PCIE20_PARF_LTSSM (0x1b0)
#define LTSSM_EN BIT(8)
#define SW_CLR_FLUSH_MODE BIT(10)
#define FLUSH_MODE BIT(11)

#define PCIE20_PARF_INT_ALL_STATUS (0x224)
#define PCIE20_PARF_INT_ALL_CLEAR (0x228)
#define PCIE20_PARF_INT_ALL_MASK (0x22c)

#define PCIE20_PARF_STATUS (0x230)
#define FLUSH_COMPLETED BIT(8)

#define PCIE20_PARF_CFG_BITS_3 (0x2C4)
#define PCIE20_PARF_DEVICE_TYPE (0x1000)
#define PCIE20_PARF_BDF_TO_SID_TABLE_N (0x2000)
#define PCIE20_PARF_BDF_TO_SID_CFG (0x2C00)

#define PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER (0x180)
#define PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET (BIT(31))

#define PCIE20_PARF_DEBUG_INT_EN (0x190)
#define PCIE20_PARF_DEBUG_INT_EN_L1SUB_TIMEOUT_BIT (BIT(0))
#define PCIE20_PARF_INT_ALL_2_STATUS (0x500)
#define PCIE20_PARF_INT_ALL_2_CLEAR (0x504)
#define PCIE20_PARF_INT_ALL_2_MASK (0X508)
#define MSM_PCIE_BW_MGT_INT_STATUS (BIT(25))

#define PCIE20_PARF_DEBUG_CNT_IN_L0S (0xc10)
#define PCIE20_PARF_DEBUG_CNT_IN_L1 (0xc0c)
#define PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L1 (0xc84)
#define PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L2 (0xc88)
#define PCIE20_PARF_PM_STTS_1 (0x28)
#define PM_STATE_L0    0
#define PM_STATE_L0s   1
#define PM_STATE_L1    2
#define PM_STATE_L2    3
#define PCIE_LINK_PM_STATE(val) ((val & (7 << 7)) >> 7)
#define PCIE_LINK_IN_L2_STATE(val) ((PCIE_LINK_PM_STATE(val)) == PM_STATE_L2)
#define PCIE20_PARF_L1SS_SLEEP_MODE_HANDLER_STATUS (0x4D0)
#define PCIE20_PARF_L1SS_SLEEP_MODE_HANDLER_CFG (0x4D4)
#define PCIE20_PARF_CORE_ERRORS (0x3C0)
#define PCIE20_LINK_DOWN_AXI_ECAM_BLOCK_STATUS (0x630)
#define PCIE20_PARF_STATUS (0x230)

#define PCIE20_PARF_CLKREQ_OVERRIDE (0x2b0)
#define PCIE20_PARF_CLKREQ_IN_VALUE (BIT(3))
#define PCIE20_PARF_CLKREQ_IN_ENABLE (BIT(1))

#define PCIE20_ELBI_SYS_CTRL (0x04)
#define PCIE20_ELBI_SYS_STTS (0x08)

#define PCIE20_CAP (0x70)
#define PCIE20_CAP_DEVCTRLSTATUS (PCIE20_CAP + 0x08)
#define PCIE20_CAP_LINKCTRLSTATUS (PCIE20_CAP + 0x10)
#define PCIE_CAP_DLL_ACTIVE BIT(29)

#define PCIE20_COMMAND_STATUS (0x04)
#define PCIE20_HEADER_TYPE (0x0c)
#define PCIE20_BRIDGE_CTRL (0x3c)
#define PCIE20_BRIDGE_CTRL_SBR (BIT(22))

#define PCIE20_DEVICE_CONTROL_STATUS (0x78)
#define PCIE20_DEVICE_CONTROL2_STATUS2 (0x98)
#define PCIE20_PCI_MSI_CAP_ID_NEXT_CTRL_REG (0x50)

#define PCIE20_PIPE_LOOPBACK_CONTROL	(0x8b8)

#define PCIE20_AUX_CLK_FREQ_REG (0xb40)
#define PCIE20_ACK_F_ASPM_CTRL_REG (0x70c)
#define PCIE20_LANE_SKEW_OFF (0x714)
#define PCIE20_ACK_N_FTS (0xff00)

#define PCIE20_PLR_IATU_VIEWPORT (0x900)
#define PCIE20_PLR_IATU_CTRL1 (0x904)
#define PCIE20_PLR_IATU_CTRL2 (0x908)
#define PCIE20_PLR_IATU_LBAR (0x90c)
#define PCIE20_PLR_IATU_UBAR (0x910)
#define PCIE20_PLR_IATU_LAR (0x914)
#define PCIE20_PLR_IATU_LTAR (0x918)
#define PCIE20_PLR_IATU_UTAR (0x91c)

#define PCIE_IATU_BASE(n) (n * 0x200)
#define PCIE_IATU_CTRL1(n) (PCIE_IATU_BASE(n) + 0x00)
#define PCIE_IATU_CTRL2(n) (PCIE_IATU_BASE(n) + 0x04)
#define PCIE_IATU_LBAR(n) (PCIE_IATU_BASE(n) + 0x08)
#define PCIE_IATU_UBAR(n) (PCIE_IATU_BASE(n) + 0x0c)
#define PCIE_IATU_LAR(n) (PCIE_IATU_BASE(n) + 0x10)
#define PCIE_IATU_LTAR(n) (PCIE_IATU_BASE(n) + 0x14)
#define PCIE_IATU_UTAR(n) (PCIE_IATU_BASE(n) + 0x18)

#define PCIE20_PORT_LINK_CTRL_REG (0x710)

#define PCIE20_CTRL1_TYPE_CFG0 (0x04)
#define PCIE20_CTRL1_TYPE_CFG1 (0x05)

#define PCIE20_CAP_ID (0x10)
#define L1SUB_CAP_ID (0x1e)

#define PCIE_CAP_PTR_OFFSET (0x34)
#define PCIE_EXT_CAP_OFFSET (0x100)

#define PCIE20_AER_UNCORR_ERR_STATUS_REG (0x104)
#define PCIE20_AER_CORR_ERR_STATUS_REG (0x110)
#define PCIE20_AER_ROOT_ERR_STATUS_REG (0x130)
#define PCIE20_AER_ERR_SRC_ID_REG (0x134)

#define PCIE20_L1SUB_CONTROL1_REG (0x204)
#define PCIE20_TX_P_FC_CREDIT_STATUS_OFF (0x730)
#define PCIE20_TX_NP_FC_CREDIT_STATUS_OFF (0x734)
#define PCIE20_TX_CPL_FC_CREDIT_STATUS_OFF (0x738)
#define PCIE20_QUEUE_STATUS_OFF (0x73C)

#define RD (0)
#define WR (1)
#define MSM_PCIE_ERROR (-1)

#define PERST_PROPAGATION_DELAY_US_MIN (1000)
#define PERST_PROPAGATION_DELAY_US_MAX (1005)
#define SWITCH_DELAY_MAX (20)
#define REFCLK_STABILIZATION_DELAY_US_MIN (1000)
#define REFCLK_STABILIZATION_DELAY_US_MAX (1005)
#define LINK_UP_TIMEOUT_US_MIN (5000)
#define LINK_UP_TIMEOUT_US_MAX (5101)
#define LINK_UP_CHECK_MAX_COUNT (20)
#define EP_UP_TIMEOUT_US_MIN (1000)
#define EP_UP_TIMEOUT_US_MAX (1005)
#define EP_UP_TIMEOUT_US (1000000)
#define PHY_STABILIZATION_DELAY_US_MIN (995)
#define PHY_STABILIZATION_DELAY_US_MAX (1005)

#define MSM_PCIE_CRC8_POLYNOMIAL (BIT(2) | BIT(1) | BIT(0))

#define GEN1_SPEED (0x1)
#define GEN2_SPEED (0x2)
#define GEN3_SPEED (0x3)

#define LINK_WIDTH_X1 (0x1)
#define LINK_WIDTH_X2 (0x3)
#define LINK_WIDTH_MASK (0x3f)
#define LINK_WIDTH_SHIFT (16)

#define NUM_OF_LANES_MASK (0x1f)
#define NUM_OF_LANES_SHIFT (8)

#define MSM_PCIE_LTSSM_MASK (0x3f)

/*
 * Allow selection of clkreq signal with PCIe controller
 *  1 - PCIe controller receives clk req from cesta
 *  0 - PCIe controller receives clk req from direct clk req gpio
 */
#define PARF_CESTA_CLKREQ_SEL BIT(0)

/* Override bit for sending timeout indication to cesta (debug purpose) */
#define PARF_CESTA_L1SUB_TIMEOUT_OVERRIDE BIT(1)

/* Override value for sending timeout indication to cesta (debug purpose) */
#define PARF_CESTA_L1SUB_TIMEOUT_VALUE BIT(2)

/* Enabling the l1ss timeout indication to cesta */
#define PARF_CESTA_L1SUB_TIMEOUT_EXT_INT_EN BIT(3)

/*
 * Enabling l1ss timeout indication to internal global int generation.
 * Legacy method (0 - no global interrupt for l1ss timeout,
 * 1 - global interrupt for l1ss timeout)
 */
#define PARF_LEGACY_L1SUB_TIMEOUT_INT_EN BIT(31)

#define MSM_PCIE_DRV_MAJOR_VERSION (1)
#define MSM_PCIE_DRV_MINOR_VERSION (0)
#define MSM_PCIE_DRV_SEQ_RESV (0xffff)

#define IPC_TIMEOUT_MS (250)

#define PHY_READY_TIMEOUT_COUNT (10)
#define XMLH_LINK_UP (0x400)
#define MAX_PROP_SIZE (32)
#define MAX_RC_NAME_LEN (15)
#define MSM_PCIE_MAX_VREG (6)
#define MAX_RC_NUM (5)
#define MAX_DEVICE_NUM (20)
#define PCIE_TLP_RD_SIZE (0x5)
#define PCIE_LOG_PAGES (50)
#define PCIE_CONF_SPACE_DW (1024)
#define PCIE_CLEAR (0xdeadbeef)
#define PCIE_LINK_DOWN (0xffffffff)

#define MSM_PCIE_MAX_RESET (5)
#define MSM_PCIE_MAX_PIPE_RESET (1)
#define MSM_PCIE_MAX_LINKDOWN_RESET (2)

/* QPHY_POWER_DOWN_CONTROL */
#define MSM_PCIE_PHY_SW_PWRDN		BIT(0)
#define MSM_PCIE_PHY_REFCLK_DRV_DSBL	BIT(1)

#define MSM_PCIE_PHY_SW_AUX_CLK_REQ	(BIT(6) | BIT(7))
#define MSM_PCIE_PHY_SW_AUX_CLK_REQ_VAL 0x2

#define MSM_PCIE_EXT_CLKBUF_EN_MUX	BIT(1)
#define MSM_PCIE_EXT_CLKBUF_EN_MUX_VAL	0x1
#define ICC_AVG_BW (500)
#define ICC_PEAK_BW (800)

/* Each tick is aux clk freq in MHz */
#define L1SS_TIMEOUT_US_TO_TICKS(x, freq) (x * freq)
#define L1SS_TIMEOUT_US (100000)

#define L23_READY_POLL_TIMEOUT (100000)

#define L1SS_POLL_INTERVAL_US (1000)
#define L1SS_POLL_TIMEOUT_US (200000)

#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define PCIE_UPPER_ADDR(addr) ((u32)((addr) >> 32))
#else
#define PCIE_UPPER_ADDR(addr) (0x0)
#endif
#define PCIE_LOWER_ADDR(addr) ((u32)((addr) & 0xffffffff))

#define PCIE_BUS_PRIV_DATA(bus) \
	((struct msm_pcie_dev_t *)(bus->sysdata))

/* Config Space Offsets */
#define BDF_OFFSET(bus, devfn) \
	((bus << 24) | (devfn << 16))

#define PCIE_DBG(dev, fmt, arg...) do {			 \
	if ((dev) && (dev)->ipc_log_long)   \
		ipc_log_string((dev)->ipc_log_long, \
			"DBG1:%s: " fmt, __func__, ##arg); \
	if ((dev) && (dev)->ipc_log)   \
		ipc_log_string((dev)->ipc_log, "%s: " fmt, __func__, ##arg); \
	} while (0)

#define PCIE_DBG2(dev, fmt, arg...) do {			 \
	if ((dev) && (dev)->ipc_log)   \
		ipc_log_string((dev)->ipc_log, "DBG2:%s: " fmt, \
				__func__, ##arg);\
	} while (0)

#define PCIE_DBG3(dev, fmt, arg...) do {			 \
	if ((dev) && (dev)->ipc_log)   \
		ipc_log_string((dev)->ipc_log, "DBG3:%s: " fmt, \
				__func__, ##arg);\
	} while (0)

#define PCIE_DUMP(dev, fmt, arg...) do {			\
	if ((dev) && (dev)->ipc_log_dump) \
		ipc_log_string((dev)->ipc_log_dump, \
			"DUMP:%s: " fmt, __func__, ##arg); \
	} while (0)

#define PCIE_DBG_FS(dev, fmt, arg...) do {			\
	if ((dev) && (dev)->ipc_log_dump) \
		ipc_log_string((dev)->ipc_log_dump, \
			"DBG_FS:%s: " fmt, __func__, ##arg); \
	pr_alert("%s: " fmt, __func__, ##arg); \
	} while (0)

#define PCIE_INFO(dev, fmt, arg...) do {			 \
	if ((dev) && (dev)->ipc_log_long)   \
		ipc_log_string((dev)->ipc_log_long, \
			"INFO:%s: " fmt, __func__, ##arg); \
	if ((dev) && (dev)->ipc_log)   \
		ipc_log_string((dev)->ipc_log, "%s: " fmt, __func__, ##arg); \
	pr_info("%s: " fmt, __func__, ##arg);  \
	} while (0)

#define PCIE_ERR(dev, fmt, arg...) do {			 \
	if ((dev) && (dev)->ipc_log_long)   \
		ipc_log_string((dev)->ipc_log_long, \
			"ERR:%s: " fmt, __func__, ##arg); \
	if ((dev) && (dev)->ipc_log)   \
		ipc_log_string((dev)->ipc_log, "%s: " fmt, __func__, ##arg); \
	pr_err("%s: " fmt, __func__, arg);  \
	} while (0)

#define CHECK_NTN3_VERSION_MASK (0x000000FF)
#define NTN3_CHIP_VERSION_1 (0x00000000)

enum msm_pcie_res {
	MSM_PCIE_RES_PARF,
	MSM_PCIE_RES_PHY,
	MSM_PCIE_RES_DM_CORE,
	MSM_PCIE_RES_ELBI,
	MSM_PCIE_RES_IATU,
	MSM_PCIE_RES_CONF,
	MSM_PCIE_RES_SM,
	MSM_PCIE_RES_MHI,
	MSM_PCIE_RES_TCSR,
	MSM_PCIE_RES_RUMI,
	MSM_PCIE_MAX_RES,
};

enum msm_pcie_irq {
	MSM_PCIE_INT_A,
	MSM_PCIE_INT_B,
	MSM_PCIE_INT_C,
	MSM_PCIE_INT_D,
	MSM_PCIE_INT_GLOBAL_INT,
	MSM_PCIE_MAX_IRQ,
};

enum msm_pcie_irq_event {
	MSM_PCIE_INT_EVT_LINK_DOWN = 1,
	MSM_PCIE_INT_EVT_BME,
	MSM_PCIE_INT_EVT_PM_TURNOFF,
	MSM_PCIE_INT_EVT_DEBUG,
	MSM_PCIE_INT_EVT_LTR,
	MSM_PCIE_INT_EVT_MHI_Q6,
	MSM_PCIE_INT_EVT_MHI_A7,
	MSM_PCIE_INT_EVT_DSTATE_CHANGE,
	MSM_PCIE_INT_EVT_L1SUB_TIMEOUT,
	MSM_PCIE_INT_EVT_MMIO_WRITE,
	MSM_PCIE_INT_EVT_CFG_WRITE,
	MSM_PCIE_INT_EVT_BRIDGE_FLUSH_N,
	MSM_PCIE_INT_EVT_LINK_UP,
	MSM_PCIE_INT_EVT_AER_LEGACY,
	MSM_PCIE_INT_EVT_AER_ERR,
	MSM_PCIE_INT_EVT_PME_LEGACY,
	MSM_PCIE_INT_EVT_PLS_PME,
	MSM_PCIE_INT_EVT_INTD,
	MSM_PCIE_INT_EVT_INTC,
	MSM_PCIE_INT_EVT_INTB,
	MSM_PCIE_INT_EVT_INTA,
	MSM_PCIE_INT_EVT_EDMA,
	MSM_PCIE_INT_EVT_MSI_0,
	MSM_PCIE_INT_EVT_MSI_1,
	MSM_PCIE_INT_EVT_MSI_2,
	MSM_PCIE_INT_EVT_MSI_3,
	MSM_PCIE_INT_EVT_MSI_4,
	MSM_PCIE_INT_EVT_MSI_5,
	MSM_PCIE_INT_EVT_MSI_6,
	MSM_PCIE_INT_EVT_MSI_7,
	MSM_PCIE_INT_EVT_MAX = 30,
};

enum msm_pcie_gpio {
	MSM_PCIE_GPIO_PERST,
	MSM_PCIE_GPIO_WAKE,
	MSM_PCIE_GPIO_EP,
	MSM_PCIE_GPIO_CARD_PRESENCE_PIN,
	MSM_PCIE_MAX_GPIO
};

enum msm_pcie_link_status {
	MSM_PCIE_LINK_DEINIT,
	MSM_PCIE_LINK_ENABLED,
	MSM_PCIE_LINK_DISABLED,
	MSM_PCIE_LINK_DRV,
	MSM_PCIE_LINK_DOWN,
};

enum msm_pcie_boot_option {
	MSM_PCIE_NO_PROBE_ENUMERATION = BIT(0),
	MSM_PCIE_NO_WAKE_ENUMERATION = BIT(1)
};

enum msm_pcie_ltssm {
	MSM_PCIE_LTSSM_DETECT_QUIET = 0x00,
	MSM_PCIE_LTSSM_DETECT_ACT = 0x01,
	MSM_PCIE_LTSSM_POLL_ACTIVE = 0x02,
	MSM_PCIE_LTSSM_POLL_COMPLIANCE = 0x03,
	MSM_PCIE_LTSSM_POLL_CONFIG = 0x04,
	MSM_PCIE_LTSSM_PRE_DETECT_QUIET = 0x05,
	MSM_PCIE_LTSSM_DETECT_WAIT = 0x06,
	MSM_PCIE_LTSSM_CFG_LINKWD_START = 0x07,
	MSM_PCIE_LTSSM_CFG_LINKWD_ACEPT = 0x08,
	MSM_PCIE_LTSSM_CFG_LANENUM_WAIT = 0x09,
	MSM_PCIE_LTSSM_CFG_LANENUM_ACEPT = 0x0a,
	MSM_PCIE_LTSSM_CFG_COMPLETE = 0x0b,
	MSM_PCIE_LTSSM_CFG_IDLE = 0x0c,
	MSM_PCIE_LTSSM_RCVRY_LOCK = 0x0d,
	MSM_PCIE_LTSSM_RCVRY_SPEED = 0x0e,
	MSM_PCIE_LTSSM_RCVRY_RCVRCFG = 0x0f,
	MSM_PCIE_LTSSM_RCVRY_IDLE = 0x10,
	MSM_PCIE_LTSSM_RCVRY_EQ0 = 0x20,
	MSM_PCIE_LTSSM_RCVRY_EQ1 = 0x21,
	MSM_PCIE_LTSSM_RCVRY_EQ2 = 0x22,
	MSM_PCIE_LTSSM_RCVRY_EQ3 = 0x23,
	MSM_PCIE_LTSSM_L0 = 0x11,
	MSM_PCIE_LTSSM_L0S = 0x12,
	MSM_PCIE_LTSSM_L123_SEND_EIDLE = 0x13,
	MSM_PCIE_LTSSM_L1_IDLE = 0x14,
	MSM_PCIE_LTSSM_L2_IDLE = 0x15,
	MSM_PCIE_LTSSM_L2_WAKE = 0x16,
	MSM_PCIE_LTSSM_DISABLED_ENTRY = 0x17,
	MSM_PCIE_LTSSM_DISABLED_IDLE = 0x18,
	MSM_PCIE_LTSSM_DISABLED = 0x19,
	MSM_PCIE_LTSSM_LPBK_ENTRY = 0x1a,
	MSM_PCIE_LTSSM_LPBK_ACTIVE = 0x1b,
	MSM_PCIE_LTSSM_LPBK_EXIT = 0x1c,
	MSM_PCIE_LTSSM_LPBK_EXIT_TIMEOUT = 0x1d,
	MSM_PCIE_LTSSM_HOT_RESET_ENTRY = 0x1e,
	MSM_PCIE_LTSSM_HOT_RESET = 0x1f,
};

static const char * const msm_pcie_ltssm_str[] = {
	[MSM_PCIE_LTSSM_DETECT_QUIET] = "LTSSM_DETECT_QUIET",
	[MSM_PCIE_LTSSM_DETECT_ACT] = "LTSSM_DETECT_ACT",
	[MSM_PCIE_LTSSM_POLL_ACTIVE] = "LTSSM_POLL_ACTIVE",
	[MSM_PCIE_LTSSM_POLL_COMPLIANCE] = "LTSSM_POLL_COMPLIANCE",
	[MSM_PCIE_LTSSM_POLL_CONFIG] = "LTSSM_POLL_CONFIG",
	[MSM_PCIE_LTSSM_PRE_DETECT_QUIET] = "LTSSM_PRE_DETECT_QUIET",
	[MSM_PCIE_LTSSM_DETECT_WAIT] = "LTSSM_DETECT_WAIT",
	[MSM_PCIE_LTSSM_CFG_LINKWD_START] = "LTSSM_CFG_LINKWD_START",
	[MSM_PCIE_LTSSM_CFG_LINKWD_ACEPT] = "LTSSM_CFG_LINKWD_ACEPT",
	[MSM_PCIE_LTSSM_CFG_LANENUM_WAIT] = "LTSSM_CFG_LANENUM_WAIT",
	[MSM_PCIE_LTSSM_CFG_LANENUM_ACEPT] = "LTSSM_CFG_LANENUM_ACEPT",
	[MSM_PCIE_LTSSM_CFG_COMPLETE] = "LTSSM_CFG_COMPLETE",
	[MSM_PCIE_LTSSM_CFG_IDLE] = "LTSSM_CFG_IDLE",
	[MSM_PCIE_LTSSM_RCVRY_LOCK] = "LTSSM_RCVRY_LOCK",
	[MSM_PCIE_LTSSM_RCVRY_SPEED] = "LTSSM_RCVRY_SPEED",
	[MSM_PCIE_LTSSM_RCVRY_RCVRCFG] = "LTSSM_RCVRY_RCVRCFG",
	[MSM_PCIE_LTSSM_RCVRY_IDLE] = "LTSSM_RCVRY_IDLE",
	[MSM_PCIE_LTSSM_RCVRY_EQ0] = "LTSSM_RCVRY_EQ0",
	[MSM_PCIE_LTSSM_RCVRY_EQ1] = "LTSSM_RCVRY_EQ1",
	[MSM_PCIE_LTSSM_RCVRY_EQ2] = "LTSSM_RCVRY_EQ2",
	[MSM_PCIE_LTSSM_RCVRY_EQ3] = "LTSSM_RCVRY_EQ3",
	[MSM_PCIE_LTSSM_L0] = "LTSSM_L0",
	[MSM_PCIE_LTSSM_L0S] = "LTSSM_L0S",
	[MSM_PCIE_LTSSM_L123_SEND_EIDLE] = "LTSSM_L123_SEND_EIDLE",
	[MSM_PCIE_LTSSM_L1_IDLE] = "LTSSM_L1_IDLE",
	[MSM_PCIE_LTSSM_L2_IDLE] = "LTSSM_L2_IDLE",
	[MSM_PCIE_LTSSM_L2_WAKE] = "LTSSM_L2_WAKE",
	[MSM_PCIE_LTSSM_DISABLED_ENTRY] = "LTSSM_DISABLED_ENTRY",
	[MSM_PCIE_LTSSM_DISABLED_IDLE] = "LTSSM_DISABLED_IDLE",
	[MSM_PCIE_LTSSM_DISABLED] = "LTSSM_DISABLED",
	[MSM_PCIE_LTSSM_LPBK_ENTRY] = "LTSSM_LPBK_ENTRY",
	[MSM_PCIE_LTSSM_LPBK_ACTIVE] = "LTSSM_LPBK_ACTIVE",
	[MSM_PCIE_LTSSM_LPBK_EXIT] = "LTSSM_LPBK_EXIT",
	[MSM_PCIE_LTSSM_LPBK_EXIT_TIMEOUT] = "LTSSM_LPBK_EXIT_TIMEOUT",
	[MSM_PCIE_LTSSM_HOT_RESET_ENTRY] = "LTSSM_HOT_RESET_ENTRY",
	[MSM_PCIE_LTSSM_HOT_RESET] = "LTSSM_HOT_RESET",
};

#define TO_LTSSM_STR(state) ((state) >= ARRAY_SIZE(msm_pcie_ltssm_str) ? \
				"LTSSM_INVALID" : msm_pcie_ltssm_str[state])

enum msm_pcie_debugfs_option {
	MSM_PCIE_OUTPUT_PCIE_INFO,
	MSM_PCIE_DISABLE_LINK,
	MSM_PCIE_ENABLE_LINK,
	MSM_PCIE_DISABLE_ENABLE_LINK,
	MSM_PCIE_DISABLE_L0S,
	MSM_PCIE_ENABLE_L0S,
	MSM_PCIE_DISABLE_L1,
	MSM_PCIE_ENABLE_L1,
	MSM_PCIE_DISABLE_L1SS,
	MSM_PCIE_ENABLE_L1SS,
	MSM_PCIE_ENUMERATION,
	MSM_PCIE_DEENUMERATION,
	MSM_PCIE_READ_PCIE_REGISTER,
	MSM_PCIE_WRITE_PCIE_REGISTER,
	MSM_PCIE_DUMP_PCIE_REGISTER_SPACE,
	MSM_PCIE_DISABLE_AER,
	MSM_PCIE_ENABLE_AER,
	MSM_PCIE_GPIO_STATUS,
	MSM_PCIE_ASSERT_PERST,
	MSM_PCIE_DEASSERT_PERST,
	MSM_PCIE_KEEP_RESOURCES_ON,
	MSM_PCIE_FORCE_GEN1,
	MSM_PCIE_FORCE_GEN2,
	MSM_PCIE_FORCE_GEN3,
	MSM_PCIE_TRIGGER_SBR,
	MSM_PCIE_REMOTE_LOOPBACK,
	MSM_PCIE_LOCAL_LOOPBACK,
	MSM_PCIE_MAX_DEBUGFS_OPTION
};

static const char * const
	msm_pcie_debugfs_option_desc[MSM_PCIE_MAX_DEBUGFS_OPTION] = {
	"OUTPUT PCIE INFO",
	"DISABLE LINK",
	"ENABLE LINK",
	"DISABLE AND ENABLE LINK",
	"DISABLE L0S",
	"ENABLE L0S",
	"DISABLE L1",
	"ENABLE L1",
	"DISABLE L1SS",
	"ENABLE L1SS",
	"ENUMERATE",
	"DE-ENUMERATE",
	"READ A PCIE REGISTER",
	"WRITE TO PCIE REGISTER",
	"DUMP PCIE REGISTER SPACE",
	"SET AER ENABLE FLAG",
	"CLEAR AER ENABLE FLAG",
	"OUTPUT PERST AND WAKE GPIO STATUS",
	"ASSERT PERST",
	"DE-ASSERT PERST",
	"SET KEEP_RESOURCES_ON FLAG",
	"SET MAXIMUM LINK SPEED TO GEN 1",
	"SET MAXIMUM LINK SPEED TO GEN 2",
	"SET MAXIMUM LINK SPEED TO GEN 3",
	"Trigger SBR",
	"PCIE REMOTE LOOPBACK",
	"PCIE LOCAL LOOPBACK",
};

/* gpio info structure */
struct msm_pcie_gpio_info_t {
	char *name;
	uint32_t num;
	bool out;
	uint32_t on;
	uint32_t init;
	bool required;
};

/* voltage regulator info structrue */
struct msm_pcie_vreg_info_t {
	struct regulator *hdl;
	char *name;
	uint32_t max_v;
	uint32_t min_v;
	uint32_t opt_mode;
	bool required;
};

/* reset info structure */
struct msm_pcie_reset_info_t {
	struct reset_control *hdl;
	char *name;
	bool required;
};

/* clock info structure */
struct msm_pcie_clk_info_t {
	struct clk *hdl;
	const char *name;
	u32 freq;

	/*
	 * Suppressible clocks are not turned off during drv suspend.
	 * These clocks will be automatically gated during XO shutdown.
	 */
	bool suppressible;
};

/* resource info structure */
struct msm_pcie_res_info_t {
	char *name;
	struct resource *resource;
	void __iomem *base;
};

/* irq info structrue */
struct msm_pcie_irq_info_t {
	char *name;
	uint32_t num;
};

/* bandwidth info structure */
struct msm_pcie_bw_scale_info_t {
	u32 cx_vreg_min;
	u32 mx_vreg_min;
	u32 rate_change_freq;
};

/* phy info structure */
struct msm_pcie_phy_info_t {
	u32 offset;
	u32 val;
	u32 delay;
};

/* tcsr info structure */
struct msm_pcie_tcsr_info_t {
	u32 offset;
	u32 val;
};

/* sid info structure */
struct msm_pcie_sid_info_t {
	u16 bdf;
	u8 pcie_sid;
	u8 hash;
	u8 next_hash;
	u32 smmu_sid;
	u32 value;
};

/* PCIe device info structure */
struct msm_pcie_device_info {
	struct list_head pcidev_node;
	struct pci_dev *dev;
};

/* DRV IPC command type */
enum msm_pcie_drv_cmds {
	MSM_PCIE_DRV_CMD_ENABLE = 0xc0000000,
	MSM_PCIE_DRV_CMD_DISABLE = 0xc0000001,
	MSM_PCIE_DRV_CMD_ENABLE_L1SS_SLEEP = 0xc0000005,
	MSM_PCIE_DRV_CMD_DISABLE_L1SS_SLEEP = 0xc0000006,
	MSM_PCIE_DRV_CMD_DISABLE_PC = 0xc0000007,
	MSM_PCIE_DRV_CMD_ENABLE_PC = 0xc0000008,
};

/* DRV IPC message type */
enum msm_pcie_drv_msg_id {
	MSM_PCIE_DRV_MSG_ID_ACK = 0xa,
	MSM_PCIE_DRV_MSG_ID_CMD = 0xc,
	MSM_PCIE_DRV_MSG_ID_EVT = 0xe,
};

/* DRV IPC header */
struct __packed msm_pcie_drv_header {
	u16 major_ver;
	u16 minor_ver;
	u16 msg_id;
	u16 seq;
	u16 reply_seq;
	u16 payload_size;
	u32 dev_id;
	u8 reserved[8];
};

/* DRV IPC transfer ring element */
struct __packed msm_pcie_drv_tre {
	u32 dword[4];
};

struct __packed msm_pcie_drv_msg {
	struct msm_pcie_drv_header hdr;
	struct msm_pcie_drv_tre pkt;
};

struct msm_pcie_drv_info {
	bool ep_connected; /* drv supports only one endpoint (no switch) */
	struct msm_pcie_drv_msg drv_enable; /* hand off payload */
	struct msm_pcie_drv_msg drv_disable; /* payload to request back */
	struct msm_pcie_drv_msg drv_enable_l1ss_sleep; /* enable l1ss sleep */
	struct msm_pcie_drv_msg drv_disable_l1ss_sleep; /* disable l1ss sleep */
	struct msm_pcie_drv_msg drv_enable_pc; /* enable drv pc */
	struct msm_pcie_drv_msg drv_disable_pc; /* disable drv pc */
	int dev_id;
	u16 seq;
	u16 reply_seq;
	u32 timeout_ms; /* IPC command timeout */
	struct completion completion;
};

/* For AER logging */

#define AER_ERROR_SOURCES_MAX (128)

#define AER_MAX_TYPEOF_COR_ERRS 16 /* as per PCI_ERR_COR_STATUS */
#define AER_MAX_TYPEOF_UNCOR_ERRS 27 /* as per PCI_ERR_UNCOR_STATUS*/
#define	PCI_EXP_AER_FLAGS (PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE | \
			   PCI_EXP_DEVCTL_FERE | PCI_EXP_DEVCTL_URRE)


#define AER_MAX_MULTI_ERR_DEVICES 5 /* Not likely to have more */

struct msm_aer_err_info {
	struct msm_pcie_dev_t *rdev;
	struct pci_dev *dev[AER_MAX_MULTI_ERR_DEVICES];
	int error_dev_num;

	unsigned int id:16;

	unsigned int severity:2;	/* 0:NONFATAL | 1:FATAL | 2:COR */
	unsigned int __pad1:5;
	unsigned int multi_error_valid:1;

	unsigned int first_error:5;
	unsigned int __pad2:2;
	unsigned int tlp_header_valid:1;

	unsigned int status;		/* COR/UNCOR Error Status */
	unsigned int mask;		/* COR/UNCOR Error Mask */
	struct aer_header_log_regs tlp;	/* TLP Header */

	u32 l1ss_ctl1;			/* PCI_L1SS_CTL1 reg value */
	u16 lnksta;			/* PCI_EXP_LNKSTA reg value */
};

struct aer_err_source {
	unsigned int status;
	unsigned int id;
};

/* AER stats for the device */
struct aer_stats {

	/*
	 * Fields for all AER capable devices. They indicate the errors
	 * "as seen by this device". Note that this may mean that if an
	 * end point is causing problems, the AER counters may increment
	 * at its link partner (e.g. root port) because the errors will be
	 * "seen" by the link partner and not he problematic end point
	 * itself (which may report all counters as 0 as it never saw any
	 * problems).
	 */
	/* Counters for different type of correctable errors */
	u64 dev_cor_errs[AER_MAX_TYPEOF_COR_ERRS];
	/* Counters for different type of fatal uncorrectable errors */
	u64 dev_fatal_errs[AER_MAX_TYPEOF_UNCOR_ERRS];
	/* Counters for different type of nonfatal uncorrectable errors */
	u64 dev_nonfatal_errs[AER_MAX_TYPEOF_UNCOR_ERRS];
	/* Total number of ERR_COR sent by this device */
	u64 dev_total_cor_errs;
	/* Total number of ERR_FATAL sent by this device */
	u64 dev_total_fatal_errs;
	/* Total number of ERR_NONFATAL sent by this device */
	u64 dev_total_nonfatal_errs;

	/*
	 * Fields for Root ports & root complex event collectors only, these
	 * indicate the total number of ERR_COR, ERR_FATAL, and ERR_NONFATAL
	 * messages received by the root port / event collector, INCLUDING the
	 * ones that are generated internally (by the rootport itself)
	 */
	u64 rootport_total_cor_errs;
	u64 rootport_total_fatal_errs;
	u64 rootport_total_nonfatal_errs;
};

#define AER_LOG_TLP_MASKS (PCI_ERR_UNC_POISON_TLP| \
				PCI_ERR_UNC_ECRC| \
				PCI_ERR_UNC_UNSUP| \
				PCI_ERR_UNC_COMP_ABORT| \
				PCI_ERR_UNC_UNX_COMP| \
				PCI_ERR_UNC_MALF_TLP)

#define ERR_COR_ID(d) (d & 0xffff)
#define ERR_UNCOR_ID(d) (d >> 16)

#define AER_AGENT_RECEIVER 0
#define AER_AGENT_REQUESTER 1
#define AER_AGENT_COMPLETER 2
#define AER_AGENT_TRANSMITTER 3

#define AER_AGENT_REQUESTER_MASK(t) ((t == AER_CORRECTABLE) ? \
	0 : (PCI_ERR_UNC_COMP_TIME|PCI_ERR_UNC_UNSUP))
#define AER_AGENT_COMPLETER_MASK(t) ((t == AER_CORRECTABLE) ? \
	0 : PCI_ERR_UNC_COMP_ABORT)
#define AER_AGENT_TRANSMITTER_MASK(t) ((t == AER_CORRECTABLE) ? \
	(PCI_ERR_COR_REP_ROLL|PCI_ERR_COR_REP_TIMER) : 0)

#define AER_GET_AGENT(t, e) \
	((e & AER_AGENT_COMPLETER_MASK(t)) ? AER_AGENT_COMPLETER : \
	(e & AER_AGENT_REQUESTER_MASK(t)) ? AER_AGENT_REQUESTER : \
	(e & AER_AGENT_TRANSMITTER_MASK(t)) ? AER_AGENT_TRANSMITTER : \
	AER_AGENT_RECEIVER)

#define AER_PHYSICAL_LAYER_ERROR 0
#define AER_DATA_LINK_LAYER_ERROR 1
#define AER_TRANSACTION_LAYER_ERROR 2

#define AER_PHYSICAL_LAYER_ERROR_MASK(t) ((t == AER_CORRECTABLE) ? \
	PCI_ERR_COR_RCVR : 0)
#define AER_DATA_LINK_LAYER_ERROR_MASK(t) ((t == AER_CORRECTABLE) ? \
	(PCI_ERR_COR_BAD_TLP| \
	PCI_ERR_COR_BAD_DLLP| \
	PCI_ERR_COR_REP_ROLL| \
	PCI_ERR_COR_REP_TIMER) : PCI_ERR_UNC_DLP)

#define AER_GET_LAYER_ERROR(t, e)					\
	((e & AER_PHYSICAL_LAYER_ERROR_MASK(t)) ? AER_PHYSICAL_LAYER_ERROR : \
	(e & AER_DATA_LINK_LAYER_ERROR_MASK(t)) ? AER_DATA_LINK_LAYER_ERROR : \
	AER_TRANSACTION_LAYER_ERROR)

/*
 * AER error strings
 */
static const char * const aer_error_severity_string[] = {
	"Uncorrected (Non-Fatal)",
	"Uncorrected (Fatal)",
	"Corrected"
};

static const char * const aer_error_layer[] = {
	"Physical Layer",
	"Data Link Layer",
	"Transaction Layer"
};

static const char * const aer_correctable_error_string[] = {
	"RxErr",			/* Bit Position 0	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"BadTLP",			/* Bit Position 6	*/
	"BadDLLP",			/* Bit Position 7	*/
	"Rollover",			/* Bit Position 8	*/
	NULL,
	NULL,
	NULL,
	"Timeout",			/* Bit Position 12	*/
	"NonFatalErr",			/* Bit Position 13	*/
	"CorrIntErr",			/* Bit Position 14	*/
	"HeaderOF",			/* Bit Position 15	*/
	NULL,				/* Bit Position 16	*/
	NULL,				/* Bit Position 17	*/
	NULL,				/* Bit Position 18	*/
	NULL,				/* Bit Position 19	*/
	NULL,				/* Bit Position 20	*/
	NULL,				/* Bit Position 21	*/
	NULL,				/* Bit Position 22	*/
	NULL,				/* Bit Position 23	*/
	NULL,				/* Bit Position 24	*/
	NULL,				/* Bit Position 25	*/
	NULL,				/* Bit Position 26	*/
	NULL,				/* Bit Position 27	*/
	NULL,				/* Bit Position 28	*/
	NULL,				/* Bit Position 29	*/
	NULL,				/* Bit Position 30	*/
	NULL,				/* Bit Position 31	*/
};

static const char * const aer_uncorrectable_error_string[] = {
	"Undefined",			/* Bit Position 0	*/
	NULL,
	NULL,
	NULL,
	"DLP",				/* Bit Position 4	*/
	"SDES",				/* Bit Position 5	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"TLP",				/* Bit Position 12	*/
	"FCP",				/* Bit Position 13	*/
	"CmpltTO",			/* Bit Position 14	*/
	"CmpltAbrt",			/* Bit Position 15	*/
	"UnxCmplt",			/* Bit Position 16	*/
	"RxOF",				/* Bit Position 17	*/
	"MalfTLP",			/* Bit Position 18	*/
	"ECRC",				/* Bit Position 19	*/
	"UnsupReq",			/* Bit Position 20	*/
	"ACSViol",			/* Bit Position 21	*/
	"UncorrIntErr",			/* Bit Position 22	*/
	"BlockedTLP",			/* Bit Position 23	*/
	"AtomicOpBlocked",		/* Bit Position 24	*/
	"TLPBlockedErr",		/* Bit Position 25	*/
	"PoisonTLPBlocked",		/* Bit Position 26	*/
	NULL,				/* Bit Position 27	*/
	NULL,				/* Bit Position 28	*/
	NULL,				/* Bit Position 29	*/
	NULL,				/* Bit Position 30	*/
	NULL,				/* Bit Position 31	*/
};

static const char * const aer_agent_string[] = {
	"Receiver ID",
	"Requester ID",
	"Completer ID",
	"Transmitter ID"
};

/* PCIe SM register indexes as defined by module param*/
enum msm_pcie_sm_regs {
	PCIE_SM_BASE,
	PCIE_SM_PWR_CTRL_OFFSET,
	PCIE_SM_PWR_MASK_OFFSET,
	PCIE_SM_PWR_INSTANCE_OFFSET,
	PCIE_SM_NUM_INSTANCES,
	MAX_PCIE_SM_REGS,
};

/*
 * This array contains the address of the PCIE_SM
 * PWR_CTRL, PWR_CTRL_MASK registers so that these
 * can be programmed for override. If the override
 * is not done then CX Power Collapse won't happen.
 *
 * Format of the array is <address of PCIE_SM reg base>,
 * <offset of PWR_CTRL register>, <offset of PWR_CTRL_MASK register>,
 * <offset of next PCIE instance>, <number of PCIE instances>.
 */
static int pcie_sm_regs[MAX_PCIE_SM_REGS];
static int count;
module_param_array(pcie_sm_regs, int, &count, 0644);
MODULE_PARM_DESC(pcie_sm_regs, "This is needed to override the PWR_CTRL/MASK regs");

/* PCIe State Manager instructions info */
struct msm_pcie_sm_info {
	u32 branch_offset;
	u32 start_offset;
	u32 *sm_seq;
	u32 *branch_seq;
	u32 *reg_dump;
	int sm_seq_len;
	int sm_branch_len;
	int reg_dump_len;
};

/* CESTA power state index */
enum msm_pcie_cesta_pwr_idx {
	POWER_STATE_0,
	POWER_STATE_1,
	MAX_POWER_STATE,
};

/* CESTA perf level index */
enum msm_pcie_cesta_perf_idx {
	PERF_LVL_D3COLD,
	PERF_LVL_L1SS,
	PERF_LVL_GEN1,
	PERF_LVL_GEN2,
	PERF_LVL_GEN3,
	PERF_LVL_GEN4,
	MAX_PERF_LVL,
};

/* CESTA curr perf ol to strings */
static const char * const msm_pcie_cesta_curr_perf_lvl[] = {
	"D3 cold state",
	"L1ss sleep state",
	"Gen1 speed",
	"Gen2 speed",
	"Gen3 speed",
	"Gen4 speed",
	"Invalid state",
};

/* CESTA usage scenarios */
enum msm_pcie_cesta_map_idx {
	D3COLD_STATE,	// Move to D3 Cold state
	D0_STATE,	// Move to D0 state
	DRV_STATE,	// Move to DRV state
	MAX_MAP_IDX,
};

/* CESTA states debug info */
static const char * const msm_pcie_cesta_states[] = {
	"D3 Cold state",
	"D0 state",
	"DRV state",
	"Invalid state",
};

/* CESTA Power state to Perf level mapping w.r.t CESTA usage scenarios */
static u32 msm_pcie_cesta_map[MAX_MAP_IDX][MAX_POWER_STATE] = {
	{PERF_LVL_D3COLD, PERF_LVL_D3COLD},
	{MAX_PERF_LVL, MAX_PERF_LVL},
	{PERF_LVL_L1SS, MAX_PERF_LVL},
};

#if IS_ENABLED(CONFIG_I2C)
struct pcie_i2c_reg_update {
	u32 offset;
	u32 val;
};

/* i2c control interface for a i2c client device */
struct pcie_i2c_ctrl {
	struct i2c_client *client;

	/* client specific register info */
	u32 gpio_config_reg;
	u32 ep_reset_reg;
	u32 ep_reset_gpio_mask;
	u32 *dump_regs;
	u32 dump_reg_count;
	struct pcie_i2c_reg_update *reg_update;
	u32 reg_update_count;
	u32 version_reg;
	bool force_i2c_setting;
	bool ep_reset_postlinkup;
	struct pcie_i2c_reg_update *switch_reg_update;
	u32 switch_reg_update_count;
	/* client specific callbacks */
	int (*client_i2c_read)(struct i2c_client *client, u32 reg_addr,
			       u32 *val);
	int (*client_i2c_write)(struct i2c_client *client, u32 reg_addr,
				u32 val);
	int (*client_i2c_reset)(struct pcie_i2c_ctrl *i2c_ctrl, bool reset);
	void (*client_i2c_dump_regs)(struct pcie_i2c_ctrl *i2c_ctrl);
	void (*client_i2c_de_emphasis_wa)(struct pcie_i2c_ctrl *i2c_ctrl);
};

enum i2c_client_id {
	I2C_CLIENT_ID_INVALID = 0xff,
	I2C_CLIENT_ID_NTN3 = 0,
	I2C_CLIENT_ID_MAX,
};

struct i2c_driver_data {
	enum i2c_client_id client_id;
};
#endif

/* msm pcie device structure */
struct msm_pcie_dev_t {
	struct platform_device *pdev;
	struct pci_dev *dev;
	struct regulator *gdsc_core;
	struct regulator *gdsc_phy;
	struct msm_pcie_vreg_info_t vreg[MSM_PCIE_MAX_VREG];
	struct msm_pcie_gpio_info_t gpio[MSM_PCIE_MAX_GPIO];
	struct msm_pcie_res_info_t res[MSM_PCIE_MAX_RES];
	struct msm_pcie_irq_info_t irq[MSM_PCIE_MAX_IRQ];
	struct msm_pcie_reset_info_t reset[MSM_PCIE_MAX_RESET];
	struct msm_pcie_reset_info_t pipe_reset[MSM_PCIE_MAX_PIPE_RESET];
	struct msm_pcie_reset_info_t linkdown_reset[MSM_PCIE_MAX_LINKDOWN_RESET];

	unsigned int num_pipe_clk;
	struct msm_pcie_clk_info_t *pipe_clk;
	unsigned int num_clk;
	struct msm_pcie_clk_info_t *clk;

	void __iomem *parf;
	void __iomem *phy;
	void __iomem *elbi;
	void __iomem *iatu;
	void __iomem *dm_core;
	void __iomem *conf;
	void __iomem *mhi;
	void __iomem *tcsr;
	void __iomem *rumi;

	uint32_t axi_bar_start;
	uint32_t axi_bar_end;

	uint32_t wake_n;
	uint32_t vreg_n;
	uint32_t gpio_n;
	uint32_t parf_deemph;
	uint32_t parf_swing;

	struct msm_pcie_vreg_info_t *cx_vreg;
	struct msm_pcie_vreg_info_t *mx_vreg;
	struct msm_pcie_bw_scale_info_t *bw_scale;
	u32 bw_gen_max;
	u32 link_width_max;

	struct clk *rate_change_clk;
	struct clk *pipe_clk_mux;
	struct clk *pipe_clk_ext_src;
	struct clk *phy_aux_clk_mux;
	struct clk *phy_aux_clk_ext_src;
	struct clk *ref_clk_src;
	struct clk *ahb_clk;

	bool cfg_access;
	bool apss_based_l1ss_sleep;
	spinlock_t cfg_lock;
	unsigned long irqsave_flags;
	struct mutex enumerate_lock;
	struct mutex setup_lock;

	struct irq_domain *irq_domain;

	enum msm_pcie_link_status link_status;
	bool user_suspend;
	bool disable_pc;

	struct pci_saved_state *default_state;
	struct pci_saved_state *saved_state;

	struct wakeup_source *ws;
	struct icc_path *icc_path;

	/*
	 * Gets set when debugfs based l1 enable/disable is used
	 * Gets unset when pcie_enable() API is called.
	 */
	bool debugfs_l1;
	bool l0s_supported;
	bool l1_supported;
	bool l1ss_supported;
	bool l1_1_pcipm_supported;
	bool l1_2_pcipm_supported;
	bool l1_1_aspm_supported;
	bool l1_2_aspm_supported;
	uint32_t l1_2_th_scale;
	uint32_t l1_2_th_value;
	bool common_clk_en;
	bool clk_power_manage_en;
	bool aux_clk_sync;
	bool aer_enable;
	uint32_t smmu_sid_base;
	uint32_t link_check_max_count;
	uint32_t target_link_speed;
	uint32_t dt_target_link_speed;
	uint32_t current_link_speed;
	uint32_t target_link_width;
	uint32_t current_link_width;
	uint32_t n_fts;
	uint32_t ep_latency;
	uint32_t switch_latency;
	uint32_t wr_halt_size;
	uint32_t slv_addr_space_size;
	uint32_t phy_status_offset;
	uint32_t phy_status_bit;
	uint32_t phy_power_down_offset;
	uint32_t phy_aux_clk_config1_offset;
	uint32_t phy_pll_clk_enable1_offset;
	uint32_t eq_pset_req_vec;
	uint32_t core_preset;
	uint32_t eq_fmdc_t_min_phase23;
	uint32_t cpl_timeout;
	uint32_t current_bdf;
	uint32_t perst_delay_us_min;
	uint32_t perst_delay_us_max;
	uint32_t tlp_rd_size;
	uint32_t aux_clk_freq;
	bool linkdown_panic;
	uint32_t boot_option;
	uint32_t link_speed_override;
	bool lpi_enable;
	bool linkdown_recovery_enable;
	bool gdsc_clk_drv_ss_nonvotable;
	bool pcie_bdf_halt_dis;
	uint32_t device_vendor_id;

	uint32_t pcie_parf_cesta_config;

	bool pcie_halt_feature_dis;
	uint32_t rc_idx;
	uint32_t phy_ver;
	bool drv_ready;
	bool enumerated;
	struct work_struct handle_wake_work;
	struct work_struct handle_sbr_work;
	struct mutex recovery_lock;
	spinlock_t irq_lock;
	struct mutex aspm_lock;
	int prevent_l1;
	ulong linkdown_counter;
	ulong link_turned_on_counter;
	ulong link_turned_off_counter;
	uint64_t l23_rdy_poll_timeout;
	bool suspending;
	ulong wake_counter;
	struct list_head enum_ep_list;
	struct list_head susp_ep_list;
	u32 num_parf_testbus_sel;
	u32 phy_len;
	struct msm_pcie_phy_info_t *phy_sequence;
	u32 tcsr_len;
	struct msm_pcie_tcsr_info_t *tcsr_config;
	u32 sid_info_len;
	struct msm_pcie_sid_info_t *sid_info;
	bool bridge_found;
	struct list_head event_reg_list;
	spinlock_t evt_reg_list_lock;
	bool power_on;
	void *ipc_log;
	void *ipc_log_long;
	void *ipc_log_dump;
	bool use_pinctrl;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_sleep;

	bool config_recovery;
	struct work_struct link_recover_wq;

	struct msm_pcie_drv_info *drv_info;
	struct work_struct drv_enable_pc_work;
	struct work_struct drv_disable_pc_work;

	/* cache drv pc req from RC client, by default drv pc is enabled */
	int drv_disable_pc_vote;
	struct mutex drv_pc_lock;
	struct completion speed_change_completion;

	const char *drv_name;
	bool drv_supported;
	bool panic_genspeed_mismatch;

	DECLARE_KFIFO(aer_fifo, struct aer_err_source, AER_ERROR_SOURCES_MAX);

	bool aer_dump;
	bool panic_on_aer;
	struct aer_stats *aer_stats;
	void (*rumi_init)(struct msm_pcie_dev_t *pcie_dev);

	u32 *filtered_bdfs;
	u32 bdf_count;

	u32 phy_debug_reg_len;
	u32 *phy_debug_reg;

	u32 parf_debug_reg_len;
	u32 *parf_debug_reg;

	u32 dbi_debug_reg_len;
	u32 *dbi_debug_reg;

	/* CESTA related structs */
	/* Device handler when using the crm driver APIs */
	const struct device *crm_dev;
	/* Register space of pcie state manager */
	void __iomem *pcie_sm;
	/* pcie state manager instructions sequence info */
	struct msm_pcie_sm_info *sm_info;
	/* Need to configure the l1ss TO when using cesta */
	u32 l1ss_timeout_us;
	u32 l1ss_sleep_disable;
	u32 clkreq_gpio;
	struct pci_host_bridge *bridge;
	bool no_client_based_bw_voting;
#if IS_ENABLED(CONFIG_I2C)
	struct pcie_i2c_ctrl i2c_ctrl;
#endif

	bool fmd_enable;
};

struct msm_root_dev_t {
	struct msm_pcie_dev_t *pcie_dev;
	struct pci_dev *pci_dev;
};

static u32 msm_pcie_keep_resources_on;

/* high prio WQ */
static struct workqueue_struct *mpcie_wq;

/* debugfs values */
static u32 rc_sel = BIT(0);
static u32 base_sel;
static u32 wr_offset;
static u32 wr_mask;
static u32 wr_value;
static u32 __maybe_unused corr_counter_limit = 5;

/* CRC8 table for BDF to SID translation */
static u8 msm_pcie_crc8_table[CRC8_TABLE_SIZE];

/* PCIe driver state */
static struct pcie_drv_sta {
	u32 rc_num;
	unsigned long rc_drv_enabled;
	struct msm_pcie_dev_t *msm_pcie_dev;
	struct rpmsg_device *rpdev;
	struct work_struct drv_connect; /* connect worker */
	struct mutex drv_lock;
	struct mutex rpmsg_lock;

	/* ssr notification  */
	struct notifier_block nb;
	void *notifier;
} pcie_drv;

#define PCIE_RC_DRV_ENABLED(rc_idx) test_bit((rc_idx), &pcie_drv.rc_drv_enabled)

/* msm pcie device data */
static struct msm_pcie_dev_t msm_pcie_dev[MAX_RC_NUM];

/* regulators */
static struct msm_pcie_vreg_info_t msm_pcie_vreg_info[MSM_PCIE_MAX_VREG] = {
	{NULL, "vreg-3p3", 0, 0, 0, false},
	{NULL, "vreg-1p2", 1200000, 1200000, 18200, true},
	{NULL, "vreg-0p9", 1000000, 1000000, 40000, true},
	{NULL, "vreg-cx", 0, 0, 0, false},
	{NULL, "vreg-mx", 0, 0, 0, false},
	{NULL, "vreg-qref", 880000, 880000, 25700, false},
};

/* GPIOs */
static struct msm_pcie_gpio_info_t msm_pcie_gpio_info[MSM_PCIE_MAX_GPIO] = {
	{"perst-gpio", 0, 1, 0, 0, 1},
	{"wake-gpio", 0, 0, 0, 0, 0},
	{"qcom,ep-gpio", 0, 1, 1, 0, 0},
	{"card-presence-pin", 0, 0, 0, 0, 0}
};

/* resets */
static struct msm_pcie_reset_info_t
msm_pcie_reset_info[MAX_RC_NUM][MSM_PCIE_MAX_RESET] = {
	{
		{NULL, "pcie_0_core_reset", false},
		{NULL, "pcie_phy_reset", false},
		{NULL, "pcie_phy_com_reset", false},
		{NULL, "pcie_phy_nocsr_com_phy_reset", false},
		{NULL, "pcie_0_phy_reset", false}
	},
	{
		{NULL, "pcie_1_core_reset", false},
		{NULL, "pcie_phy_reset", false},
		{NULL, "pcie_phy_com_reset", false},
		{NULL, "pcie_phy_nocsr_com_phy_reset", false},
		{NULL, "pcie_1_phy_reset", false}
	},
	{
		{NULL, "pcie_2_core_reset", false},
		{NULL, "pcie_phy_reset", false},
		{NULL, "pcie_phy_com_reset", false},
		{NULL, "pcie_phy_nocsr_com_phy_reset", false},
		{NULL, "pcie_2_phy_reset", false}
	},
	{
		{NULL, "pcie_3_core_reset", false},
		{NULL, "pcie_phy_reset", false},
		{NULL, "pcie_phy_com_reset", false},
		{NULL, "pcie_phy_nocsr_com_phy_reset", false},
		{NULL, "pcie_3_phy_reset", false}
	},
	{
		{NULL, "pcie_4_core_reset", false},
		{NULL, "pcie_phy_reset", false},
		{NULL, "pcie_phy_com_reset", false},
		{NULL, "pcie_phy_nocsr_com_phy_reset", false},
		{NULL, "pcie_4_phy_reset", false}
	}
};

/* pipe reset  */
static struct msm_pcie_reset_info_t
msm_pcie_pipe_reset_info[MAX_RC_NUM][MSM_PCIE_MAX_PIPE_RESET] = {
	{
		{NULL, "pcie_0_phy_pipe_reset", false}
	},
	{
		{NULL, "pcie_1_phy_pipe_reset", false}
	},
	{
		{NULL, "pcie_2_phy_pipe_reset", false}
	},
	{
		{NULL, "pcie_3_phy_pipe_reset", false}
	},
	{
		{NULL, "pcie_4_phy_pipe_reset", false}
	}
};

/* linkdown recovery resets  */
static struct msm_pcie_reset_info_t
msm_pcie_linkdown_reset_info[MAX_RC_NUM][MSM_PCIE_MAX_LINKDOWN_RESET] = {
	{
		{NULL, "pcie_0_link_down_reset", false},
		{NULL, "pcie_0_phy_nocsr_com_phy_reset", false},
	},
	{
		{NULL, "pcie_1_link_down_reset", false},
		{NULL, "pcie_1_phy_nocsr_com_phy_reset", false},
	},
	{
		{NULL, "pcie_2_link_down_reset", false},
		{NULL, "pcie_2_phy_nocsr_com_phy_reset", false},
	},
	{
		{NULL, "pcie_3_link_down_reset", false},
		{NULL, "pcie_3_phy_nocsr_com_phy_reset", false},
	},
	{
		{NULL, "pcie_4_link_down_reset", false},
		{NULL, "pcie_4_phy_nocsr_com_phy_reset", false},
	},
};

/* resources */
static const struct msm_pcie_res_info_t msm_pcie_res_info[MSM_PCIE_MAX_RES] = {
	{"parf", NULL, NULL},
	{"phy", NULL, NULL},
	{"dm_core", NULL, NULL},
	{"elbi", NULL, NULL},
	{"iatu", NULL, NULL},
	{"conf", NULL, NULL},
	{"pcie_sm", NULL, NULL},
	{"mhi", NULL, NULL},
	{"tcsr", NULL, NULL},
	{"rumi", NULL, NULL}
};

/* irqs */
static const struct msm_pcie_irq_info_t msm_pcie_irq_info[MSM_PCIE_MAX_IRQ] = {
	{"int_a", 0},
	{"int_b", 0},
	{"int_c", 0},
	{"int_d", 0},
	{"int_global_int", 0}
};

enum msm_pcie_reg_dump_type_t {
	MSM_PCIE_DUMP_PARF_REG = 0x0,
	MSM_PCIE_DUMP_DBI_REG,
	MSM_PCIE_DUMP_PHY_REG,
};

/* Rpmsg device functions */
static int msm_pcie_drv_rpmsg_probe(struct rpmsg_device *rpdev);
static void msm_pcie_drv_rpmsg_remove(struct rpmsg_device *rpdev);
static int msm_pcie_drv_rpmsg_cb(struct rpmsg_device *rpdev, void *data,
						int len, void *priv, u32 src);

static int msm_pcie_drv_send_rpmsg(struct msm_pcie_dev_t *pcie_dev,
				   struct msm_pcie_drv_msg *msg);
static void msm_pcie_config_sid(struct msm_pcie_dev_t *dev);
static void msm_pcie_config_l0s_disable_all(struct msm_pcie_dev_t *dev,
				struct pci_bus *bus);
static void msm_pcie_config_l1_disable_all(struct msm_pcie_dev_t *dev,
				struct pci_bus *bus);
static void msm_pcie_config_l1ss_disable_all(struct msm_pcie_dev_t *dev,
				struct pci_bus *bus);
static void msm_pcie_config_l0s_enable_all(struct msm_pcie_dev_t *dev);
static void msm_pcie_config_l1_enable_all(struct msm_pcie_dev_t *dev);
static void msm_pcie_config_l1ss_enable_all(struct msm_pcie_dev_t *dev);

static void msm_pcie_check_l1ss_support_all(struct msm_pcie_dev_t *dev);

static void msm_pcie_config_link_pm(struct msm_pcie_dev_t *dev, bool enable);
static int msm_pcie_set_link_width(struct msm_pcie_dev_t *pcie_dev,
				   u16 target_link_width);

static void msm_pcie_disable(struct msm_pcie_dev_t *dev);
static int msm_pcie_enable(struct msm_pcie_dev_t *dev);

static u32 msm_pcie_reg_copy(struct msm_pcie_dev_t *dev,
		u8 *buf, u32 size, u8 reg_len,
		enum msm_pcie_reg_dump_type_t type)
{
	u32 ret = 0, val, i;
	u32 *seq = NULL;
	u32 seq_len = 0;
	void __iomem *base;

	PCIE_DUMP(dev, "RC%d buf=0x%x size=%u, reg_len=%u\n",
		dev->rc_idx, buf, size, reg_len);
	if (type == MSM_PCIE_DUMP_PARF_REG) {
		seq = dev->parf_debug_reg;
		seq_len = dev->parf_debug_reg_len;
		base = dev->parf;
	} else if (type == MSM_PCIE_DUMP_DBI_REG) {
		seq = dev->dbi_debug_reg;
		seq_len = dev->dbi_debug_reg_len;
		base = dev->dm_core;
	} else if (type == MSM_PCIE_DUMP_PHY_REG) {
		seq = dev->phy_debug_reg;
		seq_len = dev->phy_debug_reg_len;
		base = dev->phy;
	} else {
		return ret;
	}

	if (seq) {
		i =  seq_len;
		while (i && (ret + reg_len <= size)) {
			PCIE_DUMP(dev, "RC%d *seq:%u\n",
				  dev->rc_idx, *seq);
			val = readl_relaxed(base + *seq);
			memcpy(buf, &val, reg_len);
			i--;
			buf += reg_len;
			ret += reg_len;
			seq++;
		}
	}
	return ret;
}

int msm_pcie_reg_dump(struct pci_dev *pci_dev, u8 *buff, u32 len)
{
	struct pci_dev *root_pci_dev;
	struct msm_pcie_dev_t *pcie_dev;
	u32 offset = 0;

	if (!pci_dev)
		return -EINVAL;

	root_pci_dev = pcie_find_root_port(pci_dev);
	if (!root_pci_dev)
		return -ENODEV;

	pcie_dev = PCIE_BUS_PRIV_DATA(root_pci_dev->bus);

	if (!pcie_dev) {
		pr_err("PCIe: did not find RC for pci endpoint device.\n");
		return -ENODEV;
	}

	PCIE_DUMP(pcie_dev, "RC%d hang event dump buff=0x%x len=%u\n",
		pcie_dev->rc_idx, buff, len);

	if (pcie_dev->link_status == MSM_PCIE_LINK_DOWN) {
		pr_err("PCIe: the link is in down state\n");
		return -ENODEV;
	}

	if (pcie_dev->suspending) {
		pr_err("PCIe: the device is in suspend state\n");
		return -ENODEV;
	}

	offset = msm_pcie_reg_copy(pcie_dev, buff, len,
			4, MSM_PCIE_DUMP_PARF_REG);

	buff += offset;
	len -= offset;

	/* check PHY status before dumping DBI registers */
	if (!(readl_relaxed(pcie_dev->phy + pcie_dev->phy_status_offset) &
	    BIT(pcie_dev->phy_status_bit))) {

		PCIE_DUMP(pcie_dev, "RC%d Dump DBI registers\n",
			pcie_dev->rc_idx);
		offset = msm_pcie_reg_copy(pcie_dev, buff, len,
			4, MSM_PCIE_DUMP_DBI_REG);
	} else {
		/* PHY status bit is set to 1 so dump 0's in dbi buffer space */
		PCIE_DUMP(pcie_dev, "RC%d PHY is off, skip DBI\n",
			pcie_dev->rc_idx);
		memset(buff, 0, pcie_dev->dbi_debug_reg_len * 4);
		offset = pcie_dev->dbi_debug_reg_len  * 4;
	}

	buff += offset;
	len -= offset;
	msm_pcie_reg_copy(pcie_dev, buff, len,
			1, MSM_PCIE_DUMP_PHY_REG);

	PCIE_DUMP(pcie_dev, "RC%d hang event Exit\n", pcie_dev->rc_idx);

	return 0;
}
EXPORT_SYMBOL(msm_pcie_reg_dump);

static void msm_pcie_config_perst(struct msm_pcie_dev_t *dev, bool assert)
{
	if (dev->fmd_enable) {
		pr_err("PCIe: FMD is enabled for RC%d\n", dev->rc_idx);
		return;
	}

	if (assert) {
		PCIE_INFO(dev, "PCIe: RC%d: assert PERST\n",
			    dev->rc_idx);
		gpio_set_value(dev->gpio[MSM_PCIE_GPIO_PERST].num,
				    dev->gpio[MSM_PCIE_GPIO_PERST].on);
	} else {
		PCIE_INFO(dev, "PCIe: RC%d: de-assert PERST\n",
			    dev->rc_idx);
		gpio_set_value(dev->gpio[MSM_PCIE_GPIO_PERST].num,
					1 - dev->gpio[MSM_PCIE_GPIO_PERST].on);
	}
}

int msm_pcie_fmd_enable(struct pci_dev *pci_dev)
{
	struct pci_dev *root_pci_dev;
	struct msm_pcie_dev_t *pcie_dev;

	root_pci_dev = pcie_find_root_port(pci_dev);
	if (!root_pci_dev)
		return -ENODEV;

	pcie_dev = PCIE_BUS_PRIV_DATA(root_pci_dev->bus);
	if (!pcie_dev) {
		pr_err("PCIe: did not find RC for pci endpoint device.\n");
		return -ENODEV;
	}

	PCIE_INFO(pcie_dev, "RC%d Enable FMD\n", pcie_dev->rc_idx);
	if (pcie_dev->fmd_enable) {
		pr_err("PCIe: FMD is already enabled for RC%d\n", pcie_dev->rc_idx);
		return 0;
	}

	if (!gpio_get_value(pcie_dev->gpio[MSM_PCIE_GPIO_PERST].num))
		msm_pcie_config_perst(pcie_dev, false);

	pcie_dev->fmd_enable = true;
	return 0;
}
EXPORT_SYMBOL_GPL(msm_pcie_fmd_enable);

static void msm_pcie_write_reg(void __iomem *base, u32 offset, u32 value)
{
	writel_relaxed(value, base + offset);
	/* ensure that changes propagated to the hardware */
	readl_relaxed(base + offset);
}

static void msm_pcie_write_reg_field(void __iomem *base, u32 offset,
	const u32 mask, u32 val)
{
	u32 shift = __ffs(mask);
	u32 tmp = readl_relaxed(base + offset);

	tmp &= ~mask; /* clear written bits */
	val = tmp | (val << shift);
	writel_relaxed(val, base + offset);
	/* ensure that changes propagated to the hardware */
	readl_relaxed(base + offset);
}

static void msm_pcie_clear_set_reg(void __iomem *base, u32 pos,
	u32 clear, u32 set)
{
	u32 val;

	val = readl_relaxed(base + pos);
	val &= ~clear;
	val |= set;
	writel_relaxed(val, base + pos);
	/* ensure that changes propagated to the hardware */
	readl_relaxed(base + pos);
}

static void msm_pcie_config_clear_set_dword(struct pci_dev *pdev,
	int pos, u32 clear, u32 set)
{
	u32 val;

	pci_read_config_dword(pdev, pos, &val);
	val &= ~clear;
	val |= set;
	pci_write_config_dword(pdev, pos, val);
}

static void msm_pcie_rumi_init(struct msm_pcie_dev_t *pcie_dev)
{
	u32 val;
	u32 reset_offs = 0x04;
	u32 phy_ctrl_offs = 0x40;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: enter.\n", pcie_dev->rc_idx);

	/* configure PCIe to RC mode */
	msm_pcie_write_reg(pcie_dev->rumi, 0x54, 0x7c70);

	val = readl_relaxed(pcie_dev->rumi + phy_ctrl_offs) | 0x1000;
	msm_pcie_write_reg(pcie_dev->rumi, phy_ctrl_offs, val);
	usleep_range(10000, 10001);

	msm_pcie_write_reg(pcie_dev->rumi, reset_offs, 0x800);
	usleep_range(50000, 50001);
	msm_pcie_write_reg(pcie_dev->rumi, reset_offs, 0xFFFFFFFF);
	usleep_range(50000, 50001);
	msm_pcie_write_reg(pcie_dev->rumi, reset_offs, 0x800);
	usleep_range(50000, 50001);
	msm_pcie_write_reg(pcie_dev->rumi, reset_offs, 0);
	usleep_range(50000, 50001);

	val = readl_relaxed(pcie_dev->rumi + phy_ctrl_offs) & 0xFFFFEFFF;
	msm_pcie_write_reg(pcie_dev->rumi, phy_ctrl_offs, val);
	usleep_range(10000, 10001);

	val = readl_relaxed(pcie_dev->rumi + phy_ctrl_offs) & 0xFFFFFFFE;
	msm_pcie_write_reg(pcie_dev->rumi, phy_ctrl_offs, val);
}

static void pcie_phy_dump(struct msm_pcie_dev_t *dev)
{
	int i, size;

	size = resource_size(dev->res[MSM_PCIE_RES_PHY].resource);
	for (i = 0; i < size; i += 32) {
		PCIE_DUMP(dev,
			"PCIe PHY of RC%d: 0x%04x %08x %08x %08x %08x %08x %08x %08x %08x\n",
			dev->rc_idx, i,
			readl_relaxed(dev->phy + i),
			readl_relaxed(dev->phy + (i + 4)),
			readl_relaxed(dev->phy + (i + 8)),
			readl_relaxed(dev->phy + (i + 12)),
			readl_relaxed(dev->phy + (i + 16)),
			readl_relaxed(dev->phy + (i + 20)),
			readl_relaxed(dev->phy + (i + 24)),
			readl_relaxed(dev->phy + (i + 28)));
	}
}

static void pcie_tcsr_init(struct msm_pcie_dev_t *dev)
{
	int i;
	struct msm_pcie_tcsr_info_t *tcsr_cfg;

	i = dev->tcsr_len;
	tcsr_cfg = dev->tcsr_config;
	while (i--) {
		msm_pcie_write_reg(dev->tcsr,
			tcsr_cfg->offset,
			tcsr_cfg->val);
		tcsr_cfg++;
	}
}

static int msm_pcie_check_align(struct msm_pcie_dev_t *dev,
						u32 offset)
{
	if (offset % 4) {
		PCIE_ERR(dev,
			"PCIe: RC%d: offset 0x%x is not correctly aligned\n",
			dev->rc_idx, offset);
		return MSM_PCIE_ERROR;
	}

	return 0;
}

static bool msm_pcie_dll_link_active(struct msm_pcie_dev_t *dev)
{
	return (readl_relaxed(dev->dm_core + PCIE20_CAP_LINKCTRLSTATUS) &
		PCIE_CAP_DLL_ACTIVE);
}

static bool msm_pcie_confirm_linkup(struct msm_pcie_dev_t *dev,
						bool check_sw_stts,
						bool check_ep,
						struct pci_dev *pcidev)
{
	if (check_sw_stts && (dev->link_status != MSM_PCIE_LINK_ENABLED)) {
		PCIE_DBG(dev, "PCIe: The link of RC %d is not enabled.\n",
			 dev->rc_idx);
		return false;
	}

	if (!msm_pcie_dll_link_active(dev)) {
		PCIE_DBG(dev, "PCIe: The link of RC %d is not up.\n",
			 dev->rc_idx);
		return false;
	}

	if (check_ep && !pci_device_is_present(pcidev)) {
		PCIE_ERR(dev,
			 "PCIe: RC%d: Config space access failed for BDF 0x%04x\n",
			 dev->rc_idx,
			 PCI_DEVID(pcidev->bus->number, pcidev->devfn));
		return false;
	}

	return true;
}

static void msm_pcie_write_mask(void __iomem *addr,
				uint32_t clear_mask, uint32_t set_mask)
{
	uint32_t val;

	val = (readl_relaxed(addr) & ~clear_mask) | set_mask;
	writel_relaxed(val, addr);
	/* ensure data is written to hardware register */
	readl_relaxed(addr);
}

static void pcie_parf_dump(struct msm_pcie_dev_t *dev)
{
	int i;
	u32 original;

	PCIE_DUMP(dev, "PCIe: RC%d PARF testbus\n", dev->rc_idx);

	original = readl_relaxed(dev->parf + PCIE20_PARF_SYS_CTRL);
	for (i = 0; i <= dev->num_parf_testbus_sel; i++) {
		msm_pcie_write_mask(dev->parf + PCIE20_PARF_SYS_CTRL,
				0xFF0000, i << 16);
		PCIE_DUMP(dev,
			"RC%d: PARF_SYS_CTRL: 0%08x PARF_TEST_BUS: 0%08x\n",
			dev->rc_idx,
			readl_relaxed(dev->parf + PCIE20_PARF_SYS_CTRL),
			readl_relaxed(dev->parf + PCIE20_PARF_TEST_BUS));
	}
	msm_pcie_write_reg(dev->parf, PCIE20_PARF_SYS_CTRL, original);

	PCIE_DUMP(dev, "PCIe: RC%d PARF register dump\n", dev->rc_idx);

	for (i = 0; i < PCIE20_PARF_BDF_TO_SID_TABLE_N; i += 32) {
		PCIE_DUMP(dev,
			"RC%d: 0x%04x %08x %08x %08x %08x %08x %08x %08x %08x\n",
			dev->rc_idx, i,
			readl_relaxed(dev->parf + i),
			readl_relaxed(dev->parf + (i + 4)),
			readl_relaxed(dev->parf + (i + 8)),
			readl_relaxed(dev->parf + (i + 12)),
			readl_relaxed(dev->parf + (i + 16)),
			readl_relaxed(dev->parf + (i + 20)),
			readl_relaxed(dev->parf + (i + 24)),
			readl_relaxed(dev->parf + (i + 28)));
	}
}

static void pcie_sm_dump(struct msm_pcie_dev_t *dev)
{
	int i;
	u32 size;

	if (!dev->pcie_sm)
		return;

	PCIE_DUMP(dev, "PCIe: RC%d State Manager reg dump\n", dev->rc_idx);

	size = resource_size(dev->res[MSM_PCIE_RES_SM].resource);

	for (i = 0; i < dev->sm_info->reg_dump_len && i < size; i++) {
		PCIE_DUMP(dev,
			"RC%d: 0x%04x %08x\n",
			dev->rc_idx, dev->sm_info->reg_dump[i],
			readl_relaxed(dev->pcie_sm + dev->sm_info->reg_dump[i]));
	}
}

static void pcie_crm_dump(struct msm_pcie_dev_t *dev)
{
	int ret;

	if (!dev->pcie_sm)
		return;

	ret = crm_dump_regs("pcie_crm");
	if (ret)
		PCIE_DUMP(dev, "PCIe: RC%d Error dumping crm regs %d\n",
							dev->rc_idx, ret);
}

static void pcie_dm_core_dump(struct msm_pcie_dev_t *dev)
{
	int i, size;

	PCIE_DUMP(dev, "PCIe: RC%d DBI/dm_core register dump\n", dev->rc_idx);

	size = resource_size(dev->res[MSM_PCIE_RES_DM_CORE].resource);

	for (i = 0; i < size; i += 32) {
		PCIE_DUMP(dev,
			"RC%d: 0x%04x %08x %08x %08x %08x %08x %08x %08x %08x\n",
			dev->rc_idx, i,
			readl_relaxed(dev->dm_core + i),
			readl_relaxed(dev->dm_core + (i + 4)),
			readl_relaxed(dev->dm_core + (i + 8)),
			readl_relaxed(dev->dm_core + (i + 12)),
			readl_relaxed(dev->dm_core + (i + 16)),
			readl_relaxed(dev->dm_core + (i + 20)),
			readl_relaxed(dev->dm_core + (i + 24)),
			readl_relaxed(dev->dm_core + (i + 28)));
	}
}

/**
 * msm_pcie_loopback - configure RC in loopback mode and test loopback mode
 * @dev: root commpex
 * @local: If true then use local loopback else use remote loopback
 */
static void msm_pcie_loopback(struct msm_pcie_dev_t *dev, bool local)
{
	/* PCIe DBI base + 8MB as initial PCIe address to be translated to target address */
	phys_addr_t loopback_lbar_phy =
		dev->res[MSM_PCIE_RES_DM_CORE].resource->start + SZ_8M;
	u8 *src_vir_addr;
	void __iomem *iatu_base_vir;
	u32 dbi_base_addr = dev->res[MSM_PCIE_RES_DM_CORE].resource->start;
	u32 iatu_base_phy, iatu_ctrl1_offset, iatu_ctrl2_offset, iatu_lbar_offset, iatu_ubar_offset,
	iatu_lar_offset, iatu_ltar_offset, iatu_utar_offset;
	/* todo: modify if want to use a different iATU region. Default is 1 */
	u32 iatu_n = 1;
	u32 type = 0x0;
	dma_addr_t loopback_dst_addr;
	u8 *loopback_dst_vir;
	u32 ltar_addr_lo;
	bool loopback_test_fail = false;
	int i = 0;

	src_vir_addr = (u8 *)ioremap(loopback_lbar_phy, SZ_4K);
	if (!src_vir_addr) {
		PCIE_ERR(dev, "PCIe: RC%d: ioremap fails for loopback_lbar_phy\n", dev->rc_idx);
		return;
	}

	/*
	 * Use platform dev to get buffer. Doing so will
	 * require change in PCIe platform devicetree to have SMMU/IOMMU tied to
	 * this device and memory region created which can be accessed by PCIe controller.
	 * Refer to change change-id: I15333e3dbf6e67d59538a807ed9622ea10c56554
	 */

	PCIE_DBG_FS(dev, "PCIe: RC%d: Allocate 4K DDR memory and map LBAR.\n", dev->rc_idx);

	if (dma_set_mask_and_coherent(&dev->pdev->dev, DMA_BIT_MASK(64))) {
		PCIE_ERR(dev, "PCIe: RC%d: DMA set mask failed\n", dev->rc_idx);
		iounmap(src_vir_addr);
		return;
	}

	loopback_dst_vir = dma_alloc_coherent(&dev->pdev->dev, SZ_4K,
				&loopback_dst_addr, GFP_KERNEL);
	if (!loopback_dst_vir) {
		PCIE_DBG_FS(dev, "PCIe: RC%d: failed to dma_alloc_coherent.\n", dev->rc_idx);
		iounmap(src_vir_addr);
		return;
	}

	PCIE_DBG_FS(dev, "PCIe: RC%d: VIR DDR memory address: 0x%pK\n",
				dev->rc_idx, loopback_dst_vir);

	PCIE_DBG_FS(dev, "PCIe: RC%d: IOVA DDR memory address: %pad\n",
				dev->rc_idx, &loopback_dst_addr);

	ltar_addr_lo = lower_32_bits(loopback_dst_addr);

	/* need to 4K aligned */
	ltar_addr_lo = rounddown(ltar_addr_lo, SZ_4K);
	if (local) {
		PCIE_DBG_FS(dev, "PCIe: RC%d: Configure Local Loopback.\n", dev->rc_idx);

		/* Disable Gen3 equalization */
		msm_pcie_write_mask(dev->dm_core + PCIE_GEN3_RELATED,
				0, BIT(16));

		PCIE_DBG_FS(dev, "PCIe: RC%d: 0x%x: 0x%x\n",
				dev->rc_idx, dbi_base_addr + PCIE_GEN3_RELATED,
				readl_relaxed(dev->dm_core + PCIE_GEN3_RELATED));

		/* Enable pipe loopback */
		msm_pcie_write_mask(dev->dm_core +  PCIE20_PIPE_LOOPBACK_CONTROL,
				 0, BIT(31));

		PCIE_DBG_FS(dev, "PCIe: RC%d: 0x%x: 0x%x\n",
				dev->rc_idx, dbi_base_addr + PCIE20_PIPE_LOOPBACK_CONTROL,
				readl_relaxed(dev->dm_core + PCIE20_PIPE_LOOPBACK_CONTROL));
	} else {
		PCIE_DBG_FS(dev, "PCIe: RC%d: Configure remote Loopback.\n", dev->rc_idx);
	}

	/* Enable Loopback */
	msm_pcie_write_mask(dev->dm_core +  PCIE20_PORT_LINK_CTRL_REG, 0, BIT(2));

	/* Set BME for RC */
	msm_pcie_write_mask(dev->dm_core + PCIE20_COMMAND_STATUS, 0, BIT(2)|BIT(1));
	PCIE_DBG_FS(dev, "PCIe: RC%d: 0x%x: 0x%x\n",
			dev->rc_idx, dbi_base_addr + PCIE20_PORT_LINK_CTRL_REG,
			readl_relaxed(dev->dm_core + PCIE20_PORT_LINK_CTRL_REG));

	iatu_base_vir = dev->iatu;
	iatu_base_phy = dev->res[MSM_PCIE_RES_IATU].resource->start;

	iatu_ctrl1_offset = PCIE_IATU_CTRL1(iatu_n);
	iatu_ctrl2_offset = PCIE_IATU_CTRL2(iatu_n);
	iatu_lbar_offset = PCIE_IATU_LBAR(iatu_n);
	iatu_ubar_offset = PCIE_IATU_UBAR(iatu_n);
	iatu_lar_offset = PCIE_IATU_LAR(iatu_n);
	iatu_ltar_offset = PCIE_IATU_LTAR(iatu_n);
	iatu_utar_offset = PCIE_IATU_UTAR(iatu_n);

	PCIE_DBG_FS(dev, "PCIe: RC%d: Setup iATU.\n", dev->rc_idx);
	/* Switch off region before changing it */
	msm_pcie_write_reg(iatu_base_vir, iatu_ctrl2_offset, 0);

	/* Setup for address matching */
	writel_relaxed(type, iatu_base_vir + iatu_ctrl1_offset);
	PCIE_DBG_FS(dev, "PCIe: RC%d: PCIE20_PLR_IATU_CTRL1:\t0x%x: 0x%x\n",
			dev->rc_idx, iatu_base_phy + iatu_ctrl1_offset,
			readl_relaxed(iatu_base_vir + iatu_ctrl1_offset));

	/* Program base address to be translated */
	writel_relaxed(loopback_lbar_phy, iatu_base_vir + iatu_lbar_offset);
	PCIE_DBG_FS(dev, "PCIe: RC%d: PCIE20_PLR_IATU_LBAR:\t0x%x: 0x%x\n",
			dev->rc_idx, iatu_base_phy + iatu_lbar_offset,
			readl_relaxed(iatu_base_vir + iatu_lbar_offset));

	writel_relaxed(0x0, iatu_base_vir + iatu_ubar_offset);
	PCIE_DBG_FS(dev, "PCIe: RC%d: PCIE20_PLR_IATU_UBAR:\t0x%x: 0x%x\n",
			dev->rc_idx, iatu_base_phy + iatu_ubar_offset,
			readl_relaxed(iatu_base_vir + iatu_ubar_offset));

	/* Program end address to be translated */
	writel_relaxed(loopback_lbar_phy + 0x1FFF, iatu_base_vir + iatu_lar_offset);
	PCIE_DBG_FS(dev, "PCIe: RC%d: PCIE20_PLR_IATU_LAR:\t0x%x: 0x%x\n",
			dev->rc_idx, iatu_base_phy + iatu_lar_offset,
			readl_relaxed(iatu_base_vir + iatu_lar_offset));

	/* Program base address of tranlated (new address) */
	writel_relaxed(ltar_addr_lo, iatu_base_vir + iatu_ltar_offset);
	PCIE_DBG_FS(dev, "PCIe: RC%d: PCIE20_PLR_IATU_LTAR:\t0x%x: 0x%x\n",
		dev->rc_idx, iatu_base_phy + iatu_ltar_offset,
		readl_relaxed(iatu_base_vir + iatu_ltar_offset));

	writel_relaxed(upper_32_bits(loopback_dst_addr), iatu_base_vir + iatu_utar_offset);
	PCIE_DBG_FS(dev, "PCIe: RC%d: PCIE20_PLR_IATU_UTAR:\t0x%x: 0x%x\n",
			dev->rc_idx, iatu_base_phy + iatu_utar_offset,
			readl_relaxed(iatu_base_vir + iatu_utar_offset));

	/* Enable this iATU region */
	writel_relaxed(BIT(31), iatu_base_vir + iatu_ctrl2_offset);
	PCIE_DBG_FS(dev, "PCIe: RC%d: PCIE20_PLR_IATU_CTRL2:\t0x%x: 0x%x\n",
			dev->rc_idx, iatu_base_phy + iatu_ctrl2_offset,
			readl_relaxed(iatu_base_vir + iatu_ctrl2_offset));

	PCIE_DBG_FS(dev, "PCIe RC%d: LTSSM_STATE: %s\n", dev->rc_idx,
		TO_LTSSM_STR((readl_relaxed(dev->elbi + PCIE20_ELBI_SYS_STTS) >> 12) & 0x3f));

	/* Fill the src buffer with random data */
	get_random_bytes(src_vir_addr, SZ_4K);
	usleep_range(100, 101);
	for (i = 0; i < SZ_4K; i++) {
		if (src_vir_addr[i] != loopback_dst_vir[i]) {
			PCIE_DBG_FS(dev, "PCIe: RC%d: exp %x: got %x\n",
				dev->rc_idx, src_vir_addr[i], loopback_dst_vir[i]);
			loopback_test_fail = true;
		}
	}

	if (loopback_test_fail)
		PCIE_DBG_FS(dev, "PCIe: RC%d: %s Loopback Test failed\n",
				dev->rc_idx, local ? "Local" : "Remote");
	else
		PCIE_DBG_FS(dev, "PCIe: RC%d: %s Loopback Test Passed\n",
				dev->rc_idx, local ? "Local" : "Remote");

	iounmap(src_vir_addr);
	dma_free_coherent(&dev->pdev->dev, SZ_4K, loopback_dst_vir, loopback_dst_addr);
}

static void msm_pcie_show_status(struct msm_pcie_dev_t *dev)
{
	PCIE_DBG_FS(dev, "PCIe: RC%d is %s enumerated\n",
		dev->rc_idx, dev->enumerated ? "" : "not");
	PCIE_DBG_FS(dev, "PCIe: link is %s\n",
		(dev->link_status == MSM_PCIE_LINK_ENABLED)
		? "enabled" : "disabled");
	PCIE_DBG_FS(dev, "cfg_access is %s allowed\n",
		dev->cfg_access ? "" : "not");
	PCIE_DBG_FS(dev, "use_pinctrl is %d\n",
		dev->use_pinctrl);
	PCIE_DBG_FS(dev, "aux_clk_freq is %d\n",
		dev->aux_clk_freq);
	PCIE_DBG_FS(dev, "user_suspend is %d\n",
		dev->user_suspend);
	PCIE_DBG_FS(dev, "num_parf_testbus_sel is 0x%x",
		dev->num_parf_testbus_sel);
	PCIE_DBG_FS(dev, "phy_len is %d",
		dev->phy_len);
	PCIE_DBG_FS(dev, "num_pipe_clk: %d\n", dev->num_pipe_clk);
	PCIE_DBG_FS(dev, "num_clk: %d\n", dev->num_clk);
	PCIE_DBG_FS(dev, "disable_pc is %d",
		dev->disable_pc);
	PCIE_DBG_FS(dev, "l0s_supported is %s supported\n",
		dev->l0s_supported ? "" : "not");
	PCIE_DBG_FS(dev, "l1_supported is %s supported\n",
		dev->l1_supported ? "" : "not");
	PCIE_DBG_FS(dev, "l1ss_supported is %s supported\n",
		dev->l1ss_supported ? "" : "not");
	PCIE_DBG_FS(dev, "l1_1_pcipm_supported is %s supported\n",
		dev->l1_1_pcipm_supported ? "" : "not");
	PCIE_DBG_FS(dev, "l1_2_pcipm_supported is %s supported\n",
		dev->l1_2_pcipm_supported ? "" : "not");
	PCIE_DBG_FS(dev, "l1_1_aspm_supported is %s supported\n",
		dev->l1_1_aspm_supported ? "" : "not");
	PCIE_DBG_FS(dev, "l1_2_aspm_supported is %s supported\n",
		dev->l1_2_aspm_supported ? "" : "not");
	PCIE_DBG_FS(dev, "l1_2_th_scale is %d\n",
		dev->l1_2_th_scale);
	PCIE_DBG_FS(dev, "l1_2_th_value is %d\n",
		dev->l1_2_th_value);
	PCIE_DBG_FS(dev, "common_clk_en is %d\n",
		dev->common_clk_en);
	PCIE_DBG_FS(dev, "clk_power_manage_en is %d\n",
		dev->clk_power_manage_en);
	PCIE_DBG_FS(dev, "aux_clk_sync is %d\n",
		dev->aux_clk_sync);
	PCIE_DBG_FS(dev, "AER is %s enable\n",
		dev->aer_enable ? "" : "not");
	PCIE_DBG_FS(dev, "boot_option is 0x%x\n",
		dev->boot_option);
	PCIE_DBG_FS(dev, "link_speed_override is 0x%x\n",
		dev->link_speed_override);
	PCIE_DBG_FS(dev, "phy_ver is %d\n",
		dev->phy_ver);
	PCIE_DBG_FS(dev, "drv_ready is %d\n",
		dev->drv_ready);
	PCIE_DBG_FS(dev, "linkdown_panic is %d\n",
		dev->linkdown_panic);
	PCIE_DBG_FS(dev, "the link is %s suspending\n",
		dev->suspending ? "" : "not");
	PCIE_DBG_FS(dev, "the power of RC is %s on\n",
		dev->power_on ? "" : "not");
	PCIE_DBG_FS(dev, "smmu_sid_base: 0x%x\n",
		dev->smmu_sid_base);
	PCIE_DBG_FS(dev, "n_fts: %d\n",
		dev->n_fts);
	PCIE_DBG_FS(dev, "ep_latency: %dms\n",
		dev->ep_latency);
	PCIE_DBG_FS(dev, "switch_latency: %dms\n",
		dev->switch_latency);
	PCIE_DBG_FS(dev, "wr_halt_size: 0x%x\n",
		dev->wr_halt_size);
	PCIE_DBG_FS(dev, "slv_addr_space_size: 0x%x\n",
		dev->slv_addr_space_size);
	PCIE_DBG_FS(dev, "PCIe: bdf_halt_dis is %d\n",
		dev->pcie_bdf_halt_dis);
	PCIE_DBG_FS(dev, "PCIe: halt_feature_dis is %d\n",
		dev->pcie_halt_feature_dis);
	PCIE_DBG_FS(dev, "phy_status_offset: 0x%x\n",
		dev->phy_status_offset);
	PCIE_DBG_FS(dev, "phy_status_bit: %u\n",
		dev->phy_status_bit);
	PCIE_DBG_FS(dev, "phy_power_down_offset: 0x%x\n",
		dev->phy_power_down_offset);
	PCIE_DBG_FS(dev, "eq_pset_req_vec: 0x%x\n",
		dev->eq_pset_req_vec);
	PCIE_DBG_FS(dev, "core_preset: 0x%x\n",
		dev->core_preset);
	PCIE_DBG_FS(dev, "eq_fmdc_t_min_phase23: 0x%x\n",
		dev->eq_fmdc_t_min_phase23);
	PCIE_DBG_FS(dev, "cpl_timeout: 0x%x\n",
		dev->cpl_timeout);
	PCIE_DBG_FS(dev, "current_bdf: 0x%x\n",
		dev->current_bdf);
	PCIE_DBG_FS(dev, "perst_delay_us_min: %dus\n",
		dev->perst_delay_us_min);
	PCIE_DBG_FS(dev, "perst_delay_us_max: %dus\n",
		dev->perst_delay_us_max);
	PCIE_DBG_FS(dev, "tlp_rd_size: 0x%x\n",
		dev->tlp_rd_size);
	PCIE_DBG_FS(dev, "linkdown_counter: %lu\n",
		dev->linkdown_counter);
	PCIE_DBG_FS(dev, "wake_counter: %lu\n",
		dev->wake_counter);
	PCIE_DBG_FS(dev, "link_check_max_count: %u\n",
		dev->link_check_max_count);
	PCIE_DBG_FS(dev, "prevent_l1: %d\n",
		dev->prevent_l1);
	PCIE_DBG_FS(dev, "target_link_speed: 0x%x\n",
		dev->target_link_speed);
	PCIE_DBG_FS(dev, "current_link_speed: 0x%x\n",
		dev->current_link_speed);
	PCIE_DBG_FS(dev, "target_link_width: %d\n",
		dev->target_link_width);
	PCIE_DBG_FS(dev, "current_link_width: %d\n",
		dev->current_link_width);
	PCIE_DBG_FS(dev, "link_width_max: %d\n",
		dev->link_width_max);
	PCIE_DBG_FS(dev, "link_turned_on_counter: %lu\n",
		dev->link_turned_on_counter);
	PCIE_DBG_FS(dev, "link_turned_off_counter: %lu\n",
		dev->link_turned_off_counter);
	PCIE_DBG_FS(dev, "l23_rdy_poll_timeout: %llu\n",
		dev->l23_rdy_poll_timeout);
	PCIE_DBG_FS(dev, "PCIe CESTA is %s\n",
		dev->pcie_sm ? "supported" : "not_supported");
}

static void msm_pcie_access_reg(struct msm_pcie_dev_t *dev, bool wr)
{
	u32 base_sel_size = 0;
	phys_addr_t wr_register;

	PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: %s a PCIe register\n\n", dev->rc_idx,
		    wr ? "writing" : "reading");

	if (!base_sel) {
		PCIE_DBG_FS(dev, "Invalid base_sel: 0x%x\n", base_sel);
		return;
	}

	if (((base_sel - 1) >= MSM_PCIE_MAX_RES) ||
				(!dev->res[base_sel - 1].resource)) {
		PCIE_DBG_FS(dev, "PCIe: RC%d Resource does not exist\n",
							dev->rc_idx);
		return;
	}

	PCIE_DBG_FS(dev, "base: %s: 0x%pK\nwr_offset: 0x%x\n",
		    dev->res[base_sel - 1].name, dev->res[base_sel - 1].base,
		    wr_offset);

	base_sel_size = resource_size(dev->res[base_sel - 1].resource);

	if (wr_offset >  base_sel_size - 4 ||
		msm_pcie_check_align(dev, wr_offset)) {
		PCIE_DBG_FS(dev,
			"PCIe: RC%d: Invalid wr_offset: 0x%x. wr_offset should be no more than 0x%x\n",
			dev->rc_idx, wr_offset, base_sel_size - 4);
	} else {
		if (!wr) {
			wr_register =
				dev->res[MSM_PCIE_RES_DM_CORE].resource->start;
			wr_register += wr_offset;
			PCIE_DBG_FS(dev,
				"PCIe: RC%d: register: 0x%pa value: 0x%x\n",
				dev->rc_idx, &wr_register,
				readl_relaxed(dev->res[base_sel - 1].base +
				wr_offset));
			return;
		}

		msm_pcie_write_reg_field(dev->res[base_sel - 1].base,
			wr_offset, wr_mask, wr_value);
	}
}

static void msm_pcie_sel_debug_testcase(struct msm_pcie_dev_t *dev,
					u32 testcase)
{
	int ret, i;
	u32 base_sel_size = 0;

	switch (testcase) {
	case MSM_PCIE_OUTPUT_PCIE_INFO:
		PCIE_DBG_FS(dev, "\n\nPCIe: Status for RC%d:\n",
			dev->rc_idx);
		msm_pcie_show_status(dev);
		break;
	case MSM_PCIE_DISABLE_LINK:
		PCIE_DBG_FS(dev,
			"\n\nPCIe: RC%d: disable link\n\n", dev->rc_idx);
		ret = msm_pcie_pm_control(MSM_PCIE_SUSPEND, 0, dev->dev, NULL,
					   MSM_PCIE_CONFIG_FORCE_SUSP);
		if (ret)
			PCIE_DBG_FS(dev, "PCIe:%s:failed to disable link\n",
				__func__);
		else
			PCIE_DBG_FS(dev, "PCIe:%s:disabled link\n",
				__func__);
		break;
	case MSM_PCIE_ENABLE_LINK:
		PCIE_DBG_FS(dev,
			"\n\nPCIe: RC%d: enable link and recover config space\n\n",
			dev->rc_idx);
		ret = msm_pcie_pm_control(MSM_PCIE_RESUME, 0, dev->dev, NULL,
					  0);
		if (ret)
			PCIE_DBG_FS(dev, "PCIe:%s:failed to enable link\n",
				__func__);
		break;
	case MSM_PCIE_DISABLE_ENABLE_LINK:
		PCIE_DBG_FS(dev,
			"\n\nPCIe: RC%d: disable and enable link then recover config space\n\n",
			dev->rc_idx);
		ret = msm_pcie_pm_control(MSM_PCIE_SUSPEND, 0, dev->dev, NULL,
					  0);
		if (ret)
			PCIE_DBG_FS(dev, "PCIe:%s:failed to disable link\n",
				__func__);
		else
			PCIE_DBG_FS(dev, "PCIe:%s:disabled link\n", __func__);
		ret = msm_pcie_pm_control(MSM_PCIE_RESUME, 0, dev->dev, NULL,
					  0);
		if (ret)
			PCIE_DBG_FS(dev, "PCIe:%s:failed to enable link\n",
				__func__);
		break;
	case MSM_PCIE_DISABLE_L0S:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: disable L0s\n\n",
			dev->rc_idx);
		if (dev->link_status == MSM_PCIE_LINK_ENABLED)
			msm_pcie_config_l0s_disable_all(dev, dev->dev->bus);
		dev->l0s_supported = false;
		break;
	case MSM_PCIE_ENABLE_L0S:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: enable L0s\n\n",
			dev->rc_idx);
		dev->l0s_supported = true;
		if (dev->link_status == MSM_PCIE_LINK_ENABLED)
			msm_pcie_config_l0s_enable_all(dev);
		break;
	case MSM_PCIE_DISABLE_L1:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: disable L1\n\n",
			dev->rc_idx);

		mutex_lock(&dev->aspm_lock);
		if (dev->link_status == MSM_PCIE_LINK_ENABLED)
			msm_pcie_config_l1_disable_all(dev, dev->dev->bus);
		dev->l1_supported = false;
		dev->debugfs_l1 = true;
		mutex_unlock(&dev->aspm_lock);

		break;
	case MSM_PCIE_ENABLE_L1:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: enable L1\n\n",
			dev->rc_idx);

		mutex_lock(&dev->aspm_lock);
		dev->l1_supported = true;
		dev->debugfs_l1 = true;
		if (dev->link_status == MSM_PCIE_LINK_ENABLED) {
			/* enable l1 mode, clear bit 5 (REQ_NOT_ENTR_L1) */
			msm_pcie_write_mask(dev->parf +
				PCIE20_PARF_PM_CTRL, BIT(5), 0);

			msm_pcie_config_l1_enable_all(dev);
		}
		mutex_unlock(&dev->aspm_lock);

		break;
	case MSM_PCIE_DISABLE_L1SS:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: disable L1ss\n\n",
			dev->rc_idx);
		if (dev->link_status == MSM_PCIE_LINK_ENABLED)
			msm_pcie_config_l1ss_disable_all(dev, dev->dev->bus);
		dev->l1ss_supported = false;
		dev->l1_1_pcipm_supported = false;
		dev->l1_2_pcipm_supported = false;
		dev->l1_1_aspm_supported = false;
		dev->l1_2_aspm_supported = false;
		break;
	case MSM_PCIE_ENABLE_L1SS:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: enable L1ss\n\n",
			dev->rc_idx);
		dev->l1ss_supported = true;
		dev->l1_1_pcipm_supported = true;
		dev->l1_2_pcipm_supported = true;
		dev->l1_1_aspm_supported = true;
		dev->l1_2_aspm_supported = true;
		if (dev->link_status == MSM_PCIE_LINK_ENABLED) {
			msm_pcie_check_l1ss_support_all(dev);
			msm_pcie_config_l1ss_enable_all(dev);
		}
		break;
	case MSM_PCIE_ENUMERATION:
		PCIE_DBG_FS(dev, "\n\nPCIe: attempting to enumerate RC%d\n\n",
			dev->rc_idx);
		if (dev->enumerated)
			PCIE_DBG_FS(dev, "PCIe: RC%d is already enumerated\n",
				dev->rc_idx);
		else {
			if (!msm_pcie_enumerate(dev->rc_idx))
				PCIE_DBG_FS(dev,
					"PCIe: RC%d is successfully enumerated\n",
					dev->rc_idx);
			else
				PCIE_DBG_FS(dev,
					"PCIe: RC%d enumeration failed\n",
					dev->rc_idx);
		}
		break;
	case MSM_PCIE_DEENUMERATION:
		PCIE_DBG_FS(dev, "\n\nPCIe: attempting to de enumerate RC%d\n\n",
			dev->rc_idx);
		if (!dev->enumerated)
			PCIE_DBG_FS(dev, "PCIe: RC%d is already de enumerated\n",
				dev->rc_idx);
		else {
			if (!msm_pcie_deenumerate(dev->rc_idx))
				PCIE_DBG_FS(dev,
					"PCIe: RC%d is successfully de enumerated\n",
					dev->rc_idx);
			else
				PCIE_DBG_FS(dev,
					"PCIe: RC%d de enumeration failed\n",
					dev->rc_idx);
		}
		break;
	case MSM_PCIE_READ_PCIE_REGISTER:
		msm_pcie_access_reg(dev, false);
		break;
	case MSM_PCIE_WRITE_PCIE_REGISTER:
		msm_pcie_access_reg(dev, true);
		break;
	case MSM_PCIE_DUMP_PCIE_REGISTER_SPACE:
		if (((base_sel - 1) >= MSM_PCIE_MAX_RES) ||
					(!dev->res[base_sel - 1].resource)) {
			PCIE_DBG_FS(dev, "PCIe: RC%d Resource does not exist\n",
								dev->rc_idx);
			break;
		}

		if (!base_sel) {
			PCIE_DBG_FS(dev, "Invalid base_sel: 0x%x\n", base_sel);
			break;
		} else if (base_sel - 1 == MSM_PCIE_RES_PARF) {
			pcie_parf_dump(dev);
			break;
		} else if (base_sel - 1 == MSM_PCIE_RES_PHY) {
			pcie_phy_dump(dev);
			break;
		} else if (base_sel - 1 == MSM_PCIE_RES_CONF) {
			base_sel_size = 0x1000;
		} else {
			base_sel_size = resource_size(
				dev->res[base_sel - 1].resource);
		}

		PCIE_DBG_FS(dev, "\n\nPCIe: Dumping %s Registers for RC%d\n\n",
			dev->res[base_sel - 1].name, dev->rc_idx);

		for (i = 0; i < base_sel_size; i += 32) {
			PCIE_DBG_FS(dev,
			"0x%04x %08x %08x %08x %08x %08x %08x %08x %08x\n",
			i, readl_relaxed(dev->res[base_sel - 1].base + i),
			readl_relaxed(dev->res[base_sel - 1].base + (i + 4)),
			readl_relaxed(dev->res[base_sel - 1].base + (i + 8)),
			readl_relaxed(dev->res[base_sel - 1].base + (i + 12)),
			readl_relaxed(dev->res[base_sel - 1].base + (i + 16)),
			readl_relaxed(dev->res[base_sel - 1].base + (i + 20)),
			readl_relaxed(dev->res[base_sel - 1].base + (i + 24)),
			readl_relaxed(dev->res[base_sel - 1].base + (i + 28)));
		}
		break;
	case MSM_PCIE_DISABLE_AER:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: clear AER enable flag\n\n",
			dev->rc_idx);
		dev->aer_enable = false;
		break;
	case MSM_PCIE_ENABLE_AER:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: set AER enable flag\n\n",
			dev->rc_idx);
		dev->aer_enable = true;
		break;
	case MSM_PCIE_GPIO_STATUS:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: PERST and WAKE status\n\n",
			dev->rc_idx);
		PCIE_DBG_FS(dev,
			"PCIe: RC%d: PERST: gpio%u value: %d\n",
			dev->rc_idx, dev->gpio[MSM_PCIE_GPIO_PERST].num,
			gpio_get_value(dev->gpio[MSM_PCIE_GPIO_PERST].num));
		PCIE_DBG_FS(dev,
			"PCIe: RC%d: WAKE: gpio%u value: %d\n",
			dev->rc_idx, dev->gpio[MSM_PCIE_GPIO_WAKE].num,
			gpio_get_value(dev->gpio[MSM_PCIE_GPIO_WAKE].num));
		break;
	case MSM_PCIE_ASSERT_PERST:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: assert PERST\n\n",
			dev->rc_idx);
		msm_pcie_config_perst(dev, true);
		usleep_range(dev->perst_delay_us_min, dev->perst_delay_us_max);
		break;
	case MSM_PCIE_DEASSERT_PERST:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: de-assert PERST\n\n",
			dev->rc_idx);
		msm_pcie_config_perst(dev, false);
		usleep_range(dev->perst_delay_us_min, dev->perst_delay_us_max);
		break;
	case MSM_PCIE_KEEP_RESOURCES_ON:
		PCIE_DBG_FS(dev,
			"\n\nPCIe: RC%d: set keep resources on flag\n\n",
			dev->rc_idx);
		msm_pcie_keep_resources_on |= BIT(dev->rc_idx);
		break;
	case MSM_PCIE_FORCE_GEN1:
		PCIE_DBG_FS(dev,
			"\n\nPCIe: RC%d: set target speed to Gen 1\n\n",
			dev->rc_idx);
		dev->target_link_speed = GEN1_SPEED;
		break;
	case MSM_PCIE_FORCE_GEN2:
		PCIE_DBG_FS(dev,
			"\n\nPCIe: RC%d: set target speed to Gen 2\n\n",
			dev->rc_idx);
		dev->target_link_speed = GEN2_SPEED;
		break;
	case MSM_PCIE_FORCE_GEN3:
		PCIE_DBG_FS(dev,
			"\n\nPCIe: RC%d: set target speed to Gen 3\n\n",
			dev->rc_idx);
		dev->target_link_speed = GEN3_SPEED;
		break;
	case MSM_PCIE_TRIGGER_SBR:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: Trigger SBR\n\n",
			dev->rc_idx);
		if (dev->link_status == MSM_PCIE_LINK_ENABLED) {
			msm_pcie_write_mask(dev->dm_core + PCIE20_BRIDGE_CTRL,
					    0, PCIE20_BRIDGE_CTRL_SBR);
			usleep_range(2000, 2001);
			msm_pcie_write_mask(dev->dm_core + PCIE20_BRIDGE_CTRL,
					    PCIE20_BRIDGE_CTRL_SBR, 0);
		}
		break;
	case MSM_PCIE_REMOTE_LOOPBACK:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: Move to remote loopback mode\n\n",
			dev->rc_idx);
		if (!dev->enumerated) {
			PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: the link is not up yet\n\n",
				dev->rc_idx);
			break;
		}

		/* link needs to be in L0 for remote loopback */
		msm_pcie_config_l0s_disable_all(dev, dev->dev->bus);
		dev->l0s_supported = false;

		mutex_lock(&dev->aspm_lock);
		msm_pcie_config_l1_disable_all(dev, dev->dev->bus);
		dev->l1_supported = false;
		dev->debugfs_l1 = true;
		mutex_unlock(&dev->aspm_lock);

		msm_pcie_loopback(dev, false);
		break;
	case MSM_PCIE_LOCAL_LOOPBACK:
		PCIE_DBG_FS(dev, "\n\nPCIe: RC%d: Move to local loopback mode\n\n", dev->rc_idx);
		if (dev->enumerated) {
			/* As endpoint is already connected use remote loopback */
			PCIE_DBG_FS(dev,
				"\n\nPCIe: RC%d: EP is already enumerated, use remote loopback mode\n\n",
						dev->rc_idx);
			break;
		}
		/* keep resources on because we will fail to enable as ep is not connected */
		msm_pcie_keep_resources_on |= BIT(dev->rc_idx);

		/* Enable all the PCIe resources */
		if (!dev->enumerated)
			msm_pcie_enable(dev);

		msm_pcie_loopback(dev, true);
		break;
	default:
		PCIE_DBG_FS(dev, "Invalid testcase: %d.\n", testcase);
		break;
	}
}

int msm_pcie_debug_info(struct pci_dev *dev, u32 option, u32 base,
			u32 offset, u32 mask, u32 value)
{
	int ret = 0;
	struct msm_pcie_dev_t *pdev = NULL;

	if (!dev) {
		pr_err("PCIe: the input pci dev is NULL.\n");
		return -ENODEV;
	}

	if (option == MSM_PCIE_READ_PCIE_REGISTER ||
		option == MSM_PCIE_WRITE_PCIE_REGISTER ||
		option == MSM_PCIE_DUMP_PCIE_REGISTER_SPACE) {
		if (!base || base >= MSM_PCIE_MAX_RES) {
			PCIE_DBG_FS(pdev, "Invalid base_sel: 0x%x\n", base);
			PCIE_DBG_FS(pdev,
				"PCIe: base_sel is still 0x%x\n", base_sel);
			return -EINVAL;
		}

		base_sel = base;
		PCIE_DBG_FS(pdev, "PCIe: base_sel is now 0x%x\n", base_sel);

		if (option == MSM_PCIE_READ_PCIE_REGISTER ||
			option == MSM_PCIE_WRITE_PCIE_REGISTER) {
			wr_offset = offset;
			wr_mask = mask;
			wr_value = value;

			PCIE_DBG_FS(pdev,
				"PCIe: wr_offset is now 0x%x\n", wr_offset);
			PCIE_DBG_FS(pdev,
				"PCIe: wr_mask is now 0x%x\n", wr_mask);
			PCIE_DBG_FS(pdev,
				"PCIe: wr_value is now 0x%x\n", wr_value);
		}
	}

	pdev = PCIE_BUS_PRIV_DATA(dev->bus);
	rc_sel = BIT(pdev->rc_idx);

	msm_pcie_sel_debug_testcase(pdev, option);

	return ret;
}
EXPORT_SYMBOL(msm_pcie_debug_info);

#ifdef CONFIG_SYSFS
static ssize_t link_check_max_count_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)
						dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pcie_dev->link_check_max_count);
}

static ssize_t link_check_max_count_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)
						dev_get_drvdata(dev);
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	pcie_dev->link_check_max_count = val;

	return count;
}
static DEVICE_ATTR_RW(link_check_max_count);

static ssize_t enumerate_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)
						dev_get_drvdata(dev);

	if (pcie_dev)
		msm_pcie_enumerate(pcie_dev->rc_idx);

	return count;
}
static DEVICE_ATTR_WO(enumerate);

static ssize_t aspm_stat_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	if (!pcie_dev->mhi)
		return scnprintf(buf, PAGE_SIZE,
				"PCIe: RC%d: No dev or MHI space found\n",
				pcie_dev->rc_idx);

	mutex_lock(&pcie_dev->aspm_lock);
	if (pcie_dev->link_status != MSM_PCIE_LINK_ENABLED) {

		mutex_unlock(&pcie_dev->aspm_lock);
		return scnprintf(buf, PAGE_SIZE,
				"PCIe: RC%d: registers are not accessible\n",
				pcie_dev->rc_idx);
	}

	mutex_unlock(&pcie_dev->aspm_lock);
	return scnprintf(buf, PAGE_SIZE,
			"PCIe: RC%d: L0s: %u L1: %u L1.1: %u L1.2: %u\n",
			pcie_dev->rc_idx,
			readl_relaxed(pcie_dev->mhi +
				PCIE20_PARF_DEBUG_CNT_IN_L0S),
			readl_relaxed(pcie_dev->mhi +
				PCIE20_PARF_DEBUG_CNT_IN_L1),
			readl_relaxed(pcie_dev->mhi +
				PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L1),
			readl_relaxed(pcie_dev->mhi +
				PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L2));
}
static DEVICE_ATTR_RO(aspm_stat);

static ssize_t l23_rdy_poll_timeout_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)
						dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			pcie_dev->l23_rdy_poll_timeout);
}

static ssize_t l23_rdy_poll_timeout_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)
						dev_get_drvdata(dev);
	u64 val;

	if (kstrtou64(buf, 0, &val))
		return -EINVAL;

	pcie_dev->l23_rdy_poll_timeout = val;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: L23_Ready poll timeout: %llu\n",
		pcie_dev->rc_idx, pcie_dev->l23_rdy_poll_timeout);

	return count;
}
static DEVICE_ATTR_RW(l23_rdy_poll_timeout);

static ssize_t boot_option_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%x\n", pcie_dev->boot_option);
}

static ssize_t boot_option_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	u32 boot_option;
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	if (kstrtou32(buf, 0, &boot_option))
		return -EINVAL;

	if (boot_option <= (BIT(0) | BIT(1))) {
		pcie_dev->boot_option = boot_option;
		PCIE_DBG(pcie_dev, "PCIe: RC%d: boot_option is now 0x%x\n",
			 pcie_dev->rc_idx, pcie_dev->boot_option);
	} else {
		pr_err("PCIe: Invalid input for boot_option: 0x%x.\n",
		       boot_option);
	}

	return count;
}
static DEVICE_ATTR_RW(boot_option);

static ssize_t panic_on_aer_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%x\n", pcie_dev->panic_on_aer);
}

static ssize_t panic_on_aer_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	u32 panic_on_aer;
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	if (kstrtou32(buf, 0, &panic_on_aer))
		return -EINVAL;

	pcie_dev->panic_on_aer = panic_on_aer;

	return count;
}
static DEVICE_ATTR_RW(panic_on_aer);

static ssize_t link_speed_override_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE,
			 "PCIe: RC%d: link speed override is set to: 0x%x\n",
			 pcie_dev->rc_idx, pcie_dev->link_speed_override);
}

static ssize_t link_speed_override_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	u32 link_speed_override;
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);
	int ret;

	if (kstrtou32(buf, 0, &link_speed_override))
		return -EINVAL;

	/* Set target PCIe link speed as maximum device/link is capable of */
	ret = msm_pcie_set_target_link_speed(pcie_dev->rc_idx,
					     link_speed_override, true);
	if (ret) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: Failed to override link speed: %d. %d\n",
			 pcie_dev->rc_idx, link_speed_override, ret);
	} else {
		pcie_dev->link_speed_override = link_speed_override;
		PCIE_DBG(pcie_dev, "PCIe: RC%d: link speed override set to: %d\n",
			 pcie_dev->rc_idx, link_speed_override);
	}

	return count;
}
static DEVICE_ATTR_RW(link_speed_override);

static ssize_t sbr_link_recovery_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE,
			 "PCIe: RC%d: sbr_link_recovery is set to: 0x%x\n",
			 pcie_dev->rc_idx, pcie_dev->linkdown_recovery_enable);
}

static ssize_t sbr_link_recovery_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	u32 linkdown_recovery_enable;
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);

	if (kstrtou32(buf, 0, &linkdown_recovery_enable))
		return -EINVAL;

	if (pcie_dev->linkdown_reset[0].hdl && pcie_dev->linkdown_reset[1].hdl)
		pcie_dev->linkdown_recovery_enable = linkdown_recovery_enable;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: sbr_link_recovery is set to: %d\n",
		 pcie_dev->rc_idx, linkdown_recovery_enable);

	return count;
}
static DEVICE_ATTR_RW(sbr_link_recovery);

static struct attribute *msm_pcie_debug_attrs[] = {
	&dev_attr_link_check_max_count.attr,
	&dev_attr_enumerate.attr,
	&dev_attr_aspm_stat.attr,
	&dev_attr_l23_rdy_poll_timeout.attr,
	&dev_attr_boot_option.attr,
	&dev_attr_panic_on_aer.attr,
	&dev_attr_link_speed_override.attr,
	&dev_attr_sbr_link_recovery.attr,
	NULL,
};

static const struct attribute_group msm_pcie_debug_attr_group = {
	.name	= "debug",
	.attrs	= msm_pcie_debug_attrs,
};

/* AER sysfs entries */
#define aer_stats_dev_attr(name, stats_array, strings_array,		\
			   total_string, total_field)			\
	static ssize_t							\
	name##_show(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	unsigned int i;							\
	u64 *stats;							\
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);		\
	size_t len = 0;							\
									\
	if (!pcie_dev->aer_stats)					\
		return -ENODEV;						\
									\
	stats = pcie_dev->aer_stats->stats_array;			\
									\
	for (i = 0; i < ARRAY_SIZE(pcie_dev->aer_stats->stats_array); i++) {\
		if (strings_array[i])					\
			len += sysfs_emit_at(buf, len, "%s %llu\n",	\
					     strings_array[i],		\
					     stats[i]);			\
		else if (stats[i])					\
			len += sysfs_emit_at(buf, len,			\
					     #stats_array "_bit[%d] %llu\n",\
					     i, stats[i]);		\
	}								\
	len += sysfs_emit_at(buf, len, "TOTAL_%s %llu\n", total_string,	\
			     pcie_dev->aer_stats->total_field);		\
	return len;							\
}									\
static DEVICE_ATTR_RO(name)

aer_stats_dev_attr(aer_dev_correctable, dev_cor_errs,
		   aer_correctable_error_string, "ERR_COR",
		   dev_total_cor_errs);
aer_stats_dev_attr(aer_dev_fatal, dev_fatal_errs,
		   aer_uncorrectable_error_string, "ERR_FATAL",
		   dev_total_fatal_errs);
aer_stats_dev_attr(aer_dev_nonfatal, dev_nonfatal_errs,
		   aer_uncorrectable_error_string, "ERR_NONFATAL",
		   dev_total_nonfatal_errs);

#define aer_stats_rootport_attr(name, field)				\
	static ssize_t							\
	name##_show(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	struct msm_pcie_dev_t *pcie_dev = dev_get_drvdata(dev);		\
									\
	if (!pcie_dev->aer_stats)					\
		return -ENODEV;						\
									\
	return sysfs_emit(buf, "%llu\n", pcie_dev->aer_stats->field);	\
}									\
static DEVICE_ATTR_RO(name)

aer_stats_rootport_attr(aer_rootport_total_err_cor,
			 rootport_total_cor_errs);
aer_stats_rootport_attr(aer_rootport_total_err_fatal,
			 rootport_total_fatal_errs);
aer_stats_rootport_attr(aer_rootport_total_err_nonfatal,
			 rootport_total_nonfatal_errs);

static struct attribute *msm_aer_stats_attrs[] __ro_after_init = {
	&dev_attr_aer_dev_correctable.attr,
	&dev_attr_aer_dev_fatal.attr,
	&dev_attr_aer_dev_nonfatal.attr,
	&dev_attr_aer_rootport_total_err_cor.attr,
	&dev_attr_aer_rootport_total_err_fatal.attr,
	&dev_attr_aer_rootport_total_err_nonfatal.attr,
	NULL
};

static const struct attribute_group msm_aer_stats_attr_group = {
	.name = "aer_stats",
	.attrs  = msm_aer_stats_attrs,
};

static void msm_pcie_sysfs_init(struct msm_pcie_dev_t *dev)
{
	int ret;

	ret = sysfs_create_group(&dev->pdev->dev.kobj,
					&msm_pcie_debug_attr_group);
	if (ret)
		PCIE_DBG_FS(dev,
			"RC%d: failed to create sysfs debug group\n",
			dev->rc_idx);

	ret = sysfs_create_group(&dev->pdev->dev.kobj,
					&msm_aer_stats_attr_group);
	if (ret)
		PCIE_DBG_FS(dev,
			"RC%d: failed to create sysfs debug group\n",
			dev->rc_idx);
}

static void msm_pcie_sysfs_exit(struct msm_pcie_dev_t *dev)
{
	if (dev->pdev) {
		sysfs_remove_group(&dev->pdev->dev.kobj,
					&msm_pcie_debug_attr_group);
		sysfs_remove_group(&dev->pdev->dev.kobj,
					&msm_aer_stats_attr_group);
	}
}
#else
static void msm_pcie_sysfs_init(struct msm_pcie_dev_t *dev)
{
}

static void msm_pcie_sysfs_exit(struct msm_pcie_dev_t *dev)
{
}
#endif

#ifdef CONFIG_DEBUG_FS
static struct dentry *dent_msm_pcie;
static struct dentry *dfile_rc_sel;
static struct dentry *dfile_case;
static struct dentry *dfile_base_sel;
static struct dentry *dfile_linkdown_panic;
static struct dentry *dfile_wr_offset;
static struct dentry *dfile_wr_mask;
static struct dentry *dfile_wr_value;
static struct dentry *dfile_boot_option;
static struct dentry *dfile_aer_enable;
static struct dentry *dfile_corr_counter_limit;

static u32 rc_sel_max;

static int msm_pcie_debugfs_parse_input(const char __user *buf,
					size_t count, unsigned int *data)
{
	unsigned long ret;
	char *str, *str_temp;

	str = kmalloc(count + 1, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	ret = copy_from_user(str, buf, count);
	if (ret) {
		kfree(str);
		return -EFAULT;
	}

	str[count] = 0;
	str_temp = str;

	ret = get_option(&str_temp, data);
	kfree(str);
	if (ret != 1)
		return -EINVAL;

	return 0;
}

static int msm_pcie_debugfs_case_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < MSM_PCIE_MAX_DEBUGFS_OPTION; i++)
		seq_printf(m, "\t%d:\t %s\n", i,
			msm_pcie_debugfs_option_desc[i]);

	return 0;
}

static int msm_pcie_debugfs_case_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_pcie_debugfs_case_show, NULL);
}

static ssize_t msm_pcie_debugfs_case_select(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int i, ret;
	unsigned int testcase = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &testcase);
	if (ret)
		return ret;

	pr_alert("PCIe: TEST: %d\n", testcase);

	for (i = 0; i < MAX_RC_NUM; i++) {
		if (rc_sel & BIT(i))
			msm_pcie_sel_debug_testcase(&msm_pcie_dev[i], testcase);
	}

	return count;
}

static const struct file_operations msm_pcie_debugfs_case_ops = {
	.open = msm_pcie_debugfs_case_open,
	.release = single_release,
	.read = seq_read,
	.write = msm_pcie_debugfs_case_select,
};

static int msm_pcie_debugfs_rc_select_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "Current rc_sel: %d which selects:\n", rc_sel);

	for (i = 0; i < MAX_RC_NUM; i++)
		if (rc_sel & BIT(i))
			seq_printf(m, "\tPCIe%d\n", i);

	return 0;
}

static int msm_pcie_debugfs_rc_select_open(struct inode *inode,
						struct file *file)
{
	return single_open(file, msm_pcie_debugfs_rc_select_show, NULL);
}

static ssize_t msm_pcie_debugfs_rc_select(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int i, ret;
	u32 new_rc_sel = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &new_rc_sel);
	if (ret)
		return ret;

	if ((!new_rc_sel) || (new_rc_sel > rc_sel_max)) {
		pr_alert("PCIe: invalid value for rc_sel: 0x%x\n", new_rc_sel);
		pr_alert("PCIe: rc_sel is still 0x%x\n", rc_sel ? rc_sel : 0x1);
	} else {
		rc_sel = new_rc_sel;
		pr_alert("PCIe: rc_sel is now: 0x%x\n", rc_sel);
	}

	pr_alert("PCIe: the following RC(s) will be tested:\n");
	for (i = 0; i < MAX_RC_NUM; i++)
		if (rc_sel & BIT(i))
			pr_alert("RC %d\n", i);

	return count;
}

static const struct file_operations msm_pcie_debugfs_rc_select_ops = {
	.open = msm_pcie_debugfs_rc_select_open,
	.release = single_release,
	.read = seq_read,
	.write = msm_pcie_debugfs_rc_select,
};

static int msm_pcie_debugfs_base_select_show(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "Options:\n");
	for (i = 0; i < MSM_PCIE_MAX_RES; i++)
		seq_printf(m, "\t%d: %s\n", i + 1, msm_pcie_res_info[i].name);

	seq_printf(m, "\nCurrent base_sel: %d: %s\n", base_sel, base_sel ?
			msm_pcie_res_info[base_sel - 1].name : "None");

	return 0;
}

static int msm_pcie_debugfs_base_select_open(struct inode *inode,
						struct file *file)
{
	return single_open(file, msm_pcie_debugfs_base_select_show, NULL);
}

static ssize_t msm_pcie_debugfs_base_select(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int ret;
	u32 new_base_sel = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &new_base_sel);
	if (ret)
		return ret;

	if (!new_base_sel || new_base_sel > MSM_PCIE_MAX_RES) {
		pr_alert("PCIe: invalid value for base_sel: 0x%x\n",
			new_base_sel);
		pr_alert("PCIe: base_sel is still 0x%x\n", base_sel);
	} else {
		base_sel = new_base_sel;
		pr_alert("PCIe: base_sel is now 0x%x\n", base_sel);
		pr_alert("%s\n", msm_pcie_res_info[base_sel - 1].name);
	}

	return count;
}

static const struct file_operations msm_pcie_debugfs_base_select_ops = {
	.open = msm_pcie_debugfs_base_select_open,
	.release = single_release,
	.read = seq_read,
	.write = msm_pcie_debugfs_base_select,
};

static ssize_t msm_pcie_debugfs_linkdown_panic(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int i, ret;
	u32 new_linkdown_panic = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &new_linkdown_panic);
	if (ret)
		return ret;

	new_linkdown_panic = !!new_linkdown_panic;

	for (i = 0; i < MAX_RC_NUM; i++) {
		if (rc_sel & BIT(i)) {
			msm_pcie_dev[i].linkdown_panic =
				new_linkdown_panic;
			PCIE_DBG_FS(&msm_pcie_dev[i],
				"PCIe: RC%d: linkdown_panic is now %d\n",
				i, msm_pcie_dev[i].linkdown_panic);
		}
	}

	return count;
}

static const struct file_operations msm_pcie_debugfs_linkdown_panic_ops = {
	.write = msm_pcie_debugfs_linkdown_panic,
};

static int msm_pcie_debugfs_wr_offset_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x\n", wr_offset);

	return 0;
}

static int msm_pcie_debugfs_wr_offset_open(struct inode *inode,
						struct file *file)
{
	return single_open(file, msm_pcie_debugfs_wr_offset_show, NULL);
}

static ssize_t msm_pcie_debugfs_wr_offset(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int ret;

	wr_offset = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &wr_offset);
	if (ret)
		return ret;

	pr_alert("PCIe: wr_offset is now 0x%x\n", wr_offset);

	return count;
}

static const struct file_operations msm_pcie_debugfs_wr_offset_ops = {
	.open = msm_pcie_debugfs_wr_offset_open,
	.release = single_release,
	.read = seq_read,
	.write = msm_pcie_debugfs_wr_offset,
};

static int msm_pcie_debugfs_wr_mask_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x\n", wr_mask);

	return 0;
}

static int msm_pcie_debugfs_wr_mask_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_pcie_debugfs_wr_mask_show, NULL);
}

static ssize_t msm_pcie_debugfs_wr_mask(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int ret;

	wr_mask = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &wr_mask);
	if (ret)
		return ret;

	pr_alert("PCIe: wr_mask is now 0x%x\n", wr_mask);

	return count;
}

static const struct file_operations msm_pcie_debugfs_wr_mask_ops = {
	.open = msm_pcie_debugfs_wr_mask_open,
	.release = single_release,
	.read = seq_read,
	.write = msm_pcie_debugfs_wr_mask,
};

static int msm_pcie_debugfs_wr_value_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x\n", wr_value);

	return 0;
}

static int msm_pcie_debugfs_wr_value_open(struct inode *inode,
						struct file *file)
{
	return single_open(file, msm_pcie_debugfs_wr_value_show, NULL);
}

static ssize_t msm_pcie_debugfs_wr_value(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int ret;

	wr_value = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &wr_value);
	if (ret)
		return ret;

	pr_alert("PCIe: wr_value is now 0x%x\n", wr_value);

	return count;
}

static const struct file_operations msm_pcie_debugfs_wr_value_ops = {
	.open = msm_pcie_debugfs_wr_value_open,
	.release = single_release,
	.read = seq_read,
	.write = msm_pcie_debugfs_wr_value,
};

static ssize_t msm_pcie_debugfs_boot_option(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int i, ret;
	u32 new_boot_option = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &new_boot_option);
	if (ret)
		return ret;

	if (new_boot_option <= (BIT(0) | BIT(1))) {
		for (i = 0; i < MAX_RC_NUM; i++) {
			if (rc_sel & BIT(i)) {
				msm_pcie_dev[i].boot_option = new_boot_option;
				PCIE_DBG_FS(&msm_pcie_dev[i],
					"PCIe: RC%d: boot_option is now 0x%x\n",
					i, msm_pcie_dev[i].boot_option);
			}
		}
	} else {
		pr_err("PCIe: Invalid input for boot_option: 0x%x.\n",
			new_boot_option);
	}

	return count;
}

static const struct file_operations msm_pcie_debugfs_boot_option_ops = {
	.write = msm_pcie_debugfs_boot_option,
};

static ssize_t msm_pcie_debugfs_aer_enable(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int i, ret;
	u32 new_aer_enable = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &new_aer_enable);
	if (ret)
		return ret;

	new_aer_enable = !!new_aer_enable;

	for (i = 0; i < MAX_RC_NUM; i++) {
		if (rc_sel & BIT(i)) {
			msm_pcie_dev[i].aer_enable = new_aer_enable;
			PCIE_DBG_FS(&msm_pcie_dev[i],
				"PCIe: RC%d: aer_enable is now %d\n",
				i, msm_pcie_dev[i].aer_enable);

			msm_pcie_write_mask(msm_pcie_dev[i].dm_core +
					PCIE20_BRIDGE_CTRL,
					new_aer_enable ? 0 : BIT(16),
					new_aer_enable ? BIT(16) : 0);

			PCIE_DBG_FS(&msm_pcie_dev[i],
				"RC%d: PCIE20_BRIDGE_CTRL: 0x%x\n", i,
				readl_relaxed(msm_pcie_dev[i].dm_core +
					PCIE20_BRIDGE_CTRL));
		}
	}

	return count;
}

static const struct file_operations msm_pcie_debugfs_aer_enable_ops = {
	.write = msm_pcie_debugfs_aer_enable,
};

static ssize_t msm_pcie_debugfs_corr_counter_limit(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	int ret;

	corr_counter_limit = 0;

	ret = msm_pcie_debugfs_parse_input(buf, count, &corr_counter_limit);
	if (ret)
		return ret;

	pr_info("PCIe: corr_counter_limit is now %u\n", corr_counter_limit);

	return count;
}

static const struct file_operations msm_pcie_debugfs_corr_counter_limit_ops = {
	.write = msm_pcie_debugfs_corr_counter_limit,
};

static void msm_pcie_debugfs_init(void)
{
	rc_sel_max = (0x1 << MAX_RC_NUM) - 1;
	wr_mask = 0xffffffff;

	dent_msm_pcie = debugfs_create_dir("pci-msm", NULL);
	if (IS_ERR(dent_msm_pcie)) {
		pr_err("PCIe: fail to create the folder for debug_fs.\n");
		return;
	}

	dfile_rc_sel = debugfs_create_file("rc_sel", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_rc_select_ops);
	if (!dfile_rc_sel || IS_ERR(dfile_rc_sel)) {
		pr_err("PCIe: fail to create the file for debug_fs rc_sel.\n");
		goto err;
	}

	dfile_case = debugfs_create_file("case", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_case_ops);
	if (!dfile_case || IS_ERR(dfile_case)) {
		pr_err("PCIe: fail to create the file for debug_fs case.\n");
		goto err;
	}

	dfile_base_sel = debugfs_create_file("base_sel", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_base_select_ops);
	if (!dfile_base_sel || IS_ERR(dfile_base_sel)) {
		pr_err("PCIe: fail to create the file for debug_fs base_sel.\n");
		goto err;
	}

	dfile_linkdown_panic = debugfs_create_file("linkdown_panic", 0644,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_linkdown_panic_ops);
	if (!dfile_linkdown_panic || IS_ERR(dfile_linkdown_panic)) {
		pr_err("PCIe: fail to create the file for debug_fs linkdown_panic.\n");
		goto err;
	}

	dfile_wr_offset = debugfs_create_file("wr_offset", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_wr_offset_ops);
	if (!dfile_wr_offset || IS_ERR(dfile_wr_offset)) {
		pr_err("PCIe: fail to create the file for debug_fs wr_offset.\n");
		goto err;
	}

	dfile_wr_mask = debugfs_create_file("wr_mask", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_wr_mask_ops);
	if (!dfile_wr_mask || IS_ERR(dfile_wr_mask)) {
		pr_err("PCIe: fail to create the file for debug_fs wr_mask.\n");
		goto err;
	}

	dfile_wr_value = debugfs_create_file("wr_value", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_wr_value_ops);
	if (!dfile_wr_value || IS_ERR(dfile_wr_value)) {
		pr_err("PCIe: fail to create the file for debug_fs wr_value.\n");
		goto err;
	}

	dfile_boot_option = debugfs_create_file("boot_option", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_boot_option_ops);
	if (!dfile_boot_option || IS_ERR(dfile_boot_option)) {
		pr_err("PCIe: fail to create the file for debug_fs boot_option.\n");
		goto err;
	}

	dfile_aer_enable = debugfs_create_file("aer_enable", 0664,
					dent_msm_pcie, NULL,
					&msm_pcie_debugfs_aer_enable_ops);
	if (!dfile_aer_enable || IS_ERR(dfile_aer_enable)) {
		pr_err("PCIe: fail to create the file for debug_fs aer_enable.\n");
		goto err;
	}

	dfile_corr_counter_limit = debugfs_create_file("corr_counter_limit",
				0664, dent_msm_pcie, NULL,
				&msm_pcie_debugfs_corr_counter_limit_ops);
	if (!dfile_corr_counter_limit || IS_ERR(dfile_corr_counter_limit)) {
		pr_err("PCIe: fail to create the file for debug_fs corr_counter_limit.\n");
		goto err;
	}
	return;
err:
	debugfs_remove_recursive(dent_msm_pcie);
}

static void msm_pcie_debugfs_exit(void)
{
	debugfs_remove_recursive(dent_msm_pcie);
}
#else
static void msm_pcie_debugfs_init(void)
{
}

static void msm_pcie_debugfs_exit(void)
{
}
#endif

static int msm_pcie_is_link_up(struct msm_pcie_dev_t *dev)
{
	return readl_relaxed(dev->dm_core +
			PCIE20_CAP_LINKCTRLSTATUS) & BIT(29);
}

static void msm_pcie_config_bandwidth_int(struct msm_pcie_dev_t *dev,
						bool enable)
{
	struct pci_dev *pci_dev = dev->dev;

	if (enable) {
		/* Clear INT_EN and PCI_MSI_ENABLE to receive interrupts */
		msm_pcie_write_mask(dev->dm_core + PCIE20_COMMAND_STATUS,
				    BIT(10), 0);
		msm_pcie_write_mask(dev->dm_core +
				    PCIE20_PCI_MSI_CAP_ID_NEXT_CTRL_REG,
				    BIT(16), 0);

		msm_pcie_write_reg_field(dev->parf, PCIE20_PARF_INT_ALL_2_MASK,
				MSM_PCIE_BW_MGT_INT_STATUS, 1);
		msm_pcie_config_clear_set_dword(pci_dev,
				pci_dev->pcie_cap + PCI_EXP_LNKCTL,
				0, PCI_EXP_LNKCTL_LBMIE);
	} else {
		msm_pcie_write_reg_field(dev->parf, PCIE20_PARF_INT_ALL_2_MASK,
				MSM_PCIE_BW_MGT_INT_STATUS, 0);
		msm_pcie_config_clear_set_dword(pci_dev,
				pci_dev->pcie_cap + PCI_EXP_LNKCTL,
				PCI_EXP_LNKCTL_LBMIE, 0);
	}
}

static void msm_pcie_clear_bandwidth_int_status(struct msm_pcie_dev_t *dev)
{
	struct pci_dev *pci_dev = dev->dev;

	msm_pcie_config_clear_set_dword(pci_dev,
					pci_dev->pcie_cap + PCI_EXP_LNKCTL,
					PCI_EXP_LNKCTL_LBMIE, 0);
	msm_pcie_write_reg_field(dev->parf, PCIE20_PARF_INT_ALL_2_CLEAR,
				 MSM_PCIE_BW_MGT_INT_STATUS, 1);
}

static bool msm_pcie_check_ltssm_state(struct msm_pcie_dev_t *dev, u32 state)
{
	u32 ltssm;

	ltssm = readl_relaxed(dev->parf + PCIE20_PARF_LTSSM) &
		MSM_PCIE_LTSSM_MASK;

	if (ltssm == state)
		return true;

	return false;
}

void msm_pcie_clk_dump(struct msm_pcie_dev_t *pcie_dev)
{
	struct msm_pcie_clk_info_t *clk_info;
	int i;

	PCIE_ERR(pcie_dev,
		 "PCIe: RC%d: Dump PCIe clocks\n",
		 pcie_dev->rc_idx);

	clk_info = pcie_dev->clk;
	for (i = 0; i < pcie_dev->num_clk; i++, clk_info++) {
		if (clk_info->hdl)
			qcom_clk_dump(clk_info->hdl, NULL, 0);
	}

	clk_info = pcie_dev->pipe_clk;
	for (i = 0; i < pcie_dev->num_pipe_clk; i++, clk_info++) {
		if (clk_info->hdl)
			qcom_clk_dump(clk_info->hdl, NULL, 0);
	}
}

/**
 * msm_pcie_iatu_config - configure outbound address translation region
 * @dev: root commpex
 * @nr: region number
 * @type: target transaction type, see PCIE20_CTRL1_TYPE_xxx
 * @host_addr: - region start address on host
 * @host_end: - region end address (low 32 bit) on host,
 *	upper 32 bits are same as for @host_addr
 * @bdf: - bus:device:function
 */
static void msm_pcie_iatu_config(struct msm_pcie_dev_t *dev, int nr, u8 type,
				 unsigned long host_addr, u32 host_end,
				 u32 bdf)
{
	void __iomem *iatu_base = dev->iatu ? dev->iatu : dev->dm_core;

	u32 iatu_viewport_offset;
	u32 iatu_ctrl1_offset;
	u32 iatu_ctrl2_offset;
	u32 iatu_lbar_offset;
	u32 iatu_ubar_offset;
	u32 iatu_lar_offset;
	u32 iatu_ltar_offset;
	u32 iatu_utar_offset;

	/* configure iATU only for endpoints */
	if (!bdf)
		return;

	if (dev->iatu) {
		iatu_viewport_offset = 0;
		iatu_ctrl1_offset = PCIE_IATU_CTRL1(nr);
		iatu_ctrl2_offset = PCIE_IATU_CTRL2(nr);
		iatu_lbar_offset = PCIE_IATU_LBAR(nr);
		iatu_ubar_offset = PCIE_IATU_UBAR(nr);
		iatu_lar_offset = PCIE_IATU_LAR(nr);
		iatu_ltar_offset = PCIE_IATU_LTAR(nr);
		iatu_utar_offset = PCIE_IATU_UTAR(nr);
	} else {
		iatu_viewport_offset = PCIE20_PLR_IATU_VIEWPORT;
		iatu_ctrl1_offset = PCIE20_PLR_IATU_CTRL1;
		iatu_ctrl2_offset = PCIE20_PLR_IATU_CTRL2;
		iatu_lbar_offset = PCIE20_PLR_IATU_LBAR;
		iatu_ubar_offset = PCIE20_PLR_IATU_UBAR;
		iatu_lar_offset = PCIE20_PLR_IATU_LAR;
		iatu_ltar_offset = PCIE20_PLR_IATU_LTAR;
		iatu_utar_offset = PCIE20_PLR_IATU_UTAR;
	}

	/* select region */
	if (iatu_viewport_offset)
		msm_pcie_write_reg(iatu_base, iatu_viewport_offset, nr);

	/* switch off region before changing it */
	msm_pcie_write_reg(iatu_base, iatu_ctrl2_offset, 0);

	msm_pcie_write_reg(iatu_base, iatu_ctrl1_offset, type);
	msm_pcie_write_reg(iatu_base, iatu_lbar_offset,
				lower_32_bits(host_addr));
	msm_pcie_write_reg(iatu_base, iatu_ubar_offset,
				upper_32_bits(host_addr));
	msm_pcie_write_reg(iatu_base, iatu_lar_offset, host_end);
	msm_pcie_write_reg(iatu_base, iatu_ltar_offset, lower_32_bits(bdf));
	msm_pcie_write_reg(iatu_base, iatu_utar_offset, 0);
	msm_pcie_write_reg(iatu_base, iatu_ctrl2_offset, BIT(31));
}

/**
 * msm_pcie_cfg_bdf - configure for config access
 * @dev: root commpex
 * @bus: PCI bus number
 * @devfn: PCI dev and function number
 *
 * Remap if required region 0 for config access of proper type
 * (CFG0 for bus 1, CFG1 for other buses)
 * Cache current device bdf for speed-up
 */
static void msm_pcie_cfg_bdf(struct msm_pcie_dev_t *dev, u8 bus, u8 devfn)
{
	struct resource *axi_conf = dev->res[MSM_PCIE_RES_CONF].resource;
	u32 bdf  = BDF_OFFSET(bus, devfn);
	u8 type = bus == 1 ? PCIE20_CTRL1_TYPE_CFG0 : PCIE20_CTRL1_TYPE_CFG1;

	if (dev->current_bdf == bdf)
		return;

	msm_pcie_iatu_config(dev, 0, type,
			axi_conf->start,
			axi_conf->start + SZ_4K - 1,
			bdf);

	dev->current_bdf = bdf;
}

static int msm_pcie_oper_conf(struct pci_bus *bus, u32 devfn, int oper,
				     int where, int size, u32 *val)
{
	uint32_t word_offset, byte_offset, mask;
	uint32_t rd_val, wr_val;
	struct msm_pcie_dev_t *dev;
	void __iomem *config_base;
	bool rc = false;
	u32 rc_idx, *filtered_bdf;
	int rv = 0, i;
	u32 bdf = BDF_OFFSET(bus->number, devfn);

	dev = PCIE_BUS_PRIV_DATA(bus);

	if (!dev) {
		pr_err("PCIe: No device found for this bus.\n");
		*val = ~0;
		rv = PCIBIOS_DEVICE_NOT_FOUND;
		goto out;
	}

	rc_idx = dev->rc_idx;
	rc = (bus->number == 0);

	spin_lock_irqsave(&dev->cfg_lock, dev->irqsave_flags);

	if (!dev->cfg_access) {
		PCIE_DBG3(dev,
			"Access denied for RC%d %d:0x%02x + 0x%04x[%d]\n",
			rc_idx, bus->number, devfn, where, size);
		*val = ~0;
		rv = PCIBIOS_DEVICE_NOT_FOUND;
		goto unlock;
	}

	if (rc && (devfn != 0)) {
		PCIE_DBG3(dev, "RC%d invalid %s - bus %d devfn %d\n", rc_idx,
			 (oper == RD) ? "rd" : "wr", bus->number, devfn);
		*val = ~0;
		rv = PCIBIOS_DEVICE_NOT_FOUND;
		goto unlock;
	}

	if (dev->link_status != MSM_PCIE_LINK_ENABLED) {
		PCIE_DBG3(dev,
			"Access to RC%d %d:0x%02x + 0x%04x[%d] is denied because link is down\n",
			rc_idx, bus->number, devfn, where, size);
		*val = ~0;
		rv = PCIBIOS_DEVICE_NOT_FOUND;
		goto unlock;
	}

	/* check if the link is up for endpoint */
	if (!rc && !msm_pcie_is_link_up(dev)) {
		PCIE_ERR(dev,
			"PCIe: RC%d %s fail, link down - bus %d devfn %d\n",
				rc_idx, (oper == RD) ? "rd" : "wr",
				bus->number, devfn);
			*val = ~0;
			rv = PCIBIOS_DEVICE_NOT_FOUND;
			goto unlock;
	}

	/* 32-bit BDF filtering */
	if (dev->bdf_count) {
		i = dev->bdf_count;
		filtered_bdf = dev->filtered_bdfs;
		while (i--) {
			if (*filtered_bdf == bdf) {
				*val = ~0;
				goto unlock;
			}
			filtered_bdf++;
		}
	}

	if (!rc)
		msm_pcie_cfg_bdf(dev, bus->number, devfn);

	word_offset = where & ~0x3;
	byte_offset = where & 0x3;
	mask = ((u32)~0 >> (8 * (4 - size))) << (8 * byte_offset);

	config_base = rc ? dev->dm_core : dev->conf;

	rd_val = readl_relaxed(config_base + word_offset);

	if (oper == RD) {
		*val = ((rd_val & mask) >> (8 * byte_offset));
		PCIE_DBG3(dev,
			"RC%d %d:0x%02x + 0x%04x[%d] -> 0x%08x; rd 0x%08x\n",
			rc_idx, bus->number, devfn, where, size, *val, rd_val);
	} else {
		wr_val = (rd_val & ~mask) |
				((*val << (8 * byte_offset)) & mask);

		if ((bus->number == 0) && (where == 0x3c))
			wr_val = wr_val | (3 << 16);

		msm_pcie_write_reg(config_base, word_offset, wr_val);

		PCIE_DBG3(dev,
			"RC%d %d:0x%02x + 0x%04x[%d] <- 0x%08x; rd 0x%08x val 0x%08x\n",
			rc_idx, bus->number, devfn, where, size,
			wr_val, rd_val, *val);
	}

	if (rd_val == PCIE_LINK_DOWN &&
	   (readl_relaxed(config_base) == PCIE_LINK_DOWN)) {
		if (dev->config_recovery) {
			PCIE_ERR(dev,
				 "RC%d link recovery schedule\n",
				 rc_idx);
			dev->cfg_access = false;
			schedule_work(&dev->link_recover_wq);
		}
	}

unlock:
	spin_unlock_irqrestore(&dev->cfg_lock, dev->irqsave_flags);
out:
	return rv;
}

static int msm_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			    int size, u32 *val)
{
	int ret = msm_pcie_oper_conf(bus, devfn, RD, where, size, val);

	if ((bus->number == 0) && (where == PCI_CLASS_REVISION))
		*val = (*val & 0xff) | (PCI_CLASS_BRIDGE_PCI << 16);

	return ret;
}

static int msm_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			    int where, int size, u32 val)
{
	return msm_pcie_oper_conf(bus, devfn, WR, where, size, &val);
}

static struct pci_ops msm_pcie_ops = {
	.read = msm_pcie_rd_conf,
	.write = msm_pcie_wr_conf,
};

/* This function will load the instruction sequence to pcie state manager */
static void msm_pcie_cesta_load_sm_seq(struct msm_pcie_dev_t *dev)
{
	int i = 0;
	struct msm_pcie_sm_info *sm_info = dev->sm_info;

	/* Remove the PWR_CTRL Overrides set for this pcie instance */
	msm_pcie_write_reg(dev->pcie_sm,
			pcie_sm_regs[PCIE_SM_PWR_CTRL_OFFSET] +
		(dev->rc_idx * pcie_sm_regs[PCIE_SM_PWR_INSTANCE_OFFSET]),
									0x0);

	/* Remove the PWR_CTRL_MASK Overrides set for this pcie instance */
	msm_pcie_write_reg(dev->pcie_sm,
			pcie_sm_regs[PCIE_SM_PWR_MASK_OFFSET] +
		(dev->rc_idx * pcie_sm_regs[PCIE_SM_PWR_INSTANCE_OFFSET]),
									0x0);

	/* Loading the pcie state manager sequence */
	for (i = 0; i < sm_info->sm_seq_len; i++) {
		PCIE_DBG(dev, "sm seq val 0x%x\n", sm_info->sm_seq[i]);
		msm_pcie_write_reg(dev->pcie_sm, 4*i, sm_info->sm_seq[i]);
	}

	/* Loading the pcie state manager branch sequence */
	for (i = 0; i < sm_info->sm_branch_len; i++) {
		PCIE_DBG(dev, "branch seq val 0x%x\n", sm_info->branch_seq[i]);
		msm_pcie_write_reg(dev->pcie_sm, sm_info->branch_offset + 4*i,
						sm_info->branch_seq[i]);
	}

	/* Enable the pcie state manager once the sequence is loaded */
	msm_pcie_write_reg_field(dev->pcie_sm, sm_info->start_offset,
								BIT(0), 1);
}

/* This function will get the pcie state manager sequence from DT node */
static int msm_pcie_cesta_get_sm_seq(struct msm_pcie_dev_t *dev)
{
	int ret, size = 0;
	struct platform_device *pdev = dev->pdev;
	struct msm_pcie_sm_info *sm_info;

	of_get_property(pdev->dev.of_node, "qcom,pcie-sm-seq", &size);
	if (!size) {
		PCIE_DBG(dev,
			"PCIe: RC%d: state manager seq is not present in DT\n",
			dev->rc_idx);
		return -EIO;
	}

	sm_info = devm_kzalloc(&pdev->dev, sizeof(struct msm_pcie_sm_info),
								GFP_KERNEL);
	if (!sm_info)
		return -ENOMEM;

	sm_info->sm_seq_len = size / sizeof(u32);

	sm_info->sm_seq = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!sm_info->sm_seq)
		return -ENOMEM;

	ret = of_property_read_u32_array(pdev->dev.of_node,
			"qcom,pcie-sm-seq", sm_info->sm_seq,
						sm_info->sm_seq_len);
	if (ret)
		return -EIO;

	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,pcie-sm-branch-offset", &sm_info->branch_offset);
	if (ret)
		return -EIO;

	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,pcie-sm-start-offset", &sm_info->start_offset);
	if (ret)
		return -EIO;

	of_get_property(pdev->dev.of_node, "qcom,pcie-sm-branch-seq", &size);
	if (!size) {
		PCIE_DBG(dev,
			"PCIe: RC%d: sm branch seq is not present in DT\n",
			dev->rc_idx);
		return -EIO;
	}

	sm_info->sm_branch_len = size / sizeof(u32);

	sm_info->branch_seq = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!sm_info->branch_seq)
		return -ENOMEM;

	ret = of_property_read_u32_array(pdev->dev.of_node,
			"qcom,pcie-sm-branch-seq", sm_info->branch_seq,
						sm_info->sm_branch_len);
	if (ret)
		return -EIO;

	of_get_property(pdev->dev.of_node, "qcom,pcie-sm-debug", &size);
	if (!size) {
		PCIE_DBG(dev,
			"PCIe: RC%d: sm debugs regs are not present in DT\n",
			dev->rc_idx);
		goto out;
	}

	sm_info->reg_dump_len = size / sizeof(u32);
	sm_info->reg_dump = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!sm_info->reg_dump)
		return -ENOMEM;

	ret = of_property_read_u32_array(pdev->dev.of_node,
			"qcom,pcie-sm-debug", sm_info->reg_dump,
						sm_info->reg_dump_len);
	if (ret)
		sm_info->reg_dump_len = 0;

out:
	dev->sm_info = sm_info;

	return 0;
}

/*
 * Arm the l1ss sleep timeout so that pcie controller can send the
 * l1ss TO signal to pcie state manager and state manager can further
 * go into l1ss sleep state to turn off the resources.
 */
static void msm_pcie_cesta_enable_l1ss_to(struct msm_pcie_dev_t *dev)
{
	u32 val;

	msm_pcie_write_reg(dev->parf, PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
				PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET);

	val = PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET |
		L1SS_TIMEOUT_US_TO_TICKS(dev->l1ss_timeout_us,
						dev->aux_clk_freq);

	msm_pcie_write_reg(dev->parf, PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
									val);
}

/* Disable L1ss timeout timer */
static void msm_pcie_cesta_disable_l1ss_to(struct msm_pcie_dev_t *dev)
{
	msm_pcie_write_reg(dev->parf, PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER, 0);
}

/* Read the curr perf ol value from the cesta register */
static const char *const msm_pcie_cesta_curr_perf_ol(struct msm_pcie_dev_t *dev)
{
	u32 ret;
	int res;

	res = crm_read_curr_perf_ol("pcie_crm", dev->rc_idx, &ret);
	if (res) {
		PCIE_ERR(dev, "PCIE: RC:%d Error getting curr_perf_ol %d\n",
				dev->rc_idx, res);
		ret = MAX_PERF_LVL;
	}

	if (ret > MAX_PERF_LVL)
		ret = MAX_PERF_LVL;

	return msm_pcie_cesta_curr_perf_lvl[ret];
}

/*
 * This function is used for configuring the CESTA power state
 * to the perf level mapping based on the Gen speed provided in
 * the argument
 */
static void msm_pcie_cesta_map_save(int gen_speed)
{
	/* Gen1 speed is equal to perf levle 2 */
	gen_speed += PERF_LVL_L1SS;

	msm_pcie_cesta_map[D0_STATE][POWER_STATE_0] = gen_speed;
	msm_pcie_cesta_map[D0_STATE][POWER_STATE_1] = gen_speed;
	msm_pcie_cesta_map[DRV_STATE][POWER_STATE_1] = gen_speed;
}

/*
 * Apply the cesta power state <--> perf ol mapping using the
 * crm driver APIs.
 */
static int msm_pcie_cesta_map_apply(struct msm_pcie_dev_t *dev, u32 cesta_st)
{
	int ret = 0;
	struct crm_cmd cmd;
	u32 pwr_st;

	if (!dev->pcie_sm)
		return 0;

	PCIE_DBG(dev, "Current perf ol is %s\n",
				msm_pcie_cesta_curr_perf_ol(dev));

	PCIE_DBG(dev, "Setting the scenario to %s and perf_idx %d\n",
			msm_pcie_cesta_states[cesta_st],
			msm_pcie_cesta_map[cesta_st][POWER_STATE_1]);

	for (pwr_st = 0; pwr_st < MAX_POWER_STATE; pwr_st++) {
		cmd.pwr_state.hw = pwr_st;
		cmd.resource_idx = dev->rc_idx;
		cmd.data = msm_pcie_cesta_map[cesta_st][pwr_st];

		ret = crm_write_perf_ol(dev->crm_dev, CRM_HW_DRV, dev->rc_idx,
									&cmd);
		if (ret) {
			PCIE_DBG(dev, "PCIe: RC%d: pwr_st %d perf_ol %d\n",
					dev->rc_idx, pwr_st, ret);
			return ret;
		}
	}

	ret = crm_write_pwr_states(dev->crm_dev, dev->rc_idx);
	if (ret) {
		PCIE_DBG(dev, "PCIe: RC%d: pwr_st %d pwr_states %d\n",
				dev->rc_idx, pwr_st, ret);
		return ret;
	}

	PCIE_DBG(dev, "New perf ol is %s\n",
				msm_pcie_cesta_curr_perf_ol(dev));
	return 0;
}

/*
 * This function will cause the entry into drv state by
 * configuring CESTA to drv state
 */
static void msm_pcie_cesta_enable_drv(struct msm_pcie_dev_t *dev,
							bool enable_to)
{
	int ret;

	if (!dev->pcie_sm)
		return;

	if (enable_to)
		msm_pcie_cesta_enable_l1ss_to(dev);

	/*
	 * Use CLKREQ as wake up capable gpio so that when APPS
	 * is in sleep CESTA block can still get the CLKREQ
	 * assertion event.
	 */
	ret = msm_gpio_mpm_wake_set(dev->clkreq_gpio, true);
	if (ret)
		PCIE_ERR(dev, "Failed to make clkreq wakeup capable%d\n", ret);

	ret = pcie_pdc_cfg_irq(dev->clkreq_gpio, IRQ_TYPE_EDGE_FALLING, true);
	if (ret)
		PCIE_ERR(dev, "Failed to make clkreq pdc wakeup capable%d\n", ret);

	/* Use CESTA to manage the resources in DRV state */
	ret = msm_pcie_cesta_map_apply(dev, DRV_STATE);
	if (ret)
		PCIE_ERR(dev, "Failed to move to DRV state %d\n", ret);
}

/*
 * This function will configure CESTA to move to D0 state
 * from the drv state
 */
static void msm_pcie_cesta_disable_drv(struct msm_pcie_dev_t *dev)
{
	int ret;

	if (!dev->pcie_sm)
		return;

	/* Use CESTA to turn on the resources into D0 state from DRV state*/
	ret = msm_pcie_cesta_map_apply(dev, D0_STATE);
	if (ret)
		PCIE_ERR(dev, "Failed to move to D0 State %d\n", ret);

	msm_pcie_cesta_disable_l1ss_to(dev);

	/* Remove CLKREQ as wake up capable gpio */
	ret = msm_gpio_mpm_wake_set(dev->clkreq_gpio, false);
	if (ret)
		PCIE_ERR(dev, "Fail to remove clkreq wakeup capable%d\n", ret);

	ret = pcie_pdc_cfg_irq(dev->clkreq_gpio, IRQ_TYPE_EDGE_FALLING, false);
	if (ret)
		PCIE_ERR(dev, "Fail to remove clkreq pdc wakeup capable%d\n", ret);
}

static int msm_pcie_gpio_init(struct msm_pcie_dev_t *dev)
{
	int rc = 0, i;
	struct msm_pcie_gpio_info_t *info;

	PCIE_DBG(dev, "RC%d\n", dev->rc_idx);

	for (i = 0; i < dev->gpio_n; i++) {
		info = &dev->gpio[i];

		if (!info->num)
			continue;

		rc = gpio_request(info->num, info->name);
		if (rc) {
			PCIE_ERR(dev, "PCIe: RC%d can't get gpio %s; %d\n",
				dev->rc_idx, info->name, rc);
			break;
		}

		if (info->out)
			rc = gpio_direction_output(info->num, info->init);
		else
			rc = gpio_direction_input(info->num);
		if (rc) {
			PCIE_ERR(dev,
				"PCIe: RC%d can't set direction for GPIO %s:%d\n",
				dev->rc_idx, info->name, rc);
			gpio_free(info->num);
			break;
		}
	}

	if (rc)
		while (i--)
			gpio_free(dev->gpio[i].num);

	return rc;
}

static void msm_pcie_gpio_deinit(struct msm_pcie_dev_t *dev)
{
	int i;

	PCIE_DBG(dev, "RC%d\n", dev->rc_idx);

	for (i = 0; i < dev->gpio_n; i++)
		gpio_free(dev->gpio[i].num);
}

static int msm_pcie_vreg_init(struct msm_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct regulator *vreg;
	struct msm_pcie_vreg_info_t *info;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	for (i = 0; i < MSM_PCIE_MAX_VREG; i++) {
		info = &dev->vreg[i];
		vreg = info->hdl;

		if (!vreg)
			continue;

		PCIE_DBG2(dev, "RC%d Vreg %s is being enabled\n",
			dev->rc_idx, info->name);
		if (info->max_v) {
			rc = regulator_set_voltage(vreg,
						   info->min_v, info->max_v);
			if (rc) {
				PCIE_ERR(dev,
					"PCIe: RC%d can't set voltage for %s: %d\n",
					dev->rc_idx, info->name, rc);
				break;
			}
		}

		if (info->opt_mode) {
			rc = regulator_set_load(vreg, info->opt_mode);
			if (rc < 0) {
				PCIE_ERR(dev,
					"PCIe: RC%d can't set mode for %s: %d\n",
					dev->rc_idx, info->name, rc);
				break;
			}
		}

		rc = regulator_enable(vreg);
		if (rc) {
			PCIE_ERR(dev,
				"PCIe: RC%d can't enable regulator %s: %d\n",
				dev->rc_idx, info->name, rc);
			break;
		}
	}

	if (rc)
		while (i--) {
			struct regulator *hdl = dev->vreg[i].hdl;

			if (hdl) {
				regulator_disable(hdl);
				if (!strcmp(dev->vreg[i].name, "vreg-cx") ||
					!strcmp(dev->vreg[i].name, "vreg-mx")) {
					PCIE_DBG(dev,
						"RC%d: Removing %s vote.\n",
						dev->rc_idx,
						dev->vreg[i].name);
					regulator_set_voltage(hdl,
						RPMH_REGULATOR_LEVEL_RETENTION,
						RPMH_REGULATOR_LEVEL_MAX);
				}

				if (dev->vreg[i].opt_mode) {
					rc = regulator_set_load(hdl, 0);
					if (rc < 0)
						PCIE_ERR(dev,
							"PCIe: RC%d can't set mode for %s: %d\n",
							dev->rc_idx,
							dev->vreg[i].name, rc);
				}
			}

		}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return rc;
}

static void msm_pcie_vreg_init_analog_rails(struct msm_pcie_dev_t *dev)
{
	int i, rc;

	for (i = 0; i < MSM_PCIE_MAX_VREG; i++) {
		if (dev->vreg[i].hdl) {
			/*
			 * Enable all the voltage regulators except the 3p3 regulator,
			 * as 3p3 is main power supply for some endpoints like NVMe.
			 */
			if (strcmp(dev->vreg[i].name, "vreg-3p3")) {
				PCIE_DBG(dev, "Vreg %s is being enabled\n",
					dev->vreg[i].name);
				rc = regulator_enable(dev->vreg[i].hdl);
				if (rc) {
					PCIE_ERR(dev,
					"PCIe: RC%d can't enable regulator %s: %d\n",
					dev->rc_idx, dev->vreg[i].name, rc);
				}
			}
		}
	}
}

static void msm_pcie_vreg_deinit_analog_rails(struct msm_pcie_dev_t *dev)
{
	int i;

	for (i = MSM_PCIE_MAX_VREG - 1; i >= 0; i--) {
		if (dev->vreg[i].hdl) {
			/*
			 * Disable all the voltage regulators except the 3p3 regulator,
			 * as 3p3 is main power supply for some endpoints like NVMe.
			 */
			if (strcmp(dev->vreg[i].name, "vreg-3p3")) {
				PCIE_DBG(dev, "Vreg %s is being disabled\n",
					dev->vreg[i].name);
				regulator_disable(dev->vreg[i].hdl);
			}
		}
	}
}

static void msm_pcie_vreg_deinit(struct msm_pcie_dev_t *dev)
{
	int i, ret;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	for (i = MSM_PCIE_MAX_VREG - 1; i >= 0; i--) {
		if (dev->vreg[i].hdl) {
			PCIE_DBG(dev, "Vreg %s is being disabled\n",
				dev->vreg[i].name);
			regulator_disable(dev->vreg[i].hdl);

			if (!strcmp(dev->vreg[i].name, "vreg-cx") ||
				!strcmp(dev->vreg[i].name, "vreg-mx")) {
				PCIE_DBG(dev,
					"RC%d: Removing %s vote.\n",
					dev->rc_idx,
					dev->vreg[i].name);
				regulator_set_voltage(dev->vreg[i].hdl,
					RPMH_REGULATOR_LEVEL_RETENTION,
					RPMH_REGULATOR_LEVEL_MAX);
			}

			if (dev->vreg[i].opt_mode) {
				ret = regulator_set_load(dev->vreg[i].hdl, 0);
				if (ret < 0)
					PCIE_ERR(dev,
						"PCIe: RC%d can't set mode for %s: %d\n",
						dev->rc_idx, dev->vreg[i].name,
						ret);
			}
		}
	}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);
}

/* This function will initialize gdsc core and gdsc phy regulators */
static int msm_pcie_gdsc_init(struct msm_pcie_dev_t *dev)
{
	int rc = 0;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	if (dev->gdsc_core) {
		rc = regulator_enable(dev->gdsc_core);
		if (rc) {
			PCIE_ERR(dev,
			"PCIe: fail to enable GDSC-CORE for RC%d (%s)\n",
						dev->rc_idx, dev->pdev->name);
			return rc;
		}
	}

	if (dev->gdsc_phy) {
		rc = regulator_enable(dev->gdsc_phy);
		if (rc) {
			PCIE_ERR(dev,
			"PCIe: fail to enable GDSC-PHY for RC%d (%s)\n",
						dev->rc_idx, dev->pdev->name);
			return rc;
		}
	}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return 0;
}

/* This function will de-initialize gdsc core and gdsc phy regulators */
static int msm_pcie_gdsc_deinit(struct msm_pcie_dev_t *dev)
{
	int rc = 0;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	if (dev->gdsc_core) {
		rc = regulator_disable(dev->gdsc_core);
		if (rc) {
			PCIE_ERR(dev,
				"PCIe:RC%d fail to disable GDSC-CORE (%s)\n",
						dev->rc_idx, dev->pdev->name);
			return rc;
		}
	}

	if (dev->gdsc_phy) {
		rc = regulator_disable(dev->gdsc_phy);
		if (rc) {
			PCIE_ERR(dev,
				"PCIe:RC%d fail to disable GDSC-PHY (%s)\n",
						dev->rc_idx, dev->pdev->name);
			return rc;
		}
	}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return 0;
}

/* This function will reset pcie controller and phy */
static int msm_pcie_core_phy_reset(struct msm_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct msm_pcie_reset_info_t *reset_info;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	for (i = 0; i < MSM_PCIE_MAX_RESET; i++) {
		reset_info = &dev->reset[i];
		if (reset_info->hdl) {
			rc = reset_control_assert(reset_info->hdl);
			if (rc)
				PCIE_ERR(dev,
					"PCIe: RC%d failed to assert reset for %s.\n",
					dev->rc_idx, reset_info->name);
			else
				PCIE_DBG2(dev,
					"PCIe: RC%d successfully asserted reset for %s.\n",
					dev->rc_idx, reset_info->name);

			/* add a 1ms delay to ensure the reset is asserted */
			usleep_range(1000, 1005);

			rc = reset_control_deassert(reset_info->hdl);
			if (rc)
				PCIE_ERR(dev,
					"PCIe: RC%d failed to deassert reset for %s.\n",
					dev->rc_idx, reset_info->name);
			else
				PCIE_DBG2(dev,
					"PCIe: RC%d successfully deasserted reset for %s.\n",
					dev->rc_idx, reset_info->name);
		}
	}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return rc;
}

static int msm_pcie_icc_vote(struct msm_pcie_dev_t *dev, u8 speed,
						u8 width, bool drv_state)
{
	u32 bw;
	int rc = 0;
	u32 icc_tags;

	if (dev->icc_path) {
		icc_tags = drv_state ? QCOM_ICC_TAG_PWR_ST_1 :
			QCOM_ICC_TAG_PWR_ST_0 | QCOM_ICC_TAG_PWR_ST_1;

		/*
		 * This API icc_set_tag() call is needed when CESTA is enabled.
		 * Instead of pcie driver settiing up the icc bandwidth votes
		 * for different power states of CESTA, icc driver will take
		 * care of it when we call icc_set_tag API.
		 */
		if (dev->pcie_sm)
			icc_set_tag(dev->icc_path, icc_tags);

		switch (speed) {
		case 1:
			bw = 250000; /* avg bw / AB: 2.5 GBps, peak bw / IB: no vote */
			break;
		case 2:
			bw = 500000; /* avg bw / AB: 5 GBps, peak bw / IB: no vote */
			break;
		case 3:
			bw = 1000000; /* avg bw / AB: 8 GBps, peak bw / IB: no vote */
			break;
		case 4:
			bw = 2000000; /* avg bw / AB: 16 GBps, peak bw / IB: no vote */
			break;
		case 5:
			bw = 4000000; /* avg bw / AB: 32 GBps, peak bw / IB: no vote */
			break;
		default:
			bw = 0;
			break;
		}

		if (speed == 0) {
			/* Speed == 0 implies to vote for '0' bandwidth. */
			rc = icc_set_bw(dev->icc_path, 0, 0);
		} else {
			/*
			 * If there is no icc voting from the client driver then vote for icc
			 * bandwidth is based up on link speed and width or vote for average
			 * icc bandwidth.
			 */
			if (dev->no_client_based_bw_voting)
				rc = icc_set_bw(dev->icc_path, width * bw, 0);
			else
				rc = icc_set_bw(dev->icc_path, ICC_AVG_BW, ICC_PEAK_BW);
		}

		if (rc)
			PCIE_ERR(dev,
				"PCIe: RC%d: failed to put the ICC vote %d.\n",
				dev->rc_idx, rc);
		else
			PCIE_DBG(dev,
				"PCIe: RC%d: ICC vote successful\n",
				dev->rc_idx);

		/*
		 * When PCIe-CESTA is enabled, need to explicitly call
		 * crm_write_pwr_states() API so that the icc votes are
		 * reflected at the HW level.
		 */
		if (dev->pcie_sm) {
			rc = crm_write_pwr_states(dev->crm_dev, dev->rc_idx);
			if (rc) {
				PCIE_DBG(dev, "PCIe: RC%d: pwr_states %d\n",
						dev->rc_idx, rc);
			}
		}
	}

	return rc;
}

static int msm_pcie_clk_init(struct msm_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct msm_pcie_clk_info_t *info;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	/* switch pipe clock source after gdsc-core is turned on */
	if (dev->pipe_clk_mux && dev->pipe_clk_ext_src)
		clk_set_parent(dev->pipe_clk_mux, dev->pipe_clk_ext_src);

	/* vote with GEN1x1 before link up */
	rc = msm_pcie_icc_vote(dev, GEN1_SPEED, LINK_WIDTH_X1, false);
	if (rc)
		return rc;

	for (i = 0; i < dev->num_clk; i++) {
		info = &dev->clk[i];

		if (!info->hdl)
			continue;

		if (info->freq) {
			rc = clk_set_rate(info->hdl, info->freq);
			if (rc) {
				PCIE_ERR(dev,
					"PCIe: RC%d can't set rate for clk %s: %d.\n",
					dev->rc_idx, info->name, rc);
				break;
			}

			PCIE_DBG2(dev,
				"PCIe: RC%d set rate for clk %s.\n",
				dev->rc_idx, info->name);
		}

		rc = clk_prepare_enable(info->hdl);

		if (rc) {
			PCIE_ERR(dev, "PCIe: RC%d failed to enable clk %s\n",
				dev->rc_idx, info->name);
			break;
		}
		PCIE_DBG2(dev, "enable clk %s for RC%d.\n", info->name,
								dev->rc_idx);
	}

	if (rc) {
		PCIE_DBG(dev, "RC%d disable clocks for error handling.\n",
			dev->rc_idx);
		while (i--) {
			struct clk *hdl = dev->clk[i].hdl;

			if (hdl)
				clk_disable_unprepare(hdl);
		}

		/* switch pipe clock mux to xo before turning off gdsc-core */
		if (dev->pipe_clk_mux && dev->ref_clk_src)
			clk_set_parent(dev->pipe_clk_mux, dev->ref_clk_src);
	}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return rc;
}

static void msm_pcie_clk_deinit(struct msm_pcie_dev_t *dev)
{
	int i;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	for (i = 0; i < dev->num_clk; i++)
		if (dev->clk[i].hdl)
			clk_disable_unprepare(dev->clk[i].hdl);

	msm_pcie_icc_vote(dev, 0, 0, false);

	/* switch phy aux clock mux to xo before turning off gdsc-core */
	if (dev->phy_aux_clk_mux && dev->ref_clk_src)
		clk_set_parent(dev->phy_aux_clk_mux, dev->ref_clk_src);

	/* switch pipe clock mux to xo before turning off gdsc */
	if (dev->pipe_clk_mux && dev->ref_clk_src)
		clk_set_parent(dev->pipe_clk_mux, dev->ref_clk_src);

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);
}

/* This function will assert, de-assert pipe reset signal */
static int msm_pcie_pipe_reset(struct msm_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct msm_pcie_reset_info_t *pipe_reset_info;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	for (i = 0; i < MSM_PCIE_MAX_PIPE_RESET; i++) {
		pipe_reset_info = &dev->pipe_reset[i];
		if (pipe_reset_info->hdl) {
			rc = reset_control_assert(pipe_reset_info->hdl);
			if (rc)
				PCIE_ERR(dev,
					"PCIe: RC%d failed to assert pipe reset for %s.\n",
					dev->rc_idx, pipe_reset_info->name);
			else
				PCIE_DBG2(dev,
					"PCIe: RC%d successfully asserted pipe reset for %s.\n",
					dev->rc_idx, pipe_reset_info->name);

			/* add a 1ms delay to ensure the reset is asserted */
			usleep_range(1000, 1005);

			rc = reset_control_deassert(
					pipe_reset_info->hdl);
			if (rc)
				PCIE_ERR(dev,
					"PCIe: RC%d failed to deassert pipe reset for %s.\n",
					dev->rc_idx, pipe_reset_info->name);
			else
				PCIE_DBG2(dev,
					"PCIe: RC%d successfully deasserted pipe reset for %s.\n",
					dev->rc_idx, pipe_reset_info->name);
		}
	}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return rc;
}

static int msm_pcie_pipe_clk_init(struct msm_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct msm_pcie_clk_info_t *info;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	for (i = 0; i < dev->num_pipe_clk; i++) {
		info = &dev->pipe_clk[i];

		if (!info->hdl)
			continue;

		if (info->freq) {
			rc = clk_set_rate(info->hdl, info->freq);
			if (rc) {
				PCIE_ERR(dev,
					"PCIe: RC%d can't set rate for clk %s: %d.\n",
					dev->rc_idx, info->name, rc);
				break;
			}

			PCIE_DBG2(dev,
				"PCIe: RC%d set rate for clk %s: %d.\n",
				dev->rc_idx, info->name, rc);
		}

		rc = clk_prepare_enable(info->hdl);

		if (rc) {
			PCIE_ERR(dev, "PCIe: RC%d failed to enable clk %s.\n",
				dev->rc_idx, info->name);
			break;
		}
		PCIE_DBG2(dev, "RC%d enabled pipe clk %s.\n", dev->rc_idx,
								info->name);
	}

	if (rc) {
		PCIE_DBG(dev, "RC%d disable pipe clocks for error handling.\n",
			dev->rc_idx);
		while (i--)
			if (dev->pipe_clk[i].hdl)
				clk_disable_unprepare(dev->pipe_clk[i].hdl);
	}

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return rc;
}

static void msm_pcie_pipe_clk_deinit(struct msm_pcie_dev_t *dev)
{
	int i;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	for (i = 0; i < dev->num_pipe_clk; i++)
		if (dev->pipe_clk[i].hdl)
			clk_disable_unprepare(
				dev->pipe_clk[i].hdl);

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);
}

static bool pcie_phy_is_ready(struct msm_pcie_dev_t *dev)
{
	/* There is no PHY status check in RUMI */
	if (dev->rumi)
		return true;

	if (readl_relaxed(dev->phy + dev->phy_status_offset) &
		BIT(dev->phy_status_bit))
		return false;
	else
		return true;
}

static int pcie_phy_init(struct msm_pcie_dev_t *dev)
{
	int i, ret;
	long retries = 0;
	struct msm_pcie_phy_info_t *phy_seq;

	PCIE_DBG(dev, "PCIe: RC%d: Initializing PHY\n", dev->rc_idx);

	if (dev->phy_sequence) {
		i =  dev->phy_len;
		phy_seq = dev->phy_sequence;
		while (i--) {
			msm_pcie_write_reg(dev->phy,
				phy_seq->offset,
				phy_seq->val);
			if (phy_seq->delay)
				usleep_range(phy_seq->delay,
					phy_seq->delay + 1);
			phy_seq++;
		}
	}

	usleep_range(PHY_STABILIZATION_DELAY_US_MIN,
		PHY_STABILIZATION_DELAY_US_MAX);

	/* Enable the pipe clock */
	ret = msm_pcie_pipe_clk_init(dev);

	/* ensure that changes propagated to the hardware */
	wmb();

	/* Assert, De-assert the pipe reset */
	ret = msm_pcie_pipe_reset(dev);

	/* ensure that changes propagated to the hardware */
	wmb();

	PCIE_DBG(dev, "PCIe RC%d: waiting for phy ready...\n", dev->rc_idx);
	do {
		if (pcie_phy_is_ready(dev))
			break;
		retries++;
		usleep_range(REFCLK_STABILIZATION_DELAY_US_MIN,
					 REFCLK_STABILIZATION_DELAY_US_MAX);
	} while (retries < PHY_READY_TIMEOUT_COUNT);

	PCIE_DBG(dev, "PCIe: RC%d: number of PHY retries: %ld.\n", dev->rc_idx,
		retries);

	if (!pcie_phy_is_ready(dev)) {
		PCIE_ERR(dev, "PCIe PHY RC%d failed to come up!\n",
			dev->rc_idx);
		pcie_phy_dump(dev);
		return -ENODEV;
	}

	PCIE_INFO(dev, "PCIe RC%d PHY is ready!\n", dev->rc_idx);

	return 0;
}

static u16 msm_pci_find_ext_capability(struct msm_pcie_dev_t *pci, u8 cap)
{
	int pos = PCI_CFG_SPACE_SIZE;
	u32 header;
	int ttl;

	/* minimum 8 bytes per capability */
	ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	header = readl_relaxed(pci->dm_core + pos);
	/*
	 * If we have no capabilities, this is indicated by cap ID,
	 * cap version and next pointer all being 0.
	 */
	if (header == 0)
		return 0;

	while (ttl-- > 0) {
		if (PCI_EXT_CAP_ID(header) == cap && pos != 0)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < PCI_CFG_SPACE_SIZE)
			break;

		header = readl_relaxed(pci->dm_core + pos);
	}

	return 0;
}

static void msm_pcie_config_core_preset(struct msm_pcie_dev_t *pcie_dev)
{
	u32 supported_link_speed, supported_link_width;
	u16 cap_id_offset, offset;
	u32 val;
	int i;

	val = readl_relaxed(pcie_dev->dm_core + PCIE20_CAP + PCI_EXP_LNKCAP);

	supported_link_speed = val & PCI_EXP_LNKCAP_SLS;
	supported_link_width =  (val & PCI_EXP_LNKCAP_MLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;

	/* enable write access to RO register */
	msm_pcie_write_mask(pcie_dev->dm_core + PCIE_GEN3_MISC_CONTROL, 0, BIT(0));

	/* Gen3 */
	if (supported_link_speed >= PCI_EXP_LNKCAP_SLS_8_0GB) {
		cap_id_offset = msm_pci_find_ext_capability(pcie_dev, PCI_EXT_CAP_ID_SECPCI);
		if (cap_id_offset == 0)
			return;
		/* GEN3 preset is at 0xC offset from Secondary PCI Express Extended Capability ID */
		offset = cap_id_offset + 0xC;
		msm_pcie_write_reg(pcie_dev->dm_core, offset, pcie_dev->core_preset);
		/*
		 * Each register provides preset hint for 2 lanes.
		 * If there are more than 2 lanes then programing remaining lanes.
		 */
		for (i = 2; i < supported_link_width; i = i+2) {
			offset += 0x4;
			msm_pcie_write_reg(pcie_dev->dm_core, offset, pcie_dev->core_preset);
		}
	}

	/* Gen4 */
	if (supported_link_speed >= PCI_EXP_LNKCAP_SLS_16_0GB) {
		cap_id_offset = msm_pci_find_ext_capability(pcie_dev, PCI_EXT_CAP_ID_PL_16GT);
		if (cap_id_offset == 0)
			return;
		/*
		 * GEN4 preset is at 0x20 offset from Physical Layer
		 * 16.0 GT/s Extended Capability ID
		 */
		offset = cap_id_offset + 0x20;
		msm_pcie_write_reg(pcie_dev->dm_core, offset, pcie_dev->core_preset);
		/*
		 * Each register provides preset hint for 4 lanes.
		 * If there are more than 4 lanes then programing remaining lanes.
		 */
		for (i = 4; i < supported_link_width; i = i+4) {
			offset += 0x4;
			msm_pcie_write_reg(pcie_dev->dm_core, offset, pcie_dev->core_preset);
		}
	}

	/* disable write access to RO register */
	msm_pcie_write_mask(pcie_dev->dm_core + PCIE_GEN3_MISC_CONTROL, BIT(0), 0);
}

/* Controller settings related to PCIe PHY */
static void msm_pcie_config_controller_phy(struct msm_pcie_dev_t *pcie_dev)
{
	int i;
	u32 supported_link_speed =
		readl_relaxed(pcie_dev->dm_core + PCIE20_CAP + PCI_EXP_LNKCAP) &
		PCI_EXP_LNKCAP_SLS;

	/* settings apply to GEN3 and above */
	for (i = PCI_EXP_LNKCAP_SLS_8_0GB; i <= supported_link_speed; i++) {
		/* select which GEN speed to configure settings for */
		msm_pcie_write_reg_field(pcie_dev->dm_core, PCIE_GEN3_RELATED,
					PCIE_GEN3_RELATED_RATE_SHADOW_SEL_MASK,
					PCIE_GEN3_RELATED_RATE_SHADOW_SEL(i));

		msm_pcie_write_reg_field(pcie_dev->dm_core, PCIE_GEN3_EQ_CONTROL,
					PCIE_GEN3_EQ_PSET_REQ_VEC_MASK,
					pcie_dev->eq_pset_req_vec);

		/* GEN3_ZRXDC_NONCOMPL */
		msm_pcie_write_mask(pcie_dev->dm_core +
					PCIE_GEN3_RELATED, BIT(0), 0);

		msm_pcie_write_reg_field(pcie_dev->dm_core,
				PCIE_GEN3_EQ_FB_MODE_DIR_CHANGE,
				PCIE_GEN3_EQ_FMDC_T_MIN_PHASE23_MASK,
				pcie_dev->eq_fmdc_t_min_phase23);
	}
}

static void msm_pcie_config_controller(struct msm_pcie_dev_t *dev)
{
	PCIE_DBG(dev, "RC%d\n", dev->rc_idx);

	/*
	 * program and enable address translation region 0 (device config
	 * address space); region type config;
	 * axi config address range to device config address range. Enable
	 * translation for bus 1 dev 0 fn 0.
	 */
	dev->current_bdf = 0; /* to force IATU re-config */
	msm_pcie_cfg_bdf(dev, 1, 0);

	/* configure N_FTS */
	PCIE_DBG2(dev, "Original PCIE20_ACK_F_ASPM_CTRL_REG:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_ACK_F_ASPM_CTRL_REG));
	if (!dev->n_fts)
		msm_pcie_write_mask(dev->dm_core + PCIE20_ACK_F_ASPM_CTRL_REG,
					0, BIT(15));
	else
		msm_pcie_write_mask(dev->dm_core + PCIE20_ACK_F_ASPM_CTRL_REG,
					PCIE20_ACK_N_FTS,
					dev->n_fts << 8);

	PCIE_DBG2(dev, "Updated PCIE20_ACK_F_ASPM_CTRL_REG:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_ACK_F_ASPM_CTRL_REG));

	/* configure AUX clock frequency register for PCIe core */
	if (dev->aux_clk_freq)
		msm_pcie_write_reg(dev->dm_core, PCIE20_AUX_CLK_FREQ_REG, dev->aux_clk_freq);

	dev->aux_clk_freq = readl_relaxed(dev->dm_core +
					  PCIE20_AUX_CLK_FREQ_REG);

	/* configure the completion timeout value for PCIe core */
	if (dev->cpl_timeout && dev->bridge_found)
		msm_pcie_write_reg_field(dev->dm_core,
					PCIE20_DEVICE_CONTROL2_STATUS2,
					0xf, dev->cpl_timeout);

	/* Enable AER on RC */
	if (dev->aer_enable) {
		msm_pcie_write_mask(dev->dm_core + PCIE20_BRIDGE_CTRL, 0,
						BIT(16)|BIT(17));
		msm_pcie_write_mask(dev->dm_core +  PCIE20_CAP_DEVCTRLSTATUS, 0,
						BIT(3)|BIT(2)|BIT(1)|BIT(0));

		PCIE_DBG(dev, "RC's PCIE20_CAP_DEVCTRLSTATUS:0x%x\n",
			readl_relaxed(dev->dm_core + PCIE20_CAP_DEVCTRLSTATUS));
	}
}

static int msm_pcie_get_clk(struct msm_pcie_dev_t *pcie_dev)
{
	struct platform_device *pdev = pcie_dev->pdev;
	u32 *clk_freq = NULL, *clk_suppressible = NULL;
	int ret, i, total_num_clk;
	struct clk_bulk_data *bulk_clks;
	struct msm_pcie_clk_info_t *clk;

	/* get clocks */
	ret = devm_clk_bulk_get_all(&pdev->dev, &bulk_clks);
	if (ret <= 0) {
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: failed to get clocks: ret: %d\n",
			 pcie_dev->rc_idx, ret);
		goto out;
	}

	total_num_clk = ret;

	ret = of_property_count_elems_of_size(pdev->dev.of_node,
					      "clock-frequency",
					      sizeof(*clk_freq));
	if (ret != total_num_clk) {
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: mismatch between number of clock and frequency entries: %d != %d\n",
			 pcie_dev->rc_idx, total_num_clk, ret);
		return -EIO;
	}

	/* get clock frequency info */
	clk_freq = devm_kcalloc(&pdev->dev, total_num_clk, sizeof(*clk_freq),
				GFP_KERNEL);
	if (!clk_freq)
		return -ENOMEM;

	ret = of_property_read_u32_array(pdev->dev.of_node, "clock-frequency",
					 clk_freq, total_num_clk);
	if (ret) {
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: failed to get clock frequencies: ret: %d\n",
			 pcie_dev->rc_idx, ret);
		goto out;
	}

	ret = of_property_count_elems_of_size(pdev->dev.of_node,
					      "clock-suppressible",
					      sizeof(*clk_suppressible));
	if (ret != total_num_clk) {
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: mismatch between number of clock and suppressible entries: %d != %d\n",
			 pcie_dev->rc_idx, total_num_clk, ret);
		return -EIO;
	}

	/* get clock suppressible info */
	clk_suppressible = devm_kcalloc(&pdev->dev, total_num_clk,
					sizeof(*clk_suppressible), GFP_KERNEL);
	if (!clk_suppressible)
		return -ENOMEM;

	ret = of_property_read_u32_array(pdev->dev.of_node,
					 "clock-suppressible",
					 clk_suppressible, total_num_clk);
	if (ret) {
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: failed to get clock suppressible info: ret: %d\n",
			 pcie_dev->rc_idx, ret);
		goto out;
	}

	/* setup array of PCIe clock info */
	clk = devm_kcalloc(&pdev->dev, total_num_clk, sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;

	/* Initially, pipe clk and clk both point to the beginning */
	pcie_dev->pipe_clk = pcie_dev->clk = clk;

	for (i = 0; i < total_num_clk; i++, clk++, bulk_clks++) {
		clk->name = bulk_clks->id;
		clk->hdl = bulk_clks->clk;
		clk->freq = *clk_freq++;
		clk->suppressible = *clk_suppressible++;

		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: %s: frequency: %d: suppressible: %d\n",
			 pcie_dev->rc_idx, clk->name, clk->freq,
			 clk->suppressible);
	}

	/*
	 * PCIe PIPE clock needs to be voted for independently from other PCIe
	 * clocks. Assumption is that PCIe pipe clocks come first in the list
	 * of clocks. The rest of the clocks will come after.
	 */
	if (!strcmp(pcie_dev->clk->name, "pcie_pipe_clk")) {
		pcie_dev->num_pipe_clk++;
		pcie_dev->clk++;
	} else {
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: could not find entry for pcie_pipe_clk\n",
			 pcie_dev->rc_idx);
		/* Mask the error when PCIe resources are managed by CESTA */
		if (!pcie_dev->pcie_sm)
			goto out;
	}

	pcie_dev->num_clk = total_num_clk - pcie_dev->num_pipe_clk;

	pcie_dev->rate_change_clk = clk_get(&pdev->dev, "pcie_rate_change_clk");
	if (IS_ERR(pcie_dev->rate_change_clk)) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: pcie_rate_change_clk is not present\n",
			 pcie_dev->rc_idx);
		pcie_dev->rate_change_clk = NULL;
	}

	pcie_dev->pipe_clk_mux = clk_get(&pdev->dev, "pcie_pipe_clk_mux");
	if (IS_ERR(pcie_dev->pipe_clk_mux)) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: pcie_pipe_clk_mux is not present\n",
			 pcie_dev->rc_idx);
		pcie_dev->pipe_clk_mux = NULL;
	}

	pcie_dev->pipe_clk_ext_src = clk_get(&pdev->dev,
					     "pcie_pipe_clk_ext_src");
	if (IS_ERR(pcie_dev->pipe_clk_ext_src)) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: pcie_pipe_clk_ext_src is not present\n",
			 pcie_dev->rc_idx);
		pcie_dev->pipe_clk_ext_src = NULL;
	}

	pcie_dev->phy_aux_clk_mux = clk_get(&pdev->dev, "pcie_phy_aux_clk_mux");
	if (IS_ERR(pcie_dev->phy_aux_clk_mux))
		pcie_dev->phy_aux_clk_mux = NULL;

	pcie_dev->phy_aux_clk_ext_src = clk_get(&pdev->dev,
					"pcie_phy_aux_clk_ext_src");
	if (IS_ERR(pcie_dev->phy_aux_clk_ext_src))
		pcie_dev->phy_aux_clk_ext_src = NULL;

	pcie_dev->ref_clk_src = clk_get(&pdev->dev, "pcie_ref_clk_src");
	if (IS_ERR(pcie_dev->ref_clk_src)) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: pcie_ref_clk_src is not present\n",
			 pcie_dev->rc_idx);
		pcie_dev->ref_clk_src = NULL;
	}

	pcie_dev->ahb_clk = clk_get(&pdev->dev, "pcie_cfg_ahb_clk");
	if (IS_ERR(pcie_dev->ahb_clk)) {
		pcie_dev->ahb_clk = NULL;
		PCIE_DBG(pcie_dev, "Clock ahb isn't available\n");
	}

	return 0;
out:
	return -EIO;
}

static int msm_pcie_get_vreg(struct msm_pcie_dev_t *pcie_dev)
{
	int i, len;
	struct platform_device *pdev = pcie_dev->pdev;
	const __be32 *prop;
	char prop_name[MAX_PROP_SIZE];

	for (i = 0; i < MSM_PCIE_MAX_VREG; i++) {
		struct msm_pcie_vreg_info_t *vreg_info = &pcie_dev->vreg[i];

		vreg_info->hdl = devm_regulator_get_optional(&pdev->dev,
						vreg_info->name);

		if (PTR_ERR(vreg_info->hdl) == -EPROBE_DEFER) {
			PCIE_DBG(pcie_dev, "EPROBE_DEFER for VReg:%s\n",
				vreg_info->name);
			return PTR_ERR(vreg_info->hdl);
		}

		if (IS_ERR(vreg_info->hdl)) {
			if (vreg_info->required && !pcie_dev->pcie_sm) {
				PCIE_DBG(pcie_dev, "Vreg %s doesn't exist\n",
					vreg_info->name);
				return PTR_ERR(vreg_info->hdl);
			}

			PCIE_DBG(pcie_dev, "Optional Vreg %s doesn't exist\n",
				vreg_info->name);
			vreg_info->hdl = NULL;
		} else {
			pcie_dev->vreg_n++;
			scnprintf(prop_name, MAX_PROP_SIZE,
				"qcom,%s-voltage-level", vreg_info->name);
			prop = of_get_property(pdev->dev.of_node,
						prop_name, &len);
			if (!prop || (len != (3 * sizeof(__be32)))) {
				PCIE_DBG(pcie_dev, "%s %s property\n",
					prop ? "invalid format" :
					"no", prop_name);
			} else {
				vreg_info->max_v = be32_to_cpup(&prop[0]);
				vreg_info->min_v = be32_to_cpup(&prop[1]);
				vreg_info->opt_mode =
					be32_to_cpup(&prop[2]);
			}

			if (!strcmp(vreg_info->name, "vreg-cx"))
				pcie_dev->cx_vreg = vreg_info;

			if (!strcmp(vreg_info->name, "vreg-mx"))
				pcie_dev->mx_vreg = vreg_info;
		}
	}

	pcie_dev->gdsc_core = devm_regulator_get(&pdev->dev, "gdsc-core-vdd");

	if (IS_ERR(pcie_dev->gdsc_core)) {
		PCIE_ERR(pcie_dev, "PCIe: RC%d: Failed to get %s GDSC-CORE:%ld\n",
			 pcie_dev->rc_idx, pdev->name,
			 PTR_ERR(pcie_dev->gdsc_core));
		if (PTR_ERR(pcie_dev->gdsc_core) == -EPROBE_DEFER)
			PCIE_DBG(pcie_dev, "PCIe: EPROBE_DEFER for %s GDSC-CORE\n",
				 pdev->name);
		if (!pcie_dev->pcie_sm)
			return PTR_ERR(pcie_dev->gdsc_core);
	}

	pcie_dev->gdsc_phy = devm_regulator_get(&pdev->dev, "gdsc-phy-vdd");

	if (IS_ERR(pcie_dev->gdsc_phy)) {
		PCIE_ERR(pcie_dev, "PCIe: RC%d: Failed to get %s GDSC-PHY:%ld\n",
			 pcie_dev->rc_idx, pdev->name,
			 PTR_ERR(pcie_dev->gdsc_phy));
		if (PTR_ERR(pcie_dev->gdsc_phy) == -EPROBE_DEFER) {
			PCIE_DBG(pcie_dev, "PCIe: EPROBE_DEFER for %s GDSC-PHY\n",
				pdev->name);
			return PTR_ERR(pcie_dev->gdsc_phy);
		}
	}

	return 0;
}

static int msm_pcie_get_reset(struct msm_pcie_dev_t *pcie_dev)
{
	int i;
	struct msm_pcie_reset_info_t *reset_info;

	for (i = 0; i < MSM_PCIE_MAX_RESET; i++) {
		reset_info = &pcie_dev->reset[i];
		reset_info->hdl = devm_reset_control_get(&pcie_dev->pdev->dev,
							reset_info->name);
		if (IS_ERR(reset_info->hdl)) {
			if (reset_info->required) {
				PCIE_DBG(pcie_dev,
					"Reset %s isn't available:%ld\n",
					reset_info->name,
					PTR_ERR(reset_info->hdl));

				return PTR_ERR(reset_info->hdl);
			}

			PCIE_DBG(pcie_dev, "Ignoring Reset %s\n",
				reset_info->name);
			reset_info->hdl = NULL;
		}
	}

	for (i = 0; i < MSM_PCIE_MAX_PIPE_RESET; i++) {
		reset_info = &pcie_dev->pipe_reset[i];
		reset_info->hdl = devm_reset_control_get(&pcie_dev->pdev->dev,
							reset_info->name);
		if (IS_ERR(reset_info->hdl)) {
			if (reset_info->required) {
				PCIE_DBG(pcie_dev,
					"Pipe Reset %s isn't available:%ld\n",
					reset_info->name,
					PTR_ERR(reset_info->hdl));
				return PTR_ERR(reset_info->hdl);
			}

			PCIE_DBG(pcie_dev, "Ignoring Pipe Reset %s\n",
				reset_info->name);
			reset_info->hdl = NULL;
		}
	}

	for (i = 0; i < MSM_PCIE_MAX_LINKDOWN_RESET; i++) {
		reset_info = &pcie_dev->linkdown_reset[i];
		reset_info->hdl = devm_reset_control_get(&pcie_dev->pdev->dev,
							reset_info->name);
		if (IS_ERR(reset_info->hdl)) {
			if (reset_info->required) {
				PCIE_DBG(pcie_dev,
					"Linkdown Reset %s isn't available:%ld\n",
					reset_info->name,
					PTR_ERR(reset_info->hdl));
				return PTR_ERR(reset_info->hdl);
			}

			PCIE_DBG(pcie_dev, "Ignoring Linkdown Reset %s\n",
				reset_info->name);
			reset_info->hdl = NULL;
		}
	}

	return 0;
}

static int msm_pcie_get_bw_scale(struct msm_pcie_dev_t *pcie_dev)
{
	int size = 0;
	struct platform_device *pdev = pcie_dev->pdev;

	of_get_property(pdev->dev.of_node, "qcom,bw-scale", &size);
	if (size) {
		pcie_dev->bw_scale = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
		if (!pcie_dev->bw_scale)
			return -ENOMEM;

		of_property_read_u32_array(pdev->dev.of_node, "qcom,bw-scale",
				(u32 *)pcie_dev->bw_scale, size / sizeof(u32));

		pcie_dev->bw_gen_max = size / sizeof(*pcie_dev->bw_scale);
	} else {
		PCIE_DBG(pcie_dev, "RC%d: bandwidth scaling is not supported\n",
			pcie_dev->rc_idx);
	}

	return 0;
}

static int msm_pcie_get_phy(struct msm_pcie_dev_t *pcie_dev)
{
	int ret, size = 0;
	struct platform_device *pdev = pcie_dev->pdev;

	of_get_property(pdev->dev.of_node, "qcom,phy-sequence", &size);
	if (!size) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: phy sequence is not present in DT\n",
			pcie_dev->rc_idx);
		return 0;
	}

	pcie_dev->phy_sequence = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!pcie_dev->phy_sequence)
		return -ENOMEM;

	pcie_dev->phy_len = size / sizeof(*pcie_dev->phy_sequence);

	ret = of_property_read_u32_array(pdev->dev.of_node,
				"qcom,phy-sequence",
				(unsigned int *)pcie_dev->phy_sequence,
				size / sizeof(pcie_dev->phy_sequence->offset));
	if (ret)
		return -EINVAL;

	return 0;
}

static int msm_pcie_get_phy_status_reg(struct msm_pcie_dev_t *pcie_dev)
{
	int ret, size = 0;
	struct platform_device *pdev = pcie_dev->pdev;

	PCIE_DBG(pcie_dev,
			"PCIe: RC%d: Enter\n",
			pcie_dev->rc_idx);
	of_get_property(pdev->dev.of_node, "qcom,phy-debug-reg", &size);
	if (!size) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: phy debug registers not present in DT\n",
			pcie_dev->rc_idx);
		pcie_dev->phy_debug_reg = NULL;
		return 0;
	}

	pcie_dev->phy_debug_reg = kmalloc(size, GFP_KERNEL);
	if (!pcie_dev->phy_debug_reg)
		return -ENOMEM;

	pcie_dev->phy_debug_reg_len = size / sizeof(*pcie_dev->phy_debug_reg);

	ret = of_property_read_u32_array(pdev->dev.of_node,
				"qcom,phy-debug-reg",
				(unsigned int *)pcie_dev->phy_debug_reg,
				size / sizeof(*pcie_dev->phy_debug_reg));
	if (ret) {
		kfree(pcie_dev->phy_debug_reg);
		pcie_dev->phy_debug_reg = NULL;
		return -EINVAL;
	}

	PCIE_DBG(pcie_dev,
			"PCIe: RC%d: no of phy dbg regs:%u size:%u\n",
			pcie_dev->rc_idx, size/sizeof(u32), size);
	return 0;
}

static int msm_pcie_get_parf_status_reg(struct msm_pcie_dev_t *pcie_dev)
{
	int ret, size = 0;
	struct platform_device *pdev = pcie_dev->pdev;

	PCIE_DBG(pcie_dev,
		"PCIe: RC%d: Enter\n", pcie_dev->rc_idx);

	of_get_property(pdev->dev.of_node, "qcom,parf-debug-reg", &size);
	if (!size) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: parf debug registers not present in DT\n",
			pcie_dev->rc_idx);
		pcie_dev->parf_debug_reg = NULL;
		return 0;
	}

	pcie_dev->parf_debug_reg = kmalloc(size, GFP_KERNEL);
	if (!pcie_dev->parf_debug_reg)
		return -ENOMEM;

	pcie_dev->parf_debug_reg_len = size / sizeof(u32);

	ret = of_property_read_u32_array(pdev->dev.of_node,
				"qcom,parf-debug-reg",
				(unsigned int *)pcie_dev->parf_debug_reg,
				size / sizeof(u32));
	if (ret) {
		kfree(pcie_dev->parf_debug_reg);
		pcie_dev->parf_debug_reg = NULL;
		return -EINVAL;
	}

	PCIE_DBG(pcie_dev,
			"PCIe: RC%d: no of parf dbg regs:%u size:%u\n",
			pcie_dev->rc_idx, size/sizeof(u32), size);
	return 0;
}

static int msm_pcie_get_dbi_status_reg(struct msm_pcie_dev_t *pcie_dev)
{
	int ret, size = 0;
	struct platform_device *pdev = pcie_dev->pdev;

	PCIE_DBG(pcie_dev,
			"PCIe: RC%d: Enter\n",
			pcie_dev->rc_idx);
	of_get_property(pdev->dev.of_node, "qcom,dbi-debug-reg", &size);
	if (!size) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: dbi debug registers not present in DT\n",
			pcie_dev->rc_idx);
		pcie_dev->dbi_debug_reg = NULL;
		return 0;
	}

	pcie_dev->dbi_debug_reg = kmalloc(size, GFP_KERNEL);
	if (!pcie_dev->dbi_debug_reg)
		return -ENOMEM;

	pcie_dev->dbi_debug_reg_len = size / sizeof(u32);

	ret = of_property_read_u32_array(pdev->dev.of_node,
				"qcom,dbi-debug-reg",
				(unsigned int *)pcie_dev->dbi_debug_reg,
				size / sizeof(u32));
	if (ret) {
		kfree(pcie_dev->dbi_debug_reg);
		pcie_dev->dbi_debug_reg = NULL;
		return -EINVAL;
	}

	PCIE_DBG(pcie_dev,
			"PCIe: RC%d: no of dbi dbg regs:%u size:%u\n",
			pcie_dev->rc_idx, size/sizeof(u32), size);
	return 0;
}

static int msm_pcie_get_iommu_map(struct msm_pcie_dev_t *pcie_dev)
{
	/* iommu map structure */
	struct {
		u32 bdf;
		u32 phandle;
		u32 smmu_sid;
		u32 smmu_sid_len;
	} *map;
	struct platform_device *pdev = pcie_dev->pdev;
	int i, size = 0;

	of_get_property(pdev->dev.of_node, "iommu-map", &size);
	if (!size) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: iommu-map is not present in DT.\n",
			pcie_dev->rc_idx);
		return 0;
	}

	map = kzalloc(size, GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	of_property_read_u32_array(pdev->dev.of_node,
		"iommu-map", (u32 *)map, size / sizeof(u32));

	pcie_dev->sid_info_len = size / (sizeof(*map));
	pcie_dev->sid_info = devm_kcalloc(&pdev->dev, pcie_dev->sid_info_len,
				sizeof(*pcie_dev->sid_info), GFP_KERNEL);
	if (!pcie_dev->sid_info) {
		kfree(map);
		return -ENOMEM;
	}

	for (i = 0; i < pcie_dev->sid_info_len; i++) {
		pcie_dev->sid_info[i].bdf = map[i].bdf;
		pcie_dev->sid_info[i].smmu_sid = map[i].smmu_sid;
		pcie_dev->sid_info[i].pcie_sid =
				pcie_dev->sid_info[i].smmu_sid -
				pcie_dev->smmu_sid_base;
	}

	kfree(map);

	return 0;
}

static int msm_pcie_get_gpio(struct msm_pcie_dev_t *pcie_dev)
{
	int i, ret;

	pcie_dev->gpio_n = 0;
	for (i = 0; i < MSM_PCIE_MAX_GPIO; i++) {
		struct msm_pcie_gpio_info_t *gpio_info = &pcie_dev->gpio[i];

		ret = of_get_named_gpio(pcie_dev->pdev->dev.of_node,
					gpio_info->name, 0);
		if (ret >= 0) {
			gpio_info->num = ret;
			pcie_dev->gpio_n++;
			PCIE_DBG(pcie_dev, "GPIO num for %s is %d\n",
				gpio_info->name, gpio_info->num);
		} else {
			if (gpio_info->required) {
				PCIE_ERR(pcie_dev,
					"Could not get required GPIO %s\n",
					gpio_info->name);
				return ret;
			}

			PCIE_DBG(pcie_dev, "Could not get optional GPIO %s\n",
				gpio_info->name);
		}
	}

	pcie_dev->wake_n = 0;
	if (pcie_dev->gpio[MSM_PCIE_GPIO_WAKE].num)
		pcie_dev->wake_n =
			gpio_to_irq(pcie_dev->gpio[MSM_PCIE_GPIO_WAKE].num);

	return 0;
}

static int msm_pcie_get_reg(struct msm_pcie_dev_t *pcie_dev)
{
	struct resource *res;
	struct msm_pcie_res_info_t *res_info;
	int i;

	for (i = 0; i < MSM_PCIE_MAX_RES; i++) {
		res_info = &pcie_dev->res[i];

		res = platform_get_resource_byname(pcie_dev->pdev,
						IORESOURCE_MEM, res_info->name);
		if (!res) {
			PCIE_ERR(pcie_dev,
				"PCIe: RC%d: no %s resource found.\n",
				pcie_dev->rc_idx, res_info->name);
		} else {
			PCIE_DBG(pcie_dev, "start addr for %s is %pa.\n",
				res_info->name,	&res->start);

			res_info->base = devm_ioremap(&pcie_dev->pdev->dev,
						res->start, resource_size(res));
			if (!res_info->base) {
				PCIE_ERR(pcie_dev,
					"PCIe: RC%d: can't remap %s.\n",
					pcie_dev->rc_idx, res_info->name);
				return -ENOMEM;
			}

			res_info->resource = res;
		}
	}

	pcie_dev->parf = pcie_dev->res[MSM_PCIE_RES_PARF].base;
	pcie_dev->phy = pcie_dev->res[MSM_PCIE_RES_PHY].base;
	pcie_dev->elbi = pcie_dev->res[MSM_PCIE_RES_ELBI].base;
	pcie_dev->iatu = pcie_dev->res[MSM_PCIE_RES_IATU].base;
	pcie_dev->dm_core = pcie_dev->res[MSM_PCIE_RES_DM_CORE].base;
	pcie_dev->conf = pcie_dev->res[MSM_PCIE_RES_CONF].base;
	pcie_dev->pcie_sm = pcie_dev->res[MSM_PCIE_RES_SM].base;
	pcie_dev->mhi = pcie_dev->res[MSM_PCIE_RES_MHI].base;
	pcie_dev->tcsr = pcie_dev->res[MSM_PCIE_RES_TCSR].base;
	pcie_dev->rumi = pcie_dev->res[MSM_PCIE_RES_RUMI].base;

	return 0;
}

static int msm_pcie_get_tcsr_values(struct msm_pcie_dev_t *dev,
					struct platform_device *pdev)
{
	int size = 0, ret = 0;

	of_get_property(pdev->dev.of_node, "qcom,tcsr", &size);

	if (!size) {
		PCIE_DBG(dev, "PCIe: RC%d: tcsr is not present in DT\n",
			dev->rc_idx);
		return 0;
	}

	dev->tcsr_config = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);

	if (!dev->tcsr_config)
		return -ENOMEM;

	dev->tcsr_len = size / sizeof(*dev->tcsr_config);

	of_property_read_u32_array(pdev->dev.of_node,
		"qcom,tcsr",
		(unsigned int *)dev->tcsr_config,
		size / sizeof(dev->tcsr_config->offset));

	return ret;
}

static int msm_pcie_get_resources(struct msm_pcie_dev_t *dev,
					struct platform_device *pdev)
{
	int i, ret = 0;
	int num;
	struct msm_pcie_irq_info_t *irq_info;

	PCIE_DBG(dev, "PCIe: RC%d: entry\n", dev->rc_idx);

	ret = msm_pcie_get_reg(dev);
	if (ret)
		return ret;

	dev->icc_path = of_icc_get(&pdev->dev, "icc_path");
	if (IS_ERR(dev->icc_path)) {
		ret = dev->icc_path ? PTR_ERR(dev->icc_path) : -EINVAL;

		PCIE_ERR(dev, "PCIe: RC%d: failed to get ICC path: %d\n",
			dev->rc_idx, ret);

		if (!dev->rumi)
			return ret;
	}

	for (i = 0; i < MSM_PCIE_MAX_IRQ; i++) {
		irq_info = &dev->irq[i];

		num = platform_get_irq_byname(pdev, irq_info->name);

		if (num < 0) {
			PCIE_DBG(dev,
			"PCIe: RC%d: can't find IRQ # for %s. ret %d\n",
					dev->rc_idx, irq_info->name, num);
		} else {
			irq_info->num = num;
			PCIE_DBG(dev, "IRQ # for %s is %d.\n", irq_info->name,
					irq_info->num);
		}
	}

	ret = msm_pcie_get_tcsr_values(dev, pdev);
	if (ret)
		return ret;

	ret = msm_pcie_get_clk(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_vreg(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_reset(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_bw_scale(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_phy(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_iommu_map(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_gpio(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_parf_status_reg(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_dbi_status_reg(dev);
	if (ret)
		return ret;

	ret = msm_pcie_get_phy_status_reg(dev);
	if (ret)
		return ret;

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return 0;
}

static void msm_pcie_release_resources(struct msm_pcie_dev_t *dev)
{

	dev->parf = NULL;
	dev->elbi = NULL;
	dev->iatu = NULL;
	dev->dm_core = NULL;
	dev->conf = NULL;
	dev->pcie_sm = NULL;
	dev->mhi = NULL;
	dev->tcsr = NULL;
	dev->rumi = NULL;
	kfree(dev->parf_debug_reg);
	kfree(dev->dbi_debug_reg);
	kfree(dev->phy_debug_reg);
	dev->parf_debug_reg = NULL;
	dev->dbi_debug_reg = NULL;
	dev->phy_debug_reg = NULL;

}

static void msm_pcie_scale_link_bandwidth(struct msm_pcie_dev_t *pcie_dev,
					u16 target_link_speed)
{
	struct msm_pcie_bw_scale_info_t *bw_scale;
	u32 index = target_link_speed - PCI_EXP_LNKCTL2_TLS_2_5GT;
	int ret;

	if (!pcie_dev->bw_scale)
		return;

	if (index >= pcie_dev->bw_gen_max) {
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d: invalid target link speed: %d\n",
			pcie_dev->rc_idx, target_link_speed);
		return;
	}

	/* Use CESTA to scale the resources */
	if (pcie_dev->pcie_sm) {

		/* If CESTA already voted for required speed then bail out */
		if (target_link_speed + PERF_LVL_L1SS ==
				msm_pcie_cesta_map[D0_STATE][POWER_STATE_1])
			return;

		msm_pcie_cesta_map_save(target_link_speed);
		ret = msm_pcie_cesta_map_apply(pcie_dev, D0_STATE);
		if (ret)
			PCIE_ERR(pcie_dev, "Failed to move to D0 state %d\n",
									ret);
		return;
	}

	bw_scale = &pcie_dev->bw_scale[index];

	if (pcie_dev->cx_vreg)
		regulator_set_voltage(pcie_dev->cx_vreg->hdl,
					bw_scale->cx_vreg_min,
					pcie_dev->cx_vreg->max_v);

	if (pcie_dev->mx_vreg)
		regulator_set_voltage(pcie_dev->mx_vreg->hdl,
					bw_scale->mx_vreg_min,
					pcie_dev->mx_vreg->max_v);

	if (pcie_dev->rate_change_clk)
		clk_set_rate(pcie_dev->rate_change_clk,
				bw_scale->rate_change_freq);
}

static int msm_pcie_link_train(struct msm_pcie_dev_t *dev)
{
	int link_check_count = 0;
	uint32_t val, link_status;

	msm_pcie_write_reg_field(dev->dm_core,
		PCIE_GEN3_GEN2_CTRL, 0x1f00, 1);

	/* Controller settings related to PCIe PHY */
	msm_pcie_config_controller_phy(dev);

	/* configure PCIe preset */
	msm_pcie_config_core_preset(dev);

	if (dev->target_link_speed)
		msm_pcie_write_reg_field(dev->dm_core,
			PCIE20_CAP + PCI_EXP_LNKCTL2,
			PCI_EXP_LNKCTL2_TLS, dev->target_link_speed);

	/* set max tlp read size */
	msm_pcie_write_reg_field(dev->dm_core, PCIE20_DEVICE_CONTROL_STATUS,
				0x7000, dev->tlp_rd_size);

	/* enable link training */
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_LTSSM, 0, BIT(8));

	PCIE_DBG(dev, "%s", "check if link is up\n");

	/* Wait for up to 100ms for the link to come up */
	do {
		usleep_range(LINK_UP_TIMEOUT_US_MIN, LINK_UP_TIMEOUT_US_MAX);
		val =  readl_relaxed(dev->elbi + PCIE20_ELBI_SYS_STTS);
		PCIE_DBG(dev, "PCIe RC%d: LTSSM_STATE: %s\n",
			dev->rc_idx, TO_LTSSM_STR((val >> 12) & 0x3f));
	} while ((!(val & XMLH_LINK_UP) || !msm_pcie_dll_link_active(dev))
		&& (link_check_count++ < dev->link_check_max_count));

	if ((val & XMLH_LINK_UP) && msm_pcie_dll_link_active(dev)) {
		PCIE_DBG(dev, "Link is up after %d checkings\n",
			link_check_count);
		PCIE_INFO(dev, "PCIe RC%d link initialized\n", dev->rc_idx);
	} else {
#if IS_ENABLED(CONFIG_I2C)
		if (dev->i2c_ctrl.client && dev->i2c_ctrl.client_i2c_dump_regs)
			dev->i2c_ctrl.client_i2c_dump_regs(&dev->i2c_ctrl);
#endif
		PCIE_INFO(dev, "PCIe: Assert the reset of endpoint of RC%d.\n",
			dev->rc_idx);
		msm_pcie_config_perst(dev, true);
		PCIE_ERR(dev, "PCIe RC%d link initialization failed\n",
			dev->rc_idx);
		return MSM_PCIE_ERROR;
	}

	link_status = readl_relaxed(dev->dm_core + PCIE20_CAP_LINKCTRLSTATUS);

	dev->current_link_speed = (link_status >> 16) & PCI_EXP_LNKSTA_CLS;
	dev->current_link_width = ((link_status >> 16) & PCI_EXP_LNKSTA_NLW) >>
				   PCI_EXP_LNKSTA_NLW_SHIFT;
	PCIE_DBG(dev, "PCIe: RC%d: Link is up at Gen%dX%d\n",
		 dev->rc_idx, dev->current_link_speed,
		 dev->current_link_width);

	if ((!dev->enumerated) && dev->panic_genspeed_mismatch &&
	    dev->target_link_speed &&
	    dev->target_link_speed != dev->current_link_speed)
		panic("PCIe: RC%d: Gen-speed mismatch:%d, expected:%d\n",
		      dev->rc_idx, dev->current_link_speed,
		      dev->target_link_speed);

	/*
	 * If the link up GEN speed is less than the max/default supported,
	 * then scale the resources accordingly.
	 */
	if (dev->bw_scale && dev->current_link_speed < dev->bw_gen_max) {
		u32 index;
		struct msm_pcie_bw_scale_info_t *bw_scale;

		index = dev->current_link_speed - PCI_EXP_LNKCTL2_TLS_2_5GT;
		if (index >= dev->bw_gen_max) {
			PCIE_ERR(dev,
				"PCIe: RC%d: unsupported gen speed: %d\n",
				dev->rc_idx, dev->current_link_speed);
			return 0;
		}

		bw_scale = &dev->bw_scale[index];

		msm_pcie_write_reg_field(dev->dm_core, PCIE20_CAP +
					PCI_EXP_LNKCTL2, PCI_EXP_LNKCTL2_TLS,
					dev->current_link_speed);
		msm_pcie_scale_link_bandwidth(dev, dev->current_link_speed);
	}

	return 0;
}

static int msm_pcie_check_ep_access(struct msm_pcie_dev_t *dev,
					unsigned long ep_up_timeout)
{
	int ret = 0;

	/* check endpoint configuration space is accessible */
	while (time_before(jiffies, ep_up_timeout)) {
		if (readl_relaxed(dev->conf) != PCIE_LINK_DOWN)
			break;
		usleep_range(EP_UP_TIMEOUT_US_MIN, EP_UP_TIMEOUT_US_MAX);
	}

	if (readl_relaxed(dev->conf) != PCIE_LINK_DOWN) {
		PCIE_DBG(dev,
			"PCIe: RC%d: endpoint config space is accessible\n",
			dev->rc_idx);
	} else {
		PCIE_ERR(dev,
			"PCIe: RC%d: endpoint config space is not accessible\n",
			dev->rc_idx);
		dev->link_status = MSM_PCIE_LINK_DISABLED;
		dev->power_on = false;
		dev->link_turned_off_counter++;
		ret = -ENODEV;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_I2C)
/* write 32-bit value to 24 bit register */
static int ntn3_i2c_write(struct i2c_client *client, u32 reg_addr,
			      u32 reg_val)
{
	int ret;
	u8 msg_buf[7];
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.len = 7;
	msg.flags = 0;

	/* Big Endian for reg addr */
	msg_buf[0] = (u8)(reg_addr >> 16);
	msg_buf[1] = (u8)(reg_addr >> 8);
	msg_buf[2] = (u8)reg_addr;

	/* Little Endian for reg val */
	msg_buf[3] = (u8)(reg_val);
	msg_buf[4] = (u8)(reg_val >> 8);
	msg_buf[5] = (u8)(reg_val >> 16);
	msg_buf[6] = (u8)(reg_val >> 24);

	msg.buf = msg_buf;
	ret = i2c_transfer(client->adapter, &msg, 1);
	return ret == 1 ? 0 : ret;
}

/* read 32 bit value from 24 bit reg addr */
static int ntn3_i2c_read(struct i2c_client *client, u32 reg_addr,
			     u32 *reg_val)
{
	int ret;
	u8 wr_data[3], rd_data[4];
	struct i2c_msg msg[2];

	msg[0].addr = client->addr;
	msg[0].len = 3;
	msg[0].flags = 0;

	// Big Endian for reg addr
	wr_data[0] = (u8)(reg_addr >> 16);
	wr_data[1] = (u8)(reg_addr >> 8);
	wr_data[2] = (u8)reg_addr;

	msg[0].buf = wr_data;

	msg[1].addr = client->addr;
	msg[1].len = 4;
	msg[1].flags = I2C_M_RD;

	msg[1].buf = rd_data;

	ret = i2c_transfer(client->adapter, &msg[0], 2);
	if (ret != 2)
		return ret;

	*reg_val = (rd_data[3] << 24) | (rd_data[2] << 16) | (rd_data[1] << 8) |
		   rd_data[0];

	return 0;
}

static int ntn3_ep_reset_ctrl(struct pcie_i2c_ctrl *i2c_ctrl, bool reset)
{
	int ret, rd_val;
	struct msm_pcie_dev_t *pcie_dev = container_of(i2c_ctrl,
						       struct msm_pcie_dev_t,
						       i2c_ctrl);

	if (!i2c_ctrl->client_i2c_write || !i2c_ctrl->client_i2c_read)
		return -EOPNOTSUPP;

	/* set NTN3 GPIO as output */
	ret = i2c_ctrl->client_i2c_read(i2c_ctrl->client,
					i2c_ctrl->gpio_config_reg, &rd_val);
	if (ret) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: gpio config reg read failed : %d\n",
			 pcie_dev->rc_idx, ret);
		return ret;
	}

	rd_val &= ~i2c_ctrl->ep_reset_gpio_mask;
	i2c_ctrl->client_i2c_write(i2c_ctrl->client, i2c_ctrl->gpio_config_reg,
				   rd_val);

	/* read back to flush write - config gpio */
	ret = i2c_ctrl->client_i2c_read(i2c_ctrl->client,
					i2c_ctrl->gpio_config_reg, &rd_val);
	if (ret) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: gpio config reg read failed : %d\n",
			 pcie_dev->rc_idx, ret);
		return ret;
	}

	ret = i2c_ctrl->client_i2c_read(i2c_ctrl->client,
					i2c_ctrl->ep_reset_reg, &rd_val);
	if (ret) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: ep_reset_gpio read failed : %d\n",
			 pcie_dev->rc_idx, ret);
		return ret;
	}

	rd_val &= ~i2c_ctrl->ep_reset_gpio_mask;
	i2c_ctrl->client_i2c_write(i2c_ctrl->client, i2c_ctrl->ep_reset_reg,
				   rd_val);

	/* read back to flush write - reset gpio */
	ret = i2c_ctrl->client_i2c_read(i2c_ctrl->client,
					i2c_ctrl->ep_reset_reg, &rd_val);
	if (ret) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: ep_reset_gpio read failed : %d\n",
			 pcie_dev->rc_idx, ret);
		return ret;
	}

	/* ep reset done */
	if (reset)
		return 0;

	/* toggle (0 -> 1) reset gpios to bring eps out of reset */
	rd_val |= i2c_ctrl->ep_reset_gpio_mask;
	i2c_ctrl->client_i2c_write(i2c_ctrl->client, i2c_ctrl->ep_reset_reg,
				   rd_val);

	/* read back to flush write - reset gpio */
	ret = i2c_ctrl->client_i2c_read(i2c_ctrl->client,
					i2c_ctrl->ep_reset_reg, &rd_val);
	if (ret) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: ep_reset_gpio read failed : %d\n",
			 pcie_dev->rc_idx, ret);
		return ret;
	}

	return 0;
}

static void ntn3_dump_regs(struct pcie_i2c_ctrl *i2c_ctrl)
{
	int i, val;
	struct msm_pcie_dev_t *pcie_dev = container_of(i2c_ctrl,
						       struct msm_pcie_dev_t,
						       i2c_ctrl);

	if (!i2c_ctrl->client_i2c_read || !i2c_ctrl->dump_reg_count)
		return;

	PCIE_DUMP(pcie_dev, "PCIe: RC%d: NTN3 reg dumps\n", pcie_dev->rc_idx);

	for (i = 0; i < i2c_ctrl->dump_reg_count; i++) {
		i2c_ctrl->client_i2c_read(i2c_ctrl->client,
					  i2c_ctrl->dump_regs[i], &val);
		PCIE_DUMP(pcie_dev, "PCIe: RC%d: reg: 0x%04x val: 0x%08x\n",
			  pcie_dev->rc_idx, i2c_ctrl->dump_regs[i], val);
	}
}

static void ntn3_de_emphasis_wa(struct pcie_i2c_ctrl *i2c_ctrl)
{
	int i, val, ret, rd_val;
	struct msm_pcie_dev_t *pcie_dev = container_of(i2c_ctrl,
						       struct msm_pcie_dev_t,
						       i2c_ctrl);
	ret = i2c_ctrl->client_i2c_read(i2c_ctrl->client,
			 i2c_ctrl->version_reg, &rd_val);
	if (ret) {
		PCIE_DBG(pcie_dev, "PCIe: RC%d: gpio version reg read failed : %d\n",
				 pcie_dev->rc_idx, ret);
	}
	i2c_ctrl->force_i2c_setting = of_property_read_bool(i2c_ctrl->client->dev.of_node,
					  "force-i2c-setting");
	rd_val &= CHECK_NTN3_VERSION_MASK;
	PCIE_DBG(pcie_dev, "PCIe: RC%d: NTN3 Version reg:0x%x and force-i2c-setting is %s enabled",
		 pcie_dev->rc_idx, rd_val, i2c_ctrl->force_i2c_setting ? "" : "not");
	if (rd_val == NTN3_CHIP_VERSION_1 || i2c_ctrl->force_i2c_setting) {
		PCIE_DBG(pcie_dev, "PCIe: RC%d: NTN3 reg update\n", pcie_dev->rc_idx);

		for (i = 0; i < i2c_ctrl->reg_update_count; i++) {
			i2c_ctrl->client_i2c_write(i2c_ctrl->client, i2c_ctrl->reg_update[i].offset,
						   i2c_ctrl->reg_update[i].val);
			/*Read to make sure writes are completed*/
			i2c_ctrl->client_i2c_read(i2c_ctrl->client, i2c_ctrl->reg_update[i].offset,
						   &val);
			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: NTN3 reg off:0x%x wr_val:0x%x rd_val:0x%x\n",
				pcie_dev->rc_idx, i2c_ctrl->reg_update[i].offset,
				i2c_ctrl->reg_update[i].val, val);
		}
	}

	for (i = 0; i < i2c_ctrl->switch_reg_update_count; i++) {
		i2c_ctrl->client_i2c_write(i2c_ctrl->client, i2c_ctrl->switch_reg_update[i].offset,
				i2c_ctrl->switch_reg_update[i].val);
		/*Read to make sure writes are completed*/
		i2c_ctrl->client_i2c_read(i2c_ctrl->client, i2c_ctrl->switch_reg_update[i].offset,
				&val);
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: NTN3 reg off:0x%x wr_val:0x%x rd_val:0x%x\n",
			 pcie_dev->rc_idx, i2c_ctrl->switch_reg_update[i].offset,
			 i2c_ctrl->switch_reg_update[i].val, val);
	}
}
#endif

static int msm_pcie_enable_link(struct msm_pcie_dev_t *dev)
{
	int ret = 0;
	uint32_t val;
	unsigned long ep_up_timeout = 0;

	/* configure PCIe to RC mode */
	msm_pcie_write_reg(dev->parf, PCIE20_PARF_DEVICE_TYPE, 0x4);

	/* enable l1 mode, clear bit 5 (REQ_NOT_ENTR_L1) */
	if (dev->l1_supported)
		msm_pcie_write_mask(dev->parf + PCIE20_PARF_PM_CTRL, BIT(5), 0);

	/* enable PCIe clocks and resets */
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_PHY_CTRL, BIT(0), 0);

	/* change DBI base address */
	msm_pcie_write_reg(dev->parf, PCIE20_PARF_DBI_BASE_ADDR, 0);

	msm_pcie_write_reg(dev->parf, PCIE20_PARF_SYS_CTRL, 0x365E);

	msm_pcie_write_mask(dev->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL,
				0, BIT(4));

	/* enable selected IRQ */
	msm_pcie_write_reg(dev->parf, PCIE20_PARF_INT_ALL_MASK, 0);

	msm_pcie_write_mask(dev->parf + PCIE20_PARF_INT_ALL_MASK, 0,
				BIT(MSM_PCIE_INT_EVT_LINK_DOWN) |
				BIT(MSM_PCIE_INT_EVT_L1SUB_TIMEOUT) |
				BIT(MSM_PCIE_INT_EVT_AER_LEGACY) |
				BIT(MSM_PCIE_INT_EVT_AER_ERR) |
				BIT(MSM_PCIE_INT_EVT_BRIDGE_FLUSH_N) |
				BIT(MSM_PCIE_INT_EVT_MSI_0) |
				BIT(MSM_PCIE_INT_EVT_MSI_1) |
				BIT(MSM_PCIE_INT_EVT_MSI_2) |
				BIT(MSM_PCIE_INT_EVT_MSI_3) |
				BIT(MSM_PCIE_INT_EVT_MSI_4) |
				BIT(MSM_PCIE_INT_EVT_MSI_5) |
				BIT(MSM_PCIE_INT_EVT_MSI_6) |
				BIT(MSM_PCIE_INT_EVT_MSI_7));

	PCIE_INFO(dev, "PCIe: RC%d: PCIE20_PARF_INT_ALL_MASK: 0x%x\n",
		dev->rc_idx,
		readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_MASK));

	msm_pcie_write_reg(dev->parf, PCIE20_PARF_SLV_ADDR_SPACE_SIZE,
				dev->slv_addr_space_size);

	if (dev->pcie_halt_feature_dis) {
		/* Disable PCIe Wr halt window */
		val = readl_relaxed(dev->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT);
		msm_pcie_write_reg(dev->parf, PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT,
				(~BIT(31)) & val);

		/* Disable PCIe Rd halt window */
		val = readl_relaxed(dev->parf + PCIE20_PARF_AXI_MSTR_RD_ADDR_HALT);
		msm_pcie_write_reg(dev->parf, PCIE20_PARF_AXI_MSTR_RD_ADDR_HALT,
			(~BIT(0)) & val);
	} else {
		val = dev->wr_halt_size ? dev->wr_halt_size :
			readl_relaxed(dev->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT);
		msm_pcie_write_reg(dev->parf, PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT,
				BIT(31) | val);
	}

	if (dev->pcie_bdf_halt_dis) {
		val = readl_relaxed(dev->parf + PCIE20_PCIE_PARF_AXI_MSTR_WR_NS_BDF_HALT);
		msm_pcie_write_reg(dev->parf, PCIE20_PCIE_PARF_AXI_MSTR_WR_NS_BDF_HALT,
				(~BIT(0)) & val);
	}

	/* init tcsr */
	if (dev->tcsr_config)
		pcie_tcsr_init(dev);

	/* init PCIe PHY */
	ret = pcie_phy_init(dev);
	if (ret)
		return ret;

	/* switch phy aux clock source from xo to phy aux clk */
	if (dev->phy_aux_clk_mux && dev->phy_aux_clk_ext_src)
		clk_set_parent(dev->phy_aux_clk_mux, dev->phy_aux_clk_ext_src);

	usleep_range(dev->ep_latency * 1000, dev->ep_latency * 1000);

	if (dev->gpio[MSM_PCIE_GPIO_EP].num)
		gpio_set_value(dev->gpio[MSM_PCIE_GPIO_EP].num,
				dev->gpio[MSM_PCIE_GPIO_EP].on);

	dev->link_width_max =
		(readl_relaxed(dev->dm_core + PCIE20_CAP + PCI_EXP_LNKCAP) &
			       PCI_EXP_LNKCAP_MLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
	PCIE_DBG(dev, "PCIe: RC%d: Maximum supported link width is %d\n",
		 dev->rc_idx, dev->link_width_max);

	if (dev->target_link_width) {
		ret = msm_pcie_set_link_width(dev, dev->target_link_width <<
					      PCI_EXP_LNKSTA_NLW_SHIFT);
		if (ret)
			return ret;
	}

	/* Disable override for fal10_veto logic to de-assert Qactive signal */
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_CFG_BITS_3, BIT(0), 0);

	/**
	 * configure LANE_SKEW_OFF BIT-5 and PARF_CFG_BITS_3 BIT-8 to support
	 * dynamic link width upscaling.
	 */
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_CFG_BITS_3, 0, BIT(8));
	msm_pcie_write_mask(dev->dm_core + PCIE20_LANE_SKEW_OFF, 0, BIT(5));

	/* override the vendor id */
	if (dev->device_vendor_id) {
		msm_pcie_write_mask(dev->dm_core + PCIE_GEN3_MISC_CONTROL, 1, BIT(0));
		msm_pcie_write_reg(dev->dm_core, 0x0, dev->device_vendor_id);
		msm_pcie_write_mask(dev->dm_core + PCIE_GEN3_MISC_CONTROL, 0, BIT(0));
	}

	/* de-assert PCIe reset link to bring EP out of reset */

	PCIE_INFO(dev, "PCIe: Release the reset of endpoint of RC%d.\n",
		dev->rc_idx);
	msm_pcie_config_perst(dev, false);
	usleep_range(dev->perst_delay_us_min, dev->perst_delay_us_max);

	ep_up_timeout = jiffies + usecs_to_jiffies(EP_UP_TIMEOUT_US);

#if IS_ENABLED(CONFIG_I2C)
	if (dev->i2c_ctrl.client && dev->i2c_ctrl.client_i2c_de_emphasis_wa) {
		dev->i2c_ctrl.client_i2c_de_emphasis_wa(&dev->i2c_ctrl);
		msleep(20);
	}
#endif

	ret = msm_pcie_link_train(dev);
	if (ret)
		return ret;

	dev->link_status = MSM_PCIE_LINK_ENABLED;
	dev->power_on = true;
	dev->suspending = false;
	dev->link_turned_on_counter++;

	if (dev->switch_latency) {
		PCIE_DBG(dev, "switch_latency: %dms\n",
			dev->switch_latency);
		if (dev->switch_latency <= SWITCH_DELAY_MAX)
			usleep_range(dev->switch_latency * 1000,
				dev->switch_latency * 1000);
		else
			msleep(dev->switch_latency);
	}

	msm_pcie_config_sid(dev);
	msm_pcie_config_controller(dev);

	ret = msm_pcie_check_ep_access(dev, ep_up_timeout);

	return ret;
}

static int msm_pcie_enable_cesta(struct msm_pcie_dev_t *dev)
{
	int ret = 0;

	if (dev->pcie_sm) {
		/*
		 * Make sure that resources are scaled to link up in max
		 * possible Gen speed and scale down the resources if link
		 * up happens in lower speeds.
		 */
		msm_pcie_cesta_map_save(dev->bw_gen_max);

		ret = msm_pcie_cesta_map_apply(dev, D0_STATE);
		if (ret)
			PCIE_ERR(dev, "Fail to go to D0 State %d\n", ret);
	}

	return ret;
}

static void msm_pcie_disable_cesta(struct msm_pcie_dev_t *dev)
{
	int ret = 0;

	if (dev->pcie_sm) {
		ret = msm_pcie_cesta_map_apply(dev, D3COLD_STATE);
		if (ret)
			PCIE_ERR(dev, "Fail to move to D3 cold state %d\n",
									ret);
	}
}

static void msm_pcie_parf_cesta_config(struct msm_pcie_dev_t *dev)
{
	u32 cesta_config_bits;

	/* Propagate l1ss timeout and clkreq signals to CESTA */
	if (dev->pcie_sm) {

		cesta_config_bits = PARF_CESTA_CLKREQ_SEL |
			PARF_CESTA_L1SUB_TIMEOUT_EXT_INT_EN |
			readl_relaxed(dev->parf + dev->pcie_parf_cesta_config);

		/* Set clkreq to be accessed by CESTA */
		msm_pcie_write_reg(dev->parf, dev->pcie_parf_cesta_config,
							cesta_config_bits);
	} else {
		/*
		 * This is currently required only for platforms where clkreq
		 * signal is routed to CESTA by default, CESTA is not enabled.
		 */
		msm_pcie_write_reg_field(dev->parf,
				dev->pcie_parf_cesta_config,
					PARF_CESTA_CLKREQ_SEL, 0);
	}
}

static int msm_pcie_enable(struct msm_pcie_dev_t *dev)
{
	int ret = 0;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	dev->prevent_l1 = 0;
	dev->debugfs_l1 = false;

	mutex_lock(&dev->setup_lock);

	if (dev->link_status == MSM_PCIE_LINK_ENABLED) {
		PCIE_ERR(dev, "PCIe: the link of RC%d is already enabled\n",
			dev->rc_idx);
		goto out;
	}

	/* assert PCIe reset link to keep EP in reset */

	PCIE_INFO(dev, "PCIe: Assert the reset of endpoint of RC%d.\n",
		dev->rc_idx);
	msm_pcie_config_perst(dev, true);
	usleep_range(dev->perst_delay_us_min, dev->perst_delay_us_max);

	/* enable power */
	ret = msm_pcie_vreg_init(dev);
	if (ret)
		goto out;

	/* enable core, phy gdsc */
	ret = msm_pcie_gdsc_init(dev);
	if (ret)
		goto gdsc_fail;

	/* enable clocks */
	ret = msm_pcie_clk_init(dev);
	/* ensure that changes propagated to the hardware */
	wmb();
	if (ret)
		goto clk_fail;

	/* Use CESTA to turn on the resources */
	ret = msm_pcie_enable_cesta(dev);
	if (ret)
		goto reset_fail;

	/* reset pcie controller and phy */
	ret = msm_pcie_core_phy_reset(dev);
	/* ensure that changes propagated to the hardware */
	wmb();
	if (ret)
		goto reset_fail;

	/* Configure clkreq, l1ss sleep timeout access to CESTA */
	if (dev->pcie_parf_cesta_config)
		msm_pcie_parf_cesta_config(dev);

	/* RUMI PCIe reset sequence */
	if (dev->rumi_init)
		dev->rumi_init(dev);

	ret = msm_pcie_enable_link(dev);
	if (ret)
		goto link_fail;

	if (dev->no_client_based_bw_voting)
		msm_pcie_icc_vote(dev, dev->current_link_speed, dev->current_link_width, false);

	if (dev->enumerated) {
		if (!dev->lpi_enable)
			msm_msi_config(dev_get_msi_domain(&dev->dev->dev));
		msm_pcie_config_link_pm(dev, true);
	}

#if IS_ENABLED(CONFIG_I2C)
	/* Bring EP out of reset*/
	if (dev->i2c_ctrl.client && dev->i2c_ctrl.client_i2c_reset) {
		dev->i2c_ctrl.client_i2c_reset(&dev->i2c_ctrl, false);
		PCIE_DBG(dev,
			 "PCIe: Bring EPs out of reset and then wait for link training.\n");
		msleep(200);
		PCIE_DBG(dev, "PCIe: Finish EPs link training wait.\n");
	}
#endif
	goto out;

link_fail:
	if (msm_pcie_keep_resources_on & BIT(dev->rc_idx))
		goto out;

	if (dev->gpio[MSM_PCIE_GPIO_EP].num)
		gpio_set_value(dev->gpio[MSM_PCIE_GPIO_EP].num,
				1 - dev->gpio[MSM_PCIE_GPIO_EP].on);

	if (dev->phy_power_down_offset)
		msm_pcie_write_reg(dev->phy, dev->phy_power_down_offset, 0);

	/* Use CESTA to turn off the resources */
	msm_pcie_disable_cesta(dev);

	msm_pcie_pipe_clk_deinit(dev);
reset_fail:

	msm_pcie_clk_deinit(dev);
clk_fail:

	msm_pcie_gdsc_deinit(dev);
gdsc_fail:

	msm_pcie_vreg_deinit(dev);
out:
	mutex_unlock(&dev->setup_lock);

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);

	return ret;
}

static void msm_pcie_disable(struct msm_pcie_dev_t *dev)
{
	int ret;

	PCIE_DBG(dev, "RC%d: entry\n", dev->rc_idx);

	mutex_lock(&dev->setup_lock);

	if (!dev->power_on) {
		PCIE_DBG(dev,
			"PCIe: the link of RC%d is already power down.\n",
			dev->rc_idx);
		mutex_unlock(&dev->setup_lock);
		return;
	}

	/* suspend access to MSI register. resume access in msm_msi_config */
	if (!dev->lpi_enable)
		msm_msi_config_access(dev_get_msi_domain(&dev->dev->dev),
				      false);

	dev->link_status = MSM_PCIE_LINK_DISABLED;
	dev->power_on = false;
	dev->link_turned_off_counter++;

#if IS_ENABLED(CONFIG_I2C)
	/* assert reset on eps */
	if (dev->i2c_ctrl.client && dev->i2c_ctrl.client_i2c_reset)
		dev->i2c_ctrl.client_i2c_reset(&dev->i2c_ctrl, true);
#endif

	PCIE_INFO(dev, "PCIe: Assert the reset of endpoint of RC%d.\n",
		dev->rc_idx);

	msm_pcie_config_perst(dev, true);

	if (dev->phy_power_down_offset)
		msm_pcie_write_reg(dev->phy, dev->phy_power_down_offset, 0);

	msm_pcie_write_mask(dev->parf + PCIE20_PARF_PHY_CTRL, 0,
				BIT(0));

	/* Enable override for fal10_veto logic to assert Qactive signal.*/
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_CFG_BITS_3, 0, BIT(0));

	/* Use CESTA to turn off the resources */
	if (dev->pcie_sm) {
		ret = msm_pcie_cesta_map_apply(dev, D3COLD_STATE);
		if (ret)
			PCIE_ERR(dev, "Failed to move to D3 cold state %d\n",
									ret);
	}

	msm_pcie_clk_deinit(dev);
	msm_pcie_gdsc_deinit(dev);
	msm_pcie_vreg_deinit(dev);
	msm_pcie_pipe_clk_deinit(dev);

	if (dev->gpio[MSM_PCIE_GPIO_EP].num)
		gpio_set_value(dev->gpio[MSM_PCIE_GPIO_EP].num,
				1 - dev->gpio[MSM_PCIE_GPIO_EP].on);

	mutex_unlock(&dev->setup_lock);

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);
}

static int msm_pcie_config_device_info(struct pci_dev *pcidev, void *pdev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *) pdev;
	struct msm_pcie_device_info *dev_info;
	int ret;

	PCIE_DBG(pcie_dev,
		"PCI device found: vendor-id:0x%x device-id:0x%x\n",
		pcidev->vendor, pcidev->device);

	if (pci_pcie_type(pcidev) == PCI_EXP_TYPE_ENDPOINT) {
		dev_info = kzalloc(sizeof(*dev_info), GFP_KERNEL);
		if (!dev_info)
			return -ENOMEM;

		dev_info->dev = pcidev;
		list_add_tail(&dev_info->pcidev_node, &pcie_dev->enum_ep_list);
	}

	/* for upstream port of a switch */
	if (pci_pcie_type(pcidev) == PCI_EXP_TYPE_UPSTREAM) {
		ret = pci_enable_device(pcidev);
		if (ret) {
			PCIE_ERR(pcie_dev,
				 "PCIe: BDF 0x%04x pci_enable_device failed\n",
				 PCI_DEVID(pcidev->bus->number, pcidev->devfn));
			return ret;
		}
		pci_set_master(pcidev);
	}

	if (pcie_dev->aer_enable) {
		if (pci_pcie_type(pcidev) == PCI_EXP_TYPE_ROOT_PORT)
			pcie_dev->aer_stats = pcidev->aer_stats;

		if (pci_enable_pcie_error_reporting(pcidev))
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: PCIE error reporting unavailable on %02x:%02x:%01x\n",
				 pcie_dev->rc_idx, pcidev->bus->number,
				 PCI_SLOT(pcidev->devfn), PCI_FUNC(pcidev->devfn));
	}

	return 0;
}

static void msm_pcie_config_sid(struct msm_pcie_dev_t *dev)
{
	void __iomem *bdf_to_sid_base = dev->parf +
		PCIE20_PARF_BDF_TO_SID_TABLE_N;
	int i;

	if (!dev->sid_info)
		return;

	/* clear BDF_TO_SID_BYPASS bit to enable BDF to SID translation */
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_BDF_TO_SID_CFG, BIT(0), 0);

	/* Registers need to be zero out first */
	memset_io(bdf_to_sid_base, 0, CRC8_TABLE_SIZE * sizeof(u32));

	if (dev->enumerated) {
		for (i = 0; i < dev->sid_info_len; i++)
			msm_pcie_write_reg(bdf_to_sid_base,
					dev->sid_info[i].hash * sizeof(u32),
					dev->sid_info[i].value);
		return;
	}

	/* initial setup for boot */
	for (i = 0; i < dev->sid_info_len; i++) {
		struct msm_pcie_sid_info_t *sid_info = &dev->sid_info[i];
		u32 val;
		u8 hash;
		__be16 bdf_be = cpu_to_be16(sid_info->bdf);

		hash = crc8(msm_pcie_crc8_table, (u8 *)&bdf_be, sizeof(bdf_be),
			0);

		val = readl_relaxed(bdf_to_sid_base + hash * sizeof(u32));

		/* if there is a collision, look for next available entry */
		while (val) {
			u8 current_hash = hash++;
			u8 next_mask = 0xff;

			/* if NEXT is NULL then update current entry */
			if (!(val & next_mask)) {
				int j;

				val |= (u32)hash;
				msm_pcie_write_reg(bdf_to_sid_base,
					current_hash * sizeof(u32), val);

				/* sid_info of current hash and update it */
				for (j = 0; j < dev->sid_info_len; j++) {
					if (dev->sid_info[j].hash !=
						current_hash)
						continue;

					dev->sid_info[j].next_hash = hash;
					dev->sid_info[j].value = val;
					break;
				}
			}

			val = readl_relaxed(bdf_to_sid_base +
				hash * sizeof(u32));
		}

		/* BDF [31:16] | SID [15:8] | NEXT [7:0] */
		val = sid_info->bdf << 16 | sid_info->pcie_sid << 8 | 0;
		msm_pcie_write_reg(bdf_to_sid_base, hash * sizeof(u32), val);

		sid_info->hash = hash;
		sid_info->value = val;
	}
}

int msm_pcie_enumerate(u32 rc_idx)
{
	int ret = 0;
	struct msm_pcie_dev_t *dev = &msm_pcie_dev[rc_idx];
	struct pci_dev *pcidev = NULL;
	struct pci_host_bridge *bridge;
	bool found = false;
	u32 ids, vendor_id, device_id;
	LIST_HEAD(res);

	mutex_lock(&dev->enumerate_lock);

	PCIE_DBG(dev, "Enumerate RC%d\n", rc_idx);

	dev->fmd_enable = false;
	if (!dev->drv_ready) {
		PCIE_DBG(dev,
			"PCIe: RC%d: has not been successfully probed yet\n",
			rc_idx);
		ret = -EPROBE_DEFER;
		goto out;
	}

	if (dev->enumerated) {
		PCIE_ERR(dev, "PCIe: RC%d: has already been enumerated.\n",
			dev->rc_idx);
		goto out;
	}

	ret = msm_pcie_enable(dev);
	if (ret) {
		PCIE_ERR(dev, "PCIe: RC%d: failed to enable\n", dev->rc_idx);
		goto out;
	}

	dev->cfg_access = true;

	/* kick start ARM PCI configuration framework */
	ids = readl_relaxed(dev->dm_core);
	vendor_id = ids & 0xffff;
	device_id = (ids & 0xffff0000) >> 16;

	PCIE_DBG(dev, "PCIe: RC%d: vendor-id:0x%x device_id:0x%x\n",
		dev->rc_idx, vendor_id, device_id);

	if (!dev->bridge) {
		bridge = devm_pci_alloc_host_bridge(&dev->pdev->dev, sizeof(*dev));
		if (!bridge) {

			PCIE_ERR(dev, "PCIe: RC%d: bridge allocation failed\n", dev->rc_idx);
			ret = -ENOMEM;
			goto out;
		}

		dev->bridge = bridge;

		if (!dev->lpi_enable) {
			ret = msm_msi_init(&dev->pdev->dev);
			if (ret)
				goto out;
		}
	} else {
		bridge = dev->bridge;
		if (!dev->lpi_enable)
			msm_msi_config_access(dev_get_msi_domain(&dev->dev->dev), true);
	}

	bridge->sysdata = dev;
	bridge->ops = &msm_pcie_ops;

	pci_host_probe(bridge);

	dev->enumerated = true;

	if (dev->drv_supported)
		schedule_work(&pcie_drv.drv_connect);

	msm_pcie_write_mask(dev->dm_core +
		PCIE20_COMMAND_STATUS, 0, BIT(2)|BIT(1));

	if (dev->cpl_timeout && dev->bridge_found)
		msm_pcie_write_reg_field(dev->dm_core,
			PCIE20_DEVICE_CONTROL2_STATUS2, 0xf, dev->cpl_timeout);

	do {
		pcidev = pci_get_device(vendor_id, device_id, pcidev);
		if (pcidev && (dev == (struct msm_pcie_dev_t *)
			PCIE_BUS_PRIV_DATA(pcidev->bus))) {
			dev->dev = pcidev;
			found = true;
		}
	} while (!found && pcidev);

	if (!pcidev) {
		PCIE_ERR(dev, "PCIe: RC%d: Did not find PCI device.\n",
			dev->rc_idx);
		ret = -ENODEV;
		goto out;
	}

	pci_walk_bus(dev->dev->bus, msm_pcie_config_device_info, dev);

	msm_pcie_check_l1ss_support_all(dev);
	msm_pcie_config_link_pm(dev, true);

	pci_save_state(pcidev);
	dev->default_state = pci_store_saved_state(pcidev);

	if (dev->boot_option & MSM_PCIE_NO_PROBE_ENUMERATION)
		dev_pm_syscore_device(&pcidev->dev, true);
out:
	mutex_unlock(&dev->enumerate_lock);

	return ret;
}
EXPORT_SYMBOL(msm_pcie_enumerate);

int msm_pcie_deenumerate(u32 rc_idx)
{
	struct msm_pcie_dev_t *dev = &msm_pcie_dev[rc_idx];
	struct pci_host_bridge *bridge = dev->bridge;

	mutex_lock(&dev->enumerate_lock);

	PCIE_DBG(dev, "RC%d: Entry\n", dev->rc_idx);

	if (!dev->enumerated) {
		PCIE_DBG(dev, "RC%d:device is not enumerated\n", dev->rc_idx);
		mutex_unlock(&dev->enumerate_lock);
		return 0;
	}

	if (dev->config_recovery) {
		PCIE_DBG(dev, "RC%d: cancel link_recover_wq\n", dev->rc_idx);
		cancel_work_sync(&dev->link_recover_wq);
	}

	spin_lock_irqsave(&dev->cfg_lock, dev->irqsave_flags);
	dev->cfg_access = false;
	spin_unlock_irqrestore(&dev->cfg_lock, dev->irqsave_flags);

	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);

	/* Mask all the interrupts */
	msm_pcie_write_reg(dev->parf, PCIE20_PARF_INT_ALL_MASK, 0);

	msm_pcie_disable(dev);

	dev->enumerated = false;

	mutex_unlock(&dev->enumerate_lock);

	PCIE_DBG(dev, "RC%d: exit\n", dev->rc_idx);
	return 0;

}
EXPORT_SYMBOL_GPL(msm_pcie_deenumerate);

static bool msm_pcie_notify_client(struct msm_pcie_dev_t *dev,
					enum msm_pcie_event event)
{
	struct msm_pcie_register_event *reg_itr, *temp;
	struct msm_pcie_notify *notify;
	struct msm_pcie_notify client_notify;
	unsigned long flags;
	bool notified = false;

	spin_lock_irqsave(&dev->evt_reg_list_lock, flags);
	list_for_each_entry_safe(reg_itr, temp, &dev->event_reg_list, node) {
		if ((reg_itr->events & event) && reg_itr->callback) {
			notify = &reg_itr->notify;
			client_notify.event = event;
			client_notify.user = reg_itr->user;
			client_notify.data = notify->data;
			client_notify.options = notify->options;
			PCIE_DUMP(dev, "PCIe: callback RC%d for event %d\n",
				  dev->rc_idx, event);

			/* Release spinlock before notifying client driver
			 * and acquire it once done because once host notifies
			 * client driver with an event, client can schedule an
			 * recovery in same context before returning and
			 * expects an new event which could cause an race
			 * condition if spinlock is acquired.
			 */
			spin_unlock_irqrestore(&dev->evt_reg_list_lock, flags);

			reg_itr->callback(&client_notify);
			notified = true;

			spin_lock_irqsave(&dev->evt_reg_list_lock, flags);
			if ((reg_itr->options & MSM_PCIE_CONFIG_NO_RECOVERY) &&
					(event == MSM_PCIE_EVENT_LINKDOWN)) {
				dev->user_suspend = true;
				PCIE_DBG(dev,
					"PCIe: Client of RC%d will recover the link later.\n",
					dev->rc_idx);
			}

			break;
		}
	}
	spin_unlock_irqrestore(&dev->evt_reg_list_lock, flags);

	return notified;
}

static void handle_sbr_func(struct work_struct *work)
{
	int rc, i;
	u32 val, link_check_count = 0;
	struct msm_pcie_reset_info_t *reset_info;
	struct msm_pcie_dev_t *dev = container_of(work, struct msm_pcie_dev_t,
					handle_sbr_work);

	PCIE_DBG(dev, "PCIe: SBR work for RC%d\n", dev->rc_idx);

	for (i = 0; i < MSM_PCIE_MAX_LINKDOWN_RESET; i++) {
		reset_info = &dev->linkdown_reset[i];
		if (!reset_info->hdl)
			continue;

		rc = reset_control_assert(reset_info->hdl);
		if (rc)
			PCIE_ERR(dev,
				"PCIe: RC%d failed to assert reset for %s.\n",
				dev->rc_idx, reset_info->name);
		else
			PCIE_DBG2(dev,
				"PCIe: RC%d successfully asserted reset for %s.\n",
				dev->rc_idx, reset_info->name);
	}

	/* add a 1ms delay to ensure the reset is asserted */
	usleep_range(1000, 1005);

	for (i = MSM_PCIE_MAX_LINKDOWN_RESET - 1; i >= 0; i--) {
		reset_info = &dev->linkdown_reset[i];
		if (!reset_info->hdl)
			continue;

		rc = reset_control_deassert(reset_info->hdl);
		if (rc)
			PCIE_ERR(dev,
				"PCIe: RC%d failed to deassert reset for %s.\n",
				dev->rc_idx, reset_info->name);
		else
			PCIE_DBG2(dev,
				"PCIe: RC%d successfully deasserted reset for %s.\n",
				dev->rc_idx, reset_info->name);
	}

	PCIE_DBG(dev, "post reset ltssm:%x\n",
		 readl_relaxed(dev->parf + PCIE20_PARF_LTSSM));

	/* enable link training */
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_LTSSM, 0, LTSSM_EN);

	/* Wait for up to 100ms for the link to come up */
	do {
		val =  readl_relaxed(dev->elbi + PCIE20_ELBI_SYS_STTS);
		PCIE_DBG(dev, "PCIe RC%d: LTSSM_STATE: %x %s\n",
			dev->rc_idx, val, TO_LTSSM_STR((val >> 12) & 0x3f));
		usleep_range(10000, 11000);
	} while ((!(val & XMLH_LINK_UP) ||
		!msm_pcie_confirm_linkup(dev, false, false, NULL))
		&& (link_check_count++ < 10));

	if ((val & XMLH_LINK_UP) &&
	     msm_pcie_confirm_linkup(dev, false, false, NULL)) {
		dev->link_status = MSM_PCIE_LINK_ENABLED;
		PCIE_DBG(dev, "Link is up after %d checkings\n",
			link_check_count);
		PCIE_INFO(dev, "PCIe RC%d link initialized\n", dev->rc_idx);
	} else {
		PCIE_ERR(dev, "PCIe RC%d link initialization failed\n",
			dev->rc_idx);
	}
}

static irqreturn_t handle_flush_irq(int irq, void *data)
{
	struct msm_pcie_dev_t *dev = data;

	schedule_work(&dev->handle_sbr_work);

	return IRQ_HANDLED;
}

static void handle_wake_func(struct work_struct *work)
{
	struct msm_pcie_dev_t *dev = container_of(work, struct msm_pcie_dev_t,
					handle_wake_work);

	PCIE_DBG(dev, "PCIe: Wake work for RC%d\n", dev->rc_idx);

	mutex_lock(&dev->recovery_lock);

	if (dev->enumerated) {
		PCIE_ERR(dev,
			 "PCIe: The enumeration for RC%d has already been done.\n",
			 dev->rc_idx);
		goto out;
	}

	PCIE_DBG(dev,
		 "PCIe: Start enumeration for RC%d upon the wake from endpoint.\n",
		 dev->rc_idx);

	if (msm_pcie_enumerate(dev->rc_idx)) {
		PCIE_ERR(dev,
			 "PCIe: failed to enable RC%d upon wake request from the device.\n",
			  dev->rc_idx);
		goto out;
	}

	msm_pcie_notify_client(dev, MSM_PCIE_EVENT_LINKUP);

out:
	mutex_unlock(&dev->recovery_lock);
}

static void handle_link_recover(struct work_struct *work)
{
	struct msm_pcie_dev_t *dev = container_of(work, struct msm_pcie_dev_t,
					link_recover_wq);
	PCIE_DBG(dev, "PCIe: link recover start for RC%d\n", dev->rc_idx);

	msm_pcie_notify_client(dev, MSM_PCIE_EVENT_LINK_RECOVER);
}

/* AER error handling */
static void msm_pci_dev_aer_stats_incr(struct pci_dev *pdev,
				       struct msm_aer_err_info *info)
{
	unsigned long status = info->status & ~info->mask;
	int i, max = -1;
	u64 *counter = NULL;
	struct aer_stats *aer_stats = pdev->aer_stats;

	if (!aer_stats)
		return;

	switch (info->severity) {
	case AER_CORRECTABLE:
		aer_stats->dev_total_cor_errs++;
		counter = &aer_stats->dev_cor_errs[0];
		max = AER_MAX_TYPEOF_COR_ERRS;
		break;
	case AER_NONFATAL:
		aer_stats->dev_total_nonfatal_errs++;
		counter = &aer_stats->dev_nonfatal_errs[0];
		max = AER_MAX_TYPEOF_UNCOR_ERRS;
		break;
	case AER_FATAL:
		aer_stats->dev_total_fatal_errs++;
		counter = &aer_stats->dev_fatal_errs[0];
		max = AER_MAX_TYPEOF_UNCOR_ERRS;
		break;
	}

	for_each_set_bit(i, &status, max)
		counter[i]++;
}

static void msm_pci_rootport_aer_stats_incr(struct pci_dev *pdev,
					    struct aer_err_source *e_src)
{
	struct aer_stats *aer_stats = pdev->aer_stats;

	if (!aer_stats)
		return;

	if (e_src->status & PCI_ERR_ROOT_COR_RCV)
		aer_stats->rootport_total_cor_errs++;

	if (e_src->status & PCI_ERR_ROOT_UNCOR_RCV) {
		if (e_src->status & PCI_ERR_ROOT_FATAL_RCV)
			aer_stats->rootport_total_fatal_errs++;
		else
			aer_stats->rootport_total_nonfatal_errs++;
	}
}

static void msm_print_tlp_header(struct pci_dev *dev,
				 struct msm_aer_err_info *info)
{
	PCIE_DBG(info->rdev, "PCIe: RC%d: TLP Header: %08x %08x %08x %08x\n",
		info->rdev->rc_idx, info->tlp.dw0, info->tlp.dw1, info->tlp.dw2, info->tlp.dw3);
}

static void msm_aer_print_error_stats(struct pci_dev *dev,
				struct msm_aer_err_info *info)
{
	const char * const *strings;
	unsigned long status = info->status & ~info->mask;
	const char *errmsg;
	int i;

	if (info->severity == AER_CORRECTABLE)
		strings = aer_correctable_error_string;
	else
		strings = aer_uncorrectable_error_string;

	for_each_set_bit(i, &status, 32) {
		errmsg = strings[i];
		if (!errmsg)
			errmsg = "Unknown Error Bit";

		PCIE_DBG(info->rdev, "PCIe: RC%d: [%2d] %-22s%s\n",
			 info->rdev->rc_idx, i, errmsg,
			 info->first_error == i ? " (First)" : "");
	}
	msm_pci_dev_aer_stats_incr(dev, info);
}

void msm_aer_print_error(struct pci_dev *dev, struct msm_aer_err_info *info)
{
	int layer, agent;
	int id = ((dev->bus->number << 8) | dev->devfn);

	if (!info->status) {
		PCIE_DBG(info->rdev,
		"PCIe: RC%d: PCIe Bus Error: severity=%s, type=Inaccessible, (Unregistered Agent ID)\n",
		info->rdev->rc_idx, aer_error_severity_string[info->severity]);
		goto out;
	}

	layer = AER_GET_LAYER_ERROR(info->severity, info->status);
	agent = AER_GET_AGENT(info->severity, info->status);

	PCIE_DBG(info->rdev, "PCIe: RC%d: PCIe Bus Error: severity=%s, type=%s, (%s)\n",
		 info->rdev->rc_idx, aer_error_severity_string[info->severity],
		 aer_error_layer[layer], aer_agent_string[agent]);

	PCIE_DBG(info->rdev, "PCIe: RC%d: device [%04x:%04x] error status/mask=%08x/%08x\n",
		 info->rdev->rc_idx, dev->vendor, dev->device, info->status,
		 info->mask);

	PCIE_DBG(info->rdev, "PCIe: RC%d: device [%04x:%04x] error l1ss_ctl1=%x lnkstat=%x\n",
		info->rdev->rc_idx, dev->vendor, dev->device, info->l1ss_ctl1,
		info->lnksta);

	msm_aer_print_error_stats(dev, info);

	if (info->tlp_header_valid)
		msm_print_tlp_header(dev, info);

out:
	if (info->id && info->error_dev_num > 1 && info->id == id)
		PCIE_DBG(info->rdev, "PCIe: RC%d: Error of this Agent is reported first\n",
			info->rdev->rc_idx);
}

static void msm_aer_print_port_info(struct pci_dev *dev,
				    struct msm_aer_err_info *info)
{
	u8 bus = info->id >> 8;
	u8 devfn = info->id & 0xff;

	PCIE_DBG(info->rdev, "PCIe: RC%d: %s%s error received: %04x:%02x:%02x.%d\n",
		 info->rdev->rc_idx, info->multi_error_valid ? "Multiple " : "",
		 aer_error_severity_string[info->severity],
		 pci_domain_nr(dev->bus), bus, PCI_SLOT(devfn),
		 PCI_FUNC(devfn));
}

/**
 * msm_add_error_device - list device to be handled
 * @e_info: pointer to error info
 * @dev: pointer to pci_dev to be added
 */
static int msm_add_error_device(struct msm_aer_err_info *e_info,
				struct pci_dev *dev)
{
	if (e_info->error_dev_num < AER_MAX_MULTI_ERR_DEVICES) {
		e_info->dev[e_info->error_dev_num] = pci_dev_get(dev);
		e_info->error_dev_num++;
		return 0;
	}
	return -ENOSPC;
}

/**
 * msm_is_error_source - check whether the device is source of reported error
 * @dev: pointer to pci_dev to be checked
 * @e_info: pointer to reported error info
 */
static bool msm_is_error_source(struct pci_dev *dev,
				struct msm_aer_err_info *e_info)
{
	int aer = dev->aer_cap;
	u32 status, mask;
	u16 reg16;

	/*
	 * When bus id is equal to 0, it might be a bad id
	 * reported by root port.
	 */
	if ((PCI_BUS_NUM(e_info->id) != 0) &&
	    !(dev->bus->bus_flags & PCI_BUS_FLAGS_NO_AERSID)) {
		/* Device ID match? */
		if (e_info->id == ((dev->bus->number << 8) | dev->devfn))
			return true;

		/* Continue id comparing if there is no multiple error */
		if (!e_info->multi_error_valid)
			return false;
	}

	/*
	 * When either
	 *      1) bus id is equal to 0. Some ports might lose the bus
	 *              id of error source id;
	 *      2) bus flag PCI_BUS_FLAGS_NO_AERSID is set
	 *      3) There are multiple errors and prior ID comparing fails;
	 * We check AER status registers to find possible reporter.
	 */
	if (atomic_read(&dev->enable_cnt) == 0)
		return false;

	/* Check if AER is enabled */
	pcie_capability_read_word(dev, PCI_EXP_DEVCTL, &reg16);
	if (!(reg16 & PCI_EXP_AER_FLAGS))
		return false;

	if (!aer)
		return false;

	/* Check if error is recorded */
	if (e_info->severity == AER_CORRECTABLE) {
		pci_read_config_dword(dev, aer + PCI_ERR_COR_STATUS, &status);
		pci_read_config_dword(dev, aer + PCI_ERR_COR_MASK, &mask);
	} else {
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, &status);
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_MASK, &mask);
	}
	if (status & ~mask)
		return true;

	return false;
}

static int msm_find_device_iter(struct pci_dev *dev, void *data)
{
	struct msm_aer_err_info *e_info = (struct msm_aer_err_info *)data;

	if (msm_is_error_source(dev, e_info)) {
		/* List this device */
		if (msm_add_error_device(e_info, dev)) {
			/* We cannot handle more... Stop iteration */
			return 1;
		}

		/* If there is only a single error, stop iteration */
		if (!e_info->multi_error_valid)
			return 1;
	}
	return 0;
}

/**
 * msm_find_source_device - search through device hierarchy for source device
 * @parent: pointer to Root Port pci_dev data structure
 * @e_info: including detailed error information such like id
 *
 * Return true if found.
 *
 * Invoked by DPC when error is detected at the Root Port.
 * Caller of this function must set id, severity, and multi_error_valid of
 * struct msm_aer_err_info pointed by @e_info properly.  This function must fill
 * e_info->error_dev_num and e_info->dev[], based on the given information.
 */
static bool msm_find_source_device(struct pci_dev *parent,
				   struct msm_aer_err_info *e_info)
{
	struct pci_dev *dev = parent;
	int result;

	/* Must reset in this function */
	e_info->error_dev_num = 0;

	/* Is Root Port an agent that sends error message? */
	result = msm_find_device_iter(dev, e_info);
	if (result)
		return true;

	pci_walk_bus(parent->subordinate, msm_find_device_iter, e_info);

	if (!e_info->error_dev_num) {
		PCIE_DBG(e_info->rdev, "PCIe: RC%d: can't find device of ID%04x\n",
			 e_info->rdev->rc_idx, e_info->id);
		return false;
	}
	return true;
}

/**
 * msm_handle_error_source - handle logging error into an event log
 * @dev: pointer to pci_dev data structure of error source device
 * @info: comprehensive error information
 *
 * Invoked when an error being detected by Root Port.
 */
static void msm_handle_error_source(struct pci_dev *dev,
				    struct msm_aer_err_info *info)
{
	int aer = dev->aer_cap;
	struct msm_pcie_dev_t *rdev = info->rdev;
	u32 status, sev;

	if (!rdev->aer_dump && !rdev->suspending &&
		rdev->link_status == MSM_PCIE_LINK_ENABLED) {
		/* Print the dumps only once */
		rdev->aer_dump = true;

		if (info->severity == AER_CORRECTABLE &&
				!rdev->panic_on_aer)
			goto skip;

		/* Disable dumping PCIe registers when we are in DRV suspend */
		spin_lock_irqsave(&rdev->cfg_lock, rdev->irqsave_flags);
		if (!rdev->cfg_access) {
			PCIE_DBG2(rdev,
				"PCIe: RC%d is currently in drv suspend.\n",
				rdev->rc_idx);
			spin_unlock_irqrestore(&rdev->cfg_lock, rdev->irqsave_flags);
			return;
		}
		pcie_parf_dump(rdev);
		pcie_dm_core_dump(rdev);
		pcie_phy_dump(rdev);
		pcie_sm_dump(rdev);
		pcie_crm_dump(rdev);
		spin_unlock_irqrestore(&rdev->cfg_lock, rdev->irqsave_flags);

skip:
		if (rdev->panic_on_aer)
			panic("AER error severity %d\n", info->severity);
	}

	if (info->severity == AER_CORRECTABLE) {
		/*
		 * Correctable error does not need software intervention.
		 * No need to go through error recovery process.
		 */
		if (aer)
			pci_write_config_dword(dev, aer + PCI_ERR_COR_STATUS,
					info->status);
		pcie_capability_clear_and_set_word(dev, PCI_EXP_DEVSTA, 0,
						   PCI_EXP_DEVSTA_CED |
						   PCI_EXP_DEVSTA_NFED |
						   PCI_EXP_DEVSTA_FED);
	} else if (info->severity == AER_NONFATAL) {
		if (aer) {
			/* Clear status bits for ERR_NONFATAL errors only */
			pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, &status);
			pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_SEVER, &sev);
			status &= ~sev;
			if (status)
				pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, status);
		}
		pcie_capability_clear_and_set_word(dev, PCI_EXP_DEVSTA, 0,
					PCI_EXP_DEVSTA_CED |
					PCI_EXP_DEVSTA_NFED |
					PCI_EXP_DEVSTA_FED);
	} else {
		/* AER_FATAL */
		panic("AER error severity %d\n", info->severity);
	}

	pci_dev_put(dev);
}

/**
 * msm_aer_get_device_error_info - read error status from dev and store it to
 * info
 * @dev: pointer to the device expected to have a error record
 * @info: pointer to structure to store the error record
 *
 * Return 1 on success, 0 on error.
 *
 * Note that @info is reused among all error devices. Clear fields properly.
 */
static int msm_aer_get_device_error_info(struct pci_dev *dev,
					 struct msm_aer_err_info *info)
{
	int type = pci_pcie_type(dev);
	int aer = dev->aer_cap;
	int temp;
	u32 l1ss_cap_id_offset;

	/* Must reset in this function */
	info->status = 0;
	info->tlp_header_valid = 0;

	/* The device might not support AER */
	if (!aer)
		return 0;

	l1ss_cap_id_offset = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss_cap_id_offset) {
		PCIE_DBG(info->rdev,
			"PCIe: RC%d: Could not read l1ss cap reg offset\n",
			info->rdev->rc_idx);
		return 0;
	}

	if (info->severity == AER_CORRECTABLE) {
		pci_read_config_dword(dev, aer + PCI_ERR_COR_STATUS,
			&info->status);
		pci_read_config_dword(dev, aer + PCI_ERR_COR_MASK,
			&info->mask);

		pci_read_config_dword(dev, l1ss_cap_id_offset + PCI_L1SS_CTL1,
			&info->l1ss_ctl1);
		pcie_capability_read_word(dev, PCI_EXP_LNKSTA,
			&info->lnksta);

		if (!(info->status & ~info->mask))
			return 0;
	} else if (type == PCI_EXP_TYPE_ROOT_PORT ||
		   type == PCI_EXP_TYPE_RC_EC ||
		   type == PCI_EXP_TYPE_DOWNSTREAM ||
		   info->severity == AER_NONFATAL) {

		/* Link is still healthy for IO reads */
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS,
			&info->status);
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_MASK,
			&info->mask);

		pci_read_config_dword(dev, l1ss_cap_id_offset + PCI_L1SS_CTL1,
			&info->l1ss_ctl1);
		pcie_capability_read_word(dev, PCI_EXP_LNKSTA,
			&info->lnksta);

		if (!(info->status & ~info->mask))
			return 0;

		/* Get First Error Pointer */
		pci_read_config_dword(dev, aer + PCI_ERR_CAP, &temp);
		info->first_error = PCI_ERR_CAP_FEP(temp);

		if (info->status & AER_LOG_TLP_MASKS) {
			info->tlp_header_valid = 1;
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG, &info->tlp.dw0);
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG + 4, &info->tlp.dw1);
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG + 8, &info->tlp.dw2);
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG + 12, &info->tlp.dw3);
		}
	}

	return 1;
}

static inline void msm_aer_process_err_devices(struct msm_aer_err_info *e_info)
{
	int i;

	/* Report all before handle them, not to lost records by reset etc. */
	for (i = 0; i < e_info->error_dev_num && e_info->dev[i]; i++) {
		if (msm_aer_get_device_error_info(e_info->dev[i], e_info))
			msm_aer_print_error(e_info->dev[i], e_info);
	}
	for (i = 0; i < e_info->error_dev_num && e_info->dev[i]; i++) {
		if (msm_aer_get_device_error_info(e_info->dev[i], e_info))
			msm_handle_error_source(e_info->dev[i], e_info);
	}
}

static void msm_aer_isr_one_error(struct msm_pcie_dev_t *dev,
			      struct aer_err_source *e_src)
{
	struct msm_aer_err_info e_info;

	e_info.rdev = dev;

	msm_pci_rootport_aer_stats_incr(dev->dev, e_src);

	/*
	 * There is a possibility that both correctable error and
	 * uncorrectable error being logged. Report correctable error first.
	 */
	if (e_src->status & PCI_ERR_ROOT_COR_RCV) {
		e_info.id = ERR_COR_ID(e_src->id);
		e_info.severity = AER_CORRECTABLE;

		if (e_src->status & PCI_ERR_ROOT_MULTI_COR_RCV)
			e_info.multi_error_valid = 1;
		else
			e_info.multi_error_valid = 0;
		msm_aer_print_port_info(dev->dev, &e_info);

		if (msm_find_source_device(dev->dev, &e_info))
			msm_aer_process_err_devices(&e_info);
	}

	if (e_src->status & PCI_ERR_ROOT_UNCOR_RCV) {
		e_info.id = ERR_UNCOR_ID(e_src->id);

		if (e_src->status & PCI_ERR_ROOT_FATAL_RCV)
			e_info.severity = AER_FATAL;
		else
			e_info.severity = AER_NONFATAL;

		if (e_src->status & PCI_ERR_ROOT_MULTI_UNCOR_RCV)
			e_info.multi_error_valid = 1;
		else
			e_info.multi_error_valid = 0;

		msm_aer_print_port_info(dev->dev, &e_info);

		if (msm_find_source_device(dev->dev, &e_info))
			msm_aer_process_err_devices(&e_info);
	}
}

static irqreturn_t handle_aer_irq(int irq, void *data)
{
	struct msm_pcie_dev_t *dev = data;
	struct aer_err_source e_src;

	if (kfifo_is_empty(&dev->aer_fifo))
		return IRQ_NONE;

	while (kfifo_get(&dev->aer_fifo, &e_src)) {

		/* Not handling aer interrupts when we are in drv suspend */
		spin_lock_irqsave(&dev->cfg_lock, dev->irqsave_flags);
		if (!dev->cfg_access) {
			PCIE_DBG2(dev,
				"PCIe: RC%d is currently in drv suspend.\n",
				dev->rc_idx);
			spin_unlock_irqrestore(&dev->cfg_lock, dev->irqsave_flags);
			goto done;
		}
		spin_unlock_irqrestore(&dev->cfg_lock, dev->irqsave_flags);
		msm_aer_isr_one_error(dev, &e_src);

	}

done:
	return IRQ_HANDLED;
}

static irqreturn_t handle_wake_irq(int irq, void *data)
{
	struct msm_pcie_dev_t *dev = data;
	unsigned long irqsave_flags;

	spin_lock_irqsave(&dev->irq_lock, irqsave_flags);

	dev->wake_counter++;
	PCIE_DBG(dev, "PCIe: No. %ld wake IRQ for RC%d\n",
			dev->wake_counter, dev->rc_idx);

	PCIE_DBG2(dev, "PCIe WAKE is asserted by Endpoint of RC%d\n",
		dev->rc_idx);

	if (!dev->enumerated && !(dev->boot_option &
		MSM_PCIE_NO_WAKE_ENUMERATION)) {
		PCIE_DBG(dev, "Start enumerating RC%d\n", dev->rc_idx);
		schedule_work(&dev->handle_wake_work);
	} else {
		PCIE_DBG2(dev, "Wake up RC%d\n", dev->rc_idx);
		__pm_stay_awake(dev->ws);
		__pm_relax(dev->ws);

		if (dev->drv_supported && !dev->suspending &&
		    dev->link_status == MSM_PCIE_LINK_ENABLED) {
			pcie_phy_dump(dev);
			pcie_parf_dump(dev);
			pcie_dm_core_dump(dev);
			pcie_sm_dump(dev);
			pcie_crm_dump(dev);
		}

		msm_pcie_notify_client(dev, MSM_PCIE_EVENT_WAKEUP);
	}

	spin_unlock_irqrestore(&dev->irq_lock, irqsave_flags);

	return IRQ_HANDLED;
}

/* Attempt to recover link, return 0 if success */
static int msm_pcie_linkdown_recovery(struct msm_pcie_dev_t *dev)
{
	u32 status = 0;
	u32 cnt = 100; /* 1msec timeout */

	PCIE_DUMP(dev, "PCIe:Linkdown IRQ for RC%d attempt recovery\n",
		dev->rc_idx);

	while (cnt--) {
		status = readl_relaxed(dev->parf + PCIE20_PARF_STATUS);
		if (status & FLUSH_COMPLETED) {
			PCIE_DBG(dev,
			       "flush complete (%d), status:%x\n", cnt, status);
			break;
		}
		udelay(10);
	}

	if (!cnt) {
		PCIE_DBG(dev, "flush timeout, status:%x\n", status);
		return -ETIMEDOUT;
	}

	/* Clear flush and move core to reset mode */
	msm_pcie_write_mask(dev->parf + PCIE20_PARF_LTSSM,
			    0, SW_CLR_FLUSH_MODE);

	/* wait for flush mode to clear */
	cnt = 100; /* 1msec timeout */
	while (cnt--) {
		status = readl_relaxed(dev->parf + PCIE20_PARF_LTSSM);
		if (!(status & FLUSH_MODE)) {
			PCIE_DBG(dev, "flush mode clear:%d, %x\n", cnt, status);
			break;
		}

		udelay(10);
	}

	if (!cnt) {
		PCIE_DBG(dev, "flush-mode timeout, status:%x\n", status);
		return -ETIMEDOUT;
	}

	return 0;
}

static void msm_pcie_handle_linkdown(struct msm_pcie_dev_t *dev)
{
	int ret;

	if (dev->link_status == MSM_PCIE_LINK_DOWN)
		return;

	dev->link_status = MSM_PCIE_LINK_DOWN;

	/* Linkdown is expected. As it must be due to card removal action. So return */
	if ((dev->gpio[MSM_PCIE_GPIO_CARD_PRESENCE_PIN].num) &&
		(gpio_get_value(dev->gpio[MSM_PCIE_GPIO_CARD_PRESENCE_PIN].num))) {
		PCIE_DUMP(dev, "Linkdown due to card removal\n");
		return;
	}

	if (!dev->suspending && !dev->fmd_enable) {
		/* PCIe registers dump on link down */
		PCIE_DUMP(dev,
			"PCIe:Linkdown IRQ for RC%d Dumping PCIe registers\n",
			dev->rc_idx);
		pcie_phy_dump(dev);
		pcie_parf_dump(dev);
		pcie_dm_core_dump(dev);
		pcie_sm_dump(dev);
		pcie_crm_dump(dev);
	}

	/* Attempt link-down recovery instead of PERST if supported */
	if (dev->linkdown_recovery_enable) {
		ret = msm_pcie_linkdown_recovery(dev);
		/* Return without PERST assertion if success */
		if (!ret)
			return;
	}

	/* assert PERST */
	if (!(msm_pcie_keep_resources_on & BIT(dev->rc_idx)))
		msm_pcie_config_perst(dev, true);

	PCIE_ERR(dev, "PCIe link is down for RC%d\n", dev->rc_idx);

	if (dev->linkdown_panic)
		panic("User has chosen to panic on linkdown\n");

	msm_pcie_notify_client(dev, MSM_PCIE_EVENT_LINKDOWN);
}

static irqreturn_t handle_linkdown_irq(int irq, void *data)
{
	struct msm_pcie_dev_t *dev = data;

	dev->linkdown_counter++;

	PCIE_DBG(dev,
		"PCIe: No. %ld linkdown IRQ for RC%d.\n",
		dev->linkdown_counter, dev->rc_idx);

	if (!dev->enumerated || dev->link_status != MSM_PCIE_LINK_ENABLED)
		PCIE_DBG(dev,
			"PCIe:Linkdown IRQ for RC%d when the link is not enabled\n",
			dev->rc_idx);
	else if (dev->suspending)
		PCIE_DBG(dev,
			"PCIe:the link of RC%d is suspending.\n",
			dev->rc_idx);
	else
		msm_pcie_handle_linkdown(dev);

	return IRQ_HANDLED;
}

static irqreturn_t handle_global_irq(int irq, void *data)
{
	int i;
	struct msm_pcie_dev_t *dev = data;
	struct pci_dev *rp = dev->dev;
	int aer;
	unsigned long irqsave_flags;
	u32 status = 0, status2 = 0;
	irqreturn_t ret = IRQ_HANDLED;
	struct aer_err_source e_src = {};

	spin_lock_irqsave(&dev->irq_lock, irqsave_flags);

	if (dev->suspending) {
		PCIE_DBG2(dev,
			"PCIe: RC%d is currently suspending.\n",
			dev->rc_idx);
		goto done;
	}

	/* Not handling the interrupts when we are in drv suspend */
	if (!dev->cfg_access) {
		PCIE_DBG2(dev,
			"PCIe: RC%d is currently in drv suspend.\n",
			dev->rc_idx);
		goto done;
	}

	status = readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_STATUS) &
			readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_MASK);

	msm_pcie_write_mask(dev->parf + PCIE20_PARF_INT_ALL_CLEAR, 0, status);

	status2 = readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_2_STATUS) &
			readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_2_MASK);

	msm_pcie_write_mask(dev->parf + PCIE20_PARF_INT_ALL_2_CLEAR, 0, status2);

	PCIE_DUMP(dev, "RC%d: Global IRQ %d received: 0x%x status2: 0x%x\n",
		  dev->rc_idx, irq, status, status2);

	for (i = 0; i <= MSM_PCIE_INT_EVT_MAX; i++) {
		if (status & BIT(i)) {
			switch (i) {
			case MSM_PCIE_INT_EVT_LINK_DOWN:
				PCIE_DBG(dev,
					"PCIe: RC%d: handle linkdown event.\n",
					dev->rc_idx);
				handle_linkdown_irq(irq, data);
				break;
			case MSM_PCIE_INT_EVT_L1SUB_TIMEOUT:
				msm_pcie_notify_client(dev,
					MSM_PCIE_EVENT_L1SS_TIMEOUT);
				break;
			case MSM_PCIE_INT_EVT_AER_LEGACY:
			case MSM_PCIE_INT_EVT_AER_ERR:
				PCIE_DBG(dev,
					"PCIe: RC%d: AER event idx %d.\n",
					dev->rc_idx, i);

				if (!rp) {
					PCIE_DBG2(dev, "PCIe: RC%d pci_dev is not allocated.\n",
										dev->rc_idx);
					goto done;
				}

				aer = rp->aer_cap;
				pci_read_config_dword(rp,
				aer + PCI_ERR_ROOT_STATUS, &e_src.status);
				if (!(e_src.status &
				   (PCI_ERR_ROOT_UNCOR_RCV|
				   PCI_ERR_ROOT_COR_RCV))) {
					ret = IRQ_NONE;
					goto done;
				}

				pci_read_config_dword(rp,
					aer + PCI_ERR_ROOT_ERR_SRC, &e_src.id);
				pci_write_config_dword(rp,
					aer + PCI_ERR_ROOT_STATUS, e_src.status);

				if (kfifo_put(&dev->aer_fifo, e_src))
					ret = IRQ_WAKE_THREAD;
				break;
			case MSM_PCIE_INT_EVT_BRIDGE_FLUSH_N:
				PCIE_DBG(dev,
					"PCIe: RC%d: FLUSH event.\n",
					dev->rc_idx);
				handle_flush_irq(irq, data);
				break;
			default:
				PCIE_DUMP(dev,
					"PCIe: RC%d: Unexpected event %d is caught!\n",
					dev->rc_idx, i);
			}
		}
	}

	if (status2 & MSM_PCIE_BW_MGT_INT_STATUS) {
		/* Disable configuration for bandwidth interrupt */
		msm_pcie_config_bandwidth_int(dev, false);
		/* Clear bandwidth interrupt status */
		msm_pcie_clear_bandwidth_int_status(dev);
		PCIE_DBG(dev,
			 "PCIe: RC%d: Speed change interrupt received.\n",
			 dev->rc_idx);
		complete(&dev->speed_change_completion);
	}

done:
	spin_unlock_irqrestore(&dev->irq_lock, irqsave_flags);

	return ret;
}

static int32_t msm_pcie_irq_init(struct msm_pcie_dev_t *dev)
{
	int rc;
	struct device *pdev = &dev->pdev->dev;

	PCIE_DBG(dev, "RC%d\n", dev->rc_idx);

	dev->ws = wakeup_source_register(pdev, dev_name(pdev));
	if (!dev->ws) {
		PCIE_ERR(dev,
			"PCIe: RC%d: failed to register wakeup source\n",
			dev->rc_idx);
		return -ENOMEM;
	}

	if (dev->irq[MSM_PCIE_INT_GLOBAL_INT].num) {
		rc = devm_request_threaded_irq(pdev,
				dev->irq[MSM_PCIE_INT_GLOBAL_INT].num,
				handle_global_irq,
				handle_aer_irq,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				dev->irq[MSM_PCIE_INT_GLOBAL_INT].name,
				dev);
		if (rc) {
			PCIE_ERR(dev,
				"PCIe: RC%d: Unable to request global_int interrupt: %d\n",
				dev->rc_idx,
				dev->irq[MSM_PCIE_INT_GLOBAL_INT].num);
			return rc;
		}
	}

	/* register handler for PCIE_WAKE_N interrupt line */
	if (dev->wake_n) {
		rc = devm_request_irq(pdev,
				dev->wake_n, handle_wake_irq,
				IRQF_TRIGGER_FALLING, "msm_pcie_wake", dev);
		if (rc) {
			PCIE_ERR(dev,
				"PCIe: RC%d: Unable to request wake interrupt\n",
				dev->rc_idx);
			return rc;
		}

		INIT_WORK(&dev->handle_wake_work, handle_wake_func);
		INIT_WORK(&dev->handle_sbr_work, handle_sbr_func);

		rc = enable_irq_wake(dev->wake_n);
		if (rc) {
			PCIE_ERR(dev,
				"PCIe: RC%d: Unable to enable wake interrupt\n",
				dev->rc_idx);
			return rc;
		}
	}

	return 0;
}

static void msm_pcie_irq_deinit(struct msm_pcie_dev_t *dev)
{
	PCIE_DBG(dev, "RC%d\n", dev->rc_idx);

	wakeup_source_unregister(dev->ws);

	if (dev->wake_n)
		disable_irq(dev->wake_n);
}

static int msm_pcie_check_l0s_support(struct pci_dev *pdev, void *dev)
{
	struct pci_dev *parent = pdev->bus->self;
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;
	u32 val;

	/* check parent supports L0s */
	if (parent) {

		pci_read_config_dword(parent, parent->pcie_cap + PCI_EXP_LNKCAP,
					&val);
		val = (val & BIT(10));
		if (!val) {
			PCIE_DBG(pcie_dev,
				"PCIe: RC%d: Parent PCI device %02x:%02x.%01x does not support L0s\n",
				pcie_dev->rc_idx, parent->bus->number,
				PCI_SLOT(parent->devfn),
				PCI_FUNC(parent->devfn));
			pcie_dev->l0s_supported = false;
			return 0;
		}
	}

	pci_read_config_dword(pdev, pdev->pcie_cap + PCI_EXP_LNKCAP, &val);
	if (!(val & BIT(10))) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: PCI device %02x:%02x.%01x does not support L0s\n",
			pcie_dev->rc_idx, pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		pcie_dev->l0s_supported = false;
	}

	return 0;
}

static bool msm_pcie_check_l1_support(struct pci_dev *pdev,
					struct msm_pcie_dev_t *pcie_dev)
{
	struct pci_dev *parent = pdev->bus->self;
	u32 val;

	/* check parent supports L1 */
	if (parent) {
		u32 val2;

		pci_read_config_dword(parent, parent->pcie_cap + PCI_EXP_LNKCAP,
					&val);
		pci_read_config_dword(parent, parent->pcie_cap + PCI_EXP_LNKCTL,
					&val2);
		val = (val & BIT(11)) && (val2 & PCI_EXP_LNKCTL_ASPM_L1);
		if (!val) {
			PCIE_DBG(pcie_dev,
				"PCIe: RC%d: Parent PCI device %02x:%02x.%01x does not support L1\n",
				pcie_dev->rc_idx, parent->bus->number,
				PCI_SLOT(parent->devfn),
				PCI_FUNC(parent->devfn));
			return false;
		}
	}

	pci_read_config_dword(pdev, pdev->pcie_cap + PCI_EXP_LNKCAP, &val);
	if (!(val & BIT(11))) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: PCI device %02x:%02x.%01x does not support L1\n",
			pcie_dev->rc_idx, pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		return false;
	}

	return true;
}

static int msm_pcie_check_l1ss_support(struct pci_dev *pdev, void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;
	u32 val;
	u32 l1ss_cap_id_offset, l1ss_cap_offset, l1ss_ctl1_offset;

	if (!pcie_dev->l1ss_supported)
		return -ENXIO;

	l1ss_cap_id_offset = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss_cap_id_offset) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: PCI device %02x:%02x.%01x could not find L1ss capability register\n",
			pcie_dev->rc_idx, pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		pcie_dev->l1ss_supported = false;
		return -ENXIO;
	}

	l1ss_cap_offset = l1ss_cap_id_offset + PCI_L1SS_CAP;
	l1ss_ctl1_offset = l1ss_cap_id_offset + PCI_L1SS_CTL1;

	pci_read_config_dword(pdev, l1ss_cap_offset, &val);
	pcie_dev->l1_1_pcipm_supported &= !!(val & (PCI_L1SS_CAP_PCIPM_L1_1));
	pcie_dev->l1_2_pcipm_supported &= !!(val & (PCI_L1SS_CAP_PCIPM_L1_2));
	pcie_dev->l1_1_aspm_supported &= !!(val & (PCI_L1SS_CAP_ASPM_L1_1));
	pcie_dev->l1_2_aspm_supported &= !!(val & (PCI_L1SS_CAP_ASPM_L1_2));
	if (!pcie_dev->l1_1_pcipm_supported &&
		!pcie_dev->l1_2_pcipm_supported &&
		!pcie_dev->l1_1_aspm_supported &&
		!pcie_dev->l1_2_aspm_supported) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: PCI device %02x:%02x.%01x does not support any L1ss\n",
			pcie_dev->rc_idx, pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		pcie_dev->l1ss_supported = false;
		return -ENXIO;
	}

	return 0;
}

static int msm_pcie_config_common_clock_enable(struct pci_dev *pdev,
							void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: PCI device %02x:%02x.%01x\n",
		pcie_dev->rc_idx, pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn));

	msm_pcie_config_clear_set_dword(pdev, pdev->pcie_cap + PCI_EXP_LNKCTL,
					0, PCI_EXP_LNKCTL_CCC);

	return 0;
}

static void msm_pcie_config_common_clock_enable_all(struct msm_pcie_dev_t *dev)
{
	if (dev->common_clk_en)
		pci_walk_bus(dev->dev->bus,
			msm_pcie_config_common_clock_enable, dev);
}

static int msm_pcie_config_clock_power_management_enable(struct pci_dev *pdev,
							void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;
	u32 val;

	/* enable only for upstream ports */
	if (pci_is_root_bus(pdev->bus))
		return 0;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: PCI device %02x:%02x.%01x\n",
		pcie_dev->rc_idx, pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn));

	pci_read_config_dword(pdev, pdev->pcie_cap + PCI_EXP_LNKCAP, &val);
	if (val & PCI_EXP_LNKCAP_CLKPM)
		msm_pcie_config_clear_set_dword(pdev,
			pdev->pcie_cap + PCI_EXP_LNKCTL, 0,
			PCI_EXP_LNKCTL_CLKREQ_EN);
	else
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: PCI device %02x:%02x.%01x does not support clock power management\n",
			pcie_dev->rc_idx, pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	return 0;
}

static void msm_pcie_config_clock_power_management_enable_all(
						struct msm_pcie_dev_t *dev)
{
	if (dev->clk_power_manage_en)
		pci_walk_bus(dev->dev->bus,
			msm_pcie_config_clock_power_management_enable, dev);
}

static void msm_pcie_config_l0s(struct msm_pcie_dev_t *dev,
				struct pci_dev *pdev, bool enable)
{
	u32 lnkctl_offset = pdev->pcie_cap + PCI_EXP_LNKCTL;

	PCIE_DBG(dev, "PCIe: RC%d: PCI device %02x:%02x.%01x %s\n",
		dev->rc_idx, pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), enable ? "enable" : "disable");

	if (enable) {
		msm_pcie_config_clear_set_dword(pdev, lnkctl_offset, 0,
			PCI_EXP_LNKCTL_ASPM_L0S);
	} else {
		msm_pcie_config_clear_set_dword(pdev, lnkctl_offset,
			PCI_EXP_LNKCTL_ASPM_L0S, 0);
	}
}

static void msm_pcie_config_l0s_disable_all(struct msm_pcie_dev_t *dev,
				struct pci_bus *bus)
{
	struct pci_dev *pdev;

	if (!dev->l0s_supported)
		return;

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		struct pci_bus *child;

		child  = pdev->subordinate;
		if (child)
			msm_pcie_config_l0s_disable_all(dev, child);
		msm_pcie_config_l0s(dev, pdev, false);
	}
}

static int msm_pcie_config_l0s_enable(struct pci_dev *pdev, void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;

	if (!pcie_dev->l0s_supported)
		return 0;

	msm_pcie_config_l0s(pcie_dev, pdev, true);
	return 0;
}

static void msm_pcie_config_l0s_enable_all(struct msm_pcie_dev_t *dev)
{
	if (dev->l0s_supported)
		pci_walk_bus(dev->dev->bus, msm_pcie_check_l0s_support, dev);

	if (dev->l0s_supported)
		pci_walk_bus(dev->dev->bus, msm_pcie_config_l0s_enable, dev);
}

static void msm_pcie_config_l1(struct msm_pcie_dev_t *dev,
				struct pci_dev *pdev, bool enable)
{
	u32 lnkctl_offset = pdev->pcie_cap + PCI_EXP_LNKCTL;
	int ret;

	PCIE_DBG(dev, "PCIe: RC%d: PCI device %02x:%02x.%01x %s\n",
		dev->rc_idx, pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), enable ? "enable" : "disable");

	if (enable) {
		ret = msm_pcie_check_l1_support(pdev, dev);
		if (!ret)
			return;

		msm_pcie_config_clear_set_dword(pdev, lnkctl_offset, 0,
			PCI_EXP_LNKCTL_ASPM_L1);
	} else {
		msm_pcie_config_clear_set_dword(pdev, lnkctl_offset,
			PCI_EXP_LNKCTL_ASPM_L1, 0);
	}
}

static void msm_pcie_config_l1_disable_all(struct msm_pcie_dev_t *dev,
				struct pci_bus *bus)
{
	struct pci_dev *pdev;

	if (!dev->l1_supported)
		return;

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		struct pci_bus *child;

		child  = pdev->subordinate;
		if (child)
			msm_pcie_config_l1_disable_all(dev, child);
		msm_pcie_config_l1(dev, pdev, false);
	}
}

static int msm_pcie_config_l1_enable(struct pci_dev *pdev, void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;

	msm_pcie_config_l1(pcie_dev, pdev, true);
	return 0;
}

static void msm_pcie_config_l1_enable_all(struct msm_pcie_dev_t *dev)
{
	if (dev->l1_supported)
		pci_walk_bus(dev->dev->bus, msm_pcie_config_l1_enable, dev);
}

static void msm_pcie_config_l1ss(struct msm_pcie_dev_t *dev,
				struct pci_dev *pdev, bool enable)
{
	u32 val, val2;
	u32 l1ss_cap_id_offset, l1ss_ctl1_offset;
	u32 devctl2_offset = pdev->pcie_cap + PCI_EXP_DEVCTL2;

	PCIE_DBG(dev, "PCIe: RC%d: PCI device %02x:%02x.%01x %s\n",
		dev->rc_idx, pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), enable ? "enable" : "disable");

	l1ss_cap_id_offset = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss_cap_id_offset) {
		PCIE_DBG(dev,
			"PCIe: RC%d: PCI device %02x:%02x.%01x could not find L1ss capability register\n",
			dev->rc_idx, pdev->bus->number, PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn));
		return;
	}

	l1ss_ctl1_offset = l1ss_cap_id_offset + PCI_L1SS_CTL1;

	/* Enable the AUX Clock and the Core Clk to be synchronous for L1ss */
	if (pci_is_root_bus(pdev->bus) && !dev->aux_clk_sync) {
		if (enable)
			msm_pcie_write_mask(dev->parf +
				PCIE20_PARF_SYS_CTRL, BIT(3), 0);
		else
			msm_pcie_write_mask(dev->parf +
				PCIE20_PARF_SYS_CTRL, 0, BIT(3));
	}

	if (enable) {
		msm_pcie_config_clear_set_dword(pdev, devctl2_offset, 0,
			PCI_EXP_DEVCTL2_LTR_EN);

		msm_pcie_config_clear_set_dword(pdev, l1ss_ctl1_offset, 0,
			(dev->l1_1_pcipm_supported ?
				PCI_L1SS_CTL1_PCIPM_L1_1 : 0) |
			(dev->l1_2_pcipm_supported ?
				PCI_L1SS_CTL1_PCIPM_L1_2 : 0) |
			(dev->l1_1_aspm_supported ?
				PCI_L1SS_CTL1_ASPM_L1_1 : 0) |
			(dev->l1_2_aspm_supported ?
				PCI_L1SS_CTL1_ASPM_L1_2 : 0));
	} else {
		msm_pcie_config_clear_set_dword(pdev, devctl2_offset,
			PCI_EXP_DEVCTL2_LTR_EN, 0);

		msm_pcie_config_clear_set_dword(pdev, l1ss_ctl1_offset,
			PCI_L1SS_CTL1_PCIPM_L1_1 | PCI_L1SS_CTL1_PCIPM_L1_2 |
			PCI_L1SS_CTL1_ASPM_L1_1 | PCI_L1SS_CTL1_ASPM_L1_2, 0);
	}

	pci_read_config_dword(pdev, l1ss_ctl1_offset, &val);
	PCIE_DBG2(dev, "PCIe: RC%d: L1SUB_CONTROL1:0x%x\n", dev->rc_idx, val);

	pci_read_config_dword(pdev, devctl2_offset, &val2);
	PCIE_DBG2(dev, "PCIe: RC%d: DEVICE_CONTROL2_STATUS2::0x%x\n",
		dev->rc_idx, val2);
}

static int msm_pcie_config_l1ss_disable(struct pci_dev *pdev, void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;

	msm_pcie_config_l1ss(pcie_dev, pdev, false);
	return 0;
}

static void msm_pcie_config_l1ss_disable_all(struct msm_pcie_dev_t *dev,
				struct pci_bus *bus)
{
	struct pci_dev *pdev;

	if (!dev->l1ss_supported)
		return;

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		struct pci_bus *child;

		child  = pdev->subordinate;
		if (child)
			msm_pcie_config_l1ss_disable_all(dev, child);
		msm_pcie_config_l1ss_disable(pdev, dev);
	}
}

static int msm_pcie_config_l1_2_threshold(struct pci_dev *pdev, void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;
	u32 l1ss_cap_id_offset, l1ss_ctl1_offset;
	u32 l1_2_th_scale_shift = 29;
	u32 l1_2_th_value_shift = 16;

	/* LTR is not supported */
	if (!pcie_dev->l1_2_th_value)
		return 0;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: PCI device %02x:%02x.%01x\n",
		pcie_dev->rc_idx, pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn));

	l1ss_cap_id_offset = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss_cap_id_offset) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: PCI device %02x:%02x.%01x could not find L1ss capability register\n",
			pcie_dev->rc_idx, pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		return 0;
	}

	l1ss_ctl1_offset = l1ss_cap_id_offset + PCI_L1SS_CTL1;

	msm_pcie_config_clear_set_dword(pdev, l1ss_ctl1_offset, 0,
		(PCI_L1SS_CTL1_LTR_L12_TH_SCALE &
		(pcie_dev->l1_2_th_scale << l1_2_th_scale_shift)) |
		(PCI_L1SS_CTL1_LTR_L12_TH_VALUE &
		(pcie_dev->l1_2_th_value << l1_2_th_value_shift)));

	return 0;
}

static int msm_pcie_config_l1ss_enable(struct pci_dev *pdev, void *dev)
{
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)dev;

	msm_pcie_config_l1ss(pcie_dev, pdev, true);
	return 0;
}

static void msm_pcie_config_l1ss_enable_all(struct msm_pcie_dev_t *dev)
{
	if (dev->l1ss_supported) {
		pci_walk_bus(dev->dev->bus, msm_pcie_config_l1_2_threshold,
				dev);
		pci_walk_bus(dev->dev->bus, msm_pcie_config_l1ss_enable, dev);
	}
}

static void msm_pcie_config_link_pm(struct msm_pcie_dev_t *dev, bool enable)
{
	struct pci_bus *bus = dev->dev->bus;

	if (enable) {
		msm_pcie_config_common_clock_enable_all(dev);
		msm_pcie_config_clock_power_management_enable_all(dev);
		msm_pcie_config_l1ss_enable_all(dev);
		msm_pcie_config_l1_enable_all(dev);
		msm_pcie_config_l0s_enable_all(dev);
	} else {
		msm_pcie_config_l0s_disable_all(dev, bus);
		msm_pcie_config_l1_disable_all(dev, bus);
		msm_pcie_config_l1ss_disable_all(dev, bus);
	}
}

static void msm_pcie_check_l1ss_support_all(struct msm_pcie_dev_t *dev)
{
	pci_walk_bus(dev->dev->bus, msm_pcie_check_l1ss_support, dev);
}

static void msm_pcie_setup_drv_msg(struct msm_pcie_drv_msg *msg, u32 dev_id,
				enum msm_pcie_drv_cmds cmd)
{
	struct msm_pcie_drv_tre *pkt = &msg->pkt;
	struct msm_pcie_drv_header *hdr = &msg->hdr;

	hdr->major_ver = MSM_PCIE_DRV_MAJOR_VERSION;
	hdr->minor_ver = MSM_PCIE_DRV_MINOR_VERSION;
	hdr->msg_id = MSM_PCIE_DRV_MSG_ID_CMD;
	hdr->payload_size = sizeof(*pkt);
	hdr->dev_id = dev_id;

	pkt->dword[0] = cmd;
	pkt->dword[1] = hdr->dev_id;
}

static int msm_pcie_setup_drv(struct msm_pcie_dev_t *pcie_dev,
			 struct device_node *of_node)
{
	struct msm_pcie_drv_info *drv_info;

	drv_info = devm_kzalloc(&pcie_dev->pdev->dev, sizeof(*drv_info),
				GFP_KERNEL);
	if (!drv_info)
		return -ENOMEM;

	drv_info->dev_id = pcie_dev->rc_idx;

	msm_pcie_setup_drv_msg(&drv_info->drv_enable, drv_info->dev_id,
				MSM_PCIE_DRV_CMD_ENABLE);

	msm_pcie_setup_drv_msg(&drv_info->drv_disable, drv_info->dev_id,
				MSM_PCIE_DRV_CMD_DISABLE);

	msm_pcie_setup_drv_msg(&drv_info->drv_enable_l1ss_sleep,
				drv_info->dev_id,
				MSM_PCIE_DRV_CMD_ENABLE_L1SS_SLEEP);
	drv_info->drv_enable_l1ss_sleep.pkt.dword[2] =
					pcie_dev->l1ss_timeout_us / 1000;

	msm_pcie_setup_drv_msg(&drv_info->drv_disable_l1ss_sleep,
				drv_info->dev_id,
				MSM_PCIE_DRV_CMD_DISABLE_L1SS_SLEEP);

	msm_pcie_setup_drv_msg(&drv_info->drv_enable_pc, drv_info->dev_id,
				MSM_PCIE_DRV_CMD_ENABLE_PC);

	msm_pcie_setup_drv_msg(&drv_info->drv_disable_pc, drv_info->dev_id,
				MSM_PCIE_DRV_CMD_DISABLE_PC);

	init_completion(&drv_info->completion);
	drv_info->timeout_ms = IPC_TIMEOUT_MS;
	pcie_dev->drv_info = drv_info;

	return 0;
}

static struct rpmsg_device_id msm_pcie_drv_rpmsg_match_table[] = {
	{ .name = "pcie_drv" },
	{},
};

static struct rpmsg_driver msm_pcie_drv_rpmsg_driver = {
	.id_table = msm_pcie_drv_rpmsg_match_table,
	.probe = msm_pcie_drv_rpmsg_probe,
	.remove = msm_pcie_drv_rpmsg_remove,
	.callback = msm_pcie_drv_rpmsg_cb,
	.drv = {
		.name = "pci-msm-drv",
	},
};

static void msm_pcie_drv_cesta_connect_worker(struct work_struct *work)
{
	struct pcie_drv_sta *pcie_drv = container_of(work, struct pcie_drv_sta,
							drv_connect);
	struct msm_pcie_dev_t *pcie_itr = pcie_drv->msm_pcie_dev;
	int i;

	for (i = 0; i < MAX_RC_NUM; i++, pcie_itr++) {

		if (!pcie_itr->pcie_sm)
			continue;

		msm_pcie_notify_client(pcie_itr,
				       MSM_PCIE_EVENT_DRV_CONNECT);
	}
}

#if IS_ENABLED(CONFIG_I2C)
static int msm_pcie_i2c_ctrl_init(struct msm_pcie_dev_t *pcie_dev)
{
	int ret = 0, size;
	struct device_node *of_node, *i2c_client_node;
	struct device *dev = &pcie_dev->pdev->dev;
	struct pcie_i2c_ctrl *i2c_ctrl = &pcie_dev->i2c_ctrl;

	of_node = of_parse_phandle(dev->of_node, "pcie-i2c-phandle", 0);
	if (!of_node) {
		PCIE_DBG(pcie_dev, "PCIe: RC%d: No i2c phandle found\n",
			 pcie_dev->rc_idx);
		return 0;
	} else {
		if (!i2c_ctrl->client) {
			PCIE_DBG(pcie_dev, "PCIe: RC%d: No i2c probe yet\n",
				 pcie_dev->rc_idx);
			return -EPROBE_DEFER;
		}
	}

	i2c_client_node = i2c_ctrl->client->dev.of_node;
	if (!i2c_client_node) {
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: No i2c slave node phandle found\n",
			 pcie_dev->rc_idx);
		goto err;
	}

	of_property_read_u32(i2c_client_node, "gpio-config-reg",
			     &i2c_ctrl->gpio_config_reg);

	of_property_read_u32(i2c_client_node, "ep-reset-reg",
			     &i2c_ctrl->ep_reset_reg);

	of_property_read_u32(i2c_client_node, "ep-reset-gpio-mask",
			     &i2c_ctrl->ep_reset_gpio_mask);
	of_property_read_u32(i2c_client_node, "version-reg",
				 &i2c_ctrl->version_reg);
	i2c_ctrl->force_i2c_setting = of_property_read_bool(i2c_client_node,
				 "force-i2c-setting");
	i2c_ctrl->ep_reset_postlinkup = of_property_read_bool(i2c_client_node,
				 "ep_reset_postlinkup");
	of_get_property(i2c_client_node, "dump-regs", &size);

	if (size) {
		i2c_ctrl->dump_regs = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!i2c_ctrl->dump_regs) {
			ret = -ENOMEM;
			goto err;
		}

		i2c_ctrl->dump_reg_count = size / sizeof(*i2c_ctrl->dump_regs);

		ret = of_property_read_u32_array(i2c_client_node, "dump-regs",
						 i2c_ctrl->dump_regs,
						 i2c_ctrl->dump_reg_count);
		if (ret)
			i2c_ctrl->dump_reg_count = 0;
	}

	of_get_property(i2c_client_node, "reg_update", &size);

	if (size) {
		i2c_ctrl->reg_update = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!i2c_ctrl->reg_update) {
			ret = -ENOMEM;
			goto err;
		}

		i2c_ctrl->reg_update_count = size / sizeof(*i2c_ctrl->reg_update);

		ret = of_property_read_u32_array(i2c_client_node,
						"reg_update",
						(unsigned int *)i2c_ctrl->reg_update,
						size/sizeof(i2c_ctrl->reg_update->offset));
		if (ret)
			i2c_ctrl->reg_update_count = 0;
	}

	of_get_property(i2c_client_node, "switch_reg_update", &size);

	if (size) {
		i2c_ctrl->switch_reg_update = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!i2c_ctrl->switch_reg_update) {
			ret = -ENOMEM;
			goto err;
		}

		i2c_ctrl->switch_reg_update_count = size / sizeof(*i2c_ctrl->switch_reg_update);

		ret = of_property_read_u32_array(i2c_client_node,
						"switch_reg_update",
						(unsigned int *)i2c_ctrl->switch_reg_update,
						size/sizeof(i2c_ctrl->switch_reg_update->offset));
		if (ret)
			i2c_ctrl->switch_reg_update_count = 0;
	}

	return 0;

err:
	of_node_put(of_node);

	return ret;
}
#endif

static void msm_pcie_read_dt(struct msm_pcie_dev_t *pcie_dev, int rc_idx,
					struct platform_device *pdev,
						struct device_node *of_node)
{
	int ret = 0;

	pcie_dev->l0s_supported = !of_property_read_bool(of_node,
				"qcom,no-l0s-supported");
	PCIE_DBG(pcie_dev, "L0s is %s supported.\n", pcie_dev->l0s_supported ?
		"" : "not");

	pcie_dev->l1_supported = !of_property_read_bool(of_node,
				"qcom,no-l1-supported");
	PCIE_DBG(pcie_dev, "L1 is %s supported.\n", pcie_dev->l1_supported ?
		"" : "not");

	pcie_dev->l1ss_supported = !of_property_read_bool(of_node,
				"qcom,no-l1ss-supported");
	PCIE_DBG(pcie_dev, "L1ss is %s supported.\n", pcie_dev->l1ss_supported ?
		"" : "not");

	pcie_dev->l1_1_aspm_supported = pcie_dev->l1ss_supported;
	pcie_dev->l1_2_aspm_supported = pcie_dev->l1ss_supported;
	pcie_dev->l1_1_pcipm_supported = pcie_dev->l1ss_supported;
	pcie_dev->l1_2_pcipm_supported = pcie_dev->l1ss_supported;

	pcie_dev->apss_based_l1ss_sleep = of_property_read_bool(of_node,
				"qcom,apss-based-l1ss-sleep");

	pcie_dev->no_client_based_bw_voting = of_property_read_bool(of_node,
				"qcom,no-client-based-bw-voting");

	of_property_read_u32(of_node, "qcom,l1-2-th-scale",
				&pcie_dev->l1_2_th_scale);
	of_property_read_u32(of_node, "qcom,l1-2-th-value",
				&pcie_dev->l1_2_th_value);
	PCIE_DBG(pcie_dev, "PCIe: RC%d: L1.2 threshold scale: %d value: %d.\n",
		pcie_dev->rc_idx, pcie_dev->l1_2_th_scale,
		pcie_dev->l1_2_th_value);

	of_property_read_u32(of_node, "qcom,device-vendor-id",
				&pcie_dev->device_vendor_id);

	pcie_dev->common_clk_en = of_property_read_bool(of_node,
				"qcom,common-clk-en");
	PCIE_DBG(pcie_dev, "Common clock is %s enabled.\n",
		pcie_dev->common_clk_en ? "" : "not");

	pcie_dev->clk_power_manage_en = of_property_read_bool(of_node,
				"qcom,clk-power-manage-en");
	PCIE_DBG(pcie_dev, "Clock power management is %s enabled.\n",
		pcie_dev->clk_power_manage_en ? "" : "not");

	pcie_dev->aux_clk_sync = !of_property_read_bool(of_node,
				"qcom,no-aux-clk-sync");
	PCIE_DBG(pcie_dev, "AUX clock is %s synchronous to Core clock.\n",
		pcie_dev->aux_clk_sync ? "" : "not");

	of_property_read_u32(of_node, "qcom,smmu-sid-base",
				&pcie_dev->smmu_sid_base);
	PCIE_DBG(pcie_dev, "RC%d: qcom,smmu-sid-base: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->smmu_sid_base);

	of_property_read_u32(of_node, "qcom,boot-option",
				&pcie_dev->boot_option);
	PCIE_DBG(pcie_dev, "PCIe: RC%d boot option is 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->boot_option);

	of_property_read_u32(of_node, "qcom,pcie-phy-ver",
				&pcie_dev->phy_ver);
	PCIE_DBG(pcie_dev, "RC%d: pcie-phy-ver: %d.\n", pcie_dev->rc_idx,
		pcie_dev->phy_ver);

	pcie_dev->link_check_max_count = LINK_UP_CHECK_MAX_COUNT;
	of_property_read_u32(pdev->dev.of_node,
				"qcom,link-check-max-count",
				&pcie_dev->link_check_max_count);
	PCIE_DBG(pcie_dev, "PCIe: RC%d: link-check-max-count: %u.\n",
		pcie_dev->rc_idx, pcie_dev->link_check_max_count);

	of_property_read_u32(of_node, "qcom,target-link-speed",
				&pcie_dev->dt_target_link_speed);
	PCIE_DBG(pcie_dev, "PCIe: RC%d: target-link-speed: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->dt_target_link_speed);

	pcie_dev->target_link_speed = pcie_dev->dt_target_link_speed;

	msm_pcie_dev[rc_idx].target_link_width = 0;
	of_property_read_u32(of_node, "qcom,target-link-width",
			     &pcie_dev->target_link_width);
	PCIE_DBG(pcie_dev, "PCIe: RC%d: target-link-width: %d.\n",
		 pcie_dev->rc_idx, pcie_dev->target_link_width);

	of_property_read_u32(of_node, "qcom,n-fts", &pcie_dev->n_fts);
	PCIE_DBG(pcie_dev, "n-fts: 0x%x.\n", pcie_dev->n_fts);

	of_property_read_u32(of_node, "qcom,ep-latency",
				&pcie_dev->ep_latency);
	PCIE_DBG(pcie_dev, "RC%d: ep-latency: %ums.\n", pcie_dev->rc_idx,
		pcie_dev->ep_latency);

	of_property_read_u32(of_node, "qcom,switch-latency",
				&pcie_dev->switch_latency);
	PCIE_DBG(pcie_dev, "RC%d: switch-latency: %ums.\n", pcie_dev->rc_idx,
		pcie_dev->switch_latency);

	ret = of_property_read_u32(of_node, "qcom,wr-halt-size",
				&pcie_dev->wr_halt_size);
	if (ret)
		PCIE_DBG(pcie_dev,
			"RC%d: wr-halt-size not specified in dt. Use default value.\n",
			pcie_dev->rc_idx);
	else
		PCIE_DBG(pcie_dev, "RC%d: wr-halt-size: 0x%x.\n",
			pcie_dev->rc_idx, pcie_dev->wr_halt_size);

	pcie_dev->gdsc_clk_drv_ss_nonvotable = of_property_read_bool(of_node,
							"qcom,gdsc-clk-drv-ss-nonvotable");
	PCIE_DBG(pcie_dev, "Gdsc clk is %s votable during drv hand over.\n",
			pcie_dev->gdsc_clk_drv_ss_nonvotable ? "not" : "");

	pcie_dev->slv_addr_space_size = SZ_16M;
	of_property_read_u32(of_node, "qcom,slv-addr-space-size",
				&pcie_dev->slv_addr_space_size);
	PCIE_DBG(pcie_dev, "RC%d: slv-addr-space-size: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->slv_addr_space_size);

	of_property_read_u32(of_node, "qcom,num-parf-testbus-sel",
				&pcie_dev->num_parf_testbus_sel);
	PCIE_DBG(pcie_dev, "RC%d: num-parf-testbus-sel: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->num_parf_testbus_sel);

	pcie_dev->pcie_bdf_halt_dis = of_property_read_bool(of_node,
			"qcom,bdf-halt-dis");
	PCIE_DBG(pcie_dev, "PCIe BDF halt feature is %s enabled.\n",
			pcie_dev->pcie_bdf_halt_dis ? "not" : "");

	pcie_dev->pcie_halt_feature_dis = of_property_read_bool(of_node,
			"qcom,pcie-halt-feature-dis");
	PCIE_DBG(pcie_dev, "PCIe halt feature is %s enabled.\n",
			pcie_dev->pcie_halt_feature_dis ? "not" : "");

	of_property_read_u32(of_node, "qcom,phy-status-offset",
				&pcie_dev->phy_status_offset);
	PCIE_DBG(pcie_dev, "RC%d: phy-status-offset: 0x%x.\n", pcie_dev->rc_idx,
		pcie_dev->phy_status_offset);

	of_property_read_u32(pdev->dev.of_node, "qcom,phy-status-bit",
				&pcie_dev->phy_status_bit);
	PCIE_DBG(pcie_dev, "RC%d: phy-status-bit: %u.\n", pcie_dev->rc_idx,
		pcie_dev->phy_status_bit);

	of_property_read_u32(of_node, "qcom,phy-power-down-offset",
				&pcie_dev->phy_power_down_offset);
	PCIE_DBG(pcie_dev, "RC%d: phy-power-down-offset: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->phy_power_down_offset);

	of_property_read_u32(of_node, "qcom,phy-aux-clk-config1-offset",
				&pcie_dev->phy_aux_clk_config1_offset);
	PCIE_DBG(pcie_dev, "RC%d: phy-aux-clk-config1-offset: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->phy_aux_clk_config1_offset);

	of_property_read_u32(of_node, "qcom,phy-pll-clk-enable1-offset",
				&pcie_dev->phy_pll_clk_enable1_offset);
	PCIE_DBG(pcie_dev, "RC%d: phy-pll-clk-enable1-offset: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->phy_pll_clk_enable1_offset);

	of_property_read_u32(pdev->dev.of_node,
				"qcom,eq-pset-req-vec",
				&pcie_dev->eq_pset_req_vec);
	PCIE_DBG(pcie_dev, "RC%d: eq-pset-req-vec: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->eq_pset_req_vec);

	pcie_dev->core_preset = PCIE_GEN3_PRESET_DEFAULT;
	of_property_read_u32(pdev->dev.of_node,
				"qcom,core-preset",
				&pcie_dev->core_preset);
	PCIE_DBG(pcie_dev, "RC%d: core-preset: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->core_preset);

	of_property_read_u32(pdev->dev.of_node,
				"qcom,eq-fmdc-t-min-phase23",
				&pcie_dev->eq_fmdc_t_min_phase23);
	PCIE_DBG(pcie_dev, "RC%d: qcom,eq-fmdc-t-min-phase23: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->eq_fmdc_t_min_phase23);

	of_property_read_u32(of_node, "qcom,cpl-timeout",
				&pcie_dev->cpl_timeout);
	PCIE_DBG(pcie_dev, "RC%d: cpl-timeout: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->cpl_timeout);

	pcie_dev->perst_delay_us_min = PERST_PROPAGATION_DELAY_US_MIN;
	pcie_dev->perst_delay_us_max = PERST_PROPAGATION_DELAY_US_MAX;
	of_property_read_u32(of_node, "qcom,perst-delay-us-min",
				&pcie_dev->perst_delay_us_min);
	of_property_read_u32(of_node, "qcom,perst-delay-us-max",
				&pcie_dev->perst_delay_us_max);
	PCIE_DBG(pcie_dev,
		"RC%d: perst-delay-us-min: %dus. perst-delay-us-max: %dus.\n",
		pcie_dev->rc_idx, pcie_dev->perst_delay_us_min,
		pcie_dev->perst_delay_us_max);

	pcie_dev->tlp_rd_size = PCIE_TLP_RD_SIZE;
	of_property_read_u32(of_node, "qcom,tlp-rd-size",
				&pcie_dev->tlp_rd_size);
	PCIE_DBG(pcie_dev, "RC%d: tlp-rd-size: 0x%x.\n", pcie_dev->rc_idx,
		pcie_dev->tlp_rd_size);

	ret = of_property_read_u32(of_node, "qcom,aux-clk-freq",
				&pcie_dev->aux_clk_freq);
	if (ret)
		PCIE_DBG(pcie_dev, "RC%d: using default aux clock frequency.\n",
			pcie_dev->rc_idx);
	else
		PCIE_DBG(pcie_dev, "RC%d: aux clock frequency: %d.\n",
			pcie_dev->rc_idx, pcie_dev->aux_clk_freq);

	pcie_dev->aer_enable = true;

	if (!of_find_property(of_node, "msi-map", NULL)) {
		PCIE_DBG(pcie_dev, "RC%d: LPI not supported.\n",
			 pcie_dev->rc_idx);
	} else {
		PCIE_DBG(pcie_dev, "RC%d: LPI supported.\n",
			 pcie_dev->rc_idx);
		pcie_dev->lpi_enable = true;
	}

	ret = of_property_read_u32(of_node, "qcom,pcie-clkreq-offset",
				&pcie_dev->pcie_parf_cesta_config);
	if (ret)
		pcie_dev->pcie_parf_cesta_config = 0;

	pcie_dev->config_recovery = of_property_read_bool(of_node,
							"qcom,config-recovery");
	if (pcie_dev->config_recovery) {
		PCIE_DUMP(pcie_dev,
			  "PCIe RC%d config space recovery enabled\n",
			  pcie_dev->rc_idx);
		INIT_WORK(&pcie_dev->link_recover_wq, handle_link_recover);
	}

	of_property_read_u32(of_node, "qcom,l1ss-sleep-disable",
					&pcie_dev->l1ss_sleep_disable);

	ret = of_property_read_u32(of_node, "qcom,drv-l1ss-timeout-us",
					&pcie_dev->l1ss_timeout_us);
	if (ret)
		pcie_dev->l1ss_timeout_us = L1SS_TIMEOUT_US;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: DRV L1ss timeout: %dus\n",
			pcie_dev->rc_idx, pcie_dev->l1ss_timeout_us);

	ret = of_property_read_string(of_node, "qcom,drv-name",
				      &pcie_dev->drv_name);
	if (!ret) {
		pcie_dev->drv_supported = true;
		ret = msm_pcie_setup_drv(pcie_dev, of_node);
		if (ret)
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: DRV: failed to setup DRV: ret: %d\n",
				pcie_dev->rc_idx, ret);
	}

	pcie_dev->panic_genspeed_mismatch = of_property_read_bool(of_node,
						"qcom,panic-genspeed-mismatch");
}

static int msm_pcie_cesta_init(struct msm_pcie_dev_t *pcie_dev,
					struct device_node *of_node)
{
	int ret = 0;

	ret = of_property_read_u32(of_node, "qcom,pcie-clkreq-gpio",
			&pcie_dev->clkreq_gpio);
	if (ret) {
		PCIE_ERR(pcie_dev, "Couldn't find clkreq gpio %d\n",
								ret);
		return ret;
	}

	ret = msm_pcie_cesta_get_sm_seq(pcie_dev);
	if (ret)
		return ret;

	msm_pcie_cesta_load_sm_seq(pcie_dev);

	pcie_dev->crm_dev = crm_get_device("pcie_crm");

	if (IS_ERR(pcie_dev->crm_dev)) {
		PCIE_ERR(pcie_dev, "PCIe: RC%d: fail to get crm_dev\n",
				pcie_dev->rc_idx);
		return ret;
	}

	msm_pcie_cesta_map_save(pcie_dev->bw_gen_max);
	INIT_WORK(&pcie_drv.drv_connect,
			msm_pcie_drv_cesta_connect_worker);
	pcie_dev->drv_supported = true;

	return 0;
}

static void msm_pcie_get_pinctrl(struct msm_pcie_dev_t *pcie_dev,
					struct platform_device *pdev)
{
	pcie_dev->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(pcie_dev->pinctrl))
		PCIE_ERR(pcie_dev, "PCIe: RC%d failed to get pinctrl\n",
		pcie_dev->rc_idx);
	else
		pcie_dev->use_pinctrl = true;

	if (pcie_dev->use_pinctrl) {
		pcie_dev->pins_default = pinctrl_lookup_state(pcie_dev->pinctrl,
								"default");
		if (IS_ERR(pcie_dev->pins_default)) {
			PCIE_ERR(pcie_dev,
				"PCIe: RC%d could not get pinctrl default state\n",
				pcie_dev->rc_idx);
			pcie_dev->pins_default = NULL;
		}

		pcie_dev->pins_sleep = pinctrl_lookup_state(pcie_dev->pinctrl,
								"sleep");
		if (IS_ERR(pcie_dev->pins_sleep)) {
			PCIE_ERR(pcie_dev,
				"PCIe: RC%d could not get pinctrl sleep state\n",
				pcie_dev->rc_idx);
			pcie_dev->pins_sleep = NULL;
		}
	}
}

static int msm_pcie_probe(struct platform_device *pdev)
{
	int ret = 0;
	int rc_idx = -1, size;
	struct msm_pcie_dev_t *pcie_dev;
	struct device_node *of_node;

	dev_info(&pdev->dev, "PCIe: %s\n", __func__);

	mutex_lock(&pcie_drv.drv_lock);

	of_node = pdev->dev.of_node;

	ret = of_property_read_u32(of_node, "cell-index", &rc_idx);
	if (ret) {
		dev_err(&pdev->dev, "PCIe: %s: Did not find RC index\n",
			__func__);
		goto out;
	}

	if (rc_idx >= MAX_RC_NUM)
		goto out;

	pcie_drv.rc_num++;
	pcie_dev = &msm_pcie_dev[rc_idx];
	pcie_dev->rc_idx = rc_idx;
	pcie_dev->pdev = pdev;
	pcie_dev->link_status = MSM_PCIE_LINK_DEINIT;

	PCIE_DBG(pcie_dev, "PCIe: RC index is %d.\n", pcie_dev->rc_idx);

	msm_pcie_read_dt(pcie_dev, rc_idx, pdev, of_node);

	memcpy(pcie_dev->vreg, msm_pcie_vreg_info, sizeof(msm_pcie_vreg_info));
	memcpy(pcie_dev->gpio, msm_pcie_gpio_info, sizeof(msm_pcie_gpio_info));
	memcpy(pcie_dev->res, msm_pcie_res_info, sizeof(msm_pcie_res_info));
	memcpy(pcie_dev->irq, msm_pcie_irq_info, sizeof(msm_pcie_irq_info));
	memcpy(pcie_dev->reset, msm_pcie_reset_info[rc_idx],
		sizeof(msm_pcie_reset_info[rc_idx]));
	memcpy(pcie_dev->pipe_reset, msm_pcie_pipe_reset_info[rc_idx],
		sizeof(msm_pcie_pipe_reset_info[rc_idx]));
	memcpy(pcie_dev->linkdown_reset, msm_pcie_linkdown_reset_info[rc_idx],
		sizeof(msm_pcie_linkdown_reset_info[rc_idx]));

	init_completion(&pcie_dev->speed_change_completion);

	dev_set_drvdata(&pdev->dev, pcie_dev);

#if IS_ENABLED(CONFIG_I2C)
	ret = msm_pcie_i2c_ctrl_init(pcie_dev);
	if (ret)
		goto decrease_rc_num;
#endif

	ret = msm_pcie_get_resources(pcie_dev, pcie_dev->pdev);
	if (ret)
		goto decrease_rc_num;

	if (pcie_dev->rumi)
		pcie_dev->rumi_init = msm_pcie_rumi_init;

	if (pcie_dev->pcie_sm) {
		PCIE_DBG(pcie_dev, "pcie CESTA is supported\n");

		ret = msm_pcie_cesta_init(pcie_dev, of_node);
		if (ret)
			goto decrease_rc_num;

	} else {
		if (pcie_dev->drv_name || pcie_dev->drv_supported) {
			ret = register_rpmsg_driver(&msm_pcie_drv_rpmsg_driver);
			if (ret && ret != -EBUSY)
				PCIE_ERR(pcie_dev,
					"PCIe %d: DRV: rpmsg register fail: ret: %d\n",
								pcie_dev->rc_idx, ret);
		}
	}

	msm_pcie_get_pinctrl(pcie_dev, pdev);

	ret = msm_pcie_gpio_init(pcie_dev);
	if (ret) {
		msm_pcie_release_resources(pcie_dev);
		goto decrease_rc_num;
	}

	ret = msm_pcie_irq_init(pcie_dev);
	if (ret) {
		msm_pcie_release_resources(pcie_dev);
		msm_pcie_gpio_deinit(pcie_dev);
		goto decrease_rc_num;
	}

	INIT_KFIFO(pcie_dev->aer_fifo);

	msm_pcie_sysfs_init(pcie_dev);

	pcie_dev->drv_ready = true;

	of_get_property(pdev->dev.of_node, "qcom,filtered-bdfs", &size);
	if (size) {
		pcie_dev->filtered_bdfs = devm_kzalloc(&pdev->dev, size,
						       GFP_KERNEL);
		if (!pcie_dev->filtered_bdfs) {
			mutex_unlock(&pcie_drv.drv_lock);
			return -ENOMEM;
		}
		pcie_dev->bdf_count = size / sizeof(*pcie_dev->filtered_bdfs);

		ret = of_property_read_u32_array(pdev->dev.of_node,
						 "qcom,filtered-bdfs",
						 pcie_dev->filtered_bdfs,
						 pcie_dev->bdf_count);
		if (ret)
			pcie_dev->bdf_count = 0;
	}

	if (pcie_dev->boot_option & MSM_PCIE_NO_PROBE_ENUMERATION) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d will be enumerated by client or endpoint.\n",
			pcie_dev->rc_idx);
		mutex_unlock(&pcie_drv.drv_lock);
		return 0;
	}

	ret = msm_pcie_enumerate(rc_idx);
	if (ret)
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d is not enabled during bootup; it will be enumerated upon client request.\n",
			pcie_dev->rc_idx);
	else
		PCIE_ERR(pcie_dev, "RC%d is enabled in bootup\n",
			pcie_dev->rc_idx);

	PCIE_DBG(pcie_dev, "PCIe probed %s\n", dev_name(&pdev->dev));

	mutex_unlock(&pcie_drv.drv_lock);
	return 0;

decrease_rc_num:
	pcie_drv.rc_num--;
	PCIE_ERR(pcie_dev, "PCIe: RC%d: Driver probe failed. ret: %d\n",
		pcie_dev->rc_idx, ret);
out:
	if (rc_idx < 0 || rc_idx >= MAX_RC_NUM)
		pr_err("PCIe: Invalid RC index %d. Driver probe failed\n",
			rc_idx);

	mutex_unlock(&pcie_drv.drv_lock);

	return ret;
}

static int msm_pcie_remove(struct platform_device *pdev)
{
	int ret = 0;
	int rc_idx;
	struct msm_pcie_device_info *dev_info, *temp;

	mutex_lock(&pcie_drv.drv_lock);

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"cell-index", &rc_idx);
	if (ret) {
		pr_err("%s: Did not find RC index.\n", __func__);
		goto out;
	} else {
		pcie_drv.rc_num--;
		dev_info(&pdev->dev, "PCIe: RC%d: being removed\n", rc_idx);
	}

	if (msm_pcie_dev[rc_idx].saved_state)
		pci_load_and_free_saved_state(msm_pcie_dev[rc_idx].dev,
				      &msm_pcie_dev[rc_idx].saved_state);

	if (msm_pcie_dev[rc_idx].default_state)
		pci_load_and_free_saved_state(msm_pcie_dev[rc_idx].dev,
				      &msm_pcie_dev[rc_idx].default_state);

	/* Use CESTA to turn off the resources */
	if (msm_pcie_dev[rc_idx].pcie_sm)
		msm_pcie_cesta_map_apply(&msm_pcie_dev[rc_idx], D3COLD_STATE);

	msm_pcie_irq_deinit(&msm_pcie_dev[rc_idx]);
	msm_pcie_vreg_deinit(&msm_pcie_dev[rc_idx]);
	msm_pcie_clk_deinit(&msm_pcie_dev[rc_idx]);
	msm_pcie_gdsc_deinit(&msm_pcie_dev[rc_idx]);
	msm_pcie_gpio_deinit(&msm_pcie_dev[rc_idx]);
	msm_pcie_release_resources(&msm_pcie_dev[rc_idx]);

	list_for_each_entry_safe(dev_info, temp,
				 &msm_pcie_dev[rc_idx].enum_ep_list,
				 pcidev_node) {
		list_del(&dev_info->pcidev_node);
		kfree(dev_info);
	}

	list_for_each_entry_safe(dev_info, temp,
				 &msm_pcie_dev[rc_idx].susp_ep_list,
				 pcidev_node) {
		list_del(&dev_info->pcidev_node);
		kfree(dev_info);
	}

out:
	mutex_unlock(&pcie_drv.drv_lock);

	return ret;
}

static int msm_pcie_link_retrain(struct msm_pcie_dev_t *pcie_dev,
				struct pci_dev *pci_dev)
{
	u32 cnt_max = 100; /* 100ms timeout */
	u32 link_status;
	u32 link_status_lbms_mask = PCI_EXP_LNKSTA_LBMS << PCI_EXP_LNKCTL;
	int ret, status;

	/* Enable configuration for bandwidth interrupt */
	msm_pcie_config_bandwidth_int(pcie_dev, true);
	reinit_completion(&pcie_dev->speed_change_completion);

	/* link retrain */
	msm_pcie_config_clear_set_dword(pci_dev,
					pci_dev->pcie_cap + PCI_EXP_LNKCTL,
					0, PCI_EXP_LNKCTL_RL);

	ret = wait_for_completion_timeout(&pcie_dev->speed_change_completion,
					  msecs_to_jiffies(cnt_max));
	if (!ret) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: Bandwidth int: completion timeout\n",
			 pcie_dev->rc_idx);
		/* poll to check if link train is done */
		if (!(readl_relaxed(pcie_dev->dm_core + pci_dev->pcie_cap +
		      PCI_EXP_LNKCTL) & link_status_lbms_mask)) {
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: failed to retrain\n",
				 pcie_dev->rc_idx);
			return -EIO;
		}

		status = (readl_relaxed(pcie_dev->dm_core + pci_dev->pcie_cap +
			  PCI_EXP_LNKCTL) & link_status_lbms_mask);
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: Status set 0x%x\n",
			 pcie_dev->rc_idx, status);
	}

	link_status = readl_relaxed(pcie_dev->dm_core +
				    PCIE20_CAP_LINKCTRLSTATUS);
	pcie_dev->current_link_speed = (link_status >> 16) & PCI_EXP_LNKSTA_CLS;
	pcie_dev->current_link_width = ((link_status >> 16) & PCI_EXP_LNKSTA_NLW) >>
					PCI_EXP_LNKSTA_NLW_SHIFT;

	return 0;
}

static int msm_pcie_set_link_width(struct msm_pcie_dev_t *pcie_dev,
				   u16 target_link_width)
{
	u16 link_width;

	if (pcie_dev->target_link_width &&
	    (pcie_dev->target_link_width > pcie_dev->link_width_max))
		goto invalid_link_width;

	switch (target_link_width) {
	case PCI_EXP_LNKSTA_NLW_X1:
		link_width = LINK_WIDTH_X1;
		break;
	case PCI_EXP_LNKSTA_NLW_X2:
		link_width = LINK_WIDTH_X2;
		break;
	default:
		goto invalid_link_width;
	}

	msm_pcie_write_reg_field(pcie_dev->dm_core,
				 PCIE20_PORT_LINK_CTRL_REG,
				 LINK_WIDTH_MASK << LINK_WIDTH_SHIFT,
				 link_width);

	/* Set NUM_OF_LANES in GEN2_CTRL_OFF */
	msm_pcie_write_reg_field(pcie_dev->dm_core,
				 PCIE_GEN3_GEN2_CTRL,
				 NUM_OF_LANES_MASK << NUM_OF_LANES_SHIFT,
				 link_width);

	/* enable write access to RO register */
	msm_pcie_write_mask(pcie_dev->dm_core + PCIE_GEN3_MISC_CONTROL, 0,
			    BIT(0));

	/* Set Maximum link width as current width */
	msm_pcie_write_reg_field(pcie_dev->dm_core, PCIE20_CAP + PCI_EXP_LNKCAP,
				 PCI_EXP_LNKCAP_MLW, link_width);

	/* disable write access to RO register */
	msm_pcie_write_mask(pcie_dev->dm_core + PCIE_GEN3_MISC_CONTROL, BIT(0),
			    0);

	pcie_dev->link_width_max =
		(readl_relaxed(pcie_dev->dm_core + PCIE20_CAP + PCI_EXP_LNKCAP) &
			       PCI_EXP_LNKCAP_MLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
	PCIE_DBG(pcie_dev,
		 "PCIe: RC%d: updated maximum link width supported to: %d\n",
		 pcie_dev->rc_idx, pcie_dev->link_width_max);

	return 0;

invalid_link_width:
	PCIE_ERR(pcie_dev,
		 "PCIe: RC%d: unsupported link width request: %d, Max: %d\n",
		 pcie_dev->rc_idx,
		 target_link_width >> PCI_EXP_LNKSTA_NLW_SHIFT,
		 pcie_dev->link_width_max);

	return -EINVAL;
}

int msm_pcie_dsp_link_control(struct pci_dev *pci_dev,
			      bool link_enable)
{
	int ret = 0;
	struct pci_dev *dsp_dev = NULL;
	u16 link_control = 0;
	u16 link_status = 0;
	u32 link_capability = 0;
	int link_check_count = 0;
	bool link_trained = false;
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(pci_dev->bus);

	if (!pcie_dev->power_on)
		return 0;

	dsp_dev = pci_dev->bus->self;
	if (pci_pcie_type(dsp_dev) != PCI_EXP_TYPE_DOWNSTREAM) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: no DSP<->EP link under this RC\n",
			pcie_dev->rc_idx);
		return 0;
	}

	pci_read_config_dword(dsp_dev, dsp_dev->pcie_cap + PCI_EXP_LNKCAP,
			      &link_capability);
	pci_read_config_word(dsp_dev, dsp_dev->pcie_cap + PCI_EXP_LNKCTL,
			     &link_control);

	if (link_enable) {
		link_control &= ~PCI_EXP_LNKCTL_LD;
		pci_write_config_word(dsp_dev,
				      dsp_dev->pcie_cap + PCI_EXP_LNKCTL,
				      link_control);
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: DSP<->EP Link is enabled\n",
			pcie_dev->rc_idx);

		/* Wait for up to 100ms for the link to come up */
		do {
			usleep_range(LINK_UP_TIMEOUT_US_MIN,
				     LINK_UP_TIMEOUT_US_MAX);
			pci_read_config_word(dsp_dev,
					     dsp_dev->pcie_cap + PCI_EXP_LNKSTA,
					     &link_status);
			if (link_capability & PCI_EXP_LNKCAP_DLLLARC)
				link_trained = (!(link_status &
						  PCI_EXP_LNKSTA_LT)) &&
						(link_status &
						 PCI_EXP_LNKSTA_DLLLA);
			else
				link_trained = !(link_status &
						 PCI_EXP_LNKSTA_LT);

			if (link_trained)
				break;
		} while (link_check_count++ < LINK_UP_CHECK_MAX_COUNT);

		if (link_trained) {
			PCIE_DBG(pcie_dev,
				"PCIe: RC%d: DSP<->EP link status: 0x%04x\n",
				pcie_dev->rc_idx, link_status);
			PCIE_DBG(pcie_dev,
				"PCIe: RC%d: DSP<->EP Link is up after %d checkings\n",
				pcie_dev->rc_idx, link_check_count);
		} else {
			PCIE_DBG(pcie_dev, "DSP<->EP link initialization failed\n");
			ret = MSM_PCIE_ERROR;
		}
	} else {
		link_control |= PCI_EXP_LNKCTL_LD;
		pci_write_config_word(dsp_dev,
				      dsp_dev->pcie_cap + PCI_EXP_LNKCTL,
				      link_control);
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: DSP<->EP Link is disabled\n",
			pcie_dev->rc_idx);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(msm_pcie_dsp_link_control);

void msm_pcie_allow_l1(struct pci_dev *pci_dev)
{
	struct pci_dev *root_pci_dev;
	struct msm_pcie_dev_t *pcie_dev;

	root_pci_dev = pcie_find_root_port(pci_dev);
	if (!root_pci_dev)
		return;

	pcie_dev = PCIE_BUS_PRIV_DATA(root_pci_dev->bus);

	mutex_lock(&pcie_dev->aspm_lock);
	if (pcie_dev->debugfs_l1) {
		PCIE_DBG2(pcie_dev,
			"PCIe: RC%d: debugfs_l1 is set so no-op\n",
			pcie_dev->rc_idx);
		mutex_unlock(&pcie_dev->aspm_lock);
		return;
	}

	if (!pcie_dev->l1_supported) {
		PCIE_DBG2(pcie_dev,
			"PCIe: RC%d: %02x:%02x.%01x: l1 not supported\n",
			pcie_dev->rc_idx, pci_dev->bus->number,
			PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
		mutex_unlock(&pcie_dev->aspm_lock);
		return;
	}

	/* Reject the allow_l1 call if we are already in drv state */
	if (pcie_dev->link_status == MSM_PCIE_LINK_DRV) {
		PCIE_DBG2(pcie_dev, "PCIe: RC%d: %02x:%02x.%01x: Error\n",
				pcie_dev->rc_idx, pci_dev->bus->number,
				PCI_SLOT(pci_dev->devfn),
				PCI_FUNC(pci_dev->devfn));
		mutex_unlock(&pcie_dev->aspm_lock);
		return;
	}

	if (unlikely(--pcie_dev->prevent_l1 < 0))
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d: %02x:%02x.%01x: unbalanced prevent_l1: %d < 0\n",
			pcie_dev->rc_idx, pci_dev->bus->number,
			PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn),
			pcie_dev->prevent_l1);

	if (pcie_dev->prevent_l1) {
		mutex_unlock(&pcie_dev->aspm_lock);
		return;
	}

	msm_pcie_write_mask(pcie_dev->parf + PCIE20_PARF_PM_CTRL, BIT(5), 0);
	/* enable L1 */
	msm_pcie_write_mask(pcie_dev->dm_core +
				(root_pci_dev->pcie_cap + PCI_EXP_LNKCTL),
				0, PCI_EXP_LNKCTL_ASPM_L1);

	PCIE_DBG2(pcie_dev, "PCIe: RC%d: %02x:%02x.%01x: exit\n",
		pcie_dev->rc_idx, pci_dev->bus->number,
		PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
	mutex_unlock(&pcie_dev->aspm_lock);
}
EXPORT_SYMBOL(msm_pcie_allow_l1);

int msm_pcie_prevent_l1(struct pci_dev *pci_dev)
{
	struct pci_dev *root_pci_dev;
	struct msm_pcie_dev_t *pcie_dev;
	u32 cnt = 0;
	u32 cnt_max = 1000; /* 100ms timeout */
	int ret = 0;

	root_pci_dev = pcie_find_root_port(pci_dev);
	if (!root_pci_dev)
		return -ENODEV;

	pcie_dev = PCIE_BUS_PRIV_DATA(root_pci_dev->bus);

	/* disable L1 */
	mutex_lock(&pcie_dev->aspm_lock);
	if (pcie_dev->debugfs_l1) {
		PCIE_DBG2(pcie_dev,
			"PCIe: RC%d: debugfs_l1 is set so no-op\n",
			pcie_dev->rc_idx);
		mutex_unlock(&pcie_dev->aspm_lock);
		return 0;
	}

	if (!pcie_dev->l1_supported) {
		PCIE_DBG2(pcie_dev,
			"PCIe: RC%d: %02x:%02x.%01x: L1 not supported\n",
			pcie_dev->rc_idx, pci_dev->bus->number,
			PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
		mutex_unlock(&pcie_dev->aspm_lock);
		return 0;
	}

	/* Reject the prevent_l1 call if we are already in drv state */
	if (pcie_dev->link_status == MSM_PCIE_LINK_DRV) {
		ret = -EINVAL;
		PCIE_DBG2(pcie_dev, "PCIe: RC%d: %02x:%02x.%01x:ret %d exit\n",
				pcie_dev->rc_idx, pci_dev->bus->number,
				PCI_SLOT(pci_dev->devfn),
				PCI_FUNC(pci_dev->devfn), ret);
		mutex_unlock(&pcie_dev->aspm_lock);
		goto out;
	}

	if (pcie_dev->prevent_l1++) {
		mutex_unlock(&pcie_dev->aspm_lock);
		return 0;
	}

	msm_pcie_write_mask(pcie_dev->dm_core +
				(root_pci_dev->pcie_cap + PCI_EXP_LNKCTL),
				PCI_EXP_LNKCTL_ASPM_L1, 0);
	msm_pcie_write_mask(pcie_dev->parf + PCIE20_PARF_PM_CTRL, 0, BIT(5));

	/* confirm link is in L0/L0s */
	while (!msm_pcie_check_ltssm_state(pcie_dev, MSM_PCIE_LTSSM_L0) &&
		!msm_pcie_check_ltssm_state(pcie_dev, MSM_PCIE_LTSSM_L0S)) {
		if (unlikely(cnt++ >= cnt_max)) {
			PCIE_ERR(pcie_dev,
				"PCIe: RC%d: %02x:%02x.%01x: failed to transition to L0\n",
				pcie_dev->rc_idx, pci_dev->bus->number,
				PCI_SLOT(pci_dev->devfn),
				PCI_FUNC(pci_dev->devfn));

			PCIE_ERR(pcie_dev,
				"PCIe: RC%d: dump PCIe registers\n",
				pcie_dev->rc_idx);
			msm_pcie_clk_dump(pcie_dev);
			pcie_parf_dump(pcie_dev);
			pcie_dm_core_dump(pcie_dev);
			pcie_phy_dump(pcie_dev);
			pcie_sm_dump(pcie_dev);
			pcie_crm_dump(pcie_dev);
			ret = -EIO;
			goto err;
		}

		usleep_range(100, 105);
	}

	PCIE_DBG2(pcie_dev, "PCIe: RC%d: %02x:%02x.%01x: exit\n",
		pcie_dev->rc_idx, pci_dev->bus->number,
		PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
	mutex_unlock(&pcie_dev->aspm_lock);

	return 0;
err:
	mutex_unlock(&pcie_dev->aspm_lock);
	msm_pcie_allow_l1(pci_dev);

out:
	return ret;
}
EXPORT_SYMBOL(msm_pcie_prevent_l1);

static int msm_pcie_read_devid_all(struct pci_dev *pdev, void *dev)
{
	u16 device_id;

	pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);

	return 0;
}

static void msm_pcie_poll_for_l0_from_l0s(struct msm_pcie_dev_t *dev)
{
	if (!dev->l0s_supported)
		return;

	while (!msm_pcie_check_ltssm_state(dev, MSM_PCIE_LTSSM_L0))
		pci_walk_bus(dev->dev->bus, msm_pcie_read_devid_all, dev);
}

int msm_pcie_set_target_link_speed(u32 rc_idx, u32 target_link_speed,
				   bool force)
{
	struct msm_pcie_dev_t *pcie_dev;

	if (rc_idx >=  MAX_RC_NUM) {
		pr_err("PCIe: invalid rc index %u\n", rc_idx);
		return -EINVAL;
	}

	pcie_dev = &msm_pcie_dev[rc_idx];

	if (!pcie_dev->drv_ready) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: has not been successfully probed yet\n",
			pcie_dev->rc_idx);
		return -EPROBE_DEFER;
	}

	/*
	 * Reject the request if it exceeds what PCIe RC is capable or if
	 * it's greater than what was specified in DT (if present)
	 */
	if (target_link_speed > pcie_dev->bw_gen_max ||
		(pcie_dev->dt_target_link_speed && !force &&
		target_link_speed > pcie_dev->dt_target_link_speed)) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: invalid target link speed: %d\n",
			pcie_dev->rc_idx, target_link_speed);
		return -EINVAL;
	}

	pcie_dev->target_link_speed = target_link_speed;

	/*
	 * The request 0 will reset maximum GEN speed to default. Default will
	 * be devicetree specified GEN speed if present else it will be whatever
	 * the PCIe root complex is capable of.
	 */
	if (!target_link_speed) {
		pcie_dev->target_link_speed = pcie_dev->dt_target_link_speed ?
			pcie_dev->dt_target_link_speed : pcie_dev->bw_gen_max;
		if (force)
			pcie_dev->target_link_speed = pcie_dev->bw_gen_max;
	}

	PCIE_DBG(pcie_dev, "PCIe: RC%d: target_link_speed is now: 0x%x.\n",
		pcie_dev->rc_idx, pcie_dev->target_link_speed);

	return 0;
}
EXPORT_SYMBOL(msm_pcie_set_target_link_speed);

/**
 * msm_pcie_set_link_bandwidth() - will perform only dynamic GEN speed request
 * @target_link_speed: input the target link speed
 * @target_link_width: currently this API does not support dynamic link width change
 */
int msm_pcie_set_link_bandwidth(struct pci_dev *pci_dev, u16 target_link_speed,
				u16 target_link_width)
{
	struct pci_dev *root_pci_dev;
	struct msm_pcie_dev_t *pcie_dev;
	u16 link_status;
	u16 current_link_speed;
	u16 current_link_width;
	bool set_link_speed = true;
	int ret;

	if (!pci_dev)
		return -EINVAL;

	root_pci_dev = pcie_find_root_port(pci_dev);
	if (!root_pci_dev)
		return -ENODEV;

	pcie_dev = PCIE_BUS_PRIV_DATA(root_pci_dev->bus);

	if (target_link_speed > pcie_dev->bw_gen_max ||
		(pcie_dev->target_link_speed &&
		target_link_speed > pcie_dev->target_link_speed)) {
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: invalid target link speed: %d\n",
			pcie_dev->rc_idx, target_link_speed);
		return -EINVAL;
	}

	pcie_capability_read_word(root_pci_dev, PCI_EXP_LNKSTA, &link_status);

	current_link_speed = link_status & PCI_EXP_LNKSTA_CLS;
	current_link_width = link_status & PCI_EXP_LNKSTA_NLW;

	if (target_link_speed == current_link_speed)
		set_link_speed = false;
	else
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: switching from Gen%d to Gen%d\n",
			pcie_dev->rc_idx, current_link_speed,
			target_link_speed);

	if (!set_link_speed)
		return 0;

	PCIE_DBG(pcie_dev,
			"PCIe: RC%d: current link width:%d max link width:%d\n",
			pcie_dev->rc_idx,
			current_link_width >> PCI_EXP_LNKSTA_NLW_SHIFT,
			pcie_dev->link_width_max);

	if (set_link_speed)
		msm_pcie_config_clear_set_dword(root_pci_dev,
						root_pci_dev->pcie_cap +
						PCI_EXP_LNKCTL2,
						PCI_EXP_LNKCTL2_TLS,
						target_link_speed);

	/* need to be in L0 for gen switch */
	ret = msm_pcie_prevent_l1(root_pci_dev);
	if (ret)
		return ret;

	msm_pcie_config_l0s_disable_all(pcie_dev, root_pci_dev->bus);

	/* in case link is already in L0s bring link back to L0 */
	msm_pcie_poll_for_l0_from_l0s(pcie_dev);

	if (target_link_speed > current_link_speed)
		msm_pcie_scale_link_bandwidth(pcie_dev, target_link_speed);

	ret = msm_pcie_link_retrain(pcie_dev, root_pci_dev);
	if (ret)
		goto out;

	if (pcie_dev->current_link_speed != target_link_speed) {
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d: failed to switch bandwidth: target speed: %d\n",
			pcie_dev->rc_idx, target_link_speed);
		ret = -EIO;
		goto out;
	}

	if (target_link_speed < current_link_speed)
		msm_pcie_scale_link_bandwidth(pcie_dev, target_link_speed);

	msm_pcie_icc_vote(pcie_dev,
			pcie_dev->current_link_speed, pcie_dev->current_link_width, false);

	PCIE_DBG(pcie_dev, "PCIe: RC%d: successfully switched link bandwidth\n",
		pcie_dev->rc_idx);
out:
	if (ret) {
		/* Dump registers incase of the bandwidth switch failure */
		pcie_parf_dump(pcie_dev);
		pcie_dm_core_dump(pcie_dev);
		pcie_phy_dump(pcie_dev);
		pcie_sm_dump(pcie_dev);
		pcie_crm_dump(pcie_dev);
	}
	msm_pcie_config_l0s_enable_all(pcie_dev);
	msm_pcie_allow_l1(root_pci_dev);

	return ret;
}
EXPORT_SYMBOL(msm_pcie_set_link_bandwidth);

static int __maybe_unused msm_pcie_pm_suspend_noirq(struct device *dev)
{
	u32 val;
	int ret_l1ss, i, rc;
	unsigned long irqsave_flags;
	char ahb_clk[MAX_PROP_SIZE];
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)
						dev_get_drvdata(dev);

	PCIE_DBG(pcie_dev, "RC%d: entry\n", pcie_dev->rc_idx);

	scnprintf(ahb_clk, MAX_PROP_SIZE, "pcie_cfg_ahb_clk");

	mutex_lock(&pcie_dev->recovery_lock);
	if (pcie_dev->enumerated && pcie_dev->power_on &&
				pcie_dev->apss_based_l1ss_sleep) {

		/* Wait till link settle's in L1ss */
		ret_l1ss = readl_poll_timeout((pcie_dev->parf
			+ PCIE20_PARF_PM_STTS), val, (val & BIT(8)), L1SS_POLL_INTERVAL_US,
			L1SS_POLL_TIMEOUT_US);

		if (!ret_l1ss) {
			PCIE_DBG(pcie_dev, "RC%d: Link is in L1ss\n",
				pcie_dev->rc_idx);
		} else {
			PCIE_INFO(pcie_dev, "RC%d: Link is not in L1ss\n",
				pcie_dev->rc_idx);

			mutex_unlock(&pcie_dev->recovery_lock);

			PCIE_DBG(pcie_dev, "RC%d: exit\n", pcie_dev->rc_idx);

			return 0;
		}

		/* Keep the device in power off state */
		pcie_dev->power_on = false;

		/* Set flag to indicate client has suspended */
		pcie_dev->user_suspend = true;

		/* Set flag to indicate device has suspended */
		spin_lock_irqsave(&pcie_dev->irq_lock, irqsave_flags);
		pcie_dev->suspending = true;
		spin_unlock_irqrestore(&pcie_dev->irq_lock, irqsave_flags);

		/* Restrict access to config space */
		spin_lock_irqsave(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);
		pcie_dev->cfg_access = false;
		spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);

		/* suspend access to MSI register. resume access in resume */
		if (!pcie_dev->lpi_enable)
			msm_msi_config_access(dev_get_msi_domain(&pcie_dev->dev->dev),
					false);

		/*
		 * When GDSC is turned off, it will reset controller and it can assert
		 * clk-req GPIO. With assertion of CLKREQ gpio, endpoint tries to bring
		 * link back to L0, but since all clocks are turned off on host, this
		 * can result in link down.
		 *
		 * So, release the control of CLKREQ gpio from controller by overriding it.
		 */
		msm_pcie_write_reg(pcie_dev->parf, PCIE20_PARF_CLKREQ_OVERRIDE,
				PCIE20_PARF_CLKREQ_IN_ENABLE | PCIE20_PARF_CLKREQ_IN_VALUE);
		if (pcie_dev->use_pinctrl && pcie_dev->pins_sleep)
			pinctrl_select_state(pcie_dev->pinctrl,
						pcie_dev->pins_sleep);

		for (i = 0; i < pcie_dev->num_clk; i++)
			if (pcie_dev->clk[i].hdl && strcmp(pcie_dev->clk[i].name, ahb_clk))
				clk_disable_unprepare(pcie_dev->clk[i].hdl);

		rc = msm_pcie_icc_vote(pcie_dev, 0, 0, false);
		if (rc)
			goto out;

		/* switch phy aux clock mux to xo before turning off gdsc-core */
		if (pcie_dev->phy_aux_clk_mux && pcie_dev->ref_clk_src)
			clk_set_parent(pcie_dev->phy_aux_clk_mux, pcie_dev->ref_clk_src);

		/* switch pipe clock mux to xo before turning off gdsc */
		if (pcie_dev->pipe_clk_mux && pcie_dev->ref_clk_src)
			clk_set_parent(pcie_dev->pipe_clk_mux, pcie_dev->ref_clk_src);

		/* Disable the pipe clock*/
		msm_pcie_pipe_clk_deinit(pcie_dev);

		/* Shut off FLL */
		if (pcie_dev->phy_aux_clk_config1_offset)
			msm_pcie_write_reg_field(pcie_dev->phy,
						 pcie_dev->phy_aux_clk_config1_offset,
						 MSM_PCIE_PHY_SW_AUX_CLK_REQ,
						 MSM_PCIE_PHY_SW_AUX_CLK_REQ_VAL);

		/* Enable ext clk buf en to eliminate VDDA lekeage path*/
		if (pcie_dev->phy_pll_clk_enable1_offset)
			msm_pcie_write_reg_field(pcie_dev->phy,
						 pcie_dev->phy_pll_clk_enable1_offset,
						 MSM_PCIE_EXT_CLKBUF_EN_MUX,
						 MSM_PCIE_EXT_CLKBUF_EN_MUX_VAL);

		/* park the PCIe PHY in power down mode */
		if (pcie_dev->phy_power_down_offset)
			msm_pcie_write_reg(pcie_dev->phy, pcie_dev->phy_power_down_offset, 0);

		/* Turn off AHB clk as there won't be any more register access */
		clk_disable_unprepare(pcie_dev->ahb_clk);

		/* disable the controller GDSC*/
		regulator_disable(pcie_dev->gdsc_core);


		/* Disable the voltage regulators*/
		msm_pcie_vreg_deinit_analog_rails(pcie_dev);

	}

	mutex_unlock(&pcie_dev->recovery_lock);

	PCIE_DBG(pcie_dev, "RC%d: exit\n", pcie_dev->rc_idx);

	return 0;

out:
	mutex_unlock(&pcie_dev->recovery_lock);

	return rc;
}

static int __maybe_unused msm_pcie_pm_resume_noirq(struct device *dev)
{
	int i, rc;
	unsigned long irqsave_flags;
	char ahb_clk[MAX_PROP_SIZE];
	struct msm_pcie_dev_t *pcie_dev = (struct msm_pcie_dev_t *)
						dev_get_drvdata(dev);

	PCIE_DBG(pcie_dev, "RC%d: entry\n", pcie_dev->rc_idx);

	scnprintf(ahb_clk, MAX_PROP_SIZE, "pcie_cfg_ahb_clk");

	mutex_lock(&pcie_dev->recovery_lock);

	if (pcie_dev->enumerated && !pcie_dev->power_on &&
				pcie_dev->apss_based_l1ss_sleep) {

		/* Enable the voltage regulators*/
		msm_pcie_vreg_init_analog_rails(pcie_dev);

		 /* Enable GDSC core */
		rc = regulator_enable(pcie_dev->gdsc_core);
		if (rc) {
			PCIE_ERR(pcie_dev, "PCIe: fail to enable GDSC-CORE for RC%d (%s)\n",
					pcie_dev->rc_idx, pcie_dev->pdev->name);
			msm_pcie_vreg_deinit_analog_rails(pcie_dev);
			mutex_unlock(&pcie_dev->recovery_lock);
			return rc;
		}

		/* Turn on ahb clock first as its needed for register access */
		clk_prepare_enable(pcie_dev->ahb_clk);

		/* switch pipe clock source after gdsc-core is turned on */
		if (pcie_dev->pipe_clk_mux && pcie_dev->pipe_clk_ext_src)
			clk_set_parent(pcie_dev->pipe_clk_mux, pcie_dev->pipe_clk_ext_src);

		if (pcie_dev->phy_pll_clk_enable1_offset)
			msm_pcie_clear_set_reg(pcie_dev->phy, pcie_dev->phy_pll_clk_enable1_offset,
							MSM_PCIE_EXT_CLKBUF_EN_MUX, 0x0);

		if (pcie_dev->phy_aux_clk_config1_offset)
			msm_pcie_clear_set_reg(pcie_dev->phy, pcie_dev->phy_aux_clk_config1_offset,
						MSM_PCIE_PHY_SW_AUX_CLK_REQ, 0x0);

		/* Bring back PCIe PHY from power down */
		if (pcie_dev->phy_power_down_offset)
			msm_pcie_write_reg(pcie_dev->phy, pcie_dev->phy_power_down_offset,
				MSM_PCIE_PHY_SW_PWRDN | MSM_PCIE_PHY_REFCLK_DRV_DSBL);

		rc = msm_pcie_icc_vote(pcie_dev, pcie_dev->current_link_speed,
				pcie_dev->current_link_width, false);
		if (rc)
			goto out;

		for (i = 0; i < pcie_dev->num_clk; i++) {
			if (pcie_dev->clk[i].hdl && strcmp(pcie_dev->clk[i].name, ahb_clk)) {
				rc = clk_prepare_enable(pcie_dev->clk[i].hdl);
				if (rc)
					PCIE_ERR(pcie_dev, "PCIe: RC%d failed to enable clk %s\n",
						pcie_dev->rc_idx, pcie_dev->clk[i].name);
				else
					PCIE_DBG2(pcie_dev, "enable clk %s for RC%d.\n",
						pcie_dev->clk[i].name, pcie_dev->rc_idx);
			}
		}

		/* Enable pipe clocks */
		for (i = 0; i < pcie_dev->num_pipe_clk; i++)
			if (pcie_dev->pipe_clk[i].hdl)
				clk_prepare_enable(pcie_dev->pipe_clk[i].hdl);

		/* switch phy aux clock source from xo to phy aux clk */
		if (pcie_dev->phy_aux_clk_mux && pcie_dev->phy_aux_clk_ext_src)
			clk_set_parent(pcie_dev->phy_aux_clk_mux, pcie_dev->phy_aux_clk_ext_src);


		/* Disable the clkreq override functionality */
		msm_pcie_write_reg(pcie_dev->parf, PCIE20_PARF_CLKREQ_OVERRIDE, 0x0);
		if (pcie_dev->use_pinctrl && pcie_dev->pins_default)
			pinctrl_select_state(pcie_dev->pinctrl,
					pcie_dev->pins_default);

		/* Keep the device in power on state */
		pcie_dev->power_on = true;

		/* Clear flag to indicate client has resumed */
		pcie_dev->user_suspend = false;

		/* Clear flag to indicate device has resumed */
		spin_lock_irqsave(&pcie_dev->irq_lock, irqsave_flags);
		pcie_dev->suspending = false;
		spin_unlock_irqrestore(&pcie_dev->irq_lock, irqsave_flags);

		/* Allow access to config space */
		spin_lock_irqsave(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);
		pcie_dev->cfg_access = true;
		spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);

		/* resume access to MSI register as link is resumed */
		if (!pcie_dev->lpi_enable)
			msm_msi_config_access(dev_get_msi_domain(&pcie_dev->dev->dev),
						true);
	}

	mutex_unlock(&pcie_dev->recovery_lock);

	PCIE_DBG(pcie_dev, "RC%d: exit\n", pcie_dev->rc_idx);

	return 0;

out:
	if (pcie_dev->pipe_clk_ext_src && pcie_dev->pipe_clk_mux)
		clk_set_parent(pcie_dev->pipe_clk_ext_src, pcie_dev->pipe_clk_mux);
	regulator_disable(pcie_dev->gdsc_core);
	msm_pcie_vreg_deinit_analog_rails(pcie_dev);

	mutex_unlock(&pcie_dev->recovery_lock);

	return rc;
}

static const struct dev_pm_ops qcom_pcie_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(msm_pcie_pm_suspend_noirq, msm_pcie_pm_resume_noirq)
};

static int msm_pci_probe(struct pci_dev *pci_dev,
		  const struct pci_device_id *device_id)
{
	int ret;
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(pci_dev->bus);
	struct msm_root_dev_t *root_dev;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: PCI Probe\n", pcie_dev->rc_idx);

	if (!pci_dev->dev.of_node)
		return -ENODEV;

	root_dev = devm_kzalloc(&pci_dev->dev, sizeof(*root_dev), GFP_KERNEL);
	if (!root_dev)
		return -ENOMEM;

	root_dev->pcie_dev = pcie_dev;
	root_dev->pci_dev = pci_dev;
	dev_set_drvdata(&pci_dev->dev, root_dev);

	ret = dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(64));
	if (ret) {
		PCIE_ERR(pcie_dev, "DMA set mask failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

static struct pci_device_id msm_pci_device_id[] = {
	{PCI_DEVICE(0x17cb, 0x0108)},
	{PCI_DEVICE(0x17cb, 0x010b)},
	{PCI_DEVICE(0x17cb, 0x010c)},
	{0},
};

static struct pci_driver msm_pci_driver = {
	.name = "pci-msm-rc",
	.id_table = msm_pci_device_id,
	.probe = msm_pci_probe,
};

static const struct of_device_id msm_pcie_match[] = {
	{ .compatible = "qcom,pci-msm", },
	{}
};

static struct platform_driver msm_pcie_driver = {
	.probe	= msm_pcie_probe,
	.remove	= msm_pcie_remove,
	.driver	= {
		.name		= "pci-msm",
		.pm = &qcom_pcie_pm_ops,
		.of_match_table	= msm_pcie_match,
	},
};

static int msm_pcie_drv_rpmsg_probe(struct rpmsg_device *rpdev)
{
	mutex_lock(&pcie_drv.rpmsg_lock);
	pcie_drv.rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, &pcie_drv);
	mutex_unlock(&pcie_drv.rpmsg_lock);

	/* start drv connection */
	schedule_work(&pcie_drv.drv_connect);

	return 0;
}

static void msm_pcie_drv_notify_client(struct pcie_drv_sta *pcie_drv,
					enum msm_pcie_event event)
{
	struct msm_pcie_dev_t *pcie_dev = pcie_drv->msm_pcie_dev;
	int i;

	for (i = 0; i < MAX_RC_NUM; i++, pcie_dev++) {
		struct msm_pcie_drv_info *drv_info = pcie_dev->drv_info;

		PCIE_DBG(pcie_dev, "PCIe: RC%d: event %d received\n",
			pcie_dev->rc_idx, event);

		/* does not support DRV or has not been probed yet */
		if (!drv_info)
			continue;

		if (drv_info->ep_connected) {
			msm_pcie_notify_client(pcie_dev, event);
			if (event & MSM_PCIE_EVENT_DRV_DISCONNECT) {
				mutex_lock(&pcie_dev->drv_pc_lock);
				drv_info->ep_connected = false;
				cancel_work_sync(&pcie_dev->drv_disable_pc_work);
				cancel_work_sync(&pcie_dev->drv_enable_pc_work);
				mutex_unlock(&pcie_dev->drv_pc_lock);
			}
		}
	}
}

static void msm_pcie_drv_rpmsg_remove(struct rpmsg_device *rpdev)
{
	int ret;
	struct pcie_drv_sta *pcie_drv = dev_get_drvdata(&rpdev->dev);
	struct msm_pcie_dev_t *pcie_dev = pcie_drv->msm_pcie_dev;

	mutex_lock(&pcie_drv->rpmsg_lock);
	pcie_drv->rc_drv_enabled = 0;
	pcie_drv->rpdev = NULL;
	mutex_unlock(&pcie_drv->rpmsg_lock);

	flush_work(&pcie_drv->drv_connect);

	msm_pcie_drv_notify_client(pcie_drv, MSM_PCIE_EVENT_DRV_DISCONNECT);

	if (!pcie_drv->notifier)
		return;

	ret = qcom_unregister_ssr_notifier(pcie_drv->notifier, &pcie_drv->nb);
	if (ret)
		PCIE_ERR(pcie_dev, "PCIe: RC%d: DRV: error %d unregistering notifier\n",
			 pcie_dev->rc_idx, ret);

	pcie_drv->notifier = NULL;
}

static int msm_pcie_drv_rpmsg_cb(struct rpmsg_device *rpdev, void *data,
				int len, void *priv, u32 src)
{
	struct pcie_drv_sta *pcie_drv = dev_get_drvdata(&rpdev->dev);
	struct msm_pcie_dev_t *pcie_dev;
	struct msm_pcie_drv_header *drv_header;
	struct msm_pcie_drv_info *drv_info;

	while (len) {
		if (len < sizeof(*drv_header)) {
			pr_err("PCIe: DRV: invalid header length: %d\n",
				len);
			return -EINVAL;
		}

		drv_header = data;
		data += sizeof(*drv_header);
		len -= sizeof(*drv_header);

		if (drv_header->dev_id >= MAX_RC_NUM) {
			pr_err("PCIe: DRV: invalid device id: %d\n",
				drv_header->dev_id);
			return -EINVAL;
		}

		pcie_dev = pcie_drv->msm_pcie_dev + drv_header->dev_id;
		drv_info = pcie_dev->drv_info;
		if (!drv_info) {
			PCIE_ERR(pcie_dev,
				"PCIe: RC%d: DRV: no device info found\n",
				pcie_dev->rc_idx);
			return -ENODEV;
		}

		switch (drv_header->msg_id) {
		case MSM_PCIE_DRV_MSG_ID_ACK:
		{
			u32 *status;
			size_t status_size = sizeof(*status);

			if (drv_header->payload_size != status_size) {
				PCIE_ERR(pcie_dev,
					"PCIe: RC%d: DRV: invalid payload size: %d\n",
					pcie_dev->rc_idx,
					drv_header->payload_size);
				return -EINVAL;
			}

			if (len < status_size) {
				PCIE_ERR(pcie_dev,
					"PCIe: RC%d: DRV: invalid status length: %d\n",
					pcie_dev->rc_idx, len);
				return -EINVAL;
			}

			status = data;
			data += status_size;
			len -= status_size;

			if (drv_header->reply_seq != drv_info->reply_seq) {
				PCIE_ERR(pcie_dev,
					"PCIe: RC%d: DRV: incorrect reply seq: %d: expected seq: %d\n",
					pcie_dev->rc_idx,
					drv_header->reply_seq,
					drv_info->reply_seq);
				return -EINVAL;
			}

			if (*status) {
				PCIE_ERR(pcie_dev,
					"PCIe: RC%d: DRV: invalid status\n",
					pcie_dev->rc_idx);
				return -EINVAL;
			}

			complete(&drv_info->completion);
			break;
		}
		default:
			PCIE_ERR(pcie_dev,
				"PCIe: RC%d: DRV: unsupported command: 0x%x\n",
				pcie_dev->rc_idx, drv_header->msg_id);
			return -EINVAL;
		}
	}

	return 0;
}

static int msm_pcie_ssr_notifier(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct pcie_drv_sta *pcie_drv = container_of(nb, struct pcie_drv_sta,
						     nb);

	if (action == QCOM_SSR_BEFORE_SHUTDOWN) {
		pcie_drv->rc_drv_enabled = 0;
		pcie_drv->rpdev = NULL;
		msm_pcie_drv_notify_client(pcie_drv, MSM_PCIE_EVENT_WAKEUP);
	}

	return NOTIFY_OK;
};

static void msm_pcie_drv_disable_pc(struct work_struct *w)
{
	struct msm_pcie_dev_t *pcie_dev = container_of(w, struct msm_pcie_dev_t,
						drv_disable_pc_work);

	msm_pcie_drv_send_rpmsg(pcie_dev, &pcie_dev->drv_info->drv_disable_pc);
}

static void msm_pcie_drv_enable_pc(struct work_struct *w)
{
	struct msm_pcie_dev_t *pcie_dev = container_of(w, struct msm_pcie_dev_t,
						drv_enable_pc_work);

	msm_pcie_drv_send_rpmsg(pcie_dev, &pcie_dev->drv_info->drv_enable_pc);
}

static void msm_pcie_drv_connect_worker(struct work_struct *work)
{
	struct pcie_drv_sta *pcie_drv = container_of(work, struct pcie_drv_sta,
						     drv_connect);
	struct msm_pcie_dev_t *pcie_itr, *pcie_dev = pcie_drv->msm_pcie_dev;
	int i;

	/* rpmsg probe hasn't happened yet */
	if (!pcie_drv->rpdev)
		return;

	pcie_itr = pcie_dev;
	for (i = 0; i < MAX_RC_NUM; i++, pcie_itr++) {
		struct msm_pcie_drv_info *drv_info = pcie_itr->drv_info;

		/* does not support DRV or has not been probed yet */
		if (!drv_info || drv_info->ep_connected)
			continue;

		if (!msm_pcie_notify_client(pcie_itr,
					    MSM_PCIE_EVENT_DRV_CONNECT))
			continue;

		mutex_lock(&pcie_itr->drv_pc_lock);
		drv_info->ep_connected = true;

		if (pcie_itr->drv_disable_pc_vote)
			queue_work(mpcie_wq, &pcie_itr->drv_disable_pc_work);
		mutex_unlock(&pcie_itr->drv_pc_lock);
	}

	if (!pcie_dev->drv_name)
		return;
	pcie_drv->notifier = qcom_register_early_ssr_notifier(pcie_dev->drv_name, &pcie_drv->nb);
	if (IS_ERR(pcie_drv->notifier)) {
		PCIE_ERR(pcie_dev, "PCIe: RC%d: DRV: failed to register ssr notifier\n",
			 pcie_dev->rc_idx);
		pcie_drv->notifier = NULL;
	}
}

#if IS_ENABLED(CONFIG_I2C)
static const struct i2c_driver_data ntn3_data = {
	.client_id = I2C_CLIENT_ID_NTN3,
};

static const struct of_device_id of_i2c_id_table[] = {
	{ .compatible = "qcom,pcie-i2c-ntn3", .data = &ntn3_data },
	{}
};
MODULE_DEVICE_TABLE(of, of_i2c_id_table);

static int pcie_i2c_ctrl_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	int rc_index = -EINVAL;
	enum i2c_client_id client_id = I2C_CLIENT_ID_INVALID;
	struct pcie_i2c_ctrl *i2c_ctrl;
	const struct of_device_id *match;
	struct i2c_driver_data *data;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -EIO;
	}

	if (client->dev.of_node) {
		match = of_match_device(of_match_ptr(of_i2c_id_table),
					&client->dev);
		if (!match) {
			dev_err(&client->dev, "Error: No device match found\n");
			return -ENODEV;
		}

		data = (struct i2c_driver_data *)match->data;
		client_id = data->client_id;
	}

	of_property_read_u32(client->dev.of_node, "rc-index", &rc_index);

	dev_info(&client->dev, "%s: PCIe rc-index: 0x%X\n", __func__, rc_index);

	if (rc_index >= MAX_RC_NUM) {
		dev_err(&client->dev, "invalid RC index %d\n", rc_index);
		return -EINVAL;
	}

	if (client_id == I2C_CLIENT_ID_NTN3) {
		i2c_ctrl = &msm_pcie_dev[rc_index].i2c_ctrl;
		i2c_ctrl->client_i2c_read = ntn3_i2c_read;
		i2c_ctrl->client_i2c_write = ntn3_i2c_write;
		i2c_ctrl->client_i2c_reset = ntn3_ep_reset_ctrl;
		i2c_ctrl->client_i2c_dump_regs = ntn3_dump_regs;
		i2c_ctrl->client_i2c_de_emphasis_wa = ntn3_de_emphasis_wa;
		i2c_ctrl->client = client;
	} else {
		dev_err(&client->dev, "invalid client id %d\n", client_id);
	}

	return 0;
}

static struct i2c_driver pcie_i2c_ctrl_driver = {
	.driver = {
		.name	=		"pcie-i2c-ctrl",
		.of_match_table	=	of_match_ptr(of_i2c_id_table),
	},

	.probe		=       pcie_i2c_ctrl_probe,
};
#endif

static int __init pcie_init(void)
{
	int ret = 0, i;
	char rc_name[MAX_RC_NAME_LEN];
	void __iomem *reg_addr;

	pr_debug("pcie:%s.\n", __func__);

	pcie_drv.rc_num = 0;
	mutex_init(&pcie_drv.drv_lock);
	mutex_init(&pcie_drv.rpmsg_lock);

	for (i = 0; i < MAX_RC_NUM; i++) {
		scnprintf(rc_name, MAX_RC_NAME_LEN, "pcie%d-short", i);
		msm_pcie_dev[i].ipc_log =
			ipc_log_context_create(PCIE_LOG_PAGES, rc_name, 0);
		if (msm_pcie_dev[i].ipc_log == NULL)
			pr_err("%s: unable to create IPC log context for %s\n",
				__func__, rc_name);
		else
			PCIE_DBG(&msm_pcie_dev[i],
				"PCIe IPC logging is enable for RC%d\n",
				i);
		scnprintf(rc_name, MAX_RC_NAME_LEN, "pcie%d-long", i);
		msm_pcie_dev[i].ipc_log_long =
			ipc_log_context_create(PCIE_LOG_PAGES, rc_name, 0);
		if (msm_pcie_dev[i].ipc_log_long == NULL)
			pr_err("%s: unable to create IPC log context for %s\n",
				__func__, rc_name);
		else
			PCIE_DBG(&msm_pcie_dev[i],
				"PCIe IPC logging %s is enable for RC%d\n",
				rc_name, i);
		scnprintf(rc_name, MAX_RC_NAME_LEN, "pcie%d-dump", i);
		msm_pcie_dev[i].ipc_log_dump =
			ipc_log_context_create(PCIE_LOG_PAGES, rc_name, 0);
		if (msm_pcie_dev[i].ipc_log_dump == NULL)
			pr_err("%s: unable to create IPC log context for %s\n",
				__func__, rc_name);
		else
			PCIE_DBG(&msm_pcie_dev[i],
				"PCIe IPC logging %s is enable for RC%d\n",
				rc_name, i);
		spin_lock_init(&msm_pcie_dev[i].cfg_lock);
		spin_lock_init(&msm_pcie_dev[i].evt_reg_list_lock);
		msm_pcie_dev[i].cfg_access = true;
		mutex_init(&msm_pcie_dev[i].enumerate_lock);
		mutex_init(&msm_pcie_dev[i].setup_lock);
		mutex_init(&msm_pcie_dev[i].recovery_lock);
		mutex_init(&msm_pcie_dev[i].aspm_lock);
		mutex_init(&msm_pcie_dev[i].drv_pc_lock);
		spin_lock_init(&msm_pcie_dev[i].irq_lock);
		msm_pcie_dev[i].drv_ready = false;
		msm_pcie_dev[i].l23_rdy_poll_timeout = L23_READY_POLL_TIMEOUT;
		INIT_WORK(&msm_pcie_dev[i].drv_disable_pc_work,
				msm_pcie_drv_disable_pc);
		INIT_WORK(&msm_pcie_dev[i].drv_enable_pc_work,
				msm_pcie_drv_enable_pc);
		INIT_LIST_HEAD(&msm_pcie_dev[i].enum_ep_list);
		INIT_LIST_HEAD(&msm_pcie_dev[i].susp_ep_list);
		INIT_LIST_HEAD(&msm_pcie_dev[i].event_reg_list);
	}

#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&pcie_i2c_ctrl_driver);
	if (ret != 0)
		pr_err("Failed to add i2c ctrl driver: %d\n", ret);
#endif

	crc8_populate_msb(msm_pcie_crc8_table, MSM_PCIE_CRC8_POLYNOMIAL);

	msm_pcie_debugfs_init();

	if (count == MAX_PCIE_SM_REGS) {
		for (i = 0; i < pcie_sm_regs[PCIE_SM_NUM_INSTANCES]; i++) {

			reg_addr = ioremap(pcie_sm_regs[PCIE_SM_BASE] +
				pcie_sm_regs[PCIE_SM_PWR_CTRL_OFFSET] +
			(i * pcie_sm_regs[PCIE_SM_PWR_INSTANCE_OFFSET]), 4);

			msm_pcie_write_reg(reg_addr, 0x0, 0x1);

			iounmap(reg_addr);

			reg_addr = ioremap(pcie_sm_regs[PCIE_SM_BASE] +
				pcie_sm_regs[PCIE_SM_PWR_MASK_OFFSET] +
			(i * pcie_sm_regs[PCIE_SM_PWR_INSTANCE_OFFSET]), 4);

			msm_pcie_write_reg(reg_addr, 0x0, 0x1);

			iounmap(reg_addr);
		}
	}

	ret = pci_register_driver(&msm_pci_driver);
	if (ret)
		return ret;

	mpcie_wq = alloc_ordered_workqueue("mpcie_wq",
						WQ_MEM_RECLAIM | WQ_HIGHPRI);
	if (!mpcie_wq)
		return -ENOMEM;

	pcie_drv.nb.notifier_call = msm_pcie_ssr_notifier;
	INIT_WORK(&pcie_drv.drv_connect, msm_pcie_drv_connect_worker);
	pcie_drv.msm_pcie_dev = msm_pcie_dev;

	ret = platform_driver_register(&msm_pcie_driver);
	if (ret)
		destroy_workqueue(mpcie_wq);

	return ret;
}

static void __exit pcie_exit(void)
{
	int i;

	pr_info("PCIe: %s\n", __func__);

#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&pcie_i2c_ctrl_driver);
#endif

	if (mpcie_wq)
		destroy_workqueue(mpcie_wq);

	platform_driver_unregister(&msm_pcie_driver);

	msm_pcie_debugfs_exit();

	for (i = 0; i < MAX_RC_NUM; i++)
		msm_pcie_sysfs_exit(&msm_pcie_dev[i]);
}

subsys_initcall_sync(pcie_init);
module_exit(pcie_exit);

/* RC do not represent the right class; set it to PCI_CLASS_BRIDGE_PCI */
static void msm_pcie_fixup_early(struct pci_dev *dev)
{
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(dev->bus);

	PCIE_DBG(pcie_dev, "hdr_type %d\n", dev->hdr_type);
	if (pci_is_root_bus(dev->bus))
		dev->class = (dev->class & 0xff) | (PCI_CLASS_BRIDGE_PCI << 8);
}
DECLARE_PCI_FIXUP_EARLY(PCIE_VENDOR_ID_QCOM, PCI_ANY_ID,
			msm_pcie_fixup_early);

static void __msm_pcie_l1ss_timeout_disable(struct msm_pcie_dev_t *pcie_dev)
{
	msm_pcie_write_mask(pcie_dev->parf + PCIE20_PARF_DEBUG_INT_EN,
			    PCIE20_PARF_DEBUG_INT_EN_L1SUB_TIMEOUT_BIT, 0);
	msm_pcie_write_reg(pcie_dev->parf, PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
				0);
}

static void __msm_pcie_l1ss_timeout_enable(struct msm_pcie_dev_t *pcie_dev)
{
	u32 val = 0;

	msm_pcie_write_reg(pcie_dev->parf, PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
			   PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET);

	msm_pcie_write_mask(pcie_dev->parf + PCIE20_PARF_INT_ALL_CLEAR, 0,
			    BIT(MSM_PCIE_INT_EVT_L1SUB_TIMEOUT));

	msm_pcie_write_mask(pcie_dev->parf + PCIE20_PARF_DEBUG_INT_EN, 0,
			    PCIE20_PARF_DEBUG_INT_EN_L1SUB_TIMEOUT_BIT);

	val = PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET |
	      L1SS_TIMEOUT_US_TO_TICKS(L1SS_TIMEOUT_US,
				       pcie_dev->aux_clk_freq);

	msm_pcie_write_reg(pcie_dev->parf, PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
			   val);
}

/* Suspend the PCIe link */
static int msm_pcie_pm_suspend(struct pci_dev *dev,
			void *user, void *data, u32 options)
{
	int ret = 0;
	u32 val = 0;
	int ret_l23;
	unsigned long irqsave_flags;
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(dev->bus);

	PCIE_DBG(pcie_dev, "RC%d: entry\n", pcie_dev->rc_idx);

	spin_lock_irqsave(&pcie_dev->irq_lock, irqsave_flags);
	pcie_dev->suspending = true;
	spin_unlock_irqrestore(&pcie_dev->irq_lock, irqsave_flags);

	if (pcie_dev->config_recovery) {
		if (work_pending(&pcie_dev->link_recover_wq)) {
			PCIE_DBG(pcie_dev,
				 "RC%d: cancel link_recover_wq at pm suspend\n",
				 pcie_dev->rc_idx);
			cancel_work_sync(&pcie_dev->link_recover_wq);
		}
	}

	if (!pcie_dev->power_on) {
		PCIE_DBG(pcie_dev,
			"PCIe: power of RC%d has been turned off.\n",
			pcie_dev->rc_idx);
		return ret;
	}

	if (dev) {
		if (msm_pcie_confirm_linkup(pcie_dev, true, true, dev)) {
			PCIE_DBG(pcie_dev, "PCIe: RC%d: save config space\n",
					 pcie_dev->rc_idx);
			ret = pci_save_state(dev);
			if (ret) {
				PCIE_ERR(pcie_dev,
					 "PCIe: RC%d: fail to save state:%d.\n",
					 pcie_dev->rc_idx, ret);
				pcie_dev->suspending = false;
				return ret;
			}

		} else {
			kfree(pcie_dev->saved_state);
			pcie_dev->saved_state = NULL;

			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: load default config space\n",
				 pcie_dev->rc_idx);
			ret = pci_load_saved_state(dev, pcie_dev->default_state);
			if (ret) {
				PCIE_ERR(pcie_dev,
					 "PCIe: RC%d: fail to load default state:%d.\n",
					 pcie_dev->rc_idx, ret);
				pcie_dev->suspending = false;
				return ret;
			}
		}

		PCIE_DBG(pcie_dev, "PCIe: RC%d: store saved state\n",
							 pcie_dev->rc_idx);
		pcie_dev->saved_state = pci_store_saved_state(dev);
	}

	spin_lock_irqsave(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);
	pcie_dev->cfg_access = false;
	spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);

	writel_relaxed(BIT(4), pcie_dev->elbi + PCIE20_ELBI_SYS_CTRL);
	wmb(); /* ensure changes propagated to the hardware */

	PCIE_DBG(pcie_dev, "RC%d: PME_TURNOFF_MSG is sent out\n",
		pcie_dev->rc_idx);

	ret_l23 = readl_poll_timeout((pcie_dev->parf + PCIE20_PARF_PM_STTS_1),
		val, PCIE_LINK_IN_L2_STATE(val),
		9000, pcie_dev->l23_rdy_poll_timeout);

	/* check L23_Ready */
	PCIE_DBG(pcie_dev, "RC%d: PCIE20_PARF_PM_STTS_1 is 0x%x.\n",
		pcie_dev->rc_idx,
		readl_relaxed(pcie_dev->parf + PCIE20_PARF_PM_STTS_1));
	if (!ret_l23)
		PCIE_DBG(pcie_dev, "RC%d: PM_Enter_L23 is received\n",
			pcie_dev->rc_idx);
	else
		PCIE_DBG(pcie_dev, "RC%d: PM_Enter_L23 is NOT received\n",
			pcie_dev->rc_idx);

	if (pcie_dev->use_pinctrl && pcie_dev->pins_sleep)
		pinctrl_select_state(pcie_dev->pinctrl,
					pcie_dev->pins_sleep);

	msm_pcie_disable(pcie_dev);

	PCIE_DBG(pcie_dev, "RC%d: exit\n", pcie_dev->rc_idx);

	return ret;
}

static void msm_pcie_fixup_suspend(struct pci_dev *dev)
{
	int ret;
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(dev->bus);

	PCIE_DBG(pcie_dev, "RC%d\n", pcie_dev->rc_idx);

	if (pcie_dev->link_status != MSM_PCIE_LINK_ENABLED ||
		!pci_is_root_bus(dev->bus))
		return;

	spin_lock_irqsave(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);
	if (pcie_dev->disable_pc) {
		PCIE_DBG(pcie_dev,
			"RC%d: Skip suspend because of user request\n",
			pcie_dev->rc_idx);
		spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);
		return;
	}
	spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);

	mutex_lock(&pcie_dev->recovery_lock);

	ret = msm_pcie_pm_suspend(dev, NULL, NULL, 0);
	if (ret)
		PCIE_ERR(pcie_dev, "PCIe: RC%d got failure in suspend:%d.\n",
			pcie_dev->rc_idx, ret);

	mutex_unlock(&pcie_dev->recovery_lock);
}
DECLARE_PCI_FIXUP_SUSPEND(PCIE_VENDOR_ID_QCOM, PCI_ANY_ID,
			  msm_pcie_fixup_suspend);

/* Resume the PCIe link */
static int msm_pcie_pm_resume(struct pci_dev *dev,
			void *user, void *data, u32 options)
{
	int ret;
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(dev->bus);

	PCIE_DBG(pcie_dev, "RC%d: entry\n", pcie_dev->rc_idx);

	if (pcie_dev->use_pinctrl && pcie_dev->pins_default)
		pinctrl_select_state(pcie_dev->pinctrl,
					pcie_dev->pins_default);

	spin_lock_irqsave(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);
	pcie_dev->cfg_access = true;
	spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				pcie_dev->irqsave_flags);

	ret = msm_pcie_enable(pcie_dev);
	if (ret) {
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d fail to enable PCIe link in resume.\n",
			pcie_dev->rc_idx);
		return ret;
	}

	pcie_dev->suspending = false;
	PCIE_DBG(pcie_dev,
		"dev->bus->number = %d dev->bus->primary = %d\n",
		 dev->bus->number, dev->bus->primary);

	if (dev) {
		PCIE_DBG(pcie_dev, "RC%d: restore config space\n",
			 pcie_dev->rc_idx);

		/*
		 * Pci framework tries to read the pm_cap config register
		 * during the system resume process and since our pcie
		 * controller might not have the clocks/regulators on at
		 * that time, framework will put the power_state as D3Cold.
		 *
		 * Since the power_state is D3Cold, pci_restore_state API
		 * will not be able to write the MSI address to config space.
		 * Thereby resulting in a smmu fault when trying to rise a
		 * MSI for the AER.
		 */
		pci_set_power_state(dev, PCI_D0);

		pci_load_and_free_saved_state(dev, &pcie_dev->saved_state);
		pci_restore_state(dev);
	}

	PCIE_DBG(pcie_dev, "RC%d: exit\n", pcie_dev->rc_idx);

	return ret;
}

static void msm_pcie_fixup_resume(struct pci_dev *dev)
{
	int ret;
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(dev->bus);

	PCIE_DBG(pcie_dev, "RC%d\n", pcie_dev->rc_idx);

	if ((pcie_dev->link_status != MSM_PCIE_LINK_DISABLED) ||
		pcie_dev->user_suspend || !pci_is_root_bus(dev->bus))
		return;

	mutex_lock(&pcie_dev->recovery_lock);
	ret = msm_pcie_pm_resume(dev, NULL, NULL, 0);
	if (ret)
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d got failure in fixup resume:%d.\n",
			pcie_dev->rc_idx, ret);

	mutex_unlock(&pcie_dev->recovery_lock);
}
DECLARE_PCI_FIXUP_RESUME(PCIE_VENDOR_ID_QCOM, PCI_ANY_ID,
				 msm_pcie_fixup_resume);

static void msm_pcie_fixup_resume_early(struct pci_dev *dev)
{
	int ret;
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(dev->bus);

	PCIE_DBG(pcie_dev, "RC%d\n", pcie_dev->rc_idx);

	if ((pcie_dev->link_status != MSM_PCIE_LINK_DISABLED) ||
		pcie_dev->user_suspend || !pci_is_root_bus(dev->bus))
		return;

	mutex_lock(&pcie_dev->recovery_lock);
	ret = msm_pcie_pm_resume(dev, NULL, NULL, 0);
	if (ret)
		PCIE_ERR(pcie_dev, "PCIe: RC%d got failure in resume:%d.\n",
			pcie_dev->rc_idx, ret);

	mutex_unlock(&pcie_dev->recovery_lock);
}
DECLARE_PCI_FIXUP_RESUME_EARLY(PCIE_VENDOR_ID_QCOM, PCI_ANY_ID,
				 msm_pcie_fixup_resume_early);

static int msm_pcie_drv_send_rpmsg(struct msm_pcie_dev_t *pcie_dev,
				   struct msm_pcie_drv_msg *msg)
{
	struct msm_pcie_drv_info *drv_info = pcie_dev->drv_info;
	int ret, re_try = 20; /* sleep 5 ms per re-try */
	struct rpmsg_device *rpdev;

	/* This function becomes a dummy call when CESTA support is present */
	if (pcie_dev->pcie_sm)
		return 0;

	mutex_lock(&pcie_drv.rpmsg_lock);
	rpdev = pcie_drv.rpdev;
	if (!pcie_drv.rpdev) {
		ret = -EIO;
		goto out;
	}

	reinit_completion(&drv_info->completion);

	drv_info->reply_seq = drv_info->seq++;
	msg->hdr.seq = drv_info->reply_seq;

	if (unlikely(drv_info->seq == MSM_PCIE_DRV_SEQ_RESV))
		drv_info->seq = 0;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: DRV: sending rpmsg: command: 0x%x\n",
		pcie_dev->rc_idx, msg->pkt.dword[0]);

retry:
	ret = rpmsg_trysend(rpdev->ept, msg, sizeof(*msg));
	if (ret) {
		if (ret == -EBUSY && re_try) {
			usleep_range(5000, 5001);
			re_try--;
			goto retry;
		}

		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: DRV: failed to send rpmsg, ret:%d\n",
			pcie_dev->rc_idx, ret);
		goto out;
	}

	ret = wait_for_completion_timeout(&drv_info->completion,
					msecs_to_jiffies(drv_info->timeout_ms));
	if (!ret) {
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d: DRV: completion timeout for rpmsg\n",
			pcie_dev->rc_idx);
		ret = -ETIMEDOUT;
		goto out;
	}

	ret = 0;

	PCIE_DBG(pcie_dev, "PCIe: RC%d: DRV: rpmsg successfully sent\n",
		pcie_dev->rc_idx);

out:
	mutex_unlock(&pcie_drv.rpmsg_lock);

	return ret;
}

static int msm_pcie_drv_resume(struct msm_pcie_dev_t *pcie_dev)
{
	struct msm_pcie_drv_info *drv_info = pcie_dev->drv_info;
	struct msm_pcie_clk_info_t *clk_info;
	u32 clkreq_override_en = 0;
	int ret, i, rpmsg_ret = 0;

	mutex_lock(&pcie_dev->recovery_lock);
	mutex_lock(&pcie_dev->setup_lock);

	/* if DRV hand-off was done and DRV subsystem is powered up */
	if (PCIE_RC_DRV_ENABLED(pcie_dev->rc_idx) &&
	    !pcie_dev->l1ss_sleep_disable)
		rpmsg_ret = msm_pcie_drv_send_rpmsg(pcie_dev,
					&drv_info->drv_disable_l1ss_sleep);

	msm_pcie_vreg_init(pcie_dev);

	PCIE_DBG(pcie_dev, "PCIe: RC%d:enable gdsc-core\n", pcie_dev->rc_idx);

	if (pcie_dev->gdsc_core && !pcie_dev->gdsc_clk_drv_ss_nonvotable) {
		ret = regulator_enable(pcie_dev->gdsc_core);
		if (ret)
			PCIE_ERR(pcie_dev,
			"PCIe: RC%d: failed to enable GDSC: ret %d\n",
			pcie_dev->rc_idx, ret);
	}

	PCIE_DBG(pcie_dev, "PCIe: RC%d:set ICC path vote\n", pcie_dev->rc_idx);

	ret = msm_pcie_icc_vote(pcie_dev, pcie_dev->current_link_speed,
			pcie_dev->current_link_width, false);
	if (ret)
		goto out;

	PCIE_DBG(pcie_dev, "PCIe: RC%d:turn on unsuppressible clks\n",
		pcie_dev->rc_idx);

	/* turn on all unsuppressible clocks */
	clk_info = pcie_dev->clk;
	for (i = 0; i < pcie_dev->num_clk; i++, clk_info++) {
		if (clk_info->hdl && !clk_info->suppressible) {
			ret = clk_prepare_enable(clk_info->hdl);
			if (ret)
				PCIE_DBG(pcie_dev,
				"PCIe: RC%d:clk_prepare_enable failed for %s\n",
				pcie_dev->rc_idx, clk_info->name);
		}
	}

	PCIE_DBG(pcie_dev, "PCIe: RC%d:turn on unsuppressible clks Done.\n",
		pcie_dev->rc_idx);

	msm_pcie_cesta_disable_drv(pcie_dev);

	clkreq_override_en = readl_relaxed(pcie_dev->parf +
				PCIE20_PARF_CLKREQ_OVERRIDE) &
				PCIE20_PARF_CLKREQ_IN_ENABLE;
	if (clkreq_override_en)
		PCIE_DBG(pcie_dev,
			"PCIe: RC%d: CLKREQ Override detected\n",
			pcie_dev->rc_idx);

	/*
	 * if PCIe CLKREQ override is still enabled, then make sure PCIe PIPE
	 * clk source mux is set to PCIe PIPE CLK. Similarly set phy aux clk src
	 * to phy aux clk before enabling PCIe PIPE CLK and phy aux clk.
	 * APPS votes for mux was PCIe PIPE and phy aux clk before DRV suspend.
	 * In order to vote for PCIe PIPE and phy aux clk, need to first set mux
	 * to XO then PCIe PIPE and phy aux clk or else clock driver will
	 * short the request.
	 */
	if (clkreq_override_en) {
		if (pcie_dev->pipe_clk_mux) {
			if (pcie_dev->ref_clk_src) {
				PCIE_DBG(pcie_dev,
					 "PCIe: RC%d: setting PCIe PIPE MUX to XO\n",
					 pcie_dev->rc_idx);
				clk_set_parent(pcie_dev->pipe_clk_mux,
					       pcie_dev->ref_clk_src);
			}

			if (pcie_dev->pipe_clk_ext_src) {
				PCIE_DBG(pcie_dev,
					 "PCIe: RC%d: setting PCIe PIPE MUX to PCIe PIPE\n",
					 pcie_dev->rc_idx);
				clk_set_parent(pcie_dev->pipe_clk_mux,
					       pcie_dev->pipe_clk_ext_src);
			}
		}

		if (pcie_dev->phy_aux_clk_mux) {
			if (pcie_dev->ref_clk_src) {
				PCIE_DBG(pcie_dev,
					 "PCIe: RC%d: setting PCIe phy aux MUX to XO\n",
					 pcie_dev->rc_idx);
				clk_set_parent(pcie_dev->phy_aux_clk_mux,
					       pcie_dev->ref_clk_src);
			}

			if (pcie_dev->phy_aux_clk_ext_src) {
				PCIE_DBG(pcie_dev,
					 "PCIe: RC%d: setting PCIe phy aux MUX to phy aux clk\n",
					 pcie_dev->rc_idx);
				clk_set_parent(pcie_dev->phy_aux_clk_mux,
					       pcie_dev->phy_aux_clk_ext_src);
			}
		}
	}

	PCIE_DBG(pcie_dev, "PCIe: RC%d:turn on pipe clk\n",
		pcie_dev->rc_idx);

	clk_info = pcie_dev->pipe_clk;
	for (i = 0; i < pcie_dev->num_pipe_clk; i++, clk_info++) {
		if (clk_info->hdl && !clk_info->suppressible) {
			ret = clk_prepare_enable(clk_info->hdl);
			if (ret)
				PCIE_DBG(pcie_dev,
				"PCIe: RC%d:clk_prepare_enable failed for %s\n",
				pcie_dev->rc_idx, clk_info->name);
		}
	}

	PCIE_DBG(pcie_dev, "PCIe: RC%d:turn on pipe clk, Done\n",
		pcie_dev->rc_idx);

	if (clkreq_override_en) {
		/* remove CLKREQ override */
		msm_pcie_write_reg_field(pcie_dev->parf,
					PCIE20_PARF_CLKREQ_OVERRIDE,
					PCIE20_PARF_CLKREQ_IN_ENABLE, 0);
		msm_pcie_write_reg_field(pcie_dev->parf,
					PCIE20_PARF_CLKREQ_OVERRIDE,
					PCIE20_PARF_CLKREQ_IN_VALUE, 0);
	}

	/* if DRV hand-off was done and DRV subsystem is powered up */
	if (PCIE_RC_DRV_ENABLED(pcie_dev->rc_idx) && !rpmsg_ret) {
		msm_pcie_drv_send_rpmsg(pcie_dev,
					&drv_info->drv_disable);
		clear_bit(pcie_dev->rc_idx, &pcie_drv.rc_drv_enabled);
	}

	/* scale CX and rate change based on current GEN speed */
	pcie_dev->current_link_speed = (readl_relaxed(pcie_dev->dm_core +
					PCIE20_CAP_LINKCTRLSTATUS) >> 16) &
					PCI_EXP_LNKSTA_CLS;

	msm_pcie_scale_link_bandwidth(pcie_dev, pcie_dev->current_link_speed);

	pcie_dev->user_suspend = false;
	spin_lock_irq(&pcie_dev->cfg_lock);
	pcie_dev->cfg_access = true;
	spin_unlock_irq(&pcie_dev->cfg_lock);
	mutex_lock(&pcie_dev->aspm_lock);
	pcie_dev->link_status = MSM_PCIE_LINK_ENABLED;
	mutex_unlock(&pcie_dev->aspm_lock);

	/* resume access to MSI register as link is resumed */
	if (!pcie_dev->lpi_enable)
		msm_msi_config_access(dev_get_msi_domain(&pcie_dev->dev->dev),
				      true);

	if (!pcie_dev->pcie_sm)
		enable_irq(pcie_dev->irq[MSM_PCIE_INT_GLOBAL_INT].num);

	mutex_unlock(&pcie_dev->setup_lock);
	mutex_unlock(&pcie_dev->recovery_lock);

	return 0;

out:
	mutex_unlock(&pcie_dev->setup_lock);
	mutex_unlock(&pcie_dev->recovery_lock);

	return ret;
}

static int msm_pcie_drv_suspend(struct msm_pcie_dev_t *pcie_dev,
				u32 options)
{
	struct msm_pcie_drv_info *drv_info = pcie_dev->drv_info;
	struct msm_pcie_clk_info_t *clk_info;
	int ret, i;
	unsigned long irqsave_flags, cfg_irqsave_flags;
	u32 ab = 0, ib = 0;

	/* If CESTA is available then drv is always supported */
	if (!pcie_dev->pcie_sm && !drv_info->ep_connected) {
		PCIE_ERR(pcie_dev,
			"PCIe: RC%d: DRV: client requests to DRV suspend while not connected\n",
			pcie_dev->rc_idx);
		return -EINVAL;
	}

	mutex_lock(&pcie_dev->recovery_lock);

	/* disable global irq - no more linkdown/aer detection */
	if (!pcie_dev->pcie_sm)
		disable_irq(pcie_dev->irq[MSM_PCIE_INT_GLOBAL_INT].num);

	ret = msm_pcie_drv_send_rpmsg(pcie_dev, &drv_info->drv_enable);
	if (ret) {
		ret = -EBUSY;
		goto out;
	}

	/* suspend access to MSI register. resume access in drv_resume */
	if (!pcie_dev->lpi_enable)
		msm_msi_config_access(dev_get_msi_domain(&pcie_dev->dev->dev),
				      false);

	pcie_dev->user_suspend = true;
	set_bit(pcie_dev->rc_idx, &pcie_drv.rc_drv_enabled);
	spin_lock_irqsave(&pcie_dev->irq_lock, irqsave_flags);
	spin_lock_irqsave(&pcie_dev->cfg_lock, cfg_irqsave_flags);
	pcie_dev->cfg_access = false;
	spin_unlock_irqrestore(&pcie_dev->cfg_lock, cfg_irqsave_flags);
	spin_unlock_irqrestore(&pcie_dev->irq_lock, irqsave_flags);
	mutex_lock(&pcie_dev->setup_lock);
	mutex_lock(&pcie_dev->aspm_lock);
	pcie_dev->link_status = MSM_PCIE_LINK_DRV;
	mutex_unlock(&pcie_dev->aspm_lock);

	if (pcie_dev->pcie_sm) {
		msm_pcie_cesta_enable_drv(pcie_dev,
				!(options & MSM_PCIE_CONFIG_NO_L1SS_TO));
		ab = ICC_AVG_BW;
		ib = ICC_PEAK_BW;
	}

	/* turn off all unsuppressible clocks */
	clk_info = pcie_dev->pipe_clk;
	for (i = 0; i < pcie_dev->num_pipe_clk; i++, clk_info++)
		if (clk_info->hdl && !clk_info->suppressible)
			clk_disable_unprepare(clk_info->hdl);

	clk_info = pcie_dev->clk;
	for (i = 0; i < pcie_dev->num_clk; i++, clk_info++)
		if (clk_info->hdl && !clk_info->suppressible)
			clk_disable_unprepare(clk_info->hdl);

	/* enable L1ss sleep if client allows it */
	if (!pcie_dev->l1ss_sleep_disable &&
		!(options & MSM_PCIE_CONFIG_NO_L1SS_TO))
		msm_pcie_drv_send_rpmsg(pcie_dev,
					&drv_info->drv_enable_l1ss_sleep);

	if (pcie_dev->pcie_sm)
		ret = msm_pcie_icc_vote(pcie_dev, pcie_dev->current_link_speed,
				pcie_dev->current_link_width, true);
	else
		ret = msm_pcie_icc_vote(pcie_dev, 0, 0, true);
	if (ret) {
		mutex_unlock(&pcie_dev->setup_lock);
		mutex_unlock(&pcie_dev->recovery_lock);
		return ret;
	}

	if (pcie_dev->gdsc_core && !pcie_dev->gdsc_clk_drv_ss_nonvotable)
		regulator_disable(pcie_dev->gdsc_core);

	msm_pcie_vreg_deinit(pcie_dev);

	mutex_unlock(&pcie_dev->setup_lock);
	mutex_unlock(&pcie_dev->recovery_lock);

	return 0;
out:
	if (!pcie_dev->pcie_sm)
		enable_irq(pcie_dev->irq[MSM_PCIE_INT_GLOBAL_INT].num);
	mutex_unlock(&pcie_dev->recovery_lock);
	return ret;
}

int msm_pcie_pm_control(enum msm_pcie_pm_opt pm_opt, u32 busnr, void *user,
			void *data, u32 options)
{
	int ret = 0;
	struct pci_dev *dev;
	unsigned long flags;
	struct msm_pcie_dev_t *pcie_dev;
	struct msm_pcie_device_info *dev_info_itr, *temp, *dev_info = NULL;
	struct pci_dev *pcidev;
	bool force_rc_suspend = !!(options & MSM_PCIE_CONFIG_FORCE_SUSP);

	if (!user) {
		pr_err("PCIe: endpoint device is NULL\n");
		ret = -ENODEV;
		goto out;
	}

	pcie_dev = PCIE_BUS_PRIV_DATA(((struct pci_dev *)user)->bus);

	if (pcie_dev) {
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: pm_opt:%d;busnr:%d;options:%d\n",
			 pcie_dev->rc_idx, pm_opt, busnr, options);
	} else {
		pr_err(
			"PCIe: did not find RC for pci endpoint device.\n"
			);
		ret = -ENODEV;
		goto out;
	}

	dev = pcie_dev->dev;

	pcidev = (struct pci_dev *)user;

	if (!pcie_dev->drv_ready) {
		PCIE_ERR(pcie_dev,
			 "RC%d has not been successfully probed yet\n",
			 pcie_dev->rc_idx);
		return -EPROBE_DEFER;
	}

	switch (pm_opt) {
	case MSM_PCIE_DRV_SUSPEND:
		PCIE_DBG(pcie_dev,
			 "PCIe: RC%d: DRV: user requests for DRV suspend\n",
			 pcie_dev->rc_idx);

		/* make sure disable pc is done before enabling drv */
		flush_work(&pcie_dev->drv_disable_pc_work);

		ret = msm_pcie_drv_suspend(pcie_dev, options);
		break;
	case MSM_PCIE_SUSPEND:
		PCIE_DBG(pcie_dev,
			 "User of RC%d requests to suspend the link\n",
			 pcie_dev->rc_idx);
		if (pcie_dev->link_status != MSM_PCIE_LINK_ENABLED)
			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: requested to suspend when link is not enabled:%d.\n",
				 pcie_dev->rc_idx, pcie_dev->link_status);

		if (!pcie_dev->power_on) {
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: requested to suspend when link is powered down:%d.\n",
				 pcie_dev->rc_idx, pcie_dev->link_status);
			break;
		}

		mutex_lock(&pcie_dev->recovery_lock);
		mutex_lock(&pcie_dev->enumerate_lock);

		/*
		 * Remove current user requesting for suspend from ep list and
		 * add it to suspend ep list. Reject susp if list is still not
		 * empty.
		 */
		list_for_each_entry_safe(dev_info_itr, temp,
					 &pcie_dev->enum_ep_list, pcidev_node) {
			if (dev_info_itr->dev == pcidev) {
				list_del(&dev_info_itr->pcidev_node);
				dev_info = dev_info_itr;
				list_add_tail(&dev_info->pcidev_node,
					      &pcie_dev->susp_ep_list);
				break;
			}
		}

		if (!dev_info)
			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: ep BDF 0x%04x not in enum list\n",
				 pcie_dev->rc_idx, PCI_DEVID(
							pcidev->bus->number,
							pcidev->devfn));

		if (!force_rc_suspend && !list_empty(&pcie_dev->enum_ep_list)) {
			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: request to suspend the link is rejected\n",
				 pcie_dev->rc_idx);
			mutex_unlock(&pcie_dev->enumerate_lock);
			mutex_unlock(&pcie_dev->recovery_lock);
			break;
		}

		pcie_dev->user_suspend = true;

		ret = msm_pcie_pm_suspend(dev, user, data, options);
		if (ret) {
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: user failed to suspend the link.\n",
				 pcie_dev->rc_idx);
			pcie_dev->user_suspend = false;

			if (dev_info) {
				list_del(&dev_info->pcidev_node);
				list_add_tail(&dev_info->pcidev_node,
					      &pcie_dev->enum_ep_list);
			}
		}

		mutex_unlock(&pcie_dev->enumerate_lock);
		mutex_unlock(&pcie_dev->recovery_lock);
		break;
	case MSM_PCIE_RESUME:
		PCIE_DBG(pcie_dev,
			 "User of RC%d requests to resume the link\n",
			 pcie_dev->rc_idx);

		/* DRV resume */
		if (pcie_dev->link_status == MSM_PCIE_LINK_DRV) {
			ret = msm_pcie_drv_resume(pcie_dev);
			break;
		}

		mutex_lock(&pcie_dev->recovery_lock);

		/* when link was suspended and link resume is requested */
		mutex_lock(&pcie_dev->enumerate_lock);
		list_for_each_entry_safe(dev_info_itr, temp,
					 &pcie_dev->susp_ep_list, pcidev_node) {
			if (dev_info_itr->dev == user) {
				list_del(&dev_info_itr->pcidev_node);
				dev_info = dev_info_itr;
				list_add_tail(&dev_info->pcidev_node,
					      &pcie_dev->enum_ep_list);
				break;
			}
		}

		if (!dev_info) {
			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: ep BDF 0x%04x not in susp list\n",
				 pcie_dev->rc_idx, PCI_DEVID(
							pcidev->bus->number,
							pcidev->devfn));
		}
		mutex_unlock(&pcie_dev->enumerate_lock);

		if (pcie_dev->power_on) {
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: requested to resume when link is already powered on.\n",
				 pcie_dev->rc_idx);
			mutex_unlock(&pcie_dev->recovery_lock);
			break;
		}

		ret = msm_pcie_pm_resume(dev, user, data, options);
		if (ret) {
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: user failed to resume the link.\n",
				 pcie_dev->rc_idx);

			mutex_lock(&pcie_dev->enumerate_lock);
			if (dev_info) {
				list_del(&dev_info->pcidev_node);
				list_add_tail(&dev_info->pcidev_node,
					      &pcie_dev->susp_ep_list);
			}
			mutex_unlock(&pcie_dev->enumerate_lock);
		} else {
			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: user succeeded to resume the link.\n",
				 pcie_dev->rc_idx);

			pcie_dev->user_suspend = false;
		}

		mutex_unlock(&pcie_dev->recovery_lock);

		break;
	case MSM_PCIE_DISABLE_PC:
		PCIE_DBG(pcie_dev,
			 "User of RC%d requests to keep the link always alive.\n",
			 pcie_dev->rc_idx);
		spin_lock_irqsave(&pcie_dev->cfg_lock, pcie_dev->irqsave_flags);
		if (pcie_dev->suspending) {
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d Link has been suspended before request\n",
				 pcie_dev->rc_idx);
			ret = MSM_PCIE_ERROR;
		} else {
			pcie_dev->disable_pc = true;
		}
		spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				       pcie_dev->irqsave_flags);
		break;
	case MSM_PCIE_ENABLE_PC:
		PCIE_DBG(pcie_dev,
			 "User of RC%d cancels the request of alive link.\n",
			 pcie_dev->rc_idx);
		spin_lock_irqsave(&pcie_dev->cfg_lock, pcie_dev->irqsave_flags);
		pcie_dev->disable_pc = false;
		spin_unlock_irqrestore(&pcie_dev->cfg_lock,
				       pcie_dev->irqsave_flags);
		break;
	case MSM_PCIE_HANDLE_LINKDOWN:
		PCIE_DBG(pcie_dev,
			 "User of RC%d requests handling link down.\n",
			 pcie_dev->rc_idx);
		spin_lock_irqsave(&pcie_dev->irq_lock, flags);
		msm_pcie_handle_linkdown(pcie_dev);
		spin_unlock_irqrestore(&pcie_dev->irq_lock, flags);
		break;
	case MSM_PCIE_DRV_PC_CTRL:
		PCIE_DBG(pcie_dev,
			 "User of RC%d requests handling drv pc options %u.\n",
			 pcie_dev->rc_idx, options);

		/* Mask the DRV_PC_CTRL if CESTA is supported */
		if (pcie_dev->pcie_sm)
			break;

		mutex_lock(&pcie_dev->drv_pc_lock);
		pcie_dev->drv_disable_pc_vote =
				options & MSM_PCIE_CONFIG_NO_DRV_PC;

		if (!pcie_dev->drv_info || !pcie_dev->drv_info->ep_connected) {
			mutex_unlock(&pcie_dev->drv_pc_lock);
			break;
		}

		if (pcie_dev->drv_disable_pc_vote) {
			queue_work(mpcie_wq, &pcie_dev->drv_disable_pc_work);
		} else {
			queue_work(mpcie_wq, &pcie_dev->drv_enable_pc_work);

			/* make sure enable pc happens asap */
			flush_work(&pcie_dev->drv_enable_pc_work);
		}
		mutex_unlock(&pcie_dev->drv_pc_lock);
		break;
	default:
		PCIE_ERR(pcie_dev,
			 "PCIe: RC%d: unsupported pm operation:%d.\n",
			 pcie_dev->rc_idx, pm_opt);
		ret = -ENODEV;
		goto out;
	}

out:
	return ret;
}
EXPORT_SYMBOL(msm_pcie_pm_control);

void msm_pcie_l1ss_timeout_disable(struct pci_dev *pci_dev)
{
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(pci_dev->bus);

	__msm_pcie_l1ss_timeout_disable(pcie_dev);
}
EXPORT_SYMBOL(msm_pcie_l1ss_timeout_disable);

void msm_pcie_l1ss_timeout_enable(struct pci_dev *pci_dev)
{
	struct msm_pcie_dev_t *pcie_dev = PCIE_BUS_PRIV_DATA(pci_dev->bus);

	__msm_pcie_l1ss_timeout_enable(pcie_dev);
}
EXPORT_SYMBOL(msm_pcie_l1ss_timeout_enable);

int msm_pcie_register_event(struct msm_pcie_register_event *reg)
{
	int ret = 0;
	struct msm_pcie_dev_t *pcie_dev;
	struct msm_pcie_register_event *reg_itr, *temp;
	struct pci_dev *pcidev;
	unsigned long flags;

	if (!reg) {
		pr_err("PCIe: Event registration is NULL\n");
		return -ENODEV;
	}

	if (!reg->user) {
		pr_err("PCIe: User of event registration is NULL\n");
		return -ENODEV;
	}

	pcie_dev = PCIE_BUS_PRIV_DATA(((struct pci_dev *)reg->user)->bus);

	if (!pcie_dev) {
		pr_err("PCIe: did not find RC for pci endpoint device.\n");
		return -ENODEV;
	}

	pcidev = (struct pci_dev *)reg->user;

	spin_lock_irqsave(&pcie_dev->evt_reg_list_lock, flags);
	list_for_each_entry_safe(reg_itr, temp,
				 &pcie_dev->event_reg_list, node) {

		if (reg_itr->user == reg->user) {
			PCIE_ERR(pcie_dev,
				 "PCIe: RC%d: EP BDF 0x%4x already registered\n",
				 pcie_dev->rc_idx,
				 PCI_DEVID(pcidev->bus->number, pcidev->devfn));
			spin_unlock(&pcie_dev->evt_reg_list_lock);
			return -EEXIST;
		}
	}
	list_add_tail(&reg->node, &pcie_dev->event_reg_list);
	spin_unlock_irqrestore(&pcie_dev->evt_reg_list_lock, flags);

	if (pcie_dev->drv_supported)
		schedule_work(&pcie_drv.drv_connect);

	return ret;
}
EXPORT_SYMBOL(msm_pcie_register_event);

int msm_pcie_deregister_event(struct msm_pcie_register_event *reg)
{
	struct msm_pcie_dev_t *pcie_dev;
	struct pci_dev *pcidev;
	struct msm_pcie_register_event *reg_itr, *temp;
	unsigned long flags;

	if (!reg) {
		pr_err("PCIe: Event deregistration is NULL\n");
		return -ENODEV;
	}

	if (!reg->user) {
		pr_err("PCIe: User of event deregistration is NULL\n");
		return -ENODEV;
	}

	pcie_dev = PCIE_BUS_PRIV_DATA(((struct pci_dev *)reg->user)->bus);

	if (!pcie_dev) {
		PCIE_ERR(pcie_dev, "%s",
			"PCIe: did not find RC for pci endpoint device.\n");
		return -ENODEV;
	}

	pcidev = (struct pci_dev *)reg->user;

	spin_lock_irqsave(&pcie_dev->evt_reg_list_lock, flags);
	list_for_each_entry_safe(reg_itr, temp, &pcie_dev->event_reg_list,
				 node) {
		if (reg_itr->user == reg->user) {
			list_del(&reg->node);
			spin_unlock_irqrestore(&pcie_dev->evt_reg_list_lock, flags);
			PCIE_DBG(pcie_dev,
				 "PCIe: RC%d: Event deregistered for BDF 0x%04x\n",
				 pcie_dev->rc_idx,
				 PCI_DEVID(pcidev->bus->number, pcidev->devfn));
			return 0;
		}
	}
	spin_unlock_irqrestore(&pcie_dev->evt_reg_list_lock, flags);

	PCIE_DBG(pcie_dev,
		 "PCIe: RC%d: Failed to deregister event for BDF 0x%04x\n",
		 pcie_dev->rc_idx,
		 PCI_DEVID(pcidev->bus->number, pcidev->devfn));

	return -EINVAL;
}
EXPORT_SYMBOL(msm_pcie_deregister_event);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. PCIe RC driver");
MODULE_LICENSE("GPL");
