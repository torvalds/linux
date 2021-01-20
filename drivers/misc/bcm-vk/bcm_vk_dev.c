// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <uapi/linux/misc/bcm_vk.h>

#include "bcm_vk.h"

#define PCI_DEVICE_ID_VALKYRIE	0x5e87
#define PCI_DEVICE_ID_VIPER	0x5e88

static DEFINE_IDA(bcm_vk_ida);

enum soc_idx {
	VALKYRIE_A0 = 0,
	VALKYRIE_B0,
	VIPER,
	VK_IDX_INVALID
};

enum img_idx {
	IMG_PRI = 0,
	IMG_SEC,
	IMG_PER_TYPE_MAX
};

struct load_image_entry {
	const u32 image_type;
	const char *image_name[IMG_PER_TYPE_MAX];
};

#define NUM_BOOT_STAGES 2
/* default firmware images names */
static const struct load_image_entry image_tab[][NUM_BOOT_STAGES] = {
	[VALKYRIE_A0] = {
		{VK_IMAGE_TYPE_BOOT1, {"vk_a0-boot1.bin", "vk-boot1.bin"}},
		{VK_IMAGE_TYPE_BOOT2, {"vk_a0-boot2.bin", "vk-boot2.bin"}}
	},
	[VALKYRIE_B0] = {
		{VK_IMAGE_TYPE_BOOT1, {"vk_b0-boot1.bin", "vk-boot1.bin"}},
		{VK_IMAGE_TYPE_BOOT2, {"vk_b0-boot2.bin", "vk-boot2.bin"}}
	},

	[VIPER] = {
		{VK_IMAGE_TYPE_BOOT1, {"vp-boot1.bin", ""}},
		{VK_IMAGE_TYPE_BOOT2, {"vp-boot2.bin", ""}}
	},
};

/* Location of memory base addresses of interest in BAR1 */
/* Load Boot1 to start of ITCM */
#define BAR1_CODEPUSH_BASE_BOOT1	0x100000

/* Allow minimum 1s for Load Image timeout responses */
#define LOAD_IMAGE_TIMEOUT_MS		(1 * MSEC_PER_SEC)

/* Image startup timeouts */
#define BOOT1_STARTUP_TIMEOUT_MS	(5 * MSEC_PER_SEC)
#define BOOT2_STARTUP_TIMEOUT_MS	(10 * MSEC_PER_SEC)

/* 1ms wait for checking the transfer complete status */
#define TXFR_COMPLETE_TIMEOUT_MS	1

/* MSIX usages */
#define VK_MSIX_MSGQ_MAX		3
#define VK_MSIX_NOTF_MAX		1
#define VK_MSIX_TTY_MAX			BCM_VK_NUM_TTY
#define VK_MSIX_IRQ_MAX			(VK_MSIX_MSGQ_MAX + VK_MSIX_NOTF_MAX + \
					 VK_MSIX_TTY_MAX)
#define VK_MSIX_IRQ_MIN_REQ             (VK_MSIX_MSGQ_MAX + VK_MSIX_NOTF_MAX)

/* Number of bits set in DMA mask*/
#define BCM_VK_DMA_BITS			64

/* Ucode boot wait time */
#define BCM_VK_UCODE_BOOT_US            (100 * USEC_PER_MSEC)
/* 50% margin */
#define BCM_VK_UCODE_BOOT_MAX_US        ((BCM_VK_UCODE_BOOT_US * 3) >> 1)

/* deinit time for the card os after receiving doorbell */
#define BCM_VK_DEINIT_TIME_MS		(2 * MSEC_PER_SEC)

/*
 * module parameters
 */
static bool auto_load = true;
module_param(auto_load, bool, 0444);
MODULE_PARM_DESC(auto_load,
		 "Load images automatically at PCIe probe time.\n");
static uint nr_scratch_pages = VK_BAR1_SCRATCH_DEF_NR_PAGES;
module_param(nr_scratch_pages, uint, 0444);
MODULE_PARM_DESC(nr_scratch_pages,
		 "Number of pre allocated DMAable coherent pages.\n");

