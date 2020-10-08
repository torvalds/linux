/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2015-2017 Google, Inc
 *
 * USB Type-C Port Controller Interface.
 */

#ifndef __LINUX_USB_TCPCI_H
#define __LINUX_USB_TCPCI_H

#define TCPC_VENDOR_ID			0x0
#define TCPC_PRODUCT_ID			0x2
#define TCPC_BCD_DEV			0x4
#define TCPC_TC_REV			0x6
#define TCPC_PD_REV			0x8
#define TCPC_PD_INT_REV			0xa

#define TCPC_ALERT			0x10
#define TCPC_ALERT_EXTENDED_STATUS	BIT(13)
#define TCPC_ALERT_VBUS_DISCNCT		BIT(11)
#define TCPC_ALERT_RX_BUF_OVF		BIT(10)
#define TCPC_ALERT_FAULT		BIT(9)
#define TCPC_ALERT_V_ALARM_LO		BIT(8)
#define TCPC_ALERT_V_ALARM_HI		BIT(7)
#define TCPC_ALERT_TX_SUCCESS		BIT(6)
#define TCPC_ALERT_TX_DISCARDED		BIT(5)
#define TCPC_ALERT_TX_FAILED		BIT(4)
#define TCPC_ALERT_RX_HARD_RST		BIT(3)
#define TCPC_ALERT_RX_STATUS		BIT(2)
#define TCPC_ALERT_POWER_STATUS		BIT(1)
#define TCPC_ALERT_CC_STATUS		BIT(0)

#define TCPC_ALERT_MASK			0x12
#define TCPC_POWER_STATUS_MASK		0x14
#define TCPC_FAULT_STATUS_MASK		0x15

#define TCPC_EXTENDED_STATUS_MASK		0x16
#define TCPC_EXTENDED_STATUS_MASK_VSAFE0V	BIT(0)

#define TCPC_CONFIG_STD_OUTPUT		0x18

#define TCPC_TCPC_CTRL			0x19
#define TCPC_TCPC_CTRL_ORIENTATION	BIT(0)
#define TCPC_TCPC_CTRL_BIST_TM		BIT(1)

#define TCPC_ROLE_CTRL			0x1a
#define TCPC_ROLE_CTRL_DRP		BIT(6)
#define TCPC_ROLE_CTRL_RP_VAL_SHIFT	4
#define TCPC_ROLE_CTRL_RP_VAL_MASK	0x3
#define TCPC_ROLE_CTRL_RP_VAL_DEF	0x0
#define TCPC_ROLE_CTRL_RP_VAL_1_5	0x1
#define TCPC_ROLE_CTRL_RP_VAL_3_0	0x2
#define TCPC_ROLE_CTRL_CC2_SHIFT	2
#define TCPC_ROLE_CTRL_CC2_MASK		0x3
#define TCPC_ROLE_CTRL_CC1_SHIFT	0
#define TCPC_ROLE_CTRL_CC1_MASK		0x3
#define TCPC_ROLE_CTRL_CC_RA		0x0
#define TCPC_ROLE_CTRL_CC_RP		0x1
#define TCPC_ROLE_CTRL_CC_RD		0x2
#define TCPC_ROLE_CTRL_CC_OPEN		0x3

#define TCPC_FAULT_CTRL			0x1b

#define TCPC_POWER_CTRL			0x1c
#define TCPC_POWER_CTRL_VCONN_ENABLE	BIT(0)

#define TCPC_CC_STATUS			0x1d
#define TCPC_CC_STATUS_TOGGLING		BIT(5)
#define TCPC_CC_STATUS_TERM		BIT(4)
#define TCPC_CC_STATUS_CC2_SHIFT	2
#define TCPC_CC_STATUS_CC2_MASK		0x3
#define TCPC_CC_STATUS_CC1_SHIFT	0
#define TCPC_CC_STATUS_CC1_MASK		0x3

#define TCPC_POWER_STATUS		0x1e
#define TCPC_POWER_STATUS_UNINIT	BIT(6)
#define TCPC_POWER_STATUS_VBUS_DET	BIT(3)
#define TCPC_POWER_STATUS_VBUS_PRES	BIT(2)

#define TCPC_FAULT_STATUS		0x1f

