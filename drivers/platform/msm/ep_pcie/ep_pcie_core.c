// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */
/*
 * MSM PCIe endpoint core driver.
 */

#include <dt-bindings/regulator/qcom,rpmh-regulator-levels.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of_gpio.h>
#include <linux/clk/qcom.h>
#include <linux/reset.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/kdebug.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/interconnect.h>
#include <linux/pci_regs.h>
#include <linux/nvmem-consumer.h>
#include <linux/pinctrl/consumer.h>

#include "ep_pcie_com.h"
#include "ep_pcie_phy.h"
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#define PCIE_PHYSICAL_DEVICE			0
#define PCIE_MHI_STATUS(n)			((n) + 0x148)
#define TCSR_PERST_SEPARATION_ENABLE		0x270
#define TCSR_PCIE_RST_SEPARATION		0x3F8
#define TCSR_PCIE_PERST_EN			0x258
#define TCSR_HOT_RESET_EN			0x3e8

#define PCIE_ISSUE_WAKE				1
#define PCIE_MHI_FWD_STATUS_MIN			5000
#define PCIE_MHI_FWD_STATUS_MAX			5110
#define PCIE_MHI_FWD_COUNT			200
#define PCIE_L1SUB_AHB_TIMEOUT_MIN		100
#define PCIE_L1SUB_AHB_TIMEOUT_MAX		120

#define PCIE_DISCONNECT_REQ_REG			BIT(6)
#define PERST_RAW_RESET_STATUS			BIT(0)
#define LINK_STATUS_REG_SHIFT			16

#define LODWORD(addr)             (addr & 0xFFFFFFFF)
#define HIDWORD(addr)             ((addr >> 32) & 0xFFFFFFFF)

#define GEN_1_2_LTSSM_DETECT_TIMEOUT_MS	20 /* in msec */
#define GEN_3_ABOVE_LTSSM_DETECT_TIMEOUT_MS	100 /* in msec */

/* debug mask sys interface */
static int ep_pcie_debug_mask;
static int ep_pcie_debug_keep_resource;
static u32 ep_pcie_bar0_address;
static bool m2_enabled;
static u32 clkreq_irq;
static bool clkreq_irq_disable;

struct ep_pcie_dev_t ep_pcie_dev = {0};

static struct ep_pcie_vreg_info_t ep_pcie_vreg_info[EP_PCIE_MAX_VREG] = {
	{NULL, "vreg-1p2", 1200000, 1200000, 30000, true},
	{NULL, "vreg-0p9", 912000, 912000, 132000, true},
	{NULL, "vreg-cx", 0, 0, 0, false},
	{NULL, "vreg-mx", 0, 0, 0, false}
};

static struct ep_pcie_gpio_info_t ep_pcie_gpio_info[EP_PCIE_MAX_GPIO] = {
	{"perst-gpio",      0, 0, 0, 1},
	{"wake-gpio",       0, 1, 0, 1},
	{"clkreq-gpio",     0, 0, 0, 1},
	{"mdm2apstatus-gpio",    0, 1, 1, 0},
};

static struct ep_pcie_clk_info_t
	ep_pcie_clk_info[EP_PCIE_MAX_CLK] = {
	{NULL, "pcie_cfg_ahb_clk", 0, true},
	{NULL, "pcie_mstr_axi_clk", 0, true},
	{NULL, "pcie_slv_axi_clk", 0, true},
	{NULL, "pcie_aux_clk", 1000000, true},
	{NULL, "pcie_ldo", 0, true},
	{NULL, "pcie_sleep_clk", 0, false},
	{NULL, "pcie_slv_q2a_axi_clk", 0, false},
	{NULL, "pcie_pipe_clk_mux", 0, false},
	{NULL, "pcie_pipe_clk_ext_src", 0, false},
	{NULL, "pcie_0_ref_clk_src", 0, false},
	{NULL, "snoc_pcie_sf_south_qx_clk", 0, false},
	{NULL, "snoc_pcie_sf_center_qx_clk", 0, false},
	{NULL, "snoc_cnoc_pcie_qx_clk", 0, false},
	{NULL, "snoc_cnoc_gemnoc_pcie_south_qx_clk", 0, false},
	{NULL, "snoc_cnoc_gemnoc_pcie_qx_clk", 0, false},
	{NULL, "gemnoc_pcie_qx_clk", 0, false},
	{NULL, "gcc_pcie_0_phy_aux_clk", 0, false},
	{NULL, "pcie_ddrss_sf_tbu_clk", 0, false},
	{NULL, "pcie_aggre_noc_0_axi_clk", 0, false},
	{NULL, "gcc_cnoc_pcie_sf_axi_clk", 0, false},
	{NULL, "pcie_pipediv2_clk", 0, false},
	{NULL, "pcie_phy_refgen_clk", 0, false},
	{NULL, "pcie_phy_aux_clk", 0, false},
};

static struct ep_pcie_clk_info_t
	ep_pcie_pipe_clk_info[EP_PCIE_MAX_PIPE_CLK] = {
	{NULL, "pcie_pipe_clk", 62500000, true},
};

static struct ep_pcie_reset_info_t
	ep_pcie_reset_info[EP_PCIE_MAX_RESET] = {
	{NULL, "pcie_core_reset", false},
	{NULL, "pcie_phy_reset", false},
};

static const struct ep_pcie_res_info_t ep_pcie_res_info[EP_PCIE_MAX_RES] = {
	{"parf",	NULL, NULL},
	{"phy",		NULL, NULL},
	{"mmio",	NULL, NULL},
	{"msi",		NULL, NULL},
	{"msi_vf",	NULL, NULL},
	{"dm_core",	NULL, NULL},
	{"dm_core_vf",	NULL, NULL},
	{"elbi",	NULL, NULL},
	{"iatu",	NULL, NULL},
	{"edma",	NULL, NULL},
	{"tcsr_pcie_perst_en",	NULL, NULL},
	{"aoss_cc_reset", NULL, NULL},
	{"rumi", NULL, NULL},
};

static const struct ep_pcie_irq_info_t ep_pcie_irq_info[EP_PCIE_MAX_IRQ] = {
	{"int_pm_turnoff",	0},
	{"int_dstate_change",		0},
	{"int_l1sub_timeout",	0},
	{"int_link_up",	0},
	{"int_link_down",	0},
	{"int_bridge_flush_n",	0},
	{"int_bme",	0},
	{"int_global",	0},
};

static int ep_pcie_core_wakeup_host_internal(enum ep_pcie_event event);
static void ep_pcie_config_inbound_iatu(struct ep_pcie_dev_t *dev, bool is_vf);

/*
 * ep_pcie_clk_dump - Clock CBCR reg info will be dumped in Dmesg logs.
 * @dev: PCIe endpoint device structure.
 */
void ep_pcie_clk_dump(struct ep_pcie_dev_t *dev)
{
	struct ep_pcie_clk_info_t *info;
	int i;

	for (i = 0; i < EP_PCIE_MAX_CLK; i++) {
		info = &dev->clk[i];

		if (!info->hdl) {
			EP_PCIE_DBG(dev,
				"PCIe V%d:  handle of Clock %s is NULL\n",
				dev->rev, info->name);
		} else {
			qcom_clk_dump(info->hdl, NULL, 0);
		}
	}

	for (i = 0; i < EP_PCIE_MAX_PIPE_CLK; i++) {
		info = &dev->pipeclk[i];

		if (!info->hdl) {
			EP_PCIE_ERR(dev,
				"PCIe V%d:  handle of Pipe Clock %s is NULL\n",
				dev->rev, info->name);
		} else {
			qcom_clk_dump(info->hdl, NULL, 0);
		}
	}
}

int ep_pcie_get_debug_mask(void)
{
	return ep_pcie_debug_mask;
}

static int ep_pcie_find_capability(struct ep_pcie_dev_t *dev, u32 cap)
{
	u8 next_cap_ptr, cap_id;
	u16 reg;

	if (!(readl_relaxed(dev->dm_core + PCIE20_COMMAND_STATUS)
		& PCIE20_CMD_STS_CAP_LIST))
		return -EINVAL;


	reg = readl_relaxed(dev->dm_core + PCI_CAPABILITY_LIST);
	next_cap_ptr = (reg & 0x00FF);
	if (!next_cap_ptr)
		return 0;

	do  {
		reg = readl_relaxed(dev->dm_core + next_cap_ptr);
		cap_id = (reg & 0x00FF);
		if (cap_id > PCI_CAP_ID_MAX)
			return 0;

		if (cap_id == cap)
			return next_cap_ptr;

		next_cap_ptr = (reg & 0xFF00) >> 8;
	} while (cap_id < PCI_CAP_ID_MAX);

	return 0;
}

static int ep_pcie_find_ext_capability(struct ep_pcie_dev_t *dev, int cap)
{
	u32 header;
	int ttl;
	int pos = PCI_CFG_SPACE_SIZE;

	/* minimum 8 bytes per capability */
	ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	if (!(readl_relaxed(dev->dm_core + PCIE20_COMMAND_STATUS)
		& PCIE20_CMD_STS_CAP_LIST))
		return -EINVAL;

	while (ttl-- > 0) {
		header = readl_relaxed(dev->dm_core + pos);
		/*
		 * If we have no capabilities, this is indicated by cap ID,
		 * cap version and next pointer all being 0.
		 */
		if (header == 0)
			break;

		if (PCI_EXT_CAP_ID(header) == cap)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < PCI_CFG_SPACE_SIZE)
			break;
	}

	return 0;
}

static bool ep_pcie_confirm_linkup(struct ep_pcie_dev_t *dev,
				bool check_sw_stts)
{
	u32 val;

	if (check_sw_stts && (dev->link_status != EP_PCIE_LINK_ENABLED)) {
		EP_PCIE_DBG(dev, "PCIe V%d: The link is not enabled\n",
			dev->rev);
		return false;
	}

	val = readl_relaxed(dev->dm_core);
	EP_PCIE_DBG(dev, "PCIe V%d: device ID and vendor ID are 0x%x\n",
		dev->rev, val);
	if (val == EP_PCIE_LINK_DOWN) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: The link is not really up; device ID and vendor ID are 0x%x\n",
			dev->rev, val);
		return false;
	}

	return true;
}

static int ep_pcie_reset_init(struct ep_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct ep_pcie_reset_info_t *reset_info;
	ktime_t timeout;

	for (i = 0; i < EP_PCIE_MAX_RESET; i++) {
		reset_info = &dev->reset[i];
		if (!reset_info->hdl)
			continue;

		rc = reset_control_assert(reset_info->hdl);
		if (rc) {
			if (!reset_info->required) {
				EP_PCIE_ERR(dev,
				"PCIe V%d: Optional reset: %s assert failed\n",
					dev->rev, reset_info->name);
				continue;
			} else {
				EP_PCIE_ERR(dev,
				"PCIe V%d: failed to assert reset for %s\n",
					dev->rev, reset_info->name);
				return rc;
			}
		} else {
			EP_PCIE_DBG(dev,
			"PCIe V%d: successfully asserted reset for %s\n",
				dev->rev, reset_info->name);
		}
		EP_PCIE_DBG(dev, "After Reset assert %s\n",
						reset_info->name);

		/* add a 1ms delay to ensure the reset is asserted */
		timeout = ktime_add_us(ktime_get(), 1000);
		while (1) {
			if (ktime_after(ktime_get(), timeout))
				break;
			udelay(1);
			cpu_relax();
		}

		rc = reset_control_deassert(reset_info->hdl);
		if (rc) {
			if (!reset_info->required) {
				EP_PCIE_ERR(dev,
				"PCIe V%d: Optional reset: %s deassert failed\n",
					dev->rev, reset_info->name);
				continue;
			} else {
				EP_PCIE_ERR(dev,
				"PCIe V%d: failed to deassert reset for %s\n",
					dev->rev, reset_info->name);
				return rc;
			}
		} else {
			EP_PCIE_DBG(dev,
			"PCIe V%d: successfully deasserted reset for %s\n",
				dev->rev, reset_info->name);
		}
		EP_PCIE_DBG(dev, "After Reset de-assert %s\n",
						reset_info->name);
	}
	return 0;
}

static int ep_pcie_gpio_init(struct ep_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct ep_pcie_gpio_info_t *info;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	for (i = 0; i < EP_PCIE_GPIO_CLKREQ; i++) {
		info = &dev->gpio[i];

		if (!info->num) {
			EP_PCIE_DBG(dev,
				"PCIe V%d: gpio %s does not exist\n",
				dev->rev, info->name);
			continue;
		}

		rc = gpio_request(info->num, info->name);
		if (rc) {
			EP_PCIE_ERR(dev, "PCIe V%d:  can't get gpio %s; %d\n",
				dev->rev, info->name, rc);
			break;
		}

		if (info->out)
			rc = gpio_direction_output(info->num, info->init);
		else
			rc = gpio_direction_input(info->num);
		if (rc) {
			EP_PCIE_ERR(dev,
				"PCIe V%d:  can't set direction for GPIO %s:%d\n",
				dev->rev, info->name, rc);
			gpio_free(info->num);
			break;
		}
	}

	if (rc)
		while (i--)
			gpio_free(dev->gpio[i].num);

	return rc;
}

static void ep_pcie_gpio_deinit(struct ep_pcie_dev_t *dev)
{
	int i;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	for (i = 0; i < EP_PCIE_MAX_GPIO; i++)
		gpio_free(dev->gpio[i].num);
}

static int ep_pcie_vreg_init(struct ep_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct regulator *vreg;
	struct ep_pcie_vreg_info_t *info;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	for (i = 0; i < EP_PCIE_MAX_VREG; i++) {
		info = &dev->vreg[i];
		vreg = info->hdl;

		if (!vreg) {
			EP_PCIE_ERR(dev,
				"PCIe V%d:  handle of Vreg %s is NULL\n",
				dev->rev, info->name);
			rc = -EINVAL;
			break;
		}

		EP_PCIE_DBG(dev, "PCIe V%d: Vreg %s is being enabled\n",
			dev->rev, info->name);
		if (info->max_v) {
			rc = regulator_set_voltage(vreg,
						   info->min_v, info->max_v);
			if (rc) {
				EP_PCIE_ERR(dev,
					"PCIe V%d:  can't set voltage for %s: %d\n",
					dev->rev, info->name, rc);
				break;
			}
		}

		if (info->opt_mode) {
			rc = regulator_set_load(vreg, info->opt_mode);
			if (rc < 0) {
				EP_PCIE_ERR(dev,
					"PCIe V%d:  can't set mode for %s: %d\n",
					dev->rev, info->name, rc);
				break;
			}
		}

		rc = regulator_enable(vreg);
		if (rc) {
			EP_PCIE_ERR(dev,
				"PCIe V%d:  can't enable regulator %s: %d\n",
				dev->rev, info->name, rc);
			break;
		}
	}

	if (rc)
		while (i--) {
			struct regulator *hdl = dev->vreg[i].hdl;

			if (hdl) {
				regulator_disable(hdl);
				if (!strcmp(dev->vreg[i].name, "vreg-mx")) {
					EP_PCIE_DBG(dev, "PCIe V%d: Removing vote for %s.\n",
						dev->rev, dev->vreg[i].name);
					regulator_set_voltage(hdl, RPMH_REGULATOR_LEVEL_RETENTION,
						RPMH_REGULATOR_LEVEL_MAX);
				}
			}
		}

	return rc;
}

static void ep_pcie_vreg_deinit(struct ep_pcie_dev_t *dev)
{
	int i;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	for (i = EP_PCIE_MAX_VREG - 1; i >= 0; i--) {
		if (dev->vreg[i].hdl) {
			EP_PCIE_DBG(dev, "Vreg %s is being disabled\n",
				dev->vreg[i].name);
			regulator_disable(dev->vreg[i].hdl);
			if (!strcmp(dev->vreg[i].name, "vreg-mx")) {
				EP_PCIE_DBG(dev, "PCIe V%d: Removing vote for %s.\n",
					 dev->rev, dev->vreg[i].name);
				regulator_set_voltage(dev->vreg[i].hdl,
					RPMH_REGULATOR_LEVEL_RETENTION, RPMH_REGULATOR_LEVEL_MAX);
			}
		}
	}
}

static int qcom_ep_pcie_icc_bw_update(struct ep_pcie_dev_t *dev, u16 speed, u16 width)
{
	u32 bw, icc_bw;
	int rc;

	if (dev->icc_path && !IS_ERR(dev->icc_path)) {

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

		icc_bw = width * bw;
		/* Speed == 0 implies to vote for '0' bandwidth. */
		if (speed == 0)
			rc = icc_set_bw(dev->icc_path, 0, 0);
		else
			rc = icc_set_bw(dev->icc_path, icc_bw, icc_bw);

		if (rc) {
			EP_PCIE_ERR(dev, "PCIe V%d: fail to set bus bandwidth:%d\n", dev->rev, rc);
			return rc;
		}

		EP_PCIE_DBG(dev, "PCIe V%d: set bus Avg and Peak bandwidth:%d\n", dev->rev, icc_bw);
	}

	return 0;
}

static int ep_pcie_clk_init(struct ep_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct ep_pcie_clk_info_t *info;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	rc = regulator_enable(dev->gdsc);
	if (rc) {
		EP_PCIE_ERR(dev, "PCIe V%d: fail to enable GDSC for %s\n",
			dev->rev, dev->pdev->name);
		return rc;
	}

	if (dev->gdsc_phy) {
		rc = regulator_enable(dev->gdsc_phy);
		if (rc) {
			EP_PCIE_ERR(dev, "PCIe V%d: fail to enable GDSC_PHY for %s\n",
					dev->rev, dev->pdev->name);
			regulator_disable(dev->gdsc);
			return rc;
		}
	}

	/* switch pipe clock source after gdsc is turned on */
	if (dev->pipe_clk_mux && dev->pipe_clk_ext_src)
		clk_set_parent(dev->pipe_clk_mux, dev->pipe_clk_ext_src);

	rc = qcom_ep_pcie_icc_bw_update(dev, PCI_EXP_LNKSTA_CLS_2_5GB, PCI_EXP_LNKSTA_NLW_X1);
	if (rc) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: fail to set bus bandwidth:%d\n",
			dev->rev, rc);
		return rc;
	}

	for (i = 0; i < EP_PCIE_MAX_CLK; i++) {
		info = &dev->clk[i];

		if (!info->hdl) {
			EP_PCIE_DBG(dev,
				"PCIe V%d:  handle of Clock %s is NULL\n",
				dev->rev, info->name);
			continue;
		}

		if (info->freq) {
			rc = clk_set_rate(info->hdl, info->freq);
			if (rc) {
				EP_PCIE_ERR(dev,
					"PCIe V%d: can't set rate for clk %s: %d\n",
					dev->rev, info->name, rc);
				break;
			}
			EP_PCIE_DBG(dev,
				"PCIe V%d: set rate %d for clk %s\n",
				dev->rev, info->freq, info->name);
		}

		rc = clk_prepare_enable(info->hdl);

		if (rc)
			EP_PCIE_ERR(dev, "PCIe V%d:  failed to enable clk %s\n",
				dev->rev, info->name);
		else
			EP_PCIE_DBG(dev, "PCIe V%d:  enable clk %s\n",
				dev->rev, info->name);
	}

	if (rc) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: disable clocks for error handling\n",
			dev->rev);
		while (i--) {
			struct clk *hdl = dev->clk[i].hdl;

			if (hdl)
				clk_disable_unprepare(hdl);
		}

		/* switch pipe clock mux to xo before turning off gdsc */
		if (dev->pipe_clk_mux && dev->ref_clk_src)
			clk_set_parent(dev->pipe_clk_mux, dev->ref_clk_src);

		if (dev->gdsc_phy)
			regulator_disable(dev->gdsc_phy);
		regulator_disable(dev->gdsc);
	}

	return rc;
}

static void ep_pcie_clk_deinit(struct ep_pcie_dev_t *dev)
{
	int i, rc;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	for (i = EP_PCIE_MAX_CLK - 1; i >= 0; i--)
		if (dev->clk[i].hdl)
			clk_disable_unprepare(dev->clk[i].hdl);

	rc = qcom_ep_pcie_icc_bw_update(dev, 0, 0);
	if (rc) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: relinquish bus bandwidth returns %d\n",
			dev->rev, rc);
	}

	if (!m2_enabled) {
		/* switch pipe clock mux to xo before turning off gdsc */
		if (dev->pipe_clk_mux && dev->ref_clk_src)
			clk_set_parent(dev->pipe_clk_mux, dev->ref_clk_src);

		if (dev->gdsc_phy)
			regulator_disable(dev->gdsc_phy);
		regulator_disable(dev->gdsc);
	}
}

static int ep_pcie_pipe_clk_init(struct ep_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct ep_pcie_clk_info_t *info;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	if (dev->rumi)
		return 0;

	for (i = 0; i < EP_PCIE_MAX_PIPE_CLK; i++) {
		info = &dev->pipeclk[i];

		if (!info->hdl) {
			EP_PCIE_ERR(dev,
				"PCIe V%d:  handle of Pipe Clock %s is NULL\n",
				dev->rev, info->name);
			rc = -EINVAL;
			break;
		}

		if (info->freq) {
			rc = clk_set_rate(info->hdl, info->freq);
			if (rc) {
				EP_PCIE_ERR(dev,
					"PCIe V%d: can't set rate for clk %s: %d\n",
					dev->rev, info->name, rc);
				break;
			}
			EP_PCIE_DBG(dev,
				"PCIe V%d: set rate for clk %s\n",
				dev->rev, info->name);
		}

		rc = clk_prepare_enable(info->hdl);

		if (rc)
			EP_PCIE_ERR(dev, "PCIe V%d: failed to enable clk %s\n",
				dev->rev, info->name);
		else
			EP_PCIE_DBG(dev, "PCIe V%d: enabled pipe clk %s\n",
				dev->rev, info->name);
	}

	if (rc) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: disable pipe clocks for error handling\n",
			dev->rev);
		while (i--)
			if (dev->pipeclk[i].hdl)
				clk_disable_unprepare(dev->pipeclk[i].hdl);
	}

	return rc;
}

static void ep_pcie_pipe_clk_deinit(struct ep_pcie_dev_t *dev)
{
	int i;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	if (dev->rumi)
		return;

	for (i = 0; i < EP_PCIE_MAX_PIPE_CLK; i++)
		if (dev->pipeclk[i].hdl)
			clk_disable_unprepare(
				dev->pipeclk[i].hdl);
}