static int bcm_vk_intf_ver_chk(struct bcm_vk *vk)
{
	struct device *dev = &vk->pdev->dev;
	u32 reg;
	u16 major, minor;
	int ret = 0;

	/* read interface register */
	reg = vkread32(vk, BAR_0, BAR_INTF_VER);
	major = (reg >> BAR_INTF_VER_MAJOR_SHIFT) & BAR_INTF_VER_MASK;
	minor = reg & BAR_INTF_VER_MASK;

	/*
	 * if major number is 0, it is pre-release and it would be allowed
	 * to continue, else, check versions accordingly
	 */
	if (!major) {
		dev_warn(dev, "Pre-release major.minor=%d.%d - drv %d.%d\n",
			 major, minor, SEMANTIC_MAJOR, SEMANTIC_MINOR);
	} else if (major != SEMANTIC_MAJOR) {
		dev_err(dev,
			"Intf major.minor=%d.%d rejected - drv %d.%d\n",
			major, minor, SEMANTIC_MAJOR, SEMANTIC_MINOR);
		ret = -EPFNOSUPPORT;
	} else {
		dev_dbg(dev,
			"Intf major.minor=%d.%d passed - drv %d.%d\n",
			major, minor, SEMANTIC_MAJOR, SEMANTIC_MINOR);
	}
	return ret;
}

static inline int bcm_vk_wait(struct bcm_vk *vk, enum pci_barno bar,
			      u64 offset, u32 mask, u32 value,
			      unsigned long timeout_ms)
{
	struct device *dev = &vk->pdev->dev;
	unsigned long start_time;
	unsigned long timeout;
	u32 rd_val, boot_status;

	start_time = jiffies;
	timeout = start_time + msecs_to_jiffies(timeout_ms);

	do {
		rd_val = vkread32(vk, bar, offset);
		dev_dbg(dev, "BAR%d Offset=0x%llx: 0x%x\n",
			bar, offset, rd_val);

		/* check for any boot err condition */
		boot_status = vkread32(vk, BAR_0, BAR_BOOT_STATUS);
		if (boot_status & BOOT_ERR_MASK) {
			dev_err(dev, "Boot Err 0x%x, progress 0x%x after %d ms\n",
				(boot_status & BOOT_ERR_MASK) >> BOOT_ERR_SHIFT,
				boot_status & BOOT_PROG_MASK,
				jiffies_to_msecs(jiffies - start_time));
			return -EFAULT;
		}

		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

		cpu_relax();
		cond_resched();
	} while ((rd_val & mask) != value);

	return 0;
}

static int bcm_vk_sync_card_info(struct bcm_vk *vk)
{
	u32 rdy_marker = vkread32(vk, BAR_1, VK_BAR1_MSGQ_DEF_RDY);

	/* check for marker, but allow diags mode to skip sync */
	if (!bcm_vk_msgq_marker_valid(vk))
		return (rdy_marker == VK_BAR1_DIAG_RDY_MARKER ? 0 : -EINVAL);

	/*
	 * Write down scratch addr which is used for DMA. For
	 * signed part, BAR1 is accessible only after boot2 has come
	 * up
	 */
	if (vk->tdma_addr) {
		vkwrite32(vk, (u64)vk->tdma_addr >> 32, BAR_1,
			  VK_BAR1_SCRATCH_OFF_HI);
		vkwrite32(vk, (u32)vk->tdma_addr, BAR_1,
			  VK_BAR1_SCRATCH_OFF_LO);
		vkwrite32(vk, nr_scratch_pages * PAGE_SIZE, BAR_1,
			  VK_BAR1_SCRATCH_SZ_ADDR);
	}
	return 0;
}

static void bcm_vk_buf_notify(struct bcm_vk *vk, void *bufp,
			      dma_addr_t host_buf_addr, u32 buf_size)
{
	/* update the dma address to the card */
	vkwrite32(vk, (u64)host_buf_addr >> 32, BAR_1,
		  VK_BAR1_DMA_BUF_OFF_HI);
	vkwrite32(vk, (u32)host_buf_addr, BAR_1,
		  VK_BAR1_DMA_BUF_OFF_LO);
	vkwrite32(vk, buf_size, BAR_1, VK_BAR1_DMA_BUF_SZ);
}

