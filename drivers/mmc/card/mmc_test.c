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
#include <linux/slab.h>

#include <linux/scatterlist.h>
#include <linux/swap.h>		/* For nr_free_buffer_pages() */

#define RESULT_OK		0
#define RESULT_FAIL		1
#define RESULT_UNSUP_HOST	2
#define RESULT_UNSUP_CARD	3

#define BUFFER_ORDER		2
#define BUFFER_SIZE		(PAGE_SIZE << BUFFER_ORDER)

/*
 * Limit the test area size to the maximum MMC HC erase group size.  Note that
 * the maximum SD allocation unit size is just 4MiB.
 */
#define TEST_AREA_MAX_SIZE (128 * 1024 * 1024)

/**
 * struct mmc_test_pages - pages allocated by 'alloc_pages()'.
 * @page: first page in the allocation
 * @order: order of the number of pages allocated
 */
struct mmc_test_pages {
	struct page *page;
	unsigned int order;
};

/**
 * struct mmc_test_mem - allocated memory.
 * @arr: array of allocations
 * @cnt: number of allocations
 */
struct mmc_test_mem {
	struct mmc_test_pages *arr;
	unsigned int cnt;
};

/**
 * struct mmc_test_area - information for performance tests.
 * @max_sz: test area size (in bytes)
 * @dev_addr: address on card at which to do performance tests
 * @max_segs: maximum segments in scatterlist @sg
 * @blocks: number of (512 byte) blocks currently mapped by @sg
 * @sg_len: length of currently mapped scatterlist @sg
 * @mem: allocated memory
 * @sg: scatterlist
 */
struct mmc_test_area {
	unsigned long max_sz;
	unsigned int dev_addr;
	unsigned int max_segs;
	unsigned int blocks;
	unsigned int sg_len;
	struct mmc_test_mem *mem;
	struct scatterlist *sg;
};

/**
 * struct mmc_test_card - test information.
 * @card: card under test
 * @scratch: transfer buffer
 * @buffer: transfer buffer
 * @highmem: buffer for highmem tests
 * @area: information for performance tests
 */
struct mmc_test_card {
	struct mmc_card	*card;

