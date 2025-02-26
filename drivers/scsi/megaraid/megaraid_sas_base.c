// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2003-2013  LSI Corporation
 *  Copyright (c) 2013-2016  Avago Technologies
 *  Copyright (c) 2016-2018  Broadcom Inc.
 *
 *  Authors: Broadcom Inc.
 *           Sreenivas Bagalkote
 *           Sumant Patro
 *           Bo Yang
 *           Adam Radford
 *           Kashyap Desai <kashyap.desai@broadcom.com>
 *           Sumit Saxena <sumit.saxena@broadcom.com>
 *
 *  Send feedback to: megaraidlinux.pdl@broadcom.com
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/unaligned.h>
#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/irq_poll.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dbg.h>
#include "megaraid_sas_fusion.h"
#include "megaraid_sas.h"

/*
 * Number of sectors per IO command
 * Will be set in megasas_init_mfi if user does not provide
 */
static unsigned int max_sectors;
module_param_named(max_sectors, max_sectors, int, 0444);
MODULE_PARM_DESC(max_sectors,
	"Maximum number of sectors per IO command");

static int msix_disable;
module_param(msix_disable, int, 0444);
MODULE_PARM_DESC(msix_disable, "Disable MSI-X interrupt handling. Default: 0");

static unsigned int msix_vectors;
module_param(msix_vectors, int, 0444);
MODULE_PARM_DESC(msix_vectors, "MSI-X max vector count. Default: Set by FW");

static int allow_vf_ioctls;
module_param(allow_vf_ioctls, int, 0444);
MODULE_PARM_DESC(allow_vf_ioctls, "Allow ioctls in SR-IOV VF mode. Default: 0");

static unsigned int throttlequeuedepth = MEGASAS_THROTTLE_QUEUE_DEPTH;
module_param(throttlequeuedepth, int, 0444);
MODULE_PARM_DESC(throttlequeuedepth,
	"Adapter queue depth when throttled due to I/O timeout. Default: 16");

unsigned int resetwaittime = MEGASAS_RESET_WAIT_TIME;
module_param(resetwaittime, int, 0444);
MODULE_PARM_DESC(resetwaittime, "Wait time in (1-180s) after I/O timeout before resetting adapter. Default: 180s");

static int smp_affinity_enable = 1;
module_param(smp_affinity_enable, int, 0444);
MODULE_PARM_DESC(smp_affinity_enable, "SMP affinity feature enable/disable Default: enable(1)");

static int rdpq_enable = 1;
module_param(rdpq_enable, int, 0444);
MODULE_PARM_DESC(rdpq_enable, "Allocate reply queue in chunks for large queue depth enable/disable Default: enable(1)");

unsigned int dual_qdepth_disable;
module_param(dual_qdepth_disable, int, 0444);
MODULE_PARM_DESC(dual_qdepth_disable, "Disable dual queue depth feature. Default: 0");

static unsigned int scmd_timeout = MEGASAS_DEFAULT_CMD_TIMEOUT;
module_param(scmd_timeout, int, 0444);
MODULE_PARM_DESC(scmd_timeout, "scsi command timeout (10-90s), default 90s. See megasas_reset_timer.");

int perf_mode = -1;
module_param(perf_mode, int, 0444);
MODULE_PARM_DESC(perf_mode, "Performance mode (only for Aero adapters), options:\n\t\t"
		"0 - balanced: High iops and low latency queues are allocated &\n\t\t"
		"interrupt coalescing is enabled only on high iops queues\n\t\t"
		"1 - iops: High iops queues are not allocated &\n\t\t"
		"interrupt coalescing is enabled on all queues\n\t\t"
		"2 - latency: High iops queues are not allocated &\n\t\t"
		"interrupt coalescing is disabled on all queues\n\t\t"
		"default mode is 'balanced'"
		);

int event_log_level = MFI_EVT_CLASS_CRITICAL;
module_param(event_log_level, int, 0644);
MODULE_PARM_DESC(event_log_level, "Asynchronous event logging level- range is: -2(CLASS_DEBUG) to 4(CLASS_DEAD), Default: 2(CLASS_CRITICAL)");

unsigned int enable_sdev_max_qd;
module_param(enable_sdev_max_qd, int, 0444);
MODULE_PARM_DESC(enable_sdev_max_qd, "Enable sdev max qd as can_queue. Default: 0");

int poll_queues;
module_param(poll_queues, int, 0444);
MODULE_PARM_DESC(poll_queues, "Number of queues to be use for io_uring poll mode.\n\t\t"
		"This parameter is effective only if host_tagset_enable=1 &\n\t\t"
		"It is not applicable for MFI_SERIES. &\n\t\t"
		"Driver will work in latency mode. &\n\t\t"
		"High iops queues are not allocated &\n\t\t"
		);

int host_tagset_enable = 1;
module_param(host_tagset_enable, int, 0444);
MODULE_PARM_DESC(host_tagset_enable, "Shared host tagset enable/disable Default: enable(1)");

MODULE_LICENSE("GPL");
MODULE_VERSION(MEGASAS_VERSION);
MODULE_AUTHOR("megaraidlinux.pdl@broadcom.com");
MODULE_DESCRIPTION("Broadcom MegaRAID SAS Driver");

int megasas_transition_to_ready(struct megasas_instance *instance, int ocr);
static int megasas_get_pd_list(struct megasas_instance *instance);
static int megasas_ld_list_query(struct megasas_instance *instance,
				 u8 query_type);
static int megasas_issue_init_mfi(struct megasas_instance *instance);
static int megasas_register_aen(struct megasas_instance *instance,
				u32 seq_num, u32 class_locale_word);
static void megasas_get_pd_info(struct megasas_instance *instance,
				struct scsi_device *sdev);
static void
megasas_set_ld_removed_by_fw(struct megasas_instance *instance);

/*
 * PCI ID table for all supported controllers
 */
static const struct pci_device_id megasas_pci_table[] = {

	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1064R)},
	/* xscale IOP */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1078R)},
	/* ppc IOP */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1078DE)},
	/* ppc IOP */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1078GEN2)},
	/* gen2*/
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS0079GEN2)},
	/* gen2*/
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS0073SKINNY)},
	/* skinny*/
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS0071SKINNY)},
	/* skinny*/
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_VERDE_ZCR)},
	/* xscale IOP, vega */
	{PCI_DEVICE(PCI_VENDOR_ID_DELL, PCI_DEVICE_ID_DELL_PERC5)},
	/* xscale IOP */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FUSION)},
	/* Fusion */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_PLASMA)},
	/* Plasma */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_INVADER)},
	/* Invader */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FURY)},
	/* Fury */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_INTRUDER)},
	/* Intruder */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_INTRUDER_24)},
	/* Intruder 24 port*/
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_CUTLASS_52)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_CUTLASS_53)},
	/* VENTURA */
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_VENTURA)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_CRUSADER)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_HARPOON)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_TOMCAT)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_VENTURA_4PORT)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_CRUSADER_4PORT)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E1)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E2)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E5)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E6)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E0)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E3)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E4)},
	{PCI_DEVICE(PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_AERO_10E7)},
	{}
};

MODULE_DEVICE_TABLE(pci, megasas_pci_table);

static int megasas_mgmt_majorno;
struct megasas_mgmt_info megasas_mgmt_info;
static struct fasync_struct *megasas_async_queue;
static DEFINE_MUTEX(megasas_async_queue_mutex);

static int megasas_poll_wait_aen;
static DECLARE_WAIT_QUEUE_HEAD(megasas_poll_wait);
static u32 support_poll_for_event;
u32 megasas_dbg_lvl;
static u32 support_device_change;
static bool support_nvme_encapsulation;
static bool support_pci_lane_margining;

/* define lock for aen poll */
static DEFINE_SPINLOCK(poll_aen_lock);

extern struct dentry *megasas_debugfs_root;
extern int megasas_blk_mq_poll(struct Scsi_Host *shost, unsigned int queue_num);

void
megasas_complete_cmd(struct megasas_instance *instance, struct megasas_cmd *cmd,
		     u8 alt_status);
static u32
megasas_read_fw_status_reg_gen2(struct megasas_instance *instance);
static int
megasas_adp_reset_gen2(struct megasas_instance *instance,
		       struct megasas_register_set __iomem *reg_set);
static irqreturn_t megasas_isr(int irq, void *devp);
static u32
megasas_init_adapter_mfi(struct megasas_instance *instance);
u32
megasas_build_and_issue_cmd(struct megasas_instance *instance,
			    struct scsi_cmnd *scmd);
static void megasas_complete_cmd_dpc(unsigned long instance_addr);
int
wait_and_poll(struct megasas_instance *instance, struct megasas_cmd *cmd,
	int seconds);
void megasas_fusion_ocr_wq(struct work_struct *work);
static int megasas_get_ld_vf_affiliation(struct megasas_instance *instance,
					 int initial);
static int
megasas_set_dma_mask(struct megasas_instance *instance);
static int
megasas_alloc_ctrl_mem(struct megasas_instance *instance);
static inline void
megasas_free_ctrl_mem(struct megasas_instance *instance);
static inline int
megasas_alloc_ctrl_dma_buffers(struct megasas_instance *instance);
static inline void
megasas_free_ctrl_dma_buffers(struct megasas_instance *instance);
static inline void
megasas_init_ctrl_params(struct megasas_instance *instance);

u32 megasas_readl(struct megasas_instance *instance,
		  const volatile void __iomem *addr)
{
	u32 i = 0, ret_val;
	/*
	 * Due to a HW errata in Aero controllers, reads to certain
	 * Fusion registers could intermittently return all zeroes.
	 * This behavior is transient in nature and subsequent reads will
	 * return valid value. As a workaround in driver, retry readl for
	 * up to thirty times until a non-zero value is read.
	 */
	if (instance->adapter_type == AERO_SERIES) {
		do {
			ret_val = readl(addr);
			i++;
		} while (ret_val == 0 && i < 30);
		return ret_val;
	} else {
		return readl(addr);
	}
}

/**
 * megasas_set_dma_settings -	Populate DMA address, length and flags for DCMDs
 * @instance:			Adapter soft state
 * @dcmd:			DCMD frame inside MFI command
 * @dma_addr:			DMA address of buffer to be passed to FW
 * @dma_len:			Length of DMA buffer to be passed to FW
 * @return:			void
 */
void megasas_set_dma_settings(struct megasas_instance *instance,
			      struct megasas_dcmd_frame *dcmd,
			      dma_addr_t dma_addr, u32 dma_len)
{
	if (instance->consistent_mask_64bit) {
		dcmd->sgl.sge64[0].phys_addr = cpu_to_le64(dma_addr);
		dcmd->sgl.sge64[0].length = cpu_to_le32(dma_len);
		dcmd->flags = cpu_to_le16(dcmd->flags | MFI_FRAME_SGL64);

	} else {
		dcmd->sgl.sge32[0].phys_addr =
				cpu_to_le32(lower_32_bits(dma_addr));
		dcmd->sgl.sge32[0].length = cpu_to_le32(dma_len);
		dcmd->flags = cpu_to_le16(dcmd->flags);
	}
}

static void
megasas_issue_dcmd(struct megasas_instance *instance, struct megasas_cmd *cmd)
{
	instance->instancet->fire_cmd(instance,
		cmd->frame_phys_addr, 0, instance->reg_set);
	return;
}

/**
 * megasas_get_cmd -	Get a command from the free pool
 * @instance:		Adapter soft state
 *
 * Returns a free command from the pool
 */
struct megasas_cmd *megasas_get_cmd(struct megasas_instance
						  *instance)
{
	unsigned long flags;
	struct megasas_cmd *cmd = NULL;

	spin_lock_irqsave(&instance->mfi_pool_lock, flags);

	if (!list_empty(&instance->cmd_pool)) {
		cmd = list_entry((&instance->cmd_pool)->next,
				 struct megasas_cmd, list);
		list_del_init(&cmd->list);
	} else {
		dev_err(&instance->pdev->dev, "Command pool empty!\n");
	}

	spin_unlock_irqrestore(&instance->mfi_pool_lock, flags);
	return cmd;
}

/**
 * megasas_return_cmd -	Return a cmd to free command pool
 * @instance:		Adapter soft state
 * @cmd:		Command packet to be returned to free command pool
 */
void
megasas_return_cmd(struct megasas_instance *instance, struct megasas_cmd *cmd)
{
	unsigned long flags;
	u32 blk_tags;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion = instance->ctrl_context;

	/* This flag is used only for fusion adapter.
	 * Wait for Interrupt for Polled mode DCMD
	 */
	if (cmd->flags & DRV_DCMD_POLLED_MODE)
		return;

	spin_lock_irqsave(&instance->mfi_pool_lock, flags);

	if (fusion) {
		blk_tags = instance->max_scsi_cmds + cmd->index;
		cmd_fusion = fusion->cmd_list[blk_tags];
		megasas_return_cmd_fusion(instance, cmd_fusion);
	}
	cmd->scmd = NULL;
	cmd->frame_count = 0;
	cmd->flags = 0;
	memset(cmd->frame, 0, instance->mfi_frame_size);
	cmd->frame->io.context = cpu_to_le32(cmd->index);
	if (!fusion && reset_devices)
		cmd->frame->hdr.cmd = MFI_CMD_INVALID;
	list_add(&cmd->list, (&instance->cmd_pool)->next);

	spin_unlock_irqrestore(&instance->mfi_pool_lock, flags);

}

static const char *
format_timestamp(uint32_t timestamp)
{
	static char buffer[32];

	if ((timestamp & 0xff000000) == 0xff000000)
		snprintf(buffer, sizeof(buffer), "boot + %us", timestamp &
		0x00ffffff);
	else
		snprintf(buffer, sizeof(buffer), "%us", timestamp);
	return buffer;
}

static const char *
format_class(int8_t class)
{
	static char buffer[6];

	switch (class) {
	case MFI_EVT_CLASS_DEBUG:
		return "debug";
	case MFI_EVT_CLASS_PROGRESS:
		return "progress";
	case MFI_EVT_CLASS_INFO:
		return "info";
	case MFI_EVT_CLASS_WARNING:
		return "WARN";
	case MFI_EVT_CLASS_CRITICAL:
		return "CRIT";
	case MFI_EVT_CLASS_FATAL:
		return "FATAL";
	case MFI_EVT_CLASS_DEAD:
		return "DEAD";
	default:
		snprintf(buffer, sizeof(buffer), "%d", class);
		return buffer;
	}
}

/**
  * megasas_decode_evt: Decode FW AEN event and print critical event
  * for information.
  * @instance:			Adapter soft state
  */
static void
megasas_decode_evt(struct megasas_instance *instance)
{
	struct megasas_evt_detail *evt_detail = instance->evt_detail;
	union megasas_evt_class_locale class_locale;
	class_locale.word = le32_to_cpu(evt_detail->cl.word);

	if ((event_log_level < MFI_EVT_CLASS_DEBUG) ||
	    (event_log_level > MFI_EVT_CLASS_DEAD)) {
		printk(KERN_WARNING "megaraid_sas: provided event log level is out of range, setting it to default 2(CLASS_CRITICAL), permissible range is: -2 to 4\n");
		event_log_level = MFI_EVT_CLASS_CRITICAL;
	}

	if (class_locale.members.class >= event_log_level)
		dev_info(&instance->pdev->dev, "%d (%s/0x%04x/%s) - %s\n",
			le32_to_cpu(evt_detail->seq_num),
			format_timestamp(le32_to_cpu(evt_detail->time_stamp)),
			(class_locale.members.locale),
			format_class(class_locale.members.class),
			evt_detail->description);

	if (megasas_dbg_lvl & LD_PD_DEBUG)
		dev_info(&instance->pdev->dev,
			 "evt_detail.args.ld.target_id/index %d/%d\n",
			 evt_detail->args.ld.target_id, evt_detail->args.ld.ld_index);

}

/*
 * The following functions are defined for xscale
 * (deviceid : 1064R, PERC5) controllers
 */

/**
 * megasas_enable_intr_xscale -	Enables interrupts
 * @instance:	Adapter soft state
 */
static inline void
megasas_enable_intr_xscale(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;

	regs = instance->reg_set;
	writel(0, &(regs)->outbound_intr_mask);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_disable_intr_xscale -Disables interrupt
 * @instance:	Adapter soft state
 */
static inline void
megasas_disable_intr_xscale(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;
	u32 mask = 0x1f;

	regs = instance->reg_set;
	writel(mask, &regs->outbound_intr_mask);
	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_read_fw_status_reg_xscale - returns the current FW status value
 * @instance:	Adapter soft state
 */
static u32
megasas_read_fw_status_reg_xscale(struct megasas_instance *instance)
{
	return readl(&instance->reg_set->outbound_msg_0);
}
/**
 * megasas_clear_intr_xscale -	Check & clear interrupt
 * @instance:	Adapter soft state
 */
static int
megasas_clear_intr_xscale(struct megasas_instance *instance)
{
	u32 status;
	u32 mfiStatus = 0;
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;

	/*
	 * Check if it is our interrupt
	 */
	status = readl(&regs->outbound_intr_status);

	if (status & MFI_OB_INTR_STATUS_MASK)
		mfiStatus = MFI_INTR_FLAG_REPLY_MESSAGE;
	if (status & MFI_XSCALE_OMR0_CHANGE_INTERRUPT)
		mfiStatus |= MFI_INTR_FLAG_FIRMWARE_STATE_CHANGE;

	/*
	 * Clear the interrupt by writing back the same value
	 */
	if (mfiStatus)
		writel(status, &regs->outbound_intr_status);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_status);

	return mfiStatus;
}

/**
 * megasas_fire_cmd_xscale -	Sends command to the FW
 * @instance:		Adapter soft state
 * @frame_phys_addr :	Physical address of cmd
 * @frame_count :	Number of frames for the command
 * @regs :		MFI register set
 */
static inline void
megasas_fire_cmd_xscale(struct megasas_instance *instance,
		dma_addr_t frame_phys_addr,
		u32 frame_count,
		struct megasas_register_set __iomem *regs)
{
	unsigned long flags;

	spin_lock_irqsave(&instance->hba_lock, flags);
	writel((frame_phys_addr >> 3)|(frame_count),
	       &(regs)->inbound_queue_port);
	spin_unlock_irqrestore(&instance->hba_lock, flags);
}

/**
 * megasas_adp_reset_xscale -  For controller reset
 * @instance:	Adapter soft state
 * @regs:	MFI register set
 */
static int
megasas_adp_reset_xscale(struct megasas_instance *instance,
	struct megasas_register_set __iomem *regs)
{
	u32 i;
	u32 pcidata;

	writel(MFI_ADP_RESET, &regs->inbound_doorbell);

	for (i = 0; i < 3; i++)
		msleep(1000); /* sleep for 3 secs */
	pcidata  = 0;
	pci_read_config_dword(instance->pdev, MFI_1068_PCSR_OFFSET, &pcidata);
	dev_notice(&instance->pdev->dev, "pcidata = %x\n", pcidata);
	if (pcidata & 0x2) {
		dev_notice(&instance->pdev->dev, "mfi 1068 offset read=%x\n", pcidata);
		pcidata &= ~0x2;
		pci_write_config_dword(instance->pdev,
				MFI_1068_PCSR_OFFSET, pcidata);

		for (i = 0; i < 2; i++)
			msleep(1000); /* need to wait 2 secs again */

		pcidata  = 0;
		pci_read_config_dword(instance->pdev,
				MFI_1068_FW_HANDSHAKE_OFFSET, &pcidata);
		dev_notice(&instance->pdev->dev, "1068 offset handshake read=%x\n", pcidata);
		if ((pcidata & 0xffff0000) == MFI_1068_FW_READY) {
			dev_notice(&instance->pdev->dev, "1068 offset pcidt=%x\n", pcidata);
			pcidata = 0;
			pci_write_config_dword(instance->pdev,
				MFI_1068_FW_HANDSHAKE_OFFSET, pcidata);
		}
	}
	return 0;
}

/**
 * megasas_check_reset_xscale -	For controller reset check
 * @instance:	Adapter soft state
 * @regs:	MFI register set
 */
static int
megasas_check_reset_xscale(struct megasas_instance *instance,
		struct megasas_register_set __iomem *regs)
{
	if ((atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL) &&
	    (le32_to_cpu(*instance->consumer) ==
		MEGASAS_ADPRESET_INPROG_SIGN))
		return 1;
	return 0;
}

static struct megasas_instance_template megasas_instance_template_xscale = {

	.fire_cmd = megasas_fire_cmd_xscale,
	.enable_intr = megasas_enable_intr_xscale,
	.disable_intr = megasas_disable_intr_xscale,
	.clear_intr = megasas_clear_intr_xscale,
	.read_fw_status_reg = megasas_read_fw_status_reg_xscale,
	.adp_reset = megasas_adp_reset_xscale,
	.check_reset = megasas_check_reset_xscale,
	.service_isr = megasas_isr,
	.tasklet = megasas_complete_cmd_dpc,
	.init_adapter = megasas_init_adapter_mfi,
	.build_and_issue_cmd = megasas_build_and_issue_cmd,
	.issue_dcmd = megasas_issue_dcmd,
};

/*
 * This is the end of set of functions & definitions specific
 * to xscale (deviceid : 1064R, PERC5) controllers
 */

/*
 * The following functions are defined for ppc (deviceid : 0x60)
 * controllers
 */

/**
 * megasas_enable_intr_ppc -	Enables interrupts
 * @instance:	Adapter soft state
 */
static inline void
megasas_enable_intr_ppc(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;

	regs = instance->reg_set;
	writel(0xFFFFFFFF, &(regs)->outbound_doorbell_clear);

	writel(~0x80000000, &(regs)->outbound_intr_mask);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_disable_intr_ppc -	Disable interrupt
 * @instance:	Adapter soft state
 */
static inline void
megasas_disable_intr_ppc(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;
	u32 mask = 0xFFFFFFFF;

	regs = instance->reg_set;
	writel(mask, &regs->outbound_intr_mask);
	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_read_fw_status_reg_ppc - returns the current FW status value
 * @instance:	Adapter soft state
 */
static u32
megasas_read_fw_status_reg_ppc(struct megasas_instance *instance)
{
	return readl(&instance->reg_set->outbound_scratch_pad_0);
}

/**
 * megasas_clear_intr_ppc -	Check & clear interrupt
 * @instance:	Adapter soft state
 */
static int
megasas_clear_intr_ppc(struct megasas_instance *instance)
{
	u32 status, mfiStatus = 0;
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;

	/*
	 * Check if it is our interrupt
	 */
	status = readl(&regs->outbound_intr_status);

	if (status & MFI_REPLY_1078_MESSAGE_INTERRUPT)
		mfiStatus = MFI_INTR_FLAG_REPLY_MESSAGE;

	if (status & MFI_G2_OUTBOUND_DOORBELL_CHANGE_INTERRUPT)
		mfiStatus |= MFI_INTR_FLAG_FIRMWARE_STATE_CHANGE;

	/*
	 * Clear the interrupt by writing back the same value
	 */
	writel(status, &regs->outbound_doorbell_clear);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_doorbell_clear);

	return mfiStatus;
}

/**
 * megasas_fire_cmd_ppc -	Sends command to the FW
 * @instance:		Adapter soft state
 * @frame_phys_addr:	Physical address of cmd
 * @frame_count:	Number of frames for the command
 * @regs:		MFI register set
 */
static inline void
megasas_fire_cmd_ppc(struct megasas_instance *instance,
		dma_addr_t frame_phys_addr,
		u32 frame_count,
		struct megasas_register_set __iomem *regs)
{
	unsigned long flags;

	spin_lock_irqsave(&instance->hba_lock, flags);
	writel((frame_phys_addr | (frame_count<<1))|1,
			&(regs)->inbound_queue_port);
	spin_unlock_irqrestore(&instance->hba_lock, flags);
}

/**
 * megasas_check_reset_ppc -	For controller reset check
 * @instance:	Adapter soft state
 * @regs:	MFI register set
 */
static int
megasas_check_reset_ppc(struct megasas_instance *instance,
			struct megasas_register_set __iomem *regs)
{
	if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL)
		return 1;

	return 0;
}

static struct megasas_instance_template megasas_instance_template_ppc = {

	.fire_cmd = megasas_fire_cmd_ppc,
	.enable_intr = megasas_enable_intr_ppc,
	.disable_intr = megasas_disable_intr_ppc,
	.clear_intr = megasas_clear_intr_ppc,
	.read_fw_status_reg = megasas_read_fw_status_reg_ppc,
	.adp_reset = megasas_adp_reset_xscale,
	.check_reset = megasas_check_reset_ppc,
	.service_isr = megasas_isr,
	.tasklet = megasas_complete_cmd_dpc,
	.init_adapter = megasas_init_adapter_mfi,
	.build_and_issue_cmd = megasas_build_and_issue_cmd,
	.issue_dcmd = megasas_issue_dcmd,
};

/**
 * megasas_enable_intr_skinny -	Enables interrupts
 * @instance:	Adapter soft state
 */
static inline void
megasas_enable_intr_skinny(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;

	regs = instance->reg_set;
	writel(0xFFFFFFFF, &(regs)->outbound_intr_mask);

	writel(~MFI_SKINNY_ENABLE_INTERRUPT_MASK, &(regs)->outbound_intr_mask);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_disable_intr_skinny -	Disables interrupt
 * @instance:	Adapter soft state
 */
static inline void
megasas_disable_intr_skinny(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;
	u32 mask = 0xFFFFFFFF;

	regs = instance->reg_set;
	writel(mask, &regs->outbound_intr_mask);
	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_read_fw_status_reg_skinny - returns the current FW status value
 * @instance:	Adapter soft state
 */
static u32
megasas_read_fw_status_reg_skinny(struct megasas_instance *instance)
{
	return readl(&instance->reg_set->outbound_scratch_pad_0);
}

/**
 * megasas_clear_intr_skinny -	Check & clear interrupt
 * @instance:	Adapter soft state
 */
static int
megasas_clear_intr_skinny(struct megasas_instance *instance)
{
	u32 status;
	u32 mfiStatus = 0;
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;

	/*
	 * Check if it is our interrupt
	 */
	status = readl(&regs->outbound_intr_status);

	if (!(status & MFI_SKINNY_ENABLE_INTERRUPT_MASK)) {
		return 0;
	}

	/*
	 * Check if it is our interrupt
	 */
	if ((megasas_read_fw_status_reg_skinny(instance) & MFI_STATE_MASK) ==
	    MFI_STATE_FAULT) {
		mfiStatus = MFI_INTR_FLAG_FIRMWARE_STATE_CHANGE;
	} else
		mfiStatus = MFI_INTR_FLAG_REPLY_MESSAGE;

	/*
	 * Clear the interrupt by writing back the same value
	 */
	writel(status, &regs->outbound_intr_status);

	/*
	 * dummy read to flush PCI
	 */
	readl(&regs->outbound_intr_status);

	return mfiStatus;
}

/**
 * megasas_fire_cmd_skinny -	Sends command to the FW
 * @instance:		Adapter soft state
 * @frame_phys_addr:	Physical address of cmd
 * @frame_count:	Number of frames for the command
 * @regs:		MFI register set
 */
static inline void
megasas_fire_cmd_skinny(struct megasas_instance *instance,
			dma_addr_t frame_phys_addr,
			u32 frame_count,
			struct megasas_register_set __iomem *regs)
{
	unsigned long flags;

	spin_lock_irqsave(&instance->hba_lock, flags);
	writel(upper_32_bits(frame_phys_addr),
	       &(regs)->inbound_high_queue_port);
	writel((lower_32_bits(frame_phys_addr) | (frame_count<<1))|1,
	       &(regs)->inbound_low_queue_port);
	spin_unlock_irqrestore(&instance->hba_lock, flags);
}

/**
 * megasas_check_reset_skinny -	For controller reset check
 * @instance:	Adapter soft state
 * @regs:	MFI register set
 */
static int
megasas_check_reset_skinny(struct megasas_instance *instance,
				struct megasas_register_set __iomem *regs)
{
	if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL)
		return 1;

	return 0;
}

static struct megasas_instance_template megasas_instance_template_skinny = {

	.fire_cmd = megasas_fire_cmd_skinny,
	.enable_intr = megasas_enable_intr_skinny,
	.disable_intr = megasas_disable_intr_skinny,
	.clear_intr = megasas_clear_intr_skinny,
	.read_fw_status_reg = megasas_read_fw_status_reg_skinny,
	.adp_reset = megasas_adp_reset_gen2,
	.check_reset = megasas_check_reset_skinny,
	.service_isr = megasas_isr,
	.tasklet = megasas_complete_cmd_dpc,
	.init_adapter = megasas_init_adapter_mfi,
	.build_and_issue_cmd = megasas_build_and_issue_cmd,
	.issue_dcmd = megasas_issue_dcmd,
};


/*
 * The following functions are defined for gen2 (deviceid : 0x78 0x79)
 * controllers
 */

/**
 * megasas_enable_intr_gen2 -  Enables interrupts
 * @instance:	Adapter soft state
 */
static inline void
megasas_enable_intr_gen2(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;

	regs = instance->reg_set;
	writel(0xFFFFFFFF, &(regs)->outbound_doorbell_clear);

	/* write ~0x00000005 (4 & 1) to the intr mask*/
	writel(~MFI_GEN2_ENABLE_INTERRUPT_MASK, &(regs)->outbound_intr_mask);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_disable_intr_gen2 - Disables interrupt
 * @instance:	Adapter soft state
 */
static inline void
megasas_disable_intr_gen2(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;
	u32 mask = 0xFFFFFFFF;

	regs = instance->reg_set;
	writel(mask, &regs->outbound_intr_mask);
	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_read_fw_status_reg_gen2 - returns the current FW status value
 * @instance:	Adapter soft state
 */
static u32
megasas_read_fw_status_reg_gen2(struct megasas_instance *instance)
{
	return readl(&instance->reg_set->outbound_scratch_pad_0);
}

/**
 * megasas_clear_intr_gen2 -      Check & clear interrupt
 * @instance:	Adapter soft state
 */
static int
megasas_clear_intr_gen2(struct megasas_instance *instance)
{
	u32 status;
	u32 mfiStatus = 0;
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;

	/*
	 * Check if it is our interrupt
	 */
	status = readl(&regs->outbound_intr_status);

	if (status & MFI_INTR_FLAG_REPLY_MESSAGE) {
		mfiStatus = MFI_INTR_FLAG_REPLY_MESSAGE;
	}
	if (status & MFI_G2_OUTBOUND_DOORBELL_CHANGE_INTERRUPT) {
		mfiStatus |= MFI_INTR_FLAG_FIRMWARE_STATE_CHANGE;
	}

	/*
	 * Clear the interrupt by writing back the same value
	 */
	if (mfiStatus)
		writel(status, &regs->outbound_doorbell_clear);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_status);

	return mfiStatus;
}

/**
 * megasas_fire_cmd_gen2 -     Sends command to the FW
 * @instance:		Adapter soft state
 * @frame_phys_addr:	Physical address of cmd
 * @frame_count:	Number of frames for the command
 * @regs:		MFI register set
 */
static inline void
megasas_fire_cmd_gen2(struct megasas_instance *instance,
			dma_addr_t frame_phys_addr,
			u32 frame_count,
			struct megasas_register_set __iomem *regs)
{
	unsigned long flags;

	spin_lock_irqsave(&instance->hba_lock, flags);
	writel((frame_phys_addr | (frame_count<<1))|1,
			&(regs)->inbound_queue_port);
	spin_unlock_irqrestore(&instance->hba_lock, flags);
}

/**
 * megasas_adp_reset_gen2 -	For controller reset
 * @instance:	Adapter soft state
 * @reg_set:	MFI register set
 */
static int
megasas_adp_reset_gen2(struct megasas_instance *instance,
			struct megasas_register_set __iomem *reg_set)
{
	u32 retry = 0 ;
	u32 HostDiag;
	u32 __iomem *seq_offset = &reg_set->seq_offset;
	u32 __iomem *hostdiag_offset = &reg_set->host_diag;

	if (instance->instancet == &megasas_instance_template_skinny) {
		seq_offset = &reg_set->fusion_seq_offset;
		hostdiag_offset = &reg_set->fusion_host_diag;
	}

	writel(0, seq_offset);
	writel(4, seq_offset);
	writel(0xb, seq_offset);
	writel(2, seq_offset);
	writel(7, seq_offset);
	writel(0xd, seq_offset);

	msleep(1000);

	HostDiag = (u32)readl(hostdiag_offset);

	while (!(HostDiag & DIAG_WRITE_ENABLE)) {
		msleep(100);
		HostDiag = (u32)readl(hostdiag_offset);
		dev_notice(&instance->pdev->dev, "RESETGEN2: retry=%x, hostdiag=%x\n",
					retry, HostDiag);

		if (retry++ >= 100)
			return 1;

	}

	dev_notice(&instance->pdev->dev, "ADP_RESET_GEN2: HostDiag=%x\n", HostDiag);

	writel((HostDiag | DIAG_RESET_ADAPTER), hostdiag_offset);

	ssleep(10);

	HostDiag = (u32)readl(hostdiag_offset);
	while (HostDiag & DIAG_RESET_ADAPTER) {
		msleep(100);
		HostDiag = (u32)readl(hostdiag_offset);
		dev_notice(&instance->pdev->dev, "RESET_GEN2: retry=%x, hostdiag=%x\n",
				retry, HostDiag);

		if (retry++ >= 1000)
			return 1;

	}
	return 0;
}

/**
 * megasas_check_reset_gen2 -	For controller reset check
 * @instance:	Adapter soft state
 * @regs:	MFI register set
 */
static int
megasas_check_reset_gen2(struct megasas_instance *instance,
		struct megasas_register_set __iomem *regs)
{
	if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL)
		return 1;

	return 0;
}

static struct megasas_instance_template megasas_instance_template_gen2 = {

	.fire_cmd = megasas_fire_cmd_gen2,
	.enable_intr = megasas_enable_intr_gen2,
	.disable_intr = megasas_disable_intr_gen2,
	.clear_intr = megasas_clear_intr_gen2,
	.read_fw_status_reg = megasas_read_fw_status_reg_gen2,
	.adp_reset = megasas_adp_reset_gen2,
	.check_reset = megasas_check_reset_gen2,
	.service_isr = megasas_isr,
	.tasklet = megasas_complete_cmd_dpc,
	.init_adapter = megasas_init_adapter_mfi,
	.build_and_issue_cmd = megasas_build_and_issue_cmd,
	.issue_dcmd = megasas_issue_dcmd,
};

/*
 * This is the end of set of functions & definitions
 * specific to gen2 (deviceid : 0x78, 0x79) controllers
 */

/*
 * Template added for TB (Fusion)
 */
extern struct megasas_instance_template megasas_instance_template_fusion;

/**
 * megasas_issue_polled -	Issues a polling command
 * @instance:			Adapter soft state
 * @cmd:			Command packet to be issued
 *
 * For polling, MFI requires the cmd_status to be set to MFI_STAT_INVALID_STATUS before posting.
 */
int
megasas_issue_polled(struct megasas_instance *instance, struct megasas_cmd *cmd)
{
	struct megasas_header *frame_hdr = &cmd->frame->hdr;

	frame_hdr->cmd_status = MFI_STAT_INVALID_STATUS;
	frame_hdr->flags |= cpu_to_le16(MFI_FRAME_DONT_POST_IN_REPLY_QUEUE);

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		return DCMD_INIT;
	}

	instance->instancet->issue_dcmd(instance, cmd);

	return wait_and_poll(instance, cmd, instance->requestorId ?
			MEGASAS_ROUTINE_WAIT_TIME_VF : MFI_IO_TIMEOUT_SECS);
}

/**
 * megasas_issue_blocked_cmd -	Synchronous wrapper around regular FW cmds
 * @instance:			Adapter soft state
 * @cmd:			Command to be issued
 * @timeout:			Timeout in seconds
 *
 * This function waits on an event for the command to be returned from ISR.
 * Max wait time is MEGASAS_INTERNAL_CMD_WAIT_TIME secs
 * Used to issue ioctl commands.
 */
int
megasas_issue_blocked_cmd(struct megasas_instance *instance,
			  struct megasas_cmd *cmd, int timeout)
{
	int ret = 0;
	cmd->cmd_status_drv = DCMD_INIT;

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		return DCMD_INIT;
	}

	instance->instancet->issue_dcmd(instance, cmd);

	if (timeout) {
		ret = wait_event_timeout(instance->int_cmd_wait_q,
		cmd->cmd_status_drv != DCMD_INIT, timeout * HZ);
		if (!ret) {
			dev_err(&instance->pdev->dev,
				"DCMD(opcode: 0x%x) is timed out, func:%s\n",
				cmd->frame->dcmd.opcode, __func__);
			return DCMD_TIMEOUT;
		}
	} else
		wait_event(instance->int_cmd_wait_q,
				cmd->cmd_status_drv != DCMD_INIT);

	return cmd->cmd_status_drv;
}

