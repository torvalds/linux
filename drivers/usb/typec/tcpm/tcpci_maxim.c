// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Google LLC
 *
 * MAXIM TCPCI based TCPC driver
 */

#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/usb/pd.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec.h>

#include "tcpci.h"

#define PD_ACTIVITY_TIMEOUT_MS				10000

#define TCPC_VENDOR_ALERT				0x80
#define TCPC_VENDOR_USBSW_CTRL				0x93
#define TCPC_VENDOR_USBSW_CTRL_ENABLE_USB_DATA		0x9
#define TCPC_VENDOR_USBSW_CTRL_DISABLE_USB_DATA		0

#define TCPC_RECEIVE_BUFFER_COUNT_OFFSET		0
#define TCPC_RECEIVE_BUFFER_FRAME_TYPE_OFFSET		1
#define TCPC_RECEIVE_BUFFER_RX_BYTE_BUF_OFFSET		2

/*
 * LongMessage not supported, hence 32 bytes for buf to be read from RECEIVE_BUFFER.
 * DEVICE_CAPABILITIES_2.LongMessage = 0, the value in READABLE_BYTE_COUNT reg shall be
 * less than or equal to 31. Since, RECEIVE_BUFFER len = 31 + 1(READABLE_BYTE_COUNT).
 */
#define TCPC_RECEIVE_BUFFER_LEN				32

#define MAX_BUCK_BOOST_SID				0x69
#define MAX_BUCK_BOOST_OP				0xb9
#define MAX_BUCK_BOOST_OFF				0
#define MAX_BUCK_BOOST_SOURCE				0xa
#define MAX_BUCK_BOOST_SINK				0x5

struct max_tcpci_chip {
	struct tcpci_data data;
	struct tcpci *tcpci;
	struct device *dev;
	struct i2c_client *client;
	struct tcpm_port *port;
};

static const struct regmap_range max_tcpci_tcpci_range[] = {
	regmap_reg_range(0x00, 0x95)
};

const struct regmap_access_table max_tcpci_tcpci_write_table = {
	.yes_ranges = max_tcpci_tcpci_range,
	.n_yes_ranges = ARRAY_SIZE(max_tcpci_tcpci_range),
};

static const struct regmap_config max_tcpci_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x95,
	.wr_table = &max_tcpci_tcpci_write_table,
};

static struct max_tcpci_chip *tdata_to_max_tcpci(struct tcpci_data *tdata)
{
	return container_of(tdata, struct max_tcpci_chip, data);
}

static int max_tcpci_read16(struct max_tcpci_chip *chip, unsigned int reg, u16 *val)
{
	return regmap_raw_read(chip->data.regmap, reg, val, sizeof(u16));
}

static int max_tcpci_write16(struct max_tcpci_chip *chip, unsigned int reg, u16 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u16));
}

static int max_tcpci_read8(struct max_tcpci_chip *chip, unsigned int reg, u8 *val)
{
	return regmap_raw_read(chip->data.regmap, reg, val, sizeof(u8));
}

static int max_tcpci_write8(struct max_tcpci_chip *chip, unsigned int reg, u8 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u8));
}