#if IS_ENABLED(CONFIG_L1SS_RESOURCES_HANDLING)
int ep_pcie_l1ss_resources_init(struct ep_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct ep_pcie_clk_info_t *clki;

	/* Turn on LDOs */
	ep_pcie_vreg_init(dev);

	/* Set bus bandwidth */
	if (dev->icc_path) {
		rc = qcom_ep_pcie_icc_bw_update(dev, dev->current_link_speed,
							dev->current_link_width);
		if (rc) {
			EP_PCIE_ERR(dev,
				"PCIe V%d: fail to set bus bandwidth:%d\n", dev->rev, rc);
			return rc;
		}

		EP_PCIE_DBG(dev, "PCIe V%d: set bus bandwidth successful\n", dev->rev);
	}

	/* Turn on the clocks */
	for (i = 0; i < EP_PCIE_MAX_CLK; i++) {
		clki = &dev->clk[i];

		if (clki->hdl) {
			EP_PCIE_DBG(dev, "PCIe V%d %s enable\n", dev->rev, clki->name);
			clk_prepare_enable(clki->hdl);
		}
	}

	/* Powering up the PHY interface */
	ep_pcie_write_reg(dev->phy, PCIE_PHY_POWER_DOWN_CONTROL, 0x1);

	EP_PCIE_DBG(dev, "PCIe V%d:Resources turned on M2/L1SS path\n", dev->rev);
	return rc;
}

int ep_pcie_l1ss_resources_deinit(struct ep_pcie_dev_t *dev)
{
	int i, rc = 0;
	struct ep_pcie_clk_info_t *clki;

	/* Power down the PHY interface */
	ep_pcie_write_reg(dev->phy, PCIE_PHY_POWER_DOWN_CONTROL, 0x0);

	/* Turn off the clocks */
	for (i = EP_PCIE_MAX_CLK - 1; i >= 0; i--) {
		clki = &dev->clk[i];

		if (clki->hdl) {
			EP_PCIE_DBG(dev, "PCIe V%d %s disable\n", dev->rev, clki->name);
			clk_disable_unprepare(clki->hdl);
		}
	}

	/* Relinquish bus bandwidth */
	if (dev->icc_path) {
		rc = qcom_ep_pcie_icc_bw_update(dev, 0, 0);
		if (rc) {
			EP_PCIE_ERR(dev,
					"PCIe V%d: Relinquish bus bandwidth error code %d\n",
					dev->rev, rc);
			return rc;
		}
	}

	/* Turn off the LDOs */
	ep_pcie_vreg_deinit(dev);

	EP_PCIE_DBG(dev, "PCIe V%d: Resources turned off M2/L1SS path\n", dev->rev);
	return rc;
}
#endif

static void ep_pcie_msix_init(struct ep_pcie_dev_t *dev)
{
	int ret;

	/* MSI-X capable */
	ret = ep_pcie_find_capability(dev, PCI_CAP_ID_MSIX);
	if (ret > 0) {
		/*
		 * SNSP controller uses BAR0 by default for MSI-X. Update
		 * this default behavior to use BAR0 for MHI MMIO access.
		 * Value 0x1DFB indicates TARGET_MAP_PF is now enabled for
		 * different bar decoding (0x1DFB indicates BAR 2 Decoding)
		 */
		ep_pcie_write_reg(dev->dm_core, PCIE20_TRGT_MAP_CTRL_OFF,
				0x00001DFB);
		dev->msix_cap = ret;
		EP_PCIE_DBG(dev, "PCIe V%d: MSI-X capable\n", dev->rev);
	}
}

static void ep_pcie_bar_init(struct ep_pcie_dev_t *dev)
{
	struct resource *res = dev->res[EP_PCIE_RES_MMIO].resource;
	u32 mask = resource_size(res);
	u32 msix_mask = 0x7FFF; //32KB size
	u32 properties = 0x4; /* 64 bit Non-prefetchable memory */

	EP_PCIE_DBG(dev, "PCIe V%d: BAR mask to program is 0x%x\n",
			dev->rev, mask);

	/* Configure BAR mask via CS2 */
	ep_pcie_write_mask(dev->elbi + PCIE20_ELBI_CS2_ENABLE, 0, BIT(0));

	/* Set the BAR number 0 and enable 4 KB */
	ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0, mask - 1);

	ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0 + 0x4, 0);

	/* enable BAR2 with BAR size 8K for MSI-X */
	if (dev->msix_cap)
		ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0 + 0x8, msix_mask);
	else
		ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0 + 0x8, 0);

	ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0 + 0xc, 0);
	ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0 + 0x10, 0);
	ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0 + 0x14, 0);

	ep_pcie_write_mask(dev->elbi + PCIE20_ELBI_CS2_ENABLE, BIT(0), 0);

	/* Configure BAR0 type and MSI-X BIR value via CS */
	ep_pcie_write_mask(dev->dm_core + PCIE20_MISC_CONTROL_1, 0, BIT(0));

	ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0, properties);

	if (dev->msix_cap) {
		ep_pcie_write_reg(dev->dm_core, PCIE20_BAR0 + 0x8, properties);

		/* Set the BIR value 2 for MSIX table and PBA table via CS2 */
		ep_pcie_write_mask(dev->elbi + PCIE20_ELBI_CS2_ENABLE, 0,
				BIT(0));
		writel_relaxed(0x2,
				dev->dm_core + PCIE20_MSIX_TABLE_OFFSET_REG);
		writel_relaxed(0x00004002,
				dev->dm_core + PCIE20_MSIX_PBA_OFFSET_REG);
		ep_pcie_write_mask(dev->elbi + PCIE20_ELBI_CS2_ENABLE, BIT(0),
				0);
	}

	ep_pcie_write_mask(dev->dm_core + PCIE20_MISC_CONTROL_1, BIT(0), 0);
}

static void ep_pcie_config_mmio(struct ep_pcie_dev_t *dev)
{
	u32 mhi_status;
	void __iomem *mhi_status_addr;

	EP_PCIE_DBG(dev,
		"Initial version of MMIO is:0x%x\n",
		readl_relaxed(dev->mmio + PCIE20_MHIVER));

	if (dev->config_mmio_init) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: MMIO already initialized, return\n",
				dev->rev);
		return;
	}

	mhi_status_addr = PCIE_MHI_STATUS(dev->mmio);
	mhi_status = readl_relaxed(mhi_status_addr);
	if (mhi_status & BIT(2)) {
		EP_PCIE_DBG(dev,
			"MHISYS error is set:%d, proceed to MHI\n",
			mhi_status);
		return;
	}

	ep_pcie_write_reg(dev->mmio, PCIE20_MHICFG, 0x02800880);
	ep_pcie_write_reg(dev->mmio, PCIE20_BHI_EXECENV, 0x2);
	ep_pcie_write_reg(dev->mmio, PCIE20_MHICTRL, 0x0);
	ep_pcie_write_reg(dev->mmio, PCIE20_MHISTATUS, 0x0);
	ep_pcie_write_reg(dev->mmio, PCIE20_MHIVER, 0x1000000);
	ep_pcie_write_reg(dev->mmio, PCIE20_BHI_VERSION_LOWER, 0x2);
	ep_pcie_write_reg(dev->mmio, PCIE20_BHI_VERSION_UPPER, 0x1);

	dev->config_mmio_init = true;
}

static void ep_pcie_sriov_init(struct ep_pcie_dev_t *dev)
{
	void __iomem *dbi = ep_pcie_dev.dm_core;
	u32 reg;

	if (ep_pcie_dev.override_disable_sriov)
		return;

	ep_pcie_dev.sriov_cap = ep_pcie_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV);
	if (ep_pcie_dev.sriov_cap) {
		reg = readl_relaxed
			(dbi + ep_pcie_dev.sriov_cap + PCIE20_TOTAL_VFS_INITIAL_VFS_REG);
		ep_pcie_dev.num_vfs = (reg & 0xFFFF0000) >> 16;
		EP_PCIE_INFO(&ep_pcie_dev,
				"PCIe V%d: SR-IOV capability is present\n", ep_pcie_dev.rev);
		EP_PCIE_INFO(&ep_pcie_dev, "PCIe V%d: Number of VFs: %d, SR-IOV mask: 0x%lx\n",
				ep_pcie_dev.rev, ep_pcie_dev.num_vfs, ep_pcie_dev.sriov_mask);
	}
}

static void ep_pcie_core_init(struct ep_pcie_dev_t *dev, bool configured)
{
	uint32_t val = 0;
	struct resource *dbi = dev->res[EP_PCIE_RES_DM_CORE].resource;
	struct resource *dbi_vf = dev->res[EP_PCIE_RES_DM_VF_CORE].resource;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);
	EP_PCIE_DBG(dev,
		"PCIe V%d: WRITING TO BDF TO SID\n",
			dev->rev);
	/* PARF_BDF_TO_SID disable */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_BDF_TO_SID_CFG,
			0, BIT(0));

	EP_PCIE_DBG(dev,
		"PCIe V%d: FINISHED WRITING BDF TO SID\n",
			dev->rev);
	/* enable debug IRQ */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_DEBUG_INT_EN,
			0, BIT(3) | BIT(2) | BIT(1));
	/* Reconnect AXI master port */
	val = readl_relaxed(dev->parf + PCIE20_PARF_BUS_DISCONNECT_STATUS);
	if (val & BIT(0)) {
		EP_PCIE_DBG(dev,
		"PCIe V%d: AXI Master port was disconnected, reconnecting...\n",
			dev->rev);
		ep_pcie_write_mask(dev->parf + PCIE20_PARF_BUS_DISCONNECT_CTRL,
								0, BIT(0));
	}

	/* Update offset to AXI address for Host initiated SOC reset */
	if (dev->mhi_soc_reset_en) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: Updating SOC reset offset with val:0x%x\n",
				dev->rev, dev->mhi_soc_reset_offset);
		ep_pcie_write_reg(dev->parf, PCIE20_BUS_DISCONNECT_STATUS,
				dev->mhi_soc_reset_offset);
		val = readl_relaxed(dev->parf +
					PCIE20_BUS_DISCONNECT_STATUS);
		EP_PCIE_DBG(dev,
			"PCIe V%d:SOC reset offset val:0x%x\n", dev->rev, val);
	}

	if (!configured) {
		/* Configure PCIe to endpoint mode */
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_DEVICE_TYPE, 0x0);

		/* adjust DBI base address */
		if (dev->phy_rev < 6) {
			if (dev->dbi_base_reg)
				writel_relaxed(0x3FFFE000,
					dev->parf + dev->dbi_base_reg);
			else
				writel_relaxed(0x3FFFE000,
					dev->parf + PCIE20_PARF_DBI_BASE_ADDR);
		}

		/* Configure PCIe core to support 1GB aperture */
		if (dev->slv_space_reg)
			ep_pcie_write_reg(dev->parf, dev->slv_space_reg,
				0x40000000);
		else
			ep_pcie_write_reg(dev->parf,
				PCIE20_PARF_SLV_ADDR_SPACE_SIZE, 0x40000000);

		EP_PCIE_DBG2(dev, "PCIe V%d: Clear disconn_req after D3_COLD\n",
			     dev->rev);

		if (!dev->tcsr_not_supported)
			ep_pcie_write_reg_field(dev->tcsr_perst_en,
					ep_pcie_dev.tcsr_reset_separation_offset,
					ep_pcie_dev.pcie_disconnect_req_reg_mask, 0);
	}

	if (!dev->enumerated) {
		EP_PCIE_DBG2(dev, "PCIe V%d: Clear L23 READY after enumeration\n", dev->rev);
		ep_pcie_write_reg_field(dev->parf, PCIE20_PARF_PM_CTRL, BIT(2), 0);
	}

	if (dev->active_config) {
		u32 dbi_lo = dbi->start;

		ep_pcie_write_reg(dev->parf + PCIE20_PARF_SLV_ADDR_MSB_CTRL,
					0, BIT(0));
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_SLV_ADDR_SPACE_SIZE_HI,
					0x200);
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_SLV_ADDR_SPACE_SIZE,
					0x0);
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_DBI_BASE_ADDR_HI,
					0x100);
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_DBI_BASE_ADDR,
					dbi_lo);

		EP_PCIE_DBG(dev,
			"PCIe V%d: DBI base:0x%x\n", dev->rev,
			readl_relaxed(dev->parf + PCIE20_PARF_DBI_BASE_ADDR));

		if (dbi_vf) {
			u32 dbi_vf_lo = dbi_vf->start;

			/*
			 * Configure the base address for VF DBI to activate access
			 * to VF DBI space
			 */
			ep_pcie_write_reg(dev->parf, PCIE20_PARF_DBI_VF_BASE_ADDR_HI,
						0x100);
			ep_pcie_write_reg(dev->parf, PCIE20_PARF_DBI_VF_BASE_ADDR,
						dbi_vf_lo);
			EP_PCIE_DBG(dev,
				"PCIe V%d: DBI VF base:0x%x\n", dev->rev,
				readl_relaxed(dev->parf + PCIE20_PARF_DBI_VF_BASE_ADDR));
		}

		if (dev->phy_rev >= 6) {
			struct resource *atu =
					dev->res[EP_PCIE_RES_IATU].resource;
			u32 atu_lo = atu->start;

			EP_PCIE_DBG(dev,
				"PCIe V%d: configure MSB of ATU base for flipping and LSB as 0x%x\n",
				dev->rev, atu_lo);
			ep_pcie_write_reg(dev->parf,
					PCIE20_PARF_ATU_BASE_ADDR_HI, 0x100);
			ep_pcie_write_reg(dev->parf, PCIE20_PARF_ATU_BASE_ADDR,
					atu_lo);
			EP_PCIE_DBG(dev,
				"PCIe V%d: LSB of ATU base:0x%x\n",
				dev->rev, readl_relaxed(dev->parf
						+ PCIE20_PARF_ATU_BASE_ADDR));
			if (dev->pcie_edma) {
				struct resource *edma =
					dev->res[EP_PCIE_RES_EDMA].resource;
				u32 edma_lo = edma->start;

				ep_pcie_write_reg(dev->parf,
					PCIE20_PARF_EDMA_BASE_ADDR_HI, 0x100);
				EP_PCIE_DBG(dev,
					"PCIe V%d: EDMA base HI :0x%x\n",
					dev->rev, readl_relaxed(dev->parf +
					PCIE20_PARF_EDMA_BASE_ADDR_HI));

				ep_pcie_write_reg(dev->parf,
					PCIE20_PARF_EDMA_BASE_ADDR, edma_lo);
				EP_PCIE_DBG(dev,
					"PCIe V%d: EDMA base:0x%x\n", dev->rev,
						readl_relaxed(dev->parf +
						PCIE20_PARF_EDMA_BASE_ADDR));
			}
		}
	}

	/* Read halts write */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_AXI_MSTR_RD_HALT_NO_WRITES,
			0, BIT(0));

	/* Write after write halt */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT,
			0, BIT(31));

	/* Dont ignore BME & block outbound traffic when BME is de-asserted */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_LINK_DOWN_ECAM_BLOCK,
			BIT(6), 0);

	/* Q2A flush disable */
	writel_relaxed(0, dev->parf + PCIE20_PARF_Q2A_FLUSH);

	/* Disable the DBI Wakeup */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_SYS_CTRL, BIT(11), 0);

	/* Disable the debouncers */
	ep_pcie_write_reg(dev->parf, PCIE20_PARF_DB_CTRL, 0x73);

	/* Disable core clock CGC */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_SYS_CTRL, 0, BIT(6));

	/* Set AUX power to be on */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_SYS_CTRL, 0, BIT(4));

	/* Request to exit from L1SS for MSI and LTR MSG */
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_CFG_BITS, 0, BIT(1));

	EP_PCIE_DBG(dev,
		"Initial: CLASS_CODE_REVISION_ID:0x%x; HDR_TYPE:0x%x; LINK_CAPABILITIES:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_CLASS_CODE_REVISION_ID),
		readl_relaxed(dev->dm_core + PCIE20_BIST_HDR_TYPE),
		readl_relaxed(dev->dm_core + PCIE20_LINK_CAPABILITIES));

	/* Configure BAR2 usage for MSI-X */
	ep_pcie_msix_init(dev);

	if (!configured) {
		int pos;

		/* Enable CS for RO(CS) register writes */
		ep_pcie_write_mask(dev->dm_core + PCIE20_MISC_CONTROL_1, 0,
			BIT(0));

		/* Set Vendor ID and Device ID */
		if (ep_pcie_dev.device_id != 0xFFFF)
			ep_pcie_write_reg_field(dev->dm_core,
						PCIE20_DEVICE_ID_VENDOR_ID,
						PCIE20_MASK_DEVICE_ID,
						ep_pcie_dev.device_id);
		if (ep_pcie_dev.vendor_id != 0xFFFF)
			ep_pcie_write_reg_field(dev->dm_core,
						PCIE20_DEVICE_ID_VENDOR_ID,
						PCIE20_MASK_VENDOR_ID,
						ep_pcie_dev.vendor_id);
		/* Set class code and revision ID */
		ep_pcie_write_reg(dev->dm_core, PCIE20_CLASS_CODE_REVISION_ID,
			0xff000000);

		/* Set header type */
		ep_pcie_write_reg(dev->dm_core, PCIE20_BIST_HDR_TYPE, 0x10);

		/* Set Subsystem ID and Subsystem Vendor ID */
		if (ep_pcie_dev.subsystem_id)
			ep_pcie_write_reg(dev->dm_core, PCIE20_SUBSYSTEM,
					ep_pcie_dev.subsystem_id);

		/* Set the PMC Register - to support PME in D0/D3hot/D3cold */
		ep_pcie_write_mask(dev->dm_core + PCIE20_CAP_ID_NXT_PTR, 0,
						BIT(31)|BIT(30)|BIT(27));

		/* Set the Endpoint L0s Acceptable Latency to 1us (max) */
		ep_pcie_write_reg_field(dev->dm_core,
			PCIE20_DEVICE_CAPABILITIES,
			PCIE20_MASK_EP_L0S_ACCPT_LATENCY, 0x7);

		/* Set the Endpoint L1 Acceptable Latency to 2 us (max) */
		ep_pcie_write_reg_field(dev->dm_core,
			PCIE20_DEVICE_CAPABILITIES,
			PCIE20_MASK_EP_L1_ACCPT_LATENCY, 0x7);

		/* Set the L0s Exit Latency to 2us-4us = 0x6 */
		ep_pcie_write_reg_field(dev->dm_core, PCIE20_LINK_CAPABILITIES,
			PCIE20_MASK_L1_EXIT_LATENCY, 0x6);

		/* Set the L1 Exit Latency to be 32us-64 us = 0x6 */
		ep_pcie_write_reg_field(dev->dm_core, PCIE20_LINK_CAPABILITIES,
			PCIE20_MASK_L0S_EXIT_LATENCY, 0x6);

		pos = ep_pcie_find_ext_capability(dev, PCI_EXT_CAP_ID_L1SS);
		if (pos > 0) {
			/* L1ss is supported */
			ep_pcie_write_mask(dev->dm_core + pos + PCI_L1SS_CAP, 0,
					   0x1f);
			EP_PCIE_DBG(dev,
			"After program: L1SUB_CAPABILITY:0x%x\n",
			readl_relaxed(dev->dm_core + pos + PCI_L1SS_CAP));
		}

		/* Set CLK_PM_EN which allows to configure the clock-power-man bit below for EP */
		ep_pcie_write_mask(dev->elbi + PCIE20_ELBI_SYS_CTRL, 1, BIT(7));

		/* Enable Clock Power Management */
		ep_pcie_write_reg_field(dev->dm_core, PCIE20_LINK_CAPABILITIES,
			PCIE20_MASK_CLOCK_POWER_MAN, 0x1);

		/* Disable CS for RO(CS) register writes */
		ep_pcie_write_mask(dev->dm_core + PCIE20_MISC_CONTROL_1, BIT(0),
			0);

		/* Configure link speed */
		ep_pcie_write_mask(dev->dm_core +
				PCIE20_LINK_CONTROL2_LINK_STATUS2,
				0xf, dev->link_speed);

		/* Set FTS value to match the PHY setting */
		ep_pcie_write_reg_field(dev->dm_core,
			PCIE20_ACK_F_ASPM_CTRL_REG,
			PCIE20_MASK_ACK_N_FTS, 0x80);

		EP_PCIE_DBG(dev,
			"After program: CLASS_CODE_REVISION_ID:0x%x; HDR_TYPE:0x%x; LINK_CAPABILITIES:0x%x; PARF_SYS_CTRL:0x%x\n",
			readl_relaxed(dev->dm_core +
				PCIE20_CLASS_CODE_REVISION_ID),
			readl_relaxed(dev->dm_core + PCIE20_BIST_HDR_TYPE),
			readl_relaxed(dev->dm_core + PCIE20_LINK_CAPABILITIES),
			readl_relaxed(dev->parf + PCIE20_PARF_SYS_CTRL));

		/* Configure BARs */
		ep_pcie_bar_init(dev);
	}

	/* Configure IRQ events */
	if (dev->aggregated_irq) {
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_INT_ALL_MASK, 0);
		ep_pcie_write_mask(dev->parf + PCIE20_PARF_INT_ALL_MASK, 0,
			BIT(EP_PCIE_INT_EVT_LINK_DOWN) |
			BIT(EP_PCIE_INT_EVT_BME) |
			BIT(EP_PCIE_INT_EVT_PM_TURNOFF) |
			BIT(EP_PCIE_INT_EVT_DSTATE_CHANGE) |
			BIT(EP_PCIE_INT_EVT_LINK_UP));
		if (!dev->mhi_a7_irq)
			ep_pcie_write_mask(dev->parf +
				PCIE20_PARF_INT_ALL_MASK, 0,
				BIT(EP_PCIE_INT_EVT_MHI_A7));
		if (dev->pcie_edma)
			ep_pcie_write_mask(dev->parf +
				PCIE20_PARF_INT_ALL_MASK, 0,
				BIT(EP_PCIE_INT_EVT_EDMA));
		if (dev->m2_autonomous)
			ep_pcie_write_mask(dev->parf +
				PCIE20_PARF_INT_ALL_MASK, 0,
				BIT(EP_PCIE_INT_EVT_L1SUB_TIMEOUT));

		EP_PCIE_DBG(dev, "PCIe V%d: PCIE20_PARF_INT_ALL_MASK:0x%x\n",
			dev->rev,
			readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_MASK));
	}

	if (dev->active_config) {
		ep_pcie_write_reg(dev->dm_core, PCIE20_AUX_CLK_FREQ_REG, dev->aux_clk_val);

		/* Disable SRIS_MODE */
		ep_pcie_write_mask(dev->parf + PCIE20_PARF_SRIS_MODE,
								BIT(0), 0);
	}

	/* Prevent L1ss wakeup after 100ms */
	ep_pcie_write_mask(dev->dm_core + PCIE20_GEN3_RELATED_OFF,
						BIT(0), 0);

	ep_pcie_sriov_init(dev);
	if (!configured) {
		ep_pcie_config_mmio(dev);
		ep_pcie_config_inbound_iatu(dev, PCIE_PHYSICAL_DEVICE);
	}

	/*
	 * Configure inbound IATU prior to BME IRQ of VF as host can quickly make a
	 * BAR access prior to configuration of inbound IATU causing transaction
	 * timeout on host side.
	 */
	if (dbi_vf)
		ep_pcie_config_inbound_iatu(dev, !PCIE_PHYSICAL_DEVICE);
}

