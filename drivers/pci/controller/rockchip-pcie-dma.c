// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/reset.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <uapi/linux/rk-pcie-dma.h>

#include "rockchip-pcie-dma.h"

/* dma transfer */
/*
 * Write buffer format
 * 0	     4               8	       0xc	0x10	SZ_1M
 * ------------------------------------------------------
 * |0x12345678|local idx(0-7)|data size|reserve	|data	|
 * ------------------------------------------------------
 *
 * Byte 3-0: Receiver check if a valid data package arrived
 * Byte 7-4: As a index for data rcv ack buffer
 * Byte 11-8: Actual data size
 *
 * Data rcv ack buffer format
 * 0		4B
 * --------------
 * |0xdeadbeef	|
 * --------------
 *
 * Data free ack buffer format
 * 0		4B
 * --------------
 * |0xcafebabe	|
 * --------------
 *
 *	RC		EP
 * -	---------	---------
 * |	|  1MB	|	|	|
 * |	|------	|	|	|
 * |	|	|	|	|
 * |	|	|	|	|
 * 8MB	|wr buf	|  ->	|rd buf	|
 * |	|	|	|	|
 * |	|	|	|	|
 * |	|	|	|	|
 * -	---------	---------
 * |	|  1MB	|	|	|
 * |	|------	|	|	|
 * |	|	|	|	|
 * |	|	|	|	|
 * 8MB	|rd buf	|  <-	|wr buf	|
 * |	|	|	|	|
 * |	|	|	|	|
 * |	|	|	|	|
 * -	---------	---------
 * |	|  4B	|	|	|
 * |	|------	|	|	|
 * |	|	|	|	|
 * 32B	|	|	|	|
 * |	|scan	|  <-	|data	|
 * |	|	|	|rcv	|
 * |	|	|	|ack	|
 * |	|	|	|send	|
 * -	---------	---------
 * |	|  4B	|	|	|
 * |	|------	|	|	|
 * |	|	|	|	|
 * 32B	|data	|  ->	|scan	|
 * |	|rcv	|	|	|
 * |	|ack	|	|	|
 * |	|send	|	|	|
 * |	|	|	|	|
 * -	---------	---------
 * |	|  4B	|	|	|
 * |	|------	|	|	|
 * |	|	|	|	|
 * 32B	|	|	|	|
 * |	|scan	|  <-	|data	|
 * |	|	|	|free	|
 * |	|	|	|ack	|
 * |	|	|	|send	|
 * -	---------	---------
 * |	|4B	|	|	|
 * |	|------	|	|	|
 * |	|	|	|	|
 * 32B	|data	|  ->	|scan	|
 * |	|free	|	|	|
 * |	|ack	|	|	|
 * |	|send	|	|	|
 * |	|	|	|	|
 * -	---------	---------
 */

#define NODE_SIZE		(sizeof(unsigned int))
#define PCIE_DMA_ACK_BLOCK_SIZE		(NODE_SIZE * 8)

#define PCIE_DMA_BUF_CNT		8

#define PCIE_DMA_DATA_CHECK		0x12345678
#define PCIE_DMA_DATA_ACK_CHECK		0xdeadbeef
#define PCIE_DMA_DATA_FREE_ACK_CHECK	0xcafebabe

#define PCIE_DMA_PARAM_SIZE		64
#define PCIE_DMA_CHN0			0x0

enum transfer_type {
	PCIE_DMA_DATA_SND,
	PCIE_DMA_DATA_RCV_ACK,
	PCIE_DMA_DATA_FREE_ACK,
	PCIE_DMA_READ_REMOTE,
};

static int enable_check_sum;
struct pcie_misc_dev {
	struct miscdevice dev;
	struct dma_trx_obj *obj;
};
static void *rk_pcie_map_kernel(phys_addr_t start, size_t len);
static void rk_pcie_unmap_kernel(void *vaddr);

static inline bool is_rc(struct dma_trx_obj *obj)
{
	return (obj->busno == 0);
}

static unsigned int rk_pcie_check_sum(unsigned int *src, int size)
{
	unsigned int result = 0;

	size /= sizeof(*src);

	while (size-- > 0)
		result ^= *src++;

	return result;
}

