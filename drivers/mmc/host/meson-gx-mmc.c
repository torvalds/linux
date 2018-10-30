/*
 * Amlogic SD/eMMC driver for the GX/S905 family SoCs
 *
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Kevin Hilman <khilman@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/interrupt.h>
#include <linux/bitfield.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_NAME "meson-gx-mmc"

#define SD_EMMC_CLOCK 0x0
#define   CLK_DIV_MASK GENMASK(5, 0)
#define   CLK_SRC_MASK GENMASK(7, 6)
#define   CLK_CORE_PHASE_MASK GENMASK(9, 8)
#define   CLK_TX_PHASE_MASK GENMASK(11, 10)
#define   CLK_RX_PHASE_MASK GENMASK(13, 12)
#define   CLK_V2_TX_DELAY_MASK GENMASK(19, 16)
#define   CLK_V2_RX_DELAY_MASK GENMASK(23, 20)
#define   CLK_V2_ALWAYS_ON BIT(24)

#define   CLK_V3_TX_DELAY_MASK GENMASK(21, 16)
#define   CLK_V3_RX_DELAY_MASK GENMASK(27, 22)
#define   CLK_V3_ALWAYS_ON BIT(28)

#define   CLK_DELAY_STEP_PS 200
#define   CLK_PHASE_STEP 30
#define   CLK_PHASE_POINT_NUM (360 / CLK_PHASE_STEP)

#define   CLK_TX_DELAY_MASK(h)		(h->data->tx_delay_mask)
#define   CLK_RX_DELAY_MASK(h)		(h->data->rx_delay_mask)
#define   CLK_ALWAYS_ON(h)		(h->data->always_on)

#define SD_EMMC_DELAY 0x4
#define SD_EMMC_ADJUST 0x8

#define SD_EMMC_DELAY1 0x4
#define SD_EMMC_DELAY2 0x8
#define SD_EMMC_V3_ADJUST 0xc

#define SD_EMMC_CALOUT 0x10
#define SD_EMMC_START 0x40
#define   START_DESC_INIT BIT(0)
#define   START_DESC_BUSY BIT(1)
#define   START_DESC_ADDR_MASK GENMASK(31, 2)

#define SD_EMMC_CFG 0x44
#define   CFG_BUS_WIDTH_MASK GENMASK(1, 0)
#define   CFG_BUS_WIDTH_1 0x0
#define   CFG_BUS_WIDTH_4 0x1
#define   CFG_BUS_WIDTH_8 0x2
#define   CFG_DDR BIT(2)
#define   CFG_BLK_LEN_MASK GENMASK(7, 4)
#define   CFG_RESP_TIMEOUT_MASK GENMASK(11, 8)
#define   CFG_RC_CC_MASK GENMASK(15, 12)
#define   CFG_STOP_CLOCK BIT(22)
#define   CFG_CLK_ALWAYS_ON BIT(18)
#define   CFG_CHK_DS BIT(20)
#define   CFG_AUTO_CLK BIT(23)

#define SD_EMMC_STATUS 0x48
#define   STATUS_BUSY BIT(31)
#define   STATUS_DATI GENMASK(23, 16)

#define SD_EMMC_IRQ_EN 0x4c
#define   IRQ_RXD_ERR_MASK GENMASK(7, 0)
#define   IRQ_TXD_ERR BIT(8)
#define   IRQ_DESC_ERR BIT(9)
#define   IRQ_RESP_ERR BIT(10)
#define   IRQ_CRC_ERR \
	(IRQ_RXD_ERR_MASK | IRQ_TXD_ERR | IRQ_DESC_ERR | IRQ_RESP_ERR)
#define   IRQ_RESP_TIMEOUT BIT(11)
#define   IRQ_DESC_TIMEOUT BIT(12)
#define   IRQ_TIMEOUTS \
	(IRQ_RESP_TIMEOUT | IRQ_DESC_TIMEOUT)
#define   IRQ_END_OF_CHAIN BIT(13)
#define   IRQ_RESP_STATUS BIT(14)
#define   IRQ_SDIO BIT(15)
#define   IRQ_EN_MASK \
	(IRQ_CRC_ERR | IRQ_TIMEOUTS | IRQ_END_OF_CHAIN | IRQ_RESP_STATUS |\
	 IRQ_SDIO)

#define SD_EMMC_CMD_CFG 0x50
#define SD_EMMC_CMD_ARG 0x54
#define SD_EMMC_CMD_DAT 0x58
#define SD_EMMC_CMD_RSP 0x5c
#define SD_EMMC_CMD_RSP1 0x60
#define SD_EMMC_CMD_RSP2 0x64
#define SD_EMMC_CMD_RSP3 0x68

#define SD_EMMC_RXD 0x94
#define SD_EMMC_TXD 0x94
#define SD_EMMC_LAST_REG SD_EMMC_TXD

#define SD_EMMC_CFG_BLK_SIZE 512 /* internal buffer max: 512 bytes */
#define SD_EMMC_CFG_RESP_TIMEOUT 256 /* in clock cycles */
#define SD_EMMC_CMD_TIMEOUT 1024 /* in ms */
#define SD_EMMC_CMD_TIMEOUT_DATA 4096 /* in ms */
#define SD_EMMC_CFG_CMD_GAP 16 /* in clock cycles */
#define SD_EMMC_DESC_BUF_LEN PAGE_SIZE

#define SD_EMMC_PRE_REQ_DONE BIT(0)
#define SD_EMMC_DESC_CHAIN_MODE BIT(1)

#define MUX_CLK_NUM_PARENTS 2

struct meson_mmc_data {
	unsigned int tx_delay_mask;
	unsigned int rx_delay_mask;
	unsigned int always_on;
};

struct sd_emmc_desc {
	u32 cmd_cfg;
	u32 cmd_arg;
	u32 cmd_data;
	u32 cmd_resp;
};

struct meson_host {
	struct	device		*dev;
	struct	meson_mmc_data *data;
	struct	mmc_host	*mmc;
	struct	mmc_command	*cmd;

