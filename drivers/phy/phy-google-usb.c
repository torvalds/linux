// SPDX-License-Identifier: GPL-2.0
/*
 * phy-google-usb.c - Google USB PHY driver
 *
 * Copyright (C) 2025, Google LLC
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/typec_mux.h>

/* USB_CFG_CSR */
#define USBCS_USB2PHY_CFG19_OFFSET 0x0
#define USBCS_USB2PHY_CFG19_PHY_CFG_PLL_FB_DIV GENMASK(19, 8)

#define USBCS_USB2PHY_CFG21_OFFSET 0x8
#define USBCS_USB2PHY_CFG21_PHY_ENABLE BIT(12)
#define USBCS_USB2PHY_CFG21_REF_FREQ_SEL GENMASK(15, 13)
#define USBCS_USB2PHY_CFG21_PHY_TX_DIG_BYPASS_SEL BIT(19)

/* USBDP_TOP */
#define USBCS_PHY_CFG1_OFFSET 0x28
#define USBCS_PHY_CFG1_PHY0_MPLLA_SSC_EN BIT(1)
#define USBCS_PHY_CFG1_PHY0_SRAM_BYPASS_MODE GENMASK(11, 10)
#define SRAM_BYPASS_MODE_BYPASS_FIRMWARE BIT(0)
#define SRAM_BYPASS_MODE_BYPASS_CONTEXT BIT(1)
#define USBCS_PHY_CFG1_SYS_VBUSVALID BIT(17)

#define USBDP_TOP_CFG_REG_OFFSET 0x44
#define USBDP_TOP_CFG_REG_PMGT_REF_CLK_REQ_N BIT(0)

#define PHY_POWER_CONFIG_REG1_OFFSET 0x48
#define PHY_POWER_CONFIG_REG1_PG_MODE_EN BIT(1)
#define PHY_POWER_CONFIG_REG1_UPCS_PIPE_CONFIG GENMASK(31, 14)
#define UPCS_PIPE_CONFIG_ISO_CPM BIT(5)
#define UPCS_PIPE_CONFIG_PG_MODE_STATIC BIT(6)
#define UPCS_PIPE_CONFIG_LANE_RESET_NO_PG_EXIT BIT(9)

/* USB3_TCA */
#define TCA_INTR_STS_OFFSET 0x8
#define TCA_INTR_STS_XA_ACT_EVT BIT(0)
#define TCA_TCPC_OFFSET 0x14
#define TCA_TCPC_MUX_CONTROL GENMASK(2, 0)
#define TCA_TCPC_MUX_CONTROL_USB_ONLY 0x1
#define TCA_TCPC_CONNECTOR_ORIENTATION BIT(3)
#define TCA_TCPC_VALID BIT(4)
#define TCA_CTRLSYNCMODE_CFG1_OFFSET 0x24
#define TCA_CTRLSYNCMODE_CFG1_XA_TIMEOUT_VAL GENMASK(19, 0)
#define TCA_CTRLSYNCMODE_CFG1_XA_TIMEOUT_VAL_100MS 0x1e85
#define TCA_PSTATE_0_OFFSET 0x50
#define TCA_PSTATE_0_UPCS_LANE0_PHYSTATUS BIT(8)

#define GPHY_TCA_DELAY_US 10
#define GPHY_TCA_TIMEOUT_US 110000

enum google_usb_phy_id {
	GOOGLE_USB2_PHY,
	GOOGLE_USB3_PHY,
	GOOGLE_USB_PHY_NUM,
};

struct google_usb_phy_instance {
	struct google_usb_phy *parent;
	unsigned int index;
	struct phy *phy;
	unsigned int num_clks;
	struct clk_bulk_data *clks;
	unsigned int num_rsts;
	struct reset_control_bulk_data *rsts;
};

struct google_usb_phy_config {
	const char * const *clk_names;
	unsigned int num_clks;
	const char * const *rst_names;
	unsigned int num_rsts;
};

static const char * const u2phy_clk_names[] = {
	"usb2",
	"usb2_apb",
};

static const char * const u3phy_clk_names[] = {
	"usb3"
};

static const char * const u2phy_rst_names[] = {
	"usb2",
	"usb2_apb",
};

static const char * const u3phy_rst_names[] = {
	"usb3"
};

