// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/mutex.h>
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
static uint nr_ib_sgl_blk = BCM_VK_DEF_IB_SGL_BLK_LEN;
module_param(nr_ib_sgl_blk, uint, 0444);
MODULE_PARM_DESC(nr_ib_sgl_blk,
		 "Number of in-band msg blks for short SGL.\n");

/*
 * alerts that could be generated from peer
 */
const struct bcm_vk_entry bcm_vk_peer_err[BCM_VK_PEER_ERR_NUM] = {
	{ERR_LOG_UECC, ERR_LOG_UECC, "uecc"},
	{ERR_LOG_SSIM_BUSY, ERR_LOG_SSIM_BUSY, "ssim_busy"},
	{ERR_LOG_AFBC_BUSY, ERR_LOG_AFBC_BUSY, "afbc_busy"},
	{ERR_LOG_HIGH_TEMP_ERR, ERR_LOG_HIGH_TEMP_ERR, "high_temp"},
	{ERR_LOG_WDOG_TIMEOUT, ERR_LOG_WDOG_TIMEOUT, "wdog_timeout"},
	{ERR_LOG_SYS_FAULT, ERR_LOG_SYS_FAULT, "sys_fault"},
	{ERR_LOG_RAMDUMP, ERR_LOG_RAMDUMP, "ramdump"},
	{ERR_LOG_COP_WDOG_TIMEOUT, ERR_LOG_COP_WDOG_TIMEOUT,
	 "cop_wdog_timeout"},
	{ERR_LOG_MEM_ALLOC_FAIL, ERR_LOG_MEM_ALLOC_FAIL, "malloc_fail warn"},
	{ERR_LOG_LOW_TEMP_WARN, ERR_LOG_LOW_TEMP_WARN, "low_temp warn"},
	{ERR_LOG_ECC, ERR_LOG_ECC, "ecc"},
	{ERR_LOG_IPC_DWN, ERR_LOG_IPC_DWN, "ipc_down"},
};

/* alerts detected by the host */
const struct bcm_vk_entry bcm_vk_host_err[BCM_VK_HOST_ERR_NUM] = {
	{ERR_LOG_HOST_PCIE_DWN, ERR_LOG_HOST_PCIE_DWN, "PCIe_down"},
	{ERR_LOG_HOST_HB_FAIL, ERR_LOG_HOST_HB_FAIL, "hb_fail"},
	{ERR_LOG_HOST_INTF_V_FAIL, ERR_LOG_HOST_INTF_V_FAIL, "intf_ver_fail"},
};

irqreturn_t bcm_vk_notf_irqhandler(int irq, void *dev_id)
{
	struct bcm_vk *vk = dev_id;

	if (!bcm_vk_drv_access_ok(vk)) {
		dev_err(&vk->pdev->dev,
			"Interrupt %d received when msgq not inited\n", irq);
		goto skip_schedule_work;
	}

	/* if notification is not pending, set bit and schedule work */
	if (test_and_set_bit(BCM_VK_WQ_NOTF_PEND, vk->wq_offload) == 0)
		queue_work(vk->wq_thread, &vk->wq_work);

skip_schedule_work:
	return IRQ_HANDLED;
}

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
		bcm_vk_set_host_alert(vk, ERR_LOG_HOST_INTF_V_FAIL);
		ret = -EPFNOSUPPORT;
	} else {
		dev_dbg(dev,
			"Intf major.minor=%d.%d passed - drv %d.%d\n",
			major, minor, SEMANTIC_MAJOR, SEMANTIC_MINOR);
	}
	return ret;
}

