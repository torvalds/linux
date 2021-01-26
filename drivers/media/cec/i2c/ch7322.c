// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Chrontel CH7322 CEC Controller
 *
 * Copyright 2020 Google LLC.
 */

/*
 * Notes
 *
 * - This device powers on in Auto Mode which has limited functionality. This
 *   driver disables Auto Mode when it attaches.
 *
 */

#include <linux/cec.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/regmap.h>
#include <media/cec.h>
#include <media/cec-notifier.h>

#define CH7322_WRITE		0x00
#define CH7322_WRITE_MSENT		0x80
#define CH7322_WRITE_BOK		0x40
#define CH7322_WRITE_NMASK		0x0f

/* Write buffer is 0x01-0x10 */
#define CH7322_WRBUF		0x01
#define CH7322_WRBUF_LEN	0x10

#define CH7322_READ		0x40
#define CH7322_READ_NRDT		0x80
#define CH7322_READ_MSENT		0x20
#define CH7322_READ_NMASK		0x0f

/* Read buffer is 0x41-0x50 */
#define CH7322_RDBUF		0x41
#define CH7322_RDBUF_LEN	0x10

#define CH7322_MODE		0x11
#define CH7322_MODE_AUTO		0x78
#define CH7322_MODE_SW			0xb5

#define CH7322_RESET		0x12
#define CH7322_RESET_RST		0x00

#define CH7322_POWER		0x13
#define CH7322_POWER_FPD		0x04

#define CH7322_CFG0		0x17
#define CH7322_CFG0_EOBEN		0x40
#define CH7322_CFG0_PEOB		0x20
#define CH7322_CFG0_CLRSPP		0x10
#define CH7322_CFG0_FLOW		0x08

#define CH7322_CFG1		0x1a
#define CH7322_CFG1_STDBYO		0x04
#define CH7322_CFG1_HPBP		0x02
#define CH7322_CFG1_PIO			0x01

#define CH7322_INTCTL		0x1b
#define CH7322_INTCTL_INTPB		0x80
#define CH7322_INTCTL_STDBY		0x40
#define CH7322_INTCTL_HPDFALL		0x20
#define CH7322_INTCTL_HPDRISE		0x10
#define CH7322_INTCTL_RXMSG		0x08
#define CH7322_INTCTL_TXMSG		0x04
#define CH7322_INTCTL_NEWPHA		0x02
#define CH7322_INTCTL_ERROR		0x01

#define CH7322_DVCLKFNH	0x1d
#define CH7322_DVCLKFNL	0x1e

#define CH7322_CTL		0x31
#define CH7322_CTL_FSTDBY		0x80
#define CH7322_CTL_PLSEN		0x40
#define CH7322_CTL_PLSPB		0x20
#define CH7322_CTL_SPADL		0x10
#define CH7322_CTL_HINIT		0x08
#define CH7322_CTL_WPHYA		0x04
#define CH7322_CTL_H1T			0x02
#define CH7322_CTL_S1T			0x01

#define CH7322_PAWH		0x32
#define CH7322_PAWL		0x33

#define CH7322_ADDLW		0x34
#define CH7322_ADDLW_MASK	0xf0

#define CH7322_ADDLR		0x3d
#define CH7322_ADDLR_HPD		0x80
#define CH7322_ADDLR_MASK		0x0f

#define CH7322_INTDATA		0x3e
#define CH7322_INTDATA_MODE		0x80
#define CH7322_INTDATA_STDBY		0x40
#define CH7322_INTDATA_HPDFALL		0x20
#define CH7322_INTDATA_HPDRISE		0x10
#define CH7322_INTDATA_RXMSG		0x08
#define CH7322_INTDATA_TXMSG		0x04
#define CH7322_INTDATA_NEWPHA		0x02
#define CH7322_INTDATA_ERROR		0x01

#define CH7322_EVENT		0x3f
#define CH7322_EVENT_TXERR		0x80
#define CH7322_EVENT_HRST		0x40
#define CH7322_EVENT_HFST		0x20
#define CH7322_EVENT_PHACHG		0x10
#define CH7322_EVENT_ACTST		0x08
#define CH7322_EVENT_PHARDY		0x04
#define CH7322_EVENT_BSOK		0x02
#define CH7322_EVENT_ERRADCF		0x01

#define CH7322_DID		0x51
#define CH7322_DID_CH7322		0x5b
#define CH7322_DID_CH7323		0x5f

#define CH7322_REVISIONID	0x52

#define CH7322_PARH		0x53
#define CH7322_PARL		0x54

#define CH7322_IOCFG2		0x75
#define CH7322_IOCFG_CIO		0x80
#define CH7322_IOCFG_IOCFGMASK		0x78
#define CH7322_IOCFG_AUDIO		0x04
#define CH7322_IOCFG_SPAMST		0x02
#define CH7322_IOCFG_SPAMSP		0x01

