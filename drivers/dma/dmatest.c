/*
 * DMA Engine test module
 *
 * Copyright (C) 2007 Atmel Corporation
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/freezer.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/wait.h>

static unsigned int test_buf_size = 16384;
module_param(test_buf_size, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(test_buf_size, "Size of the memcpy test buffer");

static char test_channel[20];
module_param_string(channel, test_channel, sizeof(test_channel),
		S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(channel, "Bus ID of the channel to test (default: any)");

static char test_device[32];
module_param_string(device, test_device, sizeof(test_device),
		S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(device, "Bus ID of the DMA Engine to test (default: any)");

static unsigned int threads_per_chan = 1;
module_param(threads_per_chan, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(threads_per_chan,
		"Number of threads to start per channel (default: 1)");

static unsigned int max_channels;
module_param(max_channels, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_channels,
		"Maximum number of channels to use (default: all)");

static unsigned int iterations;
module_param(iterations, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(iterations,
		"Iterations before stopping test (default: infinite)");

static unsigned int dmatest;
module_param(dmatest, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dmatest,
		"dmatest 0-memcpy 1-memset (default: 0)");

static unsigned int xor_sources = 3;
module_param(xor_sources, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(xor_sources,
		"Number of xor source buffers (default: 3)");

static unsigned int pq_sources = 3;
module_param(pq_sources, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pq_sources,
		"Number of p+q source buffers (default: 3)");

static int timeout = 3000;
module_param(timeout, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(timeout, "Transfer Timeout in msec (default: 3000), "
		 "Pass -1 for infinite timeout");

static bool noverify;
module_param(noverify, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(noverify, "Disable random data setup and verification");

static bool verbose;
module_param(verbose, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(verbose, "Enable \"success\" result messages (default: off)");

/**
 * struct dmatest_params - test parameters.
 * @buf_size:		size of the memcpy test buffer
 * @channel:		bus ID of the channel to test
 * @device:		bus ID of the DMA Engine to test
 * @threads_per_chan:	number of threads to start per channel
 * @max_channels:	maximum number of channels to use
 * @iterations:		iterations before stopping test
 * @xor_sources:	number of xor source buffers
 * @pq_sources:		number of p+q source buffers
 * @timeout:		transfer timeout in msec, -1 for infinite timeout
 */
struct dmatest_params {
	unsigned int	buf_size;
	char		channel[20];
	char		device[32];
	unsigned int	threads_per_chan;
	unsigned int	max_channels;
	unsigned int	iterations;
	unsigned int	xor_sources;
	unsigned int	pq_sources;
	int		timeout;
	bool		noverify;
};

/**
 * struct dmatest_info - test information.
 * @params:		test parameters
 * @lock:		access protection to the fields of this structure
 */
static struct dmatest_info {
	/* Test parameters */
	struct dmatest_params	params;

	/* Internal state */
	struct list_head	channels;
	unsigned int		nr_channels;
	struct mutex		lock;
	bool			did_init;
} test_info = {
	.channels = LIST_HEAD_INIT(test_info.channels),
	.lock = __MUTEX_INITIALIZER(test_info.lock),
};

