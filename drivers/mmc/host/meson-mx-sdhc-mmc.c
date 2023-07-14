// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson6/Meson8/Meson8b/Meson8m2 SDHC MMC host controller driver.
 *
 * Copyright (C) 2020 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>

#include "meson-mx-sdhc.h"

#define MESON_SDHC_NUM_BULK_CLKS				4
#define MESON_SDHC_MAX_BLK_SIZE					512
#define MESON_SDHC_NUM_TUNING_TRIES				10

#define MESON_SDHC_WAIT_CMD_READY_SLEEP_US			1
#define MESON_SDHC_WAIT_CMD_READY_TIMEOUT_US			100000
#define MESON_SDHC_WAIT_BEFORE_SEND_SLEEP_US			1
#define MESON_SDHC_WAIT_BEFORE_SEND_TIMEOUT_US			200

struct meson_mx_sdhc_data {
	void		(*init_hw)(struct mmc_host *mmc);
	void		(*set_pdma)(struct mmc_host *mmc);
	void		(*wait_before_send)(struct mmc_host *mmc);
	bool		hardware_flush_all_cmds;
};

struct meson_mx_sdhc_host {
	struct mmc_host			*mmc;

	struct mmc_request		*mrq;
	struct mmc_command		*cmd;
	int				error;

	struct regmap			*regmap;

	struct clk			*pclk;
	struct clk			*sd_clk;
	struct clk_bulk_data		bulk_clks[MESON_SDHC_NUM_BULK_CLKS];
	bool				bulk_clks_enabled;

	const struct meson_mx_sdhc_data	*platform;
};

static const struct regmap_config meson_mx_sdhc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = MESON_SDHC_CLK2,
};

static void meson_mx_sdhc_hw_reset(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);

	regmap_write(host->regmap, MESON_SDHC_SRST, MESON_SDHC_SRST_MAIN_CTRL |
		     MESON_SDHC_SRST_RXFIFO | MESON_SDHC_SRST_TXFIFO |
		     MESON_SDHC_SRST_DPHY_RX | MESON_SDHC_SRST_DPHY_TX |
		     MESON_SDHC_SRST_DMA_IF);
	usleep_range(10, 100);

	regmap_write(host->regmap, MESON_SDHC_SRST, 0);
	usleep_range(10, 100);
}

static void meson_mx_sdhc_clear_fifo(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	u32 stat;

	regmap_read(host->regmap, MESON_SDHC_STAT, &stat);
	if (!FIELD_GET(MESON_SDHC_STAT_RXFIFO_CNT, stat) &&
	    !FIELD_GET(MESON_SDHC_STAT_TXFIFO_CNT, stat))
		return;

	regmap_write(host->regmap, MESON_SDHC_SRST, MESON_SDHC_SRST_RXFIFO |
		     MESON_SDHC_SRST_TXFIFO | MESON_SDHC_SRST_MAIN_CTRL);
	udelay(5);

	regmap_read(host->regmap, MESON_SDHC_STAT, &stat);
	if (FIELD_GET(MESON_SDHC_STAT_RXFIFO_CNT, stat) ||
	    FIELD_GET(MESON_SDHC_STAT_TXFIFO_CNT, stat))
		dev_warn(mmc_dev(host->mmc),
			 "Failed to clear FIFOs, RX: %lu, TX: %lu\n",
			 FIELD_GET(MESON_SDHC_STAT_RXFIFO_CNT, stat),
			 FIELD_GET(MESON_SDHC_STAT_TXFIFO_CNT, stat));
}

static void meson_mx_sdhc_wait_cmd_ready(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	u32 stat, esta;
	int ret;

	ret = regmap_read_poll_timeout(host->regmap, MESON_SDHC_STAT, stat,
				       !(stat & MESON_SDHC_STAT_CMD_BUSY),
				       MESON_SDHC_WAIT_CMD_READY_SLEEP_US,
				       MESON_SDHC_WAIT_CMD_READY_TIMEOUT_US);
	if (ret) {
		dev_warn(mmc_dev(mmc),
			 "Failed to poll for CMD_BUSY while processing CMD%d\n",
			 host->cmd->opcode);
		meson_mx_sdhc_hw_reset(mmc);
	}

	ret = regmap_read_poll_timeout(host->regmap, MESON_SDHC_ESTA, esta,
				       !(esta & MESON_SDHC_ESTA_11_13),
				       MESON_SDHC_WAIT_CMD_READY_SLEEP_US,
				       MESON_SDHC_WAIT_CMD_READY_TIMEOUT_US);
	if (ret) {
		dev_warn(mmc_dev(mmc),
			 "Failed to poll for ESTA[13:11] while processing CMD%d\n",
			 host->cmd->opcode);
		meson_mx_sdhc_hw_reset(mmc);
	}
}