static int bcm_vk_load_image_by_type(struct bcm_vk *vk, u32 load_type,
				     const char *filename)
{
	struct device *dev = &vk->pdev->dev;
	const struct firmware *fw = NULL;
	void *bufp = NULL;
	size_t max_buf, offset;
	int ret;
	u64 offset_codepush;
	u32 codepush;
	u32 value;
	dma_addr_t boot_dma_addr;
	bool is_stdalone;

	if (load_type == VK_IMAGE_TYPE_BOOT1) {
		/*
		 * After POR, enable VK soft BOOTSRC so bootrom do not clear
		 * the pushed image (the TCM memories).
		 */
		value = vkread32(vk, BAR_0, BAR_BOOTSRC_SELECT);
		value |= BOOTSRC_SOFT_ENABLE;
		vkwrite32(vk, value, BAR_0, BAR_BOOTSRC_SELECT);

		codepush = CODEPUSH_BOOTSTART + CODEPUSH_BOOT1_ENTRY;
		offset_codepush = BAR_CODEPUSH_SBL;

		/* Write a 1 to request SRAM open bit */
		vkwrite32(vk, CODEPUSH_BOOTSTART, BAR_0, offset_codepush);

		/* Wait for VK to respond */
		ret = bcm_vk_wait(vk, BAR_0, BAR_BOOT_STATUS, SRAM_OPEN,
				  SRAM_OPEN, LOAD_IMAGE_TIMEOUT_MS);
		if (ret < 0) {
			dev_err(dev, "boot1 wait SRAM err - ret(%d)\n", ret);
			goto err_buf_out;
		}

		max_buf = SZ_256K;
		bufp = dma_alloc_coherent(dev,
					  max_buf,
					  &boot_dma_addr, GFP_KERNEL);
		if (!bufp) {
			dev_err(dev, "Error allocating 0x%zx\n", max_buf);
			ret = -ENOMEM;
			goto err_buf_out;
		}
	} else if (load_type == VK_IMAGE_TYPE_BOOT2) {
		codepush = CODEPUSH_BOOT2_ENTRY;
		offset_codepush = BAR_CODEPUSH_SBI;

		/* Wait for VK to respond */
		ret = bcm_vk_wait(vk, BAR_0, BAR_BOOT_STATUS, DDR_OPEN,
				  DDR_OPEN, LOAD_IMAGE_TIMEOUT_MS);
		if (ret < 0) {
			dev_err(dev, "boot2 wait DDR open error - ret(%d)\n",
				ret);
			goto err_buf_out;
		}

		max_buf = SZ_4M;
		bufp = dma_alloc_coherent(dev,
					  max_buf,
					  &boot_dma_addr, GFP_KERNEL);
		if (!bufp) {
			dev_err(dev, "Error allocating 0x%zx\n", max_buf);
			ret = -ENOMEM;
			goto err_buf_out;
		}

		bcm_vk_buf_notify(vk, bufp, boot_dma_addr, max_buf);
	} else {
		dev_err(dev, "Error invalid image type 0x%x\n", load_type);
		ret = -EINVAL;
		goto err_buf_out;
	}

	offset = 0;
	ret = request_partial_firmware_into_buf(&fw, filename, dev,
						bufp, max_buf, offset);
	if (ret) {
		dev_err(dev, "Error %d requesting firmware file: %s\n",
			ret, filename);
		goto err_firmware_out;
	}
	dev_dbg(dev, "size=0x%zx\n", fw->size);
	if (load_type == VK_IMAGE_TYPE_BOOT1)
		memcpy_toio(vk->bar[BAR_1] + BAR1_CODEPUSH_BASE_BOOT1,
			    bufp,
			    fw->size);

	dev_dbg(dev, "Signaling 0x%x to 0x%llx\n", codepush, offset_codepush);
	vkwrite32(vk, codepush, BAR_0, offset_codepush);

	if (load_type == VK_IMAGE_TYPE_BOOT1) {
		u32 boot_status;

		/* wait until done */
		ret = bcm_vk_wait(vk, BAR_0, BAR_BOOT_STATUS,
				  BOOT1_RUNNING,
				  BOOT1_RUNNING,
				  BOOT1_STARTUP_TIMEOUT_MS);

		boot_status = vkread32(vk, BAR_0, BAR_BOOT_STATUS);
		is_stdalone = !BCM_VK_INTF_IS_DOWN(boot_status) &&
			      (boot_status & BOOT_STDALONE_RUNNING);
		if (ret && !is_stdalone) {
			dev_err(dev,
				"Timeout %ld ms waiting for boot1 to come up - ret(%d)\n",
				BOOT1_STARTUP_TIMEOUT_MS, ret);
			goto err_firmware_out;
		} else if (is_stdalone) {
			u32 reg;

			reg = vkread32(vk, BAR_0, BAR_BOOT1_STDALONE_PROGRESS);
			if ((reg & BOOT1_STDALONE_PROGRESS_MASK) ==
				     BOOT1_STDALONE_SUCCESS) {
				dev_info(dev, "Boot1 standalone success\n");
				ret = 0;
			} else {
				dev_err(dev, "Timeout %ld ms - Boot1 standalone failure\n",
					BOOT1_STARTUP_TIMEOUT_MS);
				ret = -EINVAL;
				goto err_firmware_out;
			}
		}
	} else if (load_type == VK_IMAGE_TYPE_BOOT2) {
		unsigned long timeout;

		timeout = jiffies + msecs_to_jiffies(LOAD_IMAGE_TIMEOUT_MS);

		/* To send more data to VK than max_buf allowed at a time */
		do {
			/*
			 * Check for ack from card. when Ack is received,
			 * it means all the data is received by card.
			 * Exit the loop after ack is received.
			 */
			ret = bcm_vk_wait(vk, BAR_0, BAR_BOOT_STATUS,
					  FW_LOADER_ACK_RCVD_ALL_DATA,
					  FW_LOADER_ACK_RCVD_ALL_DATA,
					  TXFR_COMPLETE_TIMEOUT_MS);
			if (ret == 0) {
				dev_dbg(dev, "Exit boot2 download\n");
				break;
			} else if (ret == -EFAULT) {
				dev_err(dev, "Error detected during ACK waiting");
				goto err_firmware_out;
			}

			/* exit the loop, if there is no response from card */
			if (time_after(jiffies, timeout)) {
				dev_err(dev, "Error. No reply from card\n");
				ret = -ETIMEDOUT;
				goto err_firmware_out;
			}

			/* Wait for VK to open BAR space to copy new data */
			ret = bcm_vk_wait(vk, BAR_0, offset_codepush,
					  codepush, 0,
					  TXFR_COMPLETE_TIMEOUT_MS);
			if (ret == 0) {
				offset += max_buf;
				ret = request_partial_firmware_into_buf
						(&fw,
						 filename,
						 dev, bufp,
						 max_buf,
						 offset);
				if (ret) {
					dev_err(dev,
						"Error %d requesting firmware file: %s offset: 0x%zx\n",
						ret, filename, offset);
					goto err_firmware_out;
				}
				dev_dbg(dev, "size=0x%zx\n", fw->size);
				dev_dbg(dev, "Signaling 0x%x to 0x%llx\n",
					codepush, offset_codepush);
				vkwrite32(vk, codepush, BAR_0, offset_codepush);
				/* reload timeout after every codepush */
				timeout = jiffies +
				    msecs_to_jiffies(LOAD_IMAGE_TIMEOUT_MS);
			} else if (ret == -EFAULT) {
				dev_err(dev, "Error detected waiting for transfer\n");
				goto err_firmware_out;
			}
		} while (1);

		/* wait for fw status bits to indicate app ready */
		ret = bcm_vk_wait(vk, BAR_0, VK_BAR_FWSTS,
				  VK_FWSTS_READY,
				  VK_FWSTS_READY,
				  BOOT2_STARTUP_TIMEOUT_MS);
		if (ret < 0) {
			dev_err(dev, "Boot2 not ready - ret(%d)\n", ret);
			goto err_firmware_out;
		}

		is_stdalone = vkread32(vk, BAR_0, BAR_BOOT_STATUS) &
			      BOOT_STDALONE_RUNNING;
		if (!is_stdalone) {
			ret = bcm_vk_intf_ver_chk(vk);
			if (ret) {
				dev_err(dev, "failure in intf version check\n");
				goto err_firmware_out;
			}

			/* sync & channel other info */
			ret = bcm_vk_sync_card_info(vk);
			if (ret) {
				dev_err(dev, "Syncing Card Info failure\n");
				goto err_firmware_out;
			}
		}
	}

err_firmware_out:
	release_firmware(fw);

err_buf_out:
	if (bufp)
		dma_free_coherent(dev, max_buf, bufp, boot_dma_addr);

	return ret;
}

