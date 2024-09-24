// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */
/*
 * Debugging enhancement in MSM PCIe endpoint driver.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/msm_ep_pcie.h>
#include <linux/pci_regs.h>
#include "ep_pcie_com.h"
#include "ep_pcie_phy.h"

#define PCIE_PHYSICAL_DEVICE 0
#define EP_PCIE_MAX_DEBUGFS_OPTION 29

#define EYE_HEIGHT_STEP                         3
#define MAX_EYE_HEIGHT                          128
#define MAX_EYE_HEIGHT_MV                       (MAX_EYE_HEIGHT * EYE_HEIGHT_STEP)
#define EYE_WIDTH_STEP                          (1/32)
#define MAX_EYE_WIDTH                           64
#define MAX_EYE_WIDTH_UI                        (MAX_EYE_WIDTH * EYE_WIDTH_STEP)
#define MAX_NUM_LANES                           16
#define EOM_TEST_TIME_DEFAULT                   1
#define EOM_TEST_TIME_MAX                       1000

#define QSERDES_OFFSET_SIZE                     0x800
#define QSERDES_OFFSET_LANE_SIZE(lanenum)       (lanenum * QSERDES_OFFSET_SIZE)
#define LINK_STATUS_REG_SPEED_SHFT              0x10
#define LINK_STATUS_REG_SPEED                   0xf0000
#define LINK_STATUS_REG_LANES                   0x3f00000
#define LINK_STATUS_REG_LANES_SHFT              0x14
#define POSITIVE_SEQUENCE                       1
#define NEGATIVE_SEQUENCE                       0

static const char * const
	ep_pcie_debugfs_option_desc[EP_PCIE_MAX_DEBUGFS_OPTION] = {
	"Output status",
	"Output PHY and PARF registers",
	"Output Core/DBI registers",
	"Output MMIO/MHI registers",
	"Output ELBI registers",
	"Output MSI registers",
	"Turn on link",
	"Enumeration",
	"Turn off link",
	"Check MSI",
	"Trigger MSI",
	"Indicate the status of PCIe link",
	"Configure outbound iATU",
	"Wake up from D3hot",
	"Configure routing of doorbells",
	"Write D3",
	"Write D0",
	"Assert wake",
	"Deassert wake",
	"Output PERST# status",
	"Output WAKE# status",
	"Enable dumping core/dbi registers when D3hot set by host",
	"Disable dumping core/dbi registers when D3hot set by host",
	"Dump edma registers",
	"Dump clock CBCR registers",
	"Dump ASPM stats",
	"Positive eye sequence",
	"Negative eye sequence",
	"PCIe Link Status"
	};

static struct dentry *dent_ep_pcie;
static struct dentry *dfile_case;
static struct ep_pcie_dev_t *dev;

static void ep_pcie_check_link_state(struct ep_pcie_dev_t *dev)
{
	u32 val = 0;

	if (!dev->power_on) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: the power is already down; can't dump registers\n",
			dev->rev);
		return;
	}
	EP_PCIE_DBG_FS("PCIe V%d: ********** PCIe LTSSM State **********", dev->rev);
	val = readl_relaxed(dev->parf + PCIE20_PARF_LTSSM);
	EP_PCIE_DBG_FS("PARF_LTSSM: 0x%x\n", val);
	EP_PCIE_DBG_FS("LTSSM State: %s\n",
		ep_pcie_ltssm_str[val & PCIE20_PARF_LTSSM_STATE_MASK]);

	EP_PCIE_DBG_FS("PCIe V%d: *********** PCIe PM Status ***********", dev->rev);
	val = readl_relaxed(dev->parf + PCIE20_PARF_PM_STTS);
	EP_PCIE_DBG_FS("PARF_PM_STTS: 0x%x\n", val);
	EP_PCIE_DBG_FS("Device State: %s\n",
		ep_pcie_dsate_str[val & PCIE20_PARF_PM_STTS_PM_DSTATE_0_MASK
		>> PCIE20_PARF_PM_STTS_PM_DSTATE_0_SHIFT]);
	EP_PCIE_DBG_FS("L1 Sub State: %s\n",
		ep_pcie_l1ss_str[(val & PCIE20_PARF_PM_STTS_PM_LINKST_IN_L1SUB_MASK)
		>> PCIE20_PARF_PM_STTS_PM_LINKST_IN_L1SUB_SHIFT]);
}

static void ep_ep_pcie_phy_dump_pcs_debug_bus(struct ep_pcie_dev_t *dev,
					u32 cntrl4, u32 cntrl5,
					u32 cntrl6, u32 cntrl7)
{
	ep_pcie_write_reg(dev->phy, PCIE_PHY_TEST_CONTROL4, cntrl4);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_TEST_CONTROL5, cntrl5);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_TEST_CONTROL6, cntrl6);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_TEST_CONTROL7, cntrl7);

	if (!cntrl4 && !cntrl5 && !cntrl6 && !cntrl7) {
		EP_PCIE_DUMP(dev,
			"PCIe V%d: zero out test control registers\n\n",
			dev->rev);
		return;
	}

	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_TEST_CONTROL4: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_TEST_CONTROL4));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_TEST_CONTROL5: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_TEST_CONTROL5));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_TEST_CONTROL6: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_TEST_CONTROL6));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_TEST_CONTROL7: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_TEST_CONTROL7));

	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_DEBUG_BUS_0_STATUS: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_DEBUG_BUS_0_STATUS));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_DEBUG_BUS_1_STATUS: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_DEBUG_BUS_1_STATUS));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_DEBUG_BUS_2_STATUS: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_DEBUG_BUS_2_STATUS));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_DEBUG_BUS_3_STATUS: 0x%x\n\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_DEBUG_BUS_3_STATUS));
}

static void ep_ep_pcie_phy_dump_pcs_misc_debug_bus(struct ep_pcie_dev_t *dev,
					u32 b0, u32 b1,	u32 b2, u32 b3)
{
	ep_pcie_write_reg(dev->phy, PCIE_PHY_MISC_DEBUG_BUS_BYTE0_INDEX, b0);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_MISC_DEBUG_BUS_BYTE1_INDEX, b1);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_MISC_DEBUG_BUS_BYTE2_INDEX, b2);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_MISC_DEBUG_BUS_BYTE3_INDEX, b3);

	if (!b0 && !b1 && !b2 && !b3) {
		EP_PCIE_DUMP(dev,
			"PCIe V%d: zero out misc debug bus byte index registers\n\n",
			dev->rev);
		return;
	}

	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_BYTE0_INDEX: 0x%x\n",
		dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_BYTE0_INDEX));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_BYTE1_INDEX: 0x%x\n",
		dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_BYTE1_INDEX));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_BYTE2_INDEX: 0x%x\n",
		dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_BYTE2_INDEX));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_BYTE3_INDEX: 0x%x\n",
		dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_BYTE3_INDEX));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_0_STATUS: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_0_STATUS));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_1_STATUS: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_1_STATUS));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_2_STATUS: 0x%x\n", dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_2_STATUS));
	EP_PCIE_DUMP(dev,
		"PCIe V%d: PCIE_PHY_MISC_DEBUG_BUS_3_STATUS: 0x%x\n\n",
		dev->rev,
		readl_relaxed(dev->phy + PCIE_PHY_MISC_DEBUG_BUS_3_STATUS));
}

static void ep_pcie_phy_dump(struct ep_pcie_dev_t *dev)
{
	int i;
	u32 write_val;

	EP_PCIE_DUMP(dev, "PCIe V%d: Beginning of PHY debug dump\n\n", dev->rev);

	EP_PCIE_DUMP(dev, "PCIe V%d: PCS Debug Signals\n\n", dev->rev);

	ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0x01, 0x02, 0x03, 0x0A);
	ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0x0E, 0x0F, 0x12, 0x13);
	ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0x18, 0x19, 0x1A, 0x1B);
	ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0x1C, 0x1D, 0x1E, 0x1F);
	ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0x20, 0x21, 0x22, 0x23);
	ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0, 0, 0, 0);

	EP_PCIE_DUMP(dev, "PCIe V%d: PCS Misc Debug Signals\n\n", dev->rev);

	ep_ep_pcie_phy_dump_pcs_misc_debug_bus(dev, 0x1, 0x2, 0x3, 0x4);
	ep_ep_pcie_phy_dump_pcs_misc_debug_bus(dev, 0x5, 0x6, 0x7, 0x8);
	ep_ep_pcie_phy_dump_pcs_misc_debug_bus(dev, 0, 0, 0, 0);

	EP_PCIE_DUMP(dev, "PCIe V%d: QSERDES COM Debug Signals\n\n", dev->rev);

	for (i = 0; i < 2; i++) {
		write_val = 0x2 + i;

		ep_pcie_write_reg(dev->phy, QSERDES_COM_DEBUG_BUS_SEL,
			write_val);

		EP_PCIE_DUMP(dev,
			"PCIe V%d: to QSERDES_COM_DEBUG_BUS_SEL: 0x%x\n",
			dev->rev,
			readl_relaxed(dev->phy + QSERDES_COM_DEBUG_BUS_SEL));
		EP_PCIE_DUMP(dev,
			"PCIe V%d: QSERDES_COM_DEBUG_BUS0: 0x%x\n",
			dev->rev,
			readl_relaxed(dev->phy + QSERDES_COM_DEBUG_BUS0));
		EP_PCIE_DUMP(dev,
			"PCIe V%d: QSERDES_COM_DEBUG_BUS1: 0x%x\n",
			dev->rev,
			readl_relaxed(dev->phy + QSERDES_COM_DEBUG_BUS1));
		EP_PCIE_DUMP(dev,
			"PCIe V%d: QSERDES_COM_DEBUG_BUS2: 0x%x\n",
			dev->rev,
			readl_relaxed(dev->phy + QSERDES_COM_DEBUG_BUS2));
		EP_PCIE_DUMP(dev,
			"PCIe V%d: QSERDES_COM_DEBUG_BUS3: 0x%x\n\n",
			dev->rev,
			readl_relaxed(dev->phy + QSERDES_COM_DEBUG_BUS3));
	}

	ep_pcie_write_reg(dev->phy, QSERDES_COM_DEBUG_BUS_SEL, 0);

	EP_PCIE_DUMP(dev, "PCIe V%d: QSERDES LANE Debug Signals\n\n",
			dev->rev);

	for (i = 0; i < 3; i++) {
		write_val = 0x1 + i;
		ep_pcie_write_reg(dev->phy,
			QSERDES_TX_DEBUG_BUS_SEL, write_val);
		EP_PCIE_DUMP(dev,
			"PCIe V%d: QSERDES_TX_DEBUG_BUS_SEL: 0x%x\n",
			dev->rev,
			readl_relaxed(dev->phy + QSERDES_TX_DEBUG_BUS_SEL));

		ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0x30, 0x31, 0x32, 0x33);
	}

	ep_ep_pcie_phy_dump_pcs_debug_bus(dev, 0, 0, 0, 0);

	EP_PCIE_DUMP(dev, "PCIe V%d: End of PHY debug dump\n\n", dev->rev);

}

void ep_pcie_reg_dump(struct ep_pcie_dev_t *dev, u32 sel, bool linkdown)
{
	int r, i;
	u32 original;
	u32 size;

	EP_PCIE_DBG(dev,
		"PCIe V%d: Dump PCIe reg for 0x%x %s linkdown\n",
		dev->rev, sel, linkdown ? "with" : "without");

	if (!dev->power_on) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: the power is already down; can't dump registers\n",
			dev->rev);
		return;
	}

	if (linkdown) {
		EP_PCIE_DUMP(dev,
			"PCIe V%d: dump PARF registers for linkdown case\n",
			dev->rev);

		original = readl_relaxed(dev->parf + PCIE20_PARF_SYS_CTRL);
		for (i = 1; i <= 0x1A; i++) {
			ep_pcie_write_mask(dev->parf + PCIE20_PARF_SYS_CTRL,
				0xFF0000, i << 16);
			EP_PCIE_DUMP(dev,
				"PCIe V%d: PARF_SYS_CTRL:0x%x PARF_TEST_BUS:0x%x\n",
				dev->rev,
				readl_relaxed(dev->parf + PCIE20_PARF_SYS_CTRL),
				readl_relaxed(dev->parf +
							PCIE20_PARF_TEST_BUS));
		}
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_SYS_CTRL, original);
	}

	for (r = 0; r < EP_PCIE_MAX_RES; r++) {
		if (!(sel & BIT(r)))
			continue;

		if ((r == EP_PCIE_RES_PHY) && (dev->phy_rev > 3))
			ep_pcie_phy_dump(dev);

		size = resource_size(dev->res[r].resource);
		EP_PCIE_DUMP(dev,
			"\nPCIe V%d: dump registers of %s\n\n",
			dev->rev, dev->res[r].name);

		for (i = 0; i < size; i += 32) {
			EP_PCIE_DUMP(dev,
				"0x%04x %08x %08x %08x %08x %08x %08x %08x %08x\n",
				i, readl_relaxed(dev->res[r].base + i),
				readl_relaxed(dev->res[r].base + (i + 4)),
				readl_relaxed(dev->res[r].base + (i + 8)),
				readl_relaxed(dev->res[r].base + (i + 12)),
				readl_relaxed(dev->res[r].base + (i + 16)),
				readl_relaxed(dev->res[r].base + (i + 20)),
				readl_relaxed(dev->res[r].base + (i + 24)),
				readl_relaxed(dev->res[r].base + (i + 28)));
		}
	}
}

static void ep_pcie_show_status(struct ep_pcie_dev_t *dev)
{
	EP_PCIE_DBG_FS("PCIe: is %s enumerated\n",
		dev->enumerated ? "" : "not");
	EP_PCIE_DBG_FS("PCIe: link is %s\n",
		(dev->link_status == EP_PCIE_LINK_ENABLED)
		? "enabled" : "disabled");
	EP_PCIE_DBG_FS("the power is %s on\n",
		dev->power_on ? "" : "not");
	EP_PCIE_DBG_FS("linkdown_counter: %lu\n",
		dev->linkdown_counter);
	EP_PCIE_DBG_FS("linkup_counter: %lu\n",
		dev->linkup_counter);
	EP_PCIE_DBG_FS("wake_counter: %lu\n",
		dev->wake_counter);
	EP_PCIE_DBG_FS("d0_counter: %lu\n",
		dev->d0_counter);
	EP_PCIE_DBG_FS("d3_counter: %lu\n",
		dev->d3_counter);
	EP_PCIE_DBG_FS("perst_ast_counter: %lu\n",
		dev->perst_ast_counter);
	EP_PCIE_DBG_FS("perst_deast_counter: %lu\n",
		dev->perst_deast_counter);
}

static int ep_pcie_cmd_debug_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < EP_PCIE_MAX_DEBUGFS_OPTION; i++)
		seq_printf(m, "\t%d:\t %s\n", i,
			ep_pcie_debugfs_option_desc[i]);

	return 0;
}

static int ep_pcie_cmd_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, ep_pcie_cmd_debug_show, NULL);
}


static void ep_pcie_aspm_stat(struct ep_pcie_dev_t *ep_dev)
{
	if (!ep_dev->mmio) {
		EP_PCIE_DBG_FS("PCIe: V%d: No dev or MHI space found\n", ep_dev->rev);
		return;
	}

	if (!ep_dev->power_on) {
		EP_PCIE_DBG_FS("PCIe V%d: the power is already down; can't dump registers\n",
				ep_dev->rev);
		return;
	}

	EP_PCIE_DBG_FS("PCIe: V%d: L0s: %u L1: %u L1.1: %u L1.2: %u\n",
		ep_dev->rev,
		readl_relaxed(ep_dev->mmio + PCIE20_PARF_DEBUG_CNT_IN_L0S),
		readl_relaxed(ep_dev->mmio + PCIE20_PARF_DEBUG_CNT_IN_L1),
		readl_relaxed(ep_dev->mmio + PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L1),
		readl_relaxed(ep_dev->mmio + PCIE20_PARF_DEBUG_CNT_IN_L1SUB_L2));

}

/*
 * ep_pcie_check_spd_and_lns - Checks whether GenSpeed
 * is less than Gen 4 and whether all 16 lanes are up.
 * Since Lane Margining Control and Status registers exist from Gen4 onwards,
 * a GenSpeed less than that would result in termination of execution.
 *
 * Return: true on success, false on error
 */
