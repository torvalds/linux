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

#define DRIVER_NAME "meson-gx-mmc"

#define SD_EMMC_CLOCK 0x0
#define   CLK_DIV_SHIFT 0
#define   CLK_DIV_WIDTH 6
#define   CLK_DIV_MASK 0x3f
#define   CLK_DIV_MAX 63
#define   CLK_SRC_SHIFT 6
#define   CLK_SRC_WIDTH 2
#define   CLK_SRC_MASK 0x3
#define   CLK_SRC_XTAL 0   /* external crystal */
#define   CLK_SRC_XTAL_RATE 24000000
#define   CLK_SRC_PLL 1    /* FCLK_DIV2 */
#define   CLK_SRC_PLL_RATE 1000000000
#define   CLK_PHASE_SHIFT 8
#define   CLK_PHASE_MASK 0x3
#define   CLK_PHASE_0 0
#define   CLK_PHASE_90 1
#define   CLK_PHASE_180 2
#define   CLK_PHASE_270 3
#define   CLK_ALWAYS_ON BIT(24)

#define SD_EMMC_DElAY 0x4
#define SD_EMMC_ADJUST 0x8
#define SD_EMMC_CALOUT 0x10
#define SD_EMMC_START 0x40
#define   START_DESC_INIT BIT(0)
#define   START_DESC_BUSY BIT(1)
#define   START_DESC_ADDR_SHIFT 2
#define   START_DESC_ADDR_MASK (~0x3)

#define SD_EMMC_CFG 0x44
#define   CFG_BUS_WIDTH_SHIFT 0
#define   CFG_BUS_WIDTH_MASK 0x3
#define   CFG_BUS_WIDTH_1 0x0
#define   CFG_BUS_WIDTH_4 0x1
#define   CFG_BUS_WIDTH_8 0x2
#define   CFG_DDR BIT(2)
#define   CFG_BLK_LEN_SHIFT 4
#define   CFG_BLK_LEN_MASK 0xf
#define   CFG_RESP_TIMEOUT_SHIFT 8
#define   CFG_RESP_TIMEOUT_MASK 0xf
#define   CFG_RC_CC_SHIFT 12
#define   CFG_RC_CC_MASK 0xf
#define   CFG_STOP_CLOCK BIT(22)
#define   CFG_CLK_ALWAYS_ON BIT(18)
#define   CFG_AUTO_CLK BIT(23)

#define SD_EMMC_STATUS 0x48
#define   STATUS_BUSY BIT(31)

#define SD_EMMC_IRQ_EN 0x4c
#define   IRQ_EN_MASK 0x3fff
#define   IRQ_RXD_ERR_SHIFT 0
#define   IRQ_RXD_ERR_MASK 0xff
#define   IRQ_TXD_ERR BIT(8)
#define   IRQ_DESC_ERR BIT(9)
#define   IRQ_RESP_ERR BIT(10)
#define   IRQ_RESP_TIMEOUT BIT(11)
#define   IRQ_DESC_TIMEOUT BIT(12)
#define   IRQ_END_OF_CHAIN BIT(13)
#define   IRQ_RESP_STATUS BIT(14)
#define   IRQ_SDIO BIT(15)

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
#define SD_EMMC_CFG_CMD_GAP 16 /* in clock cycles */
#define MUX_CLK_NUM_PARENTS 2

struct meson_host {
	struct	device		*dev;
	struct	mmc_host	*mmc;
	struct	mmc_request	*mrq;
	struct	mmc_command	*cmd;

	spinlock_t lock;
	void __iomem *regs;
	int irq;
	u32 ocr_mask;
	struct clk *core_clk;
	struct clk_mux mux;
	struct clk *mux_clk;
	struct clk *mux_parent[MUX_CLK_NUM_PARENTS];
	unsigned long mux_parent_rate[MUX_CLK_NUM_PARENTS];

	struct clk_divider cfg_div;
	struct clk *cfg_div_clk;