static const struct google_usb_phy_config phy_configs[GOOGLE_USB_PHY_NUM] = {
	[GOOGLE_USB2_PHY] = {
		.clk_names = u2phy_clk_names,
		.num_clks = ARRAY_SIZE(u2phy_clk_names),
		.rst_names = u2phy_rst_names,
		.num_rsts = ARRAY_SIZE(u2phy_rst_names),
	},
	[GOOGLE_USB3_PHY] = {
		.clk_names = u3phy_clk_names,
		.num_clks = ARRAY_SIZE(u3phy_clk_names),
		.rst_names = u3phy_rst_names,
		.num_rsts = ARRAY_SIZE(u3phy_rst_names),
	},
};

static inline void google_usb_phy_clk_disable(struct google_usb_phy_instance *inst)
{
	clk_bulk_disable_unprepare(inst->num_clks, inst->clks);
}

DEFINE_FREE(inst_clk_disable, struct google_usb_phy_instance *,
	    if (_T) google_usb_phy_clk_disable(_T))

static inline void google_usb_phy_rst_disable(struct google_usb_phy_instance *inst)
{
	reset_control_bulk_assert(inst->num_rsts, inst->rsts);
}

DEFINE_FREE(inst_rst_disable, struct google_usb_phy_instance *,
	    if (_T) google_usb_phy_rst_disable(_T))

/*
 * combo_phy_state
 *	COMBO_PHY_IDLE: The ComboPHY has been torn down and USB3 has not completed
 *			bringup
 *	COMBO_PHY_INIT_DONE: The ComboPHY bringup sequence is complete.
 *	COMBO_PHY_TCA_READY: The PoR => NC transition is complete, and the TCA can be
 *			     moved into USB.
 */
enum combo_phy_state {
	COMBO_PHY_IDLE,
	COMBO_PHY_INIT_DONE,
	COMBO_PHY_TCA_READY,
};

struct google_usb_phy {
	struct device *dev;
	struct regmap *usb_cfg_regmap;
	unsigned int usb2_cfg_offset;
	void __iomem *usbdp_top_base;
	void __iomem *usb3_tca_base;
	struct google_usb_phy_instance *insts;
	/*
	 * Protect phy registers from concurrent access, specifically via
	 * google_usb_set_orientation callback. phy_mutex also protects
	 * concurrent access to phy_state.
	 */
	struct mutex phy_mutex;
	struct typec_switch_dev *sw;
	enum typec_orientation orientation;
	enum combo_phy_state phy_state;
};

