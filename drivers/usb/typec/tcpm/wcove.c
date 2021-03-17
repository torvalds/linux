// SPDX-License-Identifier: GPL-2.0
/**
 * typec_wcove.c - WhiskeyCove PMIC USB Type-C PHY driver
 *
 * Copyright (C) 2017 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/usb/tcpm.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/intel_soc_pmic.h>

/* Register offsets */
#define WCOVE_CHGRIRQ0		0x4e09

#define USBC_CONTROL1		0x7001
#define USBC_CONTROL2		0x7002
#define USBC_CONTROL3		0x7003
#define USBC_CC1_CTRL		0x7004
#define USBC_CC2_CTRL		0x7005
#define USBC_STATUS1		0x7007
#define USBC_STATUS2		0x7008
#define USBC_STATUS3		0x7009
#define USBC_CC1		0x700a
#define USBC_CC2		0x700b
#define USBC_CC1_STATUS		0x700c
#define USBC_CC2_STATUS		0x700d
#define USBC_IRQ1		0x7015
#define USBC_IRQ2		0x7016
#define USBC_IRQMASK1		0x7017
#define USBC_IRQMASK2		0x7018
#define USBC_PDCFG2		0x701a
#define USBC_PDCFG3		0x701b
#define USBC_PDSTATUS		0x701c
#define USBC_RXSTATUS		0x701d
#define USBC_RXINFO		0x701e
#define USBC_TXCMD		0x701f
#define USBC_TXINFO		0x7020
#define USBC_RX_DATA		0x7028
#define USBC_TX_DATA		0x7047

/* Register bits */

#define USBC_CONTROL1_MODE_MASK		0x3
#define   USBC_CONTROL1_MODE_SNK	0
#define   USBC_CONTROL1_MODE_SNKACC	1
#define   USBC_CONTROL1_MODE_SRC	2
#define   USBC_CONTROL1_MODE_SRCACC	3
#define   USBC_CONTROL1_MODE_DRP	4
#define   USBC_CONTROL1_MODE_DRPACC	5
#define   USBC_CONTROL1_MODE_TEST	7
#define USBC_CONTROL1_CURSRC_MASK	0xc
#define   USBC_CONTROL1_CURSRC_UA_0	(0 << 3)
#define   USBC_CONTROL1_CURSRC_UA_80	(1 << 3)
#define   USBC_CONTROL1_CURSRC_UA_180	(2 << 3)
#define   USBC_CONTROL1_CURSRC_UA_330	(3 << 3)
#define USBC_CONTROL1_DRPTOGGLE_RANDOM	0xe0

#define USBC_CONTROL2_UNATT_SNK		BIT(0)
#define USBC_CONTROL2_UNATT_SRC		BIT(1)
#define USBC_CONTROL2_DIS_ST		BIT(2)

#define USBC_CONTROL3_DET_DIS		BIT(0)
#define USBC_CONTROL3_PD_DIS		BIT(1)
#define USBC_CONTROL3_RESETPHY		BIT(2)

#define USBC_CC_CTRL_PU_EN		BIT(0)
#define USBC_CC_CTRL_VCONN_EN		BIT(1)
#define USBC_CC_CTRL_TX_EN		BIT(2)
#define USBC_CC_CTRL_PD_EN		BIT(3)
#define USBC_CC_CTRL_CDET_EN		BIT(4)
#define USBC_CC_CTRL_RDET_EN		BIT(5)
#define USBC_CC_CTRL_ADC_EN		BIT(6)
#define USBC_CC_CTRL_VBUSOK		BIT(7)

#define USBC_STATUS1_DET_ONGOING	BIT(6)
#define USBC_STATUS1_RSLT(r)		((r) & 0xf)
#define USBC_RSLT_NOTHING		0
#define USBC_RSLT_SRC_DEFAULT		1
#define USBC_RSLT_SRC_1_5A		2
#define USBC_RSLT_SRC_3_0A		3
#define USBC_RSLT_SNK			4
#define USBC_RSLT_DEBUG_ACC		5
#define USBC_RSLT_AUDIO_ACC		6
#define USBC_RSLT_UNDEF			15
#define USBC_STATUS1_ORIENT(r)		(((r) >> 4) & 0x3)
#define USBC_ORIENT_NORMAL		1
#define USBC_ORIENT_REVERSE		2