	unsigned int bounce_buf_size;
	void *bounce_buf;
	dma_addr_t bounce_dma_addr;

	bool vqmmc_enabled;
};

struct sd_emmc_desc {
	u32 cmd_cfg;
	u32 cmd_arg;
	u32 cmd_data;
	u32 cmd_resp;
};
#define CMD_CFG_LENGTH_SHIFT 0
#define CMD_CFG_LENGTH_MASK 0x1ff
#define CMD_CFG_BLOCK_MODE BIT(9)
#define CMD_CFG_R1B BIT(10)
#define CMD_CFG_END_OF_CHAIN BIT(11)
#define CMD_CFG_TIMEOUT_SHIFT 12
#define CMD_CFG_TIMEOUT_MASK 0xf
#define CMD_CFG_NO_RESP BIT(16)
#define CMD_CFG_NO_CMD BIT(17)
#define CMD_CFG_DATA_IO BIT(18)
#define CMD_CFG_DATA_WR BIT(19)
#define CMD_CFG_RESP_NOCRC BIT(20)
#define CMD_CFG_RESP_128 BIT(21)
#define CMD_CFG_RESP_NUM BIT(22)
#define CMD_CFG_DATA_NUM BIT(23)
#define CMD_CFG_CMD_INDEX_SHIFT 24
#define CMD_CFG_CMD_INDEX_MASK 0x3f
#define CMD_CFG_ERROR BIT(30)
#define CMD_CFG_OWNER BIT(31)

#define CMD_DATA_MASK (~0x3)
#define CMD_DATA_BIG_ENDIAN BIT(1)
#define CMD_DATA_SRAM BIT(0)
#define CMD_RESP_MASK (~0x1)
#define CMD_RESP_SRAM BIT(0)

static int meson_mmc_clk_set(struct meson_host *host, unsigned long clk_rate)
{
	struct mmc_host *mmc = host->mmc;
	int ret = 0;
	u32 cfg;

	if (clk_rate) {
		if (WARN_ON(clk_rate > mmc->f_max))
			clk_rate = mmc->f_max;
		else if (WARN_ON(clk_rate < mmc->f_min))
			clk_rate = mmc->f_min;
	}

	if (clk_rate == mmc->actual_clock)
		return 0;

	/* stop clock */
	cfg = readl(host->regs + SD_EMMC_CFG);
	if (!(cfg & CFG_STOP_CLOCK)) {
		cfg |= CFG_STOP_CLOCK;
		writel(cfg, host->regs + SD_EMMC_CFG);
	}

	dev_dbg(host->dev, "change clock rate %u -> %lu\n",
		mmc->actual_clock, clk_rate);

	if (clk_rate == 0) {
		mmc->actual_clock = 0;
		return 0;
	}

	ret = clk_set_rate(host->cfg_div_clk, clk_rate);
	if (ret)
		dev_warn(host->dev, "Unable to set cfg_div_clk to %lu. ret=%d\n",
			 clk_rate, ret);
	else if (clk_rate && clk_rate != clk_get_rate(host->cfg_div_clk))
		dev_warn(host->dev, "divider requested rate %lu != actual rate %lu: ret=%d\n",
			 clk_rate, clk_get_rate(host->cfg_div_clk), ret);
	else
		mmc->actual_clock = clk_rate;

	/* (re)start clock, if non-zero */
	if (!ret && clk_rate) {
		cfg = readl(host->regs + SD_EMMC_CFG);
		cfg &= ~CFG_STOP_CLOCK;
		writel(cfg, host->regs + SD_EMMC_CFG);
	}

	return ret;
}

/*
 * The SD/eMMC IP block has an internal mux and divider used for
 * generating the MMC clock.  Use the clock framework to create and
 * manage these clocks.
 */
