// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Endpoint function driver
 *
 * Copyright (C) 2023 Rockchip Electronic Co,. Ltd.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>

#include <uapi/linux/rk-pcie-ep.h>

#include "../../pci/controller/rockchip-pcie-dma.h"
#include "../../pci/controller/dwc/pcie-dw-dmatest.h"
#if IS_MODULE(CONFIG_PCIE_FUNC_RKEP) && IS_ENABLED(CONFIG_PCIE_DW_DMATEST)
#include "../../pci/controller/dwc/pcie-dw-dmatest.c"
#endif

#define DRV_NAME "pcie-rkep"

#ifndef PCI_VENDOR_ID_ROCKCHIP
#define PCI_VENDOR_ID_ROCKCHIP          0x1d87
#endif

#define MISC_DEV_NAME_MAX_LENGTH	0x80

static DEFINE_MUTEX(rkep_mutex);
#define BAR_0_SZ			SZ_4M
#define RKEP_NUM_IRQ_VECTORS		4

#define PCIe_CLIENT_MSI_IRQ_OBJ		0	/* rockchip ep object special irq */

#define PCIE_DMA_OFFSET			0x0

#define PCIE_DMA_CTRL_OFF		0x8
#define PCIE_DMA_WR_ENB			0xc
#define PCIE_DMA_WR_CTRL_LO		0x200
#define PCIE_DMA_WR_CTRL_HI		0x204
#define PCIE_DMA_WR_XFERSIZE		0x208
#define PCIE_DMA_WR_SAR_PTR_LO		0x20c
#define PCIE_DMA_WR_SAR_PTR_HI		0x210
#define PCIE_DMA_WR_DAR_PTR_LO		0x214
#define PCIE_DMA_WR_DAR_PTR_HI		0x218
#define PCIE_DMA_WR_LL_PTR_LO		0x21c
#define PCIE_DMA_WR_LL_PTR_HI		0x220
#define PCIE_DMA_WR_WEILO		0x18
#define PCIE_DMA_WR_WEIHI		0x1c
#define PCIE_DMA_WR_DOORBELL		0x10
#define PCIE_DMA_WR_INT_STATUS		0x4c
#define PCIE_DMA_WR_INT_MASK		0x54
#define PCIE_DMA_WR_INT_CLEAR		0x58
#define PCIE_DMA_WR_ERR_STATUS		0x5c
#define PCIE_DMA_WR_LL_ERR_EN		0x90

#define PCIE_DMA_RD_ENB			0x2c
#define PCIE_DMA_RD_CTRL_LO		0x300
#define PCIE_DMA_RD_CTRL_HI		0x304
#define PCIE_DMA_RD_XFERSIZE		0x308
#define PCIE_DMA_RD_SAR_PTR_LO		0x30c
#define PCIE_DMA_RD_SAR_PTR_HI		0x310
#define PCIE_DMA_RD_DAR_PTR_LO		0x314
#define PCIE_DMA_RD_DAR_PTR_HI		0x318
#define PCIE_DMA_RD_LL_PTR_LO		0x31c
#define PCIE_DMA_RD_LL_PTR_HI		0x320
#define PCIE_DMA_RD_WEILO		0x38
#define PCIE_DMA_RD_WEIHI		0x3c
#define PCIE_DMA_RD_DOORBELL		0x30
#define PCIE_DMA_RD_INT_STATUS		0xa0
#define PCIE_DMA_RD_INT_MASK		0xa8
#define PCIE_DMA_RD_INT_CLEAR		0xac
#define PCIE_DMA_RD_ERR_STATUS_LOW	0xb8
#define PCIE_DMA_RD_ERR_STATUS_HIGH	0xbc
#define PCIE_DMA_RD_LL_ERR_EN		0xc4

#define PCIE_DMA_CHANEL_MAX_NUM		2

#define RKEP_USER_MEM_SIZE		SZ_64M

#define PCIE_CFG_ELBI_APP_OFFSET	0xe00
#define PCIE_CFG_ELBI_USER_DATA_OFF	0x10

#define PCIE_ELBI_REG_NUM		0x2

#define RKEP_EP_ELBI_TIEMOUT_US		100000

#define PCIE_RK3568_RC_DBI_BASE		0xf6000000
#define PCIE_RK3588_RC_DBI_BASE		0xf5000000
#define PCIE_DBI_SIZE			0x400000

struct pcie_rkep_irq_context {
	struct pci_dev *dev;
	u16 msg_id;
};

struct pcie_rkep {
	struct pci_dev *pdev;
	void __iomem *bar0;
	void __iomem *bar2;
	void __iomem *bar4;
	int cur_mmap_res;
	struct pcie_rkep_irq_context irq_ctx[RKEP_NUM_IRQ_VECTORS];
	int irq_valid;

	struct miscdevice dev;
	struct dma_trx_obj *dma_obj;
	struct pcie_ep_obj_info *obj_info;
	struct page *user_pages; /* Allocated physical memory for user space */
	struct mutex dev_lock_mutex; /* Sync resources in multi-process, such as vid and ELBI0 */
	DECLARE_BITMAP(virtual_id_bitmap, RKEP_EP_VIRTUAL_ID_MAX);
	DECLARE_BITMAP(virtual_id_irq_bitmap, RKEP_EP_VIRTUAL_ID_MAX);
	wait_queue_head_t wq_head;
};

struct pcie_file {
	struct mutex file_lock_mutex;
	struct pcie_rkep *pcie_rkep;
	DECLARE_BITMAP(child_vid_bitmap, RKEP_EP_VIRTUAL_ID_MAX); /* The virtual IDs applied for each task */
};

static int rkep_ep_dma_xfer(struct pcie_rkep *pcie_rkep, struct pcie_ep_dma_block_req *dma)
{
	int ret;

	if (dma->wr)
		ret = pcie_dw_wired_dma_tobus_block(pcie_rkep->dma_obj, dma->chn, dma->block.bus_paddr, dma->block.local_paddr, dma->block.size);
	else
		ret = pcie_dw_wired_dma_frombus_block(pcie_rkep->dma_obj, dma->chn, dma->block.local_paddr, dma->block.bus_paddr, dma->block.size);

	return ret;
}

