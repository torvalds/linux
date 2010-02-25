/*
 *    Disk Array driver for HP Smart Array SAS controllers
 *    Copyright 2000, 2009 Hewlett-Packard Development Company, L.P.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/compat.h>
#include <linux/blktrace_api.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <linux/cciss_ioctl.h>
#include <linux/string.h>
#include <linux/bitmap.h>
#include <asm/atomic.h>
#include <linux/kthread.h>
#include "hpsa_cmd.h"
#include "hpsa.h"

/* HPSA_DRIVER_VERSION must be 3 byte values (0-255) separated by '.' */
#define HPSA_DRIVER_VERSION "2.0.2-1"
#define DRIVER_NAME "HP HPSA Driver (v " HPSA_DRIVER_VERSION ")"

/* How long to wait (in milliseconds) for board to go into simple mode */
#define MAX_CONFIG_WAIT 30000
#define MAX_IOCTL_CONFIG_WAIT 1000

/*define how many times we will try a command because of bus resets */
#define MAX_CMD_RETRIES 3

/* Embedded module documentation macros - see modules.h */
MODULE_AUTHOR("Hewlett-Packard Company");
MODULE_DESCRIPTION("Driver for HP Smart Array Controller version " \
	HPSA_DRIVER_VERSION);
MODULE_SUPPORTED_DEVICE("HP Smart Array Controllers");
MODULE_VERSION(HPSA_DRIVER_VERSION);
MODULE_LICENSE("GPL");

static int hpsa_allow_any;
module_param(hpsa_allow_any, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(hpsa_allow_any,
		"Allow hpsa driver to access unknown HP Smart Array hardware");

/* define the PCI info for the cards we can control */
static const struct pci_device_id hpsa_pci_device_id[] = {
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x3241},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x3243},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x3245},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x3247},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x3249},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x324a},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x324b},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSE,     0x103C, 0x3233},
#define PCI_DEVICE_ID_HP_CISSF 0x333f
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_CISSF,     0x103C, 0x333F},
	{PCI_VENDOR_ID_HP,     PCI_ANY_ID,             PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_RAID << 8, 0xffff << 8, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, hpsa_pci_device_id);

/*  board_id = Subsystem Device ID & Vendor ID
 *  product = Marketing Name for the board
 *  access = Address of the struct of function pointers
 */
static struct board_type products[] = {
	{0x3241103C, "Smart Array P212", &SA5_access},
	{0x3243103C, "Smart Array P410", &SA5_access},
	{0x3245103C, "Smart Array P410i", &SA5_access},
	{0x3247103C, "Smart Array P411", &SA5_access},
	{0x3249103C, "Smart Array P812", &SA5_access},
	{0x324a103C, "Smart Array P712m", &SA5_access},
	{0x324b103C, "Smart Array P711m", &SA5_access},
	{0x3233103C, "StorageWorks P1210m", &SA5_access},
	{0x333F103C, "StorageWorks P1210m", &SA5_access},
	{0xFFFF103C, "Unknown Smart Array", &SA5_access},
};

static int number_of_controllers;

static irqreturn_t do_hpsa_intr(int irq, void *dev_id);
static int hpsa_ioctl(struct scsi_device *dev, int cmd, void *arg);
static void start_io(struct ctlr_info *h);

#ifdef CONFIG_COMPAT
static int hpsa_compat_ioctl(struct scsi_device *dev, int cmd, void *arg);
#endif

static void cmd_free(struct ctlr_info *h, struct CommandList *c);
static void cmd_special_free(struct ctlr_info *h, struct CommandList *c);
static struct CommandList *cmd_alloc(struct ctlr_info *h);
static struct CommandList *cmd_special_alloc(struct ctlr_info *h);
static void fill_cmd(struct CommandList *c, u8 cmd, struct ctlr_info *h,
	void *buff, size_t size, u8 page_code, unsigned char *scsi3addr,
	int cmd_type);

static int hpsa_scsi_queue_command(struct scsi_cmnd *cmd,
		void (*done)(struct scsi_cmnd *));
static void hpsa_scan_start(struct Scsi_Host *);
static int hpsa_scan_finished(struct Scsi_Host *sh,
	unsigned long elapsed_time);
static int hpsa_change_queue_depth(struct scsi_device *sdev,
	int qdepth, int reason);

static int hpsa_eh_device_reset_handler(struct scsi_cmnd *scsicmd);
static int hpsa_slave_alloc(struct scsi_device *sdev);
static void hpsa_slave_destroy(struct scsi_device *sdev);

static ssize_t raid_level_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t lunid_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t unique_id_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static void hpsa_update_scsi_devices(struct ctlr_info *h, int hostno);
static ssize_t host_store_rescan(struct device *dev,
	 struct device_attribute *attr, const char *buf, size_t count);
static int check_for_unit_attention(struct ctlr_info *h,
	struct CommandList *c);
static void check_ioctl_unit_attention(struct ctlr_info *h,
	struct CommandList *c);
/* performant mode helper functions */
static void calc_bucket_map(int *bucket, int num_buckets,
	int nsgs, int *bucket_map);
static void hpsa_put_ctlr_into_performant_mode(struct ctlr_info *h);
static inline u32 next_command(struct ctlr_info *h);

static DEVICE_ATTR(raid_level, S_IRUGO, raid_level_show, NULL);
static DEVICE_ATTR(lunid, S_IRUGO, lunid_show, NULL);
static DEVICE_ATTR(unique_id, S_IRUGO, unique_id_show, NULL);
static DEVICE_ATTR(rescan, S_IWUSR, NULL, host_store_rescan);

static struct device_attribute *hpsa_sdev_attrs[] = {
	&dev_attr_raid_level,
	&dev_attr_lunid,
	&dev_attr_unique_id,
	NULL,
};

static struct device_attribute *hpsa_shost_attrs[] = {
	&dev_attr_rescan,
	NULL,
};

static struct scsi_host_template hpsa_driver_template = {
	.module			= THIS_MODULE,
	.name			= "hpsa",
	.proc_name		= "hpsa",
	.queuecommand		= hpsa_scsi_queue_command,
	.scan_start		= hpsa_scan_start,
	.scan_finished		= hpsa_scan_finished,
	.change_queue_depth	= hpsa_change_queue_depth,
	.this_id		= -1,
	.sg_tablesize		= MAXSGENTRIES,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler = hpsa_eh_device_reset_handler,
	.ioctl			= hpsa_ioctl,
	.slave_alloc		= hpsa_slave_alloc,
	.slave_destroy		= hpsa_slave_destroy,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= hpsa_compat_ioctl,
#endif
	.sdev_attrs = hpsa_sdev_attrs,
	.shost_attrs = hpsa_shost_attrs,
};

static inline struct ctlr_info *sdev_to_hba(struct scsi_device *sdev)
{
	unsigned long *priv = shost_priv(sdev->host);
	return (struct ctlr_info *) *priv;
}

static inline struct ctlr_info *shost_to_hba(struct Scsi_Host *sh)
{
	unsigned long *priv = shost_priv(sh);
	return (struct ctlr_info *) *priv;
}

static int check_for_unit_attention(struct ctlr_info *h,
	struct CommandList *c)
{
	if (c->err_info->SenseInfo[2] != UNIT_ATTENTION)
		return 0;

	switch (c->err_info->SenseInfo[12]) {
	case STATE_CHANGED:
		dev_warn(&h->pdev->dev, "hpsa%d: a state change "
			"detected, command retried\n", h->ctlr);
		break;
	case LUN_FAILED:
		dev_warn(&h->pdev->dev, "hpsa%d: LUN failure "
			"detected, action required\n", h->ctlr);
		break;
	case REPORT_LUNS_CHANGED:
		dev_warn(&h->pdev->dev, "hpsa%d: report LUN data "
			"changed, action required\n", h->ctlr);
	/*
	 * Note: this REPORT_LUNS_CHANGED condition only occurs on the MSA2012.
	 */
		break;
	case POWER_OR_RESET:
		dev_warn(&h->pdev->dev, "hpsa%d: a power on "
			"or device reset detected\n", h->ctlr);
		break;
	case UNIT_ATTENTION_CLEARED:
		dev_warn(&h->pdev->dev, "hpsa%d: unit attention "
		    "cleared by another initiator\n", h->ctlr);
		break;
	default:
		dev_warn(&h->pdev->dev, "hpsa%d: unknown "
			"unit attention detected\n", h->ctlr);
		break;
	}
	return 1;
}

static ssize_t host_store_rescan(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ctlr_info *h;
	struct Scsi_Host *shost = class_to_shost(dev);
	h = shost_to_hba(shost);
	hpsa_scan_start(h->scsi_host);
	return count;
}

/* Enqueuing and dequeuing functions for cmdlists. */
static inline void addQ(struct hlist_head *list, struct CommandList *c)
{
	hlist_add_head(&c->list, list);
}

static inline u32 next_command(struct ctlr_info *h)
{
	u32 a;

	if (unlikely(h->transMethod != CFGTBL_Trans_Performant))
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

/* set_performant_mode: Modify the tag for cciss performant
 * set bit 0 for pull model, bits 3-1 for block fetch
 * register number
 */
static void set_performant_mode(struct ctlr_info *h, struct CommandList *c)
{
	if (likely(h->transMethod == CFGTBL_Trans_Performant))
		c->busaddr |= 1 | (h->blockFetchTable[c->Header.SGList] << 1);
}

static void enqueue_cmd_and_start_io(struct ctlr_info *h,
	struct CommandList *c)
{
	unsigned long flags;

	set_performant_mode(h, c);
	spin_lock_irqsave(&h->lock, flags);
	addQ(&h->reqQ, c);
	h->Qdepth++;
	start_io(h);
	spin_unlock_irqrestore(&h->lock, flags);
}

static inline void removeQ(struct CommandList *c)
{
	if (WARN_ON(hlist_unhashed(&c->list)))
		return;
	hlist_del_init(&c->list);
}

static inline int is_hba_lunid(unsigned char scsi3addr[])
{
	return memcmp(scsi3addr, RAID_CTLR_LUNID, 8) == 0;
}

static inline int is_logical_dev_addr_mode(unsigned char scsi3addr[])
{
	return (scsi3addr[3] & 0xC0) == 0x40;
}

static inline int is_scsi_rev_5(struct ctlr_info *h)
{
	if (!h->hba_inquiry_data)
		return 0;
	if ((h->hba_inquiry_data[2] & 0x07) == 5)
		return 1;
	return 0;
}

static const char *raid_label[] = { "0", "4", "1(1+0)", "5", "5+1", "ADG",
	"UNKNOWN"
};
#define RAID_UNKNOWN (ARRAY_SIZE(raid_label) - 1)

static ssize_t raid_level_show(struct device *dev,
	     struct device_attribute *attr, char *buf)
{
	ssize_t l = 0;
	unsigned char rlevel;
	struct ctlr_info *h;
	struct scsi_device *sdev;
	struct hpsa_scsi_dev_t *hdev;
	unsigned long flags;

	sdev = to_scsi_device(dev);
	h = sdev_to_hba(sdev);
	spin_lock_irqsave(&h->lock, flags);
	hdev = sdev->hostdata;
	if (!hdev) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -ENODEV;
	}

	/* Is this even a logical drive? */
	if (!is_logical_dev_addr_mode(hdev->scsi3addr)) {
		spin_unlock_irqrestore(&h->lock, flags);
		l = snprintf(buf, PAGE_SIZE, "N/A\n");
		return l;
	}

	rlevel = hdev->raid_level;
	spin_unlock_irqrestore(&h->lock, flags);
	if (rlevel > RAID_UNKNOWN)
		rlevel = RAID_UNKNOWN;
	l = snprintf(buf, PAGE_SIZE, "RAID %s\n", raid_label[rlevel]);
	return l;
}

static ssize_t lunid_show(struct device *dev,
	     struct device_attribute *attr, char *buf)
{
	struct ctlr_info *h;
	struct scsi_device *sdev;
	struct hpsa_scsi_dev_t *hdev;
	unsigned long flags;
	unsigned char lunid[8];

	sdev = to_scsi_device(dev);
	h = sdev_to_hba(sdev);
	spin_lock_irqsave(&h->lock, flags);
	hdev = sdev->hostdata;
	if (!hdev) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -ENODEV;
	}
	memcpy(lunid, hdev->scsi3addr, sizeof(lunid));
	spin_unlock_irqrestore(&h->lock, flags);
	return snprintf(buf, 20, "0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		lunid[0], lunid[1], lunid[2], lunid[3],
		lunid[4], lunid[5], lunid[6], lunid[7]);
}

static ssize_t unique_id_show(struct device *dev,
	     struct device_attribute *attr, char *buf)
{
	struct ctlr_info *h;
	struct scsi_device *sdev;
	struct hpsa_scsi_dev_t *hdev;
	unsigned long flags;
	unsigned char sn[16];

	sdev = to_scsi_device(dev);
	h = sdev_to_hba(sdev);
	spin_lock_irqsave(&h->lock, flags);
	hdev = sdev->hostdata;
	if (!hdev) {
		spin_unlock_irqrestore(&h->lock, flags);
		return -ENODEV;
	}
	memcpy(sn, hdev->device_id, sizeof(sn));
	spin_unlock_irqrestore(&h->lock, flags);
	return snprintf(buf, 16 * 2 + 2,
			"%02X%02X%02X%02X%02X%02X%02X%02X"
			"%02X%02X%02X%02X%02X%02X%02X%02X\n",
			sn[0], sn[1], sn[2], sn[3],
			sn[4], sn[5], sn[6], sn[7],
			sn[8], sn[9], sn[10], sn[11],
			sn[12], sn[13], sn[14], sn[15]);
}

static int hpsa_find_target_lun(struct ctlr_info *h,
	unsigned char scsi3addr[], int bus, int *target, int *lun)
{
	/* finds an unused bus, target, lun for a new physical device
	 * assumes h->devlock is held
	 */
	int i, found = 0;
	DECLARE_BITMAP(lun_taken, HPSA_MAX_SCSI_DEVS_PER_HBA);

	memset(&lun_taken[0], 0, HPSA_MAX_SCSI_DEVS_PER_HBA >> 3);

	for (i = 0; i < h->ndevices; i++) {
		if (h->dev[i]->bus == bus && h->dev[i]->target != -1)
			set_bit(h->dev[i]->target, lun_taken);
	}

	for (i = 0; i < HPSA_MAX_SCSI_DEVS_PER_HBA; i++) {
		if (!test_bit(i, lun_taken)) {
			/* *bus = 1; */
			*target = i;
			*lun = 0;
			found = 1;
			break;
		}
	}
	return !found;
}

/* Add an entry into h->dev[] array. */
static int hpsa_scsi_add_entry(struct ctlr_info *h, int hostno,
		struct hpsa_scsi_dev_t *device,
		struct hpsa_scsi_dev_t *added[], int *nadded)
{
	/* assumes h->devlock is held */
	int n = h->ndevices;
	int i;
	unsigned char addr1[8], addr2[8];
	struct hpsa_scsi_dev_t *sd;

	if (n >= HPSA_MAX_SCSI_DEVS_PER_HBA) {
		dev_err(&h->pdev->dev, "too many devices, some will be "
			"inaccessible.\n");
		return -1;
	}

	/* physical devices do not have lun or target assigned until now. */
	if (device->lun != -1)
		/* Logical device, lun is already assigned. */
		goto lun_assigned;