static void set_vbus_valid(struct google_usb_phy *gphy)
{
	u32 reg;

	reg = readl(gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
	if (gphy->orientation == TYPEC_ORIENTATION_NONE)
		reg &= ~USBCS_PHY_CFG1_SYS_VBUSVALID;
	else
		reg |= USBCS_PHY_CFG1_SYS_VBUSVALID;
	writel(reg, gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
}

static void set_sram_bypass(struct google_usb_phy *gphy, u32 bypass)
{
	u32 reg;

	reg = readl(gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
	reg &= ~USBCS_PHY_CFG1_PHY0_SRAM_BYPASS_MODE;
	reg |= FIELD_PREP(USBCS_PHY_CFG1_PHY0_SRAM_BYPASS_MODE, bypass);
	writel(reg, gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
}

static void set_pmgt_ref_clk_req_n(struct google_usb_phy *gphy, bool resume)
{
	u32 reg;

	reg = readl(gphy->usbdp_top_base + USBDP_TOP_CFG_REG_OFFSET);
	if (resume)
		reg |= USBDP_TOP_CFG_REG_PMGT_REF_CLK_REQ_N;
	else
		reg &= ~USBDP_TOP_CFG_REG_PMGT_REF_CLK_REQ_N;
	writel(reg, gphy->usbdp_top_base + USBDP_TOP_CFG_REG_OFFSET);
}

static inline void disable_pmgt_ref_clk_req_n(struct google_usb_phy *gphy)
{
	set_pmgt_ref_clk_req_n(gphy, false);
}

DEFINE_FREE(pmgt_ref_clk_req_n, struct google_usb_phy *, if (_T) disable_pmgt_ref_clk_req_n(_T))

static int wait_tca_xa_ack(struct google_usb_phy *gphy)
{
	int ret;
	u32 reg;

	ret = readl_poll_timeout(gphy->usb3_tca_base + TCA_INTR_STS_OFFSET,
				 reg, !!(reg & TCA_INTR_STS_XA_ACT_EVT),
				 GPHY_TCA_DELAY_US, GPHY_TCA_TIMEOUT_US);
	if (ret)
		dev_err(gphy->dev, "tca xa_ack timeout, ret=%d\n", ret);

	return ret;
}

static int program_tca_locked(struct google_usb_phy *gphy)
	   __must_hold(&gphy->phy_mutex)
{
	int ret;
	u32 reg;

	reg = readl(gphy->usb3_tca_base + TCA_INTR_STS_OFFSET);
	writel(reg, gphy->usb3_tca_base + TCA_INTR_STS_OFFSET);

	reg = readl(gphy->usb3_tca_base + TCA_TCPC_OFFSET);
	reg &= ~TCA_TCPC_MUX_CONTROL;
	reg |= FIELD_PREP(TCA_TCPC_MUX_CONTROL, TCA_TCPC_MUX_CONTROL_USB_ONLY);
	if (gphy->orientation == TYPEC_ORIENTATION_REVERSE)
		reg |= TCA_TCPC_CONNECTOR_ORIENTATION;
	else
		reg &= ~TCA_TCPC_CONNECTOR_ORIENTATION;
	reg |= TCA_TCPC_VALID;
	writel(reg, gphy->usb3_tca_base + TCA_TCPC_OFFSET);

	ret = wait_tca_xa_ack(gphy);
	dev_dbg(gphy->dev, "TCA switch %s, mux %lu, orientation %s\n",
		ret ? "failed" : "success",
		FIELD_GET(TCA_TCPC_MUX_CONTROL, reg),
		FIELD_GET(TCA_TCPC_CONNECTOR_ORIENTATION, reg) ? "reverse" : "normal");

	reg = readl(gphy->usb3_tca_base + TCA_INTR_STS_OFFSET);
	writel(reg, gphy->usb3_tca_base + TCA_INTR_STS_OFFSET);

	return ret;
}

static int google_usb_set_orientation(struct typec_switch_dev *sw,
				      enum typec_orientation orientation)
{
	struct google_usb_phy *gphy = typec_switch_get_drvdata(sw);
	int ret = 0;

	dev_dbg(gphy->dev, "set orientation %d\n", orientation);

	guard(mutex)(&gphy->phy_mutex);

	gphy->orientation = orientation;

	if (IS_ENABLED(CONFIG_PM)) {
		if (pm_runtime_get_if_active(gphy->dev) <= 0)
			return 0;
	}

	set_vbus_valid(gphy);

	if (gphy->phy_state == COMBO_PHY_TCA_READY && orientation != TYPEC_ORIENTATION_NONE)
		ret = program_tca_locked(gphy);

	pm_runtime_put(gphy->dev);

	return ret;
}

static int google_usb2_phy_init(struct phy *_phy)
{
	struct google_usb_phy_instance *inst = phy_get_drvdata(_phy);
	struct google_usb_phy *gphy = inst->parent;
	u32 reg;
	int ret;

	dev_dbg(gphy->dev, "initializing usb2 phy\n");

	guard(mutex)(&gphy->phy_mutex);

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, &reg);
	reg &= ~USBCS_USB2PHY_CFG21_PHY_TX_DIG_BYPASS_SEL;
	reg &= ~USBCS_USB2PHY_CFG21_REF_FREQ_SEL;
	reg |= FIELD_PREP(USBCS_USB2PHY_CFG21_REF_FREQ_SEL, 0);
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, reg);

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG19_OFFSET, &reg);
	reg &= ~USBCS_USB2PHY_CFG19_PHY_CFG_PLL_FB_DIV;
	reg |= FIELD_PREP(USBCS_USB2PHY_CFG19_PHY_CFG_PLL_FB_DIV, 368);
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG19_OFFSET, reg);

	set_vbus_valid(gphy);

	ret = clk_bulk_prepare_enable(inst->num_clks, inst->clks);
	if (ret)
		return ret;
	struct google_usb_phy_instance *clk_dev __free(inst_clk_disable) = inst;

	ret = reset_control_bulk_deassert(inst->num_rsts, inst->rsts);
	if (ret)
		return ret;

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, &reg);
	reg |= USBCS_USB2PHY_CFG21_PHY_ENABLE;
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, reg);

	retain_and_null_ptr(clk_dev);

	return 0;
}