static void bcm_vk_log_notf(struct bcm_vk *vk,
			    struct bcm_vk_alert *alert,
			    struct bcm_vk_entry const *entry_tab,
			    const u32 table_size)
{
	u32 i;
	u32 masked_val, latched_val;
	struct bcm_vk_entry const *entry;
	u32 reg;
	u16 ecc_mem_err, uecc_mem_err;
	struct device *dev = &vk->pdev->dev;

	for (i = 0; i < table_size; i++) {
		entry = &entry_tab[i];
		masked_val = entry->mask & alert->notfs;
		latched_val = entry->mask & alert->flags;

		if (masked_val == ERR_LOG_UECC) {
			/*
			 * if there is difference between stored cnt and it
			 * is greater than threshold, log it.
			 */
			reg = vkread32(vk, BAR_0, BAR_CARD_ERR_MEM);
			BCM_VK_EXTRACT_FIELD(uecc_mem_err, reg,
					     BCM_VK_MEM_ERR_FIELD_MASK,
					     BCM_VK_UECC_MEM_ERR_SHIFT);
			if ((uecc_mem_err != vk->alert_cnts.uecc) &&
			    (uecc_mem_err >= BCM_VK_UECC_THRESHOLD))
				dev_info(dev,
					 "ALERT! %s.%d uecc RAISED - ErrCnt %d\n",
					 DRV_MODULE_NAME, vk->devid,
					 uecc_mem_err);
			vk->alert_cnts.uecc = uecc_mem_err;
		} else if (masked_val == ERR_LOG_ECC) {
			reg = vkread32(vk, BAR_0, BAR_CARD_ERR_MEM);
			BCM_VK_EXTRACT_FIELD(ecc_mem_err, reg,
					     BCM_VK_MEM_ERR_FIELD_MASK,
					     BCM_VK_ECC_MEM_ERR_SHIFT);
			if ((ecc_mem_err != vk->alert_cnts.ecc) &&
			    (ecc_mem_err >= BCM_VK_ECC_THRESHOLD))
				dev_info(dev, "ALERT! %s.%d ecc RAISED - ErrCnt %d\n",
					 DRV_MODULE_NAME, vk->devid,
					 ecc_mem_err);
			vk->alert_cnts.ecc = ecc_mem_err;
		} else if (masked_val != latched_val) {
			/* print a log as info */
			dev_info(dev, "ALERT! %s.%d %s %s\n",
				 DRV_MODULE_NAME, vk->devid, entry->str,
				 masked_val ? "RAISED" : "CLEARED");
		}
	}
}

static void bcm_vk_dump_peer_log(struct bcm_vk *vk)
{
	struct bcm_vk_peer_log log;
	struct bcm_vk_peer_log *log_info = &vk->peerlog_info;
	char loc_buf[BCM_VK_PEER_LOG_LINE_MAX];
	int cnt;
	struct device *dev = &vk->pdev->dev;
	unsigned int data_offset;

	memcpy_fromio(&log, vk->bar[BAR_2] + vk->peerlog_off, sizeof(log));

	dev_dbg(dev, "Peer PANIC: Size 0x%x(0x%x), [Rd Wr] = [%d %d]\n",
		log.buf_size, log.mask, log.rd_idx, log.wr_idx);

	if (!log_info->buf_size) {
		dev_err(dev, "Peer log dump disabled - skipped!\n");
		return;
	}

	/* perform range checking for rd/wr idx */
	if ((log.rd_idx > log_info->mask) ||
	    (log.wr_idx > log_info->mask) ||
	    (log.buf_size != log_info->buf_size) ||
	    (log.mask != log_info->mask)) {
		dev_err(dev,
			"Corrupted Ptrs: Size 0x%x(0x%x) Mask 0x%x(0x%x) [Rd Wr] = [%d %d], skip log dump.\n",
			log_info->buf_size, log.buf_size,
			log_info->mask, log.mask,
			log.rd_idx, log.wr_idx);
		return;
	}

	cnt = 0;
	data_offset = vk->peerlog_off + sizeof(struct bcm_vk_peer_log);
	loc_buf[BCM_VK_PEER_LOG_LINE_MAX - 1] = '\0';
	while (log.rd_idx != log.wr_idx) {
		loc_buf[cnt] = vkread8(vk, BAR_2, data_offset + log.rd_idx);

		if ((loc_buf[cnt] == '\0') ||
		    (cnt == (BCM_VK_PEER_LOG_LINE_MAX - 1))) {
			dev_err(dev, "%s", loc_buf);
			cnt = 0;
		} else {
			cnt++;
		}
		log.rd_idx = (log.rd_idx + 1) & log.mask;
	}
	/* update rd idx at the end */
	vkwrite32(vk, log.rd_idx, BAR_2,
		  vk->peerlog_off + offsetof(struct bcm_vk_peer_log, rd_idx));
}

