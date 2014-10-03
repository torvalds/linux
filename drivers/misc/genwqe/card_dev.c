/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@de.ibm.com>
 * Author: Michael Ruettger <michael@ibmra.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * Character device representation of the GenWQE device. This allows
 * user-space applications to communicate with the card.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/atomic.h>

#include "card_base.h"
#include "card_ddcb.h"

static int genwqe_open_files(struct genwqe_dev *cd)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&cd->file_lock, flags);
	rc = list_empty(&cd->file_list);
	spin_unlock_irqrestore(&cd->file_lock, flags);
	return !rc;
}

static void genwqe_add_file(struct genwqe_dev *cd, struct genwqe_file *cfile)
{
	unsigned long flags;

	cfile->owner = current;
	spin_lock_irqsave(&cd->file_lock, flags);
	list_add(&cfile->list, &cd->file_list);
	spin_unlock_irqrestore(&cd->file_lock, flags);
}

static int genwqe_del_file(struct genwqe_dev *cd, struct genwqe_file *cfile)
{
	unsigned long flags;

	spin_lock_irqsave(&cd->file_lock, flags);
	list_del(&cfile->list);
	spin_unlock_irqrestore(&cd->file_lock, flags);

	return 0;
}

static void genwqe_add_pin(struct genwqe_file *cfile, struct dma_mapping *m)
{
	unsigned long flags;

	spin_lock_irqsave(&cfile->pin_lock, flags);
	list_add(&m->pin_list, &cfile->pin_list);
	spin_unlock_irqrestore(&cfile->pin_lock, flags);
}

static int genwqe_del_pin(struct genwqe_file *cfile, struct dma_mapping *m)
{
	unsigned long flags;

	spin_lock_irqsave(&cfile->pin_lock, flags);
	list_del(&m->pin_list);
	spin_unlock_irqrestore(&cfile->pin_lock, flags);

	return 0;
}

/**
 * genwqe_search_pin() - Search for the mapping for a userspace address
 * @cfile:	Descriptor of opened file
 * @u_addr:	User virtual address
 * @size:	Size of buffer
 * @dma_addr:	DMA address to be updated
 *
 * Return: Pointer to the corresponding mapping	NULL if not found
 */
static struct dma_mapping *genwqe_search_pin(struct genwqe_file *cfile,
					    unsigned long u_addr,
					    unsigned int size,
					    void **virt_addr)
{
	unsigned long flags;
	struct dma_mapping *m;

	spin_lock_irqsave(&cfile->pin_lock, flags);

	list_for_each_entry(m, &cfile->pin_list, pin_list) {
		if ((((u64)m->u_vaddr) <= (u_addr)) &&
		    (((u64)m->u_vaddr + m->size) >= (u_addr + size))) {

			if (virt_addr)
				*virt_addr = m->k_vaddr +
					(u_addr - (u64)m->u_vaddr);

			spin_unlock_irqrestore(&cfile->pin_lock, flags);
			return m;
		}
	}
	spin_unlock_irqrestore(&cfile->pin_lock, flags);
	return NULL;
}

static void __genwqe_add_mapping(struct genwqe_file *cfile,
			      struct dma_mapping *dma_map)
{
	unsigned long flags;

	spin_lock_irqsave(&cfile->map_lock, flags);
	list_add(&dma_map->card_list, &cfile->map_list);
	spin_unlock_irqrestore(&cfile->map_lock, flags);
}

static void __genwqe_del_mapping(struct genwqe_file *cfile,
			      struct dma_mapping *dma_map)
{
	unsigned long flags;

	spin_lock_irqsave(&cfile->map_lock, flags);
	list_del(&dma_map->card_list);
	spin_unlock_irqrestore(&cfile->map_lock, flags);
}


/**
 * __genwqe_search_mapping() - Search for the mapping for a userspace address
 * @cfile:	descriptor of opened file
 * @u_addr:	user virtual address
 * @size:	size of buffer
 * @dma_addr:	DMA address to be updated
 * Return: Pointer to the corresponding mapping	NULL if not found
 */
static struct dma_mapping *__genwqe_search_mapping(struct genwqe_file *cfile,
						   unsigned long u_addr,
						   unsigned int size,
						   dma_addr_t *dma_addr,
						   void **virt_addr)
{
	unsigned long flags;
	struct dma_mapping *m;
	struct pci_dev *pci_dev = cfile->cd->pci_dev;

	spin_lock_irqsave(&cfile->map_lock, flags);
	list_for_each_entry(m, &cfile->map_list, card_list) {

		if ((((u64)m->u_vaddr) <= (u_addr)) &&
		    (((u64)m->u_vaddr + m->size) >= (u_addr + size))) {

			/* match found: current is as expected and
			   addr is in range */
			if (dma_addr)
				*dma_addr = m->dma_addr +
					(u_addr - (u64)m->u_vaddr);

			if (virt_addr)
				*virt_addr = m->k_vaddr +
					(u_addr - (u64)m->u_vaddr);

			spin_unlock_irqrestore(&cfile->map_lock, flags);
			return m;
		}
	}
	spin_unlock_irqrestore(&cfile->map_lock, flags);

	dev_err(&pci_dev->dev,
		"[%s] Entry not found: u_addr=%lx, size=%x\n",
		__func__, u_addr, size);

	return NULL;
}