static u32 bcm_vk_next_boot_image(struct bcm_vk *vk)
{
	u32 boot_status;
	u32 fw_status;
	u32 load_type = 0;  /* default for unknown */

	boot_status = vkread32(vk, BAR_0, BAR_BOOT_STATUS);
	fw_status = vkread32(vk, BAR_0, VK_BAR_FWSTS);

	if (!BCM_VK_INTF_IS_DOWN(boot_status) && (boot_status & SRAM_OPEN))
		load_type = VK_IMAGE_TYPE_BOOT1;
	else if (boot_status == BOOT1_RUNNING)
		load_type = VK_IMAGE_TYPE_BOOT2;

	/* Log status so that we know different stages */
	dev_info(&vk->pdev->dev,
		 "boot-status value for next image: 0x%x : fw-status 0x%x\n",
		 boot_status, fw_status);

	return load_type;
}

static enum soc_idx get_soc_idx(struct bcm_vk *vk)
{
	struct pci_dev *pdev = vk->pdev;
	enum soc_idx idx = VK_IDX_INVALID;
	u32 rev;
	static enum soc_idx const vk_soc_tab[] = { VALKYRIE_A0, VALKYRIE_B0 };

	switch (pdev->device) {
	case PCI_DEVICE_ID_VALKYRIE:
		/* get the chip id to decide sub-class */
		rev = MAJOR_SOC_REV(vkread32(vk, BAR_0, BAR_CHIP_ID));
		if (rev < ARRAY_SIZE(vk_soc_tab)) {
			idx = vk_soc_tab[rev];
		} else {
			/* Default to A0 firmware for all other chip revs */
			idx = VALKYRIE_A0;
			dev_warn(&pdev->dev,
				 "Rev %d not in image lookup table, default to idx=%d\n",
				 rev, idx);
		}
		break;

	case PCI_DEVICE_ID_VIPER:
		idx = VIPER;
		break;

	default:
		dev_err(&pdev->dev, "no images for 0x%x\n", pdev->device);
	}
	return idx;
}