#define CH7322_CTL3		0x7b
#define CH7322_CTL3_SWENA		0x80
#define CH7322_CTL3_FC_INIT		0x40
#define CH7322_CTL3_SML_FL		0x20
#define CH7322_CTL3_SM_RDST		0x10
#define CH7322_CTL3_SPP_CIAH		0x08
#define CH7322_CTL3_SPP_CIAL		0x04
#define CH7322_CTL3_SPP_ACTH		0x02
#define CH7322_CTL3_SPP_ACTL		0x01

/* BOK status means NACK */
#define CH7322_TX_FLAG_NACK	BIT(0)
/* Device will retry automatically */
#define CH7322_TX_FLAG_RETRY	BIT(1)

struct ch7322 {
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct cec_adapter *cec;
	struct mutex mutex;	/* device access mutex */
	u8 tx_flags;
};

static const struct regmap_config ch7322_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x7f,
	.disable_locking = true,
};

static int ch7322_send_message(struct ch7322 *ch7322, const struct cec_msg *msg)
{
	unsigned int val;
	unsigned int len = msg->len;
	int ret;
	int i;

	WARN_ON(!mutex_is_locked(&ch7322->mutex));

	if (len > CH7322_WRBUF_LEN || len < 1)
		return -EINVAL;

	ret = regmap_read(ch7322->regmap, CH7322_WRITE, &val);
	if (ret)
		return ret;

	/* Buffer not ready */
	if (!(val & CH7322_WRITE_MSENT))
		return -EBUSY;

	if (cec_msg_opcode(msg) == -1 &&
	    cec_msg_initiator(msg) == cec_msg_destination(msg)) {
		ch7322->tx_flags = CH7322_TX_FLAG_NACK | CH7322_TX_FLAG_RETRY;
	} else if (cec_msg_is_broadcast(msg)) {
		ch7322->tx_flags = CH7322_TX_FLAG_NACK;
	} else {
		ch7322->tx_flags = CH7322_TX_FLAG_RETRY;
	}

	ret = regmap_write(ch7322->regmap, CH7322_WRITE, len - 1);
	if (ret)
		return ret;

	for (i = 0; i < len; i++) {
		ret = regmap_write(ch7322->regmap,
				   CH7322_WRBUF + i, msg->msg[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int ch7322_receive_message(struct ch7322 *ch7322, struct cec_msg *msg)
{
	unsigned int val;
	int ret = 0;
	int i;

	WARN_ON(!mutex_is_locked(&ch7322->mutex));

	ret = regmap_read(ch7322->regmap, CH7322_READ, &val);
	if (ret)
		return ret;

	/* Message not ready */
	if (!(val & CH7322_READ_NRDT))
		return -EIO;

	msg->len = (val & CH7322_READ_NMASK) + 1;

	/* Read entire RDBUF to clear state */
	for (i = 0; i < CH7322_RDBUF_LEN; i++) {
		ret = regmap_read(ch7322->regmap, CH7322_RDBUF + i, &val);
		if (ret)
			return ret;
		msg->msg[i] = (u8)val;
	}

	return 0;
}

static void ch7322_tx_done(struct ch7322 *ch7322)
{
	int ret;
	unsigned int val;
	u8 status, flags;

	mutex_lock(&ch7322->mutex);
	ret = regmap_read(ch7322->regmap, CH7322_WRITE, &val);
	flags = ch7322->tx_flags;
	mutex_unlock(&ch7322->mutex);

	/*
	 * The device returns a one-bit OK status which usually means ACK but
	 * actually means NACK when sending a logical address query or a
	 * broadcast.
	 */
	if (ret)
		status = CEC_TX_STATUS_ERROR;
	else if ((val & CH7322_WRITE_BOK) && (flags & CH7322_TX_FLAG_NACK))
		status = CEC_TX_STATUS_NACK;
	else if (val & CH7322_WRITE_BOK)
		status = CEC_TX_STATUS_OK;
	else if (flags & CH7322_TX_FLAG_NACK)
		status = CEC_TX_STATUS_OK;
	else
		status = CEC_TX_STATUS_NACK;

	if (status == CEC_TX_STATUS_NACK && (flags & CH7322_TX_FLAG_RETRY))
		status |= CEC_TX_STATUS_MAX_RETRIES;

	cec_transmit_attempt_done(ch7322->cec, status);
}

static void ch7322_rx_done(struct ch7322 *ch7322)
{
	struct cec_msg msg;
	int ret;

	mutex_lock(&ch7322->mutex);
	ret = ch7322_receive_message(ch7322, &msg);
	mutex_unlock(&ch7322->mutex);

	if (ret)
		dev_err(&ch7322->i2c->dev, "cec receive error: %d\n", ret);
	else
		cec_received_msg(ch7322->cec, &msg);
}

/*
 * This device can either monitor the DDC lines to obtain the physical address
 * or it can allow the host to program it. This driver lets the device obtain
 * it.
 */
static void ch7322_phys_addr(struct ch7322 *ch7322)
{
	unsigned int pah, pal;
	int ret = 0;

	mutex_lock(&ch7322->mutex);
	ret |= regmap_read(ch7322->regmap, CH7322_PARH, &pah);
	ret |= regmap_read(ch7322->regmap, CH7322_PARL, &pal);
	mutex_unlock(&ch7322->mutex);

	if (ret)
		dev_err(&ch7322->i2c->dev, "phys addr error\n");
	else
		cec_s_phys_addr(ch7322->cec, pal | (pah << 8), false);
}

static irqreturn_t ch7322_irq(int irq, void *dev)
{
	struct ch7322 *ch7322 = dev;
	unsigned int data = 0;

	mutex_lock(&ch7322->mutex);
	regmap_read(ch7322->regmap, CH7322_INTDATA, &data);
	regmap_write(ch7322->regmap, CH7322_INTDATA, data);
	mutex_unlock(&ch7322->mutex);

	if (data & CH7322_INTDATA_HPDFALL)
		cec_phys_addr_invalidate(ch7322->cec);

	if (data & CH7322_INTDATA_TXMSG)
		ch7322_tx_done(ch7322);

	if (data & CH7322_INTDATA_RXMSG)
		ch7322_rx_done(ch7322);

	if (data & CH7322_INTDATA_NEWPHA)
		ch7322_phys_addr(ch7322);

	if (data & CH7322_INTDATA_ERROR)
		dev_dbg(&ch7322->i2c->dev, "unknown error\n");

	return IRQ_HANDLED;
}

/* This device is always enabled */
static int ch7322_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	return 0;
}

static int ch7322_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct ch7322 *ch7322 = cec_get_drvdata(adap);
	int ret;

	mutex_lock(&ch7322->mutex);
	ret = regmap_update_bits(ch7322->regmap, CH7322_ADDLW,
				 CH7322_ADDLW_MASK, log_addr << 4);
	mutex_unlock(&ch7322->mutex);

	return ret;
}

static int ch7322_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				    u32 signal_free_time, struct cec_msg *msg)
{
	struct ch7322 *ch7322 = cec_get_drvdata(adap);
	int ret;

	mutex_lock(&ch7322->mutex);
	ret = ch7322_send_message(ch7322, msg);
	mutex_unlock(&ch7322->mutex);

	return ret;
}

static const struct cec_adap_ops ch7322_cec_adap_ops = {
	.adap_enable = ch7322_cec_adap_enable,
	.adap_log_addr = ch7322_cec_adap_log_addr,
	.adap_transmit = ch7322_cec_adap_transmit,
};

#if IS_ENABLED(CONFIG_PCI) && IS_ENABLED(CONFIG_DMI)

struct ch7322_conn_match {
	const char *dev_name;
	const char *pci_name;
	const char *port_name;
};

static struct ch7322_conn_match google_endeavour[] = {
	{ "i2c-PRP0001:00", "0000:00:02.0", "Port B" },
	{ "i2c-PRP0001:01", "0000:00:02.0", "Port C" },
	{ },
};

static const struct dmi_system_id ch7322_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_NAME, "Endeavour"),
		},
		.driver_data = google_endeavour,
	},
	{ },
};

