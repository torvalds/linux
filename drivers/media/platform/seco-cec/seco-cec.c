// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * CEC driver for SECO X86 Boards
 *
 * Author:  Ettore Chimenti <ek5.chimenti@gmail.com>
 * Copyright (C) 2018, SECO SpA.
 * Copyright (C) 2018, Aidilab Srl.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

/* CEC Framework */
#include <media/cec-notifier.h>

#include "seco-cec.h"

struct secocec_data {
	struct device *dev;
	struct platform_device *pdev;
	struct cec_adapter *cec_adap;
	struct cec_notifier *notifier;
	struct rc_dev *ir;
	char ir_input_phys[32];
	int irq;
};

#define smb_wr16(cmd, data) smb_word_op(CMD_WORD_DATA, SECOCEC_MICRO_ADDRESS, \
					     cmd, data, SMBUS_WRITE, NULL)
#define smb_rd16(cmd, res) smb_word_op(CMD_WORD_DATA, SECOCEC_MICRO_ADDRESS, \
				       cmd, 0, SMBUS_READ, res)

static int smb_word_op(short data_format, u16 slave_addr, u8 cmd, u16 data,
		       u8 operation, u16 *result)
{
	unsigned int count;
	short _data_format;
	int status = 0;

	switch (data_format) {
	case CMD_BYTE_DATA:
		_data_format = BRA_SMB_CMD_BYTE_DATA;
		break;
	case CMD_WORD_DATA:
		_data_format = BRA_SMB_CMD_WORD_DATA;
		break;
	default:
		return -EINVAL;
	}

	/* Active wait until ready */
	for (count = 0; count <= SMBTIMEOUT; ++count) {
		if (!(inb(HSTS) & BRA_INUSE_STS))
			break;
		udelay(SMB_POLL_UDELAY);
	}

	if (count > SMBTIMEOUT)
		/* Reset the lock instead of failing */
		outb(0xff, HSTS);

	outb(0x00, HCNT);
	outb((u8)(slave_addr & 0xfe) | operation, XMIT_SLVA);
	outb(cmd, HCMD);
	inb(HCNT);

	if (operation == SMBUS_WRITE) {
		outb((u8)data, HDAT0);
		outb((u8)(data >> 8), HDAT1);
	}

	outb(BRA_START + _data_format, HCNT);

	for (count = 0; count <= SMBTIMEOUT; count++) {
		if (!(inb(HSTS) & BRA_HOST_BUSY))
			break;
		udelay(SMB_POLL_UDELAY);
	}

	if (count > SMBTIMEOUT) {
		status = -EBUSY;
		goto err;
	}

	if (inb(HSTS) & BRA_HSTS_ERR_MASK) {
		status = -EIO;
		goto err;
	}

	if (operation == SMBUS_READ)
		*result = ((inb(HDAT0) & 0xff) + ((inb(HDAT1) & 0xff) << 8));

err:
	outb(0xff, HSTS);
	return status;
}

static int secocec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct secocec_data *cec = cec_get_drvdata(adap);
	struct device *dev = cec->dev;
	u16 val = 0;
	int status;

	if (enable) {
		/* Clear the status register */
		status = smb_rd16(SECOCEC_STATUS_REG_1, &val);
		if (status)
			goto err;

		status = smb_wr16(SECOCEC_STATUS_REG_1, val);
		if (status)
			goto err;

		/* Enable the interrupts */
		status = smb_rd16(SECOCEC_ENABLE_REG_1, &val);
		if (status)
			goto err;

		status = smb_wr16(SECOCEC_ENABLE_REG_1,
				  val | SECOCEC_ENABLE_REG_1_CEC);
		if (status)
			goto err;

		dev_dbg(dev, "Device enabled");
	} else {
		/* Clear the status register */
		status = smb_rd16(SECOCEC_STATUS_REG_1, &val);
		status = smb_wr16(SECOCEC_STATUS_REG_1, val);

		/* Disable the interrupts */
		status = smb_rd16(SECOCEC_ENABLE_REG_1, &val);
		status = smb_wr16(SECOCEC_ENABLE_REG_1, val &
				  ~SECOCEC_ENABLE_REG_1_CEC &
				  ~SECOCEC_ENABLE_REG_1_IR);

		dev_dbg(dev, "Device disabled");
	}

	return 0;