	/* If this device a non-zero lun of a multi-lun device
	 * byte 4 of the 8-byte LUN addr will contain the logical
	 * unit no, zero otherise.
	 */
	if (device->scsi3addr[4] == 0) {
		/* This is not a non-zero lun of a multi-lun device */
		if (hpsa_find_target_lun(h, device->scsi3addr,
			device->bus, &device->target, &device->lun) != 0)
			return -1;
		goto lun_assigned;
	}

	/* This is a non-zero lun of a multi-lun device.
	 * Search through our list and find the device which
	 * has the same 8 byte LUN address, excepting byte 4.
	 * Assign the same bus and target for this new LUN.
	 * Use the logical unit number from the firmware.
	 */
	memcpy(addr1, device->scsi3addr, 8);
	addr1[4] = 0;
	for (i = 0; i < n; i++) {
		sd = h->dev[i];
		memcpy(addr2, sd->scsi3addr, 8);
		addr2[4] = 0;
		/* differ only in byte 4? */
		if (memcmp(addr1, addr2, 8) == 0) {
			device->bus = sd->bus;
			device->target = sd->target;
			device->lun = device->scsi3addr[4];
			break;
		}
	}
	if (device->lun == -1) {
		dev_warn(&h->pdev->dev, "physical device with no LUN=0,"
			" suspect firmware bug or unsupported hardware "
			"configuration.\n");
			return -1;
	}

lun_assigned:

	h->dev[n] = device;
	h->ndevices++;
	added[*nadded] = device;
	(*nadded)++;

	/* initially, (before registering with scsi layer) we don't
	 * know our hostno and we don't want to print anything first
	 * time anyway (the scsi layer's inquiries will show that info)
	 */
	/* if (hostno != -1) */
		dev_info(&h->pdev->dev, "%s device c%db%dt%dl%d added.\n",
			scsi_device_type(device->devtype), hostno,
			device->bus, device->target, device->lun);
	return 0;
}

/* Replace an entry from h->dev[] array. */
static void hpsa_scsi_replace_entry(struct ctlr_info *h, int hostno,
	int entry, struct hpsa_scsi_dev_t *new_entry,
	struct hpsa_scsi_dev_t *added[], int *nadded,
	struct hpsa_scsi_dev_t *removed[], int *nremoved)
{
	/* assumes h->devlock is held */
	BUG_ON(entry < 0 || entry >= HPSA_MAX_SCSI_DEVS_PER_HBA);
	removed[*nremoved] = h->dev[entry];
	(*nremoved)++;
	h->dev[entry] = new_entry;
	added[*nadded] = new_entry;
	(*nadded)++;
	dev_info(&h->pdev->dev, "%s device c%db%dt%dl%d changed.\n",
		scsi_device_type(new_entry->devtype), hostno, new_entry->bus,
			new_entry->target, new_entry->lun);
}

/* Remove an entry from h->dev[] array. */
static void hpsa_scsi_remove_entry(struct ctlr_info *h, int hostno, int entry,
	struct hpsa_scsi_dev_t *removed[], int *nremoved)
{
	/* assumes h->devlock is held */
	int i;
	struct hpsa_scsi_dev_t *sd;

	BUG_ON(entry < 0 || entry >= HPSA_MAX_SCSI_DEVS_PER_HBA);

	sd = h->dev[entry];
	removed[*nremoved] = h->dev[entry];
	(*nremoved)++;

	for (i = entry; i < h->ndevices-1; i++)
		h->dev[i] = h->dev[i+1];
	h->ndevices--;
	dev_info(&h->pdev->dev, "%s device c%db%dt%dl%d removed.\n",
		scsi_device_type(sd->devtype), hostno, sd->bus, sd->target,
		sd->lun);
}

#define SCSI3ADDR_EQ(a, b) ( \
	(a)[7] == (b)[7] && \
	(a)[6] == (b)[6] && \
	(a)[5] == (b)[5] && \
	(a)[4] == (b)[4] && \
	(a)[3] == (b)[3] && \
	(a)[2] == (b)[2] && \
	(a)[1] == (b)[1] && \
	(a)[0] == (b)[0])

static void fixup_botched_add(struct ctlr_info *h,
	struct hpsa_scsi_dev_t *added)
{
	/* called when scsi_add_device fails in order to re-adjust
	 * h->dev[] to match the mid layer's view.
	 */
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&h->lock, flags);
	for (i = 0; i < h->ndevices; i++) {
		if (h->dev[i] == added) {
			for (j = i; j < h->ndevices-1; j++)
				h->dev[j] = h->dev[j+1];
			h->ndevices--;
			break;
		}
	}
	spin_unlock_irqrestore(&h->lock, flags);
	kfree(added);
}

static inline int device_is_the_same(struct hpsa_scsi_dev_t *dev1,
	struct hpsa_scsi_dev_t *dev2)
{
	if ((is_logical_dev_addr_mode(dev1->scsi3addr) ||
		(dev1->lun != -1 && dev2->lun != -1)) &&
		dev1->devtype != 0x0C)
		return (memcmp(dev1, dev2, sizeof(*dev1)) == 0);

	/* we compare everything except lun and target as these
	 * are not yet assigned.  Compare parts likely
	 * to differ first
	 */
	if (memcmp(dev1->scsi3addr, dev2->scsi3addr,
		sizeof(dev1->scsi3addr)) != 0)
		return 0;
	if (memcmp(dev1->device_id, dev2->device_id,
		sizeof(dev1->device_id)) != 0)
		return 0;
	if (memcmp(dev1->model, dev2->model, sizeof(dev1->model)) != 0)
		return 0;
	if (memcmp(dev1->vendor, dev2->vendor, sizeof(dev1->vendor)) != 0)
		return 0;
	if (memcmp(dev1->revision, dev2->revision, sizeof(dev1->revision)) != 0)
		return 0;
	if (dev1->devtype != dev2->devtype)
		return 0;
	if (dev1->raid_level != dev2->raid_level)
		return 0;
	if (dev1->bus != dev2->bus)
		return 0;
	return 1;
}

/* Find needle in haystack.  If exact match found, return DEVICE_SAME,
 * and return needle location in *index.  If scsi3addr matches, but not
 * vendor, model, serial num, etc. return DEVICE_CHANGED, and return needle
 * location in *index.  If needle not found, return DEVICE_NOT_FOUND.
 */
static int hpsa_scsi_find_entry(struct hpsa_scsi_dev_t *needle,
	struct hpsa_scsi_dev_t *haystack[], int haystack_size,
	int *index)
{
	int i;
#define DEVICE_NOT_FOUND 0
#define DEVICE_CHANGED 1
#define DEVICE_SAME 2
	for (i = 0; i < haystack_size; i++) {
		if (haystack[i] == NULL) /* previously removed. */
			continue;
		if (SCSI3ADDR_EQ(needle->scsi3addr, haystack[i]->scsi3addr)) {
			*index = i;
			if (device_is_the_same(needle, haystack[i]))
				return DEVICE_SAME;
			else
				return DEVICE_CHANGED;
		}
	}
	*index = -1;
	return DEVICE_NOT_FOUND;
}

static void adjust_hpsa_scsi_table(struct ctlr_info *h, int hostno,
	struct hpsa_scsi_dev_t *sd[], int nsds)
{
	/* sd contains scsi3 addresses and devtypes, and inquiry
	 * data.  This function takes what's in sd to be the current
	 * reality and updates h->dev[] to reflect that reality.
	 */
	int i, entry, device_change, changes = 0;
	struct hpsa_scsi_dev_t *csd;
	unsigned long flags;
	struct hpsa_scsi_dev_t **added, **removed;
	int nadded, nremoved;
	struct Scsi_Host *sh = NULL;

	added = kzalloc(sizeof(*added) * HPSA_MAX_SCSI_DEVS_PER_HBA,
		GFP_KERNEL);
	removed = kzalloc(sizeof(*removed) * HPSA_MAX_SCSI_DEVS_PER_HBA,
		GFP_KERNEL);

	if (!added || !removed) {
		dev_warn(&h->pdev->dev, "out of memory in "
			"adjust_hpsa_scsi_table\n");
		goto free_and_out;
	}

	spin_lock_irqsave(&h->devlock, flags);

	/* find any devices in h->dev[] that are not in
	 * sd[] and remove them from h->dev[], and for any
	 * devices which have changed, remove the old device
	 * info and add the new device info.
	 */
	i = 0;
	nremoved = 0;
	nadded = 0;
	while (i < h->ndevices) {
		csd = h->dev[i];
		device_change = hpsa_scsi_find_entry(csd, sd, nsds, &entry);
		if (device_change == DEVICE_NOT_FOUND) {
			changes++;
			hpsa_scsi_remove_entry(h, hostno, i,
				removed, &nremoved);
			continue; /* remove ^^^, hence i not incremented */
		} else if (device_change == DEVICE_CHANGED) {
			changes++;
			hpsa_scsi_replace_entry(h, hostno, i, sd[entry],
				added, &nadded, removed, &nremoved);
			/* Set it to NULL to prevent it from being freed
			 * at the bottom of hpsa_update_scsi_devices()
			 */
			sd[entry] = NULL;
		}
		i++;
	}

	/* Now, make sure every device listed in sd[] is also
	 * listed in h->dev[], adding them if they aren't found
	 */

	for (i = 0; i < nsds; i++) {
		if (!sd[i]) /* if already added above. */
			continue;
		device_change = hpsa_scsi_find_entry(sd[i], h->dev,
					h->ndevices, &entry);
		if (device_change == DEVICE_NOT_FOUND) {
			changes++;
			if (hpsa_scsi_add_entry(h, hostno, sd[i],
				added, &nadded) != 0)
				break;
			sd[i] = NULL; /* prevent from being freed later. */
		} else if (device_change == DEVICE_CHANGED) {
			/* should never happen... */
			changes++;
			dev_warn(&h->pdev->dev,
				"device unexpectedly changed.\n");
			/* but if it does happen, we just ignore that device */
		}
	}
	spin_unlock_irqrestore(&h->devlock, flags);

	/* Don't notify scsi mid layer of any changes the first time through
	 * (or if there are no changes) scsi_scan_host will do it later the
	 * first time through.
	 */
	if (hostno == -1 || !changes)
		goto free_and_out;

	sh = h->scsi_host;
	/* Notify scsi mid layer of any removed devices */
	for (i = 0; i < nremoved; i++) {
		struct scsi_device *sdev =
			scsi_device_lookup(sh, removed[i]->bus,
				removed[i]->target, removed[i]->lun);
		if (sdev != NULL) {
			scsi_remove_device(sdev);
			scsi_device_put(sdev);
		} else {
			/* We don't expect to get here.
			 * future cmds to this device will get selection
			 * timeout as if the device was gone.
			 */
			dev_warn(&h->pdev->dev, "didn't find c%db%dt%dl%d "
				" for removal.", hostno, removed[i]->bus,
				removed[i]->target, removed[i]->lun);
		}
		kfree(removed[i]);
		removed[i] = NULL;
	}

	/* Notify scsi mid layer of any added devices */
	for (i = 0; i < nadded; i++) {
		if (scsi_add_device(sh, added[i]->bus,
			added[i]->target, added[i]->lun) == 0)
			continue;
		dev_warn(&h->pdev->dev, "scsi_add_device c%db%dt%dl%d failed, "
			"device not added.\n", hostno, added[i]->bus,
			added[i]->target, added[i]->lun);
		/* now we have to remove it from h->dev,
		 * since it didn't get added to scsi mid layer
		 */
		fixup_botched_add(h, added[i]);
	}

free_and_out:
	kfree(added);
	kfree(removed);
}

/*
 * Lookup bus/target/lun and retrun corresponding struct hpsa_scsi_dev_t *
 * Assume's h->devlock is held.
 */
static struct hpsa_scsi_dev_t *lookup_hpsa_scsi_dev(struct ctlr_info *h,
	int bus, int target, int lun)
{
	int i;
	struct hpsa_scsi_dev_t *sd;

	for (i = 0; i < h->ndevices; i++) {
		sd = h->dev[i];
		if (sd->bus == bus && sd->target == target && sd->lun == lun)
			return sd;
	}
	return NULL;
}

/* link sdev->hostdata to our per-device structure. */
static int hpsa_slave_alloc(struct scsi_device *sdev)
{
	struct hpsa_scsi_dev_t *sd;
	unsigned long flags;
	struct ctlr_info *h;

	h = sdev_to_hba(sdev);
	spin_lock_irqsave(&h->devlock, flags);
	sd = lookup_hpsa_scsi_dev(h, sdev_channel(sdev),
		sdev_id(sdev), sdev->lun);
	if (sd != NULL)
		sdev->hostdata = sd;
	spin_unlock_irqrestore(&h->devlock, flags);
	return 0;
}

static void hpsa_slave_destroy(struct scsi_device *sdev)
{
	/* nothing to do. */
}

static void hpsa_scsi_setup(struct ctlr_info *h)
{
	h->ndevices = 0;
	h->scsi_host = NULL;
	spin_lock_init(&h->devlock);
}

