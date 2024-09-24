/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef __EP_PCIE_COM_H
#define __EP_PCIE_COM_H

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/ipc_logging.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/msm_ep_pcie.h>
#include <linux/iommu.h>

#define PCIE20_PARF_SYS_CTRL           0x00
#define PCIE20_PARF_DB_CTRL            0x10
#define PCIE20_PARF_PM_CTRL            0x20
#define PCIE20_PARF_PM_STTS            0x24
#define PCIE20_PARF_PHY_CTRL           0x40
#define PCIE20_PARF_PHY_REFCLK         0x4C
#define PCIE20_PARF_CONFIG_BITS        0x50
#define PCIE20_PARF_TEST_BUS           0xE4
#define PCIE20_PARF_MHI_BASE_ADDR_LOWER 0x178
#define PCIE20_PARF_MHI_BASE_ADDR_UPPER 0x17c
#define PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER	0x180
#define PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET_MASK	0x8000000
#define PCIE20_PARF_MSI_GEN             0x188
#define PCIE20_PARF_DEBUG_INT_EN        0x190
#define PCIE20_PARF_DEBUG_INT_EN_L1SUB_TIMEOUT_BIT_MASK	BIT(0)
#define PCIE20_PARF_MHI_IPA_DBS                0x198
#define PCIE20_PARF_MHI_IPA_CDB_TARGET_LOWER   0x19C
#define PCIE20_PARF_MHI_IPA_EDB_TARGET_LOWER   0x1A0
#define PCIE20_PARF_AXI_MSTR_RD_HALT_NO_WRITES 0x1A4
#define PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT      0x1A8
#define PCIE20_PARF_Q2A_FLUSH          0x1AC
#define PCIE20_PARF_LTSSM              0x1B0
#define PCIE20_PARF_CFG_BITS           0x210
#define PCIE20_PARF_LTR_MSI_EXIT_L1SS  0x214
#define PCIE20_PARF_INT_ALL_STATUS     0x224
#define PCIE20_PARF_INT_ALL_CLEAR      0x228
#define PCIE20_PARF_INT_ALL_MASK       0x22C
#define PCIE20_PARF_INT_ALL_3_STATUS   0x2D88
#define PCIE20_PARF_INT_ALL_3_MASK     0x2D8C
#define PCIE20_PARF_INT_ALL_3_CLEAR    0x2D90
#define PCIE20_PARF_LTSSM_STATE_MASK   0x003f
#define PCIE20_PARF_PM_STTS_PM_LINKST_IN_L1SUB_MASK       BIT(8)
#define PCIE20_PARF_PM_STTS_PM_LINKST_IN_L1SUB_SHIFT      8
#define PCIE20_PARF_PM_STTS_PM_DSTATE_0_MASK              BIT(29)
#define PCIE20_PARF_PM_STTS_PM_DSTATE_0_SHIFT             29
#define PCIE20_PARF_MHI_BASE_ADDR_V1_VFn_LOWER(n)       (((n) * 0x8) + 0x3088)
#define PCIE20_PARF_MHI_BASE_ADDR_V1_VFn_UPPER(n)       (((n) * 0x8)  + 0x308C)
#define PCIE20_PARF_MHI_BASE_ADDR_VFn_LOWER(n)       (((n) * 0x28) + 0x3100)
#define PCIE20_PARF_MHI_BASE_ADDR_VFn_UPPER(n)       (((n) * 0x28)  + 0x3104)


#define PCIE20_PARF_MHI_IPA_DBS_V1_VF(n)                (((n) * 0x8) + 0x2E9C)
#define PCIE20_PARF_MHI_IPA_CDB_V1_VF_TARGET_LOWER(n)   (((n) * 0x18) + 0x2E08)
#define PCIE20_PARF_MHI_IPA_EDB_V1_VF_TARGET_LOWER(n)   (((n) * 0x18) + 0x2E0C)
#define PCIE20_PARF_MHI_IPA_DBS_VF(n)                (((n) * 0x28) + 0x3124)
#define PCIE20_PARF_MHI_IPA_CDB_VF_TARGET_LOWER(n)   (((n) * 0x28) + 0x3110)
#define PCIE20_PARF_MHI_IPA_EDB_VF_TARGET_LOWER(n)   (((n) * 0x28) + 0x3114)

