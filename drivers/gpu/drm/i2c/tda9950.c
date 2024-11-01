// SPDX-License-Identifier: GPL-2.0-only
/*
 *  TDA9950 Consumer Electronics Control driver
 *
 * The NXP TDA9950 implements the HDMI Consumer Electronics Control
 * interface.  The host interface is similar to a mailbox: the data
 * registers starting at REG_CDR0 are written to send a command to the
 * internal CPU, and replies are read from these registers.
 *
 * As the data registers represent a mailbox, they must be accessed
 * as a single I2C transaction.  See the TDA9950 data sheet for details.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_data/tda9950.h>
#include <linux/slab.h>
#include <drm/drm_edid.h>
#include <media/cec.h>
#include <media/cec-notifier.h>

enum {
	REG_CSR = 0x00,
	CSR_BUSY = BIT(7),
	CSR_INT  = BIT(6),
	CSR_ERR  = BIT(5),

	REG_CER = 0x01,

	REG_CVR = 0x02,

	REG_CCR = 0x03,
	CCR_RESET = BIT(7),
	CCR_ON    = BIT(6),

	REG_ACKH = 0x04,
	REG_ACKL = 0x05,

	REG_CCONR = 0x06,
	CCONR_ENABLE_ERROR = BIT(4),
	CCONR_RETRY_MASK = 7,

	REG_CDR0 = 0x07,

	CDR1_REQ = 0x00,
	CDR1_CNF = 0x01,
	CDR1_IND = 0x81,
	CDR1_ERR = 0x82,
	CDR1_IER = 0x83,

	CDR2_CNF_SUCCESS    = 0x00,
	CDR2_CNF_OFF_STATE  = 0x80,
	CDR2_CNF_BAD_REQ    = 0x81,
	CDR2_CNF_CEC_ACCESS = 0x82,
	CDR2_CNF_ARB_ERROR  = 0x83,
	CDR2_CNF_BAD_TIMING = 0x84,
	CDR2_CNF_NACK_ADDR  = 0x85,
	CDR2_CNF_NACK_DATA  = 0x86,
};

struct tda9950_priv {
	struct i2c_client *client;
	struct device *hdmi;
	struct cec_adapter *adap;
	struct tda9950_glue *glue;
	u16 addresses;
	struct cec_msg rx_msg;
	struct cec_notifier *notify;
	bool open;
};

static int tda9950_write_range(struct i2c_client *client, u8 addr, u8 *p, int cnt)
{
	struct i2c_msg msg;
	u8 buf[CEC_MAX_MSG_SIZE + 3];
	int ret;

	if (WARN_ON(cnt > sizeof(buf) - 1))
		return -EINVAL;

	buf[0] = addr;
	memcpy(buf + 1, p, cnt);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = cnt + 1;
	msg.buf = buf;

	dev_dbg(&client->dev, "wr 0x%02x: %*ph\n", addr, cnt, p);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		dev_err(&client->dev, "Error %d writing to cec:0x%x\n", ret, addr);
	return ret < 0 ? ret : 0;
}

static void tda9950_write(struct i2c_client *client, u8 addr, u8 val)
{
	tda9950_write_range(client, addr, &val, 1);
}

static int tda9950_read_range(struct i2c_client *client, u8 addr, u8 *p, int cnt)
{
	struct i2c_msg msg[2];
	int ret;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &addr;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = cnt;
	msg[1].buf = p;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		dev_err(&client->dev, "Error %d reading from cec:0x%x\n", ret, addr);

	dev_dbg(&client->dev, "rd 0x%02x: %*ph\n", addr, cnt, p);

	return ret;
}

static u8 tda9950_read(struct i2c_client *client, u8 addr)
{
	int ret;
	u8 val;

	ret = tda9950_read_range(client, addr, &val, 1);
	if (ret < 0)
		val = 0;

	return val;
}

static irqreturn_t tda9950_irq(int irq, void *data)
{
	struct tda9950_priv *priv = data;
	unsigned int tx_status;
	u8 csr, cconr, buf[19];
	u8 arb_lost_cnt, nack_cnt, err_cnt;

	if (!priv->open)
		return IRQ_NONE;

	csr = tda9950_read(priv->client, REG_CSR);
	if (!(csr & CSR_INT))
		return IRQ_NONE;

	cconr = tda9950_read(priv->client, REG_CCONR) & CCONR_RETRY_MASK;

	tda9950_read_range(priv->client, REG_CDR0, buf, sizeof(buf));

	/*
	 * This should never happen: the data sheet says that there will
	 * always be a valid message if the interrupt line is asserted.
	 */
	if (buf[0] == 0) {
		dev_warn(&priv->client->dev, "interrupt pending, but no message?\n");
		return IRQ_NONE;
	}

	switch (buf[1]) {
	case CDR1_CNF: /* transmit result */
		arb_lost_cnt = nack_cnt = err_cnt = 0;
		switch (buf[2]) {
		case CDR2_CNF_SUCCESS:
			tx_status = CEC_TX_STATUS_OK;
			break;

		case CDR2_CNF_ARB_ERROR:
			tx_status = CEC_TX_STATUS_ARB_LOST;
			arb_lost_cnt = cconr;
			break;

		case CDR2_CNF_NACK_ADDR:
			tx_status = CEC_TX_STATUS_NACK;
			nack_cnt = cconr;
			break;

		default: /* some other error, refer to TDA9950 docs */
			dev_err(&priv->client->dev, "CNF reply error 0x%02x\n",
				buf[2]);
			tx_status = CEC_TX_STATUS_ERROR;
			err_cnt = cconr;
			break;
		}
		/* TDA9950 executes all retries for us */
		if (tx_status != CEC_TX_STATUS_OK)
			tx_status |= CEC_TX_STATUS_MAX_RETRIES;
		cec_transmit_done(priv->adap, tx_status, arb_lost_cnt,
				  nack_cnt, 0, err_cnt);
		break;

	case CDR1_IND:
		priv->rx_msg.len = buf[0] - 2;
		if (priv->rx_msg.len > CEC_MAX_MSG_SIZE)
			priv->rx_msg.len = CEC_MAX_MSG_SIZE;

		memcpy(priv->rx_msg.msg, buf + 2, priv->rx_msg.len);
		cec_received_msg(priv->adap, &priv->rx_msg);
		break;

	default: /* unknown */
		dev_err(&priv->client->dev, "unknown service id 0x%02x\n",
			buf[1]);
		break;
	}

	return IRQ_HANDLED;
}