	u8		scratch[BUFFER_SIZE];
	u8		*buffer;
#ifdef CONFIG_HIGHMEM
	struct page	*highmem;
#endif
	struct mmc_test_area area;
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
	if (!mmc_card_blockaddr(test->card))
		mrq->cmd->arg <<= 9;

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

static int mmc_test_busy(struct mmc_command *cmd)
{
	return !(cmd->resp[0] & R1_READY_FOR_DATA) ||
		(R1_CURRENT_STATE(cmd->resp[0]) == 7);
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

		if (!busy && mmc_test_busy(&cmd)) {
			busy = 1;
			printk(KERN_INFO "%s: Warning: Host did not "
				"wait for busy state to end.\n",
				mmc_hostname(test->card->host));
		}
	} while (mmc_test_busy(&cmd));

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

static void mmc_test_free_mem(struct mmc_test_mem *mem)
{
	if (!mem)
		return;
	while (mem->cnt--)
		__free_pages(mem->arr[mem->cnt].page,
			     mem->arr[mem->cnt].order);
	kfree(mem->arr);
	kfree(mem);
}

/*
 * Allocate a lot of memory, preferrably max_sz but at least min_sz.  In case
 * there isn't much memory do not exceed 1/16th total lowmem pages.
 */
static struct mmc_test_mem *mmc_test_alloc_mem(unsigned long min_sz,
					       unsigned long max_sz)
{
	unsigned long max_page_cnt = DIV_ROUND_UP(max_sz, PAGE_SIZE);
	unsigned long min_page_cnt = DIV_ROUND_UP(min_sz, PAGE_SIZE);
	unsigned long page_cnt = 0;
	unsigned long limit = nr_free_buffer_pages() >> 4;
	struct mmc_test_mem *mem;

	if (max_page_cnt > limit)
		max_page_cnt = limit;
	if (max_page_cnt < min_page_cnt)
		max_page_cnt = min_page_cnt;

	mem = kzalloc(sizeof(struct mmc_test_mem), GFP_KERNEL);
	if (!mem)
		return NULL;

	mem->arr = kzalloc(sizeof(struct mmc_test_pages) * max_page_cnt,
			   GFP_KERNEL);
	if (!mem->arr)
		goto out_free;

	while (max_page_cnt) {
		struct page *page;
		unsigned int order;
		gfp_t flags = GFP_KERNEL | GFP_DMA | __GFP_NOWARN |
				__GFP_NORETRY;

		order = get_order(max_page_cnt << PAGE_SHIFT);
		while (1) {
			page = alloc_pages(flags, order);
			if (page || !order)
				break;
			order -= 1;
		}
		if (!page) {
			if (page_cnt < min_page_cnt)
				goto out_free;
			break;
		}
		mem->arr[mem->cnt].page = page;
		mem->arr[mem->cnt].order = order;
		mem->cnt += 1;
		if (max_page_cnt <= (1UL << order))
			break;
		max_page_cnt -= 1UL << order;
		page_cnt += 1UL << order;
	}

	return mem;

out_free:
	mmc_test_free_mem(mem);
	return NULL;
}

/*
 * Map memory into a scatterlist.  Optionally allow the same memory to be
 * mapped more than once.
 */
static int mmc_test_map_sg(struct mmc_test_mem *mem, unsigned long sz,
			   struct scatterlist *sglist, int repeat,
			   unsigned int max_segs, unsigned int *sg_len)
{
	struct scatterlist *sg = NULL;
	unsigned int i;

	sg_init_table(sglist, max_segs);

	*sg_len = 0;
	do {
		for (i = 0; i < mem->cnt; i++) {
			unsigned long len = PAGE_SIZE << mem->arr[i].order;

			if (sz < len)
				len = sz;
			if (sg)
				sg = sg_next(sg);
			else
				sg = sglist;
			if (!sg)
				return -EINVAL;
			sg_set_page(sg, mem->arr[i].page, len, 0);
			sz -= len;
			*sg_len += 1;
			if (!sz)
				break;
		}
	} while (sz && repeat);

	if (sz)
		return -EINVAL;

	if (sg)
		sg_mark_end(sg);

	return 0;
}

/*
 * Map memory into a scatterlist so that no pages are contiguous.  Allow the
 * same memory to be mapped more than once.
 */
static int mmc_test_map_sg_max_scatter(struct mmc_test_mem *mem,
				       unsigned long sz,
				       struct scatterlist *sglist,
				       unsigned int max_segs,
				       unsigned int *sg_len)
{
	struct scatterlist *sg = NULL;
	unsigned int i = mem->cnt, cnt;
	unsigned long len;
	void *base, *addr, *last_addr = NULL;

	sg_init_table(sglist, max_segs);

	*sg_len = 0;
	while (sz && i) {
		base = page_address(mem->arr[--i].page);
		cnt = 1 << mem->arr[i].order;
		while (sz && cnt) {
			addr = base + PAGE_SIZE * --cnt;
			if (last_addr && last_addr + PAGE_SIZE == addr)
				continue;
			last_addr = addr;
			len = PAGE_SIZE;
			if (sz < len)
				len = sz;
			if (sg)
				sg = sg_next(sg);
			else
				sg = sglist;
			if (!sg)
				return -EINVAL;
			sg_set_page(sg, virt_to_page(addr), len, 0);
			sz -= len;
			*sg_len += 1;
		}
	}

	if (sg)
		sg_mark_end(sg);

	return 0;
}

/*
 * Calculate transfer rate in bytes per second.
 */
static unsigned int mmc_test_rate(uint64_t bytes, struct timespec *ts)
{
	uint64_t ns;

	ns = ts->tv_sec;
	ns *= 1000000000;
	ns += ts->tv_nsec;

	bytes *= 1000000000;

	while (ns > UINT_MAX) {
		bytes >>= 1;
		ns >>= 1;
	}

	if (!ns)
		return 0;

	do_div(bytes, (uint32_t)ns);

	return bytes;
}

/*
 * Print the transfer rate.
 */
static void mmc_test_print_rate(struct mmc_test_card *test, uint64_t bytes,
				struct timespec *ts1, struct timespec *ts2)
{
	unsigned int rate, sectors = bytes >> 9;
	struct timespec ts;

