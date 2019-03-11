/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Silicon Graphics, Inc.  All rights reserved.
 */

/*
 *	MOATB Core Services driver.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/uio.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/sn/addrs.h>
#include <asm/sn/intr.h>
#include <asm/sn/tiocx.h>
#include "mbcs.h"

#define MBCS_DEBUG 0
#if MBCS_DEBUG
#define DBG(fmt...)    printk(KERN_ALERT fmt)
#else
#define DBG(fmt...)
#endif
static DEFINE_MUTEX(mbcs_mutex);
static int mbcs_major;

static LIST_HEAD(soft_list);

/*
 * file operations
 */
static const struct file_operations mbcs_ops = {
	.owner = THIS_MODULE,
	.open = mbcs_open,
	.llseek = mbcs_sram_llseek,
	.read = mbcs_sram_read,
	.write = mbcs_sram_write,
	.mmap = mbcs_gscr_mmap,
};

struct mbcs_callback_arg {
	int minor;
	struct cx_dev *cx_dev;
};

static inline void mbcs_getdma_init(struct getdma *gdma)
{
	memset(gdma, 0, sizeof(struct getdma));
	gdma->DoneIntEnable = 1;
}

static inline void mbcs_putdma_init(struct putdma *pdma)
{
	memset(pdma, 0, sizeof(struct putdma));
	pdma->DoneIntEnable = 1;
}

static inline void mbcs_algo_init(struct algoblock *algo_soft)
{
	memset(algo_soft, 0, sizeof(struct algoblock));
}

static inline void mbcs_getdma_set(void *mmr,
		       uint64_t hostAddr,
		       uint64_t localAddr,
		       uint64_t localRamSel,
		       uint64_t numPkts,
		       uint64_t amoEnable,
		       uint64_t intrEnable,
		       uint64_t peerIO,
		       uint64_t amoHostDest,
		       uint64_t amoModType, uint64_t intrHostDest,
		       uint64_t intrVector)
{
	union dma_control rdma_control;
	union dma_amo_dest amo_dest;
	union intr_dest intr_dest;
	union dma_localaddr local_addr;
	union dma_hostaddr host_addr;

	rdma_control.dma_control_reg = 0;
	amo_dest.dma_amo_dest_reg = 0;
	intr_dest.intr_dest_reg = 0;
	local_addr.dma_localaddr_reg = 0;
	host_addr.dma_hostaddr_reg = 0;

	host_addr.dma_sys_addr = hostAddr;
	MBCS_MMR_SET(mmr, MBCS_RD_DMA_SYS_ADDR, host_addr.dma_hostaddr_reg);

	local_addr.dma_ram_addr = localAddr;
	local_addr.dma_ram_sel = localRamSel;
	MBCS_MMR_SET(mmr, MBCS_RD_DMA_LOC_ADDR, local_addr.dma_localaddr_reg);

	rdma_control.dma_op_length = numPkts;
	rdma_control.done_amo_en = amoEnable;
	rdma_control.done_int_en = intrEnable;
	rdma_control.pio_mem_n = peerIO;
	MBCS_MMR_SET(mmr, MBCS_RD_DMA_CTRL, rdma_control.dma_control_reg);

	amo_dest.dma_amo_sys_addr = amoHostDest;
	amo_dest.dma_amo_mod_type = amoModType;
	MBCS_MMR_SET(mmr, MBCS_RD_DMA_AMO_DEST, amo_dest.dma_amo_dest_reg);

	intr_dest.address = intrHostDest;
	intr_dest.int_vector = intrVector;
	MBCS_MMR_SET(mmr, MBCS_RD_DMA_INT_DEST, intr_dest.intr_dest_reg);

}

