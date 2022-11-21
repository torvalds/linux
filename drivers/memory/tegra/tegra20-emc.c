// SPDX-License-Identifier: GPL-2.0
/*
 * Tegra20 External Memory Controller driver
 *
 * Author: Dmitry Osipenko <digetx@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/debugfs.h>
#include <linux/devfreq.h>
#include <linux/err.h>
#include <linux/interconnect-provider.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/types.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

#include "../jedec_ddr.h"
#include "../of_memory.h"

#include "mc.h"

#define EMC_INTSTATUS				0x000
#define EMC_INTMASK				0x004
#define EMC_DBG					0x008
#define EMC_ADR_CFG_0				0x010
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
#define EMC_MRR					0x0ec
#define EMC_FBIO_CFG5				0x104
#define EMC_FBIO_CFG6				0x114
#define EMC_STAT_CONTROL			0x160
#define EMC_STAT_LLMC_CONTROL			0x178
#define EMC_STAT_PWR_CLOCK_LIMIT		0x198
#define EMC_STAT_PWR_CLOCKS			0x19c
#define EMC_STAT_PWR_COUNT			0x1a0
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
#define EMC_MRR_DIVLD_INT			BIT(5)

#define EMC_DBG_READ_MUX_ASSEMBLY		BIT(0)
#define EMC_DBG_WRITE_MUX_ACTIVE		BIT(1)
#define EMC_DBG_FORCE_UPDATE			BIT(2)
#define EMC_DBG_READ_DQM_CTRL			BIT(9)
#define EMC_DBG_CFG_PRIORITY			BIT(24)

#define EMC_FBIO_CFG5_DRAM_WIDTH_X16		BIT(4)
#define EMC_FBIO_CFG5_DRAM_TYPE			GENMASK(1, 0)

#define EMC_MRR_DEV_SELECTN			GENMASK(31, 30)
#define EMC_MRR_MRR_MA				GENMASK(23, 16)
#define EMC_MRR_MRR_DATA			GENMASK(15, 0)

#define EMC_ADR_CFG_0_EMEM_NUMDEV		GENMASK(25, 24)

#define EMC_PWR_GATHER_CLEAR			(1 << 8)
#define EMC_PWR_GATHER_DISABLE			(2 << 8)
#define EMC_PWR_GATHER_ENABLE			(3 << 8)

enum emc_dram_type {
	DRAM_TYPE_RESERVED,
	DRAM_TYPE_DDR1,
	DRAM_TYPE_LPDDR2,
	DRAM_TYPE_DDR2,
};

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

enum emc_rate_request_type {
	EMC_RATE_DEVFREQ,
	EMC_RATE_DEBUG,
	EMC_RATE_ICC,
	EMC_RATE_TYPE_MAX,
};

struct emc_rate_request {
	unsigned long min_rate;
	unsigned long max_rate;
};

struct tegra_emc {
	struct device *dev;
	struct tegra_mc *mc;
	struct icc_provider provider;
	struct notifier_block clk_nb;
	struct clk *clk;
	void __iomem *regs;
	unsigned int dram_bus_width;

	struct emc_timing *timings;
	unsigned int num_timings;

	struct {
		struct dentry *root;
		unsigned long min_rate;
		unsigned long max_rate;
	} debugfs;

	/*
	 * There are multiple sources in the EMC driver which could request
	 * a min/max clock rate, these rates are contained in this array.
	 */
	struct emc_rate_request requested_rate[EMC_RATE_TYPE_MAX];

	/* protect shared rate-change code path */
	struct mutex rate_lock;

	struct devfreq_simple_ondemand_data ondemand_data;

	/* memory chip identity information */
	union lpddr2_basic_config4 basic_conf4;
	unsigned int manufacturer_id;
	unsigned int revision_id1;
	unsigned int revision_id2;

	bool mrr_error;
};

static irqreturn_t tegra_emc_isr(int irq, void *data)
{
	struct tegra_emc *emc = data;
	u32 intmask = EMC_REFRESH_OVERFLOW_INT;
	u32 status;

	status = readl_relaxed(emc->regs + EMC_INTSTATUS) & intmask;
	if (!status)
		return IRQ_NONE;

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

	return 0;
}