/* Make a best-effort attempt to locate a matching HDMI port */
static int ch7322_get_port(struct i2c_client *client,
			   struct device **dev,
			   const char **port)
{
	const struct dmi_system_id *system;
	const struct ch7322_conn_match *conn;

	*dev = NULL;
	*port = NULL;

	system = dmi_first_match(ch7322_dmi_table);
	if (!system)
		return 0;

	for (conn = system->driver_data; conn->dev_name; conn++) {
		if (!strcmp(dev_name(&client->dev), conn->dev_name)) {
			struct device *d;

			d = bus_find_device_by_name(&pci_bus_type, NULL,
						    conn->pci_name);
			if (!d)
				return -EPROBE_DEFER;

			put_device(d);

			*dev = d;
			*port = conn->port_name;

			return 0;
		}
	}

	return 0;
}

#else

static int ch7322_get_port(struct i2c_client *client,
			   struct device **dev,
			   const char **port)
{
	*dev = NULL;
	*port = NULL;

	return 0;
}

#endif

static int ch7322_probe(struct i2c_client *client)
{
	struct device *hdmi_dev;
	const char *port_name;
	struct ch7322 *ch7322;
	struct cec_notifier *notifier = NULL;
	u32 caps = CEC_CAP_DEFAULTS;
	int ret;
	unsigned int val;

	ret = ch7322_get_port(client, &hdmi_dev, &port_name);
	if (ret)
		return ret;

	if (hdmi_dev)
		caps |= CEC_CAP_CONNECTOR_INFO;

	ch7322 = devm_kzalloc(&client->dev, sizeof(*ch7322), GFP_KERNEL);
	if (!ch7322)
		return -ENOMEM;

	ch7322->regmap = devm_regmap_init_i2c(client, &ch7322_regmap);
	if (IS_ERR(ch7322->regmap))
		return PTR_ERR(ch7322->regmap);

	ret = regmap_read(ch7322->regmap, CH7322_DID, &val);
	if (ret)
		return ret;

	if (val != CH7322_DID_CH7322)
		return -EOPNOTSUPP;

	mutex_init(&ch7322->mutex);
	ch7322->i2c = client;
	ch7322->tx_flags = 0;

	i2c_set_clientdata(client, ch7322);

	/* Disable auto mode */
	ret = regmap_write(ch7322->regmap, CH7322_MODE, CH7322_MODE_SW);
	if (ret)
		goto err_mutex;

	/* Enable logical address register */
	ret = regmap_update_bits(ch7322->regmap, CH7322_CTL,
				 CH7322_CTL_SPADL, CH7322_CTL_SPADL);
	if (ret)
		goto err_mutex;

	ch7322->cec = cec_allocate_adapter(&ch7322_cec_adap_ops, ch7322,
					   dev_name(&client->dev),
					   caps, 1);

	if (IS_ERR(ch7322->cec)) {
		ret = PTR_ERR(ch7322->cec);
		goto err_mutex;
	}

	ch7322->cec->adap_controls_phys_addr = true;

	if (hdmi_dev) {
		notifier = cec_notifier_cec_adap_register(hdmi_dev,
							  port_name,
							  ch7322->cec);
		if (!notifier) {
			ret = -ENOMEM;
			goto err_cec;
		}
	}

	/* Configure, mask, and clear interrupt */
	ret = regmap_write(ch7322->regmap, CH7322_CFG1, 0);
	if (ret)
		goto err_notifier;
	ret = regmap_write(ch7322->regmap, CH7322_INTCTL, CH7322_INTCTL_INTPB);
	if (ret)
		goto err_notifier;
	ret = regmap_write(ch7322->regmap, CH7322_INTDATA, 0xff);
	if (ret)
		goto err_notifier;

	/* If HPD is up read physical address */
	ret = regmap_read(ch7322->regmap, CH7322_ADDLR, &val);
	if (ret)
		goto err_notifier;
	if (val & CH7322_ADDLR_HPD)
		ch7322_phys_addr(ch7322);

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					ch7322_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					client->name, ch7322);
	if (ret)
		goto err_notifier;

	/* Unmask interrupt */
	mutex_lock(&ch7322->mutex);
	ret = regmap_write(ch7322->regmap, CH7322_INTCTL, 0xff);
	mutex_unlock(&ch7322->mutex);

	if (ret)
		goto err_notifier;

	ret = cec_register_adapter(ch7322->cec, &client->dev);
	if (ret)
		goto err_notifier;

	dev_info(&client->dev, "device registered\n");

	return 0;