/**
 * megasas_issue_blocked_abort_cmd -	Aborts previously issued cmd
 * @instance:				Adapter soft state
 * @cmd_to_abort:			Previously issued cmd to be aborted
 * @timeout:				Timeout in seconds
 *
 * MFI firmware can abort previously issued AEN comamnd (automatic event
 * notification). The megasas_issue_blocked_abort_cmd() issues such abort
 * cmd and waits for return status.
 * Max wait time is MEGASAS_INTERNAL_CMD_WAIT_TIME secs
 */
static int
megasas_issue_blocked_abort_cmd(struct megasas_instance *instance,
				struct megasas_cmd *cmd_to_abort, int timeout)
{
	struct megasas_cmd *cmd;
	struct megasas_abort_frame *abort_fr;
	int ret = 0;
	u32 opcode;

	cmd = megasas_get_cmd(instance);

	if (!cmd)
		return -1;

	abort_fr = &cmd->frame->abort;

	/*
	 * Prepare and issue the abort frame
	 */
	abort_fr->cmd = MFI_CMD_ABORT;
	abort_fr->cmd_status = MFI_STAT_INVALID_STATUS;
	abort_fr->flags = cpu_to_le16(0);
	abort_fr->abort_context = cpu_to_le32(cmd_to_abort->index);
	abort_fr->abort_mfi_phys_addr_lo =
		cpu_to_le32(lower_32_bits(cmd_to_abort->frame_phys_addr));
	abort_fr->abort_mfi_phys_addr_hi =
		cpu_to_le32(upper_32_bits(cmd_to_abort->frame_phys_addr));

	cmd->sync_cmd = 1;
	cmd->cmd_status_drv = DCMD_INIT;

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		return DCMD_INIT;
	}

	instance->instancet->issue_dcmd(instance, cmd);

	if (timeout) {
		ret = wait_event_timeout(instance->abort_cmd_wait_q,
		cmd->cmd_status_drv != DCMD_INIT, timeout * HZ);
		if (!ret) {
			opcode = cmd_to_abort->frame->dcmd.opcode;
			dev_err(&instance->pdev->dev,
				"Abort(to be aborted DCMD opcode: 0x%x) is timed out func:%s\n",
				opcode,  __func__);
			return DCMD_TIMEOUT;
		}
	} else
		wait_event(instance->abort_cmd_wait_q,
		cmd->cmd_status_drv != DCMD_INIT);

	cmd->sync_cmd = 0;

	megasas_return_cmd(instance, cmd);
	return cmd->cmd_status_drv;
}

/**
 * megasas_make_sgl32 -	Prepares 32-bit SGL
 * @instance:		Adapter soft state
 * @scp:		SCSI command from the mid-layer
 * @mfi_sgl:		SGL to be filled in
 *
 * If successful, this function returns the number of SG elements. Otherwise,
 * it returnes -1.
 */
static int
megasas_make_sgl32(struct megasas_instance *instance, struct scsi_cmnd *scp,
		   union megasas_sgl *mfi_sgl)
{
	int i;
	int sge_count;
	struct scatterlist *os_sgl;

	sge_count = scsi_dma_map(scp);
	BUG_ON(sge_count < 0);

	if (sge_count) {
		scsi_for_each_sg(scp, os_sgl, sge_count, i) {
			mfi_sgl->sge32[i].length = cpu_to_le32(sg_dma_len(os_sgl));
			mfi_sgl->sge32[i].phys_addr = cpu_to_le32(sg_dma_address(os_sgl));
		}
	}
	return sge_count;
}

/**
 * megasas_make_sgl64 -	Prepares 64-bit SGL
 * @instance:		Adapter soft state
 * @scp:		SCSI command from the mid-layer
 * @mfi_sgl:		SGL to be filled in
 *
 * If successful, this function returns the number of SG elements. Otherwise,
 * it returnes -1.
 */
static int
megasas_make_sgl64(struct megasas_instance *instance, struct scsi_cmnd *scp,
		   union megasas_sgl *mfi_sgl)
{
	int i;
	int sge_count;
	struct scatterlist *os_sgl;

	sge_count = scsi_dma_map(scp);
	BUG_ON(sge_count < 0);

	if (sge_count) {
		scsi_for_each_sg(scp, os_sgl, sge_count, i) {
			mfi_sgl->sge64[i].length = cpu_to_le32(sg_dma_len(os_sgl));
			mfi_sgl->sge64[i].phys_addr = cpu_to_le64(sg_dma_address(os_sgl));
		}
	}
	return sge_count;
}

/**
 * megasas_make_sgl_skinny - Prepares IEEE SGL
 * @instance:           Adapter soft state
 * @scp:                SCSI command from the mid-layer
 * @mfi_sgl:            SGL to be filled in
 *
 * If successful, this function returns the number of SG elements. Otherwise,
 * it returnes -1.
 */
static int
megasas_make_sgl_skinny(struct megasas_instance *instance,
		struct scsi_cmnd *scp, union megasas_sgl *mfi_sgl)
{
	int i;
	int sge_count;
	struct scatterlist *os_sgl;

	sge_count = scsi_dma_map(scp);

	if (sge_count) {
		scsi_for_each_sg(scp, os_sgl, sge_count, i) {
			mfi_sgl->sge_skinny[i].length =
				cpu_to_le32(sg_dma_len(os_sgl));
			mfi_sgl->sge_skinny[i].phys_addr =
				cpu_to_le64(sg_dma_address(os_sgl));
			mfi_sgl->sge_skinny[i].flag = cpu_to_le32(0);
		}
	}
	return sge_count;
}

 /**
 * megasas_get_frame_count - Computes the number of frames
 * @frame_type		: type of frame- io or pthru frame
 * @sge_count		: number of sg elements
 *
 * Returns the number of frames required for numnber of sge's (sge_count)
 */

static u32 megasas_get_frame_count(struct megasas_instance *instance,
			u8 sge_count, u8 frame_type)
{
	int num_cnt;
	int sge_bytes;
	u32 sge_sz;
	u32 frame_count = 0;

	sge_sz = (IS_DMA64) ? sizeof(struct megasas_sge64) :
	    sizeof(struct megasas_sge32);

	if (instance->flag_ieee) {
		sge_sz = sizeof(struct megasas_sge_skinny);
	}

	/*
	 * Main frame can contain 2 SGEs for 64-bit SGLs and
	 * 3 SGEs for 32-bit SGLs for ldio &
	 * 1 SGEs for 64-bit SGLs and
	 * 2 SGEs for 32-bit SGLs for pthru frame
	 */
	if (unlikely(frame_type == PTHRU_FRAME)) {
		if (instance->flag_ieee == 1) {
			num_cnt = sge_count - 1;
		} else if (IS_DMA64)
			num_cnt = sge_count - 1;
		else
			num_cnt = sge_count - 2;
	} else {
		if (instance->flag_ieee == 1) {
			num_cnt = sge_count - 1;
		} else if (IS_DMA64)
			num_cnt = sge_count - 2;
		else
			num_cnt = sge_count - 3;
	}

	if (num_cnt > 0) {
		sge_bytes = sge_sz * num_cnt;

		frame_count = (sge_bytes / MEGAMFI_FRAME_SIZE) +
		    ((sge_bytes % MEGAMFI_FRAME_SIZE) ? 1 : 0) ;
	}
	/* Main frame */
	frame_count += 1;

	if (frame_count > 7)
		frame_count = 8;
	return frame_count;
}

/**
 * megasas_build_dcdb -	Prepares a direct cdb (DCDB) command
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared in
 *
 * This function prepares CDB commands. These are typcially pass-through
 * commands to the devices.
 */
static int
megasas_build_dcdb(struct megasas_instance *instance, struct scsi_cmnd *scp,
		   struct megasas_cmd *cmd)
{
	u32 is_logical;
	u32 device_id;
	u16 flags = 0;
	struct megasas_pthru_frame *pthru;

	is_logical = MEGASAS_IS_LOGICAL(scp->device);
	device_id = MEGASAS_DEV_INDEX(scp);
	pthru = (struct megasas_pthru_frame *)cmd->frame;

	if (scp->sc_data_direction == DMA_TO_DEVICE)
		flags = MFI_FRAME_DIR_WRITE;
	else if (scp->sc_data_direction == DMA_FROM_DEVICE)
		flags = MFI_FRAME_DIR_READ;
	else if (scp->sc_data_direction == DMA_NONE)
		flags = MFI_FRAME_DIR_NONE;

	if (instance->flag_ieee == 1) {
		flags |= MFI_FRAME_IEEE;
	}

	/*
	 * Prepare the DCDB frame
	 */
	pthru->cmd = (is_logical) ? MFI_CMD_LD_SCSI_IO : MFI_CMD_PD_SCSI_IO;
	pthru->cmd_status = 0x0;
	pthru->scsi_status = 0x0;
	pthru->target_id = device_id;
	pthru->lun = scp->device->lun;
	pthru->cdb_len = scp->cmd_len;
	pthru->timeout = 0;
	pthru->pad_0 = 0;
	pthru->flags = cpu_to_le16(flags);
	pthru->data_xfer_len = cpu_to_le32(scsi_bufflen(scp));

	memcpy(pthru->cdb, scp->cmnd, scp->cmd_len);

	/*
	 * If the command is for the tape device, set the
	 * pthru timeout to the os layer timeout value.
	 */
	if (scp->device->type == TYPE_TAPE) {
		if (scsi_cmd_to_rq(scp)->timeout / HZ > 0xFFFF)
			pthru->timeout = cpu_to_le16(0xFFFF);
		else
			pthru->timeout = cpu_to_le16(scsi_cmd_to_rq(scp)->timeout / HZ);
	}

	/*
	 * Construct SGL
	 */
	if (instance->flag_ieee == 1) {
		pthru->flags |= cpu_to_le16(MFI_FRAME_SGL64);
		pthru->sge_count = megasas_make_sgl_skinny(instance, scp,
						      &pthru->sgl);
	} else if (IS_DMA64) {
		pthru->flags |= cpu_to_le16(MFI_FRAME_SGL64);
		pthru->sge_count = megasas_make_sgl64(instance, scp,
						      &pthru->sgl);
	} else
		pthru->sge_count = megasas_make_sgl32(instance, scp,
						      &pthru->sgl);

	if (pthru->sge_count > instance->max_num_sge) {
		dev_err(&instance->pdev->dev, "DCDB too many SGE NUM=%x\n",
			pthru->sge_count);
		return 0;
	}

	/*
	 * Sense info specific
	 */
	pthru->sense_len = SCSI_SENSE_BUFFERSIZE;
	pthru->sense_buf_phys_addr_hi =
		cpu_to_le32(upper_32_bits(cmd->sense_phys_addr));
	pthru->sense_buf_phys_addr_lo =
		cpu_to_le32(lower_32_bits(cmd->sense_phys_addr));

	/*
	 * Compute the total number of frames this command consumes. FW uses
	 * this number to pull sufficient number of frames from host memory.
	 */
	cmd->frame_count = megasas_get_frame_count(instance, pthru->sge_count,
							PTHRU_FRAME);

	return cmd->frame_count;
}

/**
 * megasas_build_ldio -	Prepares IOs to logical devices
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared
 *
 * Frames (and accompanying SGLs) for regular SCSI IOs use this function.
 */
static int
megasas_build_ldio(struct megasas_instance *instance, struct scsi_cmnd *scp,
		   struct megasas_cmd *cmd)
{
	u32 device_id;
	u8 sc = scp->cmnd[0];
	u16 flags = 0;
	struct megasas_io_frame *ldio;

	device_id = MEGASAS_DEV_INDEX(scp);
	ldio = (struct megasas_io_frame *)cmd->frame;

	if (scp->sc_data_direction == DMA_TO_DEVICE)
		flags = MFI_FRAME_DIR_WRITE;
	else if (scp->sc_data_direction == DMA_FROM_DEVICE)
		flags = MFI_FRAME_DIR_READ;

	if (instance->flag_ieee == 1) {
		flags |= MFI_FRAME_IEEE;
	}

	/*
	 * Prepare the Logical IO frame: 2nd bit is zero for all read cmds
	 */
	ldio->cmd = (sc & 0x02) ? MFI_CMD_LD_WRITE : MFI_CMD_LD_READ;
	ldio->cmd_status = 0x0;
	ldio->scsi_status = 0x0;
	ldio->target_id = device_id;
	ldio->timeout = 0;
	ldio->reserved_0 = 0;
	ldio->pad_0 = 0;
	ldio->flags = cpu_to_le16(flags);
	ldio->start_lba_hi = 0;
	ldio->access_byte = (scp->cmd_len != 6) ? scp->cmnd[1] : 0;

	/*
	 * 6-byte READ(0x08) or WRITE(0x0A) cdb
	 */
	if (scp->cmd_len == 6) {
		ldio->lba_count = cpu_to_le32((u32) scp->cmnd[4]);
		ldio->start_lba_lo = cpu_to_le32(((u32) scp->cmnd[1] << 16) |
						 ((u32) scp->cmnd[2] << 8) |
						 (u32) scp->cmnd[3]);

		ldio->start_lba_lo &= cpu_to_le32(0x1FFFFF);
	}

	/*
	 * 10-byte READ(0x28) or WRITE(0x2A) cdb
	 */
	else if (scp->cmd_len == 10) {
		ldio->lba_count = cpu_to_le32((u32) scp->cmnd[8] |
					      ((u32) scp->cmnd[7] << 8));
		ldio->start_lba_lo = cpu_to_le32(((u32) scp->cmnd[2] << 24) |
						 ((u32) scp->cmnd[3] << 16) |
						 ((u32) scp->cmnd[4] << 8) |
						 (u32) scp->cmnd[5]);
	}

	/*
	 * 12-byte READ(0xA8) or WRITE(0xAA) cdb
	 */
	else if (scp->cmd_len == 12) {
		ldio->lba_count = cpu_to_le32(((u32) scp->cmnd[6] << 24) |
					      ((u32) scp->cmnd[7] << 16) |
					      ((u32) scp->cmnd[8] << 8) |
					      (u32) scp->cmnd[9]);

		ldio->start_lba_lo = cpu_to_le32(((u32) scp->cmnd[2] << 24) |
						 ((u32) scp->cmnd[3] << 16) |
						 ((u32) scp->cmnd[4] << 8) |
						 (u32) scp->cmnd[5]);
	}

	/*
	 * 16-byte READ(0x88) or WRITE(0x8A) cdb
	 */
	else if (scp->cmd_len == 16) {
		ldio->lba_count = cpu_to_le32(((u32) scp->cmnd[10] << 24) |
					      ((u32) scp->cmnd[11] << 16) |
					      ((u32) scp->cmnd[12] << 8) |
					      (u32) scp->cmnd[13]);

		ldio->start_lba_lo = cpu_to_le32(((u32) scp->cmnd[6] << 24) |
						 ((u32) scp->cmnd[7] << 16) |
						 ((u32) scp->cmnd[8] << 8) |
						 (u32) scp->cmnd[9]);

		ldio->start_lba_hi = cpu_to_le32(((u32) scp->cmnd[2] << 24) |
						 ((u32) scp->cmnd[3] << 16) |
						 ((u32) scp->cmnd[4] << 8) |
						 (u32) scp->cmnd[5]);

	}

	/*
	 * Construct SGL
	 */
	if (instance->flag_ieee) {
		ldio->flags |= cpu_to_le16(MFI_FRAME_SGL64);
		ldio->sge_count = megasas_make_sgl_skinny(instance, scp,
					      &ldio->sgl);
	} else if (IS_DMA64) {
		ldio->flags |= cpu_to_le16(MFI_FRAME_SGL64);
		ldio->sge_count = megasas_make_sgl64(instance, scp, &ldio->sgl);
	} else
		ldio->sge_count = megasas_make_sgl32(instance, scp, &ldio->sgl);

	if (ldio->sge_count > instance->max_num_sge) {
		dev_err(&instance->pdev->dev, "build_ld_io: sge_count = %x\n",
			ldio->sge_count);
		return 0;
	}

	/*
	 * Sense info specific
	 */
	ldio->sense_len = SCSI_SENSE_BUFFERSIZE;
	ldio->sense_buf_phys_addr_hi = 0;
	ldio->sense_buf_phys_addr_lo = cpu_to_le32(cmd->sense_phys_addr);

	/*
	 * Compute the total number of frames this command consumes. FW uses
	 * this number to pull sufficient number of frames from host memory.
	 */
	cmd->frame_count = megasas_get_frame_count(instance,
			ldio->sge_count, IO_FRAME);

	return cmd->frame_count;
}

/**
 * megasas_cmd_type -		Checks if the cmd is for logical drive/sysPD
 *				and whether it's RW or non RW
 * @cmd:			SCSI command
 *
 */
inline int megasas_cmd_type(struct scsi_cmnd *cmd)
{
	int ret;

	switch (cmd->cmnd[0]) {
	case READ_10:
	case WRITE_10:
	case READ_12:
	case WRITE_12:
	case READ_6:
	case WRITE_6:
	case READ_16:
	case WRITE_16:
		ret = (MEGASAS_IS_LOGICAL(cmd->device)) ?
			READ_WRITE_LDIO : READ_WRITE_SYSPDIO;
		break;
	default:
		ret = (MEGASAS_IS_LOGICAL(cmd->device)) ?
			NON_READ_WRITE_LDIO : NON_READ_WRITE_SYSPDIO;
	}
	return ret;
}

 /**
 * megasas_dump_pending_frames -	Dumps the frame address of all pending cmds
 *					in FW
 * @instance:				Adapter soft state
 */
static inline void
megasas_dump_pending_frames(struct megasas_instance *instance)
{
	struct megasas_cmd *cmd;
	int i,n;
	union megasas_sgl *mfi_sgl;
	struct megasas_io_frame *ldio;
	struct megasas_pthru_frame *pthru;
	u32 sgcount;
	u16 max_cmd = instance->max_fw_cmds;

	dev_err(&instance->pdev->dev, "[%d]: Dumping Frame Phys Address of all pending cmds in FW\n",instance->host->host_no);
	dev_err(&instance->pdev->dev, "[%d]: Total OS Pending cmds : %d\n",instance->host->host_no,atomic_read(&instance->fw_outstanding));
	if (IS_DMA64)
		dev_err(&instance->pdev->dev, "[%d]: 64 bit SGLs were sent to FW\n",instance->host->host_no);
	else
		dev_err(&instance->pdev->dev, "[%d]: 32 bit SGLs were sent to FW\n",instance->host->host_no);

	dev_err(&instance->pdev->dev, "[%d]: Pending OS cmds in FW : \n",instance->host->host_no);
	for (i = 0; i < max_cmd; i++) {
		cmd = instance->cmd_list[i];
		if (!cmd->scmd)
			continue;
		dev_err(&instance->pdev->dev, "[%d]: Frame addr :0x%08lx : ",instance->host->host_no,(unsigned long)cmd->frame_phys_addr);
		if (megasas_cmd_type(cmd->scmd) == READ_WRITE_LDIO) {
			ldio = (struct megasas_io_frame *)cmd->frame;
			mfi_sgl = &ldio->sgl;
			sgcount = ldio->sge_count;
			dev_err(&instance->pdev->dev, "[%d]: frame count : 0x%x, Cmd : 0x%x, Tgt id : 0x%x,"
			" lba lo : 0x%x, lba_hi : 0x%x, sense_buf addr : 0x%x,sge count : 0x%x\n",
			instance->host->host_no, cmd->frame_count, ldio->cmd, ldio->target_id,
			le32_to_cpu(ldio->start_lba_lo), le32_to_cpu(ldio->start_lba_hi),
			le32_to_cpu(ldio->sense_buf_phys_addr_lo), sgcount);
		} else {
			pthru = (struct megasas_pthru_frame *) cmd->frame;
			mfi_sgl = &pthru->sgl;
			sgcount = pthru->sge_count;
			dev_err(&instance->pdev->dev, "[%d]: frame count : 0x%x, Cmd : 0x%x, Tgt id : 0x%x, "
			"lun : 0x%x, cdb_len : 0x%x, data xfer len : 0x%x, sense_buf addr : 0x%x,sge count : 0x%x\n",
			instance->host->host_no, cmd->frame_count, pthru->cmd, pthru->target_id,
			pthru->lun, pthru->cdb_len, le32_to_cpu(pthru->data_xfer_len),
			le32_to_cpu(pthru->sense_buf_phys_addr_lo), sgcount);
		}
		if (megasas_dbg_lvl & MEGASAS_DBG_LVL) {
			for (n = 0; n < sgcount; n++) {
				if (IS_DMA64)
					dev_err(&instance->pdev->dev, "sgl len : 0x%x, sgl addr : 0x%llx\n",
						le32_to_cpu(mfi_sgl->sge64[n].length),
						le64_to_cpu(mfi_sgl->sge64[n].phys_addr));
				else
					dev_err(&instance->pdev->dev, "sgl len : 0x%x, sgl addr : 0x%x\n",
						le32_to_cpu(mfi_sgl->sge32[n].length),
						le32_to_cpu(mfi_sgl->sge32[n].phys_addr));
			}
		}
	} /*for max_cmd*/
	dev_err(&instance->pdev->dev, "[%d]: Pending Internal cmds in FW : \n",instance->host->host_no);
	for (i = 0; i < max_cmd; i++) {

		cmd = instance->cmd_list[i];

		if (cmd->sync_cmd == 1)
			dev_err(&instance->pdev->dev, "0x%08lx : ", (unsigned long)cmd->frame_phys_addr);
	}
	dev_err(&instance->pdev->dev, "[%d]: Dumping Done\n\n",instance->host->host_no);
}

u32
megasas_build_and_issue_cmd(struct megasas_instance *instance,
			    struct scsi_cmnd *scmd)
{
	struct megasas_cmd *cmd;
	u32 frame_count;

	cmd = megasas_get_cmd(instance);
	if (!cmd)
		return SCSI_MLQUEUE_HOST_BUSY;

	/*
	 * Logical drive command
	 */
	if (megasas_cmd_type(scmd) == READ_WRITE_LDIO)
		frame_count = megasas_build_ldio(instance, scmd, cmd);
	else
		frame_count = megasas_build_dcdb(instance, scmd, cmd);

	if (!frame_count)
		goto out_return_cmd;

	cmd->scmd = scmd;
	megasas_priv(scmd)->cmd_priv = cmd;

	/*
	 * Issue the command to the FW
	 */
	atomic_inc(&instance->fw_outstanding);

	instance->instancet->fire_cmd(instance, cmd->frame_phys_addr,
				cmd->frame_count-1, instance->reg_set);

	return 0;
out_return_cmd:
	megasas_return_cmd(instance, cmd);
	return SCSI_MLQUEUE_HOST_BUSY;
}


/**
 * megasas_queue_command -	Queue entry point
 * @shost:			adapter SCSI host
 * @scmd:			SCSI command to be queued
 */
static int
megasas_queue_command(struct Scsi_Host *shost, struct scsi_cmnd *scmd)
{
	struct megasas_instance *instance;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	u32 ld_tgt_id;

	instance = (struct megasas_instance *)
	    scmd->device->host->hostdata;

	if (instance->unload == 1) {
		scmd->result = DID_NO_CONNECT << 16;
		scsi_done(scmd);
		return 0;
	}

	if (instance->issuepend_done == 0)
		return SCSI_MLQUEUE_HOST_BUSY;


	/* Check for an mpio path and adjust behavior */
	if (atomic_read(&instance->adprecovery) == MEGASAS_ADPRESET_SM_INFAULT) {
		if (megasas_check_mpio_paths(instance, scmd) ==
		    (DID_REQUEUE << 16)) {
			return SCSI_MLQUEUE_HOST_BUSY;
		} else {
			scmd->result = DID_NO_CONNECT << 16;
			scsi_done(scmd);
			return 0;
		}
	}

	mr_device_priv_data = scmd->device->hostdata;
	if (!mr_device_priv_data ||
	    (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR)) {
		scmd->result = DID_NO_CONNECT << 16;
		scsi_done(scmd);
		return 0;
	}

	if (MEGASAS_IS_LOGICAL(scmd->device)) {
		ld_tgt_id = MEGASAS_TARGET_ID(scmd->device);
		if (instance->ld_tgtid_status[ld_tgt_id] == LD_TARGET_ID_DELETED) {
			scmd->result = DID_NO_CONNECT << 16;
			scsi_done(scmd);
			return 0;
		}
	}

	if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL)
		return SCSI_MLQUEUE_HOST_BUSY;

	if (mr_device_priv_data->tm_busy)
		return SCSI_MLQUEUE_DEVICE_BUSY;


	scmd->result = 0;

	if (MEGASAS_IS_LOGICAL(scmd->device) &&
	    (scmd->device->id >= instance->fw_supported_vd_count ||
		scmd->device->lun)) {
		scmd->result = DID_BAD_TARGET << 16;
		goto out_done;
	}

	if ((scmd->cmnd[0] == SYNCHRONIZE_CACHE) &&
	    MEGASAS_IS_LOGICAL(scmd->device) &&
	    (!instance->fw_sync_cache_support)) {
		scmd->result = DID_OK << 16;
		goto out_done;
	}

	return instance->instancet->build_and_issue_cmd(instance, scmd);

 out_done:
	scsi_done(scmd);
	return 0;
}

static struct megasas_instance *megasas_lookup_instance(u16 host_no)
{
	int i;

	for (i = 0; i < megasas_mgmt_info.max_index; i++) {

		if ((megasas_mgmt_info.instance[i]) &&
		    (megasas_mgmt_info.instance[i]->host->host_no == host_no))
			return megasas_mgmt_info.instance[i];
	}

	return NULL;
}

/*
* megasas_set_dynamic_target_properties -
* Device property set by driver may not be static and it is required to be
* updated after OCR
*
* set tm_capable.
* set dma alignment (only for eedp protection enable vd).
*
* @sdev: OS provided scsi device
*
* Returns void
*/
void megasas_set_dynamic_target_properties(struct scsi_device *sdev,
		struct queue_limits *lim, bool is_target_prop)
{
	u16 pd_index = 0, ld;
	u32 device_id;
	struct megasas_instance *instance;
	struct fusion_context *fusion;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;
	struct MR_LD_RAID *raid;
	struct MR_DRV_RAID_MAP_ALL *local_map_ptr;

	instance = megasas_lookup_instance(sdev->host->host_no);
	fusion = instance->ctrl_context;
	mr_device_priv_data = sdev->hostdata;

	if (!fusion || !mr_device_priv_data)
		return;

	if (MEGASAS_IS_LOGICAL(sdev)) {
		device_id = ((sdev->channel % 2) * MEGASAS_MAX_DEV_PER_CHANNEL)
					+ sdev->id;
		local_map_ptr = fusion->ld_drv_map[(instance->map_id & 1)];
		ld = MR_TargetIdToLdGet(device_id, local_map_ptr);
		if (ld >= instance->fw_supported_vd_count)
			return;
		raid = MR_LdRaidGet(ld, local_map_ptr);

		if (raid->capability.ldPiMode == MR_PROT_INFO_TYPE_CONTROLLER) {
			if (lim)
				lim->dma_alignment = 0x7;
		}

		mr_device_priv_data->is_tm_capable =
			raid->capability.tmCapable;

		if (!raid->flags.isEPD)
			sdev->no_write_same = 1;

	} else if (instance->use_seqnum_jbod_fp) {
		pd_index = (sdev->channel * MEGASAS_MAX_DEV_PER_CHANNEL) +
			sdev->id;
		pd_sync = (void *)fusion->pd_seq_sync
				[(instance->pd_seq_map_id - 1) & 1];
		mr_device_priv_data->is_tm_capable =
			pd_sync->seq[pd_index].capability.tmCapable;
	}

	if (is_target_prop && instance->tgt_prop->reset_tmo) {
		/*
		 * If FW provides a target reset timeout value, driver will use
		 * it. If not set, fallback to default values.
		 */
		mr_device_priv_data->target_reset_tmo =
			min_t(u8, instance->max_reset_tmo,
			      instance->tgt_prop->reset_tmo);
		mr_device_priv_data->task_abort_tmo = instance->task_abort_tmo;
	} else {
		mr_device_priv_data->target_reset_tmo =
						MEGASAS_DEFAULT_TM_TIMEOUT;
		mr_device_priv_data->task_abort_tmo =
						MEGASAS_DEFAULT_TM_TIMEOUT;
	}
}

/*
 * megasas_set_nvme_device_properties -
 * set nomerges=2
 * set virtual page boundary = 4K (current mr_nvme_pg_size is 4K).
 * set maximum io transfer = MDTS of NVME device provided by MR firmware.
 *
 * MR firmware provides value in KB. Caller of this function converts
 * kb into bytes.
 *
 * e.a MDTS=5 means 2^5 * nvme page size. (In case of 4K page size,
 * MR firmware provides value 128 as (32 * 4K) = 128K.
 *
 * @sdev:				scsi device
 * @max_io_size:				maximum io transfer size
 *
 */
static inline void
megasas_set_nvme_device_properties(struct scsi_device *sdev,
		struct queue_limits *lim, u32 max_io_size)
{
	struct megasas_instance *instance;
	u32 mr_nvme_pg_size;

	instance = (struct megasas_instance *)sdev->host->hostdata;
	mr_nvme_pg_size = max_t(u32, instance->nvme_page_size,
				MR_DEFAULT_NVME_PAGE_SIZE);

	lim->max_hw_sectors = max_io_size / 512;
	lim->virt_boundary_mask = mr_nvme_pg_size - 1;
}

/*
 * megasas_set_fw_assisted_qd -
 * set device queue depth to can_queue
 * set device queue depth to fw assisted qd
 *
 * @sdev:				scsi device
 * @is_target_prop			true, if fw provided target properties.
 */
static void megasas_set_fw_assisted_qd(struct scsi_device *sdev,
						 bool is_target_prop)
{
	u8 interface_type;
	u32 device_qd = MEGASAS_DEFAULT_CMD_PER_LUN;
	u32 tgt_device_qd;
	struct megasas_instance *instance;
	struct MR_PRIV_DEVICE *mr_device_priv_data;

	instance = megasas_lookup_instance(sdev->host->host_no);
	mr_device_priv_data = sdev->hostdata;
	interface_type  = mr_device_priv_data->interface_type;

	switch (interface_type) {
	case SAS_PD:
		device_qd = MEGASAS_SAS_QD;
		break;
	case SATA_PD:
		device_qd = MEGASAS_SATA_QD;
		break;
	case NVME_PD:
		device_qd = MEGASAS_NVME_QD;
		break;
	}

	if (is_target_prop) {
		tgt_device_qd = le32_to_cpu(instance->tgt_prop->device_qdepth);
		if (tgt_device_qd)
			device_qd = min(instance->host->can_queue,
					(int)tgt_device_qd);
	}

	if (instance->enable_sdev_max_qd && interface_type != UNKNOWN_DRIVE)
		device_qd = instance->host->can_queue;

	scsi_change_queue_depth(sdev, device_qd);
}

/*
 * megasas_set_static_target_properties -
 * Device property set by driver are static and it is not required to be
 * updated after OCR.
 *
 * set io timeout
 * set device queue depth
 * set nvme device properties. see - megasas_set_nvme_device_properties
 *
 * @sdev:				scsi device
 * @is_target_prop			true, if fw provided target properties.
 */
static void megasas_set_static_target_properties(struct scsi_device *sdev,
		struct queue_limits *lim, bool is_target_prop)
{
	u32 max_io_size_kb = MR_DEFAULT_NVME_MDTS_KB;
	struct megasas_instance *instance;

	instance = megasas_lookup_instance(sdev->host->host_no);

	/*
	 * The RAID firmware may require extended timeouts.
	 */
	blk_queue_rq_timeout(sdev->request_queue, scmd_timeout * HZ);

	/* max_io_size_kb will be set to non zero for
	 * nvme based vd and syspd.
	 */
	if (is_target_prop)
		max_io_size_kb = le32_to_cpu(instance->tgt_prop->max_io_size_kb);

	if (instance->nvme_page_size && max_io_size_kb)
		megasas_set_nvme_device_properties(sdev, lim,
				max_io_size_kb << 10);

	megasas_set_fw_assisted_qd(sdev, is_target_prop);
}


static int megasas_sdev_configure(struct scsi_device *sdev,
				  struct queue_limits *lim)
{
	u16 pd_index = 0;
	struct megasas_instance *instance;
	int ret_target_prop = DCMD_FAILED;
	bool is_target_prop = false;

	instance = megasas_lookup_instance(sdev->host->host_no);
	if (instance->pd_list_not_supported) {
		if (!MEGASAS_IS_LOGICAL(sdev) && sdev->type == TYPE_DISK) {
			pd_index = (sdev->channel * MEGASAS_MAX_DEV_PER_CHANNEL) +
				sdev->id;
			if (instance->pd_list[pd_index].driveState !=
				MR_PD_STATE_SYSTEM)
				return -ENXIO;
		}
	}

	mutex_lock(&instance->reset_mutex);
	/* Send DCMD to Firmware and cache the information */
	if ((instance->pd_info) && !MEGASAS_IS_LOGICAL(sdev))
		megasas_get_pd_info(instance, sdev);

	/* Some ventura firmware may not have instance->nvme_page_size set.
	 * Do not send MR_DCMD_DRV_GET_TARGET_PROP
	 */
	if ((instance->tgt_prop) && (instance->nvme_page_size))
		ret_target_prop = megasas_get_target_prop(instance, sdev);

	is_target_prop = (ret_target_prop == DCMD_SUCCESS) ? true : false;
	megasas_set_static_target_properties(sdev, lim, is_target_prop);

	/* This sdev property may change post OCR */
	megasas_set_dynamic_target_properties(sdev, lim, is_target_prop);

	mutex_unlock(&instance->reset_mutex);

	return 0;
}

static int megasas_sdev_init(struct scsi_device *sdev)
{
	u16 pd_index = 0, ld_tgt_id;
	struct megasas_instance *instance ;
	struct MR_PRIV_DEVICE *mr_device_priv_data;

	instance = megasas_lookup_instance(sdev->host->host_no);
	if (!MEGASAS_IS_LOGICAL(sdev)) {
		/*
		 * Open the OS scan to the SYSTEM PD
		 */
		pd_index =
			(sdev->channel * MEGASAS_MAX_DEV_PER_CHANNEL) +
			sdev->id;
		if ((instance->pd_list_not_supported ||
			instance->pd_list[pd_index].driveState ==
			MR_PD_STATE_SYSTEM)) {
			goto scan_target;
		}
		return -ENXIO;
	} else if (!MEGASAS_IS_LUN_VALID(sdev)) {
		sdev_printk(KERN_INFO, sdev, "%s: invalid LUN\n", __func__);
		return -ENXIO;
	}

scan_target:
	mr_device_priv_data = kzalloc(sizeof(*mr_device_priv_data),
					GFP_KERNEL);
	if (!mr_device_priv_data)
		return -ENOMEM;

	if (MEGASAS_IS_LOGICAL(sdev)) {
		ld_tgt_id = MEGASAS_TARGET_ID(sdev);
		instance->ld_tgtid_status[ld_tgt_id] = LD_TARGET_ID_ACTIVE;
		if (megasas_dbg_lvl & LD_PD_DEBUG)
			sdev_printk(KERN_INFO, sdev, "LD target ID %d created.\n", ld_tgt_id);
	}

	sdev->hostdata = mr_device_priv_data;

	atomic_set(&mr_device_priv_data->r1_ldio_hint,
		   instance->r1_ldio_hint_default);
	return 0;
}

static void megasas_sdev_destroy(struct scsi_device *sdev)
{
	u16 ld_tgt_id;
	struct megasas_instance *instance;

	instance = megasas_lookup_instance(sdev->host->host_no);

	if (MEGASAS_IS_LOGICAL(sdev)) {
		if (!MEGASAS_IS_LUN_VALID(sdev)) {
			sdev_printk(KERN_INFO, sdev, "%s: invalid LUN\n", __func__);
			return;
		}
		ld_tgt_id = MEGASAS_TARGET_ID(sdev);
		instance->ld_tgtid_status[ld_tgt_id] = LD_TARGET_ID_DELETED;
		if (megasas_dbg_lvl & LD_PD_DEBUG)
			sdev_printk(KERN_INFO, sdev,
				    "LD target ID %d removed from OS stack\n", ld_tgt_id);
	}

	kfree(sdev->hostdata);
	sdev->hostdata = NULL;
}

