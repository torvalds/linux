/*
 * Renesas RIIC driver
 *
 * Copyright (C) 2013 Wolfram Sang <wsa@sang-engineering.com>
 * Copyright (C) 2013 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * This i2c core has a lot of interrupts, namely 8. We use their chaining as
 * some kind of state machine.
 *
 * 1) The main xfer routine kicks off a transmission by putting the start bit
 * (or repeated start) on the bus and enabling the transmit interrupt (TIE)
 * since we need to send the slave address + RW bit in every case.
 *
 * 2) TIE sends slave address + RW bit and selects how to continue.
 *
 * 3a) Write case: We keep utilizing TIE as long as we have data to send. If we
 * are done, we switch over to the transmission done interrupt (TEIE) and mark
 * the message as completed (includes sending STOP) there.
 *
 * 3b) Read case: We switch over to receive interrupt (RIE). One dummy read is
 * needed to start clocking, then we keep receiving until we are done. Note
 * that we use the RDRFS mode all the time, i.e. we ACK/NACK every byte by
 * writing to the ACKBT bit. I tried using the RDRFS mode only at the end of a
 * message to create the final NACK as sketched in the datasheet. This caused
 * some subtle races (when byte n was processed and byte n+1 was already
 * waiting), though, and I started with the safe approach.
 *
 * 4) If we got a NACK somewhere, we flag the error and stop the transmission
 * via NAKIE.
 *
 * Also check the comments in the interrupt routines for some gory details.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define RIIC_ICCR1	0x00
#define RIIC_ICCR2	0x04
#define RIIC_ICMR1	0x08
#define RIIC_ICMR3	0x10
#define RIIC_ICSER	0x18
#define RIIC_ICIER	0x1c
#define RIIC_ICSR2	0x24
#define RIIC_ICBRL	0x34
#define RIIC_ICBRH	0x38
#define RIIC_ICDRT	0x3c
#define RIIC_ICDRR	0x40

#define ICCR1_ICE	0x80
#define ICCR1_IICRST	0x40
#define ICCR1_SOWP	0x10

#define ICCR2_BBSY	0x80
#define ICCR2_SP	0x08
#define ICCR2_RS	0x04
#define ICCR2_ST	0x02

#define ICMR1_CKS_MASK	0x70
#define ICMR1_BCWP	0x08
#define ICMR1_CKS(_x)	((((_x) << 4) & ICMR1_CKS_MASK) | ICMR1_BCWP)

#define ICMR3_RDRFS	0x20
#define ICMR3_ACKWP	0x10
#define ICMR3_ACKBT	0x08

#define ICIER_TIE	0x80
#define ICIER_TEIE	0x40
#define ICIER_RIE	0x20
#define ICIER_NAKIE	0x10

#define ICSR2_NACKF	0x10

/* ICBRx (@ PCLK 33MHz) */
#define ICBR_RESERVED	0xe0 /* Should be 1 on writes */
#define ICBRL_SP100K	(19 | ICBR_RESERVED)
#define ICBRH_SP100K	(16 | ICBR_RESERVED)
#define ICBRL_SP400K	(21 | ICBR_RESERVED)
#define ICBRH_SP400K	(9 | ICBR_RESERVED)

#define RIIC_INIT_MSG	-1

struct riic_dev {
	void __iomem *base;
	u8 *buf;
	struct i2c_msg *msg;
	int bytes_left;
	int err;
	int is_last;
	struct completion msg_done;
	struct i2c_adapter adapter;
	struct clk *clk;
};

struct riic_irq_desc {
	int res_num;
	irq_handler_t isr;
	char *name;
};

static inline void riic_clear_set_bit(struct riic_dev *riic, u8 clear, u8 set, u8 reg)
{
	writeb((readb(riic->base + reg) & ~clear) | set, riic->base + reg);
}

static int riic_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct riic_dev *riic = i2c_get_adapdata(adap);
	unsigned long time_left;
	int i, ret;
	u8 start_bit;

	ret = clk_prepare_enable(riic->clk);
	if (ret)
		return ret;

	if (readb(riic->base + RIIC_ICCR2) & ICCR2_BBSY) {
		riic->err = -EBUSY;
		goto out;
	}

	reinit_completion(&riic->msg_done);
	riic->err = 0;

	writeb(0, riic->base + RIIC_ICSR2);

	for (i = 0, start_bit = ICCR2_ST; i < num; i++) {
		riic->bytes_left = RIIC_INIT_MSG;
		riic->buf = msgs[i].buf;
		riic->msg = &msgs[i];
		riic->is_last = (i == num - 1);

		writeb(ICIER_NAKIE | ICIER_TIE, riic->base + RIIC_ICIER);

		writeb(start_bit, riic->base + RIIC_ICCR2);

		time_left = wait_for_completion_timeout(&riic->msg_done, riic->adapter.timeout);
		if (time_left == 0)
			riic->err = -ETIMEDOUT;

		if (riic->err)
			break;

		start_bit = ICCR2_RS;
	}

 out:
	clk_disable_unprepare(riic->clk);

	return riic->err ?: num;
}