static void meson_mx_sdhc_start_cmd(struct mmc_host *mmc,
				    struct mmc_command *cmd)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	bool manual_stop = false;
	u32 ictl, send;
	int pack_len;

	host->cmd = cmd;

	ictl = MESON_SDHC_ICTL_DATA_TIMEOUT | MESON_SDHC_ICTL_DATA_ERR_CRC |
	       MESON_SDHC_ICTL_RXFIFO_FULL | MESON_SDHC_ICTL_TXFIFO_EMPTY |
	       MESON_SDHC_ICTL_RESP_TIMEOUT | MESON_SDHC_ICTL_RESP_ERR_CRC;

	send = FIELD_PREP(MESON_SDHC_SEND_CMD_INDEX, cmd->opcode);

	if (cmd->data) {
		send |= MESON_SDHC_SEND_CMD_HAS_DATA;
		send |= FIELD_PREP(MESON_SDHC_SEND_TOTAL_PACK,
				   cmd->data->blocks - 1);

		if (cmd->data->blksz < MESON_SDHC_MAX_BLK_SIZE)
			pack_len = cmd->data->blksz;
		else
			pack_len = 0;

		if (cmd->data->flags & MMC_DATA_WRITE)
			send |= MESON_SDHC_SEND_DATA_DIR;

		/*
		 * If command with no data, just wait response done
		 * interrupt(int[0]), and if command with data transfer, just
		 * wait dma done interrupt(int[11]), don't need care about
		 * dat0 busy or not.
		 */
		if (host->platform->hardware_flush_all_cmds ||
		    cmd->data->flags & MMC_DATA_WRITE)
			/* hardware flush: */
			ictl |= MESON_SDHC_ICTL_DMA_DONE;
		else
			/* software flush: */
			ictl |= MESON_SDHC_ICTL_DATA_XFER_OK;

		/*
		 * Mimic the logic from the vendor driver where (only)
		 * SD_IO_RW_EXTENDED commands with more than one block set the
		 * MESON_SDHC_MISC_MANUAL_STOP bit. This fixes the firmware
		 * download in the brcmfmac driver for a BCM43362/1 card.
		 * Without this sdio_memcpy_toio() (with a size of 219557
		 * bytes) times out if MESON_SDHC_MISC_MANUAL_STOP is not set.
		 */
		manual_stop = cmd->data->blocks > 1 &&
			      cmd->opcode == SD_IO_RW_EXTENDED;
	} else {
		pack_len = 0;

		ictl |= MESON_SDHC_ICTL_RESP_OK;
	}

	regmap_update_bits(host->regmap, MESON_SDHC_MISC,
			   MESON_SDHC_MISC_MANUAL_STOP,
			   manual_stop ? MESON_SDHC_MISC_MANUAL_STOP : 0);

	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		send |= MESON_SDHC_SEND_DATA_STOP;

	if (cmd->flags & MMC_RSP_PRESENT)
		send |= MESON_SDHC_SEND_CMD_HAS_RESP;

	if (cmd->flags & MMC_RSP_136) {
		send |= MESON_SDHC_SEND_RESP_LEN;
		send |= MESON_SDHC_SEND_RESP_NO_CRC;
	}

	if (!(cmd->flags & MMC_RSP_CRC))
		send |= MESON_SDHC_SEND_RESP_NO_CRC;

	if (cmd->flags & MMC_RSP_BUSY)
		send |= MESON_SDHC_SEND_R1B;

	/* enable the new IRQs and mask all pending ones */
	regmap_write(host->regmap, MESON_SDHC_ICTL, ictl);
	regmap_write(host->regmap, MESON_SDHC_ISTA, MESON_SDHC_ISTA_ALL_IRQS);

	regmap_write(host->regmap, MESON_SDHC_ARGU, cmd->arg);

	regmap_update_bits(host->regmap, MESON_SDHC_CTRL,
			   MESON_SDHC_CTRL_PACK_LEN,
			   FIELD_PREP(MESON_SDHC_CTRL_PACK_LEN, pack_len));

	if (cmd->data)
		regmap_write(host->regmap, MESON_SDHC_ADDR,
			     sg_dma_address(cmd->data->sg));

	meson_mx_sdhc_wait_cmd_ready(mmc);

	if (cmd->data)
		host->platform->set_pdma(mmc);

	if (host->platform->wait_before_send)
		host->platform->wait_before_send(mmc);

	regmap_write(host->regmap, MESON_SDHC_SEND, send);
}