/*
* megasas_complete_outstanding_ioctls - Complete outstanding ioctls after a
*                                       kill adapter
* @instance:				Adapter soft state
*
*/
static void megasas_complete_outstanding_ioctls(struct megasas_instance *instance)
{
	int i;
	struct megasas_cmd *cmd_mfi;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion = instance->ctrl_context;

	/* Find all outstanding ioctls */
	if (fusion) {
		for (i = 0; i < instance->max_fw_cmds; i++) {
			cmd_fusion = fusion->cmd_list[i];
			if (cmd_fusion->sync_cmd_idx != (u32)ULONG_MAX) {
				cmd_mfi = instance->cmd_list[cmd_fusion->sync_cmd_idx];
				if (cmd_mfi->sync_cmd &&
				    (cmd_mfi->frame->hdr.cmd != MFI_CMD_ABORT)) {
					cmd_mfi->frame->hdr.cmd_status =
							MFI_STAT_WRONG_STATE;
					megasas_complete_cmd(instance,
							     cmd_mfi, DID_OK);
				}
			}
		}
	} else {
		for (i = 0; i < instance->max_fw_cmds; i++) {
			cmd_mfi = instance->cmd_list[i];
			if (cmd_mfi->sync_cmd && cmd_mfi->frame->hdr.cmd !=
				MFI_CMD_ABORT)
				megasas_complete_cmd(instance, cmd_mfi, DID_OK);
		}
	}
}


void megaraid_sas_kill_hba(struct megasas_instance *instance)
{
	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_warn(&instance->pdev->dev,
			 "Adapter already dead, skipping kill HBA\n");
		return;
	}

	/* Set critical error to block I/O & ioctls in case caller didn't */
	atomic_set(&instance->adprecovery, MEGASAS_HW_CRITICAL_ERROR);
	/* Wait 1 second to ensure IO or ioctls in build have posted */
	msleep(1000);
	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_SAS0073SKINNY) ||
		(instance->pdev->device == PCI_DEVICE_ID_LSI_SAS0071SKINNY) ||
		(instance->adapter_type != MFI_SERIES)) {
		if (!instance->requestorId) {
			writel(MFI_STOP_ADP, &instance->reg_set->doorbell);
			/* Flush */
			readl(&instance->reg_set->doorbell);
		}
		if (instance->requestorId && instance->peerIsPresent)
			memset(instance->ld_ids, 0xff, MEGASAS_MAX_LD_IDS);
	} else {
		writel(MFI_STOP_ADP,
			&instance->reg_set->inbound_doorbell);
	}
	/* Complete outstanding ioctls when adapter is killed */
	megasas_complete_outstanding_ioctls(instance);
}

 /**
  * megasas_check_and_restore_queue_depth - Check if queue depth needs to be
  *					restored to max value
  * @instance:			Adapter soft state
  *
  */
void
megasas_check_and_restore_queue_depth(struct megasas_instance *instance)
{
	unsigned long flags;

	if (instance->flag & MEGASAS_FW_BUSY
	    && time_after(jiffies, instance->last_time + 5 * HZ)
	    && atomic_read(&instance->fw_outstanding) <
	    instance->throttlequeuedepth + 1) {

		spin_lock_irqsave(instance->host->host_lock, flags);
		instance->flag &= ~MEGASAS_FW_BUSY;

		instance->host->can_queue = instance->cur_can_queue;
		spin_unlock_irqrestore(instance->host->host_lock, flags);
	}
}

/**
 * megasas_complete_cmd_dpc	 -	Returns FW's controller structure
 * @instance_addr:			Address of adapter soft state
 *
 * Tasklet to complete cmds
 */
static void megasas_complete_cmd_dpc(unsigned long instance_addr)
{
	u32 producer;
	u32 consumer;
	u32 context;
	struct megasas_cmd *cmd;
	struct megasas_instance *instance =
				(struct megasas_instance *)instance_addr;
	unsigned long flags;

	/* If we have already declared adapter dead, donot complete cmds */
	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR)
		return;

	spin_lock_irqsave(&instance->completion_lock, flags);

	producer = le32_to_cpu(*instance->producer);
	consumer = le32_to_cpu(*instance->consumer);

	while (consumer != producer) {
		context = le32_to_cpu(instance->reply_queue[consumer]);
		if (context >= instance->max_fw_cmds) {
			dev_err(&instance->pdev->dev, "Unexpected context value %x\n",
				context);
			BUG();
		}

		cmd = instance->cmd_list[context];

		megasas_complete_cmd(instance, cmd, DID_OK);

		consumer++;
		if (consumer == (instance->max_fw_cmds + 1)) {
			consumer = 0;
		}
	}

	*instance->consumer = cpu_to_le32(producer);

	spin_unlock_irqrestore(&instance->completion_lock, flags);

	/*
	 * Check if we can restore can_queue
	 */
	megasas_check_and_restore_queue_depth(instance);
}

static void megasas_sriov_heartbeat_handler(struct timer_list *t);

/**
 * megasas_start_timer - Initializes sriov heartbeat timer object
 * @instance:		Adapter soft state
 *
 */
void megasas_start_timer(struct megasas_instance *instance)
{
	struct timer_list *timer = &instance->sriov_heartbeat_timer;

	timer_setup(timer, megasas_sriov_heartbeat_handler, 0);
	timer->expires = jiffies + MEGASAS_SRIOV_HEARTBEAT_INTERVAL_VF;
	add_timer(timer);
}

static void
megasas_internal_reset_defer_cmds(struct megasas_instance *instance);

static void
process_fw_state_change_wq(struct work_struct *work);

static void megasas_do_ocr(struct megasas_instance *instance)
{
	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_SAS1064R) ||
	(instance->pdev->device == PCI_DEVICE_ID_DELL_PERC5) ||
	(instance->pdev->device == PCI_DEVICE_ID_LSI_VERDE_ZCR)) {
		*instance->consumer = cpu_to_le32(MEGASAS_ADPRESET_INPROG_SIGN);
	}
	instance->instancet->disable_intr(instance);
	atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_INFAULT);
	instance->issuepend_done = 0;

	atomic_set(&instance->fw_outstanding, 0);
	megasas_internal_reset_defer_cmds(instance);
	process_fw_state_change_wq(&instance->work_init);
}

static int megasas_get_ld_vf_affiliation_111(struct megasas_instance *instance,
					    int initial)
{
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_LD_VF_AFFILIATION_111 *new_affiliation_111 = NULL;
	dma_addr_t new_affiliation_111_h;
	int ld, retval = 0;
	u8 thisVf;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "megasas_get_ld_vf_affiliation_111:"
		       "Failed to get cmd for scsi%d\n",
			instance->host->host_no);
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	if (!instance->vf_affiliation_111) {
		dev_warn(&instance->pdev->dev, "SR-IOV: Couldn't get LD/VF "
		       "affiliation for scsi%d\n", instance->host->host_no);
		megasas_return_cmd(instance, cmd);
		return -ENOMEM;
	}

	if (initial)
			memset(instance->vf_affiliation_111, 0,
			       sizeof(struct MR_LD_VF_AFFILIATION_111));
	else {
		new_affiliation_111 =
			dma_alloc_coherent(&instance->pdev->dev,
					   sizeof(struct MR_LD_VF_AFFILIATION_111),
					   &new_affiliation_111_h, GFP_KERNEL);
		if (!new_affiliation_111) {
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "SR-IOV: Couldn't allocate "
			       "memory for new affiliation for scsi%d\n",
			       instance->host->host_no);
			megasas_return_cmd(instance, cmd);
			return -ENOMEM;
		}
	}

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_BOTH);
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len =
		cpu_to_le32(sizeof(struct MR_LD_VF_AFFILIATION_111));
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_VF_MAP_GET_ALL_LDS_111);

	if (initial)
		dcmd->sgl.sge32[0].phys_addr =
			cpu_to_le32(instance->vf_affiliation_111_h);
	else
		dcmd->sgl.sge32[0].phys_addr =
			cpu_to_le32(new_affiliation_111_h);

	dcmd->sgl.sge32[0].length = cpu_to_le32(
		sizeof(struct MR_LD_VF_AFFILIATION_111));

	dev_warn(&instance->pdev->dev, "SR-IOV: Getting LD/VF affiliation for "
	       "scsi%d\n", instance->host->host_no);

	if (megasas_issue_blocked_cmd(instance, cmd, 0) != DCMD_SUCCESS) {
		dev_warn(&instance->pdev->dev, "SR-IOV: LD/VF affiliation DCMD"
		       " failed with status 0x%x for scsi%d\n",
		       dcmd->cmd_status, instance->host->host_no);
		retval = 1; /* Do a scan if we couldn't get affiliation */
		goto out;
	}

	if (!initial) {
		thisVf = new_affiliation_111->thisVf;
		for (ld = 0 ; ld < new_affiliation_111->vdCount; ld++)
			if (instance->vf_affiliation_111->map[ld].policy[thisVf] !=
			    new_affiliation_111->map[ld].policy[thisVf]) {
				dev_warn(&instance->pdev->dev, "SR-IOV: "
				       "Got new LD/VF affiliation for scsi%d\n",
				       instance->host->host_no);
				memcpy(instance->vf_affiliation_111,
				       new_affiliation_111,
				       sizeof(struct MR_LD_VF_AFFILIATION_111));
				retval = 1;
				goto out;
			}
	}
out:
	if (new_affiliation_111) {
		dma_free_coherent(&instance->pdev->dev,
				    sizeof(struct MR_LD_VF_AFFILIATION_111),
				    new_affiliation_111,
				    new_affiliation_111_h);
	}

	megasas_return_cmd(instance, cmd);

	return retval;
}

static int megasas_get_ld_vf_affiliation_12(struct megasas_instance *instance,
					    int initial)
{
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_LD_VF_AFFILIATION *new_affiliation = NULL;
	struct MR_LD_VF_MAP *newmap = NULL, *savedmap = NULL;
	dma_addr_t new_affiliation_h;
	int i, j, retval = 0, found = 0, doscan = 0;
	u8 thisVf;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "megasas_get_ld_vf_affiliation12: "
		       "Failed to get cmd for scsi%d\n",
		       instance->host->host_no);
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	if (!instance->vf_affiliation) {
		dev_warn(&instance->pdev->dev, "SR-IOV: Couldn't get LD/VF "
		       "affiliation for scsi%d\n", instance->host->host_no);
		megasas_return_cmd(instance, cmd);
		return -ENOMEM;
	}

	if (initial)
		memset(instance->vf_affiliation, 0, (MAX_LOGICAL_DRIVES + 1) *
		       sizeof(struct MR_LD_VF_AFFILIATION));
	else {
		new_affiliation =
			dma_alloc_coherent(&instance->pdev->dev,
					   (MAX_LOGICAL_DRIVES + 1) * sizeof(struct MR_LD_VF_AFFILIATION),
					   &new_affiliation_h, GFP_KERNEL);
		if (!new_affiliation) {
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "SR-IOV: Couldn't allocate "
			       "memory for new affiliation for scsi%d\n",
			       instance->host->host_no);
			megasas_return_cmd(instance, cmd);
			return -ENOMEM;
		}
	}

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_BOTH);
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32((MAX_LOGICAL_DRIVES + 1) *
		sizeof(struct MR_LD_VF_AFFILIATION));
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_VF_MAP_GET_ALL_LDS);

	if (initial)
		dcmd->sgl.sge32[0].phys_addr =
			cpu_to_le32(instance->vf_affiliation_h);
	else
		dcmd->sgl.sge32[0].phys_addr =
			cpu_to_le32(new_affiliation_h);

	dcmd->sgl.sge32[0].length = cpu_to_le32((MAX_LOGICAL_DRIVES + 1) *
		sizeof(struct MR_LD_VF_AFFILIATION));

	dev_warn(&instance->pdev->dev, "SR-IOV: Getting LD/VF affiliation for "
	       "scsi%d\n", instance->host->host_no);


	if (megasas_issue_blocked_cmd(instance, cmd, 0) != DCMD_SUCCESS) {
		dev_warn(&instance->pdev->dev, "SR-IOV: LD/VF affiliation DCMD"
		       " failed with status 0x%x for scsi%d\n",
		       dcmd->cmd_status, instance->host->host_no);
		retval = 1; /* Do a scan if we couldn't get affiliation */
		goto out;
	}

	if (!initial) {
		if (!new_affiliation->ldCount) {
			dev_warn(&instance->pdev->dev, "SR-IOV: Got new LD/VF "
			       "affiliation for passive path for scsi%d\n",
			       instance->host->host_no);
			retval = 1;
			goto out;
		}
		newmap = new_affiliation->map;
		savedmap = instance->vf_affiliation->map;
		thisVf = new_affiliation->thisVf;
		for (i = 0 ; i < new_affiliation->ldCount; i++) {
			found = 0;
			for (j = 0; j < instance->vf_affiliation->ldCount;
			     j++) {
				if (newmap->ref.targetId ==
				    savedmap->ref.targetId) {
					found = 1;
					if (newmap->policy[thisVf] !=
					    savedmap->policy[thisVf]) {
						doscan = 1;
						goto out;
					}
				}
				savedmap = (struct MR_LD_VF_MAP *)
					((unsigned char *)savedmap +
					 savedmap->size);
			}
			if (!found && newmap->policy[thisVf] !=
			    MR_LD_ACCESS_HIDDEN) {
				doscan = 1;
				goto out;
			}
			newmap = (struct MR_LD_VF_MAP *)
				((unsigned char *)newmap + newmap->size);
		}

		newmap = new_affiliation->map;
		savedmap = instance->vf_affiliation->map;

		for (i = 0 ; i < instance->vf_affiliation->ldCount; i++) {
			found = 0;
			for (j = 0 ; j < new_affiliation->ldCount; j++) {
				if (savedmap->ref.targetId ==
				    newmap->ref.targetId) {
					found = 1;
					if (savedmap->policy[thisVf] !=
					    newmap->policy[thisVf]) {
						doscan = 1;
						goto out;
					}
				}
				newmap = (struct MR_LD_VF_MAP *)
					((unsigned char *)newmap +
					 newmap->size);
			}
			if (!found && savedmap->policy[thisVf] !=
			    MR_LD_ACCESS_HIDDEN) {
				doscan = 1;
				goto out;
			}
			savedmap = (struct MR_LD_VF_MAP *)
				((unsigned char *)savedmap +
				 savedmap->size);
		}
	}
out:
	if (doscan) {
		dev_warn(&instance->pdev->dev, "SR-IOV: Got new LD/VF "
		       "affiliation for scsi%d\n", instance->host->host_no);
		memcpy(instance->vf_affiliation, new_affiliation,
		       new_affiliation->size);
		retval = 1;
	}

	if (new_affiliation)
		dma_free_coherent(&instance->pdev->dev,
				    (MAX_LOGICAL_DRIVES + 1) *
				    sizeof(struct MR_LD_VF_AFFILIATION),
				    new_affiliation, new_affiliation_h);
	megasas_return_cmd(instance, cmd);

	return retval;
}

/* This function will get the current SR-IOV LD/VF affiliation */
static int megasas_get_ld_vf_affiliation(struct megasas_instance *instance,
	int initial)
{
	int retval;

	if (instance->PlasmaFW111)
		retval = megasas_get_ld_vf_affiliation_111(instance, initial);
	else
		retval = megasas_get_ld_vf_affiliation_12(instance, initial);
	return retval;
}

/* This function will tell FW to start the SR-IOV heartbeat */
int megasas_sriov_start_heartbeat(struct megasas_instance *instance,
					 int initial)
{
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	int retval = 0;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "megasas_sriov_start_heartbeat: "
		       "Failed to get cmd for scsi%d\n",
		       instance->host->host_no);
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	if (initial) {
		instance->hb_host_mem =
			dma_alloc_coherent(&instance->pdev->dev,
					   sizeof(struct MR_CTRL_HB_HOST_MEM),
					   &instance->hb_host_mem_h,
					   GFP_KERNEL);
		if (!instance->hb_host_mem) {
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "SR-IOV: Couldn't allocate"
			       " memory for heartbeat host memory for scsi%d\n",
			       instance->host->host_no);
			retval = -ENOMEM;
			goto out;
		}
	}

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->mbox.s[0] = cpu_to_le16(sizeof(struct MR_CTRL_HB_HOST_MEM));
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_BOTH);
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct MR_CTRL_HB_HOST_MEM));
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_SHARED_HOST_MEM_ALLOC);

	megasas_set_dma_settings(instance, dcmd, instance->hb_host_mem_h,
				 sizeof(struct MR_CTRL_HB_HOST_MEM));

	dev_warn(&instance->pdev->dev, "SR-IOV: Starting heartbeat for scsi%d\n",
	       instance->host->host_no);

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts)
		retval = megasas_issue_blocked_cmd(instance, cmd,
			MEGASAS_ROUTINE_WAIT_TIME_VF);
	else
		retval = megasas_issue_polled(instance, cmd);

	if (retval) {
		dev_warn(&instance->pdev->dev, "SR-IOV: MR_DCMD_CTRL_SHARED_HOST"
			"_MEM_ALLOC DCMD %s for scsi%d\n",
			(dcmd->cmd_status == MFI_STAT_INVALID_STATUS) ?
			"timed out" : "failed", instance->host->host_no);
		retval = 1;
	}

out:
	megasas_return_cmd(instance, cmd);

	return retval;
}

/* Handler for SR-IOV heartbeat */
static void megasas_sriov_heartbeat_handler(struct timer_list *t)
{
	struct megasas_instance *instance =
		from_timer(instance, t, sriov_heartbeat_timer);

	if (instance->hb_host_mem->HB.fwCounter !=
	    instance->hb_host_mem->HB.driverCounter) {
		instance->hb_host_mem->HB.driverCounter =
			instance->hb_host_mem->HB.fwCounter;
		mod_timer(&instance->sriov_heartbeat_timer,
			  jiffies + MEGASAS_SRIOV_HEARTBEAT_INTERVAL_VF);
	} else {
		dev_warn(&instance->pdev->dev, "SR-IOV: Heartbeat never "
		       "completed for scsi%d\n", instance->host->host_no);
		schedule_work(&instance->work_init);
	}
}

/**
 * megasas_wait_for_outstanding -	Wait for all outstanding cmds
 * @instance:				Adapter soft state
 *
 * This function waits for up to MEGASAS_RESET_WAIT_TIME seconds for FW to
 * complete all its outstanding commands. Returns error if one or more IOs
 * are pending after this time period. It also marks the controller dead.
 */
static int megasas_wait_for_outstanding(struct megasas_instance *instance)
{
	int i, sl, outstanding;
	u32 reset_index;
	u32 wait_time = MEGASAS_RESET_WAIT_TIME;
	unsigned long flags;
	struct list_head clist_local;
	struct megasas_cmd *reset_cmd;
	u32 fw_state;

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_info(&instance->pdev->dev, "%s:%d HBA is killed.\n",
		__func__, __LINE__);
		return FAILED;
	}

	if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL) {

		INIT_LIST_HEAD(&clist_local);
		spin_lock_irqsave(&instance->hba_lock, flags);
		list_splice_init(&instance->internal_reset_pending_q,
				&clist_local);
		spin_unlock_irqrestore(&instance->hba_lock, flags);

		dev_notice(&instance->pdev->dev, "HBA reset wait ...\n");
		for (i = 0; i < wait_time; i++) {
			msleep(1000);
			if (atomic_read(&instance->adprecovery) == MEGASAS_HBA_OPERATIONAL)
				break;
		}

		if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL) {
			dev_notice(&instance->pdev->dev, "reset: Stopping HBA.\n");
			atomic_set(&instance->adprecovery, MEGASAS_HW_CRITICAL_ERROR);
			return FAILED;
		}

		reset_index = 0;
		while (!list_empty(&clist_local)) {
			reset_cmd = list_entry((&clist_local)->next,
						struct megasas_cmd, list);
			list_del_init(&reset_cmd->list);
			if (reset_cmd->scmd) {
				reset_cmd->scmd->result = DID_REQUEUE << 16;
				dev_notice(&instance->pdev->dev, "%d:%p reset [%02x]\n",
					reset_index, reset_cmd,
					reset_cmd->scmd->cmnd[0]);

				scsi_done(reset_cmd->scmd);
				megasas_return_cmd(instance, reset_cmd);
			} else if (reset_cmd->sync_cmd) {
				dev_notice(&instance->pdev->dev, "%p synch cmds"
						"reset queue\n",
						reset_cmd);

				reset_cmd->cmd_status_drv = DCMD_INIT;
				instance->instancet->fire_cmd(instance,
						reset_cmd->frame_phys_addr,
						0, instance->reg_set);
			} else {
				dev_notice(&instance->pdev->dev, "%p unexpected"
					"cmds lst\n",
					reset_cmd);
			}
			reset_index++;
		}

		return SUCCESS;
	}

	for (i = 0; i < resetwaittime; i++) {
		outstanding = atomic_read(&instance->fw_outstanding);

		if (!outstanding)
			break;

		if (!(i % MEGASAS_RESET_NOTICE_INTERVAL)) {
			dev_notice(&instance->pdev->dev, "[%2d]waiting for %d "
			       "commands to complete\n",i,outstanding);
			/*
			 * Call cmd completion routine. Cmd to be
			 * be completed directly without depending on isr.
			 */
			megasas_complete_cmd_dpc((unsigned long)instance);
		}

		msleep(1000);
	}

	i = 0;
	outstanding = atomic_read(&instance->fw_outstanding);
	fw_state = instance->instancet->read_fw_status_reg(instance) & MFI_STATE_MASK;

	if ((!outstanding && (fw_state == MFI_STATE_OPERATIONAL)))
		goto no_outstanding;

	if (instance->disableOnlineCtrlReset)
		goto kill_hba_and_failed;
	do {
		if ((fw_state == MFI_STATE_FAULT) || atomic_read(&instance->fw_outstanding)) {
			dev_info(&instance->pdev->dev,
				"%s:%d waiting_for_outstanding: before issue OCR. FW state = 0x%x, outstanding 0x%x\n",
				__func__, __LINE__, fw_state, atomic_read(&instance->fw_outstanding));
			if (i == 3)
				goto kill_hba_and_failed;
			megasas_do_ocr(instance);

			if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
				dev_info(&instance->pdev->dev, "%s:%d OCR failed and HBA is killed.\n",
				__func__, __LINE__);
				return FAILED;
			}
			dev_info(&instance->pdev->dev, "%s:%d waiting_for_outstanding: after issue OCR.\n",
				__func__, __LINE__);

			for (sl = 0; sl < 10; sl++)
				msleep(500);

			outstanding = atomic_read(&instance->fw_outstanding);

			fw_state = instance->instancet->read_fw_status_reg(instance) & MFI_STATE_MASK;
			if ((!outstanding && (fw_state == MFI_STATE_OPERATIONAL)))
				goto no_outstanding;
		}
		i++;
	} while (i <= 3);

no_outstanding:

	dev_info(&instance->pdev->dev, "%s:%d no more pending commands remain after reset handling.\n",
		__func__, __LINE__);
	return SUCCESS;

kill_hba_and_failed:

	/* Reset not supported, kill adapter */
	dev_info(&instance->pdev->dev, "%s:%d killing adapter scsi%d"
		" disableOnlineCtrlReset %d fw_outstanding %d \n",
		__func__, __LINE__, instance->host->host_no, instance->disableOnlineCtrlReset,
		atomic_read(&instance->fw_outstanding));
	megasas_dump_pending_frames(instance);
	megaraid_sas_kill_hba(instance);

	return FAILED;
}

/**
 * megasas_generic_reset -	Generic reset routine
 * @scmd:			Mid-layer SCSI command
 *
 * This routine implements a generic reset handler for device, bus and host
 * reset requests. Device, bus and host specific reset handlers can use this
 * function after they do their specific tasks.
 */
static int megasas_generic_reset(struct scsi_cmnd *scmd)
{
	int ret_val;
	struct megasas_instance *instance;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	scmd_printk(KERN_NOTICE, scmd, "megasas: RESET cmd=%x retries=%x\n",
		 scmd->cmnd[0], scmd->retries);

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_err(&instance->pdev->dev, "cannot recover from previous reset failures\n");
		return FAILED;
	}

	ret_val = megasas_wait_for_outstanding(instance);
	if (ret_val == SUCCESS)
		dev_notice(&instance->pdev->dev, "reset successful\n");
	else
		dev_err(&instance->pdev->dev, "failed to do reset\n");

	return ret_val;
}

/**
 * megasas_reset_timer - quiesce the adapter if required
 * @scmd:		scsi cmnd
 *
 * Sets the FW busy flag and reduces the host->can_queue if the
 * cmd has not been completed within the timeout period.
 */
static enum scsi_timeout_action megasas_reset_timer(struct scsi_cmnd *scmd)
{
	struct megasas_instance *instance;
	unsigned long flags;

	if (time_after(jiffies, scmd->jiffies_at_alloc +
				(scmd_timeout * 2) * HZ)) {
		return SCSI_EH_NOT_HANDLED;
	}

	instance = (struct megasas_instance *)scmd->device->host->hostdata;
	if (!(instance->flag & MEGASAS_FW_BUSY)) {
		/* FW is busy, throttle IO */
		spin_lock_irqsave(instance->host->host_lock, flags);

		instance->host->can_queue = instance->throttlequeuedepth;
		instance->last_time = jiffies;
		instance->flag |= MEGASAS_FW_BUSY;

		spin_unlock_irqrestore(instance->host->host_lock, flags);
	}
	return SCSI_EH_RESET_TIMER;
}

/**
 * megasas_dump -	This function will print hexdump of provided buffer.
 * @buf:		Buffer to be dumped
 * @sz:		Size in bytes
 * @format:		Different formats of dumping e.g. format=n will
 *			cause only 'n' 32 bit words to be dumped in a single
 *			line.
 */
inline void
megasas_dump(void *buf, int sz, int format)
{
	int i;
	__le32 *buf_loc = (__le32 *)buf;

	for (i = 0; i < (sz / sizeof(__le32)); i++) {
		if ((i % format) == 0) {
			if (i != 0)
				printk(KERN_CONT "\n");
			printk(KERN_CONT "%08x: ", (i * 4));
		}
		printk(KERN_CONT "%08x ", le32_to_cpu(buf_loc[i]));
	}
	printk(KERN_CONT "\n");
}

/**
 * megasas_dump_reg_set -	This function will print hexdump of register set
 * @reg_set:	Register set to be dumped
 */
inline void
megasas_dump_reg_set(void __iomem *reg_set)
{
	unsigned int i, sz = 256;
	u32 __iomem *reg = (u32 __iomem *)reg_set;

	for (i = 0; i < (sz / sizeof(u32)); i++)
		printk("%08x: %08x\n", (i * 4), readl(&reg[i]));
}

/**
 * megasas_dump_fusion_io -	This function will print key details
 *				of SCSI IO
 * @scmd:			SCSI command pointer of SCSI IO
 */
void
megasas_dump_fusion_io(struct scsi_cmnd *scmd)
{
	struct megasas_cmd_fusion *cmd = megasas_priv(scmd)->cmd_priv;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	struct megasas_instance *instance;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	scmd_printk(KERN_INFO, scmd,
		    "scmd: (0x%p)  retries: 0x%x  allowed: 0x%x\n",
		    scmd, scmd->retries, scmd->allowed);
	scsi_print_command(scmd);

	if (cmd) {
		req_desc = (union MEGASAS_REQUEST_DESCRIPTOR_UNION *)cmd->request_desc;
		scmd_printk(KERN_INFO, scmd, "Request descriptor details:\n");
		scmd_printk(KERN_INFO, scmd,
			    "RequestFlags:0x%x  MSIxIndex:0x%x  SMID:0x%x  LMID:0x%x  DevHandle:0x%x\n",
			    req_desc->SCSIIO.RequestFlags,
			    req_desc->SCSIIO.MSIxIndex, req_desc->SCSIIO.SMID,
			    req_desc->SCSIIO.LMID, req_desc->SCSIIO.DevHandle);

		printk(KERN_INFO "IO request frame:\n");
		megasas_dump(cmd->io_request,
			     MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE, 8);
		printk(KERN_INFO "Chain frame:\n");
		megasas_dump(cmd->sg_frame,
			     instance->max_chain_frame_sz, 8);
	}

}

/*
 * megasas_dump_sys_regs - This function will dump system registers through
 *			    sysfs.
 * @reg_set:		    Pointer to System register set.
 * @buf:		    Buffer to which output is to be written.
 * @return:		    Number of bytes written to buffer.
 */
static inline ssize_t
megasas_dump_sys_regs(void __iomem *reg_set, char *buf)
{
	unsigned int i, sz = 256;
	int bytes_wrote = 0;
	char *loc = (char *)buf;
	u32 __iomem *reg = (u32 __iomem *)reg_set;

	for (i = 0; i < sz / sizeof(u32); i++) {
		bytes_wrote += scnprintf(loc + bytes_wrote,
					 PAGE_SIZE - bytes_wrote,
					 "%08x: %08x\n", (i * 4),
					 readl(&reg[i]));
	}
	return bytes_wrote;
}

/**
 * megasas_reset_bus_host -	Bus & host reset handler entry point
 * @scmd:			Mid-layer SCSI command
 */
static int megasas_reset_bus_host(struct scsi_cmnd *scmd)
{
	int ret;
	struct megasas_instance *instance;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	scmd_printk(KERN_INFO, scmd,
		"OCR is requested due to IO timeout!!\n");

	scmd_printk(KERN_INFO, scmd,
		"SCSI host state: %d  SCSI host busy: %d  FW outstanding: %d\n",
		scmd->device->host->shost_state,
		scsi_host_busy(scmd->device->host),
		atomic_read(&instance->fw_outstanding));
	/*
	 * First wait for all commands to complete
	 */
	if (instance->adapter_type == MFI_SERIES) {
		ret = megasas_generic_reset(scmd);
	} else {
		megasas_dump_fusion_io(scmd);
		ret = megasas_reset_fusion(scmd->device->host,
				SCSIIO_TIMEOUT_OCR);
	}

	return ret;
}

/**
 * megasas_task_abort - Issues task abort request to firmware
 *			(supported only for fusion adapters)
 * @scmd:		SCSI command pointer
 */
static int megasas_task_abort(struct scsi_cmnd *scmd)
{
	int ret;
	struct megasas_instance *instance;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	if (instance->adapter_type != MFI_SERIES)
		ret = megasas_task_abort_fusion(scmd);
	else {
		sdev_printk(KERN_NOTICE, scmd->device, "TASK ABORT not supported\n");
		ret = FAILED;
	}

	return ret;
}

/**
 * megasas_reset_target:  Issues target reset request to firmware
 *                        (supported only for fusion adapters)
 * @scmd:                 SCSI command pointer
 */
static int megasas_reset_target(struct scsi_cmnd *scmd)
{
	int ret;
	struct megasas_instance *instance;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	if (instance->adapter_type != MFI_SERIES)
		ret = megasas_reset_target_fusion(scmd);
	else {
		sdev_printk(KERN_NOTICE, scmd->device, "TARGET RESET not supported\n");
		ret = FAILED;
	}

	return ret;
}

/**
 * megasas_bios_param - Returns disk geometry for a disk
 * @sdev:		device handle
 * @bdev:		block device
 * @capacity:		drive capacity
 * @geom:		geometry parameters
 */
static int
megasas_bios_param(struct scsi_device *sdev, struct block_device *bdev,
		 sector_t capacity, int geom[])
{
	int heads;
	int sectors;
	sector_t cylinders;
	unsigned long tmp;

	/* Default heads (64) & sectors (32) */
	heads = 64;
	sectors = 32;

	tmp = heads * sectors;
	cylinders = capacity;

	sector_div(cylinders, tmp);

	/*
	 * Handle extended translation size for logical drives > 1Gb
	 */

	if (capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		tmp = heads*sectors;
		cylinders = capacity;
		sector_div(cylinders, tmp);
	}

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return 0;
}

static void megasas_map_queues(struct Scsi_Host *shost)
{
	struct megasas_instance *instance;
	int qoff = 0, offset;
	struct blk_mq_queue_map *map;

	instance = (struct megasas_instance *)shost->hostdata;

	if (shost->nr_hw_queues == 1)
		return;

	offset = instance->low_latency_index_start;

	/* Setup Default hctx */
	map = &shost->tag_set.map[HCTX_TYPE_DEFAULT];
	map->nr_queues = instance->msix_vectors - offset;
	map->queue_offset = 0;
	blk_mq_map_hw_queues(map, &instance->pdev->dev, offset);
	qoff += map->nr_queues;
	offset += map->nr_queues;

	/* we never use READ queue, so can't cheat blk-mq */
	shost->tag_set.map[HCTX_TYPE_READ].nr_queues = 0;

	/* Setup Poll hctx */
	map = &shost->tag_set.map[HCTX_TYPE_POLL];
	map->nr_queues = instance->iopoll_q_count;
	if (map->nr_queues) {
		/*
		 * The poll queue(s) doesn't have an IRQ (and hence IRQ
		 * affinity), so use the regular blk-mq cpu mapping
		 */
		map->queue_offset = qoff;
		blk_mq_map_queues(map);
	}
}

static void megasas_aen_polling(struct work_struct *work);

/**
 * megasas_service_aen -	Processes an event notification
 * @instance:			Adapter soft state
 * @cmd:			AEN command completed by the ISR
 *
 * For AEN, driver sends a command down to FW that is held by the FW till an
 * event occurs. When an event of interest occurs, FW completes the command
 * that it was previously holding.
 *
 * This routines sends SIGIO signal to processes that have registered with the
 * driver for AEN.
 */
static void
megasas_service_aen(struct megasas_instance *instance, struct megasas_cmd *cmd)
{
	unsigned long flags;

	/*
	 * Don't signal app if it is just an aborted previously registered aen
	 */
	if ((!cmd->abort_aen) && (instance->unload == 0)) {
		spin_lock_irqsave(&poll_aen_lock, flags);
		megasas_poll_wait_aen = 1;
		spin_unlock_irqrestore(&poll_aen_lock, flags);
		wake_up(&megasas_poll_wait);
		kill_fasync(&megasas_async_queue, SIGIO, POLL_IN);
	}
	else
		cmd->abort_aen = 0;

	instance->aen_cmd = NULL;

	megasas_return_cmd(instance, cmd);

	if ((instance->unload == 0) &&
		((instance->issuepend_done == 1))) {
		struct megasas_aen_event *ev;

		ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
		if (!ev) {
			dev_err(&instance->pdev->dev, "megasas_service_aen: out of memory\n");
		} else {
			ev->instance = instance;
			instance->ev = ev;
			INIT_DELAYED_WORK(&ev->hotplug_work,
					  megasas_aen_polling);
			schedule_delayed_work(&ev->hotplug_work, 0);
		}
	}
}

static ssize_t
fw_crash_buffer_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance =
		(struct megasas_instance *) shost->hostdata;
	int val = 0;

	if (kstrtoint(buf, 0, &val) != 0)
		return -EINVAL;

	mutex_lock(&instance->crashdump_lock);
	instance->fw_crash_buffer_offset = val;
	mutex_unlock(&instance->crashdump_lock);
	return strlen(buf);
}

static ssize_t
fw_crash_buffer_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance =
		(struct megasas_instance *) shost->hostdata;
	u32 size;
	unsigned long dmachunk = CRASH_DMA_BUF_SIZE;
	unsigned long chunk_left_bytes;
	unsigned long src_addr;
	u32 buff_offset;

	mutex_lock(&instance->crashdump_lock);
	buff_offset = instance->fw_crash_buffer_offset;
	if (!instance->crash_dump_buf ||
		!((instance->fw_crash_state == AVAILABLE) ||
		(instance->fw_crash_state == COPYING))) {
		dev_err(&instance->pdev->dev,
			"Firmware crash dump is not available\n");
		mutex_unlock(&instance->crashdump_lock);
		return -EINVAL;
	}

	if (buff_offset > (instance->fw_crash_buffer_size * dmachunk)) {
		dev_err(&instance->pdev->dev,
			"Firmware crash dump offset is out of range\n");
		mutex_unlock(&instance->crashdump_lock);
		return 0;
	}

	size = (instance->fw_crash_buffer_size * dmachunk) - buff_offset;
	chunk_left_bytes = dmachunk - (buff_offset % dmachunk);
	size = (size > chunk_left_bytes) ? chunk_left_bytes : size;
	size = (size >= PAGE_SIZE) ? (PAGE_SIZE - 1) : size;

	src_addr = (unsigned long)instance->crash_buf[buff_offset / dmachunk] +
		(buff_offset % dmachunk);
	memcpy(buf, (void *)src_addr, size);
	mutex_unlock(&instance->crashdump_lock);

	return size;
}

