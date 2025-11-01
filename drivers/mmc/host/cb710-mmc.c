// SPDX-License-Identifier: GPL-2.0-only
/*
 *  cb710/mmc.c
 *
 *  Copyright by Michał Mirosław, 2008-2009
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string_choices.h>
#include "cb710-mmc.h"

#define CB710_MMC_REQ_TIMEOUT_MS	2000

static const u8 cb710_clock_divider_log2[8] = {
/*	1, 2, 4, 8, 16, 32, 128, 512 */
	0, 1, 2, 3,  4,  5,   7,   9
};
#define CB710_MAX_DIVIDER_IDX	\
	(ARRAY_SIZE(cb710_clock_divider_log2) - 1)

static const u8 cb710_src_freq_mhz[16] = {
	33, 10, 20, 25, 30, 35, 40, 45,
	50, 55, 60, 65, 70, 75, 80, 85
};

static void cb710_mmc_select_clock_divider(struct mmc_host *mmc, int hz)
{
	struct cb710_slot *slot = cb710_mmc_to_slot(mmc);
	struct pci_dev *pdev = cb710_slot_to_chip(slot)->pdev;
	u32 src_freq_idx;
	u32 divider_idx;
	int src_hz;

	/* on CB710 in HP nx9500:
	 *   src_freq_idx == 0
	 *   indexes 1-7 work as written in the table
	 *   indexes 0,8-15 give no clock output
	 */
	pci_read_config_dword(pdev, 0x48, &src_freq_idx);
	src_freq_idx = (src_freq_idx >> 16) & 0xF;
	src_hz = cb710_src_freq_mhz[src_freq_idx] * 1000000;

	for (divider_idx = 0; divider_idx < CB710_MAX_DIVIDER_IDX; ++divider_idx) {
		if (hz >= src_hz >> cb710_clock_divider_log2[divider_idx])
			break;
	}

	if (src_freq_idx)
		divider_idx |= 0x8;
	else if (divider_idx == 0)
		divider_idx = 1;

	cb710_pci_update_config_reg(pdev, 0x40, ~0xF0000000, divider_idx << 28);

	dev_dbg(cb710_slot_dev(slot),
		"clock set to %d Hz, wanted %d Hz; src_freq_idx = %d, divider_idx = %d|%d\n",
		src_hz >> cb710_clock_divider_log2[divider_idx & 7],
		hz, src_freq_idx, divider_idx & 7, divider_idx & 8);
}

static void __cb710_mmc_enable_irq(struct cb710_slot *slot,
	unsigned short enable, unsigned short mask)
{
	/* clear global IE
	 * - it gets set later if any interrupt sources are enabled */
	mask |= CB710_MMC_IE_IRQ_ENABLE;

	/* look like interrupt is fired whenever
	 * WORD[0x0C] & WORD[0x10] != 0;
	 * -> bit 15 port 0x0C seems to be global interrupt enable
	 */

	enable = (cb710_read_port_16(slot, CB710_MMC_IRQ_ENABLE_PORT)
		& ~mask) | enable;

	if (enable)
		enable |= CB710_MMC_IE_IRQ_ENABLE;

	cb710_write_port_16(slot, CB710_MMC_IRQ_ENABLE_PORT, enable);
}

static void cb710_mmc_enable_irq(struct cb710_slot *slot,
	unsigned short enable, unsigned short mask)
{
	struct cb710_mmc_reader *reader = mmc_priv(cb710_slot_to_mmc(slot));
	unsigned long flags;

	spin_lock_irqsave(&reader->irq_lock, flags);
	/* this is the only thing irq_lock protects */
	__cb710_mmc_enable_irq(slot, enable, mask);
	spin_unlock_irqrestore(&reader->irq_lock, flags);
}

static void cb710_mmc_reset_events(struct cb710_slot *slot)
{
	cb710_write_port_8(slot, CB710_MMC_STATUS0_PORT, 0xFF);
	cb710_write_port_8(slot, CB710_MMC_STATUS1_PORT, 0xFF);
	cb710_write_port_8(slot, CB710_MMC_STATUS2_PORT, 0xFF);
}