static const char *get_load_fw_name(struct bcm_vk *vk,
				    const struct load_image_entry *entry)
{
	const struct firmware *fw;
	struct device *dev = &vk->pdev->dev;
	int ret;
	unsigned long dummy;
	int i;

	for (i = 0; i < IMG_PER_TYPE_MAX; i++) {
		fw = NULL;
		ret = request_partial_firmware_into_buf(&fw,
							entry->image_name[i],
							dev, &dummy,
							sizeof(dummy),
							0);
		release_firmware(fw);
		if (!ret)
			return entry->image_name[i];
	}
	return NULL;
}

int bcm_vk_auto_load_all_images(struct bcm_vk *vk)
{
	int i, ret = -1;
	enum soc_idx idx;
	struct device *dev = &vk->pdev->dev;
	u32 curr_type;
	const char *curr_name;

	idx = get_soc_idx(vk);
	if (idx == VK_IDX_INVALID)
		goto auto_load_all_exit;

	/* log a message to know the relative loading order */
	dev_dbg(dev, "Load All for device %d\n", vk->devid);

	for (i = 0; i < NUM_BOOT_STAGES; i++) {
		curr_type = image_tab[idx][i].image_type;
		if (bcm_vk_next_boot_image(vk) == curr_type) {
			curr_name = get_load_fw_name(vk, &image_tab[idx][i]);
			if (!curr_name) {
				dev_err(dev, "No suitable firmware exists for type %d",
					curr_type);
				ret = -ENOENT;
				goto auto_load_all_exit;
			}
			ret = bcm_vk_load_image_by_type(vk, curr_type,
							curr_name);
			dev_info(dev, "Auto load %s, ret %d\n",
				 curr_name, ret);

			if (ret) {
				dev_err(dev, "Error loading default %s\n",
					curr_name);
				goto auto_load_all_exit;
			}
		}
	}

auto_load_all_exit:
	return ret;
}

