// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018, Richtek Technology Corporation
 *
 * Richtek RT1711H Type-C Chip Driver
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/usb/tcpci.h>
#include <linux/usb/tcpm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define RT1711H_VID		0x29CF
#define RT1711H_PID		0x1711
#define RT1711H_DID		0x2171
#define RT1715_DID		0x2173

#define RT1711H_PHYCTRL1	0x80
#define RT1711H_PHYCTRL2	0x81

#define RT1711H_RTCTRL4		0x93
/* rx threshold of rd/rp: 1b0 for level 0.4V/0.7V, 1b1 for 0.35V/0.75V */
#define RT1711H_BMCIO_RXDZSEL	BIT(0)

#define RT1711H_RTCTRL8		0x9B
/* Autoidle timeout = (tout * 2 + 1) * 6.4ms */
#define RT1711H_RTCTRL8_SET(ck300, ship_off, auto_idle, tout) \
			    (((ck300) << 7) | ((ship_off) << 5) | \
			    ((auto_idle) << 3) | ((tout) & 0x07))
#define RT1711H_AUTOIDLEEN	BIT(3)
#define RT1711H_ENEXTMSG	BIT(4)

#define RT1711H_RTCTRL11	0x9E

/* I2C timeout = (tout + 1) * 12.5ms */
#define RT1711H_RTCTRL11_SET(en, tout) \
			     (((en) << 7) | ((tout) & 0x0F))

#define RT1711H_RTCTRL13	0xA0
#define RT1711H_RTCTRL14	0xA1
#define RT1711H_RTCTRL15	0xA2
#define RT1711H_RTCTRL16	0xA3

#define RT1711H_RTCTRL18	0xAF
/* 1b0 as fixed rx threshold of rd/rp 0.55V, 1b1 depends on RTCRTL4[0] */
#define BMCIO_RXDZEN	BIT(0)

struct rt1711h_chip_info {
	u32 rxdz_sel;
	u16 did;
	bool enable_pd30_extended_message;
};

struct rt1711h_chip {
	struct tcpci_data data;
	struct tcpci *tcpci;
	struct device *dev;
	struct regulator *vbus;
	const struct rt1711h_chip_info *info;
	bool src_en;
};

static int rt1711h_read16(struct rt1711h_chip *chip, unsigned int reg, u16 *val)
{
	return regmap_raw_read(chip->data.regmap, reg, val, sizeof(u16));
}

static int rt1711h_write16(struct rt1711h_chip *chip, unsigned int reg, u16 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u16));
}

static int rt1711h_read8(struct rt1711h_chip *chip, unsigned int reg, u8 *val)
{
	return regmap_raw_read(chip->data.regmap, reg, val, sizeof(u8));
}

static int rt1711h_write8(struct rt1711h_chip *chip, unsigned int reg, u8 val)
{
	return regmap_raw_write(chip->data.regmap, reg, &val, sizeof(u8));
}

static const struct regmap_config rt1711h_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xFF, /* 0x80 .. 0xFF are vendor defined */
};

static struct rt1711h_chip *tdata_to_rt1711h(struct tcpci_data *tdata)
{
	return container_of(tdata, struct rt1711h_chip, data);
}