	spinlock_t lock;
	void __iomem *regs;
	struct clk *core_clk;
	struct clk *mmc_clk;
	struct clk *rx_clk;
	struct clk *tx_clk;
	unsigned long req_rate;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_clk_gate;

	unsigned int bounce_buf_size;
	void *bounce_buf;
	dma_addr_t bounce_dma_addr;
	struct sd_emmc_desc *descs;
	dma_addr_t descs_dma_addr;

	bool vqmmc_enabled;
};

#define CMD_CFG_LENGTH_MASK GENMASK(8, 0)
#define CMD_CFG_BLOCK_MODE BIT(9)
#define CMD_CFG_R1B BIT(10)
#define CMD_CFG_END_OF_CHAIN BIT(11)
#define CMD_CFG_TIMEOUT_MASK GENMASK(15, 12)
#define CMD_CFG_NO_RESP BIT(16)
#define CMD_CFG_NO_CMD BIT(17)
#define CMD_CFG_DATA_IO BIT(18)
#define CMD_CFG_DATA_WR BIT(19)
#define CMD_CFG_RESP_NOCRC BIT(20)
#define CMD_CFG_RESP_128 BIT(21)
#define CMD_CFG_RESP_NUM BIT(22)
#define CMD_CFG_DATA_NUM BIT(23)
#define CMD_CFG_CMD_INDEX_MASK GENMASK(29, 24)
#define CMD_CFG_ERROR BIT(30)
#define CMD_CFG_OWNER BIT(31)

#define CMD_DATA_MASK GENMASK(31, 2)
#define CMD_DATA_BIG_ENDIAN BIT(1)
#define CMD_DATA_SRAM BIT(0)
#define CMD_RESP_MASK GENMASK(31, 1)
#define CMD_RESP_SRAM BIT(0)

struct meson_mmc_phase {
	struct clk_hw hw;
	void __iomem *reg;
	unsigned long phase_mask;
	unsigned long delay_mask;
	unsigned int delay_step_ps;
};

#define to_meson_mmc_phase(_hw) container_of(_hw, struct meson_mmc_phase, hw)

static int meson_mmc_clk_get_phase(struct clk_hw *hw)
{
	struct meson_mmc_phase *mmc = to_meson_mmc_phase(hw);
	unsigned int phase_num = 1 <<  hweight_long(mmc->phase_mask);
	unsigned long period_ps, p, d;
		int degrees;
	u32 val;

	val = readl(mmc->reg);
	p = (val & mmc->phase_mask) >> __ffs(mmc->phase_mask);
	degrees = p * 360 / phase_num;

	if (mmc->delay_mask) {
		period_ps = DIV_ROUND_UP((unsigned long)NSEC_PER_SEC * 1000,
					 clk_get_rate(hw->clk));
		d = (val & mmc->delay_mask) >> __ffs(mmc->delay_mask);
		degrees += d * mmc->delay_step_ps * 360 / period_ps;
		degrees %= 360;
	}

	return degrees;
}

static void meson_mmc_apply_phase_delay(struct meson_mmc_phase *mmc,
					unsigned int phase,
					unsigned int delay)
{
	u32 val;

	val = readl(mmc->reg);
	val &= ~mmc->phase_mask;
	val |= phase << __ffs(mmc->phase_mask);

	if (mmc->delay_mask) {
		val &= ~mmc->delay_mask;
		val |= delay << __ffs(mmc->delay_mask);
	}

	writel(val, mmc->reg);
}

static int meson_mmc_clk_set_phase(struct clk_hw *hw, int degrees)
{
	struct meson_mmc_phase *mmc = to_meson_mmc_phase(hw);
	unsigned int phase_num = 1 <<  hweight_long(mmc->phase_mask);
	unsigned long period_ps, d = 0, r;
	uint64_t p;

	p = degrees % 360;

	if (!mmc->delay_mask) {
		p = DIV_ROUND_CLOSEST_ULL(p, 360 / phase_num);
	} else {
		period_ps = DIV_ROUND_UP((unsigned long)NSEC_PER_SEC * 1000,
					 clk_get_rate(hw->clk));

		/* First compute the phase index (p), the remainder (r) is the
		 * part we'll try to acheive using the delays (d).
		 */
		r = do_div(p, 360 / phase_num);
		d = DIV_ROUND_CLOSEST(r * period_ps,
				      360 * mmc->delay_step_ps);
		d = min(d, mmc->delay_mask >> __ffs(mmc->delay_mask));
	}

	meson_mmc_apply_phase_delay(mmc, p, d);
	return 0;
}

static const struct clk_ops meson_mmc_clk_phase_ops = {
	.get_phase = meson_mmc_clk_get_phase,
	.set_phase = meson_mmc_clk_set_phase,
};

static unsigned int meson_mmc_get_timeout_msecs(struct mmc_data *data)
{
	unsigned int timeout = data->timeout_ns / NSEC_PER_MSEC;

	if (!timeout)
		return SD_EMMC_CMD_TIMEOUT_DATA;

	timeout = roundup_pow_of_two(timeout);

	return min(timeout, 32768U); /* max. 2^15 ms */
}

static struct mmc_command *meson_mmc_get_next_command(struct mmc_command *cmd)
{
	if (cmd->opcode == MMC_SET_BLOCK_COUNT && !cmd->error)
		return cmd->mrq->cmd;
	else if (mmc_op_multi(cmd->opcode) &&
		 (!cmd->mrq->sbc || cmd->error || cmd->data->error))
		return cmd->mrq->stop;
	else
		return NULL;
}

static void meson_mmc_get_transfer_mode(struct mmc_host *mmc,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	struct scatterlist *sg;
	int i;
	bool use_desc_chain_mode = true;

	/*
	 * Broken SDIO with AP6255-based WiFi on Khadas VIM Pro has been
	 * reported. For some strange reason this occurs in descriptor
	 * chain mode only. So let's fall back to bounce buffer mode
	 * for command SD_IO_RW_EXTENDED.
	 */
	if (mrq->cmd->opcode == SD_IO_RW_EXTENDED)
		return;

	for_each_sg(data->sg, sg, data->sg_len, i)
		/* check for 8 byte alignment */
		if (sg->offset & 7) {
			WARN_ONCE(1, "unaligned scatterlist buffer\n");
			use_desc_chain_mode = false;
			break;
		}

	if (use_desc_chain_mode)
		data->host_cookie |= SD_EMMC_DESC_CHAIN_MODE;
}