#define PCIE20_PARF_CLKREQ_OVERRIDE	0x2B0
#define PCIE20_PARF_CLKREQ_IN_OVERRIDE_STS	BIT(5)
#define PCIE20_PARF_CLKREQ_OE_OVERRIDE_STS	BIT(4)
#define PCIE20_PARF_CLKREQ_IN_OVERRIDE_VAL_MASK	BIT(3)
#define PCIE20_PARF_CLKREQ_IN_OVERRIDE_VAL_ASSERT	0
#define PCIE20_PARF_CLKREQ_IN_OVERRIDE_VAL_DEASSERT	1
#define PCIE20_PARF_CLKREQ_OE_OVERRIDE_VAL	BIT(2)
#define PCIE20_PARF_CLKREQ_IN_OVERRIDE_ENABLE_MASK	BIT(1)
#define PCIE20_PARF_CLKREQ_IN_OVERRIDE_ENABLE_DIS	0
#define PCIE20_PARF_CLKREQ_IN_OVERRIDE_ENABLE_EN	1
#define PCIE20_PARF_CLKREQ_OE_OVERRIDE_ENABLE	BIT(0)

#define PCIE20_PARF_DEBUG_CNT_IN_L0S (0xc10)
#define PCIE20_PARF_DEBUG_CNT_IN_L1 (0xc0c)
#define PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L1 (0xc84)
#define PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L2 (0xc88)

#define PCIE20_PARF_SLV_ADDR_MSB_CTRL  0x2C0
#define PCIE20_PARF_DBI_BASE_ADDR      0x350
#define PCIE20_PARF_DBI_BASE_ADDR_HI   0x354
#define PCIE20_PARF_DBI_VF_BASE_ADDR      0x2DA0
#define PCIE20_PARF_DBI_VF_BASE_ADDR_HI   0x2DA4
#define PCIE20_PARF_SLV_ADDR_SPACE_SIZE        0x358
#define PCIE20_PARF_SLV_ADDR_SPACE_SIZE_HI     0x35C

#define PCIE20_PARF_L1SS_SLEEP_MODE_HANDLER_STATUS	0x4D0
#define PCIE20_PARF_L1SS_SLEEP_MHI_FWD_DISABLE		BIT(5)
#define PCIE20_PARF_L1SS_SLEEP_MHI_FWD_ENABLE		BIT(4)

#define PCIE20_PARF_L1SS_SLEEP_MODE_HANDLER_CONFIG	0x4D4

#define PCIE20_PARF_LINK_DOWN_ECAM_BLOCK	0x608

#define PCIE20_PARF_ATU_BASE_ADDR      0x634
#define PCIE20_PARF_ATU_BASE_ADDR_HI   0x638
#define PCIE20_PARF_SRIS_MODE		0x644
#define PCIE20_PARF_BUS_DISCONNECT_CTRL          0x680
#define PCIE20_PARF_BUS_DISCONNECT_STATUS        0x684
#define PCIE20_PARF_BDF_TO_SID_CFG		0x2c00

#define PCIE20_PARF_DEVICE_TYPE        0x1000
#define PCIE20_PARF_EDMA_BASE_ADDR      0x64C
#define PCIE20_PARF_EDMA_BASE_ADDR_HI   0x650

#define PCIE20_ELBI_VERSION            0x00
#define PCIE20_ELBI_SYS_CTRL           0x04
#define PCIE20_ELBI_SYS_STTS	       0x08
#define PCIE20_ELBI_CS2_ENABLE         0xA4

