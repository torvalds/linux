// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 NXP
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define RTCC_OFFSET	0x4ul
#define RTCS_OFFSET	0x8ul
#define APIVAL_OFFSET	0x10ul

/* RTCC fields */
#define RTCC_CNTEN				BIT(31)
#define RTCC_APIEN				BIT(15)
#define RTCC_APIIE				BIT(14)
#define RTCC_CLKSEL_MASK		GENMASK(13, 12)
#define RTCC_DIV512EN			BIT(11)
#define RTCC_DIV32EN			BIT(10)

/* RTCS fields */
#define RTCS_INV_API	BIT(17)
#define RTCS_APIF		BIT(13)

#define APIVAL_MAX_VAL		GENMASK(31, 0)
#define RTC_SYNCH_TIMEOUT	(100 * USEC_PER_MSEC)

/*
 * S32G2 and S32G3 SoCs have RTC clock source1 reserved and
 * should not be used.
 */
#define RTC_CLK_SRC1_RESERVED		BIT(1)

/*
 * S32G RTC module has a 512 value and a 32 value hardware frequency
 * divisors (DIV512 and DIV32) which could be used to achieve higher
 * counter ranges by lowering the RTC frequency.
 */
enum {
	DIV1 = 1,
	DIV32 = 32,
	DIV512 = 512,
	DIV512_32 = 16384
};

static const char *const rtc_clk_src[] = {
	"source0",
	"source1",
	"source2",
	"source3"
};

struct rtc_priv {
	struct rtc_device *rdev;
	void __iomem *rtc_base;
	struct clk *ipg;
	struct clk *clk_src;
	const struct rtc_soc_data *rtc_data;
	u64 rtc_hz;
	time64_t sleep_sec;
	int irq;
	u32 clk_src_idx;
};

struct rtc_soc_data {
	u32 clk_div;
	u32 reserved_clk_mask;
};

static const struct rtc_soc_data rtc_s32g2_data = {
	.clk_div = DIV512_32,
	.reserved_clk_mask = RTC_CLK_SRC1_RESERVED,
};

static irqreturn_t s32g_rtc_handler(int irq, void *dev)
{
	struct rtc_priv *priv = platform_get_drvdata(dev);
	u32 status;

	status = readl(priv->rtc_base + RTCS_OFFSET);

	if (status & RTCS_APIF) {
		writel(0x0, priv->rtc_base + APIVAL_OFFSET);
		writel(status | RTCS_APIF, priv->rtc_base + RTCS_OFFSET);
	}

	rtc_update_irq(priv->rdev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

/*
 * The function is not really getting time from the RTC since the S32G RTC
 * has several limitations. Thus, to setup alarm use system time.
 */
static int s32g_rtc_read_time(struct device *dev,
			      struct rtc_time *tm)
{
	struct rtc_priv *priv = dev_get_drvdata(dev);
	time64_t sec;

	if (check_add_overflow(ktime_get_real_seconds(),
			       priv->sleep_sec, &sec))
		return -ERANGE;

	rtc_time64_to_tm(sec, tm);

	return 0;
}

static int s32g_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_priv *priv = dev_get_drvdata(dev);
	u32 rtcc, rtcs;

	rtcc = readl(priv->rtc_base + RTCC_OFFSET);
	rtcs = readl(priv->rtc_base + RTCS_OFFSET);

	alrm->enabled = rtcc & RTCC_APIIE;
	if (alrm->enabled)
		alrm->pending = !(rtcs & RTCS_APIF);

	return 0;
}

static int s32g_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rtc_priv *priv = dev_get_drvdata(dev);
	u32 rtcc;

	/* RTC API functionality is used both for triggering interrupts
	 * and as a wakeup event. Hence it should always be enabled.
	 */
	rtcc = readl(priv->rtc_base + RTCC_OFFSET);
	rtcc |= RTCC_APIEN | RTCC_APIIE;
	writel(rtcc, priv->rtc_base + RTCC_OFFSET);

	return 0;
}