static int bcm_vk_trigger_autoload(struct bcm_vk *vk)
{
	if (test_and_set_bit(BCM_VK_WQ_DWNLD_PEND, vk->wq_offload) != 0)
		return -EPERM;

	set_bit(BCM_VK_WQ_DWNLD_AUTO, vk->wq_offload);
	queue_work(vk->wq_thread, &vk->wq_work);

	return 0;
}

/*
 * deferred work queue for auto download.
 */
static void bcm_vk_wq_handler(struct work_struct *work)
{
	struct bcm_vk *vk = container_of(work, struct bcm_vk, wq_work);

	if (test_bit(BCM_VK_WQ_DWNLD_AUTO, vk->wq_offload)) {
		bcm_vk_auto_load_all_images(vk);

		/*
		 * at the end of operation, clear AUTO bit and pending
		 * bit
		 */
		clear_bit(BCM_VK_WQ_DWNLD_AUTO, vk->wq_offload);
		clear_bit(BCM_VK_WQ_DWNLD_PEND, vk->wq_offload);
	}
}

static void bcm_to_v_reset_doorbell(struct bcm_vk *vk, u32 db_val)
{
	vkwrite32(vk, db_val, BAR_0, VK_BAR0_RESET_DB_BASE);
}

static int bcm_vk_trigger_reset(struct bcm_vk *vk)
{
	u32 i;
	u32 value, boot_status;
	static const u32 bar0_reg_clr_list[] = { BAR_OS_UPTIME,
						 BAR_INTF_VER,
						 BAR_CARD_VOLTAGE,
						 BAR_CARD_TEMPERATURE,
						 BAR_CARD_PWR_AND_THRE };

	/* make tag '\0' terminated */
	vkwrite32(vk, 0, BAR_1, VK_BAR1_BOOT1_VER_TAG);

	for (i = 0; i < VK_BAR1_DAUTH_MAX; i++) {
		vkwrite32(vk, 0, BAR_1, VK_BAR1_DAUTH_STORE_ADDR(i));
		vkwrite32(vk, 0, BAR_1, VK_BAR1_DAUTH_VALID_ADDR(i));
	}
	for (i = 0; i < VK_BAR1_SOTP_REVID_MAX; i++)
		vkwrite32(vk, 0, BAR_1, VK_BAR1_SOTP_REVID_ADDR(i));

	/*
	 * When boot request fails, the CODE_PUSH_OFFSET stays persistent.
	 * Allowing us to debug the failure. When we call reset,
	 * we should clear CODE_PUSH_OFFSET so ROM does not execute
	 * boot again (and fails again) and instead waits for a new
	 * codepush.  And, if previous boot has encountered error, need
	 * to clear the entry values
	 */
	boot_status = vkread32(vk, BAR_0, BAR_BOOT_STATUS);
	if (boot_status & BOOT_ERR_MASK) {
		dev_info(&vk->pdev->dev,
			 "Card in boot error 0x%x, clear CODEPUSH val\n",
			 boot_status);
		value = 0;
	} else {
		value = vkread32(vk, BAR_0, BAR_CODEPUSH_SBL);
		value &= CODEPUSH_MASK;
	}
	vkwrite32(vk, value, BAR_0, BAR_CODEPUSH_SBL);

	/* reset fw_status with proper reason, and press db */
	vkwrite32(vk, VK_FWSTS_RESET_MBOX_DB, BAR_0, VK_BAR_FWSTS);
	bcm_to_v_reset_doorbell(vk, VK_BAR0_RESET_DB_SOFT);

	/* clear other necessary registers records */
	for (i = 0; i < ARRAY_SIZE(bar0_reg_clr_list); i++)
		vkwrite32(vk, 0, BAR_0, bar0_reg_clr_list[i]);

	return 0;
}