err:
	return status;
}

static int secocec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	u16 enable_val = 0;
	int status;

	/* Disable device */
	status = smb_rd16(SECOCEC_ENABLE_REG_1, &enable_val);
	if (status)
		return status;

	status = smb_wr16(SECOCEC_ENABLE_REG_1,
			  enable_val & ~SECOCEC_ENABLE_REG_1_CEC);
	if (status)
		return status;

	/* Write logical address
	 * NOTE: CEC_LOG_ADDR_INVALID is mapped to the 'Unregistered' LA
	 */
	status = smb_wr16(SECOCEC_DEVICE_LA, logical_addr & 0xf);
	if (status)
		return status;

	/* Re-enable device */
	status = smb_wr16(SECOCEC_ENABLE_REG_1,
			  enable_val | SECOCEC_ENABLE_REG_1_CEC);
	if (status)
		return status;

	return 0;
}

static int secocec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg)
{
	u16 payload_len, payload_id_len, destination, val = 0;
	u8 *payload_msg;
	int status;
	u8 i;

	/* Device msg len already accounts for header */
	payload_id_len = msg->len - 1;

	/* Send data length */
	status = smb_wr16(SECOCEC_WRITE_DATA_LENGTH, payload_id_len);
	if (status)
		goto err;

	/* Send Operation ID if present */
	if (payload_id_len > 0) {
		status = smb_wr16(SECOCEC_WRITE_OPERATION_ID, msg->msg[1]);
		if (status)
			goto err;
	}
	/* Send data if present */
	if (payload_id_len > 1) {
		/* Only data; */
		payload_len = msg->len - 2;
		payload_msg = &msg->msg[2];

		/* Copy message into registers */
		for (i = 0; i < payload_len; i += 2) {
			/* hi byte */
			val = payload_msg[i + 1] << 8;

			/* lo byte */
			val |= payload_msg[i];

			status = smb_wr16(SECOCEC_WRITE_DATA_00 + i / 2, val);
			if (status)
				goto err;
		}
	}
	/* Send msg source/destination and fire msg */
	destination = msg->msg[0];
	status = smb_wr16(SECOCEC_WRITE_BYTE0, destination);
	if (status)
		goto err;

	return 0;

err:
	return status;
}

static void secocec_tx_done(struct cec_adapter *adap, u16 status_val)
{
	if (status_val & SECOCEC_STATUS_TX_ERROR_MASK) {
		if (status_val & SECOCEC_STATUS_TX_NACK_ERROR)
			cec_transmit_attempt_done(adap, CEC_TX_STATUS_NACK);
		else
			cec_transmit_attempt_done(adap, CEC_TX_STATUS_ERROR);
	} else {
		cec_transmit_attempt_done(adap, CEC_TX_STATUS_OK);
	}

	/* Reset status reg */
	status_val = SECOCEC_STATUS_TX_ERROR_MASK |
		SECOCEC_STATUS_MSG_SENT_MASK |
		SECOCEC_STATUS_TX_NACK_ERROR;
	smb_wr16(SECOCEC_STATUS, status_val);
}