static void genwqe_remove_mappings(struct genwqe_file *cfile)
{
	int i = 0;
	struct list_head *node, *next;
	struct dma_mapping *dma_map;
	struct genwqe_dev *cd = cfile->cd;
	struct pci_dev *pci_dev = cfile->cd->pci_dev;

	list_for_each_safe(node, next, &cfile->map_list) {
		dma_map = list_entry(node, struct dma_mapping, card_list);

		list_del_init(&dma_map->card_list);

		/*
		 * This is really a bug, because those things should
		 * have been already tidied up.
		 *
		 * GENWQE_MAPPING_RAW should have been removed via mmunmap().
		 * GENWQE_MAPPING_SGL_TEMP should be removed by tidy up code.
		 */
		dev_err(&pci_dev->dev,
			"[%s] %d. cleanup mapping: u_vaddr=%p "
			"u_kaddr=%016lx dma_addr=%lx\n", __func__, i++,
			dma_map->u_vaddr, (unsigned long)dma_map->k_vaddr,
			(unsigned long)dma_map->dma_addr);

		if (dma_map->type == GENWQE_MAPPING_RAW) {
			/* we allocated this dynamically */
			__genwqe_free_consistent(cd, dma_map->size,
						dma_map->k_vaddr,
						dma_map->dma_addr);
			kfree(dma_map);
		} else if (dma_map->type == GENWQE_MAPPING_SGL_TEMP) {
			/* we use dma_map statically from the request */
			genwqe_user_vunmap(cd, dma_map, NULL);
		}
	}
}

static void genwqe_remove_pinnings(struct genwqe_file *cfile)
{
	struct list_head *node, *next;
	struct dma_mapping *dma_map;
	struct genwqe_dev *cd = cfile->cd;

	list_for_each_safe(node, next, &cfile->pin_list) {
		dma_map = list_entry(node, struct dma_mapping, pin_list);

		/*
		 * This is not a bug, because a killed processed might
		 * not call the unpin ioctl, which is supposed to free
		 * the resources.
		 *
		 * Pinnings are dymically allocated and need to be
		 * deleted.
		 */
		list_del_init(&dma_map->pin_list);
		genwqe_user_vunmap(cd, dma_map, NULL);
		kfree(dma_map);
	}
}

/**
 * genwqe_kill_fasync() - Send signal to all processes with open GenWQE files
 *
 * E.g. genwqe_send_signal(cd, SIGIO);
 */
static int genwqe_kill_fasync(struct genwqe_dev *cd, int sig)
{
	unsigned int files = 0;
	unsigned long flags;
	struct genwqe_file *cfile;

	spin_lock_irqsave(&cd->file_lock, flags);
	list_for_each_entry(cfile, &cd->file_list, list) {
		if (cfile->async_queue)
			kill_fasync(&cfile->async_queue, sig, POLL_HUP);
		files++;
	}
	spin_unlock_irqrestore(&cd->file_lock, flags);
	return files;
}

static int genwqe_force_sig(struct genwqe_dev *cd, int sig)
{
	unsigned int files = 0;
	unsigned long flags;
	struct genwqe_file *cfile;

	spin_lock_irqsave(&cd->file_lock, flags);
	list_for_each_entry(cfile, &cd->file_list, list) {
		force_sig(sig, cfile->owner);
		files++;
	}
	spin_unlock_irqrestore(&cd->file_lock, flags);
	return files;
}

/**
 * genwqe_open() - file open
 * @inode:      file system information
 * @filp:	file handle
 *
 * This function is executed whenever an application calls
 * open("/dev/genwqe",..).
 *
 * Return: 0 if successful or <0 if errors
 */
static int genwqe_open(struct inode *inode, struct file *filp)
{
	struct genwqe_dev *cd;
	struct genwqe_file *cfile;
	struct pci_dev *pci_dev;

	cfile = kzalloc(sizeof(*cfile), GFP_KERNEL);
	if (cfile == NULL)
		return -ENOMEM;

	cd = container_of(inode->i_cdev, struct genwqe_dev, cdev_genwqe);
	pci_dev = cd->pci_dev;
	cfile->cd = cd;
	cfile->filp = filp;
	cfile->client = NULL;

	spin_lock_init(&cfile->map_lock);  /* list of raw memory allocations */
	INIT_LIST_HEAD(&cfile->map_list);

	spin_lock_init(&cfile->pin_lock);  /* list of user pinned memory */
	INIT_LIST_HEAD(&cfile->pin_list);

	filp->private_data = cfile;

	genwqe_add_file(cd, cfile);
	return 0;
}

/**
 * genwqe_fasync() - Setup process to receive SIGIO.
 * @fd:        file descriptor
 * @filp:      file handle
 * @mode:      file mode
 *
 * Sending a signal is working as following:
 *
 * if (cdev->async_queue)
 *         kill_fasync(&cdev->async_queue, SIGIO, POLL_IN);
 *
 * Some devices also implement asynchronous notification to indicate
 * when the device can be written; in this case, of course,
 * kill_fasync must be called with a mode of POLL_OUT.
 */
static int genwqe_fasync(int fd, struct file *filp, int mode)
{
	struct genwqe_file *cdev = (struct genwqe_file *)filp->private_data;
	return fasync_helper(fd, filp, mode, &cdev->async_queue);
}


/**
 * genwqe_release() - file close
 * @inode:      file system information
 * @filp:       file handle
 *
 * This function is executed whenever an application calls 'close(fd_genwqe)'
 *
 * Return: always 0
 */
static int genwqe_release(struct inode *inode, struct file *filp)
{
	struct genwqe_file *cfile = (struct genwqe_file *)filp->private_data;
	struct genwqe_dev *cd = cfile->cd;

	/* there must be no entries in these lists! */
	genwqe_remove_mappings(cfile);
	genwqe_remove_pinnings(cfile);

	/* remove this filp from the asynchronously notified filp's */
	genwqe_fasync(-1, filp, 0);

	/*
	 * For this to work we must not release cd when this cfile is
	 * not yet released, otherwise the list entry is invalid,
	 * because the list itself gets reinstantiated!
	 */
	genwqe_del_file(cd, cfile);
	kfree(cfile);
	return 0;
}