static int rk_pcie_handle_dma_interrupt(struct dma_trx_obj *obj, u32 chn, enum dma_dir dir)
{
	struct dma_table *cur;

	cur = obj->cur;
	if (!cur) {
		pr_err("no pcie dma table\n");
		return 0;
	}

	obj->dma_free = true;
	obj->irq_num++;

	if (cur->dir == DMA_TO_BUS) {
		if (list_empty(&obj->tbl_list)) {
			if (obj->dma_free &&
			    obj->loop_count >= obj->loop_count_threshold)
				complete(&obj->done);
		}
	}

	return 0;
}

static void rk_pcie_prepare_dma(struct dma_trx_obj *obj,
			unsigned int idx, unsigned int bus_idx,
			unsigned int local_idx, size_t buf_size,
			enum transfer_type type, int chn)
{
	struct device *dev = obj->dev;
	phys_addr_t local, bus;
	void *virt;
	unsigned long flags;
	struct dma_table *table = NULL;
	unsigned int checksum;

	switch (type) {
	case PCIE_DMA_DATA_SND:
		table = obj->table[PCIE_DMA_DATA_SND_TABLE_OFFSET + local_idx];
		table->type = PCIE_DMA_DATA_SND;
		table->dir = DMA_TO_BUS;
		local = obj->local_mem_start + local_idx * obj->buffer_size;
		bus = obj->remote_mem_start + bus_idx * obj->buffer_size;
		virt = obj->local_mem_base + local_idx * obj->buffer_size;

		if (obj->addr_reverse) {
			if (is_rc(obj)) {
				local += obj->rd_buf_size;
				virt += obj->rd_buf_size;
				bus += obj->wr_buf_size;
			}
		} else {
			if (!is_rc(obj)) {
				local += obj->rd_buf_size;
				virt += obj->rd_buf_size;
				bus += obj->wr_buf_size;
			}
		}

		obj->begin = ktime_get();
		dma_sync_single_for_device(dev, local, buf_size, DMA_TO_DEVICE);
		obj->end = ktime_get();

		obj->cache_time_total += ktime_to_ns(ktime_sub(obj->end, obj->begin));

		writel(PCIE_DMA_DATA_CHECK, virt + obj->set_data_check_pos);
		writel(local_idx, virt + obj->set_local_idx_pos);
		writel(buf_size, virt + obj->set_buf_size_pos);

		if (enable_check_sum) {
			checksum = rk_pcie_check_sum(virt, SZ_1M - 0x10);
			writel(checksum, virt + obj->set_chk_sum_pos);
		}

		buf_size = obj->buffer_size;
		break;
	case PCIE_DMA_DATA_RCV_ACK:
		table = obj->table[PCIE_DMA_DATA_RCV_ACK_TABLE_OFFSET + idx];
		table->type = PCIE_DMA_DATA_RCV_ACK;
		table->dir = DMA_TO_BUS;
		local = obj->local_mem_start + obj->ack_base + idx * NODE_SIZE;
		virt = obj->local_mem_base + obj->ack_base + idx * NODE_SIZE;
		bus = obj->remote_mem_start + obj->ack_base + idx * NODE_SIZE;

		if (is_rc(obj)) {
			local += PCIE_DMA_ACK_BLOCK_SIZE;
			bus += PCIE_DMA_ACK_BLOCK_SIZE;
			virt += PCIE_DMA_ACK_BLOCK_SIZE;
		}
		writel(PCIE_DMA_DATA_ACK_CHECK, virt);
		break;
	case PCIE_DMA_DATA_FREE_ACK:
		table = obj->table[PCIE_DMA_DATA_FREE_ACK_TABLE_OFFSET + idx];
		table->type = PCIE_DMA_DATA_FREE_ACK;
		table->dir = DMA_TO_BUS;
		local = obj->local_mem_start + obj->ack_base + idx * NODE_SIZE;
		bus = obj->remote_mem_start + obj->ack_base + idx * NODE_SIZE;
		virt = obj->local_mem_base + obj->ack_base + idx * NODE_SIZE;
		buf_size = 4;

		if (is_rc(obj)) {
			local += 3 * PCIE_DMA_ACK_BLOCK_SIZE;
			bus += 3 * PCIE_DMA_ACK_BLOCK_SIZE;
			virt += 3 * PCIE_DMA_ACK_BLOCK_SIZE;
		} else {
			local += 2 * PCIE_DMA_ACK_BLOCK_SIZE;
			bus += 2 * PCIE_DMA_ACK_BLOCK_SIZE;
			virt += 2 * PCIE_DMA_ACK_BLOCK_SIZE;
		}
		writel(PCIE_DMA_DATA_FREE_ACK_CHECK, virt);
		break;
	case PCIE_DMA_READ_REMOTE:
		table = obj->table[PCIE_DMA_DATA_READ_REMOTE_TABLE_OFFSET + local_idx];
		table->type = PCIE_DMA_READ_REMOTE;
		table->dir = DMA_FROM_BUS;
		local = obj->local_mem_start + local_idx * obj->buffer_size;
		bus = obj->remote_mem_start + bus_idx * obj->buffer_size;
		if (!is_rc(obj)) {
			local += obj->rd_buf_size;
			bus += obj->wr_buf_size;
		}
		buf_size = obj->buffer_size;
		break;
	default:
		dev_err(dev, "type = %d not support\n", type);
		return;
	}