static irqreturn_t riic_tdre_isr(int irq, void *data)
{
	struct riic_dev *riic = data;
	u8 val;

	if (!riic->bytes_left)
		return IRQ_NONE;

	if (riic->bytes_left == RIIC_INIT_MSG) {
		val = !!(riic->msg->flags & I2C_M_RD);
		if (val)
			/* On read, switch over to receive interrupt */
			riic_clear_set_bit(riic, ICIER_TIE, ICIER_RIE, RIIC_ICIER);
		else
			/* On write, initialize length */
			riic->bytes_left = riic->msg->len;

		val |= (riic->msg->addr << 1);
	} else {
		val = *riic->buf;
		riic->buf++;
		riic->bytes_left--;
	}

	/*
	 * Switch to transmission ended interrupt when done. Do check here
	 * after bytes_left was initialized to support SMBUS_QUICK (new msg has
	 * 0 length then)
	 */
	if (riic->bytes_left == 0)
		riic_clear_set_bit(riic, ICIER_TIE, ICIER_TEIE, RIIC_ICIER);

	/*
	 * This acks the TIE interrupt. We get another TIE immediately if our
	 * value could be moved to the shadow shift register right away. So
	 * this must be after updates to ICIER (where we want to disable TIE)!
	 */
	writeb(val, riic->base + RIIC_ICDRT);

	return IRQ_HANDLED;
}

static irqreturn_t riic_tend_isr(int irq, void *data)
{
	struct riic_dev *riic = data;

	if (readb(riic->base + RIIC_ICSR2) & ICSR2_NACKF) {
		/* We got a NACKIE */
		readb(riic->base + RIIC_ICDRR);	/* dummy read */
		riic->err = -ENXIO;
	} else if (riic->bytes_left) {
		return IRQ_NONE;
	}

	if (riic->is_last || riic->err)
		writeb(ICCR2_SP, riic->base + RIIC_ICCR2);

	writeb(0, riic->base + RIIC_ICIER);
	complete(&riic->msg_done);

	return IRQ_HANDLED;
}

static irqreturn_t riic_rdrf_isr(int irq, void *data)
{
	struct riic_dev *riic = data;

	if (!riic->bytes_left)
		return IRQ_NONE;

	if (riic->bytes_left == RIIC_INIT_MSG) {
		riic->bytes_left = riic->msg->len;
		readb(riic->base + RIIC_ICDRR);	/* dummy read */
		return IRQ_HANDLED;
	}

	if (riic->bytes_left == 1) {
		/* STOP must come before we set ACKBT! */
		if (riic->is_last)
			writeb(ICCR2_SP, riic->base + RIIC_ICCR2);

		riic_clear_set_bit(riic, 0, ICMR3_ACKBT, RIIC_ICMR3);

		writeb(0, riic->base + RIIC_ICIER);
		complete(&riic->msg_done);
	} else {
		riic_clear_set_bit(riic, ICMR3_ACKBT, 0, RIIC_ICMR3);
	}

	/* Reading acks the RIE interrupt */
	*riic->buf = readb(riic->base + RIIC_ICDRR);
	riic->buf++;
	riic->bytes_left--;

	return IRQ_HANDLED;
}

static u32 riic_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm riic_algo = {
	.master_xfer	= riic_xfer,
	.functionality	= riic_func,
};