static int meson_mmc_clk_init(struct meson_host *host)
{
	struct clk_init_data init;
	char clk_name[32];
	int i, ret = 0;
	const char *mux_parent_names[MUX_CLK_NUM_PARENTS];
	unsigned int mux_parent_count = 0;
	const char *clk_div_parents[1];
	unsigned int f_min = UINT_MAX;
	u32 clk_reg, cfg;

	/* get the mux parents */
	for (i = 0; i < MUX_CLK_NUM_PARENTS; i++) {
		char name[16];

		snprintf(name, sizeof(name), "clkin%d", i);
		host->mux_parent[i] = devm_clk_get(host->dev, name);
		if (IS_ERR(host->mux_parent[i])) {
			ret = PTR_ERR(host->mux_parent[i]);
			if (PTR_ERR(host->mux_parent[i]) != -EPROBE_DEFER)
				dev_err(host->dev, "Missing clock %s\n", name);
			host->mux_parent[i] = NULL;
			return ret;
		}

		host->mux_parent_rate[i] = clk_get_rate(host->mux_parent[i]);
		mux_parent_names[i] = __clk_get_name(host->mux_parent[i]);
		mux_parent_count++;
		if (host->mux_parent_rate[i] < f_min)
			f_min = host->mux_parent_rate[i];
	}

	/* cacluate f_min based on input clocks, and max divider value */
	if (f_min != UINT_MAX)
		f_min = DIV_ROUND_UP(CLK_SRC_XTAL_RATE, CLK_DIV_MAX);
	else
		f_min = 4000000;  /* default min: 400 MHz */
	host->mmc->f_min = f_min;

	/* create the mux */
	snprintf(clk_name, sizeof(clk_name), "%s#mux", dev_name(host->dev));
	init.name = clk_name;
	init.ops = &clk_mux_ops;
	init.flags = 0;
	init.parent_names = mux_parent_names;
	init.num_parents = mux_parent_count;

	host->mux.reg = host->regs + SD_EMMC_CLOCK;
	host->mux.shift = CLK_SRC_SHIFT;
	host->mux.mask = CLK_SRC_MASK;
	host->mux.flags = 0;
	host->mux.table = NULL;
	host->mux.hw.init = &init;

	host->mux_clk = devm_clk_register(host->dev, &host->mux.hw);
	if (WARN_ON(IS_ERR(host->mux_clk)))
		return PTR_ERR(host->mux_clk);

	/* create the divider */
	snprintf(clk_name, sizeof(clk_name), "%s#div", dev_name(host->dev));
	init.name = devm_kstrdup(host->dev, clk_name, GFP_KERNEL);
	init.ops = &clk_divider_ops;
	init.flags = CLK_SET_RATE_PARENT;
	clk_div_parents[0] = __clk_get_name(host->mux_clk);
	init.parent_names = clk_div_parents;
	init.num_parents = ARRAY_SIZE(clk_div_parents);

	host->cfg_div.reg = host->regs + SD_EMMC_CLOCK;
	host->cfg_div.shift = CLK_DIV_SHIFT;
	host->cfg_div.width = CLK_DIV_WIDTH;
	host->cfg_div.hw.init = &init;
	host->cfg_div.flags = CLK_DIVIDER_ONE_BASED |
		CLK_DIVIDER_ROUND_CLOSEST | CLK_DIVIDER_ALLOW_ZERO;

	host->cfg_div_clk = devm_clk_register(host->dev, &host->cfg_div.hw);
	if (WARN_ON(PTR_ERR_OR_ZERO(host->cfg_div_clk)))
		return PTR_ERR(host->cfg_div_clk);

	/* init SD_EMMC_CLOCK to sane defaults w/min clock rate */
	clk_reg = 0;
	clk_reg |= CLK_PHASE_180 << CLK_PHASE_SHIFT;
	clk_reg |= CLK_SRC_XTAL << CLK_SRC_SHIFT;
	clk_reg |= CLK_DIV_MAX << CLK_DIV_SHIFT;
	clk_reg &= ~CLK_ALWAYS_ON;
	writel(clk_reg, host->regs + SD_EMMC_CLOCK);

	/* Ensure clock starts in "auto" mode, not "always on" */
	cfg = readl(host->regs + SD_EMMC_CFG);
	cfg &= ~CFG_CLK_ALWAYS_ON;
	cfg |= CFG_AUTO_CLK;
	writel(cfg, host->regs + SD_EMMC_CFG);

	ret = clk_prepare_enable(host->cfg_div_clk);
	if (!ret)
		ret = meson_mmc_clk_set(host, f_min);

	if (!ret)
		clk_disable_unprepare(host->cfg_div_clk);

	return ret;
}