static void secocec_rx_done(struct cec_adapter *adap, u16 status_val)
{
	struct secocec_data *cec = cec_get_drvdata(adap);
	struct device *dev = cec->dev;
	struct cec_msg msg = { };
	bool flag_overflow = false;
	u8 payload_len, i = 0;
	u8 *payload_msg;
	u16 val = 0;
	int status;

	if (status_val & SECOCEC_STATUS_RX_OVERFLOW_MASK) {
		/* NOTE: Untested, it also might not be necessary */
		dev_warn(dev, "Received more than 16 bytes. Discarding");
		flag_overflow = true;
	}

	if (status_val & SECOCEC_STATUS_RX_ERROR_MASK) {
		dev_warn(dev, "Message received with errors. Discarding");
		status = -EIO;
		goto rxerr;
	}

	/* Read message length */
	status = smb_rd16(SECOCEC_READ_DATA_LENGTH, &val);
	if (status)
		return;

	/* Device msg len already accounts for the header */
	msg.len = min(val + 1, CEC_MAX_MSG_SIZE);

	/* Read logical address */
	status = smb_rd16(SECOCEC_READ_BYTE0, &val);
	if (status)
		return;

	/* device stores source LA and destination */
	msg.msg[0] = val;

	/* Read operation ID */
	status = smb_rd16(SECOCEC_READ_OPERATION_ID, &val);
	if (status)
		return;

	msg.msg[1] = val;

	/* Read data if present */
	if (msg.len > 1) {
		payload_len = msg.len - 2;
		payload_msg = &msg.msg[2];

		/* device stores 2 bytes in every 16-bit val */
		for (i = 0; i < payload_len; i += 2) {
			status = smb_rd16(SECOCEC_READ_DATA_00 + i / 2, &val);
			if (status)
				return;

			/* low byte, skipping header */
			payload_msg[i] = val & 0x00ff;

			/* hi byte */
			payload_msg[i + 1] = (val & 0xff00) >> 8;
		}
	}

	cec_received_msg(cec->cec_adap, &msg);

	/* Reset status reg */
	status_val = SECOCEC_STATUS_MSG_RECEIVED_MASK;
	if (flag_overflow)
		status_val |= SECOCEC_STATUS_RX_OVERFLOW_MASK;

	status = smb_wr16(SECOCEC_STATUS, status_val);

	return;

rxerr:
	/* Reset error reg */
	status_val = SECOCEC_STATUS_MSG_RECEIVED_MASK |
		SECOCEC_STATUS_RX_ERROR_MASK;
	if (flag_overflow)
		status_val |= SECOCEC_STATUS_RX_OVERFLOW_MASK;
	smb_wr16(SECOCEC_STATUS, status_val);
}

static const struct cec_adap_ops secocec_cec_adap_ops = {
	/* Low-level callbacks */
	.adap_enable = secocec_adap_enable,
	.adap_log_addr = secocec_adap_log_addr,
	.adap_transmit = secocec_adap_transmit,
};

#ifdef CONFIG_VIDEO_SECO_RC
static int secocec_ir_probe(void *priv)
{
	struct secocec_data *cec = priv;
	struct device *dev = cec->dev;
	int status;
	u16 val;

	/* Prepare the RC input device */
	cec->ir = devm_rc_allocate_device(dev, RC_DRIVER_SCANCODE);
	if (!cec->ir)
		return -ENOMEM;

	snprintf(cec->ir_input_phys, sizeof(cec->ir_input_phys),
		 "%s/input0", dev_name(dev));

	cec->ir->device_name = dev_name(dev);
	cec->ir->input_phys = cec->ir_input_phys;
	cec->ir->input_id.bustype = BUS_HOST;
	cec->ir->input_id.vendor = 0;
	cec->ir->input_id.product = 0;
	cec->ir->input_id.version = 1;
	cec->ir->driver_name = SECOCEC_DEV_NAME;
	cec->ir->allowed_protocols = RC_PROTO_BIT_RC5;
	cec->ir->priv = cec;
	cec->ir->map_name = RC_MAP_HAUPPAUGE;
	cec->ir->timeout = MS_TO_NS(100);

	/* Clear the status register */
	status = smb_rd16(SECOCEC_STATUS_REG_1, &val);
	if (status != 0)
		goto err;

	status = smb_wr16(SECOCEC_STATUS_REG_1, val);
	if (status != 0)
		goto err;

	/* Enable the interrupts */
	status = smb_rd16(SECOCEC_ENABLE_REG_1, &val);
	if (status != 0)
		goto err;

	status = smb_wr16(SECOCEC_ENABLE_REG_1,
			  val | SECOCEC_ENABLE_REG_1_IR);
	if (status != 0)
		goto err;

	dev_dbg(dev, "IR enabled");

	status = devm_rc_register_device(dev, cec->ir);

	if (status) {
		dev_err(dev, "Failed to prepare input device");
		cec->ir = NULL;
		goto err;
	}

	return 0;

err:
	smb_rd16(SECOCEC_ENABLE_REG_1, &val);

	smb_wr16(SECOCEC_ENABLE_REG_1,
		 val & ~SECOCEC_ENABLE_REG_1_IR);

	dev_dbg(dev, "IR disabled");
	return status;
}

