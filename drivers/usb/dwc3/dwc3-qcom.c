// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Inspired by dwc3-of-simple.c
 */

#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/of_clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/extcon.h>
#include <linux/interconnect.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/usb/of.h>
#include <linux/reset.h>
#include <linux/iopoll.h>
#include <linux/usb/hcd.h>
#include <linux/usb.h>
#include "core.h"

/* USB QSCRATCH Hardware registers */
#define QSCRATCH_HS_PHY_CTRL			0x10
#define UTMI_OTG_VBUS_VALID			BIT(20)
#define SW_SESSVLD_SEL				BIT(28)

#define QSCRATCH_SS_PHY_CTRL			0x30
#define LANE0_PWR_PRESENT			BIT(24)

#define QSCRATCH_GENERAL_CFG			0x08
#define PIPE_UTMI_CLK_SEL			BIT(0)
#define PIPE3_PHYSTATUS_SW			BIT(3)
#define PIPE_UTMI_CLK_DIS			BIT(8)

#define PWR_EVNT_IRQ_STAT_REG			0x58
#define PWR_EVNT_LPM_IN_L2_MASK			BIT(4)
#define PWR_EVNT_LPM_OUT_L2_MASK		BIT(5)

#define SDM845_QSCRATCH_BASE_OFFSET		0xf8800
#define SDM845_QSCRATCH_SIZE			0x400
#define SDM845_DWC3_CORE_SIZE			0xcd00

/* Interconnect path bandwidths in MBps */
#define USB_MEMORY_AVG_HS_BW MBps_to_icc(240)
#define USB_MEMORY_PEAK_HS_BW MBps_to_icc(700)
#define USB_MEMORY_AVG_SS_BW  MBps_to_icc(1000)
#define USB_MEMORY_PEAK_SS_BW MBps_to_icc(2500)
#define APPS_USB_AVG_BW 0
#define APPS_USB_PEAK_BW MBps_to_icc(40)

struct dwc3_acpi_pdata {
	u32			qscratch_base_offset;
	u32			qscratch_base_size;
	u32			dwc3_core_base_size;
	int			hs_phy_irq_index;
	int			dp_hs_phy_irq_index;
	int			dm_hs_phy_irq_index;
	int			ss_phy_irq_index;
	bool			is_urs;
};

struct dwc3_qcom {
	struct device		*dev;
	void __iomem		*qscratch_base;
	struct platform_device	*dwc3;
	struct platform_device	*urs_usb;
	struct clk		**clks;
	int			num_clocks;
	struct reset_control	*resets;

	int			hs_phy_irq;
	int			dp_hs_phy_irq;
	int			dm_hs_phy_irq;
	int			ss_phy_irq;
	enum usb_device_speed	usb2_speed;

	struct extcon_dev	*edev;
	struct extcon_dev	*host_edev;
	struct notifier_block	vbus_nb;
	struct notifier_block	host_nb;

	const struct dwc3_acpi_pdata *acpi_pdata;

	enum usb_dr_mode	mode;
	bool			is_suspended;
	bool			pm_suspended;
	struct icc_path		*icc_path_ddr;
	struct icc_path		*icc_path_apps;
};

static inline void dwc3_qcom_setbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg |= val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

static inline void dwc3_qcom_clrbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg &= ~val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

static void dwc3_qcom_vbus_override_enable(struct dwc3_qcom *qcom, bool enable)
{
	if (enable) {
		dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_SS_PHY_CTRL,
				  LANE0_PWR_PRESENT);
		dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_HS_PHY_CTRL,
				  UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL);
	} else {
		dwc3_qcom_clrbits(qcom->qscratch_base, QSCRATCH_SS_PHY_CTRL,
				  LANE0_PWR_PRESENT);
		dwc3_qcom_clrbits(qcom->qscratch_base, QSCRATCH_HS_PHY_CTRL,
				  UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL);
	}
}

static int dwc3_qcom_vbus_notifier(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct dwc3_qcom *qcom = container_of(nb, struct dwc3_qcom, vbus_nb);

	/* enable vbus override for device mode */
	dwc3_qcom_vbus_override_enable(qcom, event);
	qcom->mode = event ? USB_DR_MODE_PERIPHERAL : USB_DR_MODE_HOST;

	return NOTIFY_DONE;
}

