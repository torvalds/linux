/*
 *  linux/drivers/mmc/card/mmc_test.c
 *
 *  Copyright 2007-2008 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>

#include <linux/scatterlist.h>

#define RESULT_OK		0
#define RESULT_FAIL		1
#define RESULT_UNSUP_HOST	2
#define RESULT_UNSUP_CARD	3

#define BUFFER_ORDER		2
#define BUFFER_SIZE		(PAGE_SIZE << BUFFER_ORDER)

struct mmc_test_card {
	struct mmc_card	*card;

	u8		scratch[BUFFER_SIZE];
	u8		*buffer;
#ifdef CONFIG_HIGHMEM
	struct page	*highmem;
#endif
};

/*******************************************************************/
/*  General helper functions                                       */
/*******************************************************************/

/*
 * Configure correct block size in card
 */
static int mmc_test_set_blksize(struct mmc_test_card *test, unsigned size)
{
	struct mmc_command cmd;
	int ret;

	cmd.opcode = MMC_SET_BLOCKLEN;
	cmd.arg = size;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	ret = mmc_wait_for_cmd(test->card->host, &cmd, 0);
	if (ret)
		return ret;

	return 0;
}

/*
 * Fill in the mmc_request structure given a set of transfer parameters.
 */
static void mmc_test_prepare_mrq(struct mmc_test_card *test,
	struct mmc_request *mrq, struct scatterlist *sg, unsigned sg_len,
	unsigned dev_addr, unsigned blocks, unsigned blksz, int write)
{
	BUG_ON(!mrq || !mrq->cmd || !mrq->data || !mrq->stop);

	if (blocks > 1) {
		mrq->cmd->opcode = write ?
			MMC_WRITE_MULTIPLE_BLOCK : MMC_READ_MULTIPLE_BLOCK;
	} else {
		mrq->cmd->opcode = write ?
			MMC_WRITE_BLOCK : MMC_READ_SINGLE_BLOCK;
	}

	mrq->cmd->arg = dev_addr;
	mrq->cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	if (blocks == 1)
		mrq->stop = NULL;
	else {
		mrq->stop->opcode = MMC_STOP_TRANSMISSION;
		mrq->stop->arg = 0;
		mrq->stop->flags = MMC_RSP_R1B | MMC_CMD_AC;
	}

	mrq->data->blksz = blksz;
	mrq->data->blocks = blocks;
	mrq->data->flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mrq->data->sg = sg;
	mrq->data->sg_len = sg_len;

	mmc_set_data_timeout(mrq->data, test->card);
}

/*
 * Wait for the card to finish the busy state
 */
static int mmc_test_wait_busy(struct mmc_test_card *test)
{
	int ret, busy;
	struct mmc_command cmd;

	busy = 0;
	do {
		memset(&cmd, 0, sizeof(struct mmc_command));

		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = test->card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		ret = mmc_wait_for_cmd(test->card->host, &cmd, 0);
		if (ret)
			break;

		if (!busy && !(cmd.resp[0] & R1_READY_FOR_DATA)) {
			busy = 1;
			printk(KERN_INFO "%s: Warning: Host did not "
				"wait for busy state to end.\n",
				mmc_hostname(test->card->host));
		}
	} while (!(cmd.resp[0] & R1_READY_FOR_DATA));

	return ret;
}

/*
 * Transfer a single sector of kernel addressable data
 */
static int mmc_test_buffer_transfer(struct mmc_test_card *test,
	u8 *buffer, unsigned addr, unsigned blksz, int write)
{
	int ret;

	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_data data;

	struct scatterlist sg;

	memset(&mrq, 0, sizeof(struct mmc_request));
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&data, 0, sizeof(struct mmc_data));
	memset(&stop, 0, sizeof(struct mmc_command));

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;

	sg_init_one(&sg, buffer, blksz);

	mmc_test_prepare_mrq(test, &mrq, &sg, 1, addr, 1, blksz, write);

	mmc_wait_for_req(test->card->host, &mrq);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	ret = mmc_test_wait_busy(test);
	if (ret)
		return ret;

	return 0;
}