	table->buf_size = buf_size;
	table->bus = bus;
	table->local = local;
	table->chn = chn;

	if (!obj->config_dma_func) {
		WARN_ON(1);
		return;
	}
	obj->config_dma_func(table);

	spin_lock_irqsave(&obj->tbl_list_lock, flags);
	list_add_tail(&table->tbl_node, &obj->tbl_list);
	spin_unlock_irqrestore(&obj->tbl_list_lock, flags);
}

static void rk_pcie_dma_trx_work(struct work_struct *work)
{
	unsigned long flags;
	struct dma_trx_obj *obj = container_of(work,
				struct dma_trx_obj, dma_trx_work);
	struct dma_table *table;

	while (!list_empty(&obj->tbl_list)) {
		table = list_first_entry(&obj->tbl_list, struct dma_table,
					 tbl_node);
		if (obj->dma_free) {
			obj->dma_free = false;
			spin_lock_irqsave(&obj->tbl_list_lock, flags);
			list_del_init(&table->tbl_node);
			spin_unlock_irqrestore(&obj->tbl_list_lock, flags);
			obj->cur = table;
			if (!obj->start_dma_func) {
				WARN_ON(1);
				return;
			}
			reinit_completion(&obj->done);
			obj->start_dma_func(obj, table);
		}
	}
}

static void rk_pcie_clear_ack(void *addr)
{
	writel(0x0, addr);
}

static enum hrtimer_restart rk_pcie_scan_timer(struct hrtimer *timer)
{
	unsigned int sdv;
	unsigned int idx;
	unsigned int sav;
	unsigned int suv;
	void *sda_base;
	void *scan_data_addr;
	void *scan_ack_addr;
	void *scan_user_addr;
	int i;
	bool need_ack = false;
	struct dma_trx_obj *obj = container_of(timer,
					struct dma_trx_obj, scan_timer);
	unsigned int check_sum, check_sum_tmp;

	if (!obj->remote_mem_start) {
		if (is_rc(obj))
			obj->remote_mem_start = readl(obj->region_base + 0x4);
		else
			obj->remote_mem_start = readl(obj->region_base);
		goto continue_scan;
	}

	for (i = 0; i < PCIE_DMA_BUF_CNT; i++) {
		sda_base = obj->local_mem_base + obj->buffer_size * i;

		if (obj->addr_reverse) {
			if (is_rc(obj))
				scan_data_addr = sda_base;
			else
				scan_data_addr =  sda_base + obj->rd_buf_size;
		} else {
			if (is_rc(obj))
				scan_data_addr =  sda_base + obj->rd_buf_size;
			else
				scan_data_addr = sda_base;
		}
		sdv = readl(scan_data_addr + obj->set_data_check_pos);
		idx = readl(scan_data_addr + obj->set_local_idx_pos);

		if (sdv == PCIE_DMA_DATA_CHECK) {
			if (!need_ack)
				need_ack = true;
			if (enable_check_sum) {
				check_sum = readl(scan_data_addr + obj->set_chk_sum_pos);
				check_sum_tmp = rk_pcie_check_sum(scan_data_addr, SZ_1M - 0x10);
				if (check_sum != check_sum_tmp) {
					pr_err("checksum[%d] failed, 0x%x, should be 0x%x\n",
					       idx, check_sum_tmp, check_sum);
					print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
						       32, 4, scan_data_addr, SZ_1M, false);
				}
				writel(0x0, scan_data_addr + obj->set_chk_sum_pos);
			}
			writel(0x0, scan_data_addr + obj->set_data_check_pos);

			set_bit(i, &obj->local_read_available);
			rk_pcie_prepare_dma(obj, idx, 0, 0, 0x4,
				PCIE_DMA_DATA_RCV_ACK, PCIE_DMA_DEFAULT_CHN);
		}
	}

	if (need_ack || !list_empty(&obj->tbl_list))
		queue_work(obj->dma_trx_wq, &obj->dma_trx_work);

	scan_ack_addr = obj->local_mem_base + obj->ack_base;
	scan_user_addr = obj->local_mem_base + obj->ack_base;

	if (is_rc(obj)) {
		scan_user_addr += PCIE_DMA_ACK_BLOCK_SIZE * 2;
	} else {
		scan_ack_addr += PCIE_DMA_ACK_BLOCK_SIZE;
		scan_user_addr += PCIE_DMA_ACK_BLOCK_SIZE * 3;
	}

	for (i = 0; i < PCIE_DMA_BUF_CNT; i++) {
		void *addr = scan_ack_addr + i * NODE_SIZE;

		sav = readl(addr);
		if (sav == PCIE_DMA_DATA_ACK_CHECK) {
			rk_pcie_clear_ack(addr);
			set_bit(i, &obj->local_write_available);
		}

		addr = scan_user_addr + i * NODE_SIZE;
		suv = readl(addr);
		if (suv == PCIE_DMA_DATA_FREE_ACK_CHECK) {
			rk_pcie_clear_ack(addr);
			set_bit(i, &obj->remote_write_available);
		}
	}

	if ((obj->local_write_available && obj->remote_write_available) ||
		obj->local_read_available) {
		wake_up(&obj->event_queue);
	}

