/*
 *    Disk Array driver for HP Smart Array controllers.
 *    (C) Copyright 2000, 2007 Hewlett-Packard Development Company, L.P.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *    02111-1307, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci-aspm.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/blkpg.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/hdreg.h>
#include <linux/spinlock.h>
#include <linux/compat.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/dma-mapping.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/completion.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <linux/cdrom.h>
#include <linux/scatterlist.h>
#include <linux/kthread.h>

#define CCISS_DRIVER_VERSION(maj,min,submin) ((maj<<16)|(min<<8)|(submin))
#define DRIVER_NAME "HP CISS Driver (v 3.6.26)"
#define DRIVER_VERSION CCISS_DRIVER_VERSION(3, 6, 26)

/* Embedded module documentation macros - see modules.h */
MODULE_AUTHOR("Hewlett-Packard Company");
MODULE_DESCRIPTION("Driver for HP Smart Array Controllers");
MODULE_SUPPORTED_DEVICE("HP Smart Array Controllers");
MODULE_VERSION("3.6.26");
MODULE_LICENSE("GPL");
static int cciss_tape_cmds = 6;
module_param(cciss_tape_cmds, int, 0644);
MODULE_PARM_DESC(cciss_tape_cmds,
	"number of commands to allocate for tape devices (default: 6)");
static int cciss_simple_mode;
module_param(cciss_simple_mode, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(cciss_simple_mode,
	"Use 'simple mode' rather than 'performant mode'");

static DEFINE_MUTEX(cciss_mutex);
static struct proc_dir_entry *proc_cciss;

#include "cciss_cmd.h"
#include "cciss.h"
#include <linux/cciss_ioctl.h>

/* define the PCI info for the cards we can control */
static const struct pci_device_id cciss_pci_device_id[] = {
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISS,  0x0E11, 0x4070},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSB, 0x0E11, 0x4080},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSB, 0x0E11, 0x4082},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSB, 0x0E11, 0x4083},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSC, 0x0E11, 0x4091},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSC, 0x0E11, 0x409A},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSC, 0x0E11, 0x409B},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSC, 0x0E11, 0x409C},
	{PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSC, 0x0E11, 0x409D},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSA,     0x103C, 0x3225},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSC,     0x103C, 0x3223},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSC,     0x103C, 0x3234},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSC,     0x103C, 0x3235},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSD,     0x103C, 0x3211},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSD,     0x103C, 0x3212},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSD,     0x103C, 0x3213},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSD,     0x103C, 0x3214},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSD,     0x103C, 0x3215},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSC,     0x103C, 0x3237},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSC,     0x103C, 0x323D},
	{0,}
};

MODULE_DEVICE_TABLE(pci, cciss_pci_device_id);

/*  board_id = Subsystem Device ID & Vendor ID
 *  product = Marketing Name for the board
 *  access = Address of the struct of function pointers
 */
static struct board_type products[] = {
	{0x40700E11, "Smart Array 5300", &SA5_access},
	{0x40800E11, "Smart Array 5i", &SA5B_access},
	{0x40820E11, "Smart Array 532", &SA5B_access},
	{0x40830E11, "Smart Array 5312", &SA5B_access},
	{0x409A0E11, "Smart Array 641", &SA5_access},
	{0x409B0E11, "Smart Array 642", &SA5_access},
	{0x409C0E11, "Smart Array 6400", &SA5_access},
	{0x409D0E11, "Smart Array 6400 EM", &SA5_access},
	{0x40910E11, "Smart Array 6i", &SA5_access},
	{0x3225103C, "Smart Array P600", &SA5_access},
	{0x3223103C, "Smart Array P800", &SA5_access},
	{0x3234103C, "Smart Array P400", &SA5_access},
	{0x3235103C, "Smart Array P400i", &SA5_access},
	{0x3211103C, "Smart Array E200i", &SA5_access},
	{0x3212103C, "Smart Array E200", &SA5_access},
	{0x3213103C, "Smart Array E200i", &SA5_access},
	{0x3214103C, "Smart Array E200i", &SA5_access},
	{0x3215103C, "Smart Array E200i", &SA5_access},
	{0x3237103C, "Smart Array E500", &SA5_access},
	{0x3223103C, "Smart Array P800", &SA5_access},
	{0x3234103C, "Smart Array P400", &SA5_access},
	{0x323D103C, "Smart Array P700m", &SA5_access},
};

/* How long to wait (in milliseconds) for board to go into simple mode */
#define MAX_CONFIG_WAIT 30000
#define MAX_IOCTL_CONFIG_WAIT 1000

/*define how many times we will try a command because of bus resets */
#define MAX_CMD_RETRIES 3

#define MAX_CTLR	32

/* Originally cciss driver only supports 8 major numbers */
#define MAX_CTLR_ORIG 	8

static ctlr_info_t *hba[MAX_CTLR];

static struct task_struct *cciss_scan_thread;
static DEFINE_MUTEX(scan_mutex);
static LIST_HEAD(scan_q);

static void do_cciss_request(struct request_queue *q);
static irqreturn_t do_cciss_intx(int irq, void *dev_id);
static irqreturn_t do_cciss_msix_intr(int irq, void *dev_id);
static int cciss_open(struct block_device *bdev, fmode_t mode);
static int cciss_unlocked_open(struct block_device *bdev, fmode_t mode);
static int cciss_release(struct gendisk *disk, fmode_t mode);
static int cciss_ioctl(struct block_device *bdev, fmode_t mode,
		       unsigned int cmd, unsigned long arg);
static int cciss_getgeo(struct block_device *bdev, struct hd_geometry *geo);

static int cciss_revalidate(struct gendisk *disk);
static int rebuild_lun_table(ctlr_info_t *h, int first_time, int via_ioctl);
static int deregister_disk(ctlr_info_t *h, int drv_index,
			   int clear_all, int via_ioctl);

static void cciss_read_capacity(ctlr_info_t *h, int logvol,
			sector_t *total_size, unsigned int *block_size);
static void cciss_read_capacity_16(ctlr_info_t *h, int logvol,
			sector_t *total_size, unsigned int *block_size);
static void cciss_geometry_inquiry(ctlr_info_t *h, int logvol,
			sector_t total_size,
			unsigned int block_size, InquiryData_struct *inq_buff,
				   drive_info_struct *drv);
static void __devinit cciss_interrupt_mode(ctlr_info_t *);
static int __devinit cciss_enter_simple_mode(struct ctlr_info *h);
static void start_io(ctlr_info_t *h);
static int sendcmd_withirq(ctlr_info_t *h, __u8 cmd, void *buff, size_t size,
			__u8 page_code, unsigned char scsi3addr[],
			int cmd_type);
static int sendcmd_withirq_core(ctlr_info_t *h, CommandList_struct *c,
	int attempt_retry);
static int process_sendcmd_error(ctlr_info_t *h, CommandList_struct *c);

static int add_to_scan_list(struct ctlr_info *h);
static int scan_thread(void *data);
static int check_for_unit_attention(ctlr_info_t *h, CommandList_struct *c);
static void cciss_hba_release(struct device *dev);
static void cciss_device_release(struct device *dev);
static void cciss_free_gendisk(ctlr_info_t *h, int drv_index);
static void cciss_free_drive_info(ctlr_info_t *h, int drv_index);
static inline u32 next_command(ctlr_info_t *h);
static int __devinit cciss_find_cfg_addrs(struct pci_dev *pdev,
	void __iomem *vaddr, u32 *cfg_base_addr, u64 *cfg_base_addr_index,
	u64 *cfg_offset);
static int __devinit cciss_pci_find_memory_BAR(struct pci_dev *pdev,
	unsigned long *memory_bar);
static inline u32 cciss_tag_discard_error_bits(ctlr_info_t *h, u32 tag);
static __devinit int write_driver_ver_to_cfgtable(
	CfgTable_struct __iomem *cfgtable);

/* performant mode helper functions */
static void  calc_bucket_map(int *bucket, int num_buckets, int nsgs,
				int *bucket_map);
static void cciss_put_controller_into_performant_mode(ctlr_info_t *h);

#ifdef CONFIG_PROC_FS
static void cciss_procinit(ctlr_info_t *h);
#else
static void cciss_procinit(ctlr_info_t *h)
{
}
#endif				/* CONFIG_PROC_FS */

#ifdef CONFIG_COMPAT
static int cciss_compat_ioctl(struct block_device *, fmode_t,
			      unsigned, unsigned long);
#endif

static const struct block_device_operations cciss_fops = {
	.owner = THIS_MODULE,
	.open = cciss_unlocked_open,
	.release = cciss_release,
	.ioctl = cciss_ioctl,
	.getgeo = cciss_getgeo,
#ifdef CONFIG_COMPAT
	.compat_ioctl = cciss_compat_ioctl,
#endif
	.revalidate_disk = cciss_revalidate,
};

/* set_performant_mode: Modify the tag for cciss performant
 * set bit 0 for pull model, bits 3-1 for block fetch
 * register number
 */
static void set_performant_mode(ctlr_info_t *h, CommandList_struct *c)
{
	if (likely(h->transMethod & CFGTBL_Trans_Performant))
		c->busaddr |= 1 | (h->blockFetchTable[c->Header.SGList] << 1);
}

/*
 * Enqueuing and dequeuing functions for cmdlists.
 */
static inline void addQ(struct list_head *list, CommandList_struct *c)
{
	list_add_tail(&c->list, list);
}

static inline void removeQ(CommandList_struct *c)
{
	/*
	 * After kexec/dump some commands might still
	 * be in flight, which the firmware will try
	 * to complete. Resetting the firmware doesn't work
	 * with old fw revisions, so we have to mark
	 * them off as 'stale' to prevent the driver from
	 * falling over.
	 */
	if (WARN_ON(list_empty(&c->list))) {
		c->cmd_type = CMD_MSG_STALE;
		return;
	}

	list_del_init(&c->list);
}

static void enqueue_cmd_and_start_io(ctlr_info_t *h,
	CommandList_struct *c)
{
	unsigned long flags;
	set_performant_mode(h, c);
	spin_lock_irqsave(&h->lock, flags);
	addQ(&h->reqQ, c);
	h->Qdepth++;
	if (h->Qdepth > h->maxQsinceinit)
		h->maxQsinceinit = h->Qdepth;
	start_io(h);
	spin_unlock_irqrestore(&h->lock, flags);
}

static void cciss_free_sg_chain_blocks(SGDescriptor_struct **cmd_sg_list,
	int nr_cmds)
{
	int i;

	if (!cmd_sg_list)
		return;
	for (i = 0; i < nr_cmds; i++) {
		kfree(cmd_sg_list[i]);
		cmd_sg_list[i] = NULL;
	}
	kfree(cmd_sg_list);
}

static SGDescriptor_struct **cciss_allocate_sg_chain_blocks(
	ctlr_info_t *h, int chainsize, int nr_cmds)
{
	int j;
	SGDescriptor_struct **cmd_sg_list;

	if (chainsize <= 0)
		return NULL;

	cmd_sg_list = kmalloc(sizeof(*cmd_sg_list) * nr_cmds, GFP_KERNEL);
	if (!cmd_sg_list)
		return NULL;

	/* Build up chain blocks for each command */
	for (j = 0; j < nr_cmds; j++) {
		/* Need a block of chainsized s/g elements. */
		cmd_sg_list[j] = kmalloc((chainsize *
			sizeof(*cmd_sg_list[j])), GFP_KERNEL);
		if (!cmd_sg_list[j]) {
			dev_err(&h->pdev->dev, "Cannot get memory "
				"for s/g chains.\n");
			goto clean;
		}
	}
	return cmd_sg_list;
clean:
	cciss_free_sg_chain_blocks(cmd_sg_list, nr_cmds);
	return NULL;
}

static void cciss_unmap_sg_chain_block(ctlr_info_t *h, CommandList_struct *c)
{
	SGDescriptor_struct *chain_sg;
	u64bit temp64;

	if (c->Header.SGTotal <= h->max_cmd_sgentries)
		return;

	chain_sg = &c->SG[h->max_cmd_sgentries - 1];
	temp64.val32.lower = chain_sg->Addr.lower;
	temp64.val32.upper = chain_sg->Addr.upper;
	pci_unmap_single(h->pdev, temp64.val, chain_sg->Len, PCI_DMA_TODEVICE);
}

static void cciss_map_sg_chain_block(ctlr_info_t *h, CommandList_struct *c,
	SGDescriptor_struct *chain_block, int len)
{
	SGDescriptor_struct *chain_sg;
	u64bit temp64;

	chain_sg = &c->SG[h->max_cmd_sgentries - 1];
	chain_sg->Ext = CCISS_SG_CHAIN;
	chain_sg->Len = len;
	temp64.val = pci_map_single(h->pdev, chain_block, len,
				PCI_DMA_TODEVICE);
	chain_sg->Addr.lower = temp64.val32.lower;
	chain_sg->Addr.upper = temp64.val32.upper;
}

#include "cciss_scsi.c"		/* For SCSI tape support */

static const char *raid_label[] = { "0", "4", "1(1+0)", "5", "5+1", "ADG",
	"UNKNOWN"
};
#define RAID_UNKNOWN (ARRAY_SIZE(raid_label)-1)

#ifdef CONFIG_PROC_FS

/*
 * Report information about this controller.
 */
#define ENG_GIG 1000000000
#define ENG_GIG_FACTOR (ENG_GIG/512)
#define ENGAGE_SCSI	"engage scsi"

static void cciss_seq_show_header(struct seq_file *seq)
{
	ctlr_info_t *h = seq->private;

	seq_printf(seq, "%s: HP %s Controller\n"
		"Board ID: 0x%08lx\n"
		"Firmware Version: %c%c%c%c\n"
		"IRQ: %d\n"
		"Logical drives: %d\n"
		"Current Q depth: %d\n"
		"Current # commands on controller: %d\n"
		"Max Q depth since init: %d\n"
		"Max # commands on controller since init: %d\n"
		"Max SG entries since init: %d\n",
		h->devname,
		h->product_name,
		(unsigned long)h->board_id,
		h->firm_ver[0], h->firm_ver[1], h->firm_ver[2],
		h->firm_ver[3], (unsigned int)h->intr[h->intr_mode],
		h->num_luns,
		h->Qdepth, h->commands_outstanding,
		h->maxQsinceinit, h->max_outstanding, h->maxSG);

#ifdef CONFIG_CISS_SCSI_TAPE
	cciss_seq_tape_report(seq, h);
#endif /* CONFIG_CISS_SCSI_TAPE */
}

static void *cciss_seq_start(struct seq_file *seq, loff_t *pos)
{
	ctlr_info_t *h = seq->private;
	unsigned long flags;

	/* prevent displaying bogus info during configuration
	 * or deconfiguration of a logical volume
	 */
	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring) {
		spin_unlock_irqrestore(&h->lock, flags);
		return ERR_PTR(-EBUSY);
	}
	h->busy_configuring = 1;
	spin_unlock_irqrestore(&h->lock, flags);

	if (*pos == 0)
		cciss_seq_show_header(seq);

	return pos;
}

static int cciss_seq_show(struct seq_file *seq, void *v)
{
	sector_t vol_sz, vol_sz_frac;
	ctlr_info_t *h = seq->private;
	unsigned ctlr = h->ctlr;
	loff_t *pos = v;
	drive_info_struct *drv = h->drv[*pos];

	if (*pos > h->highest_lun)
		return 0;

	if (drv == NULL) /* it's possible for h->drv[] to have holes. */
		return 0;

	if (drv->heads == 0)
		return 0;

	vol_sz = drv->nr_blocks;
	vol_sz_frac = sector_div(vol_sz, ENG_GIG_FACTOR);
	vol_sz_frac *= 100;
	sector_div(vol_sz_frac, ENG_GIG_FACTOR);

	if (drv->raid_level < 0 || drv->raid_level > RAID_UNKNOWN)
		drv->raid_level = RAID_UNKNOWN;
	seq_printf(seq, "cciss/c%dd%d:"
			"\t%4u.%02uGB\tRAID %s\n",
			ctlr, (int) *pos, (int)vol_sz, (int)vol_sz_frac,
			raid_label[drv->raid_level]);
	return 0;
}

static void *cciss_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	ctlr_info_t *h = seq->private;

	if (*pos > h->highest_lun)
		return NULL;
	*pos += 1;

	return pos;
}

static void cciss_seq_stop(struct seq_file *seq, void *v)
{
	ctlr_info_t *h = seq->private;

	/* Only reset h->busy_configuring if we succeeded in setting
	 * it during cciss_seq_start. */
	if (v == ERR_PTR(-EBUSY))
		return;

	h->busy_configuring = 0;
}

static const struct seq_operations cciss_seq_ops = {
	.start = cciss_seq_start,
	.show  = cciss_seq_show,
	.next  = cciss_seq_next,
	.stop  = cciss_seq_stop,
};

static int cciss_seq_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &cciss_seq_ops);
	struct seq_file *seq = file->private_data;

	if (!ret)
		seq->private = PDE(inode)->data;

	return ret;
}

static ssize_t
cciss_proc_write(struct file *file, const char __user *buf,
		 size_t length, loff_t *ppos)
{
	int err;
	char *buffer;

#ifndef CONFIG_CISS_SCSI_TAPE
	return -EINVAL;
#endif

	if (!buf || length > PAGE_SIZE - 1)
		return -EINVAL;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(buffer, buf, length))
		goto out;
	buffer[length] = '\0';

#ifdef CONFIG_CISS_SCSI_TAPE
	if (strncmp(ENGAGE_SCSI, buffer, sizeof ENGAGE_SCSI - 1) == 0) {
		struct seq_file *seq = file->private_data;
		ctlr_info_t *h = seq->private;

		err = cciss_engage_scsi(h);
		if (err == 0)
			err = length;
	} else
#endif /* CONFIG_CISS_SCSI_TAPE */
		err = -EINVAL;
	/* might be nice to have "disengage" too, but it's not
	   safely possible. (only 1 module use count, lock issues.) */

out:
	free_page((unsigned long)buffer);
	return err;
}

static const struct file_operations cciss_proc_fops = {
	.owner	 = THIS_MODULE,
	.open    = cciss_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
	.write	 = cciss_proc_write,
};

static void __devinit cciss_procinit(ctlr_info_t *h)
{
	struct proc_dir_entry *pde;

	if (proc_cciss == NULL)
		proc_cciss = proc_mkdir("driver/cciss", NULL);
	if (!proc_cciss)
		return;
	pde = proc_create_data(h->devname, S_IWUSR | S_IRUSR | S_IRGRP |
					S_IROTH, proc_cciss,
					&cciss_proc_fops, h);
}
#endif				/* CONFIG_PROC_FS */

#define MAX_PRODUCT_NAME_LEN 19

#define to_hba(n) container_of(n, struct ctlr_info, dev)
#define to_drv(n) container_of(n, drive_info_struct, dev)

/* List of controllers which cannot be hard reset on kexec with reset_devices */
static u32 unresettable_controller[] = {
	0x324a103C, /* Smart Array P712m */
	0x324b103C, /* SmartArray P711m */
	0x3223103C, /* Smart Array P800 */
	0x3234103C, /* Smart Array P400 */
	0x3235103C, /* Smart Array P400i */
	0x3211103C, /* Smart Array E200i */
	0x3212103C, /* Smart Array E200 */
	0x3213103C, /* Smart Array E200i */
	0x3214103C, /* Smart Array E200i */
	0x3215103C, /* Smart Array E200i */
	0x3237103C, /* Smart Array E500 */
	0x323D103C, /* Smart Array P700m */
	0x409C0E11, /* Smart Array 6400 */
	0x409D0E11, /* Smart Array 6400 EM */
};

/* List of controllers which cannot even be soft reset */
static u32 soft_unresettable_controller[] = {
	0x409C0E11, /* Smart Array 6400 */
	0x409D0E11, /* Smart Array 6400 EM */
};

static int ctlr_is_hard_resettable(u32 board_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(unresettable_controller); i++)
		if (unresettable_controller[i] == board_id)
			return 0;
	return 1;
}

static int ctlr_is_soft_resettable(u32 board_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(soft_unresettable_controller); i++)
		if (soft_unresettable_controller[i] == board_id)
			return 0;
	return 1;
}

static int ctlr_is_resettable(u32 board_id)
{
	return ctlr_is_hard_resettable(board_id) ||
		ctlr_is_soft_resettable(board_id);
}

static ssize_t host_show_resettable(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct ctlr_info *h = to_hba(dev);

	return snprintf(buf, 20, "%d\n", ctlr_is_resettable(h->board_id));
}
static DEVICE_ATTR(resettable, S_IRUGO, host_show_resettable, NULL);

static ssize_t host_store_rescan(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ctlr_info *h = to_hba(dev);

	add_to_scan_list(h);
	wake_up_process(cciss_scan_thread);
	wait_for_completion_interruptible(&h->scan_wait);

	return count;
}
static DEVICE_ATTR(rescan, S_IWUSR, NULL, host_store_rescan);

static ssize_t host_show_transport_mode(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ctlr_info *h = to_hba(dev);

	return snprintf(buf, 20, "%s\n",
		h->transMethod & CFGTBL_Trans_Performant ?
			"performant" : "simple");
}
static DEVICE_ATTR(transport_mode, S_IRUGO, host_show_transport_mode, NULL);

static ssize_t dev_show_unique_id(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	drive_info_struct *drv = to_drv(dev);
	struct ctlr_info *h = to_hba(drv->dev.parent);
	__u8 sn[16];
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring)
		ret = -EBUSY;
	else
		memcpy(sn, drv->serial_no, sizeof(sn));
	spin_unlock_irqrestore(&h->lock, flags);

	if (ret)
		return ret;
	else
		return snprintf(buf, 16 * 2 + 2,
				"%02X%02X%02X%02X%02X%02X%02X%02X"
				"%02X%02X%02X%02X%02X%02X%02X%02X\n",
				sn[0], sn[1], sn[2], sn[3],
				sn[4], sn[5], sn[6], sn[7],
				sn[8], sn[9], sn[10], sn[11],
				sn[12], sn[13], sn[14], sn[15]);
}
static DEVICE_ATTR(unique_id, S_IRUGO, dev_show_unique_id, NULL);

static ssize_t dev_show_vendor(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	drive_info_struct *drv = to_drv(dev);
	struct ctlr_info *h = to_hba(drv->dev.parent);
	char vendor[VENDOR_LEN + 1];
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring)
		ret = -EBUSY;
	else
		memcpy(vendor, drv->vendor, VENDOR_LEN + 1);
	spin_unlock_irqrestore(&h->lock, flags);

	if (ret)
		return ret;
	else
		return snprintf(buf, sizeof(vendor) + 1, "%s\n", drv->vendor);
}
static DEVICE_ATTR(vendor, S_IRUGO, dev_show_vendor, NULL);