static inline void mbcs_putdma_set(void *mmr,
		       uint64_t hostAddr,
		       uint64_t localAddr,
		       uint64_t localRamSel,
		       uint64_t numPkts,
		       uint64_t amoEnable,
		       uint64_t intrEnable,
		       uint64_t peerIO,
		       uint64_t amoHostDest,
		       uint64_t amoModType,
		       uint64_t intrHostDest, uint64_t intrVector)
{
	union dma_control wdma_control;
	union dma_amo_dest amo_dest;
	union intr_dest intr_dest;
	union dma_localaddr local_addr;
	union dma_hostaddr host_addr;

	wdma_control.dma_control_reg = 0;
	amo_dest.dma_amo_dest_reg = 0;
	intr_dest.intr_dest_reg = 0;
	local_addr.dma_localaddr_reg = 0;
	host_addr.dma_hostaddr_reg = 0;

	host_addr.dma_sys_addr = hostAddr;
	MBCS_MMR_SET(mmr, MBCS_WR_DMA_SYS_ADDR, host_addr.dma_hostaddr_reg);

	local_addr.dma_ram_addr = localAddr;
	local_addr.dma_ram_sel = localRamSel;
	MBCS_MMR_SET(mmr, MBCS_WR_DMA_LOC_ADDR, local_addr.dma_localaddr_reg);

	wdma_control.dma_op_length = numPkts;
	wdma_control.done_amo_en = amoEnable;
	wdma_control.done_int_en = intrEnable;
	wdma_control.pio_mem_n = peerIO;
	MBCS_MMR_SET(mmr, MBCS_WR_DMA_CTRL, wdma_control.dma_control_reg);

	amo_dest.dma_amo_sys_addr = amoHostDest;
	amo_dest.dma_amo_mod_type = amoModType;
	MBCS_MMR_SET(mmr, MBCS_WR_DMA_AMO_DEST, amo_dest.dma_amo_dest_reg);

	intr_dest.address = intrHostDest;
	intr_dest.int_vector = intrVector;
	MBCS_MMR_SET(mmr, MBCS_WR_DMA_INT_DEST, intr_dest.intr_dest_reg);

}

static inline void mbcs_algo_set(void *mmr,
		     uint64_t amoHostDest,
		     uint64_t amoModType,
		     uint64_t intrHostDest,
		     uint64_t intrVector, uint64_t algoStepCount)
{
	union dma_amo_dest amo_dest;
	union intr_dest intr_dest;
	union algo_step step;

	step.algo_step_reg = 0;
	intr_dest.intr_dest_reg = 0;
	amo_dest.dma_amo_dest_reg = 0;

	amo_dest.dma_amo_sys_addr = amoHostDest;
	amo_dest.dma_amo_mod_type = amoModType;
	MBCS_MMR_SET(mmr, MBCS_ALG_AMO_DEST, amo_dest.dma_amo_dest_reg);

	intr_dest.address = intrHostDest;
	intr_dest.int_vector = intrVector;
	MBCS_MMR_SET(mmr, MBCS_ALG_INT_DEST, intr_dest.intr_dest_reg);

	step.alg_step_cnt = algoStepCount;
	MBCS_MMR_SET(mmr, MBCS_ALG_STEP, step.algo_step_reg);
}

static inline int mbcs_getdma_start(struct mbcs_soft *soft)
{
	void *mmr_base;
	struct getdma *gdma;
	uint64_t numPkts;
	union cm_control cm_control;

	mmr_base = soft->mmr_base;
	gdma = &soft->getdma;

	/* check that host address got setup */
	if (!gdma->hostAddr)
		return -1;

	numPkts =
	    (gdma->bytes + (MBCS_CACHELINE_SIZE - 1)) / MBCS_CACHELINE_SIZE;

	/* program engine */
	mbcs_getdma_set(mmr_base, tiocx_dma_addr(gdma->hostAddr),
		   gdma->localAddr,
		   (gdma->localAddr < MB2) ? 0 :
		   (gdma->localAddr < MB4) ? 1 :
		   (gdma->localAddr < MB6) ? 2 : 3,
		   numPkts,
		   gdma->DoneAmoEnable,
		   gdma->DoneIntEnable,
		   gdma->peerIO,
		   gdma->amoHostDest,
		   gdma->amoModType,
		   gdma->intrHostDest, gdma->intrVector);

	/* start engine */
	cm_control.cm_control_reg = MBCS_MMR_GET(mmr_base, MBCS_CM_CONTROL);
	cm_control.rd_dma_go = 1;
	MBCS_MMR_SET(mmr_base, MBCS_CM_CONTROL, cm_control.cm_control_reg);

	return 0;

}