static void complete_scsi_command(struct CommandList *cp,
	int timeout, u32 tag)
{
	struct scsi_cmnd *cmd;
	struct ctlr_info *h;
	struct ErrorInfo *ei;

	unsigned char sense_key;
	unsigned char asc;      /* additional sense code */
	unsigned char ascq;     /* additional sense code qualifier */

	ei = cp->err_info;
	cmd = (struct scsi_cmnd *) cp->scsi_cmd;
	h = cp->h;

	scsi_dma_unmap(cmd); /* undo the DMA mappings */

	cmd->result = (DID_OK << 16); 		/* host byte */
	cmd->result |= (COMMAND_COMPLETE << 8);	/* msg byte */
	cmd->result |= ei->ScsiStatus;

	/* copy the sense data whether we need to or not. */
	memcpy(cmd->sense_buffer, ei->SenseInfo,
		ei->SenseLen > SCSI_SENSE_BUFFERSIZE ?
			SCSI_SENSE_BUFFERSIZE :
			ei->SenseLen);
	scsi_set_resid(cmd, ei->ResidualCnt);

	if (ei->CommandStatus == 0) {
		cmd->scsi_done(cmd);
		cmd_free(h, cp);
		return;
	}

	/* an error has occurred */
	switch (ei->CommandStatus) {

	case CMD_TARGET_STATUS:
		if (ei->ScsiStatus) {
			/* Get sense key */
			sense_key = 0xf & ei->SenseInfo[2];
			/* Get additional sense code */
			asc = ei->SenseInfo[12];
			/* Get addition sense code qualifier */
			ascq = ei->SenseInfo[13];
		}

		if (ei->ScsiStatus == SAM_STAT_CHECK_CONDITION) {
			if (check_for_unit_attention(h, cp)) {
				cmd->result = DID_SOFT_ERROR << 16;
				break;
			}
			if (sense_key == ILLEGAL_REQUEST) {
				/*
				 * SCSI REPORT_LUNS is commonly unsupported on
				 * Smart Array.  Suppress noisy complaint.
				 */
				if (cp->Request.CDB[0] == REPORT_LUNS)
					break;

				/* If ASC/ASCQ indicate Logical Unit
				 * Not Supported condition,
				 */
				if ((asc == 0x25) && (ascq == 0x0)) {
					dev_warn(&h->pdev->dev, "cp %p "
						"has check condition\n", cp);
					break;
				}
			}

			if (sense_key == NOT_READY) {
				/* If Sense is Not Ready, Logical Unit
				 * Not ready, Manual Intervention
				 * required
				 */
				if ((asc == 0x04) && (ascq == 0x03)) {
					dev_warn(&h->pdev->dev, "cp %p "
						"has check condition: unit "
						"not ready, manual "
						"intervention required\n", cp);
					break;
				}
			}
			if (sense_key == ABORTED_COMMAND) {
				/* Aborted command is retryable */
				dev_warn(&h->pdev->dev, "cp %p "
					"has check condition: aborted command: "
					"ASC: 0x%x, ASCQ: 0x%x\n",
					cp, asc, ascq);
				cmd->result = DID_SOFT_ERROR << 16;
				break;
			}
			/* Must be some other type of check condition */
			dev_warn(&h->pdev->dev, "cp %p has check condition: "
					"unknown type: "
					"Sense: 0x%x, ASC: 0x%x, ASCQ: 0x%x, "
					"Returning result: 0x%x, "
					"cmd=[%02x %02x %02x %02x %02x "
					"%02x %02x %02x %02x %02x %02x "
					"%02x %02x %02x %02x %02x]\n",
					cp, sense_key, asc, ascq,
					cmd->result,
					cmd->cmnd[0], cmd->cmnd[1],
					cmd->cmnd[2], cmd->cmnd[3],
					cmd->cmnd[4], cmd->cmnd[5],
					cmd->cmnd[6], cmd->cmnd[7],
					cmd->cmnd[8], cmd->cmnd[9],
					cmd->cmnd[10], cmd->cmnd[11],
					cmd->cmnd[12], cmd->cmnd[13],
					cmd->cmnd[14], cmd->cmnd[15]);
			break;
		}


		/* Problem was not a check condition
		 * Pass it up to the upper layers...
		 */
		if (ei->ScsiStatus) {
			dev_warn(&h->pdev->dev, "cp %p has status 0x%x "
				"Sense: 0x%x, ASC: 0x%x, ASCQ: 0x%x, "
				"Returning result: 0x%x\n",
				cp, ei->ScsiStatus,
				sense_key, asc, ascq,
				cmd->result);
		} else {  /* scsi status is zero??? How??? */
			dev_warn(&h->pdev->dev, "cp %p SCSI status was 0. "
				"Returning no connection.\n", cp),

			/* Ordinarily, this case should never happen,
			 * but there is a bug in some released firmware
			 * revisions that allows it to happen if, for
			 * example, a 4100 backplane loses power and
			 * the tape drive is in it.  We assume that
			 * it's a fatal error of some kind because we
			 * can't show that it wasn't. We will make it
			 * look like selection timeout since that is
			 * the most common reason for this to occur,
			 * and it's severe enough.
			 */

			cmd->result = DID_NO_CONNECT << 16;
		}
		break;

	case CMD_DATA_UNDERRUN: /* let mid layer handle it. */
		break;
	case CMD_DATA_OVERRUN:
		dev_warn(&h->pdev->dev, "cp %p has"
			" completed with data overrun "
			"reported\n", cp);
		break;
	case CMD_INVALID: {
		/* print_bytes(cp, sizeof(*cp), 1, 0);
		print_cmd(cp); */
		/* We get CMD_INVALID if you address a non-existent device
		 * instead of a selection timeout (no response).  You will
		 * see this if you yank out a drive, then try to access it.
		 * This is kind of a shame because it means that any other
		 * CMD_INVALID (e.g. driver bug) will get interpreted as a
		 * missing target. */
		cmd->result = DID_NO_CONNECT << 16;
	}
		break;
	case CMD_PROTOCOL_ERR:
		dev_warn(&h->pdev->dev, "cp %p has "
			"protocol error \n", cp);
		break;
	case CMD_HARDWARE_ERR:
		cmd->result = DID_ERROR << 16;
		dev_warn(&h->pdev->dev, "cp %p had  hardware error\n", cp);
		break;
	case CMD_CONNECTION_LOST:
		cmd->result = DID_ERROR << 16;
		dev_warn(&h->pdev->dev, "cp %p had connection lost\n", cp);
		break;
	case CMD_ABORTED:
		cmd->result = DID_ABORT << 16;
		dev_warn(&h->pdev->dev, "cp %p was aborted with status 0x%x\n",
				cp, ei->ScsiStatus);
		break;
	case CMD_ABORT_FAILED:
		cmd->result = DID_ERROR << 16;
		dev_warn(&h->pdev->dev, "cp %p reports abort failed\n", cp);
		break;
	case CMD_UNSOLICITED_ABORT:
		cmd->result = DID_RESET << 16;
		dev_warn(&h->pdev->dev, "cp %p aborted do to an unsolicited "
			"abort\n", cp);
		break;
	case CMD_TIMEOUT:
		cmd->result = DID_TIME_OUT << 16;
		dev_warn(&h->pdev->dev, "cp %p timedout\n", cp);
		break;
	default:
		cmd->result = DID_ERROR << 16;
		dev_warn(&h->pdev->dev, "cp %p returned unknown status %x\n",
				cp, ei->CommandStatus);
	}
	cmd->scsi_done(cmd);
	cmd_free(h, cp);
}

static int hpsa_scsi_detect(struct ctlr_info *h)
{
	struct Scsi_Host *sh;
	int error;

	sh = scsi_host_alloc(&hpsa_driver_template, sizeof(h));
	if (sh == NULL)
		goto fail;

	sh->io_port = 0;
	sh->n_io_port = 0;
	sh->this_id = -1;
	sh->max_channel = 3;
	sh->max_cmd_len = MAX_COMMAND_SIZE;
	sh->max_lun = HPSA_MAX_LUN;
	sh->max_id = HPSA_MAX_LUN;
	sh->can_queue = h->nr_cmds;
	sh->cmd_per_lun = h->nr_cmds;
	h->scsi_host = sh;
	sh->hostdata[0] = (unsigned long) h;
	sh->irq = h->intr[PERF_MODE_INT];
	sh->unique_id = sh->irq;
	error = scsi_add_host(sh, &h->pdev->dev);
	if (error)
		goto fail_host_put;
	scsi_scan_host(sh);
	return 0;

 fail_host_put:
	dev_err(&h->pdev->dev, "hpsa_scsi_detect: scsi_add_host"
		" failed for controller %d\n", h->ctlr);
	scsi_host_put(sh);
	return error;
 fail:
	dev_err(&h->pdev->dev, "hpsa_scsi_detect: scsi_host_alloc"
		" failed for controller %d\n", h->ctlr);
	return -ENOMEM;
}

static void hpsa_pci_unmap(struct pci_dev *pdev,
	struct CommandList *c, int sg_used, int data_direction)
{
	int i;
	union u64bit addr64;

	for (i = 0; i < sg_used; i++) {
		addr64.val32.lower = c->SG[i].Addr.lower;
		addr64.val32.upper = c->SG[i].Addr.upper;
		pci_unmap_single(pdev, (dma_addr_t) addr64.val, c->SG[i].Len,
			data_direction);
	}
}

static void hpsa_map_one(struct pci_dev *pdev,
		struct CommandList *cp,
		unsigned char *buf,
		size_t buflen,
		int data_direction)
{
	u64 addr64;

	if (buflen == 0 || data_direction == PCI_DMA_NONE) {
		cp->Header.SGList = 0;
		cp->Header.SGTotal = 0;
		return;
	}

	addr64 = (u64) pci_map_single(pdev, buf, buflen, data_direction);
	cp->SG[0].Addr.lower =
	  (u32) (addr64 & (u64) 0x00000000FFFFFFFF);
	cp->SG[0].Addr.upper =
	  (u32) ((addr64 >> 32) & (u64) 0x00000000FFFFFFFF);
	cp->SG[0].Len = buflen;
	cp->Header.SGList = (u8) 1;   /* no. SGs contig in this cmd */
	cp->Header.SGTotal = (u16) 1; /* total sgs in this cmd list */
}

static inline void hpsa_scsi_do_simple_cmd_core(struct ctlr_info *h,
	struct CommandList *c)
{
	DECLARE_COMPLETION_ONSTACK(wait);

	c->waiting = &wait;
	enqueue_cmd_and_start_io(h, c);
	wait_for_completion(&wait);
}

static void hpsa_scsi_do_simple_cmd_with_retry(struct ctlr_info *h,
	struct CommandList *c, int data_direction)
{
	int retry_count = 0;

	do {
		memset(c->err_info, 0, sizeof(c->err_info));
		hpsa_scsi_do_simple_cmd_core(h, c);
		retry_count++;
	} while (check_for_unit_attention(h, c) && retry_count <= 3);
	hpsa_pci_unmap(h->pdev, c, 1, data_direction);
}

static void hpsa_scsi_interpret_error(struct CommandList *cp)
{
	struct ErrorInfo *ei;
	struct device *d = &cp->h->pdev->dev;

	ei = cp->err_info;
	switch (ei->CommandStatus) {
	case CMD_TARGET_STATUS:
		dev_warn(d, "cmd %p has completed with errors\n", cp);
		dev_warn(d, "cmd %p has SCSI Status = %x\n", cp,
				ei->ScsiStatus);
		if (ei->ScsiStatus == 0)
			dev_warn(d, "SCSI status is abnormally zero.  "
			"(probably indicates selection timeout "
			"reported incorrectly due to a known "
			"firmware bug, circa July, 2001.)\n");
		break;
	case CMD_DATA_UNDERRUN: /* let mid layer handle it. */
			dev_info(d, "UNDERRUN\n");
		break;
	case CMD_DATA_OVERRUN:
		dev_warn(d, "cp %p has completed with data overrun\n", cp);
		break;
	case CMD_INVALID: {
		/* controller unfortunately reports SCSI passthru's
		 * to non-existent targets as invalid commands.
		 */
		dev_warn(d, "cp %p is reported invalid (probably means "
			"target device no longer present)\n", cp);
		/* print_bytes((unsigned char *) cp, sizeof(*cp), 1, 0);
		print_cmd(cp);  */
		}
		break;
	case CMD_PROTOCOL_ERR:
		dev_warn(d, "cp %p has protocol error \n", cp);
		break;
	case CMD_HARDWARE_ERR:
		/* cmd->result = DID_ERROR << 16; */
		dev_warn(d, "cp %p had hardware error\n", cp);
		break;
	case CMD_CONNECTION_LOST:
		dev_warn(d, "cp %p had connection lost\n", cp);
		break;
	case CMD_ABORTED:
		dev_warn(d, "cp %p was aborted\n", cp);
		break;
	case CMD_ABORT_FAILED:
		dev_warn(d, "cp %p reports abort failed\n", cp);
		break;
	case CMD_UNSOLICITED_ABORT:
		dev_warn(d, "cp %p aborted due to an unsolicited abort\n", cp);
		break;
	case CMD_TIMEOUT:
		dev_warn(d, "cp %p timed out\n", cp);
		break;
	default:
		dev_warn(d, "cp %p returned unknown status %x\n", cp,
				ei->CommandStatus);
	}
}

static int hpsa_scsi_do_inquiry(struct ctlr_info *h, unsigned char *scsi3addr,
			unsigned char page, unsigned char *buf,
			unsigned char bufsize)
{
	int rc = IO_OK;
	struct CommandList *c;
	struct ErrorInfo *ei;

	c = cmd_special_alloc(h);

	if (c == NULL) {			/* trouble... */
		dev_warn(&h->pdev->dev, "cmd_special_alloc returned NULL!\n");
		return -ENOMEM;
	}

	fill_cmd(c, HPSA_INQUIRY, h, buf, bufsize, page, scsi3addr, TYPE_CMD);
	hpsa_scsi_do_simple_cmd_with_retry(h, c, PCI_DMA_FROMDEVICE);
	ei = c->err_info;
	if (ei->CommandStatus != 0 && ei->CommandStatus != CMD_DATA_UNDERRUN) {
		hpsa_scsi_interpret_error(c);
		rc = -1;
	}
	cmd_special_free(h, c);
	return rc;
}

static int hpsa_send_reset(struct ctlr_info *h, unsigned char *scsi3addr)
{
	int rc = IO_OK;
	struct CommandList *c;
	struct ErrorInfo *ei;

	c = cmd_special_alloc(h);

	if (c == NULL) {			/* trouble... */
		dev_warn(&h->pdev->dev, "cmd_special_alloc returned NULL!\n");
		return -ENOMEM;
	}

	fill_cmd(c, HPSA_DEVICE_RESET_MSG, h, NULL, 0, 0, scsi3addr, TYPE_MSG);
	hpsa_scsi_do_simple_cmd_core(h, c);
	/* no unmap needed here because no data xfer. */

	ei = c->err_info;
	if (ei->CommandStatus != 0) {
		hpsa_scsi_interpret_error(c);
		rc = -1;
	}
	cmd_special_free(h, c);
	return rc;
}

static void hpsa_get_raid_level(struct ctlr_info *h,
	unsigned char *scsi3addr, unsigned char *raid_level)
{
	int rc;
	unsigned char *buf;

	*raid_level = RAID_UNKNOWN;
	buf = kzalloc(64, GFP_KERNEL);
	if (!buf)
		return;
	rc = hpsa_scsi_do_inquiry(h, scsi3addr, 0xC1, buf, 64);
	if (rc == 0)
		*raid_level = buf[8];
	if (*raid_level > RAID_UNKNOWN)
		*raid_level = RAID_UNKNOWN;
	kfree(buf);
	return;
}

/* Get the device id from inquiry page 0x83 */
static int hpsa_get_device_id(struct ctlr_info *h, unsigned char *scsi3addr,
	unsigned char *device_id, int buflen)
{
	int rc;
	unsigned char *buf;

	if (buflen > 16)
		buflen = 16;
	buf = kzalloc(64, GFP_KERNEL);
	if (!buf)
		return -1;
	rc = hpsa_scsi_do_inquiry(h, scsi3addr, 0x83, buf, 64);
	if (rc == 0)
		memcpy(device_id, &buf[8], buflen);
	kfree(buf);
	return rc != 0;
}

static int hpsa_scsi_do_report_luns(struct ctlr_info *h, int logical,
		struct ReportLUNdata *buf, int bufsize,
		int extended_response)
{
	int rc = IO_OK;
	struct CommandList *c;
	unsigned char scsi3addr[8];
	struct ErrorInfo *ei;

	c = cmd_special_alloc(h);
	if (c == NULL) {			/* trouble... */
		dev_err(&h->pdev->dev, "cmd_special_alloc returned NULL!\n");
		return -1;
	}
	/* address the controller */
	memset(scsi3addr, 0, sizeof(scsi3addr));
	fill_cmd(c, logical ? HPSA_REPORT_LOG : HPSA_REPORT_PHYS, h,
		buf, bufsize, 0, scsi3addr, TYPE_CMD);
	if (extended_response)
		c->Request.CDB[1] = extended_response;
	hpsa_scsi_do_simple_cmd_with_retry(h, c, PCI_DMA_FROMDEVICE);
	ei = c->err_info;
	if (ei->CommandStatus != 0 &&
	    ei->CommandStatus != CMD_DATA_UNDERRUN) {
		hpsa_scsi_interpret_error(c);
		rc = -1;
	}
	cmd_special_free(h, c);
	return rc;
}

static inline int hpsa_scsi_do_report_phys_luns(struct ctlr_info *h,
		struct ReportLUNdata *buf,
		int bufsize, int extended_response)
{
	return hpsa_scsi_do_report_luns(h, 0, buf, bufsize, extended_response);
}

static inline int hpsa_scsi_do_report_log_luns(struct ctlr_info *h,
		struct ReportLUNdata *buf, int bufsize)
{
	return hpsa_scsi_do_report_luns(h, 1, buf, bufsize, 0);
}

