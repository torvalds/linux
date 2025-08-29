// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * NXP NETC V4 Timer driver
 * Copyright 2025 NXP
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/fsl/netc_global.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/ptp_clock_kernel.h>

#define NETC_TMR_PCI_VENDOR_NXP		0x1131

#define NETC_TMR_CTRL			0x0080
#define  TMR_CTRL_CK_SEL		GENMASK(1, 0)
#define  TMR_CTRL_TE			BIT(2)
#define  TMR_COMP_MODE			BIT(15)
#define  TMR_CTRL_TCLK_PERIOD		GENMASK(25, 16)

#define NETC_TMR_CNT_L			0x0098
#define NETC_TMR_CNT_H			0x009c
#define NETC_TMR_ADD			0x00a0
#define NETC_TMR_PRSC			0x00a8
#define NETC_TMR_OFF_L			0x00b0
#define NETC_TMR_OFF_H			0x00b4

#define NETC_TMR_FIPER_CTRL		0x00dc
#define  FIPER_CTRL_DIS(i)		(BIT(7) << (i) * 8)
#define  FIPER_CTRL_PG(i)		(BIT(6) << (i) * 8)

#define NETC_TMR_CUR_TIME_L		0x00f0
#define NETC_TMR_CUR_TIME_H		0x00f4

#define NETC_TMR_REGS_BAR		0

#define NETC_TMR_FIPER_NUM		3
#define NETC_TMR_DEFAULT_PRSC		2

/* 1588 timer reference clock source select */
#define NETC_TMR_CCM_TIMER1		0 /* enet_timer1_clk_root, from CCM */
#define NETC_TMR_SYSTEM_CLK		1 /* enet_clk_root/2, from CCM */
#define NETC_TMR_EXT_OSC		2 /* tmr_1588_clk, from IO pins */

#define NETC_TMR_SYSCLK_333M		333333333U

struct netc_timer {
	void __iomem *base;
	struct pci_dev *pdev;
	spinlock_t lock; /* Prevent concurrent access to registers */

	struct ptp_clock *clock;
	struct ptp_clock_info caps;
	u32 clk_select;
	u32 clk_freq;
	u32 oclk_prsc;
	/* High 32-bit is integer part, low 32-bit is fractional part */
	u64 period;
};

#define netc_timer_rd(p, o)		netc_read((p)->base + (o))
#define netc_timer_wr(p, o, v)		netc_write((p)->base + (o), v)
#define ptp_to_netc_timer(ptp)		container_of((ptp), struct netc_timer, caps)

static const char *const timer_clk_src[] = {
	"ccm",
	"ext"
};

static void netc_timer_cnt_write(struct netc_timer *priv, u64 ns)
{
	u32 tmr_cnt_h = upper_32_bits(ns);
	u32 tmr_cnt_l = lower_32_bits(ns);

	/* Writes to the TMR_CNT_L register copies the written value
	 * into the shadow TMR_CNT_L register. Writes to the TMR_CNT_H
	 * register copies the values written into the shadow TMR_CNT_H
	 * register. Contents of the shadow registers are copied into
	 * the TMR_CNT_L and TMR_CNT_H registers following a write into
	 * the TMR_CNT_H register. So the user must writes to TMR_CNT_L
	 * register first. Other H/L registers should have the same
	 * behavior.
	 */
	netc_timer_wr(priv, NETC_TMR_CNT_L, tmr_cnt_l);
	netc_timer_wr(priv, NETC_TMR_CNT_H, tmr_cnt_h);
}

static u64 netc_timer_offset_read(struct netc_timer *priv)
{
	u32 tmr_off_l, tmr_off_h;
	u64 offset;

	tmr_off_l = netc_timer_rd(priv, NETC_TMR_OFF_L);
	tmr_off_h = netc_timer_rd(priv, NETC_TMR_OFF_H);
	offset = (((u64)tmr_off_h) << 32) | tmr_off_l;

	return offset;
}

static void netc_timer_offset_write(struct netc_timer *priv, u64 offset)
{
	u32 tmr_off_h = upper_32_bits(offset);
	u32 tmr_off_l = lower_32_bits(offset);

	netc_timer_wr(priv, NETC_TMR_OFF_L, tmr_off_l);
	netc_timer_wr(priv, NETC_TMR_OFF_H, tmr_off_h);
}

