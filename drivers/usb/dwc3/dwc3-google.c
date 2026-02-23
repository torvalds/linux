// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-google.c - Google DWC3 Specific Glue Layer
 *
 * Copyright (c) 2025, Google LLC
 * Author: Roy Luo <royluo@google.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include "core.h"
#include "glue.h"

/* HOST CFG registers */
#define HC_STATUS_OFFSET 0x0
#define HC_STATUS_CURRENT_POWER_STATE_U2PMU GENMASK(1, 0)
#define HC_STATUS_CURRENT_POWER_STATE_U3PMU GENMASK(4, 3)

#define HOST_CFG1_OFFSET 0x4
#define HOST_CFG1_PME_EN BIT(3)
#define HOST_CFG1_PM_POWER_STATE_REQUEST GENMASK(5, 4)
#define HOST_CFG1_PM_POWER_STATE_D0 0x0
#define HOST_CFG1_PM_POWER_STATE_D3 0x3

/* USBINT registers */
#define USBINT_CFG1_OFFSET 0x0
#define USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_MSK BIT(2)
#define USBINT_CFG1_USBDRD_PME_GEN_U3P_INTR_MSK BIT(3)
#define USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_INT_EN BIT(8)
#define USBINT_CFG1_USBDRD_PME_GEN_U3P_INTR_INT_EN BIT(9)
#define USBINT_CFG1_USBDRD_PME_GEN_U2_INTR_CLR BIT(14)
#define USBINT_CFG1_USBDRD_PME_GEN_U3_INTR_CLR BIT(15)

#define USBINT_STATUS_OFFSET 0x4
#define USBINT_STATUS_USBDRD_PME_GEN_U2P_INTR_STS_RAW BIT(2)
#define USBINT_STATUS_USBDRD_PME_GEN_U3P_INTR_STS_RAW BIT(3)

#define USBCS_TOP_CTRL_CFG1_OFFSET 0xc
#define USBCS_TOP_CTRL_CFG1_USB2ONLY_MODE BIT(5)

#define DWC3_GOOGLE_MAX_RESETS	4

struct dwc3_google {
	struct device		*dev;
	struct dwc3		dwc;
	struct clk_bulk_data	*clks;
	int			num_clks;
	struct reset_control_bulk_data rsts[DWC3_GOOGLE_MAX_RESETS];
	int			num_rsts;
	struct reset_control	*non_sticky_rst;
	struct device		*usb_psw_pd;
	struct device_link	*usb_psw_pd_dl;
	struct notifier_block	usb_psw_pd_nb;
	struct device		*usb_top_pd;
	struct device_link	*usb_top_pd_dl;
	struct regmap		*usb_cfg_regmap;
	unsigned int		host_cfg_offset;
	unsigned int		usbint_cfg_offset;
	int			hs_pme_irq;
	int			ss_pme_irq;
	bool			is_usb2only;
	bool			is_hibernation;
};

#define to_dwc3_google(d) container_of_const((d), struct dwc3_google, dwc)

static int dwc3_google_rst_init(struct dwc3_google *google)
{
	int ret;

	google->num_rsts = 4;
	google->rsts[0].id = "non_sticky";
	google->rsts[1].id = "sticky";
	google->rsts[2].id = "drd_bus";
	google->rsts[3].id = "top";

	ret = devm_reset_control_bulk_get_exclusive(google->dev,
						    google->num_rsts,
						    google->rsts);

	if (ret < 0)
		return ret;

	google->non_sticky_rst = google->rsts[0].rstc;

	return 0;
}

static int dwc3_google_set_pmu_state(struct dwc3_google *google, int state)
{
	u32 reg;
	int ret;

	regmap_read(google->usb_cfg_regmap,
		    google->host_cfg_offset + HOST_CFG1_OFFSET, &reg);

	reg &= ~HOST_CFG1_PM_POWER_STATE_REQUEST;
	reg |= (FIELD_PREP(HOST_CFG1_PM_POWER_STATE_REQUEST, state) |
		HOST_CFG1_PME_EN);
	regmap_write(google->usb_cfg_regmap,
		     google->host_cfg_offset + HOST_CFG1_OFFSET, reg);

	ret = regmap_read_poll_timeout(google->usb_cfg_regmap,
				       google->host_cfg_offset + HC_STATUS_OFFSET, reg,
				       (FIELD_GET(HC_STATUS_CURRENT_POWER_STATE_U2PMU,
						  reg) == state &&
					FIELD_GET(HC_STATUS_CURRENT_POWER_STATE_U3PMU,
						  reg) == state),
				       10, 10000);

	if (ret)
		dev_err(google->dev, "failed to set PMU state %d\n", state);

	return ret;
}

