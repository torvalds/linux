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
#include <linux/string_choices.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec_mux.h>
#include <linux/workqueue.h>

#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_port.h"

#define TYPEC_SNK_STATUS_REG				0x06
#define DETECTED_SNK_TYPE_MASK				GENMASK(6, 0)
#define SNK_DAM_MASK					GENMASK(6, 4)
#define SNK_DAM_500MA					BIT(6)
#define SNK_DAM_1500MA					BIT(5)
#define SNK_DAM_3000MA					BIT(4)
#define SNK_RP_STD					BIT(3)
#define SNK_RP_1P5					BIT(2)
#define SNK_RP_3P0					BIT(1)
#define SNK_RP_SHORT					BIT(0)

#define TYPEC_SRC_STATUS_REG				0x08
#define DETECTED_SRC_TYPE_MASK				GENMASK(4, 0)
#define SRC_HIGH_BATT					BIT(5)
#define SRC_DEBUG_ACCESS				BIT(4)
#define SRC_RD_OPEN					BIT(3)
#define SRC_RD_RA_VCONN					BIT(2)
#define SRC_RA_OPEN					BIT(1)
#define AUDIO_ACCESS_RA_RA				BIT(0)

#define TYPEC_STATE_MACHINE_STATUS_REG			0x09
#define TYPEC_ATTACH_DETACH_STATE			BIT(5)

#define TYPEC_SM_STATUS_REG				0x0A
#define TYPEC_SM_VBUS_VSAFE5V				BIT(5)
#define TYPEC_SM_VBUS_VSAFE0V				BIT(6)
#define TYPEC_SM_USBIN_LT_LV				BIT(7)

#define TYPEC_MISC_STATUS_REG				0x0B
#define TYPEC_WATER_DETECTION_STATUS			BIT(7)
#define SNK_SRC_MODE					BIT(6)
#define TYPEC_VBUS_DETECT				BIT(5)
#define TYPEC_VBUS_ERROR_STATUS				BIT(4)
#define TYPEC_DEBOUNCE_DONE				BIT(3)
#define CC_ORIENTATION					BIT(1)
#define CC_ATTACHED					BIT(0)

#define LEGACY_CABLE_STATUS_REG				0x0D
#define TYPEC_LEGACY_CABLE_STATUS			BIT(1)
#define TYPEC_NONCOMP_LEGACY_CABLE_STATUS		BIT(0)

#define TYPEC_U_USB_STATUS_REG				0x0F
#define U_USB_GROUND_NOVBUS				BIT(6)
#define U_USB_GROUND					BIT(4)
#define U_USB_FMB1					BIT(3)
#define U_USB_FLOAT1					BIT(2)
#define U_USB_FMB2					BIT(1)
#define U_USB_FLOAT2					BIT(0)

#define TYPEC_MODE_CFG_REG				0x44
#define TYPEC_TRY_MODE_MASK				GENMASK(4, 3)
#define EN_TRY_SNK					BIT(4)
#define EN_TRY_SRC					BIT(3)
#define TYPEC_POWER_ROLE_CMD_MASK			GENMASK(2, 0)
#define EN_SRC_ONLY					BIT(2)
#define EN_SNK_ONLY					BIT(1)
#define TYPEC_DISABLE_CMD				BIT(0)

#define TYPEC_VCONN_CONTROL_REG				0x46
#define VCONN_EN_ORIENTATION				BIT(2)
#define VCONN_EN_VALUE					BIT(1)
#define VCONN_EN_SRC					BIT(0)

#define TYPEC_CCOUT_CONTROL_REG				0x48
#define TYPEC_CCOUT_BUFFER_EN				BIT(2)
#define TYPEC_CCOUT_VALUE				BIT(1)
#define TYPEC_CCOUT_SRC					BIT(0)

#define DEBUG_ACCESS_SRC_CFG_REG			0x4C
#define EN_UNORIENTED_DEBUG_ACCESS_SRC			BIT(0)

#define TYPE_C_CRUDE_SENSOR_CFG_REG			0x4e
#define EN_SRC_CRUDE_SENSOR				BIT(1)
#define EN_SNK_CRUDE_SENSOR				BIT(0)

#define TYPEC_EXIT_STATE_CFG_REG			0x50
#define BYPASS_VSAFE0V_DURING_ROLE_SWAP			BIT(3)
#define SEL_SRC_UPPER_REF				BIT(2)
#define USE_TPD_FOR_EXITING_ATTACHSRC			BIT(1)
#define EXIT_SNK_BASED_ON_CC				BIT(0)

