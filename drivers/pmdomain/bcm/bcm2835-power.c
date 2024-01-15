// SPDX-License-Identifier: GPL-2.0+
/*
 * Power domain driver for Broadcom BCM2835
 *
 * Copyright (C) 2018 Broadcom
 */

#include <dt-bindings/soc/bcm2835-pm.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/bcm2835-pm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reset-controller.h>
#include <linux/types.h>

#define PM_GNRIC                        0x00
#define PM_AUDIO                        0x04
#define PM_STATUS                       0x18
#define PM_RSTC				0x1c
#define PM_RSTS				0x20
#define PM_WDOG				0x24
#define PM_PADS0			0x28
#define PM_PADS2			0x2c
#define PM_PADS3			0x30
#define PM_PADS4			0x34
#define PM_PADS5			0x38
#define PM_PADS6			0x3c
#define PM_CAM0				0x44
#define PM_CAM0_LDOHPEN			BIT(2)
#define PM_CAM0_LDOLPEN			BIT(1)
#define PM_CAM0_CTRLEN			BIT(0)

#define PM_CAM1				0x48
#define PM_CAM1_LDOHPEN			BIT(2)
#define PM_CAM1_LDOLPEN			BIT(1)
#define PM_CAM1_CTRLEN			BIT(0)

#define PM_CCP2TX			0x4c
#define PM_CCP2TX_LDOEN			BIT(1)
#define PM_CCP2TX_CTRLEN		BIT(0)

#define PM_DSI0				0x50
#define PM_DSI0_LDOHPEN			BIT(2)
#define PM_DSI0_LDOLPEN			BIT(1)
#define PM_DSI0_CTRLEN			BIT(0)

#define PM_DSI1				0x54
#define PM_DSI1_LDOHPEN			BIT(2)
#define PM_DSI1_LDOLPEN			BIT(1)
#define PM_DSI1_CTRLEN			BIT(0)

#define PM_HDMI				0x58
#define PM_HDMI_RSTDR			BIT(19)
#define PM_HDMI_LDOPD			BIT(1)
#define PM_HDMI_CTRLEN			BIT(0)

#define PM_USB				0x5c
/* The power gates must be enabled with this bit before enabling the LDO in the
 * USB block.
 */
#define PM_USB_CTRLEN			BIT(0)

#define PM_PXLDO			0x60
#define PM_PXBG				0x64
#define PM_DFT				0x68
#define PM_SMPS				0x6c
#define PM_XOSC				0x70
#define PM_SPAREW			0x74
#define PM_SPARER			0x78
#define PM_AVS_RSTDR			0x7c
#define PM_AVS_STAT			0x80
#define PM_AVS_EVENT			0x84
#define PM_AVS_INTEN			0x88
#define PM_DUMMY			0xfc

#define PM_IMAGE			0x108
#define PM_GRAFX			0x10c
#define PM_PROC				0x110
#define PM_ENAB				BIT(12)
#define PM_ISPRSTN			BIT(8)
#define PM_H264RSTN			BIT(7)
#define PM_PERIRSTN			BIT(6)
#define PM_V3DRSTN			BIT(6)
#define PM_ISFUNC			BIT(5)
#define PM_MRDONE			BIT(4)
#define PM_MEMREP			BIT(3)
#define PM_ISPOW			BIT(2)
#define PM_POWOK			BIT(1)
#define PM_POWUP			BIT(0)
#define PM_INRUSH_SHIFT			13
#define PM_INRUSH_3_5_MA		0
#define PM_INRUSH_5_MA			1
#define PM_INRUSH_10_MA			2
#define PM_INRUSH_20_MA			3
#define PM_INRUSH_MASK			(3 << PM_INRUSH_SHIFT)

#define PM_PASSWORD			0x5a000000

#define PM_WDOG_TIME_SET		0x000fffff
#define PM_RSTC_WRCFG_CLR		0xffffffcf
#define PM_RSTS_HADWRH_SET		0x00000040
#define PM_RSTC_WRCFG_SET		0x00000030
#define PM_RSTC_WRCFG_FULL_RESET	0x00000020
#define PM_RSTC_RESET			0x00000102

#define PM_READ(reg) readl(power->base + (reg))
#define PM_WRITE(reg, val) writel(PM_PASSWORD | (val), power->base + (reg))

#define ASB_BRDG_VERSION                0x00
#define ASB_CPR_CTRL                    0x04