static void ep_pcie_config_inbound_iatu(struct ep_pcie_dev_t *dev, bool is_vf)
{
	struct resource *mmio = dev->res[EP_PCIE_RES_MMIO].resource;
	u32 lower, limit, vf_lower, bar, size, vf_num = 0, vf_id = 1;
	u32 system_page_size = 1;

	lower = mmio->start;
	limit = mmio->end;
	size = resource_size(mmio);

	/*
	 * Overwrite PBL configured IATU FUNC_NUM_MATCH_EN bit in CTRL2
	 * register to Ensures that a successful Function Number TLP field
	 * comparison match.
	 */
	ep_pcie_write_reg(dev->iatu, PCIE20_IATU_I_CTRL2(0), BIT(31) | BIT(30) |  BIT(19));

	if (!is_vf) {
		bar = readl_relaxed(dev->dm_core + PCIE20_BAR0);
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_MHI_BASE_ADDR_LOWER, lower);
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_MHI_BASE_ADDR_UPPER, 0x0);

		lower = readl_relaxed(dev->parf + PCIE20_PARF_MHI_BASE_ADDR_LOWER);
	} else {
		/*
		 * In case of different BAR size alighments (eg, in ARM 64K
		 * BAR size was seen being used), hence Configure PARF_MHI_BASE_ADDR_LOWER_VF
		 * register based on system page size to facilitate proper BAR access.
		 */
		system_page_size = readl_relaxed(dev->dm_core + ep_pcie_dev.sriov_cap +
							PCIE20_SYSTEM_PAGE_SIZE_REG);
		vf_lower = lower + size;
		for (vf_id = 1; vf_id <= ep_pcie_dev.num_vfs; vf_id++) {
			vf_num = vf_id - 1;
			lower = (vf_lower + (system_page_size * size * vf_num));
			limit = lower + size;
			if (ep_pcie_dev.db_fwd_off_varied) {
				ep_pcie_write_reg(dev->parf,
					PCIE20_PARF_MHI_BASE_ADDR_VFn_LOWER(vf_num), lower);
				ep_pcie_write_reg(dev->parf,
					PCIE20_PARF_MHI_BASE_ADDR_VFn_UPPER(vf_num), 0);

				lower = readl_relaxed(dev->parf +
						PCIE20_PARF_MHI_BASE_ADDR_VFn_LOWER(vf_num));
			} else {
				ep_pcie_write_reg(dev->parf,
					PCIE20_PARF_MHI_BASE_ADDR_V1_VFn_LOWER(vf_num), lower);
				ep_pcie_write_reg(dev->parf,
					PCIE20_PARF_MHI_BASE_ADDR_V1_VFn_UPPER(vf_num), 0);

				lower = readl_relaxed(dev->parf +
						PCIE20_PARF_MHI_BASE_ADDR_V1_VFn_LOWER(vf_num));
			}
			EP_PCIE_DBG(dev, "MHI vf_id:%d lower:0x%x limit:0x%x\n",
						vf_id, lower, limit);
		}
		bar = readl_relaxed(dev->dm_core + ep_pcie_dev.sriov_cap + PCIE20_SRIOV_BAR(0));
	}

	/* Bar address is between 4-31 bits, masking 0-3 bits */
	bar &= ~(0xf);

	EP_PCIE_DBG(dev,
		"PCIe V%d: BAR for %s device is: 0x%x",
		dev->rev, is_vf ? "virtual":"physical", bar);

	if (dev->phy_rev >= 6) {
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_I_CTRL1(is_vf), 0x0);
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_I_UTAR(is_vf), 0x0);

		/*
		 * When set BIT(26) in CTRL2 register, Virtual Function BAR matching
		 * is used which allows all VFs in a PF which match a BAR
		 * to be matched with a single ATU region.
		 */
		if (is_vf) {
			ep_pcie_write_reg(dev->iatu, PCIE20_IATU_I_LTAR(is_vf), vf_lower);
			ep_pcie_write_reg(dev->iatu, PCIE20_IATU_I_CTRL2(is_vf),
						BIT(31) | BIT(26));
		} else {
			ep_pcie_write_reg(dev->iatu, PCIE20_IATU_I_LTAR(is_vf), lower);
			ep_pcie_write_reg(dev->iatu, PCIE20_IATU_I_CTRL2(is_vf),
						BIT(31) | BIT(30) | BIT(19));
		}
		EP_PCIE_DBG(dev,
			"PCIe V%d: Inbound iATU configuration\n", dev->rev);
		EP_PCIE_DBG(dev, "PCIE20_IATU_I_CTRL1(0):0x%x\n",
			readl_relaxed(dev->iatu + PCIE20_IATU_I_CTRL1(is_vf)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_I_LTAR(0):0x%x\n",
			readl_relaxed(dev->iatu + PCIE20_IATU_I_LTAR(is_vf)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_I_UTAR(0):0x%x\n",
			readl_relaxed(dev->iatu + PCIE20_IATU_I_UTAR(is_vf)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_I_CTRL2(0):0x%x\n",
			readl_relaxed(dev->iatu + PCIE20_IATU_I_CTRL2(is_vf)));
		return;
	}

	/* program inbound address translation using region 0 */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_VIEWPORT, 0x80000000);
	/* set region to mem type */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_CTRL1, 0x0);
	/* setup target address registers */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_LTAR, lower);
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_UTAR, 0x0);
	/* use BAR match mode for BAR0 and enable region 0 */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_CTRL2, 0xc0000000);

	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_VIEWPORT:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_VIEWPORT));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_CTRL1:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_CTRL1));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_LTAR:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_LTAR));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_UTAR:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_UTAR));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_CTRL2:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_CTRL2));
}

static void ep_pcie_config_outbound_iatu_entry(struct ep_pcie_dev_t *dev,
					u32 region, u32 vf_id, u32 lower, u32 upper,
					u64 limit, u32 tgt_lower, u32 tgt_upper)
{
	u32 limit_low = LODWORD(limit);
	u32 limit_high = HIDWORD(limit);

	region = region + (vf_id * 4);
	EP_PCIE_DBG(dev,
		"PCIe V%d: region:%d; lower:0x%x; limit:0x%llx; target_lower:0x%x; target_upper:0x%x vf_id:%d\n",
		dev->rev, region, lower, limit, tgt_lower, tgt_upper, vf_id);

	if (dev->phy_rev >= 6) {
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_CTRL1(region),
					0x0);
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_LBAR(region),
					lower);
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_UBAR(region),
					upper);
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_LAR(region),
					limit_low);
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_LTAR(region),
					tgt_lower);
		ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_UTAR(region),
					tgt_upper);

		/* if limit address exceeds 32 bit*/
		if (limit_high) {
			ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_CTRL1(region),
						PCIE20_IATU_O_INCREASE_REGION_SIZE);
			ep_pcie_write_reg(dev->iatu, PCIE20_IATU_O_ULAR(region),
						limit_high);
		}

		/* Set DMA Bypass bit for eDMA */
		if (dev->pcie_edma)
			ep_pcie_write_mask(dev->iatu +
				PCIE20_IATU_O_CTRL2(region), 0,
				BIT(31)|BIT(27));
		else
			ep_pcie_write_mask(dev->iatu +
				PCIE20_IATU_O_CTRL2(region), 0,
				BIT(31) | BIT(19));

		EP_PCIE_DBG(dev,
			"PCIe V%d: Outbound iATU configuration\n", dev->rev);
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_CTRL1:0x%x\n",
			readl_relaxed(dev->iatu
					+ PCIE20_IATU_O_CTRL1(region)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_LBAR:0x%x\n",
			readl_relaxed(dev->iatu +
					PCIE20_IATU_O_LBAR(region)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_UBAR:0x%x\n",
			readl_relaxed(dev->iatu +
					PCIE20_IATU_O_UBAR(region)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_LAR:0x%x\n",
			readl_relaxed(dev->iatu +
					PCIE20_IATU_O_LAR(region)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_LTAR:0x%x\n",
			readl_relaxed(dev->iatu +
					PCIE20_IATU_O_LTAR(region)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_UTAR:0x%x\n",
			readl_relaxed(dev->iatu +
					PCIE20_IATU_O_UTAR(region)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_CTRL2:0x%x\n",
			readl_relaxed(dev->iatu +
					PCIE20_IATU_O_CTRL2(region)));
		EP_PCIE_DBG(dev, "PCIE20_IATU_O_ULAR:0x%x\n",
			readl_relaxed(dev->iatu +
					PCIE20_IATU_O_ULAR(region)));
		return;
	}

	/* program outbound address translation using an input region */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_VIEWPORT, region);
	/* set region to mem type */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_CTRL1, 0x0);
	/* setup source address registers */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_LBAR, lower);
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_UBAR, upper);
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_LAR, limit);
	/* setup target address registers */
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_LTAR, tgt_lower);
	ep_pcie_write_reg(dev->dm_core, PCIE20_PLR_IATU_UTAR, tgt_upper);
	/* use DMA bypass mode and enable the region */
	ep_pcie_write_mask(dev->dm_core + PCIE20_PLR_IATU_CTRL2, 0,
				BIT(31) | BIT(27));

	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_VIEWPORT:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_VIEWPORT));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_CTRL1:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_CTRL1));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_LBAR:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_LBAR));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_UBAR:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_UBAR));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_LAR:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_LAR));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_LTAR:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_LTAR));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_UTAR:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_UTAR));
	EP_PCIE_DBG(dev, "PCIE20_PLR_IATU_CTRL2:0x%x\n",
		readl_relaxed(dev->dm_core + PCIE20_PLR_IATU_CTRL2));
}

static bool ep_pcie_notify_event(struct ep_pcie_dev_t *dev,
					enum ep_pcie_event event)
{
	if (dev->event_reg && dev->event_reg->callback &&
		(dev->event_reg->events & event)) {
		struct ep_pcie_notify *notify =	&dev->event_reg->notify;

		notify->event = event;
		notify->user = dev->event_reg->user;
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: Callback client for event 0x%x\n",
			dev->rev, event);
		dev->event_reg->callback(notify);
	} else {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: Client does not register for event 0x%x\n",
			dev->rev, event);
		return false;
	}
	return true;
}

static void ep_pcie_notify_vf_bme_event(struct ep_pcie_dev_t *dev,
					enum ep_pcie_event event,
					u32 vf_id)
{
	struct ep_pcie_notify *notify = &dev->event_reg->notify;

	notify->vf_id = vf_id;
	ep_pcie_notify_event(dev, event);
}

static int ep_pcie_get_resources(struct ep_pcie_dev_t *dev,
					struct platform_device *pdev)
{
	int i, len, cnt, ret = 0, size = 0;
	struct ep_pcie_vreg_info_t *vreg_info;
	struct ep_pcie_gpio_info_t *gpio_info;
	struct ep_pcie_clk_info_t  *clk_info;
	struct ep_pcie_reset_info_t *reset_info;
	struct resource *res;
	struct ep_pcie_res_info_t *res_info;
	struct ep_pcie_irq_info_t *irq_info;
	char prop_name[MAX_PROP_SIZE];
	const __be32 *prop;
	u32 *clkfreq = NULL;
	bool map;
	char ref_clk_src[MAX_PROP_SIZE];

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	of_get_property(pdev->dev.of_node, "qcom,phy-init", &size);
	if (size) {
		dev->phy_init = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
		if (dev->phy_init) {
			dev->phy_init_len =
				size / sizeof(*dev->phy_init);
			EP_PCIE_DBG(dev,
					"PCIe V%d: phy init length is 0x%x\n",
					dev->rev, dev->phy_init_len);

			of_property_read_u32_array(pdev->dev.of_node,
				"qcom,phy-init",
				(unsigned int *)dev->phy_init,
				size / sizeof(dev->phy_init->offset));
		} else {
			EP_PCIE_ERR(dev,
					"PCIe V%d: Could not allocate memory for phy init sequence\n",
					dev->rev);
			return -ENOMEM;
		}
	} else {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: phy init sequence is not present in DT\n",
			dev->rev, dev->phy_rev);
	}

	cnt = of_property_count_strings((&pdev->dev)->of_node,
			"clock-names");
	if (cnt > 0) {
		size_t size = cnt * sizeof(*clkfreq);

		clkfreq = kzalloc(size,	GFP_KERNEL);
		if (!clkfreq)
			return -ENOMEM;
		ret = of_property_read_u32_array(
			(&pdev->dev)->of_node,
			"max-clock-frequency-hz", clkfreq, cnt);
		if (ret)
			EP_PCIE_DBG2(dev,
				"PCIe V%d: cannot get max-clock-frequency-hz property from DT:%d\n",
				dev->rev, ret);
	}

	for (i = 0; i < EP_PCIE_MAX_VREG; i++) {
		vreg_info = &dev->vreg[i];
		vreg_info->hdl =
			devm_regulator_get(&pdev->dev, vreg_info->name);

		if (PTR_ERR(vreg_info->hdl) == -EPROBE_DEFER) {
			EP_PCIE_DBG(dev, "EPROBE_DEFER for VReg:%s\n",
				vreg_info->name);
			ret = PTR_ERR(vreg_info->hdl);
			goto out;
		}

		if (IS_ERR(vreg_info->hdl)) {
			if (vreg_info->required) {
				EP_PCIE_ERR(dev, "Vreg %s doesn't exist\n",
					vreg_info->name);
				ret = PTR_ERR(vreg_info->hdl);
				goto out;
			} else {
				EP_PCIE_DBG(dev,
					"Optional Vreg %s doesn't exist\n",
					vreg_info->name);
				vreg_info->hdl = NULL;
			}
		} else {
			snprintf(prop_name, MAX_PROP_SIZE,
				"qcom,%s-voltage-level", vreg_info->name);
			prop = of_get_property((&pdev->dev)->of_node,
						prop_name, &len);
			if (!prop || (len != (3 * sizeof(__be32)))) {
				EP_PCIE_DBG(dev, "%s %s property\n",
					prop ? "invalid format" :
					"no", prop_name);
			} else {
				vreg_info->max_v = be32_to_cpup(&prop[0]);
				vreg_info->min_v = be32_to_cpup(&prop[1]);
				vreg_info->opt_mode =
					be32_to_cpup(&prop[2]);
			}
		}
	}

	dev->gdsc = devm_regulator_get(&pdev->dev, "gdsc-vdd");

	if (IS_ERR(dev->gdsc)) {
		EP_PCIE_ERR(dev, "PCIe V%d:  Failed to get %s GDSC:%ld\n",
			dev->rev, dev->pdev->name, PTR_ERR(dev->gdsc));
		if (PTR_ERR(dev->gdsc) == -EPROBE_DEFER)
			EP_PCIE_DBG(dev, "PCIe V%d: EPROBE_DEFER for %s GDSC\n",
			dev->rev, dev->pdev->name);
		ret = PTR_ERR(dev->gdsc);
		goto out;
	}

	dev->gdsc_phy = devm_regulator_get(&pdev->dev, "gdsc-phy-vdd");
	if (IS_ERR(dev->gdsc_phy)) {
		EP_PCIE_ERR(dev, "PCIe V%d:  Failed to get %s GDSC_PHY:%ld\n",
			dev->rev, dev->pdev->name, PTR_ERR(dev->gdsc_phy));
		if (PTR_ERR(dev->gdsc_phy) == -EPROBE_DEFER) {
			EP_PCIE_DBG(dev, "PCIe V%d: EPROBE_DEFER for %s GDSC\n",
			dev->rev, dev->pdev->name);
			ret = PTR_ERR(dev->gdsc_phy);
			goto out;
		}
	}

	for (i = 0; i < EP_PCIE_MAX_GPIO; i++) {
		gpio_info = &dev->gpio[i];
		ret = of_get_named_gpio((&pdev->dev)->of_node,
					gpio_info->name, 0);
		if (ret >= 0) {
			gpio_info->num = ret;
			ret = 0;
			EP_PCIE_DBG(dev, "GPIO num for %s is %d\n",
				gpio_info->name, gpio_info->num);
		} else {
			EP_PCIE_DBG(dev,
				"GPIO %s is not supported in this configuration\n",
				gpio_info->name);
			ret = 0;
		}
	}

	dev->pipe_clk_mux = devm_clk_get(&dev->pdev->dev, "pcie_pipe_clk_mux");
	if (IS_ERR(dev->pipe_clk_mux)) {
		EP_PCIE_ERR(dev, "PCIe V%d: Failed to get pcie_pipe_clk_mux\n", dev->rev);
		dev->pipe_clk_mux = NULL;
	}

	dev->pipe_clk_ext_src = devm_clk_get(&dev->pdev->dev, "pcie_pipe_clk_ext_src");
	if (IS_ERR(dev->pipe_clk_ext_src)) {
		EP_PCIE_ERR(dev, "PCIe V%d: Failed to get pipe_ext_src\n", dev->rev);
		dev->pipe_clk_ext_src = NULL;
	}

	scnprintf(ref_clk_src, MAX_PROP_SIZE, "pcie_0_ref_clk_src");
	dev->ref_clk_src = devm_clk_get(&dev->pdev->dev, ref_clk_src);
	if (IS_ERR(dev->ref_clk_src)) {
		EP_PCIE_ERR(dev, "PCIe V%d: Failed to get ref_clk_src\n", dev->rev);
		dev->ref_clk_src = NULL;
	}

	for (i = 0; i < EP_PCIE_MAX_CLK; i++) {
		clk_info = &dev->clk[i];

		clk_info->hdl = devm_clk_get(&pdev->dev, clk_info->name);

		if (IS_ERR(clk_info->hdl)) {
			if (clk_info->required) {
				EP_PCIE_ERR(dev,
					"Clock %s isn't available:%ld\n",
					clk_info->name, PTR_ERR(clk_info->hdl));
				ret = PTR_ERR(clk_info->hdl);
				goto out;
			} else {
				EP_PCIE_DBG(dev, "Ignoring Clock %s\n",
					clk_info->name);
				clk_info->hdl = NULL;
			}
		} else {
			if (clkfreq != NULL) {
				clk_info->freq = clkfreq[i +
					EP_PCIE_MAX_PIPE_CLK];
				EP_PCIE_DBG(dev, "Freq of Clock %s is:%d\n",
					clk_info->name, clk_info->freq);
			}
		}
	}

	for (i = 0; i < EP_PCIE_MAX_PIPE_CLK; i++) {
		clk_info = &dev->pipeclk[i];

		clk_info->hdl = devm_clk_get(&pdev->dev, clk_info->name);

		if (IS_ERR(clk_info->hdl)) {
			if (clk_info->required) {
				EP_PCIE_ERR(dev,
					"Clock %s isn't available:%ld\n",
					clk_info->name, PTR_ERR(clk_info->hdl));
				ret = PTR_ERR(clk_info->hdl);
				goto out;
			} else {
				EP_PCIE_DBG(dev, "Ignoring Clock %s\n",
					clk_info->name);
				clk_info->hdl = NULL;
			}
		} else {
			if (clkfreq != NULL) {
				clk_info->freq = clkfreq[i];
				EP_PCIE_DBG(dev, "Freq of Clock %s is:%d\n",
					clk_info->name, clk_info->freq);
			}
		}
	}

	for (i = 0; i < EP_PCIE_MAX_RESET; i++) {
		reset_info = &dev->reset[i];

		reset_info->hdl = devm_reset_control_get(&pdev->dev,
						reset_info->name);

		if (IS_ERR(reset_info->hdl)) {
			if (reset_info->required) {
				EP_PCIE_ERR(dev,
					"Reset %s isn't available:%ld\n",
					reset_info->name,
					PTR_ERR(reset_info->hdl));

				ret = PTR_ERR(reset_info->hdl);
				reset_info->hdl = NULL;
				goto out;
			} else {
				EP_PCIE_DBG(dev, "Ignoring Reset %s\n",
					reset_info->name);
				reset_info->hdl = NULL;
			}
		}
	}

	for (i = 0; i < EP_PCIE_MAX_RES; i++) {
		res_info = &dev->res[i];
		map = false;

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							res_info->name);

		if (!res) {
			EP_PCIE_ERR(dev,
				"PCIe V%d: can't get resource for %s\n",
					dev->rev, res_info->name);
			if (!strcmp(res_info->name, "tcsr_pcie_perst_en") ||
				(!strcmp(res_info->name, "aoss_cc_reset"))) {
				if (!dev->tcsr_not_supported && dev->aoss_rst_clear) {
					ret = -ENOMEM;
					goto out;
				}
			}
		} else {
			EP_PCIE_DBG(dev, "start addr for %s is %pa\n",
				res_info->name,	&res->start);
			map = true;
		}

		if (map) {
			res_info->base = devm_ioremap(&pdev->dev,
					res->start, resource_size(res));
			if (!res_info->base) {
				EP_PCIE_ERR(dev, "PCIe V%d: can't remap %s\n",
					dev->rev, res_info->name);
				ret = -ENOMEM;
				goto out;
			}
			res_info->resource = res;
		}
	}

	for (i = 0; i < EP_PCIE_MAX_IRQ; i++) {
		irq_info = &dev->irq[i];

		ret = platform_get_irq_byname(pdev, irq_info->name);

		if (ret <= 0) {
			EP_PCIE_DBG2(dev, "PCIe V%d: can't find IRQ # for %s, return value is %d\n",
				dev->rev, irq_info->name, ret);
		} else {
			irq_info->num = ret;
			EP_PCIE_DBG2(dev, "IRQ # for %s is %d\n",
				irq_info->name,	irq_info->num);
			ret = 0;
		}
	}

	dev->parf = dev->res[EP_PCIE_RES_PARF].base;
	dev->phy = dev->res[EP_PCIE_RES_PHY].base;
	dev->mmio = dev->res[EP_PCIE_RES_MMIO].base;
	dev->msi = dev->res[EP_PCIE_RES_MSI].base;
	dev->dm_core = dev->res[EP_PCIE_RES_DM_CORE].base;
	dev->dm_core_vf = dev->res[EP_PCIE_RES_DM_VF_CORE].base;
	dev->edma = dev->res[EP_PCIE_RES_EDMA].base;
	dev->elbi = dev->res[EP_PCIE_RES_ELBI].base;
	dev->iatu = dev->res[EP_PCIE_RES_IATU].base;
	dev->tcsr_perst_en = dev->res[EP_PCIE_RES_TCSR_PERST].base;
	dev->aoss_rst_perst = dev->res[EP_PCIE_RES_AOSS_CC_RESET].base;
	dev->rumi = dev->res[EP_PCIE_RES_RUMI].base;
	dev->msi_vf = dev->res[EP_PCIE_RES_MSI_VF].base;

	dev->icc_path = of_icc_get(&pdev->dev, "icc_path");
	WARN_ON(!dev->icc_path || IS_ERR(dev->icc_path));
	if (!dev->icc_path) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Failed to register bus client for %s, interconnects missing\n",
			dev->rev, dev->pdev->name);
	} else if (IS_ERR(dev->icc_path)) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Failed to register bus client for %s,interconnect-names miss\n",
			dev->rev, dev->pdev->name);
	}