static inline bool meson_mmc_desc_chain_mode(const struct mmc_data *data)
{
	return data->host_cookie & SD_EMMC_DESC_CHAIN_MODE;
}

static inline bool meson_mmc_bounce_buf_read(const struct mmc_data *data)
{
	return data && data->flags & MMC_DATA_READ &&
	       !meson_mmc_desc_chain_mode(data);
}

static void meson_mmc_pre_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	meson_mmc_get_transfer_mode(mmc, mrq);
	data->host_cookie |= SD_EMMC_PRE_REQ_DONE;

	if (!meson_mmc_desc_chain_mode(data))
		return;

	data->sg_count = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len,
                                   mmc_get_dma_dir(data));
	if (!data->sg_count)
		dev_err(mmc_dev(mmc), "dma_map_sg failed");
}

static void meson_mmc_post_req(struct mmc_host *mmc, struct mmc_request *mrq,
			       int err)
{
	struct mmc_data *data = mrq->data;

	if (data && meson_mmc_desc_chain_mode(data) && data->sg_count)
		dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
}

static bool meson_mmc_timing_is_ddr(struct mmc_ios *ios)
{
	if (ios->timing == MMC_TIMING_MMC_DDR52 ||
	    ios->timing == MMC_TIMING_UHS_DDR50 ||
	    ios->timing == MMC_TIMING_MMC_HS400)
		return true;

	return false;
}

/*
 * Gating the clock on this controller is tricky.  It seems the mmc clock
 * is also used by the controller.  It may crash during some operation if the
 * clock is stopped.  The safest thing to do, whenever possible, is to keep
 * clock running at stop it at the pad using the pinmux.
 */
static void meson_mmc_clk_gate(struct meson_host *host)
{
	u32 cfg;

	if (host->pins_clk_gate) {
		pinctrl_select_state(host->pinctrl, host->pins_clk_gate);
	} else {
		/*
		 * If the pinmux is not provided - default to the classic and
		 * unsafe method
		 */
		cfg = readl(host->regs + SD_EMMC_CFG);
		cfg |= CFG_STOP_CLOCK;
		writel(cfg, host->regs + SD_EMMC_CFG);
	}
}

static void meson_mmc_clk_ungate(struct meson_host *host)
{
	u32 cfg;

	if (host->pins_clk_gate)
		pinctrl_select_state(host->pinctrl, host->pins_default);

	/* Make sure the clock is not stopped in the controller */
	cfg = readl(host->regs + SD_EMMC_CFG);
	cfg &= ~CFG_STOP_CLOCK;
	writel(cfg, host->regs + SD_EMMC_CFG);
}

static int meson_mmc_clk_set(struct meson_host *host, struct mmc_ios *ios)
{
	struct mmc_host *mmc = host->mmc;
	unsigned long rate = ios->clock;
	int ret;
	u32 cfg;

	/* DDR modes require higher module clock */
	if (meson_mmc_timing_is_ddr(ios))
		rate <<= 1;

	/* Same request - bail-out */
	if (host->req_rate == rate)
		return 0;

	/* stop clock */
	meson_mmc_clk_gate(host);
	host->req_rate = 0;

	if (!rate) {
		mmc->actual_clock = 0;
		/* return with clock being stopped */
		return 0;
	}

	/* Stop the clock during rate change to avoid glitches */
	cfg = readl(host->regs + SD_EMMC_CFG);
	cfg |= CFG_STOP_CLOCK;
	writel(cfg, host->regs + SD_EMMC_CFG);

	ret = clk_set_rate(host->mmc_clk, rate);
	if (ret) {
		dev_err(host->dev, "Unable to set cfg_div_clk to %lu. ret=%d\n",
			rate, ret);
		return ret;
	}

	host->req_rate = rate;
	mmc->actual_clock = clk_get_rate(host->mmc_clk);

	/* We should report the real output frequency of the controller */
	if (meson_mmc_timing_is_ddr(ios))
		mmc->actual_clock >>= 1;

	dev_dbg(host->dev, "clk rate: %u Hz\n", mmc->actual_clock);
	if (ios->clock != mmc->actual_clock)
		dev_dbg(host->dev, "requested rate was %u\n", ios->clock);

	/* (re)start clock */
	meson_mmc_clk_ungate(host);

	return 0;
}

/*
 * The SD/eMMC IP block has an internal mux and divider used for
 * generating the MMC clock.  Use the clock framework to create and
 * manage these clocks.
 */