static void meson_mx_sdhc_disable_clks(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);

	if (!host->bulk_clks_enabled)
		return;

	clk_bulk_disable_unprepare(MESON_SDHC_NUM_BULK_CLKS, host->bulk_clks);

	host->bulk_clks_enabled = false;
}

static int meson_mx_sdhc_enable_clks(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	int ret;

	if (host->bulk_clks_enabled)
		return 0;

	ret = clk_bulk_prepare_enable(MESON_SDHC_NUM_BULK_CLKS,
				      host->bulk_clks);
	if (ret)
		return ret;

	host->bulk_clks_enabled = true;

	return 0;
}

static int meson_mx_sdhc_set_clk(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	u32 rx_clk_phase;
	int ret;

	meson_mx_sdhc_disable_clks(mmc);

	if (ios->clock) {
		ret = clk_set_rate(host->sd_clk, ios->clock);
		if (ret) {
			dev_warn(mmc_dev(mmc),
				 "Failed to set MMC clock to %uHz: %d\n",
				 ios->clock, host->error);
			return ret;
		}

		ret = meson_mx_sdhc_enable_clks(mmc);
		if (ret)
			return ret;

		mmc->actual_clock = clk_get_rate(host->sd_clk);

		/*
		 * according to Amlogic the following latching points are
		 * selected with empirical values, there is no (known) formula
		 * to calculate these.
		 */
		if (mmc->actual_clock > 100000000) {
			rx_clk_phase = 1;
		} else if (mmc->actual_clock > 45000000) {
			if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330)
				rx_clk_phase = 15;
			else
				rx_clk_phase = 11;
		} else if (mmc->actual_clock >= 25000000) {
			rx_clk_phase = 15;
		} else if (mmc->actual_clock > 5000000) {
			rx_clk_phase = 23;
		} else if (mmc->actual_clock > 1000000) {
			rx_clk_phase = 55;
		} else {
			rx_clk_phase = 1061;
		}

		regmap_update_bits(host->regmap, MESON_SDHC_CLK2,
				   MESON_SDHC_CLK2_RX_CLK_PHASE,
				   FIELD_PREP(MESON_SDHC_CLK2_RX_CLK_PHASE,
					      rx_clk_phase));
	} else {
		mmc->actual_clock = 0;
	}

	return 0;
}

static void meson_mx_sdhc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	unsigned short vdd = ios->vdd;

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

	case MMC_POWER_ON:
		break;
	}

	host->error = meson_mx_sdhc_set_clk(mmc, ios);
	if (host->error)
		return;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		regmap_update_bits(host->regmap, MESON_SDHC_CTRL,
				   MESON_SDHC_CTRL_DAT_TYPE,
				   FIELD_PREP(MESON_SDHC_CTRL_DAT_TYPE, 0));
		break;

	case MMC_BUS_WIDTH_4:
		regmap_update_bits(host->regmap, MESON_SDHC_CTRL,
				   MESON_SDHC_CTRL_DAT_TYPE,
				   FIELD_PREP(MESON_SDHC_CTRL_DAT_TYPE, 1));
		break;

	case MMC_BUS_WIDTH_8:
		regmap_update_bits(host->regmap, MESON_SDHC_CTRL,
				   MESON_SDHC_CTRL_DAT_TYPE,
				   FIELD_PREP(MESON_SDHC_CTRL_DAT_TYPE, 2));
		break;

	default:
		dev_err(mmc_dev(mmc), "unsupported bus width: %d\n",
			ios->bus_width);
		host->error = -EINVAL;
		return;
	}
}