static u64 netc_timer_cur_time_read(struct netc_timer *priv)
{
	u32 time_h, time_l;
	u64 ns;

	/* The user should read NETC_TMR_CUR_TIME_L first to
	 * get correct current time.
	 */
	time_l = netc_timer_rd(priv, NETC_TMR_CUR_TIME_L);
	time_h = netc_timer_rd(priv, NETC_TMR_CUR_TIME_H);
	ns = (u64)time_h << 32 | time_l;

	return ns;
}

static void netc_timer_adjust_period(struct netc_timer *priv, u64 period)
{
	u32 fractional_period = lower_32_bits(period);
	u32 integral_period = upper_32_bits(period);
	u32 tmr_ctrl, old_tmr_ctrl;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	old_tmr_ctrl = netc_timer_rd(priv, NETC_TMR_CTRL);
	tmr_ctrl = u32_replace_bits(old_tmr_ctrl, integral_period,
				    TMR_CTRL_TCLK_PERIOD);
	if (tmr_ctrl != old_tmr_ctrl)
		netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);

	netc_timer_wr(priv, NETC_TMR_ADD, fractional_period);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int netc_timer_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	u64 new_period;

	new_period = adjust_by_scaled_ppm(priv->period, scaled_ppm);
	netc_timer_adjust_period(priv, new_period);

	return 0;
}

static int netc_timer_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	unsigned long flags;
	s64 tmr_off;

	spin_lock_irqsave(&priv->lock, flags);

	/* Adjusting TMROFF instead of TMR_CNT is that the timer
	 * counter keeps increasing during reading and writing
	 * TMR_CNT, which will cause latency.
	 */
	tmr_off = netc_timer_offset_read(priv);
	tmr_off += delta;
	netc_timer_offset_write(priv, tmr_off);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int netc_timer_gettimex64(struct ptp_clock_info *ptp,
				 struct timespec64 *ts,
				 struct ptp_system_timestamp *sts)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&priv->lock, flags);

	ptp_read_system_prets(sts);
	ns = netc_timer_cur_time_read(priv);
	ptp_read_system_postts(sts);

	spin_unlock_irqrestore(&priv->lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int netc_timer_settime64(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	u64 ns = timespec64_to_ns(ts);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	netc_timer_offset_write(priv, 0);
	netc_timer_cnt_write(priv, ns);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static const struct ptp_clock_info netc_timer_ptp_caps = {
	.owner		= THIS_MODULE,
	.name		= "NETC Timer PTP clock",
	.max_adj	= 500000000,
	.n_pins		= 0,
	.adjfine	= netc_timer_adjfine,
	.adjtime	= netc_timer_adjtime,
	.gettimex64	= netc_timer_gettimex64,
	.settime64	= netc_timer_settime64,
};

static void netc_timer_init(struct netc_timer *priv)
{
	u32 fractional_period = lower_32_bits(priv->period);
	u32 integral_period = upper_32_bits(priv->period);
	u32 tmr_ctrl, fiper_ctrl;
	struct timespec64 now;
	u64 ns;
	int i;

	/* Software must enable timer first and the clock selected must be
	 * active, otherwise, the registers which are in the timer clock
	 * domain are not accessible.
	 */
	tmr_ctrl = FIELD_PREP(TMR_CTRL_CK_SEL, priv->clk_select) |
		   TMR_CTRL_TE;
	netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);
	netc_timer_wr(priv, NETC_TMR_PRSC, priv->oclk_prsc);

	/* Disable FIPER by default */
	fiper_ctrl = netc_timer_rd(priv, NETC_TMR_FIPER_CTRL);
	for (i = 0; i < NETC_TMR_FIPER_NUM; i++) {
		fiper_ctrl |= FIPER_CTRL_DIS(i);
		fiper_ctrl &= ~FIPER_CTRL_PG(i);
	}
	netc_timer_wr(priv, NETC_TMR_FIPER_CTRL, fiper_ctrl);

	ktime_get_real_ts64(&now);
	ns = timespec64_to_ns(&now);
	netc_timer_cnt_write(priv, ns);

	/* Allow atomic writes to TCLK_PERIOD and TMR_ADD, An update to
	 * TCLK_PERIOD does not take effect until TMR_ADD is written.
	 */
	tmr_ctrl |= FIELD_PREP(TMR_CTRL_TCLK_PERIOD, integral_period) |
		    TMR_COMP_MODE;
	netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);
	netc_timer_wr(priv, NETC_TMR_ADD, fractional_period);
}