static int meson_mmc_clk_init(struct meson_host *host)
{
	struct clk_init_data init;
	struct clk_mux *mux;
	struct clk_divider *div;
	struct meson_mmc_phase *core, *tx, *rx;
	struct clk *clk;
	char clk_name[32];
	int i, ret = 0;
	const char *mux_parent_names[MUX_CLK_NUM_PARENTS];
	const char *clk_parent[1];
	u32 clk_reg;

	/* init SD_EMMC_CLOCK to sane defaults w/min clock rate */
	clk_reg = 0;
	clk_reg |= CLK_ALWAYS_ON(host);
	clk_reg |= CLK_DIV_MASK;
	writel(clk_reg, host->regs + SD_EMMC_CLOCK);

	/* get the mux parents */
	for (i = 0; i < MUX_CLK_NUM_PARENTS; i++) {
		struct clk *clk;
		char name[16];

		snprintf(name, sizeof(name), "clkin%d", i);
		clk = devm_clk_get(host->dev, name);
		if (IS_ERR(clk)) {
			if (clk != ERR_PTR(-EPROBE_DEFER))
				dev_err(host->dev, "Missing clock %s\n", name);
			return PTR_ERR(clk);
		}

		mux_parent_names[i] = __clk_get_name(clk);
	}

	/* create the mux */
	mux = devm_kzalloc(host->dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s#mux", dev_name(host->dev));
	init.name = clk_name;
	init.ops = &clk_mux_ops;
	init.flags = 0;
	init.parent_names = mux_parent_names;
	init.num_parents = MUX_CLK_NUM_PARENTS;

	mux->reg = host->regs + SD_EMMC_CLOCK;
	mux->shift = __ffs(CLK_SRC_MASK);
	mux->mask = CLK_SRC_MASK >> mux->shift;
	mux->hw.init = &init;

	clk = devm_clk_register(host->dev, &mux->hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	/* create the divider */
	div = devm_kzalloc(host->dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s#div", dev_name(host->dev));
	init.name = clk_name;
	init.ops = &clk_divider_ops;
	init.flags = CLK_SET_RATE_PARENT;
	clk_parent[0] = __clk_get_name(clk);
	init.parent_names = clk_parent;
	init.num_parents = 1;

	div->reg = host->regs + SD_EMMC_CLOCK;
	div->shift = __ffs(CLK_DIV_MASK);
	div->width = __builtin_popcountl(CLK_DIV_MASK);
	div->hw.init = &init;
	div->flags = CLK_DIVIDER_ONE_BASED;

	clk = devm_clk_register(host->dev, &div->hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	/* create the mmc core clock */
	core = devm_kzalloc(host->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s#core", dev_name(host->dev));
	init.name = clk_name;
	init.ops = &meson_mmc_clk_phase_ops;
	init.flags = CLK_SET_RATE_PARENT;
	clk_parent[0] = __clk_get_name(clk);
	init.parent_names = clk_parent;
	init.num_parents = 1;

	core->reg = host->regs + SD_EMMC_CLOCK;
	core->phase_mask = CLK_CORE_PHASE_MASK;
	core->hw.init = &init;

	host->mmc_clk = devm_clk_register(host->dev, &core->hw);
	if (WARN_ON(PTR_ERR_OR_ZERO(host->mmc_clk)))
		return PTR_ERR(host->mmc_clk);

	/* create the mmc tx clock */
	tx = devm_kzalloc(host->dev, sizeof(*tx), GFP_KERNEL);
	if (!tx)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s#tx", dev_name(host->dev));
	init.name = clk_name;
	init.ops = &meson_mmc_clk_phase_ops;
	init.flags = 0;
	clk_parent[0] = __clk_get_name(host->mmc_clk);
	init.parent_names = clk_parent;
	init.num_parents = 1;

	tx->reg = host->regs + SD_EMMC_CLOCK;
	tx->phase_mask = CLK_TX_PHASE_MASK;
	tx->delay_mask = CLK_TX_DELAY_MASK(host);
	tx->delay_step_ps = CLK_DELAY_STEP_PS;
	tx->hw.init = &init;

	host->tx_clk = devm_clk_register(host->dev, &tx->hw);
	if (WARN_ON(PTR_ERR_OR_ZERO(host->tx_clk)))
		return PTR_ERR(host->tx_clk);

	/* create the mmc rx clock */
	rx = devm_kzalloc(host->dev, sizeof(*rx), GFP_KERNEL);
	if (!rx)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s#rx", dev_name(host->dev));
	init.name = clk_name;
	init.ops = &meson_mmc_clk_phase_ops;
	init.flags = 0;
	clk_parent[0] = __clk_get_name(host->mmc_clk);
	init.parent_names = clk_parent;
	init.num_parents = 1;

	rx->reg = host->regs + SD_EMMC_CLOCK;
	rx->phase_mask = CLK_RX_PHASE_MASK;
	rx->delay_mask = CLK_RX_DELAY_MASK(host);
	rx->delay_step_ps = CLK_DELAY_STEP_PS;
	rx->hw.init = &init;

	host->rx_clk = devm_clk_register(host->dev, &rx->hw);
	if (WARN_ON(PTR_ERR_OR_ZERO(host->rx_clk)))
		return PTR_ERR(host->rx_clk);

	/* init SD_EMMC_CLOCK to sane defaults w/min clock rate */
	host->mmc->f_min = clk_round_rate(host->mmc_clk, 400000);
	ret = clk_set_rate(host->mmc_clk, host->mmc->f_min);
	if (ret)
		return ret;

	/*
	 * Set phases : These values are mostly the datasheet recommended ones
	 * except for the Tx phase. Datasheet recommends 180 but some cards
	 * fail at initialisation with it. 270 works just fine, it fixes these
	 * initialisation issues and enable eMMC DDR52 mode.
	 */
	clk_set_phase(host->mmc_clk, 180);
	clk_set_phase(host->tx_clk, 270);
	clk_set_phase(host->rx_clk, 0);

	return clk_prepare_enable(host->mmc_clk);
}

static void meson_mmc_shift_map(unsigned long *map, unsigned long shift)
{
	DECLARE_BITMAP(left, CLK_PHASE_POINT_NUM);
	DECLARE_BITMAP(right, CLK_PHASE_POINT_NUM);

	/*
	 * shift the bitmap right and reintroduce the dropped bits on the left
	 * of the bitmap
	 */
	bitmap_shift_right(right, map, shift, CLK_PHASE_POINT_NUM);
	bitmap_shift_left(left, map, CLK_PHASE_POINT_NUM - shift,
			  CLK_PHASE_POINT_NUM);
	bitmap_or(map, left, right, CLK_PHASE_POINT_NUM);
}

static void meson_mmc_find_next_region(unsigned long *map,
				       unsigned long *start,
				       unsigned long *stop)
{
	*start = find_next_bit(map, CLK_PHASE_POINT_NUM, *start);
	*stop = find_next_zero_bit(map, CLK_PHASE_POINT_NUM, *start);
}

static int meson_mmc_find_tuning_point(unsigned long *test)
{
	unsigned long shift, stop, offset = 0, start = 0, size = 0;

	/* Get the all good/all bad situation out the way */
	if (bitmap_full(test, CLK_PHASE_POINT_NUM))
		return 0; /* All points are good so point 0 will do */
	else if (bitmap_empty(test, CLK_PHASE_POINT_NUM))
		return -EIO; /* No successful tuning point */

	/*
	 * Now we know there is a least one region find. Make sure it does
	 * not wrap by the shifting the bitmap if necessary
	 */
	shift = find_first_zero_bit(test, CLK_PHASE_POINT_NUM);
	if (shift != 0)
		meson_mmc_shift_map(test, shift);

	while (start < CLK_PHASE_POINT_NUM) {
		meson_mmc_find_next_region(test, &start, &stop);

		if ((stop - start) > size) {
			offset = start;
			size = stop - start;
		}

		start = stop;
	}

	/* Get the center point of the region */
	offset += (size / 2);

	/* Shift the result back */
	offset = (offset + shift) % CLK_PHASE_POINT_NUM;

	return offset;
}

static int meson_mmc_clk_phase_tuning(struct mmc_host *mmc, u32 opcode,
				      struct clk *clk)
{
	int point, ret;
	DECLARE_BITMAP(test, CLK_PHASE_POINT_NUM);

	dev_dbg(mmc_dev(mmc), "%s phase/delay tunning...\n",
		__clk_get_name(clk));
	bitmap_zero(test, CLK_PHASE_POINT_NUM);

	/* Explore tuning points */
	for (point = 0; point < CLK_PHASE_POINT_NUM; point++) {
		clk_set_phase(clk, point * CLK_PHASE_STEP);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			set_bit(point, test);
	}

	/* Find the optimal tuning point and apply it */
	point = meson_mmc_find_tuning_point(test);
	if (point < 0)
		return point; /* tuning failed */

	clk_set_phase(clk, point * CLK_PHASE_STEP);
	dev_dbg(mmc_dev(mmc), "success with phase: %d\n",
		clk_get_phase(clk));
	return 0;
}

static int meson_mmc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct meson_host *host = mmc_priv(mmc);

	return meson_mmc_clk_phase_tuning(mmc, opcode, host->rx_clk);
}

static void meson_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct meson_host *host = mmc_priv(mmc);
	u32 bus_width, val;
	int err;

	/*
	 * GPIO regulator, only controls switching between 1v8 and
	 * 3v3, doesn't support MMC_POWER_OFF, MMC_POWER_ON.
	 */
	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);

		if (!IS_ERR(mmc->supply.vqmmc) && host->vqmmc_enabled) {
			regulator_disable(mmc->supply.vqmmc);
			host->vqmmc_enabled = false;
		}

		break;

	case MMC_POWER_UP:
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, ios->vdd);

		/* Reset rx phase */
		clk_set_phase(host->rx_clk, 0);

		break;

	case MMC_POWER_ON:
		if (!IS_ERR(mmc->supply.vqmmc) && !host->vqmmc_enabled) {
			int ret = regulator_enable(mmc->supply.vqmmc);

			if (ret < 0)
				dev_err(host->dev,
					"failed to enable vqmmc regulator\n");
			else
				host->vqmmc_enabled = true;
		}

		break;
	}

	/* Bus width */
	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		bus_width = CFG_BUS_WIDTH_1;
		break;
	case MMC_BUS_WIDTH_4:
		bus_width = CFG_BUS_WIDTH_4;
		break;
	case MMC_BUS_WIDTH_8:
		bus_width = CFG_BUS_WIDTH_8;
		break;
	default:
		dev_err(host->dev, "Invalid ios->bus_width: %u.  Setting to 4.\n",
			ios->bus_width);
		bus_width = CFG_BUS_WIDTH_4;
	}

	val = readl(host->regs + SD_EMMC_CFG);
	val &= ~CFG_BUS_WIDTH_MASK;
	val |= FIELD_PREP(CFG_BUS_WIDTH_MASK, bus_width);

	val &= ~CFG_DDR;
	if (meson_mmc_timing_is_ddr(ios))
		val |= CFG_DDR;

	val &= ~CFG_CHK_DS;
	if (ios->timing == MMC_TIMING_MMC_HS400)
		val |= CFG_CHK_DS;

	err = meson_mmc_clk_set(host, ios);
	if (err)
		dev_err(host->dev, "Failed to set clock: %d\n,", err);

	writel(val, host->regs + SD_EMMC_CFG);
	dev_dbg(host->dev, "SD_EMMC_CFG:  0x%08x\n", val);
}