static int dmatest_run_set(const char *val, const struct kernel_param *kp);
static int dmatest_run_get(char *val, const struct kernel_param *kp);
static const struct kernel_param_ops run_ops = {
	.set = dmatest_run_set,
	.get = dmatest_run_get,
};
static bool dmatest_run;
module_param_cb(run, &run_ops, &dmatest_run, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(run, "Run the test (default: false)");

/* Maximum amount of mismatched bytes in buffer to print */
#define MAX_ERROR_COUNT		32

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
#define PATTERN_MEMSET_IDX	0x01

/* poor man's completion - we want to use wait_event_freezable() on it */
struct dmatest_done {
	bool			done;
	wait_queue_head_t	*wait;
};

struct dmatest_thread {
	struct list_head	node;
	struct dmatest_info	*info;
	struct task_struct	*task;
	struct dma_chan		*chan;
	u8			**srcs;
	u8			**usrcs;
	u8			**dsts;
	u8			**udsts;
	enum dma_transaction_type type;
	wait_queue_head_t done_wait;
	struct dmatest_done test_done;
	bool			done;
};

struct dmatest_chan {
	struct list_head	node;
	struct dma_chan		*chan;
	struct list_head	threads;
};

static DECLARE_WAIT_QUEUE_HEAD(thread_wait);
static bool wait;

static bool is_threaded_test_run(struct dmatest_info *info)
{
	struct dmatest_chan *dtc;

	list_for_each_entry(dtc, &info->channels, node) {
		struct dmatest_thread *thread;

		list_for_each_entry(thread, &dtc->threads, node) {
			if (!thread->done)
				return true;
		}
	}

	return false;
}

static int dmatest_wait_get(char *val, const struct kernel_param *kp)
{
	struct dmatest_info *info = &test_info;
	struct dmatest_params *params = &info->params;

	if (params->iterations)
		wait_event(thread_wait, !is_threaded_test_run(info));
	wait = true;
	return param_get_bool(val, kp);
}

static const struct kernel_param_ops wait_ops = {
	.get = dmatest_wait_get,
	.set = param_set_bool,
};
module_param_cb(wait, &wait_ops, &wait, S_IRUGO);
MODULE_PARM_DESC(wait, "Wait for tests to complete (default: false)");

static bool dmatest_match_channel(struct dmatest_params *params,
		struct dma_chan *chan)
{
	if (params->channel[0] == '\0')
		return true;
	return strcmp(dma_chan_name(chan), params->channel) == 0;
}

static bool dmatest_match_device(struct dmatest_params *params,
		struct dma_device *device)
{
	if (params->device[0] == '\0')
		return true;
	return strcmp(dev_name(device->dev), params->device) == 0;
}

static unsigned long dmatest_random(void)
{
	unsigned long buf;

	prandom_bytes(&buf, sizeof(buf));
	return buf;
}

static inline u8 gen_inv_idx(u8 index, bool is_memset)
{
	u8 val = is_memset ? PATTERN_MEMSET_IDX : index;

	return ~val & PATTERN_COUNT_MASK;
}

static inline u8 gen_src_value(u8 index, bool is_memset)
{
	return PATTERN_SRC | gen_inv_idx(index, is_memset);
}

static inline u8 gen_dst_value(u8 index, bool is_memset)
{
	return PATTERN_DST | gen_inv_idx(index, is_memset);
}

static void dmatest_init_srcs(u8 **bufs, unsigned int start, unsigned int len,
		unsigned int buf_size, bool is_memset)
{
	unsigned int i;
	u8 *buf;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = gen_src_value(i, is_memset);
		for ( ; i < start + len; i++)
			buf[i] = gen_src_value(i, is_memset) | PATTERN_COPY;
		for ( ; i < buf_size; i++)
			buf[i] = gen_src_value(i, is_memset);
		buf++;
	}
}

static void dmatest_init_dsts(u8 **bufs, unsigned int start, unsigned int len,
		unsigned int buf_size, bool is_memset)
{
	unsigned int i;
	u8 *buf;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = gen_dst_value(i, is_memset);
		for ( ; i < start + len; i++)
			buf[i] = gen_dst_value(i, is_memset) |
						PATTERN_OVERWRITE;
		for ( ; i < buf_size; i++)
			buf[i] = gen_dst_value(i, is_memset);
	}
}