static void meson_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct meson_host *host = mmc_priv(mmc);
	u32 bus_width;
	u32 val, orig;

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
		break;

	case MMC_POWER_ON:
		if (!IS_ERR(mmc->supply.vqmmc) && !host->vqmmc_enabled) {
			int ret = regulator_enable(mmc->supply.vqmmc);

			if (ret < 0)
				dev_err(mmc_dev(mmc),
					"failed to enable vqmmc regulator\n");
			else
				host->vqmmc_enabled = true;
		}

		break;
	}


	meson_mmc_clk_set(host, ios->clock);

	/* Bus width */
	val = readl(host->regs + SD_EMMC_CFG);
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
		return;
	}

	val = readl(host->regs + SD_EMMC_CFG);
	orig = val;

	val &= ~(CFG_BUS_WIDTH_MASK << CFG_BUS_WIDTH_SHIFT);
	val |= bus_width << CFG_BUS_WIDTH_SHIFT;

	val &= ~(CFG_BLK_LEN_MASK << CFG_BLK_LEN_SHIFT);
	val |= ilog2(SD_EMMC_CFG_BLK_SIZE) << CFG_BLK_LEN_SHIFT;

	val &= ~(CFG_RESP_TIMEOUT_MASK << CFG_RESP_TIMEOUT_SHIFT);
	val |= ilog2(SD_EMMC_CFG_RESP_TIMEOUT) << CFG_RESP_TIMEOUT_SHIFT;

	val &= ~(CFG_RC_CC_MASK << CFG_RC_CC_SHIFT);
	val |= ilog2(SD_EMMC_CFG_CMD_GAP) << CFG_RC_CC_SHIFT;

	writel(val, host->regs + SD_EMMC_CFG);

	if (val != orig)
		dev_dbg(host->dev, "%s: SD_EMMC_CFG: 0x%08x -> 0x%08x\n",
			__func__, orig, val);
}

static int meson_mmc_request_done(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct meson_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != mrq);

	host->mrq = NULL;
	host->cmd = NULL;
	mmc_request_done(host->mmc, mrq);

	return 0;
}

