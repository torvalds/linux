/*
 * Copyright (C) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * MemToMem DMA Xfer Test Driver for S3C DMA API
 *
 */
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <mach/dma.h>

#define XFER_UNIT	1024

static unsigned int xfer_size = 256;
module_param(xfer_size, uint, S_IRUGO);
MODULE_PARM_DESC(xfer_size, "Size of each DMA enqueue request in KB");

#define DMATEST_BRSTSZ	1
static unsigned short burst = 1;
module_param(burst, ushort, S_IRUGO);
MODULE_PARM_DESC(burst, "For which parts of API to calc performance");

static unsigned short perf_test = 1;
module_param(perf_test, ushort, S_IRUGO);
MODULE_PARM_DESC(perf_test, "For which parts of API to calc performance");

static unsigned int sec = 60 * 60; /* 1 hour by default */
module_param(sec, uint, S_IRUGO);
MODULE_PARM_DESC(sec, "Number of seconds to run the test (default: 24hr)");

static unsigned int channels = 10000; /* Use all channels by default */
module_param(channels, uint, S_IRUGO);
MODULE_PARM_DESC(channels, "Number of channels to test (default: 8)");

struct s3cdma_thread {
	unsigned id; /* For Channel index */
	struct task_struct *task;
#define SRC 0
#define DST 1
	void *buff_cpu[2]; /* CPU address of the Source & Destination buffer */
	dma_addr_t buff_phys[2]; /* Physical address of the Source & Destination buffer */
	unsigned long jiffies;
	int stopped;
	enum s3c2410_dma_buffresult res;
	int size;
	unsigned done;
	struct s3c2410_dma_client cl;
	struct completion xfer_cmplt;
	struct list_head node;
};

static unsigned int delta;
static unsigned long cycles, maxtime;
static LIST_HEAD(channel_list);

void s3cdma_cb(struct s3c2410_dma_chan *chan, void *buf_id,
			int size, enum s3c2410_dma_buffresult res)
{
	struct s3cdma_thread *thread = buf_id;

	thread->res = res;
	thread->size = size;

	complete(&thread->xfer_cmplt);
}

static void dmatest_init_buf(u32 buf[], int clr, unsigned int bytes)
{
	unsigned int i;

	for (i = 0; i < bytes / 4; i++)
		buf[i] = clr ? 0 : i;
}

static bool dmatest_buf_same(u32 src[], u32 dst[], unsigned int bytes)
{
	unsigned int i;

	for (i = 0; i < (bytes - delta) / 4; i++)
		if (src[i] != dst[i])
			return false;

	for (; i < bytes / 4; i++)
		if (dst[i])
			return false;

	return true;
}

static int dmatest_func(void *data)
{
	struct s3cdma_thread *thread = data;
	enum dma_ch chan = DMACH_MTOM_0 + thread->id;
	unsigned long tout = jiffies + msecs_to_jiffies(sec * 1000);
	int src_idx = 0;
	unsigned val;

	thread->jiffies = jiffies;
	thread->done = 0;

	while (!kthread_should_stop() && time_before(jiffies, tout)) {

		u32 *srcbuf = thread->buff_cpu[src_idx];
		u32 *dstbuf = thread->buff_cpu[1 - src_idx];

		if (!perf_test) {
			dmatest_init_buf(srcbuf, 0, xfer_size);
			dmatest_init_buf(dstbuf, 1, xfer_size);
			delta = 1024;
		}

		s3c2410_dma_devconfig(chan, S3C_DMA_MEM2MEM,
					thread->buff_phys[src_idx]);

		s3c2410_dma_enqueue(chan, (void *)thread,
				thread->buff_phys[1 - src_idx], xfer_size - delta);

		s3c2410_dma_ctrl(chan, S3C2410_DMAOP_START);

		val = wait_for_completion_timeout(&thread->xfer_cmplt, msecs_to_jiffies(5*1000));
		if (!val) {
			dma_addr_t src, dst;
			s3c2410_dma_getposition(DMACH_MTOM_0 + thread->id,
						&src, &dst);

			printk("\n%s:%d Thrd-%u Done-%u <%x,%x>/<%x,%x>\n",
				 __func__, __LINE__, thread->id, thread->done,
				src, dst, thread->buff_phys[src_idx], thread->buff_phys[1 - src_idx]);
			break;
		}

		if (thread->res != S3C2410_RES_OK
				|| thread->size != xfer_size - delta) {
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%u: Cycle-%u Res-%u Xfer_size-%d!\n",
			thread->id, thread->done, thread->res, thread->size);
		} else {
			thread->done++;
		}

		if (!perf_test &&
				!dmatest_buf_same(srcbuf, dstbuf, xfer_size)) {
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%u: Cycle-%u Xfer_cmp failed!\n",
					thread->id, thread->done);
			break;
		}

		src_idx = 1 - src_idx;
	}

	thread->jiffies = jiffies - thread->jiffies;

	thread->stopped = 1;

	return 0;
}