continue_scan:
	hrtimer_add_expires(&obj->scan_timer, ktime_set(0, 100 * 1000));

	return HRTIMER_RESTART;
}

static int rk_pcie_misc_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *miscdev = filp->private_data;
	struct pcie_misc_dev *pcie_misc_dev = container_of(miscdev,
						 struct pcie_misc_dev, dev);

	filp->private_data = pcie_misc_dev->obj;

	mutex_lock(&pcie_misc_dev->obj->count_mutex);
	if (pcie_misc_dev->obj->ref_count++)
		goto already_opened;

	pcie_misc_dev->obj->loop_count = 0;
	pcie_misc_dev->obj->local_read_available = 0x0;
	pcie_misc_dev->obj->local_write_available = 0xff;
	pcie_misc_dev->obj->remote_write_available = 0xff;
	pcie_misc_dev->obj->dma_free = true;

	pr_info("Open pcie misc device success\n");

already_opened:
	mutex_unlock(&pcie_misc_dev->obj->count_mutex);
	return 0;
}

static int rk_pcie_misc_release(struct inode *inode, struct file *filp)
{
	struct dma_trx_obj *obj = filp->private_data;

	mutex_lock(&obj->count_mutex);

	if (--obj->ref_count)
		goto still_opened;
	hrtimer_cancel(&obj->scan_timer);

	pr_info("Close pcie misc device\n");

still_opened:
	mutex_unlock(&obj->count_mutex);
	return 0;
}

static int rk_pcie_misc_mmap(struct file *filp,
				     struct vm_area_struct *vma)
{
	struct dma_trx_obj *obj = filp->private_data;
	size_t size = vma->vm_end - vma->vm_start;
	int err;

	err = remap_pfn_range(vma, vma->vm_start,
			    __phys_to_pfn(obj->local_mem_start),
			    size, vma->vm_page_prot);
	if (err)
		return -EAGAIN;

	return 0;
}
static void rk_pcie_send_addr_to_remote(struct dma_trx_obj *obj)
{
	struct dma_table *table;

	/* Temporary use to send local buffer address to remote */
	table = obj->table[PCIE_DMA_DATA_SND_TABLE_OFFSET];
	table->type = PCIE_DMA_DATA_SND;
	table->dir = DMA_TO_BUS;
	table->buf_size = 0x4;
	if (is_rc(obj))
		table->local = obj->region_start;
	else
		table->local = obj->region_start + 0x4;
	table->bus = table->local;
	table->chn = PCIE_DMA_DEFAULT_CHN;
	obj->config_dma_func(table);
	obj->cur = table;
	obj->start_dma_func(obj, table);
}