static bool ep_pcie_check_spd_and_lns(struct ep_pcie_dev_t *dev, u32 linkspeed, u32 maxlanes)
{

	if (linkspeed < PCI_EXP_LNKSTA_CLS_16_0GB) {
		EP_PCIE_DBG_FS("PCIe: V%d: PCIe Link speed detected value is:Gen %d\n",
				dev->rev, linkspeed);
		EP_PCIE_DBG_FS("PCIe: V%d: Incorrect Link Speed for EOM test\n",
				dev->rev);
		EP_PCIE_DBG_FS("PCIe: V%d: Negotiated Link Speed value should be at least Gen 4\n",
				dev->rev);
		EP_PCIE_DBG_FS(
			"PCIe: V%d: Quitting EOM Test\n", dev->rev);

		return false;
	}

	if (maxlanes != MAX_NUM_LANES) {
		EP_PCIE_DBG_FS("PCIe: V%d: Detected number of lanes is: %d\n", dev->rev, maxlanes);
		EP_PCIE_DBG_FS("PCIe: V%d: Ensure all 16 lanes are up. Execution Terminated.\n",
				dev->rev);

		return false;
	}

	return true;
}

/*
 * ep_pcie_eom_init - Initialize EOM registers for the chosen lane
 *
 * Return : void
 */