static int rkep_ep_request_virtual_id(struct pcie_file *pcie_file)
{
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	int index;

	mutex_lock(&pcie_rkep->dev_lock_mutex);
	index = find_first_zero_bit(pcie_rkep->virtual_id_bitmap, RKEP_EP_VIRTUAL_ID_MAX);
	if (index >= RKEP_EP_VIRTUAL_ID_MAX) {
		dev_err(&pcie_rkep->pdev->dev, "request virtual id %d is invalid\n", index);
		mutex_unlock(&pcie_rkep->dev_lock_mutex);
		return -EINVAL;
	}
	set_bit(index, pcie_rkep->virtual_id_bitmap);
	mutex_unlock(&pcie_rkep->dev_lock_mutex);

	mutex_lock(&pcie_file->file_lock_mutex);
	set_bit(index, pcie_file->child_vid_bitmap);
	mutex_unlock(&pcie_file->file_lock_mutex);

	dev_dbg(&pcie_rkep->pdev->dev, "request virtual id %d\n", index);

	return index;
}

static int rkep_ep_release_virtual_id(struct pcie_file *pcie_file, int index)
{
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;

	if (index >= RKEP_EP_VIRTUAL_ID_MAX) {
		dev_err(&pcie_rkep->pdev->dev, "release virtual id %d out of range\n", index);

		return -EINVAL;
	}

	if (!test_bit(index, pcie_rkep->virtual_id_bitmap))
		dev_err(&pcie_rkep->pdev->dev, "release virtual id %d is already free\n", index);

	mutex_lock(&pcie_file->file_lock_mutex);
	__clear_bit(index, pcie_file->child_vid_bitmap);
	mutex_unlock(&pcie_file->file_lock_mutex);

	mutex_lock(&pcie_rkep->dev_lock_mutex);
	__clear_bit(index, pcie_rkep->virtual_id_bitmap);
	mutex_unlock(&pcie_rkep->dev_lock_mutex);

	dev_dbg(&pcie_rkep->pdev->dev, "release virtual id %d\n", index);

	return 0;
}

static int rkep_ep_raise_elbi_irq(struct pcie_file *pcie_file, u32 interrupt_num)
{
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	u32 index, off;
	int i, gap_us = 100;
	u32 val;
	int ret;

	if (interrupt_num >= (PCIE_ELBI_REG_NUM * 16)) {
		dev_err(&pcie_rkep->pdev->dev, "elbi int num out of max count\n");
		return -EINVAL;
	}

	index = interrupt_num / 16;
	off = interrupt_num % 16;

	for (i = 0; i < RKEP_EP_ELBI_TIEMOUT_US; i += gap_us) {
		pci_read_config_dword(pcie_rkep->pdev, PCIE_CFG_ELBI_APP_OFFSET + 4 * index, &val);
		if (val & BIT(off))
			usleep_range(gap_us, gap_us + 10);
		else
			break;
	}

	if (i >= gap_us)
		dev_err(&pcie_rkep->pdev->dev, "elbi int is not clear, status=%x\n", val);

	ret = pci_write_config_dword(pcie_rkep->pdev, PCIE_CFG_ELBI_APP_OFFSET + 4 * index,
				      (1 << (off + 16)) | (1 << off));

	return ret;
}

static int rkep_ep_raise_irq_user_obj(struct pcie_file *pcie_file, u32 index)
{
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	int ret;

	if (index >= RKEP_EP_VIRTUAL_ID_MAX) {
		dev_err(&pcie_rkep->pdev->dev, "raise irq_user, virtual id %d out of range\n", index);

		return -EINVAL;
	}

	pcie_rkep->obj_info->irq_type_ep = OBJ_IRQ_USER;
	pcie_rkep->obj_info->irq_user_data_ep = index;
	mutex_lock(&pcie_rkep->dev_lock_mutex);
	ret = rkep_ep_raise_elbi_irq(pcie_file, 0);
	mutex_unlock(&pcie_rkep->dev_lock_mutex);

	return ret;
}

static int rkep_ep_poll_irq_user(struct pcie_file *pcie_file, struct pcie_ep_obj_poll_virtual_id_cfg *cfg)
{
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	u32 index = cfg->virtual_id;

	if (index >= RKEP_EP_VIRTUAL_ID_MAX) {
		dev_err(&pcie_rkep->pdev->dev, "poll irq_user, virtual id %d out of range\n", index);

		return -EINVAL;
	}

	cfg->poll_status = NSIGPOLL;
	if (cfg->sync) {
		wait_event_interruptible(pcie_rkep->wq_head,
					 test_bit(index, pcie_rkep->virtual_id_irq_bitmap));
	} else {
		wait_event_interruptible_timeout(pcie_rkep->wq_head,
						 test_bit(index, pcie_rkep->virtual_id_irq_bitmap),
						 cfg->timeout_ms);
	}
	if (test_and_clear_bit(index, pcie_rkep->virtual_id_irq_bitmap))
		cfg->poll_status = POLL_IN;

	dev_dbg(&pcie_rkep->pdev->dev, "poll virtual id %d, ret=%d\n", index, cfg->poll_status);

	return 0;
}

static int pcie_rkep_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct pcie_rkep *pcie_rkep = container_of(miscdev, struct pcie_rkep, dev);
	struct pcie_file *pcie_file = NULL;

	pcie_file = devm_kzalloc(&pcie_rkep->pdev->dev, sizeof(struct pcie_file), GFP_KERNEL);
	if (!pcie_file)
		return -ENOMEM;

	pcie_file->pcie_rkep = pcie_rkep;

	mutex_init(&pcie_file->file_lock_mutex);

	file->private_data = pcie_file;

	return 0;
}

static int pcie_rkep_release(struct inode *inode, struct file *file)
{
	struct pcie_file *pcie_file = file->private_data;
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	int index;

	while (1) {
		mutex_lock(&pcie_file->file_lock_mutex);
		index = find_first_bit(pcie_file->child_vid_bitmap, RKEP_EP_VIRTUAL_ID_MAX);

		if (index >= RKEP_EP_VIRTUAL_ID_MAX)
			break;

		__clear_bit(index, pcie_file->child_vid_bitmap);
		mutex_unlock(&pcie_file->file_lock_mutex);

		mutex_lock(&pcie_rkep->dev_lock_mutex);
		__clear_bit(index, pcie_rkep->virtual_id_bitmap);
		mutex_unlock(&pcie_rkep->dev_lock_mutex);

		dev_dbg(&pcie_rkep->pdev->dev, "release virtual id %d\n", index);
	}

	devm_kfree(&pcie_rkep->pdev->dev, pcie_file);

	return 0;
}