static ssize_t dev_show_model(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	drive_info_struct *drv = to_drv(dev);
	struct ctlr_info *h = to_hba(drv->dev.parent);
	char model[MODEL_LEN + 1];
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring)
		ret = -EBUSY;
	else
		memcpy(model, drv->model, MODEL_LEN + 1);
	spin_unlock_irqrestore(&h->lock, flags);

	if (ret)
		return ret;
	else
		return snprintf(buf, sizeof(model) + 1, "%s\n", drv->model);
}
static DEVICE_ATTR(model, S_IRUGO, dev_show_model, NULL);

static ssize_t dev_show_rev(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	drive_info_struct *drv = to_drv(dev);
	struct ctlr_info *h = to_hba(drv->dev.parent);
	char rev[REV_LEN + 1];
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring)
		ret = -EBUSY;
	else
		memcpy(rev, drv->rev, REV_LEN + 1);
	spin_unlock_irqrestore(&h->lock, flags);

	if (ret)
		return ret;
	else
		return snprintf(buf, sizeof(rev) + 1, "%s\n", drv->rev);
}
static DEVICE_ATTR(rev, S_IRUGO, dev_show_rev, NULL);

static ssize_t cciss_show_lunid(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	drive_info_struct *drv = to_drv(dev);
	struct ctlr_info *h = to_hba(drv->dev.parent);
	unsigned long flags;
	unsigned char lunid[8];

	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -EBUSY;
	}
	if (!drv->heads) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -ENOTTY;
	}
	memcpy(lunid, drv->LunID, sizeof(lunid));
	spin_unlock_irqrestore(&h->lock, flags);
	return snprintf(buf, 20, "0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		lunid[0], lunid[1], lunid[2], lunid[3],
		lunid[4], lunid[5], lunid[6], lunid[7]);
}
static DEVICE_ATTR(lunid, S_IRUGO, cciss_show_lunid, NULL);

static ssize_t cciss_show_raid_level(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	drive_info_struct *drv = to_drv(dev);
	struct ctlr_info *h = to_hba(drv->dev.parent);
	int raid;
	unsigned long flags;

	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -EBUSY;
	}
	raid = drv->raid_level;
	spin_unlock_irqrestore(&h->lock, flags);
	if (raid < 0 || raid > RAID_UNKNOWN)
		raid = RAID_UNKNOWN;

	return snprintf(buf, strlen(raid_label[raid]) + 7, "RAID %s\n",
			raid_label[raid]);
}
static DEVICE_ATTR(raid_level, S_IRUGO, cciss_show_raid_level, NULL);

static ssize_t cciss_show_usage_count(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	drive_info_struct *drv = to_drv(dev);
	struct ctlr_info *h = to_hba(drv->dev.parent);
	unsigned long flags;
	int count;

	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -EBUSY;
	}
	count = drv->usage_count;
	spin_unlock_irqrestore(&h->lock, flags);
	return snprintf(buf, 20, "%d\n", count);
}
static DEVICE_ATTR(usage_count, S_IRUGO, cciss_show_usage_count, NULL);

static struct attribute *cciss_host_attrs[] = {
	&dev_attr_rescan.attr,
	&dev_attr_resettable.attr,
	&dev_attr_transport_mode.attr,
	NULL
};

static struct attribute_group cciss_host_attr_group = {
	.attrs = cciss_host_attrs,
};

static const struct attribute_group *cciss_host_attr_groups[] = {
	&cciss_host_attr_group,
	NULL
};

static struct device_type cciss_host_type = {
	.name		= "cciss_host",
	.groups		= cciss_host_attr_groups,
	.release	= cciss_hba_release,
};

static struct attribute *cciss_dev_attrs[] = {
	&dev_attr_unique_id.attr,
	&dev_attr_model.attr,
	&dev_attr_vendor.attr,
	&dev_attr_rev.attr,
	&dev_attr_lunid.attr,
	&dev_attr_raid_level.attr,
	&dev_attr_usage_count.attr,
	NULL
};

static struct attribute_group cciss_dev_attr_group = {
	.attrs = cciss_dev_attrs,
};

static const struct attribute_group *cciss_dev_attr_groups[] = {
	&cciss_dev_attr_group,
	NULL
};

static struct device_type cciss_dev_type = {
	.name		= "cciss_device",
	.groups		= cciss_dev_attr_groups,
	.release	= cciss_device_release,
};

static struct bus_type cciss_bus_type = {
	.name		= "cciss",
};

/*
 * cciss_hba_release is called when the reference count
 * of h->dev goes to zero.
 */
static void cciss_hba_release(struct device *dev)
{
	/*
	 * nothing to do, but need this to avoid a warning
	 * about not having a release handler from lib/kref.c.
	 */
}

/*
 * Initialize sysfs entry for each controller.  This sets up and registers
 * the 'cciss#' directory for each individual controller under
 * /sys/bus/pci/devices/<dev>/.
 */
static int cciss_create_hba_sysfs_entry(struct ctlr_info *h)
{
	device_initialize(&h->dev);
	h->dev.type = &cciss_host_type;
	h->dev.bus = &cciss_bus_type;
	dev_set_name(&h->dev, "%s", h->devname);
	h->dev.parent = &h->pdev->dev;

	return device_add(&h->dev);
}

/*
 * Remove sysfs entries for an hba.
 */
static void cciss_destroy_hba_sysfs_entry(struct ctlr_info *h)
{
	device_del(&h->dev);
	put_device(&h->dev); /* final put. */
}

/* cciss_device_release is called when the reference count
 * of h->drv[x]dev goes to zero.
 */
static void cciss_device_release(struct device *dev)
{
	drive_info_struct *drv = to_drv(dev);
	kfree(drv);
}

/*
 * Initialize sysfs for each logical drive.  This sets up and registers
 * the 'c#d#' directory for each individual logical drive under
 * /sys/bus/pci/devices/<dev/ccis#/. We also create a link from
 * /sys/block/cciss!c#d# to this entry.
 */
static long cciss_create_ld_sysfs_entry(struct ctlr_info *h,
				       int drv_index)
{
	struct device *dev;

	if (h->drv[drv_index]->device_initialized)
		return 0;

	dev = &h->drv[drv_index]->dev;
	device_initialize(dev);
	dev->type = &cciss_dev_type;
	dev->bus = &cciss_bus_type;
	dev_set_name(dev, "c%dd%d", h->ctlr, drv_index);
	dev->parent = &h->dev;
	h->drv[drv_index]->device_initialized = 1;
	return device_add(dev);
}

/*
 * Remove sysfs entries for a logical drive.
 */
static void cciss_destroy_ld_sysfs_entry(struct ctlr_info *h, int drv_index,
	int ctlr_exiting)
{
	struct device *dev = &h->drv[drv_index]->dev;

	/* special case for c*d0, we only destroy it on controller exit */
	if (drv_index == 0 && !ctlr_exiting)
		return;

	device_del(dev);
	put_device(dev); /* the "final" put. */
	h->drv[drv_index] = NULL;
}

/*
 * For operations that cannot sleep, a command block is allocated at init,
 * and managed by cmd_alloc() and cmd_free() using a simple bitmap to track
 * which ones are free or in use.
 */
static CommandList_struct *cmd_alloc(ctlr_info_t *h)
{
	CommandList_struct *c;
	int i;
	u64bit temp64;
	dma_addr_t cmd_dma_handle, err_dma_handle;

	do {
		i = find_first_zero_bit(h->cmd_pool_bits, h->nr_cmds);
		if (i == h->nr_cmds)
			return NULL;
	} while (test_and_set_bit(i & (BITS_PER_LONG - 1),
		  h->cmd_pool_bits + (i / BITS_PER_LONG)) != 0);
	c = h->cmd_pool + i;
	memset(c, 0, sizeof(CommandList_struct));
	cmd_dma_handle = h->cmd_pool_dhandle + i * sizeof(CommandList_struct);
	c->err_info = h->errinfo_pool + i;
	memset(c->err_info, 0, sizeof(ErrorInfo_struct));
	err_dma_handle = h->errinfo_pool_dhandle
	    + i * sizeof(ErrorInfo_struct);
	h->nr_allocs++;

	c->cmdindex = i;

	INIT_LIST_HEAD(&c->list);
	c->busaddr = (__u32) cmd_dma_handle;
	temp64.val = (__u64) err_dma_handle;
	c->ErrDesc.Addr.lower = temp64.val32.lower;
	c->ErrDesc.Addr.upper = temp64.val32.upper;
	c->ErrDesc.Len = sizeof(ErrorInfo_struct);

	c->ctlr = h->ctlr;
	return c;
}

/* allocate a command using pci_alloc_consistent, used for ioctls,
 * etc., not for the main i/o path.
 */
static CommandList_struct *cmd_special_alloc(ctlr_info_t *h)
{
	CommandList_struct *c;
	u64bit temp64;
	dma_addr_t cmd_dma_handle, err_dma_handle;

	c = (CommandList_struct *) pci_alloc_consistent(h->pdev,
		sizeof(CommandList_struct), &cmd_dma_handle);
	if (c == NULL)
		return NULL;
	memset(c, 0, sizeof(CommandList_struct));

	c->cmdindex = -1;

	c->err_info = (ErrorInfo_struct *)
	    pci_alloc_consistent(h->pdev, sizeof(ErrorInfo_struct),
		    &err_dma_handle);

	if (c->err_info == NULL) {
		pci_free_consistent(h->pdev,
			sizeof(CommandList_struct), c, cmd_dma_handle);
		return NULL;
	}
	memset(c->err_info, 0, sizeof(ErrorInfo_struct));

	INIT_LIST_HEAD(&c->list);
	c->busaddr = (__u32) cmd_dma_handle;
	temp64.val = (__u64) err_dma_handle;
	c->ErrDesc.Addr.lower = temp64.val32.lower;
	c->ErrDesc.Addr.upper = temp64.val32.upper;
	c->ErrDesc.Len = sizeof(ErrorInfo_struct);

	c->ctlr = h->ctlr;
	return c;
}

static void cmd_free(ctlr_info_t *h, CommandList_struct *c)
{
	int i;

	i = c - h->cmd_pool;
	clear_bit(i & (BITS_PER_LONG - 1),
		  h->cmd_pool_bits + (i / BITS_PER_LONG));
	h->nr_frees++;
}

static void cmd_special_free(ctlr_info_t *h, CommandList_struct *c)
{
	u64bit temp64;

	temp64.val32.lower = c->ErrDesc.Addr.lower;
	temp64.val32.upper = c->ErrDesc.Addr.upper;
	pci_free_consistent(h->pdev, sizeof(ErrorInfo_struct),
			    c->err_info, (dma_addr_t) temp64.val);
	pci_free_consistent(h->pdev, sizeof(CommandList_struct), c,
		(dma_addr_t) cciss_tag_discard_error_bits(h, (u32) c->busaddr));
}

static inline ctlr_info_t *get_host(struct gendisk *disk)
{
	return disk->queue->queuedata;
}

static inline drive_info_struct *get_drv(struct gendisk *disk)
{
	return disk->private_data;
}

/*
 * Open.  Make sure the device is really there.
 */
static int cciss_open(struct block_device *bdev, fmode_t mode)
{
	ctlr_info_t *h = get_host(bdev->bd_disk);
	drive_info_struct *drv = get_drv(bdev->bd_disk);

	dev_dbg(&h->pdev->dev, "cciss_open %s\n", bdev->bd_disk->disk_name);
	if (drv->busy_configuring)
		return -EBUSY;
	/*
	 * Root is allowed to open raw volume zero even if it's not configured
	 * so array config can still work. Root is also allowed to open any
	 * volume that has a LUN ID, so it can issue IOCTL to reread the
	 * disk information.  I don't think I really like this
	 * but I'm already using way to many device nodes to claim another one
	 * for "raw controller".
	 */
	if (drv->heads == 0) {
		if (MINOR(bdev->bd_dev) != 0) {	/* not node 0? */
			/* if not node 0 make sure it is a partition = 0 */
			if (MINOR(bdev->bd_dev) & 0x0f) {
				return -ENXIO;
				/* if it is, make sure we have a LUN ID */
			} else if (memcmp(drv->LunID, CTLR_LUNID,
				sizeof(drv->LunID))) {
				return -ENXIO;
			}
		}
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
	}
	drv->usage_count++;
	h->usage_count++;
	return 0;
}

static int cciss_unlocked_open(struct block_device *bdev, fmode_t mode)
{
	int ret;

	mutex_lock(&cciss_mutex);
	ret = cciss_open(bdev, mode);
	mutex_unlock(&cciss_mutex);

	return ret;
}

/*
 * Close.  Sync first.
 */
static int cciss_release(struct gendisk *disk, fmode_t mode)
{
	ctlr_info_t *h;
	drive_info_struct *drv;

	mutex_lock(&cciss_mutex);
	h = get_host(disk);
	drv = get_drv(disk);
	dev_dbg(&h->pdev->dev, "cciss_release %s\n", disk->disk_name);
	drv->usage_count--;
	h->usage_count--;
	mutex_unlock(&cciss_mutex);
	return 0;
}

#ifdef CONFIG_COMPAT

static int cciss_ioctl32_passthru(struct block_device *bdev, fmode_t mode,
				  unsigned cmd, unsigned long arg);
static int cciss_ioctl32_big_passthru(struct block_device *bdev, fmode_t mode,
				      unsigned cmd, unsigned long arg);

static int cciss_compat_ioctl(struct block_device *bdev, fmode_t mode,
			      unsigned cmd, unsigned long arg)
{
	switch (cmd) {
	case CCISS_GETPCIINFO:
	case CCISS_GETINTINFO:
	case CCISS_SETINTINFO:
	case CCISS_GETNODENAME:
	case CCISS_SETNODENAME:
	case CCISS_GETHEARTBEAT:
	case CCISS_GETBUSTYPES:
	case CCISS_GETFIRMVER:
	case CCISS_GETDRIVVER:
	case CCISS_REVALIDVOLS:
	case CCISS_DEREGDISK:
	case CCISS_REGNEWDISK:
	case CCISS_REGNEWD:
	case CCISS_RESCANDISK:
	case CCISS_GETLUNINFO:
		return cciss_ioctl(bdev, mode, cmd, arg);

	case CCISS_PASSTHRU32:
		return cciss_ioctl32_passthru(bdev, mode, cmd, arg);
	case CCISS_BIG_PASSTHRU32:
		return cciss_ioctl32_big_passthru(bdev, mode, cmd, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static int cciss_ioctl32_passthru(struct block_device *bdev, fmode_t mode,
				  unsigned cmd, unsigned long arg)
{
	IOCTL32_Command_struct __user *arg32 =
	    (IOCTL32_Command_struct __user *) arg;
	IOCTL_Command_struct arg64;
	IOCTL_Command_struct __user *p = compat_alloc_user_space(sizeof(arg64));
	int err;
	u32 cp;

	err = 0;
	err |=
	    copy_from_user(&arg64.LUN_info, &arg32->LUN_info,
			   sizeof(arg64.LUN_info));
	err |=
	    copy_from_user(&arg64.Request, &arg32->Request,
			   sizeof(arg64.Request));
	err |=
	    copy_from_user(&arg64.error_info, &arg32->error_info,
			   sizeof(arg64.error_info));
	err |= get_user(arg64.buf_size, &arg32->buf_size);
	err |= get_user(cp, &arg32->buf);
	arg64.buf = compat_ptr(cp);
	err |= copy_to_user(p, &arg64, sizeof(arg64));

	if (err)
		return -EFAULT;

	err = cciss_ioctl(bdev, mode, CCISS_PASSTHRU, (unsigned long)p);
	if (err)
		return err;
	err |=
	    copy_in_user(&arg32->error_info, &p->error_info,
			 sizeof(arg32->error_info));
	if (err)
		return -EFAULT;
	return err;
}

static int cciss_ioctl32_big_passthru(struct block_device *bdev, fmode_t mode,
				      unsigned cmd, unsigned long arg)
{
	BIG_IOCTL32_Command_struct __user *arg32 =
	    (BIG_IOCTL32_Command_struct __user *) arg;
	BIG_IOCTL_Command_struct arg64;
	BIG_IOCTL_Command_struct __user *p =
	    compat_alloc_user_space(sizeof(arg64));
	int err;
	u32 cp;

	memset(&arg64, 0, sizeof(arg64));
	err = 0;
	err |=
	    copy_from_user(&arg64.LUN_info, &arg32->LUN_info,
			   sizeof(arg64.LUN_info));
	err |=
	    copy_from_user(&arg64.Request, &arg32->Request,
			   sizeof(arg64.Request));
	err |=
	    copy_from_user(&arg64.error_info, &arg32->error_info,
			   sizeof(arg64.error_info));
	err |= get_user(arg64.buf_size, &arg32->buf_size);
	err |= get_user(arg64.malloc_size, &arg32->malloc_size);
	err |= get_user(cp, &arg32->buf);
	arg64.buf = compat_ptr(cp);
	err |= copy_to_user(p, &arg64, sizeof(arg64));

	if (err)
		return -EFAULT;

	err = cciss_ioctl(bdev, mode, CCISS_BIG_PASSTHRU, (unsigned long)p);
	if (err)
		return err;
	err |=
	    copy_in_user(&arg32->error_info, &p->error_info,
			 sizeof(arg32->error_info));
	if (err)
		return -EFAULT;
	return err;
}
#endif

static int cciss_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	drive_info_struct *drv = get_drv(bdev->bd_disk);

	if (!drv->cylinders)
		return -ENXIO;

	geo->heads = drv->heads;
	geo->sectors = drv->sectors;
	geo->cylinders = drv->cylinders;
	return 0;
}

static void check_ioctl_unit_attention(ctlr_info_t *h, CommandList_struct *c)
{
	if (c->err_info->CommandStatus == CMD_TARGET_STATUS &&
			c->err_info->ScsiStatus != SAM_STAT_CHECK_CONDITION)
		(void)check_for_unit_attention(h, c);
}

static int cciss_getpciinfo(ctlr_info_t *h, void __user *argp)
{
	cciss_pci_info_struct pciinfo;

	if (!argp)
		return -EINVAL;
	pciinfo.domain = pci_domain_nr(h->pdev->bus);
	pciinfo.bus = h->pdev->bus->number;
	pciinfo.dev_fn = h->pdev->devfn;
	pciinfo.board_id = h->board_id;
	if (copy_to_user(argp, &pciinfo, sizeof(cciss_pci_info_struct)))
		return -EFAULT;
	return 0;
}

static int cciss_getintinfo(ctlr_info_t *h, void __user *argp)
{
	cciss_coalint_struct intinfo;
	unsigned long flags;

	if (!argp)
		return -EINVAL;
	spin_lock_irqsave(&h->lock, flags);
	intinfo.delay = readl(&h->cfgtable->HostWrite.CoalIntDelay);
	intinfo.count = readl(&h->cfgtable->HostWrite.CoalIntCount);
	spin_unlock_irqrestore(&h->lock, flags);
	if (copy_to_user
	    (argp, &intinfo, sizeof(cciss_coalint_struct)))
		return -EFAULT;
	return 0;
}

static int cciss_setintinfo(ctlr_info_t *h, void __user *argp)
{
	cciss_coalint_struct intinfo;
	unsigned long flags;
	int i;

	if (!argp)
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&intinfo, argp, sizeof(intinfo)))
		return -EFAULT;
	if ((intinfo.delay == 0) && (intinfo.count == 0))
		return -EINVAL;
	spin_lock_irqsave(&h->lock, flags);
	/* Update the field, and then ring the doorbell */
	writel(intinfo.delay, &(h->cfgtable->HostWrite.CoalIntDelay));
	writel(intinfo.count, &(h->cfgtable->HostWrite.CoalIntCount));
	writel(CFGTBL_ChangeReq, h->vaddr + SA5_DOORBELL);

	for (i = 0; i < MAX_IOCTL_CONFIG_WAIT; i++) {
		if (!(readl(h->vaddr + SA5_DOORBELL) & CFGTBL_ChangeReq))
			break;
		udelay(1000); /* delay and try again */
	}
	spin_unlock_irqrestore(&h->lock, flags);
	if (i >= MAX_IOCTL_CONFIG_WAIT)
		return -EAGAIN;
	return 0;
}

static int cciss_getnodename(ctlr_info_t *h, void __user *argp)
{
	NodeName_type NodeName;
	unsigned long flags;
	int i;

	if (!argp)
		return -EINVAL;
	spin_lock_irqsave(&h->lock, flags);
	for (i = 0; i < 16; i++)
		NodeName[i] = readb(&h->cfgtable->ServerName[i]);
	spin_unlock_irqrestore(&h->lock, flags);
	if (copy_to_user(argp, NodeName, sizeof(NodeName_type)))
		return -EFAULT;
	return 0;
}

static int cciss_setnodename(ctlr_info_t *h, void __user *argp)
{
	NodeName_type NodeName;
	unsigned long flags;
	int i;

	if (!argp)
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(NodeName, argp, sizeof(NodeName_type)))
		return -EFAULT;
	spin_lock_irqsave(&h->lock, flags);
	/* Update the field, and then ring the doorbell */
	for (i = 0; i < 16; i++)
		writeb(NodeName[i], &h->cfgtable->ServerName[i]);
	writel(CFGTBL_ChangeReq, h->vaddr + SA5_DOORBELL);
	for (i = 0; i < MAX_IOCTL_CONFIG_WAIT; i++) {
		if (!(readl(h->vaddr + SA5_DOORBELL) & CFGTBL_ChangeReq))
			break;
		udelay(1000); /* delay and try again */
	}
	spin_unlock_irqrestore(&h->lock, flags);
	if (i >= MAX_IOCTL_CONFIG_WAIT)
		return -EAGAIN;
	return 0;
}

static int cciss_getheartbeat(ctlr_info_t *h, void __user *argp)
{
	Heartbeat_type heartbeat;
	unsigned long flags;

	if (!argp)
		return -EINVAL;
	spin_lock_irqsave(&h->lock, flags);
	heartbeat = readl(&h->cfgtable->HeartBeat);
	spin_unlock_irqrestore(&h->lock, flags);
	if (copy_to_user(argp, &heartbeat, sizeof(Heartbeat_type)))
		return -EFAULT;
	return 0;
}

static int cciss_getbustypes(ctlr_info_t *h, void __user *argp)
{
	BusTypes_type BusTypes;
	unsigned long flags;

	if (!argp)
		return -EINVAL;
	spin_lock_irqsave(&h->lock, flags);
	BusTypes = readl(&h->cfgtable->BusTypes);
	spin_unlock_irqrestore(&h->lock, flags);
	if (copy_to_user(argp, &BusTypes, sizeof(BusTypes_type)))
		return -EFAULT;
	return 0;
}

static int cciss_getfirmver(ctlr_info_t *h, void __user *argp)
{
	FirmwareVer_type firmware;

	if (!argp)
		return -EINVAL;
	memcpy(firmware, h->firm_ver, 4);

	if (copy_to_user
	    (argp, firmware, sizeof(FirmwareVer_type)))
		return -EFAULT;
	return 0;
}

static int cciss_getdrivver(ctlr_info_t *h, void __user *argp)
{
	DriverVer_type DriverVer = DRIVER_VERSION;

	if (!argp)
		return -EINVAL;
	if (copy_to_user(argp, &DriverVer, sizeof(DriverVer_type)))
		return -EFAULT;
	return 0;
}