#define TYPEC_CURRSRC_CFG_REG				0x52
#define TYPEC_SRC_RP_SEL_330UA				BIT(1)
#define TYPEC_SRC_RP_SEL_180UA				BIT(0)
#define TYPEC_SRC_RP_SEL_80UA				0
#define TYPEC_SRC_RP_SEL_MASK				GENMASK(1, 0)

#define TYPEC_INTERRUPT_EN_CFG_1_REG			0x5E
#define TYPEC_LEGACY_CABLE_INT_EN			BIT(7)
#define TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN		BIT(6)
#define TYPEC_TRYSOURCE_DETECT_INT_EN			BIT(5)
#define TYPEC_TRYSINK_DETECT_INT_EN			BIT(4)
#define TYPEC_CCOUT_DETACH_INT_EN			BIT(3)
#define TYPEC_CCOUT_ATTACH_INT_EN			BIT(2)
#define TYPEC_VBUS_DEASSERT_INT_EN			BIT(1)
#define TYPEC_VBUS_ASSERT_INT_EN			BIT(0)

#define TYPEC_INTERRUPT_EN_CFG_2_REG			0x60
#define TYPEC_SRC_BATT_HPWR_INT_EN			BIT(6)
#define MICRO_USB_STATE_CHANGE_INT_EN			BIT(5)
#define TYPEC_STATE_MACHINE_CHANGE_INT_EN		BIT(4)
#define TYPEC_DEBUG_ACCESS_DETECT_INT_EN		BIT(3)
#define TYPEC_WATER_DETECTION_INT_EN			BIT(2)
#define TYPEC_VBUS_ERROR_INT_EN				BIT(1)
#define TYPEC_DEBOUNCE_DONE_INT_EN			BIT(0)

#define TYPEC_DEBOUNCE_OPTION_REG			0x62
#define REDUCE_TCCDEBOUNCE_TO_2MS			BIT(2)

#define TYPE_C_SBU_CFG_REG				0x6A
#define SEL_SBU1_ISRC_VAL				0x04
#define SEL_SBU2_ISRC_VAL				0x01

#define TYPEC_U_USB_CFG_REG				0x70
#define EN_MICRO_USB_FACTORY_MODE			BIT(1)
#define EN_MICRO_USB_MODE				BIT(0)

#define TYPEC_PMI632_U_USB_WATER_PROTECTION_CFG_REG	0x72

#define TYPEC_U_USB_WATER_PROTECTION_CFG_REG		0x73
#define EN_MICRO_USB_WATER_PROTECTION			BIT(4)
#define MICRO_USB_DETECTION_ON_TIME_CFG_MASK		GENMASK(3, 2)
#define MICRO_USB_DETECTION_PERIOD_CFG_MASK		GENMASK(1, 0)

#define TYPEC_PMI632_MICRO_USB_MODE_REG			0x73
#define MICRO_USB_MODE_ONLY				BIT(0)

/* Interrupt numbers */
#define PMIC_TYPEC_OR_RID_IRQ				0x0
#define PMIC_TYPEC_VPD_IRQ				0x1
#define PMIC_TYPEC_CC_STATE_IRQ				0x2
#define PMIC_TYPEC_VCONN_OC_IRQ				0x3
#define PMIC_TYPEC_VBUS_IRQ				0x4
#define PMIC_TYPEC_ATTACH_DETACH_IRQ			0x5
#define PMIC_TYPEC_LEGACY_CABLE_IRQ			0x6
#define PMIC_TYPEC_TRY_SNK_SRC_IRQ			0x7

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
	bool				vbus_enabled;
	struct mutex			vbus_lock;		/* VBUS state serialization */

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

static int qcom_pmic_typec_port_vbus_detect(struct pmic_typec_port *pmic_typec_port)
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

static int qcom_pmic_typec_port_vbus_toggle(struct pmic_typec_port *pmic_typec_port, bool on)
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

static int qcom_pmic_typec_port_get_vbus(struct tcpc_dev *tcpc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
	int ret;

	mutex_lock(&pmic_typec_port->vbus_lock);
	ret = pmic_typec_port->vbus_enabled || qcom_pmic_typec_port_vbus_detect(pmic_typec_port);
	mutex_unlock(&pmic_typec_port->vbus_lock);

	return ret;
}

static int qcom_pmic_typec_port_set_vbus(struct tcpc_dev *tcpc, bool on, bool sink)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
	int ret = 0;

	mutex_lock(&pmic_typec_port->vbus_lock);
	if (pmic_typec_port->vbus_enabled == on)
		goto done;

	ret = qcom_pmic_typec_port_vbus_toggle(pmic_typec_port, on);
	if (ret)
		goto done;

	pmic_typec_port->vbus_enabled = on;
	tcpm_vbus_change(tcpm->tcpm_port);