static ssize_t
fw_crash_buffer_size_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance =
		(struct megasas_instance *) shost->hostdata;

	return snprintf(buf, PAGE_SIZE, "%ld\n", (unsigned long)
		((instance->fw_crash_buffer_size) * 1024 * 1024)/PAGE_SIZE);
}

static ssize_t
fw_crash_state_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance =
		(struct megasas_instance *) shost->hostdata;
	int val = 0;

	if (kstrtoint(buf, 0, &val) != 0)
		return -EINVAL;

	if ((val <= AVAILABLE || val > COPY_ERROR)) {
		dev_err(&instance->pdev->dev, "application updates invalid "
			"firmware crash state\n");
		return -EINVAL;
	}

	instance->fw_crash_state = val;

	if ((val == COPIED) || (val == COPY_ERROR)) {
		mutex_lock(&instance->crashdump_lock);
		megasas_free_host_crash_buffer(instance);
		mutex_unlock(&instance->crashdump_lock);
		if (val == COPY_ERROR)
			dev_info(&instance->pdev->dev, "application failed to "
				"copy Firmware crash dump\n");
		else
			dev_info(&instance->pdev->dev, "Firmware crash dump "
				"copied successfully\n");
	}
	return strlen(buf);
}

static ssize_t
fw_crash_state_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance =
		(struct megasas_instance *) shost->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d\n", instance->fw_crash_state);
}

static ssize_t
page_size_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%ld\n", (unsigned long)PAGE_SIZE - 1);
}

static ssize_t
ldio_outstanding_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance = (struct megasas_instance *)shost->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&instance->ldio_outstanding));
}

static ssize_t
fw_cmds_outstanding_show(struct device *cdev,
				 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance = (struct megasas_instance *)shost->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&instance->fw_outstanding));
}

static ssize_t
enable_sdev_max_qd_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance = (struct megasas_instance *)shost->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d\n", instance->enable_sdev_max_qd);
}

static ssize_t
enable_sdev_max_qd_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance = (struct megasas_instance *)shost->hostdata;
	u32 val = 0;
	bool is_target_prop;
	int ret_target_prop = DCMD_FAILED;
	struct scsi_device *sdev;

	if (kstrtou32(buf, 0, &val) != 0) {
		pr_err("megasas: could not set enable_sdev_max_qd\n");
		return -EINVAL;
	}

	mutex_lock(&instance->reset_mutex);
	if (val)
		instance->enable_sdev_max_qd = true;
	else
		instance->enable_sdev_max_qd = false;

	shost_for_each_device(sdev, shost) {
		ret_target_prop = megasas_get_target_prop(instance, sdev);
		is_target_prop = (ret_target_prop == DCMD_SUCCESS) ? true : false;
		megasas_set_fw_assisted_qd(sdev, is_target_prop);
	}
	mutex_unlock(&instance->reset_mutex);

	return strlen(buf);
}

static ssize_t
dump_system_regs_show(struct device *cdev,
			       struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance =
			(struct megasas_instance *)shost->hostdata;

	return megasas_dump_sys_regs(instance->reg_set, buf);
}

static ssize_t
raid_map_id_show(struct device *cdev, struct device_attribute *attr,
			  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct megasas_instance *instance =
			(struct megasas_instance *)shost->hostdata;

	return snprintf(buf, PAGE_SIZE, "%ld\n",
			(unsigned long)instance->map_id);
}

static DEVICE_ATTR_RW(fw_crash_buffer);
static DEVICE_ATTR_RO(fw_crash_buffer_size);
static DEVICE_ATTR_RW(fw_crash_state);
static DEVICE_ATTR_RO(page_size);
static DEVICE_ATTR_RO(ldio_outstanding);
static DEVICE_ATTR_RO(fw_cmds_outstanding);
static DEVICE_ATTR_RW(enable_sdev_max_qd);
static DEVICE_ATTR_RO(dump_system_regs);
static DEVICE_ATTR_RO(raid_map_id);

static struct attribute *megaraid_host_attrs[] = {
	&dev_attr_fw_crash_buffer_size.attr,
	&dev_attr_fw_crash_buffer.attr,
	&dev_attr_fw_crash_state.attr,
	&dev_attr_page_size.attr,
	&dev_attr_ldio_outstanding.attr,
	&dev_attr_fw_cmds_outstanding.attr,
	&dev_attr_enable_sdev_max_qd.attr,
	&dev_attr_dump_system_regs.attr,
	&dev_attr_raid_map_id.attr,
	NULL,
};

ATTRIBUTE_GROUPS(megaraid_host);

/*
 * Scsi host template for megaraid_sas driver
 */
static const struct scsi_host_template megasas_template = {

	.module = THIS_MODULE,
	.name = "Avago SAS based MegaRAID driver",
	.proc_name = "megaraid_sas",
	.sdev_configure = megasas_sdev_configure,
	.sdev_init = megasas_sdev_init,
	.sdev_destroy = megasas_sdev_destroy,
	.queuecommand = megasas_queue_command,
	.eh_target_reset_handler = megasas_reset_target,
	.eh_abort_handler = megasas_task_abort,
	.eh_host_reset_handler = megasas_reset_bus_host,
	.eh_timed_out = megasas_reset_timer,
	.shost_groups = megaraid_host_groups,
	.bios_param = megasas_bios_param,
	.map_queues = megasas_map_queues,
	.mq_poll = megasas_blk_mq_poll,
	.change_queue_depth = scsi_change_queue_depth,
	.max_segment_size = 0xffffffff,
	.cmd_size = sizeof(struct megasas_cmd_priv),
};

/**
 * megasas_complete_int_cmd -	Completes an internal command
 * @instance:			Adapter soft state
 * @cmd:			Command to be completed
 *
 * The megasas_issue_blocked_cmd() function waits for a command to complete
 * after it issues a command. This function wakes up that waiting routine by
 * calling wake_up() on the wait queue.
 */
static void
megasas_complete_int_cmd(struct megasas_instance *instance,
			 struct megasas_cmd *cmd)
{
	if (cmd->cmd_status_drv == DCMD_INIT)
		cmd->cmd_status_drv =
		(cmd->frame->io.cmd_status == MFI_STAT_OK) ?
		DCMD_SUCCESS : DCMD_FAILED;

	wake_up(&instance->int_cmd_wait_q);
}

/**
 * megasas_complete_abort -	Completes aborting a command
 * @instance:			Adapter soft state
 * @cmd:			Cmd that was issued to abort another cmd
 *
 * The megasas_issue_blocked_abort_cmd() function waits on abort_cmd_wait_q
 * after it issues an abort on a previously issued command. This function
 * wakes up all functions waiting on the same wait queue.
 */
static void
megasas_complete_abort(struct megasas_instance *instance,
		       struct megasas_cmd *cmd)
{
	if (cmd->sync_cmd) {
		cmd->sync_cmd = 0;
		cmd->cmd_status_drv = DCMD_SUCCESS;
		wake_up(&instance->abort_cmd_wait_q);
	}
}

static void
megasas_set_ld_removed_by_fw(struct megasas_instance *instance)
{
	uint i;

	for (i = 0; (i < MEGASAS_MAX_LD_IDS); i++) {
		if (instance->ld_ids_prev[i] != 0xff &&
		    instance->ld_ids_from_raidmap[i] == 0xff) {
			if (megasas_dbg_lvl & LD_PD_DEBUG)
				dev_info(&instance->pdev->dev,
					 "LD target ID %d removed from RAID map\n", i);
			instance->ld_tgtid_status[i] = LD_TARGET_ID_DELETED;
		}
	}
}

/**
 * megasas_complete_cmd -	Completes a command
 * @instance:			Adapter soft state
 * @cmd:			Command to be completed
 * @alt_status:			If non-zero, use this value as status to
 *				SCSI mid-layer instead of the value returned
 *				by the FW. This should be used if caller wants
 *				an alternate status (as in the case of aborted
 *				commands)
 */
void
megasas_complete_cmd(struct megasas_instance *instance, struct megasas_cmd *cmd,
		     u8 alt_status)
{
	int exception = 0;
	struct megasas_header *hdr = &cmd->frame->hdr;
	unsigned long flags;
	struct fusion_context *fusion = instance->ctrl_context;
	u32 opcode, status;

	/* flag for the retry reset */
	cmd->retry_for_fw_reset = 0;

	if (cmd->scmd)
		megasas_priv(cmd->scmd)->cmd_priv = NULL;

	switch (hdr->cmd) {
	case MFI_CMD_INVALID:
		/* Some older 1068 controller FW may keep a pended
		   MR_DCMD_CTRL_EVENT_GET_INFO left over from the main kernel
		   when booting the kdump kernel.  Ignore this command to
		   prevent a kernel panic on shutdown of the kdump kernel. */
		dev_warn(&instance->pdev->dev, "MFI_CMD_INVALID command "
		       "completed\n");
		dev_warn(&instance->pdev->dev, "If you have a controller "
		       "other than PERC5, please upgrade your firmware\n");
		break;
	case MFI_CMD_PD_SCSI_IO:
	case MFI_CMD_LD_SCSI_IO:

		/*
		 * MFI_CMD_PD_SCSI_IO and MFI_CMD_LD_SCSI_IO could have been
		 * issued either through an IO path or an IOCTL path. If it
		 * was via IOCTL, we will send it to internal completion.
		 */
		if (cmd->sync_cmd) {
			cmd->sync_cmd = 0;
			megasas_complete_int_cmd(instance, cmd);
			break;
		}
		fallthrough;

	case MFI_CMD_LD_READ:
	case MFI_CMD_LD_WRITE:

		if (alt_status) {
			cmd->scmd->result = alt_status << 16;
			exception = 1;
		}

		if (exception) {

			atomic_dec(&instance->fw_outstanding);

			scsi_dma_unmap(cmd->scmd);
			scsi_done(cmd->scmd);
			megasas_return_cmd(instance, cmd);

			break;
		}

		switch (hdr->cmd_status) {

		case MFI_STAT_OK:
			cmd->scmd->result = DID_OK << 16;
			break;

		case MFI_STAT_SCSI_IO_FAILED:
		case MFI_STAT_LD_INIT_IN_PROGRESS:
			cmd->scmd->result =
			    (DID_ERROR << 16) | hdr->scsi_status;
			break;

		case MFI_STAT_SCSI_DONE_WITH_ERROR:

			cmd->scmd->result = (DID_OK << 16) | hdr->scsi_status;

			if (hdr->scsi_status == SAM_STAT_CHECK_CONDITION) {
				memset(cmd->scmd->sense_buffer, 0,
				       SCSI_SENSE_BUFFERSIZE);
				memcpy(cmd->scmd->sense_buffer, cmd->sense,
				       hdr->sense_len);
			}

			break;

		case MFI_STAT_LD_OFFLINE:
		case MFI_STAT_DEVICE_NOT_FOUND:
			cmd->scmd->result = DID_BAD_TARGET << 16;
			break;

		default:
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "MFI FW status %#x\n",
			       hdr->cmd_status);
			cmd->scmd->result = DID_ERROR << 16;
			break;
		}

		atomic_dec(&instance->fw_outstanding);

		scsi_dma_unmap(cmd->scmd);
		scsi_done(cmd->scmd);
		megasas_return_cmd(instance, cmd);

		break;

	case MFI_CMD_SMP:
	case MFI_CMD_STP:
	case MFI_CMD_NVME:
	case MFI_CMD_TOOLBOX:
		megasas_complete_int_cmd(instance, cmd);
		break;

	case MFI_CMD_DCMD:
		opcode = le32_to_cpu(cmd->frame->dcmd.opcode);
		/* Check for LD map update */
		if ((opcode == MR_DCMD_LD_MAP_GET_INFO)
			&& (cmd->frame->dcmd.mbox.b[1] == 1)) {
			fusion->fast_path_io = 0;
			spin_lock_irqsave(instance->host->host_lock, flags);
			status = cmd->frame->hdr.cmd_status;
			instance->map_update_cmd = NULL;
			if (status != MFI_STAT_OK) {
				if (status != MFI_STAT_NOT_FOUND)
					dev_warn(&instance->pdev->dev, "map syncfailed, status = 0x%x\n",
					       cmd->frame->hdr.cmd_status);
				else {
					megasas_return_cmd(instance, cmd);
					spin_unlock_irqrestore(
						instance->host->host_lock,
						flags);
					break;
				}
			}

			megasas_return_cmd(instance, cmd);

			/*
			 * Set fast path IO to ZERO.
			 * Validate Map will set proper value.
			 * Meanwhile all IOs will go as LD IO.
			 */
			if (status == MFI_STAT_OK &&
			    (MR_ValidateMapInfo(instance, (instance->map_id + 1)))) {
				instance->map_id++;
				fusion->fast_path_io = 1;
			} else {
				fusion->fast_path_io = 0;
			}

			if (instance->adapter_type >= INVADER_SERIES)
				megasas_set_ld_removed_by_fw(instance);

			megasas_sync_map_info(instance);
			spin_unlock_irqrestore(instance->host->host_lock,
					       flags);

			break;
		}
		if (opcode == MR_DCMD_CTRL_EVENT_GET_INFO ||
		    opcode == MR_DCMD_CTRL_EVENT_GET) {
			spin_lock_irqsave(&poll_aen_lock, flags);
			megasas_poll_wait_aen = 0;
			spin_unlock_irqrestore(&poll_aen_lock, flags);
		}

		/* FW has an updated PD sequence */
		if ((opcode == MR_DCMD_SYSTEM_PD_MAP_GET_INFO) &&
			(cmd->frame->dcmd.mbox.b[0] == 1)) {

			spin_lock_irqsave(instance->host->host_lock, flags);
			status = cmd->frame->hdr.cmd_status;
			instance->jbod_seq_cmd = NULL;
			megasas_return_cmd(instance, cmd);

			if (status == MFI_STAT_OK) {
				instance->pd_seq_map_id++;
				/* Re-register a pd sync seq num cmd */
				if (megasas_sync_pd_seq_num(instance, true))
					instance->use_seqnum_jbod_fp = false;
			} else
				instance->use_seqnum_jbod_fp = false;

			spin_unlock_irqrestore(instance->host->host_lock, flags);
			break;
		}

		/*
		 * See if got an event notification
		 */
		if (opcode == MR_DCMD_CTRL_EVENT_WAIT)
			megasas_service_aen(instance, cmd);
		else
			megasas_complete_int_cmd(instance, cmd);

		break;

	case MFI_CMD_ABORT:
		/*
		 * Cmd issued to abort another cmd returned
		 */
		megasas_complete_abort(instance, cmd);
		break;

	default:
		dev_info(&instance->pdev->dev, "Unknown command completed! [0x%X]\n",
		       hdr->cmd);
		megasas_complete_int_cmd(instance, cmd);
		break;
	}
}

/**
 * megasas_issue_pending_cmds_again -	issue all pending cmds
 *					in FW again because of the fw reset
 * @instance:				Adapter soft state
 */
static inline void
megasas_issue_pending_cmds_again(struct megasas_instance *instance)
{
	struct megasas_cmd *cmd;
	struct list_head clist_local;
	union megasas_evt_class_locale class_locale;
	unsigned long flags;
	u32 seq_num;

	INIT_LIST_HEAD(&clist_local);
	spin_lock_irqsave(&instance->hba_lock, flags);
	list_splice_init(&instance->internal_reset_pending_q, &clist_local);
	spin_unlock_irqrestore(&instance->hba_lock, flags);

	while (!list_empty(&clist_local)) {
		cmd = list_entry((&clist_local)->next,
					struct megasas_cmd, list);
		list_del_init(&cmd->list);

		if (cmd->sync_cmd || cmd->scmd) {
			dev_notice(&instance->pdev->dev, "command %p, %p:%d"
				"detected to be pending while HBA reset\n",
					cmd, cmd->scmd, cmd->sync_cmd);

			cmd->retry_for_fw_reset++;

			if (cmd->retry_for_fw_reset == 3) {
				dev_notice(&instance->pdev->dev, "cmd %p, %p:%d"
					"was tried multiple times during reset."
					"Shutting down the HBA\n",
					cmd, cmd->scmd, cmd->sync_cmd);
				instance->instancet->disable_intr(instance);
				atomic_set(&instance->fw_reset_no_pci_access, 1);
				megaraid_sas_kill_hba(instance);
				return;
			}
		}

		if (cmd->sync_cmd == 1) {
			if (cmd->scmd) {
				dev_notice(&instance->pdev->dev, "unexpected"
					"cmd attached to internal command!\n");
			}
			dev_notice(&instance->pdev->dev, "%p synchronous cmd"
						"on the internal reset queue,"
						"issue it again.\n", cmd);
			cmd->cmd_status_drv = DCMD_INIT;
			instance->instancet->fire_cmd(instance,
							cmd->frame_phys_addr,
							0, instance->reg_set);
		} else if (cmd->scmd) {
			dev_notice(&instance->pdev->dev, "%p scsi cmd [%02x]"
			"detected on the internal queue, issue again.\n",
			cmd, cmd->scmd->cmnd[0]);

			atomic_inc(&instance->fw_outstanding);
			instance->instancet->fire_cmd(instance,
					cmd->frame_phys_addr,
					cmd->frame_count-1, instance->reg_set);
		} else {
			dev_notice(&instance->pdev->dev, "%p unexpected cmd on the"
				"internal reset defer list while re-issue!!\n",
				cmd);
		}
	}

	if (instance->aen_cmd) {
		dev_notice(&instance->pdev->dev, "aen_cmd in def process\n");
		megasas_return_cmd(instance, instance->aen_cmd);

		instance->aen_cmd = NULL;
	}

	/*
	 * Initiate AEN (Asynchronous Event Notification)
	 */
	seq_num = instance->last_seq_num;
	class_locale.members.reserved = 0;
	class_locale.members.locale = MR_EVT_LOCALE_ALL;
	class_locale.members.class = MR_EVT_CLASS_DEBUG;

	megasas_register_aen(instance, seq_num, class_locale.word);
}

/*
 * Move the internal reset pending commands to a deferred queue.
 *
 * We move the commands pending at internal reset time to a
 * pending queue. This queue would be flushed after successful
 * completion of the internal reset sequence. if the internal reset
 * did not complete in time, the kernel reset handler would flush
 * these commands.
 */
static void
megasas_internal_reset_defer_cmds(struct megasas_instance *instance)
{
	struct megasas_cmd *cmd;
	int i;
	u16 max_cmd = instance->max_fw_cmds;
	u32 defer_index;
	unsigned long flags;

	defer_index = 0;
	spin_lock_irqsave(&instance->mfi_pool_lock, flags);
	for (i = 0; i < max_cmd; i++) {
		cmd = instance->cmd_list[i];
		if (cmd->sync_cmd == 1 || cmd->scmd) {
			dev_notice(&instance->pdev->dev, "moving cmd[%d]:%p:%d:%p"
					"on the defer queue as internal\n",
				defer_index, cmd, cmd->sync_cmd, cmd->scmd);

			if (!list_empty(&cmd->list)) {
				dev_notice(&instance->pdev->dev, "ERROR while"
					" moving this cmd:%p, %d %p, it was"
					"discovered on some list?\n",
					cmd, cmd->sync_cmd, cmd->scmd);

				list_del_init(&cmd->list);
			}
			defer_index++;
			list_add_tail(&cmd->list,
				&instance->internal_reset_pending_q);
		}
	}
	spin_unlock_irqrestore(&instance->mfi_pool_lock, flags);
}


static void
process_fw_state_change_wq(struct work_struct *work)
{
	struct megasas_instance *instance =
		container_of(work, struct megasas_instance, work_init);
	u32 wait;
	unsigned long flags;

	if (atomic_read(&instance->adprecovery) != MEGASAS_ADPRESET_SM_INFAULT) {
		dev_notice(&instance->pdev->dev, "error, recovery st %x\n",
			   atomic_read(&instance->adprecovery));
		return ;
	}

	if (atomic_read(&instance->adprecovery) == MEGASAS_ADPRESET_SM_INFAULT) {
		dev_notice(&instance->pdev->dev, "FW detected to be in fault"
					"state, restarting it...\n");

		instance->instancet->disable_intr(instance);
		atomic_set(&instance->fw_outstanding, 0);

		atomic_set(&instance->fw_reset_no_pci_access, 1);
		instance->instancet->adp_reset(instance, instance->reg_set);
		atomic_set(&instance->fw_reset_no_pci_access, 0);

		dev_notice(&instance->pdev->dev, "FW restarted successfully,"
					"initiating next stage...\n");

		dev_notice(&instance->pdev->dev, "HBA recovery state machine,"
					"state 2 starting...\n");

		/* waiting for about 20 second before start the second init */
		for (wait = 0; wait < 30; wait++) {
			msleep(1000);
		}

		if (megasas_transition_to_ready(instance, 1)) {
			dev_notice(&instance->pdev->dev, "adapter not ready\n");

			atomic_set(&instance->fw_reset_no_pci_access, 1);
			megaraid_sas_kill_hba(instance);
			return ;
		}

		if ((instance->pdev->device == PCI_DEVICE_ID_LSI_SAS1064R) ||
			(instance->pdev->device == PCI_DEVICE_ID_DELL_PERC5) ||
			(instance->pdev->device == PCI_DEVICE_ID_LSI_VERDE_ZCR)
			) {
			*instance->consumer = *instance->producer;
		} else {
			*instance->consumer = 0;
			*instance->producer = 0;
		}

		megasas_issue_init_mfi(instance);

		spin_lock_irqsave(&instance->hba_lock, flags);
		atomic_set(&instance->adprecovery, MEGASAS_HBA_OPERATIONAL);
		spin_unlock_irqrestore(&instance->hba_lock, flags);
		instance->instancet->enable_intr(instance);

		megasas_issue_pending_cmds_again(instance);
		instance->issuepend_done = 1;
	}
}

/**
 * megasas_deplete_reply_queue -	Processes all completed commands
 * @instance:				Adapter soft state
 * @alt_status:				Alternate status to be returned to
 *					SCSI mid-layer instead of the status
 *					returned by the FW
 * Note: this must be called with hba lock held
 */
static int
megasas_deplete_reply_queue(struct megasas_instance *instance,
					u8 alt_status)
{
	u32 mfiStatus;
	u32 fw_state;

	if (instance->instancet->check_reset(instance, instance->reg_set) == 1)
		return IRQ_HANDLED;

	mfiStatus = instance->instancet->clear_intr(instance);
	if (mfiStatus == 0) {
		/* Hardware may not set outbound_intr_status in MSI-X mode */
		if (!instance->msix_vectors)
			return IRQ_NONE;
	}

	instance->mfiStatus = mfiStatus;

	if ((mfiStatus & MFI_INTR_FLAG_FIRMWARE_STATE_CHANGE)) {
		fw_state = instance->instancet->read_fw_status_reg(
				instance) & MFI_STATE_MASK;

		if (fw_state != MFI_STATE_FAULT) {
			dev_notice(&instance->pdev->dev, "fw state:%x\n",
						fw_state);
		}

		if ((fw_state == MFI_STATE_FAULT) &&
				(instance->disableOnlineCtrlReset == 0)) {
			dev_notice(&instance->pdev->dev, "wait adp restart\n");

			if ((instance->pdev->device ==
					PCI_DEVICE_ID_LSI_SAS1064R) ||
				(instance->pdev->device ==
					PCI_DEVICE_ID_DELL_PERC5) ||
				(instance->pdev->device ==
					PCI_DEVICE_ID_LSI_VERDE_ZCR)) {

				*instance->consumer =
					cpu_to_le32(MEGASAS_ADPRESET_INPROG_SIGN);
			}


			instance->instancet->disable_intr(instance);
			atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_INFAULT);
			instance->issuepend_done = 0;

			atomic_set(&instance->fw_outstanding, 0);
			megasas_internal_reset_defer_cmds(instance);

			dev_notice(&instance->pdev->dev, "fwState=%x, stage:%d\n",
					fw_state, atomic_read(&instance->adprecovery));

			schedule_work(&instance->work_init);
			return IRQ_HANDLED;

		} else {
			dev_notice(&instance->pdev->dev, "fwstate:%x, dis_OCR=%x\n",
				fw_state, instance->disableOnlineCtrlReset);
		}
	}

	tasklet_schedule(&instance->isr_tasklet);
	return IRQ_HANDLED;
}

/**
 * megasas_isr - isr entry point
 * @irq:	IRQ number
 * @devp:	IRQ context address
 */
static irqreturn_t megasas_isr(int irq, void *devp)
{
	struct megasas_irq_context *irq_context = devp;
	struct megasas_instance *instance = irq_context->instance;
	unsigned long flags;
	irqreturn_t rc;

	if (atomic_read(&instance->fw_reset_no_pci_access))
		return IRQ_HANDLED;

	spin_lock_irqsave(&instance->hba_lock, flags);
	rc = megasas_deplete_reply_queue(instance, DID_OK);
	spin_unlock_irqrestore(&instance->hba_lock, flags);

	return rc;
}

/**
 * megasas_transition_to_ready -	Move the FW to READY state
 * @instance:				Adapter soft state
 * @ocr:				Adapter reset state
 *
 * During the initialization, FW passes can potentially be in any one of
 * several possible states. If the FW in operational, waiting-for-handshake
 * states, driver must take steps to bring it to ready state. Otherwise, it
 * has to wait for the ready state.
 */
int
megasas_transition_to_ready(struct megasas_instance *instance, int ocr)
{
	int i;
	u8 max_wait;
	u32 fw_state;
	u32 abs_state, curr_abs_state;

	abs_state = instance->instancet->read_fw_status_reg(instance);
	fw_state = abs_state & MFI_STATE_MASK;

	if (fw_state != MFI_STATE_READY)
		dev_info(&instance->pdev->dev, "Waiting for FW to come to ready"
		       " state\n");

	while (fw_state != MFI_STATE_READY) {

		switch (fw_state) {

		case MFI_STATE_FAULT:
			dev_printk(KERN_ERR, &instance->pdev->dev,
				   "FW in FAULT state, Fault code:0x%x subcode:0x%x func:%s\n",
				   abs_state & MFI_STATE_FAULT_CODE,
				   abs_state & MFI_STATE_FAULT_SUBCODE, __func__);
			if (ocr) {
				max_wait = MEGASAS_RESET_WAIT_TIME;
				break;
			} else {
				dev_printk(KERN_DEBUG, &instance->pdev->dev, "System Register set:\n");
				megasas_dump_reg_set(instance->reg_set);
				return -ENODEV;
			}

		case MFI_STATE_WAIT_HANDSHAKE:
			/*
			 * Set the CLR bit in inbound doorbell
			 */
			if ((instance->pdev->device ==
				PCI_DEVICE_ID_LSI_SAS0073SKINNY) ||
				(instance->pdev->device ==
				 PCI_DEVICE_ID_LSI_SAS0071SKINNY) ||
				(instance->adapter_type != MFI_SERIES))
				writel(
				  MFI_INIT_CLEAR_HANDSHAKE|MFI_INIT_HOTPLUG,
				  &instance->reg_set->doorbell);
			else
				writel(
				    MFI_INIT_CLEAR_HANDSHAKE|MFI_INIT_HOTPLUG,
					&instance->reg_set->inbound_doorbell);

			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_BOOT_MESSAGE_PENDING:
			if ((instance->pdev->device ==
			     PCI_DEVICE_ID_LSI_SAS0073SKINNY) ||
				(instance->pdev->device ==
				 PCI_DEVICE_ID_LSI_SAS0071SKINNY) ||
				(instance->adapter_type != MFI_SERIES))
				writel(MFI_INIT_HOTPLUG,
				       &instance->reg_set->doorbell);
			else
				writel(MFI_INIT_HOTPLUG,
					&instance->reg_set->inbound_doorbell);

			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_OPERATIONAL:
			/*
			 * Bring it to READY state; assuming max wait 10 secs
			 */
			instance->instancet->disable_intr(instance);
			if ((instance->pdev->device ==
				PCI_DEVICE_ID_LSI_SAS0073SKINNY) ||
				(instance->pdev->device ==
				PCI_DEVICE_ID_LSI_SAS0071SKINNY)  ||
				(instance->adapter_type != MFI_SERIES)) {
				writel(MFI_RESET_FLAGS,
					&instance->reg_set->doorbell);

				if (instance->adapter_type != MFI_SERIES) {
					for (i = 0; i < (10 * 1000); i += 20) {
						if (megasas_readl(
							    instance,
							    &instance->
							    reg_set->
							    doorbell) & 1)
							msleep(20);
						else
							break;
					}
				}
			} else
				writel(MFI_RESET_FLAGS,
					&instance->reg_set->inbound_doorbell);

			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_UNDEFINED:
			/*
			 * This state should not last for more than 2 seconds
			 */
			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_BB_INIT:
			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_FW_INIT:
			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_FW_INIT_2:
			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_DEVICE_SCAN:
			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		case MFI_STATE_FLUSH_CACHE:
			max_wait = MEGASAS_RESET_WAIT_TIME;
			break;

		default:
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "Unknown state 0x%x\n",
			       fw_state);
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "System Register set:\n");
			megasas_dump_reg_set(instance->reg_set);
			return -ENODEV;
		}

		/*
		 * The cur_state should not last for more than max_wait secs
		 */
		for (i = 0; i < max_wait * 50; i++) {
			curr_abs_state = instance->instancet->
				read_fw_status_reg(instance);

			if (abs_state == curr_abs_state) {
				msleep(20);
			} else
				break;
		}

		/*
		 * Return error if fw_state hasn't changed after max_wait
		 */
		if (curr_abs_state == abs_state) {
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "FW state [%d] hasn't changed "
			       "in %d secs\n", fw_state, max_wait);
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "System Register set:\n");
			megasas_dump_reg_set(instance->reg_set);
			return -ENODEV;
		}

		abs_state = curr_abs_state;
		fw_state = curr_abs_state & MFI_STATE_MASK;
	}
	dev_info(&instance->pdev->dev, "FW now in Ready state\n");

	return 0;
}

/**
 * megasas_teardown_frame_pool -	Destroy the cmd frame DMA pool
 * @instance:				Adapter soft state
 */
static void megasas_teardown_frame_pool(struct megasas_instance *instance)
{
	int i;
	u16 max_cmd = instance->max_mfi_cmds;
	struct megasas_cmd *cmd;

	if (!instance->frame_dma_pool)
		return;

	/*
	 * Return all frames to pool
	 */
	for (i = 0; i < max_cmd; i++) {

		cmd = instance->cmd_list[i];

		if (cmd->frame)
			dma_pool_free(instance->frame_dma_pool, cmd->frame,
				      cmd->frame_phys_addr);

		if (cmd->sense)
			dma_pool_free(instance->sense_dma_pool, cmd->sense,
				      cmd->sense_phys_addr);
	}

	/*
	 * Now destroy the pool itself
	 */
	dma_pool_destroy(instance->frame_dma_pool);
	dma_pool_destroy(instance->sense_dma_pool);

	instance->frame_dma_pool = NULL;
	instance->sense_dma_pool = NULL;
}

/**
 * megasas_create_frame_pool -	Creates DMA pool for cmd frames
 * @instance:			Adapter soft state
 *
 * Each command packet has an embedded DMA memory buffer that is used for
 * filling MFI frame and the SG list that immediately follows the frame. This
 * function creates those DMA memory buffers for each command packet by using
 * PCI pool facility.
 */
static int megasas_create_frame_pool(struct megasas_instance *instance)
{
	int i;
	u16 max_cmd;
	u32 frame_count;
	struct megasas_cmd *cmd;

	max_cmd = instance->max_mfi_cmds;

	/*
	 * For MFI controllers.
	 * max_num_sge = 60
	 * max_sge_sz  = 16 byte (sizeof megasas_sge_skinny)
	 * Total 960 byte (15 MFI frame of 64 byte)
	 *
	 * Fusion adapter require only 3 extra frame.
	 * max_num_sge = 16 (defined as MAX_IOCTL_SGE)
	 * max_sge_sz  = 12 byte (sizeof  megasas_sge64)
	 * Total 192 byte (3 MFI frame of 64 byte)
	 */
	frame_count = (instance->adapter_type == MFI_SERIES) ?
			(15 + 1) : (3 + 1);
	instance->mfi_frame_size = MEGAMFI_FRAME_SIZE * frame_count;
	/*
	 * Use DMA pool facility provided by PCI layer
	 */
	instance->frame_dma_pool = dma_pool_create("megasas frame pool",
					&instance->pdev->dev,
					instance->mfi_frame_size, 256, 0);

	if (!instance->frame_dma_pool) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "failed to setup frame pool\n");
		return -ENOMEM;
	}

	instance->sense_dma_pool = dma_pool_create("megasas sense pool",
						   &instance->pdev->dev, 128,
						   4, 0);

	if (!instance->sense_dma_pool) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "failed to setup sense pool\n");

		dma_pool_destroy(instance->frame_dma_pool);
		instance->frame_dma_pool = NULL;

		return -ENOMEM;
	}

	/*
	 * Allocate and attach a frame to each of the commands in cmd_list.
	 * By making cmd->index as the context instead of the &cmd, we can
	 * always use 32bit context regardless of the architecture
	 */
	for (i = 0; i < max_cmd; i++) {

		cmd = instance->cmd_list[i];

		cmd->frame = dma_pool_zalloc(instance->frame_dma_pool,
					    GFP_KERNEL, &cmd->frame_phys_addr);

		cmd->sense = dma_pool_alloc(instance->sense_dma_pool,
					    GFP_KERNEL, &cmd->sense_phys_addr);

		/*
		 * megasas_teardown_frame_pool() takes care of freeing
		 * whatever has been allocated
		 */
		if (!cmd->frame || !cmd->sense) {
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "dma_pool_alloc failed\n");
			megasas_teardown_frame_pool(instance);
			return -ENOMEM;
		}

		cmd->frame->io.context = cpu_to_le32(cmd->index);
		cmd->frame->io.pad_0 = 0;
		if ((instance->adapter_type == MFI_SERIES) && reset_devices)
			cmd->frame->hdr.cmd = MFI_CMD_INVALID;
	}

	return 0;
}

/**
 * megasas_free_cmds -	Free all the cmds in the free cmd pool
 * @instance:		Adapter soft state
 */
void megasas_free_cmds(struct megasas_instance *instance)
{
	int i;

	/* First free the MFI frame pool */
	megasas_teardown_frame_pool(instance);

	/* Free all the commands in the cmd_list */
	for (i = 0; i < instance->max_mfi_cmds; i++)

		kfree(instance->cmd_list[i]);

	/* Free the cmd_list buffer itself */
	kfree(instance->cmd_list);
	instance->cmd_list = NULL;

	INIT_LIST_HEAD(&instance->cmd_pool);
}

/**
 * megasas_alloc_cmds -	Allocates the command packets
 * @instance:		Adapter soft state
 *
 * Each command that is issued to the FW, whether IO commands from the OS or
 * internal commands like IOCTLs, are wrapped in local data structure called
 * megasas_cmd. The frame embedded in this megasas_cmd is actually issued to
 * the FW.
 *
 * Each frame has a 32-bit field called context (tag). This context is used
 * to get back the megasas_cmd from the frame when a frame gets completed in
 * the ISR. Typically the address of the megasas_cmd itself would be used as
 * the context. But we wanted to keep the differences between 32 and 64 bit
 * systems to the mininum. We always use 32 bit integers for the context. In
 * this driver, the 32 bit values are the indices into an array cmd_list.
 * This array is used only to look up the megasas_cmd given the context. The
 * free commands themselves are maintained in a linked list called cmd_pool.
 */