static void dmatest_mismatch(u8 actual, u8 pattern, unsigned int index,
		unsigned int counter, bool is_srcbuf, bool is_memset)
{
	u8		diff = actual ^ pattern;
	u8		expected = pattern | gen_inv_idx(counter, is_memset);
	const char	*thread_name = current->comm;

	if (is_srcbuf)
		pr_warn("%s: srcbuf[0x%x] overwritten! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
	else if ((pattern & PATTERN_COPY)
			&& (diff & (PATTERN_COPY | PATTERN_OVERWRITE)))
		pr_warn("%s: dstbuf[0x%x] not copied! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
	else if (diff & PATTERN_SRC)
		pr_warn("%s: dstbuf[0x%x] was copied! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
	else
		pr_warn("%s: dstbuf[0x%x] mismatch! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
}

static unsigned int dmatest_verify(u8 **bufs, unsigned int start,
		unsigned int end, unsigned int counter, u8 pattern,
		bool is_srcbuf, bool is_memset)
{
	unsigned int i;
	unsigned int error_count = 0;
	u8 actual;
	u8 expected;
	u8 *buf;
	unsigned int counter_orig = counter;

	for (; (buf = *bufs); bufs++) {
		counter = counter_orig;
		for (i = start; i < end; i++) {
			actual = buf[i];
			expected = pattern | gen_inv_idx(counter, is_memset);
			if (actual != expected) {
				if (error_count < MAX_ERROR_COUNT)
					dmatest_mismatch(actual, pattern, i,
							 counter, is_srcbuf,
							 is_memset);
				error_count++;
			}
			counter++;
		}
	}

	if (error_count > MAX_ERROR_COUNT)
		pr_warn("%s: %u errors suppressed\n",
			current->comm, error_count - MAX_ERROR_COUNT);

	return error_count;
}


static void dmatest_callback(void *arg)
{
	struct dmatest_done *done = arg;
	struct dmatest_thread *thread =
		container_of(arg, struct dmatest_thread, done_wait);
	if (!thread->done) {
		done->done = true;
		wake_up_all(done->wait);
	} else {
		/*
		 * If thread->done, it means that this callback occurred
		 * after the parent thread has cleaned up. This can
		 * happen in the case that driver doesn't implement
		 * the terminate_all() functionality and a dma operation
		 * did not occur within the timeout period
		 */
		WARN(1, "dmatest: Kernel memory may be corrupted!!\n");
	}
}

static unsigned int min_odd(unsigned int x, unsigned int y)
{
	unsigned int val = min(x, y);

	return val % 2 ? val : val - 1;
}

static void result(const char *err, unsigned int n, unsigned int src_off,
		   unsigned int dst_off, unsigned int len, unsigned long data)
{
	pr_info("%s: result #%u: '%s' with src_off=0x%x dst_off=0x%x len=0x%x (%lu)\n",
		current->comm, n, err, src_off, dst_off, len, data);
}

static void dbg_result(const char *err, unsigned int n, unsigned int src_off,
		       unsigned int dst_off, unsigned int len,
		       unsigned long data)
{
	pr_debug("%s: result #%u: '%s' with src_off=0x%x dst_off=0x%x len=0x%x (%lu)\n",
		 current->comm, n, err, src_off, dst_off, len, data);
}

#define verbose_result(err, n, src_off, dst_off, len, data) ({	\
	if (verbose)						\
		result(err, n, src_off, dst_off, len, data);	\
	else							\
		dbg_result(err, n, src_off, dst_off, len, data);\
})

static unsigned long long dmatest_persec(s64 runtime, unsigned int val)
{
	unsigned long long per_sec = 1000000;

	if (runtime <= 0)
		return 0;

	/* drop precision until runtime is 32-bits */
	while (runtime > UINT_MAX) {
		runtime >>= 1;
		per_sec <<= 1;
	}

	per_sec *= val;
	do_div(per_sec, runtime);
	return per_sec;
}

static unsigned long long dmatest_KBs(s64 runtime, unsigned long long len)
{
	return dmatest_persec(runtime, len >> 10);
}

/*
 * This function repeatedly tests DMA transfers of various lengths and
 * offsets for a given operation type until it is told to exit by
 * kthread_stop(). There may be multiple threads running this function
 * in parallel for a single channel, and there may be multiple channels
 * being tested in parallel.
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
	struct dmatest_done	*done = &thread->test_done;
	struct dmatest_info	*info;
	struct dmatest_params	*params;
	struct dma_chan		*chan;
	struct dma_device	*dev;
	unsigned int		error_count;
	unsigned int		failed_tests = 0;
	unsigned int		total_tests = 0;
	dma_cookie_t		cookie;
	enum dma_status		status;
	enum dma_ctrl_flags 	flags;
	u8			*pq_coefs = NULL;
	int			ret;
	int			src_cnt;
	int			dst_cnt;
	int			i;
	ktime_t			ktime, start, diff;
	ktime_t			filltime = 0;
	ktime_t			comparetime = 0;
	s64			runtime = 0;
	unsigned long long	total_len = 0;
	u8			align = 0;
	bool			is_memset = false;

	set_freezable();

	ret = -ENOMEM;

	smp_rmb();
	info = thread->info;
	params = &info->params;
	chan = thread->chan;
	dev = chan->device;
	if (thread->type == DMA_MEMCPY) {
		align = dev->copy_align;
		src_cnt = dst_cnt = 1;
	} else if (thread->type == DMA_MEMSET) {
		align = dev->fill_align;
		src_cnt = dst_cnt = 1;
		is_memset = true;
	} else if (thread->type == DMA_XOR) {
		/* force odd to ensure dst = src */
		src_cnt = min_odd(params->xor_sources | 1, dev->max_xor);
		dst_cnt = 1;
		align = dev->xor_align;
	} else if (thread->type == DMA_PQ) {
		/* force odd to ensure dst = src */
		src_cnt = min_odd(params->pq_sources | 1, dma_maxpq(dev, 0));
		dst_cnt = 2;
		align = dev->pq_align;

		pq_coefs = kmalloc(params->pq_sources + 1, GFP_KERNEL);
		if (!pq_coefs)
			goto err_thread_type;

		for (i = 0; i < src_cnt; i++)
			pq_coefs[i] = 1;
	} else
		goto err_thread_type;

	thread->srcs = kcalloc(src_cnt + 1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->srcs)
		goto err_srcs;

	thread->usrcs = kcalloc(src_cnt + 1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->usrcs)
		goto err_usrcs;

	for (i = 0; i < src_cnt; i++) {
		thread->usrcs[i] = kmalloc(params->buf_size + align,
					   GFP_KERNEL);
		if (!thread->usrcs[i])
			goto err_srcbuf;

		/* align srcs to alignment restriction */
		if (align)
			thread->srcs[i] = PTR_ALIGN(thread->usrcs[i], align);
		else
			thread->srcs[i] = thread->usrcs[i];
	}
	thread->srcs[i] = NULL;

	thread->dsts = kcalloc(dst_cnt + 1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->dsts)
		goto err_dsts;

	thread->udsts = kcalloc(dst_cnt + 1, sizeof(u8 *), GFP_KERNEL);
	if (!thread->udsts)
		goto err_udsts;

	for (i = 0; i < dst_cnt; i++) {
		thread->udsts[i] = kmalloc(params->buf_size + align,
					   GFP_KERNEL);
		if (!thread->udsts[i])
			goto err_dstbuf;

		/* align dsts to alignment restriction */
		if (align)
			thread->dsts[i] = PTR_ALIGN(thread->udsts[i], align);
		else
			thread->dsts[i] = thread->udsts[i];
	}
	thread->dsts[i] = NULL;

	set_user_nice(current, 10);

	/*
	 * src and dst buffers are freed by ourselves below
	 */
	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	ktime = ktime_get();
	while (!kthread_should_stop()
	       && !(params->iterations && total_tests >= params->iterations)) {
		struct dma_async_tx_descriptor *tx = NULL;
		struct dmaengine_unmap_data *um;
		dma_addr_t srcs[src_cnt];
		dma_addr_t *dsts;
		unsigned int src_off, dst_off, len;

		total_tests++;

		/* Check if buffer count fits into map count variable (u8) */
		if ((src_cnt + dst_cnt) >= 255) {
			pr_err("too many buffers (%d of 255 supported)\n",
			       src_cnt + dst_cnt);
			break;
		}

		if (1 << align > params->buf_size) {
			pr_err("%u-byte buffer too small for %d-byte alignment\n",
			       params->buf_size, 1 << align);
			break;
		}

		if (params->noverify)
			len = params->buf_size;
		else
			len = dmatest_random() % params->buf_size + 1;

		len = (len >> align) << align;
		if (!len)
			len = 1 << align;

		total_len += len;

		if (params->noverify) {
			src_off = 0;
			dst_off = 0;
		} else {
			start = ktime_get();
			src_off = dmatest_random() % (params->buf_size - len + 1);
			dst_off = dmatest_random() % (params->buf_size - len + 1);

			src_off = (src_off >> align) << align;
			dst_off = (dst_off >> align) << align;

			dmatest_init_srcs(thread->srcs, src_off, len,
					  params->buf_size, is_memset);
			dmatest_init_dsts(thread->dsts, dst_off, len,
					  params->buf_size, is_memset);

			diff = ktime_sub(ktime_get(), start);
			filltime = ktime_add(filltime, diff);
		}

		um = dmaengine_get_unmap_data(dev->dev, src_cnt + dst_cnt,
					      GFP_KERNEL);
		if (!um) {
			failed_tests++;
			result("unmap data NULL", total_tests,
			       src_off, dst_off, len, ret);
			continue;
		}

		um->len = params->buf_size;
		for (i = 0; i < src_cnt; i++) {
			void *buf = thread->srcs[i];
			struct page *pg = virt_to_page(buf);
			unsigned long pg_off = offset_in_page(buf);

			um->addr[i] = dma_map_page(dev->dev, pg, pg_off,
						   um->len, DMA_TO_DEVICE);
			srcs[i] = um->addr[i] + src_off;
			ret = dma_mapping_error(dev->dev, um->addr[i]);
			if (ret) {
				dmaengine_unmap_put(um);
				result("src mapping error", total_tests,
				       src_off, dst_off, len, ret);
				failed_tests++;
				continue;
			}
			um->to_cnt++;
		}
		/* map with DMA_BIDIRECTIONAL to force writeback/invalidate */
		dsts = &um->addr[src_cnt];
		for (i = 0; i < dst_cnt; i++) {
			void *buf = thread->dsts[i];
			struct page *pg = virt_to_page(buf);
			unsigned long pg_off = offset_in_page(buf);

			dsts[i] = dma_map_page(dev->dev, pg, pg_off, um->len,
					       DMA_BIDIRECTIONAL);
			ret = dma_mapping_error(dev->dev, dsts[i]);
			if (ret) {
				dmaengine_unmap_put(um);
				result("dst mapping error", total_tests,
				       src_off, dst_off, len, ret);
				failed_tests++;
				continue;
			}
			um->bidi_cnt++;
		}

		if (thread->type == DMA_MEMCPY)
			tx = dev->device_prep_dma_memcpy(chan,
							 dsts[0] + dst_off,
							 srcs[0], len, flags);
		else if (thread->type == DMA_MEMSET)
			tx = dev->device_prep_dma_memset(chan,
						dsts[0] + dst_off,
						*(thread->srcs[0] + src_off),
						len, flags);
		else if (thread->type == DMA_XOR)
			tx = dev->device_prep_dma_xor(chan,
						      dsts[0] + dst_off,
						      srcs, src_cnt,
						      len, flags);
		else if (thread->type == DMA_PQ) {
			dma_addr_t dma_pq[dst_cnt];

			for (i = 0; i < dst_cnt; i++)
				dma_pq[i] = dsts[i] + dst_off;
			tx = dev->device_prep_dma_pq(chan, dma_pq, srcs,
						     src_cnt, pq_coefs,
						     len, flags);
		}

		if (!tx) {
			dmaengine_unmap_put(um);
			result("prep error", total_tests, src_off,
			       dst_off, len, ret);
			msleep(100);
			failed_tests++;
			continue;
		}

		done->done = false;
		tx->callback = dmatest_callback;
		tx->callback_param = done;
		cookie = tx->tx_submit(tx);

		if (dma_submit_error(cookie)) {
			dmaengine_unmap_put(um);
			result("submit error", total_tests, src_off,
			       dst_off, len, ret);
			msleep(100);
			failed_tests++;
			continue;
		}
		dma_async_issue_pending(chan);

		wait_event_freezable_timeout(thread->done_wait, done->done,
					     msecs_to_jiffies(params->timeout));

		status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);

		if (!done->done) {
			dmaengine_unmap_put(um);
			result("test timed out", total_tests, src_off, dst_off,
			       len, 0);
			failed_tests++;
			continue;
		} else if (status != DMA_COMPLETE) {
			dmaengine_unmap_put(um);
			result(status == DMA_ERROR ?
			       "completion error status" :
			       "completion busy status", total_tests, src_off,
			       dst_off, len, ret);
			failed_tests++;
			continue;
		}

		dmaengine_unmap_put(um);

		if (params->noverify) {
			verbose_result("test passed", total_tests, src_off,
				       dst_off, len, 0);
			continue;
		}

		start = ktime_get();
		pr_debug("%s: verifying source buffer...\n", current->comm);
		error_count = dmatest_verify(thread->srcs, 0, src_off,
				0, PATTERN_SRC, true, is_memset);
		error_count += dmatest_verify(thread->srcs, src_off,
				src_off + len, src_off,
				PATTERN_SRC | PATTERN_COPY, true, is_memset);
		error_count += dmatest_verify(thread->srcs, src_off + len,
				params->buf_size, src_off + len,
				PATTERN_SRC, true, is_memset);

		pr_debug("%s: verifying dest buffer...\n", current->comm);
		error_count += dmatest_verify(thread->dsts, 0, dst_off,
				0, PATTERN_DST, false, is_memset);

		error_count += dmatest_verify(thread->dsts, dst_off,
				dst_off + len, src_off,
				PATTERN_SRC | PATTERN_COPY, false, is_memset);

		error_count += dmatest_verify(thread->dsts, dst_off + len,
				params->buf_size, dst_off + len,
				PATTERN_DST, false, is_memset);

		diff = ktime_sub(ktime_get(), start);
		comparetime = ktime_add(comparetime, diff);

		if (error_count) {
			result("data error", total_tests, src_off, dst_off,
			       len, error_count);
			failed_tests++;
		} else {
			verbose_result("test passed", total_tests, src_off,
				       dst_off, len, 0);
		}
	}
	ktime = ktime_sub(ktime_get(), ktime);
	ktime = ktime_sub(ktime, comparetime);
	ktime = ktime_sub(ktime, filltime);
	runtime = ktime_to_us(ktime);

	ret = 0;