static void meson_mmc_request_done(struct mmc_host *mmc,
				   struct mmc_request *mrq)
{
	struct meson_host *host = mmc_priv(mmc);

	host->cmd = NULL;
	mmc_request_done(host->mmc, mrq);
}

static void meson_mmc_set_blksz(struct mmc_host *mmc, unsigned int blksz)
{
	struct meson_host *host = mmc_priv(mmc);
	u32 cfg, blksz_old;

	cfg = readl(host->regs + SD_EMMC_CFG);
	blksz_old = FIELD_GET(CFG_BLK_LEN_MASK, cfg);

	if (!is_power_of_2(blksz))
		dev_err(host->dev, "blksz %u is not a power of 2\n", blksz);

	blksz = ilog2(blksz);

	/* check if block-size matches, if not update */
	if (blksz == blksz_old)
		return;

	dev_dbg(host->dev, "%s: update blk_len %d -> %d\n", __func__,
		blksz_old, blksz);

	cfg &= ~CFG_BLK_LEN_MASK;
	cfg |= FIELD_PREP(CFG_BLK_LEN_MASK, blksz);
	writel(cfg, host->regs + SD_EMMC_CFG);
}

static void meson_mmc_set_response_bits(struct mmc_command *cmd, u32 *cmd_cfg)
{
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			*cmd_cfg |= CMD_CFG_RESP_128;
		*cmd_cfg |= CMD_CFG_RESP_NUM;

		if (!(cmd->flags & MMC_RSP_CRC))
			*cmd_cfg |= CMD_CFG_RESP_NOCRC;

		if (cmd->flags & MMC_RSP_BUSY)
			*cmd_cfg |= CMD_CFG_R1B;
	} else {
		*cmd_cfg |= CMD_CFG_NO_RESP;
	}
}