static void genwqe_vma_open(struct vm_area_struct *vma)
{
	/* nothing ... */
}

/**
 * genwqe_vma_close() - Called each time when vma is unmapped
 *
 * Free memory which got allocated by GenWQE mmap().
 */
static void genwqe_vma_close(struct vm_area_struct *vma)
{
	unsigned long vsize = vma->vm_end - vma->vm_start;
	struct inode *inode = vma->vm_file->f_dentry->d_inode;
	struct dma_mapping *dma_map;
	struct genwqe_dev *cd = container_of(inode->i_cdev, struct genwqe_dev,
					    cdev_genwqe);
	struct pci_dev *pci_dev = cd->pci_dev;
	dma_addr_t d_addr = 0;
	struct genwqe_file *cfile = vma->vm_private_data;

	dma_map = __genwqe_search_mapping(cfile, vma->vm_start, vsize,
					 &d_addr, NULL);
	if (dma_map == NULL) {
		dev_err(&pci_dev->dev,
			"  [%s] err: mapping not found: v=%lx, p=%lx s=%lx\n",
			__func__, vma->vm_start, vma->vm_pgoff << PAGE_SHIFT,
			vsize);
		return;
	}
	__genwqe_del_mapping(cfile, dma_map);
	__genwqe_free_consistent(cd, dma_map->size, dma_map->k_vaddr,
				 dma_map->dma_addr);
	kfree(dma_map);
}

static struct vm_operations_struct genwqe_vma_ops = {
	.open   = genwqe_vma_open,
	.close  = genwqe_vma_close,
};

/**
 * genwqe_mmap() - Provide contignous buffers to userspace
 *
 * We use mmap() to allocate contignous buffers used for DMA
 * transfers. After the buffer is allocated we remap it to user-space
 * and remember a reference to our dma_mapping data structure, where
 * we store the associated DMA address and allocated size.
 *
 * When we receive a DDCB execution request with the ATS bits set to
 * plain buffer, we lookup our dma_mapping list to find the
 * corresponding DMA address for the associated user-space address.
 */
static int genwqe_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int rc;
	unsigned long pfn, vsize = vma->vm_end - vma->vm_start;
	struct genwqe_file *cfile = (struct genwqe_file *)filp->private_data;
	struct genwqe_dev *cd = cfile->cd;
	struct dma_mapping *dma_map;

	if (vsize == 0)
		return -EINVAL;

	if (get_order(vsize) > MAX_ORDER)
		return -ENOMEM;

	dma_map = kzalloc(sizeof(struct dma_mapping), GFP_ATOMIC);
	if (dma_map == NULL)
		return -ENOMEM;

	genwqe_mapping_init(dma_map, GENWQE_MAPPING_RAW);
	dma_map->u_vaddr = (void *)vma->vm_start;
	dma_map->size = vsize;
	dma_map->nr_pages = DIV_ROUND_UP(vsize, PAGE_SIZE);
	dma_map->k_vaddr = __genwqe_alloc_consistent(cd, vsize,
						     &dma_map->dma_addr);
	if (dma_map->k_vaddr == NULL) {
		rc = -ENOMEM;
		goto free_dma_map;
	}

	if (capable(CAP_SYS_ADMIN) && (vsize > sizeof(dma_addr_t)))
		*(dma_addr_t *)dma_map->k_vaddr = dma_map->dma_addr;

	pfn = virt_to_phys(dma_map->k_vaddr) >> PAGE_SHIFT;
	rc = remap_pfn_range(vma,
			     vma->vm_start,
			     pfn,
			     vsize,
			     vma->vm_page_prot);
	if (rc != 0) {
		rc = -EFAULT;
		goto free_dma_mem;
	}

	vma->vm_private_data = cfile;
	vma->vm_ops = &genwqe_vma_ops;
	__genwqe_add_mapping(cfile, dma_map);

	return 0;

 free_dma_mem:
	__genwqe_free_consistent(cd, dma_map->size,
				dma_map->k_vaddr,
				dma_map->dma_addr);
 free_dma_map:
	kfree(dma_map);
	return rc;
}

/**
 * do_flash_update() - Excute flash update (write image or CVPD)
 * @cd:        genwqe device
 * @load:      details about image load
 *
 * Return: 0 if successful
 */

#define	FLASH_BLOCK	0x40000	/* we use 256k blocks */

static int do_flash_update(struct genwqe_file *cfile,
			   struct genwqe_bitstream *load)
{
	int rc = 0;
	int blocks_to_flash;
	dma_addr_t dma_addr;
	u64 flash = 0;
	size_t tocopy = 0;
	u8 __user *buf;
	u8 *xbuf;
	u32 crc;
	u8 cmdopts;
	struct genwqe_dev *cd = cfile->cd;
	struct pci_dev *pci_dev = cd->pci_dev;

	if ((load->size & 0x3) != 0)
		return -EINVAL;

	if (((unsigned long)(load->data_addr) & ~PAGE_MASK) != 0)
		return -EINVAL;

	/* FIXME Bits have changed for new service layer! */
	switch ((char)load->partition) {
	case '0':
		cmdopts = 0x14;
		break;		/* download/erase_first/part_0 */
	case '1':
		cmdopts = 0x1C;
		break;		/* download/erase_first/part_1 */
	case 'v':
		cmdopts = 0x0C;
		break;		/* download/erase_first/vpd */
	default:
		return -EINVAL;
	}

