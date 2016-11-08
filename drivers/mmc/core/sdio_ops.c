/*
 *  linux/drivers/mmc/sdio_ops.c
 *
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/scatterlist.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>

#include "core.h"
#include "sdio_ops.h"

int mmc_send_io_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr)
{
	struct mmc_command cmd = {0};
	int i, err = 0;

	cmd.opcode = SD_IO_SEND_OP_COND;
	cmd.arg = ocr;
	cmd.flags = MMC_RSP_SPI_R4 | MMC_RSP_R4 | MMC_CMD_BCR;

	for (i = 100; i; i--) {
		err = mmc_wait_for_cmd(host, &cmd, MMC_CMD_RETRIES);
		if (err)
			break;

		/* if we're just probing, do a single pass */
		if (ocr == 0)
			break;

		/* otherwise wait until reset completes */
		if (mmc_host_is_spi(host)) {
			/*
			 * Both R1_SPI_IDLE and MMC_CARD_BUSY indicate
			 * an initialized card under SPI, but some cards
			 * (Marvell's) only behave when looking at this
			 * one.
			 */
			if (cmd.resp[1] & MMC_CARD_BUSY)
				break;
		} else {
			if (cmd.resp[0] & MMC_CARD_BUSY)
				break;
		}

		err = -ETIMEDOUT;

		mmc_delay(10);
	}

	if (rocr)
		*rocr = cmd.resp[mmc_host_is_spi(host) ? 1 : 0];

	return err;
}

static int mmc_io_rw_direct_host(struct mmc_host *host, int write, unsigned fn,
	unsigned addr, u8 in, u8 *out)
{
	struct mmc_command cmd = {0};
	int err;

	if (fn > 7)
		return -EINVAL;

	/* sanity check */
	if (addr & ~0x1FFFF)
		return -EINVAL;

	cmd.opcode = SD_IO_RW_DIRECT;
	cmd.arg = write ? 0x80000000 : 0x00000000;
	cmd.arg |= fn << 28;
	cmd.arg |= (write && out) ? 0x08000000 : 0x00000000;
	cmd.arg |= addr << 9;
	cmd.arg |= in;
	cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(host, &cmd, 0);
	if (err)
		return err;

	if (mmc_host_is_spi(host)) {
		/* host driver already reported errors */
	} else {
		if (cmd.resp[0] & R5_ERROR)
			return -EIO;
		if (cmd.resp[0] & R5_FUNCTION_NUMBER)
			return -EINVAL;
		if (cmd.resp[0] & R5_OUT_OF_RANGE)
			return -ERANGE;
	}

	if (out) {
		if (mmc_host_is_spi(host))
			*out = (cmd.resp[0] >> 8) & 0xFF;
		else
			*out = cmd.resp[0] & 0xFF;
	}

	return 0;
}

int mmc_io_rw_direct(struct mmc_card *card, int write, unsigned fn,
	unsigned addr, u8 in, u8 *out)
{
	return mmc_io_rw_direct_host(card->host, write, fn, addr, in, out);
}

int mmc_io_rw_extended(struct mmc_card *card, int write, unsigned fn,
	unsigned addr, int incr_addr, u8 *buf, unsigned blocks, unsigned blksz)
{
	struct mmc_request mrq = {NULL};
	struct mmc_command cmd = {0};
	struct mmc_data data = {0};
	struct scatterlist sg, *sg_ptr;
	struct sg_table sgtable;
	unsigned int nents, left_size, i;
	unsigned int seg_size = card->host->max_seg_size;

	WARN_ON(blksz == 0);

	/* sanity check */
	if (addr & ~0x1FFFF)
		return -EINVAL;

	mrq.cmd = &cmd;
	mrq.data = &data;

	cmd.opcode = SD_IO_RW_EXTENDED;
	cmd.arg = write ? 0x80000000 : 0x00000000;
	cmd.arg |= fn << 28;
	cmd.arg |= incr_addr ? 0x04000000 : 0x00000000;
	cmd.arg |= addr << 9;
	if (blocks == 0)
		cmd.arg |= (blksz == 512) ? 0 : blksz;	/* byte mode */
	else
		cmd.arg |= 0x08000000 | blocks;		/* block mode */
	cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;

	data.blksz = blksz;
	/* Code in host drivers/fwk assumes that "blocks" always is >=1 */
	data.blocks = blocks ? blocks : 1;
	data.flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;

	left_size = data.blksz * data.blocks;
	nents = (left_size - 1) / seg_size + 1;
	if (nents > 1) {
		if (sg_alloc_table(&sgtable, nents, GFP_KERNEL))
			return -ENOMEM;

		data.sg = sgtable.sgl;
		data.sg_len = nents;

		for_each_sg(data.sg, sg_ptr, data.sg_len, i) {
			sg_set_page(sg_ptr, virt_to_page(buf + (i * seg_size)),
					min(seg_size, left_size),
					offset_in_page(buf + (i * seg_size)));
			left_size = left_size - seg_size;
		}
	} else {
		data.sg = &sg;
		data.sg_len = 1;

		sg_init_one(&sg, buf, left_size);
	}

	mmc_set_data_timeout(&data, card);

	mmc_wait_for_req(card->host, &mrq);

	if (nents > 1)
		sg_free_table(&sgtable);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	if (mmc_host_is_spi(card->host)) {
		/* host driver already reported errors */
	} else {
		if (cmd.resp[0] & R5_ERROR)
			return -EIO;
		if (cmd.resp[0] & R5_FUNCTION_NUMBER)
			return -EINVAL;
		if (cmd.resp[0] & R5_OUT_OF_RANGE)
			return -ERANGE;
	}

	return 0;
}

int sdio_reset(struct mmc_host *host)
{
	int ret;
	u8 abort;

	/* SDIO Simplified Specification V2.0, 4.4 Reset for SDIO */

	ret = mmc_io_rw_direct_host(host, 0, 0, SDIO_CCCR_ABORT, 0, &abort);
	if (ret)
		abort = 0x08;
	else
		abort |= 0x08;

	return mmc_io_rw_direct_host(host, 1, 0, SDIO_CCCR_ABORT, abort, NULL);
}