#define ASB_V3D_S_CTRL			0x08
#define ASB_V3D_M_CTRL			0x0c
#define ASB_ISP_S_CTRL			0x10
#define ASB_ISP_M_CTRL			0x14
#define ASB_H264_S_CTRL			0x18
#define ASB_H264_M_CTRL			0x1c

#define ASB_REQ_STOP                    BIT(0)
#define ASB_ACK                         BIT(1)
#define ASB_EMPTY                       BIT(2)
#define ASB_FULL                        BIT(3)

#define ASB_AXI_BRDG_ID			0x20

#define BCM2835_BRDG_ID			0x62726467

struct bcm2835_power_domain {
	struct generic_pm_domain base;
	struct bcm2835_power *power;
	u32 domain;
	struct clk *clk;
};

struct bcm2835_power {
	struct device		*dev;
	/* PM registers. */
	void __iomem		*base;
	/* AXI Async bridge registers. */
	void __iomem		*asb;
	/* RPiVid bridge registers. */
	void __iomem		*rpivid_asb;

	struct genpd_onecell_data pd_xlate;
	struct bcm2835_power_domain domains[BCM2835_POWER_DOMAIN_COUNT];
	struct reset_controller_dev reset;
};

static int bcm2835_asb_control(struct bcm2835_power *power, u32 reg, bool enable)
{
	void __iomem *base = power->asb;
	u64 start;
	u32 val;

	switch (reg) {
	case 0:
		return 0;
	case ASB_V3D_S_CTRL:
	case ASB_V3D_M_CTRL:
		if (power->rpivid_asb)
			base = power->rpivid_asb;
		break;
	}

	start = ktime_get_ns();

	/* Enable the module's async AXI bridges. */
	if (enable) {
		val = readl(base + reg) & ~ASB_REQ_STOP;
	} else {
		val = readl(base + reg) | ASB_REQ_STOP;
	}
	writel(PM_PASSWORD | val, base + reg);

	while (!!(readl(base + reg) & ASB_ACK) == enable) {
		cpu_relax();
		if (ktime_get_ns() - start >= 1000)
			return -ETIMEDOUT;
	}

	return 0;
}

static int bcm2835_asb_enable(struct bcm2835_power *power, u32 reg)
{
	return bcm2835_asb_control(power, reg, true);
}

static int bcm2835_asb_disable(struct bcm2835_power *power, u32 reg)
{
	return bcm2835_asb_control(power, reg, false);
}

static int bcm2835_power_power_off(struct bcm2835_power_domain *pd, u32 pm_reg)
{
	struct bcm2835_power *power = pd->power;

	/* We don't run this on BCM2711 */
	if (power->rpivid_asb)
		return 0;

	/* Enable functional isolation */
	PM_WRITE(pm_reg, PM_READ(pm_reg) & ~PM_ISFUNC);

	/* Enable electrical isolation */
	PM_WRITE(pm_reg, PM_READ(pm_reg) & ~PM_ISPOW);

	/* Open the power switches. */
	PM_WRITE(pm_reg, PM_READ(pm_reg) & ~PM_POWUP);

	return 0;
}

static int bcm2835_power_power_on(struct bcm2835_power_domain *pd, u32 pm_reg)
{
	struct bcm2835_power *power = pd->power;
	struct device *dev = power->dev;
	u64 start;
	int ret;
	int inrush;
	bool powok;

	/* We don't run this on BCM2711 */
	if (power->rpivid_asb)
		return 0;

	/* If it was already powered on by the fw, leave it that way. */
	if (PM_READ(pm_reg) & PM_POWUP)
		return 0;

	/* Enable power.  Allowing too much current at once may result
	 * in POWOK never getting set, so start low and ramp it up as
	 * necessary to succeed.
	 */
	powok = false;
	for (inrush = PM_INRUSH_3_5_MA; inrush <= PM_INRUSH_20_MA; inrush++) {
		PM_WRITE(pm_reg,
			 (PM_READ(pm_reg) & ~PM_INRUSH_MASK) |
			 (inrush << PM_INRUSH_SHIFT) |
			 PM_POWUP);

		start = ktime_get_ns();
		while (!(powok = !!(PM_READ(pm_reg) & PM_POWOK))) {
			cpu_relax();
			if (ktime_get_ns() - start >= 3000)
				break;
		}
	}
	if (!powok) {
		dev_err(dev, "Timeout waiting for %s power OK\n",
			pd->base.name);
		ret = -ETIMEDOUT;
		goto err_disable_powup;
	}

	/* Disable electrical isolation */
	PM_WRITE(pm_reg, PM_READ(pm_reg) | PM_ISPOW);

	/* Repair memory */
	PM_WRITE(pm_reg, PM_READ(pm_reg) | PM_MEMREP);
	start = ktime_get_ns();
	while (!(PM_READ(pm_reg) & PM_MRDONE)) {
		cpu_relax();
		if (ktime_get_ns() - start >= 1000) {
			dev_err(dev, "Timeout waiting for %s memory repair\n",
				pd->base.name);
			ret = -ETIMEDOUT;
			goto err_disable_ispow;
		}
	}

	/* Disable functional isolation */
	PM_WRITE(pm_reg, PM_READ(pm_reg) | PM_ISFUNC);

	return 0;

err_disable_ispow:
	PM_WRITE(pm_reg, PM_READ(pm_reg) & ~PM_ISPOW);
err_disable_powup:
	PM_WRITE(pm_reg, PM_READ(pm_reg) & ~(PM_POWUP | PM_INRUSH_MASK));
	return ret;
}