	buf = (u8 __user *)load->data_addr;
	xbuf = __genwqe_alloc_consistent(cd, FLASH_BLOCK, &dma_addr);
	if (xbuf == NULL)
		return -ENOMEM;

	blocks_to_flash = load->size / FLASH_BLOCK;
	while (load->size) {
		struct genwqe_ddcb_cmd *req;

		/*
		 * We must be 4 byte aligned. Buffer must be 0 appened
		 * to have defined values when calculating CRC.
		 */
		tocopy = min_t(size_t, load->size, FLASH_BLOCK);

		rc = copy_from_user(xbuf, buf, tocopy);
		if (rc) {
			rc = -EFAULT;
			goto free_buffer;
		}
		crc = genwqe_crc32(xbuf, tocopy, 0xffffffff);

		dev_dbg(&pci_dev->dev,
			"[%s] DMA: %lx CRC: %08x SZ: %ld %d\n",
			__func__, (unsigned long)dma_addr, crc, tocopy,
			blocks_to_flash);

		/* prepare DDCB for SLU process */
		req = ddcb_requ_alloc();
		if (req == NULL) {
			rc = -ENOMEM;
			goto free_buffer;
		}

		req->cmd = SLCMD_MOVE_FLASH;
		req->cmdopts = cmdopts;

		/* prepare invariant values */
		if (genwqe_get_slu_id(cd) <= 0x2) {
			*(__be64 *)&req->__asiv[0]  = cpu_to_be64(dma_addr);
			*(__be64 *)&req->__asiv[8]  = cpu_to_be64(tocopy);
			*(__be64 *)&req->__asiv[16] = cpu_to_be64(flash);
			*(__be32 *)&req->__asiv[24] = cpu_to_be32(0);
			req->__asiv[24]	       = load->uid;
			*(__be32 *)&req->__asiv[28] = cpu_to_be32(crc);

			/* for simulation only */
			*(__be64 *)&req->__asiv[88] = cpu_to_be64(load->slu_id);
			*(__be64 *)&req->__asiv[96] = cpu_to_be64(load->app_id);
			req->asiv_length = 32; /* bytes included in crc calc */
		} else {	/* setup DDCB for ATS architecture */
			*(__be64 *)&req->asiv[0]  = cpu_to_be64(dma_addr);
			*(__be32 *)&req->asiv[8]  = cpu_to_be32(tocopy);
			*(__be32 *)&req->asiv[12] = cpu_to_be32(0); /* resvd */
			*(__be64 *)&req->asiv[16] = cpu_to_be64(flash);
			*(__be32 *)&req->asiv[24] = cpu_to_be32(load->uid<<24);
			*(__be32 *)&req->asiv[28] = cpu_to_be32(crc);

			/* for simulation only */
			*(__be64 *)&req->asiv[80] = cpu_to_be64(load->slu_id);
			*(__be64 *)&req->asiv[88] = cpu_to_be64(load->app_id);

			/* Rd only */
			req->ats = 0x4ULL << 44;
			req->asiv_length = 40; /* bytes included in crc calc */
		}
		req->asv_length  = 8;

		/* For Genwqe5 we get back the calculated CRC */
		*(u64 *)&req->asv[0] = 0ULL;			/* 0x80 */

		rc = __genwqe_execute_raw_ddcb(cd, req);

		load->retc = req->retc;
		load->attn = req->attn;
		load->progress = req->progress;

		if (rc < 0) {
			ddcb_requ_free(req);
			goto free_buffer;
		}

		if (req->retc != DDCB_RETC_COMPLETE) {
			rc = -EIO;
			ddcb_requ_free(req);
			goto free_buffer;
		}

		load->size  -= tocopy;
		flash += tocopy;
		buf += tocopy;
		blocks_to_flash--;
		ddcb_requ_free(req);
	}

 free_buffer:
	__genwqe_free_consistent(cd, FLASH_BLOCK, xbuf, dma_addr);
	return rc;
}

static int do_flash_read(struct genwqe_file *cfile,
			 struct genwqe_bitstream *load)
{
	int rc, blocks_to_flash;
	dma_addr_t dma_addr;
	u64 flash = 0;
	size_t tocopy = 0;
	u8 __user *buf;
	u8 *xbuf;
	u8 cmdopts;
	struct genwqe_dev *cd = cfile->cd;
	struct pci_dev *pci_dev = cd->pci_dev;
	struct genwqe_ddcb_cmd *cmd;

	if ((load->size & 0x3) != 0)
		return -EINVAL;

	if (((unsigned long)(load->data_addr) & ~PAGE_MASK) != 0)
		return -EINVAL;

	/* FIXME Bits have changed for new service layer! */
	switch ((char)load->partition) {
	case '0':
		cmdopts = 0x12;
		break;		/* upload/part_0 */
	case '1':
		cmdopts = 0x1A;
		break;		/* upload/part_1 */
	case 'v':
		cmdopts = 0x0A;
		break;		/* upload/vpd */
	default:
		return -EINVAL;
	}

	buf = (u8 __user *)load->data_addr;
	xbuf = __genwqe_alloc_consistent(cd, FLASH_BLOCK, &dma_addr);
	if (xbuf == NULL)
		return -ENOMEM;