static void cb710_mmc_enable_4bit_data(struct cb710_slot *slot, int enable)
{
	if (enable)
		cb710_modify_port_8(slot, CB710_MMC_CONFIG1_PORT,
			CB710_MMC_C1_4BIT_DATA_BUS, 0);
	else
		cb710_modify_port_8(slot, CB710_MMC_CONFIG1_PORT,
			0, CB710_MMC_C1_4BIT_DATA_BUS);
}

static int cb710_check_event(struct cb710_slot *slot, u8 what)
{
	u16 status;

	status = cb710_read_port_16(slot, CB710_MMC_STATUS_PORT);

	if (status & CB710_MMC_S0_FIFO_UNDERFLOW) {
		/* it is just a guess, so log it */
		dev_dbg(cb710_slot_dev(slot),
			"CHECK : ignoring bit 6 in status %04X\n", status);
		cb710_write_port_8(slot, CB710_MMC_STATUS0_PORT,
			CB710_MMC_S0_FIFO_UNDERFLOW);
		status &= ~CB710_MMC_S0_FIFO_UNDERFLOW;
	}

	if (status & CB710_MMC_STATUS_ERROR_EVENTS) {
		dev_dbg(cb710_slot_dev(slot),
			"CHECK : returning EIO on status %04X\n", status);
		cb710_write_port_8(slot, CB710_MMC_STATUS0_PORT, status & 0xFF);
		cb710_write_port_8(slot, CB710_MMC_STATUS1_PORT,
			CB710_MMC_S1_RESET);
		return -EIO;
	}

	/* 'what' is a bit in MMC_STATUS1 */
	if ((status >> 8) & what) {
		cb710_write_port_8(slot, CB710_MMC_STATUS1_PORT, what);
		return 1;
	}

	return 0;
}

static int cb710_wait_for_event(struct cb710_slot *slot, u8 what)
{
	int err = 0;
	unsigned limit = 2000000;	/* FIXME: real timeout */

#ifdef CONFIG_CB710_DEBUG
	u32 e, x;
	e = cb710_read_port_32(slot, CB710_MMC_STATUS_PORT);
#endif

	while (!(err = cb710_check_event(slot, what))) {
		if (!--limit) {
			cb710_dump_regs(cb710_slot_to_chip(slot),
				CB710_DUMP_REGS_MMC);
			err = -ETIMEDOUT;
			break;
		}
		udelay(1);
	}

#ifdef CONFIG_CB710_DEBUG
	x = cb710_read_port_32(slot, CB710_MMC_STATUS_PORT);

	limit = 2000000 - limit;
	if (limit > 100)
		dev_dbg(cb710_slot_dev(slot),
			"WAIT10: waited %d loops, what %d, entry val %08X, exit val %08X\n",
			limit, what, e, x);
#endif
	return err < 0 ? err : 0;
}


static int cb710_wait_while_busy(struct cb710_slot *slot, uint8_t mask)
{
	unsigned limit = 500000;	/* FIXME: real timeout */
	int err = 0;

#ifdef CONFIG_CB710_DEBUG
	u32 e, x;
	e = cb710_read_port_32(slot, CB710_MMC_STATUS_PORT);
#endif

	while (cb710_read_port_8(slot, CB710_MMC_STATUS2_PORT) & mask) {
		if (!--limit) {
			cb710_dump_regs(cb710_slot_to_chip(slot),
				CB710_DUMP_REGS_MMC);
			err = -ETIMEDOUT;
			break;
		}
		udelay(1);
	}

#ifdef CONFIG_CB710_DEBUG
	x = cb710_read_port_32(slot, CB710_MMC_STATUS_PORT);

	limit = 500000 - limit;
	if (limit > 100)
		dev_dbg(cb710_slot_dev(slot),
			"WAIT12: waited %d loops, mask %02X, entry val %08X, exit val %08X\n",
			limit, mask, e, x);
#endif
	return err;
}

static void cb710_mmc_set_transfer_size(struct cb710_slot *slot,
	size_t count, size_t blocksize)
{
	cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20);
	cb710_write_port_32(slot, CB710_MMC_TRANSFER_SIZE_PORT,
		((count - 1) << 16)|(blocksize - 1));

	dev_vdbg(cb710_slot_dev(slot), "set up for %zu block%s of %zu bytes\n",
		count, str_plural(count), blocksize);
}