#define PCIE20_DEVICE_ID_VENDOR_ID     0x00
#define PCIE20_MASK_DEVICE_ID          GENMASK(31, 16)
#define PCIE20_MASK_VENDOR_ID          GENMASK(15, 0)
#define PCIE20_COMMAND_STATUS          0x04
#define PCIE20_CMD_STS_CAP_LIST        BIT(20)
#define PCIE20_CLASS_CODE_REVISION_ID  0x08
#define PCIE20_BIST_HDR_TYPE           0x0C
#define PCIE20_BAR0                    0x10
#define PCIE20_SUBSYSTEM               0x2c
#define PCIE20_CAP_ID_NXT_PTR          0x40
#define PCIE20_CON_STATUS              0x44
#define PCIE20_MSI_BASE(n)             ((n) * 0x200)
#define PCIE20_MSI_CAP_ID_NEXT_CTRL(n) (PCIE20_MSI_BASE(n) + 0x50)
#define PCIE20_MSI_LOWER(n)            (PCIE20_MSI_BASE(n) + 0x54)
#define PCIE20_MSI_UPPER(n)            (PCIE20_MSI_BASE(n) + 0x58)
#define PCIE20_MSI_DATA(n)             (PCIE20_MSI_BASE(n) + 0x5C)
#define PCIE20_MSI_MASK(n)             (PCIE20_MSI_BASE(n) + 0x60)
#define PCIE20_DEVICE_CAPABILITIES     0x74
#define PCIE20_MSIX_TABLE_OFFSET_REG   0xB4
#define PCIE20_MSIX_PBA_OFFSET_REG	0xB8
#define PCIE20_MSIX_CAP_ID_NEXT_CTRL_REG(n) (0x200*n)
#define PCIE20_MSIX_DOORBELL_OFF_REG	0x898 /* Offset from MSI-X capability base */
#define PCIE20_MSIX_ADDRESS_MATCH_LOW_OFF 0x890 /* Offset from MSI-X capability base */
#define PCIE20_MSIX_ADDRESS_MATCH_UPPER_OFF 0x894 /* Offset from MSI-X capability base */
#define PCIE20_MSIX_ADDRESS_MATCH_EN	BIT(0)
#define PCIE20_MSIX_DB_VF_ACTIVE	BIT(15)
#define PCIE20_MASK_EP_L1_ACCPT_LATENCY 0xE00
#define PCIE20_MASK_EP_L0S_ACCPT_LATENCY 0x1C0
#define PCIE20_LINK_CAPABILITIES       0x7C
#define PCIE20_MASK_CLOCK_POWER_MAN    0x40000
#define PCIE20_MASK_L1_EXIT_LATENCY    0x38000
#define PCIE20_MASK_L0S_EXIT_LATENCY   0x7000
#define PCIE20_CAP_LINKCTRLSTATUS      0x80
#define PCIE20_DEVICE_CONTROL2_STATUS2 0x98
#define PCIE20_LINK_CONTROL2_LINK_STATUS2 0xA0
#define PCIE20_BUS_DISCONNECT_STATUS   0x68c
#define PCIE20_ACK_F_ASPM_CTRL_REG     0x70C
#define PCIE20_MASK_ACK_N_FTS          0xff00
#define PCIE20_TRGT_MAP_CTRL_OFF       0x81C
#define PCIE20_MISC_CONTROL_1          0x8BC

#define PCIE20_SYSTEM_PAGE_SIZE_REG	0x20
#define PCIE20_SRIOV_BAR_OFF(n)        (n * 0x4)
#define PCIE20_SRIOV_BAR(n)            (PCIE20_SRIOV_BAR_OFF(n) + 0x24)
#define PCIE20_TOTAL_VFS_INITIAL_VFS_REG 0xC
#define PCIE20_VF_COMMAND_STATUS_OFF(n)  (n * 0x200)
#define PCIE20_VF_COMMAND_STATUS(n)      (PCIE20_VF_COMMAND_STATUS_OFF(n) + 0x4)

#define PCIE20_PLR_IATU_VIEWPORT       0x900
#define PCIE20_PLR_IATU_CTRL1          0x904
#define PCIE20_PLR_IATU_CTRL2          0x908
#define PCIE20_PLR_IATU_LBAR           0x90C
#define PCIE20_PLR_IATU_UBAR           0x910
#define PCIE20_PLR_IATU_LAR            0x914
#define PCIE20_PLR_IATU_LTAR           0x918
#define PCIE20_PLR_IATU_UTAR           0x91c

#define PCIE20_IATU_BASE(n)            (n * 0x200)

#define PCIE20_IATU_O_CTRL1(n)         (PCIE20_IATU_BASE(n) + 0x00)
#define PCIE20_IATU_O_CTRL2(n)         (PCIE20_IATU_BASE(n) + 0x04)
#define PCIE20_IATU_O_LBAR(n)          (PCIE20_IATU_BASE(n) + 0x08)
#define PCIE20_IATU_O_UBAR(n)          (PCIE20_IATU_BASE(n) + 0x0c)
#define PCIE20_IATU_O_LAR(n)           (PCIE20_IATU_BASE(n) + 0x10)
#define PCIE20_IATU_O_LTAR(n)          (PCIE20_IATU_BASE(n) + 0x14)
#define PCIE20_IATU_O_UTAR(n)          (PCIE20_IATU_BASE(n) + 0x18)
#define PCIE20_IATU_O_CTRL3(n)         (PCIE20_IATU_BASE(n) + 0x1C)
#define PCIE20_IATU_O_ULAR(n)          (PCIE20_IATU_BASE(n) + 0x20)