out:
	kfree(clkfreq);
	return ret;
}

static void ep_pcie_release_resources(struct ep_pcie_dev_t *dev)
{
	dev->parf = NULL;
	dev->elbi = NULL;
	dev->dm_core = NULL;
	dev->edma = NULL;
	dev->phy = NULL;
	dev->mmio = NULL;
	dev->msi = NULL;
	dev->iatu = NULL;
	dev->dm_core_vf = NULL;
	dev->msi_vf = NULL;

	if (dev->icc_path && !IS_ERR(dev->icc_path)) {
		icc_put(dev->icc_path);
		dev->icc_path = 0;
	}
}

static void ep_pcie_enumeration_complete(struct ep_pcie_dev_t *dev)
{
	unsigned long irqsave_flags;

	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);

	if (dev->enumerated) {
		EP_PCIE_DBG(dev, "PCIe V%d: Enumeration already done\n",
				dev->rev);
		goto done;
	}
	dev->enumerated = true;
	dev->link_status = EP_PCIE_LINK_ENABLED;

	if (dev->gpio[EP_PCIE_GPIO_MDM2AP].num) {
		/* assert MDM2AP Status GPIO */
		EP_PCIE_DBG2(dev, "PCIe V%d: assert MDM2AP Status\n",
				dev->rev);
		EP_PCIE_DBG(dev,
			"PCIe V%d: MDM2APStatus GPIO initial:%d\n",
			dev->rev,
			gpio_get_value(
			dev->gpio[EP_PCIE_GPIO_MDM2AP].num));
		gpio_set_value(dev->gpio[EP_PCIE_GPIO_MDM2AP].num,
			dev->gpio[EP_PCIE_GPIO_MDM2AP].on);
		EP_PCIE_DBG(dev,
			"PCIe V%d: MDM2APStatus GPIO after assertion:%d\n",
			dev->rev,
			gpio_get_value(
			dev->gpio[EP_PCIE_GPIO_MDM2AP].num));
	}

	hw_drv.device_id = readl_relaxed(dev->dm_core);
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: register driver for device 0x%x\n",
		ep_pcie_dev.rev, hw_drv.device_id);
	ep_pcie_register_drv(&hw_drv);
	if (!dev->no_notify)
		ep_pcie_notify_event(dev, EP_PCIE_EVENT_LINKUP);
	else
		EP_PCIE_DBG(dev,
			"PCIe V%d: do not notify client about linkup\n",
			dev->rev);

done:
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
}

static bool ep_pcie_core_get_clkreq_status(void)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;

	EP_PCIE_DBG(dev,
		"PCIe V%d: PCIe get clkreq status\n", dev->rev);

	return ((readl_relaxed(dev->parf +
			PCIE20_PARF_CLKREQ_OVERRIDE) & BIT(5)) ? false : true);
}

static int ep_pcie_core_clkreq_override(bool config)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;

	EP_PCIE_DBG(dev,
		"PCIe V%d: PCIe clockreq override config:%d\n",
						dev->rev, config);

	if (config) {
		ep_pcie_write_reg_field(dev->parf, PCIE20_PARF_CLKREQ_OVERRIDE,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_VAL_MASK,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_VAL_DEASSERT);
		ep_pcie_write_reg_field(dev->parf, PCIE20_PARF_CLKREQ_OVERRIDE,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_ENABLE_MASK,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_ENABLE_EN);
	} else {
		ep_pcie_write_reg_field(dev->parf, PCIE20_PARF_CLKREQ_OVERRIDE,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_ENABLE_MASK,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_ENABLE_DIS);
		ep_pcie_write_reg_field(dev->parf, PCIE20_PARF_CLKREQ_OVERRIDE,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_VAL_MASK,
			PCIE20_PARF_CLKREQ_IN_OVERRIDE_VAL_ASSERT);
	}

	return 0;
}

static int ep_pcie_core_config_inact_timer(struct ep_pcie_inactivity *param)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;

	EP_PCIE_DBG(dev,
		"PCIe V%d: PCIe config inact timer\n", dev->rev);

	if (!param->enable) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: timer value being disabled:0x%x\n",
			ep_pcie_dev.rev, param->enable);
		ep_pcie_write_reg_field(dev->parf,
			PCIE20_PARF_DEBUG_INT_EN,
			PCIE20_PARF_DEBUG_INT_EN_L1SUB_TIMEOUT_BIT_MASK, 0);
		return 0;
	}

	if (param->timer_us & BIT(31)) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: timer value is a 31 bit value:0x%x\n",
			ep_pcie_dev.rev, param->timer_us);
		return -EINVAL;
	}

	EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: timer value being programmed:0x%x\n",
			ep_pcie_dev.rev, param->timer_us);

	ep_pcie_write_reg_field(dev->parf,
			PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
			PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET_MASK, 0x1);

	usleep_range(PCIE_L1SUB_AHB_TIMEOUT_MIN, PCIE_L1SUB_AHB_TIMEOUT_MAX);
	ep_pcie_write_reg(dev->parf,
			PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
			param->timer_us);
	usleep_range(PCIE_L1SUB_AHB_TIMEOUT_MIN, PCIE_L1SUB_AHB_TIMEOUT_MAX);

	ep_pcie_write_reg_field(dev->parf,
			PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER,
			PCIE20_PARF_L1SUB_AHB_CLK_MAX_TIMER_RESET_MASK, 0x0);

	/* Enable L1SUB timeout bit to enable corresponding aggregated irq */
	ep_pcie_write_reg_field(dev->parf,
			PCIE20_PARF_DEBUG_INT_EN,
			PCIE20_PARF_DEBUG_INT_EN_L1SUB_TIMEOUT_BIT_MASK,
			BIT(0));

	return 0;
}

int ep_pcie_core_l1ss_sleep_config_disable(void)
{
	int rc = 0;
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	struct device *pdev = &dev->pdev->dev;

	if (!m2_enabled)
		return IRQ_HANDLED;

	mutex_lock(&dev->setup_mtx);

	/* Acquiring wakelock for CLKREQ */
	if (!atomic_read(&dev->ep_pcie_dev_wake)) {
		pm_stay_awake(&dev->pdev->dev);
		atomic_set(&dev->ep_pcie_dev_wake, 1);
		EP_PCIE_DBG(dev, "PCIe V%d: Acquired wakelock for CLKREQ\n",
				dev->rev);
	}

	/*
	 * The clkreq_irq is being disabled in the host-initiated L1SS exit
	 * while handling the interrupt. Hence ensuring the clkreq_irq is
	 * disabled in the device initiated L1SS exit as well.
	 */
	if (!clkreq_irq_disable)
		disable_irq(clkreq_irq);

	/* Turning on the resources for M2/L1SS path */
	ep_pcie_l1ss_resources_init(dev);

	/* Configure CLKREQ# pin with default clkreq_n mux */
	rc = pinctrl_pm_select_default_state(pdev);
	if (rc) {
		EP_PCIE_ERR(dev, "PCIe V%d:Error in setting to default state\n", dev->rev);
		goto done;
	}
	EP_PCIE_DBG(dev, "PCIe V%d: Pinctrl set to default state\n", dev->rev);

	/* Ungating CLKREQ# */
	rc = ep_pcie_core_clkreq_override(false);
	if (rc < 0) {
		EP_PCIE_ERR(dev, "PCIe V%d:CLKREQ# override config failed\n", dev->rev);
		goto done;
	}

	EP_PCIE_INFO(dev, "PCIe V%d:CLKREQ# ungated\n", dev->rev);

	m2_enabled = false;
	dev->power_on = true;

	/* Notify timeout exit event to MHI to come out of M2 suspend */
	ep_pcie_notify_event(dev, EP_PCIE_EVENT_L1SUB_TIMEOUT_EXIT);
	EP_PCIE_DBG(dev, "PCIe V%d:Notified TIMEOUT EXIT event\n", dev->rev);

	EP_PCIE_DBG(dev, "PCIe V%d:L1SS sleep configuration disabled\n", dev->rev);

done:
	mutex_unlock(&dev->setup_mtx);
	return rc;
}
EXPORT_SYMBOL_GPL(ep_pcie_core_l1ss_sleep_config_disable);

int ep_pcie_core_l1ss_sleep_config_enable(void)
{
	int rc, ret;
	unsigned long irqsave_flags;
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	struct device *pdev = &dev->pdev->dev;

	EP_PCIE_DBG(dev, "PCIe V%d: l1ss sleep enable config\n", dev->rev);

	/* Gating CLKREQ# */
	rc = ep_pcie_core_clkreq_override(true);
	if (rc < 0) {
		EP_PCIE_ERR(dev, "PCIe V%d:CLKREQ# override config failed\n",
							dev->rev);
		return rc;
	}
	EP_PCIE_DBG(dev, "PCIe V%d: CLKREQ# gated\n", dev->rev);

	if (ep_pcie_core_get_clkreq_status()) {
		EP_PCIE_DBG(dev, "PCIe V%d:CLKREQ status is set\n", dev->rev);
		rc = -EINVAL;
		goto disable_clkreq;
	}

	/* Configure CLKREQ# pin as GPIO to relinquish control by pcie controller */
	rc = pinctrl_pm_select_sleep_state(pdev);
	if (rc) {
		EP_PCIE_ERR(dev, "PCIe V%d:Error in setting to sleep state\n", dev->rev);
		rc = -EINVAL;
		goto disable_clkreq;
	}
	EP_PCIE_DBG(dev, "PCIe V%d: Pinctrl set to sleep state\n", dev->rev);

	enable_irq(clkreq_irq);
	EP_PCIE_DBG(dev, "PCIe V%d:Enabled CLKREQ# irq\n", dev->rev);

	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	clkreq_irq_disable = false;
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
	m2_enabled = true;

	EP_PCIE_DBG(dev, "PCIe V%d:L1SS sleep configured\n", dev->rev);
	return 0;

disable_clkreq:
	ret = ep_pcie_core_clkreq_override(false);
	if (ret)
		EP_PCIE_DBG(dev, "PCIe V%d:CLKREQ# override config failed%d\n",
								dev->rev, ret);

	return rc;
}
EXPORT_SYMBOL_GPL(ep_pcie_core_l1ss_sleep_config_enable);

static void ep_pcie_core_toggle_wake_gpio(bool is_on)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	u32 val = dev->gpio[EP_PCIE_GPIO_WAKE].on;

	if (!is_on) {
		val = !dev->gpio[EP_PCIE_GPIO_WAKE].on;
		EP_PCIE_DBG(dev,
			"PCIe V%d: deassert PCIe WAKE# after PERST# is deasserted\n",
				dev->rev);
	} else {
		dev->wake_counter++;
	}

	/*
	 * Toggle WAKE# GPIO until to prosed state
	 */
	gpio_set_value(dev->gpio[EP_PCIE_GPIO_WAKE].num, val);

	EP_PCIE_DBG(dev,
		"PCIe V%d: No. %ld to %sassert PCIe WAKE#; perst is %sasserted; D3hot is %s received, WAKE GPIO state:%d\n",
		dev->rev, dev->wake_counter,
			is_on ? "":"de-",
			atomic_read(&dev->perst_deast) ? "de-" : "",
			dev->l23_ready ? "" : "not",
			gpio_get_value(dev->gpio[EP_PCIE_GPIO_WAKE].num));

}

static int check_ltssm_detect_timeout(struct ep_pcie_dev_t *dev)
{
	/*
	 * PCIe Base Spec Version 6.0, Section 6.6 PCI Express Reset - Rules,
	 * Sub Section 6.6.1 Convectional Reset mandates device must enter the
	 * LTSSM_Detect state, within 20ms(for Gen 1 and Gen 2 Devices) and
	 * within 100ms (for other Gen devices) of the end of the fundamental reset
	 * (which includes reset based on PERST#).
	 */
	int timeout = GEN_1_2_LTSSM_DETECT_TIMEOUT_MS;
	int ret = 0;

	if (dev->link_speed > PCI_EXP_LNKSTA_CLS_5_0GB)
		timeout = GEN_3_ABOVE_LTSSM_DETECT_TIMEOUT_MS;

	ret = ((ktime_to_ms(dev->ltssm_detect_ts) > timeout) ? 1 : 0);
	dev->ltssm_detect_ts = 0;

	return ret;
}

int ep_pcie_core_enable_endpoint(enum ep_pcie_options opt)
{
	int ret = 0;
	u32 val = 0;
	u32 retries = 0;
	u32 bme = 0;
	u32 link_in_l2 = 0;
	bool perst = true;
	bool ltssm_en = false;
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	u32 reg, linkup_ts;
	int timedout = false;

	EP_PCIE_DBG(dev, "PCIe V%d: options input are 0x%x\n", dev->rev, opt);

	mutex_lock(&dev->setup_mtx);

	if (dev->link_status == EP_PCIE_LINK_ENABLED) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: link is already enabled\n",
			dev->rev);
		goto out;
	}

	if (dev->link_status == EP_PCIE_LINK_UP)
		EP_PCIE_DBG(dev,
			"PCIe V%d: link is already up, let's proceed with the voting for the resources\n",
			dev->rev);

	if (dev->power_on && (opt & EP_PCIE_OPT_POWER_ON)) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: request to turn on the power when link is already powered on\n",
			dev->rev);
		goto out;
	}

	if (opt & EP_PCIE_OPT_POWER_ON) {
		/* enable power */
		ret = ep_pcie_vreg_init(dev);
		if (ret) {
			EP_PCIE_ERR(dev, "PCIe V%d: failed to enable Vreg\n",
				dev->rev);
			goto out;
		}

		/* enable clocks */
		ret = ep_pcie_clk_init(dev);
		if (ret) {
			EP_PCIE_ERR(dev, "PCIe V%d: failed to enable clocks\n",
				dev->rev);
			goto clk_fail;
		}

		dev->power_on = true;

		if (!dev->tcsr_not_supported) {
			EP_PCIE_DBG(dev,
				"TCSR PERST_EN value before configure:0x%x\n",
				readl_relaxed(dev->tcsr_perst_en +
						ep_pcie_dev.tcsr_perst_enable_offset));

			/*
			 * Delatch PERST_EN with TCSR to avoid device reset
			 * during host reboot case.
			 */
			writel_relaxed(0, dev->tcsr_perst_en +
					ep_pcie_dev.tcsr_perst_enable_offset);

			EP_PCIE_DBG(dev,
				"TCSR PERST_EN value after configure:0x%x\n",
				readl_relaxed(dev->tcsr_perst_en +
						ep_pcie_dev.tcsr_perst_enable_offset));

			/*
			 * Delatch PERST_SEPARATION_ENABLE with TCSR, by default, to avoid
			 * device reset during host reboot and hibernate case. This can be
			 * enabled using command line arg but must be used for debug or
			 * experiment purpose only.
			 */
			writel_relaxed(dev->perst_sep_en, dev->tcsr_perst_en +
						ep_pcie_dev.tcsr_perst_separation_en_offset);

			/*
			 * Re-enable hot reset before link up, by default, since we disable it
			 * in pm_turnoff irq. This can be enabled using command line arg but
			 * must be used for debug or experiment purpose only
			 */
			ep_pcie_write_reg_field(dev->tcsr_perst_en,
					ep_pcie_dev.tcsr_hot_reset_en_offset, BIT(0),
						!dev->hot_rst_disable);
		}

		val = readl_relaxed(dev->parf + PCIE20_PARF_PM_STTS);
		EP_PCIE_DBG(dev, "PCIe V%d: PARF_PM_STTS value is : 0x%x.\n",
				dev->rev, val);

		link_in_l2 = !!(val & PARF_PM_LINKST_IN_L2);
		val = !!(val & PARF_XMLH_LINK_UP);

		if (link_in_l2)
			goto trainlink;

		 /* check link status during initial bootup */
		if (!dev->enumerated) {
			if (val) {
				EP_PCIE_INFO(dev,
					"PCIe V%d: link initialized by bootloader for LE PCIe endpoint; skip link training in HLOS.\n",
					dev->rev);
				/*
				 * Read and save the subsystem id set in PBL
				 * (needed for restore during D3->D0)
				 */
				ep_pcie_dev.subsystem_id =
					readl_relaxed(dev->dm_core +
							PCIE20_SUBSYSTEM);
				/*
				 * Skip mhi mmio config for host reboot case
				 * with bios-locking enabled.
				 */
				dev->config_mmio_init = true;
				ep_pcie_core_init(dev, true);
				dev->link_status = EP_PCIE_LINK_UP;
				dev->l23_ready = false;

				/* enable pipe clock for early link init case*/
				ret = ep_pcie_pipe_clk_init(dev);
				if (ret) {
					EP_PCIE_ERR(dev,
					"PCIe V%d: failed to enable pipe clock\n",
					dev->rev);
					goto pipe_clk_fail;
				}
				goto checkbme;
			} else {
				ltssm_en = readl_relaxed(dev->parf
					+ PCIE20_PARF_LTSSM) & BIT(8);

				if (ltssm_en) {
					EP_PCIE_ERR(dev,
						"PCIe V%d: link is not up when LTSSM has already enabled by bootloader.\n",
						dev->rev);
					ret = EP_PCIE_ERROR;
					goto link_fail;
				} else {
					EP_PCIE_DBG(dev,
						"PCIe V%d: Proceed with regular link training.\n",
						dev->rev);
				}
			}
		}

trainlink:
		ret = ep_pcie_reset_init(dev);
		if (ret)
			goto link_fail;
	}

	if (dev->rumi) {
		EP_PCIE_DBG(dev, "PCIe V%d: RUMI: drive clk from RC\n",
				dev->rev);
		ep_pcie_write_reg(dev->rumi, 0, 0x7701);
	}

	if (!(opt & EP_PCIE_OPT_ENUM))
		goto out;

	if (!dev->tcsr_not_supported) {
		EP_PCIE_DBG(dev,
			"TCSR PERST_EN value before configure:0x%x\n",
			readl_relaxed(dev->tcsr_perst_en + ep_pcie_dev.tcsr_perst_enable_offset));

		/*
		 * Delatch PERST_EN with TCSR to avoid device reset
		 * during host reboot case.
		 */
		writel_relaxed(0, dev->tcsr_perst_en + ep_pcie_dev.tcsr_perst_enable_offset);

		EP_PCIE_DBG(dev,
			"TCSR PERST_EN value after configure:0x%x\n",
			readl_relaxed(dev->tcsr_perst_en + ep_pcie_dev.tcsr_perst_enable_offset));
	}

	if (opt & EP_PCIE_OPT_AST_WAKE) {
		/* assert PCIe WAKE# */
		EP_PCIE_INFO(dev, "PCIe V%d: assert PCIe WAKE#\n",
			dev->rev);
		EP_PCIE_DBG(dev, "PCIe V%d: WAKE GPIO initial:%d\n",
			dev->rev,
			gpio_get_value(dev->gpio[EP_PCIE_GPIO_WAKE].num));
		ep_pcie_core_toggle_wake_gpio(false);
		ep_pcie_core_toggle_wake_gpio(true);
	}

	/* wait for host side to deassert PERST */
	retries = 0;
	do {
		if (gpio_get_value(dev->gpio[EP_PCIE_GPIO_PERST].num) == 1)
			break;
		retries++;
		usleep_range(PERST_TIMEOUT_US_MIN, PERST_TIMEOUT_US_MAX);
	} while (retries < PERST_CHECK_MAX_COUNT);

	EP_PCIE_DBG(dev, "PCIe V%d: number of PERST retries:%d\n",
		dev->rev, retries);

	if (retries == PERST_CHECK_MAX_COUNT) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: PERST is not de-asserted by host\n",
			dev->rev);
		ret = EP_PCIE_ERROR;
		goto link_fail;
	}
	atomic_set(&dev->perst_deast, 1);
	if (opt & EP_PCIE_OPT_AST_WAKE) {
		/* deassert PCIe WAKE# */
		ep_pcie_core_toggle_wake_gpio(false);
	}

	/* init PCIe PHY */
	ep_pcie_phy_init(dev);

	/* enable pipe clock */
	ret = ep_pcie_pipe_clk_init(dev);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: failed to enable pipe clock\n",
			dev->rev);
		goto pipe_clk_fail;
	}

	EP_PCIE_DBG(dev, "PCIe V%d: waiting for phy ready...\n", dev->rev);

	timedout = read_poll_timeout_atomic(ep_pcie_phy_is_ready,
			val, val == true, 1, PHY_READY_TIMEOUT_MS, false, dev);
	if (timedout) {
		EP_PCIE_ERR(dev, "PCIe V%d: PCIe PHY  failed to come up\n",
			dev->rev);
		ret = EP_PCIE_ERROR;
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_PHY), false);
		ep_pcie_clk_dump(dev);
		goto link_fail_pipe_clk_deinit;
	} else {
		EP_PCIE_DBG(dev, "PCIe V%d: PCIe  PHY is ready\n", dev->rev);
	}

	ep_pcie_core_init(dev, false);

	/* enable link training */
	if (dev->phy_rev >= 3)
		ep_pcie_write_mask(dev->parf + PCIE20_PARF_LTSSM, 0, BIT(8));
	else
		ep_pcie_write_mask(dev->elbi + PCIE20_ELBI_SYS_CTRL, 0, BIT(0));

	dev->ltssm_detect_ts = ktime_sub(ktime_get(), dev->ltssm_detect_ts);

	EP_PCIE_DBG(dev, "PCIe V%d: check if link is up\n", dev->rev);

	/* Wait for up to 100ms for the link to come up */
	retries = 0;
	do {
		usleep_range(LINK_UP_TIMEOUT_US_MIN, LINK_UP_TIMEOUT_US_MAX);
		val =  readl_relaxed(dev->elbi + PCIE20_ELBI_SYS_STTS);
		retries++;
		if (retries % 100 == 0)
			EP_PCIE_DBG(dev, "PCIe V%d: LTSSM_STATE:0x%x\n",
					dev->rev, (val >> 0xC) & 0x3f);
		perst = atomic_read(&dev->perst_deast) ? 1 : 0;
	} while ((!(val & XMLH_LINK_UP) ||
		!ep_pcie_confirm_linkup(dev, false))
		&& (retries < LINK_UP_CHECK_MAX_COUNT) && perst);

	if (!perst) {
		dev->perst_ast_in_enum_counter++;
		EP_PCIE_ERR(dev,
				"PCIe V%d: Perst asserted No. %ld while waiting for link to be up\n",
				dev->rev, dev->perst_ast_in_enum_counter);
		ret = EP_PCIE_ERROR;
		goto link_fail_pipe_clk_deinit;
	} else if (retries == LINK_UP_CHECK_MAX_COUNT) {
		EP_PCIE_ERR(dev, "PCIe V%d: link initialization failed\n",
			dev->rev);
		ret = EP_PCIE_ERROR;
		goto link_fail_pipe_clk_deinit;
	} else {
		dev->link_status = EP_PCIE_LINK_UP;
		dev->l23_ready = false;

		linkup_ts = LINK_UP_TIMEOUT_US_MIN * retries / 1000;
		EP_PCIE_INFO(dev,
			"PCIe V%d: PERST#-deast to LTSSM_Detect took %lld ms, linkup took %u ms\n",
			dev->rev, ktime_to_ms(dev->ltssm_detect_ts), linkup_ts);
		EP_PCIE_INFO(dev, "PCIe V%d: link initialized for LE PCIe endpoint\n", dev->rev);

		if (check_ltssm_detect_timeout(dev))
			WARN_ON(1);

		pr_crit("PCIe - link initialized for LE PCIe endpoint\n");
	}