static int netc_timer_pci_probe(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct netc_timer *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pcie_flr(pdev);
	err = pci_enable_device_mem(pdev);
	if (err)
		return dev_err_probe(dev, err, "Failed to enable device\n");

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	err = pci_request_mem_regions(pdev, KBUILD_MODNAME);
	if (err) {
		dev_err(dev, "pci_request_regions() failed, err:%pe\n",
			ERR_PTR(err));
		goto disable_dev;
	}

	pci_set_master(pdev);

	priv->pdev = pdev;
	priv->base = pci_ioremap_bar(pdev, NETC_TMR_REGS_BAR);
	if (!priv->base) {
		err = -ENOMEM;
		goto release_mem_regions;
	}

	pci_set_drvdata(pdev, priv);

	return 0;

release_mem_regions:
	pci_release_mem_regions(pdev);
disable_dev:
	pci_disable_device(pdev);

	return err;
}

static void netc_timer_pci_remove(struct pci_dev *pdev)
{
	struct netc_timer *priv = pci_get_drvdata(pdev);

	iounmap(priv->base);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

static int netc_timer_get_reference_clk_source(struct netc_timer *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct clk *clk;
	int i;

	/* Select NETC system clock as the reference clock by default */
	priv->clk_select = NETC_TMR_SYSTEM_CLK;
	priv->clk_freq = NETC_TMR_SYSCLK_333M;

	/* Update the clock source of the reference clock if the clock
	 * is specified in DT node.
	 */
	for (i = 0; i < ARRAY_SIZE(timer_clk_src); i++) {
		clk = devm_clk_get_optional_enabled(dev, timer_clk_src[i]);
		if (IS_ERR(clk))
			return dev_err_probe(dev, PTR_ERR(clk),
					     "Failed to enable clock\n");

		if (clk) {
			priv->clk_freq = clk_get_rate(clk);
			priv->clk_select = i ? NETC_TMR_EXT_OSC :
					       NETC_TMR_CCM_TIMER1;
			break;
		}
	}

	/* The period is a 64-bit number, the high 32-bit is the integer
	 * part of the period, the low 32-bit is the fractional part of
	 * the period. In order to get the desired 32-bit fixed-point
	 * format, multiply the numerator of the fraction by 2^32.
	 */
	priv->period = div_u64((u64)NSEC_PER_SEC << 32, priv->clk_freq);

	return 0;
}

static int netc_timer_parse_dt(struct netc_timer *priv)
{
	return netc_timer_get_reference_clk_source(priv);
}

static int netc_timer_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct netc_timer *priv;
	int err;

	err = netc_timer_pci_probe(pdev);
	if (err)
		return err;

	priv = pci_get_drvdata(pdev);
	err = netc_timer_parse_dt(priv);
	if (err)
		goto timer_pci_remove;

	priv->caps = netc_timer_ptp_caps;
	priv->oclk_prsc = NETC_TMR_DEFAULT_PRSC;
	spin_lock_init(&priv->lock);

	netc_timer_init(priv);
	priv->clock = ptp_clock_register(&priv->caps, dev);
	if (IS_ERR(priv->clock)) {
		err = PTR_ERR(priv->clock);
		goto timer_pci_remove;
	}

	return 0;

timer_pci_remove:
	netc_timer_pci_remove(pdev);

	return err;
}

static void netc_timer_remove(struct pci_dev *pdev)
{
	struct netc_timer *priv = pci_get_drvdata(pdev);

	netc_timer_wr(priv, NETC_TMR_CTRL, 0);
	ptp_clock_unregister(priv->clock);
	netc_timer_pci_remove(pdev);
}

static const struct pci_device_id netc_timer_id_table[] = {
	{ PCI_DEVICE(NETC_TMR_PCI_VENDOR_NXP, 0xee02) },
	{ }
};
MODULE_DEVICE_TABLE(pci, netc_timer_id_table);

static struct pci_driver netc_timer_driver = {
	.name = KBUILD_MODNAME,
	.id_table = netc_timer_id_table,
	.probe = netc_timer_probe,
	.remove = netc_timer_remove,
};
module_pci_driver(netc_timer_driver);

MODULE_DESCRIPTION("NXP NETC Timer PTP Driver");
MODULE_LICENSE("Dual BSD/GPL");
