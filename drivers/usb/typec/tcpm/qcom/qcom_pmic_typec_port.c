// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec_mux.h>
#include <linux/workqueue.h>
#include "qcom_pmic_typec_port.h"

struct pmic_typec_port_irq_data {
	int				virq;
	int				irq;
	struct pmic_typec_port		*pmic_typec_port;
};

struct pmic_typec_port {
	struct device			*dev;
	struct tcpm_port		*tcpm_port;
	struct regmap			*regmap;
	u32				base;
	unsigned int			nr_irqs;
	struct pmic_typec_port_irq_data	*irq_data;

	struct regulator		*vdd_vbus;

	int				cc;
	bool				debouncing_cc;
	struct delayed_work		cc_debounce_dwork;

	spinlock_t			lock;	/* Register atomicity */
};

static const char * const typec_cc_status_name[] = {
	[TYPEC_CC_OPEN]		= "Open",
	[TYPEC_CC_RA]		= "Ra",
	[TYPEC_CC_RD]		= "Rd",
	[TYPEC_CC_RP_DEF]	= "Rp-def",
	[TYPEC_CC_RP_1_5]	= "Rp-1.5",
	[TYPEC_CC_RP_3_0]	= "Rp-3.0",
};

static const char *rp_unknown = "unknown";

static const char *cc_to_name(enum typec_cc_status cc)
{
	if (cc > TYPEC_CC_RP_3_0)
		return rp_unknown;

	return typec_cc_status_name[cc];
}

static const char * const rp_sel_name[] = {
	[TYPEC_SRC_RP_SEL_80UA]		= "Rp-def-80uA",
	[TYPEC_SRC_RP_SEL_180UA]	= "Rp-1.5-180uA",
	[TYPEC_SRC_RP_SEL_330UA]	= "Rp-3.0-330uA",
};

static const char *rp_sel_to_name(int rp_sel)
{
	if (rp_sel > TYPEC_SRC_RP_SEL_330UA)
		return rp_unknown;

	return rp_sel_name[rp_sel];
}

#define misc_to_cc(msic) !!(misc & CC_ORIENTATION) ? "cc1" : "cc2"
#define misc_to_vconn(msic) !!(misc & CC_ORIENTATION) ? "cc2" : "cc1"

static void qcom_pmic_typec_port_cc_debounce(struct work_struct *work)
{
	struct pmic_typec_port *pmic_typec_port =
		container_of(work, struct pmic_typec_port, cc_debounce_dwork.work);
	unsigned long flags;

	spin_lock_irqsave(&pmic_typec_port->lock, flags);
	pmic_typec_port->debouncing_cc = false;
	spin_unlock_irqrestore(&pmic_typec_port->lock, flags);

	dev_dbg(pmic_typec_port->dev, "Debounce cc complete\n");
}

static irqreturn_t pmic_typec_port_isr(int irq, void *dev_id)
{
	struct pmic_typec_port_irq_data *irq_data = dev_id;
	struct pmic_typec_port *pmic_typec_port = irq_data->pmic_typec_port;
	u32 misc_stat;
	bool vbus_change = false;
	bool cc_change = false;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_port->lock, flags);

	ret = regmap_read(pmic_typec_port->regmap,
			  pmic_typec_port->base + TYPEC_MISC_STATUS_REG,
			  &misc_stat);
	if (ret)
		goto done;

	switch (irq_data->virq) {
	case PMIC_TYPEC_VBUS_IRQ:
		vbus_change = true;
		break;
	case PMIC_TYPEC_CC_STATE_IRQ:
	case PMIC_TYPEC_ATTACH_DETACH_IRQ:
		if (!pmic_typec_port->debouncing_cc)
			cc_change = true;
		break;
	}

done:
	spin_unlock_irqrestore(&pmic_typec_port->lock, flags);

	if (vbus_change)
		tcpm_vbus_change(pmic_typec_port->tcpm_port);

	if (cc_change)
		tcpm_cc_change(pmic_typec_port->tcpm_port);

	return IRQ_HANDLED;
}

int qcom_pmic_typec_port_get_vbus(struct pmic_typec_port *pmic_typec_port)
{
	struct device *dev = pmic_typec_port->dev;
	unsigned int misc;
	int ret;

	ret = regmap_read(pmic_typec_port->regmap,
			  pmic_typec_port->base + TYPEC_MISC_STATUS_REG,
			  &misc);
	if (ret)
		misc = 0;

	dev_dbg(dev, "get_vbus: 0x%08x detect %d\n", misc, !!(misc & TYPEC_VBUS_DETECT));

	return !!(misc & TYPEC_VBUS_DETECT);
}