static void cb710_mmc_fifo_hack(struct cb710_slot *slot)
{
	/* without this, received data is prepended with 8-bytes of zeroes */
	u32 r1, r2;
	int ok = 0;

	r1 = cb710_read_port_32(slot, CB710_MMC_DATA_PORT);
	r2 = cb710_read_port_32(slot, CB710_MMC_DATA_PORT);
	if (cb710_read_port_8(slot, CB710_MMC_STATUS0_PORT)
	    & CB710_MMC_S0_FIFO_UNDERFLOW) {
		cb710_write_port_8(slot, CB710_MMC_STATUS0_PORT,
			CB710_MMC_S0_FIFO_UNDERFLOW);
		ok = 1;
	}

	dev_dbg(cb710_slot_dev(slot),
		"FIFO-read-hack: expected STATUS0 bit was %s\n",
		ok ? "set." : "NOT SET!");
	dev_dbg(cb710_slot_dev(slot),
		"FIFO-read-hack: dwords ignored: %08X %08X - %s\n",
		r1, r2, (r1|r2) ? "BAD (NOT ZERO)!" : "ok");
}

static int cb710_mmc_receive_pio(struct cb710_slot *slot,
	struct sg_mapping_iter *miter, size_t dw_count)
{
	if (!(cb710_read_port_8(slot, CB710_MMC_STATUS2_PORT) & CB710_MMC_S2_FIFO_READY)) {
		int err = cb710_wait_for_event(slot,
			CB710_MMC_S1_PIO_TRANSFER_DONE);
		if (err)
			return err;
	}

	cb710_sg_dwiter_write_from_io(miter,
		slot->iobase + CB710_MMC_DATA_PORT, dw_count);

	return 0;
}

static bool cb710_is_transfer_size_supported(struct mmc_data *data)
{
	return !(data->blksz & 15 && (data->blocks != 1 || data->blksz != 8));
}

static int cb710_mmc_receive(struct cb710_slot *slot, struct mmc_data *data)
{
	struct sg_mapping_iter miter;
	size_t len, blocks = data->blocks;
	int err = 0;

	/* TODO: I don't know how/if the hardware handles non-16B-boundary blocks
	 * except single 8B block */
	if (unlikely(data->blksz & 15 && (data->blocks != 1 || data->blksz != 8)))
		return -EINVAL;

	sg_miter_start(&miter, data->sg, data->sg_len, SG_MITER_TO_SG);

	cb710_modify_port_8(slot, CB710_MMC_CONFIG2_PORT,
		15, CB710_MMC_C2_READ_PIO_SIZE_MASK);

	cb710_mmc_fifo_hack(slot);

	while (blocks-- > 0) {
		len = data->blksz;

		while (len >= 16) {
			err = cb710_mmc_receive_pio(slot, &miter, 4);
			if (err)
				goto out;
			len -= 16;
		}

		if (!len)
			continue;

		cb710_modify_port_8(slot, CB710_MMC_CONFIG2_PORT,
			len - 1, CB710_MMC_C2_READ_PIO_SIZE_MASK);

		len = (len >= 8) ? 4 : 2;
		err = cb710_mmc_receive_pio(slot, &miter, len);
		if (err)
			goto out;
	}
out:
	sg_miter_stop(&miter);
	return err;
}

static int cb710_mmc_send(struct cb710_slot *slot, struct mmc_data *data)
{
	struct sg_mapping_iter miter;
	size_t len, blocks = data->blocks;
	int err = 0;

	/* TODO: I don't know how/if the hardware handles multiple
	 * non-16B-boundary blocks */
	if (unlikely(data->blocks > 1 && data->blksz & 15))
		return -EINVAL;

	sg_miter_start(&miter, data->sg, data->sg_len, SG_MITER_FROM_SG);

	cb710_modify_port_8(slot, CB710_MMC_CONFIG2_PORT,
		0, CB710_MMC_C2_READ_PIO_SIZE_MASK);

	while (blocks-- > 0) {
		len = (data->blksz + 15) >> 4;
		do {
			if (!(cb710_read_port_8(slot, CB710_MMC_STATUS2_PORT)
			    & CB710_MMC_S2_FIFO_EMPTY)) {
				err = cb710_wait_for_event(slot,
					CB710_MMC_S1_PIO_TRANSFER_DONE);
				if (err)
					goto out;
			}
			cb710_sg_dwiter_read_to_io(&miter,
				slot->iobase + CB710_MMC_DATA_PORT, 4);
		} while (--len);
	}
out:
	sg_miter_stop(&miter);
	return err;
}