static int bcm2835_asb_power_on(struct bcm2835_power_domain *pd,
				u32 pm_reg,
				u32 asb_m_reg,
				u32 asb_s_reg,
				u32 reset_flags)
{
	struct bcm2835_power *power = pd->power;
	int ret;

	ret = clk_prepare_enable(pd->clk);
	if (ret) {
		dev_err(power->dev, "Failed to enable clock for %s\n",
			pd->base.name);
		return ret;
	}

	/* Wait 32 clocks for reset to propagate, 1 us will be enough */
	udelay(1);

	clk_disable_unprepare(pd->clk);

	/* Deassert the resets. */
	PM_WRITE(pm_reg, PM_READ(pm_reg) | reset_flags);

	ret = clk_prepare_enable(pd->clk);
	if (ret) {
		dev_err(power->dev, "Failed to enable clock for %s\n",
			pd->base.name);
		goto err_enable_resets;
	}

	ret = bcm2835_asb_enable(power, asb_m_reg);
	if (ret) {
		dev_err(power->dev, "Failed to enable ASB master for %s\n",
			pd->base.name);
		goto err_disable_clk;
	}
	ret = bcm2835_asb_enable(power, asb_s_reg);
	if (ret) {
		dev_err(power->dev, "Failed to enable ASB slave for %s\n",
			pd->base.name);
		goto err_disable_asb_master;
	}

	return 0;

err_disable_asb_master:
	bcm2835_asb_disable(power, asb_m_reg);
err_disable_clk:
	clk_disable_unprepare(pd->clk);
err_enable_resets:
	PM_WRITE(pm_reg, PM_READ(pm_reg) & ~reset_flags);
	return ret;
}

static int bcm2835_asb_power_off(struct bcm2835_power_domain *pd,
				 u32 pm_reg,
				 u32 asb_m_reg,
				 u32 asb_s_reg,
				 u32 reset_flags)
{
	struct bcm2835_power *power = pd->power;
	int ret;

	ret = bcm2835_asb_disable(power, asb_s_reg);
	if (ret) {
		dev_warn(power->dev, "Failed to disable ASB slave for %s\n",
			 pd->base.name);
		return ret;
	}
	ret = bcm2835_asb_disable(power, asb_m_reg);
	if (ret) {
		dev_warn(power->dev, "Failed to disable ASB master for %s\n",
			 pd->base.name);
		bcm2835_asb_enable(power, asb_s_reg);
		return ret;
	}

	clk_disable_unprepare(pd->clk);

	/* Assert the resets. */
	PM_WRITE(pm_reg, PM_READ(pm_reg) & ~reset_flags);

	return 0;
}