static int bcm_vk_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err;
	int i;
	int id;
	int irq;
	char name[20];
	struct bcm_vk *vk;
	struct device *dev = &pdev->dev;
	struct miscdevice *misc_device;
	u32 boot_status;

	vk = kzalloc(sizeof(*vk), GFP_KERNEL);
	if (!vk)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Cannot enable PCI device\n");
		goto err_free_exit;
	}
	vk->pdev = pci_dev_get(pdev);

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		dev_err(dev, "Cannot obtain PCI resources\n");
		goto err_disable_pdev;
	}

	/* make sure DMA is good */
	err = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(BCM_VK_DMA_BITS));
	if (err) {
		dev_err(dev, "failed to set DMA mask\n");
		goto err_disable_pdev;
	}

	/* The tdma is a scratch area for some DMA testings. */
	if (nr_scratch_pages) {
		vk->tdma_vaddr = dma_alloc_coherent
					(dev,
					 nr_scratch_pages * PAGE_SIZE,
					 &vk->tdma_addr, GFP_KERNEL);
		if (!vk->tdma_vaddr) {
			err = -ENOMEM;
			goto err_disable_pdev;
		}
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, vk);

	irq = pci_alloc_irq_vectors(pdev,
				    1,
				    VK_MSIX_IRQ_MAX,
				    PCI_IRQ_MSI | PCI_IRQ_MSIX);

	if (irq < VK_MSIX_IRQ_MIN_REQ) {
		dev_err(dev, "failed to get min %d MSIX interrupts, irq(%d)\n",
			VK_MSIX_IRQ_MIN_REQ, irq);
		err = (irq >= 0) ? -EINVAL : irq;
		goto err_disable_pdev;
	}

	if (irq != VK_MSIX_IRQ_MAX)
		dev_warn(dev, "Number of IRQs %d allocated - requested(%d).\n",
			 irq, VK_MSIX_IRQ_MAX);

	for (i = 0; i < MAX_BAR; i++) {
		/* multiple by 2 for 64 bit BAR mapping */
		vk->bar[i] = pci_ioremap_bar(pdev, i * 2);
		if (!vk->bar[i]) {
			dev_err(dev, "failed to remap BAR%d\n", i);
			goto err_iounmap;
		}
	}

	id = ida_simple_get(&bcm_vk_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		err = id;
		dev_err(dev, "unable to get id\n");
		goto err_iounmap;
	}

	vk->devid = id;
	snprintf(name, sizeof(name), DRV_MODULE_NAME ".%d", id);
	misc_device = &vk->miscdev;
	misc_device->minor = MISC_DYNAMIC_MINOR;
	misc_device->name = kstrdup(name, GFP_KERNEL);
	if (!misc_device->name) {
		err = -ENOMEM;
		goto err_ida_remove;
	}

	err = misc_register(misc_device);
	if (err) {
		dev_err(dev, "failed to register device\n");
		goto err_kfree_name;
	}

	INIT_WORK(&vk->wq_work, bcm_vk_wq_handler);

	/* create dedicated workqueue */
	vk->wq_thread = create_singlethread_workqueue(name);
	if (!vk->wq_thread) {
		dev_err(dev, "Fail to create workqueue thread\n");
		err = -ENOMEM;
		goto err_misc_deregister;
	}

	/* sync other info */
	bcm_vk_sync_card_info(vk);

	/*
	 * lets trigger an auto download.  We don't want to do it serially here
	 * because at probing time, it is not supposed to block for a long time.
	 */
	boot_status = vkread32(vk, BAR_0, BAR_BOOT_STATUS);
	if (auto_load) {
		if ((boot_status & BOOT_STATE_MASK) == BROM_RUNNING) {
			if (bcm_vk_trigger_autoload(vk))
				goto err_destroy_workqueue;
		} else {
			dev_err(dev,
				"Auto-load skipped - BROM not in proper state (0x%x)\n",
				boot_status);
		}
	}

	dev_dbg(dev, "BCM-VK:%u created\n", id);

	return 0;

err_destroy_workqueue:
	destroy_workqueue(vk->wq_thread);

err_misc_deregister:
	misc_deregister(misc_device);