static void meson_mmc_desc_chain_transfer(struct mmc_host *mmc, u32 cmd_cfg)
{
	struct meson_host *host = mmc_priv(mmc);
	struct sd_emmc_desc *desc = host->descs;
	struct mmc_data *data = host->cmd->data;
	struct scatterlist *sg;
	u32 start;
	int i;

	if (data->flags & MMC_DATA_WRITE)
		cmd_cfg |= CMD_CFG_DATA_WR;

	if (data->blocks > 1) {
		cmd_cfg |= CMD_CFG_BLOCK_MODE;
		meson_mmc_set_blksz(mmc, data->blksz);
	}

	for_each_sg(data->sg, sg, data->sg_count, i) {
		unsigned int len = sg_dma_len(sg);

		if (data->blocks > 1)
			len /= data->blksz;

		desc[i].cmd_cfg = cmd_cfg;
		desc[i].cmd_cfg |= FIELD_PREP(CMD_CFG_LENGTH_MASK, len);
		if (i > 0)
			desc[i].cmd_cfg |= CMD_CFG_NO_CMD;
		desc[i].cmd_arg = host->cmd->arg;
		desc[i].cmd_resp = 0;
		desc[i].cmd_data = sg_dma_address(sg);
	}
	desc[data->sg_count - 1].cmd_cfg |= CMD_CFG_END_OF_CHAIN;

	dma_wmb(); /* ensure descriptor is written before kicked */
	start = host->descs_dma_addr | START_DESC_BUSY;
	writel(start, host->regs + SD_EMMC_START);
}

static void meson_mmc_start_cmd(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct meson_host *host = mmc_priv(mmc);
	struct mmc_data *data = cmd->data;
	u32 cmd_cfg = 0, cmd_data = 0;
	unsigned int xfer_bytes = 0;

	/* Setup descriptors */
	dma_rmb();

	host->cmd = cmd;

	cmd_cfg |= FIELD_PREP(CMD_CFG_CMD_INDEX_MASK, cmd->opcode);
	cmd_cfg |= CMD_CFG_OWNER;  /* owned by CPU */

	meson_mmc_set_response_bits(cmd, &cmd_cfg);

	/* data? */
	if (data) {
		data->bytes_xfered = 0;
		cmd_cfg |= CMD_CFG_DATA_IO;
		cmd_cfg |= FIELD_PREP(CMD_CFG_TIMEOUT_MASK,
				      ilog2(meson_mmc_get_timeout_msecs(data)));

		if (meson_mmc_desc_chain_mode(data)) {
			meson_mmc_desc_chain_transfer(mmc, cmd_cfg);
			return;
		}

		if (data->blocks > 1) {
			cmd_cfg |= CMD_CFG_BLOCK_MODE;
			cmd_cfg |= FIELD_PREP(CMD_CFG_LENGTH_MASK,
					      data->blocks);
			meson_mmc_set_blksz(mmc, data->blksz);
		} else {
			cmd_cfg |= FIELD_PREP(CMD_CFG_LENGTH_MASK, data->blksz);
		}

		xfer_bytes = data->blksz * data->blocks;
		if (data->flags & MMC_DATA_WRITE) {
			cmd_cfg |= CMD_CFG_DATA_WR;
			WARN_ON(xfer_bytes > host->bounce_buf_size);
			sg_copy_to_buffer(data->sg, data->sg_len,
					  host->bounce_buf, xfer_bytes);
			dma_wmb();
		}

		cmd_data = host->bounce_dma_addr & CMD_DATA_MASK;
	} else {
		cmd_cfg |= FIELD_PREP(CMD_CFG_TIMEOUT_MASK,
				      ilog2(SD_EMMC_CMD_TIMEOUT));
	}

	/* Last descriptor */
	cmd_cfg |= CMD_CFG_END_OF_CHAIN;
	writel(cmd_cfg, host->regs + SD_EMMC_CMD_CFG);
	writel(cmd_data, host->regs + SD_EMMC_CMD_DAT);
	writel(0, host->regs + SD_EMMC_CMD_RSP);
	wmb(); /* ensure descriptor is written before kicked */
	writel(cmd->arg, host->regs + SD_EMMC_CMD_ARG);
}

static void meson_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct meson_host *host = mmc_priv(mmc);
	bool needs_pre_post_req = mrq->data &&
			!(mrq->data->host_cookie & SD_EMMC_PRE_REQ_DONE);

	if (needs_pre_post_req) {
		meson_mmc_get_transfer_mode(mmc, mrq);
		if (!meson_mmc_desc_chain_mode(mrq->data))
			needs_pre_post_req = false;
	}

	if (needs_pre_post_req)
		meson_mmc_pre_req(mmc, mrq);

	/* Stop execution */
	writel(0, host->regs + SD_EMMC_START);

	meson_mmc_start_cmd(mmc, mrq->sbc ?: mrq->cmd);

	if (needs_pre_post_req)
		meson_mmc_post_req(mmc, mrq, 0);
}

static void meson_mmc_read_resp(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct meson_host *host = mmc_priv(mmc);

	if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = readl(host->regs + SD_EMMC_CMD_RSP3);
		cmd->resp[1] = readl(host->regs + SD_EMMC_CMD_RSP2);
		cmd->resp[2] = readl(host->regs + SD_EMMC_CMD_RSP1);
		cmd->resp[3] = readl(host->regs + SD_EMMC_CMD_RSP);
	} else if (cmd->flags & MMC_RSP_PRESENT) {
		cmd->resp[0] = readl(host->regs + SD_EMMC_CMD_RSP);
	}
}