	ts = timespec_sub(*ts2, *ts1);

	rate = mmc_test_rate(bytes, &ts);

	printk(KERN_INFO "%s: Transfer of %u sectors (%u%s KiB) took %lu.%09lu "
			 "seconds (%u kB/s, %u KiB/s)\n",
			 mmc_hostname(test->card->host), sectors, sectors >> 1,
			 (sectors == 1 ? ".5" : ""), (unsigned long)ts.tv_sec,
			 (unsigned long)ts.tv_nsec, rate / 1000, rate / 1024);
}

/*
 * Print the average transfer rate.
 */
static void mmc_test_print_avg_rate(struct mmc_test_card *test, uint64_t bytes,
				    unsigned int count, struct timespec *ts1,
				    struct timespec *ts2)
{
	unsigned int rate, sectors = bytes >> 9;
	uint64_t tot = bytes * count;
	struct timespec ts;

	ts = timespec_sub(*ts2, *ts1);

	rate = mmc_test_rate(tot, &ts);

	printk(KERN_INFO "%s: Transfer of %u x %u sectors (%u x %u%s KiB) took "
			 "%lu.%09lu seconds (%u kB/s, %u KiB/s)\n",
			 mmc_hostname(test->card->host), count, sectors, count,
			 sectors >> 1, (sectors == 1 ? ".5" : ""),
			 (unsigned long)ts.tv_sec, (unsigned long)ts.tv_nsec,
			 rate / 1000, rate / 1024);
}

/*
 * Return the card size in sectors.
 */
static unsigned int mmc_test_capacity(struct mmc_card *card)
{
	if (!mmc_card_sd(card) && mmc_card_blockaddr(card))
		return card->ext_csd.sectors;
	else
		return card->csd.capacity << (card->csd.read_blkbits - 9);
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
		ret = mmc_test_buffer_transfer(test, test->buffer, i, 512, 1);
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
		ret = mmc_test_buffer_transfer(test, test->buffer, i, 512, 1);
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
				dev_addr + i, 512, 0);
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