static int meson_mx_sdhc_map_dma(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	unsigned int dma_len;

	if (!data)
		return 0;

	dma_len = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
	if (!dma_len) {
		dev_err(mmc_dev(mmc), "dma_map_sg failed\n");
		return -ENOMEM;
	}

	return 0;
}

static void meson_mx_sdhc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	struct mmc_command *cmd = mrq->cmd;

	if (!host->error)
		host->error = meson_mx_sdhc_map_dma(mmc, mrq);

	if (host->error) {
		cmd->error = host->error;
		mmc_request_done(mmc, mrq);
		return;
	}

	host->mrq = mrq;

	meson_mx_sdhc_start_cmd(mmc, mrq->cmd);
}

static int meson_mx_sdhc_card_busy(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	u32 stat;

	regmap_read(host->regmap, MESON_SDHC_STAT, &stat);
	return FIELD_GET(MESON_SDHC_STAT_DAT3_0, stat) == 0;
}

static bool meson_mx_sdhc_tuning_point_matches(struct mmc_host *mmc,
					       u32 opcode)
{
	unsigned int i, num_matches = 0;
	int ret;

	for (i = 0; i < MESON_SDHC_NUM_TUNING_TRIES; i++) {
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			num_matches++;
	}

	return num_matches == MESON_SDHC_NUM_TUNING_TRIES;
}

static int meson_mx_sdhc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	int div, start, len, best_start, best_len;
	int curr_phase, old_phase, new_phase;
	u32 val;

	len = 0;
	start = 0;
	best_len = 0;

	regmap_read(host->regmap, MESON_SDHC_CLK2, &val);
	old_phase = FIELD_GET(MESON_SDHC_CLK2_RX_CLK_PHASE, val);

	regmap_read(host->regmap, MESON_SDHC_CLKC, &val);
	div = FIELD_GET(MESON_SDHC_CLKC_CLK_DIV, val);

	for (curr_phase = 0; curr_phase <= div; curr_phase++) {
		regmap_update_bits(host->regmap, MESON_SDHC_CLK2,
				   MESON_SDHC_CLK2_RX_CLK_PHASE,
				   FIELD_PREP(MESON_SDHC_CLK2_RX_CLK_PHASE,
					      curr_phase));

		if (meson_mx_sdhc_tuning_point_matches(mmc, opcode)) {
			if (!len) {
				start = curr_phase;

				dev_dbg(mmc_dev(mmc),
					"New RX phase window starts at %u\n",
					start);
			}

			len++;
		} else {
			if (len > best_len) {
				best_start = start;
				best_len = len;

				dev_dbg(mmc_dev(mmc),
					"New best RX phase window: %u - %u\n",
					best_start, best_start + best_len);
			}

			/* reset the current window */
			len = 0;
		}
	}

	if (len > best_len)
		/* the last window is the best (or possibly only) window */
		new_phase = start + (len / 2);
	else if (best_len)
		/* there was a better window than the last */
		new_phase = best_start + (best_len / 2);
	else
		/* no window was found at all, reset to the original phase */
		new_phase = old_phase;

	regmap_update_bits(host->regmap, MESON_SDHC_CLK2,
			   MESON_SDHC_CLK2_RX_CLK_PHASE,
			   FIELD_PREP(MESON_SDHC_CLK2_RX_CLK_PHASE,
				      new_phase));

	if (!len && !best_len)
		return -EIO;

	dev_dbg(mmc_dev(mmc), "Tuned RX clock phase to %u\n", new_phase);

	return 0;
}

static const struct mmc_host_ops meson_mx_sdhc_ops = {
	.card_hw_reset			= meson_mx_sdhc_hw_reset,
	.request			= meson_mx_sdhc_request,
	.set_ios			= meson_mx_sdhc_set_ios,
	.card_busy			= meson_mx_sdhc_card_busy,
	.execute_tuning			= meson_mx_sdhc_execute_tuning,
	.get_cd				= mmc_gpio_get_cd,
	.get_ro				= mmc_gpio_get_ro,
};

