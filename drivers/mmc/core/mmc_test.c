// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2007-2008 Pierre Ossman
 */

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/slab.h>

#include <linux/scatterlist.h>
#include <linux/swap.h>		/* For nr_free_buffer_pages() */
#include <linux/list.h>

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/module.h>

#include "core.h"
#include "card.h"
#include "host.h"
#include "bus.h"
#include "mmc_ops.h"

#define RESULT_OK		0
#define RESULT_FAIL		1
#define RESULT_UNSUP_HOST	2
#define RESULT_UNSUP_CARD	3

#define BUFFER_ORDER		2
#define BUFFER_SIZE		(PAGE_SIZE << BUFFER_ORDER)

#define TEST_ALIGN_END		8

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
 * @max_tfr: maximum transfer size allowed by driver (in bytes)
 * @max_segs: maximum segments allowed by driver in scatterlist @sg
 * @max_seg_sz: maximum segment size allowed by driver
 * @blocks: number of (512 byte) blocks currently mapped by @sg
 * @sg_len: length of currently mapped scatterlist @sg
 * @mem: allocated memory
 * @sg: scatterlist
 * @sg_areq: scatterlist for non-blocking request
 */
struct mmc_test_area {
	unsigned long max_sz;
	unsigned int dev_addr;
	unsigned int max_tfr;
	unsigned int max_segs;
	unsigned int max_seg_sz;
	unsigned int blocks;
	unsigned int sg_len;
	struct mmc_test_mem *mem;
	struct scatterlist *sg;
	struct scatterlist *sg_areq;
};

/**
 * struct mmc_test_transfer_result - transfer results for performance tests.
 * @link: double-linked list
 * @count: amount of group of sectors to check
 * @sectors: amount of sectors to check in one group
 * @ts: time values of transfer
 * @rate: calculated transfer rate
 * @iops: I/O operations per second (times 100)
 */
struct mmc_test_transfer_result {
	struct list_head link;
	unsigned int count;
	unsigned int sectors;
	struct timespec64 ts;
	unsigned int rate;
	unsigned int iops;
};

/**
 * struct mmc_test_general_result - results for tests.
 * @link: double-linked list
 * @card: card under test
 * @testcase: number of test case
 * @result: result of test run
 * @tr_lst: transfer measurements if any as mmc_test_transfer_result
 */
struct mmc_test_general_result {
	struct list_head link;
	struct mmc_card *card;
	int testcase;
	int result;
	struct list_head tr_lst;
};

/**
 * struct mmc_test_dbgfs_file - debugfs related file.
 * @link: double-linked list
 * @card: card under test
 * @file: file created under debugfs
 */
struct mmc_test_dbgfs_file {
	struct list_head link;
	struct mmc_card *card;
	struct dentry *file;
};

/**
 * struct mmc_test_card - test information.
 * @card: card under test
 * @scratch: transfer buffer
 * @buffer: transfer buffer
 * @highmem: buffer for highmem tests
 * @area: information for performance tests
 * @gr: pointer to results of current testcase
 */
struct mmc_test_card {
	struct mmc_card	*card;

	u8		scratch[BUFFER_SIZE];
	u8		*buffer;
#ifdef CONFIG_HIGHMEM
	struct page	*highmem;
#endif
	struct mmc_test_area		area;
	struct mmc_test_general_result	*gr;
};

enum mmc_test_prep_media {
	MMC_TEST_PREP_NONE = 0,
	MMC_TEST_PREP_WRITE_FULL = 1 << 0,
	MMC_TEST_PREP_ERASE = 1 << 1,
};

struct mmc_test_multiple_rw {
	unsigned int *sg_len;
	unsigned int *bs;
	unsigned int len;
	unsigned int size;
	bool do_write;
	bool do_nonblock_req;
	enum mmc_test_prep_media prepare;
};

/*******************************************************************/
/*  General helper functions                                       */
/*******************************************************************/

/*
 * Configure correct block size in card
 */
static int mmc_test_set_blksize(struct mmc_test_card *test, unsigned size)
{
	return mmc_set_blocklen(test->card, size);
}

static bool mmc_test_card_cmd23(struct mmc_card *card)
{
	return mmc_card_mmc(card) ||
	       (mmc_card_sd(card) && card->scr.cmds & SD_SCR_CMD23_SUPPORT);
}

static void mmc_test_prepare_sbc(struct mmc_test_card *test,
				 struct mmc_request *mrq, unsigned int blocks)
{
	struct mmc_card *card = test->card;

	if (!mrq->sbc || !mmc_host_cmd23(card->host) ||
	    !mmc_test_card_cmd23(card) || !mmc_op_multi(mrq->cmd->opcode) ||
	    (card->quirks & MMC_QUIRK_BLK_NO_CMD23)) {
		mrq->sbc = NULL;
		return;
	}

	mrq->sbc->opcode = MMC_SET_BLOCK_COUNT;
	mrq->sbc->arg = blocks;
	mrq->sbc->flags = MMC_RSP_R1 | MMC_CMD_AC;
}

/*
 * Fill in the mmc_request structure given a set of transfer parameters.
 */
static void mmc_test_prepare_mrq(struct mmc_test_card *test,
	struct mmc_request *mrq, struct scatterlist *sg, unsigned sg_len,
	unsigned dev_addr, unsigned blocks, unsigned blksz, int write)
{
	if (WARN_ON(!mrq || !mrq->cmd || !mrq->data || !mrq->stop))
		return;

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

	mmc_test_prepare_sbc(test, mrq, blocks);

	mmc_set_data_timeout(mrq->data, test->card);
}

static int mmc_test_busy(struct mmc_command *cmd)
{
	return !(cmd->resp[0] & R1_READY_FOR_DATA) ||
		(R1_CURRENT_STATE(cmd->resp[0]) == R1_STATE_PRG);
}

/*
 * Wait for the card to finish the busy state
 */