static int dwc3_qcom_host_notifier(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct dwc3_qcom *qcom = container_of(nb, struct dwc3_qcom, host_nb);

	/* disable vbus override in host mode */
	dwc3_qcom_vbus_override_enable(qcom, !event);
	qcom->mode = event ? USB_DR_MODE_HOST : USB_DR_MODE_PERIPHERAL;

	return NOTIFY_DONE;
}

static int dwc3_qcom_register_extcon(struct dwc3_qcom *qcom)
{
	struct device		*dev = qcom->dev;
	struct extcon_dev	*host_edev;
	int			ret;

	if (!of_property_read_bool(dev->of_node, "extcon"))
		return 0;

	qcom->edev = extcon_get_edev_by_phandle(dev, 0);
	if (IS_ERR(qcom->edev))
		return PTR_ERR(qcom->edev);

	qcom->vbus_nb.notifier_call = dwc3_qcom_vbus_notifier;

	qcom->host_edev = extcon_get_edev_by_phandle(dev, 1);
	if (IS_ERR(qcom->host_edev))
		qcom->host_edev = NULL;

	ret = devm_extcon_register_notifier(dev, qcom->edev, EXTCON_USB,
					    &qcom->vbus_nb);
	if (ret < 0) {
		dev_err(dev, "VBUS notifier register failed\n");
		return ret;
	}

	if (qcom->host_edev)
		host_edev = qcom->host_edev;
	else
		host_edev = qcom->edev;

	qcom->host_nb.notifier_call = dwc3_qcom_host_notifier;
	ret = devm_extcon_register_notifier(dev, host_edev, EXTCON_USB_HOST,
					    &qcom->host_nb);
	if (ret < 0) {
		dev_err(dev, "Host notifier register failed\n");
		return ret;
	}

	/* Update initial VBUS override based on extcon state */
	if (extcon_get_state(qcom->edev, EXTCON_USB) ||
	    !extcon_get_state(host_edev, EXTCON_USB_HOST))
		dwc3_qcom_vbus_notifier(&qcom->vbus_nb, true, qcom->edev);
	else
		dwc3_qcom_vbus_notifier(&qcom->vbus_nb, false, qcom->edev);

	return 0;
}

static int dwc3_qcom_interconnect_enable(struct dwc3_qcom *qcom)
{
	int ret;

	ret = icc_enable(qcom->icc_path_ddr);
	if (ret)
		return ret;

	ret = icc_enable(qcom->icc_path_apps);
	if (ret)
		icc_disable(qcom->icc_path_ddr);

	return ret;
}

static int dwc3_qcom_interconnect_disable(struct dwc3_qcom *qcom)
{
	int ret;

	ret = icc_disable(qcom->icc_path_ddr);
	if (ret)
		return ret;

	ret = icc_disable(qcom->icc_path_apps);
	if (ret)
		icc_enable(qcom->icc_path_ddr);

	return ret;
}

/**
 * dwc3_qcom_interconnect_init() - Get interconnect path handles
 * and set bandwidth.
 * @qcom:			Pointer to the concerned usb core.
 *
 */
static int dwc3_qcom_interconnect_init(struct dwc3_qcom *qcom)
{
	enum usb_device_speed max_speed;
	struct device *dev = qcom->dev;
	int ret;

	if (has_acpi_companion(dev))
		return 0;

	qcom->icc_path_ddr = of_icc_get(dev, "usb-ddr");
	if (IS_ERR(qcom->icc_path_ddr)) {
		dev_err(dev, "failed to get usb-ddr path: %ld\n",
				PTR_ERR(qcom->icc_path_ddr));
		return PTR_ERR(qcom->icc_path_ddr);
	}

	qcom->icc_path_apps = of_icc_get(dev, "apps-usb");
	if (IS_ERR(qcom->icc_path_apps)) {
		dev_err(dev, "failed to get apps-usb path: %ld\n",
				PTR_ERR(qcom->icc_path_apps));
		ret = PTR_ERR(qcom->icc_path_apps);
		goto put_path_ddr;
	}

	max_speed = usb_get_maximum_speed(&qcom->dwc3->dev);
	if (max_speed >= USB_SPEED_SUPER || max_speed == USB_SPEED_UNKNOWN) {
		ret = icc_set_bw(qcom->icc_path_ddr,
				USB_MEMORY_AVG_SS_BW, USB_MEMORY_PEAK_SS_BW);
	} else {
		ret = icc_set_bw(qcom->icc_path_ddr,
				USB_MEMORY_AVG_HS_BW, USB_MEMORY_PEAK_HS_BW);
	}
	if (ret) {
		dev_err(dev, "failed to set bandwidth for usb-ddr path: %d\n", ret);
		goto put_path_apps;
	}

	ret = icc_set_bw(qcom->icc_path_apps, APPS_USB_AVG_BW, APPS_USB_PEAK_BW);
	if (ret) {
		dev_err(dev, "failed to set bandwidth for apps-usb path: %d\n", ret);
		goto put_path_apps;
	}

	return 0;

put_path_apps:
	icc_put(qcom->icc_path_apps);
put_path_ddr:
	icc_put(qcom->icc_path_ddr);
	return ret;
}