static long rk_pcie_misc_ioctl(struct file *filp, unsigned int cmd,
					unsigned long arg)
{
	struct dma_trx_obj *obj = filp->private_data;
	struct device *dev = obj->dev;
	union pcie_dma_ioctl_param msg;
	union pcie_dma_ioctl_param msg_to_user;
	phys_addr_t addr;
	void __user *uarg = (void __user *)arg;
	int ret;
	int i;
	phys_addr_t addr_send_to_remote;
	enum transfer_type type;

	if (copy_from_user(&msg, uarg, sizeof(msg)) != 0) {
		dev_err(dev, "failed to copy argument into kernel space\n");
		return -EFAULT;
	}

	switch (cmd) {
	case PCIE_DMA_START:
		test_and_clear_bit(msg.in.l_widx, &obj->local_write_available);
		test_and_clear_bit(msg.in.r_widx, &obj->remote_write_available);
		type = PCIE_DMA_DATA_SND;
		obj->loop_count++;
		break;
	case PCIE_DMA_GET_LOCAL_READ_BUFFER_INDEX:
		msg_to_user.lra = obj->local_read_available;
		addr = obj->local_mem_start;
		if (is_rc(obj))
			addr += obj->rd_buf_size;
		/* by kernel auto or by user to invalidate cache */
		for (i = 0; i < PCIE_DMA_BUF_CNT; i++) {
			if (test_bit(i, &obj->local_read_available))
				dma_sync_single_for_cpu(dev, addr + i * obj->buffer_size, obj->buffer_size, DMA_FROM_DEVICE);
		}

		ret = copy_to_user(uarg, &msg_to_user, sizeof(msg));
		if (ret) {
			dev_err(dev, "failed to get read buffer index\n");
			return -EFAULT;
		}
		break;
	case PCIE_DMA_FREE_LOCAL_READ_BUFFER_INDEX:
		test_and_clear_bit(msg.in.idx, &obj->local_read_available);
		type = PCIE_DMA_DATA_FREE_ACK;
		break;
	case PCIE_DMA_GET_LOCAL_REMOTE_WRITE_BUFFER_INDEX:
		msg_to_user.out.lwa = obj->local_write_available;
		msg_to_user.out.rwa = obj->remote_write_available;
		ret = copy_to_user(uarg, &msg_to_user, sizeof(msg));
		if (ret) {
			dev_err(dev, "failed to get write buffer index\n");
			return -EFAULT;
		}
		break;
	case PCIE_DMA_SYNC_BUFFER_FOR_CPU:
		addr = obj->local_mem_start + msg.in.idx * obj->buffer_size;
		if (is_rc(obj))
			addr += obj->rd_buf_size;
		dma_sync_single_for_cpu(dev, addr, obj->buffer_size,
					DMA_FROM_DEVICE);
		break;
	case PCIE_DMA_WAIT_TRANSFER_COMPLETE:
		ret = wait_for_completion_interruptible(&obj->done);
		if (WARN_ON(ret)) {
			pr_info("failed to wait complete\n");
			return ret;
		}

		obj->cache_time_avarage = obj->cache_time_total / obj->loop_count;

		pr_debug("cache_time: total = %lld, average = %lld, count = %d, size = 0x%x\n",
			 obj->cache_time_total, obj->cache_time_avarage,
			 obj->loop_count, obj->buffer_size);

		obj->cache_time_avarage = 0;
		obj->cache_time_total = 0;

		obj->loop_count = 0;
		break;
	case PCIE_DMA_SET_LOOP_COUNT:
		obj->loop_count_threshold = msg.count;
		pr_info("threshold = %d\n", obj->loop_count_threshold);
		break;
	case PCIE_DMA_GET_TOTAL_BUFFER_SIZE:
		msg_to_user.total_buffer_size = obj->local_mem_size;
		ret = copy_to_user(uarg, &msg_to_user, sizeof(msg));
		if (ret) {
			dev_err(dev, "failed to get write buffer index\n");
			return -EFAULT;
		}
		break;
	case PCIE_DMA_SET_BUFFER_SIZE:
		obj->buffer_size = msg.buffer_size;
		pr_debug("buffer_size = %d\n", obj->buffer_size);
		obj->rd_buf_size = obj->buffer_size * PCIE_DMA_BUF_CNT;
		obj->wr_buf_size = obj->buffer_size * PCIE_DMA_BUF_CNT;
		obj->ack_base = obj->rd_buf_size + obj->wr_buf_size;
		obj->set_data_check_pos = obj->buffer_size - 0x4;
		obj->set_local_idx_pos = obj->buffer_size - 0x8;
		obj->set_buf_size_pos = obj->buffer_size - 0xc;
		obj->set_chk_sum_pos = obj->buffer_size - 0x10;
		break;
	case PCIE_DMA_READ_FROM_REMOTE:
		pr_debug("read buffer from : %d to local : %d\n",
			 msg.in.r_widx, msg.in.l_widx);

		type = PCIE_DMA_READ_REMOTE;
		break;
	case PCIE_DMA_USER_SET_BUF_ADDR:
		/* If msg.local_addr valid, use msg.local_addr for local buffer,
		 * and should be contiguous physical address.
		 * If msg.local is zero, local buffer get from DT reserved.
		 * Anyway local buffer address should send to remote, then
		 * remote know where to send data to.
		 * Should finish this case first before send data.
		 */
		if (msg.local_addr) {
			pr_debug("local_addr = %pa\n", &msg.local_addr);
			addr_send_to_remote = (phys_addr_t)msg.local_addr;
			obj->local_mem_start = (phys_addr_t)msg.local_addr;
			/* Unmap previous */
			rk_pcie_unmap_kernel(obj->local_mem_base);
			/* Remap userspace's buffer to kernel */
			obj->local_mem_base = rk_pcie_map_kernel(obj->local_mem_start,
						obj->buffer_size * PCIE_DMA_BUF_CNT * 2 + SZ_4K);
			if (!obj->local_mem_base)
				return -EFAULT;
		} else {
			addr_send_to_remote = obj->local_mem_start;
		}
		if (is_rc(obj))
			writel(addr_send_to_remote, obj->region_base);
		else
			writel(addr_send_to_remote, obj->region_base + 0x4);
		rk_pcie_send_addr_to_remote(obj);
		hrtimer_start(&obj->scan_timer,
		      ktime_set(0, 1 * 1000 * 1000 * 1000), HRTIMER_MODE_REL);
		break;
	case PCIE_DMA_GET_BUFFER_SIZE:
		msg_to_user.buffer_size = obj->buffer_size;
		ret = copy_to_user(uarg, &msg_to_user, sizeof(msg));
		if (ret) {
			dev_err(dev, "failed to get buffer\n");
			return -EFAULT;
		}
		break;
	default:
		pr_info("%s, %d, cmd : %x not support\n", __func__, __LINE__,
			cmd);
		return -EFAULT;
	}

	if (cmd == PCIE_DMA_START || cmd == PCIE_DMA_READ_FROM_REMOTE ||
		cmd == PCIE_DMA_FREE_LOCAL_READ_BUFFER_INDEX) {
		rk_pcie_prepare_dma(obj, msg.in.idx, msg.in.r_widx,
				    msg.in.l_widx, msg.in.size, type,
				    msg.in.chn);
		queue_work(obj->dma_trx_wq, &obj->dma_trx_work);
	}

	return 0;
}