int qcom_pmic_typec_port_set_vbus(struct pmic_typec_port *pmic_typec_port, bool on)
{
	u32 sm_stat;
	u32 val;
	int ret;

	if (on) {
		ret = regulator_enable(pmic_typec_port->vdd_vbus);
		if (ret)
			return ret;

		val = TYPEC_SM_VBUS_VSAFE5V;
	} else {
		ret = regulator_disable(pmic_typec_port->vdd_vbus);
		if (ret)
			return ret;

		val = TYPEC_SM_VBUS_VSAFE0V;
	}

	/* Poll waiting for transition to required vSafe5V or vSafe0V */
	ret = regmap_read_poll_timeout(pmic_typec_port->regmap,
				       pmic_typec_port->base + TYPEC_SM_STATUS_REG,
				       sm_stat, sm_stat & val,
				       100, 250000);
	if (ret)
		dev_warn(pmic_typec_port->dev, "vbus vsafe%dv fail\n", on ? 5 : 0);

	return 0;
}

int qcom_pmic_typec_port_get_cc(struct pmic_typec_port *pmic_typec_port,
				enum typec_cc_status *cc1,
				enum typec_cc_status *cc2)
{
	struct device *dev = pmic_typec_port->dev;
	unsigned int misc, val;
	bool attached;
	int ret = 0;

	ret = regmap_read(pmic_typec_port->regmap,
			  pmic_typec_port->base + TYPEC_MISC_STATUS_REG, &misc);
	if (ret)
		goto done;

	attached = !!(misc & CC_ATTACHED);

	if (pmic_typec_port->debouncing_cc) {
		ret = -EBUSY;
		goto done;
	}

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;

	if (!attached)
		goto done;

	if (misc & SNK_SRC_MODE) {
		ret = regmap_read(pmic_typec_port->regmap,
				  pmic_typec_port->base + TYPEC_SRC_STATUS_REG,
				  &val);
		if (ret)
			goto done;
		switch (val & DETECTED_SRC_TYPE_MASK) {
		case AUDIO_ACCESS_RA_RA:
			val = TYPEC_CC_RA;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		case SRC_RD_OPEN:
			val = TYPEC_CC_RD;
			break;
		case SRC_RD_RA_VCONN:
			val = TYPEC_CC_RD;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		default:
			dev_warn(dev, "unexpected src status %.2x\n", val);
			val = TYPEC_CC_RD;
			break;
		}
	} else {
		ret = regmap_read(pmic_typec_port->regmap,
				  pmic_typec_port->base + TYPEC_SNK_STATUS_REG,
				  &val);
		if (ret)
			goto done;
		switch (val & DETECTED_SNK_TYPE_MASK) {
		case SNK_RP_STD:
			val = TYPEC_CC_RP_DEF;
			break;
		case SNK_RP_1P5:
			val = TYPEC_CC_RP_1_5;
			break;
		case SNK_RP_3P0:
			val = TYPEC_CC_RP_3_0;
			break;
		default:
			dev_warn(dev, "unexpected snk status %.2x\n", val);
			val = TYPEC_CC_RP_DEF;
			break;
		}
		val = TYPEC_CC_RP_DEF;
	}

	if (misc & CC_ORIENTATION)
		*cc2 = val;
	else
		*cc1 = val;

done:
	dev_dbg(dev, "get_cc: misc 0x%08x cc1 0x%08x %s cc2 0x%08x %s attached %d cc=%s\n",
		misc, *cc1, cc_to_name(*cc1), *cc2, cc_to_name(*cc2), attached,
		misc_to_cc(misc));

	return ret;
}

static void qcom_pmic_set_cc_debounce(struct pmic_typec_port *pmic_typec_port)
{
	pmic_typec_port->debouncing_cc = true;
	schedule_delayed_work(&pmic_typec_port->cc_debounce_dwork,
			      msecs_to_jiffies(2));
}

int qcom_pmic_typec_port_set_cc(struct pmic_typec_port *pmic_typec_port,
				enum typec_cc_status cc)
{
	struct device *dev = pmic_typec_port->dev;
	unsigned int mode, currsrc;
	unsigned int misc;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_port->lock, flags);

	ret = regmap_read(pmic_typec_port->regmap,
			  pmic_typec_port->base + TYPEC_MISC_STATUS_REG,
			  &misc);
	if (ret)
		goto done;

	mode = EN_SRC_ONLY;

	switch (cc) {
	case TYPEC_CC_OPEN:
		currsrc = TYPEC_SRC_RP_SEL_80UA;
		break;
	case TYPEC_CC_RP_DEF:
		currsrc = TYPEC_SRC_RP_SEL_80UA;
		break;
	case TYPEC_CC_RP_1_5:
		currsrc = TYPEC_SRC_RP_SEL_180UA;
		break;
	case TYPEC_CC_RP_3_0:
		currsrc = TYPEC_SRC_RP_SEL_330UA;
		break;
	case TYPEC_CC_RD:
		currsrc = TYPEC_SRC_RP_SEL_80UA;
		mode = EN_SNK_ONLY;
		break;
	default:
		dev_warn(dev, "unexpected set_cc %d\n", cc);
		ret = -EINVAL;
		goto done;
	}

	if (mode == EN_SRC_ONLY) {
		ret = regmap_write(pmic_typec_port->regmap,
				   pmic_typec_port->base + TYPEC_CURRSRC_CFG_REG,
				   currsrc);
		if (ret)
			goto done;
	}

	pmic_typec_port->cc = cc;
	qcom_pmic_set_cc_debounce(pmic_typec_port);
	ret = 0;