static void meson_mx_sdhc_request_done(struct meson_mx_sdhc_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_host *mmc = host->mmc;

	/* disable interrupts and mask all pending ones */
	regmap_update_bits(host->regmap, MESON_SDHC_ICTL,
			   MESON_SDHC_ICTL_ALL_IRQS, 0);
	regmap_update_bits(host->regmap, MESON_SDHC_ISTA,
			   MESON_SDHC_ISTA_ALL_IRQS, MESON_SDHC_ISTA_ALL_IRQS);

	host->mrq = NULL;
	host->cmd = NULL;

	mmc_request_done(mmc, mrq);
}

static u32 meson_mx_sdhc_read_response(struct meson_mx_sdhc_host *host, u8 idx)
{
	u32 val;

	regmap_update_bits(host->regmap, MESON_SDHC_PDMA,
			   MESON_SDHC_PDMA_DMA_MODE, 0);

	regmap_update_bits(host->regmap, MESON_SDHC_PDMA,
			   MESON_SDHC_PDMA_PIO_RDRESP,
			   FIELD_PREP(MESON_SDHC_PDMA_PIO_RDRESP, idx));

	regmap_read(host->regmap, MESON_SDHC_ARGU, &val);

	return val;
}

static irqreturn_t meson_mx_sdhc_irq(int irq, void *data)
{
	struct meson_mx_sdhc_host *host = data;
	struct mmc_command *cmd = host->cmd;
	u32 ictl, ista;

	regmap_read(host->regmap, MESON_SDHC_ICTL, &ictl);
	regmap_read(host->regmap, MESON_SDHC_ISTA, &ista);

	if (!(ictl & ista))
		return IRQ_NONE;

	if (ista & MESON_SDHC_ISTA_RXFIFO_FULL ||
	    ista & MESON_SDHC_ISTA_TXFIFO_EMPTY)
		cmd->error = -EIO;
	else if (ista & MESON_SDHC_ISTA_RESP_ERR_CRC)
		cmd->error = -EILSEQ;
	else if (ista & MESON_SDHC_ISTA_RESP_TIMEOUT)
		cmd->error = -ETIMEDOUT;

	if (cmd->data) {
		if (ista & MESON_SDHC_ISTA_DATA_ERR_CRC)
			cmd->data->error = -EILSEQ;
		else if (ista & MESON_SDHC_ISTA_DATA_TIMEOUT)
			cmd->data->error = -ETIMEDOUT;
	}

	if (cmd->error || (cmd->data && cmd->data->error))
		dev_dbg(mmc_dev(host->mmc), "CMD%d error, ISTA: 0x%08x\n",
			cmd->opcode, ista);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t meson_mx_sdhc_irq_thread(int irq, void *irq_data)
{
	struct meson_mx_sdhc_host *host = irq_data;
	struct mmc_command *cmd;
	u32 val;

	cmd = host->cmd;
	if (WARN_ON(!cmd))
		return IRQ_HANDLED;

	if (cmd->data && !cmd->data->error) {
		if (!host->platform->hardware_flush_all_cmds &&
		    cmd->data->flags & MMC_DATA_READ) {
			meson_mx_sdhc_wait_cmd_ready(host->mmc);

			/*
			 * If MESON_SDHC_PDMA_RXFIFO_MANUAL_FLUSH was
			 * previously 0x1 then it has to be set to 0x3. If it
			 * was 0x0 before then it has to be set to 0x2. Without
			 * this reading SD cards sometimes transfers garbage,
			 * which results in cards not being detected due to:
			 *   unrecognised SCR structure version <random number>
			 */
			val = FIELD_PREP(MESON_SDHC_PDMA_RXFIFO_MANUAL_FLUSH,
					 2);
			regmap_update_bits(host->regmap, MESON_SDHC_PDMA, val,
					   val);
		}

		dma_unmap_sg(mmc_dev(host->mmc), cmd->data->sg,
			     cmd->data->sg_len, mmc_get_dma_dir(cmd->data));

		cmd->data->bytes_xfered = cmd->data->blksz * cmd->data->blocks;
	}

	meson_mx_sdhc_wait_cmd_ready(host->mmc);

	if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = meson_mx_sdhc_read_response(host, 4);
		cmd->resp[1] = meson_mx_sdhc_read_response(host, 3);
		cmd->resp[2] = meson_mx_sdhc_read_response(host, 2);
		cmd->resp[3] = meson_mx_sdhc_read_response(host, 1);
	} else {
		cmd->resp[0] = meson_mx_sdhc_read_response(host, 0);
	}

	if (cmd->error == -EIO || cmd->error == -ETIMEDOUT)
		meson_mx_sdhc_hw_reset(host->mmc);
	else if (cmd->data)
		/*
		 * Clear the FIFOs after completing data transfers to prevent
		 * corrupting data on write access. It's not clear why this is
		 * needed (for reads and writes), but it mimics what the BSP
		 * kernel did.
		 */
		meson_mx_sdhc_clear_fifo(host->mmc);

	meson_mx_sdhc_request_done(host);

	return IRQ_HANDLED;
}