static unsigned int rk_pcie_misc_poll(struct file *filp,
						poll_table *wait)
{
	struct dma_trx_obj *obj = filp->private_data;
	u32 lwa, rwa, lra;
	u32 ret = 0;

	poll_wait(filp, &obj->event_queue, wait);

	lwa = obj->local_write_available;
	rwa = obj->remote_write_available;
	if (lwa && rwa)
		ret = POLLOUT;

	lra = obj->local_read_available;
	if (lra)
		ret |= POLLIN;

	return ret;
}

static const struct file_operations rk_pcie_misc_fops = {
	.open		= rk_pcie_misc_open,
	.release	= rk_pcie_misc_release,
	.mmap		= rk_pcie_misc_mmap,
	.unlocked_ioctl	= rk_pcie_misc_ioctl,
	.poll		= rk_pcie_misc_poll,
};

static void rk_pcie_delete_misc(struct dma_trx_obj *obj)
{
	misc_deregister(&obj->pcie_dev->dev);
}

static int rk_pcie_add_misc(struct dma_trx_obj *obj)
{
	int ret;
	struct pcie_misc_dev *pcie_dev;

	pcie_dev = devm_kzalloc(obj->dev, sizeof(*pcie_dev), GFP_KERNEL);
	if (!pcie_dev)
		return -ENOMEM;

	pcie_dev->dev.minor = MISC_DYNAMIC_MINOR;
	pcie_dev->dev.name = "pcie-dev";
	pcie_dev->dev.fops = &rk_pcie_misc_fops;
	pcie_dev->dev.parent = NULL;

	ret = misc_register(&pcie_dev->dev);
	if (ret) {
		pr_err("pcie: failed to register misc device.\n");
		return ret;
	}

	pcie_dev->obj = obj;
	obj->pcie_dev = pcie_dev;

	pr_info("register misc device pcie-dev\n");

	return 0;
}