#define PCIE20_IATU_I_CTRL1(n)         (PCIE20_IATU_BASE(n) + 0x100)
#define PCIE20_IATU_I_CTRL2(n)         (PCIE20_IATU_BASE(n) + 0x104)
#define PCIE20_IATU_I_LBAR(n)          (PCIE20_IATU_BASE(n) + 0x108)
#define PCIE20_IATU_I_UBAR(n)          (PCIE20_IATU_BASE(n) + 0x10c)
#define PCIE20_IATU_I_LAR(n)           (PCIE20_IATU_BASE(n) + 0x110)
#define PCIE20_IATU_I_LTAR(n)          (PCIE20_IATU_BASE(n) + 0x114)
#define PCIE20_IATU_I_UTAR(n)          (PCIE20_IATU_BASE(n) + 0x118)

#define PCIE20_IATU_O_INCREASE_REGION_SIZE	0x2000

#define PCIE20_MHICFG                  0x110
#define PCIE20_BHI_EXECENV             0x228
#define PCIE20_MHIVER                  0x108
#define PCIE20_MHICTRL                 0x138
#define PCIE20_MHISTATUS               0x148
#define PCIE20_BHI_VERSION_LOWER	0x200
#define PCIE20_BHI_VERSION_UPPER	0x204
#define PCIE20_BHI_INTVEC		0x220

#define PCIE20_AUX_CLK_FREQ_REG        0xB40
#define PCIE20_GEN3_RELATED_OFF		0x890

#define PCIE20_INT_ALL_VF_BME_STATUS	0x2E68
#define PCIE20_INT_ALL_VF_BME_MASK	0x2E6C
#define PCIE20_INT_ALL_VF_BME_CLEAR	0x2E70

#define PERST_TIMEOUT_US_MIN	              1000
#define PERST_TIMEOUT_US_MAX	              1000
#define PERST_CHECK_MAX_COUNT		      30000
#define LINK_UP_TIMEOUT_US_MIN	              1000
#define LINK_UP_TIMEOUT_US_MAX	              1000
#define LINK_UP_CHECK_MAX_COUNT		      30000
#define BME_TIMEOUT_US_MIN	              1000
#define BME_TIMEOUT_US_MAX	              1000
#define BME_CHECK_MAX_COUNT		      100000
#define PHY_STABILIZATION_DELAY_US_MIN	      1000
#define PHY_STABILIZATION_DELAY_US_MAX	      1000
#define PHY_READY_TIMEOUT_MS                  30000
#define MSI_EXIT_L1SS_WAIT	              10
#define MSI_EXIT_L1SS_WAIT_MAX_COUNT          100
#define XMLH_LINK_UP                          0x400
#define PARF_PM_LINKST_IN_L2                  0x00000020
#define PARF_XMLH_LINK_UP                     0x40000000

#define MAX_PROP_SIZE 32
#define MAX_MSG_LEN 80
#define MAX_NAME_LEN 80
#define MAX_IATU_ENTRY_NUM 2
#define MAX_PCIE_INSTANCES 16
#define MAX_FAST_BOOT_VALUES 16

#define EP_PCIE_LOG_PAGES 50
#define EP_PCIE_MAX_VREG 4
#define EP_PCIE_MAX_CLK 23
#define EP_PCIE_MAX_PIPE_CLK 1
#define EP_PCIE_MAX_RESET 2

#define EP_PCIE_ERROR -30655
#define EP_PCIE_LINK_DOWN 0xFFFFFFFF

#define EP_PCIE_OATU_INDEX_MSI 1
#define EP_PCIE_OATU_INDEX_CTRL 2
#define EP_PCIE_OATU_INDEX_DATA 3
#define EP_PCIE_OATU_INDEX_IPA_MSI 4

#define EP_PCIE_OATU_UPPER 0x100

#define EP_PCIE_GEN_DBG(x...) do { \
	if (ep_pcie_get_debug_mask()) \
		pr_alert(x); \
	else \
		pr_debug(x); \
	} while (0)

#define EP_PCIE_DBG(dev, fmt, arg...) do {			 \
	ipc_log_string((dev)->ipc_log_ful, "%s: " fmt, __func__, arg); \
	if (ep_pcie_get_debug_mask())   \
		pr_alert("%s: " fmt, __func__, arg);		  \
	} while (0)

#define EP_PCIE_DBG2(dev, fmt, arg...) do {			 \
	ipc_log_string((dev)->ipc_log_sel, \
		"DBG1:%s: " fmt, __func__, arg); \
	ipc_log_string((dev)->ipc_log_ful, \
		"DBG2:%s: " fmt, __func__, arg); \
	if (ep_pcie_get_debug_mask())   \
		pr_alert("%s: " fmt, __func__, arg); \
	} while (0)