static inline void hpsa_set_bus_target_lun(struct hpsa_scsi_dev_t *device,
	int bus, int target, int lun)
{
	device->bus = bus;
	device->target = target;
	device->lun = lun;
}

static int hpsa_update_device_info(struct ctlr_info *h,
	unsigned char scsi3addr[], struct hpsa_scsi_dev_t *this_device)
{
#define OBDR_TAPE_INQ_SIZE 49
	unsigned char *inq_buff;

	inq_buff = kzalloc(OBDR_TAPE_INQ_SIZE, GFP_KERNEL);
	if (!inq_buff)
		goto bail_out;

	/* Do an inquiry to the device to see what it is. */
	if (hpsa_scsi_do_inquiry(h, scsi3addr, 0, inq_buff,
		(unsigned char) OBDR_TAPE_INQ_SIZE) != 0) {
		/* Inquiry failed (msg printed already) */
		dev_err(&h->pdev->dev,
			"hpsa_update_device_info: inquiry failed\n");
		goto bail_out;
	}

	/* As a side effect, record the firmware version number
	 * if we happen to be talking to the RAID controller.
	 */
	if (is_hba_lunid(scsi3addr))
		memcpy(h->firm_ver, &inq_buff[32], 4);

	this_device->devtype = (inq_buff[0] & 0x1f);
	memcpy(this_device->scsi3addr, scsi3addr, 8);
	memcpy(this_device->vendor, &inq_buff[8],
		sizeof(this_device->vendor));
	memcpy(this_device->model, &inq_buff[16],
		sizeof(this_device->model));
	memcpy(this_device->revision, &inq_buff[32],
		sizeof(this_device->revision));
	memset(this_device->device_id, 0,
		sizeof(this_device->device_id));
	hpsa_get_device_id(h, scsi3addr, this_device->device_id,
		sizeof(this_device->device_id));

	if (this_device->devtype == TYPE_DISK &&
		is_logical_dev_addr_mode(scsi3addr))
		hpsa_get_raid_level(h, scsi3addr, &this_device->raid_level);
	else
		this_device->raid_level = RAID_UNKNOWN;

	kfree(inq_buff);
	return 0;

bail_out:
	kfree(inq_buff);
	return 1;
}

static unsigned char *msa2xxx_model[] = {
	"MSA2012",
	"MSA2024",
	"MSA2312",
	"MSA2324",
	NULL,
};

static int is_msa2xxx(struct ctlr_info *h, struct hpsa_scsi_dev_t *device)
{
	int i;

	for (i = 0; msa2xxx_model[i]; i++)
		if (strncmp(device->model, msa2xxx_model[i],
			strlen(msa2xxx_model[i])) == 0)
			return 1;
	return 0;
}

/* Helper function to assign bus, target, lun mapping of devices.
 * Puts non-msa2xxx logical volumes on bus 0, msa2xxx logical
 * volumes on bus 1, physical devices on bus 2. and the hba on bus 3.
 * Logical drive target and lun are assigned at this time, but
 * physical device lun and target assignment are deferred (assigned
 * in hpsa_find_target_lun, called by hpsa_scsi_add_entry.)
 */
static void figure_bus_target_lun(struct ctlr_info *h,
	u8 *lunaddrbytes, int *bus, int *target, int *lun,
	struct hpsa_scsi_dev_t *device)
{
	u32 lunid;

	if (is_logical_dev_addr_mode(lunaddrbytes)) {
		/* logical device */
		if (unlikely(is_scsi_rev_5(h))) {
			/* p1210m, logical drives lun assignments
			 * match SCSI REPORT LUNS data.
			 */
			lunid = le32_to_cpu(*((__le32 *) lunaddrbytes));
			*bus = 0;
			*target = 0;
			*lun = (lunid & 0x3fff) + 1;
		} else {
			/* not p1210m... */
			lunid = le32_to_cpu(*((__le32 *) lunaddrbytes));
			if (is_msa2xxx(h, device)) {
				/* msa2xxx way, put logicals on bus 1
				 * and match target/lun numbers box
				 * reports.
				 */
				*bus = 1;
				*target = (lunid >> 16) & 0x3fff;
				*lun = lunid & 0x00ff;
			} else {
				/* Traditional smart array way. */
				*bus = 0;
				*lun = 0;
				*target = lunid & 0x3fff;
			}
		}
	} else {
		/* physical device */
		if (is_hba_lunid(lunaddrbytes))
			if (unlikely(is_scsi_rev_5(h))) {
				*bus = 0; /* put p1210m ctlr at 0,0,0 */
				*target = 0;
				*lun = 0;
				return;
			} else
				*bus = 3; /* traditional smartarray */
		else
			*bus = 2; /* physical disk */
		*target = -1;
		*lun = -1; /* we will fill these in later. */
	}
}

/*
 * If there is no lun 0 on a target, linux won't find any devices.
 * For the MSA2xxx boxes, we have to manually detect the enclosure
 * which is at lun zero, as CCISS_REPORT_PHYSICAL_LUNS doesn't report
 * it for some reason.  *tmpdevice is the target we're adding,
 * this_device is a pointer into the current element of currentsd[]
 * that we're building up in update_scsi_devices(), below.
 * lunzerobits is a bitmap that tracks which targets already have a
 * lun 0 assigned.
 * Returns 1 if an enclosure was added, 0 if not.
 */
static int add_msa2xxx_enclosure_device(struct ctlr_info *h,
	struct hpsa_scsi_dev_t *tmpdevice,
	struct hpsa_scsi_dev_t *this_device, u8 *lunaddrbytes,
	int bus, int target, int lun, unsigned long lunzerobits[],
	int *nmsa2xxx_enclosures)
{
	unsigned char scsi3addr[8];

	if (test_bit(target, lunzerobits))
		return 0; /* There is already a lun 0 on this target. */

	if (!is_logical_dev_addr_mode(lunaddrbytes))
		return 0; /* It's the logical targets that may lack lun 0. */

	if (!is_msa2xxx(h, tmpdevice))
		return 0; /* It's only the MSA2xxx that have this problem. */

	if (lun == 0) /* if lun is 0, then obviously we have a lun 0. */
		return 0;

	if (is_hba_lunid(scsi3addr))
		return 0; /* Don't add the RAID controller here. */

	if (is_scsi_rev_5(h))
		return 0; /* p1210m doesn't need to do this. */

#define MAX_MSA2XXX_ENCLOSURES 32
	if (*nmsa2xxx_enclosures >= MAX_MSA2XXX_ENCLOSURES) {
		dev_warn(&h->pdev->dev, "Maximum number of MSA2XXX "
			"enclosures exceeded.  Check your hardware "
			"configuration.");
		return 0;
	}

	memset(scsi3addr, 0, 8);
	scsi3addr[3] = target;
	if (hpsa_update_device_info(h, scsi3addr, this_device))
		return 0;
	(*nmsa2xxx_enclosures)++;
	hpsa_set_bus_target_lun(this_device, bus, target, 0);
	set_bit(target, lunzerobits);
	return 1;
}

/*
 * Do CISS_REPORT_PHYS and CISS_REPORT_LOG.  Data is returned in physdev,
 * logdev.  The number of luns in physdev and logdev are returned in
 * *nphysicals and *nlogicals, respectively.
 * Returns 0 on success, -1 otherwise.
 */
static int hpsa_gather_lun_info(struct ctlr_info *h,
	int reportlunsize,
	struct ReportLUNdata *physdev, u32 *nphysicals,
	struct ReportLUNdata *logdev, u32 *nlogicals)
{
	if (hpsa_scsi_do_report_phys_luns(h, physdev, reportlunsize, 0)) {
		dev_err(&h->pdev->dev, "report physical LUNs failed.\n");
		return -1;
	}
	*nphysicals = be32_to_cpu(*((__be32 *)physdev->LUNListLength)) / 8;
	if (*nphysicals > HPSA_MAX_PHYS_LUN) {
		dev_warn(&h->pdev->dev, "maximum physical LUNs (%d) exceeded."
			"  %d LUNs ignored.\n", HPSA_MAX_PHYS_LUN,
			*nphysicals - HPSA_MAX_PHYS_LUN);
		*nphysicals = HPSA_MAX_PHYS_LUN;
	}
	if (hpsa_scsi_do_report_log_luns(h, logdev, reportlunsize)) {
		dev_err(&h->pdev->dev, "report logical LUNs failed.\n");
		return -1;
	}
	*nlogicals = be32_to_cpu(*((__be32 *) logdev->LUNListLength)) / 8;
	/* Reject Logicals in excess of our max capability. */
	if (*nlogicals > HPSA_MAX_LUN) {
		dev_warn(&h->pdev->dev,
			"maximum logical LUNs (%d) exceeded.  "
			"%d LUNs ignored.\n", HPSA_MAX_LUN,
			*nlogicals - HPSA_MAX_LUN);
			*nlogicals = HPSA_MAX_LUN;
	}
	if (*nlogicals + *nphysicals > HPSA_MAX_PHYS_LUN) {
		dev_warn(&h->pdev->dev,
			"maximum logical + physical LUNs (%d) exceeded. "
			"%d LUNs ignored.\n", HPSA_MAX_PHYS_LUN,
			*nphysicals + *nlogicals - HPSA_MAX_PHYS_LUN);
		*nlogicals = HPSA_MAX_PHYS_LUN - *nphysicals;
	}
	return 0;
}

u8 *figure_lunaddrbytes(struct ctlr_info *h, int raid_ctlr_position, int i,
	int nphysicals, int nlogicals, struct ReportLUNdata *physdev_list,
	struct ReportLUNdata *logdev_list)
{
	/* Helper function, figure out where the LUN ID info is coming from
	 * given index i, lists of physical and logical devices, where in
	 * the list the raid controller is supposed to appear (first or last)
	 */

	int logicals_start = nphysicals + (raid_ctlr_position == 0);
	int last_device = nphysicals + nlogicals + (raid_ctlr_position == 0);

	if (i == raid_ctlr_position)
		return RAID_CTLR_LUNID;

	if (i < logicals_start)
		return &physdev_list->LUN[i - (raid_ctlr_position == 0)][0];

	if (i < last_device)
		return &logdev_list->LUN[i - nphysicals -
			(raid_ctlr_position == 0)][0];
	BUG();
	return NULL;
}

static void hpsa_update_scsi_devices(struct ctlr_info *h, int hostno)
{
	/* the idea here is we could get notified
	 * that some devices have changed, so we do a report
	 * physical luns and report logical luns cmd, and adjust
	 * our list of devices accordingly.
	 *
	 * The scsi3addr's of devices won't change so long as the
	 * adapter is not reset.  That means we can rescan and
	 * tell which devices we already know about, vs. new
	 * devices, vs.  disappearing devices.
	 */
	struct ReportLUNdata *physdev_list = NULL;
	struct ReportLUNdata *logdev_list = NULL;
	unsigned char *inq_buff = NULL;
	u32 nphysicals = 0;
	u32 nlogicals = 0;
	u32 ndev_allocated = 0;
	struct hpsa_scsi_dev_t **currentsd, *this_device, *tmpdevice;
	int ncurrent = 0;
	int reportlunsize = sizeof(*physdev_list) + HPSA_MAX_PHYS_LUN * 8;
	int i, nmsa2xxx_enclosures, ndevs_to_allocate;
	int bus, target, lun;
	int raid_ctlr_position;
	DECLARE_BITMAP(lunzerobits, HPSA_MAX_TARGETS_PER_CTLR);

	currentsd = kzalloc(sizeof(*currentsd) * HPSA_MAX_SCSI_DEVS_PER_HBA,
		GFP_KERNEL);
	physdev_list = kzalloc(reportlunsize, GFP_KERNEL);
	logdev_list = kzalloc(reportlunsize, GFP_KERNEL);
	inq_buff = kmalloc(OBDR_TAPE_INQ_SIZE, GFP_KERNEL);
	tmpdevice = kzalloc(sizeof(*tmpdevice), GFP_KERNEL);

	if (!currentsd || !physdev_list || !logdev_list ||
		!inq_buff || !tmpdevice) {
		dev_err(&h->pdev->dev, "out of memory\n");
		goto out;
	}
	memset(lunzerobits, 0, sizeof(lunzerobits));

	if (hpsa_gather_lun_info(h, reportlunsize, physdev_list, &nphysicals,
			logdev_list, &nlogicals))
		goto out;

	/* We might see up to 32 MSA2xxx enclosures, actually 8 of them
	 * but each of them 4 times through different paths.  The plus 1
	 * is for the RAID controller.
	 */
	ndevs_to_allocate = nphysicals + nlogicals + MAX_MSA2XXX_ENCLOSURES + 1;

	/* Allocate the per device structures */
	for (i = 0; i < ndevs_to_allocate; i++) {
		currentsd[i] = kzalloc(sizeof(*currentsd[i]), GFP_KERNEL);
		if (!currentsd[i]) {
			dev_warn(&h->pdev->dev, "out of memory at %s:%d\n",
				__FILE__, __LINE__);
			goto out;
		}
		ndev_allocated++;
	}

	if (unlikely(is_scsi_rev_5(h)))
		raid_ctlr_position = 0;
	else
		raid_ctlr_position = nphysicals + nlogicals;

	/* adjust our table of devices */
	nmsa2xxx_enclosures = 0;
	for (i = 0; i < nphysicals + nlogicals + 1; i++) {
		u8 *lunaddrbytes;

		/* Figure out where the LUN ID info is coming from */
		lunaddrbytes = figure_lunaddrbytes(h, raid_ctlr_position,
			i, nphysicals, nlogicals, physdev_list, logdev_list);
		/* skip masked physical devices. */
		if (lunaddrbytes[3] & 0xC0 &&
			i < nphysicals + (raid_ctlr_position == 0))
			continue;

		/* Get device type, vendor, model, device id */
		if (hpsa_update_device_info(h, lunaddrbytes, tmpdevice))
			continue; /* skip it if we can't talk to it. */
		figure_bus_target_lun(h, lunaddrbytes, &bus, &target, &lun,
			tmpdevice);
		this_device = currentsd[ncurrent];

		/*
		 * For the msa2xxx boxes, we have to insert a LUN 0 which
		 * doesn't show up in CCISS_REPORT_PHYSICAL data, but there
		 * is nonetheless an enclosure device there.  We have to
		 * present that otherwise linux won't find anything if
		 * there is no lun 0.
		 */
		if (add_msa2xxx_enclosure_device(h, tmpdevice, this_device,
				lunaddrbytes, bus, target, lun, lunzerobits,
				&nmsa2xxx_enclosures)) {
			ncurrent++;
			this_device = currentsd[ncurrent];
		}

		*this_device = *tmpdevice;
		hpsa_set_bus_target_lun(this_device, bus, target, lun);

		switch (this_device->devtype) {
		case TYPE_ROM: {
			/* We don't *really* support actual CD-ROM devices,
			 * just "One Button Disaster Recovery" tape drive
			 * which temporarily pretends to be a CD-ROM drive.
			 * So we check that the device is really an OBDR tape
			 * device by checking for "$DR-10" in bytes 43-48 of
			 * the inquiry data.
			 */
				char obdr_sig[7];
#define OBDR_TAPE_SIG "$DR-10"
				strncpy(obdr_sig, &inq_buff[43], 6);
				obdr_sig[6] = '\0';
				if (strncmp(obdr_sig, OBDR_TAPE_SIG, 6) != 0)
					/* Not OBDR device, ignore it. */
					break;
			}
			ncurrent++;
			break;
		case TYPE_DISK:
			if (i < nphysicals)
				break;
			ncurrent++;
			break;
		case TYPE_TAPE:
		case TYPE_MEDIUM_CHANGER:
			ncurrent++;
			break;
		case TYPE_RAID:
			/* Only present the Smartarray HBA as a RAID controller.
			 * If it's a RAID controller other than the HBA itself
			 * (an external RAID controller, MSA500 or similar)
			 * don't present it.
			 */
			if (!is_hba_lunid(lunaddrbytes))
				break;
			ncurrent++;
			break;
		default:
			break;
		}
		if (ncurrent >= HPSA_MAX_SCSI_DEVS_PER_HBA)
			break;
	}
	adjust_hpsa_scsi_table(h, hostno, currentsd, ncurrent);
out:
	kfree(tmpdevice);
	for (i = 0; i < ndev_allocated; i++)
		kfree(currentsd[i]);
	kfree(currentsd);
	kfree(inq_buff);
	kfree(physdev_list);
	kfree(logdev_list);
}