err_dstbuf:
	for (i = 0; thread->udsts[i]; i++)
		kfree(thread->udsts[i]);
	kfree(thread->udsts);
err_udsts:
	kfree(thread->dsts);
err_dsts:
err_srcbuf:
	for (i = 0; thread->usrcs[i]; i++)
		kfree(thread->usrcs[i]);
	kfree(thread->usrcs);
err_usrcs:
	kfree(thread->srcs);
err_srcs:
	kfree(pq_coefs);
err_thread_type:
	pr_info("%s: summary %u tests, %u failures %llu iops %llu KB/s (%d)\n",
		current->comm, total_tests, failed_tests,
		dmatest_persec(runtime, total_tests),
		dmatest_KBs(runtime, total_len), ret);

	/* terminate all transfers on specified channels */
	if (ret || failed_tests)
		dmaengine_terminate_all(chan);

	thread->done = true;
	wake_up(&thread_wait);

	return ret;
}

static void dmatest_cleanup_channel(struct dmatest_chan *dtc)
{
	struct dmatest_thread	*thread;
	struct dmatest_thread	*_thread;
	int			ret;

	list_for_each_entry_safe(thread, _thread, &dtc->threads, node) {
		ret = kthread_stop(thread->task);
		pr_debug("thread %s exited with status %d\n",
			 thread->task->comm, ret);
		list_del(&thread->node);
		put_task_struct(thread->task);
		kfree(thread);
	}

	/* terminate all transfers on specified channels */
	dmaengine_terminate_all(dtc->chan);

	kfree(dtc);
}

