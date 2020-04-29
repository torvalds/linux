// SPDX-License-Identifier: GPL-2.0
/*
 * Tegra20 External Memory Controller driver
 *
 * Author: Dmitry Osipenko <digetx@gmail.com>
 */

#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/types.h>

#include <soc/tegra/fuse.h>

#define EMC_INTSTATUS				0x000
#define EMC_INTMASK				0x004
#define EMC_DBG					0x008
#define EMC_TIMING_CONTROL			0x028
#define EMC_RC					0x02c
#define EMC_RFC					0x030
#define EMC_RAS					0x034
#define EMC_RP					0x038
#define EMC_R2W					0x03c
#define EMC_W2R					0x040
#define EMC_R2P					0x044
#define EMC_W2P					0x048
#define EMC_RD_RCD				0x04c
#define EMC_WR_RCD				0x050
#define EMC_RRD					0x054
#define EMC_REXT				0x058
#define EMC_WDV					0x05c
#define EMC_QUSE				0x060
#define EMC_QRST				0x064
#define EMC_QSAFE				0x068
#define EMC_RDV					0x06c
#define EMC_REFRESH				0x070
#define EMC_BURST_REFRESH_NUM			0x074
#define EMC_PDEX2WR				0x078
#define EMC_PDEX2RD				0x07c
#define EMC_PCHG2PDEN				0x080
#define EMC_ACT2PDEN				0x084
#define EMC_AR2PDEN				0x088
#define EMC_RW2PDEN				0x08c
#define EMC_TXSR				0x090
#define EMC_TCKE				0x094
#define EMC_TFAW				0x098
#define EMC_TRPAB				0x09c
#define EMC_TCLKSTABLE				0x0a0
#define EMC_TCLKSTOP				0x0a4
#define EMC_TREFBW				0x0a8
#define EMC_QUSE_EXTRA				0x0ac
#define EMC_ODT_WRITE				0x0b0
#define EMC_ODT_READ				0x0b4
#define EMC_FBIO_CFG5				0x104
#define EMC_FBIO_CFG6				0x114
#define EMC_AUTO_CAL_INTERVAL			0x2a8
#define EMC_CFG_2				0x2b8
#define EMC_CFG_DIG_DLL				0x2bc
#define EMC_DLL_XFORM_DQS			0x2c0
#define EMC_DLL_XFORM_QUSE			0x2c4
#define EMC_ZCAL_REF_CNT			0x2e0
#define EMC_ZCAL_WAIT_CNT			0x2e4
#define EMC_CFG_CLKTRIM_0			0x2d0
#define EMC_CFG_CLKTRIM_1			0x2d4
#define EMC_CFG_CLKTRIM_2			0x2d8

#define EMC_CLKCHANGE_REQ_ENABLE		BIT(0)
#define EMC_CLKCHANGE_PD_ENABLE			BIT(1)
#define EMC_CLKCHANGE_SR_ENABLE			BIT(2)

#define EMC_TIMING_UPDATE			BIT(0)

#define EMC_REFRESH_OVERFLOW_INT		BIT(3)
#define EMC_CLKCHANGE_COMPLETE_INT		BIT(4)

#define EMC_DBG_READ_MUX_ASSEMBLY		BIT(0)
#define EMC_DBG_WRITE_MUX_ACTIVE		BIT(1)
#define EMC_DBG_FORCE_UPDATE			BIT(2)
#define EMC_DBG_READ_DQM_CTRL			BIT(9)
#define EMC_DBG_CFG_PRIORITY			BIT(24)

static const u16 emc_timing_registers[] = {
	EMC_RC,
	EMC_RFC,
	EMC_RAS,
	EMC_RP,
	EMC_R2W,
	EMC_W2R,
	EMC_R2P,
	EMC_W2P,
	EMC_RD_RCD,
	EMC_WR_RCD,
	EMC_RRD,
	EMC_REXT,
	EMC_WDV,
	EMC_QUSE,
	EMC_QRST,
	EMC_QSAFE,
	EMC_RDV,
	EMC_REFRESH,
	EMC_BURST_REFRESH_NUM,
	EMC_PDEX2WR,
	EMC_PDEX2RD,
	EMC_PCHG2PDEN,
	EMC_ACT2PDEN,
	EMC_AR2PDEN,
	EMC_RW2PDEN,
	EMC_TXSR,
	EMC_TCKE,
	EMC_TFAW,
	EMC_TRPAB,
	EMC_TCLKSTABLE,
	EMC_TCLKSTOP,
	EMC_TREFBW,
	EMC_QUSE_EXTRA,
	EMC_FBIO_CFG6,
	EMC_ODT_WRITE,
	EMC_ODT_READ,
	EMC_FBIO_CFG5,
	EMC_CFG_DIG_DLL,
	EMC_DLL_XFORM_DQS,
	EMC_DLL_XFORM_QUSE,
	EMC_ZCAL_REF_CNT,
	EMC_ZCAL_WAIT_CNT,
	EMC_AUTO_CAL_INTERVAL,
	EMC_CFG_CLKTRIM_0,
	EMC_CFG_CLKTRIM_1,
	EMC_CFG_CLKTRIM_2,
};