int megasas_alloc_cmds(struct megasas_instance *instance)
{
	int i;
	int j;
	u16 max_cmd;
	struct megasas_cmd *cmd;

	max_cmd = instance->max_mfi_cmds;

	/*
	 * instance->cmd_list is an array of struct megasas_cmd pointers.
	 * Allocate the dynamic array first and then allocate individual
	 * commands.
	 */
	instance->cmd_list = kcalloc(max_cmd, sizeof(struct megasas_cmd*), GFP_KERNEL);

	if (!instance->cmd_list) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < max_cmd; i++) {
		instance->cmd_list[i] = kmalloc(sizeof(struct megasas_cmd),
						GFP_KERNEL);

		if (!instance->cmd_list[i]) {

			for (j = 0; j < i; j++)
				kfree(instance->cmd_list[j]);

			kfree(instance->cmd_list);
			instance->cmd_list = NULL;

			return -ENOMEM;
		}
	}

	for (i = 0; i < max_cmd; i++) {
		cmd = instance->cmd_list[i];
		memset(cmd, 0, sizeof(struct megasas_cmd));
		cmd->index = i;
		cmd->scmd = NULL;
		cmd->instance = instance;

		list_add_tail(&cmd->list, &instance->cmd_pool);
	}

	/*
	 * Create a frame pool and assign one frame to each cmd
	 */
	if (megasas_create_frame_pool(instance)) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Error creating frame DMA pool\n");
		megasas_free_cmds(instance);
		return -ENOMEM;
	}

	return 0;
}

/*
 * dcmd_timeout_ocr_possible -	Check if OCR is possible based on Driver/FW state.
 * @instance:				Adapter soft state
 *
 * Return 0 for only Fusion adapter, if driver load/unload is not in progress
 * or FW is not under OCR.
 */
inline int
dcmd_timeout_ocr_possible(struct megasas_instance *instance) {

	if (instance->adapter_type == MFI_SERIES)
		return KILL_ADAPTER;
	else if (instance->unload ||
			test_bit(MEGASAS_FUSION_OCR_NOT_POSSIBLE,
				 &instance->reset_flags))
		return IGNORE_TIMEOUT;
	else
		return INITIATE_OCR;
}

static void
megasas_get_pd_info(struct megasas_instance *instance, struct scsi_device *sdev)
{
	int ret;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;

	struct MR_PRIV_DEVICE *mr_device_priv_data;
	u16 device_id = 0;

	device_id = (sdev->channel * MEGASAS_MAX_DEV_PER_CHANNEL) + sdev->id;
	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_err(&instance->pdev->dev, "Failed to get cmd %s\n", __func__);
		return;
	}

	dcmd = &cmd->frame->dcmd;

	memset(instance->pd_info, 0, sizeof(*instance->pd_info));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->mbox.s[0] = cpu_to_le16(device_id);
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct MR_PD_INFO));
	dcmd->opcode = cpu_to_le32(MR_DCMD_PD_GET_INFO);

	megasas_set_dma_settings(instance, dcmd, instance->pd_info_h,
				 sizeof(struct MR_PD_INFO));

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd, MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	switch (ret) {
	case DCMD_SUCCESS:
		mr_device_priv_data = sdev->hostdata;
		le16_to_cpus((u16 *)&instance->pd_info->state.ddf.pdType);
		mr_device_priv_data->interface_type =
				instance->pd_info->state.ddf.pdType.intf;
		break;

	case DCMD_TIMEOUT:

		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
				MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d\n",
				__func__, __LINE__);
			break;
		}

		break;
	}

	if (ret != DCMD_TIMEOUT)
		megasas_return_cmd(instance, cmd);

	return;
}
/*
 * megasas_get_pd_list_info -	Returns FW's pd_list structure
 * @instance:				Adapter soft state
 * @pd_list:				pd_list structure
 *
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 */
static int
megasas_get_pd_list(struct megasas_instance *instance)
{
	int ret = 0, pd_index = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_PD_LIST *ci;
	struct MR_PD_ADDRESS *pd_addr;

	if (instance->pd_list_not_supported) {
		dev_info(&instance->pdev->dev, "MR_DCMD_PD_LIST_QUERY "
		"not supported by firmware\n");
		return ret;
	}

	ci = instance->pd_list_buf;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "(get_pd_list): Failed to get cmd\n");
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	memset(ci, 0, sizeof(*ci));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->mbox.b[0] = MR_PD_QUERY_TYPE_EXPOSED_TO_HOST;
	dcmd->mbox.b[1] = 0;
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(MEGASAS_MAX_PD * sizeof(struct MR_PD_LIST));
	dcmd->opcode = cpu_to_le32(MR_DCMD_PD_LIST_QUERY);

	megasas_set_dma_settings(instance, dcmd, instance->pd_list_buf_h,
				 (MEGASAS_MAX_PD * sizeof(struct MR_PD_LIST)));

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd,
			MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	switch (ret) {
	case DCMD_FAILED:
		dev_info(&instance->pdev->dev, "MR_DCMD_PD_LIST_QUERY "
			"failed/not supported by firmware\n");

		if (instance->adapter_type != MFI_SERIES)
			megaraid_sas_kill_hba(instance);
		else
			instance->pd_list_not_supported = 1;
		break;
	case DCMD_TIMEOUT:

		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			/*
			 * DCMD failed from AEN path.
			 * AEN path already hold reset_mutex to avoid PCI access
			 * while OCR is in progress.
			 */
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
						MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d \n",
				__func__, __LINE__);
			break;
		}

		break;

	case DCMD_SUCCESS:
		pd_addr = ci->addr;
		if (megasas_dbg_lvl & LD_PD_DEBUG)
			dev_info(&instance->pdev->dev, "%s, sysPD count: 0x%x\n",
				 __func__, le32_to_cpu(ci->count));

		if ((le32_to_cpu(ci->count) >
			(MEGASAS_MAX_PD_CHANNELS * MEGASAS_MAX_DEV_PER_CHANNEL)))
			break;

		memset(instance->local_pd_list, 0,
				MEGASAS_MAX_PD * sizeof(struct megasas_pd_list));

		for (pd_index = 0; pd_index < le32_to_cpu(ci->count); pd_index++) {
			instance->local_pd_list[le16_to_cpu(pd_addr->deviceId)].tid	=
					le16_to_cpu(pd_addr->deviceId);
			instance->local_pd_list[le16_to_cpu(pd_addr->deviceId)].driveType	=
					pd_addr->scsiDevType;
			instance->local_pd_list[le16_to_cpu(pd_addr->deviceId)].driveState	=
					MR_PD_STATE_SYSTEM;
			if (megasas_dbg_lvl & LD_PD_DEBUG)
				dev_info(&instance->pdev->dev,
					 "PD%d: targetID: 0x%03x deviceType:0x%x\n",
					 pd_index, le16_to_cpu(pd_addr->deviceId),
					 pd_addr->scsiDevType);
			pd_addr++;
		}

		memcpy(instance->pd_list, instance->local_pd_list,
			sizeof(instance->pd_list));
		break;

	}

	if (ret != DCMD_TIMEOUT)
		megasas_return_cmd(instance, cmd);

	return ret;
}

/*
 * megasas_get_ld_list_info -	Returns FW's ld_list structure
 * @instance:				Adapter soft state
 * @ld_list:				ld_list structure
 *
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 */
static int
megasas_get_ld_list(struct megasas_instance *instance)
{
	int ret = 0, ld_index = 0, ids = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_LD_LIST *ci;
	dma_addr_t ci_h = 0;
	u32 ld_count;

	ci = instance->ld_list_buf;
	ci_h = instance->ld_list_buf_h;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "megasas_get_ld_list: Failed to get cmd\n");
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	memset(ci, 0, sizeof(*ci));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	if (instance->supportmax256vd)
		dcmd->mbox.b[0] = 1;
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct MR_LD_LIST));
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_GET_LIST);
	dcmd->pad_0  = 0;

	megasas_set_dma_settings(instance, dcmd, ci_h,
				 sizeof(struct MR_LD_LIST));

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd,
			MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	ld_count = le32_to_cpu(ci->ldCount);

	switch (ret) {
	case DCMD_FAILED:
		megaraid_sas_kill_hba(instance);
		break;
	case DCMD_TIMEOUT:

		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			/*
			 * DCMD failed from AEN path.
			 * AEN path already hold reset_mutex to avoid PCI access
			 * while OCR is in progress.
			 */
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
						MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d\n",
				__func__, __LINE__);
			break;
		}

		break;

	case DCMD_SUCCESS:
		if (megasas_dbg_lvl & LD_PD_DEBUG)
			dev_info(&instance->pdev->dev, "%s, LD count: 0x%x\n",
				 __func__, ld_count);

		if (ld_count > instance->fw_supported_vd_count)
			break;

		memset(instance->ld_ids, 0xff, MAX_LOGICAL_DRIVES_EXT);

		for (ld_index = 0; ld_index < ld_count; ld_index++) {
			if (ci->ldList[ld_index].state != 0) {
				ids = ci->ldList[ld_index].ref.targetId;
				instance->ld_ids[ids] = ci->ldList[ld_index].ref.targetId;
				if (megasas_dbg_lvl & LD_PD_DEBUG)
					dev_info(&instance->pdev->dev,
						 "LD%d: targetID: 0x%03x\n",
						 ld_index, ids);
			}
		}

		break;
	}

	if (ret != DCMD_TIMEOUT)
		megasas_return_cmd(instance, cmd);

	return ret;
}

/**
 * megasas_ld_list_query -	Returns FW's ld_list structure
 * @instance:				Adapter soft state
 * @query_type:				ld_list structure type
 *
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 */
static int
megasas_ld_list_query(struct megasas_instance *instance, u8 query_type)
{
	int ret = 0, ld_index = 0, ids = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_LD_TARGETID_LIST *ci;
	dma_addr_t ci_h = 0;
	u32 tgtid_count;

	ci = instance->ld_targetid_list_buf;
	ci_h = instance->ld_targetid_list_buf_h;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_warn(&instance->pdev->dev,
		         "megasas_ld_list_query: Failed to get cmd\n");
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	memset(ci, 0, sizeof(*ci));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->mbox.b[0] = query_type;
	if (instance->supportmax256vd)
		dcmd->mbox.b[2] = 1;

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct MR_LD_TARGETID_LIST));
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_LIST_QUERY);
	dcmd->pad_0  = 0;

	megasas_set_dma_settings(instance, dcmd, ci_h,
				 sizeof(struct MR_LD_TARGETID_LIST));

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd, MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	switch (ret) {
	case DCMD_FAILED:
		dev_info(&instance->pdev->dev,
			"DCMD not supported by firmware - %s %d\n",
				__func__, __LINE__);
		ret = megasas_get_ld_list(instance);
		break;
	case DCMD_TIMEOUT:
		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			/*
			 * DCMD failed from AEN path.
			 * AEN path already hold reset_mutex to avoid PCI access
			 * while OCR is in progress.
			 */
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
						MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d\n",
				__func__, __LINE__);
			break;
		}

		break;
	case DCMD_SUCCESS:
		tgtid_count = le32_to_cpu(ci->count);

		if (megasas_dbg_lvl & LD_PD_DEBUG)
			dev_info(&instance->pdev->dev, "%s, LD count: 0x%x\n",
				 __func__, tgtid_count);

		if ((tgtid_count > (instance->fw_supported_vd_count)))
			break;

		memset(instance->ld_ids, 0xff, MEGASAS_MAX_LD_IDS);
		for (ld_index = 0; ld_index < tgtid_count; ld_index++) {
			ids = ci->targetId[ld_index];
			instance->ld_ids[ids] = ci->targetId[ld_index];
			if (megasas_dbg_lvl & LD_PD_DEBUG)
				dev_info(&instance->pdev->dev, "LD%d: targetID: 0x%03x\n",
					 ld_index, ci->targetId[ld_index]);
		}

		break;
	}

	if (ret != DCMD_TIMEOUT)
		megasas_return_cmd(instance, cmd);

	return ret;
}

/**
 * megasas_host_device_list_query
 * dcmd.opcode            - MR_DCMD_CTRL_DEVICE_LIST_GET
 * dcmd.mbox              - reserved
 * dcmd.sge IN            - ptr to return MR_HOST_DEVICE_LIST structure
 * Desc:    This DCMD will return the combined device list
 * Status:  MFI_STAT_OK - List returned successfully
 *          MFI_STAT_INVALID_CMD - Firmware support for the feature has been
 *                                 disabled
 * @instance:			Adapter soft state
 * @is_probe:			Driver probe check
 * Return:			0 if DCMD succeeded
 *				 non-zero if failed
 */
static int
megasas_host_device_list_query(struct megasas_instance *instance,
			       bool is_probe)
{
	int ret, i, target_id;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_HOST_DEVICE_LIST *ci;
	u32 count;
	dma_addr_t ci_h;

	ci = instance->host_device_list_buf;
	ci_h = instance->host_device_list_buf_h;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_warn(&instance->pdev->dev,
			 "%s: failed to get cmd\n",
			 __func__);
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	memset(ci, 0, sizeof(*ci));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->mbox.b[0] = is_probe ? 0 : 1;
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(HOST_DEVICE_LIST_SZ);
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_DEVICE_LIST_GET);

	megasas_set_dma_settings(instance, dcmd, ci_h, HOST_DEVICE_LIST_SZ);

	if (!instance->mask_interrupts) {
		ret = megasas_issue_blocked_cmd(instance, cmd,
						MFI_IO_TIMEOUT_SECS);
	} else {
		ret = megasas_issue_polled(instance, cmd);
		cmd->flags |= DRV_DCMD_SKIP_REFIRE;
	}

	switch (ret) {
	case DCMD_SUCCESS:
		/* Fill the internal pd_list and ld_ids array based on
		 * targetIds returned by FW
		 */
		count = le32_to_cpu(ci->count);

		if (count > (MEGASAS_MAX_PD + MAX_LOGICAL_DRIVES_EXT))
			break;

		if (megasas_dbg_lvl & LD_PD_DEBUG)
			dev_info(&instance->pdev->dev, "%s, Device count: 0x%x\n",
				 __func__, count);

		memset(instance->local_pd_list, 0,
		       MEGASAS_MAX_PD * sizeof(struct megasas_pd_list));
		memset(instance->ld_ids, 0xff, MAX_LOGICAL_DRIVES_EXT);
		for (i = 0; i < count; i++) {
			target_id = le16_to_cpu(ci->host_device_list[i].target_id);
			if (ci->host_device_list[i].flags.u.bits.is_sys_pd) {
				instance->local_pd_list[target_id].tid = target_id;
				instance->local_pd_list[target_id].driveType =
						ci->host_device_list[i].scsi_type;
				instance->local_pd_list[target_id].driveState =
						MR_PD_STATE_SYSTEM;
				if (megasas_dbg_lvl & LD_PD_DEBUG)
					dev_info(&instance->pdev->dev,
						 "Device %d: PD targetID: 0x%03x deviceType:0x%x\n",
						 i, target_id, ci->host_device_list[i].scsi_type);
			} else {
				instance->ld_ids[target_id] = target_id;
				if (megasas_dbg_lvl & LD_PD_DEBUG)
					dev_info(&instance->pdev->dev,
						 "Device %d: LD targetID: 0x%03x\n",
						 i, target_id);
			}
		}

		memcpy(instance->pd_list, instance->local_pd_list,
		       sizeof(instance->pd_list));
		break;

	case DCMD_TIMEOUT:
		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
				MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d\n",
				 __func__, __LINE__);
			break;
		}
		break;
	case DCMD_FAILED:
		dev_err(&instance->pdev->dev,
			"%s: MR_DCMD_CTRL_DEVICE_LIST_GET failed\n",
			__func__);
		break;
	}

	if (ret != DCMD_TIMEOUT)
		megasas_return_cmd(instance, cmd);

	return ret;
}

/*
 * megasas_update_ext_vd_details : Update details w.r.t Extended VD
 * instance			 : Controller's instance
*/
static void megasas_update_ext_vd_details(struct megasas_instance *instance)
{
	struct fusion_context *fusion;
	u32 ventura_map_sz = 0;

	fusion = instance->ctrl_context;
	/* For MFI based controllers return dummy success */
	if (!fusion)
		return;

	instance->supportmax256vd =
		instance->ctrl_info_buf->adapterOperations3.supportMaxExtLDs;
	/* Below is additional check to address future FW enhancement */
	if (instance->ctrl_info_buf->max_lds > 64)
		instance->supportmax256vd = 1;

	instance->drv_supported_vd_count = MEGASAS_MAX_LD_CHANNELS
					* MEGASAS_MAX_DEV_PER_CHANNEL;
	instance->drv_supported_pd_count = MEGASAS_MAX_PD_CHANNELS
					* MEGASAS_MAX_DEV_PER_CHANNEL;
	if (instance->supportmax256vd) {
		instance->fw_supported_vd_count = MAX_LOGICAL_DRIVES_EXT;
		instance->fw_supported_pd_count = MAX_PHYSICAL_DEVICES;
	} else {
		instance->fw_supported_vd_count = MAX_LOGICAL_DRIVES;
		instance->fw_supported_pd_count = MAX_PHYSICAL_DEVICES;
	}

	dev_info(&instance->pdev->dev,
		"FW provided supportMaxExtLDs: %d\tmax_lds: %d\n",
		instance->ctrl_info_buf->adapterOperations3.supportMaxExtLDs ? 1 : 0,
		instance->ctrl_info_buf->max_lds);

	if (instance->max_raid_mapsize) {
		ventura_map_sz = instance->max_raid_mapsize *
						MR_MIN_MAP_SIZE; /* 64k */
		fusion->current_map_sz = ventura_map_sz;
		fusion->max_map_sz = ventura_map_sz;
	} else {
		fusion->old_map_sz =
			struct_size_t(struct MR_FW_RAID_MAP, ldSpanMap,
				      instance->fw_supported_vd_count);
		fusion->new_map_sz =  sizeof(struct MR_FW_RAID_MAP_EXT);

		fusion->max_map_sz =
			max(fusion->old_map_sz, fusion->new_map_sz);

		if (instance->supportmax256vd)
			fusion->current_map_sz = fusion->new_map_sz;
		else
			fusion->current_map_sz = fusion->old_map_sz;
	}
	/* irrespective of FW raid maps, driver raid map is constant */
	fusion->drv_map_sz = sizeof(struct MR_DRV_RAID_MAP_ALL);
}

/*
 * dcmd.opcode                - MR_DCMD_CTRL_SNAPDUMP_GET_PROPERTIES
 * dcmd.hdr.length            - number of bytes to read
 * dcmd.sge                   - Ptr to MR_SNAPDUMP_PROPERTIES
 * Desc:			 Fill in snapdump properties
 * Status:			 MFI_STAT_OK- Command successful
 */
void megasas_get_snapdump_properties(struct megasas_instance *instance)
{
	int ret = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_SNAPDUMP_PROPERTIES *ci;
	dma_addr_t ci_h = 0;

	ci = instance->snapdump_prop;
	ci_h = instance->snapdump_prop_h;

	if (!ci)
		return;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_dbg(&instance->pdev->dev, "Failed to get a free cmd\n");
		return;
	}

	dcmd = &cmd->frame->dcmd;

	memset(ci, 0, sizeof(*ci));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct MR_SNAPDUMP_PROPERTIES));
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_SNAPDUMP_GET_PROPERTIES);

	megasas_set_dma_settings(instance, dcmd, ci_h,
				 sizeof(struct MR_SNAPDUMP_PROPERTIES));

	if (!instance->mask_interrupts) {
		ret = megasas_issue_blocked_cmd(instance, cmd,
						MFI_IO_TIMEOUT_SECS);
	} else {
		ret = megasas_issue_polled(instance, cmd);
		cmd->flags |= DRV_DCMD_SKIP_REFIRE;
	}

	switch (ret) {
	case DCMD_SUCCESS:
		instance->snapdump_wait_time =
			min_t(u8, ci->trigger_min_num_sec_before_ocr,
				MEGASAS_MAX_SNAP_DUMP_WAIT_TIME);
		break;

	case DCMD_TIMEOUT:
		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
				MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d\n",
				__func__, __LINE__);
			break;
		}
	}

	if (ret != DCMD_TIMEOUT)
		megasas_return_cmd(instance, cmd);
}

/**
 * megasas_get_ctrl_info -	Returns FW's controller structure
 * @instance:				Adapter soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller structure.
 * This information is mainly used to find out the maximum IO transfer per
 * command supported by the FW.
 */
int
megasas_get_ctrl_info(struct megasas_instance *instance)
{
	int ret = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct megasas_ctrl_info *ci;
	dma_addr_t ci_h = 0;

	ci = instance->ctrl_info_buf;
	ci_h = instance->ctrl_info_buf_h;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to get a free cmd\n");
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	memset(ci, 0, sizeof(*ci));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct megasas_ctrl_info));
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_GET_INFO);
	dcmd->mbox.b[0] = 1;

	megasas_set_dma_settings(instance, dcmd, ci_h,
				 sizeof(struct megasas_ctrl_info));

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts) {
		ret = megasas_issue_blocked_cmd(instance, cmd, MFI_IO_TIMEOUT_SECS);
	} else {
		ret = megasas_issue_polled(instance, cmd);
		cmd->flags |= DRV_DCMD_SKIP_REFIRE;
	}

	switch (ret) {
	case DCMD_SUCCESS:
		/* Save required controller information in
		 * CPU endianness format.
		 */
		le32_to_cpus((u32 *)&ci->properties.OnOffProperties);
		le16_to_cpus((u16 *)&ci->properties.on_off_properties2);
		le32_to_cpus((u32 *)&ci->adapterOperations2);
		le32_to_cpus((u32 *)&ci->adapterOperations3);
		le16_to_cpus((u16 *)&ci->adapter_operations4);
		le32_to_cpus((u32 *)&ci->adapter_operations5);

		/* Update the latest Ext VD info.
		 * From Init path, store current firmware details.
		 * From OCR path, detect any firmware properties changes.
		 * in case of Firmware upgrade without system reboot.
		 */
		megasas_update_ext_vd_details(instance);
		instance->support_seqnum_jbod_fp =
			ci->adapterOperations3.useSeqNumJbodFP;
		instance->support_morethan256jbod =
			ci->adapter_operations4.support_pd_map_target_id;
		instance->support_nvme_passthru =
			ci->adapter_operations4.support_nvme_passthru;
		instance->support_pci_lane_margining =
			ci->adapter_operations5.support_pci_lane_margining;
		instance->task_abort_tmo = ci->TaskAbortTO;
		instance->max_reset_tmo = ci->MaxResetTO;

		/*Check whether controller is iMR or MR */
		instance->is_imr = (ci->memory_size ? 0 : 1);

		instance->snapdump_wait_time =
			(ci->properties.on_off_properties2.enable_snap_dump ?
			 MEGASAS_DEFAULT_SNAP_DUMP_WAIT_TIME : 0);

		instance->enable_fw_dev_list =
			ci->properties.on_off_properties2.enable_fw_dev_list;

		dev_info(&instance->pdev->dev,
			"controller type\t: %s(%dMB)\n",
			instance->is_imr ? "iMR" : "MR",
			le16_to_cpu(ci->memory_size));

		instance->disableOnlineCtrlReset =
			ci->properties.OnOffProperties.disableOnlineCtrlReset;
		instance->secure_jbod_support =
			ci->adapterOperations3.supportSecurityonJBOD;
		dev_info(&instance->pdev->dev, "Online Controller Reset(OCR)\t: %s\n",
			instance->disableOnlineCtrlReset ? "Disabled" : "Enabled");
		dev_info(&instance->pdev->dev, "Secure JBOD support\t: %s\n",
			instance->secure_jbod_support ? "Yes" : "No");
		dev_info(&instance->pdev->dev, "NVMe passthru support\t: %s\n",
			 instance->support_nvme_passthru ? "Yes" : "No");
		dev_info(&instance->pdev->dev,
			 "FW provided TM TaskAbort/Reset timeout\t: %d secs/%d secs\n",
			 instance->task_abort_tmo, instance->max_reset_tmo);
		dev_info(&instance->pdev->dev, "JBOD sequence map support\t: %s\n",
			 instance->support_seqnum_jbod_fp ? "Yes" : "No");
		dev_info(&instance->pdev->dev, "PCI Lane Margining support\t: %s\n",
			 instance->support_pci_lane_margining ? "Yes" : "No");

		break;

	case DCMD_TIMEOUT:
		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
				MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d\n",
				__func__, __LINE__);
			break;
		}
		break;
	case DCMD_FAILED:
		megaraid_sas_kill_hba(instance);
		break;

	}

	if (ret != DCMD_TIMEOUT)
		megasas_return_cmd(instance, cmd);

	return ret;
}

/*
 * megasas_set_crash_dump_params -	Sends address of crash dump DMA buffer
 *					to firmware
 *
 * @instance:				Adapter soft state
 * @crash_buf_state		-	tell FW to turn ON/OFF crash dump feature
					MR_CRASH_BUF_TURN_OFF = 0
					MR_CRASH_BUF_TURN_ON = 1
 * @return 0 on success non-zero on failure.
 * Issues an internal command (DCMD) to set parameters for crash dump feature.
 * Driver will send address of crash dump DMA buffer and set mbox to tell FW
 * that driver supports crash dump feature. This DCMD will be sent only if
 * crash dump feature is supported by the FW.
 *
 */
int megasas_set_crash_dump_params(struct megasas_instance *instance,
	u8 crash_buf_state)
{
	int ret = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_err(&instance->pdev->dev, "Failed to get a free cmd\n");
		return -ENOMEM;
	}


	dcmd = &cmd->frame->dcmd;

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);
	dcmd->mbox.b[0] = crash_buf_state;
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = MFI_STAT_INVALID_STATUS;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_NONE;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(CRASH_DMA_BUF_SIZE);
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_SET_CRASH_DUMP_PARAMS);

	megasas_set_dma_settings(instance, dcmd, instance->crash_dump_h,
				 CRASH_DMA_BUF_SIZE);

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd, MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	if (ret == DCMD_TIMEOUT) {
		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			megasas_reset_fusion(instance->host,
					MFI_IO_TIMEOUT_OCR);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev, "Ignore DCMD timeout: %s %d\n",
				__func__, __LINE__);
			break;
		}
	} else
		megasas_return_cmd(instance, cmd);

	return ret;
}

/**
 * megasas_issue_init_mfi -	Initializes the FW
 * @instance:		Adapter soft state
 *
 * Issues the INIT MFI cmd
 */
static int
megasas_issue_init_mfi(struct megasas_instance *instance)
{
	__le32 context;
	struct megasas_cmd *cmd;
	struct megasas_init_frame *init_frame;
	struct megasas_init_queue_info *initq_info;
	dma_addr_t init_frame_h;
	dma_addr_t initq_info_h;

	/*
	 * Prepare a init frame. Note the init frame points to queue info
	 * structure. Each frame has SGL allocated after first 64 bytes. For
	 * this frame - since we don't need any SGL - we use SGL's space as
	 * queue info structure
	 *
	 * We will not get a NULL command below. We just created the pool.
	 */
	cmd = megasas_get_cmd(instance);

	init_frame = (struct megasas_init_frame *)cmd->frame;
	initq_info = (struct megasas_init_queue_info *)
		((unsigned long)init_frame + 64);

	init_frame_h = cmd->frame_phys_addr;
	initq_info_h = init_frame_h + 64;

	context = init_frame->context;
	memset(init_frame, 0, MEGAMFI_FRAME_SIZE);
	memset(initq_info, 0, sizeof(struct megasas_init_queue_info));
	init_frame->context = context;

	initq_info->reply_queue_entries = cpu_to_le32(instance->max_fw_cmds + 1);
	initq_info->reply_queue_start_phys_addr_lo = cpu_to_le32(instance->reply_queue_h);

	initq_info->producer_index_phys_addr_lo = cpu_to_le32(instance->producer_h);
	initq_info->consumer_index_phys_addr_lo = cpu_to_le32(instance->consumer_h);

	init_frame->cmd = MFI_CMD_INIT;
	init_frame->cmd_status = MFI_STAT_INVALID_STATUS;
	init_frame->queue_info_new_phys_addr_lo =
		cpu_to_le32(lower_32_bits(initq_info_h));
	init_frame->queue_info_new_phys_addr_hi =
		cpu_to_le32(upper_32_bits(initq_info_h));

	init_frame->data_xfer_len = cpu_to_le32(sizeof(struct megasas_init_queue_info));

	/*
	 * disable the intr before firing the init frame to FW
	 */
	instance->instancet->disable_intr(instance);

	/*
	 * Issue the init frame in polled mode
	 */

	if (megasas_issue_polled(instance, cmd)) {
		dev_err(&instance->pdev->dev, "Failed to init firmware\n");
		megasas_return_cmd(instance, cmd);
		goto fail_fw_init;
	}

	megasas_return_cmd(instance, cmd);

	return 0;

fail_fw_init:
	return -EINVAL;
}

static u32
megasas_init_adapter_mfi(struct megasas_instance *instance)
{
	u32 context_sz;
	u32 reply_q_sz;

	/*
	 * Get various operational parameters from status register
	 */
	instance->max_fw_cmds = instance->instancet->read_fw_status_reg(instance) & 0x00FFFF;
	/*
	 * Reduce the max supported cmds by 1. This is to ensure that the
	 * reply_q_sz (1 more than the max cmd that driver may send)
	 * does not exceed max cmds that the FW can support
	 */
	instance->max_fw_cmds = instance->max_fw_cmds-1;
	instance->max_mfi_cmds = instance->max_fw_cmds;
	instance->max_num_sge = (instance->instancet->read_fw_status_reg(instance) & 0xFF0000) >>
					0x10;
	/*
	 * For MFI skinny adapters, MEGASAS_SKINNY_INT_CMDS commands
	 * are reserved for IOCTL + driver's internal DCMDs.
	 */
	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_SAS0073SKINNY) ||
		(instance->pdev->device == PCI_DEVICE_ID_LSI_SAS0071SKINNY)) {
		instance->max_scsi_cmds = (instance->max_fw_cmds -
			MEGASAS_SKINNY_INT_CMDS);
		sema_init(&instance->ioctl_sem, MEGASAS_SKINNY_INT_CMDS);
	} else {
		instance->max_scsi_cmds = (instance->max_fw_cmds -
			MEGASAS_INT_CMDS);
		sema_init(&instance->ioctl_sem, (MEGASAS_MFI_IOCTL_CMDS));
	}

	instance->cur_can_queue = instance->max_scsi_cmds;
	/*
	 * Create a pool of commands
	 */
	if (megasas_alloc_cmds(instance))
		goto fail_alloc_cmds;

	/*
	 * Allocate memory for reply queue. Length of reply queue should
	 * be _one_ more than the maximum commands handled by the firmware.
	 *
	 * Note: When FW completes commands, it places corresponding contex
	 * values in this circular reply queue. This circular queue is a fairly
	 * typical producer-consumer queue. FW is the producer (of completed
	 * commands) and the driver is the consumer.
	 */
	context_sz = sizeof(u32);
	reply_q_sz = context_sz * (instance->max_fw_cmds + 1);

	instance->reply_queue = dma_alloc_coherent(&instance->pdev->dev,
			reply_q_sz, &instance->reply_queue_h, GFP_KERNEL);

	if (!instance->reply_queue) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Out of DMA mem for reply queue\n");
		goto fail_reply_queue;
	}

	if (megasas_issue_init_mfi(instance))
		goto fail_fw_init;

	if (megasas_get_ctrl_info(instance)) {
		dev_err(&instance->pdev->dev, "(%d): Could get controller info "
			"Fail from %s %d\n", instance->unique_id,
			__func__, __LINE__);
		goto fail_fw_init;
	}

	instance->fw_support_ieee = 0;
	instance->fw_support_ieee =
		(instance->instancet->read_fw_status_reg(instance) &
		0x04000000);

	dev_notice(&instance->pdev->dev, "megasas_init_mfi: fw_support_ieee=%d",
			instance->fw_support_ieee);

	if (instance->fw_support_ieee)
		instance->flag_ieee = 1;

	return 0;

fail_fw_init:

	dma_free_coherent(&instance->pdev->dev, reply_q_sz,
			    instance->reply_queue, instance->reply_queue_h);
fail_reply_queue:
	megasas_free_cmds(instance);

fail_alloc_cmds:
	return 1;
}

static
void megasas_setup_irq_poll(struct megasas_instance *instance)
{
	struct megasas_irq_context *irq_ctx;
	u32 count, i;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;

	/* Initialize IRQ poll */
	for (i = 0; i < count; i++) {
		irq_ctx = &instance->irq_context[i];
		irq_ctx->os_irq = pci_irq_vector(instance->pdev, i);
		irq_ctx->irq_poll_scheduled = false;
		irq_poll_init(&irq_ctx->irqpoll,
			      instance->threshold_reply_count,
			      megasas_irqpoll);
	}
}

/*
 * megasas_setup_irqs_ioapic -		register legacy interrupts.
 * @instance:				Adapter soft state
 *
 * Do not enable interrupt, only setup ISRs.
 *
 * Return 0 on success.
 */
static int
megasas_setup_irqs_ioapic(struct megasas_instance *instance)
{
	struct pci_dev *pdev;

	pdev = instance->pdev;
	instance->irq_context[0].instance = instance;
	instance->irq_context[0].MSIxIndex = 0;
	snprintf(instance->irq_context->name, MEGASAS_MSIX_NAME_LEN, "%s%u",
		"megasas", instance->host->host_no);
	if (request_irq(pci_irq_vector(pdev, 0),
			instance->instancet->service_isr, IRQF_SHARED,
			instance->irq_context->name, &instance->irq_context[0])) {
		dev_err(&instance->pdev->dev,
				"Failed to register IRQ from %s %d\n",
				__func__, __LINE__);
		return -1;
	}
	instance->perf_mode = MR_LATENCY_PERF_MODE;
	instance->low_latency_index_start = 0;
	return 0;
}

/**
 * megasas_setup_irqs_msix -		register MSI-x interrupts.
 * @instance:				Adapter soft state
 * @is_probe:				Driver probe check
 *
 * Do not enable interrupt, only setup ISRs.
 *
 * Return 0 on success.
 */
static int
megasas_setup_irqs_msix(struct megasas_instance *instance, u8 is_probe)
{
	int i, j;
	struct pci_dev *pdev;

	pdev = instance->pdev;

	/* Try MSI-x */
	for (i = 0; i < instance->msix_vectors; i++) {
		instance->irq_context[i].instance = instance;
		instance->irq_context[i].MSIxIndex = i;
		snprintf(instance->irq_context[i].name, MEGASAS_MSIX_NAME_LEN, "%s%u-msix%u",
			"megasas", instance->host->host_no, i);
		if (request_irq(pci_irq_vector(pdev, i),
			instance->instancet->service_isr, 0, instance->irq_context[i].name,
			&instance->irq_context[i])) {
			dev_err(&instance->pdev->dev,
				"Failed to register IRQ for vector %d.\n", i);
			for (j = 0; j < i; j++) {
				if (j < instance->low_latency_index_start)
					irq_update_affinity_hint(
						pci_irq_vector(pdev, j), NULL);
				free_irq(pci_irq_vector(pdev, j),
					 &instance->irq_context[j]);
			}
			/* Retry irq register for IO_APIC*/
			instance->msix_vectors = 0;
			instance->msix_load_balance = false;
			if (is_probe) {
				pci_free_irq_vectors(instance->pdev);
				return megasas_setup_irqs_ioapic(instance);
			} else {
				return -1;
			}
		}
	}

	return 0;
}

/*
 * megasas_destroy_irqs-		unregister interrupts.
 * @instance:				Adapter soft state
 * return:				void
 */
static void
megasas_destroy_irqs(struct megasas_instance *instance) {

	int i;
	int count;
	struct megasas_irq_context *irq_ctx;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	if (instance->adapter_type != MFI_SERIES) {
		for (i = 0; i < count; i++) {
			irq_ctx = &instance->irq_context[i];
			irq_poll_disable(&irq_ctx->irqpoll);
		}
	}

	if (instance->msix_vectors)
		for (i = 0; i < instance->msix_vectors; i++) {
			if (i < instance->low_latency_index_start)
				irq_update_affinity_hint(
				    pci_irq_vector(instance->pdev, i), NULL);
			free_irq(pci_irq_vector(instance->pdev, i),
				 &instance->irq_context[i]);
		}
	else
		free_irq(pci_irq_vector(instance->pdev, 0),
			 &instance->irq_context[0]);
}

/**
 * megasas_setup_jbod_map -	setup jbod map for FP seq_number.
 * @instance:				Adapter soft state
 *
 * Return 0 on success.
 */
