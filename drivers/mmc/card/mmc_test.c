/*
 *  linux/drivers/mmc/card/mmc_test.c
 *
 *  Copyright 2007 Pierre Ossman
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

#define BUFFER_SIZE	(PAGE_SIZE * 4)

struct mmc_test_card {
	struct mmc_card	*card;

	u8		*buffer;
};

/*******************************************************************/
/*  Helper functions                                               */
/*******************************************************************/

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

static int __mmc_test_transfer(struct mmc_test_card *test, int write,
	unsigned broken_xfer, u8 *buffer, unsigned addr,
	unsigned blocks, unsigned blksz)
{
	int ret, busy;

	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_data data;

	struct scatterlist sg;

	memset(&mrq, 0, sizeof(struct mmc_request));

	mrq.cmd = &cmd;
	mrq.data = &data;

	memset(&cmd, 0, sizeof(struct mmc_command));

	if (broken_xfer) {
		if (blocks > 1) {
			cmd.opcode = write ?
				MMC_WRITE_BLOCK : MMC_READ_SINGLE_BLOCK;
		} else {
			cmd.opcode = MMC_SEND_STATUS;
		}
	} else {
		if (blocks > 1) {
			cmd.opcode = write ?
				MMC_WRITE_MULTIPLE_BLOCK : MMC_READ_MULTIPLE_BLOCK;
		} else {
			cmd.opcode = write ?
				MMC_WRITE_BLOCK : MMC_READ_SINGLE_BLOCK;
		}
	}

	if (broken_xfer && blocks == 1)
		cmd.arg = test->card->rca << 16;
	else
		cmd.arg = addr;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	memset(&stop, 0, sizeof(struct mmc_command));

	if (!broken_xfer && (blocks > 1)) {
		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		mrq.stop = &stop;
	}

	memset(&data, 0, sizeof(struct mmc_data));

	data.blksz = blksz;
	data.blocks = blocks;
	data.flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;

	sg_init_one(&sg, buffer, blocks * blksz);

	mmc_set_data_timeout(&data, test->card);

	mmc_wait_for_req(test->card->host, &mrq);

	ret = 0;

	if (broken_xfer) {
		if (!ret && cmd.error)
			ret = cmd.error;
		if (!ret && data.error == 0)
			ret = RESULT_FAIL;
		if (!ret && data.error != -ETIMEDOUT)
			ret = data.error;
		if (!ret && stop.error)
			ret = stop.error;
		if (blocks > 1) {
			if (!ret && data.bytes_xfered > blksz)
				ret = RESULT_FAIL;
		} else {
			if (!ret && data.bytes_xfered > 0)
				ret = RESULT_FAIL;
		}
	} else {
		if (!ret && cmd.error)
			ret = cmd.error;
		if (!ret && data.error)
			ret = data.error;
		if (!ret && stop.error)
			ret = stop.error;
		if (!ret && data.bytes_xfered != blocks * blksz)
			ret = RESULT_FAIL;
	}

	if (ret == -EINVAL)
		ret = RESULT_UNSUP_HOST;

	busy = 0;
	do {
		int ret2;

		memset(&cmd, 0, sizeof(struct mmc_command));

		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = test->card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		ret2 = mmc_wait_for_cmd(test->card->host, &cmd, 0);
		if (ret2)
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

static int mmc_test_transfer(struct mmc_test_card *test, int write,
	u8 *buffer, unsigned addr, unsigned blocks, unsigned blksz)
{
	return __mmc_test_transfer(test, write, 0, buffer,
			addr, blocks, blksz);
}

static int mmc_test_prepare_verify(struct mmc_test_card *test, int write)
{
	int ret, i;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	if (write)
		memset(test->buffer, 0xDF, BUFFER_SIZE);
	else {
		for (i = 0;i < BUFFER_SIZE;i++)
			test->buffer[i] = i;
	}

	for (i = 0;i < BUFFER_SIZE / 512;i++) {
		ret = mmc_test_transfer(test, 1, test->buffer + i * 512,
			i * 512, 1, 512);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_prepare_verify_write(struct mmc_test_card *test)
{
	return mmc_test_prepare_verify(test, 1);
}

static int mmc_test_prepare_verify_read(struct mmc_test_card *test)
{
	return mmc_test_prepare_verify(test, 0);
}

static int mmc_test_verified_transfer(struct mmc_test_card *test, int write,
	u8 *buffer, unsigned addr, unsigned blocks, unsigned blksz)
{
	int ret, i, sectors;

	/*
	 * It is assumed that the above preparation has been done.
	 */

	memset(test->buffer, 0, BUFFER_SIZE);

	if (write) {
		for (i = 0;i < blocks * blksz;i++)
			buffer[i] = i;
	}

	ret = mmc_test_set_blksize(test, blksz);
	if (ret)
		return ret;

	ret = mmc_test_transfer(test, write, buffer, addr, blocks, blksz);
	if (ret)
		return ret;

	if (write) {
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
			ret = mmc_test_transfer(test, 0,
				test->buffer + i * 512,
				addr + i * 512, 1, 512);
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
		for (i = 0;i < blocks * blksz;i++) {
			if (buffer[i] != (u8)i)
				return RESULT_FAIL;
		}
	}

	return 0;
}

static int mmc_test_cleanup_verify(struct mmc_test_card *test)
{
	int ret, i;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	memset(test->buffer, 0, BUFFER_SIZE);

	for (i = 0;i < BUFFER_SIZE / 512;i++) {
		ret = mmc_test_transfer(test, 1, test->buffer + i * 512,
			i * 512, 1, 512);
		if (ret)
			return ret;
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

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	ret = mmc_test_transfer(test, 1, test->buffer, 0, 1, 512);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_basic_read(struct mmc_test_card *test)
{
	int ret;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	ret = mmc_test_transfer(test, 0, test->buffer, 0, 1, 512);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_verify_write(struct mmc_test_card *test)
{
	int ret;

	ret = mmc_test_verified_transfer(test, 1, test->buffer, 0, 1, 512);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_verify_read(struct mmc_test_card *test)
{
	int ret;

	ret = mmc_test_verified_transfer(test, 0, test->buffer, 0, 1, 512);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_write(struct mmc_test_card *test)
{
	int ret;
	unsigned int size;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	ret = mmc_test_verified_transfer(test, 1, test->buffer, 0,
		size / 512, 512);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_multi_read(struct mmc_test_card *test)
{
	int ret;
	unsigned int size;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	ret = mmc_test_verified_transfer(test, 0, test->buffer, 0,
		size / 512, 512);
	if (ret)
		return ret;

	return 0;
}

static int mmc_test_pow2_write(struct mmc_test_card *test)
{
	int ret, i;

	if (!test->card->csd.write_partial)
		return RESULT_UNSUP_CARD;

	for (i = 1; i < 512;i <<= 1) {
		ret = mmc_test_verified_transfer(test, 1,
			test->buffer, 0, 1, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_pow2_read(struct mmc_test_card *test)
{
	int ret, i;

	if (!test->card->csd.read_partial)
		return RESULT_UNSUP_CARD;

	for (i = 1; i < 512;i <<= 1) {
		ret = mmc_test_verified_transfer(test, 0,
			test->buffer, 0, 1, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_weird_write(struct mmc_test_card *test)
{
	int ret, i;

	if (!test->card->csd.write_partial)
		return RESULT_UNSUP_CARD;

	for (i = 3; i < 512;i += 7) {
		ret = mmc_test_verified_transfer(test, 1,
			test->buffer, 0, 1, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_weird_read(struct mmc_test_card *test)
{
	int ret, i;

	if (!test->card->csd.read_partial)
		return RESULT_UNSUP_CARD;

	for (i = 3; i < 512;i += 7) {
		ret = mmc_test_verified_transfer(test, 0,
			test->buffer, 0, 1, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_write(struct mmc_test_card *test)
{
	int ret, i;

	for (i = 1;i < 4;i++) {
		ret = mmc_test_verified_transfer(test, 1, test->buffer + i,
			0, 1, 512);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_read(struct mmc_test_card *test)
{
	int ret, i;

	for (i = 1;i < 4;i++) {
		ret = mmc_test_verified_transfer(test, 0, test->buffer + i,
			0, 1, 512);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_multi_write(struct mmc_test_card *test)
{
	int ret, i;
	unsigned int size;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	for (i = 1;i < 4;i++) {
		ret = mmc_test_verified_transfer(test, 1, test->buffer + i,
			0, size / 512, 512);
		if (ret)
			return ret;
	}

	return 0;
}

static int mmc_test_align_multi_read(struct mmc_test_card *test)
{
	int ret, i;
	unsigned int size;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	size = PAGE_SIZE * 2;
	size = min(size, test->card->host->max_req_size);
	size = min(size, test->card->host->max_seg_size);
	size = min(size, test->card->host->max_blk_count * 512);

	if (size < 1024)
		return RESULT_UNSUP_HOST;

	for (i = 1;i < 4;i++) {
		ret = mmc_test_verified_transfer(test, 0, test->buffer + i,
			0, size / 512, 512);
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

	ret = __mmc_test_transfer(test, 1, 1, test->buffer, 0, 1, 512);
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

	ret = __mmc_test_transfer(test, 0, 1, test->buffer, 0, 1, 512);
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

	ret = __mmc_test_transfer(test, 1, 1, test->buffer, 0, 2, 512);
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

	ret = __mmc_test_transfer(test, 0, 1, test->buffer, 0, 2, 512);
	if (ret)
		return ret;

	return 0;
}

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
		.prepare = mmc_test_prepare_verify_write,
		.run = mmc_test_verify_write,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Basic read (with data verification)",
		.prepare = mmc_test_prepare_verify_read,
		.run = mmc_test_verify_read,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Multi-block write",
		.prepare = mmc_test_prepare_verify_write,
		.run = mmc_test_multi_write,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Multi-block read",
		.prepare = mmc_test_prepare_verify_read,
		.run = mmc_test_multi_read,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Power of two block writes",
		.prepare = mmc_test_prepare_verify_write,
		.run = mmc_test_pow2_write,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Power of two block reads",
		.prepare = mmc_test_prepare_verify_read,
		.run = mmc_test_pow2_read,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Weird sized block writes",
		.prepare = mmc_test_prepare_verify_write,
		.run = mmc_test_weird_write,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Weird sized block reads",
		.prepare = mmc_test_prepare_verify_read,
		.run = mmc_test_weird_read,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Badly aligned write",
		.prepare = mmc_test_prepare_verify_write,
		.run = mmc_test_align_write,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Badly aligned read",
		.prepare = mmc_test_prepare_verify_read,
		.run = mmc_test_align_read,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Badly aligned multi-block write",
		.prepare = mmc_test_prepare_verify_write,
		.run = mmc_test_align_multi_write,
		.cleanup = mmc_test_cleanup_verify,
	},

	{
		.name = "Badly aligned multi-block read",
		.prepare = mmc_test_prepare_verify_read,
		.run = mmc_test_align_multi_read,
		.cleanup = mmc_test_cleanup_verify,
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
};

static struct mutex mmc_test_lock;

static void mmc_test_run(struct mmc_test_card *test)
{
	int i, ret;

	printk(KERN_INFO "%s: Starting tests of card %s...\n",
		mmc_hostname(test->card->host), mmc_card_id(test->card));

	mmc_claim_host(test->card->host);

	for (i = 0;i < ARRAY_SIZE(mmc_test_cases);i++) {
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

	card = container_of(dev, struct mmc_card, dev);

	test = kzalloc(sizeof(struct mmc_test_card), GFP_KERNEL);
	if (!test)
		return -ENOMEM;

	test->card = card;

	test->buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
	if (test->buffer) {
		mutex_lock(&mmc_test_lock);
		mmc_test_run(test);
		mutex_unlock(&mmc_test_lock);
	}

	kfree(test->buffer);
	kfree(test);

	return count;
}

static DEVICE_ATTR(test, S_IWUSR | S_IRUGO, mmc_test_show, mmc_test_store);

static int mmc_test_probe(struct mmc_card *card)
{
	int ret;

	mutex_init(&mmc_test_lock);

	ret = device_create_file(&card->dev, &dev_attr_test);
	if (ret)
		return ret;

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
