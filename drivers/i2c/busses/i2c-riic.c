// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RIIC driver
 *
 * Copyright (C) 2013 Wolfram Sang <wsa@sang-engineering.com>
 * Copyright (C) 2013 Renesas Solutions Corp.
 */

/*
 * This i2c core has a lot of interrupts, namely 8. We use their chaining as
 * some kind of state machine.
 *
 * 1) The main xfer routine kicks off a transmission by putting the start bit
 * (or repeated start) on the bus and enabling the transmit interrupt (TIE)
 * since we need to send the target address + RW bit in every case.
 *
 * 2) TIE sends target address + RW bit and selects how to continue.
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

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/time.h>

#define ICCR1_ICE	BIT(7)
#define ICCR1_IICRST	BIT(6)
#define ICCR1_SOWP	BIT(4)
#define ICCR1_SCLI	BIT(1)
#define ICCR1_SDAI	BIT(0)

#define ICCR2_BBSY	BIT(7)
#define ICCR2_SP	BIT(3)
#define ICCR2_RS	BIT(2)
#define ICCR2_ST	BIT(1)

#define ICMR1_CKS_MASK	GENMASK(6, 4)
#define ICMR1_BCWP	BIT(3)
#define ICMR1_CKS(_x)	((((_x) << 4) & ICMR1_CKS_MASK) | ICMR1_BCWP)

#define ICMR3_RDRFS	BIT(5)
#define ICMR3_ACKWP	BIT(4)
#define ICMR3_ACKBT	BIT(3)

#define ICFER_FMPE	BIT(7)

#define ICIER_TIE	BIT(7)
#define ICIER_TEIE	BIT(6)
#define ICIER_RIE	BIT(5)
#define ICIER_NAKIE	BIT(4)
#define ICIER_SPIE	BIT(3)

#define ICSR2_NACKF	BIT(4)

#define ICBR_RESERVED	GENMASK(7, 5) /* Should be 1 on writes */

#define RIIC_INIT_MSG	-1

enum riic_reg_list {
	RIIC_ICCR1 = 0,
	RIIC_ICCR2,
	RIIC_ICMR1,
	RIIC_ICMR3,
	RIIC_ICFER,
	RIIC_ICSER,
	RIIC_ICIER,
	RIIC_ICSR2,
	RIIC_ICBRL,
	RIIC_ICBRH,
	RIIC_ICDRT,
	RIIC_ICDRR,
	RIIC_REG_END,
};

struct riic_of_data {
	const u8 *regs;
	bool fast_mode_plus;
};

struct riic_dev {
	void __iomem *base;
	u8 *buf;
	struct i2c_msg *msg;
	int bytes_left;
	int err;
	int is_last;
	const struct riic_of_data *info;
	struct completion msg_done;
	struct i2c_adapter adapter;
	struct clk *clk;
	struct reset_control *rstc;
	struct i2c_timings i2c_t;
};

struct riic_irq_desc {
	int res_num;
	irq_handler_t isr;
	char *name;
};

static inline void riic_writeb(struct riic_dev *riic, u8 val, u8 offset)
{
	writeb(val, riic->base + riic->info->regs[offset]);
}

static inline u8 riic_readb(struct riic_dev *riic, u8 offset)
{
	return readb(riic->base + riic->info->regs[offset]);
}

static inline void riic_clear_set_bit(struct riic_dev *riic, u8 clear, u8 set, u8 reg)
{
	riic_writeb(riic, (riic_readb(riic, reg) & ~clear) | set, reg);
}

static int riic_bus_barrier(struct riic_dev *riic)
{
	int ret;
	u8 val;

	/*
	 * The SDA line can still be low even when BBSY = 0. Therefore, after checking
	 * the BBSY flag, also verify that the SDA and SCL lines are not being held low.
	 */
	ret = readb_poll_timeout(riic->base + riic->info->regs[RIIC_ICCR2], val,
				 !(val & ICCR2_BBSY), 10, riic->adapter.timeout);
	if (ret)
		return ret;

	if ((riic_readb(riic, RIIC_ICCR1) & (ICCR1_SDAI | ICCR1_SCLI)) !=
	     (ICCR1_SDAI | ICCR1_SCLI))
		return -EBUSY;

	return 0;
}