/* hpsa_scatter_gather takes a struct scsi_cmnd, (cmd), and does the pci
 * dma mapping  and fills in the scatter gather entries of the
 * hpsa command, cp.
 */
static int hpsa_scatter_gather(struct pci_dev *pdev,
		struct CommandList *cp,
		struct scsi_cmnd *cmd)
{
	unsigned int len;
	struct scatterlist *sg;
	u64 addr64;
	int use_sg, i;

	BUG_ON(scsi_sg_count(cmd) > MAXSGENTRIES);

	use_sg = scsi_dma_map(cmd);
	if (use_sg < 0)
		return use_sg;

	if (!use_sg)
		goto sglist_finished;

	scsi_for_each_sg(cmd, sg, use_sg, i) {
		addr64 = (u64) sg_dma_address(sg);
		len  = sg_dma_len(sg);
		cp->SG[i].Addr.lower =
			(u32) (addr64 & (u64) 0x00000000FFFFFFFF);
		cp->SG[i].Addr.upper =
			(u32) ((addr64 >> 32) & (u64) 0x00000000FFFFFFFF);
		cp->SG[i].Len = len;
		cp->SG[i].Ext = 0;  /* we are not chaining */
	}

sglist_finished:

	cp->Header.SGList = (u8) use_sg;   /* no. SGs contig in this cmd */
	cp->Header.SGTotal = (u16) use_sg; /* total sgs in this cmd list */
	return 0;
}


static int hpsa_scsi_queue_command(struct scsi_cmnd *cmd,
	void (*done)(struct scsi_cmnd *))
{
	struct ctlr_info *h;
	struct hpsa_scsi_dev_t *dev;
	unsigned char scsi3addr[8];
	struct CommandList *c;
	unsigned long flags;

	/* Get the ptr to our adapter structure out of cmd->host. */
	h = sdev_to_hba(cmd->device);
	dev = cmd->device->hostdata;
	if (!dev) {
		cmd->result = DID_NO_CONNECT << 16;
		done(cmd);
		return 0;
	}
	memcpy(scsi3addr, dev->scsi3addr, sizeof(scsi3addr));

	/* Need a lock as this is being allocated from the pool */
	spin_lock_irqsave(&h->lock, flags);
	c = cmd_alloc(h);
	spin_unlock_irqrestore(&h->lock, flags);
	if (c == NULL) {			/* trouble... */
		dev_err(&h->pdev->dev, "cmd_alloc returned NULL!\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* Fill in the command list header */

	cmd->scsi_done = done;    /* save this for use by completion code */

	/* save c in case we have to abort it  */
	cmd->host_scribble = (unsigned char *) c;

	c->cmd_type = CMD_SCSI;
	c->scsi_cmd = cmd;
	c->Header.ReplyQueue = 0;  /* unused in simple mode */
	memcpy(&c->Header.LUN.LunAddrBytes[0], &scsi3addr[0], 8);
	c->Header.Tag.lower = (c->cmdindex << DIRECT_LOOKUP_SHIFT);
	c->Header.Tag.lower |= DIRECT_LOOKUP_BIT;

	/* Fill in the request block... */

	c->Request.Timeout = 0;
	memset(c->Request.CDB, 0, sizeof(c->Request.CDB));
	BUG_ON(cmd->cmd_len > sizeof(c->Request.CDB));
	c->Request.CDBLen = cmd->cmd_len;
	memcpy(c->Request.CDB, cmd->cmnd, cmd->cmd_len);
	c->Request.Type.Type = TYPE_CMD;
	c->Request.Type.Attribute = ATTR_SIMPLE;
	switch (cmd->sc_data_direction) {
	case DMA_TO_DEVICE:
		c->Request.Type.Direction = XFER_WRITE;
		break;
	case DMA_FROM_DEVICE:
		c->Request.Type.Direction = XFER_READ;
		break;
	case DMA_NONE:
		c->Request.Type.Direction = XFER_NONE;
		break;
	case DMA_BIDIRECTIONAL:
		/* This can happen if a buggy application does a scsi passthru
		 * and sets both inlen and outlen to non-zero. ( see
		 * ../scsi/scsi_ioctl.c:scsi_ioctl_send_command() )
		 */

		c->Request.Type.Direction = XFER_RSVD;
		/* This is technically wrong, and hpsa controllers should
		 * reject it with CMD_INVALID, which is the most correct
		 * response, but non-fibre backends appear to let it
		 * slide by, and give the same results as if this field
		 * were set correctly.  Either way is acceptable for
		 * our purposes here.
		 */

		break;

	default:
		dev_err(&h->pdev->dev, "unknown data direction: %d\n",
			cmd->sc_data_direction);
		BUG();
		break;
	}

	if (hpsa_scatter_gather(h->pdev, c, cmd) < 0) { /* Fill SG list */
		cmd_free(h, c);
		return SCSI_MLQUEUE_HOST_BUSY;
	}
	enqueue_cmd_and_start_io(h, c);
	/* the cmd'll come back via intr handler in complete_scsi_command()  */
	return 0;
}

static void hpsa_scan_start(struct Scsi_Host *sh)
{
	struct ctlr_info *h = shost_to_hba(sh);
	unsigned long flags;

	/* wait until any scan already in progress is finished. */
	while (1) {
		spin_lock_irqsave(&h->scan_lock, flags);
		if (h->scan_finished)
			break;
		spin_unlock_irqrestore(&h->scan_lock, flags);
		wait_event(h->scan_wait_queue, h->scan_finished);
		/* Note: We don't need to worry about a race between this
		 * thread and driver unload because the midlayer will
		 * have incremented the reference count, so unload won't
		 * happen if we're in here.
		 */
	}
	h->scan_finished = 0; /* mark scan as in progress */
	spin_unlock_irqrestore(&h->scan_lock, flags);

	hpsa_update_scsi_devices(h, h->scsi_host->host_no);

	spin_lock_irqsave(&h->scan_lock, flags);
	h->scan_finished = 1; /* mark scan as finished. */
	wake_up_all(&h->scan_wait_queue);
	spin_unlock_irqrestore(&h->scan_lock, flags);
}

static int hpsa_scan_finished(struct Scsi_Host *sh,
	unsigned long elapsed_time)
{
	struct ctlr_info *h = shost_to_hba(sh);
	unsigned long flags;
	int finished;

	spin_lock_irqsave(&h->scan_lock, flags);
	finished = h->scan_finished;
	spin_unlock_irqrestore(&h->scan_lock, flags);
	return finished;
}

static int hpsa_change_queue_depth(struct scsi_device *sdev,
	int qdepth, int reason)
{
	struct ctlr_info *h = sdev_to_hba(sdev);

	if (reason != SCSI_QDEPTH_DEFAULT)
		return -ENOTSUPP;

	if (qdepth < 1)
		qdepth = 1;
	else
		if (qdepth > h->nr_cmds)
			qdepth = h->nr_cmds;
	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
	return sdev->queue_depth;
}

static void hpsa_unregister_scsi(struct ctlr_info *h)
{
	/* we are being forcibly unloaded, and may not refuse. */
	scsi_remove_host(h->scsi_host);
	scsi_host_put(h->scsi_host);
	h->scsi_host = NULL;
}

static int hpsa_register_scsi(struct ctlr_info *h)
{
	int rc;

	rc = hpsa_scsi_detect(h);
	if (rc != 0)
		dev_err(&h->pdev->dev, "hpsa_register_scsi: failed"
			" hpsa_scsi_detect(), rc is %d\n", rc);
	return rc;
}

static int wait_for_device_to_become_ready(struct ctlr_info *h,
	unsigned char lunaddr[])
{
	int rc = 0;
	int count = 0;
	int waittime = 1; /* seconds */
	struct CommandList *c;

	c = cmd_special_alloc(h);
	if (!c) {
		dev_warn(&h->pdev->dev, "out of memory in "
			"wait_for_device_to_become_ready.\n");
		return IO_ERROR;
	}

	/* Send test unit ready until device ready, or give up. */
	while (count < HPSA_TUR_RETRY_LIMIT) {

		/* Wait for a bit.  do this first, because if we send
		 * the TUR right away, the reset will just abort it.
		 */
		msleep(1000 * waittime);
		count++;

		/* Increase wait time with each try, up to a point. */
		if (waittime < HPSA_MAX_WAIT_INTERVAL_SECS)
			waittime = waittime * 2;

		/* Send the Test Unit Ready */
		fill_cmd(c, TEST_UNIT_READY, h, NULL, 0, 0, lunaddr, TYPE_CMD);
		hpsa_scsi_do_simple_cmd_core(h, c);
		/* no unmap needed here because no data xfer. */

		if (c->err_info->CommandStatus == CMD_SUCCESS)
			break;

		if (c->err_info->CommandStatus == CMD_TARGET_STATUS &&
			c->err_info->ScsiStatus == SAM_STAT_CHECK_CONDITION &&
			(c->err_info->SenseInfo[2] == NO_SENSE ||
			c->err_info->SenseInfo[2] == UNIT_ATTENTION))
			break;

		dev_warn(&h->pdev->dev, "waiting %d secs "
			"for device to become ready.\n", waittime);
		rc = 1; /* device not ready. */
	}

	if (rc)
		dev_warn(&h->pdev->dev, "giving up on device.\n");
	else
		dev_warn(&h->pdev->dev, "device is ready.\n");

	cmd_special_free(h, c);
	return rc;
}

/* Need at least one of these error handlers to keep ../scsi/hosts.c from
 * complaining.  Doing a host- or bus-reset can't do anything good here.
 */
static int hpsa_eh_device_reset_handler(struct scsi_cmnd *scsicmd)
{
	int rc;
	struct ctlr_info *h;
	struct hpsa_scsi_dev_t *dev;

	/* find the controller to which the command to be aborted was sent */
	h = sdev_to_hba(scsicmd->device);
	if (h == NULL) /* paranoia */
		return FAILED;
	dev = scsicmd->device->hostdata;
	if (!dev) {
		dev_err(&h->pdev->dev, "hpsa_eh_device_reset_handler: "
			"device lookup failed.\n");
		return FAILED;
	}
	dev_warn(&h->pdev->dev, "resetting device %d:%d:%d:%d\n",
		h->scsi_host->host_no, dev->bus, dev->target, dev->lun);
	/* send a reset to the SCSI LUN which the command was sent to */
	rc = hpsa_send_reset(h, dev->scsi3addr);
	if (rc == 0 && wait_for_device_to_become_ready(h, dev->scsi3addr) == 0)
		return SUCCESS;

	dev_warn(&h->pdev->dev, "resetting device failed.\n");
	return FAILED;
}

/*
 * For operations that cannot sleep, a command block is allocated at init,
 * and managed by cmd_alloc() and cmd_free() using a simple bitmap to track
 * which ones are free or in use.  Lock must be held when calling this.
 * cmd_free() is the complement.
 */
static struct CommandList *cmd_alloc(struct ctlr_info *h)
{
	struct CommandList *c;
	int i;
	union u64bit temp64;
	dma_addr_t cmd_dma_handle, err_dma_handle;

	do {
		i = find_first_zero_bit(h->cmd_pool_bits, h->nr_cmds);
		if (i == h->nr_cmds)
			return NULL;
	} while (test_and_set_bit
		 (i & (BITS_PER_LONG - 1),
		  h->cmd_pool_bits + (i / BITS_PER_LONG)) != 0);
	c = h->cmd_pool + i;
	memset(c, 0, sizeof(*c));
	cmd_dma_handle = h->cmd_pool_dhandle
	    + i * sizeof(*c);
	c->err_info = h->errinfo_pool + i;
	memset(c->err_info, 0, sizeof(*c->err_info));
	err_dma_handle = h->errinfo_pool_dhandle
	    + i * sizeof(*c->err_info);
	h->nr_allocs++;

	c->cmdindex = i;

	INIT_HLIST_NODE(&c->list);
	c->busaddr = (u32) cmd_dma_handle;
	temp64.val = (u64) err_dma_handle;
	c->ErrDesc.Addr.lower = temp64.val32.lower;
	c->ErrDesc.Addr.upper = temp64.val32.upper;
	c->ErrDesc.Len = sizeof(*c->err_info);

	c->h = h;
	return c;
}

/* For operations that can wait for kmalloc to possibly sleep,
 * this routine can be called. Lock need not be held to call
 * cmd_special_alloc. cmd_special_free() is the complement.
 */
static struct CommandList *cmd_special_alloc(struct ctlr_info *h)
{
	struct CommandList *c;
	union u64bit temp64;
	dma_addr_t cmd_dma_handle, err_dma_handle;

	c = pci_alloc_consistent(h->pdev, sizeof(*c), &cmd_dma_handle);
	if (c == NULL)
		return NULL;
	memset(c, 0, sizeof(*c));

	c->cmdindex = -1;

	c->err_info = pci_alloc_consistent(h->pdev, sizeof(*c->err_info),
		    &err_dma_handle);

	if (c->err_info == NULL) {
		pci_free_consistent(h->pdev,
			sizeof(*c), c, cmd_dma_handle);
		return NULL;
	}
	memset(c->err_info, 0, sizeof(*c->err_info));

	INIT_HLIST_NODE(&c->list);
	c->busaddr = (u32) cmd_dma_handle;
	temp64.val = (u64) err_dma_handle;
	c->ErrDesc.Addr.lower = temp64.val32.lower;
	c->ErrDesc.Addr.upper = temp64.val32.upper;
	c->ErrDesc.Len = sizeof(*c->err_info);

	c->h = h;
	return c;
}

static void cmd_free(struct ctlr_info *h, struct CommandList *c)
{
	int i;

	i = c - h->cmd_pool;
	clear_bit(i & (BITS_PER_LONG - 1),
		  h->cmd_pool_bits + (i / BITS_PER_LONG));
	h->nr_frees++;
}

static void cmd_special_free(struct ctlr_info *h, struct CommandList *c)
{
	union u64bit temp64;

	temp64.val32.lower = c->ErrDesc.Addr.lower;
	temp64.val32.upper = c->ErrDesc.Addr.upper;
	pci_free_consistent(h->pdev, sizeof(*c->err_info),
			    c->err_info, (dma_addr_t) temp64.val);
	pci_free_consistent(h->pdev, sizeof(*c),
			    c, (dma_addr_t) c->busaddr);
}

#ifdef CONFIG_COMPAT

static int hpsa_ioctl32_passthru(struct scsi_device *dev, int cmd, void *arg)
{
	IOCTL32_Command_struct __user *arg32 =
	    (IOCTL32_Command_struct __user *) arg;
	IOCTL_Command_struct arg64;
	IOCTL_Command_struct __user *p = compat_alloc_user_space(sizeof(arg64));
	int err;
	u32 cp;

	err = 0;
	err |= copy_from_user(&arg64.LUN_info, &arg32->LUN_info,
			   sizeof(arg64.LUN_info));
	err |= copy_from_user(&arg64.Request, &arg32->Request,
			   sizeof(arg64.Request));
	err |= copy_from_user(&arg64.error_info, &arg32->error_info,
			   sizeof(arg64.error_info));
	err |= get_user(arg64.buf_size, &arg32->buf_size);
	err |= get_user(cp, &arg32->buf);
	arg64.buf = compat_ptr(cp);
	err |= copy_to_user(p, &arg64, sizeof(arg64));

	if (err)
		return -EFAULT;

	err = hpsa_ioctl(dev, CCISS_PASSTHRU, (void *)p);
	if (err)
		return err;
	err |= copy_in_user(&arg32->error_info, &p->error_info,
			 sizeof(arg32->error_info));
	if (err)
		return -EFAULT;
	return err;
}

static int hpsa_ioctl32_big_passthru(struct scsi_device *dev,
	int cmd, void *arg)
{
	BIG_IOCTL32_Command_struct __user *arg32 =
	    (BIG_IOCTL32_Command_struct __user *) arg;
	BIG_IOCTL_Command_struct arg64;
	BIG_IOCTL_Command_struct __user *p =
	    compat_alloc_user_space(sizeof(arg64));
	int err;
	u32 cp;

	err = 0;
	err |= copy_from_user(&arg64.LUN_info, &arg32->LUN_info,
			   sizeof(arg64.LUN_info));
	err |= copy_from_user(&arg64.Request, &arg32->Request,
			   sizeof(arg64.Request));
	err |= copy_from_user(&arg64.error_info, &arg32->error_info,
			   sizeof(arg64.error_info));
	err |= get_user(arg64.buf_size, &arg32->buf_size);
	err |= get_user(arg64.malloc_size, &arg32->malloc_size);
	err |= get_user(cp, &arg32->buf);
	arg64.buf = compat_ptr(cp);
	err |= copy_to_user(p, &arg64, sizeof(arg64));

	if (err)
		return -EFAULT;

	err = hpsa_ioctl(dev, CCISS_BIG_PASSTHRU, (void *)p);
	if (err)
		return err;
	err |= copy_in_user(&arg32->error_info, &p->error_info,
			 sizeof(arg32->error_info));
	if (err)
		return -EFAULT;
	return err;
}

static int hpsa_compat_ioctl(struct scsi_device *dev, int cmd, void *arg)
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
		return hpsa_ioctl(dev, cmd, arg);

	case CCISS_PASSTHRU32:
		return hpsa_ioctl32_passthru(dev, cmd, arg);
	case CCISS_BIG_PASSTHRU32:
		return hpsa_ioctl32_big_passthru(dev, cmd, arg);

	default:
		return -ENOIOCTLCMD;
	}
}
#endif