/*
 * Clear pme interrupts and report their status.
 * The hardware requires write-1 then write-0 sequence to clear the interrupt bits.
 */
static u32 dwc3_google_clear_pme_irqs(struct dwc3_google *google)
{
	u32 irq_status, reg_set, reg_clear;

	regmap_read(google->usb_cfg_regmap,
		    google->usbint_cfg_offset + USBINT_STATUS_OFFSET, &irq_status);

	irq_status &= (USBINT_STATUS_USBDRD_PME_GEN_U2P_INTR_STS_RAW |
		       USBINT_STATUS_USBDRD_PME_GEN_U3P_INTR_STS_RAW);
	if (!irq_status)
		return irq_status;

	regmap_read(google->usb_cfg_regmap,
		    google->usbint_cfg_offset + USBINT_CFG1_OFFSET, &reg_set);

	reg_clear = reg_set;
	if (irq_status & USBINT_STATUS_USBDRD_PME_GEN_U2P_INTR_STS_RAW) {
		reg_set |= USBINT_CFG1_USBDRD_PME_GEN_U2_INTR_CLR;
		reg_clear &= ~USBINT_CFG1_USBDRD_PME_GEN_U2_INTR_CLR;
	}
	if (irq_status & USBINT_STATUS_USBDRD_PME_GEN_U3P_INTR_STS_RAW) {
		reg_set |= USBINT_CFG1_USBDRD_PME_GEN_U3_INTR_CLR;
		reg_clear &= ~USBINT_CFG1_USBDRD_PME_GEN_U3_INTR_CLR;
	}

	regmap_write(google->usb_cfg_regmap,
		     google->usbint_cfg_offset + USBINT_CFG1_OFFSET, reg_set);
	regmap_write(google->usb_cfg_regmap,
		     google->usbint_cfg_offset + USBINT_CFG1_OFFSET, reg_clear);

	return irq_status;
}

static void dwc3_google_enable_pme_irq(struct dwc3_google *google)
{
	u32 reg;

	regmap_read(google->usb_cfg_regmap,
		    google->usbint_cfg_offset + USBINT_CFG1_OFFSET, &reg);
	reg &= ~(USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_MSK |
		 USBINT_CFG1_USBDRD_PME_GEN_U3P_INTR_MSK);
	reg |= (USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_INT_EN |
		USBINT_CFG1_USBDRD_PME_GEN_U3P_INTR_INT_EN);
	regmap_write(google->usb_cfg_regmap,
		     google->usbint_cfg_offset + USBINT_CFG1_OFFSET, reg);

	enable_irq(google->hs_pme_irq);
	enable_irq(google->ss_pme_irq);
	enable_irq_wake(google->hs_pme_irq);
	enable_irq_wake(google->ss_pme_irq);
}

static void dwc3_google_disable_pme_irq(struct dwc3_google *google)
{
	u32 reg;

	regmap_read(google->usb_cfg_regmap,
		    google->usbint_cfg_offset + USBINT_CFG1_OFFSET, &reg);
	reg &= ~(USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_INT_EN |
		 USBINT_CFG1_USBDRD_PME_GEN_U3P_INTR_INT_EN);
	reg |= (USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_MSK |
		USBINT_CFG1_USBDRD_PME_GEN_U3P_INTR_MSK);
	regmap_write(google->usb_cfg_regmap,
		     google->usbint_cfg_offset + USBINT_CFG1_OFFSET, reg);

	disable_irq_wake(google->hs_pme_irq);
	disable_irq_wake(google->ss_pme_irq);
	disable_irq_nosync(google->hs_pme_irq);
	disable_irq_nosync(google->ss_pme_irq);
}

static irqreturn_t dwc3_google_resume_irq(int irq, void *data)
{
	struct dwc3_google      *google = data;
	struct dwc3             *dwc = &google->dwc;
	u32 irq_status, dr_role;

	irq_status = dwc3_google_clear_pme_irqs(google);
	dr_role = dwc->current_dr_role;

	if (!irq_status || !google->is_hibernation ||
	    dr_role != DWC3_GCTL_PRTCAP_HOST) {
		dev_dbg(google->dev, "spurious pme irq %d, hibernation %d, dr_role %u\n",
			irq, google->is_hibernation, dr_role);
		return IRQ_HANDLED;
	}

	if (dwc->xhci)
		pm_runtime_resume(&dwc->xhci->dev);

	return IRQ_HANDLED;
}