static inline int mbcs_putdma_start(struct mbcs_soft *soft)
{
	void *mmr_base;
	struct putdma *pdma;
	uint64_t numPkts;
	union cm_control cm_control;

	mmr_base = soft->mmr_base;
	pdma = &soft->putdma;

	/* check that host address got setup */
	if (!pdma->hostAddr)
		return -1;

	numPkts =
	    (pdma->bytes + (MBCS_CACHELINE_SIZE - 1)) / MBCS_CACHELINE_SIZE;

	/* program engine */
	mbcs_putdma_set(mmr_base, tiocx_dma_addr(pdma->hostAddr),
		   pdma->localAddr,
		   (pdma->localAddr < MB2) ? 0 :
		   (pdma->localAddr < MB4) ? 1 :
		   (pdma->localAddr < MB6) ? 2 : 3,
		   numPkts,
		   pdma->DoneAmoEnable,
		   pdma->DoneIntEnable,
		   pdma->peerIO,
		   pdma->amoHostDest,
		   pdma->amoModType,
		   pdma->intrHostDest, pdma->intrVector);

	/* start engine */
	cm_control.cm_control_reg = MBCS_MMR_GET(mmr_base, MBCS_CM_CONTROL);
	cm_control.wr_dma_go = 1;
	MBCS_MMR_SET(mmr_base, MBCS_CM_CONTROL, cm_control.cm_control_reg);

	return 0;

}

static inline int mbcs_algo_start(struct mbcs_soft *soft)
{
	struct algoblock *algo_soft = &soft->algo;
	void *mmr_base = soft->mmr_base;
	union cm_control cm_control;

	if (mutex_lock_interruptible(&soft->algolock))
		return -ERESTARTSYS;

	atomic_set(&soft->algo_done, 0);

	mbcs_algo_set(mmr_base,
		 algo_soft->amoHostDest,
		 algo_soft->amoModType,
		 algo_soft->intrHostDest,
		 algo_soft->intrVector, algo_soft->algoStepCount);

	/* start algorithm */
	cm_control.cm_control_reg = MBCS_MMR_GET(mmr_base, MBCS_CM_CONTROL);
	cm_control.alg_done_int_en = 1;
	cm_control.alg_go = 1;
	MBCS_MMR_SET(mmr_base, MBCS_CM_CONTROL, cm_control.cm_control_reg);

	mutex_unlock(&soft->algolock);

	return 0;
}

static inline ssize_t
do_mbcs_sram_dmawrite(struct mbcs_soft *soft, uint64_t hostAddr,
		      size_t len, loff_t * off)
{
	int rv = 0;

	if (mutex_lock_interruptible(&soft->dmawritelock))
		return -ERESTARTSYS;

	atomic_set(&soft->dmawrite_done, 0);

	soft->putdma.hostAddr = hostAddr;
	soft->putdma.localAddr = *off;
	soft->putdma.bytes = len;

	if (mbcs_putdma_start(soft) < 0) {
		DBG(KERN_ALERT "do_mbcs_sram_dmawrite: "
					"mbcs_putdma_start failed\n");
		rv = -EAGAIN;
		goto dmawrite_exit;
	}

	if (wait_event_interruptible(soft->dmawrite_queue,
					atomic_read(&soft->dmawrite_done))) {
		rv = -ERESTARTSYS;
		goto dmawrite_exit;
	}

	rv = len;
	*off += len;

dmawrite_exit:
	mutex_unlock(&soft->dmawritelock);

	return rv;
}