static int riic_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct riic_dev *riic = i2c_get_adapdata(adap);
	struct device *dev = adap->dev.parent;
	unsigned long time_left;
	int i, ret;
	u8 start_bit;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	riic->err = riic_bus_barrier(riic);
	if (riic->err)
		goto out;

	reinit_completion(&riic->msg_done);

	riic_writeb(riic, 0, RIIC_ICSR2);

	for (i = 0, start_bit = ICCR2_ST; i < num; i++) {
		riic->bytes_left = RIIC_INIT_MSG;
		riic->buf = msgs[i].buf;
		riic->msg = &msgs[i];
		riic->is_last = (i == num - 1);

		riic_writeb(riic, ICIER_NAKIE | ICIER_TIE, RIIC_ICIER);

		riic_writeb(riic, start_bit, RIIC_ICCR2);

		time_left = wait_for_completion_timeout(&riic->msg_done, riic->adapter.timeout);
		if (time_left == 0)
			riic->err = -ETIMEDOUT;

		if (riic->err)
			break;

		start_bit = ICCR2_RS;
	}

 out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return riic->err ?: num;
}

static irqreturn_t riic_tdre_isr(int irq, void *data)
{
	struct riic_dev *riic = data;
	u8 val;

	if (!riic->bytes_left)
		return IRQ_NONE;

	if (riic->bytes_left == RIIC_INIT_MSG) {
		if (riic->msg->flags & I2C_M_RD)
			/* On read, switch over to receive interrupt */
			riic_clear_set_bit(riic, ICIER_TIE, ICIER_RIE, RIIC_ICIER);
		else
			/* On write, initialize length */
			riic->bytes_left = riic->msg->len;

		val = i2c_8bit_addr_from_msg(riic->msg);
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
	riic_writeb(riic, val, RIIC_ICDRT);

	return IRQ_HANDLED;
}

static irqreturn_t riic_tend_isr(int irq, void *data)
{
	struct riic_dev *riic = data;

	if (riic_readb(riic, RIIC_ICSR2) & ICSR2_NACKF) {
		/* We got a NACKIE */
		riic_readb(riic, RIIC_ICDRR);	/* dummy read */
		riic_clear_set_bit(riic, ICSR2_NACKF, 0, RIIC_ICSR2);
		riic->err = -ENXIO;
	} else if (riic->bytes_left) {
		return IRQ_NONE;
	}

	if (riic->is_last || riic->err) {
		riic_clear_set_bit(riic, ICIER_TEIE, ICIER_SPIE, RIIC_ICIER);
		riic_writeb(riic, ICCR2_SP, RIIC_ICCR2);
	} else {
		/* Transfer is complete, but do not send STOP */
		riic_clear_set_bit(riic, ICIER_TEIE, 0, RIIC_ICIER);
		complete(&riic->msg_done);
	}

	return IRQ_HANDLED;
}

static irqreturn_t riic_rdrf_isr(int irq, void *data)
{
	struct riic_dev *riic = data;

	if (!riic->bytes_left)
		return IRQ_NONE;

	if (riic->bytes_left == RIIC_INIT_MSG) {
		riic->bytes_left = riic->msg->len;
		riic_readb(riic, RIIC_ICDRR);	/* dummy read */
		return IRQ_HANDLED;
	}

	if (riic->bytes_left == 1) {
		/* STOP must come before we set ACKBT! */
		if (riic->is_last) {
			riic_clear_set_bit(riic, 0, ICIER_SPIE, RIIC_ICIER);
			riic_writeb(riic, ICCR2_SP, RIIC_ICCR2);
		}

		riic_clear_set_bit(riic, 0, ICMR3_ACKBT, RIIC_ICMR3);

	} else {
		riic_clear_set_bit(riic, ICMR3_ACKBT, 0, RIIC_ICMR3);
	}

	/* Reading acks the RIE interrupt */
	*riic->buf = riic_readb(riic, RIIC_ICDRR);
	riic->buf++;
	riic->bytes_left--;

	return IRQ_HANDLED;
}

static irqreturn_t riic_stop_isr(int irq, void *data)
{
	struct riic_dev *riic = data;

	/* read back registers to confirm writes have fully propagated */
	riic_writeb(riic, 0, RIIC_ICSR2);
	riic_readb(riic, RIIC_ICSR2);
	riic_writeb(riic, 0, RIIC_ICIER);
	riic_readb(riic, RIIC_ICIER);

	complete(&riic->msg_done);

	return IRQ_HANDLED;
}

static u32 riic_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm riic_algo = {
	.xfer = riic_xfer,
	.functionality = riic_func,
};

static int riic_init_hw(struct riic_dev *riic)
{
	int ret;
	unsigned long rate;
	unsigned long ns_per_tick;
	int total_ticks, cks, brl, brh;
	struct i2c_timings *t = &riic->i2c_t;
	struct device *dev = riic->adapter.dev.parent;
	bool fast_mode_plus = riic->info->fast_mode_plus;
	u32 max_freq = fast_mode_plus ? I2C_MAX_FAST_MODE_PLUS_FREQ
				      : I2C_MAX_FAST_MODE_FREQ;

	if (t->bus_freq_hz > max_freq)
		return dev_err_probe(dev, -EINVAL,
				     "unsupported bus speed %uHz (%u max)\n",
				     t->bus_freq_hz, max_freq);

	rate = clk_get_rate(riic->clk);

	/*
	 * Assume the default register settings:
	 *  FER.SCLE = 1 (SCL sync circuit enabled, adds 2 or 3 cycles)
	 *  FER.NFE = 1 (noise circuit enabled)
	 *  MR3.NF = 0 (1 cycle of noise filtered out)
	 *
	 * Freq (CKS=000) = (I2CCLK + tr + tf)/ (BRH + 3 + 1) + (BRL + 3 + 1)
	 * Freq (CKS!=000) = (I2CCLK + tr + tf)/ (BRH + 2 + 1) + (BRL + 2 + 1)
	 */

	/*
	 * Determine reference clock rate. We must be able to get the desired
	 * frequency with only 62 clock ticks max (31 high, 31 low).
	 * Aim for a duty of 60% LOW, 40% HIGH.
	 */
	total_ticks = DIV_ROUND_UP(rate, t->bus_freq_hz ?: 1);

	for (cks = 0; cks < 7; cks++) {
		/*
		 * 60% low time must be less than BRL + 2 + 1
		 * BRL max register value is 0x1F.
		 */
		brl = ((total_ticks * 6) / 10);
		if (brl <= (0x1F + 3))
			break;

		total_ticks = DIV_ROUND_UP(total_ticks, 2);
		rate /= 2;
	}

	if (brl > (0x1F + 3))
		return dev_err_probe(dev, -EINVAL, "invalid speed (%uHz). Too slow.\n",
				     t->bus_freq_hz);

	brh = total_ticks - brl;

	/* Remove automatic clock ticks for sync circuit and NF */
	if (cks == 0) {
		brl -= 4;
		brh -= 4;
	} else {
		brl -= 3;
		brh -= 3;
	}

	/*
	 * Remove clock ticks for rise and fall times. Convert ns to clock
	 * ticks.
	 */
	ns_per_tick = NSEC_PER_SEC / rate;
	brl -= t->scl_fall_ns / ns_per_tick;
	brh -= t->scl_rise_ns / ns_per_tick;

	/* Adjust for min register values for when SCLE=1 and NFE=1 */
	if (brl < 1)
		brl = 1;
	if (brh < 1)
		brh = 1;

	pr_debug("i2c-riic: freq=%lu, duty=%d, fall=%lu, rise=%lu, cks=%d, brl=%d, brh=%d\n",
		 rate / total_ticks, ((brl + 3) * 100) / (brl + brh + 6),
		 t->scl_fall_ns / ns_per_tick, t->scl_rise_ns / ns_per_tick, cks, brl, brh);

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	/* Changing the order of accessing IICRST and ICE may break things! */
	riic_writeb(riic, ICCR1_IICRST | ICCR1_SOWP, RIIC_ICCR1);
	riic_clear_set_bit(riic, 0, ICCR1_ICE, RIIC_ICCR1);

	riic_writeb(riic, ICMR1_CKS(cks), RIIC_ICMR1);
	riic_writeb(riic, brh | ICBR_RESERVED, RIIC_ICBRH);
	riic_writeb(riic, brl | ICBR_RESERVED, RIIC_ICBRL);

	riic_writeb(riic, 0, RIIC_ICSER);
	riic_writeb(riic, ICMR3_ACKWP | ICMR3_RDRFS, RIIC_ICMR3);

	if (fast_mode_plus && t->bus_freq_hz > I2C_MAX_FAST_MODE_FREQ)
		riic_clear_set_bit(riic, 0, ICFER_FMPE, RIIC_ICFER);

	riic_clear_set_bit(riic, ICCR1_IICRST, 0, RIIC_ICCR1);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return 0;
}

static const struct riic_irq_desc riic_irqs[] = {
	{ .res_num = 0, .isr = riic_tend_isr, .name = "riic-tend" },
	{ .res_num = 1, .isr = riic_rdrf_isr, .name = "riic-rdrf" },
	{ .res_num = 2, .isr = riic_tdre_isr, .name = "riic-tdre" },
	{ .res_num = 3, .isr = riic_stop_isr, .name = "riic-stop" },
	{ .res_num = 5, .isr = riic_tend_isr, .name = "riic-nack" },
};

static int riic_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct riic_dev *riic;
	struct i2c_adapter *adap;
	int i, ret;

	riic = devm_kzalloc(dev, sizeof(*riic), GFP_KERNEL);
	if (!riic)
		return -ENOMEM;

	riic->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(riic->base))
		return PTR_ERR(riic->base);

	riic->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(riic->clk))
		return dev_err_probe(dev, PTR_ERR(riic->clk),
				     "missing controller clock");

	riic->rstc = devm_reset_control_get_optional_exclusive_deasserted(dev, NULL);
	if (IS_ERR(riic->rstc))
		return dev_err_probe(dev, PTR_ERR(riic->rstc),
				     "failed to acquire deasserted reset\n");

	for (i = 0; i < ARRAY_SIZE(riic_irqs); i++) {
		int irq;

		irq = platform_get_irq(pdev, riic_irqs[i].res_num);
		if (irq < 0)
			return irq;

		ret = devm_request_irq(dev, irq, riic_irqs[i].isr,
				       0, riic_irqs[i].name, riic);
		if (ret)
			return dev_err_probe(dev, ret, "failed to request irq %s\n",
					     riic_irqs[i].name);
	}

	riic->info = of_device_get_match_data(dev);

	adap = &riic->adapter;
	i2c_set_adapdata(adap, riic);
	strscpy(adap->name, "Renesas RIIC adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->algo = &riic_algo;
	adap->dev.parent = dev;
	adap->dev.of_node = dev->of_node;

	init_completion(&riic->msg_done);

	i2c_parse_fw_timings(dev, &riic->i2c_t, true);

	/* Default 0 to save power. Can be overridden via sysfs for lower latency. */
	pm_runtime_set_autosuspend_delay(dev, 0);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	ret = riic_init_hw(riic);
	if (ret)
		goto out;

	ret = i2c_add_adapter(adap);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, riic);

	dev_info(dev, "registered with %dHz bus speed\n", riic->i2c_t.bus_freq_hz);
	return 0;