#define USBC_STATUS2_VBUS_REQ		BIT(5)

#define UCSC_CC_STATUS_SNK_RP		BIT(0)
#define UCSC_CC_STATUS_PWRDEFSNK	BIT(1)
#define UCSC_CC_STATUS_PWR_1P5A_SNK	BIT(2)
#define UCSC_CC_STATUS_PWR_3A_SNK	BIT(3)
#define UCSC_CC_STATUS_SRC_RP		BIT(4)
#define UCSC_CC_STATUS_RX(r)		(((r) >> 5) & 0x3)
#define   USBC_CC_STATUS_RD		1
#define   USBC_CC_STATUS_RA		2

#define USBC_IRQ1_ADCDONE1		BIT(2)
#define USBC_IRQ1_OVERTEMP		BIT(1)
#define USBC_IRQ1_SHORT			BIT(0)

#define USBC_IRQ2_CC_CHANGE		BIT(7)
#define USBC_IRQ2_RX_PD			BIT(6)
#define USBC_IRQ2_RX_HR			BIT(5)
#define USBC_IRQ2_RX_CR			BIT(4)
#define USBC_IRQ2_TX_SUCCESS		BIT(3)
#define USBC_IRQ2_TX_FAIL		BIT(2)

#define USBC_IRQMASK1_ALL	(USBC_IRQ1_ADCDONE1 | USBC_IRQ1_OVERTEMP | \
				 USBC_IRQ1_SHORT)

#define USBC_IRQMASK2_ALL	(USBC_IRQ2_CC_CHANGE | USBC_IRQ2_RX_PD | \
				 USBC_IRQ2_RX_HR | USBC_IRQ2_RX_CR | \
				 USBC_IRQ2_TX_SUCCESS | USBC_IRQ2_TX_FAIL)

#define USBC_PDCFG2_SOP			BIT(0)
#define USBC_PDCFG2_SOP_P		BIT(1)
#define USBC_PDCFG2_SOP_PP		BIT(2)
#define USBC_PDCFG2_SOP_P_DEBUG		BIT(3)
#define USBC_PDCFG2_SOP_PP_DEBUG	BIT(4)

#define USBC_PDCFG3_DATAROLE_SHIFT	1
#define USBC_PDCFG3_SOP_SHIFT		2

#define USBC_RXSTATUS_RXCLEAR		BIT(0)
#define USBC_RXSTATUS_RXDATA		BIT(7)

#define USBC_RXINFO_RXBYTES(i)		(((i) >> 3) & 0x1f)

#define USBC_TXCMD_BUF_RDY		BIT(0)
#define USBC_TXCMD_START		BIT(1)
#define USBC_TXCMD_NOP			(0 << 5)
#define USBC_TXCMD_MSG			(1 << 5)
#define USBC_TXCMD_CR			(2 << 5)
#define USBC_TXCMD_HR			(3 << 5)
#define USBC_TXCMD_BIST			(4 << 5)

#define USBC_TXINFO_RETRIES(d)		(d << 3)

struct wcove_typec {
	struct mutex lock; /* device lock */
	struct device *dev;
	struct regmap *regmap;
	guid_t guid;

	bool vbus;

	struct tcpc_dev tcpc;
	struct tcpm_port *tcpm;
};

#define tcpc_to_wcove(_tcpc_) container_of(_tcpc_, struct wcove_typec, tcpc)

enum wcove_typec_func {
	WCOVE_FUNC_DRIVE_VBUS = 1,
	WCOVE_FUNC_ORIENTATION,
	WCOVE_FUNC_ROLE,
	WCOVE_FUNC_DRIVE_VCONN,
};

enum wcove_typec_orientation {
	WCOVE_ORIENTATION_NORMAL,
	WCOVE_ORIENTATION_REVERSE,
};

enum wcove_typec_role {
	WCOVE_ROLE_HOST,
	WCOVE_ROLE_DEVICE,
};

#define WCOVE_DSM_UUID		"482383f0-2876-4e49-8685-db66211af037"

static int wcove_typec_func(struct wcove_typec *wcove,
			    enum wcove_typec_func func, int param)
{
	union acpi_object *obj;
	union acpi_object tmp;
	union acpi_object argv4 = ACPI_INIT_DSM_ARGV4(1, &tmp);