static void *rk_pcie_map_kernel(phys_addr_t start, size_t len)
{
	int i;
	void *vaddr;
	pgprot_t pgprot;
	phys_addr_t phys;
	int npages = PAGE_ALIGN(len) / PAGE_SIZE;
	struct page **p = vmalloc(sizeof(struct page *) * npages);

	if (!p)
		return NULL;

	pgprot = pgprot_noncached(PAGE_KERNEL);

	phys = start;
	for (i = 0; i < npages; i++) {
		p[i] = phys_to_page(phys);
		phys += PAGE_SIZE;
	}

	vaddr = vmap(p, npages, VM_MAP, pgprot);
	vfree(p);

	return vaddr;
}

static void rk_pcie_unmap_kernel(void *vaddr)
{
	vunmap(vaddr);
}

static void rk_pcie_dma_table_free(struct dma_trx_obj *obj, int num)
{
	int i;
	struct dma_table *table;

	if (num > PCIE_DMA_TABLE_NUM)
		num = PCIE_DMA_TABLE_NUM;

	for (i = 0; i < num; i++) {
		table = obj->table[i];
		dma_free_coherent(obj->dev, PCIE_DMA_PARAM_SIZE,
			table->descs, table->phys_descs);
		kfree(table);
	}
}

static int rk_pcie_dma_table_alloc(struct dma_trx_obj *obj)
{
	int i;
	struct dma_table *table;

	for (i = 0; i < PCIE_DMA_TABLE_NUM; i++) {
		table = kzalloc(sizeof(*table), GFP_KERNEL);
		if (!table)
			goto free_table;

		table->descs = dma_alloc_coherent(obj->dev, PCIE_DMA_PARAM_SIZE,
				&table->phys_descs, GFP_KERNEL | __GFP_ZERO);
		if (!table->descs) {
			kfree(table);
			goto free_table;
		}

		table->chn = PCIE_DMA_DEFAULT_CHN;
		INIT_LIST_HEAD(&table->tbl_node);
		obj->table[i] = table;
	}

	return 0;

free_table:
	rk_pcie_dma_table_free(obj, i);
	dev_err(obj->dev, "Failed to alloc dma table\n");

	return -ENOMEM;
}

#ifdef CONFIG_DEBUG_FS
static int rk_pcie_debugfs_trx_show(struct seq_file *s, void *v)
{
	struct dma_trx_obj *dma_obj = s->private;
	bool list = list_empty(&dma_obj->tbl_list);

	seq_printf(s, "version = %x,", dma_obj->version);
	seq_printf(s, "last:%s,",
			dma_obj->cur ? (dma_obj->cur->dir == DMA_FROM_BUS ? "read" : "write") : "no trx");
	seq_printf(s, "irq_num = %ld, loop_count = %d,",
			dma_obj->irq_num, dma_obj->loop_count);
	seq_printf(s, "loop_threshold = %d,",
			dma_obj->loop_count_threshold);
	seq_printf(s, "lwa = %lx, rwa = %lx, lra = %lx,",
			dma_obj->local_write_available,
			dma_obj->remote_write_available,
			dma_obj->local_read_available);
	seq_printf(s, "list : (%s), dma chn : (%s)\n",
			list ? "empty" : "not empty",
			dma_obj->dma_free ? "free" : "busy");

	return 0;
}

static int rk_pcie_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_pcie_debugfs_trx_show, inode->i_private);
}

static ssize_t rk_pcie_debugfs_write(struct file *file, const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	int ret;

	ret = kstrtoint_from_user(user_buf, count, 0, &enable_check_sum);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations rk_pcie_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rk_pcie_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rk_pcie_debugfs_write,
};
#endif

struct dma_trx_obj *rk_pcie_dma_obj_probe(struct device *dev)
{
	int ret;
	int busno;
	struct device_node *np = dev->of_node;
	struct device_node *mem;
	struct resource reg;
	struct dma_trx_obj *obj;
	int reverse;