static ssize_t pcie_rkep_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct pcie_file *pcie_file = file->private_data;
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	struct pci_dev *dev = pcie_rkep->pdev;
	unsigned int size = count;
	loff_t init_off = *ppos, off = *ppos;
	u8 *data;

	data = kzalloc(PCI_CFG_SPACE_EXP_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (off > dev->cfg_size) {
		kfree(data);
		return 0;
	}
	if (off + count > dev->cfg_size) {
		size = dev->cfg_size - off;
		count = size;
	}

	if (copy_from_user(data, buf, count)) {
		kfree(data);
		return -EFAULT;
	}

	if ((off & 1) && size) {
		pci_write_config_byte(dev, off, data[off - init_off]);
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val = data[off - init_off];

		val |= (u16) data[off - init_off + 1] << 8;
		pci_write_config_word(dev, off, val);
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val = data[off - init_off];

		val |= (u32) data[off - init_off + 1] << 8;
		val |= (u32) data[off - init_off + 2] << 16;
		val |= (u32) data[off - init_off + 3] << 24;
		pci_write_config_dword(dev, off, val);
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val = data[off - init_off];

		val |= (u16) data[off - init_off + 1] << 8;
		pci_write_config_word(dev, off, val);
		off += 2;
		size -= 2;
	}

	if (size) {
		pci_write_config_byte(dev, off, data[off - init_off]);
		off++;
		--size;
	}

	kfree(data);

	return count;
}

static ssize_t pcie_rkep_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct pcie_file *pcie_file = file->private_data;
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	struct pci_dev *dev = pcie_rkep->pdev;
	unsigned int size = count;
	loff_t init_off = *ppos, off = *ppos;
	u8 *data;

	data = kzalloc(PCI_CFG_SPACE_EXP_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (off > dev->cfg_size) {
		kfree(data);
		return 0;
	}
	if (off + count > dev->cfg_size) {
		size = dev->cfg_size - off;
		count = size;
	}

	if ((off & 1) && size) {
		u8 val;

		pci_read_config_byte(dev, off, &val);
		data[off - init_off] = val;
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val;

		pci_read_config_word(dev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val;

		pci_read_config_dword(dev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		data[off - init_off + 2] = (val >> 16) & 0xff;
		data[off - init_off + 3] = (val >> 24) & 0xff;
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val;

		pci_read_config_word(dev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		off += 2;
		size -= 2;
	}

	if (size > 0) {
		u8 val;

		pci_read_config_byte(dev, off, &val);
		data[off - init_off] = val;
		off++;
		--size;
	}

	if (copy_to_user(buf, data, count)) {
		kfree(data);
		return -EFAULT;
	}

	kfree(data);

	return count;
}

static int pcie_rkep_mmap(struct file *file, struct vm_area_struct *vma)
{
	u64 addr;
	struct pcie_file *pcie_file = file->private_data;
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	struct pci_dev *dev = pcie_rkep->pdev;
	size_t size = vma->vm_end - vma->vm_start;
	resource_size_t bar_size;
	int err;

	switch (pcie_rkep->cur_mmap_res) {
	case PCIE_EP_MMAP_RESOURCE_RK3568_RC_DBI:
		if (size > PCIE_DBI_SIZE) {
			dev_warn(&pcie_rkep->pdev->dev, "dbi mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = PCIE_RK3568_RC_DBI_BASE;
		break;
	case PCIE_EP_MMAP_RESOURCE_RK3588_RC_DBI:
		if (size > PCIE_DBI_SIZE) {
			dev_warn(&pcie_rkep->pdev->dev, "dbi mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = PCIE_RK3588_RC_DBI_BASE;
		break;
	case PCIE_EP_MMAP_RESOURCE_BAR0:
		bar_size = pci_resource_len(dev, 0);
		if (size > bar_size) {
			dev_warn(&pcie_rkep->pdev->dev, "bar0 mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = pci_resource_start(dev, 0);
		break;
	case PCIE_EP_MMAP_RESOURCE_BAR2:
		bar_size = pci_resource_len(dev, 2);
		if (size > bar_size) {
			dev_warn(&pcie_rkep->pdev->dev, "bar2 mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = pci_resource_start(dev, 2);
		break;
	case PCIE_EP_MMAP_RESOURCE_BAR4:
		bar_size = pci_resource_len(dev, 4);
		if (size > bar_size) {
			dev_warn(&pcie_rkep->pdev->dev, "bar4 mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = pci_resource_start(dev, 4);
		break;
	case PCIE_EP_MMAP_RESOURCE_USER_MEM:
		if (size > RKEP_USER_MEM_SIZE) {
			dev_warn(&pcie_rkep->pdev->dev, "mmap size is out of limitation\n");
			return -EINVAL;
		}

		if (!pcie_rkep->user_pages) {
			dev_warn(&pcie_rkep->pdev->dev, "user_pages has not been allocated yet\n");
			return -EINVAL;
		}
		addr = page_to_phys(pcie_rkep->user_pages);
		break;
	default:
		dev_err(&pcie_rkep->pdev->dev, "cur mmap_res %d is unsurreport\n", pcie_rkep->cur_mmap_res);
		return -EINVAL;
	}

	vma->vm_flags |= VM_IO;
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);

	if (pcie_rkep->cur_mmap_res == PCIE_EP_MMAP_RESOURCE_BAR2 ||
	    pcie_rkep->cur_mmap_res == PCIE_EP_MMAP_RESOURCE_USER_MEM)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	err = remap_pfn_range(vma, vma->vm_start,
			      __phys_to_pfn(addr),
			      size, vma->vm_page_prot);
	if (err)
		return -EAGAIN;

	return 0;
}

static long pcie_rkep_ioctl(struct file *file, unsigned int cmd, unsigned long args)
{
	void __user *argp;
	struct pcie_file *pcie_file = file->private_data;
	struct pcie_rkep *pcie_rkep = pcie_file->pcie_rkep;
	struct pcie_ep_dma_cache_cfg cfg;
	struct pcie_ep_dma_block_req dma;
	void __user *uarg = (void __user *)args;
	struct pcie_ep_obj_poll_virtual_id_cfg poll_cfg;
	int mmap_res;
	int ret;
	int index;
	u64 addr;

	argp = (void __user *)args;

	switch (cmd) {
	case 0x4:
		/* get mapped physical address */
		if (!pcie_rkep->user_pages) {
			dev_warn(&pcie_rkep->pdev->dev, "user_pages has not been allocated yet\n");
			return -EINVAL;
		}
		addr = page_to_phys(pcie_rkep->user_pages);
		if (copy_to_user(argp, &addr, sizeof(addr)))
			return -EFAULT;
		break;
	case PCIE_DMA_CACHE_INVALIDE:
		ret = copy_from_user(&cfg, uarg, sizeof(cfg));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev,
				"failed to get invalid cfg copy from userspace\n");
			return -EFAULT;
		}
		dma_sync_single_for_cpu(&pcie_rkep->pdev->dev, cfg.addr, cfg.size, DMA_FROM_DEVICE);
		break;
	case PCIE_DMA_CACHE_FLUSH:
		ret = copy_from_user(&cfg, uarg, sizeof(cfg));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev,
				"failed to get flush cfg copy from userspace\n");
			return -EFAULT;
		}
		dma_sync_single_for_device(&pcie_rkep->pdev->dev, cfg.addr, cfg.size,
					   DMA_TO_DEVICE);
		break;
	case PCIE_EP_DMA_XFER_BLOCK:
		ret = copy_from_user(&dma, uarg, sizeof(dma));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev,
				"failed to get dma_data copy from userspace\n");
			return -EFAULT;
		}
		ret = rkep_ep_dma_xfer(pcie_rkep, &dma);
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev, "failed to transfer dma, ret=%d\n", ret);
			return -EFAULT;
		}
		break;
	case PCIE_EP_REQUEST_VIRTUAL_ID:
		index = rkep_ep_request_virtual_id(pcie_file);
		if (index < 0) {
			dev_err(&pcie_rkep->pdev->dev,
				"request virtual id failed, ret=%d\n", index);

			return -EFAULT;
		}
		if (copy_to_user(argp, &index, sizeof(index)))
			return -EFAULT;
		break;
	case PCIE_EP_RELEASE_VIRTUAL_ID:
		ret = copy_from_user(&index, uarg, sizeof(index));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev,
				"failed to get release data copy from userspace\n");
			return -EFAULT;
		}
		ret = rkep_ep_release_virtual_id(pcie_file, index);
		if (ret < 0) {
			dev_err(&pcie_rkep->pdev->dev,
				"release virtual id %d failed, ret=%d\n", index, ret);

			return -EFAULT;
		}
		break;
	case PCIE_EP_RAISE_IRQ_USER:
		ret = copy_from_user(&index, uarg, sizeof(index));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev,
				"failed to get raise irq data copy from userspace\n");
			return -EFAULT;
		}

		ret = rkep_ep_raise_irq_user_obj(pcie_file, index);
		if (ret < 0)
			return -EFAULT;
		break;
	case PCIE_EP_POLL_IRQ_USER:
		ret = copy_from_user(&poll_cfg, uarg, sizeof(poll_cfg));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev,
				"failed to get poll irq data copy from userspace\n");

			return -EFAULT;
		}

		ret = rkep_ep_poll_irq_user(pcie_file, &poll_cfg);
		if (ret < 0)
			return -EFAULT;

		if (copy_to_user(argp, &poll_cfg, sizeof(poll_cfg)))
			return -EFAULT;
		break;
	case PCIE_EP_RAISE_ELBI:
		ret = copy_from_user(&index, uarg, sizeof(index));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev,
				"failed to get raise elbi data copy from userspace\n");
			return -EFAULT;
		}
		ret = rkep_ep_raise_elbi_irq(pcie_file, index);
		if (ret < 0) {
			dev_err(&pcie_rkep->pdev->dev,
				"raise elbi %d failed, ret=%d\n", index, ret);

			return -EFAULT;
		}
		break;
	case PCIE_EP_SET_MMAP_RESOURCE:
		ret = copy_from_user(&mmap_res, uarg, sizeof(mmap_res));
		if (ret) {
			dev_err(&pcie_rkep->pdev->dev, "failed to get copy from\n");
			return -EFAULT;
		}

		if (mmap_res >= PCIE_EP_MMAP_RESOURCE_MAX || mmap_res < 0) {
			dev_err(&pcie_rkep->pdev->dev, "mmap index %d is out of number\n", mmap_res);
			return -EINVAL;
		}

		pcie_rkep->cur_mmap_res = mmap_res;
		break;
	default:
		break;
	}

	return 0;
}