static u16 cb710_encode_cmd_flags(struct cb710_mmc_reader *reader,
	struct mmc_command *cmd)
{
	unsigned int flags = cmd->flags;
	u16 cb_flags = 0;

	/* Windows driver returned 0 for commands for which no response
	 * is expected. It happened that there were only two such commands
	 * used: MMC_GO_IDLE_STATE and MMC_GO_INACTIVE_STATE so it might
	 * as well be a bug in that driver.
	 *
	 * Original driver set bit 14 for MMC/SD application
	 * commands. There's no difference 'on the wire' and
	 * it apparently works without it anyway.
	 */

	switch (flags & MMC_CMD_MASK) {
	case MMC_CMD_AC:	cb_flags = CB710_MMC_CMD_AC;	break;
	case MMC_CMD_ADTC:	cb_flags = CB710_MMC_CMD_ADTC;	break;
	case MMC_CMD_BC:	cb_flags = CB710_MMC_CMD_BC;	break;
	case MMC_CMD_BCR:	cb_flags = CB710_MMC_CMD_BCR;	break;
	}

	if (flags & MMC_RSP_BUSY)
		cb_flags |= CB710_MMC_RSP_BUSY;

	cb_flags |= cmd->opcode << CB710_MMC_CMD_CODE_SHIFT;

	if (cmd->data && (cmd->data->flags & MMC_DATA_READ))
		cb_flags |= CB710_MMC_DATA_READ;

	if (flags & MMC_RSP_PRESENT) {
		/* Windows driver set 01 at bits 4,3 except for
		 * MMC_SET_BLOCKLEN where it set 10. Maybe the
		 * hardware can do something special about this
		 * command? The original driver looks buggy/incomplete
		 * anyway so we ignore this for now.
		 *
		 * I assume that 00 here means no response is expected.
		 */
		cb_flags |= CB710_MMC_RSP_PRESENT;

		if (flags & MMC_RSP_136)
			cb_flags |= CB710_MMC_RSP_136;
		if (!(flags & MMC_RSP_CRC))
			cb_flags |= CB710_MMC_RSP_NO_CRC;
	}

	return cb_flags;
}

static void cb710_receive_response(struct cb710_slot *slot,
	struct mmc_command *cmd)
{
	unsigned rsp_opcode, wanted_opcode;

	/* Looks like final byte with CRC is always stripped (same as SDHCI) */
	if (cmd->flags & MMC_RSP_136) {
		u32 resp[4];

		resp[0] = cb710_read_port_32(slot, CB710_MMC_RESPONSE3_PORT);
		resp[1] = cb710_read_port_32(slot, CB710_MMC_RESPONSE2_PORT);
		resp[2] = cb710_read_port_32(slot, CB710_MMC_RESPONSE1_PORT);
		resp[3] = cb710_read_port_32(slot, CB710_MMC_RESPONSE0_PORT);
		rsp_opcode = resp[0] >> 24;

		cmd->resp[0] = (resp[0] << 8)|(resp[1] >> 24);
		cmd->resp[1] = (resp[1] << 8)|(resp[2] >> 24);
		cmd->resp[2] = (resp[2] << 8)|(resp[3] >> 24);
		cmd->resp[3] = (resp[3] << 8);
	} else {
		rsp_opcode = cb710_read_port_32(slot, CB710_MMC_RESPONSE1_PORT) & 0x3F;
		cmd->resp[0] = cb710_read_port_32(slot, CB710_MMC_RESPONSE0_PORT);
	}

	wanted_opcode = (cmd->flags & MMC_RSP_OPCODE) ? cmd->opcode : 0x3F;
	if (rsp_opcode != wanted_opcode)
		cmd->error = -EILSEQ;
}

static int cb710_mmc_transfer_data(struct cb710_slot *slot,
	struct mmc_data *data)
{
	int error, to;

	if (data->flags & MMC_DATA_READ)
		error = cb710_mmc_receive(slot, data);
	else
		error = cb710_mmc_send(slot, data);

	to = cb710_wait_for_event(slot, CB710_MMC_S1_DATA_TRANSFER_DONE);
	if (!error)
		error = to;