/*******************************************************************/
/*  Test preparation and cleanup                                   */
/*******************************************************************/

/*
 * Fill the first couple of sectors of the card with known data
 * so that bad reads/writes can be detected
 */
static int __mmc_test_prepare(struct mmc_test_card *test, int write)
{
	int ret, i;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	if (write)
		memset(test->buffer, 0xDF, 512);
	else {
		for (i = 0;i < 512;i++)
			test->buffer[i] = i;
	}

	for (i = 0;i < BUFFER_SIZE / 512;i++) {
		ret = mmc_test_buffer_transfer(test, test->buffer, i * 512, 512, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_prepare_write(struct mmc_test_card *test)
{
	return __mmc_test_prepare(test, 1);
}

static int mmc_test_prepare_read(struct mmc_test_card *test)
{
	return __mmc_test_prepare(test, 0);
}

static int mmc_test_cleanup(struct mmc_test_card *test)
{
	int ret, i;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	memset(test->buffer, 0, 512);

	for (i = 0;i < BUFFER_SIZE / 512;i++) {
		ret = mmc_test_buffer_transfer(test, test->buffer, i * 512, 512, 1);
		if (ret)
			return ret;
	}

	return 0;
}

/*******************************************************************/
/*  Test execution helpers                                         */
/*******************************************************************/

/*
 * Modifies the mmc_request to perform the "short transfer" tests
 */
static void mmc_test_prepare_broken_mrq(struct mmc_test_card *test,
	struct mmc_request *mrq, int write)
{
	BUG_ON(!mrq || !mrq->cmd || !mrq->data);

	if (mrq->data->blocks > 1) {
		mrq->cmd->opcode = write ?
			MMC_WRITE_BLOCK : MMC_READ_SINGLE_BLOCK;
		mrq->stop = NULL;
	} else {
		mrq->cmd->opcode = MMC_SEND_STATUS;
		mrq->cmd->arg = test->card->rca << 16;
	}
}

/*
 * Checks that a normal transfer didn't have any errors
 */
static int mmc_test_check_result(struct mmc_test_card *test,
	struct mmc_request *mrq)
{
	int ret;

	BUG_ON(!mrq || !mrq->cmd || !mrq->data);

	ret = 0;

	if (!ret && mrq->cmd->error)
		ret = mrq->cmd->error;
	if (!ret && mrq->data->error)
		ret = mrq->data->error;
	if (!ret && mrq->stop && mrq->stop->error)
		ret = mrq->stop->error;
	if (!ret && mrq->data->bytes_xfered !=
		mrq->data->blocks * mrq->data->blksz)
		ret = RESULT_FAIL;

	if (ret == -EINVAL)
		ret = RESULT_UNSUP_HOST;

	return ret;
}

/*
 * Checks that a "short transfer" behaved as expected
 */
static int mmc_test_check_broken_result(struct mmc_test_card *test,
	struct mmc_request *mrq)
{
	int ret;

	BUG_ON(!mrq || !mrq->cmd || !mrq->data);

	ret = 0;

	if (!ret && mrq->cmd->error)
		ret = mrq->cmd->error;
	if (!ret && mrq->data->error == 0)
		ret = RESULT_FAIL;
	if (!ret && mrq->data->error != -ETIMEDOUT)
		ret = mrq->data->error;
	if (!ret && mrq->stop && mrq->stop->error)
		ret = mrq->stop->error;
	if (mrq->data->blocks > 1) {
		if (!ret && mrq->data->bytes_xfered > mrq->data->blksz)
			ret = RESULT_FAIL;
	} else {
		if (!ret && mrq->data->bytes_xfered > 0)
			ret = RESULT_FAIL;
	}

	if (ret == -EINVAL)
		ret = RESULT_UNSUP_HOST;

	return ret;
}

/*
 * Tests a basic transfer with certain parameters
 */
static int mmc_test_simple_transfer(struct mmc_test_card *test,
	struct scatterlist *sg, unsigned sg_len, unsigned dev_addr,
	unsigned blocks, unsigned blksz, int write)
{
	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_data data;

	memset(&mrq, 0, sizeof(struct mmc_request));
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&data, 0, sizeof(struct mmc_data));
	memset(&stop, 0, sizeof(struct mmc_command));

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;

	mmc_test_prepare_mrq(test, &mrq, sg, sg_len, dev_addr,
		blocks, blksz, write);

	mmc_wait_for_req(test->card->host, &mrq);

	mmc_test_wait_busy(test);

	return mmc_test_check_result(test, &mrq);
}

/*
 * Tests a transfer where the card will fail completely or partly
 */
static int mmc_test_broken_transfer(struct mmc_test_card *test,
	unsigned blocks, unsigned blksz, int write)
{
	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_data data;

	struct scatterlist sg;

	memset(&mrq, 0, sizeof(struct mmc_request));
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&data, 0, sizeof(struct mmc_data));
	memset(&stop, 0, sizeof(struct mmc_command));

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;

	sg_init_one(&sg, test->buffer, blocks * blksz);

	mmc_test_prepare_mrq(test, &mrq, &sg, 1, 0, blocks, blksz, write);
	mmc_test_prepare_broken_mrq(test, &mrq, write);

	mmc_wait_for_req(test->card->host, &mrq);

	mmc_test_wait_busy(test);

	return mmc_test_check_broken_result(test, &mrq);
}

/*
 * Does a complete transfer test where data is also validated
 *
 * Note: mmc_test_prepare() must have been done before this call
 */
static int mmc_test_transfer(struct mmc_test_card *test,
	struct scatterlist *sg, unsigned sg_len, unsigned dev_addr,
	unsigned blocks, unsigned blksz, int write)
{
	int ret, i;
	unsigned long flags;

	if (write) {
		for (i = 0;i < blocks * blksz;i++)
			test->scratch[i] = i;
	} else {
		memset(test->scratch, 0, BUFFER_SIZE);
	}
	local_irq_save(flags);
	sg_copy_from_buffer(sg, sg_len, test->scratch, BUFFER_SIZE);
	local_irq_restore(flags);

	ret = mmc_test_set_blksize(test, blksz);
	if (ret)
		return ret;

	ret = mmc_test_simple_transfer(test, sg, sg_len, dev_addr,
		blocks, blksz, write);
	if (ret)
		return ret;

	if (write) {
		int sectors;

		ret = mmc_test_set_blksize(test, 512);
		if (ret)
			return ret;

		sectors = (blocks * blksz + 511) / 512;
		if ((sectors * 512) == (blocks * blksz))
			sectors++;

		if ((sectors * 512) > BUFFER_SIZE)
			return -EINVAL;

		memset(test->buffer, 0, sectors * 512);

		for (i = 0;i < sectors;i++) {
			ret = mmc_test_buffer_transfer(test,
				test->buffer + i * 512,
				dev_addr + i * 512, 512, 0);
			if (ret)
				return ret;
		}

		for (i = 0;i < blocks * blksz;i++) {
			if (test->buffer[i] != (u8)i)
				return RESULT_FAIL;
		}

		for (;i < sectors * 512;i++) {
			if (test->buffer[i] != 0xDF)
				return RESULT_FAIL;
		}
	} else {
		local_irq_save(flags);
		sg_copy_to_buffer(sg, sg_len, test->scratch, BUFFER_SIZE);
		local_irq_restore(flags);
		for (i = 0;i < blocks * blksz;i++) {
			if (test->scratch[i] != (u8)i)
				return RESULT_FAIL;
		}
	}

	return 0;
}

/*******************************************************************/
/*  Tests                                                          */
/*******************************************************************/

struct mmc_test_case {
	const char *name;

	int (*prepare)(struct mmc_test_card *);
	int (*run)(struct mmc_test_card *);
	int (*cleanup)(struct mmc_test_card *);
};

static int mmc_test_basic_write(struct mmc_test_card *test)
{
	int ret;
	struct scatterlist sg;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	sg_init_one(&sg, test->buffer, 512);

	ret = mmc_test_simple_transfer(test, &sg, 1, 0, 1, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_basic_read(struct mmc_test_card *test)
{
	int ret;
	struct scatterlist sg;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	sg_init_one(&sg, test->buffer, 512);

	ret = mmc_test_simple_transfer(test, &sg, 1, 0, 1, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_verify_write(struct mmc_test_card *test)
{
	int ret;
	struct scatterlist sg;

	sg_init_one(&sg, test->buffer, 512);

	ret = mmc_test_transfer(test, &sg, 1, 0, 1, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_verify_read(struct mmc_test_card *test)
{
	int ret;
	struct scatterlist sg;

	sg_init_one(&sg, test->buffer, 512);

	ret = mmc_test_transfer(test, &sg, 1, 0, 1, 512, 0);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_write(struct mmc_test_card *test)
{
	int ret;
	unsigned int size;
	struct scatterlist sg;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	sg_init_one(&sg, test->buffer, size);

	ret = mmc_test_transfer(test, &sg, 1, 0, size/512, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_read(struct mmc_test_card *test)
{
	int ret;
	unsigned int size;
	struct scatterlist sg;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	sg_init_one(&sg, test->buffer, size);

	ret = mmc_test_transfer(test, &sg, 1, 0, size/512, 512, 0);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_pow2_write(struct mmc_test_card *test)
{
	int ret, i;
	struct scatterlist sg;

	if (!test->card->csd.write_partial)
		return RESULT_UNSUP_CARD;

	for (i = 1; i < 512;i <<= 1) {
		sg_init_one(&sg, test->buffer, i);
		ret = mmc_test_transfer(test, &sg, 1, 0, 1, i, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_pow2_read(struct mmc_test_card *test)
{
	int ret, i;
	struct scatterlist sg;

	if (!test->card->csd.read_partial)
		return RESULT_UNSUP_CARD;

	for (i = 1; i < 512;i <<= 1) {
		sg_init_one(&sg, test->buffer, i);
		ret = mmc_test_transfer(test, &sg, 1, 0, 1, i, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_weird_write(struct mmc_test_card *test)
{
	int ret, i;
	struct scatterlist sg;

	if (!test->card->csd.write_partial)
		return RESULT_UNSUP_CARD;

	for (i = 3; i < 512;i += 7) {
		sg_init_one(&sg, test->buffer, i);
		ret = mmc_test_transfer(test, &sg, 1, 0, 1, i, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_weird_read(struct mmc_test_card *test)
{
	int ret, i;
	struct scatterlist sg;

	if (!test->card->csd.read_partial)
		return RESULT_UNSUP_CARD;

	for (i = 3; i < 512;i += 7) {
		sg_init_one(&sg, test->buffer, i);
		ret = mmc_test_transfer(test, &sg, 1, 0, 1, i, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_write(struct mmc_test_card *test)
{
	int ret, i;
	struct scatterlist sg;

	for (i = 1;i < 4;i++) {
		sg_init_one(&sg, test->buffer + i, 512);
		ret = mmc_test_transfer(test, &sg, 1, 0, 1, 512, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_read(struct mmc_test_card *test)
{
	int ret, i;
	struct scatterlist sg;

	for (i = 1;i < 4;i++) {
		sg_init_one(&sg, test->buffer + i, 512);
		ret = mmc_test_transfer(test, &sg, 1, 0, 1, 512, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_multi_write(struct mmc_test_card *test)
{
	int ret, i;
	unsigned int size;
	struct scatterlist sg;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	for (i = 1;i < 4;i++) {
		sg_init_one(&sg, test->buffer + i, size);
		ret = mmc_test_transfer(test, &sg, 1, 0, size/512, 512, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_multi_read(struct mmc_test_card *test)
{
	int ret, i;
	unsigned int size;
	struct scatterlist sg;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	for (i = 1;i < 4;i++) {
		sg_init_one(&sg, test->buffer + i, size);
		ret = mmc_test_transfer(test, &sg, 1, 0, size/512, 512, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_xfersize_write(struct mmc_test_card *test)
{
	int ret;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	ret = mmc_test_broken_transfer(test, 1, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_xfersize_read(struct mmc_test_card *test)
{
	int ret;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	ret = mmc_test_broken_transfer(test, 1, 512, 0);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_xfersize_write(struct mmc_test_card *test)
{
	int ret;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	ret = mmc_test_broken_transfer(test, 2, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_xfersize_read(struct mmc_test_card *test)
{
	int ret;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	ret = mmc_test_broken_transfer(test, 2, 512, 0);
	if (ret)
		return ret;

	return 0;
}

#ifdef CONFIG_HIGHMEM

static int mmc_test_write_high(struct mmc_test_card *test)
{
	int ret;
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, test->highmem, 512, 0);

	ret = mmc_test_transfer(test, &sg, 1, 0, 1, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_read_high(struct mmc_test_card *test)
{
	int ret;
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, test->highmem, 512, 0);

	ret = mmc_test_transfer(test, &sg, 1, 0, 1, 512, 0);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_write_high(struct mmc_test_card *test)
{
	int ret;
	unsigned int size;
	struct scatterlist sg;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, test->highmem, size, 0);

	ret = mmc_test_transfer(test, &sg, 1, 0, size/512, 512, 1);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_read_high(struct mmc_test_card *test)
{
	int ret;
	unsigned int size;
	struct scatterlist sg;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, test->highmem, size, 0);

	ret = mmc_test_transfer(test, &sg, 1, 0, size/512, 512, 0);
	if (ret)
		return ret;

	return 0;
}

#endif /* CONFIG_HIGHMEM */

static const struct mmc_test_case mmc_test_cases[] = {
	{
		.name = "Basic write (no data verification)",
		.run = mmc_test_basic_write,
	},

	{
		.name = "Basic read (no data verification)",
		.run = mmc_test_basic_read,
	},

	{
		.name = "Basic write (with data verification)",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_verify_write,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Basic read (with data verification)",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_verify_read,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Multi-block write",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_multi_write,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Multi-block read",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_multi_read,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Power of two block writes",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_pow2_write,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Power of two block reads",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_pow2_read,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Weird sized block writes",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_weird_write,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Weird sized block reads",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_weird_read,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Badly aligned write",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_align_write,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Badly aligned read",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_align_read,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Badly aligned multi-block write",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_align_multi_write,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Badly aligned multi-block read",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_align_multi_read,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Correct xfer_size at write (start failure)",
		.run = mmc_test_xfersize_write,
	},

	{
		.name = "Correct xfer_size at read (start failure)",
		.run = mmc_test_xfersize_read,
	},

	{
		.name = "Correct xfer_size at write (midway failure)",
		.run = mmc_test_multi_xfersize_write,
	},

	{
		.name = "Correct xfer_size at read (midway failure)",
		.run = mmc_test_multi_xfersize_read,
	},

#ifdef CONFIG_HIGHMEM

	{
		.name = "Highmem write",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_write_high,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Highmem read",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_read_high,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Multi-block highmem write",
		.prepare = mmc_test_prepare_write,
		.run = mmc_test_multi_write_high,
		.cleanup = mmc_test_cleanup,
	},

	{
		.name = "Multi-block highmem read",
		.prepare = mmc_test_prepare_read,
		.run = mmc_test_multi_read_high,
		.cleanup = mmc_test_cleanup,
	},

#endif /* CONFIG_HIGHMEM */

};

static DEFINE_MUTEX(mmc_test_lock);

static void mmc_test_run(struct mmc_test_card *test, int testcase)
{
	int i, ret;

	printk(KERN_INFO "%s: Starting tests of card %s...\n",
		mmc_hostname(test->card->host), mmc_card_id(test->card));

	mmc_claim_host(test->card->host);

	for (i = 0;i < ARRAY_SIZE(mmc_test_cases);i++) {
		if (testcase && ((i + 1) != testcase))
			continue;

		printk(KERN_INFO "%s: Test case %d. %s...\n",
			mmc_hostname(test->card->host), i + 1,
			mmc_test_cases[i].name);

		if (mmc_test_cases[i].prepare) {
			ret = mmc_test_cases[i].prepare(test);
			if (ret) {
				printk(KERN_INFO "%s: Result: Prepare "
					"stage failed! (%d)\n",
					mmc_hostname(test->card->host),
					ret);
				continue;
			}
		}

		ret = mmc_test_cases[i].run(test);
		switch (ret) {
		case RESULT_OK:
			printk(KERN_INFO "%s: Result: OK\n",
				mmc_hostname(test->card->host));
			break;
		case RESULT_FAIL:
			printk(KERN_INFO "%s: Result: FAILED\n",
				mmc_hostname(test->card->host));
			break;
		case RESULT_UNSUP_HOST:
			printk(KERN_INFO "%s: Result: UNSUPPORTED "
				"(by host)\n",
				mmc_hostname(test->card->host));
			break;
		case RESULT_UNSUP_CARD:
			printk(KERN_INFO "%s: Result: UNSUPPORTED "
				"(by card)\n",
				mmc_hostname(test->card->host));
			break;
		default:
			printk(KERN_INFO "%s: Result: ERROR (%d)\n",
				mmc_hostname(test->card->host), ret);
		}

		if (mmc_test_cases[i].cleanup) {
			ret = mmc_test_cases[i].cleanup(test);
			if (ret) {
				printk(KERN_INFO "%s: Warning: Cleanup "
					"stage failed! (%d)\n",
					mmc_hostname(test->card->host),
					ret);
			}
		}
	}

	mmc_release_host(test->card->host);

	printk(KERN_INFO "%s: Tests completed.\n",
		mmc_hostname(test->card->host));
}

static ssize_t mmc_test_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	mutex_lock(&mmc_test_lock);
	mutex_unlock(&mmc_test_lock);

	return 0;
}

static ssize_t mmc_test_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mmc_card *card;
	struct mmc_test_card *test;
	int testcase;

	card = container_of(dev, struct mmc_card, dev);

	testcase = simple_strtol(buf, NULL, 10);

	test = kzalloc(sizeof(struct mmc_test_card), GFP_KERNEL);
	if (!test)
		return -ENOMEM;

	test->card = card;

	test->buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
#ifdef CONFIG_HIGHMEM
	test->highmem = alloc_pages(GFP_KERNEL | __GFP_HIGHMEM, BUFFER_ORDER);
#endif

#ifdef CONFIG_HIGHMEM
	if (test->buffer && test->highmem) {
#else
	if (test->buffer) {
#endif
		mutex_lock(&mmc_test_lock);
		mmc_test_run(test, testcase);
		mutex_unlock(&mmc_test_lock);
	}

#ifdef CONFIG_HIGHMEM
	__free_pages(test->highmem, BUFFER_ORDER);
#endif
	kfree(test->buffer);
	kfree(test);

	return count;
}

static DEVICE_ATTR(test, S_IWUSR | S_IRUGO, mmc_test_show, mmc_test_store);

static int mmc_test_probe(struct mmc_card *card)
{
	int ret;

	if ((card->type != MMC_TYPE_MMC) && (card->type != MMC_TYPE_SD))
		return -ENODEV;

	ret = device_create_file(&card->dev, &dev_attr_test);
	if (ret)
		return ret;

	dev_info(&card->dev, "Card claimed for testing.\n");

	return 0;
}

static void mmc_test_remove(struct mmc_card *card)
{
	device_remove_file(&card->dev, &dev_attr_test);
}

static struct mmc_driver mmc_driver = {
	.drv		= {
		.name	= "mmc_test",
	},
	.probe		= mmc_test_probe,
	.remove		= mmc_test_remove,
};

static int __init mmc_test_init(void)
{
	return mmc_register_driver(&mmc_driver);
}

static void __exit mmc_test_exit(void)
{
	mmc_unregister_driver(&mmc_driver);
}

module_init(mmc_test_init);
module_exit(mmc_test_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multimedia Card (MMC) host test driver");
MODULE_AUTHOR("Pierre Ossman");