static int mmc_test_wait_busy(struct mmc_test_card *test)
{
	int ret, busy;
	struct mmc_command cmd = {};

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
			if (test->card->host->caps & MMC_CAP_WAIT_WHILE_BUSY)
				pr_info("%s: Warning: Host did not wait for busy state to end.\n",
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
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_command stop = {};
	struct mmc_data data = {};

	struct scatterlist sg;

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

	return mmc_test_wait_busy(test);
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
 * Allocate a lot of memory, preferably max_sz but at least min_sz.  In case
 * there isn't much memory do not exceed 1/16th total lowmem pages.  Also do
 * not exceed a maximum number of segments and try not to make segments much
 * bigger than maximum segment size.
 */
static struct mmc_test_mem *mmc_test_alloc_mem(unsigned long min_sz,
					       unsigned long max_sz,
					       unsigned int max_segs,
					       unsigned int max_seg_sz)
{
	unsigned long max_page_cnt = DIV_ROUND_UP(max_sz, PAGE_SIZE);
	unsigned long min_page_cnt = DIV_ROUND_UP(min_sz, PAGE_SIZE);
	unsigned long max_seg_page_cnt = DIV_ROUND_UP(max_seg_sz, PAGE_SIZE);
	unsigned long page_cnt = 0;
	unsigned long limit = nr_free_buffer_pages() >> 4;
	struct mmc_test_mem *mem;

	if (max_page_cnt > limit)
		max_page_cnt = limit;
	if (min_page_cnt > max_page_cnt)
		min_page_cnt = max_page_cnt;

	if (max_seg_page_cnt > max_page_cnt)
		max_seg_page_cnt = max_page_cnt;

	if (max_segs > max_page_cnt)
		max_segs = max_page_cnt;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return NULL;

	mem->arr = kcalloc(max_segs, sizeof(*mem->arr), GFP_KERNEL);
	if (!mem->arr)
		goto out_free;

	while (max_page_cnt) {
		struct page *page;
		unsigned int order;
		gfp_t flags = GFP_KERNEL | GFP_DMA | __GFP_NOWARN |
				__GFP_NORETRY;

		order = get_order(max_seg_page_cnt << PAGE_SHIFT);
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
		if (mem->cnt >= max_segs) {
			if (page_cnt < min_page_cnt)
				goto out_free;
			break;
		}
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
static int mmc_test_map_sg(struct mmc_test_mem *mem, unsigned long size,
			   struct scatterlist *sglist, int repeat,
			   unsigned int max_segs, unsigned int max_seg_sz,
			   unsigned int *sg_len, int min_sg_len)
{
	struct scatterlist *sg = NULL;
	unsigned int i;
	unsigned long sz = size;

	sg_init_table(sglist, max_segs);
	if (min_sg_len > max_segs)
		min_sg_len = max_segs;

	*sg_len = 0;
	do {
		for (i = 0; i < mem->cnt; i++) {
			unsigned long len = PAGE_SIZE << mem->arr[i].order;

			if (min_sg_len && (size / min_sg_len < len))
				len = ALIGN(size / min_sg_len, 512);
			if (len > sz)
				len = sz;
			if (len > max_seg_sz)
				len = max_seg_sz;
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
				       unsigned int max_seg_sz,
				       unsigned int *sg_len)
{
	struct scatterlist *sg = NULL;
	unsigned int i = mem->cnt, cnt;
	unsigned long len;
	void *base, *addr, *last_addr = NULL;

	sg_init_table(sglist, max_segs);

	*sg_len = 0;
	while (sz) {
		base = page_address(mem->arr[--i].page);
		cnt = 1 << mem->arr[i].order;
		while (sz && cnt) {
			addr = base + PAGE_SIZE * --cnt;
			if (last_addr && last_addr + PAGE_SIZE == addr)
				continue;
			last_addr = addr;
			len = PAGE_SIZE;
			if (len > max_seg_sz)
				len = max_seg_sz;
			if (len > sz)
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
		if (i == 0)
			i = mem->cnt;
	}

	if (sg)
		sg_mark_end(sg);

	return 0;
}

/*
 * Calculate transfer rate in bytes per second.
 */
static unsigned int mmc_test_rate(uint64_t bytes, struct timespec64 *ts)
{
	uint64_t ns;

	ns = timespec64_to_ns(ts);
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
 * Save transfer results for future usage
 */
static void mmc_test_save_transfer_result(struct mmc_test_card *test,
	unsigned int count, unsigned int sectors, struct timespec64 ts,
	unsigned int rate, unsigned int iops)
{
	struct mmc_test_transfer_result *tr;

	if (!test->gr)
		return;

	tr = kmalloc(sizeof(*tr), GFP_KERNEL);
	if (!tr)
		return;

	tr->count = count;
	tr->sectors = sectors;
	tr->ts = ts;
	tr->rate = rate;
	tr->iops = iops;

	list_add_tail(&tr->link, &test->gr->tr_lst);
}

/*
 * Print the transfer rate.
 */
static void mmc_test_print_rate(struct mmc_test_card *test, uint64_t bytes,
				struct timespec64 *ts1, struct timespec64 *ts2)
{
	unsigned int rate, iops, sectors = bytes >> 9;
	struct timespec64 ts;

	ts = timespec64_sub(*ts2, *ts1);

	rate = mmc_test_rate(bytes, &ts);
	iops = mmc_test_rate(100, &ts); /* I/O ops per sec x 100 */

	pr_info("%s: Transfer of %u sectors (%u%s KiB) took %llu.%09u "
			 "seconds (%u kB/s, %u KiB/s, %u.%02u IOPS)\n",
			 mmc_hostname(test->card->host), sectors, sectors >> 1,
			 (sectors & 1 ? ".5" : ""), (u64)ts.tv_sec,
			 (u32)ts.tv_nsec, rate / 1000, rate / 1024,
			 iops / 100, iops % 100);

	mmc_test_save_transfer_result(test, 1, sectors, ts, rate, iops);
}

/*
 * Print the average transfer rate.
 */
static void mmc_test_print_avg_rate(struct mmc_test_card *test, uint64_t bytes,
				    unsigned int count, struct timespec64 *ts1,
				    struct timespec64 *ts2)
{
	unsigned int rate, iops, sectors = bytes >> 9;
	uint64_t tot = bytes * count;
	struct timespec64 ts;

	ts = timespec64_sub(*ts2, *ts1);

	rate = mmc_test_rate(tot, &ts);
	iops = mmc_test_rate(count * 100, &ts); /* I/O ops per sec x 100 */

	pr_info("%s: Transfer of %u x %u sectors (%u x %u%s KiB) took "
			 "%llu.%09u seconds (%u kB/s, %u KiB/s, "
			 "%u.%02u IOPS, sg_len %d)\n",
			 mmc_hostname(test->card->host), count, sectors, count,
			 sectors >> 1, (sectors & 1 ? ".5" : ""),
			 (u64)ts.tv_sec, (u32)ts.tv_nsec,
			 rate / 1000, rate / 1024, iops / 100, iops % 100,
			 test->area.sg_len);

	mmc_test_save_transfer_result(test, count, sectors, ts, rate, iops);
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
		for (i = 0; i < 512; i++)
			test->buffer[i] = i;
	}

	for (i = 0; i < BUFFER_SIZE / 512; i++) {
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

	for (i = 0; i < BUFFER_SIZE / 512; i++) {
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
	if (WARN_ON(!mrq || !mrq->cmd || !mrq->data))
		return;

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

	if (WARN_ON(!mrq || !mrq->cmd || !mrq->data))
		return -EINVAL;

	ret = 0;

	if (mrq->sbc && mrq->sbc->error)
		ret = mrq->sbc->error;
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

	if (WARN_ON(!mrq || !mrq->cmd || !mrq->data))
		return -EINVAL;

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

struct mmc_test_req {
	struct mmc_request mrq;
	struct mmc_command sbc;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_command status;
	struct mmc_data data;
};

/*
 * Tests nonblock transfer with certain parameters
 */
static void mmc_test_req_reset(struct mmc_test_req *rq)
{
	memset(rq, 0, sizeof(struct mmc_test_req));

	rq->mrq.cmd = &rq->cmd;
	rq->mrq.data = &rq->data;
	rq->mrq.stop = &rq->stop;
}

static struct mmc_test_req *mmc_test_req_alloc(void)
{
	struct mmc_test_req *rq = kmalloc(sizeof(*rq), GFP_KERNEL);

	if (rq)
		mmc_test_req_reset(rq);

	return rq;
}

static void mmc_test_wait_done(struct mmc_request *mrq)
{
	complete(&mrq->completion);
}

static int mmc_test_start_areq(struct mmc_test_card *test,
			       struct mmc_request *mrq,
			       struct mmc_request *prev_mrq)
{
	struct mmc_host *host = test->card->host;
	int err = 0;

	if (mrq) {
		init_completion(&mrq->completion);
		mrq->done = mmc_test_wait_done;
		mmc_pre_req(host, mrq);
	}

	if (prev_mrq) {
		wait_for_completion(&prev_mrq->completion);
		err = mmc_test_wait_busy(test);
		if (!err)
			err = mmc_test_check_result(test, prev_mrq);
	}

	if (!err && mrq) {
		err = mmc_start_request(host, mrq);
		if (err)
			mmc_retune_release(host);
	}

	if (prev_mrq)
		mmc_post_req(host, prev_mrq, 0);

	if (err && mrq)
		mmc_post_req(host, mrq, err);

	return err;
}

static int mmc_test_nonblock_transfer(struct mmc_test_card *test,
				      unsigned int dev_addr, int write,
				      int count)
{
	struct mmc_test_req *rq1, *rq2;
	struct mmc_request *mrq, *prev_mrq;
	int i;
	int ret = RESULT_OK;
	struct mmc_test_area *t = &test->area;
	struct scatterlist *sg = t->sg;
	struct scatterlist *sg_areq = t->sg_areq;

	rq1 = mmc_test_req_alloc();
	rq2 = mmc_test_req_alloc();
	if (!rq1 || !rq2) {
		ret = RESULT_FAIL;
		goto err;
	}

	mrq = &rq1->mrq;
	prev_mrq = NULL;

	for (i = 0; i < count; i++) {
		mmc_test_req_reset(container_of(mrq, struct mmc_test_req, mrq));
		mmc_test_prepare_mrq(test, mrq, sg, t->sg_len, dev_addr,
				     t->blocks, 512, write);
		ret = mmc_test_start_areq(test, mrq, prev_mrq);
		if (ret)
			goto err;

		if (!prev_mrq)
			prev_mrq = &rq2->mrq;

		swap(mrq, prev_mrq);
		swap(sg, sg_areq);
		dev_addr += t->blocks;
	}

	ret = mmc_test_start_areq(test, NULL, prev_mrq);
err:
	kfree(rq1);
	kfree(rq2);
	return ret;
}

/*
 * Tests a basic transfer with certain parameters
 */
static int mmc_test_simple_transfer(struct mmc_test_card *test,
	struct scatterlist *sg, unsigned sg_len, unsigned dev_addr,
	unsigned blocks, unsigned blksz, int write)
{
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_command stop = {};
	struct mmc_data data = {};

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
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_command stop = {};
	struct mmc_data data = {};

	struct scatterlist sg;

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
		for (i = 0; i < blocks * blksz; i++)
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

		for (i = 0; i < sectors; i++) {
			ret = mmc_test_buffer_transfer(test,
				test->buffer + i * 512,
				dev_addr + i, 512, 0);
			if (ret)
				return ret;
		}

		for (i = 0; i < blocks * blksz; i++) {
			if (test->buffer[i] != (u8)i)
				return RESULT_FAIL;
		}

		for (; i < sectors * 512; i++) {
			if (test->buffer[i] != 0xDF)
				return RESULT_FAIL;
		}
	} else {
		local_irq_save(flags);
		sg_copy_to_buffer(sg, sg_len, test->scratch, BUFFER_SIZE);
		local_irq_restore(flags);
		for (i = 0; i < blocks * blksz; i++) {
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

	return mmc_test_simple_transfer(test, &sg, 1, 0, 1, 512, 1);
}

static int mmc_test_basic_read(struct mmc_test_card *test)
{
	int ret;
	struct scatterlist sg;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	sg_init_one(&sg, test->buffer, 512);

	return mmc_test_simple_transfer(test, &sg, 1, 0, 1, 512, 0);
}

static int mmc_test_verify_write(struct mmc_test_card *test)
{
	struct scatterlist sg;

	sg_init_one(&sg, test->buffer, 512);

	return mmc_test_transfer(test, &sg, 1, 0, 1, 512, 1);
}

static int mmc_test_verify_read(struct mmc_test_card *test)
{
	struct scatterlist sg;

	sg_init_one(&sg, test->buffer, 512);

	return mmc_test_transfer(test, &sg, 1, 0, 1, 512, 0);
}

static int mmc_test_multi_write(struct mmc_test_card *test)
{
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

	return mmc_test_transfer(test, &sg, 1, 0, size / 512, 512, 1);
}

static int mmc_test_multi_read(struct mmc_test_card *test)
{
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

	return mmc_test_transfer(test, &sg, 1, 0, size / 512, 512, 0);
}

static int mmc_test_pow2_write(struct mmc_test_card *test)
{
	int ret, i;
	struct scatterlist sg;

	if (!test->card->csd.write_partial)
		return RESULT_UNSUP_CARD;

	for (i = 1; i < 512; i <<= 1) {
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

	for (i = 1; i < 512; i <<= 1) {
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

	for (i = 3; i < 512; i += 7) {
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

	for (i = 3; i < 512; i += 7) {
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

	for (i = 1; i < TEST_ALIGN_END; i++) {
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

	for (i = 1; i < TEST_ALIGN_END; i++) {
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

	for (i = 1; i < TEST_ALIGN_END; i++) {
		sg_init_one(&sg, test->buffer + i, size);
		ret = mmc_test_transfer(test, &sg, 1, 0, size / 512, 512, 1);
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

	for (i = 1; i < TEST_ALIGN_END; i++) {
		sg_init_one(&sg, test->buffer + i, size);
		ret = mmc_test_transfer(test, &sg, 1, 0, size / 512, 512, 0);
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

	return mmc_test_broken_transfer(test, 1, 512, 1);
}

static int mmc_test_xfersize_read(struct mmc_test_card *test)
{
	int ret;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	return mmc_test_broken_transfer(test, 1, 512, 0);
}

static int mmc_test_multi_xfersize_write(struct mmc_test_card *test)
{
	int ret;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	return mmc_test_broken_transfer(test, 2, 512, 1);
}

static int mmc_test_multi_xfersize_read(struct mmc_test_card *test)
{
	int ret;

	if (test->card->host->max_blk_count == 1)
		return RESULT_UNSUP_HOST;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	return mmc_test_broken_transfer(test, 2, 512, 0);
}

#ifdef CONFIG_HIGHMEM

static int mmc_test_write_high(struct mmc_test_card *test)
{
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, test->highmem, 512, 0);

	return mmc_test_transfer(test, &sg, 1, 0, 1, 512, 1);
}

static int mmc_test_read_high(struct mmc_test_card *test)
{
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, test->highmem, 512, 0);

	return mmc_test_transfer(test, &sg, 1, 0, 1, 512, 0);
}

static int mmc_test_multi_write_high(struct mmc_test_card *test)
{
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

	return mmc_test_transfer(test, &sg, 1, 0, size / 512, 512, 1);
}

static int mmc_test_multi_read_high(struct mmc_test_card *test)
{
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

	return mmc_test_transfer(test, &sg, 1, 0, size / 512, 512, 0);
}

#else

static int mmc_test_no_highmem(struct mmc_test_card *test)
{
	pr_info("%s: Highmem not configured - test skipped\n",
	       mmc_hostname(test->card->host));
	return 0;
}

#endif /* CONFIG_HIGHMEM */

/*
 * Map sz bytes so that it can be transferred.
 */
static int mmc_test_area_map(struct mmc_test_card *test, unsigned long sz,
			     int max_scatter, int min_sg_len, bool nonblock)
{
	struct mmc_test_area *t = &test->area;
	int err;
	unsigned int sg_len = 0;

	t->blocks = sz >> 9;

	if (max_scatter) {
		err = mmc_test_map_sg_max_scatter(t->mem, sz, t->sg,
						  t->max_segs, t->max_seg_sz,
				       &t->sg_len);
	} else {
		err = mmc_test_map_sg(t->mem, sz, t->sg, 1, t->max_segs,
				      t->max_seg_sz, &t->sg_len, min_sg_len);
	}

	if (err || !nonblock)
		goto err;

	if (max_scatter) {
		err = mmc_test_map_sg_max_scatter(t->mem, sz, t->sg_areq,
						  t->max_segs, t->max_seg_sz,
						  &sg_len);
	} else {
		err = mmc_test_map_sg(t->mem, sz, t->sg_areq, 1, t->max_segs,
				      t->max_seg_sz, &sg_len, min_sg_len);
	}
	if (!err && sg_len != t->sg_len)
		err = -EINVAL;

err:
	if (err)
		pr_info("%s: Failed to map sg list\n",
		       mmc_hostname(test->card->host));
	return err;
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
 * Map and transfer bytes for multiple transfers.
 */
static int mmc_test_area_io_seq(struct mmc_test_card *test, unsigned long sz,
				unsigned int dev_addr, int write,
				int max_scatter, int timed, int count,
				bool nonblock, int min_sg_len)
{
	struct timespec64 ts1, ts2;
	int ret = 0;
	int i;

	/*
	 * In the case of a maximally scattered transfer, the maximum transfer
	 * size is further limited by using PAGE_SIZE segments.
	 */
	if (max_scatter) {
		struct mmc_test_area *t = &test->area;
		unsigned long max_tfr;

		if (t->max_seg_sz >= PAGE_SIZE)
			max_tfr = t->max_segs * PAGE_SIZE;
		else
			max_tfr = t->max_segs * t->max_seg_sz;
		if (sz > max_tfr)
			sz = max_tfr;
	}

	ret = mmc_test_area_map(test, sz, max_scatter, min_sg_len, nonblock);
	if (ret)
		return ret;

	if (timed)
		ktime_get_ts64(&ts1);
	if (nonblock)
		ret = mmc_test_nonblock_transfer(test, dev_addr, write, count);
	else
		for (i = 0; i < count && ret == 0; i++) {
			ret = mmc_test_area_transfer(test, dev_addr, write);
			dev_addr += sz >> 9;
		}

	if (ret)
		return ret;

	if (timed)
		ktime_get_ts64(&ts2);

	if (timed)
		mmc_test_print_avg_rate(test, sz, count, &ts1, &ts2);

	return 0;
}

static int mmc_test_area_io(struct mmc_test_card *test, unsigned long sz,
			    unsigned int dev_addr, int write, int max_scatter,
			    int timed)
{
	return mmc_test_area_io_seq(test, sz, dev_addr, write, max_scatter,
				    timed, 1, false, 0);
}

/*
 * Write the test area entirely.
 */
static int mmc_test_area_fill(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;

	return mmc_test_area_io(test, t->max_tfr, t->dev_addr, 1, 0, 0);
}

/*
 * Erase the test area entirely.
 */
static int mmc_test_area_erase(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;

	if (!mmc_can_erase(test->card))
		return 0;

	return mmc_erase(test->card, t->dev_addr, t->max_sz >> 9,
			 MMC_ERASE_ARG);
}

/*
 * Cleanup struct mmc_test_area.
 */
static int mmc_test_area_cleanup(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;

	kfree(t->sg);
	kfree(t->sg_areq);
	mmc_test_free_mem(t->mem);

	return 0;
}

/*
 * Initialize an area for testing large transfers.  The test area is set to the
 * middle of the card because cards may have different characteristics at the
 * front (for FAT file system optimization).  Optionally, the area is erased
 * (if the card supports it) which may improve write performance.  Optionally,
 * the area is filled with data for subsequent read tests.
 */
static int mmc_test_area_init(struct mmc_test_card *test, int erase, int fill)
{
	struct mmc_test_area *t = &test->area;
	unsigned long min_sz = 64 * 1024, sz;
	int ret;

	ret = mmc_test_set_blksize(test, 512);
	if (ret)
		return ret;

	/* Make the test area size about 4MiB */
	sz = (unsigned long)test->card->pref_erase << 9;
	t->max_sz = sz;
	while (t->max_sz < 4 * 1024 * 1024)
		t->max_sz += sz;
	while (t->max_sz > TEST_AREA_MAX_SIZE && t->max_sz > sz)
		t->max_sz -= sz;

	t->max_segs = test->card->host->max_segs;
	t->max_seg_sz = test->card->host->max_seg_size;
	t->max_seg_sz -= t->max_seg_sz % 512;

	t->max_tfr = t->max_sz;
	if (t->max_tfr >> 9 > test->card->host->max_blk_count)
		t->max_tfr = test->card->host->max_blk_count << 9;
	if (t->max_tfr > test->card->host->max_req_size)
		t->max_tfr = test->card->host->max_req_size;
	if (t->max_tfr / t->max_seg_sz > t->max_segs)
		t->max_tfr = t->max_segs * t->max_seg_sz;

	/*
	 * Try to allocate enough memory for a max. sized transfer.  Less is OK
	 * because the same memory can be mapped into the scatterlist more than
	 * once.  Also, take into account the limits imposed on scatterlist
	 * segments by the host driver.
	 */
	t->mem = mmc_test_alloc_mem(min_sz, t->max_tfr, t->max_segs,
				    t->max_seg_sz);
	if (!t->mem)
		return -ENOMEM;

	t->sg = kmalloc_array(t->max_segs, sizeof(*t->sg), GFP_KERNEL);
	if (!t->sg) {
		ret = -ENOMEM;
		goto out_free;
	}

	t->sg_areq = kmalloc_array(t->max_segs, sizeof(*t->sg_areq),
				   GFP_KERNEL);
	if (!t->sg_areq) {
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
	struct mmc_test_area *t = &test->area;

	return mmc_test_area_io(test, t->max_tfr, t->dev_addr, write,
				max_scatter, 1);
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
	struct mmc_test_area *t = &test->area;
	unsigned long sz;
	unsigned int dev_addr;
	int ret;

	for (sz = 512; sz < t->max_tfr; sz <<= 1) {
		dev_addr = t->dev_addr + (sz >> 9);
		ret = mmc_test_area_io(test, sz, dev_addr, 0, 0, 1);
		if (ret)
			return ret;
	}
	sz = t->max_tfr;
	dev_addr = t->dev_addr;
	return mmc_test_area_io(test, sz, dev_addr, 0, 0, 1);
}

/*
 * Single write performance by transfer size.
 */
static int mmc_test_profile_write_perf(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;
	unsigned long sz;
	unsigned int dev_addr;
	int ret;

	ret = mmc_test_area_erase(test);
	if (ret)
		return ret;
	for (sz = 512; sz < t->max_tfr; sz <<= 1) {
		dev_addr = t->dev_addr + (sz >> 9);
		ret = mmc_test_area_io(test, sz, dev_addr, 1, 0, 1);
		if (ret)
			return ret;
	}
	ret = mmc_test_area_erase(test);
	if (ret)
		return ret;
	sz = t->max_tfr;
	dev_addr = t->dev_addr;
	return mmc_test_area_io(test, sz, dev_addr, 1, 0, 1);
}

/*
 * Single trim performance by transfer size.
 */
static int mmc_test_profile_trim_perf(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;
	unsigned long sz;
	unsigned int dev_addr;
	struct timespec64 ts1, ts2;
	int ret;

	if (!mmc_can_trim(test->card))
		return RESULT_UNSUP_CARD;

	if (!mmc_can_erase(test->card))
		return RESULT_UNSUP_HOST;

	for (sz = 512; sz < t->max_sz; sz <<= 1) {
		dev_addr = t->dev_addr + (sz >> 9);
		ktime_get_ts64(&ts1);
		ret = mmc_erase(test->card, dev_addr, sz >> 9, MMC_TRIM_ARG);
		if (ret)
			return ret;
		ktime_get_ts64(&ts2);
		mmc_test_print_rate(test, sz, &ts1, &ts2);
	}
	dev_addr = t->dev_addr;
	ktime_get_ts64(&ts1);
	ret = mmc_erase(test->card, dev_addr, sz >> 9, MMC_TRIM_ARG);
	if (ret)
		return ret;
	ktime_get_ts64(&ts2);
	mmc_test_print_rate(test, sz, &ts1, &ts2);
	return 0;
}

static int mmc_test_seq_read_perf(struct mmc_test_card *test, unsigned long sz)
{
	struct mmc_test_area *t = &test->area;
	unsigned int dev_addr, i, cnt;
	struct timespec64 ts1, ts2;
	int ret;

	cnt = t->max_sz / sz;
	dev_addr = t->dev_addr;
	ktime_get_ts64(&ts1);
	for (i = 0; i < cnt; i++) {
		ret = mmc_test_area_io(test, sz, dev_addr, 0, 0, 0);
		if (ret)
			return ret;
		dev_addr += (sz >> 9);
	}
	ktime_get_ts64(&ts2);
	mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);
	return 0;
}

/*
 * Consecutive read performance by transfer size.
 */
static int mmc_test_profile_seq_read_perf(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;
	unsigned long sz;
	int ret;

	for (sz = 512; sz < t->max_tfr; sz <<= 1) {
		ret = mmc_test_seq_read_perf(test, sz);
		if (ret)
			return ret;
	}
	sz = t->max_tfr;
	return mmc_test_seq_read_perf(test, sz);
}

static int mmc_test_seq_write_perf(struct mmc_test_card *test, unsigned long sz)
{
	struct mmc_test_area *t = &test->area;
	unsigned int dev_addr, i, cnt;
	struct timespec64 ts1, ts2;
	int ret;

	ret = mmc_test_area_erase(test);
	if (ret)
		return ret;
	cnt = t->max_sz / sz;
	dev_addr = t->dev_addr;
	ktime_get_ts64(&ts1);
	for (i = 0; i < cnt; i++) {
		ret = mmc_test_area_io(test, sz, dev_addr, 1, 0, 0);
		if (ret)
			return ret;
		dev_addr += (sz >> 9);
	}
	ktime_get_ts64(&ts2);
	mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);
	return 0;
}

/*
 * Consecutive write performance by transfer size.
 */
static int mmc_test_profile_seq_write_perf(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;
	unsigned long sz;
	int ret;

	for (sz = 512; sz < t->max_tfr; sz <<= 1) {
		ret = mmc_test_seq_write_perf(test, sz);
		if (ret)
			return ret;
	}
	sz = t->max_tfr;
	return mmc_test_seq_write_perf(test, sz);
}

/*
 * Consecutive trim performance by transfer size.
 */
static int mmc_test_profile_seq_trim_perf(struct mmc_test_card *test)
{
	struct mmc_test_area *t = &test->area;
	unsigned long sz;
	unsigned int dev_addr, i, cnt;
	struct timespec64 ts1, ts2;
	int ret;

	if (!mmc_can_trim(test->card))
		return RESULT_UNSUP_CARD;

	if (!mmc_can_erase(test->card))
		return RESULT_UNSUP_HOST;

	for (sz = 512; sz <= t->max_sz; sz <<= 1) {
		ret = mmc_test_area_erase(test);
		if (ret)
			return ret;
		ret = mmc_test_area_fill(test);
		if (ret)
			return ret;
		cnt = t->max_sz / sz;
		dev_addr = t->dev_addr;
		ktime_get_ts64(&ts1);
		for (i = 0; i < cnt; i++) {
			ret = mmc_erase(test->card, dev_addr, sz >> 9,
					MMC_TRIM_ARG);
			if (ret)
				return ret;
			dev_addr += (sz >> 9);
		}
		ktime_get_ts64(&ts2);
		mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);
	}
	return 0;
}

static unsigned int rnd_next = 1;

static unsigned int mmc_test_rnd_num(unsigned int rnd_cnt)
{
	uint64_t r;

	rnd_next = rnd_next * 1103515245 + 12345;
	r = (rnd_next >> 16) & 0x7fff;
	return (r * rnd_cnt) >> 15;
}

static int mmc_test_rnd_perf(struct mmc_test_card *test, int write, int print,
			     unsigned long sz)
{
	unsigned int dev_addr, cnt, rnd_addr, range1, range2, last_ea = 0, ea;
	unsigned int ssz;
	struct timespec64 ts1, ts2, ts;
	int ret;

	ssz = sz >> 9;

	rnd_addr = mmc_test_capacity(test->card) / 4;
	range1 = rnd_addr / test->card->pref_erase;
	range2 = range1 / ssz;

	ktime_get_ts64(&ts1);
	for (cnt = 0; cnt < UINT_MAX; cnt++) {
		ktime_get_ts64(&ts2);
		ts = timespec64_sub(ts2, ts1);
		if (ts.tv_sec >= 10)
			break;
		ea = mmc_test_rnd_num(range1);
		if (ea == last_ea)
			ea -= 1;
		last_ea = ea;
		dev_addr = rnd_addr + test->card->pref_erase * ea +
			   ssz * mmc_test_rnd_num(range2);
		ret = mmc_test_area_io(test, sz, dev_addr, write, 0, 0);
		if (ret)
			return ret;
	}
	if (print)
		mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);
	return 0;
}

static int mmc_test_random_perf(struct mmc_test_card *test, int write)
{
	struct mmc_test_area *t = &test->area;
	unsigned int next;
	unsigned long sz;
	int ret;

	for (sz = 512; sz < t->max_tfr; sz <<= 1) {
		/*
		 * When writing, try to get more consistent results by running
		 * the test twice with exactly the same I/O but outputting the
		 * results only for the 2nd run.
		 */
		if (write) {
			next = rnd_next;
			ret = mmc_test_rnd_perf(test, write, 0, sz);
			if (ret)
				return ret;
			rnd_next = next;
		}
		ret = mmc_test_rnd_perf(test, write, 1, sz);
		if (ret)
			return ret;
	}
	sz = t->max_tfr;
	if (write) {
		next = rnd_next;
		ret = mmc_test_rnd_perf(test, write, 0, sz);
		if (ret)
			return ret;
		rnd_next = next;
	}
	return mmc_test_rnd_perf(test, write, 1, sz);
}

/*
 * Random read performance by transfer size.
 */
static int mmc_test_random_read_perf(struct mmc_test_card *test)
{
	return mmc_test_random_perf(test, 0);
}

/*
 * Random write performance by transfer size.
 */
static int mmc_test_random_write_perf(struct mmc_test_card *test)
{
	return mmc_test_random_perf(test, 1);
}

static int mmc_test_seq_perf(struct mmc_test_card *test, int write,
			     unsigned int tot_sz, int max_scatter)
{
	struct mmc_test_area *t = &test->area;
	unsigned int dev_addr, i, cnt, sz, ssz;
	struct timespec64 ts1, ts2;
	int ret;

	sz = t->max_tfr;

	/*
	 * In the case of a maximally scattered transfer, the maximum transfer
	 * size is further limited by using PAGE_SIZE segments.
	 */
	if (max_scatter) {
		unsigned long max_tfr;

		if (t->max_seg_sz >= PAGE_SIZE)
			max_tfr = t->max_segs * PAGE_SIZE;
		else
			max_tfr = t->max_segs * t->max_seg_sz;
		if (sz > max_tfr)
			sz = max_tfr;
	}

	ssz = sz >> 9;
	dev_addr = mmc_test_capacity(test->card) / 4;
	if (tot_sz > dev_addr << 9)
		tot_sz = dev_addr << 9;
	cnt = tot_sz / sz;
	dev_addr &= 0xffff0000; /* Round to 64MiB boundary */

	ktime_get_ts64(&ts1);
	for (i = 0; i < cnt; i++) {
		ret = mmc_test_area_io(test, sz, dev_addr, write,
				       max_scatter, 0);
		if (ret)
			return ret;
		dev_addr += ssz;
	}
	ktime_get_ts64(&ts2);

	mmc_test_print_avg_rate(test, sz, cnt, &ts1, &ts2);

	return 0;
}

static int mmc_test_large_seq_perf(struct mmc_test_card *test, int write)
{
	int ret, i;

	for (i = 0; i < 10; i++) {
		ret = mmc_test_seq_perf(test, write, 10 * 1024 * 1024, 1);
		if (ret)
			return ret;
	}
	for (i = 0; i < 5; i++) {
		ret = mmc_test_seq_perf(test, write, 100 * 1024 * 1024, 1);
		if (ret)
			return ret;
	}
	for (i = 0; i < 3; i++) {
		ret = mmc_test_seq_perf(test, write, 1000 * 1024 * 1024, 1);
		if (ret)
			return ret;
	}

	return ret;
}

/*
 * Large sequential read performance.
 */
static int mmc_test_large_seq_read_perf(struct mmc_test_card *test)
{
	return mmc_test_large_seq_perf(test, 0);
}

/*
 * Large sequential write performance.
 */
static int mmc_test_large_seq_write_perf(struct mmc_test_card *test)
{
	return mmc_test_large_seq_perf(test, 1);
}

static int mmc_test_rw_multiple(struct mmc_test_card *test,
				struct mmc_test_multiple_rw *tdata,
				unsigned int reqsize, unsigned int size,
				int min_sg_len)
{
	unsigned int dev_addr;
	struct mmc_test_area *t = &test->area;
	int ret = 0;

	/* Set up test area */
	if (size > mmc_test_capacity(test->card) / 2 * 512)
		size = mmc_test_capacity(test->card) / 2 * 512;
	if (reqsize > t->max_tfr)
		reqsize = t->max_tfr;
	dev_addr = mmc_test_capacity(test->card) / 4;
	if ((dev_addr & 0xffff0000))
		dev_addr &= 0xffff0000; /* Round to 64MiB boundary */
	else
		dev_addr &= 0xfffff800; /* Round to 1MiB boundary */
	if (!dev_addr)
		goto err;

	if (reqsize > size)
		return 0;

	/* prepare test area */
	if (mmc_can_erase(test->card) &&
	    tdata->prepare & MMC_TEST_PREP_ERASE) {
		ret = mmc_erase(test->card, dev_addr,
				size / 512, MMC_SECURE_ERASE_ARG);
		if (ret)
			ret = mmc_erase(test->card, dev_addr,
					size / 512, MMC_ERASE_ARG);
		if (ret)
			goto err;
	}

	/* Run test */
	ret = mmc_test_area_io_seq(test, reqsize, dev_addr,
				   tdata->do_write, 0, 1, size / reqsize,
				   tdata->do_nonblock_req, min_sg_len);
	if (ret)
		goto err;

	return ret;
 err:
	pr_info("[%s] error\n", __func__);
	return ret;
}

static int mmc_test_rw_multiple_size(struct mmc_test_card *test,
				     struct mmc_test_multiple_rw *rw)
{
	int ret = 0;
	int i;
	void *pre_req = test->card->host->ops->pre_req;
	void *post_req = test->card->host->ops->post_req;

	if (rw->do_nonblock_req &&
	    ((!pre_req && post_req) || (pre_req && !post_req))) {
		pr_info("error: only one of pre/post is defined\n");
		return -EINVAL;
	}

	for (i = 0 ; i < rw->len && ret == 0; i++) {
		ret = mmc_test_rw_multiple(test, rw, rw->bs[i], rw->size, 0);
		if (ret)
			break;
	}
	return ret;
}

static int mmc_test_rw_multiple_sg_len(struct mmc_test_card *test,
				       struct mmc_test_multiple_rw *rw)
{
	int ret = 0;
	int i;

	for (i = 0 ; i < rw->len && ret == 0; i++) {
		ret = mmc_test_rw_multiple(test, rw, 512 * 1024, rw->size,
					   rw->sg_len[i]);
		if (ret)
			break;
	}
	return ret;
}

/*
 * Multiple blocking write 4k to 4 MB chunks
 */
static int mmc_test_profile_mult_write_blocking_perf(struct mmc_test_card *test)
{
	unsigned int bs[] = {1 << 12, 1 << 13, 1 << 14, 1 << 15, 1 << 16,
			     1 << 17, 1 << 18, 1 << 19, 1 << 20, 1 << 22};
	struct mmc_test_multiple_rw test_data = {
		.bs = bs,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(bs),
		.do_write = true,
		.do_nonblock_req = false,
		.prepare = MMC_TEST_PREP_ERASE,
	};

	return mmc_test_rw_multiple_size(test, &test_data);
};

/*
 * Multiple non-blocking write 4k to 4 MB chunks
 */
static int mmc_test_profile_mult_write_nonblock_perf(struct mmc_test_card *test)
{
	unsigned int bs[] = {1 << 12, 1 << 13, 1 << 14, 1 << 15, 1 << 16,
			     1 << 17, 1 << 18, 1 << 19, 1 << 20, 1 << 22};
	struct mmc_test_multiple_rw test_data = {
		.bs = bs,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(bs),
		.do_write = true,
		.do_nonblock_req = true,
		.prepare = MMC_TEST_PREP_ERASE,
	};

	return mmc_test_rw_multiple_size(test, &test_data);
}

/*
 * Multiple blocking read 4k to 4 MB chunks
 */
static int mmc_test_profile_mult_read_blocking_perf(struct mmc_test_card *test)
{
	unsigned int bs[] = {1 << 12, 1 << 13, 1 << 14, 1 << 15, 1 << 16,
			     1 << 17, 1 << 18, 1 << 19, 1 << 20, 1 << 22};
	struct mmc_test_multiple_rw test_data = {
		.bs = bs,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(bs),
		.do_write = false,
		.do_nonblock_req = false,
		.prepare = MMC_TEST_PREP_NONE,
	};

	return mmc_test_rw_multiple_size(test, &test_data);
}

/*
 * Multiple non-blocking read 4k to 4 MB chunks
 */
static int mmc_test_profile_mult_read_nonblock_perf(struct mmc_test_card *test)
{
	unsigned int bs[] = {1 << 12, 1 << 13, 1 << 14, 1 << 15, 1 << 16,
			     1 << 17, 1 << 18, 1 << 19, 1 << 20, 1 << 22};
	struct mmc_test_multiple_rw test_data = {
		.bs = bs,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(bs),
		.do_write = false,
		.do_nonblock_req = true,
		.prepare = MMC_TEST_PREP_NONE,
	};

	return mmc_test_rw_multiple_size(test, &test_data);
}

/*
 * Multiple blocking write 1 to 512 sg elements
 */
static int mmc_test_profile_sglen_wr_blocking_perf(struct mmc_test_card *test)
{
	unsigned int sg_len[] = {1, 1 << 3, 1 << 4, 1 << 5, 1 << 6,
				 1 << 7, 1 << 8, 1 << 9};
	struct mmc_test_multiple_rw test_data = {
		.sg_len = sg_len,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(sg_len),
		.do_write = true,
		.do_nonblock_req = false,
		.prepare = MMC_TEST_PREP_ERASE,
	};

	return mmc_test_rw_multiple_sg_len(test, &test_data);
};

/*
 * Multiple non-blocking write 1 to 512 sg elements
 */
static int mmc_test_profile_sglen_wr_nonblock_perf(struct mmc_test_card *test)
{
	unsigned int sg_len[] = {1, 1 << 3, 1 << 4, 1 << 5, 1 << 6,
				 1 << 7, 1 << 8, 1 << 9};
	struct mmc_test_multiple_rw test_data = {
		.sg_len = sg_len,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(sg_len),
		.do_write = true,
		.do_nonblock_req = true,
		.prepare = MMC_TEST_PREP_ERASE,
	};

	return mmc_test_rw_multiple_sg_len(test, &test_data);
}

/*
 * Multiple blocking read 1 to 512 sg elements
 */
static int mmc_test_profile_sglen_r_blocking_perf(struct mmc_test_card *test)
{
	unsigned int sg_len[] = {1, 1 << 3, 1 << 4, 1 << 5, 1 << 6,
				 1 << 7, 1 << 8, 1 << 9};
	struct mmc_test_multiple_rw test_data = {
		.sg_len = sg_len,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(sg_len),
		.do_write = false,
		.do_nonblock_req = false,
		.prepare = MMC_TEST_PREP_NONE,
	};

	return mmc_test_rw_multiple_sg_len(test, &test_data);
}

/*
 * Multiple non-blocking read 1 to 512 sg elements
 */
static int mmc_test_profile_sglen_r_nonblock_perf(struct mmc_test_card *test)
{
	unsigned int sg_len[] = {1, 1 << 3, 1 << 4, 1 << 5, 1 << 6,
				 1 << 7, 1 << 8, 1 << 9};
	struct mmc_test_multiple_rw test_data = {
		.sg_len = sg_len,
		.size = TEST_AREA_MAX_SIZE,
		.len = ARRAY_SIZE(sg_len),
		.do_write = false,
		.do_nonblock_req = true,
		.prepare = MMC_TEST_PREP_NONE,
	};

	return mmc_test_rw_multiple_sg_len(test, &test_data);
}

/*
 * eMMC hardware reset.
 */
static int mmc_test_reset(struct mmc_test_card *test)
{
	struct mmc_card *card = test->card;
	struct mmc_host *host = card->host;
	int err;

	err = mmc_hw_reset(host);
	if (!err) {
		/*
		 * Reset will re-enable the card's command queue, but tests
		 * expect it to be disabled.
		 */
		if (card->ext_csd.cmdq_en)
			mmc_cmdq_disable(card);
		return RESULT_OK;
	} else if (err == -EOPNOTSUPP) {
		return RESULT_UNSUP_HOST;
	}

	return RESULT_FAIL;
}

static int mmc_test_send_status(struct mmc_test_card *test,
				struct mmc_command *cmd)
{
	memset(cmd, 0, sizeof(*cmd));

	cmd->opcode = MMC_SEND_STATUS;
	if (!mmc_host_is_spi(test->card->host))
		cmd->arg = test->card->rca << 16;
	cmd->flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;

	return mmc_wait_for_cmd(test->card->host, cmd, 0);
}

static int mmc_test_ongoing_transfer(struct mmc_test_card *test,
				     unsigned int dev_addr, int use_sbc,
				     int repeat_cmd, int write, int use_areq)
{
	struct mmc_test_req *rq = mmc_test_req_alloc();
	struct mmc_host *host = test->card->host;
	struct mmc_test_area *t = &test->area;
	struct mmc_request *mrq;
	unsigned long timeout;
	bool expired = false;
	int ret = 0, cmd_ret;
	u32 status = 0;
	int count = 0;

	if (!rq)
		return -ENOMEM;

	mrq = &rq->mrq;
	if (use_sbc)
		mrq->sbc = &rq->sbc;
	mrq->cap_cmd_during_tfr = true;

	mmc_test_prepare_mrq(test, mrq, t->sg, t->sg_len, dev_addr, t->blocks,
			     512, write);

	if (use_sbc && t->blocks > 1 && !mrq->sbc) {
		ret =  mmc_host_cmd23(host) ?
		       RESULT_UNSUP_CARD :
		       RESULT_UNSUP_HOST;
		goto out_free;
	}

	/* Start ongoing data request */
	if (use_areq) {
		ret = mmc_test_start_areq(test, mrq, NULL);
		if (ret)
			goto out_free;
	} else {
		mmc_wait_for_req(host, mrq);
	}

	timeout = jiffies + msecs_to_jiffies(3000);
	do {
		count += 1;

		/* Send status command while data transfer in progress */
		cmd_ret = mmc_test_send_status(test, &rq->status);
		if (cmd_ret)
			break;

		status = rq->status.resp[0];
		if (status & R1_ERROR) {
			cmd_ret = -EIO;
			break;
		}

		if (mmc_is_req_done(host, mrq))
			break;

		expired = time_after(jiffies, timeout);
		if (expired) {
			pr_info("%s: timeout waiting for Tran state status %#x\n",
				mmc_hostname(host), status);
			cmd_ret = -ETIMEDOUT;
			break;
		}
	} while (repeat_cmd && R1_CURRENT_STATE(status) != R1_STATE_TRAN);

	/* Wait for data request to complete */
	if (use_areq) {
		ret = mmc_test_start_areq(test, NULL, mrq);
	} else {
		mmc_wait_for_req_done(test->card->host, mrq);
	}

	/*
	 * For cap_cmd_during_tfr request, upper layer must send stop if
	 * required.
	 */
	if (mrq->data->stop && (mrq->data->error || !mrq->sbc)) {
		if (ret)
			mmc_wait_for_cmd(host, mrq->data->stop, 0);
		else
			ret = mmc_wait_for_cmd(host, mrq->data->stop, 0);
	}

	if (ret)
		goto out_free;

	if (cmd_ret) {
		pr_info("%s: Send Status failed: status %#x, error %d\n",
			mmc_hostname(test->card->host), status, cmd_ret);
	}

	ret = mmc_test_check_result(test, mrq);
	if (ret)
		goto out_free;

	ret = mmc_test_wait_busy(test);
	if (ret)
		goto out_free;

	if (repeat_cmd && (t->blocks + 1) << 9 > t->max_tfr)
		pr_info("%s: %d commands completed during transfer of %u blocks\n",
			mmc_hostname(test->card->host), count, t->blocks);

	if (cmd_ret)
		ret = cmd_ret;
out_free:
	kfree(rq);

	return ret;
}

static int __mmc_test_cmds_during_tfr(struct mmc_test_card *test,
				      unsigned long sz, int use_sbc, int write,
				      int use_areq)
{
	struct mmc_test_area *t = &test->area;
	int ret;

	if (!(test->card->host->caps & MMC_CAP_CMD_DURING_TFR))
		return RESULT_UNSUP_HOST;

	ret = mmc_test_area_map(test, sz, 0, 0, use_areq);
	if (ret)
		return ret;

	ret = mmc_test_ongoing_transfer(test, t->dev_addr, use_sbc, 0, write,
					use_areq);
	if (ret)
		return ret;

	return mmc_test_ongoing_transfer(test, t->dev_addr, use_sbc, 1, write,
					 use_areq);
}

static int mmc_test_cmds_during_tfr(struct mmc_test_card *test, int use_sbc,
				    int write, int use_areq)
{
	struct mmc_test_area *t = &test->area;
	unsigned long sz;
	int ret;

	for (sz = 512; sz <= t->max_tfr; sz += 512) {
		ret = __mmc_test_cmds_during_tfr(test, sz, use_sbc, write,
						 use_areq);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Commands during read - no Set Block Count (CMD23).
 */
static int mmc_test_cmds_during_read(struct mmc_test_card *test)
{
	return mmc_test_cmds_during_tfr(test, 0, 0, 0);
}

/*
 * Commands during write - no Set Block Count (CMD23).
 */
static int mmc_test_cmds_during_write(struct mmc_test_card *test)
{
	return mmc_test_cmds_during_tfr(test, 0, 1, 0);
}

/*
 * Commands during read - use Set Block Count (CMD23).
 */
static int mmc_test_cmds_during_read_cmd23(struct mmc_test_card *test)
{
	return mmc_test_cmds_during_tfr(test, 1, 0, 0);
}

/*
 * Commands during write - use Set Block Count (CMD23).
 */
static int mmc_test_cmds_during_write_cmd23(struct mmc_test_card *test)
{
	return mmc_test_cmds_during_tfr(test, 1, 1, 0);
}

/*
 * Commands during non-blocking read - use Set Block Count (CMD23).
 */
static int mmc_test_cmds_during_read_cmd23_nonblock(struct mmc_test_card *test)
{
	return mmc_test_cmds_during_tfr(test, 1, 0, 1);
}

/*
 * Commands during non-blocking write - use Set Block Count (CMD23).
 */
static int mmc_test_cmds_during_write_cmd23_nonblock(struct mmc_test_card *test)
{
	return mmc_test_cmds_during_tfr(test, 1, 1, 1);
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

	{
		.name = "Random read performance by transfer size",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_random_read_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Random write performance by transfer size",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_random_write_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Large sequential read into scattered pages",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_large_seq_read_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Large sequential write from scattered pages",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_large_seq_write_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Write performance with blocking req 4k to 4MB",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_mult_write_blocking_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Write performance with non-blocking req 4k to 4MB",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_mult_write_nonblock_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Read performance with blocking req 4k to 4MB",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_mult_read_blocking_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Read performance with non-blocking req 4k to 4MB",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_mult_read_nonblock_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Write performance blocking req 1 to 512 sg elems",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_sglen_wr_blocking_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Write performance non-blocking req 1 to 512 sg elems",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_sglen_wr_nonblock_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Read performance blocking req 1 to 512 sg elems",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_sglen_r_blocking_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Read performance non-blocking req 1 to 512 sg elems",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_profile_sglen_r_nonblock_perf,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Reset test",
		.run = mmc_test_reset,
	},

	{
		.name = "Commands during read - no Set Block Count (CMD23)",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_cmds_during_read,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Commands during write - no Set Block Count (CMD23)",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_cmds_during_write,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Commands during read - use Set Block Count (CMD23)",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_cmds_during_read_cmd23,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Commands during write - use Set Block Count (CMD23)",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_cmds_during_write_cmd23,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Commands during non-blocking read - use Set Block Count (CMD23)",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_cmds_during_read_cmd23_nonblock,
		.cleanup = mmc_test_area_cleanup,
	},

	{
		.name = "Commands during non-blocking write - use Set Block Count (CMD23)",
		.prepare = mmc_test_area_prepare,
		.run = mmc_test_cmds_during_write_cmd23_nonblock,
		.cleanup = mmc_test_area_cleanup,
	},
};

static DEFINE_MUTEX(mmc_test_lock);

static LIST_HEAD(mmc_test_result);

static void mmc_test_run(struct mmc_test_card *test, int testcase)
{
	int i, ret;

	pr_info("%s: Starting tests of card %s...\n",
		mmc_hostname(test->card->host), mmc_card_id(test->card));

	mmc_claim_host(test->card->host);

	for (i = 0; i < ARRAY_SIZE(mmc_test_cases); i++) {
		struct mmc_test_general_result *gr;

		if (testcase && ((i + 1) != testcase))
			continue;

		pr_info("%s: Test case %d. %s...\n",
			mmc_hostname(test->card->host), i + 1,
			mmc_test_cases[i].name);

		if (mmc_test_cases[i].prepare) {
			ret = mmc_test_cases[i].prepare(test);
			if (ret) {
				pr_info("%s: Result: Prepare stage failed! (%d)\n",
					mmc_hostname(test->card->host),
					ret);
				continue;
			}
		}

		gr = kzalloc(sizeof(*gr), GFP_KERNEL);
		if (gr) {
			INIT_LIST_HEAD(&gr->tr_lst);

			/* Assign data what we know already */
			gr->card = test->card;
			gr->testcase = i;

			/* Append container to global one */
			list_add_tail(&gr->link, &mmc_test_result);

			/*
			 * Save the pointer to created container in our private
			 * structure.
			 */
			test->gr = gr;
		}

		ret = mmc_test_cases[i].run(test);
		switch (ret) {
		case RESULT_OK:
			pr_info("%s: Result: OK\n",
				mmc_hostname(test->card->host));
			break;
		case RESULT_FAIL:
			pr_info("%s: Result: FAILED\n",
				mmc_hostname(test->card->host));
			break;
		case RESULT_UNSUP_HOST:
			pr_info("%s: Result: UNSUPPORTED (by host)\n",
				mmc_hostname(test->card->host));
			break;
		case RESULT_UNSUP_CARD:
			pr_info("%s: Result: UNSUPPORTED (by card)\n",
				mmc_hostname(test->card->host));
			break;
		default:
			pr_info("%s: Result: ERROR (%d)\n",
				mmc_hostname(test->card->host), ret);
		}

		/* Save the result */
		if (gr)
			gr->result = ret;

		if (mmc_test_cases[i].cleanup) {
			ret = mmc_test_cases[i].cleanup(test);
			if (ret) {
				pr_info("%s: Warning: Cleanup stage failed! (%d)\n",
					mmc_hostname(test->card->host),
					ret);
			}
		}
	}

	mmc_release_host(test->card->host);

	pr_info("%s: Tests completed.\n",
		mmc_hostname(test->card->host));
}

static void mmc_test_free_result(struct mmc_card *card)
{
	struct mmc_test_general_result *gr, *grs;

	mutex_lock(&mmc_test_lock);

	list_for_each_entry_safe(gr, grs, &mmc_test_result, link) {
		struct mmc_test_transfer_result *tr, *trs;

		if (card && gr->card != card)
			continue;

		list_for_each_entry_safe(tr, trs, &gr->tr_lst, link) {
			list_del(&tr->link);
			kfree(tr);
		}

		list_del(&gr->link);
		kfree(gr);
	}

	mutex_unlock(&mmc_test_lock);
}

static LIST_HEAD(mmc_test_file_test);

static int mtf_test_show(struct seq_file *sf, void *data)
{
	struct mmc_card *card = (struct mmc_card *)sf->private;
	struct mmc_test_general_result *gr;

	mutex_lock(&mmc_test_lock);

	list_for_each_entry(gr, &mmc_test_result, link) {
		struct mmc_test_transfer_result *tr;

		if (gr->card != card)
			continue;

		seq_printf(sf, "Test %d: %d\n", gr->testcase + 1, gr->result);

		list_for_each_entry(tr, &gr->tr_lst, link) {
			seq_printf(sf, "%u %d %llu.%09u %u %u.%02u\n",
				tr->count, tr->sectors,
				(u64)tr->ts.tv_sec, (u32)tr->ts.tv_nsec,
				tr->rate, tr->iops / 100, tr->iops % 100);
		}
	}

	mutex_unlock(&mmc_test_lock);

	return 0;
}

static int mtf_test_open(struct inode *inode, struct file *file)
{
	return single_open(file, mtf_test_show, inode->i_private);
}

static ssize_t mtf_test_write(struct file *file, const char __user *buf,
	size_t count, loff_t *pos)
{
	struct seq_file *sf = (struct seq_file *)file->private_data;
	struct mmc_card *card = (struct mmc_card *)sf->private;
	struct mmc_test_card *test;
	long testcase;
	int ret;

	ret = kstrtol_from_user(buf, count, 10, &testcase);
	if (ret)
		return ret;

	test = kzalloc(sizeof(*test), GFP_KERNEL);
	if (!test)
		return -ENOMEM;

	/*
	 * Remove all test cases associated with given card. Thus we have only
	 * actual data of the last run.
	 */
	mmc_test_free_result(card);

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

static const struct file_operations mmc_test_fops_test = {
	.open		= mtf_test_open,
	.read		= seq_read,
	.write		= mtf_test_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int mtf_testlist_show(struct seq_file *sf, void *data)
{
	int i;

	mutex_lock(&mmc_test_lock);

	seq_puts(sf, "0:\tRun all tests\n");
	for (i = 0; i < ARRAY_SIZE(mmc_test_cases); i++)
		seq_printf(sf, "%d:\t%s\n", i + 1, mmc_test_cases[i].name);

	mutex_unlock(&mmc_test_lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mtf_testlist);

static void mmc_test_free_dbgfs_file(struct mmc_card *card)
{
	struct mmc_test_dbgfs_file *df, *dfs;

	mutex_lock(&mmc_test_lock);

	list_for_each_entry_safe(df, dfs, &mmc_test_file_test, link) {
		if (card && df->card != card)
			continue;
		debugfs_remove(df->file);
		list_del(&df->link);
		kfree(df);
	}

	mutex_unlock(&mmc_test_lock);
}

static int __mmc_test_register_dbgfs_file(struct mmc_card *card,
	const char *name, umode_t mode, const struct file_operations *fops)
{
	struct dentry *file = NULL;
	struct mmc_test_dbgfs_file *df;

	if (card->debugfs_root)
		debugfs_create_file(name, mode, card->debugfs_root, card, fops);

	df = kmalloc(sizeof(*df), GFP_KERNEL);
	if (!df) {
		debugfs_remove(file);
		return -ENOMEM;
	}

	df->card = card;
	df->file = file;

	list_add(&df->link, &mmc_test_file_test);
	return 0;
}

static int mmc_test_register_dbgfs_file(struct mmc_card *card)
{
	int ret;

	mutex_lock(&mmc_test_lock);

	ret = __mmc_test_register_dbgfs_file(card, "test", S_IWUSR | S_IRUGO,
		&mmc_test_fops_test);
	if (ret)
		goto err;

	ret = __mmc_test_register_dbgfs_file(card, "testlist", S_IRUGO,
		&mtf_testlist_fops);
	if (ret)
		goto err;

err:
	mutex_unlock(&mmc_test_lock);

	return ret;
}

static int mmc_test_probe(struct mmc_card *card)
{
	int ret;

	if (!mmc_card_mmc(card) && !mmc_card_sd(card))
		return -ENODEV;

	ret = mmc_test_register_dbgfs_file(card);
	if (ret)
		return ret;

	if (card->ext_csd.cmdq_en) {
		mmc_claim_host(card->host);
		ret = mmc_cmdq_disable(card);
		mmc_release_host(card->host);
		if (ret)
			return ret;
	}

	dev_info(&card->dev, "Card claimed for testing.\n");

	return 0;
}

static void mmc_test_remove(struct mmc_card *card)
{
	if (card->reenable_cmdq) {
		mmc_claim_host(card->host);
		mmc_cmdq_enable(card);
		mmc_release_host(card->host);
	}
	mmc_test_free_result(card);
	mmc_test_free_dbgfs_file(card);
}

static void mmc_test_shutdown(struct mmc_card *card)
{
}

static struct mmc_driver mmc_driver = {
	.drv		= {
		.name	= "mmc_test",
	},
	.probe		= mmc_test_probe,
	.remove		= mmc_test_remove,
	.shutdown	= mmc_test_shutdown,
};

static int __init mmc_test_init(void)
{
	return mmc_register_driver(&mmc_driver);
}

static void __exit mmc_test_exit(void)
{
	/* Clear stalled data if card is still plugged */
	mmc_test_free_result(NULL);
	mmc_test_free_dbgfs_file(NULL);

	mmc_unregister_driver(&mmc_driver);
}

module_init(mmc_test_init);
module_exit(mmc_test_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multimedia Card (MMC) host test driver");
MODULE_AUTHOR("Pierre Ossman");