done:
	spin_unlock_irqrestore(&pmic_typec_port->lock, flags);

	dev_dbg(dev, "set_cc: currsrc=%x %s mode %s debounce %d attached %d cc=%s\n",
		currsrc, rp_sel_to_name(currsrc),
		mode == EN_SRC_ONLY ? "EN_SRC_ONLY" : "EN_SNK_ONLY",
		pmic_typec_port->debouncing_cc, !!(misc & CC_ATTACHED),
		misc_to_cc(misc));

	return ret;
}

int qcom_pmic_typec_port_set_vconn(struct pmic_typec_port *pmic_typec_port, bool on)
{
	struct device *dev = pmic_typec_port->dev;
	unsigned int orientation, misc, mask, value;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_port->lock, flags);

	ret = regmap_read(pmic_typec_port->regmap,
			  pmic_typec_port->base + TYPEC_MISC_STATUS_REG, &misc);
	if (ret)
		goto done;

	/* Set VCONN on the inversion of the active CC channel */
	orientation = (misc & CC_ORIENTATION) ? 0 : VCONN_EN_ORIENTATION;
	if (on) {
		mask = VCONN_EN_ORIENTATION | VCONN_EN_VALUE;
		value = orientation | VCONN_EN_VALUE | VCONN_EN_SRC;
	} else {
		mask = VCONN_EN_VALUE;
		value = 0;
	}

	ret = regmap_update_bits(pmic_typec_port->regmap,
				 pmic_typec_port->base + TYPEC_VCONN_CONTROL_REG,
				 mask, value);
done:
	spin_unlock_irqrestore(&pmic_typec_port->lock, flags);

	dev_dbg(dev, "set_vconn: orientation %d control 0x%08x state %s cc %s vconn %s\n",
		orientation, value, on ? "on" : "off", misc_to_vconn(misc), misc_to_cc(misc));

	return ret;
}

int qcom_pmic_typec_port_start_toggling(struct pmic_typec_port *pmic_typec_port,
					enum typec_port_type port_type,
					enum typec_cc_status cc)
{
	struct device *dev = pmic_typec_port->dev;
	unsigned int misc;
	u8 mode = 0;
	unsigned long flags;
	int ret;

	switch (port_type) {
	case TYPEC_PORT_SRC:
		mode = EN_SRC_ONLY;
		break;
	case TYPEC_PORT_SNK:
		mode = EN_SNK_ONLY;
		break;
	case TYPEC_PORT_DRP:
		mode = EN_TRY_SNK;
		break;
	}

	spin_lock_irqsave(&pmic_typec_port->lock, flags);

	ret = regmap_read(pmic_typec_port->regmap,
			  pmic_typec_port->base + TYPEC_MISC_STATUS_REG, &misc);
	if (ret)
		goto done;

	dev_dbg(dev, "start_toggling: misc 0x%08x attached %d port_type %d current cc %d new %d\n",
		misc, !!(misc & CC_ATTACHED), port_type, pmic_typec_port->cc, cc);

	qcom_pmic_set_cc_debounce(pmic_typec_port);

	/* force it to toggle at least once */
	ret = regmap_write(pmic_typec_port->regmap,
			   pmic_typec_port->base + TYPEC_MODE_CFG_REG,
			   TYPEC_DISABLE_CMD);
	if (ret)
		goto done;

	ret = regmap_write(pmic_typec_port->regmap,
			   pmic_typec_port->base + TYPEC_MODE_CFG_REG,
			   mode);
done:
	spin_unlock_irqrestore(&pmic_typec_port->lock, flags);

	return ret;
}

#define TYPEC_INTR_EN_CFG_1_MASK		  \
	(TYPEC_LEGACY_CABLE_INT_EN		| \
	 TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN	| \
	 TYPEC_TRYSOURCE_DETECT_INT_EN		| \
	 TYPEC_TRYSINK_DETECT_INT_EN		| \
	 TYPEC_CCOUT_DETACH_INT_EN		| \
	 TYPEC_CCOUT_ATTACH_INT_EN		| \
	 TYPEC_VBUS_DEASSERT_INT_EN		| \
	 TYPEC_VBUS_ASSERT_INT_EN)