	blocks_to_flash = load->size / FLASH_BLOCK;
	while (load->size) {
		/*
		 * We must be 4 byte aligned. Buffer must be 0 appened
		 * to have defined values when calculating CRC.
		 */
		tocopy = min_t(size_t, load->size, FLASH_BLOCK);

		dev_dbg(&pci_dev->dev,
			"[%s] DMA: %lx SZ: %ld %d\n",
			__func__, (unsigned long)dma_addr, tocopy,
			blocks_to_flash);

		/* prepare DDCB for SLU process */
		cmd = ddcb_requ_alloc();
		if (cmd == NULL) {
			rc = -ENOMEM;
			goto free_buffer;
		}
		cmd->cmd = SLCMD_MOVE_FLASH;
		cmd->cmdopts = cmdopts;

		/* prepare invariant values */
		if (genwqe_get_slu_id(cd) <= 0x2) {
			*(__be64 *)&cmd->__asiv[0]  = cpu_to_be64(dma_addr);
			*(__be64 *)&cmd->__asiv[8]  = cpu_to_be64(tocopy);
			*(__be64 *)&cmd->__asiv[16] = cpu_to_be64(flash);
			*(__be32 *)&cmd->__asiv[24] = cpu_to_be32(0);
			cmd->__asiv[24] = load->uid;
			*(__be32 *)&cmd->__asiv[28] = cpu_to_be32(0) /* CRC */;
			cmd->asiv_length = 32; /* bytes included in crc calc */
		} else {	/* setup DDCB for ATS architecture */
			*(__be64 *)&cmd->asiv[0]  = cpu_to_be64(dma_addr);
			*(__be32 *)&cmd->asiv[8]  = cpu_to_be32(tocopy);
			*(__be32 *)&cmd->asiv[12] = cpu_to_be32(0); /* resvd */
			*(__be64 *)&cmd->asiv[16] = cpu_to_be64(flash);
			*(__be32 *)&cmd->asiv[24] = cpu_to_be32(load->uid<<24);
			*(__be32 *)&cmd->asiv[28] = cpu_to_be32(0); /* CRC */

			/* rd/wr */
			cmd->ats = 0x5ULL << 44;
			cmd->asiv_length = 40; /* bytes included in crc calc */
		}
		cmd->asv_length  = 8;

		/* we only get back the calculated CRC */
		*(u64 *)&cmd->asv[0] = 0ULL;	/* 0x80 */

		rc = __genwqe_execute_raw_ddcb(cd, cmd);

		load->retc = cmd->retc;
		load->attn = cmd->attn;
		load->progress = cmd->progress;

		if ((rc < 0) && (rc != -EBADMSG)) {
			ddcb_requ_free(cmd);
			goto free_buffer;
		}

		rc = copy_to_user(buf, xbuf, tocopy);
		if (rc) {
			rc = -EFAULT;
			ddcb_requ_free(cmd);
			goto free_buffer;
		}

		/* We know that we can get retc 0x104 with CRC err */
		if (((cmd->retc == DDCB_RETC_FAULT) &&
		     (cmd->attn != 0x02)) ||  /* Normally ignore CRC error */
		    ((cmd->retc == DDCB_RETC_COMPLETE) &&
		     (cmd->attn != 0x00))) {  /* Everything was fine */
			rc = -EIO;
			ddcb_requ_free(cmd);
			goto free_buffer;
		}

		load->size  -= tocopy;
		flash += tocopy;
		buf += tocopy;
		blocks_to_flash--;
		ddcb_requ_free(cmd);
	}
	rc = 0;

 free_buffer:
	__genwqe_free_consistent(cd, FLASH_BLOCK, xbuf, dma_addr);
	return rc;
}

static int genwqe_pin_mem(struct genwqe_file *cfile, struct genwqe_mem *m)
{
	int rc;
	struct genwqe_dev *cd = cfile->cd;
	struct pci_dev *pci_dev = cfile->cd->pci_dev;
	struct dma_mapping *dma_map;
	unsigned long map_addr;
	unsigned long map_size;

	if ((m->addr == 0x0) || (m->size == 0))
		return -EINVAL;

	map_addr = (m->addr & PAGE_MASK);
	map_size = round_up(m->size + (m->addr & ~PAGE_MASK), PAGE_SIZE);

	dma_map = kzalloc(sizeof(struct dma_mapping), GFP_ATOMIC);
	if (dma_map == NULL)
		return -ENOMEM;

	genwqe_mapping_init(dma_map, GENWQE_MAPPING_SGL_PINNED);
	rc = genwqe_user_vmap(cd, dma_map, (void *)map_addr, map_size, NULL);
	if (rc != 0) {
		dev_err(&pci_dev->dev,
			"[%s] genwqe_user_vmap rc=%d\n", __func__, rc);
		kfree(dma_map);
		return rc;
	}

	genwqe_add_pin(cfile, dma_map);
	return 0;
}

static int genwqe_unpin_mem(struct genwqe_file *cfile, struct genwqe_mem *m)
{
	struct genwqe_dev *cd = cfile->cd;
	struct dma_mapping *dma_map;
	unsigned long map_addr;
	unsigned long map_size;

	if (m->addr == 0x0)
		return -EINVAL;

	map_addr = (m->addr & PAGE_MASK);
	map_size = round_up(m->size + (m->addr & ~PAGE_MASK), PAGE_SIZE);

	dma_map = genwqe_search_pin(cfile, map_addr, map_size, NULL);
	if (dma_map == NULL)
		return -ENOENT;

	genwqe_del_pin(cfile, dma_map);
	genwqe_user_vunmap(cd, dma_map, NULL);
	kfree(dma_map);
	return 0;
}

/**
 * ddcb_cmd_cleanup() - Remove dynamically created fixup entries
 *
 * Only if there are any. Pinnings are not removed.
 */
static int ddcb_cmd_cleanup(struct genwqe_file *cfile, struct ddcb_requ *req)
{
	unsigned int i;
	struct dma_mapping *dma_map;
	struct genwqe_dev *cd = cfile->cd;

	for (i = 0; i < DDCB_FIXUPS; i++) {
		dma_map = &req->dma_mappings[i];

		if (dma_mapping_used(dma_map)) {
			__genwqe_del_mapping(cfile, dma_map);
			genwqe_user_vunmap(cd, dma_map, req);
		}
		if (req->sgls[i].sgl != NULL)
			genwqe_free_sync_sgl(cd, &req->sgls[i]);
	}
	return 0;
}