static int dmatest_add_threads(struct dmatest_info *info,
		struct dmatest_chan *dtc, enum dma_transaction_type type)
{
	struct dmatest_params *params = &info->params;
	struct dmatest_thread *thread;
	struct dma_chan *chan = dtc->chan;
	char *op;
	unsigned int i;

	if (type == DMA_MEMCPY)
		op = "copy";
	else if (type == DMA_MEMSET)
		op = "set";
	else if (type == DMA_XOR)
		op = "xor";
	else if (type == DMA_PQ)
		op = "pq";
	else
		return -EINVAL;

	for (i = 0; i < params->threads_per_chan; i++) {
		thread = kzalloc(sizeof(struct dmatest_thread), GFP_KERNEL);
		if (!thread) {
			pr_warn("No memory for %s-%s%u\n",
				dma_chan_name(chan), op, i);
			break;
		}
		thread->info = info;
		thread->chan = dtc->chan;
		thread->type = type;
		thread->test_done.wait = &thread->done_wait;
		init_waitqueue_head(&thread->done_wait);
		smp_wmb();
		thread->task = kthread_create(dmatest_func, thread, "%s-%s%u",
				dma_chan_name(chan), op, i);
		if (IS_ERR(thread->task)) {
			pr_warn("Failed to create thread %s-%s%u\n",
				dma_chan_name(chan), op, i);
			kfree(thread);
			break;
		}

		/* srcbuf and dstbuf are allocated by the thread itself */
		get_task_struct(thread->task);
		list_add_tail(&thread->node, &dtc->threads);
		wake_up_process(thread->task);
	}