done:
	dev_dbg(tcpm->dev, "set_vbus set: %d result %d\n", on, ret);
	mutex_unlock(&pmic_typec_port->vbus_lock);

	return ret;
}

static int qcom_pmic_typec_port_get_cc(struct tcpc_dev *tcpc,
				       enum typec_cc_status *cc1,
				       enum typec_cc_status *cc2)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
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

static int qcom_pmic_typec_port_set_cc(struct tcpc_dev *tcpc,
				       enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
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

static int qcom_pmic_typec_port_set_polarity(struct tcpc_dev *tcpc,
					     enum typec_cc_polarity pol)
{
	/* Polarity is set separately by phy-qcom-qmp.c */
	return 0;
}

static int qcom_pmic_typec_port_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
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
		orientation, value, str_on_off(on), misc_to_vconn(misc),
		misc_to_cc(misc));

	return ret;
}

static int qcom_pmic_typec_port_start_toggling(struct tcpc_dev *tcpc,
					       enum typec_port_type port_type,
					       enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
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

static int qcom_pmic_typec_port_start(struct pmic_typec *tcpm,
				      struct tcpm_port *tcpm_port)
{
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
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

static void qcom_pmic_typec_port_stop(struct pmic_typec *tcpm)
{
	struct pmic_typec_port *pmic_typec_port = tcpm->pmic_typec_port;
	int i;

	for (i = 0; i < pmic_typec_port->nr_irqs; i++)
		disable_irq(pmic_typec_port->irq_data[i].irq);
}

int qcom_pmic_typec_port_probe(struct platform_device *pdev,
			       struct pmic_typec *tcpm,
			       const struct pmic_typec_port_resources *res,
			       struct regmap *regmap,
			       u32 base)
{
	struct device *dev = &pdev->dev;
	struct pmic_typec_port_irq_data *irq_data;
	struct pmic_typec_port *pmic_typec_port;
	int i, ret, irq;

	pmic_typec_port = devm_kzalloc(dev, sizeof(*pmic_typec_port), GFP_KERNEL);
	if (!pmic_typec_port)
		return -ENOMEM;

	if (!res->nr_irqs || res->nr_irqs > PMIC_TYPEC_MAX_IRQS)
		return -EINVAL;

	irq_data = devm_kcalloc(dev, res->nr_irqs, sizeof(*irq_data),
				GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	mutex_init(&pmic_typec_port->vbus_lock);

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

	tcpm->pmic_typec_port = pmic_typec_port;

	tcpm->tcpc.get_vbus = qcom_pmic_typec_port_get_vbus;
	tcpm->tcpc.set_vbus = qcom_pmic_typec_port_set_vbus;
	tcpm->tcpc.set_cc = qcom_pmic_typec_port_set_cc;
	tcpm->tcpc.get_cc = qcom_pmic_typec_port_get_cc;
	tcpm->tcpc.set_polarity = qcom_pmic_typec_port_set_polarity;
	tcpm->tcpc.set_vconn = qcom_pmic_typec_port_set_vconn;
	tcpm->tcpc.start_toggling = qcom_pmic_typec_port_start_toggling;

	tcpm->port_start = qcom_pmic_typec_port_start;
	tcpm->port_stop = qcom_pmic_typec_port_stop;

	return 0;
}

const struct pmic_typec_port_resources pm8150b_port_res = {
	.irq_params = {
		{
			.irq_name = "vpd-detect",
			.virq = PMIC_TYPEC_VPD_IRQ,
		},

		{
			.irq_name = "cc-state-change",
			.virq = PMIC_TYPEC_CC_STATE_IRQ,
		},
		{
			.irq_name = "vconn-oc",
			.virq = PMIC_TYPEC_VCONN_OC_IRQ,
		},

		{
			.irq_name = "vbus-change",
			.virq = PMIC_TYPEC_VBUS_IRQ,
		},

		{
			.irq_name = "attach-detach",
			.virq = PMIC_TYPEC_ATTACH_DETACH_IRQ,
		},
		{
			.irq_name = "legacy-cable-detect",
			.virq = PMIC_TYPEC_LEGACY_CABLE_IRQ,
		},

		{
			.irq_name = "try-snk-src-detect",
			.virq = PMIC_TYPEC_TRY_SNK_SRC_IRQ,
		},
	},
	.nr_irqs = 7,
};