static int cciss_getluninfo(ctlr_info_t *h,
	struct gendisk *disk, void __user *argp)
{
	LogvolInfo_struct luninfo;
	drive_info_struct *drv = get_drv(disk);

	if (!argp)
		return -EINVAL;
	memcpy(&luninfo.LunID, drv->LunID, sizeof(luninfo.LunID));
	luninfo.num_opens = drv->usage_count;
	luninfo.num_parts = 0;
	if (copy_to_user(argp, &luninfo, sizeof(LogvolInfo_struct)))
		return -EFAULT;
	return 0;
}

static int cciss_passthru(ctlr_info_t *h, void __user *argp)
{
	IOCTL_Command_struct iocommand;
	CommandList_struct *c;
	char *buff = NULL;
	u64bit temp64;
	DECLARE_COMPLETION_ONSTACK(wait);

	if (!argp)
		return -EINVAL;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (copy_from_user
	    (&iocommand, argp, sizeof(IOCTL_Command_struct)))
		return -EFAULT;
	if ((iocommand.buf_size < 1) &&
	    (iocommand.Request.Type.Direction != XFER_NONE)) {
		return -EINVAL;
	}
	if (iocommand.buf_size > 0) {
		buff = kmalloc(iocommand.buf_size, GFP_KERNEL);
		if (buff == NULL)
			return -EFAULT;
	}
	if (iocommand.Request.Type.Direction == XFER_WRITE) {
		/* Copy the data into the buffer we created */
		if (copy_from_user(buff, iocommand.buf, iocommand.buf_size)) {
			kfree(buff);
			return -EFAULT;
		}
	} else {
		memset(buff, 0, iocommand.buf_size);
	}
	c = cmd_special_alloc(h);
	if (!c) {
		kfree(buff);
		return -ENOMEM;
	}
	/* Fill in the command type */
	c->cmd_type = CMD_IOCTL_PEND;
	/* Fill in Command Header */
	c->Header.ReplyQueue = 0;   /* unused in simple mode */
	if (iocommand.buf_size > 0) { /* buffer to fill */
		c->Header.SGList = 1;
		c->Header.SGTotal = 1;
	} else { /* no buffers to fill */
		c->Header.SGList = 0;
		c->Header.SGTotal = 0;
	}
	c->Header.LUN = iocommand.LUN_info;
	/* use the kernel address the cmd block for tag */
	c->Header.Tag.lower = c->busaddr;

	/* Fill in Request block */
	c->Request = iocommand.Request;

	/* Fill in the scatter gather information */
	if (iocommand.buf_size > 0) {
		temp64.val = pci_map_single(h->pdev, buff,
			iocommand.buf_size, PCI_DMA_BIDIRECTIONAL);
		c->SG[0].Addr.lower = temp64.val32.lower;
		c->SG[0].Addr.upper = temp64.val32.upper;
		c->SG[0].Len = iocommand.buf_size;
		c->SG[0].Ext = 0;  /* we are not chaining */
	}
	c->waiting = &wait;

	enqueue_cmd_and_start_io(h, c);
	wait_for_completion(&wait);

	/* unlock the buffers from DMA */
	temp64.val32.lower = c->SG[0].Addr.lower;
	temp64.val32.upper = c->SG[0].Addr.upper;
	pci_unmap_single(h->pdev, (dma_addr_t) temp64.val, iocommand.buf_size,
			 PCI_DMA_BIDIRECTIONAL);
	check_ioctl_unit_attention(h, c);

	/* Copy the error information out */
	iocommand.error_info = *(c->err_info);
	if (copy_to_user(argp, &iocommand, sizeof(IOCTL_Command_struct))) {
		kfree(buff);
		cmd_special_free(h, c);
		return -EFAULT;
	}

	if (iocommand.Request.Type.Direction == XFER_READ) {
		/* Copy the data out of the buffer we created */
		if (copy_to_user(iocommand.buf, buff, iocommand.buf_size)) {
			kfree(buff);
			cmd_special_free(h, c);
			return -EFAULT;
		}
	}
	kfree(buff);
	cmd_special_free(h, c);
	return 0;
}

static int cciss_bigpassthru(ctlr_info_t *h, void __user *argp)
{
	BIG_IOCTL_Command_struct *ioc;
	CommandList_struct *c;
	unsigned char **buff = NULL;
	int *buff_size = NULL;
	u64bit temp64;
	BYTE sg_used = 0;
	int status = 0;
	int i;
	DECLARE_COMPLETION_ONSTACK(wait);
	__u32 left;
	__u32 sz;
	BYTE __user *data_ptr;

	if (!argp)
		return -EINVAL;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	ioc = kmalloc(sizeof(*ioc), GFP_KERNEL);
	if (!ioc) {
		status = -ENOMEM;
		goto cleanup1;
	}
	if (copy_from_user(ioc, argp, sizeof(*ioc))) {
		status = -EFAULT;
		goto cleanup1;
	}
	if ((ioc->buf_size < 1) &&
	    (ioc->Request.Type.Direction != XFER_NONE)) {
		status = -EINVAL;
		goto cleanup1;
	}
	/* Check kmalloc limits  using all SGs */
	if (ioc->malloc_size > MAX_KMALLOC_SIZE) {
		status = -EINVAL;
		goto cleanup1;
	}
	if (ioc->buf_size > ioc->malloc_size * MAXSGENTRIES) {
		status = -EINVAL;
		goto cleanup1;
	}
	buff = kzalloc(MAXSGENTRIES * sizeof(char *), GFP_KERNEL);
	if (!buff) {
		status = -ENOMEM;
		goto cleanup1;
	}
	buff_size = kmalloc(MAXSGENTRIES * sizeof(int), GFP_KERNEL);
	if (!buff_size) {
		status = -ENOMEM;
		goto cleanup1;
	}
	left = ioc->buf_size;
	data_ptr = ioc->buf;
	while (left) {
		sz = (left > ioc->malloc_size) ? ioc->malloc_size : left;
		buff_size[sg_used] = sz;
		buff[sg_used] = kmalloc(sz, GFP_KERNEL);
		if (buff[sg_used] == NULL) {
			status = -ENOMEM;
			goto cleanup1;
		}
		if (ioc->Request.Type.Direction == XFER_WRITE) {
			if (copy_from_user(buff[sg_used], data_ptr, sz)) {
				status = -EFAULT;
				goto cleanup1;
			}
		} else {
			memset(buff[sg_used], 0, sz);
		}
		left -= sz;
		data_ptr += sz;
		sg_used++;
	}
	c = cmd_special_alloc(h);
	if (!c) {
		status = -ENOMEM;
		goto cleanup1;
	}
	c->cmd_type = CMD_IOCTL_PEND;
	c->Header.ReplyQueue = 0;
	c->Header.SGList = sg_used;
	c->Header.SGTotal = sg_used;
	c->Header.LUN = ioc->LUN_info;
	c->Header.Tag.lower = c->busaddr;

	c->Request = ioc->Request;
	for (i = 0; i < sg_used; i++) {
		temp64.val = pci_map_single(h->pdev, buff[i], buff_size[i],
				    PCI_DMA_BIDIRECTIONAL);
		c->SG[i].Addr.lower = temp64.val32.lower;
		c->SG[i].Addr.upper = temp64.val32.upper;
		c->SG[i].Len = buff_size[i];
		c->SG[i].Ext = 0;	/* we are not chaining */
	}
	c->waiting = &wait;
	enqueue_cmd_and_start_io(h, c);
	wait_for_completion(&wait);
	/* unlock the buffers from DMA */
	for (i = 0; i < sg_used; i++) {
		temp64.val32.lower = c->SG[i].Addr.lower;
		temp64.val32.upper = c->SG[i].Addr.upper;
		pci_unmap_single(h->pdev,
			(dma_addr_t) temp64.val, buff_size[i],
			PCI_DMA_BIDIRECTIONAL);
	}
	check_ioctl_unit_attention(h, c);
	/* Copy the error information out */
	ioc->error_info = *(c->err_info);
	if (copy_to_user(argp, ioc, sizeof(*ioc))) {
		cmd_special_free(h, c);
		status = -EFAULT;
		goto cleanup1;
	}
	if (ioc->Request.Type.Direction == XFER_READ) {
		/* Copy the data out of the buffer we created */
		BYTE __user *ptr = ioc->buf;
		for (i = 0; i < sg_used; i++) {
			if (copy_to_user(ptr, buff[i], buff_size[i])) {
				cmd_special_free(h, c);
				status = -EFAULT;
				goto cleanup1;
			}
			ptr += buff_size[i];
		}
	}
	cmd_special_free(h, c);
	status = 0;
cleanup1:
	if (buff) {
		for (i = 0; i < sg_used; i++)
			kfree(buff[i]);
		kfree(buff);
	}
	kfree(buff_size);
	kfree(ioc);
	return status;
}

static int cciss_ioctl(struct block_device *bdev, fmode_t mode,
	unsigned int cmd, unsigned long arg)
{
	struct gendisk *disk = bdev->bd_disk;
	ctlr_info_t *h = get_host(disk);
	void __user *argp = (void __user *)arg;

	dev_dbg(&h->pdev->dev, "cciss_ioctl: Called with cmd=%x %lx\n",
		cmd, arg);
	switch (cmd) {
	case CCISS_GETPCIINFO:
		return cciss_getpciinfo(h, argp);
	case CCISS_GETINTINFO:
		return cciss_getintinfo(h, argp);
	case CCISS_SETINTINFO:
		return cciss_setintinfo(h, argp);
	case CCISS_GETNODENAME:
		return cciss_getnodename(h, argp);
	case CCISS_SETNODENAME:
		return cciss_setnodename(h, argp);
	case CCISS_GETHEARTBEAT:
		return cciss_getheartbeat(h, argp);
	case CCISS_GETBUSTYPES:
		return cciss_getbustypes(h, argp);
	case CCISS_GETFIRMVER:
		return cciss_getfirmver(h, argp);
	case CCISS_GETDRIVVER:
		return cciss_getdrivver(h, argp);
	case CCISS_DEREGDISK:
	case CCISS_REGNEWD:
	case CCISS_REVALIDVOLS:
		return rebuild_lun_table(h, 0, 1);
	case CCISS_GETLUNINFO:
		return cciss_getluninfo(h, disk, argp);
	case CCISS_PASSTHRU:
		return cciss_passthru(h, argp);
	case CCISS_BIG_PASSTHRU:
		return cciss_bigpassthru(h, argp);

	/* scsi_cmd_blk_ioctl handles these, below, though some are not */
	/* very meaningful for cciss.  SG_IO is the main one people want. */

	case SG_GET_VERSION_NUM:
	case SG_SET_TIMEOUT:
	case SG_GET_TIMEOUT:
	case SG_GET_RESERVED_SIZE:
	case SG_SET_RESERVED_SIZE:
	case SG_EMULATED_HOST:
	case SG_IO:
	case SCSI_IOCTL_SEND_COMMAND:
		return scsi_cmd_blk_ioctl(bdev, mode, cmd, argp);

	/* scsi_cmd_blk_ioctl would normally handle these, below, but */
	/* they aren't a good fit for cciss, as CD-ROMs are */
	/* not supported, and we don't have any bus/target/lun */
	/* which we present to the kernel. */

	case CDROM_SEND_PACKET:
	case CDROMCLOSETRAY:
	case CDROMEJECT:
	case SCSI_IOCTL_GET_IDLUN:
	case SCSI_IOCTL_GET_BUS_NUMBER:
	default:
		return -ENOTTY;
	}
}

static void cciss_check_queues(ctlr_info_t *h)
{
	int start_queue = h->next_to_run;
	int i;

	/* check to see if we have maxed out the number of commands that can
	 * be placed on the queue.  If so then exit.  We do this check here
	 * in case the interrupt we serviced was from an ioctl and did not
	 * free any new commands.
	 */
	if ((find_first_zero_bit(h->cmd_pool_bits, h->nr_cmds)) == h->nr_cmds)
		return;

	/* We have room on the queue for more commands.  Now we need to queue
	 * them up.  We will also keep track of the next queue to run so
	 * that every queue gets a chance to be started first.
	 */
	for (i = 0; i < h->highest_lun + 1; i++) {
		int curr_queue = (start_queue + i) % (h->highest_lun + 1);
		/* make sure the disk has been added and the drive is real
		 * because this can be called from the middle of init_one.
		 */
		if (!h->drv[curr_queue])
			continue;
		if (!(h->drv[curr_queue]->queue) ||
			!(h->drv[curr_queue]->heads))
			continue;
		blk_start_queue(h->gendisk[curr_queue]->queue);

		/* check to see if we have maxed out the number of commands
		 * that can be placed on the queue.
		 */
		if ((find_first_zero_bit(h->cmd_pool_bits, h->nr_cmds)) == h->nr_cmds) {
			if (curr_queue == start_queue) {
				h->next_to_run =
				    (start_queue + 1) % (h->highest_lun + 1);
				break;
			} else {
				h->next_to_run = curr_queue;
				break;
			}
		}
	}
}

static void cciss_softirq_done(struct request *rq)
{
	CommandList_struct *c = rq->completion_data;
	ctlr_info_t *h = hba[c->ctlr];
	SGDescriptor_struct *curr_sg = c->SG;
	u64bit temp64;
	unsigned long flags;
	int i, ddir;
	int sg_index = 0;

	if (c->Request.Type.Direction == XFER_READ)
		ddir = PCI_DMA_FROMDEVICE;
	else
		ddir = PCI_DMA_TODEVICE;

	/* command did not need to be retried */
	/* unmap the DMA mapping for all the scatter gather elements */
	for (i = 0; i < c->Header.SGList; i++) {
		if (curr_sg[sg_index].Ext == CCISS_SG_CHAIN) {
			cciss_unmap_sg_chain_block(h, c);
			/* Point to the next block */
			curr_sg = h->cmd_sg_list[c->cmdindex];
			sg_index = 0;
		}
		temp64.val32.lower = curr_sg[sg_index].Addr.lower;
		temp64.val32.upper = curr_sg[sg_index].Addr.upper;
		pci_unmap_page(h->pdev, temp64.val, curr_sg[sg_index].Len,
				ddir);
		++sg_index;
	}

	dev_dbg(&h->pdev->dev, "Done with %p\n", rq);

	/* set the residual count for pc requests */
	if (rq->cmd_type == REQ_TYPE_BLOCK_PC)
		rq->resid_len = c->err_info->ResidualCnt;

	blk_end_request_all(rq, (rq->errors == 0) ? 0 : -EIO);

	spin_lock_irqsave(&h->lock, flags);
	cmd_free(h, c);
	cciss_check_queues(h);
	spin_unlock_irqrestore(&h->lock, flags);
}

static inline void log_unit_to_scsi3addr(ctlr_info_t *h,
	unsigned char scsi3addr[], uint32_t log_unit)
{
	memcpy(scsi3addr, h->drv[log_unit]->LunID,
		sizeof(h->drv[log_unit]->LunID));
}

/* This function gets the SCSI vendor, model, and revision of a logical drive
 * via the inquiry page 0.  Model, vendor, and rev are set to empty strings if
 * they cannot be read.
 */
static void cciss_get_device_descr(ctlr_info_t *h, int logvol,
				   char *vendor, char *model, char *rev)
{
	int rc;
	InquiryData_struct *inq_buf;
	unsigned char scsi3addr[8];

	*vendor = '\0';
	*model = '\0';
	*rev = '\0';

	inq_buf = kzalloc(sizeof(InquiryData_struct), GFP_KERNEL);
	if (!inq_buf)
		return;

	log_unit_to_scsi3addr(h, scsi3addr, logvol);
	rc = sendcmd_withirq(h, CISS_INQUIRY, inq_buf, sizeof(*inq_buf), 0,
			scsi3addr, TYPE_CMD);
	if (rc == IO_OK) {
		memcpy(vendor, &inq_buf->data_byte[8], VENDOR_LEN);
		vendor[VENDOR_LEN] = '\0';
		memcpy(model, &inq_buf->data_byte[16], MODEL_LEN);
		model[MODEL_LEN] = '\0';
		memcpy(rev, &inq_buf->data_byte[32], REV_LEN);
		rev[REV_LEN] = '\0';
	}

	kfree(inq_buf);
	return;
}

/* This function gets the serial number of a logical drive via
 * inquiry page 0x83.  Serial no. is 16 bytes.  If the serial
 * number cannot be had, for whatever reason, 16 bytes of 0xff
 * are returned instead.
 */
static void cciss_get_serial_no(ctlr_info_t *h, int logvol,
				unsigned char *serial_no, int buflen)
{
#define PAGE_83_INQ_BYTES 64
	int rc;
	unsigned char *buf;
	unsigned char scsi3addr[8];

	if (buflen > 16)
		buflen = 16;
	memset(serial_no, 0xff, buflen);
	buf = kzalloc(PAGE_83_INQ_BYTES, GFP_KERNEL);
	if (!buf)
		return;
	memset(serial_no, 0, buflen);
	log_unit_to_scsi3addr(h, scsi3addr, logvol);
	rc = sendcmd_withirq(h, CISS_INQUIRY, buf,
		PAGE_83_INQ_BYTES, 0x83, scsi3addr, TYPE_CMD);
	if (rc == IO_OK)
		memcpy(serial_no, &buf[8], buflen);
	kfree(buf);
	return;
}

/*
 * cciss_add_disk sets up the block device queue for a logical drive
 */
static int cciss_add_disk(ctlr_info_t *h, struct gendisk *disk,
				int drv_index)
{
	disk->queue = blk_init_queue(do_cciss_request, &h->lock);
	if (!disk->queue)
		goto init_queue_failure;
	sprintf(disk->disk_name, "cciss/c%dd%d", h->ctlr, drv_index);
	disk->major = h->major;
	disk->first_minor = drv_index << NWD_SHIFT;
	disk->fops = &cciss_fops;
	if (cciss_create_ld_sysfs_entry(h, drv_index))
		goto cleanup_queue;
	disk->private_data = h->drv[drv_index];
	disk->driverfs_dev = &h->drv[drv_index]->dev;

	/* Set up queue information */
	blk_queue_bounce_limit(disk->queue, h->pdev->dma_mask);

	/* This is a hardware imposed limit. */
	blk_queue_max_segments(disk->queue, h->maxsgentries);

	blk_queue_max_hw_sectors(disk->queue, h->cciss_max_sectors);

	blk_queue_softirq_done(disk->queue, cciss_softirq_done);

	disk->queue->queuedata = h;

	blk_queue_logical_block_size(disk->queue,
				     h->drv[drv_index]->block_size);

	/* Make sure all queue data is written out before */
	/* setting h->drv[drv_index]->queue, as setting this */
	/* allows the interrupt handler to start the queue */
	wmb();
	h->drv[drv_index]->queue = disk->queue;
	add_disk(disk);
	return 0;

cleanup_queue:
	blk_cleanup_queue(disk->queue);
	disk->queue = NULL;
init_queue_failure:
	return -1;
}

/* This function will check the usage_count of the drive to be updated/added.
 * If the usage_count is zero and it is a heretofore unknown drive, or,
 * the drive's capacity, geometry, or serial number has changed,
 * then the drive information will be updated and the disk will be
 * re-registered with the kernel.  If these conditions don't hold,
 * then it will be left alone for the next reboot.  The exception to this
 * is disk 0 which will always be left registered with the kernel since it
 * is also the controller node.  Any changes to disk 0 will show up on
 * the next reboot.
 */
static void cciss_update_drive_info(ctlr_info_t *h, int drv_index,
	int first_time, int via_ioctl)
{
	struct gendisk *disk;
	InquiryData_struct *inq_buff = NULL;
	unsigned int block_size;
	sector_t total_size;
	unsigned long flags = 0;
	int ret = 0;
	drive_info_struct *drvinfo;

	/* Get information about the disk and modify the driver structure */
	inq_buff = kmalloc(sizeof(InquiryData_struct), GFP_KERNEL);
	drvinfo = kzalloc(sizeof(*drvinfo), GFP_KERNEL);
	if (inq_buff == NULL || drvinfo == NULL)
		goto mem_msg;

	/* testing to see if 16-byte CDBs are already being used */
	if (h->cciss_read == CCISS_READ_16) {
		cciss_read_capacity_16(h, drv_index,
			&total_size, &block_size);

	} else {
		cciss_read_capacity(h, drv_index, &total_size, &block_size);
		/* if read_capacity returns all F's this volume is >2TB */
		/* in size so we switch to 16-byte CDB's for all */
		/* read/write ops */
		if (total_size == 0xFFFFFFFFULL) {
			cciss_read_capacity_16(h, drv_index,
			&total_size, &block_size);
			h->cciss_read = CCISS_READ_16;
			h->cciss_write = CCISS_WRITE_16;
		} else {
			h->cciss_read = CCISS_READ_10;
			h->cciss_write = CCISS_WRITE_10;
		}
	}

	cciss_geometry_inquiry(h, drv_index, total_size, block_size,
			       inq_buff, drvinfo);
	drvinfo->block_size = block_size;
	drvinfo->nr_blocks = total_size + 1;

	cciss_get_device_descr(h, drv_index, drvinfo->vendor,
				drvinfo->model, drvinfo->rev);
	cciss_get_serial_no(h, drv_index, drvinfo->serial_no,
			sizeof(drvinfo->serial_no));
	/* Save the lunid in case we deregister the disk, below. */
	memcpy(drvinfo->LunID, h->drv[drv_index]->LunID,
		sizeof(drvinfo->LunID));

	/* Is it the same disk we already know, and nothing's changed? */
	if (h->drv[drv_index]->raid_level != -1 &&
		((memcmp(drvinfo->serial_no,
				h->drv[drv_index]->serial_no, 16) == 0) &&
		drvinfo->block_size == h->drv[drv_index]->block_size &&
		drvinfo->nr_blocks == h->drv[drv_index]->nr_blocks &&
		drvinfo->heads == h->drv[drv_index]->heads &&
		drvinfo->sectors == h->drv[drv_index]->sectors &&
		drvinfo->cylinders == h->drv[drv_index]->cylinders))
			/* The disk is unchanged, nothing to update */
			goto freeret;

	/* If we get here it's not the same disk, or something's changed,
	 * so we need to * deregister it, and re-register it, if it's not
	 * in use.
	 * If the disk already exists then deregister it before proceeding
	 * (unless it's the first disk (for the controller node).
	 */
	if (h->drv[drv_index]->raid_level != -1 && drv_index != 0) {
		dev_warn(&h->pdev->dev, "disk %d has changed.\n", drv_index);
		spin_lock_irqsave(&h->lock, flags);
		h->drv[drv_index]->busy_configuring = 1;
		spin_unlock_irqrestore(&h->lock, flags);

		/* deregister_disk sets h->drv[drv_index]->queue = NULL
		 * which keeps the interrupt handler from starting
		 * the queue.
		 */
		ret = deregister_disk(h, drv_index, 0, via_ioctl);
	}

	/* If the disk is in use return */
	if (ret)
		goto freeret;

	/* Save the new information from cciss_geometry_inquiry
	 * and serial number inquiry.  If the disk was deregistered
	 * above, then h->drv[drv_index] will be NULL.
	 */
	if (h->drv[drv_index] == NULL) {
		drvinfo->device_initialized = 0;
		h->drv[drv_index] = drvinfo;
		drvinfo = NULL; /* so it won't be freed below. */
	} else {
		/* special case for cxd0 */
		h->drv[drv_index]->block_size = drvinfo->block_size;
		h->drv[drv_index]->nr_blocks = drvinfo->nr_blocks;
		h->drv[drv_index]->heads = drvinfo->heads;
		h->drv[drv_index]->sectors = drvinfo->sectors;
		h->drv[drv_index]->cylinders = drvinfo->cylinders;
		h->drv[drv_index]->raid_level = drvinfo->raid_level;
		memcpy(h->drv[drv_index]->serial_no, drvinfo->serial_no, 16);
		memcpy(h->drv[drv_index]->vendor, drvinfo->vendor,
			VENDOR_LEN + 1);
		memcpy(h->drv[drv_index]->model, drvinfo->model, MODEL_LEN + 1);
		memcpy(h->drv[drv_index]->rev, drvinfo->rev, REV_LEN + 1);
	}

	++h->num_luns;
	disk = h->gendisk[drv_index];
	set_capacity(disk, h->drv[drv_index]->nr_blocks);

	/* If it's not disk 0 (drv_index != 0)
	 * or if it was disk 0, but there was previously
	 * no actual corresponding configured logical drive
	 * (raid_leve == -1) then we want to update the
	 * logical drive's information.
	 */
	if (drv_index || first_time) {
		if (cciss_add_disk(h, disk, drv_index) != 0) {
			cciss_free_gendisk(h, drv_index);
			cciss_free_drive_info(h, drv_index);
			dev_warn(&h->pdev->dev, "could not update disk %d\n",
				drv_index);
			--h->num_luns;
		}
	}

freeret:
	kfree(inq_buff);
	kfree(drvinfo);
	return;
mem_msg:
	dev_err(&h->pdev->dev, "out of memory\n");
	goto freeret;
}