static int hpsa_getpciinfo_ioctl(struct ctlr_info *h, void __user *argp)
{
	struct hpsa_pci_info pciinfo;

	if (!argp)
		return -EINVAL;
	pciinfo.domain = pci_domain_nr(h->pdev->bus);
	pciinfo.bus = h->pdev->bus->number;
	pciinfo.dev_fn = h->pdev->devfn;
	pciinfo.board_id = h->board_id;
	if (copy_to_user(argp, &pciinfo, sizeof(pciinfo)))
		return -EFAULT;
	return 0;
}

static int hpsa_getdrivver_ioctl(struct ctlr_info *h, void __user *argp)
{
	DriverVer_type DriverVer;
	unsigned char vmaj, vmin, vsubmin;
	int rc;

	rc = sscanf(HPSA_DRIVER_VERSION, "%hhu.%hhu.%hhu",
		&vmaj, &vmin, &vsubmin);
	if (rc != 3) {
		dev_info(&h->pdev->dev, "driver version string '%s' "
			"unrecognized.", HPSA_DRIVER_VERSION);
		vmaj = 0;
		vmin = 0;
		vsubmin = 0;
	}
	DriverVer = (vmaj << 16) | (vmin << 8) | vsubmin;
	if (!argp)
		return -EINVAL;
	if (copy_to_user(argp, &DriverVer, sizeof(DriverVer_type)))
		return -EFAULT;
	return 0;
}

static int hpsa_passthru_ioctl(struct ctlr_info *h, void __user *argp)
{
	IOCTL_Command_struct iocommand;
	struct CommandList *c;
	char *buff = NULL;
	union u64bit temp64;

	if (!argp)
		return -EINVAL;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (copy_from_user(&iocommand, argp, sizeof(iocommand)))
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
	} else
		memset(buff, 0, iocommand.buf_size);
	c = cmd_special_alloc(h);
	if (c == NULL) {
		kfree(buff);
		return -ENOMEM;
	}
	/* Fill in the command type */
	c->cmd_type = CMD_IOCTL_PEND;
	/* Fill in Command Header */
	c->Header.ReplyQueue = 0; /* unused in simple mode */
	if (iocommand.buf_size > 0) {	/* buffer to fill */
		c->Header.SGList = 1;
		c->Header.SGTotal = 1;
	} else	{ /* no buffers to fill */
		c->Header.SGList = 0;
		c->Header.SGTotal = 0;
	}
	memcpy(&c->Header.LUN, &iocommand.LUN_info, sizeof(c->Header.LUN));
	/* use the kernel address the cmd block for tag */
	c->Header.Tag.lower = c->busaddr;

	/* Fill in Request block */
	memcpy(&c->Request, &iocommand.Request,
		sizeof(c->Request));

	/* Fill in the scatter gather information */
	if (iocommand.buf_size > 0) {
		temp64.val = pci_map_single(h->pdev, buff,
			iocommand.buf_size, PCI_DMA_BIDIRECTIONAL);
		c->SG[0].Addr.lower = temp64.val32.lower;
		c->SG[0].Addr.upper = temp64.val32.upper;
		c->SG[0].Len = iocommand.buf_size;
		c->SG[0].Ext = 0; /* we are not chaining*/
	}
	hpsa_scsi_do_simple_cmd_core(h, c);
	hpsa_pci_unmap(h->pdev, c, 1, PCI_DMA_BIDIRECTIONAL);
	check_ioctl_unit_attention(h, c);

	/* Copy the error information out */
	memcpy(&iocommand.error_info, c->err_info,
		sizeof(iocommand.error_info));
	if (copy_to_user(argp, &iocommand, sizeof(iocommand))) {
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

static int hpsa_big_passthru_ioctl(struct ctlr_info *h, void __user *argp)
{
	BIG_IOCTL_Command_struct *ioc;
	struct CommandList *c;
	unsigned char **buff = NULL;
	int *buff_size = NULL;
	union u64bit temp64;
	BYTE sg_used = 0;
	int status = 0;
	int i;
	u32 left;
	u32 sz;
	BYTE __user *data_ptr;

	if (!argp)
		return -EINVAL;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	ioc = (BIG_IOCTL_Command_struct *)
	    kmalloc(sizeof(*ioc), GFP_KERNEL);
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
				status = -ENOMEM;
				goto cleanup1;
			}
		} else
			memset(buff[sg_used], 0, sz);
		left -= sz;
		data_ptr += sz;
		sg_used++;
	}
	c = cmd_special_alloc(h);
	if (c == NULL) {
		status = -ENOMEM;
		goto cleanup1;
	}
	c->cmd_type = CMD_IOCTL_PEND;
	c->Header.ReplyQueue = 0;

	if (ioc->buf_size > 0) {
		c->Header.SGList = sg_used;
		c->Header.SGTotal = sg_used;
	} else {
		c->Header.SGList = 0;
		c->Header.SGTotal = 0;
	}
	memcpy(&c->Header.LUN, &ioc->LUN_info, sizeof(c->Header.LUN));
	c->Header.Tag.lower = c->busaddr;
	memcpy(&c->Request, &ioc->Request, sizeof(c->Request));
	if (ioc->buf_size > 0) {
		int i;
		for (i = 0; i < sg_used; i++) {
			temp64.val = pci_map_single(h->pdev, buff[i],
				    buff_size[i], PCI_DMA_BIDIRECTIONAL);
			c->SG[i].Addr.lower = temp64.val32.lower;
			c->SG[i].Addr.upper = temp64.val32.upper;
			c->SG[i].Len = buff_size[i];
			/* we are not chaining */
			c->SG[i].Ext = 0;
		}
	}
	hpsa_scsi_do_simple_cmd_core(h, c);
	hpsa_pci_unmap(h->pdev, c, sg_used, PCI_DMA_BIDIRECTIONAL);
	check_ioctl_unit_attention(h, c);
	/* Copy the error information out */
	memcpy(&ioc->error_info, c->err_info, sizeof(ioc->error_info));
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

static void check_ioctl_unit_attention(struct ctlr_info *h,
	struct CommandList *c)
{
	if (c->err_info->CommandStatus == CMD_TARGET_STATUS &&
			c->err_info->ScsiStatus != SAM_STAT_CHECK_CONDITION)
		(void) check_for_unit_attention(h, c);
}
/*
 * ioctl
 */
static int hpsa_ioctl(struct scsi_device *dev, int cmd, void *arg)
{
	struct ctlr_info *h;
	void __user *argp = (void __user *)arg;

	h = sdev_to_hba(dev);

	switch (cmd) {
	case CCISS_DEREGDISK:
	case CCISS_REGNEWDISK:
	case CCISS_REGNEWD:
		hpsa_scan_start(h->scsi_host);
		return 0;
	case CCISS_GETPCIINFO:
		return hpsa_getpciinfo_ioctl(h, argp);
	case CCISS_GETDRIVVER:
		return hpsa_getdrivver_ioctl(h, argp);
	case CCISS_PASSTHRU:
		return hpsa_passthru_ioctl(h, argp);
	case CCISS_BIG_PASSTHRU:
		return hpsa_big_passthru_ioctl(h, argp);
	default:
		return -ENOTTY;
	}
}

static void fill_cmd(struct CommandList *c, u8 cmd, struct ctlr_info *h,
	void *buff, size_t size, u8 page_code, unsigned char *scsi3addr,
	int cmd_type)
{
	int pci_dir = XFER_NONE;

	c->cmd_type = CMD_IOCTL_PEND;
	c->Header.ReplyQueue = 0;
	if (buff != NULL && size > 0) {
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
		case HPSA_INQUIRY:
			/* are we trying to read a vital product page */
			if (page_code != 0) {
				c->Request.CDB[1] = 0x01;
				c->Request.CDB[2] = page_code;
			}
			c->Request.CDBLen = 6;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = HPSA_INQUIRY;
			c->Request.CDB[4] = size & 0xFF;
			break;
		case HPSA_REPORT_LOG:
		case HPSA_REPORT_PHYS:
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

		case HPSA_READ_CAPACITY:
			c->Request.CDBLen = 10;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			break;
		case HPSA_CACHE_FLUSH:
			c->Request.CDBLen = 12;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_WRITE;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = BMIC_WRITE;
			c->Request.CDB[6] = BMIC_CACHE_FLUSH;
			break;
		case TEST_UNIT_READY:
			c->Request.CDBLen = 6;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_NONE;
			c->Request.Timeout = 0;
			break;
		default:
			dev_warn(&h->pdev->dev, "unknown command 0x%c\n", cmd);
			BUG();
			return;
		}
	} else if (cmd_type == TYPE_MSG) {
		switch (cmd) {

		case  HPSA_DEVICE_RESET_MSG:
			c->Request.CDBLen = 16;
			c->Request.Type.Type =  1; /* It is a MSG not a CMD */
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_NONE;
			c->Request.Timeout = 0; /* Don't time out */
			c->Request.CDB[0] =  0x01; /* RESET_MSG is 0x01 */
			c->Request.CDB[1] = 0x03;  /* Reset target above */
			/* If bytes 4-7 are zero, it means reset the */
			/* LunID device */
			c->Request.CDB[4] = 0x00;
			c->Request.CDB[5] = 0x00;
			c->Request.CDB[6] = 0x00;
			c->Request.CDB[7] = 0x00;
		break;

		default:
			dev_warn(&h->pdev->dev, "unknown message type %d\n",
				cmd);
			BUG();
		}
	} else {
		dev_warn(&h->pdev->dev, "unknown command type %d\n", cmd_type);
		BUG();
	}

	switch (c->Request.Type.Direction) {
	case XFER_READ:
		pci_dir = PCI_DMA_FROMDEVICE;
		break;
	case XFER_WRITE:
		pci_dir = PCI_DMA_TODEVICE;
		break;
	case XFER_NONE:
		pci_dir = PCI_DMA_NONE;
		break;
	default:
		pci_dir = PCI_DMA_BIDIRECTIONAL;
	}

	hpsa_map_one(h->pdev, c, buff, size, pci_dir);

	return;
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

/* Takes cmds off the submission queue and sends them to the hardware,
 * then puts them on the queue of cmds waiting for completion.
 */