out:
	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);
	return ret;
}

static void riic_i2c_remove(struct platform_device *pdev)
{
	struct riic_dev *riic = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (!ret) {
		riic_writeb(riic, 0, RIIC_ICIER);
		pm_runtime_put(dev);
	}
	i2c_del_adapter(&riic->adapter);
	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);
}

static const u8 riic_rz_a_regs[RIIC_REG_END] = {
	[RIIC_ICCR1] = 0x00,
	[RIIC_ICCR2] = 0x04,
	[RIIC_ICMR1] = 0x08,
	[RIIC_ICMR3] = 0x10,
	[RIIC_ICFER] = 0x14,
	[RIIC_ICSER] = 0x18,
	[RIIC_ICIER] = 0x1c,
	[RIIC_ICSR2] = 0x24,
	[RIIC_ICBRL] = 0x34,
	[RIIC_ICBRH] = 0x38,
	[RIIC_ICDRT] = 0x3c,
	[RIIC_ICDRR] = 0x40,
};

static const struct riic_of_data riic_rz_a_info = {
	.regs = riic_rz_a_regs,
	.fast_mode_plus = true,
};

static const struct riic_of_data riic_rz_a1h_info = {
	.regs = riic_rz_a_regs,
};

static const u8 riic_rz_v2h_regs[RIIC_REG_END] = {
	[RIIC_ICCR1] = 0x00,
	[RIIC_ICCR2] = 0x01,
	[RIIC_ICMR1] = 0x02,
	[RIIC_ICMR3] = 0x04,
	[RIIC_ICFER] = 0x05,
	[RIIC_ICSER] = 0x06,
	[RIIC_ICIER] = 0x07,
	[RIIC_ICSR2] = 0x09,
	[RIIC_ICBRL] = 0x10,
	[RIIC_ICBRH] = 0x11,
	[RIIC_ICDRT] = 0x12,
	[RIIC_ICDRR] = 0x13,
};