static int bcm2835_power_pd_power_on(struct generic_pm_domain *domain)
{
	struct bcm2835_power_domain *pd =
		container_of(domain, struct bcm2835_power_domain, base);
	struct bcm2835_power *power = pd->power;

	switch (pd->domain) {
	case BCM2835_POWER_DOMAIN_GRAFX:
		return bcm2835_power_power_on(pd, PM_GRAFX);

	case BCM2835_POWER_DOMAIN_GRAFX_V3D:
		return bcm2835_asb_power_on(pd, PM_GRAFX,
					    ASB_V3D_M_CTRL, ASB_V3D_S_CTRL,
					    PM_V3DRSTN);

	case BCM2835_POWER_DOMAIN_IMAGE:
		return bcm2835_power_power_on(pd, PM_IMAGE);

	case BCM2835_POWER_DOMAIN_IMAGE_PERI:
		return bcm2835_asb_power_on(pd, PM_IMAGE,
					    0, 0,
					    PM_PERIRSTN);

	case BCM2835_POWER_DOMAIN_IMAGE_ISP:
		return bcm2835_asb_power_on(pd, PM_IMAGE,
					    ASB_ISP_M_CTRL, ASB_ISP_S_CTRL,
					    PM_ISPRSTN);

	case BCM2835_POWER_DOMAIN_IMAGE_H264:
		return bcm2835_asb_power_on(pd, PM_IMAGE,
					    ASB_H264_M_CTRL, ASB_H264_S_CTRL,
					    PM_H264RSTN);

	case BCM2835_POWER_DOMAIN_USB:
		PM_WRITE(PM_USB, PM_USB_CTRLEN);
		return 0;

	case BCM2835_POWER_DOMAIN_DSI0:
		PM_WRITE(PM_DSI0, PM_DSI0_CTRLEN);
		PM_WRITE(PM_DSI0, PM_DSI0_CTRLEN | PM_DSI0_LDOHPEN);
		return 0;

	case BCM2835_POWER_DOMAIN_DSI1:
		PM_WRITE(PM_DSI1, PM_DSI1_CTRLEN);
		PM_WRITE(PM_DSI1, PM_DSI1_CTRLEN | PM_DSI1_LDOHPEN);
		return 0;

	case BCM2835_POWER_DOMAIN_CCP2TX:
		PM_WRITE(PM_CCP2TX, PM_CCP2TX_CTRLEN);
		PM_WRITE(PM_CCP2TX, PM_CCP2TX_CTRLEN | PM_CCP2TX_LDOEN);
		return 0;

	case BCM2835_POWER_DOMAIN_HDMI:
		PM_WRITE(PM_HDMI, PM_READ(PM_HDMI) | PM_HDMI_RSTDR);
		PM_WRITE(PM_HDMI, PM_READ(PM_HDMI) | PM_HDMI_CTRLEN);
		PM_WRITE(PM_HDMI, PM_READ(PM_HDMI) & ~PM_HDMI_LDOPD);
		usleep_range(100, 200);
		PM_WRITE(PM_HDMI, PM_READ(PM_HDMI) & ~PM_HDMI_RSTDR);
		return 0;

	default:
		dev_err(power->dev, "Invalid domain %d\n", pd->domain);
		return -EINVAL;
	}
}

static int bcm2835_power_pd_power_off(struct generic_pm_domain *domain)
{
	struct bcm2835_power_domain *pd =
		container_of(domain, struct bcm2835_power_domain, base);
	struct bcm2835_power *power = pd->power;

	switch (pd->domain) {
	case BCM2835_POWER_DOMAIN_GRAFX:
		return bcm2835_power_power_off(pd, PM_GRAFX);

	case BCM2835_POWER_DOMAIN_GRAFX_V3D:
		return bcm2835_asb_power_off(pd, PM_GRAFX,
					     ASB_V3D_M_CTRL, ASB_V3D_S_CTRL,
					     PM_V3DRSTN);

	case BCM2835_POWER_DOMAIN_IMAGE:
		return bcm2835_power_power_off(pd, PM_IMAGE);

	case BCM2835_POWER_DOMAIN_IMAGE_PERI:
		return bcm2835_asb_power_off(pd, PM_IMAGE,
					     0, 0,
					     PM_PERIRSTN);

	case BCM2835_POWER_DOMAIN_IMAGE_ISP:
		return bcm2835_asb_power_off(pd, PM_IMAGE,
					     ASB_ISP_M_CTRL, ASB_ISP_S_CTRL,
					     PM_ISPRSTN);

	case BCM2835_POWER_DOMAIN_IMAGE_H264:
		return bcm2835_asb_power_off(pd, PM_IMAGE,
					     ASB_H264_M_CTRL, ASB_H264_S_CTRL,
					     PM_H264RSTN);

	case BCM2835_POWER_DOMAIN_USB:
		PM_WRITE(PM_USB, 0);
		return 0;

	case BCM2835_POWER_DOMAIN_DSI0:
		PM_WRITE(PM_DSI0, PM_DSI0_CTRLEN);
		PM_WRITE(PM_DSI0, 0);
		return 0;

	case BCM2835_POWER_DOMAIN_DSI1:
		PM_WRITE(PM_DSI1, PM_DSI1_CTRLEN);
		PM_WRITE(PM_DSI1, 0);
		return 0;

	case BCM2835_POWER_DOMAIN_CCP2TX:
		PM_WRITE(PM_CCP2TX, PM_CCP2TX_CTRLEN);
		PM_WRITE(PM_CCP2TX, 0);
		return 0;

	case BCM2835_POWER_DOMAIN_HDMI:
		PM_WRITE(PM_HDMI, PM_READ(PM_HDMI) | PM_HDMI_LDOPD);
		PM_WRITE(PM_HDMI, PM_READ(PM_HDMI) & ~PM_HDMI_CTRLEN);
		return 0;

	default:
		dev_err(power->dev, "Invalid domain %d\n", pd->domain);
		return -EINVAL;
	}
}

