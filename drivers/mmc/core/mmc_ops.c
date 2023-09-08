// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/mmc/core/mmc_ops.h
 *
 *  Copyright 2006-2007 Pierre Ossman
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/scatterlist.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>

#include "core.h"
#include "card.h"
#include "host.h"
#include "mmc_ops.h"

#define MMC_BKOPS_TIMEOUT_MS		(120 * 1000) /* 120s */
#define MMC_SANITIZE_TIMEOUT_MS		(240 * 1000) /* 240s */
#define MMC_OP_COND_PERIOD_US		(4 * 1000) /* 4ms */
#define MMC_OP_COND_TIMEOUT_MS		1000 /* 1s */

static const u8 tuning_blk_pattern_4bit[] = {
	0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
	0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
	0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
	0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
	0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
	0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
	0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
	0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

static const u8 tuning_blk_pattern_8bit[] = {
	0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
	0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
	0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
	0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
	0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
	0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
	0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
	0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
	0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
	0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
	0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
	0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
	0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
	0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
	0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
	0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

struct mmc_busy_data {
	struct mmc_card *card;
	bool retry_crc_err;
	enum mmc_busy_cmd busy_cmd;
};

struct mmc_op_cond_busy_data {
	struct mmc_host *host;
	u32 ocr;
	struct mmc_command *cmd;
};

int __mmc_send_status(struct mmc_card *card, u32 *status, unsigned int retries)
{
	int err;
	struct mmc_command cmd = {};

	cmd.opcode = MMC_SEND_STATUS;
	if (!mmc_host_is_spi(card->host))
		cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(card->host, &cmd, retries);
	if (err)
		return err;

	/* NOTE: callers are required to understand the difference
	 * between "native" and SPI format status words!
	 */
	if (status)
		*status = cmd.resp[0];

	return 0;
}
EXPORT_SYMBOL_GPL(__mmc_send_status);

int mmc_send_status(struct mmc_card *card, u32 *status)
{
	return __mmc_send_status(card, status, MMC_CMD_RETRIES);
}
EXPORT_SYMBOL_GPL(mmc_send_status);

static int _mmc_select_card(struct mmc_host *host, struct mmc_card *card)
{
	struct mmc_command cmd = {};

	cmd.opcode = MMC_SELECT_CARD;

	if (card) {
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	} else {
		cmd.arg = 0;
		cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;
	}

	return mmc_wait_for_cmd(host, &cmd, MMC_CMD_RETRIES);
}

int mmc_select_card(struct mmc_card *card)
{

	return _mmc_select_card(card->host, card);
}

int mmc_deselect_cards(struct mmc_host *host)
{
	return _mmc_select_card(host, NULL);
}

/*
 * Write the value specified in the device tree or board code into the optional
 * 16 bit Driver Stage Register. This can be used to tune raise/fall times and
 * drive strength of the DAT and CMD outputs. The actual meaning of a given
 * value is hardware dependant.
 * The presence of the DSR register can be determined from the CSD register,
 * bit 76.
 */
int mmc_set_dsr(struct mmc_host *host)
{
	struct mmc_command cmd = {};

	cmd.opcode = MMC_SET_DSR;

	cmd.arg = (host->dsr << 16) | 0xffff;
	cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;

	return mmc_wait_for_cmd(host, &cmd, MMC_CMD_RETRIES);
}

int mmc_go_idle(struct mmc_host *host)
{
	int err;
	struct mmc_command cmd = {};

	/*
	 * Non-SPI hosts need to prevent chipselect going active during
	 * GO_IDLE; that would put chips into SPI mode.  Remind them of
	 * that in case of hardware that won't pull up DAT3/nCS otherwise.
	 *
	 * SPI hosts ignore ios.chip_select; it's managed according to
	 * rules that must accommodate non-MMC slaves which this layer
	 * won't even know about.
	 */
	if (!mmc_host_is_spi(host)) {
		mmc_set_chip_select(host, MMC_CS_HIGH);
		mmc_delay(1);
	}

	cmd.opcode = MMC_GO_IDLE_STATE;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_NONE | MMC_CMD_BC;

	err = mmc_wait_for_cmd(host, &cmd, 0);

	mmc_delay(1);

	if (!mmc_host_is_spi(host)) {
		mmc_set_chip_select(host, MMC_CS_DONTCARE);
		mmc_delay(1);
	}

	host->use_spi_crc = 0;

	return err;
}

static int __mmc_send_op_cond_cb(void *cb_data, bool *busy)
{
	struct mmc_op_cond_busy_data *data = cb_data;
	struct mmc_host *host = data->host;
	struct mmc_command *cmd = data->cmd;
	u32 ocr = data->ocr;
	int err = 0;

	err = mmc_wait_for_cmd(host, cmd, 0);
	if (err)
		return err;

	if (mmc_host_is_spi(host)) {
		if (!(cmd->resp[0] & R1_SPI_IDLE)) {
			*busy = false;
			return 0;
		}
	} else {
		if (cmd->resp[0] & MMC_CARD_BUSY) {
			*busy = false;
			return 0;
		}
	}

	*busy = true;

	/*
	 * According to eMMC specification v5.1 section 6.4.3, we
	 * should issue CMD1 repeatedly in the idle state until
	 * the eMMC is ready. Otherwise some eMMC devices seem to enter
	 * the inactive mode after mmc_init_card() issued CMD0 when
	 * the eMMC device is busy.
	 */
	if (!ocr && !mmc_host_is_spi(host))
		cmd->arg = cmd->resp[0] | BIT(30);

	return 0;
}

int mmc_send_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr)
{
	struct mmc_command cmd = {};
	int err = 0;
	struct mmc_op_cond_busy_data cb_data = {
		.host = host,
		.ocr = ocr,
		.cmd = &cmd
	};

	cmd.opcode = MMC_SEND_OP_COND;
	cmd.arg = mmc_host_is_spi(host) ? 0 : ocr;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R3 | MMC_CMD_BCR;

	err = __mmc_poll_for_busy(host, MMC_OP_COND_PERIOD_US,
				  MMC_OP_COND_TIMEOUT_MS,
				  &__mmc_send_op_cond_cb, &cb_data);
	if (err)
		return err;

	if (rocr && !mmc_host_is_spi(host))
		*rocr = cmd.resp[0];

	return err;
}

int mmc_set_relative_addr(struct mmc_card *card)
{
	struct mmc_command cmd = {};

	cmd.opcode = MMC_SET_RELATIVE_ADDR;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

	return mmc_wait_for_cmd(card->host, &cmd, MMC_CMD_RETRIES);
}

static int
mmc_send_cxd_native(struct mmc_host *host, u32 arg, u32 *cxd, int opcode)
{
	int err;
	struct mmc_command cmd = {};

	cmd.opcode = opcode;
	cmd.arg = arg;
	cmd.flags = MMC_RSP_R2 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(host, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	memcpy(cxd, cmd.resp, sizeof(u32) * 4);

	return 0;
}

/*
 * NOTE: void *buf, caller for the buf is required to use DMA-capable
 * buffer or on-stack buffer (with some overhead in callee).
 */
int mmc_send_adtc_data(struct mmc_card *card, struct mmc_host *host, u32 opcode,
		       u32 args, void *buf, unsigned len)
{
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_data data = {};
	struct scatterlist sg;

	mrq.cmd = &cmd;
	mrq.data = &data;

	cmd.opcode = opcode;
	cmd.arg = args;

	/* NOTE HACK:  the MMC_RSP_SPI_R1 is always correct here, but we
	 * rely on callers to never use this with "native" calls for reading
	 * CSD or CID.  Native versions of those commands use the R2 type,
	 * not R1 plus a data block.
	 */
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blksz = len;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;

	sg_init_one(&sg, buf, len);

	if (opcode == MMC_SEND_CSD || opcode == MMC_SEND_CID) {
		/*
		 * The spec states that CSR and CID accesses have a timeout
		 * of 64 clock cycles.
		 */
		data.timeout_ns = 0;
		data.timeout_clks = 64;
	} else
		mmc_set_data_timeout(&data, card);

	mmc_wait_for_req(host, &mrq);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	return 0;
}

static int mmc_spi_send_cxd(struct mmc_host *host, u32 *cxd, u32 opcode)
{
	int ret, i;
	__be32 *cxd_tmp;

	cxd_tmp = kzalloc(16, GFP_KERNEL);
	if (!cxd_tmp)
		return -ENOMEM;

	ret = mmc_send_adtc_data(NULL, host, opcode, 0, cxd_tmp, 16);
	if (ret)
		goto err;

	for (i = 0; i < 4; i++)
		cxd[i] = be32_to_cpu(cxd_tmp[i]);

err:
	kfree(cxd_tmp);
	return ret;
}

int mmc_send_csd(struct mmc_card *card, u32 *csd)
{
	if (mmc_host_is_spi(card->host))
		return mmc_spi_send_cxd(card->host, csd, MMC_SEND_CSD);

	return mmc_send_cxd_native(card->host, card->rca << 16,	csd,
				MMC_SEND_CSD);
}

int mmc_send_cid(struct mmc_host *host, u32 *cid)
{
	if (mmc_host_is_spi(host))
		return mmc_spi_send_cxd(host, cid, MMC_SEND_CID);

	return mmc_send_cxd_native(host, 0, cid, MMC_ALL_SEND_CID);
}

int mmc_get_ext_csd(struct mmc_card *card, u8 **new_ext_csd)
{
	int err;
	u8 *ext_csd;

	if (!card || !new_ext_csd)
		return -EINVAL;

	if (!mmc_can_ext_csd(card))
		return -EOPNOTSUPP;

	/*
	 * As the ext_csd is so large and mostly unused, we don't store the
	 * raw block in mmc_card.
	 */
	ext_csd = kzalloc(512, GFP_KERNEL);
	if (!ext_csd)
		return -ENOMEM;

	err = mmc_send_adtc_data(card, card->host, MMC_SEND_EXT_CSD, 0, ext_csd,
				512);
	if (err)
		kfree(ext_csd);
	else
		*new_ext_csd = ext_csd;

	return err;
}
EXPORT_SYMBOL_GPL(mmc_get_ext_csd);

int mmc_spi_read_ocr(struct mmc_host *host, int highcap, u32 *ocrp)
{
	struct mmc_command cmd = {};
	int err;

	cmd.opcode = MMC_SPI_READ_OCR;
	cmd.arg = highcap ? (1 << 30) : 0;
	cmd.flags = MMC_RSP_SPI_R3;

	err = mmc_wait_for_cmd(host, &cmd, 0);

	*ocrp = cmd.resp[1];
	return err;
}

int mmc_spi_set_crc(struct mmc_host *host, int use_crc)
{
	struct mmc_command cmd = {};
	int err;

	cmd.opcode = MMC_SPI_CRC_ON_OFF;
	cmd.flags = MMC_RSP_SPI_R1;
	cmd.arg = use_crc;

	err = mmc_wait_for_cmd(host, &cmd, 0);
	if (!err)
		host->use_spi_crc = use_crc;
	return err;
}

static int mmc_switch_status_error(struct mmc_host *host, u32 status)
{
	if (mmc_host_is_spi(host)) {
		if (status & R1_SPI_ILLEGAL_COMMAND)
			return -EBADMSG;
	} else {
		if (R1_STATUS(status))
			pr_warn("%s: unexpected status %#x after switch\n",
				mmc_hostname(host), status);
		if (status & R1_SWITCH_ERROR)
			return -EBADMSG;
	}
	return 0;
}

/* Caller must hold re-tuning */
int mmc_switch_status(struct mmc_card *card, bool crc_err_fatal)
{
	u32 status;
	int err;

	err = mmc_send_status(card, &status);
	if (!crc_err_fatal && err == -EILSEQ)
		return 0;
	if (err)
		return err;

	return mmc_switch_status_error(card->host, status);
}

static int mmc_busy_cb(void *cb_data, bool *busy)
{
	struct mmc_busy_data *data = cb_data;
	struct mmc_host *host = data->card->host;
	u32 status = 0;
	int err;

	if (data->busy_cmd != MMC_BUSY_IO && host->ops->card_busy) {
		*busy = host->ops->card_busy(host);
		return 0;
	}

	err = mmc_send_status(data->card, &status);
	if (data->retry_crc_err && err == -EILSEQ) {
		*busy = true;
		return 0;
	}
	if (err)
		return err;

	switch (data->busy_cmd) {
	case MMC_BUSY_CMD6:
		err = mmc_switch_status_error(host, status);
		break;
	case MMC_BUSY_ERASE:
		err = R1_STATUS(status) ? -EIO : 0;
		break;
	case MMC_BUSY_HPI:
	case MMC_BUSY_EXTR_SINGLE:
	case MMC_BUSY_IO:
		break;
	default:
		err = -EINVAL;
	}

	if (err)
		return err;

	*busy = !mmc_ready_for_data(status);
	return 0;
}

int __mmc_poll_for_busy(struct mmc_host *host, unsigned int period_us,
			unsigned int timeout_ms,
			int (*busy_cb)(void *cb_data, bool *busy),
			void *cb_data)
{
	int err;
	unsigned long timeout;
	unsigned int udelay = period_us ? period_us : 32, udelay_max = 32768;
	bool expired = false;
	bool busy = false;

	timeout = jiffies + msecs_to_jiffies(timeout_ms) + 1;
	do {
		/*
		 * Due to the possibility of being preempted while polling,
		 * check the expiration time first.
		 */
		expired = time_after(jiffies, timeout);

		err = (*busy_cb)(cb_data, &busy);
		if (err)
			return err;

		/* Timeout if the device still remains busy. */
		if (expired && busy) {
			pr_err("%s: Card stuck being busy! %s\n",
				mmc_hostname(host), __func__);
			return -ETIMEDOUT;
		}

		/* Throttle the polling rate to avoid hogging the CPU. */
		if (busy) {
			usleep_range(udelay, udelay * 2);
			if (udelay < udelay_max)
				udelay *= 2;
		}
	} while (busy);

	return 0;
}
EXPORT_SYMBOL_GPL(__mmc_poll_for_busy);

int mmc_poll_for_busy(struct mmc_card *card, unsigned int timeout_ms,
		      bool retry_crc_err, enum mmc_busy_cmd busy_cmd)
{
	struct mmc_host *host = card->host;
	struct mmc_busy_data cb_data;

	cb_data.card = card;
	cb_data.retry_crc_err = retry_crc_err;
	cb_data.busy_cmd = busy_cmd;

	return __mmc_poll_for_busy(host, 0, timeout_ms, &mmc_busy_cb, &cb_data);
}
EXPORT_SYMBOL_GPL(mmc_poll_for_busy);

bool mmc_prepare_busy_cmd(struct mmc_host *host, struct mmc_command *cmd,
			  unsigned int timeout_ms)
{
	/*
	 * If the max_busy_timeout of the host is specified, make sure it's
	 * enough to fit the used timeout_ms. In case it's not, let's instruct
	 * the host to avoid HW busy detection, by converting to a R1 response
	 * instead of a R1B. Note, some hosts requires R1B, which also means
	 * they are on their own when it comes to deal with the busy timeout.
	 */
	if (!(host->caps & MMC_CAP_NEED_RSP_BUSY) && host->max_busy_timeout &&
	    (timeout_ms > host->max_busy_timeout)) {
		cmd->flags = MMC_CMD_AC | MMC_RSP_SPI_R1 | MMC_RSP_R1;
		return false;
	}

	cmd->flags = MMC_CMD_AC | MMC_RSP_SPI_R1B | MMC_RSP_R1B;
	cmd->busy_timeout = timeout_ms;
	return true;
}
EXPORT_SYMBOL_GPL(mmc_prepare_busy_cmd);

/**
 *	__mmc_switch - modify EXT_CSD register
 *	@card: the MMC card associated with the data transfer
 *	@set: cmd set values
 *	@index: EXT_CSD register index
 *	@value: value to program into EXT_CSD register
 *	@timeout_ms: timeout (ms) for operation performed by register write,
 *                   timeout of zero implies maximum possible timeout
 *	@timing: new timing to change to
 *	@send_status: send status cmd to poll for busy
 *	@retry_crc_err: retry when CRC errors when polling with CMD13 for busy
 *	@retries: number of retries
 *
 *	Modifies the EXT_CSD register for selected card.
 */
int __mmc_switch(struct mmc_card *card, u8 set, u8 index, u8 value,
		unsigned int timeout_ms, unsigned char timing,
		bool send_status, bool retry_crc_err, unsigned int retries)
{
	struct mmc_host *host = card->host;
	int err;
	struct mmc_command cmd = {};
	bool use_r1b_resp;
	unsigned char old_timing = host->ios.timing;

	mmc_retune_hold(host);

	if (!timeout_ms) {
		pr_warn("%s: unspecified timeout for CMD6 - use generic\n",
			mmc_hostname(host));
		timeout_ms = card->ext_csd.generic_cmd6_time;
	}

	cmd.opcode = MMC_SWITCH;
	cmd.arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
		  (index << 16) |
		  (value << 8) |
		  set;
	use_r1b_resp = mmc_prepare_busy_cmd(host, &cmd, timeout_ms);

	err = mmc_wait_for_cmd(host, &cmd, retries);
	if (err)
		goto out;

	/*If SPI or used HW busy detection above, then we don't need to poll. */
	if (((host->caps & MMC_CAP_WAIT_WHILE_BUSY) && use_r1b_resp) ||
		mmc_host_is_spi(host))
		goto out_tim;

	/*
	 * If the host doesn't support HW polling via the ->card_busy() ops and
	 * when it's not allowed to poll by using CMD13, then we need to rely on
	 * waiting the stated timeout to be sufficient.
	 */
	if (!send_status && !host->ops->card_busy) {
		mmc_delay(timeout_ms);
		goto out_tim;
	}

	/* Let's try to poll to find out when the command is completed. */
	err = mmc_poll_for_busy(card, timeout_ms, retry_crc_err, MMC_BUSY_CMD6);
	if (err)
		goto out;

out_tim:
	/* Switch to new timing before check switch status. */
	if (timing)
		mmc_set_timing(host, timing);

	if (send_status) {
		err = mmc_switch_status(card, true);
		if (err && timing)
			mmc_set_timing(host, old_timing);
	}
out:
	mmc_retune_release(host);

	return err;
}

int mmc_switch(struct mmc_card *card, u8 set, u8 index, u8 value,
		unsigned int timeout_ms)
{
	return __mmc_switch(card, set, index, value, timeout_ms, 0,
			    true, false, MMC_CMD_RETRIES);
}
EXPORT_SYMBOL_GPL(mmc_switch);

int mmc_send_tuning(struct mmc_host *host, u32 opcode, int *cmd_error)
{
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_data data = {};
	struct scatterlist sg;
	struct mmc_ios *ios = &host->ios;
	const u8 *tuning_block_pattern;
	int size, err = 0;
	u8 *data_buf;

	if (ios->bus_width == MMC_BUS_WIDTH_8) {
		tuning_block_pattern = tuning_blk_pattern_8bit;
		size = sizeof(tuning_blk_pattern_8bit);
	} else if (ios->bus_width == MMC_BUS_WIDTH_4) {
		tuning_block_pattern = tuning_blk_pattern_4bit;
		size = sizeof(tuning_blk_pattern_4bit);
	} else
		return -EINVAL;

	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;

	mrq.cmd = &cmd;
	mrq.data = &data;

	cmd.opcode = opcode;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blksz = size;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	/*
	 * According to the tuning specs, Tuning process
	 * is normally shorter 40 executions of CMD19,
	 * and timeout value should be shorter than 150 ms
	 */
	data.timeout_ns = 150 * NSEC_PER_MSEC;

	data.sg = &sg;
	data.sg_len = 1;
	sg_init_one(&sg, data_buf, size);

	mmc_wait_for_req(host, &mrq);

	if (cmd_error)
		*cmd_error = cmd.error;

	if (cmd.error) {
		err = cmd.error;
		goto out;
	}

	if (data.error) {
		err = data.error;
		goto out;
	}

	if (memcmp(data_buf, tuning_block_pattern, size))
		err = -EIO;

out:
	kfree(data_buf);
	return err;
}
EXPORT_SYMBOL_GPL(mmc_send_tuning);

int mmc_send_abort_tuning(struct mmc_host *host, u32 opcode)
{
	struct mmc_command cmd = {};

	/*
	 * eMMC specification specifies that CMD12 can be used to stop a tuning
	 * command, but SD specification does not, so do nothing unless it is
	 * eMMC.
	 */
	if (opcode != MMC_SEND_TUNING_BLOCK_HS200)
		return 0;

	cmd.opcode = MMC_STOP_TRANSMISSION;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

	/*
	 * For drivers that override R1 to R1b, set an arbitrary timeout based
	 * on the tuning timeout i.e. 150ms.
	 */
	cmd.busy_timeout = 150;

	return mmc_wait_for_cmd(host, &cmd, 0);
}
EXPORT_SYMBOL_GPL(mmc_send_abort_tuning);

static int
mmc_send_bus_test(struct mmc_card *card, struct mmc_host *host, u8 opcode,
		  u8 len)
{
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_data data = {};
	struct scatterlist sg;
	u8 *data_buf;
	u8 *test_buf;
	int i, err;
	static u8 testdata_8bit[8] = { 0x55, 0xaa, 0, 0, 0, 0, 0, 0 };
	static u8 testdata_4bit[4] = { 0x5a, 0, 0, 0 };

	/* dma onto stack is unsafe/nonportable, but callers to this
	 * routine normally provide temporary on-stack buffers ...
	 */
	data_buf = kmalloc(len, GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;

	if (len == 8)
		test_buf = testdata_8bit;
	else if (len == 4)
		test_buf = testdata_4bit;
	else {
		pr_err("%s: Invalid bus_width %d\n",
		       mmc_hostname(host), len);
		kfree(data_buf);
		return -EINVAL;
	}

	if (opcode == MMC_BUS_TEST_W)
		memcpy(data_buf, test_buf, len);

	mrq.cmd = &cmd;
	mrq.data = &data;
	cmd.opcode = opcode;
	cmd.arg = 0;

	/* NOTE HACK:  the MMC_RSP_SPI_R1 is always correct here, but we
	 * rely on callers to never use this with "native" calls for reading
	 * CSD or CID.  Native versions of those commands use the R2 type,
	 * not R1 plus a data block.
	 */
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blksz = len;
	data.blocks = 1;
	if (opcode == MMC_BUS_TEST_R)
		data.flags = MMC_DATA_READ;
	else
		data.flags = MMC_DATA_WRITE;

	data.sg = &sg;
	data.sg_len = 1;
	mmc_set_data_timeout(&data, card);
	sg_init_one(&sg, data_buf, len);
	mmc_wait_for_req(host, &mrq);
	err = 0;
	if (opcode == MMC_BUS_TEST_R) {
		for (i = 0; i < len / 4; i++)
			if ((test_buf[i] ^ data_buf[i]) != 0xff) {
				err = -EIO;
				break;
			}
	}
	kfree(data_buf);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	return err;
}

int mmc_bus_test(struct mmc_card *card, u8 bus_width)
{
	int width;

	if (bus_width == MMC_BUS_WIDTH_8)
		width = 8;
	else if (bus_width == MMC_BUS_WIDTH_4)
		width = 4;
	else if (bus_width == MMC_BUS_WIDTH_1)
		return 0; /* no need for test */
	else
		return -EINVAL;

	/*
	 * Ignore errors from BUS_TEST_W.  BUS_TEST_R will fail if there
	 * is a problem.  This improves chances that the test will work.
	 */
	mmc_send_bus_test(card, card->host, MMC_BUS_TEST_W, width);
	return mmc_send_bus_test(card, card->host, MMC_BUS_TEST_R, width);
}

static int mmc_send_hpi_cmd(struct mmc_card *card)
{
	unsigned int busy_timeout_ms = card->ext_csd.out_of_int_time;
	struct mmc_host *host = card->host;
	bool use_r1b_resp = false;
	struct mmc_command cmd = {};
	int err;

	cmd.opcode = card->ext_csd.hpi_cmd;
	cmd.arg = card->rca << 16 | 1;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

	if (cmd.opcode == MMC_STOP_TRANSMISSION)
		use_r1b_resp = mmc_prepare_busy_cmd(host, &cmd,
						    busy_timeout_ms);

	err = mmc_wait_for_cmd(host, &cmd, 0);
	if (err) {
		pr_warn("%s: HPI error %d. Command response %#x\n",
			mmc_hostname(host), err, cmd.resp[0]);
		return err;
	}

	/* No need to poll when using HW busy detection. */
	if (host->caps & MMC_CAP_WAIT_WHILE_BUSY && use_r1b_resp)
		return 0;

	/* Let's poll to find out when the HPI request completes. */
	return mmc_poll_for_busy(card, busy_timeout_ms, false, MMC_BUSY_HPI);
}

/**
 *	mmc_interrupt_hpi - Issue for High priority Interrupt
 *	@card: the MMC card associated with the HPI transfer
 *
 *	Issued High Priority Interrupt, and check for card status
 *	until out-of prg-state.
 */
static int mmc_interrupt_hpi(struct mmc_card *card)
{
	int err;
	u32 status;

	if (!card->ext_csd.hpi_en) {
		pr_info("%s: HPI enable bit unset\n", mmc_hostname(card->host));
		return 1;
	}

	err = mmc_send_status(card, &status);
	if (err) {
		pr_err("%s: Get card status fail\n", mmc_hostname(card->host));
		goto out;
	}

	switch (R1_CURRENT_STATE(status)) {
	case R1_STATE_IDLE:
	case R1_STATE_READY:
	case R1_STATE_STBY:
	case R1_STATE_TRAN:
		/*
		 * In idle and transfer states, HPI is not needed and the caller
		 * can issue the next intended command immediately
		 */
		goto out;
	case R1_STATE_PRG:
		break;
	default:
		/* In all other states, it's illegal to issue HPI */
		pr_debug("%s: HPI cannot be sent. Card state=%d\n",
			mmc_hostname(card->host), R1_CURRENT_STATE(status));
		err = -EINVAL;
		goto out;
	}

	err = mmc_send_hpi_cmd(card);
out:
	return err;
}

int mmc_can_ext_csd(struct mmc_card *card)
{
	return (card && card->csd.mmca_vsn > CSD_SPEC_VER_3);
}

static int mmc_read_bkops_status(struct mmc_card *card)
{
	int err;
	u8 *ext_csd;

	err = mmc_get_ext_csd(card, &ext_csd);
	if (err)
		return err;

	card->ext_csd.raw_bkops_status = ext_csd[EXT_CSD_BKOPS_STATUS];
	card->ext_csd.raw_exception_status = ext_csd[EXT_CSD_EXP_EVENTS_STATUS];
	kfree(ext_csd);
	return 0;
}

/**
 *	mmc_run_bkops - Run BKOPS for supported cards
 *	@card: MMC card to run BKOPS for
 *
 *	Run background operations synchronously for cards having manual BKOPS
 *	enabled and in case it reports urgent BKOPS level.
*/
void mmc_run_bkops(struct mmc_card *card)
{
	int err;

	if (!card->ext_csd.man_bkops_en)
		return;

	err = mmc_read_bkops_status(card);
	if (err) {
		pr_err("%s: Failed to read bkops status: %d\n",
		       mmc_hostname(card->host), err);
		return;
	}

	if (!card->ext_csd.raw_bkops_status ||
	    card->ext_csd.raw_bkops_status < EXT_CSD_BKOPS_LEVEL_2)
		return;

	mmc_retune_hold(card->host);

	/*
	 * For urgent BKOPS status, LEVEL_2 and higher, let's execute
	 * synchronously. Future wise, we may consider to start BKOPS, for less
	 * urgent levels by using an asynchronous background task, when idle.
	 */
	err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
			 EXT_CSD_BKOPS_START, 1, MMC_BKOPS_TIMEOUT_MS);
	/*
	 * If the BKOPS timed out, the card is probably still busy in the
	 * R1_STATE_PRG. Rather than continue to wait, let's try to abort
	 * it with a HPI command to get back into R1_STATE_TRAN.
	 */
	if (err == -ETIMEDOUT && !mmc_interrupt_hpi(card))
		pr_warn("%s: BKOPS aborted\n", mmc_hostname(card->host));
	else if (err)
		pr_warn("%s: Error %d running bkops\n",
			mmc_hostname(card->host), err);

	mmc_retune_release(card->host);
}
EXPORT_SYMBOL(mmc_run_bkops);

static int mmc_cmdq_switch(struct mmc_card *card, bool enable)
{
	u8 val = enable ? EXT_CSD_CMDQ_MODE_ENABLED : 0;
	int err;

	if (!card->ext_csd.cmdq_support)
		return -EOPNOTSUPP;

	err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_CMDQ_MODE_EN,
			 val, card->ext_csd.generic_cmd6_time);
	if (!err)
		card->ext_csd.cmdq_en = enable;

	return err;
}

int mmc_cmdq_enable(struct mmc_card *card)
{
	return mmc_cmdq_switch(card, true);
}
EXPORT_SYMBOL_GPL(mmc_cmdq_enable);

int mmc_cmdq_disable(struct mmc_card *card)
{
	return mmc_cmdq_switch(card, false);
}
EXPORT_SYMBOL_GPL(mmc_cmdq_disable);

int mmc_sanitize(struct mmc_card *card, unsigned int timeout_ms)
{
	struct mmc_host *host = card->host;
	int err;

	if (!mmc_can_sanitize(card)) {
		pr_warn("%s: Sanitize not supported\n", mmc_hostname(host));
		return -EOPNOTSUPP;
	}

	if (!timeout_ms)
		timeout_ms = MMC_SANITIZE_TIMEOUT_MS;

	pr_debug("%s: Sanitize in progress...\n", mmc_hostname(host));

	mmc_retune_hold(host);

	err = __mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_SANITIZE_START,
			   1, timeout_ms, 0, true, false, 0);
	if (err)
		pr_err("%s: Sanitize failed err=%d\n", mmc_hostname(host), err);

	/*
	 * If the sanitize operation timed out, the card is probably still busy
	 * in the R1_STATE_PRG. Rather than continue to wait, let's try to abort
	 * it with a HPI command to get back into R1_STATE_TRAN.
	 */
	if (err == -ETIMEDOUT && !mmc_interrupt_hpi(card))
		pr_warn("%s: Sanitize aborted\n", mmc_hostname(host));

	mmc_retune_release(host);

	pr_debug("%s: Sanitize completed\n", mmc_hostname(host));
	return err;
}
EXPORT_SYMBOL_GPL(mmc_sanitize);