static int dwc3_google_request_irq(struct dwc3_google *google, struct platform_device *pdev,
				   const char *irq_name, const char *req_name)
{
	int ret;
	int irq;

	irq = platform_get_irq_byname(pdev, irq_name);
	if (irq < 0)
		return irq;

	irq_set_status_flags(irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(google->dev, irq, NULL,
					dwc3_google_resume_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					req_name, google);
	if (ret < 0) {
		dev_err(google->dev, "failed to request irq %s\n", req_name);
		return ret;
	}

	return irq;
}

static int dwc3_google_usb_psw_pd_notifier(struct notifier_block *nb, unsigned long action, void *d)
{
	struct dwc3_google *google = container_of(nb, struct dwc3_google, usb_psw_pd_nb);
	int ret;

	if (!google->is_hibernation)
		return NOTIFY_OK;

	if (action == GENPD_NOTIFY_OFF) {
		dev_dbg(google->dev, "enter D3 power state\n");
		dwc3_google_set_pmu_state(google, HOST_CFG1_PM_POWER_STATE_D3);
		ret = reset_control_assert(google->non_sticky_rst);
		if (ret)
			dev_err(google->dev, "non sticky reset assert failed: %d\n", ret);
	} else if (action == GENPD_NOTIFY_ON) {
		dev_dbg(google->dev, "enter D0 power state\n");
		dwc3_google_clear_pme_irqs(google);
		ret = reset_control_deassert(google->non_sticky_rst);
		if (ret)
			dev_err(google->dev, "non sticky reset deassert failed: %d\n", ret);
		dwc3_google_set_pmu_state(google, HOST_CFG1_PM_POWER_STATE_D0);
	}

	return NOTIFY_OK;
}

static void dwc3_google_pm_domain_deinit(struct dwc3_google *google)
{
	if (google->usb_top_pd_dl)
		device_link_del(google->usb_top_pd_dl);

	if (!IS_ERR_OR_NULL(google->usb_top_pd)) {
		device_set_wakeup_capable(google->usb_top_pd, false);
		dev_pm_domain_detach(google->usb_top_pd, true);
	}

	if (google->usb_psw_pd_dl)
		device_link_del(google->usb_psw_pd_dl);

	if (!IS_ERR_OR_NULL(google->usb_psw_pd)) {
		dev_pm_genpd_remove_notifier(google->usb_psw_pd);
		dev_pm_domain_detach(google->usb_psw_pd, true);
	}
}

static int dwc3_google_pm_domain_init(struct dwc3_google *google)
{
	int ret;

	/*
	 * Establish PM RUNTIME link between dwc dev and its power domain usb_psw_pd,
	 * register notifier block to handle hibernation.
	 */
	google->usb_psw_pd = dev_pm_domain_attach_by_name(google->dev, "psw");
	if (IS_ERR_OR_NULL(google->usb_psw_pd)) {
		dev_err(google->dev, "failed to get psw pd");
		ret = google->usb_psw_pd ? PTR_ERR(google->usb_psw_pd) : -ENODATA;
		return ret;
	}

	google->usb_psw_pd_nb.notifier_call = dwc3_google_usb_psw_pd_notifier;
	ret = dev_pm_genpd_add_notifier(google->usb_psw_pd, &google->usb_psw_pd_nb);
	if (ret) {
		dev_err(google->dev, "failed to add psw pd notifier");
		goto err;
	}

	google->usb_psw_pd_dl = device_link_add(google->dev, google->usb_psw_pd,
						DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME |
						DL_FLAG_RPM_ACTIVE);
	if (!google->usb_psw_pd_dl) {
		dev_err(google->usb_psw_pd, "failed to add device link");
		ret = -ENODEV;
		goto err;
	}

	/*
	 * usb_top_pd is the parent power domain of usb_psw_pd. Keeping usb_top_pd on
	 * while usb_psw_pd is off places the controller in a power-gated state,
	 * essential for hibernation. Acquire a handle to usb_top_pd and sets it as
	 * wakeup-capable to allow the domain to be left on during system suspend.
	 */
	google->usb_top_pd = dev_pm_domain_attach_by_name(google->dev, "top");
	if (IS_ERR_OR_NULL(google->usb_top_pd)) {
		dev_err(google->dev, "failed to get top pd");
		ret = google->usb_top_pd ? PTR_ERR(google->usb_top_pd) : -ENODATA;
		goto err;
	}
	device_set_wakeup_capable(google->usb_top_pd, true);

	google->usb_top_pd_dl = device_link_add(google->dev, google->usb_top_pd,
						DL_FLAG_STATELESS);
	if (!google->usb_top_pd_dl) {
		dev_err(google->usb_top_pd, "failed to add device link");
		ret = -ENODEV;
		goto err;
	}

	return 0;

err:
	dwc3_google_pm_domain_deinit(google);

	return ret;
}

static void dwc3_google_program_usb2only(struct dwc3_google *google)
{
	u32 reg;

	regmap_read(google->usb_cfg_regmap,
		    google->usbint_cfg_offset + USBCS_TOP_CTRL_CFG1_OFFSET, &reg);
	reg |= USBCS_TOP_CTRL_CFG1_USB2ONLY_MODE;
	regmap_write(google->usb_cfg_regmap,
		     google->usbint_cfg_offset + USBCS_TOP_CTRL_CFG1_OFFSET, reg);
}

static int dwc3_google_probe(struct platform_device *pdev)
{
	struct dwc3_probe_data	probe_data = {};
	struct device		*dev = &pdev->dev;
	struct dwc3_google	*google;
	struct resource		*res;
	int			ret;
	u32			args[2];

	google = devm_kzalloc(&pdev->dev, sizeof(*google), GFP_KERNEL);
	if (!google)
		return -ENOMEM;

	google->dev = &pdev->dev;

	ret = dwc3_google_pm_domain_init(google);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to init pdom\n");

	google->usb_cfg_regmap =
		syscon_regmap_lookup_by_phandle_args(dev->of_node,
						     "google,usb-cfg-csr",
						     ARRAY_SIZE(args), args);
	if (IS_ERR(google->usb_cfg_regmap)) {
		return dev_err_probe(dev, PTR_ERR(google->usb_cfg_regmap),
				     "invalid usb cfg csr\n");
	}

	google->host_cfg_offset = args[0];
	google->usbint_cfg_offset = args[1];

	if (device_property_match_string(dev, "phy-names", "usb3-phy") < 0) {
		google->is_usb2only = true;
		dwc3_google_program_usb2only(google);
	}

	ret = devm_clk_bulk_get_all_enabled(dev, &google->clks);
	if (ret < 0) {
		ret = dev_err_probe(dev, ret, "failed to get and enable clks\n");
		goto err_deinit_pdom;
	}
	google->num_clks = ret;

	ret = dwc3_google_rst_init(google);
	if (ret) {
		ret = dev_err_probe(dev, ret, "failed to get resets\n");
		goto err_deinit_pdom;
	}

	ret = reset_control_bulk_deassert(google->num_rsts, google->rsts);
	if (ret) {
		ret = dev_err_probe(dev, ret, "failed to deassert rsts\n");
		goto err_deinit_pdom;
	}

	ret = dwc3_google_request_irq(google, pdev, "hs_pme", "USB HS wakeup");
	if (ret < 0) {
		ret = dev_err_probe(dev, ret, "failed to request hs pme irq");
		goto err_reset_assert;
	}
	google->hs_pme_irq = ret;

	ret = dwc3_google_request_irq(google, pdev, "ss_pme", "USB SS wakeup");
	if (ret < 0) {
		ret = dev_err_probe(dev, ret, "failed to request ss pme irq");
		goto err_reset_assert;
	}
	google->ss_pme_irq = ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = dev_err_probe(dev, -ENODEV, "invalid memory\n");
		goto err_reset_assert;
	}

	device_init_wakeup(dev, true);

	google->dwc.dev = dev;
	probe_data.dwc = &google->dwc;
	probe_data.res = res;
	probe_data.ignore_clocks_and_resets = true;
	ret = dwc3_core_probe(&probe_data);
	if (ret)  {
		ret = dev_err_probe(dev, ret, "failed to register DWC3 Core\n");
		goto err_reset_assert;
	}

	return 0;

err_reset_assert:
	reset_control_bulk_assert(google->num_rsts, google->rsts);

err_deinit_pdom:
	dwc3_google_pm_domain_deinit(google);

	return ret;
}