checkbme:
	reg = readl_relaxed(dev->dm_core + PCIE20_CAP_LINKCTRLSTATUS);
	dev->current_link_speed = (reg >> LINK_STATUS_REG_SHIFT) & PCI_EXP_LNKSTA_CLS;
	dev->current_link_width = ((reg >> LINK_STATUS_REG_SHIFT) & PCI_EXP_LNKSTA_NLW) >>
		PCI_EXP_LNKSTA_NLW_SHIFT;

	EP_PCIE_INFO(dev,
			"PCIe V%d Link is up at Gen%dX%d\n",
			dev->rev, dev->current_link_speed, dev->current_link_width);

	/* Update icc voting to match bandwidth for actual gen speed and link width */
	ret = qcom_ep_pcie_icc_bw_update(dev, dev->current_link_speed, dev->current_link_width);
	if (ret) {
		EP_PCIE_ERR(dev, "PCIe V%d: fail to set bus bandwidth:%d\n", dev->rev, ret);
		goto out;
	}

	/* Clear AOSS_CC_RESET_STATUS::PERST_RAW_RESET_STATUS when linking up */
	if (dev->aoss_rst_clear && dev->aoss_rst_perst)
		writel_relaxed(ep_pcie_dev.perst_raw_rst_status_mask, dev->aoss_rst_perst);

	/* Make sure clkreq control is with controller, not CESTA */
	if (dev->pcie_cesta_clkreq_offset) {
		ep_pcie_write_reg_field(dev->parf, dev->pcie_cesta_clkreq_offset, BIT(0), 0);
		val = readl_relaxed(dev->parf + dev->pcie_cesta_clkreq_offset);
		EP_PCIE_DBG(dev, "PCIe V%d: CESTA_CLKREQ_CTRL:0x%x.\n", dev->rev, val);
	}

	/*
	 * De-assert WAKE# GPIO following link until L2/3 and WAKE#
	 * is triggered to send data from device to host at which point
	 * it will assert WAKE#.
	 */
	ep_pcie_core_toggle_wake_gpio(false);

	if (dev->active_config)
		ep_pcie_write_reg(dev->dm_core, PCIE20_AUX_CLK_FREQ_REG, dev->aux_clk_val);

	if (!(opt & EP_PCIE_OPT_ENUM_ASYNC)) {
		/* Wait for up to 1000ms for BME to be set */
		retries = 0;

		bme = readl_relaxed(dev->dm_core +
		PCIE20_COMMAND_STATUS) & BIT(2);
		while (!bme && (retries < BME_CHECK_MAX_COUNT)) {
			retries++;
			usleep_range(BME_TIMEOUT_US_MIN, BME_TIMEOUT_US_MAX);
			bme = readl_relaxed(dev->dm_core +
				PCIE20_COMMAND_STATUS) & BIT(2);
		}
	} else {
		EP_PCIE_DBG(dev,
			"PCIe V%d: EP_PCIE_OPT_ENUM_ASYNC is true\n",
			dev->rev);
		bme = readl_relaxed(dev->dm_core +
			PCIE20_COMMAND_STATUS) & BIT(2);
	}

	if (bme) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PCIe link is up and BME is enabled after %d checkings (%d ms)\n",
			dev->rev, retries,
			BME_TIMEOUT_US_MIN * retries / 1000);
		ep_pcie_enumeration_complete(dev);

		EP_PCIE_DBG2(dev, "PCIe V%d: %s Allow L1 after BME is set\n",
				dev->rev, ep_pcie_dev.l1_disable ? "Don't" : "");
		ep_pcie_write_reg_field(dev->parf, PCIE20_PARF_PM_CTRL, BIT(5), dev->l1_disable);

		/* expose BAR to user space to identify modem */
		ep_pcie_bar0_address =
			readl_relaxed(dev->dm_core + PCIE20_BAR0);
	} else {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PCIe link is up but BME is disabled; current SW link status:%d\n",
			dev->rev, dev->link_status);
		dev->link_status = EP_PCIE_LINK_UP;
	}

	goto out;

link_fail_pipe_clk_deinit:
	if (!ep_pcie_debug_keep_resource)
		ep_pcie_pipe_clk_deinit(dev);
link_fail:
	dev->power_on = false;
	if (dev->phy_rev >= 3)
		ep_pcie_write_mask(dev->parf + PCIE20_PARF_LTSSM, BIT(8), 0);
	else
		ep_pcie_write_mask(dev->elbi + PCIE20_ELBI_SYS_CTRL, BIT(0), 0);
pipe_clk_fail:
	if (!ep_pcie_debug_keep_resource)
		ep_pcie_clk_deinit(dev);
clk_fail:
	if (!ep_pcie_debug_keep_resource)
		ep_pcie_vreg_deinit(dev);
	else
		ret = 0;
out:
	mutex_unlock(&dev->setup_mtx);

	return ret;
}

int ep_pcie_core_disable_endpoint(void)
{
	unsigned long irqsave_flags;
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	u32 status;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	mutex_lock(&dev->setup_mtx);
	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	if (atomic_read(&dev->perst_deast) && !m2_enabled) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PERST is de-asserted, exiting disable\n",
			dev->rev);
		goto out;
	}

	if (!dev->power_on) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: the link is already power down\n",
			dev->rev);
		goto out;
	}
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);

	if (!m2_enabled) {
		dev->link_status = EP_PCIE_LINK_DISABLED;

		EP_PCIE_DBG(dev, "PCIe V%d: shut down the link\n",
			dev->rev);
		/* clear all saved IATU config, to enable re-configuration*/
		memset(&dev->conf_ipa_msi_iatu, 0, sizeof(dev->conf_ipa_msi_iatu));
		dev->sriov_enumerated = 0;

		EP_PCIE_DBG2(dev, "PCIe V%d: Set pcie_disconnect_req during D3_COLD\n",
				dev->rev);

		if (!dev->tcsr_not_supported)
			ep_pcie_write_reg_field(dev->tcsr_perst_en,
					ep_pcie_dev.tcsr_reset_separation_offset,
					ep_pcie_dev.pcie_disconnect_req_reg_mask, 1);

		/* Reading status to check which global irq was triggered after PERST# assertion */
		status = readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_STATUS);
		ep_pcie_write_reg(dev->parf, PCIE20_PARF_INT_ALL_CLEAR, status);
		if (status)
			EP_PCIE_DUMP(dev, "PCIe V%d: Global IRQ received; status:0x%x\n",
				dev->rev, status);

		ep_pcie_pipe_clk_deinit(dev);
		ep_pcie_clk_deinit(dev);
		ep_pcie_vreg_deinit(dev);
	} else {
		/* Turn off the resources in M2/L1SS path */
		ep_pcie_l1ss_resources_deinit(dev);
	}

	dev->power_on = false;

	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	if (atomic_read(&dev->ep_pcie_dev_wake) &&
		!atomic_read(&dev->perst_deast)) {
		EP_PCIE_DBG(dev, "PCIe V%d: Released wakelock\n", dev->rev);
		atomic_set(&dev->ep_pcie_dev_wake, 0);
		pm_relax(&dev->pdev->dev);
	} else if (m2_enabled && atomic_read(&dev->ep_pcie_dev_wake)) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: Released wakelock for autonomus M2\n", dev->rev);
		atomic_set(&dev->ep_pcie_dev_wake, 0);
		pm_relax(&dev->pdev->dev);
	} else {
		EP_PCIE_DBG(dev, "PCIe V%d: Bail, Perst-assert:%d wake:%d\n",
			dev->rev, atomic_read(&dev->perst_deast),
				atomic_read(&dev->ep_pcie_dev_wake));
	}

	/*
	 * In some caes though device requested to do an inband PME
	 * the host might still proceed with PERST assertion, below
	 * code is to toggle WAKE in such sceanrios.
	 */
	if (atomic_read(&dev->host_wake_pending)) {
		EP_PCIE_DBG(dev, "PCIe V%d: wake pending, init wakeup\n",
			dev->rev);
		ep_pcie_core_wakeup_host_internal(EP_PCIE_EVENT_PM_D3_COLD);
	}

out:
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
	mutex_unlock(&dev->setup_mtx);
	return 0;
}

int ep_pcie_core_mask_irq_event(enum ep_pcie_irq_event event,
				bool enable)
{
	int rc = 0;
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	unsigned long irqsave_flags;
	u32 mask = 0;

	EP_PCIE_DUMP(dev,
		"PCIe V%d: Client askes to %s IRQ event 0x%x\n",
		dev->rev,
		enable ? "enable" : "disable",
		event);

	spin_lock_irqsave(&dev->ext_lock, irqsave_flags);

	if (dev->aggregated_irq) {
		mask = readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_MASK);
		EP_PCIE_DUMP(dev,
			"PCIe V%d: current PCIE20_PARF_INT_ALL_MASK:0x%x\n",
			dev->rev, mask);
		if (enable)
			ep_pcie_write_mask(dev->parf + PCIE20_PARF_INT_ALL_MASK,
						0, BIT(event));
		else
			ep_pcie_write_mask(dev->parf + PCIE20_PARF_INT_ALL_MASK,
						BIT(event), 0);
		EP_PCIE_DUMP(dev,
			"PCIe V%d: new PCIE20_PARF_INT_ALL_MASK:0x%x\n",
			dev->rev,
			readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_MASK));
	} else {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Client askes to %s IRQ event 0x%x when aggregated IRQ is not supported\n",
			dev->rev,
			enable ? "enable" : "disable",
			event);
		rc = EP_PCIE_ERROR;
	}

	spin_unlock_irqrestore(&dev->ext_lock, irqsave_flags);
	return rc;
}

static irqreturn_t ep_pcie_handle_bme_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;

	dev->bme_counter++;
	EP_PCIE_DBG(dev,
		"PCIe V%d: No. %ld BME IRQ\n", dev->rev, dev->bme_counter);

	if (readl_relaxed(dev->dm_core + PCIE20_COMMAND_STATUS) & BIT(2)) {
		/* BME has been enabled */
		if (!dev->enumerated) {
			EP_PCIE_DBG(dev,
				"PCIe V%d:BME is set. Enumeration is complete\n",
				dev->rev);
			schedule_work(&dev->handle_bme_work);
		} else {
			EP_PCIE_DBG(dev,
				"PCIe V%d:BME is set again after the enumeration has completed; callback client for link ready\n",
				dev->rev);
			ep_pcie_notify_event(dev, EP_PCIE_EVENT_LINKUP);
		}

		EP_PCIE_DBG2(dev, "PCIe V%d: %s Allow L1 after BME is set\n",
				dev->rev, ep_pcie_dev.l1_disable ? "Don't" : "");
		ep_pcie_write_reg_field(dev->parf, PCIE20_PARF_PM_CTRL, BIT(5), dev->l1_disable);
	} else {
		EP_PCIE_DBG(dev,
				"PCIe V%d:BME is still disabled\n", dev->rev);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ep_pcie_handle_linkdown_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;

	dev->linkdown_counter++;
	EP_PCIE_DBG(dev,
		"PCIe V%d: No. %ld linkdown IRQ\n",
		dev->rev, dev->linkdown_counter);

	if (dev->link_status == EP_PCIE_LINK_DISABLED) {
		EP_PCIE_DBG(dev,
			"PCIe V%d:Linkdown IRQ happened when the link is disabled\n",
			dev->rev);
	} else if (!atomic_read(&dev->perst_deast)) {
		EP_PCIE_DBG(dev,
			"PCIe V%d:Linkdown IRQ happened when PERST asserted\n",
			dev->rev);
	} else if (dev->link_status == EP_PCIE_LINK_IN_L23READY) {
		EP_PCIE_DBG(dev,
			"PCIe V%d:Linkdown IRQ happened when link goes to l23ready\n",
			dev->rev);
	} else {
		dev->link_status = EP_PCIE_LINK_DISABLED;
		EP_PCIE_DBG(dev, "PCIe V%d:PCIe link is down for %ld times\n",
			dev->rev, dev->linkdown_counter);
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_PHY) |
				BIT(EP_PCIE_RES_PARF) | BIT(EP_PCIE_RES_MMIO) |
				BIT(EP_PCIE_RES_DM_CORE), true);
		ep_pcie_notify_event(dev, EP_PCIE_EVENT_LINKDOWN);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ep_pcie_handle_linkup_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;

	dev->linkup_counter++;
	EP_PCIE_DBG(dev,
		"PCIe V%d: No. %ld linkup IRQ\n",
		dev->rev, dev->linkup_counter);

	dev->link_status = EP_PCIE_LINK_UP;

	return IRQ_HANDLED;
}

static irqreturn_t ep_pcie_handle_pm_turnoff_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;

	if (!dev->tcsr_not_supported)
		/*
		 * Some hosts will try to recovery link if it doesn't receive PM_L23_Enter
		 * within 10ms after sending PME turn off, in which case if Hot reset is
		 * enabled, PERST# timeout circuit will start to work. If it measures that
		 * the link doesn't enter L0 within a predetermined time, device will crash
		 * with PERST_TIMEOUT_RESET_STATUS set to 1.
		 *
		 * Note that PERST# timeout circuit monitors the Detect to L0 transition and
		 * it gets activated in two scenario:
		 * 1) When PERST# gets de-asserted and perst-separation is enabled.
		 * 2) When hot-reset is enabled and link training is initiated.
		 */
		ep_pcie_write_reg_field(dev->tcsr_perst_en,
					ep_pcie_dev.tcsr_hot_reset_en_offset, BIT(0), 0);

	dev->pm_to_counter++;
	EP_PCIE_DBG2(dev,
		"PCIe V%d: No. %ld PM_TURNOFF is received\n",
		dev->rev, dev->pm_to_counter);
	EP_PCIE_DBG2(dev, "PCIe V%d: Put the link into L23\n",	dev->rev);
	ep_pcie_write_mask(dev->parf + PCIE20_PARF_PM_CTRL, 0, BIT(2));
	dev->link_status = EP_PCIE_LINK_IN_L23READY;

	return IRQ_HANDLED;
}

static irqreturn_t ep_pcie_handle_dstate_change_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;
	u32 dstate;

	dstate = readl_relaxed(dev->dm_core +
			PCIE20_CON_STATUS) & 0x3;

	if (dev->dump_conf)
		ep_pcie_reg_dump(dev, BIT(EP_PCIE_RES_DM_CORE), false);

	if (dstate == 3) {
		dev->l23_ready = true;
		dev->d3_counter++;
		EP_PCIE_DBG(dev,
			"PCIe V%d: No. %ld change to D3 state\n",
			dev->rev, dev->d3_counter);

		if (dev->enumerated) {
			ep_pcie_notify_event(dev, EP_PCIE_EVENT_PM_D3_HOT);
			if (dev->configure_hard_reset) {
				EP_PCIE_ERR(dev,
					"PCIe V%d: Configuring SOC to hard reset, during D3cold\n",
					dev->rev);
			}
		} else {
			EP_PCIE_DBG(dev,
				"PCIe V%d: do not notify client about this D3 hot event since enumeration by HLOS is not done yet\n",
				dev->rev);
		}
		if (atomic_read(&dev->host_wake_pending))
			ep_pcie_core_wakeup_host_internal(
				EP_PCIE_EVENT_PM_D3_HOT);

	} else if (dstate == 0) {
		dev->l23_ready = false;
		dev->d0_counter++;

		atomic_set(&dev->host_wake_pending, 0);
		EP_PCIE_DBG(dev,
			"PCIe V%d: No. %ld change to D0 state, clearing wake pending:%d\n",
			dev->rev, dev->d0_counter,
			atomic_read(&dev->host_wake_pending));
		/*
		 * During device bootup, there will not be any PERT-deassert,
		 * so aquire wakelock from D0 event
		 */
		if (!atomic_read(&dev->ep_pcie_dev_wake)) {
			pm_stay_awake(&dev->pdev->dev);
			atomic_set(&dev->ep_pcie_dev_wake, 1);
			EP_PCIE_DBG(dev, "PCIe V%d: Acquired wakelock in D0\n",
				dev->rev);
		}
		ep_pcie_notify_event(dev, EP_PCIE_EVENT_PM_D0);
	} else {
		EP_PCIE_ERR(dev,
			"PCIe V%d:invalid D state change to 0x%x\n",
			dev->rev, dstate);
	}
	return IRQ_HANDLED;
}

static int ep_pcie_enumeration(struct ep_pcie_dev_t *dev)
{
	int ret = 0;

	if (!dev) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: the input handler is NULL\n",
			ep_pcie_dev.rev);
		return -ENODEV;
	}

	EP_PCIE_DBG(dev,
		"PCIe V%d: start PCIe link enumeration per host side\n",
		dev->rev);

	ret = ep_pcie_core_enable_endpoint(EP_PCIE_OPT_ALL);

	if (ret) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: PCIe link enumeration failed\n",
			ep_pcie_dev.rev);
	} else {
		if (dev->link_status == EP_PCIE_LINK_ENABLED) {
			EP_PCIE_INFO(&ep_pcie_dev,
				"PCIe V%d: PCIe link enumeration is successful with host side\n",
				ep_pcie_dev.rev);
		} else if (dev->link_status == EP_PCIE_LINK_UP) {
			EP_PCIE_INFO(&ep_pcie_dev,
				"PCIe V%d: PCIe link training is successful with host side. Waiting for enumeration to complete\n",
				ep_pcie_dev.rev);
		} else {
			EP_PCIE_ERR(&ep_pcie_dev,
				"PCIe V%d: PCIe link is in the unexpected status: %d\n",
				ep_pcie_dev.rev, dev->link_status);
		}
	}

	return ret;
}

static void handle_d3cold_func(struct work_struct *work)
{
	struct ep_pcie_dev_t *dev = container_of(work, struct ep_pcie_dev_t,
					handle_d3cold_work);
	unsigned long irqsave_flags;

	EP_PCIE_DBG(dev,
		"PCIe V%d: shutdown PCIe link due to PERST assertion before BME is set\n",
		dev->rev);
	ep_pcie_core_disable_endpoint();
	dev->no_notify = false;
	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	if (atomic_read(&dev->ep_pcie_dev_wake) &&
		!atomic_read(&dev->perst_deast)) {
		atomic_set(&dev->ep_pcie_dev_wake, 0);
		pm_relax(&dev->pdev->dev);
		EP_PCIE_DBG(dev, "PCIe V%d: Released wakelock\n", dev->rev);
	} else {
		EP_PCIE_DBG(dev, "PCIe V%d: Bail, Perst-assert:%d wake:%d\n",
			dev->rev, atomic_read(&dev->perst_deast),
				atomic_read(&dev->ep_pcie_dev_wake));
	}
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
}

static void handle_clkreq_func(struct work_struct *work)
{
	int res = 0;
	struct ep_pcie_dev_t *dev = container_of(work,
			struct ep_pcie_dev_t, handle_clkreq);

	EP_PCIE_DBG(dev, "PCIe V%d: Handling clkreq irq\n", dev->rev);

	res = ep_pcie_core_l1ss_sleep_config_disable();
	if (res) {
		EP_PCIE_ERR(dev, "PCIe V%d: Error disabling L1SS sleep\n", dev->rev);
		return;
	}
}

static void handle_bme_func(struct work_struct *work)
{
	struct ep_pcie_dev_t *dev = container_of(work,
			struct ep_pcie_dev_t, handle_bme_work);

	ep_pcie_enumeration_complete(dev);
}

