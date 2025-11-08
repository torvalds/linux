// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * meson-mx-sdio.c - Meson6, Meson8 and Meson8b SDIO/MMC Host Controller
 *
 * Copyright (C) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 * Copyright (C) 2017 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/types.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>

#define MESON_MX_SDIO_ARGU					0x00

#define MESON_MX_SDIO_SEND					0x04
	#define MESON_MX_SDIO_SEND_COMMAND_INDEX_MASK		GENMASK(7, 0)
	#define MESON_MX_SDIO_SEND_CMD_RESP_BITS_MASK		GENMASK(15, 8)
	#define MESON_MX_SDIO_SEND_RESP_WITHOUT_CRC7		BIT(16)
	#define MESON_MX_SDIO_SEND_RESP_HAS_DATA		BIT(17)
	#define MESON_MX_SDIO_SEND_RESP_CRC7_FROM_8		BIT(18)
	#define MESON_MX_SDIO_SEND_CHECK_DAT0_BUSY		BIT(19)
	#define MESON_MX_SDIO_SEND_DATA				BIT(20)
	#define MESON_MX_SDIO_SEND_USE_INT_WINDOW		BIT(21)
	#define MESON_MX_SDIO_SEND_REPEAT_PACKAGE_TIMES_MASK	GENMASK(31, 24)

#define MESON_MX_SDIO_CONF					0x08
	#define MESON_MX_SDIO_CONF_CMD_CLK_DIV_SHIFT		0
	#define MESON_MX_SDIO_CONF_CMD_CLK_DIV_WIDTH		10
	#define MESON_MX_SDIO_CONF_CMD_DISABLE_CRC		BIT(10)
	#define MESON_MX_SDIO_CONF_CMD_OUT_AT_POSITIVE_EDGE	BIT(11)
	#define MESON_MX_SDIO_CONF_CMD_ARGUMENT_BITS_MASK	GENMASK(17, 12)
	#define MESON_MX_SDIO_CONF_RESP_LATCH_AT_NEGATIVE_EDGE	BIT(18)
	#define MESON_MX_SDIO_CONF_DATA_LATCH_AT_NEGATIVE_EDGE	BIT(19)
	#define MESON_MX_SDIO_CONF_BUS_WIDTH			BIT(20)
	#define MESON_MX_SDIO_CONF_M_ENDIAN_MASK		GENMASK(22, 21)
	#define MESON_MX_SDIO_CONF_WRITE_NWR_MASK		GENMASK(28, 23)
	#define MESON_MX_SDIO_CONF_WRITE_CRC_OK_STATUS_MASK	GENMASK(31, 29)

#define MESON_MX_SDIO_IRQS					0x0c
	#define MESON_MX_SDIO_IRQS_STATUS_STATE_MACHINE_MASK	GENMASK(3, 0)
	#define MESON_MX_SDIO_IRQS_CMD_BUSY			BIT(4)
	#define MESON_MX_SDIO_IRQS_RESP_CRC7_OK			BIT(5)
	#define MESON_MX_SDIO_IRQS_DATA_READ_CRC16_OK		BIT(6)
	#define MESON_MX_SDIO_IRQS_DATA_WRITE_CRC16_OK		BIT(7)
	#define MESON_MX_SDIO_IRQS_IF_INT			BIT(8)
	#define MESON_MX_SDIO_IRQS_CMD_INT			BIT(9)
	#define MESON_MX_SDIO_IRQS_STATUS_INFO_MASK		GENMASK(15, 12)
	#define MESON_MX_SDIO_IRQS_TIMING_OUT_INT		BIT(16)
	#define MESON_MX_SDIO_IRQS_AMRISC_TIMING_OUT_INT_EN	BIT(17)
	#define MESON_MX_SDIO_IRQS_ARC_TIMING_OUT_INT_EN	BIT(18)
	#define MESON_MX_SDIO_IRQS_TIMING_OUT_COUNT_MASK	GENMASK(31, 19)