void
megasas_setup_jbod_map(struct megasas_instance *instance)
{
	int i;
	struct fusion_context *fusion = instance->ctrl_context;
	size_t pd_seq_map_sz;

	pd_seq_map_sz = struct_size_t(struct MR_PD_CFG_SEQ_NUM_SYNC, seq,
				      MAX_PHYSICAL_DEVICES);

	instance->use_seqnum_jbod_fp =
		instance->support_seqnum_jbod_fp;
	if (reset_devices || !fusion ||
		!instance->support_seqnum_jbod_fp) {
		dev_info(&instance->pdev->dev,
			"JBOD sequence map is disabled %s %d\n",
			__func__, __LINE__);
		instance->use_seqnum_jbod_fp = false;
		return;
	}

	if (fusion->pd_seq_sync[0])
		goto skip_alloc;

	for (i = 0; i < JBOD_MAPS_COUNT; i++) {
		fusion->pd_seq_sync[i] = dma_alloc_coherent
			(&instance->pdev->dev, pd_seq_map_sz,
			&fusion->pd_seq_phys[i], GFP_KERNEL);
		if (!fusion->pd_seq_sync[i]) {
			dev_err(&instance->pdev->dev,
				"Failed to allocate memory from %s %d\n",
				__func__, __LINE__);
			if (i == 1) {
				dma_free_coherent(&instance->pdev->dev,
					pd_seq_map_sz, fusion->pd_seq_sync[0],
					fusion->pd_seq_phys[0]);
				fusion->pd_seq_sync[0] = NULL;
			}
			instance->use_seqnum_jbod_fp = false;
			return;
		}
	}

skip_alloc:
	if (!megasas_sync_pd_seq_num(instance, false) &&
		!megasas_sync_pd_seq_num(instance, true))
		instance->use_seqnum_jbod_fp = true;
	else
		instance->use_seqnum_jbod_fp = false;
}

static void megasas_setup_reply_map(struct megasas_instance *instance)
{
	const struct cpumask *mask;
	unsigned int queue, cpu, low_latency_index_start;

	low_latency_index_start = instance->low_latency_index_start;

	for (queue = low_latency_index_start; queue < instance->msix_vectors; queue++) {
		mask = pci_irq_get_affinity(instance->pdev, queue);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			instance->reply_map[cpu] = queue;
	}
	return;

fallback:
	queue = low_latency_index_start;
	for_each_possible_cpu(cpu) {
		instance->reply_map[cpu] = queue;
		if (queue == (instance->msix_vectors - 1))
			queue = low_latency_index_start;
		else
			queue++;
	}
}

/**
 * megasas_get_device_list -	Get the PD and LD device list from FW.
 * @instance:			Adapter soft state
 * @return:			Success or failure
 *
 * Issue DCMDs to Firmware to get the PD and LD list.
 * Based on the FW support, driver sends the HOST_DEVICE_LIST or combination
 * of PD_LIST/LD_LIST_QUERY DCMDs to get the device list.
 */
static
int megasas_get_device_list(struct megasas_instance *instance)
{
	if (instance->enable_fw_dev_list) {
		if (megasas_host_device_list_query(instance, true))
			return FAILED;
	} else {
		if (megasas_get_pd_list(instance) < 0) {
			dev_err(&instance->pdev->dev, "failed to get PD list\n");
			return FAILED;
		}

		if (megasas_ld_list_query(instance,
					  MR_LD_QUERY_TYPE_EXPOSED_TO_HOST)) {
			dev_err(&instance->pdev->dev, "failed to get LD list\n");
			return FAILED;
		}
	}

	return SUCCESS;
}

/**
 * megasas_set_high_iops_queue_affinity_and_hint -	Set affinity and hint
 *							for high IOPS queues
 * @instance:						Adapter soft state
 * return:						void
 */
static inline void
megasas_set_high_iops_queue_affinity_and_hint(struct megasas_instance *instance)
{
	int i;
	unsigned int irq;
	const struct cpumask *mask;

	if (instance->perf_mode == MR_BALANCED_PERF_MODE) {
		mask = cpumask_of_node(dev_to_node(&instance->pdev->dev));

		for (i = 0; i < instance->low_latency_index_start; i++) {
			irq = pci_irq_vector(instance->pdev, i);
			irq_set_affinity_and_hint(irq, mask);
		}
	}
}

static int
__megasas_alloc_irq_vectors(struct megasas_instance *instance)
{
	int i, irq_flags;
	struct irq_affinity desc = { .pre_vectors = instance->low_latency_index_start };
	struct irq_affinity *descp = &desc;

	irq_flags = PCI_IRQ_MSIX;

	if (instance->smp_affinity_enable)
		irq_flags |= PCI_IRQ_AFFINITY | PCI_IRQ_ALL_TYPES;
	else
		descp = NULL;

	/* Do not allocate msix vectors for poll_queues.
	 * msix_vectors is always within a range of FW supported reply queue.
	 */
	i = pci_alloc_irq_vectors_affinity(instance->pdev,
		instance->low_latency_index_start,
		instance->msix_vectors - instance->iopoll_q_count, irq_flags, descp);

	return i;
}

/**
 * megasas_alloc_irq_vectors -	Allocate IRQ vectors/enable MSI-x vectors
 * @instance:			Adapter soft state
 * return:			void
 */
static void
megasas_alloc_irq_vectors(struct megasas_instance *instance)
{
	int i;
	unsigned int num_msix_req;

	instance->iopoll_q_count = 0;
	if ((instance->adapter_type != MFI_SERIES) &&
		poll_queues) {

		instance->perf_mode = MR_LATENCY_PERF_MODE;
		instance->low_latency_index_start = 1;

		/* reserve for default and non-mananged pre-vector. */
		if (instance->msix_vectors > (poll_queues + 2))
			instance->iopoll_q_count = poll_queues;
		else
			instance->iopoll_q_count = 0;

		num_msix_req = num_online_cpus() + instance->low_latency_index_start;
		instance->msix_vectors = min(num_msix_req,
				instance->msix_vectors);

	}

	i = __megasas_alloc_irq_vectors(instance);

	if (((instance->perf_mode == MR_BALANCED_PERF_MODE)
		|| instance->iopoll_q_count) &&
	    (i != (instance->msix_vectors - instance->iopoll_q_count))) {
		if (instance->msix_vectors)
			pci_free_irq_vectors(instance->pdev);
		/* Disable Balanced IOPS mode and try realloc vectors */
		instance->perf_mode = MR_LATENCY_PERF_MODE;
		instance->low_latency_index_start = 1;
		num_msix_req = num_online_cpus() + instance->low_latency_index_start;

		instance->msix_vectors = min(num_msix_req,
				instance->msix_vectors);

		instance->iopoll_q_count = 0;
		i = __megasas_alloc_irq_vectors(instance);

	}

	dev_info(&instance->pdev->dev,
		"requested/available msix %d/%d poll_queue %d\n",
			instance->msix_vectors - instance->iopoll_q_count,
			i, instance->iopoll_q_count);

	if (i > 0)
		instance->msix_vectors = i;
	else
		instance->msix_vectors = 0;

	if (instance->smp_affinity_enable)
		megasas_set_high_iops_queue_affinity_and_hint(instance);
}

/**
 * megasas_init_fw -	Initializes the FW
 * @instance:		Adapter soft state
 *
 * This is the main function for initializing firmware
 */

static int megasas_init_fw(struct megasas_instance *instance)
{
	u32 max_sectors_1;
	u32 max_sectors_2, tmp_sectors, msix_enable;
	u32 scratch_pad_1, scratch_pad_2, scratch_pad_3, status_reg;
	resource_size_t base_addr;
	void *base_addr_phys;
	struct megasas_ctrl_info *ctrl_info = NULL;
	unsigned long bar_list;
	int i, j, loop;
	struct IOV_111 *iovPtr;
	struct fusion_context *fusion;
	bool intr_coalescing;
	unsigned int num_msix_req;
	u16 lnksta, speed;

	fusion = instance->ctrl_context;

	/* Find first memory bar */
	bar_list = pci_select_bars(instance->pdev, IORESOURCE_MEM);
	instance->bar = find_first_bit(&bar_list, BITS_PER_LONG);
	if (pci_request_selected_regions(instance->pdev, 1<<instance->bar,
					 "megasas: LSI")) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "IO memory region busy!\n");
		return -EBUSY;
	}

	base_addr = pci_resource_start(instance->pdev, instance->bar);
	instance->reg_set = ioremap(base_addr, 8192);

	if (!instance->reg_set) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to map IO mem\n");
		goto fail_ioremap;
	}

	base_addr_phys = &base_addr;
	dev_printk(KERN_DEBUG, &instance->pdev->dev,
		   "BAR:0x%lx  BAR's base_addr(phys):%pa  mapped virt_addr:0x%p\n",
		   instance->bar, base_addr_phys, instance->reg_set);

	if (instance->adapter_type != MFI_SERIES)
		instance->instancet = &megasas_instance_template_fusion;
	else {
		switch (instance->pdev->device) {
		case PCI_DEVICE_ID_LSI_SAS1078R:
		case PCI_DEVICE_ID_LSI_SAS1078DE:
			instance->instancet = &megasas_instance_template_ppc;
			break;
		case PCI_DEVICE_ID_LSI_SAS1078GEN2:
		case PCI_DEVICE_ID_LSI_SAS0079GEN2:
			instance->instancet = &megasas_instance_template_gen2;
			break;
		case PCI_DEVICE_ID_LSI_SAS0073SKINNY:
		case PCI_DEVICE_ID_LSI_SAS0071SKINNY:
			instance->instancet = &megasas_instance_template_skinny;
			break;
		case PCI_DEVICE_ID_LSI_SAS1064R:
		case PCI_DEVICE_ID_DELL_PERC5:
		default:
			instance->instancet = &megasas_instance_template_xscale;
			instance->pd_list_not_supported = 1;
			break;
		}
	}

	if (megasas_transition_to_ready(instance, 0)) {
		dev_info(&instance->pdev->dev,
			 "Failed to transition controller to ready from %s!\n",
			 __func__);
		if (instance->adapter_type != MFI_SERIES) {
			status_reg = instance->instancet->read_fw_status_reg(
					instance);
			if (status_reg & MFI_RESET_ADAPTER) {
				if (megasas_adp_reset_wait_for_ready
					(instance, true, 0) == FAILED)
					goto fail_ready_state;
			} else {
				goto fail_ready_state;
			}
		} else {
			atomic_set(&instance->fw_reset_no_pci_access, 1);
			instance->instancet->adp_reset
				(instance, instance->reg_set);
			atomic_set(&instance->fw_reset_no_pci_access, 0);

			/*waiting for about 30 second before retry*/
			ssleep(30);

			if (megasas_transition_to_ready(instance, 0))
				goto fail_ready_state;
		}

		dev_info(&instance->pdev->dev,
			 "FW restarted successfully from %s!\n",
			 __func__);
	}

	megasas_init_ctrl_params(instance);

	if (megasas_set_dma_mask(instance))
		goto fail_ready_state;

	if (megasas_alloc_ctrl_mem(instance))
		goto fail_alloc_dma_buf;

	if (megasas_alloc_ctrl_dma_buffers(instance))
		goto fail_alloc_dma_buf;

	fusion = instance->ctrl_context;

	if (instance->adapter_type >= VENTURA_SERIES) {
		scratch_pad_2 =
			megasas_readl(instance,
				      &instance->reg_set->outbound_scratch_pad_2);
		instance->max_raid_mapsize = ((scratch_pad_2 >>
			MR_MAX_RAID_MAP_SIZE_OFFSET_SHIFT) &
			MR_MAX_RAID_MAP_SIZE_MASK);
	}

	instance->enable_sdev_max_qd = enable_sdev_max_qd;

	switch (instance->adapter_type) {
	case VENTURA_SERIES:
		fusion->pcie_bw_limitation = true;
		break;
	case AERO_SERIES:
		fusion->r56_div_offload = true;
		break;
	default:
		break;
	}

	/* Check if MSI-X is supported while in ready state */
	msix_enable = (instance->instancet->read_fw_status_reg(instance) &
		       0x4000000) >> 0x1a;
	if (msix_enable && !msix_disable) {

		scratch_pad_1 = megasas_readl
			(instance, &instance->reg_set->outbound_scratch_pad_1);
		/* Check max MSI-X vectors */
		if (fusion) {
			if (instance->adapter_type == THUNDERBOLT_SERIES) {
				/* Thunderbolt Series*/
				instance->msix_vectors = (scratch_pad_1
					& MR_MAX_REPLY_QUEUES_OFFSET) + 1;
			} else {
				instance->msix_vectors = ((scratch_pad_1
					& MR_MAX_REPLY_QUEUES_EXT_OFFSET)
					>> MR_MAX_REPLY_QUEUES_EXT_OFFSET_SHIFT) + 1;

				/*
				 * For Invader series, > 8 MSI-x vectors
				 * supported by FW/HW implies combined
				 * reply queue mode is enabled.
				 * For Ventura series, > 16 MSI-x vectors
				 * supported by FW/HW implies combined
				 * reply queue mode is enabled.
				 */
				switch (instance->adapter_type) {
				case INVADER_SERIES:
					if (instance->msix_vectors > 8)
						instance->msix_combined = true;
					break;
				case AERO_SERIES:
				case VENTURA_SERIES:
					if (instance->msix_vectors > 16)
						instance->msix_combined = true;
					break;
				}

				if (rdpq_enable)
					instance->is_rdpq = (scratch_pad_1 & MR_RDPQ_MODE_OFFSET) ?
								1 : 0;

				if (instance->adapter_type >= INVADER_SERIES &&
				    !instance->msix_combined) {
					instance->msix_load_balance = true;
					instance->smp_affinity_enable = false;
				}

				/* Save 1-15 reply post index address to local memory
				 * Index 0 is already saved from reg offset
				 * MPI2_REPLY_POST_HOST_INDEX_OFFSET
				 */
				for (loop = 1; loop < MR_MAX_MSIX_REG_ARRAY; loop++) {
					instance->reply_post_host_index_addr[loop] =
						(u32 __iomem *)
						((u8 __iomem *)instance->reg_set +
						MPI2_SUP_REPLY_POST_HOST_INDEX_OFFSET
						+ (loop * 0x10));
				}
			}

			dev_info(&instance->pdev->dev,
				 "firmware supports msix\t: (%d)",
				 instance->msix_vectors);
			if (msix_vectors)
				instance->msix_vectors = min(msix_vectors,
					instance->msix_vectors);
		} else /* MFI adapters */
			instance->msix_vectors = 1;


		/*
		 * For Aero (if some conditions are met), driver will configure a
		 * few additional reply queues with interrupt coalescing enabled.
		 * These queues with interrupt coalescing enabled are called
		 * High IOPS queues and rest of reply queues (based on number of
		 * logical CPUs) are termed as Low latency queues.
		 *
		 * Total Number of reply queues = High IOPS queues + low latency queues
		 *
		 * For rest of fusion adapters, 1 additional reply queue will be
		 * reserved for management commands, rest of reply queues
		 * (based on number of logical CPUs) will be used for IOs and
		 * referenced as IO queues.
		 * Total Number of reply queues = 1 + IO queues
		 *
		 * MFI adapters supports single MSI-x so single reply queue
		 * will be used for IO and management commands.
		 */

		intr_coalescing = (scratch_pad_1 & MR_INTR_COALESCING_SUPPORT_OFFSET) ?
								true : false;
		if (intr_coalescing &&
			(num_online_cpus() >= MR_HIGH_IOPS_QUEUE_COUNT) &&
			(instance->msix_vectors == MEGASAS_MAX_MSIX_QUEUES))
			instance->perf_mode = MR_BALANCED_PERF_MODE;
		else
			instance->perf_mode = MR_LATENCY_PERF_MODE;


		if (instance->adapter_type == AERO_SERIES) {
			pcie_capability_read_word(instance->pdev, PCI_EXP_LNKSTA, &lnksta);
			speed = lnksta & PCI_EXP_LNKSTA_CLS;

			/*
			 * For Aero, if PCIe link speed is <16 GT/s, then driver should operate
			 * in latency perf mode and enable R1 PCI bandwidth algorithm
			 */
			if (speed < 0x4) {
				instance->perf_mode = MR_LATENCY_PERF_MODE;
				fusion->pcie_bw_limitation = true;
			}

			/*
			 * Performance mode settings provided through module parameter-perf_mode will
			 * take affect only for:
			 * 1. Aero family of adapters.
			 * 2. When user sets module parameter- perf_mode in range of 0-2.
			 */
			if ((perf_mode >= MR_BALANCED_PERF_MODE) &&
				(perf_mode <= MR_LATENCY_PERF_MODE))
				instance->perf_mode = perf_mode;
			/*
			 * If intr coalescing is not supported by controller FW, then IOPS
			 * and Balanced modes are not feasible.
			 */
			if (!intr_coalescing)
				instance->perf_mode = MR_LATENCY_PERF_MODE;

		}

		if (instance->perf_mode == MR_BALANCED_PERF_MODE)
			instance->low_latency_index_start =
				MR_HIGH_IOPS_QUEUE_COUNT;
		else
			instance->low_latency_index_start = 1;

		num_msix_req = num_online_cpus() + instance->low_latency_index_start;

		instance->msix_vectors = min(num_msix_req,
				instance->msix_vectors);

		megasas_alloc_irq_vectors(instance);
		if (!instance->msix_vectors)
			instance->msix_load_balance = false;
	}
	/*
	 * MSI-X host index 0 is common for all adapter.
	 * It is used for all MPT based Adapters.
	 */
	if (instance->msix_combined) {
		instance->reply_post_host_index_addr[0] =
				(u32 *)((u8 *)instance->reg_set +
				MPI2_SUP_REPLY_POST_HOST_INDEX_OFFSET);
	} else {
		instance->reply_post_host_index_addr[0] =
			(u32 *)((u8 *)instance->reg_set +
			MPI2_REPLY_POST_HOST_INDEX_OFFSET);
	}

	if (!instance->msix_vectors) {
		i = pci_alloc_irq_vectors(instance->pdev, 1, 1, PCI_IRQ_INTX);
		if (i < 0)
			goto fail_init_adapter;
	}

	megasas_setup_reply_map(instance);

	dev_info(&instance->pdev->dev,
		"current msix/online cpus\t: (%d/%d)\n",
		instance->msix_vectors, (unsigned int)num_online_cpus());
	dev_info(&instance->pdev->dev,
		"RDPQ mode\t: (%s)\n", instance->is_rdpq ? "enabled" : "disabled");

	tasklet_init(&instance->isr_tasklet, instance->instancet->tasklet,
		(unsigned long)instance);

	/*
	 * Below are default value for legacy Firmware.
	 * non-fusion based controllers
	 */
	instance->fw_supported_vd_count = MAX_LOGICAL_DRIVES;
	instance->fw_supported_pd_count = MAX_PHYSICAL_DEVICES;
	/* Get operational params, sge flags, send init cmd to controller */
	if (instance->instancet->init_adapter(instance))
		goto fail_init_adapter;

	if (instance->adapter_type >= VENTURA_SERIES) {
		scratch_pad_3 =
			megasas_readl(instance,
				      &instance->reg_set->outbound_scratch_pad_3);
		if ((scratch_pad_3 & MR_NVME_PAGE_SIZE_MASK) >=
			MR_DEFAULT_NVME_PAGE_SHIFT)
			instance->nvme_page_size =
				(1 << (scratch_pad_3 & MR_NVME_PAGE_SIZE_MASK));

		dev_info(&instance->pdev->dev,
			 "NVME page size\t: (%d)\n", instance->nvme_page_size);
	}

	if (instance->msix_vectors ?
		megasas_setup_irqs_msix(instance, 1) :
		megasas_setup_irqs_ioapic(instance))
		goto fail_init_adapter;

	if (instance->adapter_type != MFI_SERIES)
		megasas_setup_irq_poll(instance);

	instance->instancet->enable_intr(instance);

	dev_info(&instance->pdev->dev, "INIT adapter done\n");

	megasas_setup_jbod_map(instance);

	if (megasas_get_device_list(instance) != SUCCESS) {
		dev_err(&instance->pdev->dev,
			"%s: megasas_get_device_list failed\n",
			__func__);
		goto fail_get_ld_pd_list;
	}

	/* stream detection initialization */
	if (instance->adapter_type >= VENTURA_SERIES) {
		fusion->stream_detect_by_ld =
			kcalloc(MAX_LOGICAL_DRIVES_EXT,
				sizeof(struct LD_STREAM_DETECT *),
				GFP_KERNEL);
		if (!fusion->stream_detect_by_ld) {
			dev_err(&instance->pdev->dev,
				"unable to allocate stream detection for pool of LDs\n");
			goto fail_get_ld_pd_list;
		}
		for (i = 0; i < MAX_LOGICAL_DRIVES_EXT; ++i) {
			fusion->stream_detect_by_ld[i] =
				kzalloc(sizeof(struct LD_STREAM_DETECT),
				GFP_KERNEL);
			if (!fusion->stream_detect_by_ld[i]) {
				dev_err(&instance->pdev->dev,
					"unable to allocate stream detect by LD\n");
				for (j = 0; j < i; ++j)
					kfree(fusion->stream_detect_by_ld[j]);
				kfree(fusion->stream_detect_by_ld);
				fusion->stream_detect_by_ld = NULL;
				goto fail_get_ld_pd_list;
			}
			fusion->stream_detect_by_ld[i]->mru_bit_map
				= MR_STREAM_BITMAP;
		}
	}

	/*
	 * Compute the max allowed sectors per IO: The controller info has two
	 * limits on max sectors. Driver should use the minimum of these two.
	 *
	 * 1 << stripe_sz_ops.min = max sectors per strip
	 *
	 * Note that older firmwares ( < FW ver 30) didn't report information
	 * to calculate max_sectors_1. So the number ended up as zero always.
	 */
	tmp_sectors = 0;
	ctrl_info = instance->ctrl_info_buf;

	max_sectors_1 = (1 << ctrl_info->stripe_sz_ops.min) *
		le16_to_cpu(ctrl_info->max_strips_per_io);
	max_sectors_2 = le32_to_cpu(ctrl_info->max_request_size);

	tmp_sectors = min_t(u32, max_sectors_1, max_sectors_2);

	instance->peerIsPresent = ctrl_info->cluster.peerIsPresent;
	instance->passive = ctrl_info->cluster.passive;
	memcpy(instance->clusterId, ctrl_info->clusterId, sizeof(instance->clusterId));
	instance->UnevenSpanSupport =
		ctrl_info->adapterOperations2.supportUnevenSpans;
	if (instance->UnevenSpanSupport) {
		struct fusion_context *fusion = instance->ctrl_context;
		if (MR_ValidateMapInfo(instance, instance->map_id))
			fusion->fast_path_io = 1;
		else
			fusion->fast_path_io = 0;

	}
	if (ctrl_info->host_interface.SRIOV) {
		instance->requestorId = ctrl_info->iov.requestorId;
		if (instance->pdev->device == PCI_DEVICE_ID_LSI_PLASMA) {
			if (!ctrl_info->adapterOperations2.activePassive)
			    instance->PlasmaFW111 = 1;

			dev_info(&instance->pdev->dev, "SR-IOV: firmware type: %s\n",
			    instance->PlasmaFW111 ? "1.11" : "new");

			if (instance->PlasmaFW111) {
			    iovPtr = (struct IOV_111 *)
				((unsigned char *)ctrl_info + IOV_111_OFFSET);
			    instance->requestorId = iovPtr->requestorId;
			}
		}
		dev_info(&instance->pdev->dev, "SRIOV: VF requestorId %d\n",
			instance->requestorId);
	}

	instance->crash_dump_fw_support =
		ctrl_info->adapterOperations3.supportCrashDump;
	instance->crash_dump_drv_support =
		(instance->crash_dump_fw_support &&
		instance->crash_dump_buf);
	if (instance->crash_dump_drv_support)
		megasas_set_crash_dump_params(instance,
			MR_CRASH_BUF_TURN_OFF);

	else {
		if (instance->crash_dump_buf)
			dma_free_coherent(&instance->pdev->dev,
				CRASH_DMA_BUF_SIZE,
				instance->crash_dump_buf,
				instance->crash_dump_h);
		instance->crash_dump_buf = NULL;
	}

	if (instance->snapdump_wait_time) {
		megasas_get_snapdump_properties(instance);
		dev_info(&instance->pdev->dev, "Snap dump wait time\t: %d\n",
			 instance->snapdump_wait_time);
	}

	dev_info(&instance->pdev->dev,
		"pci id\t\t: (0x%04x)/(0x%04x)/(0x%04x)/(0x%04x)\n",
		le16_to_cpu(ctrl_info->pci.vendor_id),
		le16_to_cpu(ctrl_info->pci.device_id),
		le16_to_cpu(ctrl_info->pci.sub_vendor_id),
		le16_to_cpu(ctrl_info->pci.sub_device_id));
	dev_info(&instance->pdev->dev, "unevenspan support	: %s\n",
		instance->UnevenSpanSupport ? "yes" : "no");
	dev_info(&instance->pdev->dev, "firmware crash dump	: %s\n",
		instance->crash_dump_drv_support ? "yes" : "no");
	dev_info(&instance->pdev->dev, "JBOD sequence map	: %s\n",
		instance->use_seqnum_jbod_fp ? "enabled" : "disabled");

	instance->max_sectors_per_req = instance->max_num_sge *
						SGE_BUFFER_SIZE / 512;
	if (tmp_sectors && (instance->max_sectors_per_req > tmp_sectors))
		instance->max_sectors_per_req = tmp_sectors;

	/* Check for valid throttlequeuedepth module parameter */
	if (throttlequeuedepth &&
			throttlequeuedepth <= instance->max_scsi_cmds)
		instance->throttlequeuedepth = throttlequeuedepth;
	else
		instance->throttlequeuedepth =
				MEGASAS_THROTTLE_QUEUE_DEPTH;

	if ((resetwaittime < 1) ||
	    (resetwaittime > MEGASAS_RESET_WAIT_TIME))
		resetwaittime = MEGASAS_RESET_WAIT_TIME;

	if ((scmd_timeout < 10) || (scmd_timeout > MEGASAS_DEFAULT_CMD_TIMEOUT))
		scmd_timeout = MEGASAS_DEFAULT_CMD_TIMEOUT;

	/* Launch SR-IOV heartbeat timer */
	if (instance->requestorId) {
		if (!megasas_sriov_start_heartbeat(instance, 1)) {
			megasas_start_timer(instance);
		} else {
			instance->skip_heartbeat_timer_del = 1;
			goto fail_get_ld_pd_list;
		}
	}

	/*
	 * Create and start watchdog thread which will monitor
	 * controller state every 1 sec and trigger OCR when
	 * it enters fault state
	 */
	if (instance->adapter_type != MFI_SERIES)
		if (megasas_fusion_start_watchdog(instance) != SUCCESS)
			goto fail_start_watchdog;

	return 0;

fail_start_watchdog:
	if (instance->requestorId && !instance->skip_heartbeat_timer_del)
		del_timer_sync(&instance->sriov_heartbeat_timer);
fail_get_ld_pd_list:
	instance->instancet->disable_intr(instance);
	megasas_destroy_irqs(instance);
fail_init_adapter:
	if (instance->msix_vectors)
		pci_free_irq_vectors(instance->pdev);
	instance->msix_vectors = 0;
fail_alloc_dma_buf:
	megasas_free_ctrl_dma_buffers(instance);
	megasas_free_ctrl_mem(instance);
fail_ready_state:
	iounmap(instance->reg_set);

fail_ioremap:
	pci_release_selected_regions(instance->pdev, 1<<instance->bar);

	dev_err(&instance->pdev->dev, "Failed from %s %d\n",
		__func__, __LINE__);
	return -EINVAL;
}

/**
 * megasas_release_mfi -	Reverses the FW initialization
 * @instance:			Adapter soft state
 */
static void megasas_release_mfi(struct megasas_instance *instance)
{
	u32 reply_q_sz = sizeof(u32) *(instance->max_mfi_cmds + 1);

	if (instance->reply_queue)
		dma_free_coherent(&instance->pdev->dev, reply_q_sz,
			    instance->reply_queue, instance->reply_queue_h);

	megasas_free_cmds(instance);

	iounmap(instance->reg_set);

	pci_release_selected_regions(instance->pdev, 1<<instance->bar);
}

/**
 * megasas_get_seq_num -	Gets latest event sequence numbers
 * @instance:			Adapter soft state
 * @eli:			FW event log sequence numbers information
 *
 * FW maintains a log of all events in a non-volatile area. Upper layers would
 * usually find out the latest sequence number of the events, the seq number at
 * the boot etc. They would "read" all the events below the latest seq number
 * by issuing a direct fw cmd (DCMD). For the future events (beyond latest seq
 * number), they would subsribe to AEN (asynchronous event notification) and
 * wait for the events to happen.
 */
static int
megasas_get_seq_num(struct megasas_instance *instance,
		    struct megasas_evt_log_info *eli)
{
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct megasas_evt_log_info *el_info;
	dma_addr_t el_info_h = 0;
	int ret;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;
	el_info = dma_alloc_coherent(&instance->pdev->dev,
				     sizeof(struct megasas_evt_log_info),
				     &el_info_h, GFP_KERNEL);
	if (!el_info) {
		megasas_return_cmd(instance, cmd);
		return -ENOMEM;
	}

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct megasas_evt_log_info));
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_EVENT_GET_INFO);

	megasas_set_dma_settings(instance, dcmd, el_info_h,
				 sizeof(struct megasas_evt_log_info));

	ret = megasas_issue_blocked_cmd(instance, cmd, MFI_IO_TIMEOUT_SECS);
	if (ret != DCMD_SUCCESS) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		goto dcmd_failed;
	}

	/*
	 * Copy the data back into callers buffer
	 */
	eli->newest_seq_num = el_info->newest_seq_num;
	eli->oldest_seq_num = el_info->oldest_seq_num;
	eli->clear_seq_num = el_info->clear_seq_num;
	eli->shutdown_seq_num = el_info->shutdown_seq_num;
	eli->boot_seq_num = el_info->boot_seq_num;

dcmd_failed:
	dma_free_coherent(&instance->pdev->dev,
			sizeof(struct megasas_evt_log_info),
			el_info, el_info_h);

	megasas_return_cmd(instance, cmd);

	return ret;
}

/**
 * megasas_register_aen -	Registers for asynchronous event notification
 * @instance:			Adapter soft state
 * @seq_num:			The starting sequence number
 * @class_locale_word:		Class of the event
 *
 * This function subscribes for AEN for events beyond the @seq_num. It requests
 * to be notified if and only if the event is of type @class_locale
 */
static int
megasas_register_aen(struct megasas_instance *instance, u32 seq_num,
		     u32 class_locale_word)
{
	int ret_val;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	union megasas_evt_class_locale curr_aen;
	union megasas_evt_class_locale prev_aen;

	/*
	 * If there an AEN pending already (aen_cmd), check if the
	 * class_locale of that pending AEN is inclusive of the new
	 * AEN request we currently have. If it is, then we don't have
	 * to do anything. In other words, whichever events the current
	 * AEN request is subscribing to, have already been subscribed
	 * to.
	 *
	 * If the old_cmd is _not_ inclusive, then we have to abort
	 * that command, form a class_locale that is superset of both
	 * old and current and re-issue to the FW
	 */

	curr_aen.word = class_locale_word;

	if (instance->aen_cmd) {

		prev_aen.word =
			le32_to_cpu(instance->aen_cmd->frame->dcmd.mbox.w[1]);

		if ((curr_aen.members.class < MFI_EVT_CLASS_DEBUG) ||
		    (curr_aen.members.class > MFI_EVT_CLASS_DEAD)) {
			dev_info(&instance->pdev->dev,
				 "%s %d out of range class %d send by application\n",
				 __func__, __LINE__, curr_aen.members.class);
			return 0;
		}

		/*
		 * A class whose enum value is smaller is inclusive of all
		 * higher values. If a PROGRESS (= -1) was previously
		 * registered, then a new registration requests for higher
		 * classes need not be sent to FW. They are automatically
		 * included.
		 *
		 * Locale numbers don't have such hierarchy. They are bitmap
		 * values
		 */
		if ((prev_aen.members.class <= curr_aen.members.class) &&
		    !((prev_aen.members.locale & curr_aen.members.locale) ^
		      curr_aen.members.locale)) {
			/*
			 * Previously issued event registration includes
			 * current request. Nothing to do.
			 */
			return 0;
		} else {
			curr_aen.members.locale |= prev_aen.members.locale;

			if (prev_aen.members.class < curr_aen.members.class)
				curr_aen.members.class = prev_aen.members.class;

			instance->aen_cmd->abort_aen = 1;
			ret_val = megasas_issue_blocked_abort_cmd(instance,
								  instance->
								  aen_cmd, 30);

			if (ret_val) {
				dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to abort "
				       "previous AEN command\n");
				return ret_val;
			}
		}
	}

	cmd = megasas_get_cmd(instance);

	if (!cmd)
		return -ENOMEM;

	dcmd = &cmd->frame->dcmd;

	memset(instance->evt_detail, 0, sizeof(struct megasas_evt_detail));

	/*
	 * Prepare DCMD for aen registration
	 */
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(sizeof(struct megasas_evt_detail));
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_EVENT_WAIT);
	dcmd->mbox.w[0] = cpu_to_le32(seq_num);
	instance->last_seq_num = seq_num;
	dcmd->mbox.w[1] = cpu_to_le32(curr_aen.word);

	megasas_set_dma_settings(instance, dcmd, instance->evt_detail_h,
				 sizeof(struct megasas_evt_detail));

	if (instance->aen_cmd != NULL) {
		megasas_return_cmd(instance, cmd);
		return 0;
	}

	/*
	 * Store reference to the cmd used to register for AEN. When an
	 * application wants us to register for AEN, we have to abort this
	 * cmd and re-register with a new EVENT LOCALE supplied by that app
	 */
	instance->aen_cmd = cmd;

	/*
	 * Issue the aen registration frame
	 */
	instance->instancet->issue_dcmd(instance, cmd);

	return 0;
}

/* megasas_get_target_prop - Send DCMD with below details to firmware.
 *
 * This DCMD will fetch few properties of LD/system PD defined
 * in MR_TARGET_DEV_PROPERTIES. eg. Queue Depth, MDTS value.
 *
 * DCMD send by drivers whenever new target is added to the OS.
 *
 * dcmd.opcode         - MR_DCMD_DEV_GET_TARGET_PROP
 * dcmd.mbox.b[0]      - DCMD is to be fired for LD or system PD.
 *                       0 = system PD, 1 = LD.
 * dcmd.mbox.s[1]      - TargetID for LD/system PD.
 * dcmd.sge IN         - Pointer to return MR_TARGET_DEV_PROPERTIES.
 *
 * @instance:		Adapter soft state
 * @sdev:		OS provided scsi device
 *
 * Returns 0 on success non-zero on failure.
 */
int
megasas_get_target_prop(struct megasas_instance *instance,
			struct scsi_device *sdev)
{
	int ret;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	u16 targetId = ((sdev->channel % 2) * MEGASAS_MAX_DEV_PER_CHANNEL) +
			sdev->id;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_err(&instance->pdev->dev,
			"Failed to get cmd %s\n", __func__);
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	memset(instance->tgt_prop, 0, sizeof(*instance->tgt_prop));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);
	dcmd->mbox.b[0] = MEGASAS_IS_LOGICAL(sdev);

	dcmd->mbox.s[1] = cpu_to_le16(targetId);
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len =
		cpu_to_le32(sizeof(struct MR_TARGET_PROPERTIES));
	dcmd->opcode = cpu_to_le32(MR_DCMD_DRV_GET_TARGET_PROP);

	megasas_set_dma_settings(instance, dcmd, instance->tgt_prop_h,
				 sizeof(struct MR_TARGET_PROPERTIES));

	if ((instance->adapter_type != MFI_SERIES) &&
	    !instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance,
						cmd, MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	switch (ret) {
	case DCMD_TIMEOUT:
		switch (dcmd_timeout_ocr_possible(instance)) {
		case INITIATE_OCR:
			cmd->flags |= DRV_DCMD_SKIP_REFIRE;
			mutex_unlock(&instance->reset_mutex);
			megasas_reset_fusion(instance->host,
					     MFI_IO_TIMEOUT_OCR);
			mutex_lock(&instance->reset_mutex);
			break;
		case KILL_ADAPTER:
			megaraid_sas_kill_hba(instance);
			break;
		case IGNORE_TIMEOUT:
			dev_info(&instance->pdev->dev,
				 "Ignore DCMD timeout: %s %d\n",
				 __func__, __LINE__);
			break;
		}
		break;

	default:
		megasas_return_cmd(instance, cmd);
	}
	if (ret != DCMD_SUCCESS)
		dev_err(&instance->pdev->dev,
			"return from %s %d return value %d\n",
			__func__, __LINE__, ret);

	return ret;
}