	if (!error)
		data->bytes_xfered = data->blksz * data->blocks;
	return error;
}

static int cb710_mmc_command(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct cb710_slot *slot = cb710_mmc_to_slot(mmc);
	struct cb710_mmc_reader *reader = mmc_priv(mmc);
	struct mmc_data *data = cmd->data;

	u16 cb_cmd = cb710_encode_cmd_flags(reader, cmd);
	dev_dbg(cb710_slot_dev(slot), "cmd request: 0x%04X\n", cb_cmd);

	if (data) {
		if (!cb710_is_transfer_size_supported(data)) {
			data->error = -EINVAL;
			return -1;
		}
		cb710_mmc_set_transfer_size(slot, data->blocks, data->blksz);
	}

	cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20|CB710_MMC_S2_BUSY_10);
	cb710_write_port_16(slot, CB710_MMC_CMD_TYPE_PORT, cb_cmd);
	cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20);
	cb710_write_port_32(slot, CB710_MMC_CMD_PARAM_PORT, cmd->arg);
	cb710_mmc_reset_events(slot);
	cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG0_PORT, 0x01, 0);

	cmd->error = cb710_wait_for_event(slot, CB710_MMC_S1_COMMAND_SENT);
	if (cmd->error)
		return -1;

	if (cmd->flags & MMC_RSP_PRESENT) {
		cb710_receive_response(slot, cmd);
		if (cmd->error)
			return -1;
	}

	if (data)
		data->error = cb710_mmc_transfer_data(slot, data);
	return 0;
}

static void cb710_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct cb710_slot *slot = cb710_mmc_to_slot(mmc);
	struct cb710_mmc_reader *reader = mmc_priv(mmc);

	WARN_ON(reader->mrq != NULL);

	reader->mrq = mrq;
	cb710_mmc_enable_irq(slot, CB710_MMC_IE_TEST_MASK, 0);

	if (!cb710_mmc_command(mmc, mrq->cmd) && mrq->stop)
		cb710_mmc_command(mmc, mrq->stop);

	queue_work(system_bh_wq, &reader->finish_req_bh_work);
}

static int cb710_mmc_powerup(struct cb710_slot *slot)
{
#ifdef CONFIG_CB710_DEBUG
	struct cb710_chip *chip = cb710_slot_to_chip(slot);
#endif
	int err;

	/* a lot of magic for now */
	dev_dbg(cb710_slot_dev(slot), "bus powerup\n");
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	err = cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20);
	if (unlikely(err))
		return err;
	cb710_modify_port_8(slot, CB710_MMC_CONFIG1_PORT, 0x80, 0);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG3_PORT, 0x80, 0);
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	mdelay(1);
	dev_dbg(cb710_slot_dev(slot), "after delay 1\n");
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	err = cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20);
	if (unlikely(err))
		return err;
	cb710_modify_port_8(slot, CB710_MMC_CONFIG1_PORT, 0x09, 0);
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	mdelay(1);
	dev_dbg(cb710_slot_dev(slot), "after delay 2\n");
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	err = cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20);
	if (unlikely(err))
		return err;
	cb710_modify_port_8(slot, CB710_MMC_CONFIG1_PORT, 0, 0x08);
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	mdelay(2);
	dev_dbg(cb710_slot_dev(slot), "after delay 3\n");
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG0_PORT, 0x06, 0);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG1_PORT, 0x70, 0);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG2_PORT, 0x80, 0);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG3_PORT, 0x03, 0);
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	err = cb710_wait_while_busy(slot, CB710_MMC_S2_BUSY_20);
	if (unlikely(err))
		return err;
	/* This port behaves weird: quick byte reads of 0x08,0x09 return
	 * 0xFF,0x00 after writing 0xFFFF to 0x08; it works correctly when
	 * read/written from userspace...  What am I missing here?
	 * (it doesn't depend on write-to-read delay) */
	cb710_write_port_16(slot, CB710_MMC_CONFIGB_PORT, 0xFFFF);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG0_PORT, 0x06, 0);
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);
	dev_dbg(cb710_slot_dev(slot), "bus powerup finished\n");

	return cb710_check_event(slot, 0);
}

static void cb710_mmc_powerdown(struct cb710_slot *slot)
{
	cb710_modify_port_8(slot, CB710_MMC_CONFIG1_PORT, 0, 0x81);
	cb710_modify_port_8(slot, CB710_MMC_CONFIG3_PORT, 0, 0x80);
}