#define MESON_MX_SDIO_IRQC					0x10
	#define MESON_MX_SDIO_IRQC_ARC_IF_INT_EN		BIT(3)
	#define MESON_MX_SDIO_IRQC_ARC_CMD_INT_EN		BIT(4)
	#define MESON_MX_SDIO_IRQC_IF_CONFIG_MASK		GENMASK(7, 6)
	#define MESON_MX_SDIO_IRQC_FORCE_DATA_CLK		BIT(8)
	#define MESON_MX_SDIO_IRQC_FORCE_DATA_CMD		BIT(9)
	#define MESON_MX_SDIO_IRQC_FORCE_DATA_DAT_MASK		GENMASK(13, 10)
	#define MESON_MX_SDIO_IRQC_SOFT_RESET			BIT(15)
	#define MESON_MX_SDIO_IRQC_FORCE_HALT			BIT(30)
	#define MESON_MX_SDIO_IRQC_HALT_HOLE			BIT(31)

#define MESON_MX_SDIO_MULT					0x14
	#define MESON_MX_SDIO_MULT_PORT_SEL_MASK		GENMASK(1, 0)
	#define MESON_MX_SDIO_MULT_MEMORY_STICK_ENABLE		BIT(2)
	#define MESON_MX_SDIO_MULT_MEMORY_STICK_SCLK_ALWAYS	BIT(3)
	#define MESON_MX_SDIO_MULT_STREAM_ENABLE		BIT(4)
	#define MESON_MX_SDIO_MULT_STREAM_8BITS_MODE		BIT(5)
	#define MESON_MX_SDIO_MULT_WR_RD_OUT_INDEX		BIT(8)
	#define MESON_MX_SDIO_MULT_DAT0_DAT1_SWAPPED		BIT(10)
	#define MESON_MX_SDIO_MULT_DAT1_DAT0_SWAPPED		BIT(11)
	#define MESON_MX_SDIO_MULT_RESP_READ_INDEX_MASK		GENMASK(15, 12)

#define MESON_MX_SDIO_ADDR					0x18

#define MESON_MX_SDIO_EXT					0x1c
	#define MESON_MX_SDIO_EXT_DATA_RW_NUMBER_MASK		GENMASK(29, 16)

#define MESON_MX_SDIO_BOUNCE_REQ_SIZE				(128 * 1024)
#define MESON_MX_SDIO_RESPONSE_CRC16_BITS			(16 - 1)
#define MESON_MX_SDIO_MAX_SLOTS					3

struct meson_mx_mmc_host_clkc {
	struct clk_divider		cfg_div;
	struct clk_fixed_factor		fixed_div2;
};

struct meson_mx_mmc_host {
	struct device			*controller_dev;

	struct clk			*cfg_div_clk;
	struct regmap			*regmap;
	int				irq;
	spinlock_t			irq_lock;

	struct timer_list		cmd_timeout;

	unsigned int			slot_id;
	struct mmc_host			*mmc;

	struct mmc_request		*mrq;
	struct mmc_command		*cmd;
	int				error;
};

static void meson_mx_mmc_soft_reset(struct meson_mx_mmc_host *host)
{
	regmap_write(host->regmap, MESON_MX_SDIO_IRQC,
		     MESON_MX_SDIO_IRQC_SOFT_RESET);
	udelay(2);
}

static struct mmc_command *meson_mx_mmc_get_next_cmd(struct mmc_command *cmd)
{
	if (cmd->opcode == MMC_SET_BLOCK_COUNT && !cmd->error)
		return cmd->mrq->cmd;
	else if (mmc_op_multi(cmd->opcode) &&
		 (!cmd->mrq->sbc || cmd->error || cmd->data->error))
		return cmd->mrq->stop;
	else
		return NULL;
}

static void meson_mx_mmc_start_cmd(struct mmc_host *mmc,
				   struct mmc_command *cmd)
{
	struct meson_mx_mmc_host *host = mmc_priv(mmc);
	unsigned int pack_size;
	unsigned long irqflags, timeout;
	u32 send = 0, ext = 0;

	host->cmd = cmd;

