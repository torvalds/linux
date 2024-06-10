// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/pd.h>
#include <linux/usb/tcpm.h>
#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_pdphy.h"

/* PD PHY register offsets and bit fields */
#define USB_PDPHY_MSG_CONFIG_REG	0x40
#define MSG_CONFIG_PORT_DATA_ROLE	BIT(3)
#define MSG_CONFIG_PORT_POWER_ROLE	BIT(2)
#define MSG_CONFIG_SPEC_REV_MASK	(BIT(1) | BIT(0))

#define USB_PDPHY_EN_CONTROL_REG	0x46
#define CONTROL_ENABLE			BIT(0)

#define USB_PDPHY_RX_STATUS_REG		0x4A
#define RX_FRAME_TYPE			(BIT(0) | BIT(1) | BIT(2))

#define USB_PDPHY_FRAME_FILTER_REG	0x4C
#define FRAME_FILTER_EN_HARD_RESET	BIT(5)
#define FRAME_FILTER_EN_SOP		BIT(0)

#define USB_PDPHY_TX_SIZE_REG		0x42
#define TX_SIZE_MASK			0xF

#define USB_PDPHY_TX_CONTROL_REG	0x44
#define TX_CONTROL_RETRY_COUNT(n)	(((n) & 0x3) << 5)
#define TX_CONTROL_FRAME_TYPE(n)        (((n) & 0x7) << 2)
#define TX_CONTROL_FRAME_TYPE_CABLE_RESET	(0x1 << 2)
#define TX_CONTROL_SEND_SIGNAL		BIT(1)
#define TX_CONTROL_SEND_MSG		BIT(0)

#define USB_PDPHY_RX_SIZE_REG		0x48

#define USB_PDPHY_RX_ACKNOWLEDGE_REG	0x4B
#define RX_BUFFER_TOKEN			BIT(0)

#define USB_PDPHY_BIST_MODE_REG		0x4E
#define BIST_MODE_MASK			0xF
#define BIST_ENABLE			BIT(7)
#define PD_MSG_BIST			0x3
#define PD_BIST_TEST_DATA_MODE		0x8

#define USB_PDPHY_TX_BUFFER_HDR_REG	0x60
#define USB_PDPHY_TX_BUFFER_DATA_REG	0x62

#define USB_PDPHY_RX_BUFFER_REG		0x80

/* VDD regulator */
#define VDD_PDPHY_VOL_MIN		2800000	/* uV */
#define VDD_PDPHY_VOL_MAX		3300000	/* uV */
#define VDD_PDPHY_HPM_LOAD		3000	/* uA */

/* Message Spec Rev field */
#define PD_MSG_HDR_REV(hdr)		(((hdr) >> 6) & 3)

/* timers */
#define RECEIVER_RESPONSE_TIME		15	/* tReceiverResponse */
#define HARD_RESET_COMPLETE_TIME	5	/* tHardResetComplete */

/* Interrupt numbers */
#define PMIC_PDPHY_SIG_TX_IRQ		0x0
#define PMIC_PDPHY_SIG_RX_IRQ		0x1
#define PMIC_PDPHY_MSG_TX_IRQ		0x2
#define PMIC_PDPHY_MSG_RX_IRQ		0x3
#define PMIC_PDPHY_MSG_TX_FAIL_IRQ	0x4
#define PMIC_PDPHY_MSG_TX_DISCARD_IRQ	0x5
#define PMIC_PDPHY_MSG_RX_DISCARD_IRQ	0x6
#define PMIC_PDPHY_FR_SWAP_IRQ		0x7


struct pmic_typec_pdphy_irq_data {
	int				virq;
	int				irq;
	struct pmic_typec_pdphy		*pmic_typec_pdphy;
};

struct pmic_typec_pdphy {
	struct device			*dev;
	struct tcpm_port		*tcpm_port;
	struct regmap			*regmap;
	u32				base;

	unsigned int			nr_irqs;
	struct pmic_typec_pdphy_irq_data	*irq_data;

	struct work_struct		reset_work;
	struct work_struct		receive_work;
	struct regulator		*vdd_pdphy;
	spinlock_t			lock;		/* Register atomicity */
};

static void qcom_pmic_typec_pdphy_reset_on(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	struct device *dev = pmic_typec_pdphy->dev;
	int ret;

	/* Terminate TX */
	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_TX_CONTROL_REG, 0);
	if (ret)
		goto err;

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_FRAME_FILTER_REG, 0);
	if (ret)
		goto err;

	return;
err:
	dev_err(dev, "pd_reset_on error\n");
}