	return i;
}

static int dmatest_add_channel(struct dmatest_info *info,
		struct dma_chan *chan)
{
	struct dmatest_chan	*dtc;
	struct dma_device	*dma_dev = chan->device;
	unsigned int		thread_count = 0;
	int cnt;

	dtc = kmalloc(sizeof(struct dmatest_chan), GFP_KERNEL);
	if (!dtc) {
		pr_warn("No memory for %s\n", dma_chan_name(chan));
		return -ENOMEM;
	}

	dtc->chan = chan;
	INIT_LIST_HEAD(&dtc->threads);

	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask)) {
		if (dmatest == 0) {
			cnt = dmatest_add_threads(info, dtc, DMA_MEMCPY);
			thread_count += cnt > 0 ? cnt : 0;
		}
	}

	if (dma_has_cap(DMA_MEMSET, dma_dev->cap_mask)) {
		if (dmatest == 1) {
			cnt = dmatest_add_threads(info, dtc, DMA_MEMSET);
			thread_count += cnt > 0 ? cnt : 0;
		}
	}

	if (dma_has_cap(DMA_XOR, dma_dev->cap_mask)) {
		cnt = dmatest_add_threads(info, dtc, DMA_XOR);
		thread_count += cnt > 0 ? cnt : 0;
	}
	if (dma_has_cap(DMA_PQ, dma_dev->cap_mask)) {
		cnt = dmatest_add_threads(info, dtc, DMA_PQ);
		thread_count += cnt > 0 ? cnt : 0;
	}

	pr_info("Started %u threads using %s\n",
		thread_count, dma_chan_name(chan));

	list_add_tail(&dtc->node, &info->channels);
	info->nr_channels++;

	return 0;
}