static void start_io(struct ctlr_info *h)
{
	struct CommandList *c;

	while (!hlist_empty(&h->reqQ)) {
		c = hlist_entry(h->reqQ.first, struct CommandList, list);
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

static inline unsigned long get_next_completion(struct ctlr_info *h)
{
	return h->access.command_completed(h);
}

static inline bool interrupt_pending(struct ctlr_info *h)
{
	return h->access.intr_pending(h);
}

static inline long interrupt_not_for_us(struct ctlr_info *h)
{
	return !(h->msi_vector || h->msix_vector) &&
		((h->access.intr_pending(h) == 0) ||
		(h->interrupts_enabled == 0));
}

static inline int bad_tag(struct ctlr_info *h, u32 tag_index,
	u32 raw_tag)
{
	if (unlikely(tag_index >= h->nr_cmds)) {
		dev_warn(&h->pdev->dev, "bad tag 0x%08x ignored.\n", raw_tag);
		return 1;
	}
	return 0;
}

static inline void finish_cmd(struct CommandList *c, u32 raw_tag)
{
	removeQ(c);
	if (likely(c->cmd_type == CMD_SCSI))
		complete_scsi_command(c, 0, raw_tag);
	else if (c->cmd_type == CMD_IOCTL_PEND)
		complete(c->waiting);
}

static inline u32 hpsa_tag_contains_index(u32 tag)
{
#define DIRECT_LOOKUP_BIT 0x10
	return tag & DIRECT_LOOKUP_BIT;
}

static inline u32 hpsa_tag_to_index(u32 tag)
{
#define DIRECT_LOOKUP_SHIFT 5
	return tag >> DIRECT_LOOKUP_SHIFT;
}

static inline u32 hpsa_tag_discard_error_bits(u32 tag)
{
#define HPSA_ERROR_BITS 0x03
	return tag & ~HPSA_ERROR_BITS;
}

/* process completion of an indexed ("direct lookup") command */
static inline u32 process_indexed_cmd(struct ctlr_info *h,
	u32 raw_tag)
{
	u32 tag_index;
	struct CommandList *c;

	tag_index = hpsa_tag_to_index(raw_tag);
	if (bad_tag(h, tag_index, raw_tag))
		return next_command(h);
	c = h->cmd_pool + tag_index;
	finish_cmd(c, raw_tag);
	return next_command(h);
}

/* process completion of a non-indexed command */
static inline u32 process_nonindexed_cmd(struct ctlr_info *h,
	u32 raw_tag)
{
	u32 tag;
	struct CommandList *c = NULL;
	struct hlist_node *tmp;

	tag = hpsa_tag_discard_error_bits(raw_tag);
	hlist_for_each_entry(c, tmp, &h->cmpQ, list) {
		if ((c->busaddr & 0xFFFFFFE0) == (tag & 0xFFFFFFE0)) {
			finish_cmd(c, raw_tag);
			return next_command(h);
		}
	}
	bad_tag(h, h->nr_cmds + 1, raw_tag);
	return next_command(h);
}

static irqreturn_t do_hpsa_intr(int irq, void *dev_id)
{
	struct ctlr_info *h = dev_id;
	unsigned long flags;
	u32 raw_tag;

	if (interrupt_not_for_us(h))
		return IRQ_NONE;
	spin_lock_irqsave(&h->lock, flags);
	raw_tag = get_next_completion(h);
	while (raw_tag != FIFO_EMPTY) {
		if (hpsa_tag_contains_index(raw_tag))
			raw_tag = process_indexed_cmd(h, raw_tag);
		else
			raw_tag = process_nonindexed_cmd(h, raw_tag);
	}
	spin_unlock_irqrestore(&h->lock, flags);
	return IRQ_HANDLED;
}

/* Send a message CDB to the firmware. */
static __devinit int hpsa_message(struct pci_dev *pdev, unsigned char opcode,
						unsigned char type)
{
	struct Command {
		struct CommandListHeader CommandHeader;
		struct RequestBlock Request;
		struct ErrDescriptor ErrorDescriptor;
	};
	struct Command *cmd;
	static const size_t cmd_sz = sizeof(*cmd) +
					sizeof(cmd->ErrorDescriptor);
	dma_addr_t paddr64;
	uint32_t paddr32, tag;
	void __iomem *vaddr;
	int i, err;

	vaddr = pci_ioremap_bar(pdev, 0);
	if (vaddr == NULL)
		return -ENOMEM;

	/* The Inbound Post Queue only accepts 32-bit physical addresses for the
	 * CCISS commands, so they must be allocated from the lower 4GiB of
	 * memory.
	 */
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
	 * although there's no guarantee, we assume that the address is at
	 * least 4-byte aligned (most likely, it's page-aligned).
	 */
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
	memset(&cmd->Request.CDB[2], 0, 14); /* rest of the CDB is reserved */
	cmd->ErrorDescriptor.Addr.lower = paddr32 + sizeof(*cmd);
	cmd->ErrorDescriptor.Addr.upper = 0;
	cmd->ErrorDescriptor.Len = sizeof(struct ErrorInfo);

	writel(paddr32, vaddr + SA5_REQUEST_PORT_OFFSET);

	for (i = 0; i < HPSA_MSG_SEND_RETRY_LIMIT; i++) {
		tag = readl(vaddr + SA5_REPLY_PORT_OFFSET);
		if (hpsa_tag_discard_error_bits(tag) == paddr32)
			break;
		msleep(HPSA_MSG_SEND_RETRY_INTERVAL_MSECS);
	}

	iounmap(vaddr);

	/* we leak the DMA buffer here ... no choice since the controller could
	 *  still complete the command.
	 */
	if (i == HPSA_MSG_SEND_RETRY_LIMIT) {
		dev_err(&pdev->dev, "controller message %02x:%02x timed out\n",
			opcode, type);
		return -ETIMEDOUT;
	}

	pci_free_consistent(pdev, cmd_sz, cmd, paddr64);

	if (tag & HPSA_ERROR_BIT) {
		dev_err(&pdev->dev, "controller message %02x:%02x failed\n",
			opcode, type);
		return -EIO;
	}

	dev_info(&pdev->dev, "controller message %02x:%02x succeeded\n",
		opcode, type);
	return 0;
}

#define hpsa_soft_reset_controller(p) hpsa_message(p, 1, 0)
#define hpsa_noop(p) hpsa_message(p, 3, 0)

static __devinit int hpsa_reset_msi(struct pci_dev *pdev)
{
/* the #defines are stolen from drivers/pci/msi.h. */
#define msi_control_reg(base)		(base + PCI_MSI_FLAGS)
#define PCI_MSIX_FLAGS_ENABLE		(1 << 15)

	int pos;
	u16 control = 0;

	pos = pci_find_capability(pdev, PCI_CAP_ID_MSI);
	if (pos) {
		pci_read_config_word(pdev, msi_control_reg(pos), &control);
		if (control & PCI_MSI_FLAGS_ENABLE) {
			dev_info(&pdev->dev, "resetting MSI\n");
			pci_write_config_word(pdev, msi_control_reg(pos),
					control & ~PCI_MSI_FLAGS_ENABLE);
		}
	}

	pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	if (pos) {
		pci_read_config_word(pdev, msi_control_reg(pos), &control);
		if (control & PCI_MSIX_FLAGS_ENABLE) {
			dev_info(&pdev->dev, "resetting MSI-X\n");
			pci_write_config_word(pdev, msi_control_reg(pos),
					control & ~PCI_MSIX_FLAGS_ENABLE);
		}
	}

	return 0;
}

/* This does a hard reset of the controller using PCI power management
 * states.
 */
static __devinit int hpsa_hard_reset_controller(struct pci_dev *pdev)
{
	u16 pmcsr, saved_config_space[32];
	int i, pos;

	dev_info(&pdev->dev, "using PCI PM to reset controller\n");

	/* This is very nearly the same thing as
	 *
	 * pci_save_state(pci_dev);
	 * pci_set_power_state(pci_dev, PCI_D3hot);
	 * pci_set_power_state(pci_dev, PCI_D0);
	 * pci_restore_state(pci_dev);
	 *
	 * but we can't use these nice canned kernel routines on
	 * kexec, because they also check the MSI/MSI-X state in PCI
	 * configuration space and do the wrong thing when it is
	 * set/cleared.  Also, the pci_save/restore_state functions
	 * violate the ordering requirements for restoring the
	 * configuration space from the CCISS document (see the
	 * comment below).  So we roll our own ....
	 */

	for (i = 0; i < 32; i++)
		pci_read_config_word(pdev, 2*i, &saved_config_space[i]);

	pos = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pos == 0) {
		dev_err(&pdev->dev,
			"hpsa_reset_controller: PCI PM not supported\n");
		return -ENODEV;
	}

	/* Quoting from the Open CISS Specification: "The Power
	 * Management Control/Status Register (CSR) controls the power
	 * state of the device.  The normal operating state is D0,
	 * CSR=00h.  The software off state is D3, CSR=03h.  To reset
	 * the controller, place the interface device in D3 then to
	 * D0, this causes a secondary PCI reset which will reset the
	 * controller."
	 */

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

	msleep(500);

	/* Restore the PCI configuration space.  The Open CISS
	 * Specification says, "Restore the PCI Configuration
	 * Registers, offsets 00h through 60h. It is important to
	 * restore the command register, 16-bits at offset 04h,
	 * last. Do not restore the configuration status register,
	 * 16-bits at offset 06h."  Note that the offset is 2*i.
	 */
	for (i = 0; i < 32; i++) {
		if (i == 2 || i == 3)
			continue;
		pci_write_config_word(pdev, 2*i, saved_config_space[i]);
	}
	wmb();
	pci_write_config_word(pdev, 4, saved_config_space[2]);

	return 0;
}

/*
 *  We cannot read the structure directly, for portability we must use
 *   the io functions.
 *   This is for debug only.
 */
#ifdef HPSA_DEBUG
static void print_cfg_table(struct device *dev, struct CfgTable *tb)
{
	int i;
	char temp_name[17];

	dev_info(dev, "Controller Configuration information\n");
	dev_info(dev, "------------------------------------\n");
	for (i = 0; i < 4; i++)
		temp_name[i] = readb(&(tb->Signature[i]));
	temp_name[4] = '\0';
	dev_info(dev, "   Signature = %s\n", temp_name);
	dev_info(dev, "   Spec Number = %d\n", readl(&(tb->SpecValence)));
	dev_info(dev, "   Transport methods supported = 0x%x\n",
	       readl(&(tb->TransportSupport)));
	dev_info(dev, "   Transport methods active = 0x%x\n",
	       readl(&(tb->TransportActive)));
	dev_info(dev, "   Requested transport Method = 0x%x\n",
	       readl(&(tb->HostWrite.TransportRequest)));
	dev_info(dev, "   Coalesce Interrupt Delay = 0x%x\n",
	       readl(&(tb->HostWrite.CoalIntDelay)));
	dev_info(dev, "   Coalesce Interrupt Count = 0x%x\n",
	       readl(&(tb->HostWrite.CoalIntCount)));
	dev_info(dev, "   Max outstanding commands = 0x%d\n",
	       readl(&(tb->CmdsOutMax)));
	dev_info(dev, "   Bus Types = 0x%x\n", readl(&(tb->BusTypes)));
	for (i = 0; i < 16; i++)
		temp_name[i] = readb(&(tb->ServerName[i]));
	temp_name[16] = '\0';
	dev_info(dev, "   Server Name = %s\n", temp_name);
	dev_info(dev, "   Heartbeat Counter = 0x%x\n\n\n",
		readl(&(tb->HeartBeat)));
}
#endif				/* HPSA_DEBUG */

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
				       "base address is invalid\n");
				return -1;
				break;
			}
		}
		if (offset == pci_bar_addr - PCI_BASE_ADDRESS_0)
			return i + 1;
	}
	return -1;
}

/* If MSI/MSI-X is supported by the kernel we will try to enable it on
 * controllers that are capable. If not, we use IO-APIC mode.
 */

static void __devinit hpsa_interrupt_mode(struct ctlr_info *h,
					   struct pci_dev *pdev, u32 board_id)
{
#ifdef CONFIG_PCI_MSI
	int err;
	struct msix_entry hpsa_msix_entries[4] = { {0, 0}, {0, 1},
	{0, 2}, {0, 3}
	};

	/* Some boards advertise MSI but don't really support it */
	if ((board_id == 0x40700E11) ||
	    (board_id == 0x40800E11) ||
	    (board_id == 0x40820E11) || (board_id == 0x40830E11))
		goto default_int_mode;
	if (pci_find_capability(pdev, PCI_CAP_ID_MSIX)) {
		dev_info(&pdev->dev, "MSIX\n");
		err = pci_enable_msix(pdev, hpsa_msix_entries, 4);
		if (!err) {
			h->intr[0] = hpsa_msix_entries[0].vector;
			h->intr[1] = hpsa_msix_entries[1].vector;
			h->intr[2] = hpsa_msix_entries[2].vector;
			h->intr[3] = hpsa_msix_entries[3].vector;
			h->msix_vector = 1;
			return;
		}
		if (err > 0) {
			dev_warn(&pdev->dev, "only %d MSI-X vectors "
			       "available\n", err);
			goto default_int_mode;
		} else {
			dev_warn(&pdev->dev, "MSI-X init failed %d\n",
			       err);
			goto default_int_mode;
		}
	}
	if (pci_find_capability(pdev, PCI_CAP_ID_MSI)) {
		dev_info(&pdev->dev, "MSI\n");
		if (!pci_enable_msi(pdev))
			h->msi_vector = 1;
		else
			dev_warn(&pdev->dev, "MSI init failed\n");
	}
default_int_mode:
#endif				/* CONFIG_PCI_MSI */
	/* if we get here we're going to use the default interrupt mode */
	h->intr[PERF_MODE_INT] = pdev->irq;
}

static int hpsa_pci_init(struct ctlr_info *h, struct pci_dev *pdev)
{
	ushort subsystem_vendor_id, subsystem_device_id, command;
	u32 board_id, scratchpad = 0;
	u64 cfg_offset;
	u32 cfg_base_addr;
	u64 cfg_base_addr_index;
	u32 trans_offset;
	int i, prod_index, err;

	subsystem_vendor_id = pdev->subsystem_vendor;
	subsystem_device_id = pdev->subsystem_device;
	board_id = (((u32) (subsystem_device_id << 16) & 0xffff0000) |
		    subsystem_vendor_id);

	for (i = 0; i < ARRAY_SIZE(products); i++)
		if (board_id == products[i].board_id)
			break;

	prod_index = i;

	if (prod_index == ARRAY_SIZE(products)) {
		prod_index--;
		if (subsystem_vendor_id != PCI_VENDOR_ID_HP ||
				!hpsa_allow_any) {
			dev_warn(&pdev->dev, "unrecognized board ID:"
				" 0x%08lx, ignoring.\n",
				(unsigned long) board_id);
			return -ENODEV;
		}
	}
	/* check to see if controller has been disabled
	 * BEFORE trying to enable it
	 */
	(void)pci_read_config_word(pdev, PCI_COMMAND, &command);
	if (!(command & 0x02)) {
		dev_warn(&pdev->dev, "controller appears to be disabled\n");
		return -ENODEV;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_warn(&pdev->dev, "unable to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, "hpsa");
	if (err) {
		dev_err(&pdev->dev, "cannot obtain PCI resources, aborting\n");
		return err;
	}

	/* If the kernel supports MSI/MSI-X we will try to enable that,
	 * else we use the IO-APIC interrupt assigned to us by system ROM.
	 */
	hpsa_interrupt_mode(h, pdev, board_id);

	/* find the memory BAR */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_MEM)
			break;
	}
	if (i == DEVICE_COUNT_RESOURCE) {
		dev_warn(&pdev->dev, "no memory BAR found\n");
		err = -ENODEV;
		goto err_out_free_res;
	}

	h->paddr = pci_resource_start(pdev, i); /* addressing mode bits
						 * already removed
						 */

	h->vaddr = remap_pci_mem(h->paddr, 0x250);

	/* Wait for the board to become ready.  */
	for (i = 0; i < HPSA_BOARD_READY_ITERATIONS; i++) {
		scratchpad = readl(h->vaddr + SA5_SCRATCHPAD_OFFSET);
		if (scratchpad == HPSA_FIRMWARE_READY)
			break;
		msleep(HPSA_BOARD_READY_POLL_INTERVAL_MSECS);
	}
	if (scratchpad != HPSA_FIRMWARE_READY) {
		dev_warn(&pdev->dev, "board not ready, timed out.\n");
		err = -ENODEV;
		goto err_out_free_res;
	}

	/* get the address index number */
	cfg_base_addr = readl(h->vaddr + SA5_CTCFG_OFFSET);
	cfg_base_addr &= (u32) 0x0000ffff;
	cfg_base_addr_index = find_PCI_BAR_index(pdev, cfg_base_addr);
	if (cfg_base_addr_index == -1) {
		dev_warn(&pdev->dev, "cannot find cfg_base_addr_index\n");
		err = -ENODEV;
		goto err_out_free_res;
	}

	cfg_offset = readl(h->vaddr + SA5_CTMEM_OFFSET);
	h->cfgtable = remap_pci_mem(pci_resource_start(pdev,
			       cfg_base_addr_index) + cfg_offset,
				sizeof(h->cfgtable));
	/* Find performant mode table. */
	trans_offset = readl(&(h->cfgtable->TransMethodOffset));
	h->transtable = remap_pci_mem(pci_resource_start(pdev,
				cfg_base_addr_index)+cfg_offset+trans_offset,
				sizeof(*h->transtable));

	h->board_id = board_id;
	h->max_commands = readl(&(h->cfgtable->MaxPerformantModeCommands));
	h->product_name = products[prod_index].product_name;
	h->access = *(products[prod_index].access);
	/* Allow room for some ioctls */
	h->nr_cmds = h->max_commands - 4;

	if ((readb(&h->cfgtable->Signature[0]) != 'C') ||
	    (readb(&h->cfgtable->Signature[1]) != 'I') ||
	    (readb(&h->cfgtable->Signature[2]) != 'S') ||
	    (readb(&h->cfgtable->Signature[3]) != 'S')) {
		dev_warn(&pdev->dev, "not a valid CISS config table\n");
		err = -ENODEV;
		goto err_out_free_res;
	}
#ifdef CONFIG_X86
	{
		/* Need to enable prefetch in the SCSI core for 6400 in x86 */
		u32 prefetch;
		prefetch = readl(&(h->cfgtable->SCSI_Prefetch));
		prefetch |= 0x100;
		writel(prefetch, &(h->cfgtable->SCSI_Prefetch));
	}
#endif

	/* Disabling DMA prefetch for the P600
	 * An ASIC bug may result in a prefetch beyond
	 * physical memory.
	 */
	if (board_id == 0x3225103C) {
		u32 dma_prefetch;
		dma_prefetch = readl(h->vaddr + I2O_DMA1_CFG);
		dma_prefetch |= 0x8000;
		writel(dma_prefetch, h->vaddr + I2O_DMA1_CFG);
	}

	h->max_commands = readl(&(h->cfgtable->CmdsOutMax));
	/* Update the field, and then ring the doorbell */
	writel(CFGTBL_Trans_Simple, &(h->cfgtable->HostWrite.TransportRequest));
	writel(CFGTBL_ChangeReq, h->vaddr + SA5_DOORBELL);

	/* under certain very rare conditions, this can take awhile.
	 * (e.g.: hot replace a failed 144GB drive in a RAID 5 set right
	 * as we enter this code.)
	 */
	for (i = 0; i < MAX_CONFIG_WAIT; i++) {
		if (!(readl(h->vaddr + SA5_DOORBELL) & CFGTBL_ChangeReq))
			break;
		/* delay and try again */
		msleep(10);
	}

#ifdef HPSA_DEBUG
	print_cfg_table(&pdev->dev, h->cfgtable);
#endif				/* HPSA_DEBUG */

	if (!(readl(&(h->cfgtable->TransportActive)) & CFGTBL_Trans_Simple)) {
		dev_warn(&pdev->dev, "unable to get board into simple mode\n");
		err = -ENODEV;
		goto err_out_free_res;
	}
	return 0;

err_out_free_res:
	/*
	 * Deliberately omit pci_disable_device(): it does something nasty to
	 * Smart Array controllers that pci_enable_device does not undo
	 */
	pci_release_regions(pdev);
	return err;
}