static void meson_mx_sdhc_init_hw_meson8(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);

	regmap_write(host->regmap, MESON_SDHC_MISC,
		     FIELD_PREP(MESON_SDHC_MISC_TXSTART_THRES, 7) |
		     FIELD_PREP(MESON_SDHC_MISC_WCRC_ERR_PATT, 5) |
		     FIELD_PREP(MESON_SDHC_MISC_WCRC_OK_PATT, 2));

	regmap_write(host->regmap, MESON_SDHC_ENHC,
		     FIELD_PREP(MESON_SDHC_ENHC_RXFIFO_TH, 63) |
		     MESON_SDHC_ENHC_MESON6_DMA_WR_RESP |
		     FIELD_PREP(MESON_SDHC_ENHC_MESON6_RX_TIMEOUT, 255) |
		     FIELD_PREP(MESON_SDHC_ENHC_SDIO_IRQ_PERIOD, 12));
};

static void meson_mx_sdhc_set_pdma_meson8(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);

	if (host->cmd->data->flags & MMC_DATA_WRITE)
		regmap_update_bits(host->regmap, MESON_SDHC_PDMA,
				   MESON_SDHC_PDMA_DMA_MODE |
				   MESON_SDHC_PDMA_RD_BURST |
				   MESON_SDHC_PDMA_TXFIFO_FILL,
				   MESON_SDHC_PDMA_DMA_MODE |
				   FIELD_PREP(MESON_SDHC_PDMA_RD_BURST, 31) |
				   MESON_SDHC_PDMA_TXFIFO_FILL);
	else
		regmap_update_bits(host->regmap, MESON_SDHC_PDMA,
				   MESON_SDHC_PDMA_DMA_MODE |
				   MESON_SDHC_PDMA_RXFIFO_MANUAL_FLUSH,
				   MESON_SDHC_PDMA_DMA_MODE |
				   FIELD_PREP(MESON_SDHC_PDMA_RXFIFO_MANUAL_FLUSH,
					      1));

	if (host->cmd->data->flags & MMC_DATA_WRITE)
		regmap_update_bits(host->regmap, MESON_SDHC_PDMA,
				   MESON_SDHC_PDMA_RD_BURST,
				   FIELD_PREP(MESON_SDHC_PDMA_RD_BURST, 15));
}

static void meson_mx_sdhc_wait_before_send_meson8(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);
	u32 val;
	int ret;

	ret = regmap_read_poll_timeout(host->regmap, MESON_SDHC_ESTA, val,
				       val == 0,
				       MESON_SDHC_WAIT_BEFORE_SEND_SLEEP_US,
				       MESON_SDHC_WAIT_BEFORE_SEND_TIMEOUT_US);
	if (ret)
		dev_warn(mmc_dev(mmc),
			 "Failed to wait for ESTA to clear: 0x%08x\n", val);

	if (host->cmd->data && host->cmd->data->flags & MMC_DATA_WRITE) {
		ret = regmap_read_poll_timeout(host->regmap, MESON_SDHC_STAT,
					val, val & MESON_SDHC_STAT_TXFIFO_CNT,
					MESON_SDHC_WAIT_BEFORE_SEND_SLEEP_US,
					MESON_SDHC_WAIT_BEFORE_SEND_TIMEOUT_US);
		if (ret)
			dev_warn(mmc_dev(mmc),
				 "Failed to wait for TX FIFO to fill\n");
	}
}