static void max_tcpci_init_regs(struct max_tcpci_chip *chip)
{
	u16 alert_mask = 0;
	int ret;

	ret = max_tcpci_write16(chip, TCPC_ALERT, 0xffff);
	if (ret < 0) {
		dev_err(chip->dev, "Error writing to TCPC_ALERT ret:%d\n", ret);
		return;
	}

	ret = max_tcpci_write16(chip, TCPC_VENDOR_ALERT, 0xffff);
	if (ret < 0) {
		dev_err(chip->dev, "Error writing to TCPC_VENDOR_ALERT ret:%d\n", ret);
		return;
	}

	ret = max_tcpci_write8(chip, TCPC_ALERT_EXTENDED, 0xff);
	if (ret < 0) {
		dev_err(chip->dev, "Unable to clear TCPC_ALERT_EXTENDED ret:%d\n", ret);
		return;
	}

	/* Enable VSAFE0V detection */
	ret = max_tcpci_write8(chip, TCPC_EXTENDED_STATUS_MASK, TCPC_EXTENDED_STATUS_VSAFE0V);
	if (ret < 0) {
		dev_err(chip->dev, "Unable to unmask TCPC_EXTENDED_STATUS_VSAFE0V ret:%d\n", ret);
		return;
	}

	alert_mask = TCPC_ALERT_TX_SUCCESS | TCPC_ALERT_TX_DISCARDED | TCPC_ALERT_TX_FAILED |
		TCPC_ALERT_RX_HARD_RST | TCPC_ALERT_RX_STATUS | TCPC_ALERT_CC_STATUS |
		TCPC_ALERT_VBUS_DISCNCT | TCPC_ALERT_RX_BUF_OVF | TCPC_ALERT_POWER_STATUS |
		/* Enable Extended alert for detecting Fast Role Swap Signal */
		TCPC_ALERT_EXTND | TCPC_ALERT_EXTENDED_STATUS;

	ret = max_tcpci_write16(chip, TCPC_ALERT_MASK, alert_mask);
	if (ret < 0) {
		dev_err(chip->dev,
			"Error enabling TCPC_ALERT: TCPC_ALERT_MASK write failed ret:%d\n", ret);
		return;
	}

	/* Enable vbus voltage monitoring and voltage alerts */
	ret = max_tcpci_write8(chip, TCPC_POWER_CTRL, 0);
	if (ret < 0) {
		dev_err(chip->dev, "Error writing to TCPC_POWER_CTRL ret:%d\n", ret);
		return;
	}

	ret = max_tcpci_write8(chip, TCPC_ALERT_EXTENDED_MASK, TCPC_SINK_FAST_ROLE_SWAP);
	if (ret < 0)
		return;
}

static void process_rx(struct max_tcpci_chip *chip, u16 status)
{
	struct pd_message msg;
	u8 count, frame_type, rx_buf[TCPC_RECEIVE_BUFFER_LEN];
	int ret, payload_index;
	u8 *rx_buf_ptr;

	/*
	 * READABLE_BYTE_COUNT: Indicates the number of bytes in the RX_BUF_BYTE_x registers
	 * plus one (for the RX_BUF_FRAME_TYPE) Table 4-36.
	 * Read the count and frame type.
	 */
	ret = regmap_raw_read(chip->data.regmap, TCPC_RX_BYTE_CNT, rx_buf, 2);
	if (ret < 0) {
		dev_err(chip->dev, "TCPC_RX_BYTE_CNT read failed ret:%d", ret);
		return;
	}

	count = rx_buf[TCPC_RECEIVE_BUFFER_COUNT_OFFSET];
	frame_type = rx_buf[TCPC_RECEIVE_BUFFER_FRAME_TYPE_OFFSET];

	if (count == 0 || frame_type != TCPC_RX_BUF_FRAME_TYPE_SOP) {
		max_tcpci_write16(chip, TCPC_ALERT, TCPC_ALERT_RX_STATUS);
		dev_err(chip->dev, "%s", count ==  0 ? "error: count is 0" :
			"error frame_type is not SOP");
		return;
	}

	if (count > sizeof(struct pd_message) || count + 1 > TCPC_RECEIVE_BUFFER_LEN) {
		dev_err(chip->dev, "Invalid TCPC_RX_BYTE_CNT %d", count);
		return;
	}

	/*
	 * Read count + 1 as RX_BUF_BYTE_x is hidden and can only be read through
	 * TCPC_RX_BYTE_CNT
	 */
	count += 1;
	ret = regmap_raw_read(chip->data.regmap, TCPC_RX_BYTE_CNT, rx_buf, count);
	if (ret < 0) {
		dev_err(chip->dev, "Error: TCPC_RX_BYTE_CNT read failed: %d", ret);
		return;
	}

	rx_buf_ptr = rx_buf + TCPC_RECEIVE_BUFFER_RX_BYTE_BUF_OFFSET;
	msg.header = cpu_to_le16(*(u16 *)rx_buf_ptr);
	rx_buf_ptr = rx_buf_ptr + sizeof(msg.header);
	for (payload_index = 0; payload_index < pd_header_cnt_le(msg.header); payload_index++,
	     rx_buf_ptr += sizeof(msg.payload[0]))
		msg.payload[payload_index] = cpu_to_le32(*(u32 *)rx_buf_ptr);

	/*
	 * Read complete, clear RX status alert bit.
	 * Clear overflow as well if set.
	 */
	ret = max_tcpci_write16(chip, TCPC_ALERT, status & TCPC_ALERT_RX_BUF_OVF ?
				TCPC_ALERT_RX_STATUS | TCPC_ALERT_RX_BUF_OVF :
				TCPC_ALERT_RX_STATUS);
	if (ret < 0)
		return;

	tcpm_pd_receive(chip->port, &msg);
}