struct emc_timing {
	unsigned long rate;
	u32 data[ARRAY_SIZE(emc_timing_registers)];
};

struct tegra_emc {
	struct device *dev;
	struct completion clk_handshake_complete;
	struct notifier_block clk_nb;
	struct clk *clk;
	void __iomem *regs;

	struct emc_timing *timings;
	unsigned int num_timings;

	struct {
		struct dentry *root;
		unsigned long min_rate;
		unsigned long max_rate;
	} debugfs;
};

static irqreturn_t tegra_emc_isr(int irq, void *data)
{
	struct tegra_emc *emc = data;
	u32 intmask = EMC_REFRESH_OVERFLOW_INT | EMC_CLKCHANGE_COMPLETE_INT;
	u32 status;

	status = readl_relaxed(emc->regs + EMC_INTSTATUS) & intmask;
	if (!status)
		return IRQ_NONE;

	/* notify about EMC-CAR handshake completion */
	if (status & EMC_CLKCHANGE_COMPLETE_INT)
		complete(&emc->clk_handshake_complete);

	/* notify about HW problem */
	if (status & EMC_REFRESH_OVERFLOW_INT)
		dev_err_ratelimited(emc->dev,
				    "refresh request overflow timeout\n");

	/* clear interrupts */
	writel_relaxed(status, emc->regs + EMC_INTSTATUS);

	return IRQ_HANDLED;
}

static struct emc_timing *tegra_emc_find_timing(struct tegra_emc *emc,
						unsigned long rate)
{
	struct emc_timing *timing = NULL;
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate >= rate) {
			timing = &emc->timings[i];
			break;
		}
	}

	if (!timing) {
		dev_err(emc->dev, "no timing for rate %lu\n", rate);
		return NULL;
	}

	return timing;
}

static int emc_prepare_timing_change(struct tegra_emc *emc, unsigned long rate)
{
	struct emc_timing *timing = tegra_emc_find_timing(emc, rate);
	unsigned int i;

	if (!timing)
		return -EINVAL;

	dev_dbg(emc->dev, "%s: using timing rate %lu for requested rate %lu\n",
		__func__, timing->rate, rate);

	/* program shadow registers */
	for (i = 0; i < ARRAY_SIZE(timing->data); i++)
		writel_relaxed(timing->data[i],
			       emc->regs + emc_timing_registers[i]);

	/* wait until programming has settled */
	readl_relaxed(emc->regs + emc_timing_registers[i - 1]);

	reinit_completion(&emc->clk_handshake_complete);

	return 0;
}

static int emc_complete_timing_change(struct tegra_emc *emc, bool flush)
{
	unsigned long timeout;

	dev_dbg(emc->dev, "%s: flush %d\n", __func__, flush);

	if (flush) {
		/* manually initiate memory timing update */
		writel_relaxed(EMC_TIMING_UPDATE,
			       emc->regs + EMC_TIMING_CONTROL);
		return 0;
	}

	timeout = wait_for_completion_timeout(&emc->clk_handshake_complete,
					      msecs_to_jiffies(100));
	if (timeout == 0) {
		dev_err(emc->dev, "EMC-CAR handshake failed\n");
		return -EIO;
	}

	return 0;
}

static int tegra_emc_clk_change_notify(struct notifier_block *nb,
				       unsigned long msg, void *data)
{
	struct tegra_emc *emc = container_of(nb, struct tegra_emc, clk_nb);
	struct clk_notifier_data *cnd = data;
	int err;

	switch (msg) {
	case PRE_RATE_CHANGE:
		err = emc_prepare_timing_change(emc, cnd->new_rate);
		break;

	case ABORT_RATE_CHANGE:
		err = emc_prepare_timing_change(emc, cnd->old_rate);
		if (err)
			break;

		err = emc_complete_timing_change(emc, true);
		break;

	case POST_RATE_CHANGE:
		err = emc_complete_timing_change(emc, false);
		break;

	default:
		return NOTIFY_DONE;
	}

	return notifier_from_errno(err);
}