static int tda9950_cec_transmit(struct cec_adapter *adap, u8 attempts,
				u32 signal_free_time, struct cec_msg *msg)
{
	struct tda9950_priv *priv = adap->priv;
	u8 buf[CEC_MAX_MSG_SIZE + 2];

	buf[0] = 2 + msg->len;
	buf[1] = CDR1_REQ;
	memcpy(buf + 2, msg->msg, msg->len);

	if (attempts > 5)
		attempts = 5;

	tda9950_write(priv->client, REG_CCONR, attempts);

	return tda9950_write_range(priv->client, REG_CDR0, buf, 2 + msg->len);
}

static int tda9950_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct tda9950_priv *priv = adap->priv;
	u16 addresses;
	u8 buf[2];

	if (addr == CEC_LOG_ADDR_INVALID)
		addresses = priv->addresses = 0;
	else
		addresses = priv->addresses |= BIT(addr);

	/* TDA9950 doesn't want address 15 set */
	addresses &= 0x7fff;
	buf[0] = addresses >> 8;
	buf[1] = addresses;

	return tda9950_write_range(priv->client, REG_ACKH, buf, 2);
}

/*
 * When operating as part of the TDA998x, we need additional handling
 * to initialise and shut down the TDA9950 part of the device.  These
 * two hooks are provided to allow the TDA998x code to perform those
 * activities.
 */
static int tda9950_glue_open(struct tda9950_priv *priv)
{
	int ret = 0;

	if (priv->glue && priv->glue->open)
		ret = priv->glue->open(priv->glue->data);

	priv->open = true;

	return ret;
}

static void tda9950_glue_release(struct tda9950_priv *priv)
{
	priv->open = false;

	if (priv->glue && priv->glue->release)
		priv->glue->release(priv->glue->data);
}

static int tda9950_open(struct tda9950_priv *priv)
{
	struct i2c_client *client = priv->client;
	int ret;

	ret = tda9950_glue_open(priv);
	if (ret)
		return ret;

	/* Reset the TDA9950, and wait 250ms for it to recover */
	tda9950_write(client, REG_CCR, CCR_RESET);
	msleep(250);

	tda9950_cec_adap_log_addr(priv->adap, CEC_LOG_ADDR_INVALID);

	/* Start the command processor */
	tda9950_write(client, REG_CCR, CCR_ON);

	return 0;
}

static void tda9950_release(struct tda9950_priv *priv)
{
	struct i2c_client *client = priv->client;
	int timeout = 50;
	u8 csr;

	/* Stop the command processor */
	tda9950_write(client, REG_CCR, 0);

	/* Wait up to .5s for it to signal non-busy */
	do {
		csr = tda9950_read(client, REG_CSR);
		if (!(csr & CSR_BUSY) || !--timeout)
			break;
		msleep(10);
	} while (1);

	/* Warn the user that their IRQ may die if it's shared. */
	if (csr & CSR_BUSY)
		dev_warn(&client->dev, "command processor failed to stop, irq%d may die (csr=0x%02x)\n",
			 client->irq, csr);

	tda9950_glue_release(priv);
}