static int secocec_ir_rx(struct secocec_data *priv)
{
	struct secocec_data *cec = priv;
	struct device *dev = cec->dev;
	u16 val, status, key, addr, toggle;

	if (!cec->ir)
		return -ENODEV;

	status = smb_rd16(SECOCEC_IR_READ_DATA, &val);
	if (status != 0)
		goto err;

	key = val & SECOCEC_IR_COMMAND_MASK;
	addr = (val & SECOCEC_IR_ADDRESS_MASK) >> SECOCEC_IR_ADDRESS_SHL;
	toggle = (val & SECOCEC_IR_TOGGLE_MASK) >> SECOCEC_IR_TOGGLE_SHL;

	rc_keydown(cec->ir, RC_PROTO_RC5, RC_SCANCODE_RC5(addr, key), toggle);

	dev_dbg(dev, "IR key pressed: 0x%02x addr 0x%02x toggle 0x%02x", key,
		addr, toggle);

	return 0;

err:
	dev_err(dev, "IR Receive message failed (%d)", status);
	return -EIO;
}
#else
static void secocec_ir_rx(struct secocec_data *priv)
{
}

static int secocec_ir_probe(void *priv)
{
	return 0;
}
#endif

static irqreturn_t secocec_irq_handler(int irq, void *priv)
{
	struct secocec_data *cec = priv;
	struct device *dev = cec->dev;
	u16 status_val, cec_val, val = 0;
	int status;

	/*  Read status register */
	status = smb_rd16(SECOCEC_STATUS_REG_1, &status_val);
	if (status)
		goto err;

	if (status_val & SECOCEC_STATUS_REG_1_CEC) {
		/* Read CEC status register */
		status = smb_rd16(SECOCEC_STATUS, &cec_val);
		if (status)
			goto err;

		if (cec_val & SECOCEC_STATUS_MSG_RECEIVED_MASK)
			secocec_rx_done(cec->cec_adap, cec_val);

		if (cec_val & SECOCEC_STATUS_MSG_SENT_MASK)
			secocec_tx_done(cec->cec_adap, cec_val);

		if ((~cec_val & SECOCEC_STATUS_MSG_SENT_MASK) &&
		    (~cec_val & SECOCEC_STATUS_MSG_RECEIVED_MASK))
			dev_warn_once(dev,
				      "Message not received or sent, but interrupt fired");

		val = SECOCEC_STATUS_REG_1_CEC;
	}

	if (status_val & SECOCEC_STATUS_REG_1_IR) {
		val |= SECOCEC_STATUS_REG_1_IR;

		secocec_ir_rx(cec);
	}

	/*  Reset status register */
	status = smb_wr16(SECOCEC_STATUS_REG_1, val);
	if (status)
		goto err;

	return IRQ_HANDLED;

err:
	dev_err_once(dev, "IRQ: R/W SMBus operation failed (%d)", status);

	/*  Reset status register */
	val = SECOCEC_STATUS_REG_1_CEC | SECOCEC_STATUS_REG_1_IR;
	smb_wr16(SECOCEC_STATUS_REG_1, val);

	return IRQ_HANDLED;
}

struct cec_dmi_match {
	const char *sys_vendor;
	const char *product_name;
	const char *devname;
	const char *conn;
};

static const struct cec_dmi_match secocec_dmi_match_table[] = {
	/* UDOO X86 */
	{ "SECO", "UDOO x86", "0000:00:02.0", "Port B" },
};

