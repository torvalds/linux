/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018-2020 Broadcom.
 */

#ifndef BCM_VK_H
#define BCM_VK_H

#include <linux/firmware.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/sched/signal.h>
#include <uapi/linux/misc/bcm_vk.h>

#include "bcm_vk_msg.h"

#define DRV_MODULE_NAME		"bcm-vk"

/*
 * Load Image is completed in two stages:
 *
 * 1) When the VK device boot-up, M7 CPU runs and executes the BootROM.
 * The Secure Boot Loader (SBL) as part of the BootROM will run
 * to open up ITCM for host to push BOOT1 image.
 * SBL will authenticate the image before jumping to BOOT1 image.
 *
 * 2) Because BOOT1 image is a secured image, we also called it the
 * Secure Boot Image (SBI). At second stage, SBI will initialize DDR
 * and wait for host to push BOOT2 image to DDR.
 * SBI will authenticate the image before jumping to BOOT2 image.
 *
 */
/* Location of registers of interest in BAR0 */

/* Request register for Secure Boot Loader (SBL) download */
#define BAR_CODEPUSH_SBL		0x400
/* Start of ITCM */
#define CODEPUSH_BOOT1_ENTRY		0x00400000
#define CODEPUSH_MASK		        0xfffff000
#define CODEPUSH_BOOTSTART		BIT(0)

/* Boot Status register */
#define BAR_BOOT_STATUS			0x404

#define SRAM_OPEN			BIT(16)
#define DDR_OPEN			BIT(17)

/* Firmware loader progress status definitions */
#define FW_LOADER_ACK_SEND_MORE_DATA	BIT(18)
#define FW_LOADER_ACK_IN_PROGRESS	BIT(19)
#define FW_LOADER_ACK_RCVD_ALL_DATA	BIT(20)

/* Boot1/2 is running in standalone mode */
#define BOOT_STDALONE_RUNNING		BIT(21)

/* definitions for boot status register */
#define BOOT_STATE_MASK			(0xffffffff & \
					 ~(FW_LOADER_ACK_SEND_MORE_DATA | \
					   FW_LOADER_ACK_IN_PROGRESS | \
					   BOOT_STDALONE_RUNNING))

#define BOOT_ERR_SHIFT			4
#define BOOT_ERR_MASK			(0xf << BOOT_ERR_SHIFT)
#define BOOT_PROG_MASK			0xf

#define BROM_STATUS_NOT_RUN		0x2
#define BROM_NOT_RUN			(SRAM_OPEN | BROM_STATUS_NOT_RUN)
#define BROM_STATUS_COMPLETE		0x6
#define BROM_RUNNING			(SRAM_OPEN | BROM_STATUS_COMPLETE)
#define BOOT1_STATUS_COMPLETE		0x6
#define BOOT1_RUNNING			(DDR_OPEN | BOOT1_STATUS_COMPLETE)
#define BOOT2_STATUS_COMPLETE		0x6
#define BOOT2_RUNNING			(FW_LOADER_ACK_RCVD_ALL_DATA | \
					 BOOT2_STATUS_COMPLETE)

/* Boot request for Secure Boot Image (SBI) */
#define BAR_CODEPUSH_SBI		0x408
/* 64M mapped to BAR2 */
#define CODEPUSH_BOOT2_ENTRY		0x60000000

#define BAR_CARD_STATUS			0x410

#define BAR_BOOT1_STDALONE_PROGRESS	0x420
#define BOOT1_STDALONE_SUCCESS		(BIT(13) | BIT(14))
#define BOOT1_STDALONE_PROGRESS_MASK	BOOT1_STDALONE_SUCCESS

#define BAR_METADATA_VERSION		0x440
#define BAR_OS_UPTIME			0x444
#define BAR_CHIP_ID			0x448
#define MAJOR_SOC_REV(_chip_id)		(((_chip_id) >> 20) & 0xf)

#define BAR_CARD_TEMPERATURE		0x45c

#define BAR_CARD_VOLTAGE		0x460

#define BAR_CARD_ERR_LOG		0x464

#define BAR_CARD_ERR_MEM		0x468

#define BAR_CARD_PWR_AND_THRE		0x46c