static bool filter(struct dma_chan *chan, void *param)
{
	struct dmatest_params *params = param;

	if (!dmatest_match_channel(params, chan) ||
	    !dmatest_match_device(params, chan->device))
		return false;
	else
		return true;
}

static void request_channels(struct dmatest_info *info,
			     enum dma_transaction_type type)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(type, mask);
	for (;;) {
		struct dmatest_params *params = &info->params;
		struct dma_chan *chan;

		chan = dma_request_channel(mask, filter, params);
		if (chan) {
			if (dmatest_add_channel(info, chan)) {
				dma_release_channel(chan);
				break; /* add_channel failed, punt */
			}
		} else
			break; /* no more channels available */
		if (params->max_channels &&
		    info->nr_channels >= params->max_channels)
			break; /* we have all we need */
	}
}

static void run_threaded_test(struct dmatest_info *info)
{
	struct dmatest_params *params = &info->params;

	/* Copy test parameters */
	params->buf_size = test_buf_size;
	strlcpy(params->channel, strim(test_channel), sizeof(params->channel));
	strlcpy(params->device, strim(test_device), sizeof(params->device));
	params->threads_per_chan = threads_per_chan;
	params->max_channels = max_channels;
	params->iterations = iterations;
	params->xor_sources = xor_sources;
	params->pq_sources = pq_sources;
	params->timeout = timeout;
	params->noverify = noverify;

	request_channels(info, DMA_MEMCPY);
	request_channels(info, DMA_MEMSET);
	request_channels(info, DMA_XOR);
	request_channels(info, DMA_PQ);
}