	ret = mmc_test_simple_transfer(test, &sg, 1, 0, 1, 512, 0);
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

#else

static int mmc_test_no_highmem(struct mmc_test_card *test)
{
	printk(KERN_INFO "%s: Highmem not configured - test skipped\n",
	       mmc_hostname(test->card->host));
	return 0;
}

#endif /* CONFIG_HIGHMEM */

/*
 * Map sz bytes so that it can be transferred.
 */
static int mmc_test_area_map(struct mmc_test_card *test, unsigned long sz,
			     int max_scatter)
{
	struct mmc_test_area *t = &test->area;

	t->blocks = sz >> 9;

	if (max_scatter) {
		return mmc_test_map_sg_max_scatter(t->mem, sz, t->sg,
						   t->max_segs, &t->sg_len);
	} else {
		return mmc_test_map_sg(t->mem, sz, t->sg, 1, t->max_segs,
				       &t->sg_len);
	}
}

/*
 * Transfer bytes mapped by mmc_test_area_map().
 */
static int mmc_test_area_transfer(struct mmc_test_card *test,
				  unsigned int dev_addr, int write)
{
	struct mmc_test_area *t = &test->area;

	return mmc_test_simple_transfer(test, t->sg, t->sg_len, dev_addr,
					t->blocks, 512, write);
}

/*
 * Map and transfer bytes.
 */
static int mmc_test_area_io(struct mmc_test_card *test, unsigned long sz,
			    unsigned int dev_addr, int write, int max_scatter,
			    int timed)
{
	struct timespec ts1, ts2;
	int ret;

	ret = mmc_test_area_map(test, sz, max_scatter);
	if (ret)
		return ret;

	if (timed)
		getnstimeofday(&ts1);

	ret = mmc_test_area_transfer(test, dev_addr, write);
	if (ret)
		return ret;

	if (timed)
		getnstimeofday(&ts2);

	if (timed)
		mmc_test_print_rate(test, sz, &ts1, &ts2);

	return 0;
}

/*
 * Write the test area entirely.
 */
static int mmc_test_area_fill(struct mmc_test_card *test)
{
	return mmc_test_area_io(test, test->area.max_sz, test->area.dev_addr,
				1, 0, 0);
}

/*
 * Erase the test area entirely.
 */
static int mmc_test_area_erase(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;

	if (!mmc_can_erase(test->card))
		return 0;

	return mmc_erase(test->card, t->dev_addr, test->area.max_sz >> 9,
			 MMC_ERASE_ARG);
}

/*
 * Cleanup struct mmc_test_area.
 */
static int mmc_test_area_cleanup(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;

	kfree(t->sg);
	mmc_test_free_mem(t->mem);

	return 0;
}

/*
 * Initialize an area for testing large transfers.  The size of the area is the
 * preferred erase size which is a good size for optimal transfer speed.  Note
 * that is typically 4MiB for modern cards.  The test area is set to the middle
 * of the card because cards may have different charateristics at the front
 * (for FAT file system optimization).  Optionally, the area is erased (if the
 * card supports it) which may improve write performance.  Optionally, the area
 * is filled with data for subsequent read tests.
 */
static int mmc_test_area_init(struct mmc_test_card *test, int erase, int fill)
{
	struct mmc_test_area *t = &test->area;
	unsigned long min_sz = 64 * 1024;
	int ret;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	if (test->card->pref_erase > TEST_AREA_MAX_SIZE >> 9)
		t->max_sz = TEST_AREA_MAX_SIZE;
	else
		t->max_sz = (unsigned long)test->card->pref_erase << 9;
	/*
	 * Try to allocate enough memory for the whole area.  Less is OK
	 * because the same memory can be mapped into the scatterlist more than
	 * once.
	 */
	t->mem = mmc_test_alloc_mem(min_sz, t->max_sz);
	if (!t->mem)
		return -ENOMEM;

	t->max_segs = DIV_ROUND_UP(t->max_sz, PAGE_SIZE);
	t->sg = kmalloc(sizeof(struct scatterlist) * t->max_segs, GFP_KERNEL);
	if (!t->sg) {
		ret = -ENOMEM;
		goto out_free;
	}

	t->dev_addr = mmc_test_capacity(test->card) / 2;
	t->dev_addr -= t->dev_addr % (t->max_sz >> 9);

	if (erase) {
		ret = mmc_test_area_erase(test);
		if (ret)
			goto out_free;
	}

	if (fill) {
		ret = mmc_test_area_fill(test);
		if (ret)
			goto out_free;
	}

	return 0;

out_free:
	mmc_test_area_cleanup(test);
	return ret;
}

/*
 * Prepare for large transfers.  Do not erase the test area.
 */
static int mmc_test_area_prepare(struct mmc_test_card *test)
{
	return mmc_test_area_init(test, 0, 0);
}

/*
 * Prepare for large transfers.  Do erase the test area.
 */
static int mmc_test_area_prepare_erase(struct mmc_test_card *test)
{
	return mmc_test_area_init(test, 1, 0);
}

/*
 * Prepare for large transfers.  Erase and fill the test area.
 */
static int mmc_test_area_prepare_fill(struct mmc_test_card *test)
{
	return mmc_test_area_init(test, 1, 1);
}

/*
 * Test best-case performance.  Best-case performance is expected from
 * a single large transfer.
 *
 * An additional option (max_scatter) allows the measurement of the same
 * transfer but with no contiguous pages in the scatter list.  This tests
 * the efficiency of DMA to handle scattered pages.
 */
static int mmc_test_best_performance(struct mmc_test_card *test, int write,
				     int max_scatter)
{
	return mmc_test_area_io(test, test->area.max_sz, test->area.dev_addr,
				write, max_scatter, 1);
}

/*
 * Best-case read performance.
 */
static int mmc_test_best_read_performance(struct mmc_test_card *test)
{
	return mmc_test_best_performance(test, 0, 0);
}

/*
 * Best-case write performance.
 */
static int mmc_test_best_write_performance(struct mmc_test_card *test)
{
	return mmc_test_best_performance(test, 1, 0);
}

/*
 * Best-case read performance into scattered pages.
 */
static int mmc_test_best_read_perf_max_scatter(struct mmc_test_card *test)
{
	return mmc_test_best_performance(test, 0, 1);
}

/*
 * Best-case write performance from scattered pages.
 */
static int mmc_test_best_write_perf_max_scatter(struct mmc_test_card *test)
{
	return mmc_test_best_performance(test, 1, 1);
}

/*
 * Single read performance by transfer size.
 */
static int mmc_test_profile_read_perf(struct mmc_test_card *test)
{
	unsigned long sz;
	unsigned int dev_addr;
	int ret;

	for (sz = 512; sz < test->area.max_sz; sz <<= 1) {
		dev_addr = test->area.dev_addr + (sz >> 9);
		ret = mmc_test_area_io(test, sz, dev_addr, 0, 0, 1);
		if (ret)
			return ret;
	}
	dev_addr = test->area.dev_addr;
	return mmc_test_area_io(test, sz, dev_addr, 0, 0, 1);
}

/*
 * Single write performance by transfer size.
 */
static int mmc_test_profile_write_perf(struct mmc_test_card *test)
{
	unsigned long sz;
	unsigned int dev_addr;
	int ret;

	ret = mmc_test_area_erase(test);
	if (ret)
		return ret;
	for (sz = 512; sz < test->area.max_sz; sz <<= 1) {
		dev_addr = test->area.dev_addr + (sz >> 9);
		ret = mmc_test_area_io(test, sz, dev_addr, 1, 0, 1);
		if (ret)
			return ret;
	}
	ret = mmc_test_area_erase(test);
	if (ret)
		return ret;
	dev_addr = test->area.dev_addr;
	return mmc_test_area_io(test, sz, dev_addr, 1, 0, 1);
}

/*
 * Single trim performance by transfer size.
 */
static int mmc_test_profile_trim_perf(struct mmc_test_card *test)
{
	unsigned long sz;
	unsigned int dev_addr;
	struct timespec ts1, ts2;
	int ret;

	if (!mmc_can_trim(test->card))
		return RESULT_UNSUP_CARD;

	if (!mmc_can_erase(test->card))
		return RESULT_UNSUP_HOST;

	for (sz = 512; sz < test->area.max_sz; sz <<= 1) {
		dev_addr = test->area.dev_addr + (sz >> 9);
		getnstimeofday(&ts1);
		ret = mmc_erase(test->card, dev_addr, sz >> 9, MMC_TRIM_ARG);
		if (ret)
			return ret;
		getnstimeofday(&ts2);
		mmc_test_print_rate(test, sz, &ts1, &ts2);
	}
	dev_addr = test->area.dev_addr;
	getnstimeofday(&ts1);
	ret = mmc_erase(test->card, dev_addr, sz >> 9, MMC_TRIM_ARG);
	if (ret)
		return ret;
	getnstimeofday(&ts2);
	mmc_test_print_rate(test, sz, &ts1, &ts2);
	return 0;
}

/*
 * Consecutive read performance by transfer size.
 */
static int mmc_test_profile_seq_read_perf(struct mmc_test_card *test)
{
	unsigned long sz;
	unsigned int dev_addr, i, cnt;
	struct timespec ts1, ts2;
	int ret;

	for (sz = 512; sz <= test->area.max_sz; sz <<= 1) {
		cnt = test->area.max_sz / sz;
		dev_addr = test->area.dev_addr;
		getnstimeofday(&ts1);
		for (i = 0; i < cnt; i++) {
			ret = mmc_test_area_io(test, sz, dev_addr, 0, 0, 0);
			if (ret)
				return ret;
			dev_addr += (sz >> 9);
		}
		getnstimeofday(&ts2);
		mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);
	}
	return 0;
}