static int load_one_timing_from_dt(struct tegra_emc *emc,
				   struct emc_timing *timing,
				   struct device_node *node)
{
	u32 rate;
	int err;

	if (!of_device_is_compatible(node, "nvidia,tegra20-emc-table")) {
		dev_err(emc->dev, "incompatible DT node: %pOF\n", node);
		return -EINVAL;
	}

	err = of_property_read_u32(node, "clock-frequency", &rate);
	if (err) {
		dev_err(emc->dev, "timing %pOF: failed to read rate: %d\n",
			node, err);
		return err;
	}

	err = of_property_read_u32_array(node, "nvidia,emc-registers",
					 timing->data,
					 ARRAY_SIZE(emc_timing_registers));
	if (err) {
		dev_err(emc->dev,
			"timing %pOF: failed to read emc timing data: %d\n",
			node, err);
		return err;
	}

	/*
	 * The EMC clock rate is twice the bus rate, and the bus rate is
	 * measured in kHz.
	 */
	timing->rate = rate * 2 * 1000;

	dev_dbg(emc->dev, "%s: %pOF: EMC rate %lu\n",
		__func__, node, timing->rate);

	return 0;
}

static int cmp_timings(const void *_a, const void *_b)
{
	const struct emc_timing *a = _a;
	const struct emc_timing *b = _b;

	if (a->rate < b->rate)
		return -1;

	if (a->rate > b->rate)
		return 1;

	return 0;
}

static int tegra_emc_load_timings_from_dt(struct tegra_emc *emc,
					  struct device_node *node)
{
	struct device_node *child;
	struct emc_timing *timing;
	int child_count;
	int err;

	child_count = of_get_child_count(node);
	if (!child_count) {
		dev_err(emc->dev, "no memory timings in DT node: %pOF\n", node);
		return -EINVAL;
	}

	emc->timings = devm_kcalloc(emc->dev, child_count, sizeof(*timing),
				    GFP_KERNEL);
	if (!emc->timings)
		return -ENOMEM;

	emc->num_timings = child_count;
	timing = emc->timings;

	for_each_child_of_node(node, child) {
		err = load_one_timing_from_dt(emc, timing++, child);
		if (err) {
			of_node_put(child);
			return err;
		}
	}

	sort(emc->timings, emc->num_timings, sizeof(*timing), cmp_timings,
	     NULL);

	dev_info(emc->dev,
		 "got %u timings for RAM code %u (min %luMHz max %luMHz)\n",
		 emc->num_timings,
		 tegra_read_ram_code(),
		 emc->timings[0].rate / 1000000,
		 emc->timings[emc->num_timings - 1].rate / 1000000);

	return 0;
}

static struct device_node *
tegra_emc_find_node_by_ram_code(struct device *dev)
{
	struct device_node *np;
	u32 value, ram_code;
	int err;

	if (!of_property_read_bool(dev->of_node, "nvidia,use-ram-code"))
		return of_node_get(dev->of_node);

	ram_code = tegra_read_ram_code();

	for (np = of_find_node_by_name(dev->of_node, "emc-tables"); np;
	     np = of_find_node_by_name(np, "emc-tables")) {
		err = of_property_read_u32(np, "nvidia,ram-code", &value);
		if (err || value != ram_code) {
			of_node_put(np);
			continue;
		}

		return np;
	}

	dev_err(dev, "no memory timings for RAM code %u found in device tree\n",
		ram_code);

	return NULL;
}

static int emc_setup_hw(struct tegra_emc *emc)
{
	u32 intmask = EMC_REFRESH_OVERFLOW_INT | EMC_CLKCHANGE_COMPLETE_INT;
	u32 emc_cfg, emc_dbg;

	emc_cfg = readl_relaxed(emc->regs + EMC_CFG_2);

	/*
	 * Depending on a memory type, DRAM should enter either self-refresh
	 * or power-down state on EMC clock change.
	 */
	if (!(emc_cfg & EMC_CLKCHANGE_PD_ENABLE) &&
	    !(emc_cfg & EMC_CLKCHANGE_SR_ENABLE)) {
		dev_err(emc->dev,
			"bootloader didn't specify DRAM auto-suspend mode\n");
		return -EINVAL;
	}

	/* enable EMC and CAR to handshake on PLL divider/source changes */
	emc_cfg |= EMC_CLKCHANGE_REQ_ENABLE;
	writel_relaxed(emc_cfg, emc->regs + EMC_CFG_2);

	/* initialize interrupt */
	writel_relaxed(intmask, emc->regs + EMC_INTMASK);
	writel_relaxed(intmask, emc->regs + EMC_INTSTATUS);

	/* ensure that unwanted debug features are disabled */
	emc_dbg = readl_relaxed(emc->regs + EMC_DBG);
	emc_dbg |= EMC_DBG_CFG_PRIORITY;
	emc_dbg &= ~EMC_DBG_READ_MUX_ASSEMBLY;
	emc_dbg &= ~EMC_DBG_WRITE_MUX_ACTIVE;
	emc_dbg &= ~EMC_DBG_FORCE_UPDATE;
	writel_relaxed(emc_dbg, emc->regs + EMC_DBG);

	return 0;
}