static inline ssize_t
do_mbcs_sram_dmaread(struct mbcs_soft *soft, uint64_t hostAddr,
		     size_t len, loff_t * off)
{
	int rv = 0;

	if (mutex_lock_interruptible(&soft->dmareadlock))
		return -ERESTARTSYS;

	atomic_set(&soft->dmawrite_done, 0);

	soft->getdma.hostAddr = hostAddr;
	soft->getdma.localAddr = *off;
	soft->getdma.bytes = len;

	if (mbcs_getdma_start(soft) < 0) {
		DBG(KERN_ALERT "mbcs_strategy: mbcs_getdma_start failed\n");
		rv = -EAGAIN;
		goto dmaread_exit;
	}

	if (wait_event_interruptible(soft->dmaread_queue,
					atomic_read(&soft->dmaread_done))) {
		rv = -ERESTARTSYS;
		goto dmaread_exit;
	}

	rv = len;
	*off += len;

dmaread_exit:
	mutex_unlock(&soft->dmareadlock);

	return rv;
}

static int mbcs_open(struct inode *ip, struct file *fp)
{
	struct mbcs_soft *soft;
	int minor;

	mutex_lock(&mbcs_mutex);
	minor = iminor(ip);

	/* Nothing protects access to this list... */
	list_for_each_entry(soft, &soft_list, list) {
		if (soft->nasid == minor) {
			fp->private_data = soft->cxdev;
			mutex_unlock(&mbcs_mutex);
			return 0;
		}
	}

	mutex_unlock(&mbcs_mutex);
	return -ENODEV;
}

static ssize_t mbcs_sram_read(struct file * fp, char __user *buf, size_t len, loff_t * off)
{
	struct cx_dev *cx_dev = fp->private_data;
	struct mbcs_soft *soft = cx_dev->soft;
	uint64_t hostAddr;
	int rv = 0;

	hostAddr = __get_dma_pages(GFP_KERNEL, get_order(len));
	if (hostAddr == 0)
		return -ENOMEM;

	rv = do_mbcs_sram_dmawrite(soft, hostAddr, len, off);
	if (rv < 0)
		goto exit;

	if (copy_to_user(buf, (void *)hostAddr, len))
		rv = -EFAULT;

      exit:
	free_pages(hostAddr, get_order(len));

	return rv;
}

static ssize_t
mbcs_sram_write(struct file * fp, const char __user *buf, size_t len, loff_t * off)
{
	struct cx_dev *cx_dev = fp->private_data;
	struct mbcs_soft *soft = cx_dev->soft;
	uint64_t hostAddr;
	int rv = 0;

	hostAddr = __get_dma_pages(GFP_KERNEL, get_order(len));
	if (hostAddr == 0)
		return -ENOMEM;

	if (copy_from_user((void *)hostAddr, buf, len)) {
		rv = -EFAULT;
		goto exit;
	}

	rv = do_mbcs_sram_dmaread(soft, hostAddr, len, off);

      exit:
	free_pages(hostAddr, get_order(len));

	return rv;
}

static loff_t mbcs_sram_llseek(struct file * filp, loff_t off, int whence)
{
	return generic_file_llseek_size(filp, off, whence, MAX_LFS_FILESIZE,
					MBCS_SRAM_SIZE);
}

static uint64_t mbcs_pioaddr(struct mbcs_soft *soft, uint64_t offset)
{
	uint64_t mmr_base;

	mmr_base = (uint64_t) (soft->mmr_base + offset);

	return mmr_base;
}

static void mbcs_debug_pioaddr_set(struct mbcs_soft *soft)
{
	soft->debug_addr = mbcs_pioaddr(soft, MBCS_DEBUG_START);
}

static void mbcs_gscr_pioaddr_set(struct mbcs_soft *soft)
{
	soft->gscr_addr = mbcs_pioaddr(soft, MBCS_GSCR_START);
}