	if (cmd->busy_timeout)
		timeout = msecs_to_jiffies(cmd->busy_timeout);
	else
		timeout = msecs_to_jiffies(1000);

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
	case MMC_RSP_R3:
		/* 7 (CMD) + 32 (response) + 7 (CRC) -1 */
		send |= FIELD_PREP(MESON_MX_SDIO_SEND_CMD_RESP_BITS_MASK, 45);
		break;
	case MMC_RSP_R2:
		/* 7 (CMD) + 120 (response) + 7 (CRC) -1 */
		send |= FIELD_PREP(MESON_MX_SDIO_SEND_CMD_RESP_BITS_MASK, 133);
		send |= MESON_MX_SDIO_SEND_RESP_CRC7_FROM_8;
		break;
	default:
		break;
	}

	if (!(cmd->flags & MMC_RSP_CRC))
		send |= MESON_MX_SDIO_SEND_RESP_WITHOUT_CRC7;

	if (cmd->flags & MMC_RSP_BUSY)
		send |= MESON_MX_SDIO_SEND_CHECK_DAT0_BUSY;

	if (cmd->data) {
		send |= FIELD_PREP(MESON_MX_SDIO_SEND_REPEAT_PACKAGE_TIMES_MASK,
				   (cmd->data->blocks - 1));

		pack_size = cmd->data->blksz * BITS_PER_BYTE;
		if (mmc->ios.bus_width == MMC_BUS_WIDTH_4)
			pack_size += MESON_MX_SDIO_RESPONSE_CRC16_BITS * 4;
		else
			pack_size += MESON_MX_SDIO_RESPONSE_CRC16_BITS * 1;

		ext |= FIELD_PREP(MESON_MX_SDIO_EXT_DATA_RW_NUMBER_MASK,
				  pack_size);

		if (cmd->data->flags & MMC_DATA_WRITE)
			send |= MESON_MX_SDIO_SEND_DATA;
		else
			send |= MESON_MX_SDIO_SEND_RESP_HAS_DATA;

		cmd->data->bytes_xfered = 0;
	}

	send |= FIELD_PREP(MESON_MX_SDIO_SEND_COMMAND_INDEX_MASK,
			   (0x40 | cmd->opcode));

	spin_lock_irqsave(&host->irq_lock, irqflags);

	regmap_update_bits(host->regmap, MESON_MX_SDIO_MULT,
			   MESON_MX_SDIO_MULT_PORT_SEL_MASK | BIT(31),
			   FIELD_PREP(MESON_MX_SDIO_MULT_PORT_SEL_MASK,
				      host->slot_id) | BIT(31));

	/* enable the CMD done interrupt */
	regmap_set_bits(host->regmap, MESON_MX_SDIO_IRQC,
			MESON_MX_SDIO_IRQC_ARC_CMD_INT_EN);

	/* clear pending interrupts */
	regmap_set_bits(host->regmap, MESON_MX_SDIO_IRQS,
			MESON_MX_SDIO_IRQS_CMD_INT);

	regmap_write(host->regmap, MESON_MX_SDIO_ARGU, cmd->arg);
	regmap_write(host->regmap, MESON_MX_SDIO_EXT, ext);
	regmap_write(host->regmap, MESON_MX_SDIO_SEND, send);

	spin_unlock_irqrestore(&host->irq_lock, irqflags);

	mod_timer(&host->cmd_timeout, jiffies + timeout);
}

static void meson_mx_mmc_request_done(struct meson_mx_mmc_host *host)
{
	struct mmc_request *mrq;

	mrq = host->mrq;

	if (host->cmd->error)
		meson_mx_mmc_soft_reset(host);

	host->mrq = NULL;
	host->cmd = NULL;

	mmc_request_done(host->mmc, mrq);
}

static void meson_mx_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct meson_mx_mmc_host *host = mmc_priv(mmc);
	unsigned short vdd = ios->vdd;
	unsigned long clk_rate = ios->clock;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		regmap_clear_bits(host->regmap, MESON_MX_SDIO_CONF,
				  MESON_MX_SDIO_CONF_BUS_WIDTH);
		break;

	case MMC_BUS_WIDTH_4:
		regmap_set_bits(host->regmap, MESON_MX_SDIO_CONF,
				MESON_MX_SDIO_CONF_BUS_WIDTH);
		break;

	case MMC_BUS_WIDTH_8:
	default:
		dev_err(mmc_dev(mmc), "unsupported bus width: %d\n",
			ios->bus_width);
		host->error = -EINVAL;
		return;
	}

	host->error = clk_set_rate(host->cfg_div_clk, ios->clock);
	if (host->error) {
		dev_warn(mmc_dev(mmc),
				"failed to set MMC clock to %lu: %d\n",
				clk_rate, host->error);
		return;
	}

	mmc->actual_clock = clk_get_rate(host->cfg_div_clk);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		vdd = 0;
		fallthrough;
	case MMC_POWER_UP:
		if (!IS_ERR(mmc->supply.vmmc)) {
			host->error = mmc_regulator_set_ocr(mmc,
							    mmc->supply.vmmc,
							    vdd);
			if (host->error)
				return;
		}
		break;
	}
}