void bcm_vk_handle_notf(struct bcm_vk *vk)
{
	u32 reg;
	struct bcm_vk_alert alert;
	bool intf_down;
	unsigned long flags;

	/* handle peer alerts and then locally detected ones */
	reg = vkread32(vk, BAR_0, BAR_CARD_ERR_LOG);
	intf_down = BCM_VK_INTF_IS_DOWN(reg);
	if (!intf_down) {
		vk->peer_alert.notfs = reg;
		bcm_vk_log_notf(vk, &vk->peer_alert, bcm_vk_peer_err,
				ARRAY_SIZE(bcm_vk_peer_err));
		vk->peer_alert.flags = vk->peer_alert.notfs;
	} else {
		/* turn off access */
		bcm_vk_blk_drv_access(vk);
	}

	/* check and make copy of alert with lock and then free lock */
	spin_lock_irqsave(&vk->host_alert_lock, flags);
	if (intf_down)
		vk->host_alert.notfs |= ERR_LOG_HOST_PCIE_DWN;

	alert = vk->host_alert;
	vk->host_alert.flags = vk->host_alert.notfs;
	spin_unlock_irqrestore(&vk->host_alert_lock, flags);

	/* call display with copy */
	bcm_vk_log_notf(vk, &alert, bcm_vk_host_err,
			ARRAY_SIZE(bcm_vk_host_err));

	/*
	 * If it is a sys fault or heartbeat timeout, we would like extract
	 * log msg from the card so that we would know what is the last fault
	 */
	if (!intf_down &&
	    ((vk->host_alert.flags & ERR_LOG_HOST_HB_FAIL) ||
	     (vk->peer_alert.flags & ERR_LOG_SYS_FAULT)))
		bcm_vk_dump_peer_log(vk);
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

static void bcm_vk_get_card_info(struct bcm_vk *vk)
{
	struct device *dev = &vk->pdev->dev;
	u32 offset;
	int i;
	u8 *dst;
	struct bcm_vk_card_info *info = &vk->card_info;

	/* first read the offset from spare register */
	offset = vkread32(vk, BAR_0, BAR_CARD_STATIC_INFO);
	offset &= (pci_resource_len(vk->pdev, BAR_2 * 2) - 1);

	/* based on the offset, read info to internal card info structure */
	dst = (u8 *)info;
	for (i = 0; i < sizeof(*info); i++)
		*dst++ = vkread8(vk, BAR_2, offset++);

#define CARD_INFO_LOG_FMT "version   : %x\n" \
			  "os_tag    : %s\n" \
			  "cmpt_tag  : %s\n" \
			  "cpu_freq  : %d MHz\n" \
			  "cpu_scale : %d full, %d lowest\n" \
			  "ddr_freq  : %d MHz\n" \
			  "ddr_size  : %d MB\n" \
			  "video_freq: %d MHz\n"
	dev_dbg(dev, CARD_INFO_LOG_FMT, info->version, info->os_tag,
		info->cmpt_tag, info->cpu_freq_mhz, info->cpu_scale[0],
		info->cpu_scale[MAX_OPP - 1], info->ddr_freq_mhz,
		info->ddr_size_MB, info->video_core_freq_mhz);

	/*
	 * get the peer log pointer, only need the offset, and get record
	 * of the log buffer information which would be used for checking
	 * before dump, in case the BAR2 memory has been corrupted.
	 */
	vk->peerlog_off = offset;
	memcpy_fromio(&vk->peerlog_info, vk->bar[BAR_2] + vk->peerlog_off,
		      sizeof(vk->peerlog_info));

	/*
	 * Do a range checking and if out of bound, the record will be zeroed
	 * which guarantees that nothing would be dumped.  In other words,
	 * peer dump is disabled.
	 */
	if ((vk->peerlog_info.buf_size > BCM_VK_PEER_LOG_BUF_MAX) ||
	    (vk->peerlog_info.mask != (vk->peerlog_info.buf_size - 1)) ||
	    (vk->peerlog_info.rd_idx > vk->peerlog_info.mask) ||
	    (vk->peerlog_info.wr_idx > vk->peerlog_info.mask)) {
		dev_err(dev, "Peer log disabled - range error: Size 0x%x(0x%x), [Rd Wr] = [%d %d]\n",
			vk->peerlog_info.buf_size,
			vk->peerlog_info.mask,
			vk->peerlog_info.rd_idx,
			vk->peerlog_info.wr_idx);
		memset(&vk->peerlog_info, 0, sizeof(vk->peerlog_info));
	} else {
		dev_dbg(dev, "Peer log: Size 0x%x(0x%x), [Rd Wr] = [%d %d]\n",
			vk->peerlog_info.buf_size,
			vk->peerlog_info.mask,
			vk->peerlog_info.rd_idx,
			vk->peerlog_info.wr_idx);
	}
}

static void bcm_vk_get_proc_mon_info(struct bcm_vk *vk)
{
	struct device *dev = &vk->pdev->dev;
	struct bcm_vk_proc_mon_info *mon = &vk->proc_mon_info;
	u32 num, entry_size, offset, buf_size;
	u8 *dst;

	/* calculate offset which is based on peerlog offset */
	buf_size = vkread32(vk, BAR_2,
			    vk->peerlog_off
			    + offsetof(struct bcm_vk_peer_log, buf_size));
	offset = vk->peerlog_off + sizeof(struct bcm_vk_peer_log)
		 + buf_size;

	/* first read the num and entry size */
	num = vkread32(vk, BAR_2, offset);
	entry_size = vkread32(vk, BAR_2, offset + sizeof(num));

	/* check for max allowed */
	if (num > BCM_VK_PROC_MON_MAX) {
		dev_err(dev, "Processing monitoring entry %d exceeds max %d\n",
			num, BCM_VK_PROC_MON_MAX);
		return;
	}
	mon->num = num;
	mon->entry_size = entry_size;

	vk->proc_mon_off = offset;

	/* read it once that will capture those static info */
	dst = (u8 *)&mon->entries[0];
	offset += sizeof(num) + sizeof(entry_size);
	memcpy_fromio(dst, vk->bar[BAR_2] + offset, num * entry_size);
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

	/* get static card info, only need to read once */
	bcm_vk_get_card_info(vk);

	/* get the proc mon info once */
	bcm_vk_get_proc_mon_info(vk);

	return 0;
}

void bcm_vk_blk_drv_access(struct bcm_vk *vk)
{
	int i;

	/*
	 * kill all the apps except for the process that is resetting.
	 * If not called during reset, reset_pid will be 0, and all will be
	 * killed.
	 */
	spin_lock(&vk->ctx_lock);

	/* set msgq_inited to 0 so that all rd/wr will be blocked */
	atomic_set(&vk->msgq_inited, 0);

	for (i = 0; i < VK_PID_HT_SZ; i++) {
		struct bcm_vk_ctx *ctx;

		list_for_each_entry(ctx, &vk->pid_ht[i].head, node) {
			if (ctx->pid != vk->reset_pid) {
				dev_dbg(&vk->pdev->dev,
					"Send kill signal to pid %d\n",
					ctx->pid);
				kill_pid(find_vpid(ctx->pid), SIGKILL, 1);
			}
		}
	}
	bcm_vk_tty_terminate_tty_user(vk);
	spin_unlock(&vk->ctx_lock);
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

			/*
			 * Next, initialize Message Q if we are loading boot2.
			 * Do a force sync
			 */
			ret = bcm_vk_sync_msgq(vk, true);
			if (ret) {
				dev_err(dev, "Boot2 Error reading comm msg Q info\n");
				ret = -EIO;
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
 * deferred work queue for draining and auto download.
 */
static void bcm_vk_wq_handler(struct work_struct *work)
{
	struct bcm_vk *vk = container_of(work, struct bcm_vk, wq_work);
	struct device *dev = &vk->pdev->dev;
	s32 ret;

	/* check wq offload bit map to perform various operations */
	if (test_bit(BCM_VK_WQ_NOTF_PEND, vk->wq_offload)) {
		/* clear bit right the way for notification */
		clear_bit(BCM_VK_WQ_NOTF_PEND, vk->wq_offload);
		bcm_vk_handle_notf(vk);
	}
	if (test_bit(BCM_VK_WQ_DWNLD_AUTO, vk->wq_offload)) {
		bcm_vk_auto_load_all_images(vk);

		/*
		 * at the end of operation, clear AUTO bit and pending
		 * bit
		 */
		clear_bit(BCM_VK_WQ_DWNLD_AUTO, vk->wq_offload);
		clear_bit(BCM_VK_WQ_DWNLD_PEND, vk->wq_offload);
	}

	/* next, try to drain */
	ret = bcm_to_h_msg_dequeue(vk);

	if (ret == 0)
		dev_dbg(dev, "Spurious trigger for workqueue\n");
	else if (ret < 0)
		bcm_vk_blk_drv_access(vk);
}

static long bcm_vk_load_image(struct bcm_vk *vk,
			      const struct vk_image __user *arg)
{
	struct device *dev = &vk->pdev->dev;
	const char *image_name;
	struct vk_image image;
	u32 next_loadable;
	enum soc_idx idx;
	int image_idx;
	int ret = -EPERM;

	if (copy_from_user(&image, arg, sizeof(image)))
		return -EACCES;

	if ((image.type != VK_IMAGE_TYPE_BOOT1) &&
	    (image.type != VK_IMAGE_TYPE_BOOT2)) {
		dev_err(dev, "invalid image.type %u\n", image.type);
		return ret;
	}

	next_loadable = bcm_vk_next_boot_image(vk);
	if (next_loadable != image.type) {
		dev_err(dev, "Next expected image %u, Loading %u\n",
			next_loadable, image.type);
		return ret;
	}

	/*
	 * if something is pending download already.  This could only happen
	 * for now when the driver is being loaded, or if someone has issued
	 * another download command in another shell.
	 */
	if (test_and_set_bit(BCM_VK_WQ_DWNLD_PEND, vk->wq_offload) != 0) {
		dev_err(dev, "Download operation already pending.\n");
		return ret;
	}

	image_name = image.filename;
	if (image_name[0] == '\0') {
		/* Use default image name if NULL */
		idx = get_soc_idx(vk);
		if (idx == VK_IDX_INVALID)
			goto err_idx;

		/* Image idx starts with boot1 */
		image_idx = image.type - VK_IMAGE_TYPE_BOOT1;
		image_name = get_load_fw_name(vk, &image_tab[idx][image_idx]);
		if (!image_name) {
			dev_err(dev, "No suitable image found for type %d",
				image.type);
			ret = -ENOENT;
			goto err_idx;
		}
	} else {
		/* Ensure filename is NULL terminated */
		image.filename[sizeof(image.filename) - 1] = '\0';
	}
	ret = bcm_vk_load_image_by_type(vk, image.type, image_name);
	dev_info(dev, "Load %s, ret %d\n", image_name, ret);
err_idx:
	clear_bit(BCM_VK_WQ_DWNLD_PEND, vk->wq_offload);

	return ret;
}

static int bcm_vk_reset_successful(struct bcm_vk *vk)
{
	struct device *dev = &vk->pdev->dev;
	u32 fw_status, reset_reason;
	int ret = -EAGAIN;

	/*
	 * Reset could be triggered when the card in several state:
	 *   i)   in bootROM
	 *   ii)  after boot1
	 *   iii) boot2 running
	 *
	 * i) & ii) - no status bits will be updated.  If vkboot1
	 * runs automatically after reset, it  will update the reason
	 * to be unknown reason
	 * iii) - reboot reason match + deinit done.
	 */
	fw_status = vkread32(vk, BAR_0, VK_BAR_FWSTS);
	/* immediate exit if interface goes down */
	if (BCM_VK_INTF_IS_DOWN(fw_status)) {
		dev_err(dev, "PCIe Intf Down!\n");
		goto reset_exit;
	}

	reset_reason = (fw_status & VK_FWSTS_RESET_REASON_MASK);
	if ((reset_reason == VK_FWSTS_RESET_MBOX_DB) ||
	    (reset_reason == VK_FWSTS_RESET_UNKNOWN))
		ret = 0;

	/*
	 * if some of the deinit bits are set, but done
	 * bit is not, this is a failure if triggered while boot2 is running
	 */
	if ((fw_status & VK_FWSTS_DEINIT_TRIGGERED) &&
	    !(fw_status & VK_FWSTS_RESET_DONE))
		ret = -EAGAIN;

reset_exit:
	dev_dbg(dev, "FW status = 0x%x ret %d\n", fw_status, ret);

	return ret;
}

static void bcm_to_v_reset_doorbell(struct bcm_vk *vk, u32 db_val)
{
	vkwrite32(vk, db_val, BAR_0, VK_BAR0_RESET_DB_BASE);
}

static int bcm_vk_trigger_reset(struct bcm_vk *vk)
{
	u32 i;
	u32 value, boot_status;
	bool is_stdalone, is_boot2;
	static const u32 bar0_reg_clr_list[] = { BAR_OS_UPTIME,
						 BAR_INTF_VER,
						 BAR_CARD_VOLTAGE,
						 BAR_CARD_TEMPERATURE,
						 BAR_CARD_PWR_AND_THRE };

	/* clean up before pressing the door bell */
	bcm_vk_drain_msg_on_reset(vk);
	vkwrite32(vk, 0, BAR_1, VK_BAR1_MSGQ_DEF_RDY);
	/* make tag '\0' terminated */
	vkwrite32(vk, 0, BAR_1, VK_BAR1_BOOT1_VER_TAG);

	for (i = 0; i < VK_BAR1_DAUTH_MAX; i++) {
		vkwrite32(vk, 0, BAR_1, VK_BAR1_DAUTH_STORE_ADDR(i));
		vkwrite32(vk, 0, BAR_1, VK_BAR1_DAUTH_VALID_ADDR(i));
	}
	for (i = 0; i < VK_BAR1_SOTP_REVID_MAX; i++)
		vkwrite32(vk, 0, BAR_1, VK_BAR1_SOTP_REVID_ADDR(i));

	memset(&vk->card_info, 0, sizeof(vk->card_info));
	memset(&vk->peerlog_info, 0, sizeof(vk->peerlog_info));
	memset(&vk->proc_mon_info, 0, sizeof(vk->proc_mon_info));
	memset(&vk->alert_cnts, 0, sizeof(vk->alert_cnts));

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

	/* special reset handling */
	is_stdalone = boot_status & BOOT_STDALONE_RUNNING;
	is_boot2 = (boot_status & BOOT_STATE_MASK) == BOOT2_RUNNING;
	if (vk->peer_alert.flags & ERR_LOG_RAMDUMP) {
		/*
		 * if card is in ramdump mode, it is hitting an error.  Don't
		 * reset the reboot reason as it will contain valid info that
		 * is important - simply use special reset
		 */
		vkwrite32(vk, VK_BAR0_RESET_RAMPDUMP, BAR_0, VK_BAR_FWSTS);
		return VK_BAR0_RESET_RAMPDUMP;
	} else if (is_stdalone && !is_boot2) {
		dev_info(&vk->pdev->dev, "Hard reset on Standalone mode");
		bcm_to_v_reset_doorbell(vk, VK_BAR0_RESET_DB_HARD);
		return VK_BAR0_RESET_DB_HARD;
	}

	/* reset fw_status with proper reason, and press db */
	vkwrite32(vk, VK_FWSTS_RESET_MBOX_DB, BAR_0, VK_BAR_FWSTS);
	bcm_to_v_reset_doorbell(vk, VK_BAR0_RESET_DB_SOFT);

	/* clear other necessary registers and alert records */
	for (i = 0; i < ARRAY_SIZE(bar0_reg_clr_list); i++)
		vkwrite32(vk, 0, BAR_0, bar0_reg_clr_list[i]);
	memset(&vk->host_alert, 0, sizeof(vk->host_alert));
	memset(&vk->peer_alert, 0, sizeof(vk->peer_alert));
	/* clear 4096 bits of bitmap */
	bitmap_clear(vk->bmap, 0, VK_MSG_ID_BITMAP_SIZE);

	return 0;
}

static long bcm_vk_reset(struct bcm_vk *vk, struct vk_reset __user *arg)
{
	struct device *dev = &vk->pdev->dev;
	struct vk_reset reset;
	int ret = 0;
	u32 ramdump_reset;
	int special_reset;

	if (copy_from_user(&reset, arg, sizeof(struct vk_reset)))
		return -EFAULT;

	/* check if any download is in-progress, if so return error */
	if (test_and_set_bit(BCM_VK_WQ_DWNLD_PEND, vk->wq_offload) != 0) {
		dev_err(dev, "Download operation pending - skip reset.\n");
		return -EPERM;
	}

	ramdump_reset = vk->peer_alert.flags & ERR_LOG_RAMDUMP;
	dev_info(dev, "Issue Reset %s\n",
		 ramdump_reset ? "in ramdump mode" : "");

	/*
	 * The following is the sequence of reset:
	 * - send card level graceful shut down
	 * - wait enough time for VK to handle its business, stopping DMA etc
	 * - kill host apps
	 * - Trigger interrupt with DB
	 */
	bcm_vk_send_shutdown_msg(vk, VK_SHUTDOWN_GRACEFUL, 0, 0);

	spin_lock(&vk->ctx_lock);
	if (!vk->reset_pid) {
		vk->reset_pid = task_pid_nr(current);
	} else {
		dev_err(dev, "Reset already launched by process pid %d\n",
			vk->reset_pid);
		ret = -EACCES;
	}
	spin_unlock(&vk->ctx_lock);
	if (ret)
		goto err_exit;

	bcm_vk_blk_drv_access(vk);
	special_reset = bcm_vk_trigger_reset(vk);

	/*
	 * Wait enough time for card os to deinit
	 * and populate the reset reason.
	 */
	msleep(BCM_VK_DEINIT_TIME_MS);

	if (special_reset) {
		/* if it is special ramdump reset, return the type to user */
		reset.arg2 = special_reset;
		if (copy_to_user(arg, &reset, sizeof(reset)))
			ret = -EFAULT;
	} else {
		ret = bcm_vk_reset_successful(vk);
	}

err_exit:
	clear_bit(BCM_VK_WQ_DWNLD_PEND, vk->wq_offload);
	return ret;
}

static int bcm_vk_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct bcm_vk_ctx *ctx = file->private_data;
	struct bcm_vk *vk = container_of(ctx->miscdev, struct bcm_vk, miscdev);
	unsigned long pg_size;

	/* only BAR2 is mmap possible, which is bar num 4 due to 64bit */
#define VK_MMAPABLE_BAR 4

	pg_size = ((pci_resource_len(vk->pdev, VK_MMAPABLE_BAR) - 1)
		    >> PAGE_SHIFT) + 1;
	if (vma->vm_pgoff + vma_pages(vma) > pg_size)
		return -EINVAL;

	vma->vm_pgoff += (pci_resource_start(vk->pdev, VK_MMAPABLE_BAR)
			  >> PAGE_SHIFT);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				  vma->vm_end - vma->vm_start,
				  vma->vm_page_prot);
}

static long bcm_vk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -EINVAL;
	struct bcm_vk_ctx *ctx = file->private_data;
	struct bcm_vk *vk = container_of(ctx->miscdev, struct bcm_vk, miscdev);
	void __user *argp = (void __user *)arg;

	dev_dbg(&vk->pdev->dev,
		"ioctl, cmd=0x%02x, arg=0x%02lx\n",
		cmd, arg);

	mutex_lock(&vk->mutex);

	switch (cmd) {
	case VK_IOCTL_LOAD_IMAGE:
		ret = bcm_vk_load_image(vk, argp);
		break;

	case VK_IOCTL_RESET:
		ret = bcm_vk_reset(vk, argp);
		break;

	default:
		break;
	}

	mutex_unlock(&vk->mutex);

	return ret;
}