#define EP_PCIE_DBG_FS(fmt, arg...) pr_alert("%s: " fmt, __func__, arg)

#define EP_PCIE_DUMP(dev, fmt, arg...) do {			\
	ipc_log_string((dev)->ipc_log_dump, \
		"DUMP:%s: " fmt, __func__, arg); \
	if (ep_pcie_get_debug_mask())   \
		pr_alert("%s: " fmt, __func__, arg); \
	} while (0)

#define EP_PCIE_EOM(lane, dev, fmt, arg...) do {			\
	ipc_log_string((dev)->ipc_log_eom[lane], \
		"" fmt, arg); \
	if (ep_pcie_get_debug_mask())   \
		pr_alert("%s: " fmt, __func__, arg); \
	} while (0)

#define EP_PCIE_INFO(dev, fmt, arg...) do {			 \
	ipc_log_string((dev)->ipc_log_sel, \
		"INFO:%s: " fmt, __func__, arg); \
	ipc_log_string((dev)->ipc_log_ful, "%s: " fmt, __func__, arg); \
	pr_info("%s: " fmt, __func__, arg);  \
	} while (0)

#define EP_PCIE_ERR(dev, fmt, arg...) do {			 \
	ipc_log_string((dev)->ipc_log_sel, \
		"ERR:%s: " fmt, __func__, arg); \
	ipc_log_string((dev)->ipc_log_ful, "%s: " fmt, __func__, arg); \
	pr_err("%s: " fmt, __func__, arg);  \
	} while (0)

enum ep_pcie_res {
	EP_PCIE_RES_PARF,
	EP_PCIE_RES_PHY,
	EP_PCIE_RES_MMIO,
	EP_PCIE_RES_MSI,
	EP_PCIE_RES_MSI_VF,
	EP_PCIE_RES_DM_CORE,
	EP_PCIE_RES_DM_VF_CORE,
	EP_PCIE_RES_ELBI,
	EP_PCIE_RES_IATU,
	EP_PCIE_RES_EDMA,
	EP_PCIE_RES_TCSR_PERST,
	EP_PCIE_RES_AOSS_CC_RESET,
	EP_PCIE_RES_RUMI,
	EP_PCIE_MAX_RES,
};

enum ep_pcie_irq {
	EP_PCIE_INT_PM_TURNOFF,
	EP_PCIE_INT_DSTATE_CHANGE,
	EP_PCIE_INT_L1SUB_TIMEOUT,
	EP_PCIE_INT_LINK_UP,
	EP_PCIE_INT_LINK_DOWN,
	EP_PCIE_INT_BRIDGE_FLUSH_N,
	EP_PCIE_INT_BME,
	EP_PCIE_INT_GLOBAL,
	EP_PCIE_MAX_IRQ,
};

enum ep_pcie_gpio {
	EP_PCIE_GPIO_PERST,
	EP_PCIE_GPIO_WAKE,
	EP_PCIE_GPIO_CLKREQ,
	EP_PCIE_GPIO_MDM2AP,
	EP_PCIE_MAX_GPIO,
};

enum ep_pcie_ltssm {
	LTSSM_DETECT_QUIET = 0x00,
	LTSSM_DETECT_ACT = 0x01,
	LTSSM_POLL_ACTIVE = 0x02,
	LTSSM_POLL_COMPLIANCE = 0x03,
	LTSSM_POLL_CONFIG = 0x04,
	LTSSM_PRE_DETECT_QUIET = 0x05,
	LTSSM_DETECT_WAIT = 0x06,
	LTSSM_CFG_LINKWD_START = 0x07,
	LTSSM_CFG_LINKWD_ACEPT = 0x08,
	LTSSM_CFG_LANENUM_WAIT = 0x09,
	LTSSM_CFG_LANENUM_ACEPT = 0x0a,
	LTSSM_CFG_COMPLETE = 0x0b,
	LTSSM_CFG_IDLE = 0x0c,
	LTSSM_RCVRY_LOCK = 0x0d,
	LTSSM_RCVRY_SPEED = 0x0e,
	LTSSM_RCVRY_RCVRCFG = 0x0f,
	LTSSM_RCVRY_IDLE = 0x10,
	LTSSM_RCVRY_EQ0 = 0x20,
	LTSSM_RCVRY_EQ1 = 0x21,
	LTSSM_RCVRY_EQ2 = 0x22,
	LTSSM_RCVRY_EQ3 = 0x23,
	LTSSM_L0 = 0x11,
	LTSSM_L0S = 0x12,
	LTSSM_L123_SEND_EIDLE = 0x13,
	LTSSM_L1_IDLE = 0x14,
	LTSSM_L2_IDLE = 0x15,
	LTSSM_L2_WAKE = 0x16,
	LTSSM_DISABLED_ENTRY = 0x17,
	LTSSM_DISABLED_IDLE = 0x18,
	LTSSM_DISABLED = 0x19,
	LTSSM_LPBK_ENTRY = 0x1a,
	LTSSM_LPBK_ACTIVE = 0x1b,
	LTSSM_LPBK_EXIT = 0x1c,
	LTSSM_LPBK_EXIT_TIMEOUT = 0x1d,
	LTSSM_HOT_RESET_ENTRY = 0x1e,
	LTSSM_HOT_RESET = 0x1f,
};