static irqreturn_t meson_mmc_irq(int irq, void *dev_id)
{
	struct meson_host *host = dev_id;
	struct mmc_command *cmd;
	struct mmc_data *data;
	u32 irq_en, status, raw_status;
	irqreturn_t ret = IRQ_NONE;

	if (WARN_ON(!host) || WARN_ON(!host->cmd))
		return IRQ_NONE;

	spin_lock(&host->lock);

	cmd = host->cmd;
	data = cmd->data;
	irq_en = readl(host->regs + SD_EMMC_IRQ_EN);
	raw_status = readl(host->regs + SD_EMMC_STATUS);
	status = raw_status & irq_en;

	cmd->error = 0;
	if (status & IRQ_CRC_ERR) {
		dev_dbg(host->dev, "CRC Error - status 0x%08x\n", status);
		cmd->error = -EILSEQ;
		ret = IRQ_HANDLED;
		goto out;
	}

	if (status & IRQ_TIMEOUTS) {
		dev_dbg(host->dev, "Timeout - status 0x%08x\n", status);
		cmd->error = -ETIMEDOUT;
		ret = IRQ_HANDLED;
		goto out;
	}

	meson_mmc_read_resp(host->mmc, cmd);

	if (status & IRQ_SDIO) {
		dev_dbg(host->dev, "IRQ: SDIO TODO.\n");
		ret = IRQ_HANDLED;
	}

	if (status & (IRQ_END_OF_CHAIN | IRQ_RESP_STATUS)) {
		if (data && !cmd->error)
			data->bytes_xfered = data->blksz * data->blocks;
		if (meson_mmc_bounce_buf_read(data) ||
		    meson_mmc_get_next_command(cmd))
			ret = IRQ_WAKE_THREAD;
		else
			ret = IRQ_HANDLED;
	}

out:
	/* ack all enabled interrupts */
	writel(irq_en, host->regs + SD_EMMC_STATUS);

	if (ret == IRQ_HANDLED)
		meson_mmc_request_done(host->mmc, cmd->mrq);
	else if (ret == IRQ_NONE)
		dev_warn(host->dev,
			 "Unexpected IRQ! status=0x%08x, irq_en=0x%08x\n",
			 raw_status, irq_en);

	spin_unlock(&host->lock);
	return ret;
}

static irqreturn_t meson_mmc_irq_thread(int irq, void *dev_id)
{
	struct meson_host *host = dev_id;
	struct mmc_command *next_cmd, *cmd = host->cmd;
	struct mmc_data *data;
	unsigned int xfer_bytes;

	if (WARN_ON(!cmd))
		return IRQ_NONE;

	data = cmd->data;
	if (meson_mmc_bounce_buf_read(data)) {
		xfer_bytes = data->blksz * data->blocks;
		WARN_ON(xfer_bytes > host->bounce_buf_size);
		sg_copy_from_buffer(data->sg, data->sg_len,
				    host->bounce_buf, xfer_bytes);
	}

	next_cmd = meson_mmc_get_next_command(cmd);
	if (next_cmd)
		meson_mmc_start_cmd(host->mmc, next_cmd);
	else
		meson_mmc_request_done(host->mmc, cmd->mrq);

	return IRQ_HANDLED;
}

/*
 * NOTE: we only need this until the GPIO/pinctrl driver can handle
 * interrupts.  For now, the MMC core will use this for polling.
 */
static int meson_mmc_get_cd(struct mmc_host *mmc)
{
	int status = mmc_gpio_get_cd(mmc);

	if (status == -ENOSYS)
		return 1; /* assume present */

	return status;
}

static void meson_mmc_cfg_init(struct meson_host *host)
{
	u32 cfg = 0;

	cfg |= FIELD_PREP(CFG_RESP_TIMEOUT_MASK,
			  ilog2(SD_EMMC_CFG_RESP_TIMEOUT));
	cfg |= FIELD_PREP(CFG_RC_CC_MASK, ilog2(SD_EMMC_CFG_CMD_GAP));
	cfg |= FIELD_PREP(CFG_BLK_LEN_MASK, ilog2(SD_EMMC_CFG_BLK_SIZE));

	writel(cfg, host->regs + SD_EMMC_CFG);
}

static int meson_mmc_card_busy(struct mmc_host *mmc)
{
	struct meson_host *host = mmc_priv(mmc);
	u32 regval;

	regval = readl(host->regs + SD_EMMC_STATUS);

	/* We are only interrested in lines 0 to 3, so mask the other ones */
	return !(FIELD_GET(STATUS_DATI, regval) & 0xf);
}

static int meson_mmc_voltage_switch(struct mmc_host *mmc, struct mmc_ios *ios)
{
	/* vqmmc regulator is available */
	if (!IS_ERR(mmc->supply.vqmmc)) {
		/*
		 * The usual amlogic setup uses a GPIO to switch from one
		 * regulator to the other. While the voltage ramp up is
		 * pretty fast, care must be taken when switching from 3.3v
		 * to 1.8v. Please make sure the regulator framework is aware
		 * of your own regulator constraints
		 */
		return mmc_regulator_set_vqmmc(mmc, ios);
	}

	/* no vqmmc regulator, assume fixed regulator at 3/3.3V */
	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330)
		return 0;

	return -EINVAL;
}

static const struct mmc_host_ops meson_mmc_ops = {
	.request	= meson_mmc_request,
	.set_ios	= meson_mmc_set_ios,
	.get_cd         = meson_mmc_get_cd,
	.pre_req	= meson_mmc_pre_req,
	.post_req	= meson_mmc_post_req,
	.execute_tuning = meson_mmc_execute_tuning,
	.card_busy	= meson_mmc_card_busy,
	.start_signal_voltage_switch = meson_mmc_voltage_switch,
};