static int google_usb2_phy_exit(struct phy *_phy)
{
	struct google_usb_phy_instance *inst = phy_get_drvdata(_phy);
	struct google_usb_phy *gphy = inst->parent;
	u32 reg;

	dev_dbg(gphy->dev, "exiting usb2 phy\n");

	guard(mutex)(&gphy->phy_mutex);

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, &reg);
	reg &= ~USBCS_USB2PHY_CFG21_PHY_ENABLE;
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, reg);

	reset_control_bulk_assert(inst->num_rsts, inst->rsts);
	clk_bulk_disable_unprepare(inst->num_clks, inst->clks);

	return 0;
}

static const struct phy_ops google_usb2_phy_ops = {
	.init		= google_usb2_phy_init,
	.exit		= google_usb2_phy_exit,
};

static int google_usb3_phy_init(struct phy *_phy)
{
	struct google_usb_phy_instance *inst = phy_get_drvdata(_phy);
	struct google_usb_phy *gphy = inst->parent;
	int ret = 0;
	u32 reg;

	dev_dbg(gphy->dev, "initializing usb3 phy\n");

	guard(mutex)(&gphy->phy_mutex);

	if (gphy->phy_state != COMBO_PHY_IDLE) {
		dev_warn(gphy->dev, "usb3 phy init called when combo phy state is not idle\n");
		return 0;
	}

	reg = readl(gphy->usb3_tca_base + TCA_CTRLSYNCMODE_CFG1_OFFSET);
	reg &= ~TCA_CTRLSYNCMODE_CFG1_XA_TIMEOUT_VAL;
	reg |= FIELD_PREP(TCA_CTRLSYNCMODE_CFG1_XA_TIMEOUT_VAL,
			  TCA_CTRLSYNCMODE_CFG1_XA_TIMEOUT_VAL_100MS);
	writel(reg, gphy->usb3_tca_base + TCA_CTRLSYNCMODE_CFG1_OFFSET);

	reg = readl(gphy->usbdp_top_base + PHY_POWER_CONFIG_REG1_OFFSET);
	reg |= PHY_POWER_CONFIG_REG1_PG_MODE_EN;
	reg &= ~PHY_POWER_CONFIG_REG1_UPCS_PIPE_CONFIG;
	reg |= FIELD_PREP(PHY_POWER_CONFIG_REG1_UPCS_PIPE_CONFIG,
			  (UPCS_PIPE_CONFIG_ISO_CPM |
			   UPCS_PIPE_CONFIG_PG_MODE_STATIC |
			   UPCS_PIPE_CONFIG_LANE_RESET_NO_PG_EXIT));
	writel(reg, gphy->usbdp_top_base + PHY_POWER_CONFIG_REG1_OFFSET);

	set_vbus_valid(gphy);

	reg = readl(gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
	reg |= USBCS_PHY_CFG1_PHY0_MPLLA_SSC_EN;
	writel(reg, gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);

	set_sram_bypass(gphy, SRAM_BYPASS_MODE_BYPASS_FIRMWARE |
			SRAM_BYPASS_MODE_BYPASS_CONTEXT);
	set_pmgt_ref_clk_req_n(gphy, true);
	struct google_usb_phy *pmgt_ref_clk_req_dev __free(pmgt_ref_clk_req_n) = gphy;

	ret = clk_bulk_prepare_enable(inst->num_clks, inst->clks);
	if (ret)
		return ret;
	struct google_usb_phy_instance *clk_dev __free(inst_clk_disable) = inst;

	ret = reset_control_bulk_deassert(inst->num_rsts, inst->rsts);
	if (ret)
		return ret;
	struct google_usb_phy_instance *rst_dev __free(inst_rst_disable) = inst;

	ret = readl_poll_timeout(gphy->usb3_tca_base + TCA_PSTATE_0_OFFSET,
				 reg, !(reg & TCA_PSTATE_0_UPCS_LANE0_PHYSTATUS),
				 GPHY_TCA_DELAY_US, GPHY_TCA_TIMEOUT_US);
	if (ret) {
		dev_err(gphy->dev, "wait for lane0 phystatus timed out\n");
		return ret;
	}

	gphy->phy_state = COMBO_PHY_INIT_DONE;

	retain_and_null_ptr(rst_dev);
	retain_and_null_ptr(clk_dev);
	retain_and_null_ptr(pmgt_ref_clk_req_dev);

	return 0;
}

static int google_usb3_phy_exit(struct phy *_phy)
{
	struct google_usb_phy_instance *inst = phy_get_drvdata(_phy);
	struct google_usb_phy *gphy = inst->parent;

	dev_dbg(gphy->dev, "exiting usb3 phy\n");

	guard(mutex)(&gphy->phy_mutex);

	reset_control_bulk_assert(inst->num_rsts, inst->rsts);
	clk_bulk_disable_unprepare(inst->num_clks, inst->clks);
	set_pmgt_ref_clk_req_n(gphy, false);

	gphy->phy_state = COMBO_PHY_IDLE;

	return 0;
}

static int google_usb3_phy_power_on(struct phy *_phy)
{
	struct google_usb_phy_instance *inst = phy_get_drvdata(_phy);
	struct google_usb_phy *gphy = inst->parent;
	int ret;

	dev_dbg(gphy->dev, "power on usb3 phy\n");

	guard(mutex)(&gphy->phy_mutex);

	if (gphy->phy_state == COMBO_PHY_IDLE)
		return -EINVAL;

	if (gphy->phy_state == COMBO_PHY_INIT_DONE) {
		/* Wait for PoR -> NC transitions*/
		ret = wait_tca_xa_ack(gphy);
		if (ret) {
			dev_err(gphy->dev, "PoR->NC transition timeout\n");
			return ret;
		}
		gphy->phy_state = COMBO_PHY_TCA_READY;
	}

	if (gphy->orientation != TYPEC_ORIENTATION_NONE)
		return program_tca_locked(gphy);

	return 0;
}

static const struct phy_ops google_usb3_phy_ops = {
	.init		= google_usb3_phy_init,
	.exit		= google_usb3_phy_exit,
	.power_on	= google_usb3_phy_power_on,
};

static struct phy *google_usb_phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	if (args->args[0] >= GOOGLE_USB_PHY_NUM) {
		dev_err(dev, "invalid PHY index requested from DT\n");
		return ERR_PTR(-ENODEV);
	}
	return gphy->insts[args->args[0]].phy;
}