static int tda9950_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct tda9950_priv *priv = adap->priv;

	if (!enable) {
		tda9950_release(priv);
		return 0;
	} else {
		return tda9950_open(priv);
	}
}

static const struct cec_adap_ops tda9950_cec_ops = {
	.adap_enable = tda9950_cec_adap_enable,
	.adap_log_addr = tda9950_cec_adap_log_addr,
	.adap_transmit = tda9950_cec_transmit,
};

/*
 * When operating as part of the TDA998x, we need to claim additional
 * resources.  These two hooks permit the management of those resources.
 */
static void tda9950_devm_glue_exit(void *data)
{
	struct tda9950_glue *glue = data;

	if (glue && glue->exit)
		glue->exit(glue->data);
}

static int tda9950_devm_glue_init(struct device *dev, struct tda9950_glue *glue)
{
	int ret;

	if (glue && glue->init) {
		ret = glue->init(glue->data);
		if (ret)
			return ret;
	}

	ret = devm_add_action(dev, tda9950_devm_glue_exit, glue);
	if (ret)
		tda9950_devm_glue_exit(glue);

	return ret;
}

static void tda9950_cec_del(void *data)
{
	struct tda9950_priv *priv = data;

	cec_delete_adapter(priv->adap);
}

static int tda9950_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct tda9950_glue *glue = client->dev.platform_data;
	struct device *dev = &client->dev;
	struct tda9950_priv *priv;
	unsigned long irqflags;
	int ret;
	u8 cvr;

	/*
	 * We must have I2C functionality: our multi-byte accesses
	 * must be performed as a single contiguous transaction.
	 */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
			"adapter does not support I2C functionality\n");
		return -ENXIO;
	}

	/* We must have an interrupt to be functional. */
	if (client->irq <= 0) {
		dev_err(&client->dev, "driver requires an interrupt\n");
		return -ENXIO;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->glue = glue;

	i2c_set_clientdata(client, priv);

	/*
	 * If we're part of a TDA998x, we want the class devices to be
	 * associated with the HDMI Tx so we have a tight relationship
	 * between the HDMI interface and the CEC interface.
	 */
	priv->hdmi = dev;
	if (glue && glue->parent)
		priv->hdmi = glue->parent;

	priv->adap = cec_allocate_adapter(&tda9950_cec_ops, priv, "tda9950",
					  CEC_CAP_DEFAULTS |
					  CEC_CAP_CONNECTOR_INFO,
					  CEC_MAX_LOG_ADDRS);
	if (IS_ERR(priv->adap))
		return PTR_ERR(priv->adap);

	ret = devm_add_action(dev, tda9950_cec_del, priv);
	if (ret) {
		cec_delete_adapter(priv->adap);
		return ret;
	}

	ret = tda9950_devm_glue_init(dev, glue);
	if (ret)
		return ret;

	ret = tda9950_glue_open(priv);
	if (ret)
		return ret;

	cvr = tda9950_read(client, REG_CVR);

	dev_info(&client->dev,
		 "TDA9950 CEC interface, hardware version %u.%u\n",
		 cvr >> 4, cvr & 15);

	tda9950_glue_release(priv);

	irqflags = IRQF_TRIGGER_FALLING;
	if (glue)
		irqflags = glue->irq_flags;

	ret = devm_request_threaded_irq(dev, client->irq, NULL, tda9950_irq,
					irqflags | IRQF_SHARED | IRQF_ONESHOT,
					dev_name(&client->dev), priv);
	if (ret < 0)
		return ret;

	priv->notify = cec_notifier_cec_adap_register(priv->hdmi, NULL,
						      priv->adap);
	if (!priv->notify)
		return -ENOMEM;

	ret = cec_register_adapter(priv->adap, priv->hdmi);
	if (ret < 0) {
		cec_notifier_cec_adap_unregister(priv->notify, priv->adap);
		return ret;
	}

	/*
	 * CEC documentation says we must not call cec_delete_adapter
	 * after a successful call to cec_register_adapter().
	 */
	devm_remove_action(dev, tda9950_cec_del, priv);

	return 0;
}

static void tda9950_remove(struct i2c_client *client)
{
	struct tda9950_priv *priv = i2c_get_clientdata(client);

	cec_notifier_cec_adap_unregister(priv->notify, priv->adap);
	cec_unregister_adapter(priv->adap);
}

static struct i2c_device_id tda9950_ids[] = {
	{ "tda9950", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tda9950_ids);

static struct i2c_driver tda9950_driver = {
	.probe = tda9950_probe,
	.remove = tda9950_remove,
	.driver = {
		.name = "tda9950",
	},
	.id_table = tda9950_ids,
};

module_i2c_driver(tda9950_driver);

MODULE_AUTHOR("Russell King <rmk+kernel@armlinux.org.uk>");
MODULE_DESCRIPTION("TDA9950/TDA998x Consumer Electronics Control Driver");
MODULE_LICENSE("GPL v2");