static int
bcm2835_init_power_domain(struct bcm2835_power *power,
			  int pd_xlate_index, const char *name)
{
	struct device *dev = power->dev;
	struct bcm2835_power_domain *dom = &power->domains[pd_xlate_index];

	dom->clk = devm_clk_get(dev->parent, name);
	if (IS_ERR(dom->clk)) {
		int ret = PTR_ERR(dom->clk);

		if (ret == -EPROBE_DEFER)
			return ret;

		/* Some domains don't have a clk, so make sure that we
		 * don't deref an error pointer later.
		 */
		dom->clk = NULL;
	}

	dom->base.name = name;
	dom->base.power_on = bcm2835_power_pd_power_on;
	dom->base.power_off = bcm2835_power_pd_power_off;

	dom->domain = pd_xlate_index;
	dom->power = power;

	/* XXX: on/off at boot? */
	pm_genpd_init(&dom->base, NULL, true);

	power->pd_xlate.domains[pd_xlate_index] = &dom->base;

	return 0;
}

/** bcm2835_reset_reset - Resets a block that has a reset line in the
 * PM block.
 *
 * The consumer of the reset controller must have the power domain up
 * -- there's no reset ability with the power domain down.  To reset
 * the sub-block, we just disable its access to memory through the
 * ASB, reset, and re-enable.
 */
static int bcm2835_reset_reset(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct bcm2835_power *power = container_of(rcdev, struct bcm2835_power,
						   reset);
	struct bcm2835_power_domain *pd;
	int ret;

	switch (id) {
	case BCM2835_RESET_V3D:
		pd = &power->domains[BCM2835_POWER_DOMAIN_GRAFX_V3D];
		break;
	case BCM2835_RESET_H264:
		pd = &power->domains[BCM2835_POWER_DOMAIN_IMAGE_H264];
		break;
	case BCM2835_RESET_ISP:
		pd = &power->domains[BCM2835_POWER_DOMAIN_IMAGE_ISP];
		break;
	default:
		dev_err(power->dev, "Bad reset id %ld\n", id);
		return -EINVAL;
	}

	ret = bcm2835_power_pd_power_off(&pd->base);
	if (ret)
		return ret;

	return bcm2835_power_pd_power_on(&pd->base);
}

static int bcm2835_reset_status(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct bcm2835_power *power = container_of(rcdev, struct bcm2835_power,
						   reset);

	switch (id) {
	case BCM2835_RESET_V3D:
		return !PM_READ(PM_GRAFX & PM_V3DRSTN);
	case BCM2835_RESET_H264:
		return !PM_READ(PM_IMAGE & PM_H264RSTN);
	case BCM2835_RESET_ISP:
		return !PM_READ(PM_IMAGE & PM_ISPRSTN);
	default:
		return -EINVAL;
	}
}

static const struct reset_control_ops bcm2835_reset_ops = {
	.reset = bcm2835_reset_reset,
	.status = bcm2835_reset_status,
};

static const char *const power_domain_names[] = {
	[BCM2835_POWER_DOMAIN_GRAFX] = "grafx",
	[BCM2835_POWER_DOMAIN_GRAFX_V3D] = "v3d",

	[BCM2835_POWER_DOMAIN_IMAGE] = "image",
	[BCM2835_POWER_DOMAIN_IMAGE_PERI] = "peri_image",
	[BCM2835_POWER_DOMAIN_IMAGE_H264] = "h264",
	[BCM2835_POWER_DOMAIN_IMAGE_ISP] = "isp",

	[BCM2835_POWER_DOMAIN_USB] = "usb",
	[BCM2835_POWER_DOMAIN_DSI0] = "dsi0",
	[BCM2835_POWER_DOMAIN_DSI1] = "dsi1",
	[BCM2835_POWER_DOMAIN_CAM0] = "cam0",
	[BCM2835_POWER_DOMAIN_CAM1] = "cam1",
	[BCM2835_POWER_DOMAIN_CCP2TX] = "ccp2tx",
	[BCM2835_POWER_DOMAIN_HDMI] = "hdmi",
};