#define BAR_CARD_STATIC_INFO		0x470

#define BAR_INTF_VER			0x47c
#define BAR_INTF_VER_MAJOR_SHIFT	16
#define BAR_INTF_VER_MASK		0xffff
/*
 * major and minor semantic version numbers supported
 * Please update as required on interface changes
 */
#define SEMANTIC_MAJOR			1
#define SEMANTIC_MINOR			0

/*
 * first door bell reg, ie for queue = 0.  Only need the first one, as
 * we will use the queue number to derive the others
 */
#define VK_BAR0_REGSEG_DB_BASE		0x484
#define VK_BAR0_REGSEG_DB_REG_GAP	8 /*
					   * DB register gap,
					   * DB1 at 0x48c and DB2 at 0x494
					   */

/* reset register and specific values */
#define VK_BAR0_RESET_DB_NUM		3
#define VK_BAR0_RESET_DB_SOFT		0xffffffff
#define VK_BAR0_RESET_DB_HARD		0xfffffffd
#define VK_BAR0_RESET_RAMPDUMP		0xa0000000

#define VK_BAR0_Q_DB_BASE(q_num)	(VK_BAR0_REGSEG_DB_BASE + \
					 ((q_num) * VK_BAR0_REGSEG_DB_REG_GAP))
#define VK_BAR0_RESET_DB_BASE		(VK_BAR0_REGSEG_DB_BASE + \
					 (VK_BAR0_RESET_DB_NUM * VK_BAR0_REGSEG_DB_REG_GAP))

#define BAR_BOOTSRC_SELECT		0xc78
/* BOOTSRC definitions */
#define BOOTSRC_SOFT_ENABLE		BIT(14)

/* Card OS Firmware version size */
#define BAR_FIRMWARE_TAG_SIZE		50
#define FIRMWARE_STATUS_PRE_INIT_DONE	0x1f

/*
 * BAR1
 */

/* BAR1 message q definition */

/* indicate if msgq ctrl in BAR1 is populated */
#define VK_BAR1_MSGQ_DEF_RDY		0x60c0
/* ready marker value for the above location, normal boot2 */
#define VK_BAR1_MSGQ_RDY_MARKER		0xbeefcafe
/* ready marker value for the above location, normal boot2 */
#define VK_BAR1_DIAG_RDY_MARKER		0xdeadcafe
/* number of msgqs in BAR1 */
#define VK_BAR1_MSGQ_NR			0x60c4
/* BAR1 queue control structure offset */
#define VK_BAR1_MSGQ_CTRL_OFF		0x60c8

/* BAR1 ucode and boot1 version tag */
#define VK_BAR1_UCODE_VER_TAG		0x6170
#define VK_BAR1_BOOT1_VER_TAG		0x61b0
#define VK_BAR1_VER_TAG_SIZE		64

/* Memory to hold the DMA buffer memory address allocated for boot2 download */
#define VK_BAR1_DMA_BUF_OFF_HI		0x61e0
#define VK_BAR1_DMA_BUF_OFF_LO		(VK_BAR1_DMA_BUF_OFF_HI + 4)
#define VK_BAR1_DMA_BUF_SZ		(VK_BAR1_DMA_BUF_OFF_HI + 8)

/* Scratch memory allocated on host for VK */
#define VK_BAR1_SCRATCH_OFF_HI		0x61f0
#define VK_BAR1_SCRATCH_OFF_LO		(VK_BAR1_SCRATCH_OFF_HI + 4)
#define VK_BAR1_SCRATCH_SZ_ADDR		(VK_BAR1_SCRATCH_OFF_HI + 8)
#define VK_BAR1_SCRATCH_DEF_NR_PAGES	32

/* BAR1 DAUTH info */
#define VK_BAR1_DAUTH_BASE_ADDR		0x6200
#define VK_BAR1_DAUTH_STORE_SIZE	0x48
#define VK_BAR1_DAUTH_VALID_SIZE	0x8
#define VK_BAR1_DAUTH_MAX		4
#define VK_BAR1_DAUTH_STORE_ADDR(x) \
		(VK_BAR1_DAUTH_BASE_ADDR + \
		 (x) * (VK_BAR1_DAUTH_STORE_SIZE + VK_BAR1_DAUTH_VALID_SIZE))