static int max_tcpci_set_vbus(struct tcpci *tcpci, struct tcpci_data *tdata, bool source, bool sink)
{
	struct max_tcpci_chip *chip = tdata_to_max_tcpci(tdata);
	u8 buffer_source[2] = {MAX_BUCK_BOOST_OP, MAX_BUCK_BOOST_SOURCE};
	u8 buffer_sink[2] = {MAX_BUCK_BOOST_OP, MAX_BUCK_BOOST_SINK};
	u8 buffer_none[2] = {MAX_BUCK_BOOST_OP, MAX_BUCK_BOOST_OFF};
	struct i2c_client *i2c = chip->client;
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr = MAX_BUCK_BOOST_SID,
			.flags = i2c->flags & I2C_M_TEN,
			.len = 2,
			.buf = source ? buffer_source : sink ? buffer_sink : buffer_none,
		},
	};

	if (source && sink) {
		dev_err(chip->dev, "Both source and sink set\n");
		return -EINVAL;
	}

	ret = i2c_transfer(i2c->adapter, msgs, 1);

	return  ret < 0 ? ret : 1;
}

static void process_power_status(struct max_tcpci_chip *chip)
{
	u8 pwr_status;
	int ret;

	ret = max_tcpci_read8(chip, TCPC_POWER_STATUS, &pwr_status);
	if (ret < 0)
		return;

	if (pwr_status == 0xff)
		max_tcpci_init_regs(chip);
	else if (pwr_status & TCPC_POWER_STATUS_SOURCING_VBUS)
		tcpm_sourcing_vbus(chip->port);
	else
		tcpm_vbus_change(chip->port);
}

static void max_tcpci_frs_sourcing_vbus(struct tcpci *tcpci, struct tcpci_data *tdata)
{
	/*
	 * For Fast Role Swap case, Boost turns on autonomously without
	 * AP intervention, but, needs AP to enable source mode explicitly
	 * for AP to regain control.
	 */
	max_tcpci_set_vbus(tcpci, tdata, true, false);
}

static void process_tx(struct max_tcpci_chip *chip, u16 status)
{
	if (status & TCPC_ALERT_TX_SUCCESS)
		tcpm_pd_transmit_complete(chip->port, TCPC_TX_SUCCESS);
	else if (status & TCPC_ALERT_TX_DISCARDED)
		tcpm_pd_transmit_complete(chip->port, TCPC_TX_DISCARDED);
	else if (status & TCPC_ALERT_TX_FAILED)
		tcpm_pd_transmit_complete(chip->port, TCPC_TX_FAILED);

	/* Reinit regs as Hard reset sets them to default value */
	if ((status & TCPC_ALERT_TX_SUCCESS) && (status & TCPC_ALERT_TX_FAILED))
		max_tcpci_init_regs(chip);
}

/* Enable USB switches when partner is USB communications capable */
static void max_tcpci_set_partner_usb_comm_capable(struct tcpci *tcpci, struct tcpci_data *data,
						   bool capable)
{
	struct max_tcpci_chip *chip = tdata_to_max_tcpci(data);
	int ret;

	ret = max_tcpci_write8(chip, TCPC_VENDOR_USBSW_CTRL, capable ?
			       TCPC_VENDOR_USBSW_CTRL_ENABLE_USB_DATA :
			       TCPC_VENDOR_USBSW_CTRL_DISABLE_USB_DATA);

	if (ret < 0)
		dev_err(chip->dev, "Failed to enable USB switches");
}