static const char * const ep_pcie_ltssm_str[] = {
	[LTSSM_DETECT_QUIET] = "LTSSM_DETECT_QUIET",
	[LTSSM_DETECT_ACT] = "LTSSM_DETECT_ACT",
	[LTSSM_POLL_ACTIVE] = "LTSSM_POLL_ACTIVE",
	[LTSSM_POLL_COMPLIANCE] = "LTSSM_POLL_COMPLIANCE",
	[LTSSM_POLL_CONFIG] = "LTSSM_POLL_CONFIG",
	[LTSSM_PRE_DETECT_QUIET] = "LTSSM_PRE_DETECT_QUIET",
	[LTSSM_DETECT_WAIT] = "LTSSM_DETECT_WAIT",
	[LTSSM_CFG_LINKWD_START] = "LTSSM_CFG_LINKWD_START",
	[LTSSM_CFG_LINKWD_ACEPT] = "LTSSM_CFG_LINKWD_ACEPT",
	[LTSSM_CFG_LANENUM_WAIT] = "LTSSM_CFG_LANENUM_WAIT",
	[LTSSM_CFG_LANENUM_ACEPT] = "LTSSM_CFG_LANENUM_ACEPT",
	[LTSSM_CFG_COMPLETE] = "LTSSM_CFG_COMPLETE",
	[LTSSM_CFG_IDLE] = "LTSSM_CFG_IDLE",
	[LTSSM_RCVRY_LOCK] = "LTSSM_RCVRY_LOCK",
	[LTSSM_RCVRY_SPEED] = "LTSSM_RCVRY_SPEED",
	[LTSSM_RCVRY_RCVRCFG] = "LTSSM_RCVRY_RCVRCFG",
	[LTSSM_RCVRY_IDLE] = "LTSSM_RCVRY_IDLE",
	[LTSSM_RCVRY_EQ0] = "LTSSM_RCVRY_EQ0",
	[LTSSM_RCVRY_EQ1] = "LTSSM_RCVRY_EQ1",
	[LTSSM_RCVRY_EQ2] = "LTSSM_RCVRY_EQ2",
	[LTSSM_RCVRY_EQ3] = "LTSSM_RCVRY_EQ3",
	[LTSSM_L0] = "LTSSM_L0",
	[LTSSM_L0S] = "LTSSM_L0S",
	[LTSSM_L123_SEND_EIDLE] = "LTSSM_L123_SEND_EIDLE",
	[LTSSM_L1_IDLE] = "LTSSM_L1_IDLE",
	[LTSSM_L2_IDLE] = "LTSSM_L2_IDLE",
	[LTSSM_L2_WAKE] = "LTSSM_L2_WAKE",
	[LTSSM_DISABLED_ENTRY] = "LTSSM_DISABLED_ENTRY",
	[LTSSM_DISABLED_IDLE] = "LTSSM_DISABLED_IDLE",
	[LTSSM_DISABLED] = "LTSSM_DISABLED",
	[LTSSM_LPBK_ENTRY] = "LTSSM_LPBK_ENTRY",
	[LTSSM_LPBK_ACTIVE] = "LTSSM_LPBK_ACTIVE",
	[LTSSM_LPBK_EXIT] = "LTSSM_LPBK_EXIT",
	[LTSSM_LPBK_EXIT_TIMEOUT] = "LTSSM_LPBK_EXIT_TIMEOUT",
	[LTSSM_HOT_RESET_ENTRY] = "LTSSM_HOT_RESET_ENTRY",
	[LTSSM_HOT_RESET] = "LTSSM_HOT_RESET",
};