static int emc_complete_timing_change(struct tegra_emc *emc, bool flush)
{
	int err;
	u32 v;

	dev_dbg(emc->dev, "%s: flush %d\n", __func__, flush);

	if (flush) {
		/* manually initiate memory timing update */
		writel_relaxed(EMC_TIMING_UPDATE,
			       emc->regs + EMC_TIMING_CONTROL);
		return 0;
	}

	err = readl_relaxed_poll_timeout_atomic(emc->regs + EMC_INTSTATUS, v,
						v & EMC_CLKCHANGE_COMPLETE_INT,
						1, 100);
	if (err) {
		dev_err(emc->dev, "emc-car handshake timeout: %d\n", err);
		return err;
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

	timing = emc->timings;

	for_each_child_of_node(node, child) {
		if (of_node_name_eq(child, "lpddr2"))
			continue;

		err = load_one_timing_from_dt(emc, timing++, child);
		if (err) {
			of_node_put(child);
			return err;
		}

		emc->num_timings++;
	}

	sort(emc->timings, emc->num_timings, sizeof(*timing), cmp_timings,
	     NULL);

	dev_info_once(emc->dev,
		      "got %u timings for RAM code %u (min %luMHz max %luMHz)\n",
		      emc->num_timings,
		      tegra_read_ram_code(),
		      emc->timings[0].rate / 1000000,
		      emc->timings[emc->num_timings - 1].rate / 1000000);

	return 0;
}

static struct device_node *
tegra_emc_find_node_by_ram_code(struct tegra_emc *emc)
{
	struct device *dev = emc->dev;
	struct device_node *np;
	u32 value, ram_code;
	int err;

	if (emc->mrr_error) {
		dev_warn(dev, "memory timings skipped due to MRR error\n");
		return NULL;
	}

	if (of_get_child_count(dev->of_node) == 0) {
		dev_info_once(dev, "device-tree doesn't have memory timings\n");
		return NULL;
	}

	if (!of_property_read_bool(dev->of_node, "nvidia,use-ram-code"))
		return of_node_get(dev->of_node);

	ram_code = tegra_read_ram_code();

	for (np = of_find_node_by_name(dev->of_node, "emc-tables"); np;
	     np = of_find_node_by_name(np, "emc-tables")) {
		err = of_property_read_u32(np, "nvidia,ram-code", &value);
		if (err || value != ram_code) {
			struct device_node *lpddr2_np;
			bool cfg_mismatches = false;

			lpddr2_np = of_find_node_by_name(np, "lpddr2");
			if (lpddr2_np) {
				const struct lpddr2_info *info;

				info = of_lpddr2_get_info(lpddr2_np, dev);
				if (info) {
					if (info->manufacturer_id >= 0 &&
					    info->manufacturer_id != emc->manufacturer_id)
						cfg_mismatches = true;

					if (info->revision_id1 >= 0 &&
					    info->revision_id1 != emc->revision_id1)
						cfg_mismatches = true;

					if (info->revision_id2 >= 0 &&
					    info->revision_id2 != emc->revision_id2)
						cfg_mismatches = true;

					if (info->density != emc->basic_conf4.density)
						cfg_mismatches = true;

					if (info->io_width != emc->basic_conf4.io_width)
						cfg_mismatches = true;

					if (info->arch_type != emc->basic_conf4.arch_type)
						cfg_mismatches = true;
				} else {
					dev_err(dev, "failed to parse %pOF\n", lpddr2_np);
					cfg_mismatches = true;
				}

				of_node_put(lpddr2_np);
			} else {
				cfg_mismatches = true;
			}

			if (cfg_mismatches) {
				of_node_put(np);
				continue;
			}
		}

		return np;
	}

	dev_err(dev, "no memory timings for RAM code %u found in device tree\n",
		ram_code);

	return NULL;
}

static int emc_read_lpddr_mode_register(struct tegra_emc *emc,
					unsigned int emem_dev,
					unsigned int register_addr,
					unsigned int *register_data)
{
	u32 memory_dev = emem_dev ? 1 : 2;
	u32 val, mr_mask = 0xff;
	int err;

	/* clear data-valid interrupt status */
	writel_relaxed(EMC_MRR_DIVLD_INT, emc->regs + EMC_INTSTATUS);

	/* issue mode register read request */
	val  = FIELD_PREP(EMC_MRR_DEV_SELECTN, memory_dev);
	val |= FIELD_PREP(EMC_MRR_MRR_MA, register_addr);

	writel_relaxed(val, emc->regs + EMC_MRR);

	/* wait for the LPDDR2 data-valid interrupt */
	err = readl_relaxed_poll_timeout_atomic(emc->regs + EMC_INTSTATUS, val,
						val & EMC_MRR_DIVLD_INT,
						1, 100);
	if (err) {
		dev_err(emc->dev, "mode register %u read failed: %d\n",
			register_addr, err);
		emc->mrr_error = true;
		return err;
	}

	/* read out mode register data */
	val = readl_relaxed(emc->regs + EMC_MRR);
	*register_data = FIELD_GET(EMC_MRR_MRR_DATA, val) & mr_mask;

	return 0;
}

static void emc_read_lpddr_sdram_info(struct tegra_emc *emc,
				      unsigned int emem_dev,
				      bool print_out)
{
	/* these registers are standard for all LPDDR JEDEC memory chips */
	emc_read_lpddr_mode_register(emc, emem_dev, 5, &emc->manufacturer_id);
	emc_read_lpddr_mode_register(emc, emem_dev, 6, &emc->revision_id1);
	emc_read_lpddr_mode_register(emc, emem_dev, 7, &emc->revision_id2);
	emc_read_lpddr_mode_register(emc, emem_dev, 8, &emc->basic_conf4.value);

	if (!print_out)
		return;

	dev_info(emc->dev, "SDRAM[dev%u]: manufacturer: 0x%x (%s) rev1: 0x%x rev2: 0x%x prefetch: S%u density: %uMbit iowidth: %ubit\n",
		 emem_dev, emc->manufacturer_id,
		 lpddr2_jedec_manufacturer(emc->manufacturer_id),
		 emc->revision_id1, emc->revision_id2,
		 4 >> emc->basic_conf4.arch_type,
		 64 << emc->basic_conf4.density,
		 32 >> emc->basic_conf4.io_width);
}

static int emc_setup_hw(struct tegra_emc *emc)
{
	u32 emc_cfg, emc_dbg, emc_fbio, emc_adr_cfg;
	u32 intmask = EMC_REFRESH_OVERFLOW_INT;
	static bool print_sdram_info_once;
	enum emc_dram_type dram_type;
	const char *dram_type_str;
	unsigned int emem_numdev;

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

	emc_fbio = readl_relaxed(emc->regs + EMC_FBIO_CFG5);

	if (emc_fbio & EMC_FBIO_CFG5_DRAM_WIDTH_X16)
		emc->dram_bus_width = 16;
	else
		emc->dram_bus_width = 32;

	dram_type = FIELD_GET(EMC_FBIO_CFG5_DRAM_TYPE, emc_fbio);

	switch (dram_type) {
	case DRAM_TYPE_RESERVED:
		dram_type_str = "INVALID";
		break;
	case DRAM_TYPE_DDR1:
		dram_type_str = "DDR1";
		break;
	case DRAM_TYPE_LPDDR2:
		dram_type_str = "LPDDR2";
		break;
	case DRAM_TYPE_DDR2:
		dram_type_str = "DDR2";
		break;
	}

	emc_adr_cfg = readl_relaxed(emc->regs + EMC_ADR_CFG_0);
	emem_numdev = FIELD_GET(EMC_ADR_CFG_0_EMEM_NUMDEV, emc_adr_cfg) + 1;

	dev_info_once(emc->dev, "%ubit DRAM bus, %u %s %s attached\n",
		      emc->dram_bus_width, emem_numdev, dram_type_str,
		      emem_numdev == 2 ? "devices" : "device");

	if (dram_type == DRAM_TYPE_LPDDR2) {
		while (emem_numdev--)
			emc_read_lpddr_sdram_info(emc, emem_numdev,
						  !print_sdram_info_once);
		print_sdram_info_once = true;
	}

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

	if (!emc->num_timings)
		return clk_get_rate(emc->clk);

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

static void tegra_emc_rate_requests_init(struct tegra_emc *emc)
{
	unsigned int i;

	for (i = 0; i < EMC_RATE_TYPE_MAX; i++) {
		emc->requested_rate[i].min_rate = 0;
		emc->requested_rate[i].max_rate = ULONG_MAX;
	}
}

static int emc_request_rate(struct tegra_emc *emc,
			    unsigned long new_min_rate,
			    unsigned long new_max_rate,
			    enum emc_rate_request_type type)
{
	struct emc_rate_request *req = emc->requested_rate;
	unsigned long min_rate = 0, max_rate = ULONG_MAX;
	unsigned int i;
	int err;

	/* select minimum and maximum rates among the requested rates */
	for (i = 0; i < EMC_RATE_TYPE_MAX; i++, req++) {
		if (i == type) {
			min_rate = max(new_min_rate, min_rate);
			max_rate = min(new_max_rate, max_rate);
		} else {
			min_rate = max(req->min_rate, min_rate);
			max_rate = min(req->max_rate, max_rate);
		}
	}

	if (min_rate > max_rate) {
		dev_err_ratelimited(emc->dev, "%s: type %u: out of range: %lu %lu\n",
				    __func__, type, min_rate, max_rate);
		return -ERANGE;
	}

	/*
	 * EMC rate-changes should go via OPP API because it manages voltage
	 * changes.
	 */
	err = dev_pm_opp_set_rate(emc->dev, min_rate);
	if (err)
		return err;

	emc->requested_rate[type].min_rate = new_min_rate;
	emc->requested_rate[type].max_rate = new_max_rate;

	return 0;
}

static int emc_set_min_rate(struct tegra_emc *emc, unsigned long rate,
			    enum emc_rate_request_type type)
{
	struct emc_rate_request *req = &emc->requested_rate[type];
	int ret;

	mutex_lock(&emc->rate_lock);
	ret = emc_request_rate(emc, rate, req->max_rate, type);
	mutex_unlock(&emc->rate_lock);

	return ret;
}

static int emc_set_max_rate(struct tegra_emc *emc, unsigned long rate,
			    enum emc_rate_request_type type)
{
	struct emc_rate_request *req = &emc->requested_rate[type];
	int ret;

	mutex_lock(&emc->rate_lock);
	ret = emc_request_rate(emc, req->min_rate, rate, type);
	mutex_unlock(&emc->rate_lock);

	return ret;
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

	err = emc_set_min_rate(emc, rate, EMC_RATE_DEBUG);
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

	err = emc_set_max_rate(emc, rate, EMC_RATE_DEBUG);
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

	debugfs_create_file("available_rates", 0444, emc->debugfs.root,
			    emc, &tegra_emc_debug_available_rates_fops);
	debugfs_create_file("min_rate", 0644, emc->debugfs.root,
			    emc, &tegra_emc_debug_min_rate_fops);
	debugfs_create_file("max_rate", 0644, emc->debugfs.root,
			    emc, &tegra_emc_debug_max_rate_fops);
}

static inline struct tegra_emc *
to_tegra_emc_provider(struct icc_provider *provider)
{
	return container_of(provider, struct tegra_emc, provider);
}

static struct icc_node_data *
emc_of_icc_xlate_extended(struct of_phandle_args *spec, void *data)
{
	struct icc_provider *provider = data;
	struct icc_node_data *ndata;
	struct icc_node *node;

	/* External Memory is the only possible ICC route */
	list_for_each_entry(node, &provider->nodes, node_list) {
		if (node->id != TEGRA_ICC_EMEM)
			continue;

		ndata = kzalloc(sizeof(*ndata), GFP_KERNEL);
		if (!ndata)
			return ERR_PTR(-ENOMEM);

		/*
		 * SRC and DST nodes should have matching TAG in order to have
		 * it set by default for a requested path.
		 */
		ndata->tag = TEGRA_MC_ICC_TAG_ISO;
		ndata->node = node;

		return ndata;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static int emc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct tegra_emc *emc = to_tegra_emc_provider(dst->provider);
	unsigned long long peak_bw = icc_units_to_bps(dst->peak_bw);
	unsigned long long avg_bw = icc_units_to_bps(dst->avg_bw);
	unsigned long long rate = max(avg_bw, peak_bw);
	unsigned int dram_data_bus_width_bytes;
	int err;

	/*
	 * Tegra20 EMC runs on x2 clock rate of SDRAM bus because DDR data
	 * is sampled on both clock edges.  This means that EMC clock rate
	 * equals to the peak data-rate.
	 */
	dram_data_bus_width_bytes = emc->dram_bus_width / 8;
	do_div(rate, dram_data_bus_width_bytes);
	rate = min_t(u64, rate, U32_MAX);

	err = emc_set_min_rate(emc, rate, EMC_RATE_ICC);
	if (err)
		return err;

	return 0;
}

static int tegra_emc_interconnect_init(struct tegra_emc *emc)
{
	const struct tegra_mc_soc *soc;
	struct icc_node *node;
	int err;

	emc->mc = devm_tegra_memory_controller_get(emc->dev);
	if (IS_ERR(emc->mc))
		return PTR_ERR(emc->mc);

	soc = emc->mc->soc;

	emc->provider.dev = emc->dev;
	emc->provider.set = emc_icc_set;
	emc->provider.data = &emc->provider;
	emc->provider.aggregate = soc->icc_ops->aggregate;
	emc->provider.xlate_extended = emc_of_icc_xlate_extended;

	err = icc_provider_add(&emc->provider);
	if (err)
		goto err_msg;

	/* create External Memory Controller node */
	node = icc_node_create(TEGRA_ICC_EMC);
	if (IS_ERR(node)) {
		err = PTR_ERR(node);
		goto del_provider;
	}

	node->name = "External Memory Controller";
	icc_node_add(node, &emc->provider);

	/* link External Memory Controller to External Memory (DRAM) */
	err = icc_link_create(node, TEGRA_ICC_EMEM);
	if (err)
		goto remove_nodes;

	/* create External Memory node */
	node = icc_node_create(TEGRA_ICC_EMEM);
	if (IS_ERR(node)) {
		err = PTR_ERR(node);
		goto remove_nodes;
	}

	node->name = "External Memory (DRAM)";
	icc_node_add(node, &emc->provider);

	return 0;

remove_nodes:
	icc_nodes_remove(&emc->provider);
del_provider:
	icc_provider_del(&emc->provider);
err_msg:
	dev_err(emc->dev, "failed to initialize ICC: %d\n", err);

	return err;
}

static void devm_tegra_emc_unset_callback(void *data)
{
	tegra20_clk_set_emc_round_callback(NULL, NULL);
}

static void devm_tegra_emc_unreg_clk_notifier(void *data)
{
	struct tegra_emc *emc = data;

	clk_notifier_unregister(emc->clk, &emc->clk_nb);
}

static int tegra_emc_init_clk(struct tegra_emc *emc)
{
	int err;

	tegra20_clk_set_emc_round_callback(emc_round_rate, emc);

	err = devm_add_action_or_reset(emc->dev, devm_tegra_emc_unset_callback,
				       NULL);
	if (err)
		return err;

	emc->clk = devm_clk_get(emc->dev, NULL);
	if (IS_ERR(emc->clk)) {
		dev_err(emc->dev, "failed to get EMC clock: %pe\n", emc->clk);
		return PTR_ERR(emc->clk);
	}

	err = clk_notifier_register(emc->clk, &emc->clk_nb);
	if (err) {
		dev_err(emc->dev, "failed to register clk notifier: %d\n", err);
		return err;
	}

	err = devm_add_action_or_reset(emc->dev,
				       devm_tegra_emc_unreg_clk_notifier, emc);
	if (err)
		return err;

	return 0;
}

static int tegra_emc_devfreq_target(struct device *dev, unsigned long *freq,
				    u32 flags)
{
	struct tegra_emc *emc = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long rate;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		dev_err(dev, "failed to find opp for %lu Hz\n", *freq);
		return PTR_ERR(opp);
	}

	rate = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);

	return emc_set_min_rate(emc, rate, EMC_RATE_DEVFREQ);
}

static int tegra_emc_devfreq_get_dev_status(struct device *dev,
					    struct devfreq_dev_status *stat)
{
	struct tegra_emc *emc = dev_get_drvdata(dev);

	/* freeze counters */
	writel_relaxed(EMC_PWR_GATHER_DISABLE, emc->regs + EMC_STAT_CONTROL);

	/*
	 *  busy_time: number of clocks EMC request was accepted
	 * total_time: number of clocks PWR_GATHER control was set to ENABLE
	 */
	stat->busy_time = readl_relaxed(emc->regs + EMC_STAT_PWR_COUNT);
	stat->total_time = readl_relaxed(emc->regs + EMC_STAT_PWR_CLOCKS);
	stat->current_frequency = clk_get_rate(emc->clk);

	/* clear counters and restart */
	writel_relaxed(EMC_PWR_GATHER_CLEAR, emc->regs + EMC_STAT_CONTROL);
	writel_relaxed(EMC_PWR_GATHER_ENABLE, emc->regs + EMC_STAT_CONTROL);

	return 0;
}

static struct devfreq_dev_profile tegra_emc_devfreq_profile = {
	.polling_ms = 30,
	.target = tegra_emc_devfreq_target,
	.get_dev_status = tegra_emc_devfreq_get_dev_status,
};

static int tegra_emc_devfreq_init(struct tegra_emc *emc)
{
	struct devfreq *devfreq;

	/*
	 * PWR_COUNT is 1/2 of PWR_CLOCKS at max, and thus, the up-threshold
	 * should be less than 50.  Secondly, multiple active memory clients
	 * may cause over 20% of lost clock cycles due to stalls caused by
	 * competing memory accesses.  This means that threshold should be
	 * set to a less than 30 in order to have a properly working governor.
	 */
	emc->ondemand_data.upthreshold = 20;

	/*
	 * Reset statistic gathers state, select global bandwidth for the
	 * statistics collection mode and set clocks counter saturation
	 * limit to maximum.
	 */
	writel_relaxed(0x00000000, emc->regs + EMC_STAT_CONTROL);
	writel_relaxed(0x00000000, emc->regs + EMC_STAT_LLMC_CONTROL);
	writel_relaxed(0xffffffff, emc->regs + EMC_STAT_PWR_CLOCK_LIMIT);

	devfreq = devm_devfreq_add_device(emc->dev, &tegra_emc_devfreq_profile,
					  DEVFREQ_GOV_SIMPLE_ONDEMAND,
					  &emc->ondemand_data);
	if (IS_ERR(devfreq)) {
		dev_err(emc->dev, "failed to initialize devfreq: %pe", devfreq);
		return PTR_ERR(devfreq);
	}

	return 0;
}

static int tegra_emc_probe(struct platform_device *pdev)
{
	struct tegra_core_opp_params opp_params = {};
	struct device_node *np;
	struct tegra_emc *emc;
	int irq, err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "please update your device tree\n");
		return irq;
	}

	emc = devm_kzalloc(&pdev->dev, sizeof(*emc), GFP_KERNEL);
	if (!emc)
		return -ENOMEM;

	mutex_init(&emc->rate_lock);
	emc->clk_nb.notifier_call = tegra_emc_clk_change_notify;
	emc->dev = &pdev->dev;

	emc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(emc->regs))
		return PTR_ERR(emc->regs);

	err = emc_setup_hw(emc);
	if (err)
		return err;

	np = tegra_emc_find_node_by_ram_code(emc);
	if (np) {
		err = tegra_emc_load_timings_from_dt(emc, np);
		of_node_put(np);
		if (err)
			return err;
	}

	err = devm_request_irq(&pdev->dev, irq, tegra_emc_isr, 0,
			       dev_name(&pdev->dev), emc);
	if (err) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", err);
		return err;
	}

	err = tegra_emc_init_clk(emc);
	if (err)
		return err;

	opp_params.init_state = true;

	err = devm_tegra_core_dev_init_opp_table(&pdev->dev, &opp_params);
	if (err)
		return err;

	platform_set_drvdata(pdev, emc);
	tegra_emc_rate_requests_init(emc);
	tegra_emc_debugfs_init(emc);
	tegra_emc_interconnect_init(emc);
	tegra_emc_devfreq_init(emc);

	/*
	 * Don't allow the kernel module to be unloaded. Unloading adds some
	 * extra complexity which doesn't really worth the effort in a case of
	 * this driver.
	 */
	try_module_get(THIS_MODULE);

	return 0;
}

static const struct of_device_id tegra_emc_of_match[] = {
	{ .compatible = "nvidia,tegra20-emc", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_emc_of_match);

static struct platform_driver tegra_emc_driver = {
	.probe = tegra_emc_probe,
	.driver = {
		.name = "tegra20-emc",
		.of_match_table = tegra_emc_of_match,
		.suppress_bind_attrs = true,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(tegra_emc_driver);

MODULE_AUTHOR("Dmitry Osipenko <digetx@gmail.com>");
MODULE_DESCRIPTION("NVIDIA Tegra20 EMC driver");
MODULE_SOFTDEP("pre: governor_simpleondemand");
MODULE_LICENSE("GPL v2");