static void cb710_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct cb710_slot *slot = cb710_mmc_to_slot(mmc);
	struct cb710_mmc_reader *reader = mmc_priv(mmc);
	int err;

	cb710_mmc_select_clock_divider(mmc, ios->clock);

	if (ios->power_mode != reader->last_power_mode) {
		switch (ios->power_mode) {
		case MMC_POWER_ON:
			err = cb710_mmc_powerup(slot);
			if (err) {
				dev_warn(cb710_slot_dev(slot),
					"powerup failed (%d)- retrying\n", err);
				cb710_mmc_powerdown(slot);
				udelay(1);
				err = cb710_mmc_powerup(slot);
				if (err)
					dev_warn(cb710_slot_dev(slot),
						"powerup retry failed (%d) - expect errors\n",
					err);
			}
			reader->last_power_mode = MMC_POWER_ON;
			break;
		case MMC_POWER_OFF:
			cb710_mmc_powerdown(slot);
			reader->last_power_mode = MMC_POWER_OFF;
			break;
		case MMC_POWER_UP:
		default:
			/* ignore */
			break;
		}
	}

	cb710_mmc_enable_4bit_data(slot, ios->bus_width != MMC_BUS_WIDTH_1);

	cb710_mmc_enable_irq(slot, CB710_MMC_IE_TEST_MASK, 0);
}

static int cb710_mmc_get_ro(struct mmc_host *mmc)
{
	struct cb710_slot *slot = cb710_mmc_to_slot(mmc);

	return cb710_read_port_8(slot, CB710_MMC_STATUS3_PORT)
		& CB710_MMC_S3_WRITE_PROTECTED;
}

static int cb710_mmc_get_cd(struct mmc_host *mmc)
{
	struct cb710_slot *slot = cb710_mmc_to_slot(mmc);

	return cb710_read_port_8(slot, CB710_MMC_STATUS3_PORT)
		& CB710_MMC_S3_CARD_DETECTED;
}

static int cb710_mmc_irq_handler(struct cb710_slot *slot)
{
	struct mmc_host *mmc = cb710_slot_to_mmc(slot);
	struct cb710_mmc_reader *reader = mmc_priv(mmc);
	u32 status, config1, config2, irqen;

	status = cb710_read_port_32(slot, CB710_MMC_STATUS_PORT);
	irqen = cb710_read_port_32(slot, CB710_MMC_IRQ_ENABLE_PORT);
	config2 = cb710_read_port_32(slot, CB710_MMC_CONFIGB_PORT);
	config1 = cb710_read_port_32(slot, CB710_MMC_CONFIG_PORT);

	dev_dbg(cb710_slot_dev(slot), "interrupt; status: %08X, "
		"ie: %08X, c2: %08X, c1: %08X\n",
		status, irqen, config2, config1);

	if (status & (CB710_MMC_S1_CARD_CHANGED << 8)) {
		/* ack the event */
		cb710_write_port_8(slot, CB710_MMC_STATUS1_PORT,
			CB710_MMC_S1_CARD_CHANGED);
		if ((irqen & CB710_MMC_IE_CISTATUS_MASK)
		    == CB710_MMC_IE_CISTATUS_MASK)
			mmc_detect_change(mmc, HZ/5);
	} else {
		dev_dbg(cb710_slot_dev(slot), "unknown interrupt (test)\n");
		spin_lock(&reader->irq_lock);
		__cb710_mmc_enable_irq(slot, 0, CB710_MMC_IE_TEST_MASK);
		spin_unlock(&reader->irq_lock);
	}

	return 1;
}

static void cb710_mmc_finish_request_bh_work(struct work_struct *t)
{
	struct cb710_mmc_reader *reader = from_work(reader, t,
						       finish_req_bh_work);
	struct mmc_request *mrq = reader->mrq;

	reader->mrq = NULL;
	mmc_request_done(mmc_from_priv(reader), mrq);
}

static const struct mmc_host_ops cb710_mmc_host = {
	.request = cb710_mmc_request,
	.set_ios = cb710_mmc_set_ios,
	.get_ro = cb710_mmc_get_ro,
	.get_cd = cb710_mmc_get_cd,
};