static void meson_mx_sdhc_init_hw_meson8m2(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);

	regmap_write(host->regmap, MESON_SDHC_MISC,
		     FIELD_PREP(MESON_SDHC_MISC_TXSTART_THRES, 6) |
		     FIELD_PREP(MESON_SDHC_MISC_WCRC_ERR_PATT, 5) |
		     FIELD_PREP(MESON_SDHC_MISC_WCRC_OK_PATT, 2));

	regmap_write(host->regmap, MESON_SDHC_ENHC,
		     FIELD_PREP(MESON_SDHC_ENHC_RXFIFO_TH, 64) |
		     FIELD_PREP(MESON_SDHC_ENHC_MESON8M2_DEBUG, 1) |
		     MESON_SDHC_ENHC_MESON8M2_WRRSP_MODE |
		     FIELD_PREP(MESON_SDHC_ENHC_SDIO_IRQ_PERIOD, 12));
}

static void meson_mx_sdhc_set_pdma_meson8m2(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);

	regmap_update_bits(host->regmap, MESON_SDHC_PDMA,
			   MESON_SDHC_PDMA_DMA_MODE, MESON_SDHC_PDMA_DMA_MODE);
}

static void meson_mx_sdhc_init_hw(struct mmc_host *mmc)
{
	struct meson_mx_sdhc_host *host = mmc_priv(mmc);

	meson_mx_sdhc_hw_reset(mmc);

	regmap_write(host->regmap, MESON_SDHC_CTRL,
		     FIELD_PREP(MESON_SDHC_CTRL_RX_PERIOD, 0xf) |
		     FIELD_PREP(MESON_SDHC_CTRL_RX_TIMEOUT, 0x7f) |
		     FIELD_PREP(MESON_SDHC_CTRL_RX_ENDIAN, 0x7) |
		     FIELD_PREP(MESON_SDHC_CTRL_TX_ENDIAN, 0x7));

	/*
	 * start with a valid divider and enable the memory (un-setting
	 * MESON_SDHC_CLKC_MEM_PWR_OFF).
	 */
	regmap_write(host->regmap, MESON_SDHC_CLKC, MESON_SDHC_CLKC_CLK_DIV);

	regmap_write(host->regmap, MESON_SDHC_CLK2,
		     FIELD_PREP(MESON_SDHC_CLK2_SD_CLK_PHASE, 1));

	regmap_write(host->regmap, MESON_SDHC_PDMA,
		     MESON_SDHC_PDMA_DMA_URGENT |
		     FIELD_PREP(MESON_SDHC_PDMA_WR_BURST, 7) |
		     FIELD_PREP(MESON_SDHC_PDMA_TXFIFO_TH, 49) |
		     FIELD_PREP(MESON_SDHC_PDMA_RD_BURST, 15) |
		     FIELD_PREP(MESON_SDHC_PDMA_RXFIFO_TH, 7));

	/* some initialization bits depend on the SoC: */
	host->platform->init_hw(mmc);

	/* disable and mask all interrupts: */
	regmap_write(host->regmap, MESON_SDHC_ICTL, 0);
	regmap_write(host->regmap, MESON_SDHC_ISTA, MESON_SDHC_ISTA_ALL_IRQS);
}

static void meason_mx_mmc_free_host(void *data)
{
       mmc_free_host(data);
}