static int meson_mx_mmc_map_dma(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	int dma_len;
	struct scatterlist *sg;

	if (!data)
		return 0;

	sg = data->sg;
	if (sg->offset & 3 || sg->length & 3) {
		dev_err(mmc_dev(mmc),
			"unaligned scatterlist: offset %x length %d\n",
			sg->offset, sg->length);
		return -EINVAL;
	}

	dma_len = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
	if (dma_len <= 0) {
		dev_err(mmc_dev(mmc), "dma_map_sg failed\n");
		return -ENOMEM;
	}

	return 0;
}

static void meson_mx_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct meson_mx_mmc_host *host = mmc_priv(mmc);
	struct mmc_command *cmd = mrq->cmd;

	if (!host->error)
		host->error = meson_mx_mmc_map_dma(mmc, mrq);

	if (host->error) {
		cmd->error = host->error;
		mmc_request_done(mmc, mrq);
		return;
	}

	host->mrq = mrq;

	if (mrq->data)
		regmap_write(host->regmap, MESON_MX_SDIO_ADDR,
			     sg_dma_address(mrq->data->sg));

	if (mrq->sbc)
		meson_mx_mmc_start_cmd(mmc, mrq->sbc);
	else
		meson_mx_mmc_start_cmd(mmc, mrq->cmd);
}

static void meson_mx_mmc_read_response(struct mmc_host *mmc,
				       struct mmc_command *cmd)
{
	struct meson_mx_mmc_host *host = mmc_priv(mmc);
	unsigned int i, resp[4];

	regmap_update_bits(host->regmap, MESON_MX_SDIO_MULT,
			   MESON_MX_SDIO_MULT_WR_RD_OUT_INDEX |
			   MESON_MX_SDIO_MULT_RESP_READ_INDEX_MASK,
			   MESON_MX_SDIO_MULT_WR_RD_OUT_INDEX |
			   FIELD_PREP(MESON_MX_SDIO_MULT_RESP_READ_INDEX_MASK,
				      0));

	if (cmd->flags & MMC_RSP_136) {
		for (i = 0; i <= 3; i++)
			regmap_read(host->regmap, MESON_MX_SDIO_ARGU,
				    &resp[3 - i]);

		cmd->resp[0] = (resp[0] << 8) | ((resp[1] >> 24) & 0xff);
		cmd->resp[1] = (resp[1] << 8) | ((resp[2] >> 24) & 0xff);
		cmd->resp[2] = (resp[2] << 8) | ((resp[3] >> 24) & 0xff);
		cmd->resp[3] = (resp[3] << 8);
	} else if (cmd->flags & MMC_RSP_PRESENT) {
		regmap_read(host->regmap, MESON_MX_SDIO_ARGU, &cmd->resp[0]);
	}
}