static void ep_pcie_eom_init(struct ep_pcie_dev_t *dev, u32 is_positive_seq)
{
	u32 lanenum = 0;

	EP_PCIE_DUMP(dev, "Initializing lanes%d", dev->rev);

	while (lanenum < MAX_NUM_LANES) {
		EP_PCIE_EOM(lanenum, dev, "*** Margining RC , lane %d ***", lanenum);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_TX0_RESET_GEN_MUXES +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x3);

		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_CDR_RESET_OVERRIDE +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0xA);

		if (is_positive_seq) {
			ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_EOM_CTRL1 +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x98);
		} else {
			ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_EOM_CTRL1 +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0xD8);
		}

		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_EOM_CTRL2 +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x28);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_AUX_CONTROL +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x40);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RCLK_AUXDATA_SEL +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0xfc);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL2 +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x80);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RX_MARG_VERTICAL_CTRL +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x2);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_AUXDATA_TB +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x80);

		ndelay(100);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL_4 +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x33);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL3 +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x4c);

		ndelay(100);
		ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL3 +
			QSERDES_OFFSET_LANE_SIZE(lanenum), 0x48);
		lanenum = lanenum + 1;
	}

}

/*
 * ep_pcie_eom_eye_seq - Varies the x and y coordinates
 * from (0,0) to (MAX_EYE_WIDTH, MAX_EYE_HEIGHT) for each lane
 * and logs the error rate at each sample location.
 * Return : void
 */