static void stop_threaded_test(struct dmatest_info *info)
{
	struct dmatest_chan *dtc, *_dtc;
	struct dma_chan *chan;

	list_for_each_entry_safe(dtc, _dtc, &info->channels, node) {
		list_del(&dtc->node);
		chan = dtc->chan;
		dmatest_cleanup_channel(dtc);
		pr_debug("dropped channel %s\n", dma_chan_name(chan));
		dma_release_channel(chan);
	}

	info->nr_channels = 0;
}

static void restart_threaded_test(struct dmatest_info *info, bool run)
{
	/* we might be called early to set run=, defer running until all
	 * parameters have been evaluated
	 */
	if (!info->did_init)
		return;

	/* Stop any running test first */
	stop_threaded_test(info);

	/* Run test with new parameters */
	run_threaded_test(info);
}

static int dmatest_run_get(char *val, const struct kernel_param *kp)
{
	struct dmatest_info *info = &test_info;

	mutex_lock(&info->lock);
	if (is_threaded_test_run(info)) {
		dmatest_run = true;
	} else {
		stop_threaded_test(info);
		dmatest_run = false;
	}
	mutex_unlock(&info->lock);

	return param_get_bool(val, kp);
}

static int dmatest_run_set(const char *val, const struct kernel_param *kp)
{
	struct dmatest_info *info = &test_info;
	int ret;

	mutex_lock(&info->lock);
	ret = param_set_bool(val, kp);
	if (ret) {
		mutex_unlock(&info->lock);
		return ret;
	}

	if (is_threaded_test_run(info))
		ret = -EBUSY;
	else if (dmatest_run)
		restart_threaded_test(info, dmatest_run);

	mutex_unlock(&info->lock);

	return ret;
}

static int __init dmatest_init(void)
{
	struct dmatest_info *info = &test_info;
	struct dmatest_params *params = &info->params;

	if (dmatest_run) {
		mutex_lock(&info->lock);
		run_threaded_test(info);
		mutex_unlock(&info->lock);
	}

	if (params->iterations && wait)
		wait_event(thread_wait, !is_threaded_test_run(info));

	/* module parameters are stable, inittime tests are started,
	 * let userspace take over 'run' control
	 */
	info->did_init = true;

	return 0;
}
/* when compiled-in wait for drivers to load first */
late_initcall(dmatest_init);

static void __exit dmatest_exit(void)
{
	struct dmatest_info *info = &test_info;

	mutex_lock(&info->lock);
	stop_threaded_test(info);
	mutex_unlock(&info->lock);
}
module_exit(dmatest_exit);

MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_LICENSE("GPL v2");