static const struct riic_of_data riic_rz_v2h_info = {
	.regs = riic_rz_v2h_regs,
	.fast_mode_plus = true,
};

static int riic_i2c_suspend(struct device *dev)
{
	struct riic_dev *riic = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	i2c_mark_adapter_suspended(&riic->adapter);

	/* Disable output on SDA, SCL pins. */
	riic_clear_set_bit(riic, ICCR1_ICE, 0, RIIC_ICCR1);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_sync(dev);

	return reset_control_assert(riic->rstc);
}

static int riic_i2c_resume(struct device *dev)
{
	struct riic_dev *riic = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(riic->rstc);
	if (ret)
		return ret;

	ret = riic_init_hw(riic);
	if (ret) {
		/*
		 * In case this happens there is no way to recover from this
		 * state. The driver will remain loaded. We want to avoid
		 * keeping the reset line de-asserted for no reason.
		 */
		reset_control_assert(riic->rstc);
		return ret;
	}

	i2c_mark_adapter_resumed(&riic->adapter);

	return 0;
}

static const struct dev_pm_ops riic_i2c_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(riic_i2c_suspend, riic_i2c_resume)
};

static const struct of_device_id riic_i2c_dt_ids[] = {
	{ .compatible = "renesas,riic-rz", .data = &riic_rz_a_info },
	{ .compatible = "renesas,riic-r7s72100", .data =  &riic_rz_a1h_info, },
	{ .compatible = "renesas,riic-r9a09g057", .data = &riic_rz_v2h_info },
	{ /* Sentinel */ },
};

static struct platform_driver riic_i2c_driver = {
	.probe		= riic_i2c_probe,
	.remove		= riic_i2c_remove,
	.driver		= {
		.name	= "i2c-riic",
		.of_match_table = riic_i2c_dt_ids,
		.pm	= pm_ptr(&riic_i2c_pm_ops),
	},
};

module_platform_driver(riic_i2c_driver);

MODULE_DESCRIPTION("Renesas RIIC adapter");
MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, riic_i2c_dt_ids);