static void qcom_pmic_typec_pdphy_reset_off(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	struct device *dev = pmic_typec_pdphy->dev;
	int ret;

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_FRAME_FILTER_REG,
			   FRAME_FILTER_EN_SOP | FRAME_FILTER_EN_HARD_RESET);
	if (ret)
		dev_err(dev, "pd_reset_off error\n");
}

static void qcom_pmic_typec_pdphy_sig_reset_work(struct work_struct *work)
{
	struct pmic_typec_pdphy *pmic_typec_pdphy = container_of(work, struct pmic_typec_pdphy,
						     reset_work);
	unsigned long flags;

	spin_lock_irqsave(&pmic_typec_pdphy->lock, flags);

	qcom_pmic_typec_pdphy_reset_on(pmic_typec_pdphy);
	qcom_pmic_typec_pdphy_reset_off(pmic_typec_pdphy);

	spin_unlock_irqrestore(&pmic_typec_pdphy->lock, flags);

	tcpm_pd_hard_reset(pmic_typec_pdphy->tcpm_port);
}

static int
qcom_pmic_typec_pdphy_clear_tx_control_reg(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	struct device *dev = pmic_typec_pdphy->dev;
	unsigned int val;
	int ret;

	/* Clear TX control register */
	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_TX_CONTROL_REG, 0);
	if (ret)
		goto done;

	/* Perform readback to ensure sufficient delay for command to latch */
	ret = regmap_read(pmic_typec_pdphy->regmap,
			  pmic_typec_pdphy->base + USB_PDPHY_TX_CONTROL_REG, &val);

done:
	if (ret)
		dev_err(dev, "pd_clear_tx_control_reg: clear tx flag\n");

	return ret;
}

static int
qcom_pmic_typec_pdphy_pd_transmit_signal(struct pmic_typec_pdphy *pmic_typec_pdphy,
					 enum tcpm_transmit_type type,
					 unsigned int negotiated_rev)
{
	struct device *dev = pmic_typec_pdphy->dev;
	unsigned int val;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_pdphy->lock, flags);

	/* Clear TX control register */
	ret = qcom_pmic_typec_pdphy_clear_tx_control_reg(pmic_typec_pdphy);
	if (ret)
		goto done;

	val = TX_CONTROL_SEND_SIGNAL;
	if (negotiated_rev == PD_REV30)
		val |= TX_CONTROL_RETRY_COUNT(2);
	else
		val |= TX_CONTROL_RETRY_COUNT(3);

	if (type == TCPC_TX_CABLE_RESET || type == TCPC_TX_HARD_RESET)
		val |= TX_CONTROL_FRAME_TYPE(1);

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_TX_CONTROL_REG, val);

done:
	spin_unlock_irqrestore(&pmic_typec_pdphy->lock, flags);

	dev_vdbg(dev, "pd_transmit_signal: type %d negotiate_rev %d send %d\n",
		 type, negotiated_rev, ret);

	return ret;
}

static int
qcom_pmic_typec_pdphy_pd_transmit_payload(struct pmic_typec_pdphy *pmic_typec_pdphy,
					  enum tcpm_transmit_type type,
					  const struct pd_message *msg,
					  unsigned int negotiated_rev)
{
	struct device *dev = pmic_typec_pdphy->dev;
	unsigned int val, hdr_len, txbuf_len, txsize_len;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_pdphy->lock, flags);

	ret = regmap_read(pmic_typec_pdphy->regmap,
			  pmic_typec_pdphy->base + USB_PDPHY_RX_ACKNOWLEDGE_REG,
			  &val);
	if (ret)
		goto done;

	if (val) {
		dev_err(dev, "pd_transmit_payload: RX message pending\n");
		ret = -EBUSY;
		goto done;
	}

	/* Clear TX control register */
	ret = qcom_pmic_typec_pdphy_clear_tx_control_reg(pmic_typec_pdphy);
	if (ret)
		goto done;

	hdr_len = sizeof(msg->header);
	txbuf_len = pd_header_cnt_le(msg->header) * 4;
	txsize_len = hdr_len + txbuf_len - 1;

	/* Write message header sizeof(u16) to USB_PDPHY_TX_BUFFER_HDR_REG */
	ret = regmap_bulk_write(pmic_typec_pdphy->regmap,
				pmic_typec_pdphy->base + USB_PDPHY_TX_BUFFER_HDR_REG,
				&msg->header, hdr_len);
	if (ret)
		goto done;

	/* Write payload to USB_PDPHY_TX_BUFFER_DATA_REG for txbuf_len */
	if (txbuf_len) {
		ret = regmap_bulk_write(pmic_typec_pdphy->regmap,
					pmic_typec_pdphy->base + USB_PDPHY_TX_BUFFER_DATA_REG,
					&msg->payload, txbuf_len);
		if (ret)
			goto done;
	}

	/* Write total length ((header + data) - 1) to USB_PDPHY_TX_SIZE_REG */
	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_TX_SIZE_REG,
			   txsize_len);
	if (ret)
		goto done;

	/* Clear TX control register */
	ret = qcom_pmic_typec_pdphy_clear_tx_control_reg(pmic_typec_pdphy);
	if (ret)
		goto done;

	/* Initiate transmit with retry count as indicated by PD revision */
	val = TX_CONTROL_FRAME_TYPE(type) | TX_CONTROL_SEND_MSG;
	if (pd_header_rev(msg->header) == PD_REV30)
		val |= TX_CONTROL_RETRY_COUNT(2);
	else
		val |= TX_CONTROL_RETRY_COUNT(3);

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_TX_CONTROL_REG, val);