	tmp.type = ACPI_TYPE_INTEGER;
	tmp.integer.value = param;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(wcove->dev), &wcove->guid, 1, func,
				&argv4);
	if (!obj) {
		dev_err(wcove->dev, "%s: failed to evaluate _DSM\n", __func__);
		return -EIO;
	}

	ACPI_FREE(obj);
	return 0;
}

static int wcove_init(struct tcpc_dev *tcpc)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);
	int ret;

	ret = regmap_write(wcove->regmap, USBC_CONTROL1, 0);
	if (ret)
		return ret;

	/* Unmask everything */
	ret = regmap_write(wcove->regmap, USBC_IRQMASK1, 0);
	if (ret)
		return ret;

	return regmap_write(wcove->regmap, USBC_IRQMASK2, 0);
}

static int wcove_get_vbus(struct tcpc_dev *tcpc)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);
	unsigned int cc1ctrl;
	int ret;

	ret = regmap_read(wcove->regmap, USBC_CC1_CTRL, &cc1ctrl);
	if (ret)
		return ret;

	wcove->vbus = !!(cc1ctrl & USBC_CC_CTRL_VBUSOK);

	return wcove->vbus;
}

static int wcove_set_vbus(struct tcpc_dev *tcpc, bool on, bool sink)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);

	return wcove_typec_func(wcove, WCOVE_FUNC_DRIVE_VBUS, on);
}

static int wcove_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);

	return wcove_typec_func(wcove, WCOVE_FUNC_DRIVE_VCONN, on);
}

static enum typec_cc_status wcove_to_typec_cc(unsigned int cc)
{
	if (cc & UCSC_CC_STATUS_SNK_RP) {
		if (cc & UCSC_CC_STATUS_PWRDEFSNK)
			return TYPEC_CC_RP_DEF;
		else if (cc & UCSC_CC_STATUS_PWR_1P5A_SNK)
			return TYPEC_CC_RP_1_5;
		else if (cc & UCSC_CC_STATUS_PWR_3A_SNK)
			return TYPEC_CC_RP_3_0;
	} else {
		switch (UCSC_CC_STATUS_RX(cc)) {
		case USBC_CC_STATUS_RD:
			return TYPEC_CC_RD;
		case USBC_CC_STATUS_RA:
			return TYPEC_CC_RA;
		default:
			break;
		}
	}
	return TYPEC_CC_OPEN;
}

static int wcove_get_cc(struct tcpc_dev *tcpc, enum typec_cc_status *cc1,
			enum typec_cc_status *cc2)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);
	unsigned int cc1_status;
	unsigned int cc2_status;
	int ret;

	ret = regmap_read(wcove->regmap, USBC_CC1_STATUS, &cc1_status);
	if (ret)
		return ret;

	ret = regmap_read(wcove->regmap, USBC_CC2_STATUS, &cc2_status);
	if (ret)
		return ret;

	*cc1 = wcove_to_typec_cc(cc1_status);
	*cc2 = wcove_to_typec_cc(cc2_status);

	return 0;
}

static int wcove_set_cc(struct tcpc_dev *tcpc, enum typec_cc_status cc)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);
	unsigned int ctrl;

	switch (cc) {
	case TYPEC_CC_RD:
		ctrl = USBC_CONTROL1_MODE_SNK;
		break;
	case TYPEC_CC_RP_DEF:
		ctrl = USBC_CONTROL1_CURSRC_UA_80 | USBC_CONTROL1_MODE_SRC;
		break;
	case TYPEC_CC_RP_1_5:
		ctrl = USBC_CONTROL1_CURSRC_UA_180 | USBC_CONTROL1_MODE_SRC;
		break;
	case TYPEC_CC_RP_3_0:
		ctrl = USBC_CONTROL1_CURSRC_UA_330 | USBC_CONTROL1_MODE_SRC;
		break;
	case TYPEC_CC_OPEN:
		ctrl = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_write(wcove->regmap, USBC_CONTROL1, ctrl);
}

static int wcove_set_polarity(struct tcpc_dev *tcpc, enum typec_cc_polarity pol)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);

	return wcove_typec_func(wcove, WCOVE_FUNC_ORIENTATION, pol);
}

static int wcove_set_current_limit(struct tcpc_dev *tcpc, u32 max_ma, u32 mv)
{
	return 0;
}