static const struct file_operations pcie_rkep_fops = {
	.owner		= THIS_MODULE,
	.open		= pcie_rkep_open,
	.write		= pcie_rkep_write,
	.read		= pcie_rkep_read,
	.unlocked_ioctl = pcie_rkep_ioctl,
	.mmap		= pcie_rkep_mmap,
	.release	= pcie_rkep_release,
	.llseek		= default_llseek,
};

static inline void pcie_rkep_writel_dbi(struct pcie_rkep *pcie_rkep, u32 reg, u32 val)
{
	writel(val, pcie_rkep->bar4 + reg);
}

static inline u32 pcie_rkep_readl_dbi(struct pcie_rkep *pcie_rkep, u32 reg)
{
	return readl(pcie_rkep->bar4 + reg);
}

static void pcie_rkep_dma_debug(struct dma_trx_obj *obj, struct dma_table *table)
{
	struct pci_dev *pdev = container_of(obj->dev, struct pci_dev, dev);
	struct pcie_rkep *pcie_rkep = pci_get_drvdata(pdev);
	unsigned int ctr_off = PCIE_DMA_OFFSET + table->chn * 0x200;

	dev_err(&pdev->dev, "chnl=%x\n", table->start.chnl);
	dev_err(&pdev->dev, "%s\n", table->dir == DMA_FROM_BUS ? "udma read" : "udma write");
	if (table->dma_mode == RK_PCIE_DMA_BLOCK) {
		dev_err(&pdev->dev, "src=0x%x %x\n", table->ctx_reg.sarptrhi, table->ctx_reg.sarptrlo);
		dev_err(&pdev->dev, "dst=0x%x %x\n", table->ctx_reg.darptrhi, table->ctx_reg.darptrlo);
	} else {
		dev_err(&pdev->dev, "phys_descs=0x%llx\n", table->phys_descs);
	}
	dev_err(&pdev->dev, "xfersize=%x\n", table->ctx_reg.xfersize);

	if (table->dir == DMA_FROM_BUS) {
		if (table->dma_mode == RK_PCIE_DMA_BLOCK) {
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_INT_MASK = %x\n", PCIE_DMA_RD_INT_MASK, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_MASK));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_ENB = %x\n", PCIE_DMA_RD_ENB, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ENB));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_CTRL_LO = %x\n", ctr_off + PCIE_DMA_RD_CTRL_LO, pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_CTRL_HI = %x\n", ctr_off + PCIE_DMA_RD_CTRL_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_XFERSIZE = %x\n", ctr_off + PCIE_DMA_RD_XFERSIZE,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_XFERSIZE));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_SAR_PTR_LO = %x\n", ctr_off + PCIE_DMA_RD_SAR_PTR_LO,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_SAR_PTR_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_SAR_PTR_HI = %x\n", ctr_off + PCIE_DMA_RD_SAR_PTR_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_SAR_PTR_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_DAR_PTR_LO = %x\n", ctr_off + PCIE_DMA_RD_DAR_PTR_LO,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_DAR_PTR_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_DAR_PTR_HI = %x\n", ctr_off + PCIE_DMA_RD_DAR_PTR_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_DAR_PTR_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_DOORBELL = %x\n", PCIE_DMA_RD_DOORBELL, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_DOORBELL));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_INT_STATUS = %x\n", PCIE_DMA_RD_INT_STATUS, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_STATUS));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_ERR_STATUS_LOW = %x\n", PCIE_DMA_RD_ERR_STATUS_LOW, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ERR_STATUS_LOW));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_ERR_STATUS_HIGH = %x\n", PCIE_DMA_RD_ERR_STATUS_HIGH, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ERR_STATUS_HIGH));
		} else {
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_INT_MASK = %x\n", PCIE_DMA_RD_INT_MASK, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_MASK));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_ENB = %x\n", PCIE_DMA_RD_ENB, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ENB));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_CTRL_LO = %x\n", ctr_off + PCIE_DMA_RD_CTRL_LO, pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_CTRL_HI = %x\n", ctr_off + PCIE_DMA_RD_CTRL_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_LL_PTR_LO = %x\n", ctr_off + PCIE_DMA_RD_LL_PTR_LO,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_LL_PTR_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_LL_PTR_HI = %x\n", ctr_off + PCIE_DMA_RD_LL_PTR_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_LL_PTR_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_DOORBELL = %x\n", PCIE_DMA_RD_DOORBELL, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_DOORBELL));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_ERR_STATUS_LOW = %x\n", PCIE_DMA_RD_ERR_STATUS_LOW, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ERR_STATUS_LOW));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_RD_ERR_STATUS_HIGH = %x\n", PCIE_DMA_RD_ERR_STATUS_HIGH, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ERR_STATUS_HIGH));
		}
	} else {
		if (table->dma_mode == RK_PCIE_DMA_BLOCK) {
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_INT_MASK = %x\n", PCIE_DMA_WR_INT_MASK, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_MASK));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_ENB = %x\n", PCIE_DMA_WR_ENB, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_ENB));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_CTRL_LO = %x\n", ctr_off + PCIE_DMA_WR_CTRL_LO, pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_CTRL_HI = %x\n", ctr_off + PCIE_DMA_WR_CTRL_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_XFERSIZE = %x\n", ctr_off + PCIE_DMA_WR_XFERSIZE,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_XFERSIZE));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_SAR_PTR_LO = %x\n", ctr_off + PCIE_DMA_WR_SAR_PTR_LO,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_SAR_PTR_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_SAR_PTR_HI = %x\n", ctr_off + PCIE_DMA_WR_SAR_PTR_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_SAR_PTR_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_DAR_PTR_LO = %x\n", ctr_off + PCIE_DMA_WR_DAR_PTR_LO,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_DAR_PTR_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_DAR_PTR_HI = %x\n", ctr_off + PCIE_DMA_WR_DAR_PTR_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_DAR_PTR_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_DOORBELL = %x\n", PCIE_DMA_WR_DOORBELL, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_DOORBELL));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_INT_STATUS = %x\n", PCIE_DMA_WR_INT_STATUS, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_STATUS));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_ERR_STATUS = %x\n", PCIE_DMA_WR_ERR_STATUS, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_ERR_STATUS));
		} else {
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_INT_MASK = %x\n", PCIE_DMA_WR_INT_MASK, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_MASK));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_ENB = %x\n", PCIE_DMA_WR_ENB, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_ENB));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_CTRL_LO = %x\n", ctr_off + PCIE_DMA_WR_CTRL_LO, pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_CTRL_HI = %x\n", ctr_off + PCIE_DMA_WR_CTRL_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_LL_PTR_LO = %x\n", ctr_off + PCIE_DMA_WR_LL_PTR_LO,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_LL_PTR_LO));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_LL_PTR_HI = %x\n", ctr_off + PCIE_DMA_WR_LL_PTR_HI,  pcie_rkep_readl_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_LL_PTR_HI));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_DOORBELL = %x\n", PCIE_DMA_WR_DOORBELL, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_DOORBELL));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_INT_STATUS = %x\n", PCIE_DMA_WR_INT_STATUS, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_STATUS));
			dev_err(&pdev->dev, "reg[0x%x] PCIE_DMA_WR_ERR_STATUS = %x\n", PCIE_DMA_WR_ERR_STATUS, pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_ERR_STATUS));
		}
	}
}