err_kfree_name:
	kfree(misc_device->name);
	misc_device->name = NULL;

err_ida_remove:
	ida_simple_remove(&bcm_vk_ida, id);

err_iounmap:
	for (i = 0; i < MAX_BAR; i++) {
		if (vk->bar[i])
			pci_iounmap(pdev, vk->bar[i]);
	}
	pci_release_regions(pdev);

err_disable_pdev:
	if (vk->tdma_vaddr)
		dma_free_coherent(&pdev->dev, nr_scratch_pages * PAGE_SIZE,
				  vk->tdma_vaddr, vk->tdma_addr);

	pci_free_irq_vectors(pdev);
	pci_disable_device(pdev);
	pci_dev_put(pdev);

err_free_exit:
	kfree(vk);

	return err;
}

static void bcm_vk_remove(struct pci_dev *pdev)
{
	int i;
	struct bcm_vk *vk = pci_get_drvdata(pdev);
	struct miscdevice *misc_device = &vk->miscdev;

	/*
	 * Trigger a reset to card and wait enough time for UCODE to rerun,
	 * which re-initialize the card into its default state.
	 * This ensures when driver is re-enumerated it will start from
	 * a completely clean state.
	 */
	bcm_vk_trigger_reset(vk);
	usleep_range(BCM_VK_UCODE_BOOT_US, BCM_VK_UCODE_BOOT_MAX_US);

	if (vk->tdma_vaddr)
		dma_free_coherent(&pdev->dev, nr_scratch_pages * PAGE_SIZE,
				  vk->tdma_vaddr, vk->tdma_addr);

	/* remove if name is set which means misc dev registered */
	if (misc_device->name) {
		misc_deregister(misc_device);
		kfree(misc_device->name);
		ida_simple_remove(&bcm_vk_ida, vk->devid);
	}

	cancel_work_sync(&vk->wq_work);
	destroy_workqueue(vk->wq_thread);

	for (i = 0; i < MAX_BAR; i++) {
		if (vk->bar[i])
			pci_iounmap(pdev, vk->bar[i]);
	}

	dev_dbg(&pdev->dev, "BCM-VK:%d released\n", vk->devid);

	pci_release_regions(pdev);
	pci_free_irq_vectors(pdev);
	pci_disable_device(pdev);
}

static void bcm_vk_shutdown(struct pci_dev *pdev)
{
	struct bcm_vk *vk = pci_get_drvdata(pdev);
	u32 reg, boot_stat;

	reg = vkread32(vk, BAR_0, BAR_BOOT_STATUS);
	boot_stat = reg & BOOT_STATE_MASK;

	if (boot_stat == BOOT1_RUNNING) {
		/* simply trigger a reset interrupt to park it */
		bcm_vk_trigger_reset(vk);
	} else if (boot_stat == BROM_NOT_RUN) {
		int err;
		u16 lnksta;

		/*
		 * The boot status only reflects boot condition since last reset
		 * As ucode will run only once to configure pcie, if multiple
		 * resets happen, we lost track if ucode has run or not.
		 * Here, read the current link speed and use that to
		 * sync up the bootstatus properly so that on reboot-back-up,
		 * it has the proper state to start with autoload
		 */
		err = pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnksta);
		if (!err &&
		    (lnksta & PCI_EXP_LNKSTA_CLS) != PCI_EXP_LNKSTA_CLS_2_5GB) {
			reg |= BROM_STATUS_COMPLETE;
			vkwrite32(vk, reg, BAR_0, BAR_BOOT_STATUS);
		}
	}
}

static const struct pci_device_id bcm_vk_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_VALKYRIE), },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_VIPER), },
	{ }
};
MODULE_DEVICE_TABLE(pci, bcm_vk_ids);

static struct pci_driver pci_driver = {
	.name     = DRV_MODULE_NAME,
	.id_table = bcm_vk_ids,
	.probe    = bcm_vk_probe,
	.remove   = bcm_vk_remove,
	.shutdown = bcm_vk_shutdown,
};
module_pci_driver(pci_driver);

MODULE_DESCRIPTION("Broadcom VK Host Driver");
MODULE_AUTHOR("Scott Branden <scott.branden@broadcom.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
