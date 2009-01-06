/*
 * DMA Engine test module
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/wait.h>

static unsigned int test_buf_size = 16384;
module_param(test_buf_size, uint, S_IRUGO);
MODULE_PARM_DESC(test_buf_size, "Size of the memcpy test buffer");

static char test_channel[20];
module_param_string(channel, test_channel, sizeof(test_channel), S_IRUGO);
MODULE_PARM_DESC(channel, "Bus ID of the channel to test (default: any)");

static char test_device[20];
module_param_string(device, test_device, sizeof(test_device), S_IRUGO);
MODULE_PARM_DESC(device, "Bus ID of the DMA Engine to test (default: any)");

static unsigned int threads_per_chan = 1;
module_param(threads_per_chan, uint, S_IRUGO);
MODULE_PARM_DESC(threads_per_chan,
		"Number of threads to start per channel (default: 1)");

static unsigned int max_channels;
module_param(max_channels, uint, S_IRUGO);
MODULE_PARM_DESC(max_channels,
		"Maximum number of channels to use (default: all)");

/*
 * Initialization patterns. All bytes in the source buffer has bit 7
 * set, all bytes in the destination buffer has bit 7 cleared.
 *
 * Bit 6 is set for all bytes which are to be copied by the DMA
 * engine. Bit 5 is set for all bytes which are to be overwritten by
 * the DMA engine.
 *
 * The remaining bits are the inverse of a counter which increments by
 * one for each byte address.
 */
#define PATTERN_SRC		0x80
#define PATTERN_DST		0x00
#define PATTERN_COPY		0x40
#define PATTERN_OVERWRITE	0x20
#define PATTERN_COUNT_MASK	0x1f

struct dmatest_thread {
	struct list_head	node;
	struct task_struct	*task;
	struct dma_chan		*chan;
	u8			*srcbuf;
	u8			*dstbuf;
};

struct dmatest_chan {
	struct list_head	node;
	struct dma_chan		*chan;
	struct list_head	threads;
};

/*
 * These are protected by dma_list_mutex since they're only used by
 * the DMA filter function callback
 */
static LIST_HEAD(dmatest_channels);
static unsigned int nr_channels;

static bool dmatest_match_channel(struct dma_chan *chan)
{
	if (test_channel[0] == '\0')
		return true;
	return strcmp(dma_chan_name(chan), test_channel) == 0;
}

static bool dmatest_match_device(struct dma_device *device)
{
	if (test_device[0] == '\0')
		return true;
	return strcmp(dev_name(device->dev), test_device) == 0;
}

static unsigned long dmatest_random(void)
{
	unsigned long buf;

	get_random_bytes(&buf, sizeof(buf));
	return buf;
}

static void dmatest_init_srcbuf(u8 *buf, unsigned int start, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < start; i++)
		buf[i] = PATTERN_SRC | (~i & PATTERN_COUNT_MASK);
	for ( ; i < start + len; i++)
		buf[i] = PATTERN_SRC | PATTERN_COPY
			| (~i & PATTERN_COUNT_MASK);;
	for ( ; i < test_buf_size; i++)
		buf[i] = PATTERN_SRC | (~i & PATTERN_COUNT_MASK);
}

static void dmatest_init_dstbuf(u8 *buf, unsigned int start, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < start; i++)
		buf[i] = PATTERN_DST | (~i & PATTERN_COUNT_MASK);
	for ( ; i < start + len; i++)
		buf[i] = PATTERN_DST | PATTERN_OVERWRITE
			| (~i & PATTERN_COUNT_MASK);
	for ( ; i < test_buf_size; i++)
		buf[i] = PATTERN_DST | (~i & PATTERN_COUNT_MASK);
}