static long emc_round_rate(unsigned long rate,
			   unsigned long min_rate,
			   unsigned long max_rate,
			   void *arg)
{
	struct emc_timing *timing = NULL;
	struct tegra_emc *emc = arg;
	unsigned int i;

	min_rate = min(min_rate, emc->timings[emc->num_timings - 1].rate);

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate < rate && i != emc->num_timings - 1)
			continue;

		if (emc->timings[i].rate > max_rate) {
			i = max(i, 1u) - 1;

			if (emc->timings[i].rate < min_rate)
				break;
		}

		if (emc->timings[i].rate < min_rate)
			continue;

		timing = &emc->timings[i];
		break;
	}

	if (!timing) {
		dev_err(emc->dev, "no timing for rate %lu min %lu max %lu\n",
			rate, min_rate, max_rate);
		return -EINVAL;
	}

	return timing->rate;
}

/*
 * debugfs interface
 *
 * The memory controller driver exposes some files in debugfs that can be used
 * to control the EMC frequency. The top-level directory can be found here:
 *
 *   /sys/kernel/debug/emc
 *
 * It contains the following files:
 *
 *   - available_rates: This file contains a list of valid, space-separated
 *     EMC frequencies.
 *
 *   - min_rate: Writing a value to this file sets the given frequency as the
 *       floor of the permitted range. If this is higher than the currently
 *       configured EMC frequency, this will cause the frequency to be
 *       increased so that it stays within the valid range.
 *
 *   - max_rate: Similarily to the min_rate file, writing a value to this file
 *       sets the given frequency as the ceiling of the permitted range. If
 *       the value is lower than the currently configured EMC frequency, this
 *       will cause the frequency to be decreased so that it stays within the
 *       valid range.
 */

static bool tegra_emc_validate_rate(struct tegra_emc *emc, unsigned long rate)
{
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++)
		if (rate == emc->timings[i].rate)
			return true;

	return false;
}

static int tegra_emc_debug_available_rates_show(struct seq_file *s, void *data)
{
	struct tegra_emc *emc = s->private;
	const char *prefix = "";
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++) {
		seq_printf(s, "%s%lu", prefix, emc->timings[i].rate);
		prefix = " ";
	}

	seq_puts(s, "\n");

	return 0;
}

static int tegra_emc_debug_available_rates_open(struct inode *inode,
						struct file *file)
{
	return single_open(file, tegra_emc_debug_available_rates_show,
			   inode->i_private);
}