static irqreturn_t meson_mx_mmc_process_cmd_irq(struct meson_mx_mmc_host *host,
						u32 irqs, u32 send)
{
	struct mmc_command *cmd = host->cmd;

	/*
	 * NOTE: even though it shouldn't happen we sometimes get command
	 * interrupts twice (at least this is what it looks like). Ideally
	 * we find out why this happens and warn here as soon as it occurs.
	 */
	if (!cmd)
		return IRQ_HANDLED;

	cmd->error = 0;
	meson_mx_mmc_read_response(host->mmc, cmd);

	if (cmd->data) {
		if (!((irqs & MESON_MX_SDIO_IRQS_DATA_READ_CRC16_OK) ||
		      (irqs & MESON_MX_SDIO_IRQS_DATA_WRITE_CRC16_OK)))
			cmd->error = -EILSEQ;
	} else {
		if (!((irqs & MESON_MX_SDIO_IRQS_RESP_CRC7_OK) ||
		      (send & MESON_MX_SDIO_SEND_RESP_WITHOUT_CRC7)))
			cmd->error = -EILSEQ;
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t meson_mx_mmc_irq(int irq, void *data)
{
	struct meson_mx_mmc_host *host = (void *) data;
	u32 irqs, send;
	irqreturn_t ret;

	spin_lock(&host->irq_lock);

	regmap_read(host->regmap, MESON_MX_SDIO_IRQS, &irqs);
	regmap_read(host->regmap, MESON_MX_SDIO_SEND, &send);

	if (irqs & MESON_MX_SDIO_IRQS_CMD_INT)
		ret = meson_mx_mmc_process_cmd_irq(host, irqs, send);
	else
		ret = IRQ_HANDLED;

	/* finally ACK all pending interrupts */
	regmap_write(host->regmap, MESON_MX_SDIO_IRQS, irqs);

	spin_unlock(&host->irq_lock);

	return ret;
}

static irqreturn_t meson_mx_mmc_irq_thread(int irq, void *irq_data)
{
	struct meson_mx_mmc_host *host = (void *) irq_data;
	struct mmc_command *cmd = host->cmd, *next_cmd;

	if (WARN_ON(!cmd))
		return IRQ_HANDLED;

	timer_delete_sync(&host->cmd_timeout);

	if (cmd->data) {
		dma_unmap_sg(mmc_dev(host->mmc), cmd->data->sg,
			     cmd->data->sg_len, mmc_get_dma_dir(cmd->data));

		cmd->data->bytes_xfered = cmd->data->blksz * cmd->data->blocks;
	}

	next_cmd = meson_mx_mmc_get_next_cmd(cmd);
	if (next_cmd)
		meson_mx_mmc_start_cmd(host->mmc, next_cmd);
	else
		meson_mx_mmc_request_done(host);

	return IRQ_HANDLED;
}

static void meson_mx_mmc_timeout(struct timer_list *t)
{
	struct meson_mx_mmc_host *host = timer_container_of(host, t,
							    cmd_timeout);
	unsigned long irqflags;
	u32 irqs, argu;

	spin_lock_irqsave(&host->irq_lock, irqflags);

	/* disable the CMD interrupt */
	regmap_clear_bits(host->regmap, MESON_MX_SDIO_IRQC,
			  MESON_MX_SDIO_IRQC_ARC_CMD_INT_EN);

	spin_unlock_irqrestore(&host->irq_lock, irqflags);

	/*
	 * skip the timeout handling if the interrupt handler already processed
	 * the command.
	 */
	if (!host->cmd)
		return;

	regmap_read(host->regmap, MESON_MX_SDIO_IRQS, &irqs);
	regmap_read(host->regmap, MESON_MX_SDIO_ARGU, &argu);

	dev_dbg(mmc_dev(host->mmc),
		"Timeout on CMD%u (IRQS = 0x%08x, ARGU = 0x%08x)\n",
		host->cmd->opcode, irqs, argu);

	host->cmd->error = -ETIMEDOUT;

	meson_mx_mmc_request_done(host);
}

static struct mmc_host_ops meson_mx_mmc_ops = {
	.request		= meson_mx_mmc_request,
	.set_ios		= meson_mx_mmc_set_ios,
	.get_cd			= mmc_gpio_get_cd,
	.get_ro			= mmc_gpio_get_ro,
};

static struct platform_device *meson_mx_mmc_slot_pdev(struct device *parent)
{
	struct platform_device *pdev = NULL;

	for_each_available_child_of_node_scoped(parent->of_node, slot_node) {
		if (!of_device_is_compatible(slot_node, "mmc-slot"))
			continue;

		/*
		 * TODO: the MMC core framework currently does not support
		 * controllers with multiple slots properly. So we only
		 * register the first slot for now.
		 */
		if (pdev) {
			dev_warn(parent,
				 "more than one 'mmc-slot' compatible child found - using the first one and ignoring all subsequent ones\n");
			break;
		}

		pdev = of_platform_device_create(slot_node, NULL, parent);
		if (!pdev)
			dev_err(parent,
				"Failed to create platform device for mmc-slot node '%pOF'\n",
				slot_node);
	}

	return pdev;
}

static int meson_mx_mmc_add_host(struct meson_mx_mmc_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct device *slot_dev = mmc_dev(mmc);
	int ret;

	if (of_property_read_u32(slot_dev->of_node, "reg", &host->slot_id))
		return dev_err_probe(slot_dev, -EINVAL,
				     "missing 'reg' property\n");

	if (host->slot_id >= MESON_MX_SDIO_MAX_SLOTS)
		return dev_err_probe(slot_dev, -EINVAL,
				     "invalid 'reg' property value %d\n",
				     host->slot_id);

	/* Get regulators and the supported OCR mask */
	ret = mmc_regulator_get_supply(mmc);
	if (ret)
		return ret;

	mmc->max_req_size = MESON_MX_SDIO_BOUNCE_REQ_SIZE;
	mmc->max_seg_size = mmc->max_req_size;
	mmc->max_blk_count =
		FIELD_GET(MESON_MX_SDIO_SEND_REPEAT_PACKAGE_TIMES_MASK,
			  0xffffffff);
	mmc->max_blk_size = FIELD_GET(MESON_MX_SDIO_EXT_DATA_RW_NUMBER_MASK,
				      0xffffffff);
	mmc->max_blk_size -= (4 * MESON_MX_SDIO_RESPONSE_CRC16_BITS);
	mmc->max_blk_size /= BITS_PER_BYTE;

	/* Get the min and max supported clock rates */
	mmc->f_min = clk_round_rate(host->cfg_div_clk, 1);
	mmc->f_max = clk_round_rate(host->cfg_div_clk, ULONG_MAX);

	mmc->caps |= MMC_CAP_CMD23 | MMC_CAP_WAIT_WHILE_BUSY;
	mmc->ops = &meson_mx_mmc_ops;

	ret = mmc_of_parse(mmc);
	if (ret)
		return ret;

	ret = mmc_add_host(mmc);
	if (ret)
		return ret;

	return 0;
}

static struct clk *meson_mx_mmc_register_clk(struct device *dev,
					     void __iomem *base)
{
	const char *fixed_div2_name, *cfg_div_name;
	struct meson_mx_mmc_host_clkc *host_clkc;
	struct clk *clk;
	int ret;

	/* use a dedicated memory allocation for the clock controller to
	 * prevent use-after-free as meson_mx_mmc_host is free'd before
	 * dev (controller dev, not mmc_host->dev) is free'd.
	 */
	host_clkc = devm_kzalloc(dev, sizeof(*host_clkc), GFP_KERNEL);
	if (!host_clkc)
		return ERR_PTR(-ENOMEM);

	fixed_div2_name = devm_kasprintf(dev, GFP_KERNEL, "%s#fixed_div2",
					 dev_name(dev));
	if (!fixed_div2_name)
		return ERR_PTR(-ENOMEM);

	host_clkc->fixed_div2.div = 2;
	host_clkc->fixed_div2.mult = 1;
	host_clkc->fixed_div2.hw.init = CLK_HW_INIT_FW_NAME(fixed_div2_name,
							    "clkin",
							    &clk_fixed_factor_ops,
							    0);
	ret = devm_clk_hw_register(dev, &host_clkc->fixed_div2.hw);
	if (ret)
		return dev_err_ptr_probe(dev, ret,
					 "Failed to register %s clock\n",
					 fixed_div2_name);

	cfg_div_name = devm_kasprintf(dev, GFP_KERNEL, "%s#div", dev_name(dev));
	if (!cfg_div_name)
		return ERR_PTR(-ENOMEM);

	host_clkc->cfg_div.reg = base + MESON_MX_SDIO_CONF;
	host_clkc->cfg_div.shift = MESON_MX_SDIO_CONF_CMD_CLK_DIV_SHIFT;
	host_clkc->cfg_div.width = MESON_MX_SDIO_CONF_CMD_CLK_DIV_WIDTH;
	host_clkc->cfg_div.hw.init = CLK_HW_INIT_HW(cfg_div_name,
						    &host_clkc->fixed_div2.hw,
						    &clk_divider_ops,
						    CLK_DIVIDER_ALLOW_ZERO);
	ret = devm_clk_hw_register(dev, &host_clkc->cfg_div.hw);
	if (ret)
		return dev_err_ptr_probe(dev, ret,
					 "Failed to register %s clock\n",
					 cfg_div_name);

	clk = devm_clk_hw_get_clk(dev, &host_clkc->cfg_div.hw, "cfg_div_clk");
	if (IS_ERR(clk))
		return dev_err_ptr_probe(dev, PTR_ERR(clk),
					 "Failed to get the cfg_div clock\n");

	return clk;
}

static int meson_mx_mmc_probe(struct platform_device *pdev)
{
	const struct regmap_config meson_mx_sdio_regmap_config = {
		.reg_bits = 8,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = MESON_MX_SDIO_EXT,
	};
	struct platform_device *slot_pdev;
	struct mmc_host *mmc;
	struct meson_mx_mmc_host *host;
	struct clk *core_clk;
	void __iomem *base;
	int ret, irq;
	u32 conf;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	slot_pdev = meson_mx_mmc_slot_pdev(&pdev->dev);
	if (!slot_pdev)
		return -ENODEV;

	mmc = devm_mmc_alloc_host(&slot_pdev->dev, sizeof(*host));
	if (!mmc) {
		ret = -ENOMEM;
		goto error_unregister_slot_pdev;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->controller_dev = &pdev->dev;

	spin_lock_init(&host->irq_lock);
	timer_setup(&host->cmd_timeout, meson_mx_mmc_timeout, 0);

	platform_set_drvdata(pdev, host);

	host->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					     &meson_mx_sdio_regmap_config);
	if (IS_ERR(host->regmap)) {
		ret = dev_err_probe(host->controller_dev, PTR_ERR(host->regmap),
				    "Failed to initialize regmap\n");
		goto error_unregister_slot_pdev;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto error_unregister_slot_pdev;
	}

	ret = devm_request_threaded_irq(host->controller_dev, irq,
					meson_mx_mmc_irq,
					meson_mx_mmc_irq_thread, IRQF_ONESHOT,
					NULL, host);
	if (ret) {
		dev_err_probe(host->controller_dev, ret,
			      "Failed to request IRQ\n");
		goto error_unregister_slot_pdev;
	}

	core_clk = devm_clk_get_enabled(host->controller_dev, "core");
	if (IS_ERR(core_clk)) {
		ret = dev_err_probe(host->controller_dev, PTR_ERR(core_clk),
				    "Failed to get and enable 'core' clock\n");
		goto error_unregister_slot_pdev;
	}

	host->cfg_div_clk = meson_mx_mmc_register_clk(&pdev->dev, base);
	if (IS_ERR(host->cfg_div_clk)) {
		ret = PTR_ERR(host->cfg_div_clk);
		goto error_unregister_slot_pdev;
	}

	ret = clk_prepare_enable(host->cfg_div_clk);
	if (ret) {
		dev_err_probe(host->controller_dev, ret,
			      "Failed to enable MMC (cfg div) clock\n");
		goto error_unregister_slot_pdev;
	}

	conf = 0;
	conf |= FIELD_PREP(MESON_MX_SDIO_CONF_CMD_ARGUMENT_BITS_MASK, 39);
	conf |= FIELD_PREP(MESON_MX_SDIO_CONF_M_ENDIAN_MASK, 0x3);
	conf |= FIELD_PREP(MESON_MX_SDIO_CONF_WRITE_NWR_MASK, 0x2);
	conf |= FIELD_PREP(MESON_MX_SDIO_CONF_WRITE_CRC_OK_STATUS_MASK, 0x2);
	regmap_write(host->regmap, MESON_MX_SDIO_CONF, conf);

	meson_mx_mmc_soft_reset(host);

	ret = meson_mx_mmc_add_host(host);
	if (ret)
		goto error_disable_div_clk;

	return 0;

error_disable_div_clk:
	clk_disable_unprepare(host->cfg_div_clk);
error_unregister_slot_pdev:
	of_platform_device_destroy(&slot_pdev->dev, NULL);
	return ret;
}

static void meson_mx_mmc_remove(struct platform_device *pdev)
{
	struct meson_mx_mmc_host *host = platform_get_drvdata(pdev);
	struct device *slot_dev = mmc_dev(host->mmc);

	timer_delete_sync(&host->cmd_timeout);

	mmc_remove_host(host->mmc);

	of_platform_device_destroy(slot_dev, NULL);

	clk_disable_unprepare(host->cfg_div_clk);
}

static const struct of_device_id meson_mx_mmc_of_match[] = {
	{ .compatible = "amlogic,meson8-sdio", },
	{ .compatible = "amlogic,meson8b-sdio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_mx_mmc_of_match);

static struct platform_driver meson_mx_mmc_driver = {
	.probe   = meson_mx_mmc_probe,
	.remove = meson_mx_mmc_remove,
	.driver  = {
		.name = "meson-mx-sdio",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(meson_mx_mmc_of_match),
	},
};

module_platform_driver(meson_mx_mmc_driver);

MODULE_DESCRIPTION("Meson6, Meson8 and Meson8b SDIO/MMC Host Driver");
MODULE_AUTHOR("Carlo Caione <carlo@endlessm.com>");
MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_LICENSE("GPL v2");