static void ep_pcie_eom_eye_seq(struct ep_pcie_dev_t *dev, u32 is_positive_seq)
{
	u32 lanenum;
	u32 regval;
	u32 temp;
	u32 tmp;
	u32 xcoord;
	u32 ycoord;
	u32 linkspeed;
	u32 maxlanes;
	u32 temp_err_low;
	u32 temp_err_high;
	u32 errorcntr;

	char logname[15];

	EP_PCIE_DUMP(dev, "Starting PCIe EOM tests for lane %d", dev->rev);

	regval = readl_relaxed(dev->dm_core + PCIE20_CAP_LINKCTRLSTATUS);
	linkspeed = (regval & LINK_STATUS_REG_SPEED) >> LINK_STATUS_REG_SPEED_SHFT;
	maxlanes = (regval & LINK_STATUS_REG_LANES) >> LINK_STATUS_REG_LANES_SHFT;

	if (!ep_pcie_check_spd_and_lns(dev, linkspeed, maxlanes))
		return;

	EP_PCIE_DUMP(dev, "Entering EOM_INIT%d", dev->rev);

	lanenum = 0;
	while (lanenum < MAX_NUM_LANES) {
		if (!dev->ipc_log_eom[lanenum]) {
			snprintf(logname, 15, "ep-pcie-eom-%d", lanenum);
			dev->ipc_log_eom[lanenum] = ipc_log_context_create(130, logname, 0);
		}
		lanenum = lanenum+1;
	}

	ep_pcie_eom_init(dev, is_positive_seq);

	lanenum = 0;
	ycoord = 0x0;
	while (ycoord < MAX_EYE_HEIGHT) {
		xcoord = 0;
		while (xcoord < MAX_EYE_WIDTH) {
			lanenum = 0;
			EP_PCIE_DBG_FS("Running for x:%d y:%d\n", xcoord, ycoord);
			while (lanenum < MAX_NUM_LANES) {
				temp = ycoord | 0x80;
				ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_AUXDATA_TB +
					QSERDES_OFFSET_LANE_SIZE(lanenum), temp);
				tmp = (xcoord | 0x40);
				ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_AUX_CONTROL +
					QSERDES_OFFSET_LANE_SIZE(lanenum), tmp);
				ndelay(100);
				ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL_4 +
					QSERDES_OFFSET_LANE_SIZE(lanenum), 0x33);
				ndelay(100);
				ep_pcie_write_reg(dev->phy,	PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL3
						+ QSERDES_OFFSET_LANE_SIZE(lanenum), 0x4c);
				ndelay(100);
				ep_pcie_write_reg(dev->phy,	PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL3
						+ QSERDES_OFFSET_LANE_SIZE(lanenum), 0x48);
				ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RCLK_AUXDATA_SEL
						+ QSERDES_OFFSET_LANE_SIZE(lanenum), 0xfc);
				ndelay(100);
				ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RCLK_AUXDATA_SEL
						+ QSERDES_OFFSET_LANE_SIZE(lanenum), 0xf4);
				lanenum = lanenum + 1;
			}
				usleep_range(200000, 205000);

			lanenum = 0;
			while (lanenum < MAX_NUM_LANES) {
				temp_err_low = readl_relaxed(dev->phy +
						PCIE20_PHY_QSERDES_RX0_IA_ERROR_COUNTER_LOW +
						QSERDES_OFFSET_LANE_SIZE(lanenum));

				temp_err_high = readl_relaxed(dev->phy +
						PCIE20_PHY_QSERDES_RX0_IA_ERROR_COUNTER_HIGH +
						QSERDES_OFFSET_LANE_SIZE(lanenum));

				errorcntr = (temp_err_low & 0xff);
				errorcntr = errorcntr | ((temp_err_high & 0xff) << 8);

				EP_PCIE_EOM(lanenum, dev, "L: %d, x: %d, y: %d, E: %d", lanenum,
					xcoord, ycoord, errorcntr);

				ep_pcie_write_reg(dev->phy, PCIE20_PHY_QSERDES_RX0_RX_MARG_CTRL_4 +
					QSERDES_OFFSET_LANE_SIZE(lanenum), 0x23);

				lanenum = lanenum + 1;
			}
			xcoord  = xcoord+1;
		}
		ycoord = ycoord+1;
	}
}