static int meson_mx_sdhc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_mx_sdhc_host *host;
	struct mmc_host *mmc;
	void __iomem *base;
	int ret, irq;

	mmc = mmc_alloc_host(sizeof(*host), dev);
	if (!mmc)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, meason_mx_mmc_free_host, mmc);
	if (ret) {
		dev_err(dev, "Failed to register mmc_free_host action\n");
		return ret;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;

	platform_set_drvdata(pdev, host);

	host->platform = device_get_match_data(dev);
	if (!host->platform)
		return -EINVAL;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	host->regmap = devm_regmap_init_mmio(dev, base,
					     &meson_mx_sdhc_regmap_config);
	if (IS_ERR(host->regmap))
		return PTR_ERR(host->regmap);

	host->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(host->pclk))
		return PTR_ERR(host->pclk);

	/* accessing any register requires the module clock to be enabled: */
	ret = clk_prepare_enable(host->pclk);
	if (ret) {
		dev_err(dev, "Failed to enable 'pclk' clock\n");
		return ret;
	}

	meson_mx_sdhc_init_hw(mmc);

	ret = meson_mx_sdhc_register_clkc(dev, base, host->bulk_clks);
	if (ret)
		goto err_disable_pclk;

	host->sd_clk = host->bulk_clks[1].clk;

	/* Get regulators and the supported OCR mask */
	ret = mmc_regulator_get_supply(mmc);
	if (ret)
		goto err_disable_pclk;

	mmc->max_req_size = SZ_128K;
	mmc->max_seg_size = mmc->max_req_size;
	mmc->max_blk_count = FIELD_GET(MESON_SDHC_SEND_TOTAL_PACK, ~0);
	mmc->max_blk_size = MESON_SDHC_MAX_BLK_SIZE;
	mmc->max_busy_timeout = 30 * MSEC_PER_SEC;
	mmc->f_min = clk_round_rate(host->sd_clk, 1);
	mmc->f_max = clk_round_rate(host->sd_clk, ULONG_MAX);
	mmc->max_current_180 = 300;
	mmc->max_current_330 = 300;
	mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_HW_RESET;
	mmc->ops = &meson_mx_sdhc_ops;

	ret = mmc_of_parse(mmc);
	if (ret)
		goto err_disable_pclk;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_disable_pclk;
	}

	ret = devm_request_threaded_irq(dev, irq, meson_mx_sdhc_irq,
					meson_mx_sdhc_irq_thread, IRQF_ONESHOT,
					NULL, host);
	if (ret)
		goto err_disable_pclk;

	ret = mmc_add_host(mmc);
	if (ret)
		goto err_disable_pclk;

	return 0;

err_disable_pclk:
	clk_disable_unprepare(host->pclk);
	return ret;
}

static int meson_mx_sdhc_remove(struct platform_device *pdev)
{
	struct meson_mx_sdhc_host *host = platform_get_drvdata(pdev);

	mmc_remove_host(host->mmc);

	meson_mx_sdhc_disable_clks(host->mmc);

	clk_disable_unprepare(host->pclk);

	return 0;
}

static const struct meson_mx_sdhc_data meson_mx_sdhc_data_meson8 = {
	.init_hw			= meson_mx_sdhc_init_hw_meson8,
	.set_pdma			= meson_mx_sdhc_set_pdma_meson8,
	.wait_before_send		= meson_mx_sdhc_wait_before_send_meson8,
	.hardware_flush_all_cmds	= false,
};

static const struct meson_mx_sdhc_data meson_mx_sdhc_data_meson8m2 = {
	.init_hw			= meson_mx_sdhc_init_hw_meson8m2,
	.set_pdma			= meson_mx_sdhc_set_pdma_meson8m2,
	.hardware_flush_all_cmds	= true,
};

static const struct of_device_id meson_mx_sdhc_of_match[] = {
	{
		.compatible = "amlogic,meson8-sdhc",
		.data = &meson_mx_sdhc_data_meson8
	},
	{
		.compatible = "amlogic,meson8b-sdhc",
		.data = &meson_mx_sdhc_data_meson8
	},
	{
		.compatible = "amlogic,meson8m2-sdhc",
		.data = &meson_mx_sdhc_data_meson8m2
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_mx_sdhc_of_match);

static struct platform_driver meson_mx_sdhc_driver = {
	.probe   = meson_mx_sdhc_probe,
	.remove  = meson_mx_sdhc_remove,
	.driver  = {
		.name = "meson-mx-sdhc",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(meson_mx_sdhc_of_match),
	},
};

module_platform_driver(meson_mx_sdhc_driver);

MODULE_DESCRIPTION("Meson6, Meson8, Meson8b and Meson8m2 SDHC Host Driver");
MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_LICENSE("GPL v2");