static struct device *secocec_cec_find_hdmi_dev(struct device *dev,
						const char **conn)
{
	int i;

	for (i = 0 ; i < ARRAY_SIZE(secocec_dmi_match_table) ; ++i) {
		const struct cec_dmi_match *m = &secocec_dmi_match_table[i];

		if (dmi_match(DMI_SYS_VENDOR, m->sys_vendor) &&
		    dmi_match(DMI_PRODUCT_NAME, m->product_name)) {
			struct device *d;

			/* Find the device, bail out if not yet registered */
			d = bus_find_device_by_name(&pci_bus_type, NULL,
						    m->devname);
			if (!d)
				return ERR_PTR(-EPROBE_DEFER);

			put_device(d);
			*conn = m->conn;
			return d;
		}
	}

	return ERR_PTR(-EINVAL);
}

static int secocec_acpi_probe(struct secocec_data *sdev)
{
	struct device *dev = sdev->dev;
	struct gpio_desc *gpio;
	int irq = 0;

	gpio = devm_gpiod_get(dev, NULL, GPIOF_IN);
	if (IS_ERR(gpio)) {
		dev_err(dev, "Cannot request interrupt gpio");
		return PTR_ERR(gpio);
	}

	irq = gpiod_to_irq(gpio);
	if (irq < 0) {
		dev_err(dev, "Cannot find valid irq");
		return -ENODEV;
	}
	dev_dbg(dev, "irq-gpio is bound to IRQ %d", irq);

	sdev->irq = irq;

	return 0;
}

static int secocec_probe(struct platform_device *pdev)
{
	struct secocec_data *secocec;
	struct device *dev = &pdev->dev;
	struct device *hdmi_dev;
	const char *conn = NULL;
	int ret;
	u16 val;

	hdmi_dev = secocec_cec_find_hdmi_dev(&pdev->dev, &conn);
	if (IS_ERR(hdmi_dev))
		return PTR_ERR(hdmi_dev);

	secocec = devm_kzalloc(dev, sizeof(*secocec), GFP_KERNEL);
	if (!secocec)
		return -ENOMEM;

	dev_set_drvdata(dev, secocec);

	/* Request SMBus regions */
	if (!request_muxed_region(BRA_SMB_BASE_ADDR, 7, "CEC00001")) {
		dev_err(dev, "Request memory region failed");
		return -ENXIO;
	}

	secocec->pdev = pdev;
	secocec->dev = dev;

	if (!has_acpi_companion(dev)) {
		dev_dbg(dev, "Cannot find any ACPI companion");
		ret = -ENODEV;
		goto err;
	}

	ret = secocec_acpi_probe(secocec);
	if (ret) {
		dev_err(dev, "Cannot assign gpio to IRQ");
		ret = -ENODEV;
		goto err;
	}

	/* Firmware version check */
	ret = smb_rd16(SECOCEC_VERSION, &val);
	if (ret) {
		dev_err(dev, "Cannot check fw version");
		goto err;
	}
	if (val < SECOCEC_LATEST_FW) {
		dev_err(dev, "CEC Firmware not supported (v.%04x). Use ver > v.%04x",
			val, SECOCEC_LATEST_FW);
		ret = -EINVAL;
		goto err;
	}

	ret = devm_request_threaded_irq(dev,
					secocec->irq,
					NULL,
					secocec_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(&pdev->dev), secocec);

	if (ret) {
		dev_err(dev, "Cannot request IRQ %d", secocec->irq);
		ret = -EIO;
		goto err;
	}

	/* Allocate CEC adapter */
	secocec->cec_adap = cec_allocate_adapter(&secocec_cec_adap_ops,
						 secocec,
						 dev_name(dev),
						 CEC_CAP_DEFAULTS |
						 CEC_CAP_CONNECTOR_INFO,
						 SECOCEC_MAX_ADDRS);

	if (IS_ERR(secocec->cec_adap)) {
		ret = PTR_ERR(secocec->cec_adap);
		goto err;
	}

	secocec->notifier = cec_notifier_cec_adap_register(hdmi_dev, conn,
							   secocec->cec_adap);
	if (!secocec->notifier) {
		ret = -ENOMEM;
		goto err_delete_adapter;
	}

	ret = cec_register_adapter(secocec->cec_adap, dev);
	if (ret)
		goto err_notifier;

	ret = secocec_ir_probe(secocec);
	if (ret)
		goto err_notifier;

	platform_set_drvdata(pdev, secocec);

	dev_dbg(dev, "Device registered");

	return ret;

err_notifier:
	cec_notifier_cec_adap_unregister(secocec->notifier, secocec->cec_adap);
err_delete_adapter:
	cec_delete_adapter(secocec->cec_adap);
err:
	release_region(BRA_SMB_BASE_ADDR, 7);
	dev_err(dev, "%s device probe failed\n", dev_name(dev));

	return ret;
}