static void meson_mmc_start_cmd(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct meson_host *host = mmc_priv(mmc);
	struct sd_emmc_desc *desc, desc_tmp;
	u32 cfg;
	u8 blk_len, cmd_cfg_timeout;
	unsigned int xfer_bytes = 0;

	/* Setup descriptors */
	dma_rmb();
	desc = &desc_tmp;
	memset(desc, 0, sizeof(struct sd_emmc_desc));

	desc->cmd_cfg |= (cmd->opcode & CMD_CFG_CMD_INDEX_MASK)	<<
		CMD_CFG_CMD_INDEX_SHIFT;
	desc->cmd_cfg |= CMD_CFG_OWNER;  /* owned by CPU */
	desc->cmd_arg = cmd->arg;

	/* Response */
	if (cmd->flags & MMC_RSP_PRESENT) {
		desc->cmd_cfg &= ~CMD_CFG_NO_RESP;
		if (cmd->flags & MMC_RSP_136)
			desc->cmd_cfg |= CMD_CFG_RESP_128;
		desc->cmd_cfg |= CMD_CFG_RESP_NUM;
		desc->cmd_resp = 0;

		if (!(cmd->flags & MMC_RSP_CRC))
			desc->cmd_cfg |= CMD_CFG_RESP_NOCRC;

		if (cmd->flags & MMC_RSP_BUSY)
			desc->cmd_cfg |= CMD_CFG_R1B;
	} else {
		desc->cmd_cfg |= CMD_CFG_NO_RESP;
	}

	/* data? */
	if (cmd->data) {
		desc->cmd_cfg |= CMD_CFG_DATA_IO;
		if (cmd->data->blocks > 1) {
			desc->cmd_cfg |= CMD_CFG_BLOCK_MODE;
			desc->cmd_cfg |=
				(cmd->data->blocks & CMD_CFG_LENGTH_MASK) <<
				CMD_CFG_LENGTH_SHIFT;

			/* check if block-size matches, if not update */
			cfg = readl(host->regs + SD_EMMC_CFG);
			blk_len = cfg & (CFG_BLK_LEN_MASK << CFG_BLK_LEN_SHIFT);
			blk_len >>= CFG_BLK_LEN_SHIFT;
			if (blk_len != ilog2(cmd->data->blksz)) {
				dev_warn(host->dev, "%s: update blk_len %d -> %d\n",
					__func__, blk_len,
					 ilog2(cmd->data->blksz));
				blk_len = ilog2(cmd->data->blksz);
				cfg &= ~(CFG_BLK_LEN_MASK << CFG_BLK_LEN_SHIFT);
				cfg |= blk_len << CFG_BLK_LEN_SHIFT;
				writel(cfg, host->regs + SD_EMMC_CFG);
			}
		} else {
			desc->cmd_cfg &= ~CMD_CFG_BLOCK_MODE;
			desc->cmd_cfg |=
				(cmd->data->blksz & CMD_CFG_LENGTH_MASK) <<
				CMD_CFG_LENGTH_SHIFT;
		}

		cmd->data->bytes_xfered = 0;
		xfer_bytes = cmd->data->blksz * cmd->data->blocks;
		if (cmd->data->flags & MMC_DATA_WRITE) {
			desc->cmd_cfg |= CMD_CFG_DATA_WR;
			WARN_ON(xfer_bytes > host->bounce_buf_size);
			sg_copy_to_buffer(cmd->data->sg, cmd->data->sg_len,
					  host->bounce_buf, xfer_bytes);
			cmd->data->bytes_xfered = xfer_bytes;
			dma_wmb();
		} else {
			desc->cmd_cfg &= ~CMD_CFG_DATA_WR;
		}

		if (xfer_bytes > 0) {
			desc->cmd_cfg &= ~CMD_CFG_DATA_NUM;
			desc->cmd_data = host->bounce_dma_addr & CMD_DATA_MASK;
		} else {
			/* write data to data_addr */
			desc->cmd_cfg |= CMD_CFG_DATA_NUM;
			desc->cmd_data = 0;
		}

		cmd_cfg_timeout = 12;
	} else {
		desc->cmd_cfg &= ~CMD_CFG_DATA_IO;
		cmd_cfg_timeout = 10;
	}
	desc->cmd_cfg |= (cmd_cfg_timeout & CMD_CFG_TIMEOUT_MASK) <<
		CMD_CFG_TIMEOUT_SHIFT;

	host->cmd = cmd;

	/* Last descriptor */
	desc->cmd_cfg |= CMD_CFG_END_OF_CHAIN;
	writel(desc->cmd_cfg, host->regs + SD_EMMC_CMD_CFG);
	writel(desc->cmd_data, host->regs + SD_EMMC_CMD_DAT);
	writel(desc->cmd_resp, host->regs + SD_EMMC_CMD_RSP);
	wmb(); /* ensure descriptor is written before kicked */
	writel(desc->cmd_arg, host->regs + SD_EMMC_CMD_ARG);
}

static void meson_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct meson_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);

	/* Stop execution */
	writel(0, host->regs + SD_EMMC_START);

	/* clear, ack, enable all interrupts */
	writel(0, host->regs + SD_EMMC_IRQ_EN);
	writel(IRQ_EN_MASK, host->regs + SD_EMMC_STATUS);
	writel(IRQ_EN_MASK, host->regs + SD_EMMC_IRQ_EN);

	host->mrq = mrq;

	if (mrq->sbc)
		meson_mmc_start_cmd(mmc, mrq->sbc);
	else
		meson_mmc_start_cmd(mmc, mrq->cmd);
}