/**
 * dwc3_qcom_interconnect_exit() - Release interconnect path handles
 * @qcom:			Pointer to the concerned usb core.
 *
 * This function is used to release interconnect path handle.
 */
static void dwc3_qcom_interconnect_exit(struct dwc3_qcom *qcom)
{
	icc_put(qcom->icc_path_ddr);
	icc_put(qcom->icc_path_apps);
}

/* Only usable in contexts where the role can not change. */
static bool dwc3_qcom_is_host(struct dwc3_qcom *qcom)
{
	struct dwc3 *dwc;

	/*
	 * FIXME: Fix this layering violation.
	 */
	dwc = platform_get_drvdata(qcom->dwc3);

	/* Core driver may not have probed yet. */
	if (!dwc)
		return false;

	return dwc->xhci;
}

static enum usb_device_speed dwc3_qcom_read_usb2_speed(struct dwc3_qcom *qcom)
{
	struct dwc3 *dwc = platform_get_drvdata(qcom->dwc3);
	struct usb_device *udev;
	struct usb_hcd __maybe_unused *hcd;

	/*
	 * FIXME: Fix this layering violation.
	 */
	hcd = platform_get_drvdata(dwc->xhci);

	/*
	 * It is possible to query the speed of all children of
	 * USB2.0 root hub via usb_hub_for_each_child(). DWC3 code
	 * currently supports only 1 port per controller. So
	 * this is sufficient.
	 */
#ifdef CONFIG_USB
	udev = usb_hub_find_child(hcd->self.root_hub, 1);
#else
	udev = NULL;
#endif
	if (!udev)
		return USB_SPEED_UNKNOWN;

	return udev->speed;
}

static void dwc3_qcom_enable_wakeup_irq(int irq, unsigned int polarity)
{
	if (!irq)
		return;

	if (polarity)
		irq_set_irq_type(irq, polarity);

	enable_irq(irq);
	enable_irq_wake(irq);
}

static void dwc3_qcom_disable_wakeup_irq(int irq)
{
	if (!irq)
		return;

	disable_irq_wake(irq);
	disable_irq_nosync(irq);
}

static void dwc3_qcom_disable_interrupts(struct dwc3_qcom *qcom)
{
	dwc3_qcom_disable_wakeup_irq(qcom->hs_phy_irq);

	if (qcom->usb2_speed == USB_SPEED_LOW) {
		dwc3_qcom_disable_wakeup_irq(qcom->dm_hs_phy_irq);
	} else if ((qcom->usb2_speed == USB_SPEED_HIGH) ||
			(qcom->usb2_speed == USB_SPEED_FULL)) {
		dwc3_qcom_disable_wakeup_irq(qcom->dp_hs_phy_irq);
	} else {
		dwc3_qcom_disable_wakeup_irq(qcom->dp_hs_phy_irq);
		dwc3_qcom_disable_wakeup_irq(qcom->dm_hs_phy_irq);
	}

	dwc3_qcom_disable_wakeup_irq(qcom->ss_phy_irq);
}