static int secocec_remove(struct platform_device *pdev)
{
	struct secocec_data *secocec = platform_get_drvdata(pdev);
	u16 val;

	if (secocec->ir) {
		smb_rd16(SECOCEC_ENABLE_REG_1, &val);

		smb_wr16(SECOCEC_ENABLE_REG_1, val & ~SECOCEC_ENABLE_REG_1_IR);

		dev_dbg(&pdev->dev, "IR disabled");
	}
	cec_notifier_cec_adap_unregister(secocec->notifier, secocec->cec_adap);
	cec_unregister_adapter(secocec->cec_adap);

	release_region(BRA_SMB_BASE_ADDR, 7);

	dev_dbg(&pdev->dev, "CEC device removed");

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int secocec_suspend(struct device *dev)
{
	int status;
	u16 val;

	dev_dbg(dev, "Device going to suspend, disabling");

	/* Clear the status register */
	status = smb_rd16(SECOCEC_STATUS_REG_1, &val);
	if (status)
		goto err;

	status = smb_wr16(SECOCEC_STATUS_REG_1, val);
	if (status)
		goto err;

	/* Disable the interrupts */
	status = smb_rd16(SECOCEC_ENABLE_REG_1, &val);
	if (status)
		goto err;

	status = smb_wr16(SECOCEC_ENABLE_REG_1, val &
			  ~SECOCEC_ENABLE_REG_1_CEC & ~SECOCEC_ENABLE_REG_1_IR);
	if (status)
		goto err;

	return 0;

err:
	dev_err(dev, "Suspend failed (err: %d)", status);
	return status;
}

static int secocec_resume(struct device *dev)
{
	int status;
	u16 val;

	dev_dbg(dev, "Resuming device from suspend");

	/* Clear the status register */
	status = smb_rd16(SECOCEC_STATUS_REG_1, &val);
	if (status)
		goto err;

	status = smb_wr16(SECOCEC_STATUS_REG_1, val);
	if (status)
		goto err;

	/* Enable the interrupts */
	status = smb_rd16(SECOCEC_ENABLE_REG_1, &val);
	if (status)
		goto err;

	status = smb_wr16(SECOCEC_ENABLE_REG_1, val | SECOCEC_ENABLE_REG_1_CEC);
	if (status)
		goto err;

	dev_dbg(dev, "Device resumed from suspend");

	return 0;

err:
	dev_err(dev, "Resume failed (err: %d)", status);
	return status;
}

static SIMPLE_DEV_PM_OPS(secocec_pm_ops, secocec_suspend, secocec_resume);
#define SECOCEC_PM_OPS (&secocec_pm_ops)
#else
#define SECOCEC_PM_OPS NULL
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id secocec_acpi_match[] = {
	{"CEC00001", 0},
	{},
};

MODULE_DEVICE_TABLE(acpi, secocec_acpi_match);
#endif

static struct platform_driver secocec_driver = {
	.driver = {
		   .name = SECOCEC_DEV_NAME,
		   .acpi_match_table = ACPI_PTR(secocec_acpi_match),
		   .pm = SECOCEC_PM_OPS,
	},
	.probe = secocec_probe,
	.remove = secocec_remove,
};

module_platform_driver(secocec_driver);

MODULE_DESCRIPTION("SECO CEC X86 Driver");
MODULE_AUTHOR("Ettore Chimenti <ek5.chimenti@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");