static ssize_t ep_pcie_cmd_debug(struct file *file,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long ret;
	char str[MAX_MSG_LEN];
	unsigned int testcase = 0;
	struct ep_pcie_msi_config msi_cfg;
	int i, vf_id = PCIE_PHYSICAL_DEVICE;
	struct ep_pcie_hw *phandle = NULL;
	struct ep_pcie_iatu entries[2] = {
		{0x80000000, 0xbe7fffff, 0, 0},
		{0xb1440000, 0xb144ae1e, 0x31440000, 0}
	};
	struct ep_pcie_db_config chdb_cfg = {0x64, 0x6b, 0xfd4fa000};
	struct ep_pcie_db_config erdb_cfg = {0x64, 0x6b, 0xfd4fa080};

	phandle = ep_pcie_get_phandle(hw_drv.device_id);

	memset(str, 0, sizeof(str));
	ret = copy_from_user(str, buf, sizeof(str));
	if (ret)
		return -EFAULT;

	for (i = 0; i < sizeof(str) && (str[i] >= '0') && (str[i] <= '9'); ++i)
		testcase = (testcase * 10) + (str[i] - '0');

	EP_PCIE_DBG_FS("PCIe: TEST: %d\n", testcase);


	switch (testcase) {
	case 0: /* output status */
		ep_pcie_show_status(dev);
		break;
	case 1: /* output PHY and PARF registers */
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_PHY) |
				BIT(EP_PCIE_RES_PARF), true);
		break;
	case 2: /* output core registers */
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_DM_CORE), false);
		break;
	case 3: /* output MMIO registers */
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_MMIO), false);
		break;
	case 4: /* output ELBI registers */
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_ELBI), false);
		break;
	case 5: /* output MSI registers */
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_MSI), false);
		break;
	case 6: /* turn on link */
		ep_pcie_enable_endpoint(phandle, EP_PCIE_OPT_ALL);
		break;
	case 7: /* enumeration */
		ep_pcie_enable_endpoint(phandle, EP_PCIE_OPT_ENUM);
		break;
	case 8: /* turn off link */
		ep_pcie_disable_endpoint(phandle);
		break;
	case 9: /* check MSI */
		ep_pcie_get_msi_config(phandle, &msi_cfg, vf_id);
		break;
	case 10: /* trigger MSI */
		ep_pcie_trigger_msi(phandle, 0, vf_id);
		break;
	case 11: /* indicate the status of PCIe link */
		EP_PCIE_DBG_FS("\nPCIe: link status is %d\n\n",
			ep_pcie_get_linkstatus(phandle));
		break;
	case 12: /* configure outbound iATU */
		ep_pcie_config_outbound_iatu(phandle, entries, 2, vf_id);
		break;
	case 13: /* wake up the host */
		ep_pcie_wakeup_host(phandle, EP_PCIE_EVENT_PM_D3_HOT);
		break;
	case 14: /* Configure routing of doorbells of physical MHI*/
		ep_pcie_config_db_routing(phandle, chdb_cfg, erdb_cfg, vf_id);
		break;
	case 15: /* write D3 */
		EP_PCIE_DBG_FS("\nPCIe Testcase %d: write D3 to EP\n\n",
			testcase);
		EP_PCIE_DBG_FS("\nPCIe: 0x44 of EP is 0x%x before change\n\n",
			readl_relaxed(dev->dm_core + 0x44));
		ep_pcie_write_mask(dev->dm_core + 0x44, 0, 0x3);
		EP_PCIE_DBG_FS("\nPCIe: 0x44 of EP is 0x%x now\n\n",
			readl_relaxed(dev->dm_core + 0x44));
		break;
	case 16: /* write D0 */
		EP_PCIE_DBG_FS("\nPCIe Testcase %d: write D0 to EP\n\n",
			testcase);
		EP_PCIE_DBG_FS("\nPCIe: 0x44 of EP is 0x%x before change\n\n",
			readl_relaxed(dev->dm_core + 0x44));
		ep_pcie_write_mask(dev->dm_core + 0x44, 0x3, 0);
		EP_PCIE_DBG_FS("\nPCIe: 0x44 of EP is 0x%x now\n\n",
			readl_relaxed(dev->dm_core + 0x44));
		break;
	case 17: /* assert wake */
		EP_PCIE_DBG_FS("\nPCIe Testcase %d: assert wake\n\n",
			testcase);
		gpio_set_value(dev->gpio[EP_PCIE_GPIO_WAKE].num,
			dev->gpio[EP_PCIE_GPIO_WAKE].on);
		break;
	case 18: /* deassert wake */
		EP_PCIE_DBG_FS("\nPCIe Testcase %d: deassert wake\n\n",
			testcase);
		gpio_set_value(dev->gpio[EP_PCIE_GPIO_WAKE].num,
			1 - dev->gpio[EP_PCIE_GPIO_WAKE].on);
		break;
	case 19: /* output PERST# status */
		EP_PCIE_DBG_FS("\nPCIe: PERST# is %d\n\n",
			gpio_get_value(dev->gpio[EP_PCIE_GPIO_PERST].num));
		break;
	case 20: /* output WAKE# status */
		EP_PCIE_DBG_FS("\nPCIe: WAKE# is %d\n\n",
			gpio_get_value(dev->gpio[EP_PCIE_GPIO_WAKE].num));
		break;
	case 21: /* output core registers when D3 hot is set by host*/
		dev->dump_conf = true;
		break;
	case 22: /* do not output core registers when D3 hot is set by host*/
		dev->dump_conf = false;
		break;
	case 23: /* output edma registers */
		edma_dump();
		break;
	case 24: /* Dump clock CBCR registers */
		ep_pcie_clk_dump(dev);
		EP_PCIE_DBG_FS("\nPCIe Testcase %d: Clock CBCR reg info will be dumped in Dmesg",
			testcase);
		break;
	case 25: /* Dump ASPM stats */
		ep_pcie_aspm_stat(dev);
		break;
	case 26: /* positive eye sequence */
		ep_pcie_eom_eye_seq(dev, POSITIVE_SEQUENCE);
		EP_PCIE_DBG_FS("Run complete %d\n", testcase);
		break;
	case 27: /* negative eye sequence */
		ep_pcie_eom_eye_seq(dev, NEGATIVE_SEQUENCE);
		EP_PCIE_DBG_FS("Run complete %d\n", testcase);
		break;
	case 28:
		ep_pcie_check_link_state(dev);
		break;
	default:
		EP_PCIE_DBG_FS("PCIe: Invalid testcase: %d\n", testcase);
		break;
	}

	if (ret == 0)
		return count;
	else
		return -EFAULT;
}

const struct file_operations ep_pcie_cmd_debug_ops = {
	.write = ep_pcie_cmd_debug,
	.release = single_release,
	.read = seq_read,
	.open = ep_pcie_cmd_debug_open,
};


void ep_pcie_debugfs_init(struct ep_pcie_dev_t *ep_dev)
{
	dev = ep_dev;
	dent_ep_pcie = debugfs_create_dir("pcie-ep", 0);
	if (IS_ERR(dent_ep_pcie)) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: fail to create the folder for debug_fs\n",
			dev->rev);
		return;
	}

	dfile_case = debugfs_create_file("case", 0664,
					dent_ep_pcie, 0,
					&ep_pcie_cmd_debug_ops);
	if (!dfile_case || IS_ERR(dfile_case)) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: fail to create the file for case\n",
			dev->rev);
		goto case_error;
	}
	EP_PCIE_DBG2(dev,
		"PCIe V%d: debugfs is enabled\n",
		dev->rev);

	return;

case_error:
	debugfs_remove(dent_ep_pcie);

}

void ep_pcie_debugfs_exit(void)
{
	debugfs_remove(dfile_case);
	debugfs_remove(dent_ep_pcie);
}
