// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Duje Mihanović <duje@dujemihanovic.xyz>
 */

#include <linux/auxiliary_bus.h>
#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>

#include <dt-bindings/power/marvell,pxa1908-power.h>

/* VPU, GPU, ISP */
#define APMU_PWR_CTRL_REG	0xd8
#define APMU_PWR_BLK_TMR_REG	0xdc
#define APMU_PWR_STATUS_REG	0xf0

/* DSI */
#define APMU_DEBUG		0x88
#define DSI_PHY_DVM_MASK	BIT(31)

#define POWER_ON_LATENCY_US	300
#define POWER_OFF_LATENCY_US	20
#define POWER_POLL_TIMEOUT_US	(25 * USEC_PER_MSEC)
#define POWER_POLL_SLEEP_US	6

#define NR_DOMAINS	5

#define to_pxa1908_pd(_genpd) container_of(_genpd, struct pxa1908_pd, genpd)

struct pxa1908_pd_ctrl {
	struct generic_pm_domain *domains[NR_DOMAINS];
	struct genpd_onecell_data onecell_data;
	struct regmap *base;
	struct device *dev;
};

struct pxa1908_pd_data {
	u32 reg_clk_res_ctrl;
	u32 pwr_state;
	u32 hw_mode;
	bool keep_on;
	int id;
};

struct pxa1908_pd {
	const struct pxa1908_pd_data data;
	struct pxa1908_pd_ctrl *ctrl;
	struct generic_pm_domain genpd;
	bool initialized;
};

static inline bool pxa1908_pd_is_on(struct pxa1908_pd *pd)
{
	struct pxa1908_pd_ctrl *ctrl = pd->ctrl;

	return pd->data.id != PXA1908_POWER_DOMAIN_DSI
		? regmap_test_bits(ctrl->base, APMU_PWR_STATUS_REG, pd->data.pwr_state)
		: regmap_test_bits(ctrl->base, APMU_DEBUG, DSI_PHY_DVM_MASK);
}

static int pxa1908_pd_power_on(struct generic_pm_domain *genpd)
{
	struct pxa1908_pd *pd = to_pxa1908_pd(genpd);
	const struct pxa1908_pd_data *data = &pd->data;
	struct pxa1908_pd_ctrl *ctrl = pd->ctrl;
	unsigned int status;
	int ret = 0;

	regmap_set_bits(ctrl->base, data->reg_clk_res_ctrl, data->hw_mode);
	if (data->id != PXA1908_POWER_DOMAIN_ISP)
		regmap_write(ctrl->base, APMU_PWR_BLK_TMR_REG, 0x20001fff);
	regmap_set_bits(ctrl->base, APMU_PWR_CTRL_REG, data->pwr_state);

	ret = regmap_read_poll_timeout(ctrl->base, APMU_PWR_STATUS_REG, status,
				       status & data->pwr_state, POWER_POLL_SLEEP_US,
				       POWER_ON_LATENCY_US + POWER_POLL_TIMEOUT_US);
	if (ret == -ETIMEDOUT)
		dev_err(ctrl->dev, "timed out powering on domain '%s'\n", pd->genpd.name);

	return ret;
}

static int pxa1908_pd_power_off(struct generic_pm_domain *genpd)
{
	struct pxa1908_pd *pd = to_pxa1908_pd(genpd);
	const struct pxa1908_pd_data *data = &pd->data;
	struct pxa1908_pd_ctrl *ctrl = pd->ctrl;
	unsigned int status;
	int ret;

	regmap_clear_bits(ctrl->base, APMU_PWR_CTRL_REG, data->pwr_state);

	ret = regmap_read_poll_timeout(ctrl->base, APMU_PWR_STATUS_REG, status,
				       !(status & data->pwr_state), POWER_POLL_SLEEP_US,
				       POWER_OFF_LATENCY_US + POWER_POLL_TIMEOUT_US);
	if (ret == -ETIMEDOUT) {
		dev_err(ctrl->dev, "timed out powering off domain '%s'\n", pd->genpd.name);
		return ret;
	}

	return regmap_clear_bits(ctrl->base, data->reg_clk_res_ctrl, data->hw_mode);
}

static inline int pxa1908_dsi_power_on(struct generic_pm_domain *genpd)
{
	struct pxa1908_pd *pd = to_pxa1908_pd(genpd);
	struct pxa1908_pd_ctrl *ctrl = pd->ctrl;

	return regmap_set_bits(ctrl->base, APMU_DEBUG, DSI_PHY_DVM_MASK);
}

static inline int pxa1908_dsi_power_off(struct generic_pm_domain *genpd)
{
	struct pxa1908_pd *pd = to_pxa1908_pd(genpd);
	struct pxa1908_pd_ctrl *ctrl = pd->ctrl;

	return regmap_clear_bits(ctrl->base, APMU_DEBUG, DSI_PHY_DVM_MASK);
}