static int meson_mmc_read_resp(struct mmc_host *mmc, struct mmc_command *cmd)
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

	return 0;
}

static irqreturn_t meson_mmc_irq(int irq, void *dev_id)
{
	struct meson_host *host = dev_id;
	struct mmc_request *mrq;
	struct mmc_command *cmd = host->cmd;
	u32 irq_en, status, raw_status;
	irqreturn_t ret = IRQ_HANDLED;

	if (WARN_ON(!host))
		return IRQ_NONE;

	mrq = host->mrq;

	if (WARN_ON(!mrq))
		return IRQ_NONE;

	if (WARN_ON(!cmd))
		return IRQ_NONE;

	spin_lock(&host->lock);
	irq_en = readl(host->regs + SD_EMMC_IRQ_EN);
	raw_status = readl(host->regs + SD_EMMC_STATUS);
	status = raw_status & irq_en;

	if (!status) {
		dev_warn(host->dev, "Spurious IRQ! status=0x%08x, irq_en=0x%08x\n",
			 raw_status, irq_en);
		ret = IRQ_NONE;
		goto out;
	}

	cmd->error = 0;
	if (status & IRQ_RXD_ERR_MASK) {
		dev_dbg(host->dev, "Unhandled IRQ: RXD error\n");
		cmd->error = -EILSEQ;
	}
	if (status & IRQ_TXD_ERR) {
		dev_dbg(host->dev, "Unhandled IRQ: TXD error\n");
		cmd->error = -EILSEQ;
	}
	if (status & IRQ_DESC_ERR)
		dev_dbg(host->dev, "Unhandled IRQ: Descriptor error\n");
	if (status & IRQ_RESP_ERR) {
		dev_dbg(host->dev, "Unhandled IRQ: Response error\n");
		cmd->error = -EILSEQ;
	}
	if (status & IRQ_RESP_TIMEOUT) {
		dev_dbg(host->dev, "Unhandled IRQ: Response timeout\n");
		cmd->error = -ETIMEDOUT;
	}
	if (status & IRQ_DESC_TIMEOUT) {
		dev_dbg(host->dev, "Unhandled IRQ: Descriptor timeout\n");
		cmd->error = -ETIMEDOUT;
	}
	if (status & IRQ_SDIO)
		dev_dbg(host->dev, "Unhandled IRQ: SDIO.\n");

	if (status & (IRQ_END_OF_CHAIN | IRQ_RESP_STATUS))
		ret = IRQ_WAKE_THREAD;
	else  {
		dev_warn(host->dev, "Unknown IRQ! status=0x%04x: MMC CMD%u arg=0x%08x flags=0x%08x stop=%d\n",
			 status, cmd->opcode, cmd->arg,
			 cmd->flags, mrq->stop ? 1 : 0);
		if (cmd->data) {
			struct mmc_data *data = cmd->data;

			dev_warn(host->dev, "\tblksz %u blocks %u flags 0x%08x (%s%s)",
				 data->blksz, data->blocks, data->flags,
				 data->flags & MMC_DATA_WRITE ? "write" : "",
				 data->flags & MMC_DATA_READ ? "read" : "");
		}
	}

out:
	/* ack all (enabled) interrupts */
	writel(status, host->regs + SD_EMMC_STATUS);

	if (ret == IRQ_HANDLED) {
		meson_mmc_read_resp(host->mmc, cmd);
		meson_mmc_request_done(host->mmc, cmd->mrq);
	}

	spin_unlock(&host->lock);
	return ret;
}