static int cb710_mmc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cb710_slot *slot = cb710_pdev_to_slot(pdev);

	cb710_mmc_enable_irq(slot, 0, ~0);
	return 0;
}

static int cb710_mmc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cb710_slot *slot = cb710_pdev_to_slot(pdev);

	cb710_mmc_enable_irq(slot, 0, ~0);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(cb710_mmc_pmops, cb710_mmc_suspend, cb710_mmc_resume);

static int cb710_mmc_init(struct platform_device *pdev)
{
	struct cb710_slot *slot = cb710_pdev_to_slot(pdev);
	struct cb710_chip *chip = cb710_slot_to_chip(slot);
	struct mmc_host *mmc;
	struct cb710_mmc_reader *reader;
	int err;
	u32 val;

	mmc = devm_mmc_alloc_host(cb710_slot_dev(slot), sizeof(*reader));
	if (!mmc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mmc);

	/* harmless (maybe) magic */
	pci_read_config_dword(chip->pdev, 0x48, &val);
	val = cb710_src_freq_mhz[(val >> 16) & 0xF];
	dev_dbg(cb710_slot_dev(slot), "source frequency: %dMHz\n", val);
	val *= 1000000;

	mmc->ops = &cb710_mmc_host;
	mmc->f_max = val;
	mmc->f_min = val >> cb710_clock_divider_log2[CB710_MAX_DIVIDER_IDX];
	mmc->ocr_avail = MMC_VDD_32_33|MMC_VDD_33_34;
	mmc->caps = MMC_CAP_4_BIT_DATA;
	/*
	 * In cb710_wait_for_event() we use a fixed timeout of ~2s, hence let's
	 * inform the core about it. A future improvement should instead make
	 * use of the cmd->busy_timeout.
	 */
	mmc->max_busy_timeout = CB710_MMC_REQ_TIMEOUT_MS;

	reader = mmc_priv(mmc);

	INIT_WORK(&reader->finish_req_bh_work,
			cb710_mmc_finish_request_bh_work);
	spin_lock_init(&reader->irq_lock);
	cb710_dump_regs(chip, CB710_DUMP_REGS_MMC);

	cb710_mmc_enable_irq(slot, 0, ~0);
	cb710_set_irq_handler(slot, cb710_mmc_irq_handler);

	err = mmc_add_host(mmc);
	if (unlikely(err))
		goto err_free_mmc;

	dev_dbg(cb710_slot_dev(slot), "mmc_hostname is %s\n",
		mmc_hostname(mmc));

	cb710_mmc_enable_irq(slot, CB710_MMC_IE_CARD_INSERTION_STATUS, 0);

	return 0;

err_free_mmc:
	dev_dbg(cb710_slot_dev(slot), "mmc_add_host() failed: %d\n", err);

	cb710_set_irq_handler(slot, NULL);
	return err;
}

static void cb710_mmc_exit(struct platform_device *pdev)
{
	struct cb710_slot *slot = cb710_pdev_to_slot(pdev);
	struct mmc_host *mmc = cb710_slot_to_mmc(slot);
	struct cb710_mmc_reader *reader = mmc_priv(mmc);

	cb710_mmc_enable_irq(slot, 0, CB710_MMC_IE_CARD_INSERTION_STATUS);

	mmc_remove_host(mmc);

	/* IRQs should be disabled now, but let's stay on the safe side */
	cb710_mmc_enable_irq(slot, 0, ~0);
	cb710_set_irq_handler(slot, NULL);

	/* clear config ports - just in case */
	cb710_write_port_32(slot, CB710_MMC_CONFIG_PORT, 0);
	cb710_write_port_16(slot, CB710_MMC_CONFIGB_PORT, 0);

	cancel_work_sync(&reader->finish_req_bh_work);
}

static struct platform_driver cb710_mmc_driver = {
	.driver = {
		.name = "cb710-mmc",
		.pm = pm_sleep_ptr(&cb710_mmc_pmops),
	},
	.probe = cb710_mmc_init,
	.remove = cb710_mmc_exit,
};

module_platform_driver(cb710_mmc_driver);

MODULE_AUTHOR("Michał Mirosław <mirq-linux@rere.qmqm.pl>");
MODULE_DESCRIPTION("ENE CB710 memory card reader driver - MMC/SD part");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cb710-mmc");