static int google_usb_phy_parse_clocks(struct google_usb_phy *gphy)
{
	struct device *dev = gphy->dev;
	int id, i, ret;

	for (id = 0; id < GOOGLE_USB_PHY_NUM; id++) {
		const struct google_usb_phy_config *cfg = &phy_configs[id];
		struct google_usb_phy_instance *inst = &gphy->insts[id];

		inst->num_clks = cfg->num_clks;
		inst->clks = devm_kcalloc(dev, inst->num_clks, sizeof(*inst->clks), GFP_KERNEL);
		if (!inst->clks)
			return -ENOMEM;

		for (i = 0; i < inst->num_clks; i++)
			inst->clks[i].id = cfg->clk_names[i];

		ret = devm_clk_bulk_get(dev, inst->num_clks, inst->clks);
		if (ret)
			return dev_err_probe(dev, ret, "failed to get phy%d clks\n", id);
	}

	return 0;
}

static int google_usb_phy_parse_resets(struct google_usb_phy *gphy)
{
	struct device *dev = gphy->dev;
	int id, i, ret;

	for (id = 0; id < GOOGLE_USB_PHY_NUM; id++) {
		const struct google_usb_phy_config *cfg = &phy_configs[id];
		struct google_usb_phy_instance *inst = &gphy->insts[id];

		inst->num_rsts = cfg->num_rsts;
		inst->rsts = devm_kcalloc(dev, inst->num_rsts, sizeof(*inst->rsts), GFP_KERNEL);
		if (!inst->rsts)
			return -ENOMEM;

		for (i = 0; i < inst->num_rsts; i++)
			inst->rsts[i].id = cfg->rst_names[i];
		ret = devm_reset_control_bulk_get_exclusive(dev, inst->num_rsts, inst->rsts);
		if (ret)
			return dev_err_probe(dev, ret, "failed to get phy%d resets\n", id);
	}

	return 0;
}