static int bcm2835_power_probe(struct platform_device *pdev)
{
	struct bcm2835_pm *pm = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct bcm2835_power *power;
	static const struct {
		int parent, child;
	} domain_deps[] = {
		{ BCM2835_POWER_DOMAIN_GRAFX, BCM2835_POWER_DOMAIN_GRAFX_V3D },
		{ BCM2835_POWER_DOMAIN_IMAGE, BCM2835_POWER_DOMAIN_IMAGE_PERI },
		{ BCM2835_POWER_DOMAIN_IMAGE, BCM2835_POWER_DOMAIN_IMAGE_H264 },
		{ BCM2835_POWER_DOMAIN_IMAGE, BCM2835_POWER_DOMAIN_IMAGE_ISP },
		{ BCM2835_POWER_DOMAIN_IMAGE_PERI, BCM2835_POWER_DOMAIN_USB },
		{ BCM2835_POWER_DOMAIN_IMAGE_PERI, BCM2835_POWER_DOMAIN_CAM0 },
		{ BCM2835_POWER_DOMAIN_IMAGE_PERI, BCM2835_POWER_DOMAIN_CAM1 },
	};
	int ret = 0, i;
	u32 id;

	power = devm_kzalloc(dev, sizeof(*power), GFP_KERNEL);
	if (!power)
		return -ENOMEM;
	platform_set_drvdata(pdev, power);

	power->dev = dev;
	power->base = pm->base;
	power->asb = pm->asb;
	power->rpivid_asb = pm->rpivid_asb;

	id = readl(power->asb + ASB_AXI_BRDG_ID);
	if (id != BCM2835_BRDG_ID /* "BRDG" */) {
		dev_err(dev, "ASB register ID returned 0x%08x\n", id);
		return -ENODEV;
	}

	if (power->rpivid_asb) {
		id = readl(power->rpivid_asb + ASB_AXI_BRDG_ID);
		if (id != BCM2835_BRDG_ID /* "BRDG" */) {
			dev_err(dev, "RPiVid ASB register ID returned 0x%08x\n",
				     id);
			return -ENODEV;
		}
	}

	power->pd_xlate.domains = devm_kcalloc(dev,
					       ARRAY_SIZE(power_domain_names),
					       sizeof(*power->pd_xlate.domains),
					       GFP_KERNEL);
	if (!power->pd_xlate.domains)
		return -ENOMEM;

	power->pd_xlate.num_domains = ARRAY_SIZE(power_domain_names);

	for (i = 0; i < ARRAY_SIZE(power_domain_names); i++) {
		ret = bcm2835_init_power_domain(power, i, power_domain_names[i]);
		if (ret)
			goto fail;
	}

	for (i = 0; i < ARRAY_SIZE(domain_deps); i++) {
		pm_genpd_add_subdomain(&power->domains[domain_deps[i].parent].base,
				       &power->domains[domain_deps[i].child].base);
	}

	power->reset.owner = THIS_MODULE;
	power->reset.nr_resets = BCM2835_RESET_COUNT;
	power->reset.ops = &bcm2835_reset_ops;
	power->reset.of_node = dev->parent->of_node;

	ret = devm_reset_controller_register(dev, &power->reset);
	if (ret)
		goto fail;

	of_genpd_add_provider_onecell(dev->parent->of_node, &power->pd_xlate);

	dev_info(dev, "Broadcom BCM2835 power domains driver");
	return 0;

fail:
	for (i = 0; i < ARRAY_SIZE(power_domain_names); i++) {
		struct generic_pm_domain *dom = &power->domains[i].base;

		if (dom->name)
			pm_genpd_remove(dom);
	}
	return ret;
}

static struct platform_driver bcm2835_power_driver = {
	.probe		= bcm2835_power_probe,
	.driver = {
		.name =	"bcm2835-power",
	},
};
module_platform_driver(bcm2835_power_driver);

MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("Driver for Broadcom BCM2835 PM power domains and reset");