static irqreturn_t _max_tcpci_irq(struct max_tcpci_chip *chip, u16 status)
{
	u16 mask;
	int ret;
	u8 reg_status;

	/*
	 * Clear alert status for everything except RX_STATUS, which shouldn't
	 * be cleared until we have successfully retrieved message.
	 */
	if (status & ~TCPC_ALERT_RX_STATUS) {
		mask = status & TCPC_ALERT_RX_BUF_OVF ?
			status & ~(TCPC_ALERT_RX_STATUS | TCPC_ALERT_RX_BUF_OVF) :
			status & ~TCPC_ALERT_RX_STATUS;
		ret = max_tcpci_write16(chip, TCPC_ALERT, mask);
		if (ret < 0) {
			dev_err(chip->dev, "ALERT clear failed\n");
			return ret;
		}
	}

	if (status & TCPC_ALERT_RX_BUF_OVF && !(status & TCPC_ALERT_RX_STATUS)) {
		ret = max_tcpci_write16(chip, TCPC_ALERT, (TCPC_ALERT_RX_STATUS |
							  TCPC_ALERT_RX_BUF_OVF));
		if (ret < 0) {
			dev_err(chip->dev, "ALERT clear failed\n");
			return ret;
		}
	}

	if (status & TCPC_ALERT_EXTND) {
		ret = max_tcpci_read8(chip, TCPC_ALERT_EXTENDED, &reg_status);
		if (ret < 0)
			return ret;

		ret = max_tcpci_write8(chip, TCPC_ALERT_EXTENDED, reg_status);
		if (ret < 0)
			return ret;

		if (reg_status & TCPC_SINK_FAST_ROLE_SWAP) {
			dev_info(chip->dev, "FRS Signal");
			tcpm_sink_frs(chip->port);
		}
	}

	if (status & TCPC_ALERT_EXTENDED_STATUS) {
		ret = max_tcpci_read8(chip, TCPC_EXTENDED_STATUS, (u8 *)&reg_status);
		if (ret >= 0 && (reg_status & TCPC_EXTENDED_STATUS_VSAFE0V))
			tcpm_vbus_change(chip->port);
	}

	if (status & TCPC_ALERT_RX_STATUS)
		process_rx(chip, status);

	if (status & TCPC_ALERT_VBUS_DISCNCT)
		tcpm_vbus_change(chip->port);

	if (status & TCPC_ALERT_CC_STATUS)
		tcpm_cc_change(chip->port);

	if (status & TCPC_ALERT_POWER_STATUS)
		process_power_status(chip);

	if (status & TCPC_ALERT_RX_HARD_RST) {
		tcpm_pd_hard_reset(chip->port);
		max_tcpci_init_regs(chip);
	}

	if (status & TCPC_ALERT_TX_SUCCESS || status & TCPC_ALERT_TX_DISCARDED || status &
	    TCPC_ALERT_TX_FAILED)
		process_tx(chip, status);

	return IRQ_HANDLED;
}

static irqreturn_t max_tcpci_irq(int irq, void *dev_id)
{
	struct max_tcpci_chip *chip = dev_id;
	u16 status;
	irqreturn_t irq_return = IRQ_HANDLED;
	int ret;

	if (!chip->port)
		return IRQ_HANDLED;

	ret = max_tcpci_read16(chip, TCPC_ALERT, &status);
	if (ret < 0) {
		dev_err(chip->dev, "ALERT read failed\n");
		return ret;
	}
	while (status) {
		irq_return = _max_tcpci_irq(chip, status);
		/* Do not return if the ALERT is already set. */
		ret = max_tcpci_read16(chip, TCPC_ALERT, &status);
		if (ret < 0)
			break;
	}

	return irq_return;
}

static irqreturn_t max_tcpci_isr(int irq, void *dev_id)
{
	struct max_tcpci_chip *chip = dev_id;

	pm_wakeup_event(chip->dev, PD_ACTIVITY_TIMEOUT_MS);

	if (!chip->port)
		return IRQ_HANDLED;

	return IRQ_WAKE_THREAD;
}