/*
 * Consecutive write performance by transfer size.
 */
static int mmc_test_profile_seq_write_perf(struct mmc_test_card *test)
{
	unsigned long sz;
	unsigned int dev_addr, i, cnt;
	struct timespec ts1, ts2;
	int ret;

	for (sz = 512; sz <= test->area.max_sz; sz <<= 1) {
		ret = mmc_test_area_erase(test);
		if (ret)
			return ret;
		cnt = test->area.max_sz / sz;
		dev_addr = test->area.dev_addr;
		getnstimeofday(&ts1);
		for (i = 0; i < cnt; i++) {
			ret = mmc_test_area_io(test, sz, dev_addr, 1, 0, 0);
			if (ret)
				return ret;
			dev_addr += (sz >> 9);
		}
		getnstimeofday(&ts2);
		mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);
	}
	return 0;
}

/*
 * Consecutive trim performance by transfer size.
 */
static int mmc_test_profile_seq_trim_perf(struct mmc_test_card *test)
{
	unsigned long sz;
	unsigned int dev_addr, i, cnt;
	struct timespec ts1, ts2;
	int ret;

	if (!mmc_can_trim(test->card))
		return RESULT_UNSUP_CARD;

	if (!mmc_can_erase(test->card))
		return RESULT_UNSUP_HOST;

	for (sz = 512; sz <= test->area.max_sz; sz <<= 1) {
		ret = mmc_test_area_erase(test);
		if (ret)
			return ret;
		ret = mmc_test_area_fill(test);
		if (ret)
			return ret;
		cnt = test->area.max_sz / sz;
		dev_addr = test->area.dev_addr;
		getnstimeofday(&ts1);
		for (i = 0; i < cnt; i++) {
			ret = mmc_erase(test->card, dev_addr, sz >> 9,
					MMC_TRIM_ARG);
			if (ret)
				return ret;
			dev_addr += (sz >> 9);
		}
		getnstimeofday(&ts2);
		mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);
	}
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