static irqreturn_t ep_pcie_handle_perst_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;
	unsigned long irqsave_flags;
	irqreturn_t result = IRQ_HANDLED;
	u32 perst;

	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);

	perst = gpio_get_value(dev->gpio[EP_PCIE_GPIO_PERST].num);

	if (!dev->enumerated) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PCIe is not enumerated yet; PERST is %sasserted\n",
			dev->rev, perst ? "de" : "");
		atomic_set(&dev->perst_deast, perst ? 1 : 0);
		if (perst) {
			/*
			 * Hold a wakelock to avoid delay during
			 * link enablement in PCIE layer in non
			 * enumerated scenario.
			 */
			if (!atomic_read(&dev->ep_pcie_dev_wake)) {
				pm_stay_awake(&dev->pdev->dev);
				atomic_set(&dev->ep_pcie_dev_wake, 1);
				EP_PCIE_DBG(dev,
					"PCIe V%d: Acquired wakelock\n",
					dev->rev);
			}
			/*
			 * Perform link enumeration with the host side in the
			 * bottom half
			 */
			result = IRQ_WAKE_THREAD;
		} else {
			dev->no_notify = true;
			/* shutdown the link if the link is already on */
			schedule_work(&dev->handle_d3cold_work);
		}

		goto out;
	}

	if (perst) {
		atomic_set(&dev->perst_deast, 1);
		dev->perst_deast_counter++;
		/*
		 * Hold a wakelock to avoid missing BME and other
		 * interrupts if apps goes into suspend before BME is set.
		 */
		if (!atomic_read(&dev->ep_pcie_dev_wake)) {
			pm_stay_awake(&dev->pdev->dev);
			atomic_set(&dev->ep_pcie_dev_wake, 1);
			EP_PCIE_DBG(dev, "PCIe V%d: Acquired wakelock\n",
				dev->rev);
		}
		EP_PCIE_DBG(dev,
			"PCIe V%d: No. %ld PERST deassertion\n",
			dev->rev, dev->perst_deast_counter);
		dev->ltssm_detect_ts = ktime_get();
		result = IRQ_WAKE_THREAD;
	} else {
		atomic_set(&dev->perst_deast, 0);
		dev->perst_ast_counter++;
		EP_PCIE_DBG(dev,
			"PCIe V%d: No. %ld PERST assertion\n",
			dev->rev, dev->perst_ast_counter);

		if (!ep_pcie_notify_event(dev, EP_PCIE_EVENT_PM_D3_COLD)) {
			dev->no_notify = true;
			EP_PCIE_DBG(dev,
				"PCIe V%d: Client driver is not ready when this PERST assertion happens; shutdown link now\n",
				dev->rev);
			schedule_work(&dev->handle_d3cold_work);
		}
	}

out:
	/* Set trigger type based on the next expected value of perst gpio */
	irq_set_irq_type(dev->perst_irq, (perst ? IRQF_TRIGGER_LOW :
						  IRQF_TRIGGER_HIGH));

	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);

	return result;
}

static irqreturn_t ep_pcie_handle_perst_deassert(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;

	if (!ep_pcie_notify_event(dev, EP_PCIE_EVENT_PM_RST_DEAST)) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: Start enumeration due to PERST deassertion\n", dev->rev);
		ep_pcie_enumeration(dev);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ep_pcie_handle_clkreq_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;
	unsigned long irqsave_flags;

	EP_PCIE_INFO(dev, "PCIe V%d: received CLKREQ# irq\n", dev->rev);

	if (!m2_enabled)
		return IRQ_HANDLED;

	disable_irq_nosync(clkreq_irq);
	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	clkreq_irq_disable = true;
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
	EP_PCIE_DBG(dev, "PCIe V%d: Disabled CLKREQ# irq\n", dev->rev);

	schedule_work(&dev->handle_clkreq);
	return IRQ_HANDLED;
}

static irqreturn_t ep_pcie_handle_sriov_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;
	int i;
	u32 sriov_irq_status, sriov_irq_mask = 0;

	if (!ep_pcie_dev.sriov_cap)
		goto exit_irq;

	if (dev->sriov_mask) {
		sriov_irq_status = readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_3_STATUS);
		sriov_irq_mask = readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_3_MASK);
		ep_pcie_write_mask(
			dev->parf + PCIE20_PARF_INT_ALL_3_CLEAR, 0, sriov_irq_status);
		sriov_irq_status &= sriov_irq_mask;
		sriov_irq_status >>= find_first_bit(&dev->sriov_mask, BITS_PER_LONG);
	} else {
		sriov_irq_status = readl_relaxed(dev->parf + PCIE20_INT_ALL_VF_BME_STATUS);
		sriov_irq_mask = readl_relaxed(dev->parf + PCIE20_INT_ALL_VF_BME_MASK);
		ep_pcie_write_mask(
			dev->parf + PCIE20_INT_ALL_VF_BME_CLEAR, 0, sriov_irq_status);
		sriov_irq_status &= sriov_irq_mask;
	}

	dev->sriov_irq_counter++;
	EP_PCIE_DUMP(dev,
		"PCIe V%d: No. %ld SR-IOV IRQ %d received; status:0x%x; mask:0x%x\n",
		dev->rev, dev->sriov_irq_counter, irq, sriov_irq_status, sriov_irq_mask);

	if (!sriov_irq_status)
		goto exit_irq;

	for (i = 0; i < ep_pcie_dev.num_vfs; i++) {
		if ((sriov_irq_status & BIT(i)) && !(dev->sriov_enumerated & BIT(i))) {
			ep_pcie_notify_vf_bme_event(dev, EP_PCIE_EVENT_LINKUP_VF, i + 1);
			dev->sriov_enumerated |= BIT(i);
		}
	}

exit_irq:
	return IRQ_HANDLED;

}

static irqreturn_t ep_pcie_handle_global_irq(int irq, void *data)
{
	struct ep_pcie_dev_t *dev = data;
	int i, ret;
	u32 status;
	unsigned long irqsave_flags;

	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	if (!atomic_read(&dev->perst_deast)) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Global irq not processed as PERST# is asserted\n",
			dev->rev);
		spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
		return IRQ_HANDLED;
	}

	status = readl_relaxed(dev->parf + PCIE20_PARF_INT_ALL_STATUS);
	ep_pcie_write_reg(dev->parf, PCIE20_PARF_INT_ALL_CLEAR, status);

	dev->global_irq_counter++;
	EP_PCIE_DUMP(dev,
		"PCIe V%d: No. %ld Global IRQ %d received; status:0x%x\n",
		dev->rev, dev->global_irq_counter, irq, status);

	if (!status)
		goto sriov_irq;

	for (i = 1; i <= EP_PCIE_INT_EVT_MAX; i++) {
		if (status & BIT(i)) {
			switch (i) {
			case EP_PCIE_INT_EVT_LINK_DOWN:
				EP_PCIE_DUMP(dev,
					"PCIe V%d: handle linkdown event\n",
					dev->rev);
				ep_pcie_handle_linkdown_irq(irq, data);
				break;
			case EP_PCIE_INT_EVT_BME:
				EP_PCIE_DUMP(dev,
					"PCIe V%d: handle BME event\n",
					dev->rev);
				ep_pcie_handle_bme_irq(irq, data);
				break;
			case EP_PCIE_INT_EVT_PM_TURNOFF:
				EP_PCIE_DUMP(dev,
					"PCIe V%d: handle PM Turn-off event\n",
					dev->rev);
				ep_pcie_handle_pm_turnoff_irq(irq, data);
				break;
			case EP_PCIE_INT_EVT_MHI_A7:
				EP_PCIE_DUMP(dev,
					"PCIe V%d: handle MHI A7 event\n",
					dev->rev);
				ep_pcie_notify_event(dev, EP_PCIE_EVENT_MHI_A7);
				break;
			case EP_PCIE_INT_EVT_DSTATE_CHANGE:
				EP_PCIE_DUMP(dev,
					"PCIe V%d: handle D state change event\n",
					dev->rev);
				ep_pcie_handle_dstate_change_irq(irq, data);
				break;
			case EP_PCIE_INT_EVT_LINK_UP:
				EP_PCIE_DUMP(dev,
					"PCIe V%d: handle linkup event\n",
					dev->rev);
				ep_pcie_handle_linkup_irq(irq, data);
				break;
			case EP_PCIE_INT_EVT_L1SUB_TIMEOUT:
				EP_PCIE_DUMP(dev,
					"PCIe V%d: handle L1ss timeout event\n",
					dev->rev);
				ep_pcie_notify_event(dev,
						EP_PCIE_EVENT_L1SUB_TIMEOUT);
				break;
			default:
				EP_PCIE_ERR(dev,
					"PCIe V%d: Unexpected event %d\n",
					dev->rev, i);
			}
		}
	}

sriov_irq:
	ret = ep_pcie_handle_sriov_irq(irq, data);
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
	return ret;
}

int32_t ep_pcie_irq_init(struct ep_pcie_dev_t *dev)
{
	int ret;
	struct device *pdev = &dev->pdev->dev;

	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	/* Initialize all works to be performed before registering for IRQs*/
	INIT_WORK(&dev->handle_bme_work, handle_bme_func);
	INIT_WORK(&dev->handle_clkreq, handle_clkreq_func);
	INIT_WORK(&dev->handle_d3cold_work, handle_d3cold_func);

	if (dev->aggregated_irq) {
		if (!ep_pcie_dev.perst_enum)
			irq_set_status_flags(dev->irq[EP_PCIE_INT_GLOBAL].num, IRQ_NOAUTOEN);
		ret = devm_request_irq(pdev,
			dev->irq[EP_PCIE_INT_GLOBAL].num,
			ep_pcie_handle_global_irq,
			IRQF_TRIGGER_HIGH, dev->irq[EP_PCIE_INT_GLOBAL].name,
			dev);
		if (ret) {
			EP_PCIE_ERR(dev,
				"PCIe V%d: Unable to request global interrupt %d\n",
				dev->rev, dev->irq[EP_PCIE_INT_GLOBAL].num);
			return ret;
		}

		ret = enable_irq_wake(dev->irq[EP_PCIE_INT_GLOBAL].num);
		if (ret) {
			EP_PCIE_ERR(dev,
				"PCIe V%d: Unable to enable wake for Global interrupt\n",
				dev->rev);
		}

		EP_PCIE_DBG(dev,
			"PCIe V%d: request global interrupt %d\n",
			dev->rev, dev->irq[EP_PCIE_INT_GLOBAL].num);
		goto perst_irq;
	}

	/* register handler for BME interrupt */
	ret = devm_request_irq(pdev,
		dev->irq[EP_PCIE_INT_BME].num,
		ep_pcie_handle_bme_irq,
		IRQF_TRIGGER_RISING, dev->irq[EP_PCIE_INT_BME].name,
		dev);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to request BME interrupt %d\n",
			dev->rev, dev->irq[EP_PCIE_INT_BME].num);
		return ret;
	}

	ret = enable_irq_wake(dev->irq[EP_PCIE_INT_BME].num);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to enable wake for BME interrupt\n",
			dev->rev);
		return ret;
	}

	/* register handler for linkdown interrupt */
	ret = devm_request_irq(pdev,
		dev->irq[EP_PCIE_INT_LINK_DOWN].num,
		ep_pcie_handle_linkdown_irq,
		IRQF_TRIGGER_RISING, dev->irq[EP_PCIE_INT_LINK_DOWN].name,
		dev);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to request linkdown interrupt %d\n",
			dev->rev, dev->irq[EP_PCIE_INT_LINK_DOWN].num);
		return ret;
	}

	/* register handler for linkup interrupt */
	ret = devm_request_irq(pdev,
		dev->irq[EP_PCIE_INT_LINK_UP].num, ep_pcie_handle_linkup_irq,
		IRQF_TRIGGER_RISING, dev->irq[EP_PCIE_INT_LINK_UP].name,
		dev);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to request linkup interrupt %d\n",
			dev->rev, dev->irq[EP_PCIE_INT_LINK_UP].num);
		return ret;
	}

	/* register handler for PM_TURNOFF interrupt */
	ret = devm_request_irq(pdev,
		dev->irq[EP_PCIE_INT_PM_TURNOFF].num,
		ep_pcie_handle_pm_turnoff_irq,
		IRQF_TRIGGER_RISING, dev->irq[EP_PCIE_INT_PM_TURNOFF].name,
		dev);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to request PM_TURNOFF interrupt %d\n",
			dev->rev, dev->irq[EP_PCIE_INT_PM_TURNOFF].num);
		return ret;
	}

	/* register handler for D state change interrupt */
	ret = devm_request_irq(pdev,
		dev->irq[EP_PCIE_INT_DSTATE_CHANGE].num,
		ep_pcie_handle_dstate_change_irq,
		IRQF_TRIGGER_RISING, dev->irq[EP_PCIE_INT_DSTATE_CHANGE].name,
		dev);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to request D state change interrupt %d\n",
			dev->rev, dev->irq[EP_PCIE_INT_DSTATE_CHANGE].num);
		return ret;
	}

perst_irq:
	/*
	 * Check initial state of perst gpio to set the trigger type
	 * based on the next expected level of the gpio
	 */
	if (gpio_get_value(dev->gpio[EP_PCIE_GPIO_PERST].num) == 1)
		atomic_set(&dev->perst_deast, 1);

	dev->perst_irq = gpio_to_irq(dev->gpio[EP_PCIE_GPIO_PERST].num);
	if (dev->perst_irq < 0) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to get IRQ from GPIO_PERST %d\n",
			dev->rev, dev->perst_irq);
		return dev->perst_irq;
	}

	/* register handler for PERST interrupt */
	ret = devm_request_threaded_irq(pdev, dev->perst_irq, ep_pcie_handle_perst_irq,
				ep_pcie_handle_perst_deassert,
			       ((atomic_read(&dev->perst_deast) ?
				 IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH) |
			       IRQF_EARLY_RESUME), "ep_pcie_perst", dev);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to request PERST interrupt %d\n",
			dev->rev, dev->perst_irq);
		return ret;
	}

	ret = enable_irq_wake(dev->perst_irq);
	if (ret) {
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unable to enable PERST interrupt %d\n",
			dev->rev, dev->perst_irq);
		return ret;
	}

	if (dev->m2_autonomous) {
		/* register handler for clkreq interrupt */
		EP_PCIE_ERR(dev,
				"PCIe V%d: Register for CLKREQ interrupt %d\n",
				dev->rev, clkreq_irq);
		clkreq_irq = gpio_to_irq(dev->gpio[EP_PCIE_GPIO_CLKREQ].num);
		irq_set_status_flags(clkreq_irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(pdev, clkreq_irq, NULL,
			ep_pcie_handle_clkreq_irq,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_EARLY_RESUME,
			"ep_pcie_clkreq", dev);
		if (ret) {
			EP_PCIE_ERR(dev,
				"PCIe V%d: Unable to request CLKREQ interrupt %d\n",
				dev->rev, clkreq_irq);
			return ret;
		}

		/* Set CLKREQ# irq as wake capable */
		ret = enable_irq_wake(clkreq_irq);
		if (ret) {
			EP_PCIE_ERR(dev,
				"PCIe V%d: Error in enable wake for clkreq interrupt %d\n",
				dev->rev, clkreq_irq);
			return ret;
		}
	}

	return 0;
}

void ep_pcie_irq_deinit(struct ep_pcie_dev_t *dev)
{
	EP_PCIE_DBG(dev, "PCIe V%d\n", dev->rev);

	if (dev->perst_irq >= 0)
		disable_irq(dev->perst_irq);
}

int ep_pcie_core_register_event(struct ep_pcie_register_event *reg)
{
	void __iomem *dbi = ep_pcie_dev.dm_core_vf;
	u32 bme, vf_id;

	if (!reg) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: Event registration is NULL\n",
			ep_pcie_dev.rev);
		return -ENODEV;
	}

	if (!reg->user) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: User of event registration is NULL\n",
			ep_pcie_dev.rev);
		return -ENODEV;
	}

	ep_pcie_dev.event_reg = reg;
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: Event 0x%x is registered\n",
		ep_pcie_dev.rev, reg->events);

	ep_pcie_dev.client_ready = true;

	/*
	 * When EP undergoes a warmboot, the config spaceand BME of VF
	 * instances are kept intact by the host. Hence there is no BME
	 * IRQ triggered. Check for BME on VF DBI space and generate
	 * LINKUP_VF event if BME is set.
	 */
	if (reg->events & EP_PCIE_EVENT_LINKUP_VF) {
		for (vf_id = 0; vf_id < ep_pcie_dev.num_vfs; vf_id++) {
			bme = readl_relaxed(dbi +
				(PCIE20_VF_COMMAND_STATUS(vf_id))) & BIT(2);
			if (bme && !(ep_pcie_dev.sriov_enumerated & BIT(vf_id))) {
				ep_pcie_notify_vf_bme_event(&ep_pcie_dev,
					EP_PCIE_EVENT_LINKUP_VF, vf_id + 1);
				ep_pcie_dev.sriov_enumerated |= BIT(vf_id);
			}
		}
	}

	return 0;
}

int ep_pcie_core_deregister_event(void)
{
	if (ep_pcie_dev.event_reg) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: current registered events:0x%x; events are deregistered\n",
			ep_pcie_dev.rev, ep_pcie_dev.event_reg->events);
		ep_pcie_dev.event_reg = NULL;
	} else {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: Event registration is NULL\n",
			ep_pcie_dev.rev);
	}

	return 0;
}

enum ep_pcie_link_status ep_pcie_core_get_linkstatus(void)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	u32 bme;

	if (!dev->power_on || (dev->link_status == EP_PCIE_LINK_DISABLED)) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PCIe endpoint is not powered on\n",
			dev->rev);
		return EP_PCIE_LINK_DISABLED;
	}

	if (dev->link_status == EP_PCIE_LINK_IN_L23READY) {
		EP_PCIE_DBG(dev, "PCIe V%d: PCIe endpoint has sent PM_ENTER_L23\n", dev->rev);
		return EP_PCIE_LINK_IN_L23READY;
	}

	bme = readl_relaxed(dev->dm_core +
		PCIE20_COMMAND_STATUS) & BIT(2);
	if (bme) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PCIe link is up and BME is enabled; current SW link status:%d\n",
			dev->rev, dev->link_status);
		dev->link_status = EP_PCIE_LINK_ENABLED;
		if (dev->no_notify) {
			EP_PCIE_DBG(dev,
				"PCIe V%d: BME is set now, but do not tell client about BME enable\n",
				dev->rev);
			return EP_PCIE_LINK_UP;
		}
	} else {
		EP_PCIE_DBG(dev,
			"PCIe V%d: PCIe link is up but BME is disabled; current SW link status:%d\n",
			dev->rev, dev->link_status);
		dev->link_status = EP_PCIE_LINK_UP;
	}
	return dev->link_status;
}

int ep_pcie_core_config_outbound_iatu(struct ep_pcie_iatu entries[],
				u32 num_entries, u32 vf_id)
{
	u64 data_start = 0;
	u64 data_end = 0;
	u64 data_tgt_lower = 0;
	u64 data_tgt_upper = 0;
	u64 ctrl_start = 0;
	u64 ctrl_end = 0;
	u64 ctrl_tgt_lower = 0;
	u64 ctrl_tgt_upper = 0;
	bool once = true;
	u32 data_start_upper = 0;
	u32 ctrl_start_upper = 0;

	if ((num_entries > MAX_IATU_ENTRY_NUM) || !num_entries) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: Wrong iATU entry number %d\n",
			ep_pcie_dev.rev, num_entries);
		return EP_PCIE_ERROR;
	}

	data_start = entries[0].start;
	data_end = entries[0].end;
	data_tgt_lower = entries[0].tgt_lower;
	data_tgt_upper = entries[0].tgt_upper;

	if (num_entries > 1) {
		ctrl_start = entries[1].start;
		ctrl_end = entries[1].end;
		ctrl_tgt_lower = entries[1].tgt_lower;
		ctrl_tgt_upper = entries[1].tgt_upper;
	}

	if (ep_pcie_dev.active_config) {
		data_start_upper = EP_PCIE_OATU_UPPER;
		ctrl_start_upper = EP_PCIE_OATU_UPPER;
		if (once) {
			once = false;
			EP_PCIE_DBG2(&ep_pcie_dev,
				"PCIe V%d: No outbound iATU config is needed since active config is enabled\n",
				ep_pcie_dev.rev);
		}
	} else {
		data_start_upper = HIDWORD(data_start);
		ctrl_start_upper = HIDWORD(ctrl_start);
	}

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: data_start:0x%llx; data_end:0x%llx; data_tgt_lower:0x%llx; data_tgt_upper:0x%llx; ctrl_start:0x%llx; ctrl_end:0x%llx; ctrl_tgt_lower:0x%llx; ctrl_tgt_upper:0x%llx\n",
		ep_pcie_dev.rev, data_start, data_end, data_tgt_lower,
		data_tgt_upper, ctrl_start, ctrl_end, ctrl_tgt_lower,
		ctrl_tgt_upper);

	if ((ctrl_end < data_start) || (data_end < ctrl_start)) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: iATU configuration case No. 1: detached\n",
			ep_pcie_dev.rev);
		ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
					EP_PCIE_OATU_INDEX_DATA,
					vf_id,
					data_start, data_start_upper, data_end,
					data_tgt_lower, data_tgt_upper);
		ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
					EP_PCIE_OATU_INDEX_CTRL,
					vf_id,
					ctrl_start, ctrl_start_upper, ctrl_end,
					ctrl_tgt_lower, ctrl_tgt_upper);
	} else if ((data_start <= ctrl_start) && (ctrl_end <= data_end)) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: iATU configuration case No. 2: included\n",
			ep_pcie_dev.rev);
		ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
					EP_PCIE_OATU_INDEX_DATA,
					vf_id,
					data_start, data_start_upper, data_end,
					data_tgt_lower, data_tgt_upper);
	} else {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: iATU configuration case No. 3: overlap\n",
			ep_pcie_dev.rev);
		ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
					EP_PCIE_OATU_INDEX_CTRL,
					vf_id,
					ctrl_start, ctrl_start_upper, ctrl_end,
					ctrl_tgt_lower, ctrl_tgt_upper);
		ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
					EP_PCIE_OATU_INDEX_DATA,
					vf_id,
					data_start, data_start_upper, data_end,
					data_tgt_lower, data_tgt_upper);
	}

	return 0;
}

int ep_pcie_core_msix_db_val(u32 idx, u32 vf_id)
{
	u32 n = 0;

	if (vf_id) {
		n = vf_id - 1;
		/* Shift idx to the vf postion to generate msi */
		idx = idx | (n << 16);
		/* Set bit(15) to activate virtual function usage */
		idx |= PCIE20_MSIX_DB_VF_ACTIVE;
	}
	return idx;
}

int ep_pcie_core_get_msix_config(struct ep_pcie_msi_config *cfg, u32 vf_id)
{
	u32 lower;
	u32 data = 0, ctrl_reg;
	u32 cap = ep_pcie_dev.msix_cap;
	void __iomem *dbi = ep_pcie_dev.dm_core;

	/*
	 * We can only use the upper region of 40th bit for MSIX doorbell writes
	 * as ECPRI doesn't have access in the NOC connectivity to PCIE memory.
	 * Since the upper region is usually used for host DDR memory transactions
	 * BAR address is chosen consciously to avoid creation of any memory
	 * hole in the host addressable memory region.
	 */
	lower = readl_relaxed(ep_pcie_dev.dm_core + PCIE20_BAR0);
	/* Bar address is between 4-31 bits, masking 0-3 bits */
	lower &= ~(0xf);
	cfg->lower = lower;

	/* Set 40th bit in upper address */
	cfg->upper = 0x100;

	lower |= PCIE20_MSIX_ADDRESS_MATCH_EN;
	ep_pcie_write_reg(dbi, cap + PCIE20_MSIX_ADDRESS_MATCH_LOW_OFF, lower);
	/*
	 * Make sure 40th bit is not set in upper address match register
	 * The PCIE EP controller is configured to flip 40th bit during
	 * transactions. While a MSIX generation happens from MHI driver
	 * or ECPRI/IPA DMA engiene to the BAR address with 40th bit set
	 * the controller will flip 40th bit and match it with the
	 * MSIX_ADDRESS_MATCH LOW,HIGH registers. So HIGH register has to
	 * have 40th bit unset so that the ADDRESS matches and the
	 * transaction is treated as a write to generate MSIX to host.
	 */
	ep_pcie_write_reg(dbi, cap + PCIE20_MSIX_ADDRESS_MATCH_UPPER_OFF, 0x0);

	data = ep_pcie_core_msix_db_val(data, vf_id);
	/* Read max num of MSI-X vector support */
	ctrl_reg = readl_relaxed(dbi + cap);
	cfg->msg_num = (ctrl_reg >> 16) & 0x7FF;

	cfg->data = data;

	return 0;
}