static void dwc3_qcom_enable_interrupts(struct dwc3_qcom *qcom)
{
	dwc3_qcom_enable_wakeup_irq(qcom->hs_phy_irq, 0);

	/*
	 * Configure DP/DM line interrupts based on the USB2 device attached to
	 * the root hub port. When HS/FS device is connected, configure the DP line
	 * as falling edge to detect both disconnect and remote wakeup scenarios. When
	 * LS device is connected, configure DM line as falling edge to detect both
	 * disconnect and remote wakeup. When no device is connected, configure both
	 * DP and DM lines as rising edge to detect HS/HS/LS device connect scenario.
	 */

	if (qcom->usb2_speed == USB_SPEED_LOW) {
		dwc3_qcom_enable_wakeup_irq(qcom->dm_hs_phy_irq,
						IRQ_TYPE_EDGE_FALLING);
	} else if ((qcom->usb2_speed == USB_SPEED_HIGH) ||
			(qcom->usb2_speed == USB_SPEED_FULL)) {
		dwc3_qcom_enable_wakeup_irq(qcom->dp_hs_phy_irq,
						IRQ_TYPE_EDGE_FALLING);
	} else {
		dwc3_qcom_enable_wakeup_irq(qcom->dp_hs_phy_irq,
						IRQ_TYPE_EDGE_RISING);
		dwc3_qcom_enable_wakeup_irq(qcom->dm_hs_phy_irq,
						IRQ_TYPE_EDGE_RISING);
	}

	dwc3_qcom_enable_wakeup_irq(qcom->ss_phy_irq, 0);
}

static int dwc3_qcom_suspend(struct dwc3_qcom *qcom, bool wakeup)
{
	u32 val;
	int i, ret;

	if (qcom->is_suspended)
		return 0;

	val = readl(qcom->qscratch_base + PWR_EVNT_IRQ_STAT_REG);
	if (!(val & PWR_EVNT_LPM_IN_L2_MASK))
		dev_err(qcom->dev, "HS-PHY not in L2\n");

	for (i = qcom->num_clocks - 1; i >= 0; i--)
		clk_disable_unprepare(qcom->clks[i]);

	ret = dwc3_qcom_interconnect_disable(qcom);
	if (ret)
		dev_warn(qcom->dev, "failed to disable interconnect: %d\n", ret);

	/*
	 * The role is stable during suspend as role switching is done from a
	 * freezable workqueue.
	 */
	if (dwc3_qcom_is_host(qcom) && wakeup) {
		qcom->usb2_speed = dwc3_qcom_read_usb2_speed(qcom);
		dwc3_qcom_enable_interrupts(qcom);
	}

	qcom->is_suspended = true;

	return 0;
}

static int dwc3_qcom_resume(struct dwc3_qcom *qcom, bool wakeup)
{
	int ret;
	int i;

	if (!qcom->is_suspended)
		return 0;

	if (dwc3_qcom_is_host(qcom) && wakeup)
		dwc3_qcom_disable_interrupts(qcom);

	for (i = 0; i < qcom->num_clocks; i++) {
		ret = clk_prepare_enable(qcom->clks[i]);
		if (ret < 0) {
			while (--i >= 0)
				clk_disable_unprepare(qcom->clks[i]);
			return ret;
		}
	}

	ret = dwc3_qcom_interconnect_enable(qcom);
	if (ret)
		dev_warn(qcom->dev, "failed to enable interconnect: %d\n", ret);

	/* Clear existing events from PHY related to L2 in/out */
	dwc3_qcom_setbits(qcom->qscratch_base, PWR_EVNT_IRQ_STAT_REG,
			  PWR_EVNT_LPM_IN_L2_MASK | PWR_EVNT_LPM_OUT_L2_MASK);

	qcom->is_suspended = false;

	return 0;
}

static irqreturn_t qcom_dwc3_resume_irq(int irq, void *data)
{
	struct dwc3_qcom *qcom = data;
	struct dwc3	*dwc = platform_get_drvdata(qcom->dwc3);

	/* If pm_suspended then let pm_resume take care of resuming h/w */
	if (qcom->pm_suspended)
		return IRQ_HANDLED;

	/*
	 * This is safe as role switching is done from a freezable workqueue
	 * and the wakeup interrupts are disabled as part of resume.
	 */
	if (dwc3_qcom_is_host(qcom))
		pm_runtime_resume(&dwc->xhci->dev);

	return IRQ_HANDLED;
}