done:
	spin_unlock_irqrestore(&pmic_typec_pdphy->lock, flags);

	if (ret) {
		dev_err(dev, "pd_transmit_payload: hdr %*ph data %*ph ret %d\n",
			hdr_len, &msg->header, txbuf_len, &msg->payload, ret);
	}

	return ret;
}

static int qcom_pmic_typec_pdphy_pd_transmit(struct tcpc_dev *tcpc,
					     enum tcpm_transmit_type type,
					     const struct pd_message *msg,
					     unsigned int negotiated_rev)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_pdphy *pmic_typec_pdphy = tcpm->pmic_typec_pdphy;
	struct device *dev = pmic_typec_pdphy->dev;
	int ret;

	if (msg) {
		ret = qcom_pmic_typec_pdphy_pd_transmit_payload(pmic_typec_pdphy,
								type, msg,
								negotiated_rev);
	} else {
		ret = qcom_pmic_typec_pdphy_pd_transmit_signal(pmic_typec_pdphy,
							       type,
							       negotiated_rev);
	}

	if (ret)
		dev_dbg(dev, "pd_transmit: type %x result %d\n", type, ret);

	return ret;
}

static void qcom_pmic_typec_pdphy_pd_receive(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	struct device *dev = pmic_typec_pdphy->dev;
	struct pd_message msg;
	unsigned int size, rx_status;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_pdphy->lock, flags);

	ret = regmap_read(pmic_typec_pdphy->regmap,
			  pmic_typec_pdphy->base + USB_PDPHY_RX_SIZE_REG, &size);
	if (ret)
		goto done;

	/* Hardware requires +1 of the real read value to be passed */
	if (size < 1 || size > sizeof(msg.payload) + 1) {
		dev_dbg(dev, "pd_receive: invalid size %d\n", size);
		goto done;
	}

	size += 1;
	ret = regmap_read(pmic_typec_pdphy->regmap,
			  pmic_typec_pdphy->base + USB_PDPHY_RX_STATUS_REG,
			  &rx_status);

	if (ret)
		goto done;

	ret = regmap_bulk_read(pmic_typec_pdphy->regmap,
			       pmic_typec_pdphy->base + USB_PDPHY_RX_BUFFER_REG,
			       (u8 *)&msg, size);
	if (ret)
		goto done;

	/* Return ownership of RX buffer to hardware */
	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_RX_ACKNOWLEDGE_REG, 0);

done:
	spin_unlock_irqrestore(&pmic_typec_pdphy->lock, flags);

	if (!ret) {
		dev_vdbg(dev, "pd_receive: handing %d bytes to tcpm\n", size);
		tcpm_pd_receive(pmic_typec_pdphy->tcpm_port, &msg, TCPC_TX_SOP);
	}
}

static irqreturn_t qcom_pmic_typec_pdphy_isr(int irq, void *dev_id)
{
	struct pmic_typec_pdphy_irq_data *irq_data = dev_id;
	struct pmic_typec_pdphy *pmic_typec_pdphy = irq_data->pmic_typec_pdphy;
	struct device *dev = pmic_typec_pdphy->dev;

	switch (irq_data->virq) {
	case PMIC_PDPHY_SIG_TX_IRQ:
		dev_err(dev, "isr: tx_sig\n");
		break;
	case PMIC_PDPHY_SIG_RX_IRQ:
		schedule_work(&pmic_typec_pdphy->reset_work);
		break;
	case PMIC_PDPHY_MSG_TX_IRQ:
		tcpm_pd_transmit_complete(pmic_typec_pdphy->tcpm_port,
					  TCPC_TX_SUCCESS);
		break;
	case PMIC_PDPHY_MSG_RX_IRQ:
		qcom_pmic_typec_pdphy_pd_receive(pmic_typec_pdphy);
		break;
	case PMIC_PDPHY_MSG_TX_FAIL_IRQ:
		tcpm_pd_transmit_complete(pmic_typec_pdphy->tcpm_port,
					  TCPC_TX_FAILED);
		break;
	case PMIC_PDPHY_MSG_TX_DISCARD_IRQ:
		tcpm_pd_transmit_complete(pmic_typec_pdphy->tcpm_port,
					  TCPC_TX_DISCARDED);
		break;
	}

	return IRQ_HANDLED;
}