static void __devinit hpsa_hba_inquiry(struct ctlr_info *h)
{
	int rc;

#define HBA_INQUIRY_BYTE_COUNT 64
	h->hba_inquiry_data = kmalloc(HBA_INQUIRY_BYTE_COUNT, GFP_KERNEL);
	if (!h->hba_inquiry_data)
		return;
	rc = hpsa_scsi_do_inquiry(h, RAID_CTLR_LUNID, 0,
		h->hba_inquiry_data, HBA_INQUIRY_BYTE_COUNT);
	if (rc != 0) {
		kfree(h->hba_inquiry_data);
		h->hba_inquiry_data = NULL;
	}
}

static int __devinit hpsa_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	int i, rc;
	int dac;
	struct ctlr_info *h;

	if (number_of_controllers == 0)
		printk(KERN_INFO DRIVER_NAME "\n");
	if (reset_devices) {
		/* Reset the controller with a PCI power-cycle */
		if (hpsa_hard_reset_controller(pdev) || hpsa_reset_msi(pdev))
			return -ENODEV;

		/* Some devices (notably the HP Smart Array 5i Controller)
		   need a little pause here */
		msleep(HPSA_POST_RESET_PAUSE_MSECS);

		/* Now try to get the controller to respond to a no-op */
		for (i = 0; i < HPSA_POST_RESET_NOOP_RETRIES; i++) {
			if (hpsa_noop(pdev) == 0)
				break;
			else
				dev_warn(&pdev->dev, "no-op failed%s\n",
						(i < 11 ? "; re-trying" : ""));
		}
	}

	/* Command structures must be aligned on a 32-byte boundary because
	 * the 5 lower bits of the address are used by the hardware. and by
	 * the driver.  See comments in hpsa.h for more info.
	 */
#define COMMANDLIST_ALIGNMENT 32
	BUILD_BUG_ON(sizeof(struct CommandList) % COMMANDLIST_ALIGNMENT);
	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	h->busy_initializing = 1;
	INIT_HLIST_HEAD(&h->cmpQ);
	INIT_HLIST_HEAD(&h->reqQ);
	rc = hpsa_pci_init(h, pdev);
	if (rc != 0)
		goto clean1;

	sprintf(h->devname, "hpsa%d", number_of_controllers);
	h->ctlr = number_of_controllers;
	number_of_controllers++;
	h->pdev = pdev;

	/* configure PCI DMA stuff */
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc == 0) {
		dac = 1;
	} else {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc == 0) {
			dac = 0;
		} else {
			dev_err(&pdev->dev, "no suitable DMA available\n");
			goto clean1;
		}
	}

	/* make sure the board interrupts are off */
	h->access.set_intr_mask(h, HPSA_INTR_OFF);
	rc = request_irq(h->intr[PERF_MODE_INT], do_hpsa_intr,
			IRQF_DISABLED, h->devname, h);
	if (rc) {
		dev_err(&pdev->dev, "unable to get irq %d for %s\n",
		       h->intr[PERF_MODE_INT], h->devname);
		goto clean2;
	}

	dev_info(&pdev->dev, "%s: <0x%x> at IRQ %d%s using DAC\n",
	       h->devname, pdev->device,
	       h->intr[PERF_MODE_INT], dac ? "" : " not");

	h->cmd_pool_bits =
	    kmalloc(((h->nr_cmds + BITS_PER_LONG -
		      1) / BITS_PER_LONG) * sizeof(unsigned long), GFP_KERNEL);
	h->cmd_pool = pci_alloc_consistent(h->pdev,
		    h->nr_cmds * sizeof(*h->cmd_pool),
		    &(h->cmd_pool_dhandle));
	h->errinfo_pool = pci_alloc_consistent(h->pdev,
		    h->nr_cmds * sizeof(*h->errinfo_pool),
		    &(h->errinfo_pool_dhandle));
	if ((h->cmd_pool_bits == NULL)
	    || (h->cmd_pool == NULL)
	    || (h->errinfo_pool == NULL)) {
		dev_err(&pdev->dev, "out of memory");
		rc = -ENOMEM;
		goto clean4;
	}
	spin_lock_init(&h->lock);
	spin_lock_init(&h->scan_lock);
	init_waitqueue_head(&h->scan_wait_queue);
	h->scan_finished = 1; /* no scan currently in progress */

	pci_set_drvdata(pdev, h);
	memset(h->cmd_pool_bits, 0,
	       ((h->nr_cmds + BITS_PER_LONG -
		 1) / BITS_PER_LONG) * sizeof(unsigned long));

	hpsa_scsi_setup(h);

	/* Turn the interrupts on so we can service requests */
	h->access.set_intr_mask(h, HPSA_INTR_ON);

	hpsa_put_ctlr_into_performant_mode(h);
	hpsa_hba_inquiry(h);
	hpsa_register_scsi(h);	/* hook ourselves into SCSI subsystem */
	h->busy_initializing = 0;
	return 1;

clean4:
	kfree(h->cmd_pool_bits);
	if (h->cmd_pool)
		pci_free_consistent(h->pdev,
			    h->nr_cmds * sizeof(struct CommandList),
			    h->cmd_pool, h->cmd_pool_dhandle);
	if (h->errinfo_pool)
		pci_free_consistent(h->pdev,
			    h->nr_cmds * sizeof(struct ErrorInfo),
			    h->errinfo_pool,
			    h->errinfo_pool_dhandle);
	free_irq(h->intr[PERF_MODE_INT], h);
clean2:
clean1:
	h->busy_initializing = 0;
	kfree(h);
	return rc;
}

static void hpsa_flush_cache(struct ctlr_info *h)
{
	char *flush_buf;
	struct CommandList *c;

	flush_buf = kzalloc(4, GFP_KERNEL);
	if (!flush_buf)
		return;

	c = cmd_special_alloc(h);
	if (!c) {
		dev_warn(&h->pdev->dev, "cmd_special_alloc returned NULL!\n");
		goto out_of_memory;
	}
	fill_cmd(c, HPSA_CACHE_FLUSH, h, flush_buf, 4, 0,
		RAID_CTLR_LUNID, TYPE_CMD);
	hpsa_scsi_do_simple_cmd_with_retry(h, c, PCI_DMA_TODEVICE);
	if (c->err_info->CommandStatus != 0)
		dev_warn(&h->pdev->dev,
			"error flushing cache on controller\n");
	cmd_special_free(h, c);
out_of_memory:
	kfree(flush_buf);
}

static void hpsa_shutdown(struct pci_dev *pdev)
{
	struct ctlr_info *h;

	h = pci_get_drvdata(pdev);
	/* Turn board interrupts off  and send the flush cache command
	 * sendcmd will turn off interrupt, and send the flush...
	 * To write all data in the battery backed cache to disks
	 */
	hpsa_flush_cache(h);
	h->access.set_intr_mask(h, HPSA_INTR_OFF);
	free_irq(h->intr[PERF_MODE_INT], h);
#ifdef CONFIG_PCI_MSI
	if (h->msix_vector)
		pci_disable_msix(h->pdev);
	else if (h->msi_vector)
		pci_disable_msi(h->pdev);
#endif				/* CONFIG_PCI_MSI */
}

static void __devexit hpsa_remove_one(struct pci_dev *pdev)
{
	struct ctlr_info *h;

	if (pci_get_drvdata(pdev) == NULL) {
		dev_err(&pdev->dev, "unable to remove device \n");
		return;
	}
	h = pci_get_drvdata(pdev);
	hpsa_unregister_scsi(h);	/* unhook from SCSI subsystem */
	hpsa_shutdown(pdev);
	iounmap(h->vaddr);
	pci_free_consistent(h->pdev,
		h->nr_cmds * sizeof(struct CommandList),
		h->cmd_pool, h->cmd_pool_dhandle);
	pci_free_consistent(h->pdev,
		h->nr_cmds * sizeof(struct ErrorInfo),
		h->errinfo_pool, h->errinfo_pool_dhandle);
	pci_free_consistent(h->pdev, h->reply_pool_size,
		h->reply_pool, h->reply_pool_dhandle);
	kfree(h->cmd_pool_bits);
	kfree(h->blockFetchTable);
	kfree(h->hba_inquiry_data);
	/*
	 * Deliberately omit pci_disable_device(): it does something nasty to
	 * Smart Array controllers that pci_enable_device does not undo
	 */
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	kfree(h);
}

static int hpsa_suspend(__attribute__((unused)) struct pci_dev *pdev,
	__attribute__((unused)) pm_message_t state)
{
	return -ENOSYS;
}

static int hpsa_resume(__attribute__((unused)) struct pci_dev *pdev)
{
	return -ENOSYS;
}

static struct pci_driver hpsa_pci_driver = {
	.name = "hpsa",
	.probe = hpsa_init_one,
	.remove = __devexit_p(hpsa_remove_one),
	.id_table = hpsa_pci_device_id,	/* id_table */
	.shutdown = hpsa_shutdown,
	.suspend = hpsa_suspend,
	.resume = hpsa_resume,
};

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

static void hpsa_put_ctlr_into_performant_mode(struct ctlr_info *h)
{
	u32 trans_support;
	u64 trans_offset;
	/*  5 = 1 s/g entry or 4k
	 *  6 = 2 s/g entry or 8k
	 *  8 = 4 s/g entry or 16k
	 * 10 = 6 s/g entry or 24k
	 */
	int bft[8] = {5, 6, 8, 10, 12, 20, 28, 35}; /* for scatter/gathers */
	int i = 0;
	int l = 0;
	unsigned long register_value;

	trans_support = readl(&(h->cfgtable->TransportSupport));
	if (!(trans_support & PERFORMANT_MODE))
		return;

	h->max_commands = readl(&(h->cfgtable->MaxPerformantModeCommands));
	h->max_sg_entries = 32;
	/* Performant mode ring buffer and supporting data structures */
	h->reply_pool_size = h->max_commands * sizeof(u64);
	h->reply_pool = pci_alloc_consistent(h->pdev, h->reply_pool_size,
				&(h->reply_pool_dhandle));

	/* Need a block fetch table for performant mode */
	h->blockFetchTable = kmalloc(((h->max_sg_entries+1) *
				sizeof(u32)), GFP_KERNEL);

	if ((h->reply_pool == NULL)
		|| (h->blockFetchTable == NULL))
		goto clean_up;

	h->reply_pool_wraparound = 1; /* spec: init to 1 */

	/* Controller spec: zero out this buffer. */
	memset(h->reply_pool, 0, h->reply_pool_size);
	h->reply_pool_head = h->reply_pool;

	trans_offset = readl(&(h->cfgtable->TransMethodOffset));
	bft[7] = h->max_sg_entries + 4;
	calc_bucket_map(bft, ARRAY_SIZE(bft), 32, h->blockFetchTable);
	for (i = 0; i < 8; i++)
		writel(bft[i], &h->transtable->BlockFetch[i]);

	/* size of controller ring buffer */
	writel(h->max_commands, &h->transtable->RepQSize);
	writel(1, &h->transtable->RepQCount);
	writel(0, &h->transtable->RepQCtrAddrLow32);
	writel(0, &h->transtable->RepQCtrAddrHigh32);
	writel(h->reply_pool_dhandle, &h->transtable->RepQAddr0Low32);
	writel(0, &h->transtable->RepQAddr0High32);
	writel(CFGTBL_Trans_Performant,
		&(h->cfgtable->HostWrite.TransportRequest));
	writel(CFGTBL_ChangeReq, h->vaddr + SA5_DOORBELL);
	/* under certain very rare conditions, this can take awhile.
	 * (e.g.: hot replace a failed 144GB drive in a RAID 5 set right
	 * as we enter this code.) */
	for (l = 0; l < MAX_CONFIG_WAIT; l++) {
		register_value = readl(h->vaddr + SA5_DOORBELL);
		if (!(register_value & CFGTBL_ChangeReq))
			break;
		/* delay and try again */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(10);
	}
	register_value = readl(&(h->cfgtable->TransportActive));
	if (!(register_value & CFGTBL_Trans_Performant)) {
		dev_warn(&h->pdev->dev, "unable to get board into"
					" performant mode\n");
		return;
	}

	/* Change the access methods to the performant access methods */
	h->access = SA5_performant_access;
	h->transMethod = CFGTBL_Trans_Performant;

	return;

clean_up:
	if (h->reply_pool)
		pci_free_consistent(h->pdev, h->reply_pool_size,
			h->reply_pool, h->reply_pool_dhandle);
	kfree(h->blockFetchTable);
}

/*
 *  This is it.  Register the PCI driver information for the cards we control
 *  the OS will call our registered routines when it finds one of our cards.
 */
static int __init hpsa_init(void)
{
	return pci_register_driver(&hpsa_pci_driver);
}

static void __exit hpsa_cleanup(void)
{
	pci_unregister_driver(&hpsa_pci_driver);
}

module_init(hpsa_init);
module_exit(hpsa_cleanup);