static int mbcs_gscr_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct cx_dev *cx_dev = fp->private_data;
	struct mbcs_soft *soft = cx_dev->soft;

	if (vma->vm_pgoff != 0)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* Remap-pfn-range will mark the range VM_IO */
	if (remap_pfn_range(vma,
			    vma->vm_start,
			    __pa(soft->gscr_addr) >> PAGE_SHIFT,
			    PAGE_SIZE,
			    vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * mbcs_completion_intr_handler - Primary completion handler.
 * @irq: irq
 * @arg: soft struct for device
 *
 */
static irqreturn_t
mbcs_completion_intr_handler(int irq, void *arg)
{
	struct mbcs_soft *soft = (struct mbcs_soft *)arg;
	void *mmr_base;
	union cm_status cm_status;
	union cm_control cm_control;

	mmr_base = soft->mmr_base;
	cm_status.cm_status_reg = MBCS_MMR_GET(mmr_base, MBCS_CM_STATUS);

	if (cm_status.rd_dma_done) {
		/* stop dma-read engine, clear status */
		cm_control.cm_control_reg =
		    MBCS_MMR_GET(mmr_base, MBCS_CM_CONTROL);
		cm_control.rd_dma_clr = 1;
		MBCS_MMR_SET(mmr_base, MBCS_CM_CONTROL,
			     cm_control.cm_control_reg);
		atomic_set(&soft->dmaread_done, 1);
		wake_up(&soft->dmaread_queue);
	}
	if (cm_status.wr_dma_done) {
		/* stop dma-write engine, clear status */
		cm_control.cm_control_reg =
		    MBCS_MMR_GET(mmr_base, MBCS_CM_CONTROL);
		cm_control.wr_dma_clr = 1;
		MBCS_MMR_SET(mmr_base, MBCS_CM_CONTROL,
			     cm_control.cm_control_reg);
		atomic_set(&soft->dmawrite_done, 1);
		wake_up(&soft->dmawrite_queue);
	}
	if (cm_status.alg_done) {
		/* clear status */
		cm_control.cm_control_reg =
		    MBCS_MMR_GET(mmr_base, MBCS_CM_CONTROL);
		cm_control.alg_done_clr = 1;
		MBCS_MMR_SET(mmr_base, MBCS_CM_CONTROL,
			     cm_control.cm_control_reg);
		atomic_set(&soft->algo_done, 1);
		wake_up(&soft->algo_queue);
	}

	return IRQ_HANDLED;
}

/**
 * mbcs_intr_alloc - Allocate interrupts.
 * @dev: device pointer
 *
 */
static int mbcs_intr_alloc(struct cx_dev *dev)
{
	struct sn_irq_info *sn_irq;
	struct mbcs_soft *soft;
	struct getdma *getdma;
	struct putdma *putdma;
	struct algoblock *algo;

	soft = dev->soft;
	getdma = &soft->getdma;
	putdma = &soft->putdma;
	algo = &soft->algo;

	soft->get_sn_irq = NULL;
	soft->put_sn_irq = NULL;
	soft->algo_sn_irq = NULL;

	sn_irq = tiocx_irq_alloc(dev->cx_id.nasid, TIOCX_CORELET, -1, -1, -1);
	if (sn_irq == NULL)
		return -EAGAIN;
	soft->get_sn_irq = sn_irq;
	getdma->intrHostDest = sn_irq->irq_xtalkaddr;
	getdma->intrVector = sn_irq->irq_irq;
	if (request_irq(sn_irq->irq_irq,
			(void *)mbcs_completion_intr_handler, IRQF_SHARED,
			"MBCS get intr", (void *)soft)) {
		tiocx_irq_free(soft->get_sn_irq);
		return -EAGAIN;
	}

	sn_irq = tiocx_irq_alloc(dev->cx_id.nasid, TIOCX_CORELET, -1, -1, -1);
	if (sn_irq == NULL) {
		free_irq(soft->get_sn_irq->irq_irq, soft);
		tiocx_irq_free(soft->get_sn_irq);
		return -EAGAIN;
	}
	soft->put_sn_irq = sn_irq;
	putdma->intrHostDest = sn_irq->irq_xtalkaddr;
	putdma->intrVector = sn_irq->irq_irq;
	if (request_irq(sn_irq->irq_irq,
			(void *)mbcs_completion_intr_handler, IRQF_SHARED,
			"MBCS put intr", (void *)soft)) {
		tiocx_irq_free(soft->put_sn_irq);
		free_irq(soft->get_sn_irq->irq_irq, soft);
		tiocx_irq_free(soft->get_sn_irq);
		return -EAGAIN;
	}

	sn_irq = tiocx_irq_alloc(dev->cx_id.nasid, TIOCX_CORELET, -1, -1, -1);
	if (sn_irq == NULL) {
		free_irq(soft->put_sn_irq->irq_irq, soft);
		tiocx_irq_free(soft->put_sn_irq);
		free_irq(soft->get_sn_irq->irq_irq, soft);
		tiocx_irq_free(soft->get_sn_irq);
		return -EAGAIN;
	}
	soft->algo_sn_irq = sn_irq;
	algo->intrHostDest = sn_irq->irq_xtalkaddr;
	algo->intrVector = sn_irq->irq_irq;
	if (request_irq(sn_irq->irq_irq,
			(void *)mbcs_completion_intr_handler, IRQF_SHARED,
			"MBCS algo intr", (void *)soft)) {
		tiocx_irq_free(soft->algo_sn_irq);
		free_irq(soft->put_sn_irq->irq_irq, soft);
		tiocx_irq_free(soft->put_sn_irq);
		free_irq(soft->get_sn_irq->irq_irq, soft);
		tiocx_irq_free(soft->get_sn_irq);
		return -EAGAIN;
	}

	return 0;
}

/**
 * mbcs_intr_dealloc - Remove interrupts.
 * @dev: device pointer
 *
 */
static void mbcs_intr_dealloc(struct cx_dev *dev)
{
	struct mbcs_soft *soft;

	soft = dev->soft;

	free_irq(soft->get_sn_irq->irq_irq, soft);
	tiocx_irq_free(soft->get_sn_irq);
	free_irq(soft->put_sn_irq->irq_irq, soft);
	tiocx_irq_free(soft->put_sn_irq);
	free_irq(soft->algo_sn_irq->irq_irq, soft);
	tiocx_irq_free(soft->algo_sn_irq);
}

static inline int mbcs_hw_init(struct mbcs_soft *soft)
{
	void *mmr_base = soft->mmr_base;
	union cm_control cm_control;
	union cm_req_timeout cm_req_timeout;
	uint64_t err_stat;

	cm_req_timeout.cm_req_timeout_reg =
	    MBCS_MMR_GET(mmr_base, MBCS_CM_REQ_TOUT);

	cm_req_timeout.time_out = MBCS_CM_CONTROL_REQ_TOUT_MASK;
	MBCS_MMR_SET(mmr_base, MBCS_CM_REQ_TOUT,
		     cm_req_timeout.cm_req_timeout_reg);

	mbcs_gscr_pioaddr_set(soft);
	mbcs_debug_pioaddr_set(soft);

	/* clear errors */
	err_stat = MBCS_MMR_GET(mmr_base, MBCS_CM_ERR_STAT);
	MBCS_MMR_SET(mmr_base, MBCS_CM_CLR_ERR_STAT, err_stat);
	MBCS_MMR_ZERO(mmr_base, MBCS_CM_ERROR_DETAIL1);

	/* enable interrupts */
	/* turn off 2^23 (INT_EN_PIO_REQ_ADDR_INV) */
	MBCS_MMR_SET(mmr_base, MBCS_CM_ERR_INT_EN, 0x3ffffff7e00ffUL);

	/* arm status regs and clear engines */
	cm_control.cm_control_reg = MBCS_MMR_GET(mmr_base, MBCS_CM_CONTROL);
	cm_control.rearm_stat_regs = 1;
	cm_control.alg_clr = 1;
	cm_control.wr_dma_clr = 1;
	cm_control.rd_dma_clr = 1;

	MBCS_MMR_SET(mmr_base, MBCS_CM_CONTROL, cm_control.cm_control_reg);

	return 0;
}

static ssize_t show_algo(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cx_dev *cx_dev = to_cx_dev(dev);
	struct mbcs_soft *soft = cx_dev->soft;
	uint64_t debug0;

	/*
	 * By convention, the first debug register contains the
	 * algorithm number and revision.
	 */
	debug0 = *(uint64_t *) soft->debug_addr;

	return sprintf(buf, "0x%x 0x%x\n",
		       upper_32_bits(debug0), lower_32_bits(debug0));
}

static ssize_t store_algo(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int n;
	struct cx_dev *cx_dev = to_cx_dev(dev);
	struct mbcs_soft *soft = cx_dev->soft;

	if (count <= 0)
		return 0;

	n = simple_strtoul(buf, NULL, 0);

	if (n == 1) {
		mbcs_algo_start(soft);
		if (wait_event_interruptible(soft->algo_queue,
					atomic_read(&soft->algo_done)))
			return -ERESTARTSYS;
	}

	return count;
}

DEVICE_ATTR(algo, 0644, show_algo, store_algo);

/**
 * mbcs_probe - Initialize for device
 * @dev: device pointer
 * @device_id: id table pointer
 *
 */
static int mbcs_probe(struct cx_dev *dev, const struct cx_device_id *id)
{
	struct mbcs_soft *soft;

	dev->soft = NULL;

	soft = kzalloc(sizeof(struct mbcs_soft), GFP_KERNEL);
	if (soft == NULL)
		return -ENOMEM;

	soft->nasid = dev->cx_id.nasid;
	list_add(&soft->list, &soft_list);
	soft->mmr_base = (void *)tiocx_swin_base(dev->cx_id.nasid);
	dev->soft = soft;
	soft->cxdev = dev;

	init_waitqueue_head(&soft->dmawrite_queue);
	init_waitqueue_head(&soft->dmaread_queue);
	init_waitqueue_head(&soft->algo_queue);

	mutex_init(&soft->dmawritelock);
	mutex_init(&soft->dmareadlock);
	mutex_init(&soft->algolock);

	mbcs_getdma_init(&soft->getdma);
	mbcs_putdma_init(&soft->putdma);
	mbcs_algo_init(&soft->algo);

	mbcs_hw_init(soft);

	/* Allocate interrupts */
	mbcs_intr_alloc(dev);

	device_create_file(&dev->dev, &dev_attr_algo);

	return 0;
}

static int mbcs_remove(struct cx_dev *dev)
{
	if (dev->soft) {
		mbcs_intr_dealloc(dev);
		kfree(dev->soft);
	}

	device_remove_file(&dev->dev, &dev_attr_algo);

	return 0;
}

static const struct cx_device_id mbcs_id_table[] = {
	{
	 .part_num = MBCS_PART_NUM,
	 .mfg_num = MBCS_MFG_NUM,
	 },
	{
	 .part_num = MBCS_PART_NUM_ALG0,
	 .mfg_num = MBCS_MFG_NUM,
	 },
	{0, 0}
};

MODULE_DEVICE_TABLE(cx, mbcs_id_table);

static struct cx_drv mbcs_driver = {
	.name = DEVICE_NAME,
	.id_table = mbcs_id_table,
	.probe = mbcs_probe,
	.remove = mbcs_remove,
};

static void __exit mbcs_exit(void)
{
	unregister_chrdev(mbcs_major, DEVICE_NAME);
	cx_driver_unregister(&mbcs_driver);
}

static int __init mbcs_init(void)
{
	int rv;

	if (!ia64_platform_is("sn2"))
		return -ENODEV;

	// Put driver into chrdevs[].  Get major number.
	rv = register_chrdev(mbcs_major, DEVICE_NAME, &mbcs_ops);
	if (rv < 0) {
		DBG(KERN_ALERT "mbcs_init: can't get major number. %d\n", rv);
		return rv;
	}
	mbcs_major = rv;

	return cx_driver_register(&mbcs_driver);
}

module_init(mbcs_init);
module_exit(mbcs_exit);

MODULE_AUTHOR("Bruce Losure <blosure@sgi.com>");
MODULE_DESCRIPTION("Driver for MOATB Core Services");
MODULE_LICENSE("GPL");