static int qcom_pmic_typec_pdphy_set_pd_rx(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_pdphy *pmic_typec_pdphy = tcpm->pmic_typec_pdphy;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_pdphy->lock, flags);

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_RX_ACKNOWLEDGE_REG, !on);

	spin_unlock_irqrestore(&pmic_typec_pdphy->lock, flags);

	dev_dbg(pmic_typec_pdphy->dev, "set_pd_rx: %s\n", on ? "on" : "off");

	return ret;
}

static int qcom_pmic_typec_pdphy_set_roles(struct tcpc_dev *tcpc, bool attached,
					   enum typec_role power_role,
					   enum typec_data_role data_role)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmic_typec_pdphy *pmic_typec_pdphy = tcpm->pmic_typec_pdphy;
	struct device *dev = pmic_typec_pdphy->dev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pmic_typec_pdphy->lock, flags);

	ret = regmap_update_bits(pmic_typec_pdphy->regmap,
				 pmic_typec_pdphy->base + USB_PDPHY_MSG_CONFIG_REG,
				 MSG_CONFIG_PORT_DATA_ROLE |
				 MSG_CONFIG_PORT_POWER_ROLE,
				 (data_role == TYPEC_HOST ? MSG_CONFIG_PORT_DATA_ROLE : 0) |
				 (power_role == TYPEC_SOURCE ? MSG_CONFIG_PORT_POWER_ROLE : 0));

	spin_unlock_irqrestore(&pmic_typec_pdphy->lock, flags);

	dev_dbg(dev, "pdphy_set_roles: data_role_host=%d power_role_src=%d\n",
		data_role, power_role);

	return ret;
}

static int qcom_pmic_typec_pdphy_enable(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	struct device *dev = pmic_typec_pdphy->dev;
	int ret;

	/* PD 2.0, DR=TYPEC_DEVICE, PR=TYPEC_SINK */
	ret = regmap_update_bits(pmic_typec_pdphy->regmap,
				 pmic_typec_pdphy->base + USB_PDPHY_MSG_CONFIG_REG,
				 MSG_CONFIG_SPEC_REV_MASK, PD_REV20);
	if (ret)
		goto done;

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_EN_CONTROL_REG, 0);
	if (ret)
		goto done;

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_EN_CONTROL_REG,
			   CONTROL_ENABLE);
	if (ret)
		goto done;

	qcom_pmic_typec_pdphy_reset_off(pmic_typec_pdphy);
done:
	if (ret)
		dev_err(dev, "pdphy_enable fail %d\n", ret);

	return ret;
}

static int qcom_pmic_typec_pdphy_disable(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	int ret;

	qcom_pmic_typec_pdphy_reset_on(pmic_typec_pdphy);

	ret = regmap_write(pmic_typec_pdphy->regmap,
			   pmic_typec_pdphy->base + USB_PDPHY_EN_CONTROL_REG, 0);

	return ret;
}

static int pmic_typec_pdphy_reset(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	int ret;

	ret = qcom_pmic_typec_pdphy_disable(pmic_typec_pdphy);
	if (ret)
		goto done;

	usleep_range(400, 500);
	ret = qcom_pmic_typec_pdphy_enable(pmic_typec_pdphy);
done:
	return ret;
}

static int qcom_pmic_typec_pdphy_start(struct pmic_typec *tcpm,
				       struct tcpm_port *tcpm_port)
{
	struct pmic_typec_pdphy *pmic_typec_pdphy = tcpm->pmic_typec_pdphy;
	int i;
	int ret;

	ret = regulator_enable(pmic_typec_pdphy->vdd_pdphy);
	if (ret)
		return ret;

	pmic_typec_pdphy->tcpm_port = tcpm_port;

	ret = pmic_typec_pdphy_reset(pmic_typec_pdphy);
	if (ret)
		goto err_disable_vdd_pdhy;

	for (i = 0; i < pmic_typec_pdphy->nr_irqs; i++)
		enable_irq(pmic_typec_pdphy->irq_data[i].irq);