static void pcie_rkep_start_dma_rd(struct dma_trx_obj *obj, struct dma_table *cur, int ctr_off)
{
	struct pci_dev *pdev = container_of(obj->dev, struct pci_dev, dev);
	struct pcie_rkep *pcie_rkep = pci_get_drvdata(pdev);

	if (cur->dma_mode == RK_PCIE_DMA_BLOCK) {
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ENB,
				cur->enb.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_LO,
				cur->ctx_reg.ctrllo.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_HI,
				cur->ctx_reg.ctrlhi.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_XFERSIZE,
				cur->ctx_reg.xfersize);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_SAR_PTR_LO,
				cur->ctx_reg.sarptrlo);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_SAR_PTR_HI,
				cur->ctx_reg.sarptrhi);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_DAR_PTR_LO,
				cur->ctx_reg.darptrlo);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_DAR_PTR_HI,
				cur->ctx_reg.darptrhi);
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_DOORBELL,
				cur->start.asdword);
	} else {
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_ENB,
				cur->enb.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_LO,
				cur->ctx_reg.ctrllo.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_CTRL_HI,
				cur->ctx_reg.ctrlhi.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_LL_PTR_LO,
				lower_32_bits(cur->phys_descs));
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_RD_LL_PTR_HI,
				upper_32_bits(cur->phys_descs));
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_DOORBELL,
				cur->start.asdword);
	}
	/* pcie_rkep_dma_debug(obj, cur); */
}