/**
 * ddcb_cmd_fixups() - Establish DMA fixups/sglists for user memory references
 *
 * Before the DDCB gets executed we need to handle the fixups. We
 * replace the user-space addresses with DMA addresses or do
 * additional setup work e.g. generating a scatter-gather list which
 * is used to describe the memory referred to in the fixup.
 */
static int ddcb_cmd_fixups(struct genwqe_file *cfile, struct ddcb_requ *req)
{
	int rc;
	unsigned int asiv_offs, i;
	struct genwqe_dev *cd = cfile->cd;
	struct genwqe_ddcb_cmd *cmd = &req->cmd;
	struct dma_mapping *m;
	const char *type = "UNKNOWN";

	for (i = 0, asiv_offs = 0x00; asiv_offs <= 0x58;
	     i++, asiv_offs += 0x08) {

		u64 u_addr;
		dma_addr_t d_addr;
		u32 u_size = 0;
		u64 ats_flags;

		ats_flags = ATS_GET_FLAGS(cmd->ats, asiv_offs);

		switch (ats_flags) {

		case ATS_TYPE_DATA:
			break;	/* nothing to do here */

		case ATS_TYPE_FLAT_RDWR:
		case ATS_TYPE_FLAT_RD: {
			u_addr = be64_to_cpu(*((__be64 *)&cmd->
					       asiv[asiv_offs]));
			u_size = be32_to_cpu(*((__be32 *)&cmd->
					       asiv[asiv_offs + 0x08]));

			/*
			 * No data available. Ignore u_addr in this
			 * case and set addr to 0. Hardware must not
			 * fetch the buffer.
			 */
			if (u_size == 0x0) {
				*((__be64 *)&cmd->asiv[asiv_offs]) =
					cpu_to_be64(0x0);
				break;
			}

			m = __genwqe_search_mapping(cfile, u_addr, u_size,
						   &d_addr, NULL);
			if (m == NULL) {
				rc = -EFAULT;
				goto err_out;
			}

			*((__be64 *)&cmd->asiv[asiv_offs]) =
				cpu_to_be64(d_addr);
			break;
		}

		case ATS_TYPE_SGL_RDWR:
		case ATS_TYPE_SGL_RD: {
			int page_offs;

			u_addr = be64_to_cpu(*((__be64 *)
					       &cmd->asiv[asiv_offs]));
			u_size = be32_to_cpu(*((__be32 *)
					       &cmd->asiv[asiv_offs + 0x08]));

			/*
			 * No data available. Ignore u_addr in this
			 * case and set addr to 0. Hardware must not
			 * fetch the empty sgl.
			 */
			if (u_size == 0x0) {
				*((__be64 *)&cmd->asiv[asiv_offs]) =
					cpu_to_be64(0x0);
				break;
			}

			m = genwqe_search_pin(cfile, u_addr, u_size, NULL);
			if (m != NULL) {
				type = "PINNING";
				page_offs = (u_addr -
					     (u64)m->u_vaddr)/PAGE_SIZE;
			} else {
				type = "MAPPING";
				m = &req->dma_mappings[i];

				genwqe_mapping_init(m,
						    GENWQE_MAPPING_SGL_TEMP);
				rc = genwqe_user_vmap(cd, m, (void *)u_addr,
						      u_size, req);
				if (rc != 0)
					goto err_out;

				__genwqe_add_mapping(cfile, m);
				page_offs = 0;
			}

			/* create genwqe style scatter gather list */
			rc = genwqe_alloc_sync_sgl(cd, &req->sgls[i],
						   (void __user *)u_addr,
						   u_size);
			if (rc != 0)
				goto err_out;

			genwqe_setup_sgl(cd, &req->sgls[i],
					 &m->dma_list[page_offs]);

			*((__be64 *)&cmd->asiv[asiv_offs]) =
				cpu_to_be64(req->sgls[i].sgl_dma_addr);

			break;
		}
		default:
			rc = -EINVAL;
			goto err_out;
		}
	}
	return 0;

 err_out:
	ddcb_cmd_cleanup(cfile, req);
	return rc;
}

/**
 * genwqe_execute_ddcb() - Execute DDCB using userspace address fixups
 *
 * The code will build up the translation tables or lookup the
 * contignous memory allocation table to find the right translations
 * and DMA addresses.
 */
static int genwqe_execute_ddcb(struct genwqe_file *cfile,
			       struct genwqe_ddcb_cmd *cmd)
{
	int rc;
	struct genwqe_dev *cd = cfile->cd;
	struct ddcb_requ *req = container_of(cmd, struct ddcb_requ, cmd);

	rc = ddcb_cmd_fixups(cfile, req);
	if (rc != 0)
		return rc;

	rc = __genwqe_execute_raw_ddcb(cd, cmd);
	ddcb_cmd_cleanup(cfile, req);
	return rc;
}

