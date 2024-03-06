/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */
#ifndef __QCOM_PMIC_PDPHY_H__
#define __QCOM_PMIC_PDPHY_H__

#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/usb/tcpm.h>

#define USB_PDPHY_MAX_DATA_OBJ_LEN	28
#define USB_PDPHY_MSG_HDR_LEN		2

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

/* Resources */
#define PMIC_PDPHY_MAX_IRQS		0x08

struct pmic_typec_pdphy_irq_params {
	int				virq;
	char				*irq_name;
};

struct pmic_typec_pdphy_resources {
	unsigned int				nr_irqs;
	struct pmic_typec_pdphy_irq_params	irq_params[PMIC_PDPHY_MAX_IRQS];
};

/* API */
struct pmic_typec_pdphy;

struct pmic_typec_pdphy *qcom_pmic_typec_pdphy_alloc(struct device *dev);

int qcom_pmic_typec_pdphy_probe(struct platform_device *pdev,
				struct pmic_typec_pdphy *pmic_typec_pdphy,
				struct pmic_typec_pdphy_resources *res,
				struct regmap *regmap,
				u32 base);

int qcom_pmic_typec_pdphy_start(struct pmic_typec_pdphy *pmic_typec_pdphy,
				struct tcpm_port *tcpm_port);

void qcom_pmic_typec_pdphy_stop(struct pmic_typec_pdphy *pmic_typec_pdphy);

int qcom_pmic_typec_pdphy_set_roles(struct pmic_typec_pdphy *pmic_typec_pdphy,
				    bool power_role_src, bool data_role_host);

int qcom_pmic_typec_pdphy_set_pd_rx(struct pmic_typec_pdphy *pmic_typec_pdphy, bool on);

int qcom_pmic_typec_pdphy_pd_transmit(struct pmic_typec_pdphy *pmic_typec_pdphy,
				      enum tcpm_transmit_type type,
				      const struct pd_message *msg,
				      unsigned int negotiated_rev);

#endif /* __QCOM_PMIC_TYPEC_PDPHY_H__ */