int ep_pcie_core_get_msi_config(struct ep_pcie_msi_config *cfg, u32 vf_id)
{
	u32 cap, lower, upper, data, ctrl_reg;
	static u32 changes;
	u32 n = 0;
	void __iomem *dbi;
	struct resource *msi;
	struct ep_pcie_msi_config *msi_cfg = &ep_pcie_dev.msi_cfg[vf_id];
	u32 msix_cap = ep_pcie_dev.msix_cap;

	if (!vf_id) {
		dbi = ep_pcie_dev.dm_core;
		msi = ep_pcie_dev.res[EP_PCIE_RES_MSI].resource;
	} else {
		dbi = ep_pcie_dev.dm_core_vf;
		msi = ep_pcie_dev.res[EP_PCIE_RES_MSI_VF].resource;
		n = vf_id - 1;
	}

	if (ep_pcie_dev.link_status == EP_PCIE_LINK_DISABLED) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: PCIe link is currently disabled\n",
			ep_pcie_dev.rev);
		return EP_PCIE_ERROR;
	}

	if (msix_cap) {
		ctrl_reg = readl_relaxed(dbi + msix_cap + PCIE20_MSIX_CAP_ID_NEXT_CTRL_REG(n));
		if (ctrl_reg & BIT(31)) {
			cfg->msi_type = MSIX;
			return ep_pcie_core_get_msix_config(cfg, vf_id);
		}
	}

	cap = readl_relaxed(dbi + PCIE20_MSI_CAP_ID_NEXT_CTRL(n));

	if (cap & BIT(16)) {
		cfg->msi_type = MSI;

		lower = readl_relaxed(dbi + PCIE20_MSI_LOWER(n));
		upper = readl_relaxed(dbi + PCIE20_MSI_UPPER(n));
		data = readl_relaxed(dbi + PCIE20_MSI_DATA(n));

		if (ep_pcie_dev.use_iatu_msi) {
			if (ep_pcie_dev.active_config)
				ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
						EP_PCIE_OATU_INDEX_MSI,
						vf_id,
						msi->start + (n * 0x8), EP_PCIE_OATU_UPPER,
						msi->end, lower, upper);
			else
				ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
						EP_PCIE_OATU_INDEX_MSI,
						vf_id,
						msi->start + (n * 0x8), 0, msi->end,
						lower, upper);
		}

		if (ep_pcie_dev.active_config || ep_pcie_dev.pcie_edma ||
			ep_pcie_dev.no_path_from_ipa_to_pcie) {
			cfg->lower = lower;
			cfg->upper = upper;
		} else {
			cfg->lower = msi->start + (n * 0x8) + (lower & 0xfff);
			cfg->upper = 0;
		}
		cfg->data = data;
		cfg->msg_num = (cap >> 20) & 0x7;
		/* Total number of MSI vectors supported {0 to ((2^n)-1)} */
		cfg->msg_num = ((1 << cfg->msg_num) - 1);
		if (ep_pcie_dev.use_iatu_msi) {
			if ((lower != msi_cfg->lower)
				|| (upper != msi_cfg->upper)
				|| (data != msi_cfg->data)
				|| (cfg->msg_num != msi_cfg->msg_num)) {
				changes++;
				EP_PCIE_DBG(&ep_pcie_dev,
					"PCIe V%d: MSI config has been changed by host side for %d time(s)\n",
					ep_pcie_dev.rev, changes);
				msi_cfg->lower = lower;
				msi_cfg->upper = upper;
				msi_cfg->data = data;
				ep_pcie_dev.conf_ipa_msi_iatu[vf_id] = false;
				EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: MSI CAP:0x%x\n",
					ep_pcie_dev.rev, cap);
				EP_PCIE_DBG(&ep_pcie_dev,
					"PCIe V%d: New MSI cfg: lower:0x%x; upper:0x%x; data:0x%x; msg_num:0x%x\n",
					ep_pcie_dev.rev, msi_cfg->lower,
					msi_cfg->upper,
					msi_cfg->data,
					msi_cfg->msg_num);
			}
		}
		/*
		 * All transactions originating from IPA have the RO
		 * bit set by default. Setup another ATU region to clear
		 * the RO bit for MSIs triggered via IPA DMA.
		 */
		if (ep_pcie_dev.no_path_from_ipa_to_pcie ||
		(ep_pcie_dev.active_config &&
		!ep_pcie_dev.conf_ipa_msi_iatu[vf_id])) {
			ep_pcie_config_outbound_iatu_entry(&ep_pcie_dev,
				EP_PCIE_OATU_INDEX_IPA_MSI,
				vf_id,
				lower, 0,
				(lower + resource_size(msi) - 1),
				lower, upper);
			ep_pcie_dev.conf_ipa_msi_iatu[vf_id] = true;
			EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: Conf iATU for IPA MSI info: lower:0x%x; upper:0x%x\n",
				ep_pcie_dev.rev, lower, upper);
		}
		return 0;
	}

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: MSI is not enabled yet or not supported\n",
		ep_pcie_dev.rev);
	return -EOPNOTSUPP;
}

int ep_pcie_core_trigger_msix(u32 idx, u32 vf_id)
{
	ep_pcie_dev.msix_counter++;
	EP_PCIE_DUMP(&ep_pcie_dev,
		"PCIe V%d: No. %ld MSIx fired for IRQ %d vf_id:%d ;active-config is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.msix_counter,
		idx, vf_id,
		ep_pcie_dev.active_config ? "" : "not");

	idx = ep_pcie_core_msix_db_val(idx, vf_id);
	ep_pcie_write_reg(ep_pcie_dev.dm_core,
			ep_pcie_dev.msix_cap + PCIE20_MSIX_DOORBELL_OFF_REG, idx);
	return 0;
}

int ep_pcie_core_trigger_msi(u32 idx, u32 vf_id)
{
	u32 addr, data, ctrl_reg;
	u32 status;
	void __iomem *dbi = ep_pcie_dev.dm_core;
	void __iomem *msi = ep_pcie_dev.msi;
	u32 n = 0;
	u32 msix_cap = ep_pcie_dev.msix_cap;
	int max_poll = MSI_EXIT_L1SS_WAIT_MAX_COUNT;

	if (ep_pcie_dev.link_status == EP_PCIE_LINK_DISABLED) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: PCIe link is currently disabled\n",
			ep_pcie_dev.rev);
		return EP_PCIE_ERROR;
	}

	if (vf_id) {
		n = vf_id - 1;
		dbi = ep_pcie_dev.dm_core_vf;
	}

	if (msix_cap) {
		ctrl_reg = readl_relaxed(dbi + msix_cap + PCIE20_MSIX_CAP_ID_NEXT_CTRL_REG(n));
		if (ctrl_reg & BIT(31))
			return ep_pcie_core_trigger_msix(idx, vf_id);
		EP_PCIE_DUMP(&ep_pcie_dev,
			"PCIe V%d: MSIx capable , but not enabled\n", ep_pcie_dev.rev);
	}

	if (vf_id) {
		msi = ep_pcie_dev.msi_vf;
		if (!ep_pcie_dev.parf_msi_vf_indexed) {
			/* Shift idx to the vf postion to generate msi */
			idx = idx << (8 + (n*5));
		}

		/* Update msi virtual-function number field */
		idx |= n << 6;
		/* Set bit(5) to activate virtual function usage */
		idx |= BIT(5);
	}

	addr = readl_relaxed(dbi + PCIE20_MSI_LOWER(n));
	data = readl_relaxed(dbi + PCIE20_MSI_DATA(n));
	ctrl_reg = readl_relaxed(dbi + PCIE20_MSI_CAP_ID_NEXT_CTRL(n));

	if (ctrl_reg & BIT(16)) {
		ep_pcie_dev.msi_counter++;
		EP_PCIE_DUMP(&ep_pcie_dev,
			"PCIe V%d: No. %ld MSI fired for IRQ %d; index from client:%d; active-config is %s enabled\n",
			ep_pcie_dev.rev, ep_pcie_dev.msi_counter,
			data + idx, idx,
			ep_pcie_dev.active_config ? "" : "not");

		if (ep_pcie_dev.active_config) {
			if (ep_pcie_dev.use_iatu_msi) {
				/*
				 * On targets which supports SRIOV, configuration of outbound
				 * IATU for MSI host address leads to ECPRI transfers going
				 * via outbound IATU leading to generation on PF BDF always.
				 * This happens as we configure outbound IATU for entire host
				 * physical address range as shared via MHI.
				 * PARF based MSI are safer to use along with ECPRI based MSI as
				 * it doesn't require outbound IATU configuration.
				 */
				EP_PCIE_DBG2(&ep_pcie_dev,
						"PCIe V%d: try to trigger MSI by direct address write as well\n",
						ep_pcie_dev.rev);
				ep_pcie_write_reg(msi + (vf_id * 0x8), addr & 0xfff,
							data + idx);
			} else {
				EP_PCIE_DBG2(&ep_pcie_dev,
					"PCIe V%d: try to trigger MSI by PARF_MSI_GEN\n",
					ep_pcie_dev.rev);
				ep_pcie_write_reg(ep_pcie_dev.parf,
					PCIE20_PARF_MSI_GEN, idx);
				status = readl_relaxed(ep_pcie_dev.parf +
					PCIE20_PARF_LTR_MSI_EXIT_L1SS);
				while ((status & BIT(1)) && (max_poll-- > 0)) {
					udelay(MSI_EXIT_L1SS_WAIT);
					status = readl_relaxed(ep_pcie_dev.parf
						+
						PCIE20_PARF_LTR_MSI_EXIT_L1SS);
				}
				if (max_poll == 0)
					EP_PCIE_DBG2(&ep_pcie_dev,
						"PCIe V%d: MSI_EXIT_L1SS is not cleared yet\n",
						ep_pcie_dev.rev);
				else
					EP_PCIE_DBG2(&ep_pcie_dev,
						"PCIe V%d: MSI_EXIT_L1SS has been cleared\n",
						ep_pcie_dev.rev);
			}
		} else {
			ep_pcie_write_reg(msi + (vf_id * 0x8), addr & 0xfff, data
						+ idx);
		}
		return 0;
	}

	EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: MSI is disabled or not supported\n",
				ep_pcie_dev.rev);
	return -EOPNOTSUPP;
}

static void ep_pcie_core_issue_inband_pme(void)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	u32 pm_ctrl = 0;

	EP_PCIE_DBG(dev,
		"PCIe V%d: request to assert inband wake\n",
		dev->rev);

	pm_ctrl = readl_relaxed(dev->parf + PCIE20_PARF_PM_CTRL);
	ep_pcie_write_reg(dev->parf, PCIE20_PARF_PM_CTRL,
						(pm_ctrl | BIT(4)));
	ep_pcie_write_reg(dev->parf, PCIE20_PARF_PM_CTRL, pm_ctrl);

	EP_PCIE_DBG(dev,
		"PCIe V%d: completed assert for inband wake\n",
		dev->rev);
}
static int ep_pcie_core_wakeup_host_internal(enum ep_pcie_event event)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;

	if (atomic_read(&dev->host_wake_pending)) {
		EP_PCIE_DBG(dev, "PCIe V%d: Host wake is already pending, returning\n", dev->rev);
		return 0;
	}

	if (!atomic_read(&dev->perst_deast)) {
		if (event == EP_PCIE_EVENT_INVALID)
			EP_PCIE_DBG(dev, "PCIe V%d: Wake from DMA Call Back\n", dev->rev);
		/*D3 cold handling*/
		ep_pcie_core_toggle_wake_gpio(true);
		atomic_set(&dev->host_wake_pending, 1);
	} else if (dev->l23_ready) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: request to assert WAKE# when in D3hot\n",
			dev->rev);
		/*D3 hot handling*/
		ep_pcie_core_issue_inband_pme();
		atomic_set(&dev->host_wake_pending, 1);
	} else {
		/*D0 handling*/
		EP_PCIE_DBG(dev,
			"PCIe V%d: request to assert WAKE# when in D0\n",
			dev->rev);
	}


	EP_PCIE_DBG(dev,
		"PCIe V%d: Set wake pending : %d and return ; perst is %s de-asserted; D3hot is %s set\n",
		dev->rev, atomic_read(&dev->host_wake_pending),
		atomic_read(&dev->perst_deast) ? "" : "not",
		dev->l23_ready ? "" : "not");
	return 0;

}
static int ep_pcie_core_wakeup_host(enum ep_pcie_event event)
{
	unsigned long irqsave_flags;
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;

	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	ep_pcie_core_wakeup_host_internal(event);
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);
	return 0;
}

int ep_pcie_core_config_db_routing(struct ep_pcie_db_config chdb_cfg,
				struct ep_pcie_db_config erdb_cfg, u32 vf_id)
{
	u32 dbs = (erdb_cfg.end << 24) | (erdb_cfg.base << 16) |
			(chdb_cfg.end << 8) | chdb_cfg.base;
	u32 n = vf_id - 1;

	if (!vf_id) {
		ep_pcie_write_reg(ep_pcie_dev.parf, PCIE20_PARF_MHI_IPA_DBS, dbs);
		ep_pcie_write_reg(ep_pcie_dev.parf,
				PCIE20_PARF_MHI_IPA_CDB_TARGET_LOWER,
				chdb_cfg.tgt_addr);
		ep_pcie_write_reg(ep_pcie_dev.parf,
				PCIE20_PARF_MHI_IPA_EDB_TARGET_LOWER,
				erdb_cfg.tgt_addr);
	} else {
		if (ep_pcie_dev.db_fwd_off_varied) {
			ep_pcie_write_reg(ep_pcie_dev.parf, PCIE20_PARF_MHI_IPA_DBS_VF(n), dbs);
			ep_pcie_write_reg(ep_pcie_dev.parf,
					PCIE20_PARF_MHI_IPA_CDB_VF_TARGET_LOWER(n),
					chdb_cfg.tgt_addr);
			ep_pcie_write_reg(ep_pcie_dev.parf,
					PCIE20_PARF_MHI_IPA_EDB_VF_TARGET_LOWER(n),
					erdb_cfg.tgt_addr);
		} else {
			ep_pcie_write_reg(ep_pcie_dev.parf, PCIE20_PARF_MHI_IPA_DBS_V1_VF(n), dbs);
			ep_pcie_write_reg(ep_pcie_dev.parf,
					PCIE20_PARF_MHI_IPA_CDB_V1_VF_TARGET_LOWER(n),
					chdb_cfg.tgt_addr);
			ep_pcie_write_reg(ep_pcie_dev.parf,
					PCIE20_PARF_MHI_IPA_EDB_V1_VF_TARGET_LOWER(n),
					erdb_cfg.tgt_addr);
		}
	}

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: DB routing info: chdb_cfg.base:0x%x; chdb_cfg.end:0x%x; erdb_cfg.base:0x%x; erdb_cfg.end:0x%x; chdb_cfg.tgt_addr:0x%x; erdb_cfg.tgt_addr:0x%x\n",
		ep_pcie_dev.rev, chdb_cfg.base, chdb_cfg.end, erdb_cfg.base,
		erdb_cfg.end, chdb_cfg.tgt_addr, erdb_cfg.tgt_addr);

	return 0;
}

static int ep_pcie_core_panic_reboot_callback(struct notifier_block *nb,
					   unsigned long reason, void *arg)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;
	u32 mhi_syserr = BIT(2)|(0xff << 8);
	unsigned long irqsave_flags;

	if (!ep_pcie_dev.avoid_reboot_in_d3hot)
		goto out;

	/* If the device is in D3hot state, bring it to D0 */
	spin_lock_irqsave(&dev->isr_lock, irqsave_flags);
	if (dev->l23_ready && atomic_read(&dev->perst_deast)) {

		EP_PCIE_INFO(dev,
			"PCIe V%d got %s notification while in D3hot\n",
			dev->rev, reason ? "reboot":"panic/die");

		/* Set MHI to SYSERR state */
		if (dev->config_mmio_init)
			ep_pcie_write_reg(dev->mmio, PCIE20_MHISTATUS,
						mhi_syserr);
		/* Bring device out of D3hot */
		ep_pcie_core_issue_inband_pme();
	}
	spin_unlock_irqrestore(&dev->isr_lock, irqsave_flags);

out:
	return NOTIFY_DONE;
}

static struct notifier_block ep_pcie_core_reboot_notifier = {
	.notifier_call	= ep_pcie_core_panic_reboot_callback,
};

static struct notifier_block ep_pcie_core_die_notifier = {
	.notifier_call	= ep_pcie_core_panic_reboot_callback,
};

static struct notifier_block ep_pcie_core_panic_notifier = {
	.notifier_call	= ep_pcie_core_panic_reboot_callback,
};

static int ep_pcie_core_get_cap(struct ep_pcie_cap *ep_cap)
{
	u32 ctrl_reg;
	void __iomem *dbi = ep_pcie_dev.dm_core;

	if (ep_pcie_dev.link_status == EP_PCIE_LINK_DISABLED) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: PCIe link is currently disabled\n",
			ep_pcie_dev.rev);
		return EP_PCIE_ERROR;
	}

	if (ep_pcie_dev.msix_cap) {
		ctrl_reg = readl_relaxed(dbi + ep_pcie_dev.msix_cap);
		if (ctrl_reg & BIT(31))
			ep_cap->msix_enabled = true;
	}

	if (ep_pcie_dev.sriov_cap) {
		ep_cap->sriov_enabled = true;
		ep_cap->num_vfs = ep_pcie_dev.num_vfs;
	}

	return 0;
}

struct ep_pcie_hw hw_drv = {
	.register_event	= ep_pcie_core_register_event,
	.deregister_event = ep_pcie_core_deregister_event,
	.get_linkstatus = ep_pcie_core_get_linkstatus,
	.config_outbound_iatu = ep_pcie_core_config_outbound_iatu,
	.get_msi_config = ep_pcie_core_get_msi_config,
	.trigger_msi = ep_pcie_core_trigger_msi,
	.wakeup_host = ep_pcie_core_wakeup_host,
	.config_db_routing = ep_pcie_core_config_db_routing,
	.enable_endpoint = ep_pcie_core_enable_endpoint,
	.disable_endpoint = ep_pcie_core_disable_endpoint,
	.mask_irq_event = ep_pcie_core_mask_irq_event,
	.configure_inactivity_timer = ep_pcie_core_config_inact_timer,
	.get_capability = ep_pcie_core_get_cap,
};

/*
 * is_pcie_boot_config - reads boot_config register using
 * nvmem interface and determines if host-interface is pcie or not.
 * @pdev: pointer to ep_pcie device.
 *
 * Return 0 on success, negative number on error.
 */
static int is_pcie_boot_config(struct platform_device *pdev)
{
	struct nvmem_cell *cell = NULL;
	u32 *buf, fast_boot, host_bypass, fast_boot_mask = 0, host_bypass_mask = 0;
	size_t len = 0;
	int ret = 0, num_fast_boot_values = 0, i;
	u32 fast_boot_values[MAX_FAST_BOOT_VALUES];

	/*
	 * BOOT_CONFIG is a FUSE based register.
	 * This register allows to read the proper PBL related values.
	 * Using nvmem interface to read boot_config register and
	 * using bitmask (provided through devicetree) for
	 * masking fast_boot and pcie_host_bypass.
	 */
	if (!of_find_property((&pdev->dev)->of_node, "nvmem-cells", NULL))
		return 0;

	ret = of_property_read_u32((&pdev->dev)->of_node, "qcom,fast-boot-mask", &fast_boot_mask);
	if (ret) {
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: qcom,fast-boot-mask does not exist\n",
			ep_pcie_dev.rev);
		return ret;
	}

	ret = of_property_read_u32((&pdev->dev)->of_node, "qcom,host-bypass-mask",
			&host_bypass_mask);
	if (ret) {
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: qcom,host-bypass-mask does not exist\n",
			ep_pcie_dev.rev);
		return ret;
	}

	if (!host_bypass_mask || !fast_boot_mask) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: host_bypass_mask and fast_boot_mask should be non-zero\n",
			ep_pcie_dev.rev);
		return -EINVAL;
	}

	if (!of_get_property((&pdev->dev)->of_node, "qcom,fast-boot-values", NULL))
		return -EINVAL;

	num_fast_boot_values = of_property_count_elems_of_size((&pdev->dev)->of_node,
			"qcom,fast-boot-values", sizeof(u32));
	if ((num_fast_boot_values < 0) || (num_fast_boot_values > MAX_FAST_BOOT_VALUES)) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: qcom,fast-boot-values not valid\n", ep_pcie_dev.rev);
		return num_fast_boot_values;
	}

	ret = of_property_read_u32_array((&pdev->dev)->of_node, "qcom,fast-boot-values",
			fast_boot_values, num_fast_boot_values);
	if (ret) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: qcom,fast-boot-values not found\n", ep_pcie_dev.rev);
		return ret;
	}

	cell = nvmem_cell_get(&pdev->dev, "boot_conf");
	if (IS_ERR(cell)) {
		EP_PCIE_ERR(&ep_pcie_dev, "PCIe V%d: Error in reading BOOT_CONFIG cell\n",
			ep_pcie_dev.rev);
		return PTR_ERR(cell);
	}

	buf = nvmem_cell_read(cell, &len);
	if (IS_ERR(buf)) {
		EP_PCIE_ERR(&ep_pcie_dev, "PCIe V%d: Error in reading BOOT_CONFIG\n",
			ep_pcie_dev.rev);
		nvmem_cell_put(cell);
		return PTR_ERR(buf);
	}

	fast_boot = (((*buf) & fast_boot_mask) >> ((ffs(fast_boot_mask)) - 1));
	host_bypass = (((*buf) & host_bypass_mask) >> ((ffs(host_bypass_mask)) - 1));
	EP_PCIE_INFO(&ep_pcie_dev,
		"PCIe V%d: BOOT_CONFIG val = %x, fast_boot = %x, host_bypass = %x\n",
		ep_pcie_dev.rev, (*buf), fast_boot, host_bypass);
	kfree(buf);
	nvmem_cell_put(cell);

	for (i = 0; i < num_fast_boot_values; i++) {
		if (fast_boot == fast_boot_values[i] && !host_bypass)
			return 0;
	}

	return -EPERM;
}