static int google_usb_phy_probe(struct platform_device *pdev)
{
	struct typec_switch_desc sw_desc = { };
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct google_usb_phy *gphy;
	u32 args[1];
	int ret;

	gphy = devm_kzalloc(dev, sizeof(*gphy), GFP_KERNEL);
	if (!gphy)
		return -ENOMEM;

	dev_set_drvdata(dev, gphy);
	gphy->dev = dev;

	ret = devm_mutex_init(dev, &gphy->phy_mutex);
	if (ret)
		return ret;

	gphy->usb_cfg_regmap =
		syscon_regmap_lookup_by_phandle_args(dev->of_node,
						     "google,usb-cfg-csr",
						     ARRAY_SIZE(args), args);
	if (IS_ERR(gphy->usb_cfg_regmap)) {
		return dev_err_probe(dev, PTR_ERR(gphy->usb_cfg_regmap),
				     "invalid usb cfg csr\n");
	}

	gphy->usb2_cfg_offset = args[0];

	gphy->usbdp_top_base = devm_platform_ioremap_resource_byname(pdev,
								     "usbdp_top");
	if (IS_ERR(gphy->usbdp_top_base))
		return dev_err_probe(dev, PTR_ERR(gphy->usbdp_top_base),
				    "invalid usbdp top\n");

	gphy->usb3_tca_base = devm_platform_ioremap_resource_byname(pdev,
								    "usb3_tca");
	if (IS_ERR(gphy->usb3_tca_base))
		return dev_err_probe(dev, PTR_ERR(gphy->usb3_tca_base),
				    "invalid usb3 tca\n");

	gphy->insts = devm_kcalloc(dev, GOOGLE_USB_PHY_NUM, sizeof(*gphy->insts), GFP_KERNEL);
	if (!gphy->insts)
		return -ENOMEM;

	gphy->insts[GOOGLE_USB2_PHY].phy = devm_phy_create(dev, NULL, &google_usb2_phy_ops);
	gphy->insts[GOOGLE_USB2_PHY].index = GOOGLE_USB2_PHY;
	gphy->insts[GOOGLE_USB2_PHY].parent = gphy;
	if (IS_ERR(gphy->insts[GOOGLE_USB2_PHY].phy))
		return dev_err_probe(dev, PTR_ERR(gphy->insts[GOOGLE_USB2_PHY].phy),
				     "failed to create usb2 phy instance\n");
	phy_set_drvdata(gphy->insts[GOOGLE_USB2_PHY].phy, &gphy->insts[GOOGLE_USB2_PHY]);

	gphy->insts[GOOGLE_USB3_PHY].phy = devm_phy_create(dev, NULL, &google_usb3_phy_ops);
	gphy->insts[GOOGLE_USB3_PHY].index = GOOGLE_USB3_PHY;
	gphy->insts[GOOGLE_USB3_PHY].parent = gphy;
	if (IS_ERR(gphy->insts[GOOGLE_USB3_PHY].phy))
		return dev_err_probe(dev, PTR_ERR(gphy->insts[GOOGLE_USB3_PHY].phy),
				     "failed to create usb3 phy instance\n");
	phy_set_drvdata(gphy->insts[GOOGLE_USB3_PHY].phy, &gphy->insts[GOOGLE_USB3_PHY]);

	ret = google_usb_phy_parse_clocks(gphy);
	if (ret)
		return ret;

	ret = google_usb_phy_parse_resets(gphy);
	if (ret)
		return ret;

	phy_provider = devm_of_phy_provider_register(dev, google_usb_phy_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "failed to register phy provider\n");

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.drvdata = gphy;
	sw_desc.name = fwnode_get_name(dev_fwnode(dev));
	sw_desc.set = google_usb_set_orientation;

	gphy->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(gphy->sw))
		return dev_err_probe(dev, PTR_ERR(gphy->sw),
				     "failed to register typec switch\n");

	return 0;
}

static void google_usb_phy_remove(struct platform_device *pdev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(&pdev->dev);

	typec_switch_unregister(gphy->sw);
}

static const struct of_device_id google_usb_phy_of_match[] = {
	{
		.compatible = "google,lga-usb-phy",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, google_usb_phy_of_match);

static struct platform_driver google_usb_phy = {
	.probe	= google_usb_phy_probe,
	.remove = google_usb_phy_remove,
	.driver = {
		.name		= "google-usb-phy",
		.of_match_table	= google_usb_phy_of_match,
	}
};

module_platform_driver(google_usb_phy);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google USB phy driver");