	return 0;

err_disable_vdd_pdhy:
	regulator_disable(pmic_typec_pdphy->vdd_pdphy);

	return ret;
}

static void qcom_pmic_typec_pdphy_stop(struct pmic_typec *tcpm)
{
	struct pmic_typec_pdphy *pmic_typec_pdphy = tcpm->pmic_typec_pdphy;
	int i;

	for (i = 0; i < pmic_typec_pdphy->nr_irqs; i++)
		disable_irq(pmic_typec_pdphy->irq_data[i].irq);

	qcom_pmic_typec_pdphy_reset_on(pmic_typec_pdphy);

	regulator_disable(pmic_typec_pdphy->vdd_pdphy);
}

int qcom_pmic_typec_pdphy_probe(struct platform_device *pdev,
				struct pmic_typec *tcpm,
				const struct pmic_typec_pdphy_resources *res,
				struct regmap *regmap,
				u32 base)
{
	struct pmic_typec_pdphy *pmic_typec_pdphy;
	struct device *dev = &pdev->dev;
	struct pmic_typec_pdphy_irq_data *irq_data;
	int i, ret, irq;

	pmic_typec_pdphy = devm_kzalloc(dev, sizeof(*pmic_typec_pdphy), GFP_KERNEL);
	if (!pmic_typec_pdphy)
		return -ENOMEM;

	if (!res->nr_irqs || res->nr_irqs > PMIC_PDPHY_MAX_IRQS)
		return -EINVAL;

	irq_data = devm_kzalloc(dev, sizeof(*irq_data) * res->nr_irqs,
				GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	pmic_typec_pdphy->vdd_pdphy = devm_regulator_get(dev, "vdd-pdphy");
	if (IS_ERR(pmic_typec_pdphy->vdd_pdphy))
		return PTR_ERR(pmic_typec_pdphy->vdd_pdphy);

	pmic_typec_pdphy->dev = dev;
	pmic_typec_pdphy->base = base;
	pmic_typec_pdphy->regmap = regmap;
	pmic_typec_pdphy->nr_irqs = res->nr_irqs;
	pmic_typec_pdphy->irq_data = irq_data;
	spin_lock_init(&pmic_typec_pdphy->lock);
	INIT_WORK(&pmic_typec_pdphy->reset_work, qcom_pmic_typec_pdphy_sig_reset_work);

	for (i = 0; i < res->nr_irqs; i++, irq_data++) {
		irq = platform_get_irq_byname(pdev, res->irq_params[i].irq_name);
		if (irq < 0)
			return irq;

		irq_data->pmic_typec_pdphy = pmic_typec_pdphy;
		irq_data->irq = irq;
		irq_data->virq = res->irq_params[i].virq;

		ret = devm_request_threaded_irq(dev, irq, NULL,
						qcom_pmic_typec_pdphy_isr,
						IRQF_ONESHOT | IRQF_NO_AUTOEN,
						res->irq_params[i].irq_name,
						irq_data);
		if (ret)
			return ret;
	}

	tcpm->pmic_typec_pdphy = pmic_typec_pdphy;

	tcpm->tcpc.set_pd_rx = qcom_pmic_typec_pdphy_set_pd_rx;
	tcpm->tcpc.set_roles = qcom_pmic_typec_pdphy_set_roles;
	tcpm->tcpc.pd_transmit = qcom_pmic_typec_pdphy_pd_transmit;

	tcpm->pdphy_start = qcom_pmic_typec_pdphy_start;
	tcpm->pdphy_stop = qcom_pmic_typec_pdphy_stop;

	return 0;
}

const struct pmic_typec_pdphy_resources pm8150b_pdphy_res = {
	.irq_params = {
		{
			.virq = PMIC_PDPHY_SIG_TX_IRQ,
			.irq_name = "sig-tx",
		},
		{
			.virq = PMIC_PDPHY_SIG_RX_IRQ,
			.irq_name = "sig-rx",
		},
		{
			.virq = PMIC_PDPHY_MSG_TX_IRQ,
			.irq_name = "msg-tx",
		},
		{
			.virq = PMIC_PDPHY_MSG_RX_IRQ,
			.irq_name = "msg-rx",
		},
		{
			.virq = PMIC_PDPHY_MSG_TX_FAIL_IRQ,
			.irq_name = "msg-tx-failed",
		},
		{
			.virq = PMIC_PDPHY_MSG_TX_DISCARD_IRQ,
			.irq_name = "msg-tx-discarded",
		},
		{
			.virq = PMIC_PDPHY_MSG_RX_DISCARD_IRQ,
			.irq_name = "msg-rx-discarded",
		},
	},
	.nr_irqs = 7,
};