/**
 * megasas_start_aen -	Subscribes to AEN during driver load time
 * @instance:		Adapter soft state
 */
static int megasas_start_aen(struct megasas_instance *instance)
{
	struct megasas_evt_log_info eli;
	union megasas_evt_class_locale class_locale;

	/*
	 * Get the latest sequence number from FW
	 */
	memset(&eli, 0, sizeof(eli));

	if (megasas_get_seq_num(instance, &eli))
		return -1;

	/*
	 * Register AEN with FW for latest sequence number plus 1
	 */
	class_locale.members.reserved = 0;
	class_locale.members.locale = MR_EVT_LOCALE_ALL;
	class_locale.members.class = MR_EVT_CLASS_DEBUG;

	return megasas_register_aen(instance,
			le32_to_cpu(eli.newest_seq_num) + 1,
			class_locale.word);
}

/**
 * megasas_io_attach -	Attaches this driver to SCSI mid-layer
 * @instance:		Adapter soft state
 */
static int megasas_io_attach(struct megasas_instance *instance)
{
	struct Scsi_Host *host = instance->host;

	/*
	 * Export parameters required by SCSI mid-layer
	 */
	host->unique_id = instance->unique_id;
	host->can_queue = instance->max_scsi_cmds;
	host->this_id = instance->init_id;
	host->sg_tablesize = instance->max_num_sge;

	if (instance->fw_support_ieee)
		instance->max_sectors_per_req = MEGASAS_MAX_SECTORS_IEEE;

	/*
	 * Check if the module parameter value for max_sectors can be used
	 */
	if (max_sectors && max_sectors < instance->max_sectors_per_req)
		instance->max_sectors_per_req = max_sectors;
	else {
		if (max_sectors) {
			if (((instance->pdev->device ==
				PCI_DEVICE_ID_LSI_SAS1078GEN2) ||
				(instance->pdev->device ==
				PCI_DEVICE_ID_LSI_SAS0079GEN2)) &&
				(max_sectors <= MEGASAS_MAX_SECTORS)) {
				instance->max_sectors_per_req = max_sectors;
			} else {
			dev_info(&instance->pdev->dev, "max_sectors should be > 0"
				"and <= %d (or < 1MB for GEN2 controller)\n",
				instance->max_sectors_per_req);
			}
		}
	}

	host->max_sectors = instance->max_sectors_per_req;
	host->cmd_per_lun = MEGASAS_DEFAULT_CMD_PER_LUN;
	host->max_channel = MEGASAS_MAX_CHANNELS - 1;
	host->max_id = MEGASAS_MAX_DEV_PER_CHANNEL;
	host->max_lun = MEGASAS_MAX_LUN;
	host->max_cmd_len = 16;

	/* Use shared host tagset only for fusion adaptors
	 * if there are managed interrupts (smp affinity enabled case).
	 * Single msix_vectors in kdump, so shared host tag is also disabled.
	 */

	host->host_tagset = 0;
	host->nr_hw_queues = 1;

	if ((instance->adapter_type != MFI_SERIES) &&
		(instance->msix_vectors > instance->low_latency_index_start) &&
		host_tagset_enable &&
		instance->smp_affinity_enable) {
		host->host_tagset = 1;
		host->nr_hw_queues = instance->msix_vectors -
			instance->low_latency_index_start + instance->iopoll_q_count;
		if (instance->iopoll_q_count)
			host->nr_maps = 3;
	} else {
		instance->iopoll_q_count = 0;
	}

	dev_info(&instance->pdev->dev,
		"Max firmware commands: %d shared with default "
		"hw_queues = %d poll_queues %d\n", instance->max_fw_cmds,
		host->nr_hw_queues - instance->iopoll_q_count,
		instance->iopoll_q_count);
	/*
	 * Notify the mid-layer about the new controller
	 */
	if (scsi_add_host(host, &instance->pdev->dev)) {
		dev_err(&instance->pdev->dev,
			"Failed to add host from %s %d\n",
			__func__, __LINE__);
		return -ENODEV;
	}

	return 0;
}

/**
 * megasas_set_dma_mask -	Set DMA mask for supported controllers
 *
 * @instance:		Adapter soft state
 * Description:
 *
 * For Ventura, driver/FW will operate in 63bit DMA addresses.
 *
 * For invader-
 *	By default, driver/FW will operate in 32bit DMA addresses
 *	for consistent DMA mapping but if 32 bit consistent
 *	DMA mask fails, driver will try with 63 bit consistent
 *	mask provided FW is true 63bit DMA capable
 *
 * For older controllers(Thunderbolt and MFI based adapters)-
 *	driver/FW will operate in 32 bit consistent DMA addresses.
 */
static int
megasas_set_dma_mask(struct megasas_instance *instance)
{
	u64 consistent_mask;
	struct pci_dev *pdev;
	u32 scratch_pad_1;

	pdev = instance->pdev;
	consistent_mask = (instance->adapter_type >= VENTURA_SERIES) ?
				DMA_BIT_MASK(63) : DMA_BIT_MASK(32);

	if (IS_DMA64) {
		if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(63)) &&
		    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)))
			goto fail_set_dma_mask;

		if ((*pdev->dev.dma_mask == DMA_BIT_MASK(63)) &&
		    (dma_set_coherent_mask(&pdev->dev, consistent_mask) &&
		     dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)))) {
			/*
			 * If 32 bit DMA mask fails, then try for 64 bit mask
			 * for FW capable of handling 64 bit DMA.
			 */
			scratch_pad_1 = megasas_readl
				(instance, &instance->reg_set->outbound_scratch_pad_1);

			if (!(scratch_pad_1 & MR_CAN_HANDLE_64_BIT_DMA_OFFSET))
				goto fail_set_dma_mask;
			else if (dma_set_mask_and_coherent(&pdev->dev,
							   DMA_BIT_MASK(63)))
				goto fail_set_dma_mask;
		}
	} else if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)))
		goto fail_set_dma_mask;

	if (pdev->dev.coherent_dma_mask == DMA_BIT_MASK(32))
		instance->consistent_mask_64bit = false;
	else
		instance->consistent_mask_64bit = true;

	dev_info(&pdev->dev, "%s bit DMA mask and %s bit consistent mask\n",
		 ((*pdev->dev.dma_mask == DMA_BIT_MASK(63)) ? "63" : "32"),
		 (instance->consistent_mask_64bit ? "63" : "32"));

	return 0;

fail_set_dma_mask:
	dev_err(&pdev->dev, "Failed to set DMA mask\n");
	return -1;

}

/*
 * megasas_set_adapter_type -	Set adapter type.
 *				Supported controllers can be divided in
 *				different categories-
 *					enum MR_ADAPTER_TYPE {
 *						MFI_SERIES = 1,
 *						THUNDERBOLT_SERIES = 2,
 *						INVADER_SERIES = 3,
 *						VENTURA_SERIES = 4,
 *						AERO_SERIES = 5,
 *					};
 * @instance:			Adapter soft state
 * return:			void
 */
static inline void megasas_set_adapter_type(struct megasas_instance *instance)
{
	if ((instance->pdev->vendor == PCI_VENDOR_ID_DELL) &&
	    (instance->pdev->device == PCI_DEVICE_ID_DELL_PERC5)) {
		instance->adapter_type = MFI_SERIES;
	} else {
		switch (instance->pdev->device) {
		case PCI_DEVICE_ID_LSI_AERO_10E1:
		case PCI_DEVICE_ID_LSI_AERO_10E2:
		case PCI_DEVICE_ID_LSI_AERO_10E5:
		case PCI_DEVICE_ID_LSI_AERO_10E6:
			instance->adapter_type = AERO_SERIES;
			break;
		case PCI_DEVICE_ID_LSI_VENTURA:
		case PCI_DEVICE_ID_LSI_CRUSADER:
		case PCI_DEVICE_ID_LSI_HARPOON:
		case PCI_DEVICE_ID_LSI_TOMCAT:
		case PCI_DEVICE_ID_LSI_VENTURA_4PORT:
		case PCI_DEVICE_ID_LSI_CRUSADER_4PORT:
			instance->adapter_type = VENTURA_SERIES;
			break;
		case PCI_DEVICE_ID_LSI_FUSION:
		case PCI_DEVICE_ID_LSI_PLASMA:
			instance->adapter_type = THUNDERBOLT_SERIES;
			break;
		case PCI_DEVICE_ID_LSI_INVADER:
		case PCI_DEVICE_ID_LSI_INTRUDER:
		case PCI_DEVICE_ID_LSI_INTRUDER_24:
		case PCI_DEVICE_ID_LSI_CUTLASS_52:
		case PCI_DEVICE_ID_LSI_CUTLASS_53:
		case PCI_DEVICE_ID_LSI_FURY:
			instance->adapter_type = INVADER_SERIES;
			break;
		default: /* For all other supported controllers */
			instance->adapter_type = MFI_SERIES;
			break;
		}
	}
}

static inline int megasas_alloc_mfi_ctrl_mem(struct megasas_instance *instance)
{
	instance->producer = dma_alloc_coherent(&instance->pdev->dev,
			sizeof(u32), &instance->producer_h, GFP_KERNEL);
	instance->consumer = dma_alloc_coherent(&instance->pdev->dev,
			sizeof(u32), &instance->consumer_h, GFP_KERNEL);

	if (!instance->producer || !instance->consumer) {
		dev_err(&instance->pdev->dev,
			"Failed to allocate memory for producer, consumer\n");
		return -1;
	}

	*instance->producer = 0;
	*instance->consumer = 0;
	return 0;
}

/**
 * megasas_alloc_ctrl_mem -	Allocate per controller memory for core data
 *				structures which are not common across MFI
 *				adapters and fusion adapters.
 *				For MFI based adapters, allocate producer and
 *				consumer buffers. For fusion adapters, allocate
 *				memory for fusion context.
 * @instance:			Adapter soft state
 * return:			0 for SUCCESS
 */
static int megasas_alloc_ctrl_mem(struct megasas_instance *instance)
{
	instance->reply_map = kcalloc(nr_cpu_ids, sizeof(unsigned int),
				      GFP_KERNEL);
	if (!instance->reply_map)
		return -ENOMEM;

	switch (instance->adapter_type) {
	case MFI_SERIES:
		if (megasas_alloc_mfi_ctrl_mem(instance))
			return -ENOMEM;
		break;
	case AERO_SERIES:
	case VENTURA_SERIES:
	case THUNDERBOLT_SERIES:
	case INVADER_SERIES:
		if (megasas_alloc_fusion_context(instance))
			return -ENOMEM;
		break;
	}

	return 0;
}

/*
 * megasas_free_ctrl_mem -	Free fusion context for fusion adapters and
 *				producer, consumer buffers for MFI adapters
 *
 * @instance -			Adapter soft instance
 *
 */
static inline void megasas_free_ctrl_mem(struct megasas_instance *instance)
{
	kfree(instance->reply_map);
	if (instance->adapter_type == MFI_SERIES) {
		if (instance->producer)
			dma_free_coherent(&instance->pdev->dev, sizeof(u32),
					    instance->producer,
					    instance->producer_h);
		if (instance->consumer)
			dma_free_coherent(&instance->pdev->dev, sizeof(u32),
					    instance->consumer,
					    instance->consumer_h);
	} else {
		megasas_free_fusion_context(instance);
	}
}

/**
 * megasas_alloc_ctrl_dma_buffers -	Allocate consistent DMA buffers during
 *					driver load time
 *
 * @instance:				Adapter soft instance
 *
 * @return:				O for SUCCESS
 */
static inline
int megasas_alloc_ctrl_dma_buffers(struct megasas_instance *instance)
{
	struct pci_dev *pdev = instance->pdev;
	struct fusion_context *fusion = instance->ctrl_context;

	instance->evt_detail = dma_alloc_coherent(&pdev->dev,
			sizeof(struct megasas_evt_detail),
			&instance->evt_detail_h, GFP_KERNEL);

	if (!instance->evt_detail) {
		dev_err(&instance->pdev->dev,
			"Failed to allocate event detail buffer\n");
		return -ENOMEM;
	}

	if (fusion) {
		fusion->ioc_init_request =
			dma_alloc_coherent(&pdev->dev,
					   sizeof(struct MPI2_IOC_INIT_REQUEST),
					   &fusion->ioc_init_request_phys,
					   GFP_KERNEL);

		if (!fusion->ioc_init_request) {
			dev_err(&pdev->dev,
				"Failed to allocate ioc init request\n");
			return -ENOMEM;
		}

		instance->snapdump_prop = dma_alloc_coherent(&pdev->dev,
				sizeof(struct MR_SNAPDUMP_PROPERTIES),
				&instance->snapdump_prop_h, GFP_KERNEL);

		if (!instance->snapdump_prop)
			dev_err(&pdev->dev,
				"Failed to allocate snapdump properties buffer\n");

		instance->host_device_list_buf = dma_alloc_coherent(&pdev->dev,
							HOST_DEVICE_LIST_SZ,
							&instance->host_device_list_buf_h,
							GFP_KERNEL);

		if (!instance->host_device_list_buf) {
			dev_err(&pdev->dev,
				"Failed to allocate targetid list buffer\n");
			return -ENOMEM;
		}

	}

	instance->pd_list_buf =
		dma_alloc_coherent(&pdev->dev,
				     MEGASAS_MAX_PD * sizeof(struct MR_PD_LIST),
				     &instance->pd_list_buf_h, GFP_KERNEL);

	if (!instance->pd_list_buf) {
		dev_err(&pdev->dev, "Failed to allocate PD list buffer\n");
		return -ENOMEM;
	}

	instance->ctrl_info_buf =
		dma_alloc_coherent(&pdev->dev,
				     sizeof(struct megasas_ctrl_info),
				     &instance->ctrl_info_buf_h, GFP_KERNEL);

	if (!instance->ctrl_info_buf) {
		dev_err(&pdev->dev,
			"Failed to allocate controller info buffer\n");
		return -ENOMEM;
	}

	instance->ld_list_buf =
		dma_alloc_coherent(&pdev->dev,
				     sizeof(struct MR_LD_LIST),
				     &instance->ld_list_buf_h, GFP_KERNEL);

	if (!instance->ld_list_buf) {
		dev_err(&pdev->dev, "Failed to allocate LD list buffer\n");
		return -ENOMEM;
	}

	instance->ld_targetid_list_buf =
		dma_alloc_coherent(&pdev->dev,
				sizeof(struct MR_LD_TARGETID_LIST),
				&instance->ld_targetid_list_buf_h, GFP_KERNEL);

	if (!instance->ld_targetid_list_buf) {
		dev_err(&pdev->dev,
			"Failed to allocate LD targetid list buffer\n");
		return -ENOMEM;
	}

	if (!reset_devices) {
		instance->system_info_buf =
			dma_alloc_coherent(&pdev->dev,
					sizeof(struct MR_DRV_SYSTEM_INFO),
					&instance->system_info_h, GFP_KERNEL);
		instance->pd_info =
			dma_alloc_coherent(&pdev->dev,
					sizeof(struct MR_PD_INFO),
					&instance->pd_info_h, GFP_KERNEL);
		instance->tgt_prop =
			dma_alloc_coherent(&pdev->dev,
					sizeof(struct MR_TARGET_PROPERTIES),
					&instance->tgt_prop_h, GFP_KERNEL);
		instance->crash_dump_buf =
			dma_alloc_coherent(&pdev->dev, CRASH_DMA_BUF_SIZE,
					&instance->crash_dump_h, GFP_KERNEL);

		if (!instance->system_info_buf)
			dev_err(&instance->pdev->dev,
				"Failed to allocate system info buffer\n");

		if (!instance->pd_info)
			dev_err(&instance->pdev->dev,
				"Failed to allocate pd_info buffer\n");

		if (!instance->tgt_prop)
			dev_err(&instance->pdev->dev,
				"Failed to allocate tgt_prop buffer\n");

		if (!instance->crash_dump_buf)
			dev_err(&instance->pdev->dev,
				"Failed to allocate crash dump buffer\n");
	}

	return 0;
}

/*
 * megasas_free_ctrl_dma_buffers -	Free consistent DMA buffers allocated
 *					during driver load time
 *
 * @instance-				Adapter soft instance
 *
 */
static inline
void megasas_free_ctrl_dma_buffers(struct megasas_instance *instance)
{
	struct pci_dev *pdev = instance->pdev;
	struct fusion_context *fusion = instance->ctrl_context;

	if (instance->evt_detail)
		dma_free_coherent(&pdev->dev, sizeof(struct megasas_evt_detail),
				    instance->evt_detail,
				    instance->evt_detail_h);

	if (fusion && fusion->ioc_init_request)
		dma_free_coherent(&pdev->dev,
				  sizeof(struct MPI2_IOC_INIT_REQUEST),
				  fusion->ioc_init_request,
				  fusion->ioc_init_request_phys);

	if (instance->pd_list_buf)
		dma_free_coherent(&pdev->dev,
				    MEGASAS_MAX_PD * sizeof(struct MR_PD_LIST),
				    instance->pd_list_buf,
				    instance->pd_list_buf_h);

	if (instance->ld_list_buf)
		dma_free_coherent(&pdev->dev, sizeof(struct MR_LD_LIST),
				    instance->ld_list_buf,
				    instance->ld_list_buf_h);

	if (instance->ld_targetid_list_buf)
		dma_free_coherent(&pdev->dev, sizeof(struct MR_LD_TARGETID_LIST),
				    instance->ld_targetid_list_buf,
				    instance->ld_targetid_list_buf_h);

	if (instance->ctrl_info_buf)
		dma_free_coherent(&pdev->dev, sizeof(struct megasas_ctrl_info),
				    instance->ctrl_info_buf,
				    instance->ctrl_info_buf_h);

	if (instance->system_info_buf)
		dma_free_coherent(&pdev->dev, sizeof(struct MR_DRV_SYSTEM_INFO),
				    instance->system_info_buf,
				    instance->system_info_h);

	if (instance->pd_info)
		dma_free_coherent(&pdev->dev, sizeof(struct MR_PD_INFO),
				    instance->pd_info, instance->pd_info_h);

	if (instance->tgt_prop)
		dma_free_coherent(&pdev->dev, sizeof(struct MR_TARGET_PROPERTIES),
				    instance->tgt_prop, instance->tgt_prop_h);

	if (instance->crash_dump_buf)
		dma_free_coherent(&pdev->dev, CRASH_DMA_BUF_SIZE,
				    instance->crash_dump_buf,
				    instance->crash_dump_h);

	if (instance->snapdump_prop)
		dma_free_coherent(&pdev->dev,
				  sizeof(struct MR_SNAPDUMP_PROPERTIES),
				  instance->snapdump_prop,
				  instance->snapdump_prop_h);

	if (instance->host_device_list_buf)
		dma_free_coherent(&pdev->dev,
				  HOST_DEVICE_LIST_SZ,
				  instance->host_device_list_buf,
				  instance->host_device_list_buf_h);

}

/*
 * megasas_init_ctrl_params -		Initialize controller's instance
 *					parameters before FW init
 * @instance -				Adapter soft instance
 * @return -				void
 */
static inline void megasas_init_ctrl_params(struct megasas_instance *instance)
{
	instance->fw_crash_state = UNAVAILABLE;

	megasas_poll_wait_aen = 0;
	instance->issuepend_done = 1;
	atomic_set(&instance->adprecovery, MEGASAS_HBA_OPERATIONAL);

	/*
	 * Initialize locks and queues
	 */
	INIT_LIST_HEAD(&instance->cmd_pool);
	INIT_LIST_HEAD(&instance->internal_reset_pending_q);

	atomic_set(&instance->fw_outstanding, 0);
	atomic64_set(&instance->total_io_count, 0);

	init_waitqueue_head(&instance->int_cmd_wait_q);
	init_waitqueue_head(&instance->abort_cmd_wait_q);

	mutex_init(&instance->crashdump_lock);
	spin_lock_init(&instance->mfi_pool_lock);
	spin_lock_init(&instance->hba_lock);
	spin_lock_init(&instance->stream_lock);
	spin_lock_init(&instance->completion_lock);

	mutex_init(&instance->reset_mutex);

	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_SAS0073SKINNY) ||
	    (instance->pdev->device == PCI_DEVICE_ID_LSI_SAS0071SKINNY))
		instance->flag_ieee = 1;

	instance->flag = 0;
	instance->unload = 1;
	instance->last_time = 0;
	instance->disableOnlineCtrlReset = 1;
	instance->UnevenSpanSupport = 0;
	instance->smp_affinity_enable = smp_affinity_enable ? true : false;
	instance->msix_load_balance = false;

	if (instance->adapter_type != MFI_SERIES)
		INIT_WORK(&instance->work_init, megasas_fusion_ocr_wq);
	else
		INIT_WORK(&instance->work_init, process_fw_state_change_wq);
}

/**
 * megasas_probe_one -	PCI hotplug entry point
 * @pdev:		PCI device structure
 * @id:			PCI ids of supported hotplugged adapter
 */
static int megasas_probe_one(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	int rval, pos;
	struct Scsi_Host *host;
	struct megasas_instance *instance;
	u16 control = 0;

	switch (pdev->device) {
	case PCI_DEVICE_ID_LSI_AERO_10E0:
	case PCI_DEVICE_ID_LSI_AERO_10E3:
	case PCI_DEVICE_ID_LSI_AERO_10E4:
	case PCI_DEVICE_ID_LSI_AERO_10E7:
		dev_err(&pdev->dev, "Adapter is in non secure mode\n");
		return 1;
	case PCI_DEVICE_ID_LSI_AERO_10E1:
	case PCI_DEVICE_ID_LSI_AERO_10E5:
		dev_info(&pdev->dev, "Adapter is in configurable secure mode\n");
		break;
	}

	/* Reset MSI-X in the kdump kernel */
	if (reset_devices) {
		pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
		if (pos) {
			pci_read_config_word(pdev, pos + PCI_MSIX_FLAGS,
					     &control);
			if (control & PCI_MSIX_FLAGS_ENABLE) {
				dev_info(&pdev->dev, "resetting MSI-X\n");
				pci_write_config_word(pdev,
						      pos + PCI_MSIX_FLAGS,
						      control &
						      ~PCI_MSIX_FLAGS_ENABLE);
			}
		}
	}

	/*
	 * PCI prepping: enable device set bus mastering and dma mask
	 */
	rval = pci_enable_device_mem(pdev);

	if (rval) {
		return rval;
	}

	pci_set_master(pdev);

	host = scsi_host_alloc(&megasas_template,
			       sizeof(struct megasas_instance));

	if (!host) {
		dev_printk(KERN_DEBUG, &pdev->dev, "scsi_host_alloc failed\n");
		goto fail_alloc_instance;
	}

	instance = (struct megasas_instance *)host->hostdata;
	memset(instance, 0, sizeof(*instance));
	atomic_set(&instance->fw_reset_no_pci_access, 0);

	/*
	 * Initialize PCI related and misc parameters
	 */
	instance->pdev = pdev;
	instance->host = host;
	instance->unique_id = pci_dev_id(pdev);
	instance->init_id = MEGASAS_DEFAULT_INIT_ID;

	megasas_set_adapter_type(instance);

	/*
	 * Initialize MFI Firmware
	 */
	if (megasas_init_fw(instance))
		goto fail_init_mfi;

	if (instance->requestorId) {
		if (instance->PlasmaFW111) {
			instance->vf_affiliation_111 =
				dma_alloc_coherent(&pdev->dev,
					sizeof(struct MR_LD_VF_AFFILIATION_111),
					&instance->vf_affiliation_111_h,
					GFP_KERNEL);
			if (!instance->vf_affiliation_111)
				dev_warn(&pdev->dev, "Can't allocate "
				       "memory for VF affiliation buffer\n");
		} else {
			instance->vf_affiliation =
				dma_alloc_coherent(&pdev->dev,
					(MAX_LOGICAL_DRIVES + 1) *
					sizeof(struct MR_LD_VF_AFFILIATION),
					&instance->vf_affiliation_h,
					GFP_KERNEL);
			if (!instance->vf_affiliation)
				dev_warn(&pdev->dev, "Can't allocate "
				       "memory for VF affiliation buffer\n");
		}
	}

	/*
	 * Store instance in PCI softstate
	 */
	pci_set_drvdata(pdev, instance);

	/*
	 * Add this controller to megasas_mgmt_info structure so that it
	 * can be exported to management applications
	 */
	megasas_mgmt_info.count++;
	megasas_mgmt_info.instance[megasas_mgmt_info.max_index] = instance;
	megasas_mgmt_info.max_index++;

	/*
	 * Register with SCSI mid-layer
	 */
	if (megasas_io_attach(instance))
		goto fail_io_attach;

	instance->unload = 0;
	/*
	 * Trigger SCSI to scan our drives
	 */
	if (!instance->enable_fw_dev_list ||
	    (instance->host_device_list_buf->count > 0))
		scsi_scan_host(host);

	/*
	 * Initiate AEN (Asynchronous Event Notification)
	 */
	if (megasas_start_aen(instance)) {
		dev_printk(KERN_DEBUG, &pdev->dev, "start aen failed\n");
		goto fail_start_aen;
	}

	megasas_setup_debugfs(instance);

	/* Get current SR-IOV LD/VF affiliation */
	if (instance->requestorId)
		megasas_get_ld_vf_affiliation(instance, 1);

	return 0;

fail_start_aen:
	instance->unload = 1;
	scsi_remove_host(instance->host);
fail_io_attach:
	megasas_mgmt_info.count--;
	megasas_mgmt_info.max_index--;
	megasas_mgmt_info.instance[megasas_mgmt_info.max_index] = NULL;

	if (instance->requestorId && !instance->skip_heartbeat_timer_del)
		del_timer_sync(&instance->sriov_heartbeat_timer);

	instance->instancet->disable_intr(instance);
	megasas_destroy_irqs(instance);

	if (instance->adapter_type != MFI_SERIES)
		megasas_release_fusion(instance);
	else
		megasas_release_mfi(instance);

	if (instance->msix_vectors)
		pci_free_irq_vectors(instance->pdev);
	instance->msix_vectors = 0;

	if (instance->fw_crash_state != UNAVAILABLE)
		megasas_free_host_crash_buffer(instance);

	if (instance->adapter_type != MFI_SERIES)
		megasas_fusion_stop_watchdog(instance);
fail_init_mfi:
	scsi_host_put(host);
fail_alloc_instance:
	pci_disable_device(pdev);

	return -ENODEV;
}

/**
 * megasas_flush_cache -	Requests FW to flush all its caches
 * @instance:			Adapter soft state
 */
static void megasas_flush_cache(struct megasas_instance *instance)
{
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR)
		return;

	cmd = megasas_get_cmd(instance);

	if (!cmd)
		return;

	dcmd = &cmd->frame->dcmd;

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 0;
	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_NONE);
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = 0;
	dcmd->opcode = cpu_to_le32(MR_DCMD_CTRL_CACHE_FLUSH);
	dcmd->mbox.b[0] = MR_FLUSH_CTRL_CACHE | MR_FLUSH_DISK_CACHE;

	if (megasas_issue_blocked_cmd(instance, cmd, MFI_IO_TIMEOUT_SECS)
			!= DCMD_SUCCESS) {
		dev_err(&instance->pdev->dev,
			"return from %s %d\n", __func__, __LINE__);
		return;
	}

	megasas_return_cmd(instance, cmd);
}

/**
 * megasas_shutdown_controller -	Instructs FW to shutdown the controller
 * @instance:				Adapter soft state
 * @opcode:				Shutdown/Hibernate
 */
static void megasas_shutdown_controller(struct megasas_instance *instance,
					u32 opcode)
{
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR)
		return;

	cmd = megasas_get_cmd(instance);

	if (!cmd)
		return;

	if (instance->aen_cmd)
		megasas_issue_blocked_abort_cmd(instance,
			instance->aen_cmd, MFI_IO_TIMEOUT_SECS);
	if (instance->map_update_cmd)
		megasas_issue_blocked_abort_cmd(instance,
			instance->map_update_cmd, MFI_IO_TIMEOUT_SECS);
	if (instance->jbod_seq_cmd)
		megasas_issue_blocked_abort_cmd(instance,
			instance->jbod_seq_cmd, MFI_IO_TIMEOUT_SECS);

	dcmd = &cmd->frame->dcmd;

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 0;
	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_NONE);
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = 0;
	dcmd->opcode = cpu_to_le32(opcode);

	if (megasas_issue_blocked_cmd(instance, cmd, MFI_IO_TIMEOUT_SECS)
			!= DCMD_SUCCESS) {
		dev_err(&instance->pdev->dev,
			"return from %s %d\n", __func__, __LINE__);
		return;
	}

	megasas_return_cmd(instance, cmd);
}

/**
 * megasas_suspend -	driver suspend entry point
 * @dev:		Device structure
 */
static int __maybe_unused
megasas_suspend(struct device *dev)
{
	struct megasas_instance *instance;

	instance = dev_get_drvdata(dev);

	if (!instance)
		return 0;

	instance->unload = 1;

	dev_info(dev, "%s is called\n", __func__);

	/* Shutdown SR-IOV heartbeat timer */
	if (instance->requestorId && !instance->skip_heartbeat_timer_del)
		del_timer_sync(&instance->sriov_heartbeat_timer);

	/* Stop the FW fault detection watchdog */
	if (instance->adapter_type != MFI_SERIES)
		megasas_fusion_stop_watchdog(instance);

	megasas_flush_cache(instance);
	megasas_shutdown_controller(instance, MR_DCMD_HIBERNATE_SHUTDOWN);

	/* cancel the delayed work if this work still in queue */
	if (instance->ev != NULL) {
		struct megasas_aen_event *ev = instance->ev;
		cancel_delayed_work_sync(&ev->hotplug_work);
		instance->ev = NULL;
	}

	tasklet_kill(&instance->isr_tasklet);

	pci_set_drvdata(instance->pdev, instance);
	instance->instancet->disable_intr(instance);

	megasas_destroy_irqs(instance);

	if (instance->msix_vectors)
		pci_free_irq_vectors(instance->pdev);

	return 0;
}

/**
 * megasas_resume-      driver resume entry point
 * @dev:		Device structure
 */
static int __maybe_unused
megasas_resume(struct device *dev)
{
	int rval;
	struct Scsi_Host *host;
	struct megasas_instance *instance;
	u32 status_reg;

	instance = dev_get_drvdata(dev);

	if (!instance)
		return 0;

	host = instance->host;

	dev_info(dev, "%s is called\n", __func__);

	/*
	 * We expect the FW state to be READY
	 */

	if (megasas_transition_to_ready(instance, 0)) {
		dev_info(&instance->pdev->dev,
			 "Failed to transition controller to ready from %s!\n",
			 __func__);
		if (instance->adapter_type != MFI_SERIES) {
			status_reg =
				instance->instancet->read_fw_status_reg(instance);
			if (!(status_reg & MFI_RESET_ADAPTER) ||
				((megasas_adp_reset_wait_for_ready
				(instance, true, 0)) == FAILED))
				goto fail_ready_state;
		} else {
			atomic_set(&instance->fw_reset_no_pci_access, 1);
			instance->instancet->adp_reset
				(instance, instance->reg_set);
			atomic_set(&instance->fw_reset_no_pci_access, 0);

			/* waiting for about 30 seconds before retry */
			ssleep(30);

			if (megasas_transition_to_ready(instance, 0))
				goto fail_ready_state;
		}

		dev_info(&instance->pdev->dev,
			 "FW restarted successfully from %s!\n",
			 __func__);
	}
	if (megasas_set_dma_mask(instance))
		goto fail_set_dma_mask;

	/*
	 * Initialize MFI Firmware
	 */

	atomic_set(&instance->fw_outstanding, 0);
	atomic_set(&instance->ldio_outstanding, 0);

	/* Now re-enable MSI-X */
	if (instance->msix_vectors)
		megasas_alloc_irq_vectors(instance);

	if (!instance->msix_vectors) {
		rval = pci_alloc_irq_vectors(instance->pdev, 1, 1,
					     PCI_IRQ_INTX);
		if (rval < 0)
			goto fail_reenable_msix;
	}

	megasas_setup_reply_map(instance);

	if (instance->adapter_type != MFI_SERIES) {
		megasas_reset_reply_desc(instance);
		if (megasas_ioc_init_fusion(instance)) {
			megasas_free_cmds(instance);
			megasas_free_cmds_fusion(instance);
			goto fail_init_mfi;
		}
		if (!megasas_get_map_info(instance))
			megasas_sync_map_info(instance);
	} else {
		*instance->producer = 0;
		*instance->consumer = 0;
		if (megasas_issue_init_mfi(instance))
			goto fail_init_mfi;
	}

	if (megasas_get_ctrl_info(instance) != DCMD_SUCCESS)
		goto fail_init_mfi;

	tasklet_init(&instance->isr_tasklet, instance->instancet->tasklet,
		     (unsigned long)instance);

	if (instance->msix_vectors ?
			megasas_setup_irqs_msix(instance, 0) :
			megasas_setup_irqs_ioapic(instance))
		goto fail_init_mfi;

	if (instance->adapter_type != MFI_SERIES)
		megasas_setup_irq_poll(instance);

	/* Re-launch SR-IOV heartbeat timer */
	if (instance->requestorId) {
		if (!megasas_sriov_start_heartbeat(instance, 0))
			megasas_start_timer(instance);
		else {
			instance->skip_heartbeat_timer_del = 1;
			goto fail_init_mfi;
		}
	}

	instance->instancet->enable_intr(instance);
	megasas_setup_jbod_map(instance);
	instance->unload = 0;

	/*
	 * Initiate AEN (Asynchronous Event Notification)
	 */
	if (megasas_start_aen(instance))
		dev_err(&instance->pdev->dev, "Start AEN failed\n");

	/* Re-launch FW fault watchdog */
	if (instance->adapter_type != MFI_SERIES)
		if (megasas_fusion_start_watchdog(instance) != SUCCESS)
			goto fail_start_watchdog;

	return 0;

fail_start_watchdog:
	if (instance->requestorId && !instance->skip_heartbeat_timer_del)
		del_timer_sync(&instance->sriov_heartbeat_timer);
fail_init_mfi:
	megasas_free_ctrl_dma_buffers(instance);
	megasas_free_ctrl_mem(instance);
	scsi_host_put(host);

fail_reenable_msix:
fail_set_dma_mask:
fail_ready_state:

	return -ENODEV;
}

static inline int
megasas_wait_for_adapter_operational(struct megasas_instance *instance)
{
	int wait_time = MEGASAS_RESET_WAIT_TIME * 2;
	int i;
	u8 adp_state;

	for (i = 0; i < wait_time; i++) {
		adp_state = atomic_read(&instance->adprecovery);
		if ((adp_state == MEGASAS_HBA_OPERATIONAL) ||
		    (adp_state == MEGASAS_HW_CRITICAL_ERROR))
			break;

		if (!(i % MEGASAS_RESET_NOTICE_INTERVAL))
			dev_notice(&instance->pdev->dev, "waiting for controller reset to finish\n");

		msleep(1000);
	}

	if (adp_state != MEGASAS_HBA_OPERATIONAL) {
		dev_info(&instance->pdev->dev,
			 "%s HBA failed to become operational, adp_state %d\n",
			 __func__, adp_state);
		return 1;
	}

	return 0;
}

/**
 * megasas_detach_one -	PCI hot"un"plug entry point
 * @pdev:		PCI device structure
 */