static void ep_pcie_tcsr_aoss_data_dt(struct platform_device *pdev)
{
	int ret;

	ep_pcie_dev.tcsr_not_supported = of_property_read_bool((&pdev->dev)->of_node,
								"qcom,tcsr-not-supported");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: tcsr pcie perst is %s supported\n",
		ep_pcie_dev.rev, ep_pcie_dev.tcsr_not_supported ? "not" : "");

	if (!ep_pcie_dev.tcsr_not_supported) {
		ret = of_property_read_u32((&pdev->dev)->of_node,
					"qcom,tcsr-perst-separation-enable-offset",
					&ep_pcie_dev.tcsr_perst_separation_en_offset);
		if (ret) {
			EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: TCSR Perst Separation En Offset is not supplied from DT",
				ep_pcie_dev.rev);
			ep_pcie_dev.tcsr_perst_separation_en_offset = TCSR_PERST_SEPARATION_ENABLE;
		}

		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: TCSR Perst Separation En Offset: 0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.tcsr_perst_separation_en_offset);

		ret = of_property_read_u32((&pdev->dev)->of_node,
					"qcom,tcsr-reset-separation-offset",
					&ep_pcie_dev.tcsr_reset_separation_offset);
		if (ret) {
			EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: TCSR Reset Separation Offset is not supplied from DT",
				ep_pcie_dev.rev);
			ep_pcie_dev.tcsr_reset_separation_offset = TCSR_PCIE_RST_SEPARATION;
		}

		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: TCSR Reset Separation Offset: 0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.tcsr_reset_separation_offset);

		ret = of_property_read_u32((&pdev->dev)->of_node, "qcom,pcie-disconnect-req-reg-b",
					&ep_pcie_dev.pcie_disconnect_req_reg_mask);

		ep_pcie_dev.pcie_disconnect_req_reg_mask =
						BIT(ep_pcie_dev.pcie_disconnect_req_reg_mask);
		if (ret) {
			EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: Pcie disconnect req reg bit is not supplied from DT",
				ep_pcie_dev.rev);
			ep_pcie_dev.pcie_disconnect_req_reg_mask = PCIE_DISCONNECT_REQ_REG;
		}

		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: Pcie disconnect req reg Mask: 0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.pcie_disconnect_req_reg_mask);

		ret = of_property_read_u32((&pdev->dev)->of_node, "qcom,tcsr-perst-enable-offset",
					&ep_pcie_dev.tcsr_perst_enable_offset);
		if (ret) {
			EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: TCSR Perst Enable Offset is not supplied from DT",
				ep_pcie_dev.rev);
			ep_pcie_dev.tcsr_perst_enable_offset = TCSR_PCIE_PERST_EN;
		}

		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: TCSR Perst Enable Offset: 0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.tcsr_perst_enable_offset);

		ret = of_property_read_u32((&pdev->dev)->of_node, "qcom,tcsr-hot-reset-en-offset",
					&ep_pcie_dev.tcsr_hot_reset_en_offset);
		if (ret) {
			EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: TCSR Hot Reset Enable Offset is not supplied from DT",
				ep_pcie_dev.rev);
			ep_pcie_dev.tcsr_hot_reset_en_offset = TCSR_HOT_RESET_EN;
		}

		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: TCSR Hot Reset Enable Offset: 0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.tcsr_hot_reset_en_offset);
	}

	ep_pcie_dev.aoss_rst_clear = of_property_read_bool((&pdev->dev)->of_node,
								"qcom,aoss-rst-clr");

	if (ep_pcie_dev.aoss_rst_clear) {
		ret = of_property_read_u32((&pdev->dev)->of_node,
					"qcom,perst-raw-rst-status-b",
					&ep_pcie_dev.perst_raw_rst_status_mask);

		ep_pcie_dev.perst_raw_rst_status_mask = BIT(ep_pcie_dev.perst_raw_rst_status_mask);
		if (ret) {
			EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: Perst raw rst status bit is not supplied from DT",
				ep_pcie_dev.rev);
			ep_pcie_dev.perst_raw_rst_status_mask = PERST_RAW_RESET_STATUS;
		}

		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: Perst Raw Reset Status Mask: 0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.perst_raw_rst_status_mask);
	}
}

static int ep_pcie_probe(struct platform_device *pdev)
{
	int ret, num_ipc_pages_dev_fac;
	u32 sriov_mask = 0;
	char logname[MAX_NAME_LEN];

	ret = is_pcie_boot_config(pdev);
	if (ret) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: boot_config is not PCIe\n",
			ep_pcie_dev.rev);
		goto res_failure;
	}

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,ep-pcie-num-ipc-pages-dev-fac",
				&num_ipc_pages_dev_fac);
	if (ret) {
		pr_err("qcom,ep-pcie-num-ipc-pages-dev-fac does not exist\n");
		num_ipc_pages_dev_fac = 1;
	}
	if (num_ipc_pages_dev_fac < 1 || num_ipc_pages_dev_fac > 5) {
		pr_err("Invalid value received for num_ipc_pages_dev_fac\n");
		num_ipc_pages_dev_fac = 1;
	}

	snprintf(logname, MAX_NAME_LEN, "ep-pcie-long");
	ep_pcie_dev.ipc_log_sel =
		ipc_log_context_create(EP_PCIE_LOG_PAGES/num_ipc_pages_dev_fac, logname, 0);
	if (ep_pcie_dev.ipc_log_sel == NULL)
		pr_err("%s: unable to create IPC selected log for %s\n",
			__func__, logname);
	else
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: IPC selected logging is enable for %s\n",
			ep_pcie_dev.rev, logname);

	snprintf(logname, MAX_NAME_LEN, "ep-pcie-short");
	ep_pcie_dev.ipc_log_ful =
		ipc_log_context_create((EP_PCIE_LOG_PAGES * 2)/num_ipc_pages_dev_fac, logname, 0);
	if (ep_pcie_dev.ipc_log_ful == NULL)
		pr_err("%s: unable to create IPC detailed log for %s\n",
			__func__, logname);
	else
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: IPC detailed logging is enable for %s\n",
			ep_pcie_dev.rev, logname);

	snprintf(logname, MAX_NAME_LEN, "ep-pcie-dump");
	ep_pcie_dev.ipc_log_dump =
		ipc_log_context_create(EP_PCIE_LOG_PAGES, logname, 0);
	if (ep_pcie_dev.ipc_log_dump == NULL)
		pr_err("%s: unable to create IPC dump log for %s\n",
			__func__, logname);
	else
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: IPC dump logging is enable for %s\n",
			ep_pcie_dev.rev, logname);

	ep_pcie_dev.link_speed = 1;
	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,pcie-link-speed",
				&ep_pcie_dev.link_speed);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: pcie-link-speed does not exist\n",
			ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: pcie-link-speed:%d\n",
			ep_pcie_dev.rev, ep_pcie_dev.link_speed);

	ep_pcie_dev.vendor_id = 0xFFFF;
	ret = of_property_read_u16((&pdev->dev)->of_node,
				"qcom,pcie-vendor-id",
				&ep_pcie_dev.vendor_id);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: pcie-vendor-id does not exist.\n",
				ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: pcie-vendor-id:%d.\n",
				ep_pcie_dev.rev, ep_pcie_dev.vendor_id);

	ep_pcie_dev.device_id = 0xFFFF;
	ret = of_property_read_u16((&pdev->dev)->of_node,
				"qcom,pcie-device-id",
				&ep_pcie_dev.device_id);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
				"PCIe V%d: pcie-device-id does not exist.\n",
				ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: pcie-device-id:%d.\n",
				ep_pcie_dev.rev, ep_pcie_dev.device_id);

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,dbi-base-reg",
				&ep_pcie_dev.dbi_base_reg);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: dbi-base-reg does not exist\n",
			ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: dbi-base-reg:0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.dbi_base_reg);

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,slv-space-reg",
				&ep_pcie_dev.slv_space_reg);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: slv-space-reg does not exist\n",
			ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: slv-space-reg:0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.slv_space_reg);

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,phy-status-reg",
				&ep_pcie_dev.phy_status_reg);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: phy-status-reg does not exist\n",
			ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: phy-status-reg:0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.phy_status_reg);

	ep_pcie_dev.phy_status_bit_mask_bit = BIT(6);

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,phy-status-reg2",
				&ep_pcie_dev.phy_status_reg);
	if (ret) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: phy-status-reg2 does not exist\n",
			ep_pcie_dev.rev);
	} else {
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: phy-status-reg2:0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.phy_status_reg);
		ep_pcie_dev.phy_status_bit_mask_bit = BIT(7);
	}

	ep_pcie_dev.phy_rev = 1;
	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,pcie-phy-ver",
				&ep_pcie_dev.phy_rev);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: pcie-phy-ver does not exist\n",
			ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: pcie-phy-ver:%d\n",
			ep_pcie_dev.rev, ep_pcie_dev.phy_rev);

	ep_pcie_dev.pcie_edma = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,pcie-edma");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: pcie edma is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.pcie_edma ? "" : "not");

	ep_pcie_dev.active_config = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,pcie-active-config");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: active config is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.active_config ? "" : "not");

	ep_pcie_dev.aggregated_irq =
		of_property_read_bool((&pdev->dev)->of_node,
				"qcom,pcie-aggregated-irq");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: aggregated IRQ is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.aggregated_irq ? "" : "not");

	ep_pcie_dev.mhi_a7_irq =
		of_property_read_bool((&pdev->dev)->of_node,
				"qcom,pcie-mhi-a7-irq");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: Mhi a7 IRQ is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.mhi_a7_irq ? "" : "not");

	ep_pcie_dev.perst_enum = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,pcie-perst-enum");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: enum by PERST is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.perst_enum ? "" : "not");

	ep_pcie_tcsr_aoss_data_dt(pdev);

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: AOSS reset for perst needed\n", ep_pcie_dev.rev);
	ep_pcie_dev.parf_msi_vf_indexed = of_property_read_bool((&pdev->dev)->of_node,
							"qcom,pcie-parf-msi-vf-indexed");

	ep_pcie_dev.db_fwd_off_varied = of_property_read_bool(
						(&pdev->dev)->of_node,
						"qcom,db-fwd-off-varied");

	ep_pcie_dev.override_disable_sriov = of_property_read_bool((&pdev->dev)->of_node,
						"qcom,override-disable-sriov");
	ep_pcie_dev.rev = 1711211;
	ep_pcie_dev.pdev = pdev;
	ep_pcie_dev.m2_autonomous =
		of_property_read_bool((&pdev->dev)->of_node,
				"qcom,pcie-m2-autonomous");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: MHI M2 autonomous is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.m2_autonomous ? "" : "not");

	ep_pcie_dev.avoid_reboot_in_d3hot =
		of_property_read_bool((&pdev->dev)->of_node,
				"qcom,avoid-reboot-in-d3hot");
	EP_PCIE_DBG(&ep_pcie_dev,
	"PCIe V%d: PME during reboot/panic (in D3hot) is %s needed\n",
	ep_pcie_dev.rev, ep_pcie_dev.avoid_reboot_in_d3hot ? "" : "not");

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,mhi-soc-reset-offset",
				&ep_pcie_dev.mhi_soc_reset_offset);
	if (ret) {
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: qcom,mhi-soc-reset does not exist\n",
			ep_pcie_dev.rev);
	} else {
		EP_PCIE_DBG(&ep_pcie_dev, "PCIe V%d: soc-reset-offset:0x%x\n",
			ep_pcie_dev.rev, ep_pcie_dev.mhi_soc_reset_offset);
		ep_pcie_dev.mhi_soc_reset_en = true;
	}

	ep_pcie_dev.aux_clk_val = 0x14;
	ret = of_property_read_u32((&pdev->dev)->of_node, "qcom,aux-clk",
					&ep_pcie_dev.aux_clk_val);
	if (ret)
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: Using default value 19.2 MHz.\n",
				ep_pcie_dev.rev);
	else
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: Gen4 using aux_clk = 16.6 MHz\n",
				ep_pcie_dev.rev);

	ret = of_property_read_u32((&pdev->dev)->of_node, "qcom,sriov-mask",
					&sriov_mask);
	if (!ret) {
		ep_pcie_dev.sriov_mask = (unsigned long)sriov_mask;
		EP_PCIE_INFO(&ep_pcie_dev, "PCIe V%d: SR-IOV mask:0x%x\n",
			ep_pcie_dev.rev, sriov_mask);
	}

	ep_pcie_dev.no_path_from_ipa_to_pcie = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,no-path-from-ipa-to-pcie");
	EP_PCIE_INFO(&ep_pcie_dev, "Path from IPA to PCIe is %s present\n",
				ep_pcie_dev.no_path_from_ipa_to_pcie ? "not" : "");

	ep_pcie_dev.use_iatu_msi = of_property_read_bool((&pdev->dev)->of_node,
				"qcom,pcie-use-iatu-msi");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: pcie edma is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.use_iatu_msi ? "" : "not");

	ret = of_property_read_u32((&pdev->dev)->of_node,
				"qcom,pcie-cesta-clkreq-offset",
				&ep_pcie_dev.pcie_cesta_clkreq_offset);
	if (ret)
		ep_pcie_dev.pcie_cesta_clkreq_offset = 0;

	ep_pcie_dev.configure_hard_reset = of_property_read_bool((&pdev->dev)->of_node,
					"qcom,pcie-configure-hard-reset");
	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: pcie configure hard reset is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.configure_hard_reset ? "" : "not");

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: EP PCIe hot reset is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.hot_rst_disable ? "not" : "");

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: EP PCIe L1 is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.l1_disable ? "not" : "");

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: EP PCIe PERST Separation is %s enabled\n",
		ep_pcie_dev.rev, ep_pcie_dev.perst_sep_en ? "" : "not");

	memcpy(ep_pcie_dev.vreg, ep_pcie_vreg_info,
				sizeof(ep_pcie_vreg_info));
	memcpy(ep_pcie_dev.gpio, ep_pcie_gpio_info,
				sizeof(ep_pcie_gpio_info));
	memcpy(ep_pcie_dev.clk, ep_pcie_clk_info,
				sizeof(ep_pcie_clk_info));
	memcpy(ep_pcie_dev.pipeclk, ep_pcie_pipe_clk_info,
				sizeof(ep_pcie_pipe_clk_info));
	memcpy(ep_pcie_dev.reset, ep_pcie_reset_info,
				sizeof(ep_pcie_reset_info));
	memcpy(ep_pcie_dev.res, ep_pcie_res_info,
				sizeof(ep_pcie_res_info));
	memcpy(ep_pcie_dev.irq, ep_pcie_irq_info,
				sizeof(ep_pcie_irq_info));

	ret = ep_pcie_get_resources(&ep_pcie_dev,
				ep_pcie_dev.pdev);
	if (ret) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: failed to get resources\n",
			ep_pcie_dev.rev);
		goto res_failure;
	}

	ret = ep_pcie_gpio_init(&ep_pcie_dev);
	if (ret) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: failed to init GPIO\n",
			ep_pcie_dev.rev);
		ep_pcie_release_resources(&ep_pcie_dev);
		goto gpio_failure;
	}

	ret = ep_pcie_irq_init(&ep_pcie_dev);
	if (ret) {
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: failed to init IRQ\n",
			ep_pcie_dev.rev);
		ep_pcie_release_resources(&ep_pcie_dev);
		ep_pcie_gpio_deinit(&ep_pcie_dev);
		goto irq_failure;
	}

	/*
	 * Wakelock is needed to avoid missing BME and other
	 * interrupts if apps goes into suspend before host
	 * sets them.
	 */
	device_init_wakeup(&ep_pcie_dev.pdev->dev, true);
	atomic_set(&ep_pcie_dev.ep_pcie_dev_wake, 0);

	if (ep_pcie_dev.perst_enum &&
		!gpio_get_value(ep_pcie_dev.gpio[EP_PCIE_GPIO_PERST].num)) {
		EP_PCIE_DBG2(&ep_pcie_dev,
			"PCIe V%d: %s probe is done; link will be trained when PERST is deasserted\n",
		ep_pcie_dev.rev, dev_name(&(pdev->dev)));
		return 0;
	}

	EP_PCIE_DBG(&ep_pcie_dev,
		"PCIe V%d: %s got resources successfully; start turning on the link\n",
		ep_pcie_dev.rev, dev_name(&(pdev->dev)));

	ret = ep_pcie_enumeration(&ep_pcie_dev);
	if (ret == EP_PCIE_ERROR)
		EP_PCIE_ERR(&ep_pcie_dev,
				"PCIe V%d: Enumeration failed in probe, waiting for Perst deassert\n",
				ep_pcie_dev.rev);
	if (ret && !ep_pcie_debug_keep_resource & !ep_pcie_dev.perst_enum)
		goto irq_deinit;

	register_reboot_notifier(&ep_pcie_core_reboot_notifier);
	/* Handler for wilful crash like BUG_ON */
	register_die_notifier(&ep_pcie_core_die_notifier);
	atomic_notifier_chain_register(&panic_notifier_list,
				       &ep_pcie_core_panic_notifier);

	qcom_edma_init(&pdev->dev);

	if (!ep_pcie_dev.perst_enum)
		enable_irq(ep_pcie_dev.irq[EP_PCIE_INT_GLOBAL].num);
	return 0;

irq_deinit:
	ep_pcie_irq_deinit(&ep_pcie_dev);
irq_failure:
	ep_pcie_gpio_deinit(&ep_pcie_dev);
gpio_failure:
	ep_pcie_release_resources(&ep_pcie_dev);
res_failure:
	EP_PCIE_ERR(&ep_pcie_dev, "PCIe V%d: Driver probe failed:%d\n",
		ep_pcie_dev.rev, ret);

	return ret;
}

static int __exit ep_pcie_remove(struct platform_device *pdev)
{

	unregister_reboot_notifier(&ep_pcie_core_reboot_notifier);
	unregister_die_notifier(&ep_pcie_core_die_notifier);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &ep_pcie_core_panic_notifier);

	ep_pcie_irq_deinit(&ep_pcie_dev);
	ep_pcie_vreg_deinit(&ep_pcie_dev);
	ep_pcie_pipe_clk_deinit(&ep_pcie_dev);
	ep_pcie_clk_deinit(&ep_pcie_dev);
	ep_pcie_gpio_deinit(&ep_pcie_dev);
	ep_pcie_release_resources(&ep_pcie_dev);
	ep_pcie_deregister_drv(&hw_drv);

	return 0;
}

static const struct of_device_id ep_pcie_match[] = {
	{	.compatible = "qcom,pcie-ep",
	},
	{}
};

static int ep_pcie_suspend_noirq(struct device *pdev)
{
	struct ep_pcie_dev_t *dev = &ep_pcie_dev;


	/* Allow suspend if autonomous M2 is enabled  */
	if (dev->m2_autonomous) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: Autonomous M2 is enabled, allow suspend\n",
			dev->rev);
		return 0;
	}

	/* Allow suspend only after D3 cold is received */
	if (atomic_read(&dev->perst_deast)) {
		EP_PCIE_DBG(dev,
			"PCIe V%d: Perst not asserted, fail suspend\n",
			dev->rev);
		return -EBUSY;
	}

	EP_PCIE_DBG(dev,
		"PCIe V%d: Perst asserted, allow suspend\n",
		dev->rev);

	return 0;
}

static const struct dev_pm_ops ep_pcie_pm_ops = {
	.suspend_noirq = ep_pcie_suspend_noirq,
};

static struct platform_driver ep_pcie_driver = {
	.probe	= ep_pcie_probe,
	.remove	= ep_pcie_remove,
	.driver	= {
		.name		= "pcie-ep",
		.pm             = &ep_pcie_pm_ops,
		.of_match_table	= ep_pcie_match,
	},
};

static int __init ep_pcie_hot_reset(char *str)
{
	if (!strcmp(str, "disable_hot_reset"))
		ep_pcie_dev.hot_rst_disable = true;
	pr_err("%s hot_rst_disable:%d\n", __func__, ep_pcie_dev.hot_rst_disable);
	return 0;
}
early_param("ep_pcie_hot_rst", ep_pcie_hot_reset);

static int __init ep_pcie_perst_sep(char *str)
{
	if (!strcmp(str, "enable_perst_sep"))
		ep_pcie_dev.perst_sep_en = true;
	pr_err("%s perst_sep_en:%d\n", __func__, ep_pcie_dev.perst_sep_en);
	return 0;
}
early_param("ep_pcie_perst_sep", ep_pcie_perst_sep);

static int __init ep_pcie_l1_disable(char *str)
{
	if (!strcmp(str, "disable_l1"))
		ep_pcie_dev.l1_disable = true;
	pr_err("%s l1_disable:%d\n", __func__, ep_pcie_dev.l1_disable);
	return 0;
}
early_param("ep_pcie_l1_cfg", ep_pcie_l1_disable);

static int __init ep_pcie_init(void)
{
	int ret;

	mutex_init(&ep_pcie_dev.setup_mtx);
	mutex_init(&ep_pcie_dev.ext_mtx);
	spin_lock_init(&ep_pcie_dev.ext_lock);
	spin_lock_init(&ep_pcie_dev.isr_lock);

	ep_pcie_debugfs_init(&ep_pcie_dev);

	ret = platform_driver_register(&ep_pcie_driver);

	if (ret)
		EP_PCIE_ERR(&ep_pcie_dev,
			"PCIe V%d: failed register platform driver:%d\n",
			ep_pcie_dev.rev, ret);
	else
		EP_PCIE_DBG(&ep_pcie_dev,
			"PCIe V%d: platform driver is registered\n",
			ep_pcie_dev.rev);

	return ret;
}

static void __exit ep_pcie_exit(void)
{

	ipc_log_context_destroy(ep_pcie_dev.ipc_log_sel);
	ipc_log_context_destroy(ep_pcie_dev.ipc_log_ful);
	ipc_log_context_destroy(ep_pcie_dev.ipc_log_dump);

	ep_pcie_debugfs_exit();

	platform_driver_unregister(&ep_pcie_driver);
}

subsys_initcall(ep_pcie_init);
module_exit(ep_pcie_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MSM PCIe Endpoint Driver");