static int do_execute_ddcb(struct genwqe_file *cfile,
			   unsigned long arg, int raw)
{
	int rc;
	struct genwqe_ddcb_cmd *cmd;
	struct ddcb_requ *req;
	struct genwqe_dev *cd = cfile->cd;

	cmd = ddcb_requ_alloc();
	if (cmd == NULL)
		return -ENOMEM;

	req = container_of(cmd, struct ddcb_requ, cmd);

	if (copy_from_user(cmd, (void __user *)arg, sizeof(*cmd))) {
		ddcb_requ_free(cmd);
		return -EFAULT;
	}

	if (!raw)
		rc = genwqe_execute_ddcb(cfile, cmd);
	else
		rc = __genwqe_execute_raw_ddcb(cd, cmd);

	/* Copy back only the modifed fields. Do not copy ASIV
	   back since the copy got modified by the driver. */
	if (copy_to_user((void __user *)arg, cmd,
			 sizeof(*cmd) - DDCB_ASIV_LENGTH)) {
		ddcb_requ_free(cmd);
		return -EFAULT;
	}

	ddcb_requ_free(cmd);
	return rc;
}

/**
 * genwqe_ioctl() - IO control
 * @filp:       file handle
 * @cmd:        command identifier (passed from user)
 * @arg:        argument (passed from user)
 *
 * Return: 0 success
 */
static long genwqe_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	int rc = 0;
	struct genwqe_file *cfile = (struct genwqe_file *)filp->private_data;
	struct genwqe_dev *cd = cfile->cd;
	struct pci_dev *pci_dev = cd->pci_dev;
	struct genwqe_reg_io __user *io;
	u64 val;
	u32 reg_offs;

	/* Return -EIO if card hit EEH */
	if (pci_channel_offline(pci_dev))
		return -EIO;

	if (_IOC_TYPE(cmd) != GENWQE_IOC_CODE)
		return -EINVAL;

	switch (cmd) {

	case GENWQE_GET_CARD_STATE:
		put_user(cd->card_state, (enum genwqe_card_state __user *)arg);
		return 0;

		/* Register access */
	case GENWQE_READ_REG64: {
		io = (struct genwqe_reg_io __user *)arg;

		if (get_user(reg_offs, &io->num))
			return -EFAULT;

		if ((reg_offs >= cd->mmio_len) || (reg_offs & 0x7))
			return -EINVAL;

		val = __genwqe_readq(cd, reg_offs);
		put_user(val, &io->val64);
		return 0;
	}

	case GENWQE_WRITE_REG64: {
		io = (struct genwqe_reg_io __user *)arg;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if ((filp->f_flags & O_ACCMODE) == O_RDONLY)
			return -EPERM;

		if (get_user(reg_offs, &io->num))
			return -EFAULT;

		if ((reg_offs >= cd->mmio_len) || (reg_offs & 0x7))
			return -EINVAL;

		if (get_user(val, &io->val64))
			return -EFAULT;

		__genwqe_writeq(cd, reg_offs, val);
		return 0;
	}

	case GENWQE_READ_REG32: {
		io = (struct genwqe_reg_io __user *)arg;

		if (get_user(reg_offs, &io->num))
			return -EFAULT;

		if ((reg_offs >= cd->mmio_len) || (reg_offs & 0x3))
			return -EINVAL;

		val = __genwqe_readl(cd, reg_offs);
		put_user(val, &io->val64);
		return 0;
	}

	case GENWQE_WRITE_REG32: {
		io = (struct genwqe_reg_io __user *)arg;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if ((filp->f_flags & O_ACCMODE) == O_RDONLY)
			return -EPERM;

		if (get_user(reg_offs, &io->num))
			return -EFAULT;

		if ((reg_offs >= cd->mmio_len) || (reg_offs & 0x3))
			return -EINVAL;

		if (get_user(val, &io->val64))
			return -EFAULT;

		__genwqe_writel(cd, reg_offs, val);
		return 0;
	}

		/* Flash update/reading */
	case GENWQE_SLU_UPDATE: {
		struct genwqe_bitstream load;

		if (!genwqe_is_privileged(cd))
			return -EPERM;

		if ((filp->f_flags & O_ACCMODE) == O_RDONLY)
			return -EPERM;

		if (copy_from_user(&load, (void __user *)arg,
				   sizeof(load)))
			return -EFAULT;

		rc = do_flash_update(cfile, &load);

		if (copy_to_user((void __user *)arg, &load, sizeof(load)))
			return -EFAULT;

		return rc;
	}

	case GENWQE_SLU_READ: {
		struct genwqe_bitstream load;

		if (!genwqe_is_privileged(cd))
			return -EPERM;

		if (genwqe_flash_readback_fails(cd))
			return -ENOSPC;	 /* known to fail for old versions */

		if (copy_from_user(&load, (void __user *)arg, sizeof(load)))
			return -EFAULT;

		rc = do_flash_read(cfile, &load);

		if (copy_to_user((void __user *)arg, &load, sizeof(load)))
			return -EFAULT;

		return rc;
	}

		/* memory pinning and unpinning */
	case GENWQE_PIN_MEM: {
		struct genwqe_mem m;

		if (copy_from_user(&m, (void __user *)arg, sizeof(m)))
			return -EFAULT;

		return genwqe_pin_mem(cfile, &m);
	}

	case GENWQE_UNPIN_MEM: {
		struct genwqe_mem m;

		if (copy_from_user(&m, (void __user *)arg, sizeof(m)))
			return -EFAULT;

		return genwqe_unpin_mem(cfile, &m);
	}

		/* launch an DDCB and wait for completion */
	case GENWQE_EXECUTE_DDCB:
		return do_execute_ddcb(cfile, arg, 0);

	case GENWQE_EXECUTE_RAW_DDCB: {

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		return do_execute_ddcb(cfile, arg, 1);
	}

	default:
		return -EINVAL;
	}

	return rc;
}

#if defined(CONFIG_COMPAT)
/**
 * genwqe_compat_ioctl() - Compatibility ioctl
 *
 * Called whenever a 32-bit process running under a 64-bit kernel
 * performs an ioctl on /dev/genwqe<n>_card.
 *
 * @filp:        file pointer.
 * @cmd:         command.
 * @arg:         user argument.
 * Return:       zero on success or negative number on failure.
 */