static int __init dmatest_init(void)
{
	struct s3cdma_thread *thread;
	int ret, i = 0;

	xfer_size *= XFER_UNIT;

	if (sec < 5) {
		sec = 5;
		printk(KERN_INFO "S3C DMA M2M Test: Using 5secs test time\n");
	}

	while (i < 10) {
		if (burst == (1 << i))
			break;
		i++;
	}
	/* If invalid burst value provided */
	if (i == 10) {
		burst = 1;
		printk(KERN_INFO "S3C DMA M2M Test: Using 1 burst size\n");
	}

	for (i = 0; i < channels; i++) {
		thread = kzalloc(sizeof(struct s3cdma_thread), GFP_KERNEL);
		if (!thread) {
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%u No memory for channel\n", i);
			goto thrd_alloc_err;
		}

		thread->buff_cpu[SRC] = dma_alloc_coherent(NULL, xfer_size,
						&thread->buff_phys[SRC], GFP_KERNEL);
		if (!thread->buff_cpu[SRC]) {
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%u No memory for src buff\n", i);
			goto src_alloc_err;
		}

		thread->buff_cpu[DST] = dma_alloc_coherent(NULL, xfer_size,
						&thread->buff_phys[DST], GFP_KERNEL);
		if (!thread->buff_cpu[DST]) {
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%u No memory for dst buff\n", i);
			goto dst_alloc_err;
		}

		dmatest_init_buf(thread->buff_cpu[SRC], 0, xfer_size);
		dmatest_init_buf(thread->buff_cpu[DST], 1, xfer_size);

		thread->id = i;
		thread->cl.name = (char *) thread;
		thread->stopped = 0;

		init_completion(&thread->xfer_cmplt);

		ret = s3c2410_dma_request(DMACH_MTOM_0 + thread->id,
						&thread->cl, NULL);
		if (ret) {
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%d acq(%d)\n", i, ret);
			goto thrd_dma_acq_err;
		}

		s3c2410_dma_set_buffdone_fn(DMACH_MTOM_0 + thread->id, s3cdma_cb);

		ret = s3c2410_dma_config(DMACH_MTOM_0 + thread->id, burst);
		if (ret) {
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%d config(%d)\n", i, ret);
			goto thrd_dma_cfg_err;
		}

		thread->task = kthread_run(dmatest_func, thread,
						"dma-m2m-test%u", i);
		if (IS_ERR(thread->task)) {
			printk(KERN_INFO "S3C DMA M2M Test: Failed to run thread dma-m2m-test%u\n", i);
			goto thrd_run_err;
		}

		list_add_tail(&thread->node, &channel_list);

		continue;

thrd_run_err:
thrd_dma_cfg_err:
		s3c2410_dma_free(DMACH_MTOM_0 + thread->id, &thread->cl);
thrd_dma_acq_err:
		dma_free_coherent(NULL, xfer_size,
			thread->buff_cpu[DST], thread->buff_phys[DST]);
dst_alloc_err:
		dma_free_coherent(NULL, xfer_size,
			thread->buff_cpu[SRC], thread->buff_phys[SRC]);
src_alloc_err:
		kfree(thread);
thrd_alloc_err:
		break;
	}

	printk(KERN_INFO "S3C DMA M2M Test: Testing with %u Channels\n", i);

	return 0;
}
module_init(dmatest_init);

static void __exit dmatest_exit(void)
{
	struct s3cdma_thread *thread;

	while (!list_empty(&channel_list)) {
		thread = list_entry(channel_list.next,
				struct s3cdma_thread, node);

		list_del(&thread->node);

		if (perf_test && !dmatest_buf_same(thread->buff_cpu[SRC],
				thread->buff_cpu[DST], xfer_size))
			printk(KERN_INFO "S3C DMA M2M Test: Thrd-%u: Xfer_cmp failed!\n", thread->id);

		if (!thread->stopped)
			kthread_stop(thread->task);

		if (jiffies_to_msecs(thread->jiffies) > maxtime)
			maxtime = jiffies_to_msecs(thread->jiffies);

		cycles += thread->done;

		printk(KERN_INFO "S3C DMA M2M Test: Thrd-%u %ux%u Kb in %ums\n",
			thread->id, thread->done, xfer_size / XFER_UNIT,
			jiffies_to_msecs(thread->jiffies));

		s3c2410_dma_free(DMACH_MTOM_0 + thread->id, &thread->cl);

		dma_free_coherent(NULL, xfer_size,
			thread->buff_cpu[DST], thread->buff_phys[DST]);

		dma_free_coherent(NULL, xfer_size,
			thread->buff_cpu[SRC], thread->buff_phys[SRC]);

		kfree(thread);
	}

	printk(KERN_INFO "S3C DMA M2M Test: Overall %lux%u Kb in %lums\n",
			cycles, xfer_size / XFER_UNIT, maxtime);
	printk(KERN_INFO "S3C DMA M2M Test: %lu MB/Sec\n",
			cycles * 1000 / maxtime * xfer_size / XFER_UNIT / 1024);
}
module_exit(dmatest_exit);

MODULE_AUTHOR("Jaswinder Singh <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("S3C DMA MemToMem Test Driver");
MODULE_LICENSE("GPL");