#define DOMAIN(_id, _name, ctrl, mode, state) \
	[_id] = { \
		.data = { \
			.reg_clk_res_ctrl = ctrl, \
			.hw_mode = BIT(mode), \
			.pwr_state = BIT(state), \
			.id = _id, \
		}, \
		.genpd = { \
			.name = _name, \
			.power_on = pxa1908_pd_power_on, \
			.power_off = pxa1908_pd_power_off, \
		}, \
	}

static struct pxa1908_pd domains[NR_DOMAINS] = {
	DOMAIN(PXA1908_POWER_DOMAIN_VPU, "vpu", 0xa4, 19, 2),
	DOMAIN(PXA1908_POWER_DOMAIN_GPU, "gpu", 0xcc, 11, 0),
	DOMAIN(PXA1908_POWER_DOMAIN_GPU2D, "gpu2d", 0xf4, 11, 6),
	DOMAIN(PXA1908_POWER_DOMAIN_ISP, "isp", 0x38, 15, 4),
	[PXA1908_POWER_DOMAIN_DSI] = {
		.genpd = {
			.name = "dsi",
			.power_on = pxa1908_dsi_power_on,
			.power_off = pxa1908_dsi_power_off,
			/*
			 * TODO: There is no DSI driver written yet and until then we probably
			 * don't want to power off the DSI PHY ever.
			 */
			.flags = GENPD_FLAG_ALWAYS_ON,
		},
		.data = {
			/* See above. */
			.keep_on = true,
		},
	},
};

static void pxa1908_pd_remove(struct auxiliary_device *auxdev)
{
	struct pxa1908_pd *pd;
	int ret;

	for (int i = NR_DOMAINS - 1; i >= 0; i--) {
		pd = &domains[i];

		if (!pd->initialized)
			continue;

		if (pxa1908_pd_is_on(pd) && !pd->data.keep_on)
			pxa1908_pd_power_off(&pd->genpd);

		ret = pm_genpd_remove(&pd->genpd);
		if (ret)
			dev_err(&auxdev->dev, "failed to remove domain '%s': %d\n",
				pd->genpd.name, ret);
	}
}

static int
pxa1908_pd_init(struct pxa1908_pd_ctrl *ctrl, int id, struct device *dev)
{
	struct pxa1908_pd *pd = &domains[id];
	int ret;

	ctrl->domains[id] = &pd->genpd;

	pd->ctrl = ctrl;

	/* Make sure the state of the hardware is synced with the domain table above. */
	if (pd->data.keep_on) {
		ret = pd->genpd.power_on(&pd->genpd);
		if (ret)
			return dev_err_probe(dev, ret, "failed to power on domain '%s'\n",
					     pd->genpd.name);
	} else {
		if (pxa1908_pd_is_on(pd)) {
			dev_warn(dev,
				 "domain '%s' is on despite being default off; powering off\n",
				 pd->genpd.name);

			ret = pd->genpd.power_off(&pd->genpd);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to power off domain '%s'\n",
						     pd->genpd.name);
		}
	}

	ret = pm_genpd_init(&pd->genpd, NULL, !pd->data.keep_on);
	if (ret)
		return dev_err_probe(dev, ret, "domain '%s' failed to initialize\n",
				     pd->genpd.name);

	pd->initialized = true;

	return 0;
}

static int
pxa1908_pd_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *aux_id)
{
	struct pxa1908_pd_ctrl *ctrl;
	struct device *dev = &auxdev->dev;
	int ret;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	auxiliary_set_drvdata(auxdev, ctrl);

	ctrl->base = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(ctrl->base))
		return dev_err_probe(dev, PTR_ERR(ctrl->base), "no regmap available\n");

	ctrl->dev = dev;
	ctrl->onecell_data.domains = ctrl->domains;
	ctrl->onecell_data.num_domains = NR_DOMAINS;

	for (int i = 0; i < NR_DOMAINS; i++) {
		ret = pxa1908_pd_init(ctrl, i, dev);
		if (ret)
			goto err;
	}

	return of_genpd_add_provider_onecell(dev->parent->of_node, &ctrl->onecell_data);

err:
	pxa1908_pd_remove(auxdev);
	return ret;
}

static const struct auxiliary_device_id pxa1908_pd_id[] = {
	{ .name = "clk_pxa1908_apmu.power" },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, pxa1908_pd_id);

static struct auxiliary_driver pxa1908_pd_driver = {
	.probe = pxa1908_pd_probe,
	.remove = pxa1908_pd_remove,
	.id_table = pxa1908_pd_id,
};
module_auxiliary_driver(pxa1908_pd_driver);

MODULE_AUTHOR("Duje Mihanović <duje@dujemihanovic.xyz>");
MODULE_DESCRIPTION("Marvell PXA1908 power domain driver");
MODULE_LICENSE("GPL");