static int s32g_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_priv *priv = dev_get_drvdata(dev);
	unsigned long long cycles;
	long long t_offset;
	time64_t alrm_time;
	u32 rtcs;
	int ret;

	alrm_time = rtc_tm_to_time64(&alrm->time);
	t_offset = alrm_time - ktime_get_real_seconds() - priv->sleep_sec;
	if (t_offset < 0)
		return -ERANGE;

	cycles = t_offset * priv->rtc_hz;
	if (cycles > APIVAL_MAX_VAL)
		return -ERANGE;

	/* APIVAL could have been reset from the IRQ handler.
	 * Hence, we wait in case there is a synchronization process.
	 */
	ret = read_poll_timeout(readl, rtcs, !(rtcs & RTCS_INV_API),
				0, RTC_SYNCH_TIMEOUT, false, priv->rtc_base + RTCS_OFFSET);
	if (ret)
		return ret;

	writel(cycles, priv->rtc_base + APIVAL_OFFSET);

	return read_poll_timeout(readl, rtcs, !(rtcs & RTCS_INV_API),
				0, RTC_SYNCH_TIMEOUT, false, priv->rtc_base + RTCS_OFFSET);
}

/*
 * Disable the 32-bit free running counter.
 * This allows Clock Source and Divisors selection
 * to be performed without causing synchronization issues.
 */
static void s32g_rtc_disable(struct rtc_priv *priv)
{
	u32 rtcc = readl(priv->rtc_base + RTCC_OFFSET);

	rtcc &= ~RTCC_CNTEN;
	writel(rtcc, priv->rtc_base + RTCC_OFFSET);
}

static void s32g_rtc_enable(struct rtc_priv *priv)
{
	u32 rtcc = readl(priv->rtc_base + RTCC_OFFSET);

	rtcc |= RTCC_CNTEN;
	writel(rtcc, priv->rtc_base + RTCC_OFFSET);
}

static int rtc_clk_src_setup(struct rtc_priv *priv)
{
	u32 rtcc;

	rtcc = FIELD_PREP(RTCC_CLKSEL_MASK, priv->clk_src_idx);

	switch (priv->rtc_data->clk_div) {
	case DIV512_32:
		rtcc |= RTCC_DIV512EN;
		rtcc |= RTCC_DIV32EN;
		break;
	case DIV512:
		rtcc |= RTCC_DIV512EN;
		break;
	case DIV32:
		rtcc |= RTCC_DIV32EN;
		break;
	case DIV1:
		break;
	default:
		return -EINVAL;
	}

	rtcc |= RTCC_APIEN | RTCC_APIIE;
	/*
	 * Make sure the CNTEN is 0 before we configure
	 * the clock source and dividers.
	 */
	s32g_rtc_disable(priv);
	writel(rtcc, priv->rtc_base + RTCC_OFFSET);
	s32g_rtc_enable(priv);

	return 0;
}

static const struct rtc_class_ops rtc_ops = {
	.read_time = s32g_rtc_read_time,
	.read_alarm = s32g_rtc_read_alarm,
	.set_alarm = s32g_rtc_set_alarm,
	.alarm_irq_enable = s32g_rtc_alarm_irq_enable,
};

static int rtc_clk_dts_setup(struct rtc_priv *priv,
			     struct device *dev)
{
	u32 i;

	priv->ipg = devm_clk_get_enabled(dev, "ipg");
	if (IS_ERR(priv->ipg))
		return dev_err_probe(dev, PTR_ERR(priv->ipg),
				"Failed to get 'ipg' clock\n");

	for (i = 0; i < ARRAY_SIZE(rtc_clk_src); i++) {
		if (priv->rtc_data->reserved_clk_mask & BIT(i))
			return -EOPNOTSUPP;

		priv->clk_src = devm_clk_get_enabled(dev, rtc_clk_src[i]);
		if (!IS_ERR(priv->clk_src)) {
			priv->clk_src_idx = i;
			break;
		}
	}