static const struct file_operations bcm_vk_fops = {
	.owner = THIS_MODULE,
	.open = bcm_vk_open,
	.read = bcm_vk_read,
	.write = bcm_vk_write,
	.poll = bcm_vk_poll,
	.release = bcm_vk_release,
	.mmap = bcm_vk_mmap,
	.unlocked_ioctl = bcm_vk_ioctl,
};

static int bcm_vk_on_panic(struct notifier_block *nb,
			   unsigned long e, void *p)
{
	struct bcm_vk *vk = container_of(nb, struct bcm_vk, panic_nb);

	bcm_to_v_reset_doorbell(vk, VK_BAR0_RESET_DB_HARD);

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

	/* allocate vk structure which is tied to kref for freeing */
	vk = kzalloc(sizeof(*vk), GFP_KERNEL);
	if (!vk)
		return -ENOMEM;

	kref_init(&vk->kref);
	if (nr_ib_sgl_blk > BCM_VK_IB_SGL_BLK_MAX) {
		dev_warn(dev, "Inband SGL blk %d limited to max %d\n",
			 nr_ib_sgl_blk, BCM_VK_IB_SGL_BLK_MAX);
		nr_ib_sgl_blk = BCM_VK_IB_SGL_BLK_MAX;
	}
	vk->ib_sgl_size = nr_ib_sgl_blk * VK_MSGQ_BLK_SIZE;
	mutex_init(&vk->mutex);

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
			err = -ENOMEM;
			goto err_iounmap;
		}
	}

	for (vk->num_irqs = 0;
	     vk->num_irqs < VK_MSIX_MSGQ_MAX;
	     vk->num_irqs++) {
		err = devm_request_irq(dev, pci_irq_vector(pdev, vk->num_irqs),
				       bcm_vk_msgq_irqhandler,
				       IRQF_SHARED, DRV_MODULE_NAME, vk);
		if (err) {
			dev_err(dev, "failed to request msgq IRQ %d for MSIX %d\n",
				pdev->irq + vk->num_irqs, vk->num_irqs + 1);
			goto err_irq;
		}
	}
	/* one irq for notification from VK */
	err = devm_request_irq(dev, pci_irq_vector(pdev, vk->num_irqs),
			       bcm_vk_notf_irqhandler,
			       IRQF_SHARED, DRV_MODULE_NAME, vk);
	if (err) {
		dev_err(dev, "failed to request notf IRQ %d for MSIX %d\n",
			pdev->irq + vk->num_irqs, vk->num_irqs + 1);
		goto err_irq;
	}
	vk->num_irqs++;

	for (i = 0;
	     (i < VK_MSIX_TTY_MAX) && (vk->num_irqs < irq);
	     i++, vk->num_irqs++) {
		err = devm_request_irq(dev, pci_irq_vector(pdev, vk->num_irqs),
				       bcm_vk_tty_irqhandler,
				       IRQF_SHARED, DRV_MODULE_NAME, vk);
		if (err) {
			dev_err(dev, "failed request tty IRQ %d for MSIX %d\n",
				pdev->irq + vk->num_irqs, vk->num_irqs + 1);
			goto err_irq;
		}
		bcm_vk_tty_set_irq_enabled(vk, i);
	}

	id = ida_simple_get(&bcm_vk_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		err = id;
		dev_err(dev, "unable to get id\n");
		goto err_irq;
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
	misc_device->fops = &bcm_vk_fops,

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

	err = bcm_vk_msg_init(vk);
	if (err) {
		dev_err(dev, "failed to init msg queue info\n");
		goto err_destroy_workqueue;
	}

	/* sync other info */
	bcm_vk_sync_card_info(vk);

	/* register for panic notifier */
	vk->panic_nb.notifier_call = bcm_vk_on_panic;
	err = atomic_notifier_chain_register(&panic_notifier_list,
					     &vk->panic_nb);
	if (err) {
		dev_err(dev, "Fail to register panic notifier\n");
		goto err_destroy_workqueue;
	}

	snprintf(name, sizeof(name), KBUILD_MODNAME ".%d_ttyVK", id);
	err = bcm_vk_tty_init(vk, name);
	if (err)
		goto err_unregister_panic_notifier;

	/*
	 * lets trigger an auto download.  We don't want to do it serially here
	 * because at probing time, it is not supposed to block for a long time.
	 */
	boot_status = vkread32(vk, BAR_0, BAR_BOOT_STATUS);
	if (auto_load) {
		if ((boot_status & BOOT_STATE_MASK) == BROM_RUNNING) {
			err = bcm_vk_trigger_autoload(vk);
			if (err)
				goto err_bcm_vk_tty_exit;
		} else {
			dev_err(dev,
				"Auto-load skipped - BROM not in proper state (0x%x)\n",
				boot_status);
		}
	}

	/* enable hb */
	bcm_vk_hb_init(vk);

	dev_dbg(dev, "BCM-VK:%u created\n", id);

	return 0;

err_bcm_vk_tty_exit:
	bcm_vk_tty_exit(vk);

err_unregister_panic_notifier:
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &vk->panic_nb);