static const char * const ep_pcie_l1ss_str[] = {
	"LINK_IS_NOT_IN_L1SS",
	"LINK_IS_IN_L1SS",
};

static const char * const ep_pcie_dsate_str[] = {
	"D0_STATE",
	"D3_HOT_STATE",
};

struct ep_pcie_gpio_info_t {
	char  *name;
	u32   num;
	bool  out;
	u32   on;
	u32   init;
};

struct ep_pcie_vreg_info_t {
	struct regulator  *hdl;
	char              *name;
	u32           max_v;
	u32           min_v;
	u32           opt_mode;
	bool          required;
};

struct ep_pcie_clk_info_t {
	struct clk  *hdl;
	char        *name;
	u32         freq;
	bool        required;
};

struct ep_pcie_reset_info_t {
	struct reset_control *hdl;
	char *name;
	bool required;
};

struct ep_pcie_res_info_t {
	char            *name;
	struct resource *resource;
	void __iomem    *base;
};

struct ep_pcie_irq_info_t {
	char         *name;
	u32          num;
};

/* phy info structure */
struct ep_pcie_phy_info_t {
	u32	offset;
	u32	val;
	u32	delay;
};

/* pcie endpoint device structure */
struct ep_pcie_dev_t {
	struct platform_device       *pdev;
	struct regulator             *gdsc;
	struct regulator             *gdsc_phy;
	struct ep_pcie_vreg_info_t   vreg[EP_PCIE_MAX_VREG];
	struct ep_pcie_gpio_info_t   gpio[EP_PCIE_MAX_GPIO];
	struct ep_pcie_clk_info_t    clk[EP_PCIE_MAX_CLK];
	struct ep_pcie_clk_info_t    pipeclk[EP_PCIE_MAX_PIPE_CLK];
	struct ep_pcie_reset_info_t  reset[EP_PCIE_MAX_RESET];
	struct ep_pcie_irq_info_t    irq[EP_PCIE_MAX_IRQ];
	struct ep_pcie_res_info_t    res[EP_PCIE_MAX_RES];

	void __iomem                 *parf;
	void __iomem                 *phy;
	void __iomem                 *mmio;
	void __iomem                 *msi;
	void __iomem                 *msi_vf;
	void __iomem                 *dm_core;
	void __iomem                 *dm_core_vf;
	void __iomem                 *edma;
	void __iomem                 *elbi;
	void __iomem                 *iatu;
	void __iomem		     *tcsr_perst_en;
	void __iomem		     *aoss_rst_perst;
	void __iomem		     *rumi;

	struct msm_bus_scale_pdata   *bus_scale_table;
	struct icc_path		     *icc_path;
	u16                          vendor_id;
	u16                          device_id;
	u32                          subsystem_id;
	u32                          link_speed;
	/* Stores current link speed and link width */
	u32                          current_link_speed;
	u32                          current_link_width;
	bool                         active_config;
	bool                         aggregated_irq;
	bool                         mhi_a7_irq;
	bool                         db_fwd_off_varied;
	bool                         parf_msi_vf_indexed;
	bool                         pcie_edma;
	bool                         tcsr_not_supported;
	bool			     m2_autonomous;
	bool			     mhi_soc_reset_en;
	bool			     aoss_rst_clear;
	bool			     avoid_reboot_in_d3hot;
	u32                          dbi_base_reg;
	u32                          slv_space_reg;
	u32                          phy_status_reg;
	u32			     pcie_cesta_clkreq_offset;
	u32			phy_status_bit_mask_bit;
	u32                          phy_init_len;
	u32			     mhi_soc_reset_offset;
	struct ep_pcie_phy_info_t    *phy_init;
	bool                         perst_enum;

	u32                          rev;
	u32                          phy_rev;
	u32			     aux_clk_val;
	/* MSIX enable status, offset of capability register */
	u32			     msix_cap;
	u32			     sriov_cap;
	u32			     num_vfs;
	/* sriov_mask signifies the BME bit positions in PARF_INT_ALL_3_STATUS register */
	ulong                        sriov_mask;
	ulong                        sriov_enumerated;
	void                         *ipc_log_sel;
	void                         *ipc_log_ful;
	void                         *ipc_log_dump;
	void                         *ipc_log_eom[16];
	void                         *ipc_log_eom_delay;
	struct mutex                 setup_mtx;
	struct mutex                 ext_mtx;
	spinlock_t                   ext_lock;
	unsigned long                ext_save_flags;