static void dwc3_qcom_select_utmi_clk(struct dwc3_qcom *qcom)
{
	/* Configure dwc3 to use UTMI clock as PIPE clock not present */
	dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_GENERAL_CFG,
			  PIPE_UTMI_CLK_DIS);

	usleep_range(100, 1000);

	dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_GENERAL_CFG,
			  PIPE_UTMI_CLK_SEL | PIPE3_PHYSTATUS_SW);

	usleep_range(100, 1000);

	dwc3_qcom_clrbits(qcom->qscratch_base, QSCRATCH_GENERAL_CFG,
			  PIPE_UTMI_CLK_DIS);
}

static int dwc3_qcom_get_irq(struct platform_device *pdev,
			     const char *name, int num)
{
	struct dwc3_qcom *qcom = platform_get_drvdata(pdev);
	struct platform_device *pdev_irq = qcom->urs_usb ? qcom->urs_usb : pdev;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (np)
		ret = platform_get_irq_byname_optional(pdev_irq, name);
	else
		ret = platform_get_irq_optional(pdev_irq, num);

	return ret;
}

static int dwc3_qcom_setup_irq(struct platform_device *pdev)
{
	struct dwc3_qcom *qcom = platform_get_drvdata(pdev);
	const struct dwc3_acpi_pdata *pdata = qcom->acpi_pdata;
	int irq;
	int ret;

	irq = dwc3_qcom_get_irq(pdev, "hs_phy_irq",
				pdata ? pdata->hs_phy_irq_index : -1);
	if (irq > 0) {
		/* Keep wakeup interrupts disabled until suspend */
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"qcom_dwc3 HS", qcom);
		if (ret) {
			dev_err(qcom->dev, "hs_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->hs_phy_irq = irq;
	}

	irq = dwc3_qcom_get_irq(pdev, "dp_hs_phy_irq",
				pdata ? pdata->dp_hs_phy_irq_index : -1);
	if (irq > 0) {
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"qcom_dwc3 DP_HS", qcom);
		if (ret) {
			dev_err(qcom->dev, "dp_hs_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->dp_hs_phy_irq = irq;
	}

	irq = dwc3_qcom_get_irq(pdev, "dm_hs_phy_irq",
				pdata ? pdata->dm_hs_phy_irq_index : -1);
	if (irq > 0) {
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"qcom_dwc3 DM_HS", qcom);
		if (ret) {
			dev_err(qcom->dev, "dm_hs_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->dm_hs_phy_irq = irq;
	}

	irq = dwc3_qcom_get_irq(pdev, "ss_phy_irq",
				pdata ? pdata->ss_phy_irq_index : -1);
	if (irq > 0) {
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"qcom_dwc3 SS", qcom);
		if (ret) {
			dev_err(qcom->dev, "ss_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->ss_phy_irq = irq;
	}

	return 0;
}

static int dwc3_qcom_clk_init(struct dwc3_qcom *qcom, int count)
{
	struct device		*dev = qcom->dev;
	struct device_node	*np = dev->of_node;
	int			i;

	if (!np || !count)
		return 0;

	if (count < 0)
		return count;

	qcom->num_clocks = count;

	qcom->clks = devm_kcalloc(dev, qcom->num_clocks,
				  sizeof(struct clk *), GFP_KERNEL);
	if (!qcom->clks)
		return -ENOMEM;

	for (i = 0; i < qcom->num_clocks; i++) {
		struct clk	*clk;
		int		ret;

		clk = of_clk_get(np, i);
		if (IS_ERR(clk)) {
			while (--i >= 0)
				clk_put(qcom->clks[i]);
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		if (ret < 0) {
			while (--i >= 0) {
				clk_disable_unprepare(qcom->clks[i]);
				clk_put(qcom->clks[i]);
			}
			clk_put(clk);

			return ret;
		}

		qcom->clks[i] = clk;
	}

	return 0;
}

static const struct property_entry dwc3_qcom_acpi_properties[] = {
	PROPERTY_ENTRY_STRING("dr_mode", "host"),
	{}
};

static const struct software_node dwc3_qcom_swnode = {
	.properties = dwc3_qcom_acpi_properties,
};

static int dwc3_qcom_acpi_register_core(struct platform_device *pdev)
{
	struct dwc3_qcom	*qcom = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;
	struct resource		*res, *child_res = NULL;
	struct platform_device	*pdev_irq = qcom->urs_usb ? qcom->urs_usb :
							    pdev;
	int			irq;
	int			ret;

	qcom->dwc3 = platform_device_alloc("dwc3", PLATFORM_DEVID_AUTO);
	if (!qcom->dwc3)
		return -ENOMEM;

	qcom->dwc3->dev.parent = dev;
	qcom->dwc3->dev.type = dev->type;
	qcom->dwc3->dev.dma_mask = dev->dma_mask;
	qcom->dwc3->dev.dma_parms = dev->dma_parms;
	qcom->dwc3->dev.coherent_dma_mask = dev->coherent_dma_mask;

	child_res = kcalloc(2, sizeof(*child_res), GFP_KERNEL);
	if (!child_res) {
		platform_device_put(qcom->dwc3);
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		ret = -ENODEV;
		goto out;
	}

	child_res[0].flags = res->flags;
	child_res[0].start = res->start;
	child_res[0].end = child_res[0].start +
		qcom->acpi_pdata->dwc3_core_base_size;

	irq = platform_get_irq(pdev_irq, 0);
	if (irq < 0) {
		ret = irq;
		goto out;
	}
	child_res[1].flags = IORESOURCE_IRQ;
	child_res[1].start = child_res[1].end = irq;

	ret = platform_device_add_resources(qcom->dwc3, child_res, 2);
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto out;
	}

	ret = device_add_software_node(&qcom->dwc3->dev, &dwc3_qcom_swnode);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add properties\n");
		goto out;
	}

	ret = platform_device_add(qcom->dwc3);
	if (ret) {
		dev_err(&pdev->dev, "failed to add device\n");
		device_remove_software_node(&qcom->dwc3->dev);
		goto out;
	}
	kfree(child_res);
	return 0;

out:
	platform_device_put(qcom->dwc3);
	kfree(child_res);
	return ret;
}

static int dwc3_qcom_of_register_core(struct platform_device *pdev)
{
	struct dwc3_qcom	*qcom = platform_get_drvdata(pdev);
	struct device_node	*np = pdev->dev.of_node, *dwc3_np;
	struct device		*dev = &pdev->dev;
	int			ret;

	dwc3_np = of_get_compatible_child(np, "snps,dwc3");
	if (!dwc3_np) {
		dev_err(dev, "failed to find dwc3 core child\n");
		return -ENODEV;
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to register dwc3 core - %d\n", ret);
		goto node_put;
	}

	qcom->dwc3 = of_find_device_by_node(dwc3_np);
	if (!qcom->dwc3) {
		ret = -ENODEV;
		dev_err(dev, "failed to get dwc3 platform device\n");
	}

node_put:
	of_node_put(dwc3_np);

	return ret;
}

static struct platform_device *
dwc3_qcom_create_urs_usb_platdev(struct device *dev)
{
	struct fwnode_handle *fwh;
	struct acpi_device *adev;
	char name[8];
	int ret;
	int id;

	/* Figure out device id */
	ret = sscanf(fwnode_get_name(dev->fwnode), "URS%d", &id);
	if (!ret)
		return NULL;

	/* Find the child using name */
	snprintf(name, sizeof(name), "USB%d", id);
	fwh = fwnode_get_named_child_node(dev->fwnode, name);
	if (!fwh)
		return NULL;

	adev = to_acpi_device_node(fwh);
	if (!adev)
		return NULL;

	return acpi_create_platform_device(adev, NULL);
}

static int dwc3_qcom_probe(struct platform_device *pdev)
{
	struct device_node	*np = pdev->dev.of_node;
	struct device		*dev = &pdev->dev;
	struct dwc3_qcom	*qcom;
	struct resource		*res, *parent_res = NULL;
	struct resource		local_res;
	int			ret, i;
	bool			ignore_pipe_clk;
	bool			wakeup_source;

	qcom = devm_kzalloc(&pdev->dev, sizeof(*qcom), GFP_KERNEL);
	if (!qcom)
		return -ENOMEM;

	platform_set_drvdata(pdev, qcom);
	qcom->dev = &pdev->dev;

	if (has_acpi_companion(dev)) {
		qcom->acpi_pdata = acpi_device_get_match_data(dev);
		if (!qcom->acpi_pdata) {
			dev_err(&pdev->dev, "no supporting ACPI device data\n");
			return -EINVAL;
		}
	}

	qcom->resets = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(qcom->resets)) {
		ret = PTR_ERR(qcom->resets);
		dev_err(&pdev->dev, "failed to get resets, err=%d\n", ret);
		return ret;
	}

	ret = reset_control_assert(qcom->resets);
	if (ret) {
		dev_err(&pdev->dev, "failed to assert resets, err=%d\n", ret);
		return ret;
	}

	usleep_range(10, 1000);

	ret = reset_control_deassert(qcom->resets);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert resets, err=%d\n", ret);
		goto reset_assert;
	}

	ret = dwc3_qcom_clk_init(qcom, of_clk_get_parent_count(np));
	if (ret) {
		dev_err(dev, "failed to get clocks\n");
		goto reset_assert;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (np) {
		parent_res = res;
	} else {
		memcpy(&local_res, res, sizeof(struct resource));
		parent_res = &local_res;

		parent_res->start = res->start +
			qcom->acpi_pdata->qscratch_base_offset;
		parent_res->end = parent_res->start +
			qcom->acpi_pdata->qscratch_base_size;

		if (qcom->acpi_pdata->is_urs) {
			qcom->urs_usb = dwc3_qcom_create_urs_usb_platdev(dev);
			if (IS_ERR_OR_NULL(qcom->urs_usb)) {
				dev_err(dev, "failed to create URS USB platdev\n");
				if (!qcom->urs_usb)
					ret = -ENODEV;
				else
					ret = PTR_ERR(qcom->urs_usb);
				goto clk_disable;
			}
		}
	}

	qcom->qscratch_base = devm_ioremap_resource(dev, parent_res);
	if (IS_ERR(qcom->qscratch_base)) {
		ret = PTR_ERR(qcom->qscratch_base);
		goto clk_disable;
	}

	ret = dwc3_qcom_setup_irq(pdev);
	if (ret) {
		dev_err(dev, "failed to setup IRQs, err=%d\n", ret);
		goto clk_disable;
	}

	/*
	 * Disable pipe_clk requirement if specified. Used when dwc3
	 * operates without SSPHY and only HS/FS/LS modes are supported.
	 */
	ignore_pipe_clk = device_property_read_bool(dev,
				"qcom,select-utmi-as-pipe-clk");
	if (ignore_pipe_clk)
		dwc3_qcom_select_utmi_clk(qcom);

	if (np)
		ret = dwc3_qcom_of_register_core(pdev);
	else
		ret = dwc3_qcom_acpi_register_core(pdev);

	if (ret) {
		dev_err(dev, "failed to register DWC3 Core, err=%d\n", ret);
		goto depopulate;
	}

	ret = dwc3_qcom_interconnect_init(qcom);
	if (ret)
		goto depopulate;

	qcom->mode = usb_get_dr_mode(&qcom->dwc3->dev);

	/* enable vbus override for device mode */
	if (qcom->mode != USB_DR_MODE_HOST)
		dwc3_qcom_vbus_override_enable(qcom, true);

	/* register extcon to override sw_vbus on Vbus change later */
	ret = dwc3_qcom_register_extcon(qcom);
	if (ret)
		goto interconnect_exit;

	wakeup_source = of_property_read_bool(dev->of_node, "wakeup-source");
	device_init_wakeup(&pdev->dev, wakeup_source);
	device_init_wakeup(&qcom->dwc3->dev, wakeup_source);

	qcom->is_suspended = false;
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_forbid(dev);

	return 0;

interconnect_exit:
	dwc3_qcom_interconnect_exit(qcom);
depopulate:
	if (np)
		of_platform_depopulate(&pdev->dev);
	else
		platform_device_put(pdev);
clk_disable:
	for (i = qcom->num_clocks - 1; i >= 0; i--) {
		clk_disable_unprepare(qcom->clks[i]);
		clk_put(qcom->clks[i]);
	}
reset_assert:
	reset_control_assert(qcom->resets);

	return ret;
}

static int dwc3_qcom_remove(struct platform_device *pdev)
{
	struct dwc3_qcom *qcom = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int i;

	device_remove_software_node(&qcom->dwc3->dev);
	if (np)
		of_platform_depopulate(&pdev->dev);
	else
		platform_device_put(pdev);

	for (i = qcom->num_clocks - 1; i >= 0; i--) {
		clk_disable_unprepare(qcom->clks[i]);
		clk_put(qcom->clks[i]);
	}
	qcom->num_clocks = 0;

	dwc3_qcom_interconnect_exit(qcom);
	reset_control_assert(qcom->resets);

	pm_runtime_allow(dev);
	pm_runtime_disable(dev);

	return 0;
}

static int __maybe_unused dwc3_qcom_pm_suspend(struct device *dev)
{
	struct dwc3_qcom *qcom = dev_get_drvdata(dev);
	bool wakeup = device_may_wakeup(dev);
	int ret;

	ret = dwc3_qcom_suspend(qcom, wakeup);
	if (ret)
		return ret;

	qcom->pm_suspended = true;

	return 0;
}

static int __maybe_unused dwc3_qcom_pm_resume(struct device *dev)
{
	struct dwc3_qcom *qcom = dev_get_drvdata(dev);
	bool wakeup = device_may_wakeup(dev);
	int ret;

	ret = dwc3_qcom_resume(qcom, wakeup);
	if (ret)
		return ret;

	qcom->pm_suspended = false;

	return 0;
}

static int __maybe_unused dwc3_qcom_runtime_suspend(struct device *dev)
{
	struct dwc3_qcom *qcom = dev_get_drvdata(dev);

	return dwc3_qcom_suspend(qcom, true);
}

static int __maybe_unused dwc3_qcom_runtime_resume(struct device *dev)
{
	struct dwc3_qcom *qcom = dev_get_drvdata(dev);

	return dwc3_qcom_resume(qcom, true);
}

static const struct dev_pm_ops dwc3_qcom_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_qcom_pm_suspend, dwc3_qcom_pm_resume)
	SET_RUNTIME_PM_OPS(dwc3_qcom_runtime_suspend, dwc3_qcom_runtime_resume,
			   NULL)
};