	obj = devm_kzalloc(dev, sizeof(struct dma_trx_obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->dev = dev;

	ret = of_property_read_u32(np, "busno", &busno);
	if (ret < 0) {
		dev_err(dev, "missing \"busno\" property\n");
		return ERR_PTR(ret);
	}

	obj->busno = busno;

	ret = of_property_read_u32(np, "reverse", &reverse);
	if (ret < 0)
		obj->addr_reverse = 0;
	else
		obj->addr_reverse = reverse;

	mem = of_parse_phandle(np, "memory-region", 0);
	if (!mem) {
		dev_err(dev, "missing \"memory-region\" property\n");
		return ERR_PTR(-ENODEV);
	}

	ret = of_address_to_resource(mem, 0, &reg);
	if (ret < 0) {
		dev_err(dev, "missing \"reg\" property\n");
		return ERR_PTR(-ENODEV);
	}

	obj->local_mem_start = reg.start;
	obj->local_mem_size = resource_size(&reg);
	obj->local_mem_base = rk_pcie_map_kernel(obj->local_mem_start,
						 obj->local_mem_size);
	if (!obj->local_mem_base)
		return ERR_PTR(-ENOMEM);

	mem = of_parse_phandle(np, "memory-region1", 0);
	if (!mem) {
		dev_err(dev, "missing \"memory-region1\" property\n");
		obj = ERR_PTR(-ENODEV);
		goto unmap_local_mem_region;
	}

	ret = of_address_to_resource(mem, 0, &reg);
	if (ret < 0) {
		dev_err(dev, "missing \"reg\" property\n");
		obj = ERR_PTR(-ENODEV);
		goto unmap_local_mem_region;
	}

	obj->region_start = reg.start;
	obj->region_size = resource_size(&reg);
	obj->region_base = rk_pcie_map_kernel(obj->region_start,
					      obj->region_size);
	if (!obj->region_base) {
		dev_err(dev, "mapping region_base error\n");
		obj = ERR_PTR(-ENOMEM);
		goto unmap_local_mem_region;
	}
	if (!is_rc(obj))
		writel(0x0, obj->region_base);
	else
		writel(0x0, obj->region_base + 0x4);

	ret = rk_pcie_dma_table_alloc(obj);
	if (ret) {
		dev_err(dev, "rk_pcie_dma_table_alloc error\n");
		obj = ERR_PTR(-ENOMEM);
		goto unmap_region;

	}
	obj->dma_trx_wq = create_singlethread_workqueue("dma_trx_wq");
	INIT_WORK(&obj->dma_trx_work, rk_pcie_dma_trx_work);

	INIT_LIST_HEAD(&obj->tbl_list);
	spin_lock_init(&obj->tbl_list_lock);

	init_waitqueue_head(&obj->event_queue);

	hrtimer_init_on_stack(&obj->scan_timer, CLOCK_MONOTONIC,
				HRTIMER_MODE_REL);
	obj->scan_timer.function = rk_pcie_scan_timer;
	obj->irq_num = 0;
	obj->loop_count_threshold = 0;
	obj->ref_count = 0;
	obj->version = 0x4;
	init_completion(&obj->done);
	obj->cb = rk_pcie_handle_dma_interrupt;

	mutex_init(&obj->count_mutex);
	rk_pcie_add_misc(obj);

#ifdef CONFIG_DEBUG_FS
	obj->pcie_root = debugfs_create_dir("pcie", NULL);
	if (!obj->pcie_root) {
		obj = ERR_PTR(-EINVAL);
		goto free_dma_table;
	}

	debugfs_create_file("pcie_trx", 0644, obj->pcie_root, obj,
			&rk_pcie_debugfs_fops);
#endif

	return obj;
free_dma_table:
	rk_pcie_dma_table_free(obj, PCIE_DMA_TABLE_NUM);
unmap_region:
	rk_pcie_unmap_kernel(obj->region_base);
unmap_local_mem_region:
	rk_pcie_unmap_kernel(obj->local_mem_base);

	return obj;
}
EXPORT_SYMBOL_GPL(rk_pcie_dma_obj_probe);

void rk_pcie_dma_obj_remove(struct dma_trx_obj *obj)
{
	hrtimer_cancel(&obj->scan_timer);
	destroy_hrtimer_on_stack(&obj->scan_timer);
	rk_pcie_delete_misc(obj);
	rk_pcie_unmap_kernel(obj->local_mem_base);
	rk_pcie_dma_table_free(obj, PCIE_DMA_TABLE_NUM);
	destroy_workqueue(obj->dma_trx_wq);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(obj->pcie_root);
#endif
}
EXPORT_SYMBOL_GPL(rk_pcie_dma_obj_remove);