static void dmatest_mismatch(u8 actual, u8 pattern, unsigned int index,
		unsigned int counter, bool is_srcbuf)
{
	u8		diff = actual ^ pattern;
	u8		expected = pattern | (~counter & PATTERN_COUNT_MASK);
	const char	*thread_name = current->comm;

	if (is_srcbuf)
		pr_warning("%s: srcbuf[0x%x] overwritten!"
				" Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else if ((pattern & PATTERN_COPY)
			&& (diff & (PATTERN_COPY | PATTERN_OVERWRITE)))
		pr_warning("%s: dstbuf[0x%x] not copied!"
				" Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else if (diff & PATTERN_SRC)
		pr_warning("%s: dstbuf[0x%x] was copied!"
				" Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
	else
		pr_warning("%s: dstbuf[0x%x] mismatch!"
				" Expected %02x, got %02x\n",
				thread_name, index, expected, actual);
}

static unsigned int dmatest_verify(u8 *buf, unsigned int start,
		unsigned int end, unsigned int counter, u8 pattern,
		bool is_srcbuf)
{
	unsigned int i;
	unsigned int error_count = 0;
	u8 actual;

	for (i = start; i < end; i++) {
		actual = buf[i];
		if (actual != (pattern | (~counter & PATTERN_COUNT_MASK))) {
			if (error_count < 32)
				dmatest_mismatch(actual, pattern, i, counter,
						is_srcbuf);
			error_count++;
		}
		counter++;
	}

	if (error_count > 32)
		pr_warning("%s: %u errors suppressed\n",
			current->comm, error_count - 32);

	return error_count;
}

/*
 * This function repeatedly tests DMA transfers of various lengths and
 * offsets until it is told to exit by kthread_stop(). There may be
 * multiple threads running this function in parallel for a single
 * channel, and there may be multiple channels being tested in
 * parallel.
 *
 * Before each test, the source and destination buffer is initialized
 * with a known pattern. This pattern is different depending on
 * whether it's in an area which is supposed to be copied or
 * overwritten, and different in the source and destination buffers.
 * So if the DMA engine doesn't copy exactly what we tell it to copy,
 * we'll notice.
 */
static int dmatest_func(void *data)
{
	struct dmatest_thread	*thread = data;
	struct dma_chan		*chan;
	const char		*thread_name;
	unsigned int		src_off, dst_off, len;
	unsigned int		error_count;
	unsigned int		failed_tests = 0;
	unsigned int		total_tests = 0;
	dma_cookie_t		cookie;
	enum dma_status		status;
	int			ret;

	thread_name = current->comm;

	ret = -ENOMEM;
	thread->srcbuf = kmalloc(test_buf_size, GFP_KERNEL);
	if (!thread->srcbuf)
		goto err_srcbuf;
	thread->dstbuf = kmalloc(test_buf_size, GFP_KERNEL);
	if (!thread->dstbuf)
		goto err_dstbuf;

	smp_rmb();
	chan = thread->chan;

	while (!kthread_should_stop()) {
		total_tests++;

		len = dmatest_random() % test_buf_size + 1;
		src_off = dmatest_random() % (test_buf_size - len + 1);
		dst_off = dmatest_random() % (test_buf_size - len + 1);

		dmatest_init_srcbuf(thread->srcbuf, src_off, len);
		dmatest_init_dstbuf(thread->dstbuf, dst_off, len);

		cookie = dma_async_memcpy_buf_to_buf(chan,
				thread->dstbuf + dst_off,
				thread->srcbuf + src_off,
				len);
		if (dma_submit_error(cookie)) {
			pr_warning("%s: #%u: submit error %d with src_off=0x%x "
					"dst_off=0x%x len=0x%x\n",
					thread_name, total_tests - 1, cookie,
					src_off, dst_off, len);
			msleep(100);
			failed_tests++;
			continue;
		}
		dma_async_memcpy_issue_pending(chan);

		do {
			msleep(1);
			status = dma_async_memcpy_complete(
					chan, cookie, NULL, NULL);
		} while (status == DMA_IN_PROGRESS);

		if (status == DMA_ERROR) {
			pr_warning("%s: #%u: error during copy\n",
					thread_name, total_tests - 1);
			failed_tests++;
			continue;
		}

		error_count = 0;

		pr_debug("%s: verifying source buffer...\n", thread_name);
		error_count += dmatest_verify(thread->srcbuf, 0, src_off,
				0, PATTERN_SRC, true);
		error_count += dmatest_verify(thread->srcbuf, src_off,
				src_off + len, src_off,
				PATTERN_SRC | PATTERN_COPY, true);
		error_count += dmatest_verify(thread->srcbuf, src_off + len,
				test_buf_size, src_off + len,
				PATTERN_SRC, true);

		pr_debug("%s: verifying dest buffer...\n",
				thread->task->comm);
		error_count += dmatest_verify(thread->dstbuf, 0, dst_off,
				0, PATTERN_DST, false);
		error_count += dmatest_verify(thread->dstbuf, dst_off,
				dst_off + len, src_off,
				PATTERN_SRC | PATTERN_COPY, false);
		error_count += dmatest_verify(thread->dstbuf, dst_off + len,
				test_buf_size, dst_off + len,
				PATTERN_DST, false);

		if (error_count) {
			pr_warning("%s: #%u: %u errors with "
				"src_off=0x%x dst_off=0x%x len=0x%x\n",
				thread_name, total_tests - 1, error_count,
				src_off, dst_off, len);
			failed_tests++;
		} else {
			pr_debug("%s: #%u: No errors with "
				"src_off=0x%x dst_off=0x%x len=0x%x\n",
				thread_name, total_tests - 1,
				src_off, dst_off, len);
		}
	}

	ret = 0;
	kfree(thread->dstbuf);
err_dstbuf:
	kfree(thread->srcbuf);
err_srcbuf:
	pr_notice("%s: terminating after %u tests, %u failures (status %d)\n",
			thread_name, total_tests, failed_tests, ret);
	return ret;
}

static void dmatest_cleanup_channel(struct dmatest_chan *dtc)
{
	struct dmatest_thread	*thread;
	struct dmatest_thread	*_thread;
	int			ret;

	list_for_each_entry_safe(thread, _thread, &dtc->threads, node) {
		ret = kthread_stop(thread->task);
		pr_debug("dmatest: thread %s exited with status %d\n",
				thread->task->comm, ret);
		list_del(&thread->node);
		kfree(thread);
	}
	kfree(dtc);
}

static int dmatest_add_channel(struct dma_chan *chan)
{
	struct dmatest_chan	*dtc;
	struct dmatest_thread	*thread;
	unsigned int		i;

	dtc = kmalloc(sizeof(struct dmatest_chan), GFP_KERNEL);
	if (!dtc) {
		pr_warning("dmatest: No memory for %s\n", dma_chan_name(chan));
		return -ENOMEM;
	}

	dtc->chan = chan;
	INIT_LIST_HEAD(&dtc->threads);

	for (i = 0; i < threads_per_chan; i++) {
		thread = kzalloc(sizeof(struct dmatest_thread), GFP_KERNEL);
		if (!thread) {
			pr_warning("dmatest: No memory for %s-test%u\n",
				   dma_chan_name(chan), i);
			break;
		}
		thread->chan = dtc->chan;
		smp_wmb();
		thread->task = kthread_run(dmatest_func, thread, "%s-test%u",
				dma_chan_name(chan), i);
		if (IS_ERR(thread->task)) {
			pr_warning("dmatest: Failed to run thread %s-test%u\n",
					dma_chan_name(chan), i);
			kfree(thread);
			break;
		}

		/* srcbuf and dstbuf are allocated by the thread itself */

		list_add_tail(&thread->node, &dtc->threads);
	}

	pr_info("dmatest: Started %u threads using %s\n", i, dma_chan_name(chan));

	list_add_tail(&dtc->node, &dmatest_channels);
	nr_channels++;

	return 0;
}

static bool filter(struct dma_chan *chan, void *param)
{
	if (!dmatest_match_channel(chan) || !dmatest_match_device(chan->device))
		return false;
	else
		return true;
}

static int __init dmatest_init(void)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	int err = 0;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	for (;;) {
		chan = dma_request_channel(mask, filter, NULL);
		if (chan) {
			err = dmatest_add_channel(chan);
			if (err == 0)
				continue;
			else {
				dma_release_channel(chan);
				break; /* add_channel failed, punt */
			}
		} else
			break; /* no more channels available */
		if (max_channels && nr_channels >= max_channels)
			break; /* we have all we need */
	}

	return err;
}
/* when compiled-in wait for drivers to load first */
late_initcall(dmatest_init);

static void __exit dmatest_exit(void)
{
	struct dmatest_chan *dtc, *_dtc;

	list_for_each_entry_safe(dtc, _dtc, &dmatest_channels, node) {
		list_del(&dtc->node);
		dmatest_cleanup_channel(dtc);
		pr_debug("dmatest: dropped channel %s\n",
			 dma_chan_name(dtc->chan));
		dma_release_channel(dtc->chan);
	}
}
module_exit(dmatest_exit);

MODULE_AUTHOR("Haavard Skinnemoen <hskinnemoen@atmel.com>");
MODULE_LICENSE("GPL v2");