static void megasas_detach_one(struct pci_dev *pdev)
{
	int i;
	struct Scsi_Host *host;
	struct megasas_instance *instance;
	struct fusion_context *fusion;
	size_t pd_seq_map_sz;

	instance = pci_get_drvdata(pdev);

	if (!instance)
		return;

	host = instance->host;
	fusion = instance->ctrl_context;

	/* Shutdown SR-IOV heartbeat timer */
	if (instance->requestorId && !instance->skip_heartbeat_timer_del)
		del_timer_sync(&instance->sriov_heartbeat_timer);

	/* Stop the FW fault detection watchdog */
	if (instance->adapter_type != MFI_SERIES)
		megasas_fusion_stop_watchdog(instance);

	if (instance->fw_crash_state != UNAVAILABLE)
		megasas_free_host_crash_buffer(instance);
	scsi_remove_host(instance->host);
	instance->unload = 1;

	if (megasas_wait_for_adapter_operational(instance))
		goto skip_firing_dcmds;

	megasas_flush_cache(instance);
	megasas_shutdown_controller(instance, MR_DCMD_CTRL_SHUTDOWN);

skip_firing_dcmds:
	/* cancel the delayed work if this work still in queue*/
	if (instance->ev != NULL) {
		struct megasas_aen_event *ev = instance->ev;
		cancel_delayed_work_sync(&ev->hotplug_work);
		instance->ev = NULL;
	}

	/* cancel all wait events */
	wake_up_all(&instance->int_cmd_wait_q);

	tasklet_kill(&instance->isr_tasklet);

	/*
	 * Take the instance off the instance array. Note that we will not
	 * decrement the max_index. We let this array be sparse array
	 */
	for (i = 0; i < megasas_mgmt_info.max_index; i++) {
		if (megasas_mgmt_info.instance[i] == instance) {
			megasas_mgmt_info.count--;
			megasas_mgmt_info.instance[i] = NULL;

			break;
		}
	}

	instance->instancet->disable_intr(instance);

	megasas_destroy_irqs(instance);

	if (instance->msix_vectors)
		pci_free_irq_vectors(instance->pdev);

	if (instance->adapter_type >= VENTURA_SERIES) {
		for (i = 0; i < MAX_LOGICAL_DRIVES_EXT; ++i)
			kfree(fusion->stream_detect_by_ld[i]);
		kfree(fusion->stream_detect_by_ld);
		fusion->stream_detect_by_ld = NULL;
	}


	if (instance->adapter_type != MFI_SERIES) {
		megasas_release_fusion(instance);
		pd_seq_map_sz =
			struct_size_t(struct MR_PD_CFG_SEQ_NUM_SYNC,
				      seq, MAX_PHYSICAL_DEVICES);
		for (i = 0; i < 2 ; i++) {
			if (fusion->ld_map[i])
				dma_free_coherent(&instance->pdev->dev,
						  fusion->max_map_sz,
						  fusion->ld_map[i],
						  fusion->ld_map_phys[i]);
			if (fusion->ld_drv_map[i]) {
				if (is_vmalloc_addr(fusion->ld_drv_map[i]))
					vfree(fusion->ld_drv_map[i]);
				else
					free_pages((ulong)fusion->ld_drv_map[i],
						   fusion->drv_map_pages);
			}

			if (fusion->pd_seq_sync[i])
				dma_free_coherent(&instance->pdev->dev,
					pd_seq_map_sz,
					fusion->pd_seq_sync[i],
					fusion->pd_seq_phys[i]);
		}
	} else {
		megasas_release_mfi(instance);
	}

	if (instance->vf_affiliation)
		dma_free_coherent(&pdev->dev, (MAX_LOGICAL_DRIVES + 1) *
				    sizeof(struct MR_LD_VF_AFFILIATION),
				    instance->vf_affiliation,
				    instance->vf_affiliation_h);

	if (instance->vf_affiliation_111)
		dma_free_coherent(&pdev->dev,
				    sizeof(struct MR_LD_VF_AFFILIATION_111),
				    instance->vf_affiliation_111,
				    instance->vf_affiliation_111_h);

	if (instance->hb_host_mem)
		dma_free_coherent(&pdev->dev, sizeof(struct MR_CTRL_HB_HOST_MEM),
				    instance->hb_host_mem,
				    instance->hb_host_mem_h);

	megasas_free_ctrl_dma_buffers(instance);

	megasas_free_ctrl_mem(instance);

	megasas_destroy_debugfs(instance);

	scsi_host_put(host);

	pci_disable_device(pdev);
}

/**
 * megasas_shutdown -	Shutdown entry point
 * @pdev:		PCI device structure
 */
static void megasas_shutdown(struct pci_dev *pdev)
{
	struct megasas_instance *instance = pci_get_drvdata(pdev);

	if (!instance)
		return;

	instance->unload = 1;

	if (megasas_wait_for_adapter_operational(instance))
		goto skip_firing_dcmds;

	megasas_flush_cache(instance);
	megasas_shutdown_controller(instance, MR_DCMD_CTRL_SHUTDOWN);

skip_firing_dcmds:
	instance->instancet->disable_intr(instance);
	megasas_destroy_irqs(instance);

	if (instance->msix_vectors)
		pci_free_irq_vectors(instance->pdev);
}

/*
 * megasas_mgmt_open -	char node "open" entry point
 * @inode:	char node inode
 * @filep:	char node file
 */
static int megasas_mgmt_open(struct inode *inode, struct file *filep)
{
	/*
	 * Allow only those users with admin rights
	 */
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	return 0;
}

/*
 * megasas_mgmt_fasync -	Async notifier registration from applications
 * @fd:		char node file descriptor number
 * @filep:	char node file
 * @mode:	notifier on/off
 *
 * This function adds the calling process to a driver global queue. When an
 * event occurs, SIGIO will be sent to all processes in this queue.
 */
static int megasas_mgmt_fasync(int fd, struct file *filep, int mode)
{
	int rc;

	mutex_lock(&megasas_async_queue_mutex);

	rc = fasync_helper(fd, filep, mode, &megasas_async_queue);

	mutex_unlock(&megasas_async_queue_mutex);

	if (rc >= 0) {
		/* For sanity check when we get ioctl */
		filep->private_data = filep;
		return 0;
	}

	printk(KERN_DEBUG "megasas: fasync_helper failed [%d]\n", rc);

	return rc;
}

/*
 * megasas_mgmt_poll -  char node "poll" entry point
 * @filep:	char node file
 * @wait:	Events to poll for
 */
static __poll_t megasas_mgmt_poll(struct file *file, poll_table *wait)
{
	__poll_t mask;
	unsigned long flags;

	poll_wait(file, &megasas_poll_wait, wait);
	spin_lock_irqsave(&poll_aen_lock, flags);
	if (megasas_poll_wait_aen)
		mask = (EPOLLIN | EPOLLRDNORM);
	else
		mask = 0;
	megasas_poll_wait_aen = 0;
	spin_unlock_irqrestore(&poll_aen_lock, flags);
	return mask;
}

/*
 * megasas_set_crash_dump_params_ioctl:
 *		Send CRASH_DUMP_MODE DCMD to all controllers
 * @cmd:	MFI command frame
 */

static int megasas_set_crash_dump_params_ioctl(struct megasas_cmd *cmd)
{
	struct megasas_instance *local_instance;
	int i, error = 0;
	int crash_support;

	crash_support = cmd->frame->dcmd.mbox.w[0];

	for (i = 0; i < megasas_mgmt_info.max_index; i++) {
		local_instance = megasas_mgmt_info.instance[i];
		if (local_instance && local_instance->crash_dump_drv_support) {
			if ((atomic_read(&local_instance->adprecovery) ==
				MEGASAS_HBA_OPERATIONAL) &&
				!megasas_set_crash_dump_params(local_instance,
					crash_support)) {
				local_instance->crash_dump_app_support =
					crash_support;
				dev_info(&local_instance->pdev->dev,
					"Application firmware crash "
					"dump mode set success\n");
				error = 0;
			} else {
				dev_info(&local_instance->pdev->dev,
					"Application firmware crash "
					"dump mode set failed\n");
				error = -1;
			}
		}
	}
	return error;
}

/**
 * megasas_mgmt_fw_ioctl -	Issues management ioctls to FW
 * @instance:			Adapter soft state
 * @user_ioc:			User's ioctl packet
 * @ioc:			ioctl packet
 */
static int
megasas_mgmt_fw_ioctl(struct megasas_instance *instance,
		      struct megasas_iocpacket __user * user_ioc,
		      struct megasas_iocpacket *ioc)
{
	struct megasas_sge64 *kern_sge64 = NULL;
	struct megasas_sge32 *kern_sge32 = NULL;
	struct megasas_cmd *cmd;
	void *kbuff_arr[MAX_IOCTL_SGE];
	dma_addr_t buf_handle = 0;
	int error = 0, i;
	void *sense = NULL;
	dma_addr_t sense_handle;
	void *sense_ptr;
	u32 opcode = 0;
	int ret = DCMD_SUCCESS;

	memset(kbuff_arr, 0, sizeof(kbuff_arr));

	if (ioc->sge_count > MAX_IOCTL_SGE) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "SGE count [%d] >  max limit [%d]\n",
		       ioc->sge_count, MAX_IOCTL_SGE);
		return -EINVAL;
	}

	if ((ioc->frame.hdr.cmd >= MFI_CMD_OP_COUNT) ||
	    ((ioc->frame.hdr.cmd == MFI_CMD_NVME) &&
	    !instance->support_nvme_passthru) ||
	    ((ioc->frame.hdr.cmd == MFI_CMD_TOOLBOX) &&
	    !instance->support_pci_lane_margining)) {
		dev_err(&instance->pdev->dev,
			"Received invalid ioctl command 0x%x\n",
			ioc->frame.hdr.cmd);
		return -ENOTSUPP;
	}

	cmd = megasas_get_cmd(instance);
	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to get a cmd packet\n");
		return -ENOMEM;
	}

	/*
	 * User's IOCTL packet has 2 frames (maximum). Copy those two
	 * frames into our cmd's frames. cmd->frame's context will get
	 * overwritten when we copy from user's frames. So set that value
	 * alone separately
	 */
	memcpy(cmd->frame, ioc->frame.raw, 2 * MEGAMFI_FRAME_SIZE);
	cmd->frame->hdr.context = cpu_to_le32(cmd->index);
	cmd->frame->hdr.pad_0 = 0;

	cmd->frame->hdr.flags &= (~MFI_FRAME_IEEE);

	if (instance->consistent_mask_64bit)
		cmd->frame->hdr.flags |= cpu_to_le16((MFI_FRAME_SGL64 |
				       MFI_FRAME_SENSE64));
	else
		cmd->frame->hdr.flags &= cpu_to_le16(~(MFI_FRAME_SGL64 |
					       MFI_FRAME_SENSE64));

	if (cmd->frame->hdr.cmd == MFI_CMD_DCMD)
		opcode = le32_to_cpu(cmd->frame->dcmd.opcode);

	if (opcode == MR_DCMD_CTRL_SHUTDOWN) {
		mutex_lock(&instance->reset_mutex);
		if (megasas_get_ctrl_info(instance) != DCMD_SUCCESS) {
			megasas_return_cmd(instance, cmd);
			mutex_unlock(&instance->reset_mutex);
			return -1;
		}
		mutex_unlock(&instance->reset_mutex);
	}

	if (opcode == MR_DRIVER_SET_APP_CRASHDUMP_MODE) {
		error = megasas_set_crash_dump_params_ioctl(cmd);
		megasas_return_cmd(instance, cmd);
		return error;
	}

	/*
	 * The management interface between applications and the fw uses
	 * MFI frames. E.g, RAID configuration changes, LD property changes
	 * etc are accomplishes through different kinds of MFI frames. The
	 * driver needs to care only about substituting user buffers with
	 * kernel buffers in SGLs. The location of SGL is embedded in the
	 * struct iocpacket itself.
	 */
	if (instance->consistent_mask_64bit)
		kern_sge64 = (struct megasas_sge64 *)
			((unsigned long)cmd->frame + ioc->sgl_off);
	else
		kern_sge32 = (struct megasas_sge32 *)
			((unsigned long)cmd->frame + ioc->sgl_off);

	/*
	 * For each user buffer, create a mirror buffer and copy in
	 */
	for (i = 0; i < ioc->sge_count; i++) {
		if (!ioc->sgl[i].iov_len)
			continue;

		kbuff_arr[i] = dma_alloc_coherent(&instance->pdev->dev,
						    ioc->sgl[i].iov_len,
						    &buf_handle, GFP_KERNEL);
		if (!kbuff_arr[i]) {
			dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to alloc "
			       "kernel SGL buffer for IOCTL\n");
			error = -ENOMEM;
			goto out;
		}

		/*
		 * We don't change the dma_coherent_mask, so
		 * dma_alloc_coherent only returns 32bit addresses
		 */
		if (instance->consistent_mask_64bit) {
			kern_sge64[i].phys_addr = cpu_to_le64(buf_handle);
			kern_sge64[i].length = cpu_to_le32(ioc->sgl[i].iov_len);
		} else {
			kern_sge32[i].phys_addr = cpu_to_le32(buf_handle);
			kern_sge32[i].length = cpu_to_le32(ioc->sgl[i].iov_len);
		}

		/*
		 * We created a kernel buffer corresponding to the
		 * user buffer. Now copy in from the user buffer
		 */
		if (copy_from_user(kbuff_arr[i], ioc->sgl[i].iov_base,
				   (u32) (ioc->sgl[i].iov_len))) {
			error = -EFAULT;
			goto out;
		}
	}

	if (ioc->sense_len) {
		/* make sure the pointer is part of the frame */
		if (ioc->sense_off >
		    (sizeof(union megasas_frame) - sizeof(__le64))) {
			error = -EINVAL;
			goto out;
		}

		sense = dma_alloc_coherent(&instance->pdev->dev, ioc->sense_len,
					     &sense_handle, GFP_KERNEL);
		if (!sense) {
			error = -ENOMEM;
			goto out;
		}

		/* always store 64 bits regardless of addressing */
		sense_ptr = (void *)cmd->frame + ioc->sense_off;
		put_unaligned_le64(sense_handle, sense_ptr);
	}

	/*
	 * Set the sync_cmd flag so that the ISR knows not to complete this
	 * cmd to the SCSI mid-layer
	 */
	cmd->sync_cmd = 1;

	ret = megasas_issue_blocked_cmd(instance, cmd, 0);
	switch (ret) {
	case DCMD_INIT:
	case DCMD_BUSY:
		cmd->sync_cmd = 0;
		dev_err(&instance->pdev->dev,
			"return -EBUSY from %s %d cmd 0x%x opcode 0x%x cmd->cmd_status_drv 0x%x\n",
			 __func__, __LINE__, cmd->frame->hdr.cmd, opcode,
			 cmd->cmd_status_drv);
		error = -EBUSY;
		goto out;
	}

	cmd->sync_cmd = 0;

	if (instance->unload == 1) {
		dev_info(&instance->pdev->dev, "Driver unload is in progress "
			"don't submit data to application\n");
		goto out;
	}
	/*
	 * copy out the kernel buffers to user buffers
	 */
	for (i = 0; i < ioc->sge_count; i++) {
		if (copy_to_user(ioc->sgl[i].iov_base, kbuff_arr[i],
				 ioc->sgl[i].iov_len)) {
			error = -EFAULT;
			goto out;
		}
	}

	/*
	 * copy out the sense
	 */
	if (ioc->sense_len) {
		void __user *uptr;
		/*
		 * sense_ptr points to the location that has the user
		 * sense buffer address
		 */
		sense_ptr = (void *)ioc->frame.raw + ioc->sense_off;
		if (in_compat_syscall())
			uptr = compat_ptr(get_unaligned((compat_uptr_t *)
							sense_ptr));
		else
			uptr = get_unaligned((void __user **)sense_ptr);

		if (copy_to_user(uptr, sense, ioc->sense_len)) {
			dev_err(&instance->pdev->dev, "Failed to copy out to user "
					"sense data\n");
			error = -EFAULT;
			goto out;
		}
	}

	/*
	 * copy the status codes returned by the fw
	 */
	if (copy_to_user(&user_ioc->frame.hdr.cmd_status,
			 &cmd->frame->hdr.cmd_status, sizeof(u8))) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Error copying out cmd_status\n");
		error = -EFAULT;
	}

out:
	if (sense) {
		dma_free_coherent(&instance->pdev->dev, ioc->sense_len,
				    sense, sense_handle);
	}

	for (i = 0; i < ioc->sge_count; i++) {
		if (kbuff_arr[i]) {
			if (instance->consistent_mask_64bit)
				dma_free_coherent(&instance->pdev->dev,
					le32_to_cpu(kern_sge64[i].length),
					kbuff_arr[i],
					le64_to_cpu(kern_sge64[i].phys_addr));
			else
				dma_free_coherent(&instance->pdev->dev,
					le32_to_cpu(kern_sge32[i].length),
					kbuff_arr[i],
					le32_to_cpu(kern_sge32[i].phys_addr));
			kbuff_arr[i] = NULL;
		}
	}

	megasas_return_cmd(instance, cmd);
	return error;
}

static struct megasas_iocpacket *
megasas_compat_iocpacket_get_user(void __user *arg)
{
	struct megasas_iocpacket *ioc;
	struct compat_megasas_iocpacket __user *cioc = arg;
	size_t size;
	int err = -EFAULT;
	int i;

	ioc = kzalloc(sizeof(*ioc), GFP_KERNEL);
	if (!ioc)
		return ERR_PTR(-ENOMEM);
	size = offsetof(struct megasas_iocpacket, frame) + sizeof(ioc->frame);
	if (copy_from_user(ioc, arg, size))
		goto out;

	for (i = 0; i < MAX_IOCTL_SGE; i++) {
		compat_uptr_t iov_base;

		if (get_user(iov_base, &cioc->sgl[i].iov_base) ||
		    get_user(ioc->sgl[i].iov_len, &cioc->sgl[i].iov_len))
			goto out;

		ioc->sgl[i].iov_base = compat_ptr(iov_base);
	}

	return ioc;
out:
	kfree(ioc);
	return ERR_PTR(err);
}

static int megasas_mgmt_ioctl_fw(struct file *file, unsigned long arg)
{
	struct megasas_iocpacket __user *user_ioc =
	    (struct megasas_iocpacket __user *)arg;
	struct megasas_iocpacket *ioc;
	struct megasas_instance *instance;
	int error;

	if (in_compat_syscall())
		ioc = megasas_compat_iocpacket_get_user(user_ioc);
	else
		ioc = memdup_user(user_ioc, sizeof(struct megasas_iocpacket));

	if (IS_ERR(ioc))
		return PTR_ERR(ioc);

	instance = megasas_lookup_instance(ioc->host_no);
	if (!instance) {
		error = -ENODEV;
		goto out_kfree_ioc;
	}

	/* Block ioctls in VF mode */
	if (instance->requestorId && !allow_vf_ioctls) {
		error = -ENODEV;
		goto out_kfree_ioc;
	}

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_err(&instance->pdev->dev, "Controller in crit error\n");
		error = -ENODEV;
		goto out_kfree_ioc;
	}

	if (instance->unload == 1) {
		error = -ENODEV;
		goto out_kfree_ioc;
	}

	if (down_interruptible(&instance->ioctl_sem)) {
		error = -ERESTARTSYS;
		goto out_kfree_ioc;
	}

	if  (megasas_wait_for_adapter_operational(instance)) {
		error = -ENODEV;
		goto out_up;
	}

	error = megasas_mgmt_fw_ioctl(instance, user_ioc, ioc);
out_up:
	up(&instance->ioctl_sem);

out_kfree_ioc:
	kfree(ioc);
	return error;
}

static int megasas_mgmt_ioctl_aen(struct file *file, unsigned long arg)
{
	struct megasas_instance *instance;
	struct megasas_aen aen;
	int error;

	if (file->private_data != file) {
		printk(KERN_DEBUG "megasas: fasync_helper was not "
		       "called first\n");
		return -EINVAL;
	}

	if (copy_from_user(&aen, (void __user *)arg, sizeof(aen)))
		return -EFAULT;

	instance = megasas_lookup_instance(aen.host_no);

	if (!instance)
		return -ENODEV;

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		return -ENODEV;
	}

	if (instance->unload == 1) {
		return -ENODEV;
	}

	if  (megasas_wait_for_adapter_operational(instance))
		return -ENODEV;

	mutex_lock(&instance->reset_mutex);
	error = megasas_register_aen(instance, aen.seq_num,
				     aen.class_locale_word);
	mutex_unlock(&instance->reset_mutex);
	return error;
}

/**
 * megasas_mgmt_ioctl -	char node ioctl entry point
 * @file:	char device file pointer
 * @cmd:	ioctl command
 * @arg:	ioctl command arguments address
 */
static long
megasas_mgmt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case MEGASAS_IOC_FIRMWARE:
		return megasas_mgmt_ioctl_fw(file, arg);

	case MEGASAS_IOC_GET_AEN:
		return megasas_mgmt_ioctl_aen(file, arg);
	}

	return -ENOTTY;
}

#ifdef CONFIG_COMPAT
static long
megasas_mgmt_compat_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	switch (cmd) {
	case MEGASAS_IOC_FIRMWARE32:
		return megasas_mgmt_ioctl_fw(file, arg);
	case MEGASAS_IOC_GET_AEN:
		return megasas_mgmt_ioctl_aen(file, arg);
	}

	return -ENOTTY;
}
#endif

/*
 * File operations structure for management interface
 */
static const struct file_operations megasas_mgmt_fops = {
	.owner = THIS_MODULE,
	.open = megasas_mgmt_open,
	.fasync = megasas_mgmt_fasync,
	.unlocked_ioctl = megasas_mgmt_ioctl,
	.poll = megasas_mgmt_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = megasas_mgmt_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static SIMPLE_DEV_PM_OPS(megasas_pm_ops, megasas_suspend, megasas_resume);

/*
 * PCI hotplug support registration structure
 */
static struct pci_driver megasas_pci_driver = {

	.name = "megaraid_sas",
	.id_table = megasas_pci_table,
	.probe = megasas_probe_one,
	.remove = megasas_detach_one,
	.driver.pm = &megasas_pm_ops,
	.shutdown = megasas_shutdown,
};

/*
 * Sysfs driver attributes
 */
static ssize_t version_show(struct device_driver *dd, char *buf)
{
	return snprintf(buf, strlen(MEGASAS_VERSION) + 2, "%s\n",
			MEGASAS_VERSION);
}
static DRIVER_ATTR_RO(version);

static ssize_t release_date_show(struct device_driver *dd, char *buf)
{
	return snprintf(buf, strlen(MEGASAS_RELDATE) + 2, "%s\n",
		MEGASAS_RELDATE);
}
static DRIVER_ATTR_RO(release_date);

static ssize_t support_poll_for_event_show(struct device_driver *dd, char *buf)
{
	return sprintf(buf, "%u\n", support_poll_for_event);
}
static DRIVER_ATTR_RO(support_poll_for_event);

static ssize_t support_device_change_show(struct device_driver *dd, char *buf)
{
	return sprintf(buf, "%u\n", support_device_change);
}
static DRIVER_ATTR_RO(support_device_change);

static ssize_t dbg_lvl_show(struct device_driver *dd, char *buf)
{
	return sprintf(buf, "%u\n", megasas_dbg_lvl);
}

static ssize_t dbg_lvl_store(struct device_driver *dd, const char *buf,
			     size_t count)
{
	int retval = count;

	if (sscanf(buf, "%u", &megasas_dbg_lvl) < 1) {
		printk(KERN_ERR "megasas: could not set dbg_lvl\n");
		retval = -EINVAL;
	}
	return retval;
}
static DRIVER_ATTR_RW(dbg_lvl);

static ssize_t
support_nvme_encapsulation_show(struct device_driver *dd, char *buf)
{
	return sprintf(buf, "%u\n", support_nvme_encapsulation);
}

static DRIVER_ATTR_RO(support_nvme_encapsulation);

static ssize_t
support_pci_lane_margining_show(struct device_driver *dd, char *buf)
{
	return sprintf(buf, "%u\n", support_pci_lane_margining);
}

static DRIVER_ATTR_RO(support_pci_lane_margining);

static inline void megasas_remove_scsi_device(struct scsi_device *sdev)
{
	sdev_printk(KERN_INFO, sdev, "SCSI device is removed\n");
	scsi_remove_device(sdev);
	scsi_device_put(sdev);
}

/**
 * megasas_update_device_list -	Update the PD and LD device list from FW
 *				after an AEN event notification
 * @instance:			Adapter soft state
 * @event_type:			Indicates type of event (PD or LD event)
 *
 * @return:			Success or failure
 *
 * Issue DCMDs to Firmware to update the internal device list in driver.
 * Based on the FW support, driver sends the HOST_DEVICE_LIST or combination
 * of PD_LIST/LD_LIST_QUERY DCMDs to get the device list.
 */
static
int megasas_update_device_list(struct megasas_instance *instance,
			       int event_type)
{
	int dcmd_ret;

	if (instance->enable_fw_dev_list) {
		return megasas_host_device_list_query(instance, false);
	} else {
		if (event_type & SCAN_PD_CHANNEL) {
			dcmd_ret = megasas_get_pd_list(instance);
			if (dcmd_ret != DCMD_SUCCESS)
				return dcmd_ret;
		}

		if (event_type & SCAN_VD_CHANNEL) {
			if (!instance->requestorId ||
			megasas_get_ld_vf_affiliation(instance, 0)) {
				return megasas_ld_list_query(instance,
						MR_LD_QUERY_TYPE_EXPOSED_TO_HOST);
			}
		}
	}
	return DCMD_SUCCESS;
}

/**
 * megasas_add_remove_devices -	Add/remove devices to SCSI mid-layer
 *				after an AEN event notification
 * @instance:			Adapter soft state
 * @scan_type:			Indicates type of devices (PD/LD) to add
 * @return			void
 */
static
void megasas_add_remove_devices(struct megasas_instance *instance,
				int scan_type)
{
	int i, j;
	u16 pd_index = 0;
	u16 ld_index = 0;
	u16 channel = 0, id = 0;
	struct Scsi_Host *host;
	struct scsi_device *sdev1;
	struct MR_HOST_DEVICE_LIST *targetid_list = NULL;
	struct MR_HOST_DEVICE_LIST_ENTRY *targetid_entry = NULL;

	host = instance->host;

	if (instance->enable_fw_dev_list) {
		targetid_list = instance->host_device_list_buf;
		for (i = 0; i < targetid_list->count; i++) {
			targetid_entry = &targetid_list->host_device_list[i];
			if (targetid_entry->flags.u.bits.is_sys_pd) {
				channel = le16_to_cpu(targetid_entry->target_id) /
						MEGASAS_MAX_DEV_PER_CHANNEL;
				id = le16_to_cpu(targetid_entry->target_id) %
						MEGASAS_MAX_DEV_PER_CHANNEL;
			} else {
				channel = MEGASAS_MAX_PD_CHANNELS +
					  (le16_to_cpu(targetid_entry->target_id) /
					   MEGASAS_MAX_DEV_PER_CHANNEL);
				id = le16_to_cpu(targetid_entry->target_id) %
						MEGASAS_MAX_DEV_PER_CHANNEL;
			}
			sdev1 = scsi_device_lookup(host, channel, id, 0);
			if (!sdev1) {
				scsi_add_device(host, channel, id, 0);
			} else {
				scsi_device_put(sdev1);
			}
		}
	}

	if (scan_type & SCAN_PD_CHANNEL) {
		for (i = 0; i < MEGASAS_MAX_PD_CHANNELS; i++) {
			for (j = 0; j < MEGASAS_MAX_DEV_PER_CHANNEL; j++) {
				pd_index = i * MEGASAS_MAX_DEV_PER_CHANNEL + j;
				sdev1 = scsi_device_lookup(host, i, j, 0);
				if (instance->pd_list[pd_index].driveState ==
							MR_PD_STATE_SYSTEM) {
					if (!sdev1)
						scsi_add_device(host, i, j, 0);
					else
						scsi_device_put(sdev1);
				} else {
					if (sdev1)
						megasas_remove_scsi_device(sdev1);
				}
			}
		}
	}

	if (scan_type & SCAN_VD_CHANNEL) {
		for (i = 0; i < MEGASAS_MAX_LD_CHANNELS; i++) {
			for (j = 0; j < MEGASAS_MAX_DEV_PER_CHANNEL; j++) {
				ld_index = (i * MEGASAS_MAX_DEV_PER_CHANNEL) + j;
				sdev1 = scsi_device_lookup(host,
						MEGASAS_MAX_PD_CHANNELS + i, j, 0);
				if (instance->ld_ids[ld_index] != 0xff) {
					if (!sdev1)
						scsi_add_device(host, MEGASAS_MAX_PD_CHANNELS + i, j, 0);
					else
						scsi_device_put(sdev1);
				} else {
					if (sdev1)
						megasas_remove_scsi_device(sdev1);
				}
			}
		}
	}

}

static void
megasas_aen_polling(struct work_struct *work)
{
	struct megasas_aen_event *ev =
		container_of(work, struct megasas_aen_event, hotplug_work.work);
	struct megasas_instance *instance = ev->instance;
	union megasas_evt_class_locale class_locale;
	int event_type = 0;
	u32 seq_num;
	u16 ld_target_id;
	int error;
	u8  dcmd_ret = DCMD_SUCCESS;
	struct scsi_device *sdev1;

	if (!instance) {
		printk(KERN_ERR "invalid instance!\n");
		kfree(ev);
		return;
	}

	/* Don't run the event workqueue thread if OCR is running */
	mutex_lock(&instance->reset_mutex);

	instance->ev = NULL;
	if (instance->evt_detail) {
		megasas_decode_evt(instance);

		switch (le32_to_cpu(instance->evt_detail->code)) {

		case MR_EVT_PD_INSERTED:
		case MR_EVT_PD_REMOVED:
			event_type = SCAN_PD_CHANNEL;
			break;

		case MR_EVT_LD_OFFLINE:
		case MR_EVT_LD_DELETED:
			ld_target_id = instance->evt_detail->args.ld.target_id;
			sdev1 = scsi_device_lookup(instance->host,
						   MEGASAS_MAX_PD_CHANNELS +
						   (ld_target_id / MEGASAS_MAX_DEV_PER_CHANNEL),
						   (ld_target_id % MEGASAS_MAX_DEV_PER_CHANNEL),
						   0);
			if (sdev1) {
				mutex_unlock(&instance->reset_mutex);
				megasas_remove_scsi_device(sdev1);
				mutex_lock(&instance->reset_mutex);
			}

			event_type = SCAN_VD_CHANNEL;
			break;
		case MR_EVT_LD_CREATED:
			event_type = SCAN_VD_CHANNEL;
			break;

		case MR_EVT_CFG_CLEARED:
		case MR_EVT_CTRL_HOST_BUS_SCAN_REQUESTED:
		case MR_EVT_FOREIGN_CFG_IMPORTED:
		case MR_EVT_LD_STATE_CHANGE:
			event_type = SCAN_PD_CHANNEL | SCAN_VD_CHANNEL;
			dev_info(&instance->pdev->dev, "scanning for scsi%d...\n",
				instance->host->host_no);
			break;

		case MR_EVT_CTRL_PROP_CHANGED:
			dcmd_ret = megasas_get_ctrl_info(instance);
			if (dcmd_ret == DCMD_SUCCESS &&
			    instance->snapdump_wait_time) {
				megasas_get_snapdump_properties(instance);
				dev_info(&instance->pdev->dev,
					 "Snap dump wait time\t: %d\n",
					 instance->snapdump_wait_time);
			}
			break;
		default:
			event_type = 0;
			break;
		}
	} else {
		dev_err(&instance->pdev->dev, "invalid evt_detail!\n");
		mutex_unlock(&instance->reset_mutex);
		kfree(ev);
		return;
	}

	if (event_type)
		dcmd_ret = megasas_update_device_list(instance, event_type);

	mutex_unlock(&instance->reset_mutex);

	if (event_type && dcmd_ret == DCMD_SUCCESS)
		megasas_add_remove_devices(instance, event_type);

	if (dcmd_ret == DCMD_SUCCESS)
		seq_num = le32_to_cpu(instance->evt_detail->seq_num) + 1;
	else
		seq_num = instance->last_seq_num;

	/* Register AEN with FW for latest sequence number plus 1 */
	class_locale.members.reserved = 0;
	class_locale.members.locale = MR_EVT_LOCALE_ALL;
	class_locale.members.class = MR_EVT_CLASS_DEBUG;

	if (instance->aen_cmd != NULL) {
		kfree(ev);
		return;
	}

	mutex_lock(&instance->reset_mutex);
	error = megasas_register_aen(instance, seq_num,
					class_locale.word);
	if (error)
		dev_err(&instance->pdev->dev,
			"register aen failed error %x\n", error);

	mutex_unlock(&instance->reset_mutex);
	kfree(ev);
}

/**
 * megasas_init - Driver load entry point
 */
static int __init megasas_init(void)
{
	int rval;

	/*
	 * Booted in kdump kernel, minimize memory footprints by
	 * disabling few features
	 */
	if (reset_devices) {
		msix_vectors = 1;
		rdpq_enable = 0;
		dual_qdepth_disable = 1;
		poll_queues = 0;
	}

	/*
	 * Announce driver version and other information
	 */
	pr_info("megasas: %s\n", MEGASAS_VERSION);

	megasas_dbg_lvl = 0;
	support_poll_for_event = 2;
	support_device_change = 1;
	support_nvme_encapsulation = true;
	support_pci_lane_margining = true;

	memset(&megasas_mgmt_info, 0, sizeof(megasas_mgmt_info));

	/*
	 * Register character device node
	 */
	rval = register_chrdev(0, "megaraid_sas_ioctl", &megasas_mgmt_fops);

	if (rval < 0) {
		printk(KERN_DEBUG "megasas: failed to open device node\n");
		return rval;
	}

	megasas_mgmt_majorno = rval;

	megasas_init_debugfs();

	/*
	 * Register ourselves as PCI hotplug module
	 */
	rval = pci_register_driver(&megasas_pci_driver);

	if (rval) {
		printk(KERN_DEBUG "megasas: PCI hotplug registration failed \n");
		goto err_pcidrv;
	}

	if ((event_log_level < MFI_EVT_CLASS_DEBUG) ||
	    (event_log_level > MFI_EVT_CLASS_DEAD)) {
		pr_warn("megaraid_sas: provided event log level is out of range, setting it to default 2(CLASS_CRITICAL), permissible range is: -2 to 4\n");
		event_log_level = MFI_EVT_CLASS_CRITICAL;
	}

	rval = driver_create_file(&megasas_pci_driver.driver,
				  &driver_attr_version);
	if (rval)
		goto err_dcf_attr_ver;

	rval = driver_create_file(&megasas_pci_driver.driver,
				  &driver_attr_release_date);
	if (rval)
		goto err_dcf_rel_date;

	rval = driver_create_file(&megasas_pci_driver.driver,
				&driver_attr_support_poll_for_event);
	if (rval)
		goto err_dcf_support_poll_for_event;

	rval = driver_create_file(&megasas_pci_driver.driver,
				  &driver_attr_dbg_lvl);
	if (rval)
		goto err_dcf_dbg_lvl;
	rval = driver_create_file(&megasas_pci_driver.driver,
				&driver_attr_support_device_change);
	if (rval)
		goto err_dcf_support_device_change;

	rval = driver_create_file(&megasas_pci_driver.driver,
				  &driver_attr_support_nvme_encapsulation);
	if (rval)
		goto err_dcf_support_nvme_encapsulation;

	rval = driver_create_file(&megasas_pci_driver.driver,
				  &driver_attr_support_pci_lane_margining);
	if (rval)
		goto err_dcf_support_pci_lane_margining;

	return rval;

err_dcf_support_pci_lane_margining:
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_support_nvme_encapsulation);

err_dcf_support_nvme_encapsulation:
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_support_device_change);

err_dcf_support_device_change:
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_dbg_lvl);
err_dcf_dbg_lvl:
	driver_remove_file(&megasas_pci_driver.driver,
			&driver_attr_support_poll_for_event);
err_dcf_support_poll_for_event:
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_release_date);
err_dcf_rel_date:
	driver_remove_file(&megasas_pci_driver.driver, &driver_attr_version);
err_dcf_attr_ver:
	pci_unregister_driver(&megasas_pci_driver);
err_pcidrv:
	megasas_exit_debugfs();
	unregister_chrdev(megasas_mgmt_majorno, "megaraid_sas_ioctl");
	return rval;
}

/**
 * megasas_exit - Driver unload entry point
 */
static void __exit megasas_exit(void)
{
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_dbg_lvl);
	driver_remove_file(&megasas_pci_driver.driver,
			&driver_attr_support_poll_for_event);
	driver_remove_file(&megasas_pci_driver.driver,
			&driver_attr_support_device_change);
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_release_date);
	driver_remove_file(&megasas_pci_driver.driver, &driver_attr_version);
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_support_nvme_encapsulation);
	driver_remove_file(&megasas_pci_driver.driver,
			   &driver_attr_support_pci_lane_margining);

	pci_unregister_driver(&megasas_pci_driver);
	megasas_exit_debugfs();
	unregister_chrdev(megasas_mgmt_majorno, "megaraid_sas_ioctl");
}

module_init(megasas_init);
module_exit(megasas_exit);