static void pcie_rkep_start_dma_wr(struct dma_trx_obj *obj, struct dma_table *cur, int ctr_off)
{
	struct pci_dev *pdev = container_of(obj->dev, struct pci_dev, dev);
	struct pcie_rkep *pcie_rkep = pci_get_drvdata(pdev);

	if (cur->dma_mode == RK_PCIE_DMA_BLOCK) {
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_ENB,
				cur->enb.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_LO,
				cur->ctx_reg.ctrllo.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_HI,
				cur->ctx_reg.ctrlhi.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_XFERSIZE,
				cur->ctx_reg.xfersize);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_SAR_PTR_LO,
				cur->ctx_reg.sarptrlo);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_SAR_PTR_HI,
				cur->ctx_reg.sarptrhi);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_DAR_PTR_LO,
				cur->ctx_reg.darptrlo);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_DAR_PTR_HI,
				cur->ctx_reg.darptrhi);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_WEILO,
				cur->weilo.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_DOORBELL,
				cur->start.asdword);
	} else {
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_ENB,
				cur->enb.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_LO,
				cur->ctx_reg.ctrllo.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_CTRL_HI,
				cur->ctx_reg.ctrlhi.asdword);
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_LL_PTR_LO,
				lower_32_bits(cur->phys_descs));
		pcie_rkep_writel_dbi(pcie_rkep, ctr_off + PCIE_DMA_WR_LL_PTR_HI,
				upper_32_bits(cur->phys_descs));
		pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_DOORBELL,
				cur->start.asdword);
	}
	/* pcie_rkep_dma_debug(obj, cur); */
}

static void pcie_rkep_start_dma_dwc(struct dma_trx_obj *obj, struct dma_table *table)
{
	int dir = table->dir;
	int chn = table->chn;

	int ctr_off = PCIE_DMA_OFFSET + chn * 0x200;

	if (dir == DMA_FROM_BUS)
		pcie_rkep_start_dma_rd(obj, table, ctr_off);
	else if (dir == DMA_TO_BUS)
		pcie_rkep_start_dma_wr(obj, table, ctr_off);
}

static void pcie_rkep_config_dma_dwc(struct dma_table *table)
{
	if (table->dma_mode == RK_PCIE_DMA_BLOCK) {
		table->enb.enb = 0x1;
		table->ctx_reg.ctrllo.lie = 0x1;
		table->ctx_reg.ctrllo.rie = 0x0;
		table->ctx_reg.ctrllo.td = 0x1;
		table->ctx_reg.ctrlhi.asdword = 0x0;
		table->ctx_reg.xfersize = table->buf_size;
		if (table->dir == DMA_FROM_BUS) {
			table->ctx_reg.sarptrlo = (u32)(table->bus & 0xffffffff);
			table->ctx_reg.sarptrhi = (u32)(table->bus >> 32);
			table->ctx_reg.darptrlo = (u32)(table->local & 0xffffffff);
			table->ctx_reg.darptrhi = (u32)(table->local >> 32);
		} else if (table->dir == DMA_TO_BUS) {
			table->ctx_reg.sarptrlo = (u32)(table->local & 0xffffffff);
			table->ctx_reg.sarptrhi = (u32)(table->local >> 32);
			table->ctx_reg.darptrlo = (u32)(table->bus & 0xffffffff);
			table->ctx_reg.darptrhi = (u32)(table->bus >> 32);
		}
		table->weilo.weight0 = 0x0;
		table->start.stop = 0x0;
		table->start.chnl = table->chn;
	} else {
		table->enb.enb = 0x1;
		table->ctx_reg.ctrllo.lie = 0x1;
		table->ctx_reg.ctrllo.rie = 0x0;
		table->ctx_reg.ctrllo.ccs = 1;
		table->ctx_reg.ctrllo.llen = 1;
		table->ctx_reg.ctrlhi.asdword = 0x0;
		table->start.chnl = table->chn;
	}
}

static int pcie_rkep_get_dma_status(struct dma_trx_obj *obj, u8 chn, enum dma_dir dir)
{
	struct pci_dev *pdev = container_of(obj->dev, struct pci_dev, dev);
	struct pcie_rkep *pcie_rkep = pci_get_drvdata(pdev);
	union int_status status;
	union int_clear clears;
	int ret = 0;

	dev_dbg(&pdev->dev, "%s %x %x\n", __func__,
		pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_STATUS),
		pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_STATUS));

	if (dir == DMA_TO_BUS) {
		status.asdword =
			pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_STATUS);
		if (status.donesta & BIT(chn)) {
			clears.doneclr = BIT(chn);
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_CLEAR,
					     clears.asdword);
			ret = 1;
		}

		if (status.abortsta & BIT(chn)) {
			dev_err(&pdev->dev, "%s, write abort %x\n", __func__, status.asdword);
			clears.abortclr = BIT(chn);
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_CLEAR,
					     clears.asdword);
			ret = -1;
		}
	} else {
		status.asdword =
			pcie_rkep_readl_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_STATUS);

		if (status.donesta & BIT(chn)) {
			clears.doneclr = BIT(chn);
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_CLEAR,
					     clears.asdword);
			ret = 1;
		}

		if (status.abortsta & BIT(chn)) {
			dev_err(&pdev->dev, "%s, read abort %x\n", __func__, status.asdword);
			clears.abortclr = BIT(chn);
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_CLEAR,
					     clears.asdword);
			ret = -1;
		}
	}

	return ret;
}

static int pcie_rkep_obj_handler(struct pcie_rkep *pcie_rkep, struct pci_dev *pdev)
{
	union int_status wr_status, rd_status;
	u32 irq_type;
	u32 chn;
	union int_clear clears;
	u32 reg;

	irq_type = pcie_rkep->obj_info->irq_type_rc;
	if (irq_type == OBJ_IRQ_DMA) {
		/* DMA helper */
		wr_status.asdword = pcie_rkep->obj_info->dma_status_rc.wr;
		rd_status.asdword = pcie_rkep->obj_info->dma_status_rc.rd;

		for (chn = 0; chn < PCIE_DMA_CHANEL_MAX_NUM; chn++) {
			if (wr_status.donesta & BIT(chn)) {
				if (pcie_rkep->dma_obj && pcie_rkep->dma_obj->cb) {
					pcie_rkep->dma_obj->cb(pcie_rkep->dma_obj, chn, DMA_TO_BUS);
					clears.doneclr = 0x1 << chn;
					pcie_rkep->obj_info->dma_status_rc.wr &= (~clears.doneclr);
				}
			}

			if (wr_status.abortsta & BIT(chn)) {
				dev_err(&pdev->dev, "%s, abort\n", __func__);
				if (pcie_rkep->dma_obj && pcie_rkep->dma_obj->cb) {
					clears.abortclr = 0x1 << chn;
					pcie_rkep->obj_info->dma_status_rc.wr &= (~clears.abortclr);
				}
			}
		}

		for (chn = 0; chn < PCIE_DMA_CHANEL_MAX_NUM; chn++) {
			if (rd_status.donesta & BIT(chn)) {
				if (pcie_rkep->dma_obj && pcie_rkep->dma_obj->cb) {
					pcie_rkep->dma_obj->cb(pcie_rkep->dma_obj, chn,
							       DMA_FROM_BUS);
					clears.doneclr = 0x1 << chn;
					pcie_rkep->obj_info->dma_status_rc.rd &= (~clears.doneclr);
				}
			}

			if (rd_status.abortsta & BIT(chn)) {
				dev_err(&pdev->dev, "%s, abort\n", __func__);
				if (pcie_rkep->dma_obj && pcie_rkep->dma_obj->cb) {
					clears.abortclr = 0x1 << chn;
					pcie_rkep->obj_info->dma_status_rc.rd &= (~clears.abortclr);
				}
			}
		}
	} else if (irq_type == OBJ_IRQ_USER) {
		reg = pcie_rkep->obj_info->irq_user_data_rc;
		if (reg < RKEP_EP_VIRTUAL_ID_MAX) {
			set_bit(reg, pcie_rkep->virtual_id_irq_bitmap);
			wake_up_interruptible(&pcie_rkep->wq_head);
		}
	}

	return 0;
}