static void dwc3_google_remove(struct platform_device *pdev)
{
	struct dwc3 *dwc = platform_get_drvdata(pdev);
	struct dwc3_google *google = to_dwc3_google(dwc);

	dwc3_core_remove(&google->dwc);

	reset_control_bulk_assert(google->num_rsts, google->rsts);

	dwc3_google_pm_domain_deinit(google);
}

static int dwc3_google_suspend(struct dwc3_google *google, pm_message_t msg)
{
	if (pm_runtime_suspended(google->dev))
		return 0;

	if (google->dwc.current_dr_role == DWC3_GCTL_PRTCAP_HOST) {
		/*
		 * Follow dwc3_suspend_common() guidelines for deciding between
		 * a full teardown and hibernation.
		 */
		if (PMSG_IS_AUTO(msg) || device_may_wakeup(google->dev)) {
			dev_dbg(google->dev, "enter hibernation");
			pm_runtime_get_sync(google->usb_top_pd);
			device_wakeup_enable(google->usb_top_pd);
			dwc3_google_enable_pme_irq(google);
			google->is_hibernation = true;
			return 0;
		}
	}

	reset_control_bulk_assert(google->num_rsts, google->rsts);
	clk_bulk_disable_unprepare(google->num_clks, google->clks);

	return 0;
}