static int max_tcpci_init_alert(struct max_tcpci_chip *chip, struct i2c_client *client)
{
	int ret;

	ret = devm_request_threaded_irq(chip->dev, client->irq, max_tcpci_isr, max_tcpci_irq,
					(IRQF_TRIGGER_LOW | IRQF_ONESHOT), dev_name(chip->dev),
					chip);

	if (ret < 0)
		return ret;

	enable_irq_wake(client->irq);
	return 0;
}

static int max_tcpci_start_toggling(struct tcpci *tcpci, struct tcpci_data *tdata,
				    enum typec_cc_status cc)
{
	struct max_tcpci_chip *chip = tdata_to_max_tcpci(tdata);

	max_tcpci_init_regs(chip);

	return 0;
}

static int tcpci_init(struct tcpci *tcpci, struct tcpci_data *data)
{
	/*
	 * Generic TCPCI overwrites the regs once this driver initializes
	 * them. Prevent this by returning -1.
	 */
	return -1;
}

static int max_tcpci_probe(struct i2c_client *client, const struct i2c_device_id *i2c_id)
{
	int ret;
	struct max_tcpci_chip *chip;
	u8 power_status;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->data.regmap = devm_regmap_init_i2c(client, &max_tcpci_regmap_config);
	if (IS_ERR(chip->data.regmap)) {
		dev_err(&client->dev, "Regmap init failed\n");
		return PTR_ERR(chip->data.regmap);
	}

	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

	ret = max_tcpci_read8(chip, TCPC_POWER_STATUS, &power_status);
	if (ret < 0)
		return ret;

	/* Chip level tcpci callbacks */
	chip->data.set_vbus = max_tcpci_set_vbus;
	chip->data.start_drp_toggling = max_tcpci_start_toggling;
	chip->data.TX_BUF_BYTE_x_hidden = true;
	chip->data.init = tcpci_init;
	chip->data.frs_sourcing_vbus = max_tcpci_frs_sourcing_vbus;
	chip->data.auto_discharge_disconnect = true;
	chip->data.vbus_vsafe0v = true;
	chip->data.set_partner_usb_comm_capable = max_tcpci_set_partner_usb_comm_capable;

	max_tcpci_init_regs(chip);
	chip->tcpci = tcpci_register_port(chip->dev, &chip->data);
	if (IS_ERR(chip->tcpci)) {
		dev_err(&client->dev, "TCPCI port registration failed");
		ret = PTR_ERR(chip->tcpci);
		return PTR_ERR(chip->tcpci);
	}
	chip->port = tcpci_get_tcpm_port(chip->tcpci);
	ret = max_tcpci_init_alert(chip, client);
	if (ret < 0)
		goto unreg_port;

	device_init_wakeup(chip->dev, true);
	return 0;

unreg_port:
	tcpci_unregister_port(chip->tcpci);

	return ret;
}

static int max_tcpci_remove(struct i2c_client *client)
{
	struct max_tcpci_chip *chip = i2c_get_clientdata(client);

	if (!IS_ERR_OR_NULL(chip->tcpci))
		tcpci_unregister_port(chip->tcpci);

	return 0;
}

static const struct i2c_device_id max_tcpci_id[] = {
	{ "maxtcpc", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max_tcpci_id);

#ifdef CONFIG_OF
static const struct of_device_id max_tcpci_of_match[] = {
	{ .compatible = "maxim,max33359", },
	{},
};
MODULE_DEVICE_TABLE(of, max_tcpci_of_match);
#endif

static struct i2c_driver max_tcpci_i2c_driver = {
	.driver = {
		.name = "maxtcpc",
		.of_match_table = of_match_ptr(max_tcpci_of_match),
	},
	.probe = max_tcpci_probe,
	.remove = max_tcpci_remove,
	.id_table = max_tcpci_id,
};
module_i2c_driver(max_tcpci_i2c_driver);

MODULE_AUTHOR("Badhri Jagan Sridharan <badhri@google.com>");
MODULE_DESCRIPTION("Maxim TCPCI based USB Type-C Port Controller Interface Driver");
MODULE_LICENSE("GPL v2");