static int meson_mmc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct meson_host *host;
	struct mmc_host *mmc;
	int ret, irq;

	mmc = mmc_alloc_host(sizeof(struct meson_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;
	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, host);

	spin_lock_init(&host->lock);

	/* Get regulators and the supported OCR mask */
	host->vqmmc_enabled = false;
	ret = mmc_regulator_get_supply(mmc);
	if (ret)
		goto free_host;

	ret = mmc_of_parse(mmc);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_warn(&pdev->dev, "error parsing DT: %d\n", ret);
		goto free_host;
	}

	host->data = (struct meson_mmc_data *)
		of_device_get_match_data(&pdev->dev);
	if (!host->data) {
		ret = -EINVAL;
		goto free_host;
	}

	ret = device_reset_optional(&pdev->dev);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "device reset failed: %d\n", ret);

		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->regs)) {
		ret = PTR_ERR(host->regs);
		goto free_host;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "failed to get interrupt resource.\n");
		ret = -EINVAL;
		goto free_host;
	}

	host->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(host->pinctrl)) {
		ret = PTR_ERR(host->pinctrl);
		goto free_host;
	}

	host->pins_default = pinctrl_lookup_state(host->pinctrl,
						  PINCTRL_STATE_DEFAULT);
	if (IS_ERR(host->pins_default)) {
		ret = PTR_ERR(host->pins_default);
		goto free_host;
	}

	host->pins_clk_gate = pinctrl_lookup_state(host->pinctrl,
						   "clk-gate");
	if (IS_ERR(host->pins_clk_gate)) {
		dev_warn(&pdev->dev,
			 "can't get clk-gate pinctrl, using clk_stop bit\n");
		host->pins_clk_gate = NULL;
	}

	host->core_clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(host->core_clk)) {
		ret = PTR_ERR(host->core_clk);
		goto free_host;
	}

	ret = clk_prepare_enable(host->core_clk);
	if (ret)
		goto free_host;

	ret = meson_mmc_clk_init(host);
	if (ret)
		goto err_core_clk;

	/* set config to sane default */
	meson_mmc_cfg_init(host);

	/* Stop execution */
	writel(0, host->regs + SD_EMMC_START);

	/* clear, ack and enable interrupts */
	writel(0, host->regs + SD_EMMC_IRQ_EN);
	writel(IRQ_CRC_ERR | IRQ_TIMEOUTS | IRQ_END_OF_CHAIN,
	       host->regs + SD_EMMC_STATUS);
	writel(IRQ_CRC_ERR | IRQ_TIMEOUTS | IRQ_END_OF_CHAIN,
	       host->regs + SD_EMMC_IRQ_EN);

	ret = devm_request_threaded_irq(&pdev->dev, irq, meson_mmc_irq,
					meson_mmc_irq_thread, IRQF_SHARED,
					NULL, host);
	if (ret)
		goto err_init_clk;

	mmc->caps |= MMC_CAP_CMD23;
	mmc->max_blk_count = CMD_CFG_LENGTH_MASK;
	mmc->max_req_size = mmc->max_blk_count * mmc->max_blk_size;
	mmc->max_segs = SD_EMMC_DESC_BUF_LEN / sizeof(struct sd_emmc_desc);
	mmc->max_seg_size = mmc->max_req_size;

	/* data bounce buffer */
	host->bounce_buf_size = mmc->max_req_size;
	host->bounce_buf =
		dma_alloc_coherent(host->dev, host->bounce_buf_size,
				   &host->bounce_dma_addr, GFP_KERNEL);
	if (host->bounce_buf == NULL) {
		dev_err(host->dev, "Unable to map allocate DMA bounce buffer.\n");
		ret = -ENOMEM;
		goto err_init_clk;
	}

	host->descs = dma_alloc_coherent(host->dev, SD_EMMC_DESC_BUF_LEN,
		      &host->descs_dma_addr, GFP_KERNEL);
	if (!host->descs) {
		dev_err(host->dev, "Allocating descriptor DMA buffer failed\n");
		ret = -ENOMEM;
		goto err_bounce_buf;
	}

	mmc->ops = &meson_mmc_ops;
	mmc_add_host(mmc);

	return 0;

err_bounce_buf:
	dma_free_coherent(host->dev, host->bounce_buf_size,
			  host->bounce_buf, host->bounce_dma_addr);
err_init_clk:
	clk_disable_unprepare(host->mmc_clk);
err_core_clk:
	clk_disable_unprepare(host->core_clk);
free_host:
	mmc_free_host(mmc);
	return ret;
}

static int meson_mmc_remove(struct platform_device *pdev)
{
	struct meson_host *host = dev_get_drvdata(&pdev->dev);

	mmc_remove_host(host->mmc);

	/* disable interrupts */
	writel(0, host->regs + SD_EMMC_IRQ_EN);

	dma_free_coherent(host->dev, SD_EMMC_DESC_BUF_LEN,
			  host->descs, host->descs_dma_addr);
	dma_free_coherent(host->dev, host->bounce_buf_size,
			  host->bounce_buf, host->bounce_dma_addr);

	clk_disable_unprepare(host->mmc_clk);
	clk_disable_unprepare(host->core_clk);

	mmc_free_host(host->mmc);
	return 0;
}

static const struct meson_mmc_data meson_gx_data = {
	.tx_delay_mask	= CLK_V2_TX_DELAY_MASK,
	.rx_delay_mask	= CLK_V2_RX_DELAY_MASK,
	.always_on	= CLK_V2_ALWAYS_ON,
};

static const struct meson_mmc_data meson_axg_data = {
	.tx_delay_mask	= CLK_V3_TX_DELAY_MASK,
	.rx_delay_mask	= CLK_V3_RX_DELAY_MASK,
	.always_on	= CLK_V3_ALWAYS_ON,
};

static const struct of_device_id meson_mmc_of_match[] = {
	{ .compatible = "amlogic,meson-gx-mmc",		.data = &meson_gx_data },
	{ .compatible = "amlogic,meson-gxbb-mmc", 	.data = &meson_gx_data },
	{ .compatible = "amlogic,meson-gxl-mmc",	.data = &meson_gx_data },
	{ .compatible = "amlogic,meson-gxm-mmc",	.data = &meson_gx_data },
	{ .compatible = "amlogic,meson-axg-mmc",	.data = &meson_axg_data },
	{}
};
MODULE_DEVICE_TABLE(of, meson_mmc_of_match);

static struct platform_driver meson_mmc_driver = {
	.probe		= meson_mmc_probe,
	.remove		= meson_mmc_remove,
	.driver		= {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(meson_mmc_of_match),
	},
};

module_platform_driver(meson_mmc_driver);

MODULE_DESCRIPTION("Amlogic S905*/GX*/AXG SD/eMMC driver");
MODULE_AUTHOR("Kevin Hilman <khilman@baylibre.com>");
MODULE_LICENSE("GPL v2");