/* This function will find the first index of the controllers drive array
 * that has a null drv pointer and allocate the drive info struct and
 * will return that index   This is where new drives will be added.
 * If the index to be returned is greater than the highest_lun index for
 * the controller then highest_lun is set * to this new index.
 * If there are no available indexes or if tha allocation fails, then -1
 * is returned.  * "controller_node" is used to know if this is a real
 * logical drive, or just the controller node, which determines if this
 * counts towards highest_lun.
 */
static int cciss_alloc_drive_info(ctlr_info_t *h, int controller_node)
{
	int i;
	drive_info_struct *drv;

	/* Search for an empty slot for our drive info */
	for (i = 0; i < CISS_MAX_LUN; i++) {

		/* if not cxd0 case, and it's occupied, skip it. */
		if (h->drv[i] && i != 0)
			continue;
		/*
		 * If it's cxd0 case, and drv is alloc'ed already, and a
		 * disk is configured there, skip it.
		 */
		if (i == 0 && h->drv[i] && h->drv[i]->raid_level != -1)
			continue;

		/*
		 * We've found an empty slot.  Update highest_lun
		 * provided this isn't just the fake cxd0 controller node.
		 */
		if (i > h->highest_lun && !controller_node)
			h->highest_lun = i;

		/* If adding a real disk at cxd0, and it's already alloc'ed */
		if (i == 0 && h->drv[i] != NULL)
			return i;

		/*
		 * Found an empty slot, not already alloc'ed.  Allocate it.
		 * Mark it with raid_level == -1, so we know it's new later on.
		 */
		drv = kzalloc(sizeof(*drv), GFP_KERNEL);
		if (!drv)
			return -1;
		drv->raid_level = -1; /* so we know it's new */
		h->drv[i] = drv;
		return i;
	}
	return -1;
}

static void cciss_free_drive_info(ctlr_info_t *h, int drv_index)
{
	kfree(h->drv[drv_index]);
	h->drv[drv_index] = NULL;
}

static void cciss_free_gendisk(ctlr_info_t *h, int drv_index)
{
	put_disk(h->gendisk[drv_index]);
	h->gendisk[drv_index] = NULL;
}

/* cciss_add_gendisk finds a free hba[]->drv structure
 * and allocates a gendisk if needed, and sets the lunid
 * in the drvinfo structure.   It returns the index into
 * the ->drv[] array, or -1 if none are free.
 * is_controller_node indicates whether highest_lun should
 * count this disk, or if it's only being added to provide
 * a means to talk to the controller in case no logical
 * drives have yet been configured.
 */
static int cciss_add_gendisk(ctlr_info_t *h, unsigned char lunid[],
	int controller_node)
{
	int drv_index;

	drv_index = cciss_alloc_drive_info(h, controller_node);
	if (drv_index == -1)
		return -1;

	/*Check if the gendisk needs to be allocated */
	if (!h->gendisk[drv_index]) {
		h->gendisk[drv_index] =
			alloc_disk(1 << NWD_SHIFT);
		if (!h->gendisk[drv_index]) {
			dev_err(&h->pdev->dev,
				"could not allocate a new disk %d\n",
				drv_index);
			goto err_free_drive_info;
		}
	}
	memcpy(h->drv[drv_index]->LunID, lunid,
		sizeof(h->drv[drv_index]->LunID));
	if (cciss_create_ld_sysfs_entry(h, drv_index))
		goto err_free_disk;
	/* Don't need to mark this busy because nobody */
	/* else knows about this disk yet to contend */
	/* for access to it. */
	h->drv[drv_index]->busy_configuring = 0;
	wmb();
	return drv_index;

err_free_disk:
	cciss_free_gendisk(h, drv_index);
err_free_drive_info:
	cciss_free_drive_info(h, drv_index);
	return -1;
}

/* This is for the special case of a controller which
 * has no logical drives.  In this case, we still need
 * to register a disk so the controller can be accessed
 * by the Array Config Utility.
 */
static void cciss_add_controller_node(ctlr_info_t *h)
{
	struct gendisk *disk;
	int drv_index;

	if (h->gendisk[0] != NULL) /* already did this? Then bail. */
		return;

	drv_index = cciss_add_gendisk(h, CTLR_LUNID, 1);
	if (drv_index == -1)
		goto error;
	h->drv[drv_index]->block_size = 512;
	h->drv[drv_index]->nr_blocks = 0;
	h->drv[drv_index]->heads = 0;
	h->drv[drv_index]->sectors = 0;
	h->drv[drv_index]->cylinders = 0;
	h->drv[drv_index]->raid_level = -1;
	memset(h->drv[drv_index]->serial_no, 0, 16);
	disk = h->gendisk[drv_index];
	if (cciss_add_disk(h, disk, drv_index) == 0)
		return;
	cciss_free_gendisk(h, drv_index);
	cciss_free_drive_info(h, drv_index);
error:
	dev_warn(&h->pdev->dev, "could not add disk 0.\n");
	return;
}

/* This function will add and remove logical drives from the Logical
 * drive array of the controller and maintain persistency of ordering
 * so that mount points are preserved until the next reboot.  This allows
 * for the removal of logical drives in the middle of the drive array
 * without a re-ordering of those drives.
 * INPUT
 * h		= The controller to perform the operations on
 */
static int rebuild_lun_table(ctlr_info_t *h, int first_time,
	int via_ioctl)
{
	int num_luns;
	ReportLunData_struct *ld_buff = NULL;
	int return_code;
	int listlength = 0;
	int i;
	int drv_found;
	int drv_index = 0;
	unsigned char lunid[8] = CTLR_LUNID;
	unsigned long flags;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* Set busy_configuring flag for this operation */
	spin_lock_irqsave(&h->lock, flags);
	if (h->busy_configuring) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -EBUSY;
	}
	h->busy_configuring = 1;
	spin_unlock_irqrestore(&h->lock, flags);

	ld_buff = kzalloc(sizeof(ReportLunData_struct), GFP_KERNEL);
	if (ld_buff == NULL)
		goto mem_msg;

	return_code = sendcmd_withirq(h, CISS_REPORT_LOG, ld_buff,
				      sizeof(ReportLunData_struct),
				      0, CTLR_LUNID, TYPE_CMD);

	if (return_code == IO_OK)
		listlength = be32_to_cpu(*(__be32 *) ld_buff->LUNListLength);
	else {	/* reading number of logical volumes failed */
		dev_warn(&h->pdev->dev,
			"report logical volume command failed\n");
		listlength = 0;
		goto freeret;
	}

	num_luns = listlength / 8;	/* 8 bytes per entry */
	if (num_luns > CISS_MAX_LUN) {
		num_luns = CISS_MAX_LUN;
		dev_warn(&h->pdev->dev, "more luns configured"
		       " on controller than can be handled by"
		       " this driver.\n");
	}

	if (num_luns == 0)
		cciss_add_controller_node(h);

	/* Compare controller drive array to driver's drive array
	 * to see if any drives are missing on the controller due
	 * to action of Array Config Utility (user deletes drive)
	 * and deregister logical drives which have disappeared.
	 */
	for (i = 0; i <= h->highest_lun; i++) {
		int j;
		drv_found = 0;

		/* skip holes in the array from already deleted drives */
		if (h->drv[i] == NULL)
			continue;

		for (j = 0; j < num_luns; j++) {
			memcpy(lunid, &ld_buff->LUN[j][0], sizeof(lunid));
			if (memcmp(h->drv[i]->LunID, lunid,
				sizeof(lunid)) == 0) {
				drv_found = 1;
				break;
			}
		}
		if (!drv_found) {
			/* Deregister it from the OS, it's gone. */
			spin_lock_irqsave(&h->lock, flags);
			h->drv[i]->busy_configuring = 1;
			spin_unlock_irqrestore(&h->lock, flags);
			return_code = deregister_disk(h, i, 1, via_ioctl);
			if (h->drv[i] != NULL)
				h->drv[i]->busy_configuring = 0;
		}
	}

	/* Compare controller drive array to driver's drive array.
	 * Check for updates in the drive information and any new drives
	 * on the controller due to ACU adding logical drives, or changing
	 * a logical drive's size, etc.  Reregister any new/changed drives
	 */
	for (i = 0; i < num_luns; i++) {
		int j;

		drv_found = 0;

		memcpy(lunid, &ld_buff->LUN[i][0], sizeof(lunid));
		/* Find if the LUN is already in the drive array
		 * of the driver.  If so then update its info
		 * if not in use.  If it does not exist then find
		 * the first free index and add it.
		 */
		for (j = 0; j <= h->highest_lun; j++) {
			if (h->drv[j] != NULL &&
				memcmp(h->drv[j]->LunID, lunid,
					sizeof(h->drv[j]->LunID)) == 0) {
				drv_index = j;
				drv_found = 1;
				break;
			}
		}

		/* check if the drive was found already in the array */
		if (!drv_found) {
			drv_index = cciss_add_gendisk(h, lunid, 0);
			if (drv_index == -1)
				goto freeret;
		}
		cciss_update_drive_info(h, drv_index, first_time, via_ioctl);
	}		/* end for */

freeret:
	kfree(ld_buff);
	h->busy_configuring = 0;
	/* We return -1 here to tell the ACU that we have registered/updated
	 * all of the drives that we can and to keep it from calling us
	 * additional times.
	 */
	return -1;
mem_msg:
	dev_err(&h->pdev->dev, "out of memory\n");
	h->busy_configuring = 0;
	goto freeret;
}

static void cciss_clear_drive_info(drive_info_struct *drive_info)
{
	/* zero out the disk size info */
	drive_info->nr_blocks = 0;
	drive_info->block_size = 0;
	drive_info->heads = 0;
	drive_info->sectors = 0;
	drive_info->cylinders = 0;
	drive_info->raid_level = -1;
	memset(drive_info->serial_no, 0, sizeof(drive_info->serial_no));
	memset(drive_info->model, 0, sizeof(drive_info->model));
	memset(drive_info->rev, 0, sizeof(drive_info->rev));
	memset(drive_info->vendor, 0, sizeof(drive_info->vendor));
	/*
	 * don't clear the LUNID though, we need to remember which
	 * one this one is.
	 */
}

/* This function will deregister the disk and it's queue from the
 * kernel.  It must be called with the controller lock held and the
 * drv structures busy_configuring flag set.  It's parameters are:
 *
 * disk = This is the disk to be deregistered
 * drv  = This is the drive_info_struct associated with the disk to be
 *        deregistered.  It contains information about the disk used
 *        by the driver.
 * clear_all = This flag determines whether or not the disk information
 *             is going to be completely cleared out and the highest_lun
 *             reset.  Sometimes we want to clear out information about
 *             the disk in preparation for re-adding it.  In this case
 *             the highest_lun should be left unchanged and the LunID
 *             should not be cleared.
 * via_ioctl
 *    This indicates whether we've reached this path via ioctl.
 *    This affects the maximum usage count allowed for c0d0 to be messed with.
 *    If this path is reached via ioctl(), then the max_usage_count will
 *    be 1, as the process calling ioctl() has got to have the device open.
 *    If we get here via sysfs, then the max usage count will be zero.
*/
static int deregister_disk(ctlr_info_t *h, int drv_index,
			   int clear_all, int via_ioctl)
{
	int i;
	struct gendisk *disk;
	drive_info_struct *drv;
	int recalculate_highest_lun;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	drv = h->drv[drv_index];
	disk = h->gendisk[drv_index];

	/* make sure logical volume is NOT is use */
	if (clear_all || (h->gendisk[0] == disk)) {
		if (drv->usage_count > via_ioctl)
			return -EBUSY;
	} else if (drv->usage_count > 0)
		return -EBUSY;

	recalculate_highest_lun = (drv == h->drv[h->highest_lun]);

	/* invalidate the devices and deregister the disk.  If it is disk
	 * zero do not deregister it but just zero out it's values.  This
	 * allows us to delete disk zero but keep the controller registered.
	 */
	if (h->gendisk[0] != disk) {
		struct request_queue *q = disk->queue;
		if (disk->flags & GENHD_FL_UP) {
			cciss_destroy_ld_sysfs_entry(h, drv_index, 0);
			del_gendisk(disk);
		}
		if (q)
			blk_cleanup_queue(q);
		/* If clear_all is set then we are deleting the logical
		 * drive, not just refreshing its info.  For drives
		 * other than disk 0 we will call put_disk.  We do not
		 * do this for disk 0 as we need it to be able to
		 * configure the controller.
		 */
		if (clear_all){
			/* This isn't pretty, but we need to find the
			 * disk in our array and NULL our the pointer.
			 * This is so that we will call alloc_disk if
			 * this index is used again later.
			 */
			for (i=0; i < CISS_MAX_LUN; i++){
				if (h->gendisk[i] == disk) {
					h->gendisk[i] = NULL;
					break;
				}
			}
			put_disk(disk);
		}
	} else {
		set_capacity(disk, 0);
		cciss_clear_drive_info(drv);
	}

	--h->num_luns;

	/* if it was the last disk, find the new hightest lun */
	if (clear_all && recalculate_highest_lun) {
		int newhighest = -1;
		for (i = 0; i <= h->highest_lun; i++) {
			/* if the disk has size > 0, it is available */
			if (h->drv[i] && h->drv[i]->heads)
				newhighest = i;
		}
		h->highest_lun = newhighest;
	}
	return 0;
}

static int fill_cmd(ctlr_info_t *h, CommandList_struct *c, __u8 cmd, void *buff,
		size_t size, __u8 page_code, unsigned char *scsi3addr,
		int cmd_type)
{
	u64bit buff_dma_handle;
	int status = IO_OK;

	c->cmd_type = CMD_IOCTL_PEND;
	c->Header.ReplyQueue = 0;
	if (buff != NULL) {
		c->Header.SGList = 1;
		c->Header.SGTotal = 1;
	} else {
		c->Header.SGList = 0;
		c->Header.SGTotal = 0;
	}
	c->Header.Tag.lower = c->busaddr;
	memcpy(c->Header.LUN.LunAddrBytes, scsi3addr, 8);

	c->Request.Type.Type = cmd_type;
	if (cmd_type == TYPE_CMD) {
		switch (cmd) {
		case CISS_INQUIRY:
			/* are we trying to read a vital product page */
			if (page_code != 0) {
				c->Request.CDB[1] = 0x01;
				c->Request.CDB[2] = page_code;
			}
			c->Request.CDBLen = 6;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = CISS_INQUIRY;
			c->Request.CDB[4] = size & 0xFF;
			break;
		case CISS_REPORT_LOG:
		case CISS_REPORT_PHYS:
			/* Talking to controller so It's a physical command
			   mode = 00 target = 0.  Nothing to write.
			 */
			c->Request.CDBLen = 12;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			c->Request.CDB[6] = (size >> 24) & 0xFF; /* MSB */
			c->Request.CDB[7] = (size >> 16) & 0xFF;
			c->Request.CDB[8] = (size >> 8) & 0xFF;
			c->Request.CDB[9] = size & 0xFF;
			break;

		case CCISS_READ_CAPACITY:
			c->Request.CDBLen = 10;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			break;
		case CCISS_READ_CAPACITY_16:
			c->Request.CDBLen = 16;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			c->Request.CDB[1] = 0x10;
			c->Request.CDB[10] = (size >> 24) & 0xFF;
			c->Request.CDB[11] = (size >> 16) & 0xFF;
			c->Request.CDB[12] = (size >> 8) & 0xFF;
			c->Request.CDB[13] = size & 0xFF;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			break;
		case CCISS_CACHE_FLUSH:
			c->Request.CDBLen = 12;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_WRITE;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = BMIC_WRITE;
			c->Request.CDB[6] = BMIC_CACHE_FLUSH;
			c->Request.CDB[7] = (size >> 8) & 0xFF;
			c->Request.CDB[8] = size & 0xFF;
			break;
		case TEST_UNIT_READY:
			c->Request.CDBLen = 6;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_NONE;
			c->Request.Timeout = 0;
			break;
		default:
			dev_warn(&h->pdev->dev, "Unknown Command 0x%c\n", cmd);
			return IO_ERROR;
		}
	} else if (cmd_type == TYPE_MSG) {
		switch (cmd) {
		case CCISS_ABORT_MSG:
			c->Request.CDBLen = 12;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_WRITE;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;	/* abort */
			c->Request.CDB[1] = 0;	/* abort a command */
			/* buff contains the tag of the command to abort */
			memcpy(&c->Request.CDB[4], buff, 8);
			break;
		case CCISS_RESET_MSG:
			c->Request.CDBLen = 16;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_NONE;
			c->Request.Timeout = 0;
			memset(&c->Request.CDB[0], 0, sizeof(c->Request.CDB));
			c->Request.CDB[0] = cmd;	/* reset */
			c->Request.CDB[1] = CCISS_RESET_TYPE_TARGET;
			break;
		case CCISS_NOOP_MSG:
			c->Request.CDBLen = 1;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_WRITE;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			break;
		default:
			dev_warn(&h->pdev->dev,
				"unknown message type %d\n", cmd);
			return IO_ERROR;
		}
	} else {
		dev_warn(&h->pdev->dev, "unknown command type %d\n", cmd_type);
		return IO_ERROR;
	}
	/* Fill in the scatter gather information */
	if (size > 0) {
		buff_dma_handle.val = (__u64) pci_map_single(h->pdev,
							     buff, size,
							     PCI_DMA_BIDIRECTIONAL);
		c->SG[0].Addr.lower = buff_dma_handle.val32.lower;
		c->SG[0].Addr.upper = buff_dma_handle.val32.upper;
		c->SG[0].Len = size;
		c->SG[0].Ext = 0;	/* we are not chaining */
	}
	return status;
}

static int __devinit cciss_send_reset(ctlr_info_t *h, unsigned char *scsi3addr,
	u8 reset_type)
{
	CommandList_struct *c;
	int return_status;

	c = cmd_alloc(h);
	if (!c)
		return -ENOMEM;
	return_status = fill_cmd(h, c, CCISS_RESET_MSG, NULL, 0, 0,
		CTLR_LUNID, TYPE_MSG);
	c->Request.CDB[1] = reset_type; /* fill_cmd defaults to target reset */
	if (return_status != IO_OK) {
		cmd_special_free(h, c);
		return return_status;
	}
	c->waiting = NULL;
	enqueue_cmd_and_start_io(h, c);
	/* Don't wait for completion, the reset won't complete.  Don't free
	 * the command either.  This is the last command we will send before
	 * re-initializing everything, so it doesn't matter and won't leak.
	 */
	return 0;
}

static int check_target_status(ctlr_info_t *h, CommandList_struct *c)
{
	switch (c->err_info->ScsiStatus) {
	case SAM_STAT_GOOD:
		return IO_OK;
	case SAM_STAT_CHECK_CONDITION:
		switch (0xf & c->err_info->SenseInfo[2]) {
		case 0: return IO_OK; /* no sense */
		case 1: return IO_OK; /* recovered error */
		default:
			if (check_for_unit_attention(h, c))
				return IO_NEEDS_RETRY;
			dev_warn(&h->pdev->dev, "cmd 0x%02x "
				"check condition, sense key = 0x%02x\n",
				c->Request.CDB[0], c->err_info->SenseInfo[2]);
		}
		break;
	default:
		dev_warn(&h->pdev->dev, "cmd 0x%02x"
			"scsi status = 0x%02x\n",
			c->Request.CDB[0], c->err_info->ScsiStatus);
		break;
	}
	return IO_ERROR;
}

static int process_sendcmd_error(ctlr_info_t *h, CommandList_struct *c)
{
	int return_status = IO_OK;

	if (c->err_info->CommandStatus == CMD_SUCCESS)
		return IO_OK;

	switch (c->err_info->CommandStatus) {
	case CMD_TARGET_STATUS:
		return_status = check_target_status(h, c);
		break;
	case CMD_DATA_UNDERRUN:
	case CMD_DATA_OVERRUN:
		/* expected for inquiry and report lun commands */
		break;
	case CMD_INVALID:
		dev_warn(&h->pdev->dev, "cmd 0x%02x is "
		       "reported invalid\n", c->Request.CDB[0]);
		return_status = IO_ERROR;
		break;
	case CMD_PROTOCOL_ERR:
		dev_warn(&h->pdev->dev, "cmd 0x%02x has "
		       "protocol error\n", c->Request.CDB[0]);
		return_status = IO_ERROR;
		break;
	case CMD_HARDWARE_ERR:
		dev_warn(&h->pdev->dev, "cmd 0x%02x had "
		       " hardware error\n", c->Request.CDB[0]);
		return_status = IO_ERROR;
		break;
	case CMD_CONNECTION_LOST:
		dev_warn(&h->pdev->dev, "cmd 0x%02x had "
		       "connection lost\n", c->Request.CDB[0]);
		return_status = IO_ERROR;
		break;
	case CMD_ABORTED:
		dev_warn(&h->pdev->dev, "cmd 0x%02x was "
		       "aborted\n", c->Request.CDB[0]);
		return_status = IO_ERROR;
		break;
	case CMD_ABORT_FAILED:
		dev_warn(&h->pdev->dev, "cmd 0x%02x reports "
		       "abort failed\n", c->Request.CDB[0]);
		return_status = IO_ERROR;
		break;
	case CMD_UNSOLICITED_ABORT:
		dev_warn(&h->pdev->dev, "unsolicited abort 0x%02x\n",
			c->Request.CDB[0]);
		return_status = IO_NEEDS_RETRY;
		break;
	case CMD_UNABORTABLE:
		dev_warn(&h->pdev->dev, "cmd unabortable\n");
		return_status = IO_ERROR;
		break;
	default:
		dev_warn(&h->pdev->dev, "cmd 0x%02x returned "
		       "unknown status %x\n", c->Request.CDB[0],
		       c->err_info->CommandStatus);
		return_status = IO_ERROR;
	}
	return return_status;
}