static const struct of_device_id dwc3_qcom_of_match[] = {
	{ .compatible = "qcom,dwc3" },
	{ }
};
MODULE_DEVICE_TABLE(of, dwc3_qcom_of_match);

#ifdef CONFIG_ACPI
static const struct dwc3_acpi_pdata sdm845_acpi_pdata = {
	.qscratch_base_offset = SDM845_QSCRATCH_BASE_OFFSET,
	.qscratch_base_size = SDM845_QSCRATCH_SIZE,
	.dwc3_core_base_size = SDM845_DWC3_CORE_SIZE,
	.hs_phy_irq_index = 1,
	.dp_hs_phy_irq_index = 4,
	.dm_hs_phy_irq_index = 3,
	.ss_phy_irq_index = 2
};

static const struct dwc3_acpi_pdata sdm845_acpi_urs_pdata = {
	.qscratch_base_offset = SDM845_QSCRATCH_BASE_OFFSET,
	.qscratch_base_size = SDM845_QSCRATCH_SIZE,
	.dwc3_core_base_size = SDM845_DWC3_CORE_SIZE,
	.hs_phy_irq_index = 1,
	.dp_hs_phy_irq_index = 4,
	.dm_hs_phy_irq_index = 3,
	.ss_phy_irq_index = 2,
	.is_urs = true,
};

static const struct acpi_device_id dwc3_qcom_acpi_match[] = {
	{ "QCOM2430", (unsigned long)&sdm845_acpi_pdata },
	{ "QCOM0304", (unsigned long)&sdm845_acpi_urs_pdata },
	{ "QCOM0497", (unsigned long)&sdm845_acpi_urs_pdata },
	{ "QCOM04A6", (unsigned long)&sdm845_acpi_pdata },
	{ },
};
MODULE_DEVICE_TABLE(acpi, dwc3_qcom_acpi_match);
#endif

static struct platform_driver dwc3_qcom_driver = {
	.probe		= dwc3_qcom_probe,
	.remove		= dwc3_qcom_remove,
	.driver		= {
		.name	= "dwc3-qcom",
		.pm	= &dwc3_qcom_dev_pm_ops,
		.of_match_table	= dwc3_qcom_of_match,
		.acpi_match_table = ACPI_PTR(dwc3_qcom_acpi_match),
	},
};

module_platform_driver(dwc3_qcom_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DWC3 QCOM Glue Driver");