static long genwqe_compat_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	return genwqe_ioctl(filp, cmd, arg);
}
#endif /* defined(CONFIG_COMPAT) */

static const struct file_operations genwqe_fops = {
	.owner		= THIS_MODULE,
	.open		= genwqe_open,
	.fasync		= genwqe_fasync,
	.mmap		= genwqe_mmap,
	.unlocked_ioctl	= genwqe_ioctl,
#if defined(CONFIG_COMPAT)
	.compat_ioctl   = genwqe_compat_ioctl,
#endif
	.release	= genwqe_release,
};

static int genwqe_device_initialized(struct genwqe_dev *cd)
{
	return cd->dev != NULL;
}

/**
 * genwqe_device_create() - Create and configure genwqe char device
 * @cd:      genwqe device descriptor
 *
 * This function must be called before we create any more genwqe
 * character devices, because it is allocating the major and minor
 * number which are supposed to be used by the client drivers.
 */
int genwqe_device_create(struct genwqe_dev *cd)
{
	int rc;
	struct pci_dev *pci_dev = cd->pci_dev;

	/*
	 * Here starts the individual setup per client. It must
	 * initialize its own cdev data structure with its own fops.
	 * The appropriate devnum needs to be created. The ranges must
	 * not overlap.
	 */
	rc = alloc_chrdev_region(&cd->devnum_genwqe, 0,
				 GENWQE_MAX_MINOR, GENWQE_DEVNAME);
	if (rc < 0) {
		dev_err(&pci_dev->dev, "err: alloc_chrdev_region failed\n");
		goto err_dev;
	}

	cdev_init(&cd->cdev_genwqe, &genwqe_fops);
	cd->cdev_genwqe.owner = THIS_MODULE;

	rc = cdev_add(&cd->cdev_genwqe, cd->devnum_genwqe, 1);
	if (rc < 0) {
		dev_err(&pci_dev->dev, "err: cdev_add failed\n");
		goto err_add;
	}

	/*
	 * Finally the device in /dev/... must be created. The rule is
	 * to use card%d_clientname for each created device.
	 */
	cd->dev = device_create_with_groups(cd->class_genwqe,
					    &cd->pci_dev->dev,
					    cd->devnum_genwqe, cd,
					    genwqe_attribute_groups,
					    GENWQE_DEVNAME "%u_card",
					    cd->card_idx);
	if (IS_ERR(cd->dev)) {
		rc = PTR_ERR(cd->dev);
		goto err_cdev;
	}

	rc = genwqe_init_debugfs(cd);
	if (rc != 0)
		goto err_debugfs;

	return 0;

 err_debugfs:
	device_destroy(cd->class_genwqe, cd->devnum_genwqe);
 err_cdev:
	cdev_del(&cd->cdev_genwqe);
 err_add:
	unregister_chrdev_region(cd->devnum_genwqe, GENWQE_MAX_MINOR);
 err_dev:
	cd->dev = NULL;
	return rc;
}

static int genwqe_inform_and_stop_processes(struct genwqe_dev *cd)
{
	int rc;
	unsigned int i;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (!genwqe_open_files(cd))
		return 0;

	dev_warn(&pci_dev->dev, "[%s] send SIGIO and wait ...\n", __func__);

	rc = genwqe_kill_fasync(cd, SIGIO);
	if (rc > 0) {
		/* give kill_timeout seconds to close file descriptors ... */
		for (i = 0; (i < genwqe_kill_timeout) &&
			     genwqe_open_files(cd); i++) {
			dev_info(&pci_dev->dev, "  %d sec ...", i);

			cond_resched();
			msleep(1000);
		}

		/* if no open files we can safely continue, else ... */
		if (!genwqe_open_files(cd))
			return 0;

		dev_warn(&pci_dev->dev,
			 "[%s] send SIGKILL and wait ...\n", __func__);

		rc = genwqe_force_sig(cd, SIGKILL); /* force terminate */
		if (rc) {
			/* Give kill_timout more seconds to end processes */
			for (i = 0; (i < genwqe_kill_timeout) &&
				     genwqe_open_files(cd); i++) {
				dev_warn(&pci_dev->dev, "  %d sec ...", i);

				cond_resched();
				msleep(1000);
			}
		}
	}
	return 0;
}

/**
 * genwqe_device_remove() - Remove genwqe's char device
 *
 * This function must be called after the client devices are removed
 * because it will free the major/minor number range for the genwqe
 * drivers.
 *
 * This function must be robust enough to be called twice.
 */
int genwqe_device_remove(struct genwqe_dev *cd)
{
	int rc;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (!genwqe_device_initialized(cd))
		return 1;

	genwqe_inform_and_stop_processes(cd);

	/*
	 * We currently do wait until all filedescriptors are
	 * closed. This leads to a problem when we abort the
	 * application which will decrease this reference from
	 * 1/unused to 0/illegal and not from 2/used 1/empty.
	 */
	rc = atomic_read(&cd->cdev_genwqe.kobj.kref.refcount);
	if (rc != 1) {
		dev_err(&pci_dev->dev,
			"[%s] err: cdev_genwqe...refcount=%d\n", __func__, rc);
		panic("Fatal err: cannot free resources with pending references!");
	}

	genqwe_exit_debugfs(cd);
	device_destroy(cd->class_genwqe, cd->devnum_genwqe);
	cdev_del(&cd->cdev_genwqe);
	unregister_chrdev_region(cd->devnum_genwqe, GENWQE_MAX_MINOR);
	cd->dev = NULL;

	return 0;
}