static int rt1711h_init(struct tcpci *tcpci, struct tcpci_data *tdata)
{
	struct rt1711h_chip *chip = tdata_to_rt1711h(tdata);
	struct regmap *regmap = chip->data.regmap;
	int ret;

	/* CK 300K from 320K, shipping off, auto_idle enable, tout = 32ms */
	ret = rt1711h_write8(chip, RT1711H_RTCTRL8,
			     RT1711H_RTCTRL8_SET(0, 1, 1, 2));
	if (ret < 0)
		return ret;

	/* Enable PD30 extended message for RT1715 */
	if (chip->info->enable_pd30_extended_message) {
		ret = regmap_update_bits(regmap, RT1711H_RTCTRL8,
					 RT1711H_ENEXTMSG, RT1711H_ENEXTMSG);
		if (ret < 0)
			return ret;
	}

	/* I2C reset : (val + 1) * 12.5ms */
	ret = rt1711h_write8(chip, RT1711H_RTCTRL11,
			     RT1711H_RTCTRL11_SET(1, 0x0F));
	if (ret < 0)
		return ret;

	/* tTCPCfilter : (26.7 * val) us */
	ret = rt1711h_write8(chip, RT1711H_RTCTRL14, 0x0F);
	if (ret < 0)
		return ret;

	/*  tDRP : (51.2 + 6.4 * val) ms */
	ret = rt1711h_write8(chip, RT1711H_RTCTRL15, 0x04);
	if (ret < 0)
		return ret;

	/* dcSRC.DRP : 33% */
	ret = rt1711h_write16(chip, RT1711H_RTCTRL16, 330);
	if (ret < 0)
		return ret;

	/* Enable phy discard retry, retry count 7, rx filter deglitch 100 us */
	ret = rt1711h_write8(chip, RT1711H_PHYCTRL1, 0xF1);
	if (ret < 0)
		return ret;

	/* Decrease wait time of BMC-encoded 1 bit from 2.67us to 2.55us */
	/* wait time : (val * .4167) us */
	return rt1711h_write8(chip, RT1711H_PHYCTRL2, 62);
}

static int rt1711h_set_vbus(struct tcpci *tcpci, struct tcpci_data *tdata,
			    bool src, bool snk)
{
	struct rt1711h_chip *chip = tdata_to_rt1711h(tdata);
	int ret;

	if (chip->src_en == src)
		return 0;

	if (src)
		ret = regulator_enable(chip->vbus);
	else
		ret = regulator_disable(chip->vbus);

	if (!ret)
		chip->src_en = src;
	return ret;
}

static int rt1711h_set_vconn(struct tcpci *tcpci, struct tcpci_data *tdata,
			     bool enable)
{
	struct rt1711h_chip *chip = tdata_to_rt1711h(tdata);

	return regmap_update_bits(chip->data.regmap, RT1711H_RTCTRL8,
				  RT1711H_AUTOIDLEEN, enable ? 0 : RT1711H_AUTOIDLEEN);
}

/*
 * Selects the CC PHY noise filter voltage level according to the remote current
 * CC voltage level.
 *
 * @status: The port's current cc status read from IC
 * Return 0 if writes succeed; failure code otherwise
 */
static inline int rt1711h_init_cc_params(struct rt1711h_chip *chip, u8 status)
{
	int ret, cc1, cc2;
	u8 role = 0;
	u32 rxdz_en, rxdz_sel;

	ret = rt1711h_read8(chip, TCPC_ROLE_CTRL, &role);
	if (ret < 0)
		return ret;

	cc1 = tcpci_to_typec_cc(FIELD_GET(TCPC_CC_STATUS_CC1, status),
				status & TCPC_CC_STATUS_TERM ||
				tcpc_presenting_rd(role, CC1));
	cc2 = tcpci_to_typec_cc(FIELD_GET(TCPC_CC_STATUS_CC2, status),
				status & TCPC_CC_STATUS_TERM ||
				tcpc_presenting_rd(role, CC2));

	if ((cc1 >= TYPEC_CC_RP_1_5 && cc2 < TYPEC_CC_RP_DEF) ||
	    (cc2 >= TYPEC_CC_RP_1_5 && cc1 < TYPEC_CC_RP_DEF)) {
		rxdz_en = BMCIO_RXDZEN;
		rxdz_sel = chip->info->rxdz_sel;
	} else {
		rxdz_en = 0;
		rxdz_sel = RT1711H_BMCIO_RXDZSEL;
	}

	ret = regmap_update_bits(chip->data.regmap, RT1711H_RTCTRL18,
				 BMCIO_RXDZEN, rxdz_en);
	if (ret < 0)
		return ret;

	return regmap_update_bits(chip->data.regmap, RT1711H_RTCTRL4,
				  RT1711H_BMCIO_RXDZSEL, rxdz_sel);
}