static int sendcmd_withirq_core(ctlr_info_t *h, CommandList_struct *c,
	int attempt_retry)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	u64bit buff_dma_handle;
	int return_status = IO_OK;

resend_cmd2:
	c->waiting = &wait;
	enqueue_cmd_and_start_io(h, c);

	wait_for_completion(&wait);

	if (c->err_info->CommandStatus == 0 || !attempt_retry)
		goto command_done;

	return_status = process_sendcmd_error(h, c);

	if (return_status == IO_NEEDS_RETRY &&
		c->retry_count < MAX_CMD_RETRIES) {
		dev_warn(&h->pdev->dev, "retrying 0x%02x\n",
			c->Request.CDB[0]);
		c->retry_count++;
		/* erase the old error information */
		memset(c->err_info, 0, sizeof(ErrorInfo_struct));
		return_status = IO_OK;
		INIT_COMPLETION(wait);
		goto resend_cmd2;
	}

command_done:
	/* unlock the buffers from DMA */
	buff_dma_handle.val32.lower = c->SG[0].Addr.lower;
	buff_dma_handle.val32.upper = c->SG[0].Addr.upper;
	pci_unmap_single(h->pdev, (dma_addr_t) buff_dma_handle.val,
			 c->SG[0].Len, PCI_DMA_BIDIRECTIONAL);
	return return_status;
}

static int sendcmd_withirq(ctlr_info_t *h, __u8 cmd, void *buff, size_t size,
			   __u8 page_code, unsigned char scsi3addr[],
			int cmd_type)
{
	CommandList_struct *c;
	int return_status;

	c = cmd_special_alloc(h);
	if (!c)
		return -ENOMEM;
	return_status = fill_cmd(h, c, cmd, buff, size, page_code,
		scsi3addr, cmd_type);
	if (return_status == IO_OK)
		return_status = sendcmd_withirq_core(h, c, 1);

	cmd_special_free(h, c);
	return return_status;
}

static void cciss_geometry_inquiry(ctlr_info_t *h, int logvol,
				   sector_t total_size,
				   unsigned int block_size,
				   InquiryData_struct *inq_buff,
				   drive_info_struct *drv)
{
	int return_code;
	unsigned long t;
	unsigned char scsi3addr[8];

	memset(inq_buff, 0, sizeof(InquiryData_struct));
	log_unit_to_scsi3addr(h, scsi3addr, logvol);
	return_code = sendcmd_withirq(h, CISS_INQUIRY, inq_buff,
			sizeof(*inq_buff), 0xC1, scsi3addr, TYPE_CMD);
	if (return_code == IO_OK) {
		if (inq_buff->data_byte[8] == 0xFF) {
			dev_warn(&h->pdev->dev,
			       "reading geometry failed, volume "
			       "does not support reading geometry\n");
			drv->heads = 255;
			drv->sectors = 32;	/* Sectors per track */
			drv->cylinders = total_size + 1;
			drv->raid_level = RAID_UNKNOWN;
		} else {
			drv->heads = inq_buff->data_byte[6];
			drv->sectors = inq_buff->data_byte[7];
			drv->cylinders = (inq_buff->data_byte[4] & 0xff) << 8;
			drv->cylinders += inq_buff->data_byte[5];
			drv->raid_level = inq_buff->data_byte[8];
		}
		drv->block_size = block_size;
		drv->nr_blocks = total_size + 1;
		t = drv->heads * drv->sectors;
		if (t > 1) {
			sector_t real_size = total_size + 1;
			unsigned long rem = sector_div(real_size, t);
			if (rem)
				real_size++;
			drv->cylinders = real_size;
		}
	} else {		/* Get geometry failed */
		dev_warn(&h->pdev->dev, "reading geometry failed\n");
	}
}

static void
cciss_read_capacity(ctlr_info_t *h, int logvol, sector_t *total_size,
		    unsigned int *block_size)
{
	ReadCapdata_struct *buf;
	int return_code;
	unsigned char scsi3addr[8];

	buf = kzalloc(sizeof(ReadCapdata_struct), GFP_KERNEL);
	if (!buf) {
		dev_warn(&h->pdev->dev, "out of memory\n");
		return;
	}

	log_unit_to_scsi3addr(h, scsi3addr, logvol);
	return_code = sendcmd_withirq(h, CCISS_READ_CAPACITY, buf,
		sizeof(ReadCapdata_struct), 0, scsi3addr, TYPE_CMD);
	if (return_code == IO_OK) {
		*total_size = be32_to_cpu(*(__be32 *) buf->total_size);
		*block_size = be32_to_cpu(*(__be32 *) buf->block_size);
	} else {		/* read capacity command failed */
		dev_warn(&h->pdev->dev, "read capacity failed\n");
		*total_size = 0;
		*block_size = BLOCK_SIZE;
	}
	kfree(buf);
}

static void cciss_read_capacity_16(ctlr_info_t *h, int logvol,
	sector_t *total_size, unsigned int *block_size)
{
	ReadCapdata_struct_16 *buf;
	int return_code;
	unsigned char scsi3addr[8];

	buf = kzalloc(sizeof(ReadCapdata_struct_16), GFP_KERNEL);
	if (!buf) {
		dev_warn(&h->pdev->dev, "out of memory\n");
		return;
	}

	log_unit_to_scsi3addr(h, scsi3addr, logvol);
	return_code = sendcmd_withirq(h, CCISS_READ_CAPACITY_16,
		buf, sizeof(ReadCapdata_struct_16),
			0, scsi3addr, TYPE_CMD);
	if (return_code == IO_OK) {
		*total_size = be64_to_cpu(*(__be64 *) buf->total_size);
		*block_size = be32_to_cpu(*(__be32 *) buf->block_size);
	} else {		/* read capacity command failed */
		dev_warn(&h->pdev->dev, "read capacity failed\n");
		*total_size = 0;
		*block_size = BLOCK_SIZE;
	}
	dev_info(&h->pdev->dev, "      blocks= %llu block_size= %d\n",
	       (unsigned long long)*total_size+1, *block_size);
	kfree(buf);
}

static int cciss_revalidate(struct gendisk *disk)
{
	ctlr_info_t *h = get_host(disk);
	drive_info_struct *drv = get_drv(disk);
	int logvol;
	int FOUND = 0;
	unsigned int block_size;
	sector_t total_size;
	InquiryData_struct *inq_buff = NULL;

	for (logvol = 0; logvol <= h->highest_lun; logvol++) {
		if (!h->drv[logvol])
			continue;
		if (memcmp(h->drv[logvol]->LunID, drv->LunID,
			sizeof(drv->LunID)) == 0) {
			FOUND = 1;
			break;
		}
	}

	if (!FOUND)
		return 1;

	inq_buff = kmalloc(sizeof(InquiryData_struct), GFP_KERNEL);
	if (inq_buff == NULL) {
		dev_warn(&h->pdev->dev, "out of memory\n");
		return 1;
	}
	if (h->cciss_read == CCISS_READ_10) {
		cciss_read_capacity(h, logvol,
					&total_size, &block_size);
	} else {
		cciss_read_capacity_16(h, logvol,
					&total_size, &block_size);
	}
	cciss_geometry_inquiry(h, logvol, total_size, block_size,
			       inq_buff, drv);

	blk_queue_logical_block_size(drv->queue, drv->block_size);
	set_capacity(disk, drv->nr_blocks);

	kfree(inq_buff);
	return 0;
}

/*
 * Map (physical) PCI mem into (virtual) kernel space
 */
static void __iomem *remap_pci_mem(ulong base, ulong size)
{
	ulong page_base = ((ulong) base) & PAGE_MASK;
	ulong page_offs = ((ulong) base) - page_base;
	void __iomem *page_remapped = ioremap(page_base, page_offs + size);

	return page_remapped ? (page_remapped + page_offs) : NULL;
}

/*
 * Takes jobs of the Q and sends them to the hardware, then puts it on
 * the Q to wait for completion.
 */
static void start_io(ctlr_info_t *h)
{
	CommandList_struct *c;

	while (!list_empty(&h->reqQ)) {
		c = list_entry(h->reqQ.next, CommandList_struct, list);
		/* can't do anything if fifo is full */
		if ((h->access.fifo_full(h))) {
			dev_warn(&h->pdev->dev, "fifo full\n");
			break;
		}

		/* Get the first entry from the Request Q */
		removeQ(c);
		h->Qdepth--;

		/* Tell the controller execute command */
		h->access.submit_command(h, c);

		/* Put job onto the completed Q */
		addQ(&h->cmpQ, c);
	}
}

/* Assumes that h->lock is held. */
/* Zeros out the error record and then resends the command back */
/* to the controller */
static inline void resend_cciss_cmd(ctlr_info_t *h, CommandList_struct *c)
{
	/* erase the old error information */
	memset(c->err_info, 0, sizeof(ErrorInfo_struct));

	/* add it to software queue and then send it to the controller */
	addQ(&h->reqQ, c);
	h->Qdepth++;
	if (h->Qdepth > h->maxQsinceinit)
		h->maxQsinceinit = h->Qdepth;

	start_io(h);
}

static inline unsigned int make_status_bytes(unsigned int scsi_status_byte,
	unsigned int msg_byte, unsigned int host_byte,
	unsigned int driver_byte)
{
	/* inverse of macros in scsi.h */
	return (scsi_status_byte & 0xff) |
		((msg_byte & 0xff) << 8) |
		((host_byte & 0xff) << 16) |
		((driver_byte & 0xff) << 24);
}

static inline int evaluate_target_status(ctlr_info_t *h,
			CommandList_struct *cmd, int *retry_cmd)
{
	unsigned char sense_key;
	unsigned char status_byte, msg_byte, host_byte, driver_byte;
	int error_value;

	*retry_cmd = 0;
	/* If we get in here, it means we got "target status", that is, scsi status */
	status_byte = cmd->err_info->ScsiStatus;
	driver_byte = DRIVER_OK;
	msg_byte = cmd->err_info->CommandStatus; /* correct?  seems too device specific */

	if (cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC)
		host_byte = DID_PASSTHROUGH;
	else
		host_byte = DID_OK;

	error_value = make_status_bytes(status_byte, msg_byte,
		host_byte, driver_byte);

	if (cmd->err_info->ScsiStatus != SAM_STAT_CHECK_CONDITION) {
		if (cmd->rq->cmd_type != REQ_TYPE_BLOCK_PC)
			dev_warn(&h->pdev->dev, "cmd %p "
			       "has SCSI Status 0x%x\n",
			       cmd, cmd->err_info->ScsiStatus);
		return error_value;
	}

	/* check the sense key */
	sense_key = 0xf & cmd->err_info->SenseInfo[2];
	/* no status or recovered error */
	if (((sense_key == 0x0) || (sense_key == 0x1)) &&
	    (cmd->rq->cmd_type != REQ_TYPE_BLOCK_PC))
		error_value = 0;

	if (check_for_unit_attention(h, cmd)) {
		*retry_cmd = !(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC);
		return 0;
	}

	/* Not SG_IO or similar? */
	if (cmd->rq->cmd_type != REQ_TYPE_BLOCK_PC) {
		if (error_value != 0)
			dev_warn(&h->pdev->dev, "cmd %p has CHECK CONDITION"
			       " sense key = 0x%x\n", cmd, sense_key);
		return error_value;
	}

	/* SG_IO or similar, copy sense data back */
	if (cmd->rq->sense) {
		if (cmd->rq->sense_len > cmd->err_info->SenseLen)
			cmd->rq->sense_len = cmd->err_info->SenseLen;
		memcpy(cmd->rq->sense, cmd->err_info->SenseInfo,
			cmd->rq->sense_len);
	} else
		cmd->rq->sense_len = 0;

	return error_value;
}

/* checks the status of the job and calls complete buffers to mark all
 * buffers for the completed job. Note that this function does not need
 * to hold the hba/queue lock.
 */