err_notifier:
	if (notifier)
		cec_notifier_cec_adap_unregister(notifier, ch7322->cec);
err_cec:
	cec_delete_adapter(ch7322->cec);
err_mutex:
	mutex_destroy(&ch7322->mutex);
	return ret;
}

static int ch7322_remove(struct i2c_client *client)
{
	struct ch7322 *ch7322 = i2c_get_clientdata(client);

	/* Mask interrupt */
	mutex_lock(&ch7322->mutex);
	regmap_write(ch7322->regmap, CH7322_INTCTL, CH7322_INTCTL_INTPB);
	mutex_unlock(&ch7322->mutex);

	cec_unregister_adapter(ch7322->cec);
	mutex_destroy(&ch7322->mutex);

	dev_info(&client->dev, "device unregistered\n");

	return 0;
}

static const struct of_device_id ch7322_of_match[] = {
	{ .compatible = "chrontel,ch7322", },
	{},
};
MODULE_DEVICE_TABLE(of, ch7322_of_match);

static struct i2c_driver ch7322_i2c_driver = {
	.driver = {
		.name = "ch7322",
		.of_match_table = of_match_ptr(ch7322_of_match),
	},
	.probe_new	= ch7322_probe,
	.remove		= ch7322_remove,
};

module_i2c_driver(ch7322_i2c_driver);

MODULE_DESCRIPTION("Chrontel CH7322 CEC Controller Driver");
MODULE_AUTHOR("Jeff Chase <jnchase@google.com>");
MODULE_LICENSE("GPL");