#define VK_BAR1_DAUTH_VALID_ADDR(x) \
		(VK_BAR1_DAUTH_STORE_ADDR(x) + VK_BAR1_DAUTH_STORE_SIZE)

/* BAR1 SOTP AUTH and REVID info */
#define VK_BAR1_SOTP_REVID_BASE_ADDR	0x6340
#define VK_BAR1_SOTP_REVID_SIZE		0x10
#define VK_BAR1_SOTP_REVID_MAX		2
#define VK_BAR1_SOTP_REVID_ADDR(x) \
		(VK_BAR1_SOTP_REVID_BASE_ADDR + (x) * VK_BAR1_SOTP_REVID_SIZE)

/* VK device supports a maximum of 3 bars */
#define MAX_BAR	3

enum pci_barno {
	BAR_0 = 0,
	BAR_1,
	BAR_2
};

#define BCM_VK_NUM_TTY 2

/* DAUTH related info */
struct bcm_vk_dauth_key {
	char store[VK_BAR1_DAUTH_STORE_SIZE];
	char valid[VK_BAR1_DAUTH_VALID_SIZE];
};

struct bcm_vk_dauth_info {
	struct bcm_vk_dauth_key keys[VK_BAR1_DAUTH_MAX];
};

struct bcm_vk {
	struct pci_dev *pdev;
	void __iomem *bar[MAX_BAR];

	struct bcm_vk_dauth_info dauth_info;

	struct miscdevice miscdev;
	int devid; /* dev id allocated */

	/* Reference-counting to handle file operations */
	struct kref kref;

	spinlock_t ctx_lock; /* Spinlock for component context */
	struct bcm_vk_ctx ctx[VK_CMPT_CTX_MAX];
	struct bcm_vk_ht_entry pid_ht[VK_PID_HT_SZ];

	struct workqueue_struct *wq_thread;
	struct work_struct wq_work; /* work queue for deferred job */
	unsigned long wq_offload[1]; /* various flags on wq requested */
	void *tdma_vaddr; /* test dma segment virtual addr */
	dma_addr_t tdma_addr; /* test dma segment bus addr */

	struct notifier_block panic_nb;
};

/* wq offload work items bits definitions */
enum bcm_vk_wq_offload_flags {
	BCM_VK_WQ_DWNLD_PEND = 0,
	BCM_VK_WQ_DWNLD_AUTO = 1,
};

/*
 * check if PCIe interface is down on read.  Use it when it is
 * certain that _val should never be all ones.
 */
#define BCM_VK_INTF_IS_DOWN(val) ((val) == 0xffffffff)

static inline u32 vkread32(struct bcm_vk *vk, enum pci_barno bar, u64 offset)
{
	return readl(vk->bar[bar] + offset);
}

static inline void vkwrite32(struct bcm_vk *vk,
			     u32 value,
			     enum pci_barno bar,
			     u64 offset)
{
	writel(value, vk->bar[bar] + offset);
}

static inline u8 vkread8(struct bcm_vk *vk, enum pci_barno bar, u64 offset)
{
	return readb(vk->bar[bar] + offset);
}

static inline void vkwrite8(struct bcm_vk *vk,
			    u8 value,
			    enum pci_barno bar,
			    u64 offset)
{
	writeb(value, vk->bar[bar] + offset);
}

static inline bool bcm_vk_msgq_marker_valid(struct bcm_vk *vk)
{
	u32 rdy_marker = 0;
	u32 fw_status;

	fw_status = vkread32(vk, BAR_0, VK_BAR_FWSTS);

	if ((fw_status & VK_FWSTS_READY) == VK_FWSTS_READY)
		rdy_marker = vkread32(vk, BAR_1, VK_BAR1_MSGQ_DEF_RDY);

	return (rdy_marker == VK_BAR1_MSGQ_RDY_MARKER);
}

int bcm_vk_open(struct inode *inode, struct file *p_file);
int bcm_vk_release(struct inode *inode, struct file *p_file);
void bcm_vk_release_data(struct kref *kref);
int bcm_vk_auto_load_all_images(struct bcm_vk *vk);

#endif