static inline void complete_command(ctlr_info_t *h, CommandList_struct *cmd,
				    int timeout)
{
	int retry_cmd = 0;
	struct request *rq = cmd->rq;

	rq->errors = 0;

	if (timeout)
		rq->errors = make_status_bytes(0, 0, 0, DRIVER_TIMEOUT);

	if (cmd->err_info->CommandStatus == 0)	/* no error has occurred */
		goto after_error_processing;

	switch (cmd->err_info->CommandStatus) {
	case CMD_TARGET_STATUS:
		rq->errors = evaluate_target_status(h, cmd, &retry_cmd);
		break;
	case CMD_DATA_UNDERRUN:
		if (cmd->rq->cmd_type == REQ_TYPE_FS) {
			dev_warn(&h->pdev->dev, "cmd %p has"
			       " completed with data underrun "
			       "reported\n", cmd);
			cmd->rq->resid_len = cmd->err_info->ResidualCnt;
		}
		break;
	case CMD_DATA_OVERRUN:
		if (cmd->rq->cmd_type == REQ_TYPE_FS)
			dev_warn(&h->pdev->dev, "cciss: cmd %p has"
			       " completed with data overrun "
			       "reported\n", cmd);
		break;
	case CMD_INVALID:
		dev_warn(&h->pdev->dev, "cciss: cmd %p is "
		       "reported invalid\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ERROR);
		break;
	case CMD_PROTOCOL_ERR:
		dev_warn(&h->pdev->dev, "cciss: cmd %p has "
		       "protocol error\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ERROR);
		break;
	case CMD_HARDWARE_ERR:
		dev_warn(&h->pdev->dev, "cciss: cmd %p had "
		       " hardware error\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ERROR);
		break;
	case CMD_CONNECTION_LOST:
		dev_warn(&h->pdev->dev, "cciss: cmd %p had "
		       "connection lost\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ERROR);
		break;
	case CMD_ABORTED:
		dev_warn(&h->pdev->dev, "cciss: cmd %p was "
		       "aborted\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ABORT);
		break;
	case CMD_ABORT_FAILED:
		dev_warn(&h->pdev->dev, "cciss: cmd %p reports "
		       "abort failed\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ERROR);
		break;
	case CMD_UNSOLICITED_ABORT:
		dev_warn(&h->pdev->dev, "cciss%d: unsolicited "
		       "abort %p\n", h->ctlr, cmd);
		if (cmd->retry_count < MAX_CMD_RETRIES) {
			retry_cmd = 1;
			dev_warn(&h->pdev->dev, "retrying %p\n", cmd);
			cmd->retry_count++;
		} else
			dev_warn(&h->pdev->dev,
				"%p retried too many times\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ABORT);
		break;
	case CMD_TIMEOUT:
		dev_warn(&h->pdev->dev, "cmd %p timedout\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ERROR);
		break;
	case CMD_UNABORTABLE:
		dev_warn(&h->pdev->dev, "cmd %p unabortable\n", cmd);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC ?
				DID_PASSTHROUGH : DID_ERROR);
		break;
	default:
		dev_warn(&h->pdev->dev, "cmd %p returned "
		       "unknown status %x\n", cmd,
		       cmd->err_info->CommandStatus);
		rq->errors = make_status_bytes(SAM_STAT_GOOD,
			cmd->err_info->CommandStatus, DRIVER_OK,
			(cmd->rq->cmd_type == REQ_TYPE_BLOCK_PC) ?
				DID_PASSTHROUGH : DID_ERROR);
	}

after_error_processing:

	/* We need to return this command */
	if (retry_cmd) {
		resend_cciss_cmd(h, cmd);
		return;
	}
	cmd->rq->completion_data = cmd;
	blk_complete_request(cmd->rq);
}

static inline u32 cciss_tag_contains_index(u32 tag)
{
#define DIRECT_LOOKUP_BIT 0x10
	return tag & DIRECT_LOOKUP_BIT;
}

static inline u32 cciss_tag_to_index(u32 tag)
{
#define DIRECT_LOOKUP_SHIFT 5
	return tag >> DIRECT_LOOKUP_SHIFT;
}

static inline u32 cciss_tag_discard_error_bits(ctlr_info_t *h, u32 tag)
{
#define CCISS_PERF_ERROR_BITS ((1 << DIRECT_LOOKUP_SHIFT) - 1)
#define CCISS_SIMPLE_ERROR_BITS 0x03
	if (likely(h->transMethod & CFGTBL_Trans_Performant))
		return tag & ~CCISS_PERF_ERROR_BITS;
	return tag & ~CCISS_SIMPLE_ERROR_BITS;
}

static inline void cciss_mark_tag_indexed(u32 *tag)
{
	*tag |= DIRECT_LOOKUP_BIT;
}

static inline void cciss_set_tag_index(u32 *tag, u32 index)
{
	*tag |= (index << DIRECT_LOOKUP_SHIFT);
}

/*
 * Get a request and submit it to the controller.
 */
static void do_cciss_request(struct request_queue *q)
{
	ctlr_info_t *h = q->queuedata;
	CommandList_struct *c;
	sector_t start_blk;
	int seg;
	struct request *creq;
	u64bit temp64;
	struct scatterlist *tmp_sg;
	SGDescriptor_struct *curr_sg;
	drive_info_struct *drv;
	int i, dir;
	int sg_index = 0;
	int chained = 0;

      queue:
	creq = blk_peek_request(q);
	if (!creq)
		goto startio;

	BUG_ON(creq->nr_phys_segments > h->maxsgentries);

	c = cmd_alloc(h);
	if (!c)
		goto full;

	blk_start_request(creq);

	tmp_sg = h->scatter_list[c->cmdindex];
	spin_unlock_irq(q->queue_lock);

	c->cmd_type = CMD_RWREQ;
	c->rq = creq;

	/* fill in the request */
	drv = creq->rq_disk->private_data;
	c->Header.ReplyQueue = 0;	/* unused in simple mode */
	/* got command from pool, so use the command block index instead */
	/* for direct lookups. */
	/* The first 2 bits are reserved for controller error reporting. */
	cciss_set_tag_index(&c->Header.Tag.lower, c->cmdindex);
	cciss_mark_tag_indexed(&c->Header.Tag.lower);
	memcpy(&c->Header.LUN, drv->LunID, sizeof(drv->LunID));
	c->Request.CDBLen = 10;	/* 12 byte commands not in FW yet; */
	c->Request.Type.Type = TYPE_CMD;	/* It is a command. */
	c->Request.Type.Attribute = ATTR_SIMPLE;
	c->Request.Type.Direction =
	    (rq_data_dir(creq) == READ) ? XFER_READ : XFER_WRITE;
	c->Request.Timeout = 0;	/* Don't time out */
	c->Request.CDB[0] =
	    (rq_data_dir(creq) == READ) ? h->cciss_read : h->cciss_write;
	start_blk = blk_rq_pos(creq);
	dev_dbg(&h->pdev->dev, "sector =%d nr_sectors=%d\n",
	       (int)blk_rq_pos(creq), (int)blk_rq_sectors(creq));
	sg_init_table(tmp_sg, h->maxsgentries);
	seg = blk_rq_map_sg(q, creq, tmp_sg);

	/* get the DMA records for the setup */
	if (c->Request.Type.Direction == XFER_READ)
		dir = PCI_DMA_FROMDEVICE;
	else
		dir = PCI_DMA_TODEVICE;

	curr_sg = c->SG;
	sg_index = 0;
	chained = 0;

	for (i = 0; i < seg; i++) {
		if (((sg_index+1) == (h->max_cmd_sgentries)) &&
			!chained && ((seg - i) > 1)) {
			/* Point to next chain block. */
			curr_sg = h->cmd_sg_list[c->cmdindex];
			sg_index = 0;
			chained = 1;
		}
		curr_sg[sg_index].Len = tmp_sg[i].length;
		temp64.val = (__u64) pci_map_page(h->pdev, sg_page(&tmp_sg[i]),
						tmp_sg[i].offset,
						tmp_sg[i].length, dir);
		curr_sg[sg_index].Addr.lower = temp64.val32.lower;
		curr_sg[sg_index].Addr.upper = temp64.val32.upper;
		curr_sg[sg_index].Ext = 0;  /* we are not chaining */
		++sg_index;
	}
	if (chained)
		cciss_map_sg_chain_block(h, c, h->cmd_sg_list[c->cmdindex],
			(seg - (h->max_cmd_sgentries - 1)) *
				sizeof(SGDescriptor_struct));

	/* track how many SG entries we are using */
	if (seg > h->maxSG)
		h->maxSG = seg;

	dev_dbg(&h->pdev->dev, "Submitting %u sectors in %d segments "
			"chained[%d]\n",
			blk_rq_sectors(creq), seg, chained);

	c->Header.SGTotal = seg + chained;
	if (seg <= h->max_cmd_sgentries)
		c->Header.SGList = c->Header.SGTotal;
	else
		c->Header.SGList = h->max_cmd_sgentries;
	set_performant_mode(h, c);

	if (likely(creq->cmd_type == REQ_TYPE_FS)) {
		if(h->cciss_read == CCISS_READ_10) {
			c->Request.CDB[1] = 0;
			c->Request.CDB[2] = (start_blk >> 24) & 0xff; /* MSB */
			c->Request.CDB[3] = (start_blk >> 16) & 0xff;
			c->Request.CDB[4] = (start_blk >> 8) & 0xff;
			c->Request.CDB[5] = start_blk & 0xff;
			c->Request.CDB[6] = 0; /* (sect >> 24) & 0xff; MSB */
			c->Request.CDB[7] = (blk_rq_sectors(creq) >> 8) & 0xff;
			c->Request.CDB[8] = blk_rq_sectors(creq) & 0xff;
			c->Request.CDB[9] = c->Request.CDB[11] = c->Request.CDB[12] = 0;
		} else {
			u32 upper32 = upper_32_bits(start_blk);

			c->Request.CDBLen = 16;
			c->Request.CDB[1]= 0;
			c->Request.CDB[2]= (upper32 >> 24) & 0xff; /* MSB */
			c->Request.CDB[3]= (upper32 >> 16) & 0xff;
			c->Request.CDB[4]= (upper32 >>  8) & 0xff;
			c->Request.CDB[5]= upper32 & 0xff;
			c->Request.CDB[6]= (start_blk >> 24) & 0xff;
			c->Request.CDB[7]= (start_blk >> 16) & 0xff;
			c->Request.CDB[8]= (start_blk >>  8) & 0xff;
			c->Request.CDB[9]= start_blk & 0xff;
			c->Request.CDB[10]= (blk_rq_sectors(creq) >> 24) & 0xff;
			c->Request.CDB[11]= (blk_rq_sectors(creq) >> 16) & 0xff;
			c->Request.CDB[12]= (blk_rq_sectors(creq) >>  8) & 0xff;
			c->Request.CDB[13]= blk_rq_sectors(creq) & 0xff;
			c->Request.CDB[14] = c->Request.CDB[15] = 0;
		}
	} else if (creq->cmd_type == REQ_TYPE_BLOCK_PC) {
		c->Request.CDBLen = creq->cmd_len;
		memcpy(c->Request.CDB, creq->cmd, BLK_MAX_CDB);
	} else {
		dev_warn(&h->pdev->dev, "bad request type %d\n",
			creq->cmd_type);
		BUG();
	}

	spin_lock_irq(q->queue_lock);

	addQ(&h->reqQ, c);
	h->Qdepth++;
	if (h->Qdepth > h->maxQsinceinit)
		h->maxQsinceinit = h->Qdepth;

	goto queue;
full:
	blk_stop_queue(q);
startio:
	/* We will already have the driver lock here so not need
	 * to lock it.
	 */
	start_io(h);
}

static inline unsigned long get_next_completion(ctlr_info_t *h)
{
	return h->access.command_completed(h);
}

static inline int interrupt_pending(ctlr_info_t *h)
{
	return h->access.intr_pending(h);
}

static inline long interrupt_not_for_us(ctlr_info_t *h)
{
	return ((h->access.intr_pending(h) == 0) ||
		(h->interrupts_enabled == 0));
}

static inline int bad_tag(ctlr_info_t *h, u32 tag_index,
			u32 raw_tag)
{
	if (unlikely(tag_index >= h->nr_cmds)) {
		dev_warn(&h->pdev->dev, "bad tag 0x%08x ignored.\n", raw_tag);
		return 1;
	}
	return 0;
}

static inline void finish_cmd(ctlr_info_t *h, CommandList_struct *c,
				u32 raw_tag)
{
	removeQ(c);
	if (likely(c->cmd_type == CMD_RWREQ))
		complete_command(h, c, 0);
	else if (c->cmd_type == CMD_IOCTL_PEND)
		complete(c->waiting);
#ifdef CONFIG_CISS_SCSI_TAPE
	else if (c->cmd_type == CMD_SCSI)
		complete_scsi_command(c, 0, raw_tag);
#endif
}

static inline u32 next_command(ctlr_info_t *h)
{
	u32 a;

	if (unlikely(!(h->transMethod & CFGTBL_Trans_Performant)))
		return h->access.command_completed(h);

	if ((*(h->reply_pool_head) & 1) == (h->reply_pool_wraparound)) {
		a = *(h->reply_pool_head); /* Next cmd in ring buffer */
		(h->reply_pool_head)++;
		h->commands_outstanding--;
	} else {
		a = FIFO_EMPTY;
	}
	/* Check for wraparound */
	if (h->reply_pool_head == (h->reply_pool + h->max_commands)) {
		h->reply_pool_head = h->reply_pool;
		h->reply_pool_wraparound ^= 1;
	}
	return a;
}

/* process completion of an indexed ("direct lookup") command */
static inline u32 process_indexed_cmd(ctlr_info_t *h, u32 raw_tag)
{
	u32 tag_index;
	CommandList_struct *c;

	tag_index = cciss_tag_to_index(raw_tag);
	if (bad_tag(h, tag_index, raw_tag))
		return next_command(h);
	c = h->cmd_pool + tag_index;
	finish_cmd(h, c, raw_tag);
	return next_command(h);
}

/* process completion of a non-indexed command */
static inline u32 process_nonindexed_cmd(ctlr_info_t *h, u32 raw_tag)
{
	CommandList_struct *c = NULL;
	__u32 busaddr_masked, tag_masked;

	tag_masked = cciss_tag_discard_error_bits(h, raw_tag);
	list_for_each_entry(c, &h->cmpQ, list) {
		busaddr_masked = cciss_tag_discard_error_bits(h, c->busaddr);
		if (busaddr_masked == tag_masked) {
			finish_cmd(h, c, raw_tag);
			return next_command(h);
		}
	}
	bad_tag(h, h->nr_cmds + 1, raw_tag);
	return next_command(h);
}

/* Some controllers, like p400, will give us one interrupt
 * after a soft reset, even if we turned interrupts off.
 * Only need to check for this in the cciss_xxx_discard_completions
 * functions.
 */
static int ignore_bogus_interrupt(ctlr_info_t *h)
{
	if (likely(!reset_devices))
		return 0;

	if (likely(h->interrupts_enabled))
		return 0;

	dev_info(&h->pdev->dev, "Received interrupt while interrupts disabled "
		"(known firmware bug.)  Ignoring.\n");

	return 1;
}

static irqreturn_t cciss_intx_discard_completions(int irq, void *dev_id)
{
	ctlr_info_t *h = dev_id;
	unsigned long flags;
	u32 raw_tag;

	if (ignore_bogus_interrupt(h))
		return IRQ_NONE;

	if (interrupt_not_for_us(h))
		return IRQ_NONE;
	spin_lock_irqsave(&h->lock, flags);
	while (interrupt_pending(h)) {
		raw_tag = get_next_completion(h);
		while (raw_tag != FIFO_EMPTY)
			raw_tag = next_command(h);
	}
	spin_unlock_irqrestore(&h->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t cciss_msix_discard_completions(int irq, void *dev_id)
{
	ctlr_info_t *h = dev_id;
	unsigned long flags;
	u32 raw_tag;

	if (ignore_bogus_interrupt(h))
		return IRQ_NONE;

	spin_lock_irqsave(&h->lock, flags);
	raw_tag = get_next_completion(h);
	while (raw_tag != FIFO_EMPTY)
		raw_tag = next_command(h);
	spin_unlock_irqrestore(&h->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t do_cciss_intx(int irq, void *dev_id)
{
	ctlr_info_t *h = dev_id;
	unsigned long flags;
	u32 raw_tag;

	if (interrupt_not_for_us(h))
		return IRQ_NONE;
	spin_lock_irqsave(&h->lock, flags);
	while (interrupt_pending(h)) {
		raw_tag = get_next_completion(h);
		while (raw_tag != FIFO_EMPTY) {
			if (cciss_tag_contains_index(raw_tag))
				raw_tag = process_indexed_cmd(h, raw_tag);
			else
				raw_tag = process_nonindexed_cmd(h, raw_tag);
		}
	}
	spin_unlock_irqrestore(&h->lock, flags);
	return IRQ_HANDLED;
}

/* Add a second interrupt handler for MSI/MSI-X mode. In this mode we never
 * check the interrupt pending register because it is not set.
 */
static irqreturn_t do_cciss_msix_intr(int irq, void *dev_id)
{
	ctlr_info_t *h = dev_id;
	unsigned long flags;
	u32 raw_tag;

	spin_lock_irqsave(&h->lock, flags);
	raw_tag = get_next_completion(h);
	while (raw_tag != FIFO_EMPTY) {
		if (cciss_tag_contains_index(raw_tag))
			raw_tag = process_indexed_cmd(h, raw_tag);
		else
			raw_tag = process_nonindexed_cmd(h, raw_tag);
	}
	spin_unlock_irqrestore(&h->lock, flags);
	return IRQ_HANDLED;
}

/**
 * add_to_scan_list() - add controller to rescan queue
 * @h:		      Pointer to the controller.
 *
 * Adds the controller to the rescan queue if not already on the queue.
 *
 * returns 1 if added to the queue, 0 if skipped (could be on the
 * queue already, or the controller could be initializing or shutting
 * down).
 **/
static int add_to_scan_list(struct ctlr_info *h)
{
	struct ctlr_info *test_h;
	int found = 0;
	int ret = 0;

	if (h->busy_initializing)
		return 0;

	if (!mutex_trylock(&h->busy_shutting_down))
		return 0;

	mutex_lock(&scan_mutex);
	list_for_each_entry(test_h, &scan_q, scan_list) {
		if (test_h == h) {
			found = 1;
			break;
		}
	}
	if (!found && !h->busy_scanning) {
		INIT_COMPLETION(h->scan_wait);
		list_add_tail(&h->scan_list, &scan_q);
		ret = 1;
	}
	mutex_unlock(&scan_mutex);
	mutex_unlock(&h->busy_shutting_down);

	return ret;
}

/**
 * remove_from_scan_list() - remove controller from rescan queue
 * @h:			   Pointer to the controller.
 *
 * Removes the controller from the rescan queue if present. Blocks if
 * the controller is currently conducting a rescan.  The controller
 * can be in one of three states:
 * 1. Doesn't need a scan
 * 2. On the scan list, but not scanning yet (we remove it)
 * 3. Busy scanning (and not on the list). In this case we want to wait for
 *    the scan to complete to make sure the scanning thread for this
 *    controller is completely idle.
 **/
static void remove_from_scan_list(struct ctlr_info *h)
{
	struct ctlr_info *test_h, *tmp_h;

	mutex_lock(&scan_mutex);
	list_for_each_entry_safe(test_h, tmp_h, &scan_q, scan_list) {
		if (test_h == h) { /* state 2. */
			list_del(&h->scan_list);
			complete_all(&h->scan_wait);
			mutex_unlock(&scan_mutex);
			return;
		}
	}
	if (h->busy_scanning) { /* state 3. */
		mutex_unlock(&scan_mutex);
		wait_for_completion(&h->scan_wait);
	} else { /* state 1, nothing to do. */
		mutex_unlock(&scan_mutex);
	}
}

/**
 * scan_thread() - kernel thread used to rescan controllers
 * @data:	 Ignored.
 *
 * A kernel thread used scan for drive topology changes on
 * controllers. The thread processes only one controller at a time
 * using a queue.  Controllers are added to the queue using
 * add_to_scan_list() and removed from the queue either after done
 * processing or using remove_from_scan_list().
 *
 * returns 0.
 **/
static int scan_thread(void *data)
{
	struct ctlr_info *h;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (kthread_should_stop())
			break;

		while (1) {
			mutex_lock(&scan_mutex);
			if (list_empty(&scan_q)) {
				mutex_unlock(&scan_mutex);
				break;
			}

			h = list_entry(scan_q.next,
				       struct ctlr_info,
				       scan_list);
			list_del(&h->scan_list);
			h->busy_scanning = 1;
			mutex_unlock(&scan_mutex);

			rebuild_lun_table(h, 0, 0);
			complete_all(&h->scan_wait);
			mutex_lock(&scan_mutex);
			h->busy_scanning = 0;
			mutex_unlock(&scan_mutex);
		}
	}

	return 0;
}

static int check_for_unit_attention(ctlr_info_t *h, CommandList_struct *c)
{
	if (c->err_info->SenseInfo[2] != UNIT_ATTENTION)
		return 0;

	switch (c->err_info->SenseInfo[12]) {
	case STATE_CHANGED:
		dev_warn(&h->pdev->dev, "a state change "
			"detected, command retried\n");
		return 1;
	break;
	case LUN_FAILED:
		dev_warn(&h->pdev->dev, "LUN failure "
			"detected, action required\n");
		return 1;
	break;
	case REPORT_LUNS_CHANGED:
		dev_warn(&h->pdev->dev, "report LUN data changed\n");
	/*
	 * Here, we could call add_to_scan_list and wake up the scan thread,
	 * except that it's quite likely that we will get more than one
	 * REPORT_LUNS_CHANGED condition in quick succession, which means
	 * that those which occur after the first one will likely happen
	 * *during* the scan_thread's rescan.  And the rescan code is not
	 * robust enough to restart in the middle, undoing what it has already
	 * done, and it's not clear that it's even possible to do this, since
	 * part of what it does is notify the block layer, which starts
	 * doing it's own i/o to read partition tables and so on, and the
	 * driver doesn't have visibility to know what might need undoing.
	 * In any event, if possible, it is horribly complicated to get right
	 * so we just don't do it for now.
	 *
	 * Note: this REPORT_LUNS_CHANGED condition only occurs on the MSA2012.
	 */
		return 1;
	break;
	case POWER_OR_RESET:
		dev_warn(&h->pdev->dev,
			"a power on or device reset detected\n");
		return 1;
	break;
	case UNIT_ATTENTION_CLEARED:
		dev_warn(&h->pdev->dev,
			"unit attention cleared by another initiator\n");
		return 1;
	break;
	default:
		dev_warn(&h->pdev->dev, "unknown unit attention detected\n");
		return 1;
	}
}

/*
 *  We cannot read the structure directly, for portability we must use
 *   the io functions.
 *   This is for debug only.
 */
static void print_cfg_table(ctlr_info_t *h)
{
	int i;
	char temp_name[17];
	CfgTable_struct *tb = h->cfgtable;

	dev_dbg(&h->pdev->dev, "Controller Configuration information\n");
	dev_dbg(&h->pdev->dev, "------------------------------------\n");
	for (i = 0; i < 4; i++)
		temp_name[i] = readb(&(tb->Signature[i]));
	temp_name[4] = '\0';
	dev_dbg(&h->pdev->dev, "   Signature = %s\n", temp_name);
	dev_dbg(&h->pdev->dev, "   Spec Number = %d\n",
		readl(&(tb->SpecValence)));
	dev_dbg(&h->pdev->dev, "   Transport methods supported = 0x%x\n",
	       readl(&(tb->TransportSupport)));
	dev_dbg(&h->pdev->dev, "   Transport methods active = 0x%x\n",
	       readl(&(tb->TransportActive)));
	dev_dbg(&h->pdev->dev, "   Requested transport Method = 0x%x\n",
	       readl(&(tb->HostWrite.TransportRequest)));
	dev_dbg(&h->pdev->dev, "   Coalesce Interrupt Delay = 0x%x\n",
	       readl(&(tb->HostWrite.CoalIntDelay)));
	dev_dbg(&h->pdev->dev, "   Coalesce Interrupt Count = 0x%x\n",
	       readl(&(tb->HostWrite.CoalIntCount)));
	dev_dbg(&h->pdev->dev, "   Max outstanding commands = 0x%d\n",
	       readl(&(tb->CmdsOutMax)));
	dev_dbg(&h->pdev->dev, "   Bus Types = 0x%x\n",
		readl(&(tb->BusTypes)));
	for (i = 0; i < 16; i++)
		temp_name[i] = readb(&(tb->ServerName[i]));
	temp_name[16] = '\0';
	dev_dbg(&h->pdev->dev, "   Server Name = %s\n", temp_name);
	dev_dbg(&h->pdev->dev, "   Heartbeat Counter = 0x%x\n\n\n",
		readl(&(tb->HeartBeat)));
}

static int find_PCI_BAR_index(struct pci_dev *pdev, unsigned long pci_bar_addr)
{
	int i, offset, mem_type, bar_type;
	if (pci_bar_addr == PCI_BASE_ADDRESS_0)	/* looking for BAR zero? */
		return 0;
	offset = 0;
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		bar_type = pci_resource_flags(pdev, i) & PCI_BASE_ADDRESS_SPACE;
		if (bar_type == PCI_BASE_ADDRESS_SPACE_IO)
			offset += 4;
		else {
			mem_type = pci_resource_flags(pdev, i) &
			    PCI_BASE_ADDRESS_MEM_TYPE_MASK;
			switch (mem_type) {
			case PCI_BASE_ADDRESS_MEM_TYPE_32:
			case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				offset += 4;	/* 32 bit */
				break;
			case PCI_BASE_ADDRESS_MEM_TYPE_64:
				offset += 8;
				break;
			default:	/* reserved in PCI 2.2 */
				dev_warn(&pdev->dev,
				       "Base address is invalid\n");
				return -1;
				break;
			}
		}
		if (offset == pci_bar_addr - PCI_BASE_ADDRESS_0)
			return i + 1;
	}
	return -1;
}

/* Fill in bucket_map[], given nsgs (the max number of
 * scatter gather elements supported) and bucket[],
 * which is an array of 8 integers.  The bucket[] array
 * contains 8 different DMA transfer sizes (in 16
 * byte increments) which the controller uses to fetch
 * commands.  This function fills in bucket_map[], which
 * maps a given number of scatter gather elements to one of
 * the 8 DMA transfer sizes.  The point of it is to allow the
 * controller to only do as much DMA as needed to fetch the
 * command, with the DMA transfer size encoded in the lower
 * bits of the command address.
 */
static void  calc_bucket_map(int bucket[], int num_buckets,
	int nsgs, int *bucket_map)
{
	int i, j, b, size;

	/* even a command with 0 SGs requires 4 blocks */
#define MINIMUM_TRANSFER_BLOCKS 4
#define NUM_BUCKETS 8
	/* Note, bucket_map must have nsgs+1 entries. */
	for (i = 0; i <= nsgs; i++) {
		/* Compute size of a command with i SG entries */
		size = i + MINIMUM_TRANSFER_BLOCKS;
		b = num_buckets; /* Assume the biggest bucket */
		/* Find the bucket that is just big enough */
		for (j = 0; j < 8; j++) {
			if (bucket[j] >= size) {
				b = j;
				break;
			}
		}
		/* for a command with i SG entries, use bucket b. */
		bucket_map[i] = b;
	}
}

static void __devinit cciss_wait_for_mode_change_ack(ctlr_info_t *h)
{
	int i;

	/* under certain very rare conditions, this can take awhile.
	 * (e.g.: hot replace a failed 144GB drive in a RAID 5 set right
	 * as we enter this code.) */
	for (i = 0; i < MAX_CONFIG_WAIT; i++) {
		if (!(readl(h->vaddr + SA5_DOORBELL) & CFGTBL_ChangeReq))
			break;
		usleep_range(10000, 20000);
	}
}

static __devinit void cciss_enter_performant_mode(ctlr_info_t *h,
	u32 use_short_tags)
{
	/* This is a bit complicated.  There are 8 registers on
	 * the controller which we write to to tell it 8 different
	 * sizes of commands which there may be.  It's a way of
	 * reducing the DMA done to fetch each command.  Encoded into
	 * each command's tag are 3 bits which communicate to the controller
	 * which of the eight sizes that command fits within.  The size of
	 * each command depends on how many scatter gather entries there are.
	 * Each SG entry requires 16 bytes.  The eight registers are programmed
	 * with the number of 16-byte blocks a command of that size requires.
	 * The smallest command possible requires 5 such 16 byte blocks.
	 * the largest command possible requires MAXSGENTRIES + 4 16-byte
	 * blocks.  Note, this only extends to the SG entries contained
	 * within the command block, and does not extend to chained blocks
	 * of SG elements.   bft[] contains the eight values we write to
	 * the registers.  They are not evenly distributed, but have more
	 * sizes for small commands, and fewer sizes for larger commands.
	 */
	__u32 trans_offset;
	int bft[8] = { 5, 6, 8, 10, 12, 20, 28, MAXSGENTRIES + 4};
			/*
			 *  5 = 1 s/g entry or 4k
			 *  6 = 2 s/g entry or 8k
			 *  8 = 4 s/g entry or 16k
			 * 10 = 6 s/g entry or 24k
			 */
	unsigned long register_value;
	BUILD_BUG_ON(28 > MAXSGENTRIES + 4);

	h->reply_pool_wraparound = 1; /* spec: init to 1 */

	/* Controller spec: zero out this buffer. */
	memset(h->reply_pool, 0, h->max_commands * sizeof(__u64));
	h->reply_pool_head = h->reply_pool;

	trans_offset = readl(&(h->cfgtable->TransMethodOffset));
	calc_bucket_map(bft, ARRAY_SIZE(bft), h->maxsgentries,
				h->blockFetchTable);
	writel(bft[0], &h->transtable->BlockFetch0);
	writel(bft[1], &h->transtable->BlockFetch1);
	writel(bft[2], &h->transtable->BlockFetch2);
	writel(bft[3], &h->transtable->BlockFetch3);
	writel(bft[4], &h->transtable->BlockFetch4);
	writel(bft[5], &h->transtable->BlockFetch5);
	writel(bft[6], &h->transtable->BlockFetch6);
	writel(bft[7], &h->transtable->BlockFetch7);

	/* size of controller ring buffer */
	writel(h->max_commands, &h->transtable->RepQSize);
	writel(1, &h->transtable->RepQCount);
	writel(0, &h->transtable->RepQCtrAddrLow32);
	writel(0, &h->transtable->RepQCtrAddrHigh32);
	writel(h->reply_pool_dhandle, &h->transtable->RepQAddr0Low32);
	writel(0, &h->transtable->RepQAddr0High32);
	writel(CFGTBL_Trans_Performant | use_short_tags,
			&(h->cfgtable->HostWrite.TransportRequest));

	writel(CFGTBL_ChangeReq, h->vaddr + SA5_DOORBELL);
	cciss_wait_for_mode_change_ack(h);
	register_value = readl(&(h->cfgtable->TransportActive));
	if (!(register_value & CFGTBL_Trans_Performant))
		dev_warn(&h->pdev->dev, "cciss: unable to get board into"
					" performant mode\n");
}

static void __devinit cciss_put_controller_into_performant_mode(ctlr_info_t *h)
{
	__u32 trans_support;

	if (cciss_simple_mode)
		return;

	dev_dbg(&h->pdev->dev, "Trying to put board into Performant mode\n");
	/* Attempt to put controller into performant mode if supported */
	/* Does board support performant mode? */
	trans_support = readl(&(h->cfgtable->TransportSupport));
	if (!(trans_support & PERFORMANT_MODE))
		return;

	dev_dbg(&h->pdev->dev, "Placing controller into performant mode\n");
	/* Performant mode demands commands on a 32 byte boundary
	 * pci_alloc_consistent aligns on page boundarys already.
	 * Just need to check if divisible by 32
	 */
	if ((sizeof(CommandList_struct) % 32) != 0) {
		dev_warn(&h->pdev->dev, "%s %d %s\n",
			"cciss info: command size[",
			(int)sizeof(CommandList_struct),
			"] not divisible by 32, no performant mode..\n");
		return;
	}

	/* Performant mode ring buffer and supporting data structures */
	h->reply_pool = (__u64 *)pci_alloc_consistent(
		h->pdev, h->max_commands * sizeof(__u64),
		&(h->reply_pool_dhandle));

	/* Need a block fetch table for performant mode */
	h->blockFetchTable = kmalloc(((h->maxsgentries+1) *
		sizeof(__u32)), GFP_KERNEL);

	if ((h->reply_pool == NULL) || (h->blockFetchTable == NULL))
		goto clean_up;

	cciss_enter_performant_mode(h,
		trans_support & CFGTBL_Trans_use_short_tags);

	/* Change the access methods to the performant access methods */
	h->access = SA5_performant_access;
	h->transMethod = CFGTBL_Trans_Performant;

	return;
clean_up:
	kfree(h->blockFetchTable);
	if (h->reply_pool)
		pci_free_consistent(h->pdev,
				h->max_commands * sizeof(__u64),
				h->reply_pool,
				h->reply_pool_dhandle);
	return;

} /* cciss_put_controller_into_performant_mode */

/* If MSI/MSI-X is supported by the kernel we will try to enable it on
 * controllers that are capable. If not, we use IO-APIC mode.
 */

static void __devinit cciss_interrupt_mode(ctlr_info_t *h)
{
#ifdef CONFIG_PCI_MSI
	int err;
	struct msix_entry cciss_msix_entries[4] = { {0, 0}, {0, 1},
	{0, 2}, {0, 3}
	};

	/* Some boards advertise MSI but don't really support it */
	if ((h->board_id == 0x40700E11) || (h->board_id == 0x40800E11) ||
	    (h->board_id == 0x40820E11) || (h->board_id == 0x40830E11))
		goto default_int_mode;

	if (pci_find_capability(h->pdev, PCI_CAP_ID_MSIX)) {
		err = pci_enable_msix(h->pdev, cciss_msix_entries, 4);
		if (!err) {
			h->intr[0] = cciss_msix_entries[0].vector;
			h->intr[1] = cciss_msix_entries[1].vector;
			h->intr[2] = cciss_msix_entries[2].vector;
			h->intr[3] = cciss_msix_entries[3].vector;
			h->msix_vector = 1;
			return;
		}
		if (err > 0) {
			dev_warn(&h->pdev->dev,
				"only %d MSI-X vectors available\n", err);
			goto default_int_mode;
		} else {
			dev_warn(&h->pdev->dev,
				"MSI-X init failed %d\n", err);
			goto default_int_mode;
		}
	}
	if (pci_find_capability(h->pdev, PCI_CAP_ID_MSI)) {
		if (!pci_enable_msi(h->pdev))
			h->msi_vector = 1;
		else
			dev_warn(&h->pdev->dev, "MSI init failed\n");
	}
default_int_mode:
#endif				/* CONFIG_PCI_MSI */
	/* if we get here we're going to use the default interrupt mode */
	h->intr[h->intr_mode] = h->pdev->irq;
	return;
}

static int __devinit cciss_lookup_board_id(struct pci_dev *pdev, u32 *board_id)
{
	int i;
	u32 subsystem_vendor_id, subsystem_device_id;

	subsystem_vendor_id = pdev->subsystem_vendor;
	subsystem_device_id = pdev->subsystem_device;
	*board_id = ((subsystem_device_id << 16) & 0xffff0000) |
			subsystem_vendor_id;

	for (i = 0; i < ARRAY_SIZE(products); i++)
		if (*board_id == products[i].board_id)
			return i;
	dev_warn(&pdev->dev, "unrecognized board ID: 0x%08x, ignoring.\n",
		*board_id);
	return -ENODEV;
}

static inline bool cciss_board_disabled(ctlr_info_t *h)
{
	u16 command;

	(void) pci_read_config_word(h->pdev, PCI_COMMAND, &command);
	return ((command & PCI_COMMAND_MEMORY) == 0);
}

static int __devinit cciss_pci_find_memory_BAR(struct pci_dev *pdev,
	unsigned long *memory_bar)
{
	int i;

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++)
		if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
			/* addressing mode bits already removed */
			*memory_bar = pci_resource_start(pdev, i);
			dev_dbg(&pdev->dev, "memory BAR = %lx\n",
				*memory_bar);
			return 0;
		}
	dev_warn(&pdev->dev, "no memory BAR found\n");
	return -ENODEV;
}

static int __devinit cciss_wait_for_board_state(struct pci_dev *pdev,
	void __iomem *vaddr, int wait_for_ready)
#define BOARD_READY 1
#define BOARD_NOT_READY 0
{
	int i, iterations;
	u32 scratchpad;

	if (wait_for_ready)
		iterations = CCISS_BOARD_READY_ITERATIONS;
	else
		iterations = CCISS_BOARD_NOT_READY_ITERATIONS;

	for (i = 0; i < iterations; i++) {
		scratchpad = readl(vaddr + SA5_SCRATCHPAD_OFFSET);
		if (wait_for_ready) {
			if (scratchpad == CCISS_FIRMWARE_READY)
				return 0;
		} else {
			if (scratchpad != CCISS_FIRMWARE_READY)
				return 0;
		}
		msleep(CCISS_BOARD_READY_POLL_INTERVAL_MSECS);
	}
	dev_warn(&pdev->dev, "board not ready, timed out.\n");
	return -ENODEV;
}

static int __devinit cciss_find_cfg_addrs(struct pci_dev *pdev,
	void __iomem *vaddr, u32 *cfg_base_addr, u64 *cfg_base_addr_index,
	u64 *cfg_offset)
{
	*cfg_base_addr = readl(vaddr + SA5_CTCFG_OFFSET);
	*cfg_offset = readl(vaddr + SA5_CTMEM_OFFSET);
	*cfg_base_addr &= (u32) 0x0000ffff;
	*cfg_base_addr_index = find_PCI_BAR_index(pdev, *cfg_base_addr);
	if (*cfg_base_addr_index == -1) {
		dev_warn(&pdev->dev, "cannot find cfg_base_addr_index, "
			"*cfg_base_addr = 0x%08x\n", *cfg_base_addr);
		return -ENODEV;
	}
	return 0;
}

static int __devinit cciss_find_cfgtables(ctlr_info_t *h)
{
	u64 cfg_offset;
	u32 cfg_base_addr;
	u64 cfg_base_addr_index;
	u32 trans_offset;
	int rc;

	rc = cciss_find_cfg_addrs(h->pdev, h->vaddr, &cfg_base_addr,
		&cfg_base_addr_index, &cfg_offset);
	if (rc)
		return rc;
	h->cfgtable = remap_pci_mem(pci_resource_start(h->pdev,
		cfg_base_addr_index) + cfg_offset, sizeof(h->cfgtable));
	if (!h->cfgtable)
		return -ENOMEM;
	rc = write_driver_ver_to_cfgtable(h->cfgtable);
	if (rc)
		return rc;
	/* Find performant mode table. */
	trans_offset = readl(&h->cfgtable->TransMethodOffset);
	h->transtable = remap_pci_mem(pci_resource_start(h->pdev,
				cfg_base_addr_index)+cfg_offset+trans_offset,
				sizeof(*h->transtable));
	if (!h->transtable)
		return -ENOMEM;
	return 0;
}

static void __devinit cciss_get_max_perf_mode_cmds(struct ctlr_info *h)
{
	h->max_commands = readl(&(h->cfgtable->MaxPerformantModeCommands));

	/* Limit commands in memory limited kdump scenario. */
	if (reset_devices && h->max_commands > 32)
		h->max_commands = 32;

	if (h->max_commands < 16) {
		dev_warn(&h->pdev->dev, "Controller reports "
			"max supported commands of %d, an obvious lie. "
			"Using 16.  Ensure that firmware is up to date.\n",
			h->max_commands);
		h->max_commands = 16;
	}
}

/* Interrogate the hardware for some limits:
 * max commands, max SG elements without chaining, and with chaining,
 * SG chain block size, etc.
 */
static void __devinit cciss_find_board_params(ctlr_info_t *h)
{
	cciss_get_max_perf_mode_cmds(h);
	h->nr_cmds = h->max_commands - 4 - cciss_tape_cmds;
	h->maxsgentries = readl(&(h->cfgtable->MaxSGElements));
	/*
	 * Limit in-command s/g elements to 32 save dma'able memory.
	 * Howvever spec says if 0, use 31
	 */
	h->max_cmd_sgentries = 31;
	if (h->maxsgentries > 512) {
		h->max_cmd_sgentries = 32;
		h->chainsize = h->maxsgentries - h->max_cmd_sgentries + 1;
		h->maxsgentries--; /* save one for chain pointer */
	} else {
		h->maxsgentries = 31; /* default to traditional values */
		h->chainsize = 0;
	}
}

static inline bool CISS_signature_present(ctlr_info_t *h)
{
	if ((readb(&h->cfgtable->Signature[0]) != 'C') ||
	    (readb(&h->cfgtable->Signature[1]) != 'I') ||
	    (readb(&h->cfgtable->Signature[2]) != 'S') ||
	    (readb(&h->cfgtable->Signature[3]) != 'S')) {
		dev_warn(&h->pdev->dev, "not a valid CISS config table\n");
		return false;
	}
	return true;
}

/* Need to enable prefetch in the SCSI core for 6400 in x86 */
static inline void cciss_enable_scsi_prefetch(ctlr_info_t *h)
{
#ifdef CONFIG_X86
	u32 prefetch;

	prefetch = readl(&(h->cfgtable->SCSI_Prefetch));
	prefetch |= 0x100;
	writel(prefetch, &(h->cfgtable->SCSI_Prefetch));
#endif
}

/* Disable DMA prefetch for the P600.  Otherwise an ASIC bug may result
 * in a prefetch beyond physical memory.
 */
static inline void cciss_p600_dma_prefetch_quirk(ctlr_info_t *h)
{
	u32 dma_prefetch;
	__u32 dma_refetch;

	if (h->board_id != 0x3225103C)
		return;
	dma_prefetch = readl(h->vaddr + I2O_DMA1_CFG);
	dma_prefetch |= 0x8000;
	writel(dma_prefetch, h->vaddr + I2O_DMA1_CFG);
	pci_read_config_dword(h->pdev, PCI_COMMAND_PARITY, &dma_refetch);
	dma_refetch |= 0x1;
	pci_write_config_dword(h->pdev, PCI_COMMAND_PARITY, dma_refetch);
}

static int __devinit cciss_pci_init(ctlr_info_t *h)
{
	int prod_index, err;

	prod_index = cciss_lookup_board_id(h->pdev, &h->board_id);
	if (prod_index < 0)
		return -ENODEV;
	h->product_name = products[prod_index].product_name;
	h->access = *(products[prod_index].access);

	if (cciss_board_disabled(h)) {
		dev_warn(&h->pdev->dev, "controller appears to be disabled\n");
		return -ENODEV;
	}

	pci_disable_link_state(h->pdev, PCIE_LINK_STATE_L0S |
				PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_CLKPM);

	err = pci_enable_device(h->pdev);
	if (err) {
		dev_warn(&h->pdev->dev, "Unable to Enable PCI device\n");
		return err;
	}

	err = pci_request_regions(h->pdev, "cciss");
	if (err) {
		dev_warn(&h->pdev->dev,
			"Cannot obtain PCI resources, aborting\n");
		return err;
	}

	dev_dbg(&h->pdev->dev, "irq = %x\n", h->pdev->irq);
	dev_dbg(&h->pdev->dev, "board_id = %x\n", h->board_id);

/* If the kernel supports MSI/MSI-X we will try to enable that functionality,
 * else we use the IO-APIC interrupt assigned to us by system ROM.
 */
	cciss_interrupt_mode(h);
	err = cciss_pci_find_memory_BAR(h->pdev, &h->paddr);
	if (err)
		goto err_out_free_res;
	h->vaddr = remap_pci_mem(h->paddr, 0x250);
	if (!h->vaddr) {
		err = -ENOMEM;
		goto err_out_free_res;
	}
	err = cciss_wait_for_board_state(h->pdev, h->vaddr, BOARD_READY);
	if (err)
		goto err_out_free_res;
	err = cciss_find_cfgtables(h);
	if (err)
		goto err_out_free_res;
	print_cfg_table(h);
	cciss_find_board_params(h);

	if (!CISS_signature_present(h)) {
		err = -ENODEV;
		goto err_out_free_res;
	}
	cciss_enable_scsi_prefetch(h);
	cciss_p600_dma_prefetch_quirk(h);
	err = cciss_enter_simple_mode(h);
	if (err)
		goto err_out_free_res;
	cciss_put_controller_into_performant_mode(h);
	return 0;

err_out_free_res:
	/*
	 * Deliberately omit pci_disable_device(): it does something nasty to
	 * Smart Array controllers that pci_enable_device does not undo
	 */
	if (h->transtable)
		iounmap(h->transtable);
	if (h->cfgtable)
		iounmap(h->cfgtable);
	if (h->vaddr)
		iounmap(h->vaddr);
	pci_release_regions(h->pdev);
	return err;
}

/* Function to find the first free pointer into our hba[] array
 * Returns -1 if no free entries are left.
 */
static int alloc_cciss_hba(struct pci_dev *pdev)
{
	int i;

	for (i = 0; i < MAX_CTLR; i++) {
		if (!hba[i]) {
			ctlr_info_t *h;

			h = kzalloc(sizeof(ctlr_info_t), GFP_KERNEL);
			if (!h)
				goto Enomem;
			hba[i] = h;
			return i;
		}
	}
	dev_warn(&pdev->dev, "This driver supports a maximum"
	       " of %d controllers.\n", MAX_CTLR);
	return -1;
Enomem:
	dev_warn(&pdev->dev, "out of memory.\n");
	return -1;
}

static void free_hba(ctlr_info_t *h)
{
	int i;

	hba[h->ctlr] = NULL;
	for (i = 0; i < h->highest_lun + 1; i++)
		if (h->gendisk[i] != NULL)
			put_disk(h->gendisk[i]);
	kfree(h);
}

/* Send a message CDB to the firmware. */
static __devinit int cciss_message(struct pci_dev *pdev, unsigned char opcode, unsigned char type)
{
	typedef struct {
		CommandListHeader_struct CommandHeader;
		RequestBlock_struct Request;
		ErrDescriptor_struct ErrorDescriptor;
	} Command;
	static const size_t cmd_sz = sizeof(Command) + sizeof(ErrorInfo_struct);
	Command *cmd;
	dma_addr_t paddr64;
	uint32_t paddr32, tag;
	void __iomem *vaddr;
	int i, err;

	vaddr = ioremap_nocache(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	if (vaddr == NULL)
		return -ENOMEM;

	/* The Inbound Post Queue only accepts 32-bit physical addresses for the
	   CCISS commands, so they must be allocated from the lower 4GiB of
	   memory. */
	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err) {
		iounmap(vaddr);
		return -ENOMEM;
	}

	cmd = pci_alloc_consistent(pdev, cmd_sz, &paddr64);
	if (cmd == NULL) {
		iounmap(vaddr);
		return -ENOMEM;
	}

	/* This must fit, because of the 32-bit consistent DMA mask.  Also,
	   although there's no guarantee, we assume that the address is at
	   least 4-byte aligned (most likely, it's page-aligned). */
	paddr32 = paddr64;

	cmd->CommandHeader.ReplyQueue = 0;
	cmd->CommandHeader.SGList = 0;
	cmd->CommandHeader.SGTotal = 0;
	cmd->CommandHeader.Tag.lower = paddr32;
	cmd->CommandHeader.Tag.upper = 0;
	memset(&cmd->CommandHeader.LUN.LunAddrBytes, 0, 8);

	cmd->Request.CDBLen = 16;
	cmd->Request.Type.Type = TYPE_MSG;
	cmd->Request.Type.Attribute = ATTR_HEADOFQUEUE;
	cmd->Request.Type.Direction = XFER_NONE;
	cmd->Request.Timeout = 0; /* Don't time out */
	cmd->Request.CDB[0] = opcode;
	cmd->Request.CDB[1] = type;
	memset(&cmd->Request.CDB[2], 0, 14); /* the rest of the CDB is reserved */

	cmd->ErrorDescriptor.Addr.lower = paddr32 + sizeof(Command);
	cmd->ErrorDescriptor.Addr.upper = 0;
	cmd->ErrorDescriptor.Len = sizeof(ErrorInfo_struct);

	writel(paddr32, vaddr + SA5_REQUEST_PORT_OFFSET);

	for (i = 0; i < 10; i++) {
		tag = readl(vaddr + SA5_REPLY_PORT_OFFSET);
		if ((tag & ~3) == paddr32)
			break;
		msleep(CCISS_POST_RESET_NOOP_TIMEOUT_MSECS);
	}

	iounmap(vaddr);

	/* we leak the DMA buffer here ... no choice since the controller could
	   still complete the command. */
	if (i == 10) {
		dev_err(&pdev->dev,
			"controller message %02x:%02x timed out\n",
			opcode, type);
		return -ETIMEDOUT;
	}

	pci_free_consistent(pdev, cmd_sz, cmd, paddr64);

	if (tag & 2) {
		dev_err(&pdev->dev, "controller message %02x:%02x failed\n",
			opcode, type);
		return -EIO;
	}

	dev_info(&pdev->dev, "controller message %02x:%02x succeeded\n",
		opcode, type);
	return 0;
}

#define cciss_noop(p) cciss_message(p, 3, 0)

static int cciss_controller_hard_reset(struct pci_dev *pdev,
	void * __iomem vaddr, u32 use_doorbell)
{
	u16 pmcsr;
	int pos;

	if (use_doorbell) {
		/* For everything after the P600, the PCI power state method
		 * of resetting the controller doesn't work, so we have this
		 * other way using the doorbell register.
		 */
		dev_info(&pdev->dev, "using doorbell to reset controller\n");
		writel(use_doorbell, vaddr + SA5_DOORBELL);
	} else { /* Try to do it the PCI power state way */

		/* Quoting from the Open CISS Specification: "The Power
		 * Management Control/Status Register (CSR) controls the power
		 * state of the device.  The normal operating state is D0,
		 * CSR=00h.  The software off state is D3, CSR=03h.  To reset
		 * the controller, place the interface device in D3 then to D0,
		 * this causes a secondary PCI reset which will reset the
		 * controller." */

		pos = pci_find_capability(pdev, PCI_CAP_ID_PM);
		if (pos == 0) {
			dev_err(&pdev->dev,
				"cciss_controller_hard_reset: "
				"PCI PM not supported\n");
			return -ENODEV;
		}
		dev_info(&pdev->dev, "using PCI PM to reset controller\n");
		/* enter the D3hot power management state */
		pci_read_config_word(pdev, pos + PCI_PM_CTRL, &pmcsr);
		pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
		pmcsr |= PCI_D3hot;
		pci_write_config_word(pdev, pos + PCI_PM_CTRL, pmcsr);

		msleep(500);

		/* enter the D0 power management state */
		pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
		pmcsr |= PCI_D0;
		pci_write_config_word(pdev, pos + PCI_PM_CTRL, pmcsr);

		/*
		 * The P600 requires a small delay when changing states.
		 * Otherwise we may think the board did not reset and we bail.
		 * This for kdump only and is particular to the P600.
		 */
		msleep(500);
	}
	return 0;
}

static __devinit void init_driver_version(char *driver_version, int len)
{
	memset(driver_version, 0, len);
	strncpy(driver_version, "cciss " DRIVER_NAME, len - 1);
}

static __devinit int write_driver_ver_to_cfgtable(
	CfgTable_struct __iomem *cfgtable)
{
	char *driver_version;
	int i, size = sizeof(cfgtable->driver_version);

	driver_version = kmalloc(size, GFP_KERNEL);
	if (!driver_version)
		return -ENOMEM;

	init_driver_version(driver_version, size);
	for (i = 0; i < size; i++)
		writeb(driver_version[i], &cfgtable->driver_version[i]);
	kfree(driver_version);
	return 0;
}

static __devinit void read_driver_ver_from_cfgtable(
	CfgTable_struct __iomem *cfgtable, unsigned char *driver_ver)
{
	int i;

	for (i = 0; i < sizeof(cfgtable->driver_version); i++)
		driver_ver[i] = readb(&cfgtable->driver_version[i]);
}

static __devinit int controller_reset_failed(
	CfgTable_struct __iomem *cfgtable)
{

	char *driver_ver, *old_driver_ver;
	int rc, size = sizeof(cfgtable->driver_version);

	old_driver_ver = kmalloc(2 * size, GFP_KERNEL);
	if (!old_driver_ver)
		return -ENOMEM;
	driver_ver = old_driver_ver + size;

	/* After a reset, the 32 bytes of "driver version" in the cfgtable
	 * should have been changed, otherwise we know the reset failed.
	 */
	init_driver_version(old_driver_ver, size);
	read_driver_ver_from_cfgtable(cfgtable, driver_ver);
	rc = !memcmp(driver_ver, old_driver_ver, size);
	kfree(old_driver_ver);
	return rc;
}

/* This does a hard reset of the controller using PCI power management
 * states or using the doorbell register. */
static __devinit int cciss_kdump_hard_reset_controller(struct pci_dev *pdev)
{
	u64 cfg_offset;
	u32 cfg_base_addr;
	u64 cfg_base_addr_index;
	void __iomem *vaddr;
	unsigned long paddr;
	u32 misc_fw_support;
	int rc;
	CfgTable_struct __iomem *cfgtable;
	u32 use_doorbell;
	u32 board_id;
	u16 command_register;

	/* For controllers as old a the p600, this is very nearly
	 * the same thing as
	 *
	 * pci_save_state(pci_dev);
	 * pci_set_power_state(pci_dev, PCI_D3hot);
	 * pci_set_power_state(pci_dev, PCI_D0);
	 * pci_restore_state(pci_dev);
	 *
	 * For controllers newer than the P600, the pci power state
	 * method of resetting doesn't work so we have another way
	 * using the doorbell register.
	 */

	/* Exclude 640x boards.  These are two pci devices in one slot
	 * which share a battery backed cache module.  One controls the
	 * cache, the other accesses the cache through the one that controls
	 * it.  If we reset the one controlling the cache, the other will
	 * likely not be happy.  Just forbid resetting this conjoined mess.
	 */
	cciss_lookup_board_id(pdev, &board_id);
	if (!ctlr_is_resettable(board_id)) {
		dev_warn(&pdev->dev, "Cannot reset Smart Array 640x "
				"due to shared cache module.");
		return -ENODEV;
	}

	/* if controller is soft- but not hard resettable... */
	if (!ctlr_is_hard_resettable(board_id))
		return -ENOTSUPP; /* try soft reset later. */

	/* Save the PCI command register */
	pci_read_config_word(pdev, 4, &command_register);
	/* Turn the board off.  This is so that later pci_restore_state()
	 * won't turn the board on before the rest of config space is ready.
	 */
	pci_disable_device(pdev);
	pci_save_state(pdev);

	/* find the first memory BAR, so we can find the cfg table */
	rc = cciss_pci_find_memory_BAR(pdev, &paddr);
	if (rc)
		return rc;
	vaddr = remap_pci_mem(paddr, 0x250);
	if (!vaddr)
		return -ENOMEM;

	/* find cfgtable in order to check if reset via doorbell is supported */
	rc = cciss_find_cfg_addrs(pdev, vaddr, &cfg_base_addr,
					&cfg_base_addr_index, &cfg_offset);
	if (rc)
		goto unmap_vaddr;
	cfgtable = remap_pci_mem(pci_resource_start(pdev,
		       cfg_base_addr_index) + cfg_offset, sizeof(*cfgtable));
	if (!cfgtable) {
		rc = -ENOMEM;
		goto unmap_vaddr;
	}
	rc = write_driver_ver_to_cfgtable(cfgtable);
	if (rc)
		goto unmap_vaddr;

	/* If reset via doorbell register is supported, use that.
	 * There are two such methods.  Favor the newest method.
	 */
	misc_fw_support = readl(&cfgtable->misc_fw_support);
	use_doorbell = misc_fw_support & MISC_FW_DOORBELL_RESET2;
	if (use_doorbell) {
		use_doorbell = DOORBELL_CTLR_RESET2;
	} else {
		use_doorbell = misc_fw_support & MISC_FW_DOORBELL_RESET;
		if (use_doorbell) {
			dev_warn(&pdev->dev, "Controller claims that "
				"'Bit 2 doorbell reset' is "
				"supported, but not 'bit 5 doorbell reset'.  "
				"Firmware update is recommended.\n");
			rc = -ENOTSUPP; /* use the soft reset */
			goto unmap_cfgtable;
		}
	}

	rc = cciss_controller_hard_reset(pdev, vaddr, use_doorbell);
	if (rc)
		goto unmap_cfgtable;
	pci_restore_state(pdev);
	rc = pci_enable_device(pdev);
	if (rc) {
		dev_warn(&pdev->dev, "failed to enable device.\n");
		goto unmap_cfgtable;
	}
	pci_write_config_word(pdev, 4, command_register);

	/* Some devices (notably the HP Smart Array 5i Controller)
	   need a little pause here */
	msleep(CCISS_POST_RESET_PAUSE_MSECS);

	/* Wait for board to become not ready, then ready. */
	dev_info(&pdev->dev, "Waiting for board to reset.\n");
	rc = cciss_wait_for_board_state(pdev, vaddr, BOARD_NOT_READY);
	if (rc) {
		dev_warn(&pdev->dev, "Failed waiting for board to hard reset."
				"  Will try soft reset.\n");
		rc = -ENOTSUPP; /* Not expected, but try soft reset later */
		goto unmap_cfgtable;
	}
	rc = cciss_wait_for_board_state(pdev, vaddr, BOARD_READY);
	if (rc) {
		dev_warn(&pdev->dev,
			"failed waiting for board to become ready "
			"after hard reset\n");
		goto unmap_cfgtable;
	}

	rc = controller_reset_failed(vaddr);
	if (rc < 0)
		goto unmap_cfgtable;
	if (rc) {
		dev_warn(&pdev->dev, "Unable to successfully hard reset "
			"controller. Will try soft reset.\n");
		rc = -ENOTSUPP; /* Not expected, but try soft reset later */
	} else {
		dev_info(&pdev->dev, "Board ready after hard reset.\n");
	}

unmap_cfgtable:
	iounmap(cfgtable);

unmap_vaddr:
	iounmap(vaddr);
	return rc;
}

static __devinit int cciss_init_reset_devices(struct pci_dev *pdev)
{
	int rc, i;

	if (!reset_devices)
		return 0;

	/* Reset the controller with a PCI power-cycle or via doorbell */
	rc = cciss_kdump_hard_reset_controller(pdev);

	/* -ENOTSUPP here means we cannot reset the controller
	 * but it's already (and still) up and running in
	 * "performant mode".  Or, it might be 640x, which can't reset
	 * due to concerns about shared bbwc between 6402/6404 pair.
	 */
	if (rc == -ENOTSUPP)
		return rc; /* just try to do the kdump anyhow. */
	if (rc)
		return -ENODEV;

	/* Now try to get the controller to respond to a no-op */
	dev_warn(&pdev->dev, "Waiting for controller to respond to no-op\n");
	for (i = 0; i < CCISS_POST_RESET_NOOP_RETRIES; i++) {
		if (cciss_noop(pdev) == 0)
			break;
		else
			dev_warn(&pdev->dev, "no-op failed%s\n",
				(i < CCISS_POST_RESET_NOOP_RETRIES - 1 ?
					"; re-trying" : ""));
		msleep(CCISS_POST_RESET_NOOP_INTERVAL_MSECS);
	}
	return 0;
}

static __devinit int cciss_allocate_cmd_pool(ctlr_info_t *h)
{
	h->cmd_pool_bits = kmalloc(
		DIV_ROUND_UP(h->nr_cmds, BITS_PER_LONG) *
		sizeof(unsigned long), GFP_KERNEL);
	h->cmd_pool = pci_alloc_consistent(h->pdev,
		h->nr_cmds * sizeof(CommandList_struct),
		&(h->cmd_pool_dhandle));
	h->errinfo_pool = pci_alloc_consistent(h->pdev,
		h->nr_cmds * sizeof(ErrorInfo_struct),
		&(h->errinfo_pool_dhandle));
	if ((h->cmd_pool_bits == NULL)
		|| (h->cmd_pool == NULL)
		|| (h->errinfo_pool == NULL)) {
		dev_err(&h->pdev->dev, "out of memory");
		return -ENOMEM;
	}
	return 0;
}

static __devinit int cciss_allocate_scatterlists(ctlr_info_t *h)
{
	int i;

	/* zero it, so that on free we need not know how many were alloc'ed */
	h->scatter_list = kzalloc(h->max_commands *
				sizeof(struct scatterlist *), GFP_KERNEL);
	if (!h->scatter_list)
		return -ENOMEM;

	for (i = 0; i < h->nr_cmds; i++) {
		h->scatter_list[i] = kmalloc(sizeof(struct scatterlist) *
						h->maxsgentries, GFP_KERNEL);
		if (h->scatter_list[i] == NULL) {
			dev_err(&h->pdev->dev, "could not allocate "
				"s/g lists\n");
			return -ENOMEM;
		}
	}
	return 0;
}

static void cciss_free_scatterlists(ctlr_info_t *h)
{
	int i;

	if (h->scatter_list) {
		for (i = 0; i < h->nr_cmds; i++)
			kfree(h->scatter_list[i]);
		kfree(h->scatter_list);
	}
}

static void cciss_free_cmd_pool(ctlr_info_t *h)
{
	kfree(h->cmd_pool_bits);
	if (h->cmd_pool)
		pci_free_consistent(h->pdev,
			h->nr_cmds * sizeof(CommandList_struct),
			h->cmd_pool, h->cmd_pool_dhandle);
	if (h->errinfo_pool)
		pci_free_consistent(h->pdev,
			h->nr_cmds * sizeof(ErrorInfo_struct),
			h->errinfo_pool, h->errinfo_pool_dhandle);
}

static int cciss_request_irq(ctlr_info_t *h,
	irqreturn_t (*msixhandler)(int, void *),
	irqreturn_t (*intxhandler)(int, void *))
{
	if (h->msix_vector || h->msi_vector) {
		if (!request_irq(h->intr[h->intr_mode], msixhandler,
				0, h->devname, h))
			return 0;
		dev_err(&h->pdev->dev, "Unable to get msi irq %d"
			" for %s\n", h->intr[h->intr_mode],
			h->devname);
		return -1;
	}

	if (!request_irq(h->intr[h->intr_mode], intxhandler,
			IRQF_SHARED, h->devname, h))
		return 0;
	dev_err(&h->pdev->dev, "Unable to get irq %d for %s\n",
		h->intr[h->intr_mode], h->devname);
	return -1;
}

static int __devinit cciss_kdump_soft_reset(ctlr_info_t *h)
{
	if (cciss_send_reset(h, CTLR_LUNID, CCISS_RESET_TYPE_CONTROLLER)) {
		dev_warn(&h->pdev->dev, "Resetting array controller failed.\n");
		return -EIO;
	}

	dev_info(&h->pdev->dev, "Waiting for board to soft reset.\n");
	if (cciss_wait_for_board_state(h->pdev, h->vaddr, BOARD_NOT_READY)) {
		dev_warn(&h->pdev->dev, "Soft reset had no effect.\n");
		return -1;
	}

	dev_info(&h->pdev->dev, "Board reset, awaiting READY status.\n");
	if (cciss_wait_for_board_state(h->pdev, h->vaddr, BOARD_READY)) {
		dev_warn(&h->pdev->dev, "Board failed to become ready "
			"after soft reset.\n");
		return -1;
	}

	return 0;
}

static void cciss_undo_allocations_after_kdump_soft_reset(ctlr_info_t *h)
{
	int ctlr = h->ctlr;

	free_irq(h->intr[h->intr_mode], h);
#ifdef CONFIG_PCI_MSI
	if (h->msix_vector)
		pci_disable_msix(h->pdev);
	else if (h->msi_vector)
		pci_disable_msi(h->pdev);
#endif /* CONFIG_PCI_MSI */
	cciss_free_sg_chain_blocks(h->cmd_sg_list, h->nr_cmds);
	cciss_free_scatterlists(h);
	cciss_free_cmd_pool(h);
	kfree(h->blockFetchTable);
	if (h->reply_pool)
		pci_free_consistent(h->pdev, h->max_commands * sizeof(__u64),
				h->reply_pool, h->reply_pool_dhandle);
	if (h->transtable)
		iounmap(h->transtable);
	if (h->cfgtable)
		iounmap(h->cfgtable);
	if (h->vaddr)
		iounmap(h->vaddr);
	unregister_blkdev(h->major, h->devname);
	cciss_destroy_hba_sysfs_entry(h);
	pci_release_regions(h->pdev);
	kfree(h);
	hba[ctlr] = NULL;
}

/*
 *  This is it.  Find all the controllers and register them.  I really hate
 *  stealing all these major device numbers.
 *  returns the number of block devices registered.
 */
static int __devinit cciss_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	int i;
	int j = 0;
	int rc;
	int try_soft_reset = 0;
	int dac, return_code;
	InquiryData_struct *inq_buff;
	ctlr_info_t *h;
	unsigned long flags;

	rc = cciss_init_reset_devices(pdev);
	if (rc) {
		if (rc != -ENOTSUPP)
			return rc;
		/* If the reset fails in a particular way (it has no way to do
		 * a proper hard reset, so returns -ENOTSUPP) we can try to do
		 * a soft reset once we get the controller configured up to the
		 * point that it can accept a command.
		 */
		try_soft_reset = 1;
		rc = 0;
	}

reinit_after_soft_reset:

	i = alloc_cciss_hba(pdev);
	if (i < 0)
		return -1;

	h = hba[i];
	h->pdev = pdev;
	h->busy_initializing = 1;
	h->intr_mode = cciss_simple_mode ? SIMPLE_MODE_INT : PERF_MODE_INT;
	INIT_LIST_HEAD(&h->cmpQ);
	INIT_LIST_HEAD(&h->reqQ);
	mutex_init(&h->busy_shutting_down);

	if (cciss_pci_init(h) != 0)
		goto clean_no_release_regions;

	sprintf(h->devname, "cciss%d", i);
	h->ctlr = i;

	if (cciss_tape_cmds < 2)
		cciss_tape_cmds = 2;
	if (cciss_tape_cmds > 16)
		cciss_tape_cmds = 16;

	init_completion(&h->scan_wait);

	if (cciss_create_hba_sysfs_entry(h))
		goto clean0;

	/* configure PCI DMA stuff */
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))
		dac = 1;
	else if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))
		dac = 0;
	else {
		dev_err(&h->pdev->dev, "no suitable DMA available\n");
		goto clean1;
	}

	/*
	 * register with the major number, or get a dynamic major number
	 * by passing 0 as argument.  This is done for greater than
	 * 8 controller support.
	 */
	if (i < MAX_CTLR_ORIG)
		h->major = COMPAQ_CISS_MAJOR + i;
	rc = register_blkdev(h->major, h->devname);
	if (rc == -EBUSY || rc == -EINVAL) {
		dev_err(&h->pdev->dev,
		       "Unable to get major number %d for %s "
		       "on hba %d\n", h->major, h->devname, i);
		goto clean1;
	} else {
		if (i >= MAX_CTLR_ORIG)
			h->major = rc;
	}

	/* make sure the board interrupts are off */
	h->access.set_intr_mask(h, CCISS_INTR_OFF);
	rc = cciss_request_irq(h, do_cciss_msix_intr, do_cciss_intx);
	if (rc)
		goto clean2;

	dev_info(&h->pdev->dev, "%s: <0x%x> at PCI %s IRQ %d%s using DAC\n",
	       h->devname, pdev->device, pci_name(pdev),
	       h->intr[h->intr_mode], dac ? "" : " not");

	if (cciss_allocate_cmd_pool(h))
		goto clean4;

	if (cciss_allocate_scatterlists(h))
		goto clean4;

	h->cmd_sg_list = cciss_allocate_sg_chain_blocks(h,
		h->chainsize, h->nr_cmds);
	if (!h->cmd_sg_list && h->chainsize > 0)
		goto clean4;

	spin_lock_init(&h->lock);

	/* Initialize the pdev driver private data.
	   have it point to h.  */
	pci_set_drvdata(pdev, h);
	/* command and error info recs zeroed out before
	   they are used */
	memset(h->cmd_pool_bits, 0,
	       DIV_ROUND_UP(h->nr_cmds, BITS_PER_LONG)
			* sizeof(unsigned long));

	h->num_luns = 0;
	h->highest_lun = -1;
	for (j = 0; j < CISS_MAX_LUN; j++) {
		h->drv[j] = NULL;
		h->gendisk[j] = NULL;
	}

	/* At this point, the controller is ready to take commands.
	 * Now, if reset_devices and the hard reset didn't work, try
	 * the soft reset and see if that works.
	 */
	if (try_soft_reset) {

		/* This is kind of gross.  We may or may not get a completion
		 * from the soft reset command, and if we do, then the value
		 * from the fifo may or may not be valid.  So, we wait 10 secs
		 * after the reset throwing away any completions we get during
		 * that time.  Unregister the interrupt handler and register
		 * fake ones to scoop up any residual completions.
		 */
		spin_lock_irqsave(&h->lock, flags);
		h->access.set_intr_mask(h, CCISS_INTR_OFF);
		spin_unlock_irqrestore(&h->lock, flags);
		free_irq(h->intr[h->intr_mode], h);
		rc = cciss_request_irq(h, cciss_msix_discard_completions,
					cciss_intx_discard_completions);
		if (rc) {
			dev_warn(&h->pdev->dev, "Failed to request_irq after "
				"soft reset.\n");
			goto clean4;
		}

		rc = cciss_kdump_soft_reset(h);
		if (rc) {
			dev_warn(&h->pdev->dev, "Soft reset failed.\n");
			goto clean4;
		}

		dev_info(&h->pdev->dev, "Board READY.\n");
		dev_info(&h->pdev->dev,
			"Waiting for stale completions to drain.\n");
		h->access.set_intr_mask(h, CCISS_INTR_ON);
		msleep(10000);
		h->access.set_intr_mask(h, CCISS_INTR_OFF);

		rc = controller_reset_failed(h->cfgtable);
		if (rc)
			dev_info(&h->pdev->dev,
				"Soft reset appears to have failed.\n");

		/* since the controller's reset, we have to go back and re-init
		 * everything.  Easiest to just forget what we've done and do it
		 * all over again.
		 */
		cciss_undo_allocations_after_kdump_soft_reset(h);
		try_soft_reset = 0;
		if (rc)
			/* don't go to clean4, we already unallocated */
			return -ENODEV;

		goto reinit_after_soft_reset;
	}

	cciss_scsi_setup(h);

	/* Turn the interrupts on so we can service requests */
	h->access.set_intr_mask(h, CCISS_INTR_ON);

	/* Get the firmware version */
	inq_buff = kzalloc(sizeof(InquiryData_struct), GFP_KERNEL);
	if (inq_buff == NULL) {
		dev_err(&h->pdev->dev, "out of memory\n");
		goto clean4;
	}

	return_code = sendcmd_withirq(h, CISS_INQUIRY, inq_buff,
		sizeof(InquiryData_struct), 0, CTLR_LUNID, TYPE_CMD);
	if (return_code == IO_OK) {
		h->firm_ver[0] = inq_buff->data_byte[32];
		h->firm_ver[1] = inq_buff->data_byte[33];
		h->firm_ver[2] = inq_buff->data_byte[34];
		h->firm_ver[3] = inq_buff->data_byte[35];
	} else {	 /* send command failed */
		dev_warn(&h->pdev->dev, "unable to determine firmware"
			" version of controller\n");
	}
	kfree(inq_buff);

	cciss_procinit(h);

	h->cciss_max_sectors = 8192;

	rebuild_lun_table(h, 1, 0);
	cciss_engage_scsi(h);
	h->busy_initializing = 0;
	return 1;

clean4:
	cciss_free_cmd_pool(h);
	cciss_free_scatterlists(h);
	cciss_free_sg_chain_blocks(h->cmd_sg_list, h->nr_cmds);
	free_irq(h->intr[h->intr_mode], h);
clean2:
	unregister_blkdev(h->major, h->devname);
clean1:
	cciss_destroy_hba_sysfs_entry(h);
clean0:
	pci_release_regions(pdev);
clean_no_release_regions:
	h->busy_initializing = 0;

	/*
	 * Deliberately omit pci_disable_device(): it does something nasty to
	 * Smart Array controllers that pci_enable_device does not undo
	 */
	pci_set_drvdata(pdev, NULL);
	free_hba(h);
	return -1;
}

static void cciss_shutdown(struct pci_dev *pdev)
{
	ctlr_info_t *h;
	char *flush_buf;
	int return_code;

	h = pci_get_drvdata(pdev);
	flush_buf = kzalloc(4, GFP_KERNEL);
	if (!flush_buf) {
		dev_warn(&h->pdev->dev, "cache not flushed, out of memory.\n");
		return;
	}
	/* write all data in the battery backed cache to disk */
	memset(flush_buf, 0, 4);
	return_code = sendcmd_withirq(h, CCISS_CACHE_FLUSH, flush_buf,
		4, 0, CTLR_LUNID, TYPE_CMD);
	kfree(flush_buf);
	if (return_code != IO_OK)
		dev_warn(&h->pdev->dev, "Error flushing cache\n");
	h->access.set_intr_mask(h, CCISS_INTR_OFF);
	free_irq(h->intr[h->intr_mode], h);
}

static int __devinit cciss_enter_simple_mode(struct ctlr_info *h)
{
	u32 trans_support;

	trans_support = readl(&(h->cfgtable->TransportSupport));
	if (!(trans_support & SIMPLE_MODE))
		return -ENOTSUPP;

	h->max_commands = readl(&(h->cfgtable->CmdsOutMax));
	writel(CFGTBL_Trans_Simple, &(h->cfgtable->HostWrite.TransportRequest));
	writel(CFGTBL_ChangeReq, h->vaddr + SA5_DOORBELL);
	cciss_wait_for_mode_change_ack(h);
	print_cfg_table(h);
	if (!(readl(&(h->cfgtable->TransportActive)) & CFGTBL_Trans_Simple)) {
		dev_warn(&h->pdev->dev, "unable to get board into simple mode\n");
		return -ENODEV;
	}
	h->transMethod = CFGTBL_Trans_Simple;
	return 0;
}


static void __devexit cciss_remove_one(struct pci_dev *pdev)
{
	ctlr_info_t *h;
	int i, j;

	if (pci_get_drvdata(pdev) == NULL) {
		dev_err(&pdev->dev, "Unable to remove device\n");
		return;
	}

	h = pci_get_drvdata(pdev);
	i = h->ctlr;
	if (hba[i] == NULL) {
		dev_err(&pdev->dev, "device appears to already be removed\n");
		return;
	}

	mutex_lock(&h->busy_shutting_down);

	remove_from_scan_list(h);
	remove_proc_entry(h->devname, proc_cciss);
	unregister_blkdev(h->major, h->devname);

	/* remove it from the disk list */
	for (j = 0; j < CISS_MAX_LUN; j++) {
		struct gendisk *disk = h->gendisk[j];
		if (disk) {
			struct request_queue *q = disk->queue;

			if (disk->flags & GENHD_FL_UP) {
				cciss_destroy_ld_sysfs_entry(h, j, 1);
				del_gendisk(disk);
			}
			if (q)
				blk_cleanup_queue(q);
		}
	}

#ifdef CONFIG_CISS_SCSI_TAPE
	cciss_unregister_scsi(h);	/* unhook from SCSI subsystem */
#endif

	cciss_shutdown(pdev);

#ifdef CONFIG_PCI_MSI
	if (h->msix_vector)
		pci_disable_msix(h->pdev);
	else if (h->msi_vector)
		pci_disable_msi(h->pdev);
#endif				/* CONFIG_PCI_MSI */

	iounmap(h->transtable);
	iounmap(h->cfgtable);
	iounmap(h->vaddr);

	cciss_free_cmd_pool(h);
	/* Free up sg elements */
	for (j = 0; j < h->nr_cmds; j++)
		kfree(h->scatter_list[j]);
	kfree(h->scatter_list);
	cciss_free_sg_chain_blocks(h->cmd_sg_list, h->nr_cmds);
	kfree(h->blockFetchTable);
	if (h->reply_pool)
		pci_free_consistent(h->pdev, h->max_commands * sizeof(__u64),
				h->reply_pool, h->reply_pool_dhandle);
	/*
	 * Deliberately omit pci_disable_device(): it does something nasty to
	 * Smart Array controllers that pci_enable_device does not undo
	 */
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	cciss_destroy_hba_sysfs_entry(h);
	mutex_unlock(&h->busy_shutting_down);
	free_hba(h);
}

static struct pci_driver cciss_pci_driver = {
	.name = "cciss",
	.probe = cciss_init_one,
	.remove = __devexit_p(cciss_remove_one),
	.id_table = cciss_pci_device_id,	/* id_table */
	.shutdown = cciss_shutdown,
};

/*
 *  This is it.  Register the PCI driver information for the cards we control
 *  the OS will call our registered routines when it finds one of our cards.
 */
static int __init cciss_init(void)
{
	int err;

	/*
	 * The hardware requires that commands are aligned on a 64-bit
	 * boundary. Given that we use pci_alloc_consistent() to allocate an
	 * array of them, the size must be a multiple of 8 bytes.
	 */
	BUILD_BUG_ON(sizeof(CommandList_struct) % COMMANDLIST_ALIGNMENT);
	printk(KERN_INFO DRIVER_NAME "\n");

	err = bus_register(&cciss_bus_type);
	if (err)
		return err;

	/* Start the scan thread */
	cciss_scan_thread = kthread_run(scan_thread, NULL, "cciss_scan");
	if (IS_ERR(cciss_scan_thread)) {
		err = PTR_ERR(cciss_scan_thread);
		goto err_bus_unregister;
	}

	/* Register for our PCI devices */
	err = pci_register_driver(&cciss_pci_driver);
	if (err)
		goto err_thread_stop;

	return err;

err_thread_stop:
	kthread_stop(cciss_scan_thread);
err_bus_unregister:
	bus_unregister(&cciss_bus_type);

	return err;
}

static void __exit cciss_cleanup(void)
{
	int i;

	pci_unregister_driver(&cciss_pci_driver);
	/* double check that all controller entrys have been removed */
	for (i = 0; i < MAX_CTLR; i++) {
		if (hba[i] != NULL) {
			dev_warn(&hba[i]->pdev->dev,
				"had to remove controller\n");
			cciss_remove_one(hba[i]->pdev);
		}
	}
	kthread_stop(cciss_scan_thread);
	if (proc_cciss)
		remove_proc_entry("driver/cciss", NULL);
	bus_unregister(&cciss_bus_type);
}

module_init(cciss_init);
module_exit(cciss_cleanup);