static int rt1711h_start_drp_toggling(struct tcpci *tcpci,
				      struct tcpci_data *tdata,
				      enum typec_cc_status cc)
{
	struct rt1711h_chip *chip = tdata_to_rt1711h(tdata);
	int ret;
	unsigned int reg = 0;

	switch (cc) {
	default:
	case TYPEC_CC_RP_DEF:
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				  TCPC_ROLE_CTRL_RP_VAL_DEF);
		break;
	case TYPEC_CC_RP_1_5:
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				  TCPC_ROLE_CTRL_RP_VAL_1_5);
		break;
	case TYPEC_CC_RP_3_0:
		reg |= FIELD_PREP(TCPC_ROLE_CTRL_RP_VAL,
				  TCPC_ROLE_CTRL_RP_VAL_3_0);
		break;
	}

	if (cc == TYPEC_CC_RD)
		reg |= (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RD)
			| FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RD));
	else
		reg |= (FIELD_PREP(TCPC_ROLE_CTRL_CC1, TCPC_ROLE_CTRL_CC_RP)
			| FIELD_PREP(TCPC_ROLE_CTRL_CC2, TCPC_ROLE_CTRL_CC_RP));

	ret = rt1711h_write8(chip, TCPC_ROLE_CTRL, reg);
	if (ret < 0)
		return ret;
	usleep_range(500, 1000);

	return 0;
}

static irqreturn_t rt1711h_irq(int irq, void *dev_id)
{
	int ret;
	u16 alert;
	u8 status;
	struct rt1711h_chip *chip = dev_id;

	if (!chip->tcpci)
		return IRQ_HANDLED;

	ret = rt1711h_read16(chip, TCPC_ALERT, &alert);
	if (ret < 0)
		goto out;

	if (alert & TCPC_ALERT_CC_STATUS) {
		ret = rt1711h_read8(chip, TCPC_CC_STATUS, &status);
		if (ret < 0)
			goto out;
		/* Clear cc change event triggered by starting toggling */
		if (status & TCPC_CC_STATUS_TOGGLING)
			rt1711h_write8(chip, TCPC_ALERT, TCPC_ALERT_CC_STATUS);
		else
			rt1711h_init_cc_params(chip, status);
	}

out:
	return tcpci_irq(chip->tcpci);
}

static int rt1711h_sw_reset(struct rt1711h_chip *chip)
{
	int ret;

	ret = rt1711h_write8(chip, RT1711H_RTCTRL13, 0x01);
	if (ret < 0)
		return ret;

	usleep_range(1000, 2000);
	return 0;
}

static int rt1711h_check_revision(struct i2c_client *i2c, struct rt1711h_chip *chip)
{
	int ret;

	ret = i2c_smbus_read_word_data(i2c, TCPC_VENDOR_ID);
	if (ret < 0)
		return ret;
	if (ret != RT1711H_VID) {
		dev_err(&i2c->dev, "vid is not correct, 0x%04x\n", ret);
		return -ENODEV;
	}
	ret = i2c_smbus_read_word_data(i2c, TCPC_PRODUCT_ID);
	if (ret < 0)
		return ret;
	if (ret != RT1711H_PID) {
		dev_err(&i2c->dev, "pid is not correct, 0x%04x\n", ret);
		return -ENODEV;
	}
	ret = i2c_smbus_read_word_data(i2c, TCPC_BCD_DEV);
	if (ret < 0)
		return ret;
	if (ret != chip->info->did) {
		dev_err(&i2c->dev, "did is not correct, 0x%04x\n", ret);
		return -ENODEV;
	}
	dev_dbg(&i2c->dev, "did is 0x%04x\n", ret);
	return ret;
}