static const struct file_operations tegra_emc_debug_available_rates_fops = {
	.open = tegra_emc_debug_available_rates_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int tegra_emc_debug_min_rate_get(void *data, u64 *rate)
{
	struct tegra_emc *emc = data;

	*rate = emc->debugfs.min_rate;

	return 0;
}

static int tegra_emc_debug_min_rate_set(void *data, u64 rate)
{
	struct tegra_emc *emc = data;
	int err;

	if (!tegra_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = clk_set_min_rate(emc->clk, rate);
	if (err < 0)
		return err;

	emc->debugfs.min_rate = rate;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(tegra_emc_debug_min_rate_fops,
			tegra_emc_debug_min_rate_get,
			tegra_emc_debug_min_rate_set, "%llu\n");

static int tegra_emc_debug_max_rate_get(void *data, u64 *rate)
{
	struct tegra_emc *emc = data;

	*rate = emc->debugfs.max_rate;

	return 0;
}

static int tegra_emc_debug_max_rate_set(void *data, u64 rate)
{
	struct tegra_emc *emc = data;
	int err;

	if (!tegra_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = clk_set_max_rate(emc->clk, rate);
	if (err < 0)
		return err;

	emc->debugfs.max_rate = rate;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(tegra_emc_debug_max_rate_fops,
			tegra_emc_debug_max_rate_get,
			tegra_emc_debug_max_rate_set, "%llu\n");

static void tegra_emc_debugfs_init(struct tegra_emc *emc)
{
	struct device *dev = emc->dev;
	unsigned int i;
	int err;

	emc->debugfs.min_rate = ULONG_MAX;
	emc->debugfs.max_rate = 0;

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate < emc->debugfs.min_rate)
			emc->debugfs.min_rate = emc->timings[i].rate;

		if (emc->timings[i].rate > emc->debugfs.max_rate)
			emc->debugfs.max_rate = emc->timings[i].rate;
	}

	if (!emc->num_timings) {
		emc->debugfs.min_rate = clk_get_rate(emc->clk);
		emc->debugfs.max_rate = emc->debugfs.min_rate;
	}

	err = clk_set_rate_range(emc->clk, emc->debugfs.min_rate,
				 emc->debugfs.max_rate);
	if (err < 0) {
		dev_err(dev, "failed to set rate range [%lu-%lu] for %pC\n",
			emc->debugfs.min_rate, emc->debugfs.max_rate,
			emc->clk);
	}

	emc->debugfs.root = debugfs_create_dir("emc", NULL);
	if (!emc->debugfs.root) {
		dev_err(emc->dev, "failed to create debugfs directory\n");
		return;
	}

	debugfs_create_file("available_rates", S_IRUGO, emc->debugfs.root,
			    emc, &tegra_emc_debug_available_rates_fops);
	debugfs_create_file("min_rate", S_IRUGO | S_IWUSR, emc->debugfs.root,
			    emc, &tegra_emc_debug_min_rate_fops);
	debugfs_create_file("max_rate", S_IRUGO | S_IWUSR, emc->debugfs.root,
			    emc, &tegra_emc_debug_max_rate_fops);
}

static int tegra_emc_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct tegra_emc *emc;
	struct resource *res;
	int irq, err;

	/* driver has nothing to do in a case of memory timing absence */
	if (of_get_child_count(pdev->dev.of_node) == 0) {
		dev_info(&pdev->dev,
			 "EMC device tree node doesn't have memory timings\n");
		return 0;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "interrupt not specified\n");
		dev_err(&pdev->dev, "please update your device tree\n");
		return irq;
	}

	np = tegra_emc_find_node_by_ram_code(&pdev->dev);
	if (!np)
		return -EINVAL;

	emc = devm_kzalloc(&pdev->dev, sizeof(*emc), GFP_KERNEL);
	if (!emc) {
		of_node_put(np);
		return -ENOMEM;
	}

	init_completion(&emc->clk_handshake_complete);
	emc->clk_nb.notifier_call = tegra_emc_clk_change_notify;
	emc->dev = &pdev->dev;

	err = tegra_emc_load_timings_from_dt(emc, np);
	of_node_put(np);
	if (err)
		return err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	emc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(emc->regs))
		return PTR_ERR(emc->regs);

	err = emc_setup_hw(emc);
	if (err)
		return err;

	err = devm_request_irq(&pdev->dev, irq, tegra_emc_isr, 0,
			       dev_name(&pdev->dev), emc);
	if (err) {
		dev_err(&pdev->dev, "failed to request IRQ#%u: %d\n", irq, err);
		return err;
	}

	tegra20_clk_set_emc_round_callback(emc_round_rate, emc);

	emc->clk = devm_clk_get(&pdev->dev, "emc");
	if (IS_ERR(emc->clk)) {
		err = PTR_ERR(emc->clk);
		dev_err(&pdev->dev, "failed to get emc clock: %d\n", err);
		goto unset_cb;
	}

	err = clk_notifier_register(emc->clk, &emc->clk_nb);
	if (err) {
		dev_err(&pdev->dev, "failed to register clk notifier: %d\n",
			err);
		goto unset_cb;
	}

	platform_set_drvdata(pdev, emc);
	tegra_emc_debugfs_init(emc);

	return 0;

unset_cb:
	tegra20_clk_set_emc_round_callback(NULL, NULL);

	return err;
}

static const struct of_device_id tegra_emc_of_match[] = {
	{ .compatible = "nvidia,tegra20-emc", },
	{},
};

static struct platform_driver tegra_emc_driver = {
	.probe = tegra_emc_probe,
	.driver = {
		.name = "tegra20-emc",
		.of_match_table = tegra_emc_of_match,
		.suppress_bind_attrs = true,
	},
};

static int __init tegra_emc_init(void)
{
	return platform_driver_register(&tegra_emc_driver);
}
subsys_initcall(tegra_emc_init);