static int wcove_set_roles(struct tcpc_dev *tcpc, bool attached,
			   enum typec_role role, enum typec_data_role data)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);
	unsigned int val;
	int ret;

	ret = wcove_typec_func(wcove, WCOVE_FUNC_ROLE, data == TYPEC_HOST ?
			       WCOVE_ROLE_HOST : WCOVE_ROLE_DEVICE);
	if (ret)
		return ret;

	val = role;
	val |= data << USBC_PDCFG3_DATAROLE_SHIFT;
	val |= PD_REV20 << USBC_PDCFG3_SOP_SHIFT;

	return regmap_write(wcove->regmap, USBC_PDCFG3, val);
}

static int wcove_set_pd_rx(struct tcpc_dev *tcpc, bool on)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);

	return regmap_write(wcove->regmap, USBC_PDCFG2,
			    on ? USBC_PDCFG2_SOP : 0);
}

static int wcove_pd_transmit(struct tcpc_dev *tcpc,
			     enum tcpm_transmit_type type,
			     const struct pd_message *msg,
			     unsigned int negotiated_rev)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);
	unsigned int info = 0;
	unsigned int cmd;
	int ret;

	ret = regmap_read(wcove->regmap, USBC_TXCMD, &cmd);
	if (ret)
		return ret;

	if (!(cmd & USBC_TXCMD_BUF_RDY)) {
		dev_warn(wcove->dev, "%s: Last transmission still ongoing!",
			 __func__);
		return -EBUSY;
	}

	if (msg) {
		const u8 *data = (void *)msg;
		int i;

		for (i = 0; i < pd_header_cnt(msg->header) * 4 + 2; i++) {
			ret = regmap_write(wcove->regmap, USBC_TX_DATA + i,
					   data[i]);
			if (ret)
				return ret;
		}
	}

	switch (type) {
	case TCPC_TX_SOP:
	case TCPC_TX_SOP_PRIME:
	case TCPC_TX_SOP_PRIME_PRIME:
	case TCPC_TX_SOP_DEBUG_PRIME:
	case TCPC_TX_SOP_DEBUG_PRIME_PRIME:
		info = type + 1;
		cmd = USBC_TXCMD_MSG;
		break;
	case TCPC_TX_HARD_RESET:
		cmd = USBC_TXCMD_HR;
		break;
	case TCPC_TX_CABLE_RESET:
		cmd = USBC_TXCMD_CR;
		break;
	case TCPC_TX_BIST_MODE_2:
		cmd = USBC_TXCMD_BIST;
		break;
	default:
		return -EINVAL;
	}

	/* NOTE Setting maximum number of retries (7) */
	ret = regmap_write(wcove->regmap, USBC_TXINFO,
			   info | USBC_TXINFO_RETRIES(7));
	if (ret)
		return ret;

	return regmap_write(wcove->regmap, USBC_TXCMD, cmd | USBC_TXCMD_START);
}

static int wcove_start_toggling(struct tcpc_dev *tcpc,
				enum typec_port_type port_type,
				enum typec_cc_status cc)
{
	struct wcove_typec *wcove = tcpc_to_wcove(tcpc);
	unsigned int usbc_ctrl;

	if (port_type != TYPEC_PORT_DRP)
		return -EOPNOTSUPP;

	usbc_ctrl = USBC_CONTROL1_MODE_DRP | USBC_CONTROL1_DRPTOGGLE_RANDOM;

	switch (cc) {
	case TYPEC_CC_RP_1_5:
		usbc_ctrl |= USBC_CONTROL1_CURSRC_UA_180;
		break;
	case TYPEC_CC_RP_3_0:
		usbc_ctrl |= USBC_CONTROL1_CURSRC_UA_330;
		break;
	default:
		usbc_ctrl |= USBC_CONTROL1_CURSRC_UA_80;
		break;
	}

	return regmap_write(wcove->regmap, USBC_CONTROL1, usbc_ctrl);
}

static int wcove_read_rx_buffer(struct wcove_typec *wcove, void *msg)
{
	unsigned int info;
	int ret;
	int i;

	ret = regmap_read(wcove->regmap, USBC_RXINFO, &info);
	if (ret)
		return ret;

	/* FIXME: Check that USBC_RXINFO_RXBYTES(info) matches the header */

	for (i = 0; i < USBC_RXINFO_RXBYTES(info); i++) {
		ret = regmap_read(wcove->regmap, USBC_RX_DATA + i, msg + i);
		if (ret)
			return ret;
	}

	return regmap_write(wcove->regmap, USBC_RXSTATUS,
			    USBC_RXSTATUS_RXCLEAR);
}