static irqreturn_t pcie_rkep_pcie_interrupt(int irq, void *context)
{
	struct pcie_rkep_irq_context *ctx = context;
	struct pci_dev *pdev = ctx->dev;
	struct pcie_rkep *pcie_rkep = pci_get_drvdata(pdev);

	if (!pcie_rkep)
		return IRQ_HANDLED;

	/*
	 * The irq 0 is the dedicated interrupt for obj to issue remote rc device.
	 */
	if (irq == pci_irq_vector(pcie_rkep->pdev, PCIe_CLIENT_MSI_IRQ_OBJ))
		pcie_rkep_obj_handler(pcie_rkep, pdev);

	return IRQ_HANDLED;
}

static void pcie_rkep_release_irq(struct pcie_rkep *pcie_rkep)
{
	int i;

	if (pcie_rkep->irq_valid) {
		for (i = 0; i < pcie_rkep->irq_valid; i++)
			pci_free_irq(pcie_rkep->pdev, i, &pcie_rkep->irq_ctx[i]);

		pci_free_irq_vectors(pcie_rkep->pdev);
	}
	pcie_rkep->irq_valid = 0;
}

static int pcie_rkep_request_irq(struct pcie_rkep *pcie_rkep, u32 irq_type)
{
	int nvec, ret = -EINVAL, i;

	/* Using msi as default */
	nvec = pci_alloc_irq_vectors(pcie_rkep->pdev, 1, RKEP_NUM_IRQ_VECTORS, irq_type);
	if (nvec < 0)
		return nvec;

	if (nvec != RKEP_NUM_IRQ_VECTORS)
		dev_err(&pcie_rkep->pdev->dev, "only allocate %d irq interrupt, irq_type=%d\n", nvec, irq_type);

	pcie_rkep->irq_valid = 0;
	for (i = 0; i < nvec; i++) {
		pcie_rkep->irq_ctx[i].dev = pcie_rkep->pdev;
		pcie_rkep->irq_ctx[i].msg_id = i;
		ret = pci_request_irq(pcie_rkep->pdev, i,
				      pcie_rkep_pcie_interrupt, NULL,
				      &pcie_rkep->irq_ctx[i], "%s-%d", pcie_rkep->dev.name, i);
		if (ret)
			break;
		pcie_rkep->irq_valid++;
	}

	if (ret) {
		pcie_rkep_release_irq(pcie_rkep);
		dev_err(&pcie_rkep->pdev->dev, "fail to allocate msi interrupt\n");
	} else {
		dev_err(&pcie_rkep->pdev->dev, "success to request msi irq\n");
	}

	return ret;
}

static int rkep_loadfile(struct device *dev, char *path, void __iomem *bar, int pos)
{
	struct file *p_file = NULL;
	loff_t size;
	loff_t offset;

	p_file = filp_open(path, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(p_file) || p_file == NULL) {
		dev_err(dev, "unable to open file: %s\n", path);

		return -ENODEV;
	}

	size = i_size_read(file_inode(p_file));
	dev_info(dev, "%s file %s size %lld to %p\n", __func__, path, size, bar + pos);

	offset = 0;
	kernel_read(p_file, (void *)bar + pos, (size_t)size, (loff_t *)&offset);

	dev_info(dev, "kernel_read size %lld from %s to %p\n", size, path, bar + pos);

	return 0;
}

#define RKEP_CMD_LOADER_RUN     0x524b4501
static ssize_t rkep_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct pcie_rkep *pcie_rkep = dev_get_drvdata(dev);
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	dev_info(dev, "%s val %d\n", __func__, val);
	if (val == 1)
		rkep_loadfile(dev, "/data/uboot.img", pcie_rkep->bar2, 0);
	else if (val == 2)
		rkep_loadfile(dev, "/data/boot.img", pcie_rkep->bar2, 0x400000);
	else if (val == 3)
		writel(RKEP_CMD_LOADER_RUN, pcie_rkep->bar0 + 0x400);

	dev_info(dev, "%s done\n", __func__);

	return count;
}
static DEVICE_ATTR_WO(rkep);