static int rt1711h_probe(struct i2c_client *client)
{
	int ret;
	struct rt1711h_chip *chip;
	const u16 alert_mask = TCPC_ALERT_TX_SUCCESS | TCPC_ALERT_TX_DISCARDED |
			       TCPC_ALERT_TX_FAILED | TCPC_ALERT_RX_HARD_RST |
			       TCPC_ALERT_RX_STATUS | TCPC_ALERT_POWER_STATUS |
			       TCPC_ALERT_CC_STATUS | TCPC_ALERT_RX_BUF_OVF |
			       TCPC_ALERT_FAULT;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->info = i2c_get_match_data(client);

	ret = rt1711h_check_revision(client, chip);
	if (ret < 0) {
		dev_err(&client->dev, "check vid/pid fail\n");
		return ret;
	}

	chip->data.regmap = devm_regmap_init_i2c(client,
						 &rt1711h_regmap_config);
	if (IS_ERR(chip->data.regmap))
		return PTR_ERR(chip->data.regmap);

	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

	ret = rt1711h_sw_reset(chip);
	if (ret < 0)
		return ret;

	/* Disable chip interrupts before requesting irq */
	ret = rt1711h_write16(chip, TCPC_ALERT_MASK, 0);
	if (ret < 0)
		return ret;

	chip->vbus = devm_regulator_get(&client->dev, "vbus");
	if (IS_ERR(chip->vbus))
		return PTR_ERR(chip->vbus);

	chip->data.init = rt1711h_init;
	chip->data.set_vbus = rt1711h_set_vbus;
	chip->data.set_vconn = rt1711h_set_vconn;
	chip->data.start_drp_toggling = rt1711h_start_drp_toggling;
	chip->tcpci = tcpci_register_port(chip->dev, &chip->data);
	if (IS_ERR_OR_NULL(chip->tcpci))
		return PTR_ERR(chip->tcpci);

	ret = devm_request_threaded_irq(chip->dev, client->irq, NULL,
					rt1711h_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					dev_name(chip->dev), chip);
	if (ret < 0)
		return ret;

	/* Enable alert interrupts */
	ret = rt1711h_write16(chip, TCPC_ALERT_MASK, alert_mask);
	if (ret < 0)
		return ret;

	enable_irq_wake(client->irq);

	return 0;
}

static void rt1711h_remove(struct i2c_client *client)
{
	struct rt1711h_chip *chip = i2c_get_clientdata(client);

	tcpci_unregister_port(chip->tcpci);
}

static const struct rt1711h_chip_info rt1711h = {
	.did = RT1711H_DID,
};

static const struct rt1711h_chip_info rt1715 = {
	.rxdz_sel = RT1711H_BMCIO_RXDZSEL,
	.did = RT1715_DID,
	.enable_pd30_extended_message = true,
};

static const struct i2c_device_id rt1711h_id[] = {
	{ "rt1711h", (kernel_ulong_t)&rt1711h },
	{ "rt1715", (kernel_ulong_t)&rt1715 },
	{}
};
MODULE_DEVICE_TABLE(i2c, rt1711h_id);

static const struct of_device_id rt1711h_of_match[] = {
	{ .compatible = "richtek,rt1711h", .data = &rt1711h },
	{ .compatible = "richtek,rt1715", .data = &rt1715 },
	{}
};
MODULE_DEVICE_TABLE(of, rt1711h_of_match);

static struct i2c_driver rt1711h_i2c_driver = {
	.driver = {
		.name = "rt1711h",
		.of_match_table = rt1711h_of_match,
	},
	.probe = rt1711h_probe,
	.remove = rt1711h_remove,
	.id_table = rt1711h_id,
};
module_i2c_driver(rt1711h_i2c_driver);

MODULE_AUTHOR("ShuFan Lee <shufan_lee@richtek.com>");
MODULE_DESCRIPTION("RT1711H USB Type-C Port Controller Interface Driver");
MODULE_LICENSE("GPL");