static irqreturn_t meson_mmc_irq_thread(int irq, void *dev_id)
{
	struct meson_host *host = dev_id;
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data;
	unsigned int xfer_bytes;
	int ret = IRQ_HANDLED;

	if (WARN_ON(!mrq))
		ret = IRQ_NONE;

	if (WARN_ON(!cmd))
		ret = IRQ_NONE;

	data = cmd->data;
	if (data) {
		xfer_bytes = data->blksz * data->blocks;
		if (data->flags & MMC_DATA_READ) {
			WARN_ON(xfer_bytes > host->bounce_buf_size);
			sg_copy_from_buffer(data->sg, data->sg_len,
					    host->bounce_buf, xfer_bytes);
			data->bytes_xfered = xfer_bytes;
		}
	}

	meson_mmc_read_resp(host->mmc, cmd);
	if (!data || !data->stop || mrq->sbc)
		meson_mmc_request_done(host->mmc, mrq);
	else
		meson_mmc_start_cmd(host->mmc, data->stop);

	return ret;
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

static const struct mmc_host_ops meson_mmc_ops = {
	.request	= meson_mmc_request,
	.set_ios	= meson_mmc_set_ios,
	.get_cd         = meson_mmc_get_cd,
};

static int meson_mmc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct meson_host *host;
	struct mmc_host *mmc;
	int ret;

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
	if (ret == -EPROBE_DEFER)
		goto free_host;

	ret = mmc_of_parse(mmc);
	if (ret) {
		dev_warn(&pdev->dev, "error parsing DT: %d\n", ret);
		goto free_host;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->regs)) {
		ret = PTR_ERR(host->regs);
		goto free_host;
	}

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq == 0) {
		dev_err(&pdev->dev, "failed to get interrupt resource.\n");
		ret = -EINVAL;
		goto free_host;
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
		goto free_host;

	/* Stop execution */
	writel(0, host->regs + SD_EMMC_START);

	/* clear, ack, enable all interrupts */
	writel(0, host->regs + SD_EMMC_IRQ_EN);
	writel(IRQ_EN_MASK, host->regs + SD_EMMC_STATUS);

	ret = devm_request_threaded_irq(&pdev->dev, host->irq,
					meson_mmc_irq, meson_mmc_irq_thread,
					IRQF_SHARED, DRIVER_NAME, host);
	if (ret)
		goto free_host;

	/* data bounce buffer */
	host->bounce_buf_size = SZ_512K;
	host->bounce_buf =
		dma_alloc_coherent(host->dev, host->bounce_buf_size,
				   &host->bounce_dma_addr, GFP_KERNEL);
	if (host->bounce_buf == NULL) {
		dev_err(host->dev, "Unable to map allocate DMA bounce buffer.\n");
		ret = -ENOMEM;
		goto free_host;
	}

	mmc->ops = &meson_mmc_ops;
	mmc_add_host(mmc);

	return 0;

free_host:
	clk_disable_unprepare(host->cfg_div_clk);
	clk_disable_unprepare(host->core_clk);
	mmc_free_host(mmc);
	return ret;
}

static int meson_mmc_remove(struct platform_device *pdev)
{
	struct meson_host *host = dev_get_drvdata(&pdev->dev);

	if (WARN_ON(!host))
		return 0;

	if (host->bounce_buf)
		dma_free_coherent(host->dev, host->bounce_buf_size,
				  host->bounce_buf, host->bounce_dma_addr);

	clk_disable_unprepare(host->cfg_div_clk);
	clk_disable_unprepare(host->core_clk);

	mmc_free_host(host->mmc);
	return 0;
}

static const struct of_device_id meson_mmc_of_match[] = {
	{ .compatible = "amlogic,meson-gx-mmc", },
	{ .compatible = "amlogic,meson-gxbb-mmc", },
	{ .compatible = "amlogic,meson-gxl-mmc", },
	{ .compatible = "amlogic,meson-gxm-mmc", },
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

MODULE_DESCRIPTION("Amlogic S905*/GX* SD/eMMC driver");
MODULE_AUTHOR("Kevin Hilman <khilman@baylibre.com>");
MODULE_LICENSE("GPL v2");