static int pcie_rkep_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	struct pcie_rkep *pcie_rkep;
	u8 *name;
	u16 val;
	bool dmatest_irq = false;

	pcie_rkep = devm_kzalloc(&pdev->dev, sizeof(*pcie_rkep), GFP_KERNEL);
	if (!pcie_rkep)
		return -ENOMEM;

	name = devm_kzalloc(&pdev->dev, MISC_DEV_NAME_MAX_LENGTH, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	set_bit(0, pcie_rkep->virtual_id_bitmap);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed %d\n", ret);
		goto err_pci_enable_dev;
	}

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_regions failed %d\n", ret);
		goto err_req_regions;
	}

	pcie_rkep->bar0 = pci_iomap(pdev, 0, 0);
	if (!pcie_rkep->bar0) {
		dev_err(&pdev->dev, "pci_iomap bar0 failed\n");
		ret = -ENOMEM;
		goto err_pci_iomap;
	}
	pcie_rkep->obj_info = (struct pcie_ep_obj_info *)pcie_rkep->bar0;
	dev_dbg(&pdev->dev, "get bar0 address is %p\n", pcie_rkep->bar0);

	pcie_rkep->bar2 = pci_iomap(pdev, 2, 0);
	if (!pcie_rkep->bar2) {
		dev_err(&pdev->dev, "pci_iomap bar2 failed");
		ret = -ENOMEM;
		goto err_pci_iomap;
	}
	dev_dbg(&pdev->dev, "get bar2 address is %p\n", pcie_rkep->bar2);

	pcie_rkep->bar4 = pci_iomap(pdev, 4, 0);
	if (!pcie_rkep->bar4) {
		dev_err(&pdev->dev, "pci_iomap bar4 failed\n");
		ret = -ENOMEM;
		goto err_pci_iomap;
	}

	dev_dbg(&pdev->dev, "get bar4 address is %p\n", pcie_rkep->bar4);

	sprintf(name, "%s-%s", DRV_NAME, dev_name(&pdev->dev));
	pcie_rkep->dev.minor = MISC_DYNAMIC_MINOR;
	pcie_rkep->dev.name = name;
	pcie_rkep->dev.fops = &pcie_rkep_fops;
	pcie_rkep->dev.parent = NULL;

	mutex_init(&pcie_rkep->dev_lock_mutex);

	ret = misc_register(&pcie_rkep->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register misc device.\n");
		goto err_pci_iomap;
	}

	pcie_rkep->pdev = pdev; /* Save pci device struct */

	pci_set_drvdata(pdev, pcie_rkep);

	init_waitqueue_head(&pcie_rkep->wq_head);
	ret = pcie_rkep_request_irq(pcie_rkep, PCI_IRQ_MSI);
	if (ret)
		goto err_register_irq;

	pcie_rkep->dma_obj = pcie_dw_dmatest_register(&pdev->dev, dmatest_irq);
	if (IS_ERR(pcie_rkep->dma_obj)) {
		dev_err(&pcie_rkep->pdev->dev, "failed to prepare dmatest\n");
		ret = -EINVAL;
		goto err_register_obj;
	}

	if (pcie_rkep->dma_obj) {
		dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
		pcie_rkep->dma_obj->start_dma_func = pcie_rkep_start_dma_dwc;
		pcie_rkep->dma_obj->config_dma_func = pcie_rkep_config_dma_dwc;
		pcie_rkep->dma_obj->get_dma_status = pcie_rkep_get_dma_status;
		pcie_rkep->dma_obj->dma_debug = pcie_rkep_dma_debug;
		if (!dmatest_irq) {
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_MASK, 0xffffffff);
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_MASK, 0xffffffff);

			/* Enable linked list err en */
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_WR_LL_ERR_EN, 0xffffffff);
			pcie_rkep_writel_dbi(pcie_rkep, PCIE_DMA_OFFSET + PCIE_DMA_RD_LL_ERR_EN, 0xffffffff);
		}
	}

#if IS_ENABLED(CONFIG_PCIE_FUNC_RKEP_USERPAGES)
	pcie_rkep->user_pages =
		alloc_contig_pages(RKEP_USER_MEM_SIZE >> PAGE_SHIFT, GFP_KERNEL, 0, NULL);
	if (!pcie_rkep->user_pages) {
		dev_err(&pcie_rkep->pdev->dev, "failed to allocate contiguous pages\n");
		ret = -EINVAL;
		if (pcie_rkep->dma_obj)
			pcie_dw_dmatest_unregister(pcie_rkep->dma_obj);
		goto err_register_obj;
	}
	pcie_rkep->cur_mmap_res = PCIE_EP_MMAP_RESOURCE_USER_MEM;
	dev_err(&pdev->dev, "successfully allocate continuouse buffer for userspace\n");
#endif

	pci_read_config_word(pcie_rkep->pdev, PCI_VENDOR_ID, &val);
	dev_info(&pdev->dev, "vid=%x\n", val);
	pci_read_config_word(pcie_rkep->pdev, PCI_DEVICE_ID, &val);
	dev_info(&pdev->dev, "did=%x\n", val);
	dev_info(&pdev->dev, "obj_info magic=%x, ver=%x\n", pcie_rkep->obj_info->magic,
		 pcie_rkep->obj_info->version);

	device_create_file(&pdev->dev, &dev_attr_rkep);

	return 0;
err_register_obj:
	pcie_rkep_release_irq(pcie_rkep);
err_register_irq:
	misc_deregister(&pcie_rkep->dev);
err_pci_iomap:
	if (pcie_rkep->bar0)
		pci_iounmap(pdev, pcie_rkep->bar0);
	if (pcie_rkep->bar2)
		pci_iounmap(pdev, pcie_rkep->bar2);
	if (pcie_rkep->bar4)
		pci_iounmap(pdev, pcie_rkep->bar4);
	pci_release_regions(pdev);
err_req_regions:
	pci_disable_device(pdev);
err_pci_enable_dev:

	return ret;
}

static void pcie_rkep_remove(struct pci_dev *pdev)
{
	struct pcie_rkep *pcie_rkep = pci_get_drvdata(pdev);

	if (pcie_rkep->dma_obj)
		pcie_dw_dmatest_unregister(pcie_rkep->dma_obj);

	device_remove_file(&pdev->dev, &dev_attr_rkep);
#if IS_ENABLED(CONFIG_PCIE_FUNC_RKEP_USERPAGES)
	free_contig_range(page_to_pfn(pcie_rkep->user_pages), RKEP_USER_MEM_SIZE >> PAGE_SHIFT);
#endif
	pcie_rkep_release_irq(pcie_rkep);

	if (pcie_rkep->bar0)
		pci_iounmap(pdev, pcie_rkep->bar0);
	if (pcie_rkep->bar2)
		pci_iounmap(pdev, pcie_rkep->bar2);
	if (pcie_rkep->bar4)
		pci_iounmap(pdev, pcie_rkep->bar4);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	misc_deregister(&pcie_rkep->dev);
}

static const struct pci_device_id pcie_rkep_pcidev_id[] = {
	{ PCI_VDEVICE(ROCKCHIP, 0x356a), 1,  },
	{ }
};
MODULE_DEVICE_TABLE(pcie_rkep, pcie_rkep_pcidev_id);

static struct pci_driver pcie_rkep_driver = {
	.name = DRV_NAME,
	.id_table = pcie_rkep_pcidev_id,
	.probe = pcie_rkep_probe,
	.remove = pcie_rkep_remove,
};
module_pci_driver(pcie_rkep_driver);

MODULE_DESCRIPTION("Rockchip pcie-rkep demo function driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