#define TCPC_COMMAND			0x23
#define TCPC_CMD_WAKE_I2C		0x11
#define TCPC_CMD_DISABLE_VBUS_DETECT	0x22
#define TCPC_CMD_ENABLE_VBUS_DETECT	0x33
#define TCPC_CMD_DISABLE_SINK_VBUS	0x44
#define TCPC_CMD_SINK_VBUS		0x55
#define TCPC_CMD_DISABLE_SRC_VBUS	0x66
#define TCPC_CMD_SRC_VBUS_DEFAULT	0x77
#define TCPC_CMD_SRC_VBUS_HIGH		0x88
#define TCPC_CMD_LOOK4CONNECTION	0x99
#define TCPC_CMD_RXONEMORE		0xAA
#define TCPC_CMD_I2C_IDLE		0xFF

#define TCPC_DEV_CAP_1			0x24
#define TCPC_DEV_CAP_2			0x26
#define TCPC_STD_INPUT_CAP		0x28
#define TCPC_STD_OUTPUT_CAP		0x29

#define TCPC_MSG_HDR_INFO		0x2e
#define TCPC_MSG_HDR_INFO_DATA_ROLE	BIT(3)
#define TCPC_MSG_HDR_INFO_PWR_ROLE	BIT(0)
#define TCPC_MSG_HDR_INFO_REV_SHIFT	1
#define TCPC_MSG_HDR_INFO_REV_MASK	0x3

#define TCPC_RX_DETECT			0x2f
#define TCPC_RX_DETECT_HARD_RESET	BIT(5)
#define TCPC_RX_DETECT_SOP		BIT(0)

#define TCPC_RX_BYTE_CNT		0x30
#define TCPC_RX_BUF_FRAME_TYPE		0x31
#define TCPC_RX_BUF_FRAME_TYPE_SOP	0
#define TCPC_RX_HDR			0x32
#define TCPC_RX_DATA			0x34 /* through 0x4f */

#define TCPC_TRANSMIT			0x50
#define TCPC_TRANSMIT_RETRY_SHIFT	4
#define TCPC_TRANSMIT_RETRY_MASK	0x3
#define TCPC_TRANSMIT_TYPE_SHIFT	0
#define TCPC_TRANSMIT_TYPE_MASK		0x7

#define TCPC_TX_BYTE_CNT		0x51
#define TCPC_TX_HDR			0x52
#define TCPC_TX_DATA			0x54 /* through 0x6f */

#define TCPC_VBUS_VOLTAGE			0x70
#define TCPC_VBUS_SINK_DISCONNECT_THRESH	0x72
#define TCPC_VBUS_STOP_DISCHARGE_THRESH		0x74
#define TCPC_VBUS_VOLTAGE_ALARM_HI_CFG		0x76
#define TCPC_VBUS_VOLTAGE_ALARM_LO_CFG		0x78

/* I2C_WRITE_BYTE_COUNT + 1 when TX_BUF_BYTE_x is only accessible I2C_WRITE_BYTE_COUNT */
#define TCPC_TRANSMIT_BUFFER_MAX_LEN		31

/*
 * @TX_BUF_BYTE_x_hidden
 *		optional; Set when TX_BUF_BYTE_x can only be accessed through I2C_WRITE_BYTE_COUNT.
 */
struct tcpci;
struct tcpci_data {
	struct regmap *regmap;
	unsigned char TX_BUF_BYTE_x_hidden:1;
	int (*init)(struct tcpci *tcpci, struct tcpci_data *data);
	int (*set_vconn)(struct tcpci *tcpci, struct tcpci_data *data,
			 bool enable);
	int (*start_drp_toggling)(struct tcpci *tcpci, struct tcpci_data *data,
				  enum typec_cc_status cc);
	int (*set_vbus)(struct tcpci *tcpci, struct tcpci_data *data, bool source, bool sink);
};

struct tcpci *tcpci_register_port(struct device *dev, struct tcpci_data *data);
void tcpci_unregister_port(struct tcpci *tcpci);
irqreturn_t tcpci_irq(struct tcpci *tcpci);

struct tcpm_port;
struct tcpm_port *tcpci_get_tcpm_port(struct tcpci *tcpci);
#endif /* __LINUX_USB_TCPCI_H */