static irqreturn_t wcove_typec_irq(int irq, void *data)
{
	struct wcove_typec *wcove = data;
	unsigned int usbc_irq1 = 0;
	unsigned int usbc_irq2 = 0;
	unsigned int cc1ctrl;
	int ret;

	mutex_lock(&wcove->lock);

	/* Read.. */
	ret = regmap_read(wcove->regmap, USBC_IRQ1, &usbc_irq1);
	if (ret)
		goto err;

	ret = regmap_read(wcove->regmap, USBC_IRQ2, &usbc_irq2);
	if (ret)
		goto err;

	ret = regmap_read(wcove->regmap, USBC_CC1_CTRL, &cc1ctrl);
	if (ret)
		goto err;

	if (!wcove->tcpm)
		goto err;

	/* ..check.. */
	if (usbc_irq1 & USBC_IRQ1_OVERTEMP) {
		dev_err(wcove->dev, "VCONN Switch Over Temperature!\n");
		wcove_typec_func(wcove, WCOVE_FUNC_DRIVE_VCONN, false);
		/* REVISIT: Report an error? */
	}

	if (usbc_irq1 & USBC_IRQ1_SHORT) {
		dev_err(wcove->dev, "VCONN Switch Short Circuit!\n");
		wcove_typec_func(wcove, WCOVE_FUNC_DRIVE_VCONN, false);
		/* REVISIT: Report an error? */
	}

	if (wcove->vbus != !!(cc1ctrl & USBC_CC_CTRL_VBUSOK))
		tcpm_vbus_change(wcove->tcpm);

	/* REVISIT: See if tcpm code can be made to consider Type-C HW FSMs */
	if (usbc_irq2 & USBC_IRQ2_CC_CHANGE)
		tcpm_cc_change(wcove->tcpm);

	if (usbc_irq2 & USBC_IRQ2_RX_PD) {
		unsigned int status;

		/*
		 * FIXME: Need to check if TX is ongoing and report
		 * TX_DIREGARDED if needed?
		 */

		ret = regmap_read(wcove->regmap, USBC_RXSTATUS, &status);
		if (ret)
			goto err;

		/* Flush all buffers */
		while (status & USBC_RXSTATUS_RXDATA) {
			struct pd_message msg;

			ret = wcove_read_rx_buffer(wcove, &msg);
			if (ret) {
				dev_err(wcove->dev, "%s: RX read failed\n",
					__func__);
				goto err;
			}

			tcpm_pd_receive(wcove->tcpm, &msg);

			ret = regmap_read(wcove->regmap, USBC_RXSTATUS,
					  &status);
			if (ret)
				goto err;
		}
	}

	if (usbc_irq2 & USBC_IRQ2_RX_HR)
		tcpm_pd_hard_reset(wcove->tcpm);

	/* REVISIT: if (usbc_irq2 & USBC_IRQ2_RX_CR) */

	if (usbc_irq2 & USBC_IRQ2_TX_SUCCESS)
		tcpm_pd_transmit_complete(wcove->tcpm, TCPC_TX_SUCCESS);

	if (usbc_irq2 & USBC_IRQ2_TX_FAIL)
		tcpm_pd_transmit_complete(wcove->tcpm, TCPC_TX_FAILED);

err:
	/* ..and clear. */
	if (usbc_irq1) {
		ret = regmap_write(wcove->regmap, USBC_IRQ1, usbc_irq1);
		if (ret)
			dev_WARN(wcove->dev, "%s failed to clear IRQ1\n",
				 __func__);
	}

	if (usbc_irq2) {
		ret = regmap_write(wcove->regmap, USBC_IRQ2, usbc_irq2);
		if (ret)
			dev_WARN(wcove->dev, "%s failed to clear IRQ2\n",
				 __func__);
	}

	/* REVISIT: Clear WhiskeyCove CHGR Type-C interrupt */
	regmap_write(wcove->regmap, WCOVE_CHGRIRQ0, BIT(5));

	mutex_unlock(&wcove->lock);
	return IRQ_HANDLED;
}

/*
 * The following power levels should be safe to use with Joule board.
 */
static const u32 src_pdo[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |
		  PDO_FIXED_USB_COMM),
};