	if (IS_ERR(priv->clk_src))
		return dev_err_probe(dev, PTR_ERR(priv->clk_src),
				"Failed to get rtc module clock source\n");

	return 0;
}

static int s32g_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtc_priv *priv;
	unsigned long rtc_hz;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rtc_data = of_device_get_match_data(dev);
	if (!priv->rtc_data)
		return -ENODEV;

	priv->rtc_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->rtc_base))
		return PTR_ERR(priv->rtc_base);

	device_init_wakeup(dev, true);

	ret = rtc_clk_dts_setup(priv, dev);
	if (ret)
		return ret;

	priv->rdev = devm_rtc_allocate_device(dev);
	if (IS_ERR(priv->rdev))
		return PTR_ERR(priv->rdev);

	ret = rtc_clk_src_setup(priv);
	if (ret)
		return ret;

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		ret = priv->irq;
		goto disable_rtc;
	}

	rtc_hz = clk_get_rate(priv->clk_src);
	if (!rtc_hz) {
		dev_err(dev, "Failed to get RTC frequency\n");
		ret = -EINVAL;
		goto disable_rtc;
	}

	priv->rtc_hz = DIV_ROUND_UP(rtc_hz, priv->rtc_data->clk_div);

	platform_set_drvdata(pdev, priv);
	priv->rdev->ops = &rtc_ops;

	ret = devm_request_irq(dev, priv->irq,
			       s32g_rtc_handler, 0, dev_name(dev), pdev);
	if (ret) {
		dev_err(dev, "Request interrupt %d failed, error: %d\n",
			priv->irq, ret);
		goto disable_rtc;
	}

	ret = devm_rtc_register_device(priv->rdev);
	if (ret)
		goto disable_rtc;

	return 0;

disable_rtc:
	s32g_rtc_disable(priv);
	return ret;
}

static int s32g_rtc_suspend(struct device *dev)
{
	struct rtc_priv *priv = dev_get_drvdata(dev);
	u32 apival = readl(priv->rtc_base + APIVAL_OFFSET);

	if (check_add_overflow(priv->sleep_sec, div64_u64(apival, priv->rtc_hz),
			       &priv->sleep_sec)) {
		dev_warn(dev, "Overflow on sleep cycles occurred. Resetting to 0.\n");
		priv->sleep_sec = 0;
	}

	return 0;
}

static int s32g_rtc_resume(struct device *dev)
{
	struct rtc_priv *priv = dev_get_drvdata(dev);

	/* The transition from resume to run is a reset event.
	 * This leads to the RTC registers being reset after resume from
	 * suspend. It is uncommon, but this behaviour has been observed
	 * on S32G RTC after issuing a Suspend to RAM operation.
	 * Thus, reconfigure RTC registers on the resume path.
	 */
	return rtc_clk_src_setup(priv);
}

static const struct of_device_id rtc_dt_ids[] = {
	{ .compatible = "nxp,s32g2-rtc", .data = &rtc_s32g2_data },
	{ /* sentinel */ },
};

static DEFINE_SIMPLE_DEV_PM_OPS(s32g_rtc_pm_ops,
			 s32g_rtc_suspend, s32g_rtc_resume);

static struct platform_driver s32g_rtc_driver = {
	.driver = {
		.name = "s32g-rtc",
		.pm = pm_sleep_ptr(&s32g_rtc_pm_ops),
		.of_match_table = rtc_dt_ids,
	},
	.probe = s32g_rtc_probe,
};
module_platform_driver(s32g_rtc_driver);

MODULE_AUTHOR("NXP");
MODULE_DESCRIPTION("NXP RTC driver for S32G2/S32G3");
MODULE_LICENSE("GPL");