static int dwc3_google_resume(struct dwc3_google *google, pm_message_t msg)
{
	int ret;

	if (google->is_hibernation) {
		dev_dbg(google->dev, "exit hibernation");
		dwc3_google_disable_pme_irq(google);
		device_wakeup_disable(google->usb_top_pd);
		pm_runtime_put_sync(google->usb_top_pd);
		google->is_hibernation = false;
		return 0;
	}

	if (google->is_usb2only)
		dwc3_google_program_usb2only(google);

	ret = clk_bulk_prepare_enable(google->num_clks, google->clks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(google->num_rsts, google->rsts);
	if (ret) {
		clk_bulk_disable_unprepare(google->num_clks, google->clks);
		return ret;
	}

	return 0;
}

static int dwc3_google_pm_suspend(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_google *google = to_dwc3_google(dwc);
	int ret;

	ret = dwc3_pm_suspend(&google->dwc);
	if (ret)
		return ret;

	return dwc3_google_suspend(google, PMSG_SUSPEND);
}

static int dwc3_google_pm_resume(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_google *google = to_dwc3_google(dwc);
	int ret;

	ret = dwc3_google_resume(google, PMSG_RESUME);
	if (ret)
		return ret;

	return dwc3_pm_resume(&google->dwc);
}

static void dwc3_google_complete(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);

	dwc3_pm_complete(dwc);
}

static int dwc3_google_prepare(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);

	return dwc3_pm_prepare(dwc);
}

static int dwc3_google_runtime_suspend(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_google *google = to_dwc3_google(dwc);
	int ret;

	ret = dwc3_runtime_suspend(&google->dwc);
	if (ret)
		return ret;

	return dwc3_google_suspend(google, PMSG_AUTO_SUSPEND);
}

static int dwc3_google_runtime_resume(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_google *google = to_dwc3_google(dwc);
	int ret;

	ret = dwc3_google_resume(google, PMSG_AUTO_RESUME);
	if (ret)
		return ret;

	return dwc3_runtime_resume(&google->dwc);
}

static int dwc3_google_runtime_idle(struct device *dev)
{
	return dwc3_runtime_idle(dev_get_drvdata(dev));
}

static const struct dev_pm_ops dwc3_google_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dwc3_google_pm_suspend, dwc3_google_pm_resume)
	RUNTIME_PM_OPS(dwc3_google_runtime_suspend, dwc3_google_runtime_resume,
		       dwc3_google_runtime_idle)
	.complete = pm_sleep_ptr(dwc3_google_complete),
	.prepare = pm_sleep_ptr(dwc3_google_prepare),
};

static const struct of_device_id dwc3_google_of_match[] = {
	{ .compatible = "google,lga-dwc3" },
	{ }
};
MODULE_DEVICE_TABLE(of, dwc3_google_of_match);

static struct platform_driver dwc3_google_driver = {
	.probe		= dwc3_google_probe,
	.remove		= dwc3_google_remove,
	.driver		= {
		.name	= "dwc3-google",
		.pm	= pm_ptr(&dwc3_google_dev_pm_ops),
		.of_match_table	= dwc3_google_of_match,
	},
};

module_platform_driver(dwc3_google_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare DWC3 Google Glue Driver");