#else

	{
		.name = "Highmem write",
		.run = mmc_test_no_highmem,
	},

	{
		.name = "Highmem read",
		.run = mmc_test_no_highmem,
	},

	{
		.name = "Multi-block highmem write",
		.run = mmc_test_no_highmem,
	},

	{
		.name = "Multi-block highmem read",
		.run = mmc_test_no_highmem,
	},

#endif /* CONFIG_HIGHMEM */

	{
		.name = "Best-case read performance",
		.prepare = mmc_test_area_prepare_fill,
		.run = mmc_test_best_read_performance,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Best-case write performance",
		.prepare = mmc_test_area_prepare_erase,
		.run = mmc_test_best_write_performance,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Best-case read performance into scattered pages",
		.prepare = mmc_test_area_prepare_fill,
		.run = mmc_test_best_read_perf_max_scatter,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Best-case write performance from scattered pages",
		.prepare = mmc_test_area_prepare_erase,
		.run = mmc_test_best_write_perf_max_scatter,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Single read performance by transfer size",
		.prepare = mmc_test_area_prepare_fill,
		.run = mmc_test_profile_read_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Single write performance by transfer size",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_write_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Single trim performance by transfer size",
		.prepare = mmc_test_area_prepare_fill,
		.run = mmc_test_profile_trim_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Consecutive read performance by transfer size",
		.prepare = mmc_test_area_prepare_fill,
		.run = mmc_test_profile_seq_read_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Consecutive write performance by transfer size",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_seq_write_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Consecutive trim performance by transfer size",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_seq_trim_perf,
		.cleanup = mmc_test_area_cleanup,
	},

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