static int riic_init_hw(struct riic_dev *riic, u32 spd)
{
	int ret;
	unsigned long rate;

	ret = clk_prepare_enable(riic->clk);
	if (ret)
		return ret;

	/*
	 * TODO: Implement formula to calculate the timing values depending on
	 * variable parent clock rate and arbitrary bus speed
	 */
	rate = clk_get_rate(riic->clk);
	if (rate != 33325000) {
		dev_err(&riic->adapter.dev,
			"invalid parent clk (%lu). Must be 33325000Hz\n", rate);
		clk_disable_unprepare(riic->clk);
		return -EINVAL;
	}

	/* Changing the order of accessing IICRST and ICE may break things! */
	writeb(ICCR1_IICRST | ICCR1_SOWP, riic->base + RIIC_ICCR1);
	riic_clear_set_bit(riic, 0, ICCR1_ICE, RIIC_ICCR1);

	switch (spd) {
	case 100000:
		writeb(ICMR1_CKS(3), riic->base + RIIC_ICMR1);
		writeb(ICBRH_SP100K, riic->base + RIIC_ICBRH);
		writeb(ICBRL_SP100K, riic->base + RIIC_ICBRL);
		break;
	case 400000:
		writeb(ICMR1_CKS(1), riic->base + RIIC_ICMR1);
		writeb(ICBRH_SP400K, riic->base + RIIC_ICBRH);
		writeb(ICBRL_SP400K, riic->base + RIIC_ICBRL);
		break;
	default:
		dev_err(&riic->adapter.dev,
			"unsupported bus speed (%dHz). Use 100000 or 400000\n", spd);
		clk_disable_unprepare(riic->clk);
		return -EINVAL;
	}

	writeb(0, riic->base + RIIC_ICSER);
	writeb(ICMR3_ACKWP | ICMR3_RDRFS, riic->base + RIIC_ICMR3);

	riic_clear_set_bit(riic, ICCR1_IICRST, 0, RIIC_ICCR1);

	clk_disable_unprepare(riic->clk);

	return 0;
}

static struct riic_irq_desc riic_irqs[] = {
	{ .res_num = 0, .isr = riic_tend_isr, .name = "riic-tend" },
	{ .res_num = 1, .isr = riic_rdrf_isr, .name = "riic-rdrf" },
	{ .res_num = 2, .isr = riic_tdre_isr, .name = "riic-tdre" },
	{ .res_num = 5, .isr = riic_tend_isr, .name = "riic-nack" },
};

static int riic_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct riic_dev *riic;
	struct i2c_adapter *adap;
	struct resource *res;
	u32 bus_rate = 0;
	int i, ret;

	riic = devm_kzalloc(&pdev->dev, sizeof(*riic), GFP_KERNEL);
	if (!riic)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	riic->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(riic->base))
		return PTR_ERR(riic->base);

	riic->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(riic->clk)) {
		dev_err(&pdev->dev, "missing controller clock");
		return PTR_ERR(riic->clk);
	}

	for (i = 0; i < ARRAY_SIZE(riic_irqs); i++) {
		res = platform_get_resource(pdev, IORESOURCE_IRQ, riic_irqs[i].res_num);
		if (!res)
			return -ENODEV;

		ret = devm_request_irq(&pdev->dev, res->start, riic_irqs[i].isr,
					0, riic_irqs[i].name, riic);
		if (ret) {
			dev_err(&pdev->dev, "failed to request irq %s\n", riic_irqs[i].name);
			return ret;
		}
	}

	adap = &riic->adapter;
	i2c_set_adapdata(adap, riic);
	strlcpy(adap->name, "Renesas RIIC adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->algo = &riic_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	init_completion(&riic->msg_done);

	of_property_read_u32(np, "clock-frequency", &bus_rate);
	ret = riic_init_hw(riic, bus_rate);
	if (ret)
		return ret;


	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_err(&pdev->dev, "failed to add adapter\n");
		return ret;
	}

	platform_set_drvdata(pdev, riic);

	dev_info(&pdev->dev, "registered with %dHz bus speed\n", bus_rate);
	return 0;
}

static int riic_i2c_remove(struct platform_device *pdev)
{
	struct riic_dev *riic = platform_get_drvdata(pdev);

	writeb(0, riic->base + RIIC_ICIER);
	i2c_del_adapter(&riic->adapter);

	return 0;
}

static struct of_device_id riic_i2c_dt_ids[] = {
	{ .compatible = "renesas,riic-rz" },
	{ /* Sentinel */ },
};

static struct platform_driver riic_i2c_driver = {
	.probe		= riic_i2c_probe,
	.remove		= riic_i2c_remove,
	.driver		= {
		.name	= "i2c-riic",
		.owner	= THIS_MODULE,
		.of_match_table = riic_i2c_dt_ids,
	},
};

module_platform_driver(riic_i2c_driver);

MODULE_DESCRIPTION("Renesas RIIC adapter");
MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, riic_i2c_dt_ids);