	spinlock_t                   isr_lock;
	unsigned long                isr_save_flags;
	ulong                        linkdown_counter;
	ulong                        linkup_counter;
	ulong                        bme_counter;
	ulong                        pm_to_counter;
	ulong                        d0_counter;
	ulong                        d3_counter;
	ulong                        perst_ast_counter;
	ulong                        perst_deast_counter;
	ktime_t                      ltssm_detect_ts;
	ulong                        wake_counter;
	ulong                        msi_counter;
	ulong                        msix_counter;
	ulong                        global_irq_counter;
	ulong                        sriov_irq_counter;
	ulong                        perst_ast_in_enum_counter;

	bool                         dump_conf;
	bool                         config_mmio_init;
	bool                         enumerated;
	enum ep_pcie_link_status     link_status;
	bool                         power_on;
	bool                         l23_ready;
	bool                         l1ss_enabled;
	bool                         no_notify;
	bool                         client_ready;
	atomic_t		     ep_pcie_dev_wake;
	atomic_t                     perst_deast;
	int                          perst_irq;
	atomic_t                     host_wake_pending;
	struct ep_pcie_msi_config    msi_cfg[MAX_PCIE_INSTANCES];
	bool                         conf_ipa_msi_iatu[MAX_PCIE_INSTANCES];
	bool                         use_iatu_msi;

	struct ep_pcie_register_event *event_reg;
	struct work_struct           handle_bme_work;
	struct work_struct           handle_clkreq;
	struct work_struct           handle_d3cold_work;

	struct clk		     *pipe_clk_mux;
	struct clk		     *pipe_clk_ext_src;
	struct clk		     *ref_clk_src;

	bool				override_disable_sriov;
	bool				no_path_from_ipa_to_pcie;
	bool				configure_hard_reset;
	bool				l1_disable;
	bool				perst_sep_en;
	bool				hot_rst_disable;
	u32				tcsr_perst_separation_en_offset;
	u32				tcsr_reset_separation_offset;
	u32				tcsr_perst_enable_offset;
	u32				perst_raw_rst_status_mask;
	u32				pcie_disconnect_req_reg_mask;
	u32				tcsr_hot_reset_en_offset;
};

extern struct ep_pcie_dev_t ep_pcie_dev;
extern struct ep_pcie_hw hw_drv;

static inline void ep_pcie_write_mask(void __iomem *addr,
				u32 clear_mask, u32 set_mask)
{
	u32 val;

	val = (readl_relaxed(addr) & ~clear_mask) | set_mask;
	writel_relaxed(val, addr);
	/* ensure register write goes through before next register operation */
	wmb();
}

static inline void ep_pcie_write_reg(void __iomem *base, u32 offset, u32 value)
{
	writel_relaxed(value, base + offset);
	/* ensure register write goes through before next register operation */
	wmb();
}

static inline void ep_pcie_write_reg_field(void __iomem *base, u32 offset,
	const u32 mask, u32 val)
{
	u32 shift = find_first_bit((void *)&mask, 32);
	u32 tmp = readl_relaxed(base + offset);

	tmp &= ~mask; /* clear written bits */
	val = tmp | (val << shift);
	writel_relaxed(val, base + offset);
	/* ensure register write goes through before next register operation */
	wmb();
}

extern int ep_pcie_core_register_event(struct ep_pcie_register_event *reg);
extern int ep_pcie_get_debug_mask(void);
extern void ep_pcie_phy_init(struct ep_pcie_dev_t *dev);
extern bool ep_pcie_phy_is_ready(struct ep_pcie_dev_t *dev);
extern void ep_pcie_reg_dump(struct ep_pcie_dev_t *dev, u32 sel, bool linkdown);
extern void ep_pcie_clk_dump(struct ep_pcie_dev_t *dev);
extern void ep_pcie_debugfs_init(struct ep_pcie_dev_t *ep_dev);
extern void ep_pcie_debugfs_exit(void);

#if IS_ENABLED(CONFIG_L1SS_RESOURCES_HANDLING)
int ep_pcie_l1ss_resources_init(struct ep_pcie_dev_t *dev);
int ep_pcie_l1ss_resources_deinit(struct ep_pcie_dev_t *dev);
#else
static inline int ep_pcie_l1ss_resources_init(struct ep_pcie_dev_t *dev)
{
	return 0;
}

static inline int ep_pcie_l1ss_resources_deinit(struct ep_pcie_dev_t *dev)
{
	return 0;
}
#endif /* CONFIG_L1SS_RESOURCES_HANDLING */

#endif