err_destroy_workqueue:
	destroy_workqueue(vk->wq_thread);

err_misc_deregister:
	misc_deregister(misc_device);

err_kfree_name:
	kfree(misc_device->name);
	misc_device->name = NULL;

err_ida_remove:
	ida_simple_remove(&bcm_vk_ida, id);

err_irq:
	for (i = 0; i < vk->num_irqs; i++)
		devm_free_irq(dev, pci_irq_vector(pdev, i), vk);

	pci_disable_msix(pdev);
	pci_disable_msi(pdev);

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

void bcm_vk_release_data(struct kref *kref)
{
	struct bcm_vk *vk = container_of(kref, struct bcm_vk, kref);
	struct pci_dev *pdev = vk->pdev;

	dev_dbg(&pdev->dev, "BCM-VK:%d release data 0x%p\n", vk->devid, vk);
	pci_dev_put(pdev);
	kfree(vk);
}

static void bcm_vk_remove(struct pci_dev *pdev)
{
	int i;
	struct bcm_vk *vk = pci_get_drvdata(pdev);
	struct miscdevice *misc_device = &vk->miscdev;

	bcm_vk_hb_deinit(vk);

	/*
	 * Trigger a reset to card and wait enough time for UCODE to rerun,
	 * which re-initialize the card into its default state.
	 * This ensures when driver is re-enumerated it will start from
	 * a completely clean state.
	 */
	bcm_vk_trigger_reset(vk);
	usleep_range(BCM_VK_UCODE_BOOT_US, BCM_VK_UCODE_BOOT_MAX_US);

	/* unregister panic notifier */
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &vk->panic_nb);

	bcm_vk_msg_remove(vk);
	bcm_vk_tty_exit(vk);

	if (vk->tdma_vaddr)
		dma_free_coherent(&pdev->dev, nr_scratch_pages * PAGE_SIZE,
				  vk->tdma_vaddr, vk->tdma_addr);

	/* remove if name is set which means misc dev registered */
	if (misc_device->name) {
		misc_deregister(misc_device);
		kfree(misc_device->name);
		ida_simple_remove(&bcm_vk_ida, vk->devid);
	}
	for (i = 0; i < vk->num_irqs; i++)
		devm_free_irq(&pdev->dev, pci_irq_vector(pdev, i), vk);

	pci_disable_msix(pdev);
	pci_disable_msi(pdev);

	cancel_work_sync(&vk->wq_work);
	destroy_workqueue(vk->wq_thread);
	bcm_vk_tty_wq_exit(vk);

	for (i = 0; i < MAX_BAR; i++) {
		if (vk->bar[i])
			pci_iounmap(pdev, vk->bar[i]);
	}

	dev_dbg(&pdev->dev, "BCM-VK:%d released\n", vk->devid);

	pci_release_regions(pdev);
	pci_free_irq_vectors(pdev);
	pci_disable_device(pdev);

	kref_put(&vk->kref, bcm_vk_release_data);
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