static const u32 snk_pdo[] = {
	PDO_FIXED(5000, 500, PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |
		  PDO_FIXED_USB_COMM),
	PDO_VAR(5000, 12000, 3000),
};

static const struct property_entry wcove_props[] = {
	PROPERTY_ENTRY_STRING("data-role", "dual"),
	PROPERTY_ENTRY_STRING("power-role", "dual"),
	PROPERTY_ENTRY_STRING("try-power-role", "sink"),
	PROPERTY_ENTRY_U32_ARRAY("source-pdos", src_pdo),
	PROPERTY_ENTRY_U32_ARRAY("sink-pdos", snk_pdo),
	PROPERTY_ENTRY_U32("op-sink-microwatt", 15000000),
	{ }
};

static int wcove_typec_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct wcove_typec *wcove;
	int irq;
	int ret;

	wcove = devm_kzalloc(&pdev->dev, sizeof(*wcove), GFP_KERNEL);
	if (!wcove)
		return -ENOMEM;

	mutex_init(&wcove->lock);
	wcove->dev = &pdev->dev;
	wcove->regmap = pmic->regmap;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	irq = regmap_irq_get_virq(pmic->irq_chip_data_chgr, irq);
	if (irq < 0)
		return irq;

	ret = guid_parse(WCOVE_DSM_UUID, &wcove->guid);
	if (ret)
		return ret;

	if (!acpi_check_dsm(ACPI_HANDLE(&pdev->dev), &wcove->guid, 0, 0x1f)) {
		dev_err(&pdev->dev, "Missing _DSM functions\n");
		return -ENODEV;
	}

	wcove->tcpc.init = wcove_init;
	wcove->tcpc.get_vbus = wcove_get_vbus;
	wcove->tcpc.set_vbus = wcove_set_vbus;
	wcove->tcpc.set_cc = wcove_set_cc;
	wcove->tcpc.get_cc = wcove_get_cc;
	wcove->tcpc.set_polarity = wcove_set_polarity;
	wcove->tcpc.set_vconn = wcove_set_vconn;
	wcove->tcpc.set_current_limit = wcove_set_current_limit;
	wcove->tcpc.start_toggling = wcove_start_toggling;

	wcove->tcpc.set_pd_rx = wcove_set_pd_rx;
	wcove->tcpc.set_roles = wcove_set_roles;
	wcove->tcpc.pd_transmit = wcove_pd_transmit;

	wcove->tcpc.fwnode = fwnode_create_software_node(wcove_props, NULL);
	if (IS_ERR(wcove->tcpc.fwnode))
		return PTR_ERR(wcove->tcpc.fwnode);

	wcove->tcpm = tcpm_register_port(wcove->dev, &wcove->tcpc);
	if (IS_ERR(wcove->tcpm)) {
		fwnode_remove_software_node(wcove->tcpc.fwnode);
		return PTR_ERR(wcove->tcpm);
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					wcove_typec_irq, IRQF_ONESHOT,
					"wcove_typec", wcove);
	if (ret) {
		tcpm_unregister_port(wcove->tcpm);
		fwnode_remove_software_node(wcove->tcpc.fwnode);
		return ret;
	}

	platform_set_drvdata(pdev, wcove);
	return 0;
}

static int wcove_typec_remove(struct platform_device *pdev)
{
	struct wcove_typec *wcove = platform_get_drvdata(pdev);
	unsigned int val;

	/* Mask everything */
	regmap_read(wcove->regmap, USBC_IRQMASK1, &val);
	regmap_write(wcove->regmap, USBC_IRQMASK1, val | USBC_IRQMASK1_ALL);
	regmap_read(wcove->regmap, USBC_IRQMASK2, &val);
	regmap_write(wcove->regmap, USBC_IRQMASK2, val | USBC_IRQMASK2_ALL);

	tcpm_unregister_port(wcove->tcpm);
	fwnode_remove_software_node(wcove->tcpc.fwnode);

	return 0;
}

static struct platform_driver wcove_typec_driver = {
	.driver = {
		.name		= "bxt_wcove_usbc",
	},
	.probe			= wcove_typec_probe,
	.remove			= wcove_typec_remove,
};

module_platform_driver(wcove_typec_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("WhiskeyCove PMIC USB Type-C PHY driver");
MODULE_ALIAS("platform:bxt_wcove_usbc");