#define TYPEC_INTR_EN_CFG_2_MASK \
	(TYPEC_STATE_MACHINE_CHANGE_INT_EN | TYPEC_VBUS_ERROR_INT_EN | \
	 TYPEC_DEBOUNCE_DONE_INT_EN)

int qcom_pmic_typec_port_start(struct pmic_typec_port *pmic_typec_port,
			       struct tcpm_port *tcpm_port)
{
	int i;
	int mask;
	int ret;

	/* Configure interrupt sources */
	ret = regmap_write(pmic_typec_port->regmap,
			   pmic_typec_port->base + TYPEC_INTERRUPT_EN_CFG_1_REG,
			   TYPEC_INTR_EN_CFG_1_MASK);
	if (ret)
		goto done;

	ret = regmap_write(pmic_typec_port->regmap,
			   pmic_typec_port->base + TYPEC_INTERRUPT_EN_CFG_2_REG,
			   TYPEC_INTR_EN_CFG_2_MASK);
	if (ret)
		goto done;

	/* start in TRY_SNK mode */
	ret = regmap_write(pmic_typec_port->regmap,
			   pmic_typec_port->base + TYPEC_MODE_CFG_REG, EN_TRY_SNK);
	if (ret)
		goto done;

	/* Configure VCONN for software control */
	ret = regmap_update_bits(pmic_typec_port->regmap,
				 pmic_typec_port->base + TYPEC_VCONN_CONTROL_REG,
				 VCONN_EN_SRC | VCONN_EN_VALUE, VCONN_EN_SRC);
	if (ret)
		goto done;

	/* Set CC threshold to 1.6 Volts | tPDdebounce = 10-20ms */
	mask = SEL_SRC_UPPER_REF | USE_TPD_FOR_EXITING_ATTACHSRC;
	ret = regmap_update_bits(pmic_typec_port->regmap,
				 pmic_typec_port->base + TYPEC_EXIT_STATE_CFG_REG,
				 mask, mask);
	if (ret)
		goto done;

	pmic_typec_port->tcpm_port = tcpm_port;

	for (i = 0; i < pmic_typec_port->nr_irqs; i++)
		enable_irq(pmic_typec_port->irq_data[i].irq);

done:
	return ret;
}

void qcom_pmic_typec_port_stop(struct pmic_typec_port *pmic_typec_port)
{
	int i;

	for (i = 0; i < pmic_typec_port->nr_irqs; i++)
		disable_irq(pmic_typec_port->irq_data[i].irq);
}

struct pmic_typec_port *qcom_pmic_typec_port_alloc(struct device *dev)
{
	return devm_kzalloc(dev, sizeof(struct pmic_typec_port), GFP_KERNEL);
}

int qcom_pmic_typec_port_probe(struct platform_device *pdev,
			       struct pmic_typec_port *pmic_typec_port,
			       struct pmic_typec_port_resources *res,
			       struct regmap *regmap,
			       u32 base)
{
	struct device *dev = &pdev->dev;
	struct pmic_typec_port_irq_data *irq_data;
	int i, ret, irq;

	if (!res->nr_irqs || res->nr_irqs > PMIC_TYPEC_MAX_IRQS)
		return -EINVAL;

	irq_data = devm_kzalloc(dev, sizeof(*irq_data) * res->nr_irqs,
				GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	pmic_typec_port->vdd_vbus = devm_regulator_get(dev, "vdd-vbus");
	if (IS_ERR(pmic_typec_port->vdd_vbus))
		return PTR_ERR(pmic_typec_port->vdd_vbus);

	pmic_typec_port->dev = dev;
	pmic_typec_port->base = base;
	pmic_typec_port->regmap = regmap;
	pmic_typec_port->nr_irqs = res->nr_irqs;
	pmic_typec_port->irq_data = irq_data;
	spin_lock_init(&pmic_typec_port->lock);
	INIT_DELAYED_WORK(&pmic_typec_port->cc_debounce_dwork,
			  qcom_pmic_typec_port_cc_debounce);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	for (i = 0; i < res->nr_irqs; i++, irq_data++) {
		irq = platform_get_irq_byname(pdev,
					      res->irq_params[i].irq_name);
		if (irq < 0)
			return irq;

		irq_data->pmic_typec_port = pmic_typec_port;
		irq_data->irq = irq;
		irq_data->virq = res->irq_params[i].virq;
		ret = devm_request_threaded_irq(dev, irq, NULL, pmic_typec_port_isr,
						IRQF_ONESHOT | IRQF_NO_AUTOEN,
						res->irq_params[i].irq_name,
						irq_data);
		if (ret)
			return ret;
	}

	return 0;
}
