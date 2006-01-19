/*
 *    Disk Array driver for Compaq SMART2 Controllers
 *    Copyright 1998 Compaq Computer Corporation
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
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
#include <linux/config.h>	/* CONFIG_PROC_FS */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/bio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <asm/uaccess.h>
#include <asm/io.h>


#define SMART2_DRIVER_VERSION(maj,min,submin) ((maj<<16)|(min<<8)|(submin))

#define DRIVER_NAME "Compaq SMART2 Driver (v 2.6.0)"
#define DRIVER_VERSION SMART2_DRIVER_VERSION(2,6,0)

/* Embedded module documentation macros - see modules.h */
/* Original author Chris Frantz - Compaq Computer Corporation */
MODULE_AUTHOR("Compaq Computer Corporation");
MODULE_DESCRIPTION("Driver for Compaq Smart2 Array Controllers version 2.6.0");
MODULE_LICENSE("GPL");

#include "cpqarray.h"
#include "ida_cmd.h"
#include "smart1,2.h"
#include "ida_ioctl.h"

#define READ_AHEAD	128
#define NR_CMDS		128 /* This could probably go as high as ~400 */

#define MAX_CTLR	8
#define CTLR_SHIFT	8

#define CPQARRAY_DMA_MASK	0xFFFFFFFF	/* 32 bit DMA */

static int nr_ctlr;
static ctlr_info_t *hba[MAX_CTLR];

static int eisa[8];

#define NR_PRODUCTS ARRAY_SIZE(products)

/*  board_id = Subsystem Device ID & Vendor ID
 *  product = Marketing Name for the board
 *  access = Address of the struct of function pointers
 */
static struct board_type products[] = {
	{ 0x0040110E, "IDA",			&smart1_access },
	{ 0x0140110E, "IDA-2",			&smart1_access },
	{ 0x1040110E, "IAES",			&smart1_access },
	{ 0x2040110E, "SMART",			&smart1_access },
	{ 0x3040110E, "SMART-2/E",		&smart2e_access },
	{ 0x40300E11, "SMART-2/P",		&smart2_access },
	{ 0x40310E11, "SMART-2SL",		&smart2_access },
	{ 0x40320E11, "Smart Array 3200",	&smart2_access },
	{ 0x40330E11, "Smart Array 3100ES",	&smart2_access },
	{ 0x40340E11, "Smart Array 221",	&smart2_access },
	{ 0x40400E11, "Integrated Array",	&smart4_access },
	{ 0x40480E11, "Compaq Raid LC2",        &smart4_access },
	{ 0x40500E11, "Smart Array 4200",	&smart4_access },
	{ 0x40510E11, "Smart Array 4250ES",	&smart4_access },
	{ 0x40580E11, "Smart Array 431",	&smart4_access },
};

/* define the PCI info for the PCI cards this driver can control */
static const struct pci_device_id cpqarray_pci_device_id[] =
{
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_COMPAQ_42XX,
		0x0E11, 0x4058, 0, 0, 0},       /* SA431 */
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_COMPAQ_42XX,
		0x0E11, 0x4051, 0, 0, 0},      /* SA4250ES */
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_COMPAQ_42XX,
		0x0E11, 0x4050, 0, 0, 0},      /* SA4200 */
	{ PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C1510,
		0x0E11, 0x4048, 0, 0, 0},       /* LC2 */
	{ PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C1510,
		0x0E11, 0x4040, 0, 0, 0},      /* Integrated Array */
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_SMART2P,
		0x0E11, 0x4034, 0, 0, 0},       /* SA 221 */
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_SMART2P,
		0x0E11, 0x4033, 0, 0, 0},       /* SA 3100ES*/
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_SMART2P,
		0x0E11, 0x4032, 0, 0, 0},       /* SA 3200*/
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_SMART2P,
		0x0E11, 0x4031, 0, 0, 0},       /* SA 2SL*/
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_SMART2P,
		0x0E11, 0x4030, 0, 0, 0},       /* SA 2P */
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, cpqarray_pci_device_id);

static struct gendisk *ida_gendisk[MAX_CTLR][NWD];

/* Debug... */
#define DBG(s)	do { s } while(0)
/* Debug (general info)... */
#define DBGINFO(s) do { } while(0)
/* Debug Paranoid... */
#define DBGP(s)  do { } while(0)
/* Debug Extra Paranoid... */
#define DBGPX(s) do { } while(0)

static int cpqarray_pci_init(ctlr_info_t *c, struct pci_dev *pdev);
static void __iomem *remap_pci_mem(ulong base, ulong size);
static int cpqarray_eisa_detect(void);
static int pollcomplete(int ctlr);
static void getgeometry(int ctlr);
static void start_fwbk(int ctlr);

static cmdlist_t * cmd_alloc(ctlr_info_t *h, int get_from_pool);
static void cmd_free(ctlr_info_t *h, cmdlist_t *c, int got_from_pool);

static void free_hba(int i);
static int alloc_cpqarray_hba(void);

static int sendcmd(
	__u8	cmd,
	int	ctlr,
	void	*buff,
	size_t	size,
	unsigned int blk,
	unsigned int blkcnt,
	unsigned int log_unit );

static int ida_open(struct inode *inode, struct file *filep);
static int ida_release(struct inode *inode, struct file *filep);
static int ida_ioctl(struct inode *inode, struct file *filep, unsigned int cmd, unsigned long arg);
static int ida_getgeo(struct block_device *bdev, struct hd_geometry *geo);
static int ida_ctlr_ioctl(ctlr_info_t *h, int dsk, ida_ioctl_t *io);

static void do_ida_request(request_queue_t *q);
static void start_io(ctlr_info_t *h);

static inline void addQ(cmdlist_t **Qptr, cmdlist_t *c);
static inline cmdlist_t *removeQ(cmdlist_t **Qptr, cmdlist_t *c);
static inline void complete_buffers(struct bio *bio, int ok);
static inline void complete_command(cmdlist_t *cmd, int timeout);

static irqreturn_t do_ida_intr(int irq, void *dev_id, struct pt_regs * regs);
static void ida_timer(unsigned long tdata);
static int ida_revalidate(struct gendisk *disk);
static int revalidate_allvol(ctlr_info_t *host);
static int cpqarray_register_ctlr(int ctlr, struct pci_dev *pdev);

#ifdef CONFIG_PROC_FS
static void ida_procinit(int i);
static int ida_proc_get_info(char *buffer, char **start, off_t offset, int length, int *eof, void *data);
#else
static void ida_procinit(int i) {}
#endif

static inline drv_info_t *get_drv(struct gendisk *disk)
{
	return disk->private_data;
}

static inline ctlr_info_t *get_host(struct gendisk *disk)
{
	return disk->queue->queuedata;
}


static struct block_device_operations ida_fops  = {
	.owner		= THIS_MODULE,
	.open		= ida_open,
	.release	= ida_release,
	.ioctl		= ida_ioctl,
	.getgeo		= ida_getgeo,
	.revalidate_disk= ida_revalidate,
};


#ifdef CONFIG_PROC_FS

static struct proc_dir_entry *proc_array;

/*
 * Get us a file in /proc/array that says something about each controller.
 * Create /proc/array if it doesn't exist yet.
 */
static void __init ida_procinit(int i)
{
	if (proc_array == NULL) {
		proc_array = proc_mkdir("cpqarray", proc_root_driver);
		if (!proc_array) return;
	}

	create_proc_read_entry(hba[i]->devname, 0, proc_array,
			       ida_proc_get_info, hba[i]);
}

/*
 * Report information about this controller.
 */
static int ida_proc_get_info(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	off_t pos = 0;
	off_t len = 0;
	int size, i, ctlr;
	ctlr_info_t *h = (ctlr_info_t*)data;
	drv_info_t *drv;
#ifdef CPQ_PROC_PRINT_QUEUES
	cmdlist_t *c;
	unsigned long flags;
#endif

	ctlr = h->ctlr;
	size = sprintf(buffer, "%s:  Compaq %s Controller\n"
		"       Board ID: 0x%08lx\n"
		"       Firmware Revision: %c%c%c%c\n"
		"       Controller Sig: 0x%08lx\n"
		"       Memory Address: 0x%08lx\n"
		"       I/O Port: 0x%04x\n"
		"       IRQ: %d\n"
		"       Logical drives: %d\n"
		"       Physical drives: %d\n\n"
		"       Current Q depth: %d\n"
		"       Max Q depth since init: %d\n\n",
		h->devname, 
		h->product_name,
		(unsigned long)h->board_id,
		h->firm_rev[0], h->firm_rev[1], h->firm_rev[2], h->firm_rev[3],
		(unsigned long)h->ctlr_sig, (unsigned long)h->vaddr,
		(unsigned int) h->io_mem_addr, (unsigned int)h->intr,
		h->log_drives, h->phys_drives,
		h->Qdepth, h->maxQsinceinit);

	pos += size; len += size;
	
	size = sprintf(buffer+len, "Logical Drive Info:\n");
	pos += size; len += size;

	for(i=0; i<h->log_drives; i++) {
		drv = &h->drv[i];
		size = sprintf(buffer+len, "ida/c%dd%d: blksz=%d nr_blks=%d\n",
				ctlr, i, drv->blk_size, drv->nr_blks);
		pos += size; len += size;
	}

#ifdef CPQ_PROC_PRINT_QUEUES
	spin_lock_irqsave(IDA_LOCK(h->ctlr), flags); 
	size = sprintf(buffer+len, "\nCurrent Queues:\n");
	pos += size; len += size;

	c = h->reqQ;
	size = sprintf(buffer+len, "reqQ = %p", c); pos += size; len += size;
	if (c) c=c->next;
	while(c && c != h->reqQ) {
		size = sprintf(buffer+len, "->%p", c);
		pos += size; len += size;
		c=c->next;
	}

	c = h->cmpQ;
	size = sprintf(buffer+len, "\ncmpQ = %p", c); pos += size; len += size;
	if (c) c=c->next;
	while(c && c != h->cmpQ) {
		size = sprintf(buffer+len, "->%p", c);
		pos += size; len += size;
		c=c->next;
	}

	size = sprintf(buffer+len, "\n"); pos += size; len += size;
	spin_unlock_irqrestore(IDA_LOCK(h->ctlr), flags); 
#endif
	size = sprintf(buffer+len, "nr_allocs = %d\nnr_frees = %d\n",
			h->nr_allocs, h->nr_frees);
	pos += size; len += size;

	*eof = 1;
	*start = buffer+offset;
	len -= offset;
	if (len>length)
		len = length;
	return len;
}
#endif /* CONFIG_PROC_FS */

module_param_array(eisa, int, NULL, 0);

static void release_io_mem(ctlr_info_t *c)
{
	/* if IO mem was not protected do nothing */
	if( c->io_mem_addr == 0)
		return;
	release_region(c->io_mem_addr, c->io_mem_length);
	c->io_mem_addr = 0;
	c->io_mem_length = 0;
}

static void __devexit cpqarray_remove_one(int i)
{
	int j;
	char buff[4];

	/* sendcmd will turn off interrupt, and send the flush...
	 * To write all data in the battery backed cache to disks
	 * no data returned, but don't want to send NULL to sendcmd */
	if( sendcmd(FLUSH_CACHE, i, buff, 4, 0, 0, 0))
	{
		printk(KERN_WARNING "Unable to flush cache on controller %d\n",
				i);
	}
	free_irq(hba[i]->intr, hba[i]);
	iounmap(hba[i]->vaddr);
	unregister_blkdev(COMPAQ_SMART2_MAJOR+i, hba[i]->devname);
	del_timer(&hba[i]->timer);
	remove_proc_entry(hba[i]->devname, proc_array);
	pci_free_consistent(hba[i]->pci_dev,
			NR_CMDS * sizeof(cmdlist_t), (hba[i]->cmd_pool),
			hba[i]->cmd_pool_dhandle);
	kfree(hba[i]->cmd_pool_bits);
	for(j = 0; j < NWD; j++) {
		if (ida_gendisk[i][j]->flags & GENHD_FL_UP)
			del_gendisk(ida_gendisk[i][j]);
		devfs_remove("ida/c%dd%d",i,j);
		put_disk(ida_gendisk[i][j]);
	}
	blk_cleanup_queue(hba[i]->queue);
	release_io_mem(hba[i]);
	free_hba(i);
}

static void __devexit cpqarray_remove_one_pci (struct pci_dev *pdev)
{
	int i;
	ctlr_info_t *tmp_ptr;

	if (pci_get_drvdata(pdev) == NULL) {
		printk( KERN_ERR "cpqarray: Unable to remove device \n");
		return;
	}

	tmp_ptr = pci_get_drvdata(pdev);
	i = tmp_ptr->ctlr;
	if (hba[i] == NULL) {
		printk(KERN_ERR "cpqarray: controller %d appears to have"
			"already been removed \n", i);
		return;
        }
	pci_set_drvdata(pdev, NULL);

	cpqarray_remove_one(i);
}

/* removing an instance that was not removed automatically..
 * must be an eisa card.
 */
static void __devexit cpqarray_remove_one_eisa (int i)
{
	if (hba[i] == NULL) {
		printk(KERN_ERR "cpqarray: controller %d appears to have"
			"already been removed \n", i);
		return;
        }
	cpqarray_remove_one(i);
}

/* pdev is NULL for eisa */
static int cpqarray_register_ctlr( int i, struct pci_dev *pdev)
{
	request_queue_t *q;
	int j;

	/* 
	 * register block devices
	 * Find disks and fill in structs
	 * Get an interrupt, set the Q depth and get into /proc
	 */

	/* If this successful it should insure that we are the only */
	/* instance of the driver */
	if (register_blkdev(COMPAQ_SMART2_MAJOR+i, hba[i]->devname)) {
		goto Enomem4;
	}
	hba[i]->access.set_intr_mask(hba[i], 0);
	if (request_irq(hba[i]->intr, do_ida_intr,
		SA_INTERRUPT|SA_SHIRQ|SA_SAMPLE_RANDOM,
		hba[i]->devname, hba[i]))
	{
		printk(KERN_ERR "cpqarray: Unable to get irq %d for %s\n",
				hba[i]->intr, hba[i]->devname);
		goto Enomem3;
	}
		
	for (j=0; j<NWD; j++) {
		ida_gendisk[i][j] = alloc_disk(1 << NWD_SHIFT);
		if (!ida_gendisk[i][j])
			goto Enomem2;
	}

	hba[i]->cmd_pool = (cmdlist_t *)pci_alloc_consistent(
		hba[i]->pci_dev, NR_CMDS * sizeof(cmdlist_t),
		&(hba[i]->cmd_pool_dhandle));
	hba[i]->cmd_pool_bits = kmalloc(
		((NR_CMDS+BITS_PER_LONG-1)/BITS_PER_LONG)*sizeof(unsigned long),
		GFP_KERNEL);

	if (!hba[i]->cmd_pool_bits || !hba[i]->cmd_pool)
			goto Enomem1;

	memset(hba[i]->cmd_pool, 0, NR_CMDS * sizeof(cmdlist_t));
	memset(hba[i]->cmd_pool_bits, 0, ((NR_CMDS+BITS_PER_LONG-1)/BITS_PER_LONG)*sizeof(unsigned long));
	printk(KERN_INFO "cpqarray: Finding drives on %s",
		hba[i]->devname);

	spin_lock_init(&hba[i]->lock);
	q = blk_init_queue(do_ida_request, &hba[i]->lock);
	if (!q)
		goto Enomem1;

	hba[i]->queue = q;
	q->queuedata = hba[i];

	getgeometry(i);
	start_fwbk(i);

	ida_procinit(i);

	if (pdev)
		blk_queue_bounce_limit(q, hba[i]->pci_dev->dma_mask);

	/* This is a hardware imposed limit. */
	blk_queue_max_hw_segments(q, SG_MAX);

	/* This is a driver limit and could be eliminated. */
	blk_queue_max_phys_segments(q, SG_MAX);
	
	init_timer(&hba[i]->timer);
	hba[i]->timer.expires = jiffies + IDA_TIMER;
	hba[i]->timer.data = (unsigned long)hba[i];
	hba[i]->timer.function = ida_timer;
	add_timer(&hba[i]->timer);

	/* Enable IRQ now that spinlock and rate limit timer are set up */
	hba[i]->access.set_intr_mask(hba[i], FIFO_NOT_EMPTY);

	for(j=0; j<NWD; j++) {
		struct gendisk *disk = ida_gendisk[i][j];
		drv_info_t *drv = &hba[i]->drv[j];
		sprintf(disk->disk_name, "ida/c%dd%d", i, j);
		disk->major = COMPAQ_SMART2_MAJOR + i;
		disk->first_minor = j<<NWD_SHIFT;
		disk->fops = &ida_fops;
		if (j && !drv->nr_blks)
			continue;
		blk_queue_hardsect_size(hba[i]->queue, drv->blk_size);
		set_capacity(disk, drv->nr_blks);
		disk->queue = hba[i]->queue;
		disk->private_data = drv;
		add_disk(disk);
	}

	/* done ! */
	return(i);

Enomem1:
	nr_ctlr = i; 
	kfree(hba[i]->cmd_pool_bits);
	if (hba[i]->cmd_pool)
		pci_free_consistent(hba[i]->pci_dev, NR_CMDS*sizeof(cmdlist_t), 
				    hba[i]->cmd_pool, hba[i]->cmd_pool_dhandle);
Enomem2:
	while (j--) {
		put_disk(ida_gendisk[i][j]);
		ida_gendisk[i][j] = NULL;
	}
	free_irq(hba[i]->intr, hba[i]);
Enomem3:
	unregister_blkdev(COMPAQ_SMART2_MAJOR+i, hba[i]->devname);
Enomem4:
	if (pdev)
		pci_set_drvdata(pdev, NULL);
	release_io_mem(hba[i]);
	free_hba(i);

	printk( KERN_ERR "cpqarray: out of memory");

	return -1;
}

static int __init cpqarray_init_one( struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	int i;

	printk(KERN_DEBUG "cpqarray: Device 0x%x has been found at"
			" bus %d dev %d func %d\n",
			pdev->device, pdev->bus->number, PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn));
	i = alloc_cpqarray_hba();
	if( i < 0 )
		return (-1);
	memset(hba[i], 0, sizeof(ctlr_info_t));
	sprintf(hba[i]->devname, "ida%d", i);
	hba[i]->ctlr = i;
	/* Initialize the pdev driver private data */
	pci_set_drvdata(pdev, hba[i]);

	if (cpqarray_pci_init(hba[i], pdev) != 0) {
		pci_set_drvdata(pdev, NULL);
		release_io_mem(hba[i]);
		free_hba(i);
		return -1;
	}

	return (cpqarray_register_ctlr(i, pdev));
}

static struct pci_driver cpqarray_pci_driver = {
	.name = "cpqarray",
	.probe = cpqarray_init_one,
	.remove = __devexit_p(cpqarray_remove_one_pci),
	.id_table = cpqarray_pci_device_id,
};

/*
 *  This is it.  Find all the controllers and register them.
 *  returns the number of block devices registered.
 */
static int __init cpqarray_init(void)
{
	int num_cntlrs_reg = 0;
	int i;
	int rc = 0;

	/* detect controllers */
	printk(DRIVER_NAME "\n");

	rc = pci_register_driver(&cpqarray_pci_driver);
	if (rc)
		return rc;
	cpqarray_eisa_detect();
	
	for (i=0; i < MAX_CTLR; i++) {
		if (hba[i] != NULL)
			num_cntlrs_reg++;
	}

	return(num_cntlrs_reg);
}

/* Function to find the first free pointer into our hba[] array */
/* Returns -1 if no free entries are left.  */
static int alloc_cpqarray_hba(void)
{
	int i;

	for(i=0; i< MAX_CTLR; i++) {
		if (hba[i] == NULL) {
			hba[i] = kmalloc(sizeof(ctlr_info_t), GFP_KERNEL);
			if(hba[i]==NULL) {
				printk(KERN_ERR "cpqarray: out of memory.\n");
				return (-1);
			}
			return (i);
		}
	}
	printk(KERN_WARNING "cpqarray: This driver supports a maximum"
		" of 8 controllers.\n");
	return(-1);
}

static void free_hba(int i)
{
	kfree(hba[i]);
	hba[i]=NULL;
}

/*
 * Find the IO address of the controller, its IRQ and so forth.  Fill
 * in some basic stuff into the ctlr_info_t structure.
 */
static int cpqarray_pci_init(ctlr_info_t *c, struct pci_dev *pdev)
{
	ushort vendor_id, device_id, command;
	unchar cache_line_size, latency_timer;
	unchar irq, revision;
	unsigned long addr[6];
	__u32 board_id;

	int i;

	c->pci_dev = pdev;
	if (pci_enable_device(pdev)) {
		printk(KERN_ERR "cpqarray: Unable to Enable PCI device\n");
		return -1;
	}
	vendor_id = pdev->vendor;
	device_id = pdev->device;
	irq = pdev->irq;

	for(i=0; i<6; i++)
		addr[i] = pci_resource_start(pdev, i);

	if (pci_set_dma_mask(pdev, CPQARRAY_DMA_MASK) != 0)
	{
		printk(KERN_ERR "cpqarray: Unable to set DMA mask\n");
		return -1;
	}

	pci_read_config_word(pdev, PCI_COMMAND, &command);
	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &revision);
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &cache_line_size);
	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &latency_timer);

	pci_read_config_dword(pdev, 0x2c, &board_id);

	/* check to see if controller has been disabled */
	if(!(command & 0x02)) {
		printk(KERN_WARNING
			"cpqarray: controller appears to be disabled\n");
		return(-1);
	}

DBGINFO(
	printk("vendor_id = %x\n", vendor_id);
	printk("device_id = %x\n", device_id);
	printk("command = %x\n", command);
	for(i=0; i<6; i++)
		printk("addr[%d] = %lx\n", i, addr[i]);
	printk("revision = %x\n", revision);
	printk("irq = %x\n", irq);
	printk("cache_line_size = %x\n", cache_line_size);
	printk("latency_timer = %x\n", latency_timer);
	printk("board_id = %x\n", board_id);
);

	c->intr = irq;

	for(i=0; i<6; i++) {
		if (pci_resource_flags(pdev, i) & PCI_BASE_ADDRESS_SPACE_IO)
		{ /* IO space */
			c->io_mem_addr = addr[i];
			c->io_mem_length = pci_resource_end(pdev, i)
				- pci_resource_start(pdev, i) + 1;
			if(!request_region( c->io_mem_addr, c->io_mem_length,
				"cpqarray"))
			{
				printk( KERN_WARNING "cpqarray I/O memory range already in use addr %lx length = %ld\n", c->io_mem_addr, c->io_mem_length);
				c->io_mem_addr = 0;
				c->io_mem_length = 0;
			}
			break;
		}
	}

	c->paddr = 0;
	for(i=0; i<6; i++)
		if (!(pci_resource_flags(pdev, i) &
				PCI_BASE_ADDRESS_SPACE_IO)) {
			c->paddr = pci_resource_start (pdev, i);
			break;
		}
	if (!c->paddr)
		return -1;
	c->vaddr = remap_pci_mem(c->paddr, 128);
	if (!c->vaddr)
		return -1;
	c->board_id = board_id;

	for(i=0; i<NR_PRODUCTS; i++) {
		if (board_id == products[i].board_id) {
			c->product_name = products[i].product_name;
			c->access = *(products[i].access);
			break;
		}
	}
	if (i == NR_PRODUCTS) {
		printk(KERN_WARNING "cpqarray: Sorry, I don't know how"
			" to access the SMART Array controller %08lx\n", 
				(unsigned long)board_id);
		return -1;
	}

	return 0;
}

/*
 * Map (physical) PCI mem into (virtual) kernel space
 */
static void __iomem *remap_pci_mem(ulong base, ulong size)
{
        ulong page_base        = ((ulong) base) & PAGE_MASK;
        ulong page_offs        = ((ulong) base) - page_base;
        void __iomem *page_remapped    = ioremap(page_base, page_offs+size);

        return (page_remapped ? (page_remapped + page_offs) : NULL);
}

#ifndef MODULE
/*
 * Config string is a comma separated set of i/o addresses of EISA cards.
 */
static int cpqarray_setup(char *str)
{
	int i, ints[9];

	(void)get_options(str, ARRAY_SIZE(ints), ints);

	for(i=0; i<ints[0] && i<8; i++)
		eisa[i] = ints[i+1];
	return 1;
}

__setup("smart2=", cpqarray_setup);

#endif

/*
 * Find an EISA controller's signature.  Set up an hba if we find it.
 */
static int cpqarray_eisa_detect(void)
{
	int i=0, j;
	__u32 board_id;
	int intr;
	int ctlr;
	int num_ctlr = 0;

	while(i<8 && eisa[i]) {
		ctlr = alloc_cpqarray_hba();
		if(ctlr == -1)
			break;
		board_id = inl(eisa[i]+0xC80);
		for(j=0; j < NR_PRODUCTS; j++)
			if (board_id == products[j].board_id) 
				break;

		if (j == NR_PRODUCTS) {
			printk(KERN_WARNING "cpqarray: Sorry, I don't know how"
				" to access the SMART Array controller %08lx\n",				 (unsigned long)board_id);
			continue;
		}

		memset(hba[ctlr], 0, sizeof(ctlr_info_t));
		hba[ctlr]->io_mem_addr = eisa[i];
		hba[ctlr]->io_mem_length = 0x7FF;
		if(!request_region(hba[ctlr]->io_mem_addr,
				hba[ctlr]->io_mem_length,
				"cpqarray"))
		{
			printk(KERN_WARNING "cpqarray: I/O range already in "
					"use addr = %lx length = %ld\n",
					hba[ctlr]->io_mem_addr,
					hba[ctlr]->io_mem_length);
			free_hba(ctlr);
			continue;
		}

		/*
		 * Read the config register to find our interrupt
		 */
		intr = inb(eisa[i]+0xCC0) >> 4;
		if (intr & 1) intr = 11;
		else if (intr & 2) intr = 10;
		else if (intr & 4) intr = 14;
		else if (intr & 8) intr = 15;
		
		hba[ctlr]->intr = intr;
		sprintf(hba[ctlr]->devname, "ida%d", nr_ctlr);
		hba[ctlr]->product_name = products[j].product_name;
		hba[ctlr]->access = *(products[j].access);
		hba[ctlr]->ctlr = ctlr;
		hba[ctlr]->board_id = board_id;
		hba[ctlr]->pci_dev = NULL; /* not PCI */

DBGINFO(
	printk("i = %d, j = %d\n", i, j);
	printk("irq = %x\n", intr);
	printk("product name = %s\n", products[j].product_name);
	printk("board_id = %x\n", board_id);
);

		num_ctlr++;
		i++;

		if (cpqarray_register_ctlr(ctlr, NULL) == -1)
			printk(KERN_WARNING
				"cpqarray: Can't register EISA controller %d\n",
				ctlr);

	}

	return num_ctlr;
}

/*
 * Open.  Make sure the device is really there.
 */
static int ida_open(struct inode *inode, struct file *filep)
{
	drv_info_t *drv = get_drv(inode->i_bdev->bd_disk);
	ctlr_info_t *host = get_host(inode->i_bdev->bd_disk);

	DBGINFO(printk("ida_open %s\n", inode->i_bdev->bd_disk->disk_name));
	/*
	 * Root is allowed to open raw volume zero even if it's not configured
	 * so array config can still work.  I don't think I really like this,
	 * but I'm already using way to many device nodes to claim another one
	 * for "raw controller".
	 */
	if (!drv->nr_blks) {
		if (!capable(CAP_SYS_RAWIO))
			return -ENXIO;
		if (!capable(CAP_SYS_ADMIN) && drv != host->drv)
			return -ENXIO;
	}
	host->usage_count++;
	return 0;
}

/*
 * Close.  Sync first.
 */
static int ida_release(struct inode *inode, struct file *filep)
{
	ctlr_info_t *host = get_host(inode->i_bdev->bd_disk);
	host->usage_count--;
	return 0;
}

/*
 * Enqueuing and dequeuing functions for cmdlists.
 */
static inline void addQ(cmdlist_t **Qptr, cmdlist_t *c)
{
	if (*Qptr == NULL) {
		*Qptr = c;
		c->next = c->prev = c;
	} else {
		c->prev = (*Qptr)->prev;
		c->next = (*Qptr);
		(*Qptr)->prev->next = c;
		(*Qptr)->prev = c;
	}
}

static inline cmdlist_t *removeQ(cmdlist_t **Qptr, cmdlist_t *c)
{
	if (c && c->next != c) {
		if (*Qptr == c) *Qptr = c->next;
		c->prev->next = c->next;
		c->next->prev = c->prev;
	} else {
		*Qptr = NULL;
	}
	return c;
}

/*
 * Get a request and submit it to the controller.
 * This routine needs to grab all the requests it possibly can from the
 * req Q and submit them.  Interrupts are off (and need to be off) when you
 * are in here (either via the dummy do_ida_request functions or by being
 * called from the interrupt handler
 */
static void do_ida_request(request_queue_t *q)
{
	ctlr_info_t *h = q->queuedata;
	cmdlist_t *c;
	struct request *creq;
	struct scatterlist tmp_sg[SG_MAX];
	int i, dir, seg;

	if (blk_queue_plugged(q))
		goto startio;

queue_next:
	creq = elv_next_request(q);
	if (!creq)
		goto startio;

	if (creq->nr_phys_segments > SG_MAX)
		BUG();

	if ((c = cmd_alloc(h,1)) == NULL)
		goto startio;

	blkdev_dequeue_request(creq);

	c->ctlr = h->ctlr;
	c->hdr.unit = (drv_info_t *)(creq->rq_disk->private_data) - h->drv;
	c->hdr.size = sizeof(rblk_t) >> 2;
	c->size += sizeof(rblk_t);

	c->req.hdr.blk = creq->sector;
	c->rq = creq;
DBGPX(
	printk("sector=%d, nr_sectors=%d\n", creq->sector, creq->nr_sectors);
);
	seg = blk_rq_map_sg(q, creq, tmp_sg);

	/* Now do all the DMA Mappings */
	if (rq_data_dir(creq) == READ)
		dir = PCI_DMA_FROMDEVICE;
	else
		dir = PCI_DMA_TODEVICE;
	for( i=0; i < seg; i++)
	{
		c->req.sg[i].size = tmp_sg[i].length;
		c->req.sg[i].addr = (__u32) pci_map_page(h->pci_dev,
						 tmp_sg[i].page,
						 tmp_sg[i].offset,
						 tmp_sg[i].length, dir);
	}
DBGPX(	printk("Submitting %d sectors in %d segments\n", creq->nr_sectors, seg); );
	c->req.hdr.sg_cnt = seg;
	c->req.hdr.blk_cnt = creq->nr_sectors;
	c->req.hdr.cmd = (rq_data_dir(creq) == READ) ? IDA_READ : IDA_WRITE;
	c->type = CMD_RWREQ;

	/* Put the request on the tail of the request queue */
	addQ(&h->reqQ, c);
	h->Qdepth++;
	if (h->Qdepth > h->maxQsinceinit) 
		h->maxQsinceinit = h->Qdepth;

	goto queue_next;

startio:
	start_io(h);
}

/* 
 * start_io submits everything on a controller's request queue
 * and moves it to the completion queue.
 *
 * Interrupts had better be off if you're in here
 */
static void start_io(ctlr_info_t *h)
{
	cmdlist_t *c;

	while((c = h->reqQ) != NULL) {
		/* Can't do anything if we're busy */
		if (h->access.fifo_full(h) == 0)
			return;

		/* Get the first entry from the request Q */
		removeQ(&h->reqQ, c);
		h->Qdepth--;
	
		/* Tell the controller to do our bidding */
		h->access.submit_command(h, c);

		/* Get onto the completion Q */
		addQ(&h->cmpQ, c);
	}
}

static inline void complete_buffers(struct bio *bio, int ok)
{
	struct bio *xbh;
	while(bio) {
		int nr_sectors = bio_sectors(bio);

		xbh = bio->bi_next;
		bio->bi_next = NULL;
		
		blk_finished_io(nr_sectors);
		bio_endio(bio, nr_sectors << 9, ok ? 0 : -EIO);

		bio = xbh;
	}
}
/*
 * Mark all buffers that cmd was responsible for
 */
static inline void complete_command(cmdlist_t *cmd, int timeout)
{
	int ok=1;
	int i, ddir;

	if (cmd->req.hdr.rcode & RCODE_NONFATAL &&
	   (hba[cmd->ctlr]->misc_tflags & MISC_NONFATAL_WARN) == 0) {
		printk(KERN_NOTICE "Non Fatal error on ida/c%dd%d\n",
				cmd->ctlr, cmd->hdr.unit);
		hba[cmd->ctlr]->misc_tflags |= MISC_NONFATAL_WARN;
	}
	if (cmd->req.hdr.rcode & RCODE_FATAL) {
		printk(KERN_WARNING "Fatal error on ida/c%dd%d\n",
				cmd->ctlr, cmd->hdr.unit);
		ok = 0;
	}
	if (cmd->req.hdr.rcode & RCODE_INVREQ) {
				printk(KERN_WARNING "Invalid request on ida/c%dd%d = (cmd=%x sect=%d cnt=%d sg=%d ret=%x)\n",
				cmd->ctlr, cmd->hdr.unit, cmd->req.hdr.cmd,
				cmd->req.hdr.blk, cmd->req.hdr.blk_cnt,
				cmd->req.hdr.sg_cnt, cmd->req.hdr.rcode);
		ok = 0;	
	}
	if (timeout) ok = 0;
	/* unmap the DMA mapping for all the scatter gather elements */
	if (cmd->req.hdr.cmd == IDA_READ)
		ddir = PCI_DMA_FROMDEVICE;
	else
		ddir = PCI_DMA_TODEVICE;
        for(i=0; i<cmd->req.hdr.sg_cnt; i++)
                pci_unmap_page(hba[cmd->ctlr]->pci_dev, cmd->req.sg[i].addr,
				cmd->req.sg[i].size, ddir);

	complete_buffers(cmd->rq->bio, ok);

        DBGPX(printk("Done with %p\n", cmd->rq););
	end_that_request_last(cmd->rq, ok ? 1 : -EIO);
}

/*
 *  The controller will interrupt us upon completion of commands.
 *  Find the command on the completion queue, remove it, tell the OS and
 *  try to queue up more IO
 */
static irqreturn_t do_ida_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	ctlr_info_t *h = dev_id;
	cmdlist_t *c;
	unsigned long istat;
	unsigned long flags;
	__u32 a,a1;

	istat = h->access.intr_pending(h);
	/* Is this interrupt for us? */
	if (istat == 0)
		return IRQ_NONE;

	/*
	 * If there are completed commands in the completion queue,
	 * we had better do something about it.
	 */
	spin_lock_irqsave(IDA_LOCK(h->ctlr), flags);
	if (istat & FIFO_NOT_EMPTY) {
		while((a = h->access.command_completed(h))) {
			a1 = a; a &= ~3;
			if ((c = h->cmpQ) == NULL)
			{  
				printk(KERN_WARNING "cpqarray: Completion of %08lx ignored\n", (unsigned long)a1);
				continue;	
			} 
			while(c->busaddr != a) {
				c = c->next;
				if (c == h->cmpQ) 
					break;
			}
			/*
			 * If we've found the command, take it off the
			 * completion Q and free it
			 */
			if (c->busaddr == a) {
				removeQ(&h->cmpQ, c);
				/*  Check for invalid command.
                                 *  Controller returns command error,
                                 *  But rcode = 0.
                                 */

				if((a1 & 0x03) && (c->req.hdr.rcode == 0))
                                {
                                	c->req.hdr.rcode = RCODE_INVREQ;
                                }
				if (c->type == CMD_RWREQ) {
					complete_command(c, 0);
					cmd_free(h, c, 1);
				} else if (c->type == CMD_IOCTL_PEND) {
					c->type = CMD_IOCTL_DONE;
				}
				continue;
			}
		}
	}

	/*
	 * See if we can queue up some more IO
	 */
	do_ida_request(h->queue);
	spin_unlock_irqrestore(IDA_LOCK(h->ctlr), flags); 
	return IRQ_HANDLED;
}

/*
 * This timer was for timing out requests that haven't happened after
 * IDA_TIMEOUT.  That wasn't such a good idea.  This timer is used to
 * reset a flags structure so we don't flood the user with
 * "Non-Fatal error" messages.
 */
static void ida_timer(unsigned long tdata)
{
	ctlr_info_t *h = (ctlr_info_t*)tdata;

	h->timer.expires = jiffies + IDA_TIMER;
	add_timer(&h->timer);
	h->misc_tflags = 0;
}

static int ida_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	drv_info_t *drv = get_drv(bdev->bd_disk);

	if (drv->cylinders) {
		geo->heads = drv->heads;
		geo->sectors = drv->sectors;
		geo->cylinders = drv->cylinders;
	} else {
		geo->heads = 0xff;
		geo->sectors = 0x3f;
		geo->cylinders = drv->nr_blks / (0xff*0x3f);
	}

	return 0;
}

/*
 *  ida_ioctl does some miscellaneous stuff like reporting drive geometry,
 *  setting readahead and submitting commands from userspace to the controller.
 */
static int ida_ioctl(struct inode *inode, struct file *filep, unsigned int cmd, unsigned long arg)
{
	drv_info_t *drv = get_drv(inode->i_bdev->bd_disk);
	ctlr_info_t *host = get_host(inode->i_bdev->bd_disk);
	int error;
	ida_ioctl_t __user *io = (ida_ioctl_t __user *)arg;
	ida_ioctl_t *my_io;

	switch(cmd) {
	case IDAGETDRVINFO:
		if (copy_to_user(&io->c.drv, drv, sizeof(drv_info_t)))
			return -EFAULT;
		return 0;
	case IDAPASSTHRU:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		my_io = kmalloc(sizeof(ida_ioctl_t), GFP_KERNEL);
		if (!my_io)
			return -ENOMEM;
		error = -EFAULT;
		if (copy_from_user(my_io, io, sizeof(*my_io)))
			goto out_passthru;
		error = ida_ctlr_ioctl(host, drv - host->drv, my_io);
		if (error)
			goto out_passthru;
		error = -EFAULT;
		if (copy_to_user(io, my_io, sizeof(*my_io)))
			goto out_passthru;
		error = 0;
out_passthru:
		kfree(my_io);
		return error;
	case IDAGETCTLRSIG:
		if (!arg) return -EINVAL;
		put_user(host->ctlr_sig, (int __user *)arg);
		return 0;
	case IDAREVALIDATEVOLS:
		if (iminor(inode) != 0)
			return -ENXIO;
		return revalidate_allvol(host);
	case IDADRIVERVERSION:
		if (!arg) return -EINVAL;
		put_user(DRIVER_VERSION, (unsigned long __user *)arg);
		return 0;
	case IDAGETPCIINFO:
	{
		
		ida_pci_info_struct pciinfo;

		if (!arg) return -EINVAL;
		pciinfo.bus = host->pci_dev->bus->number;
		pciinfo.dev_fn = host->pci_dev->devfn;
		pciinfo.board_id = host->board_id;
		if(copy_to_user((void __user *) arg, &pciinfo,  
			sizeof( ida_pci_info_struct)))
				return -EFAULT;
		return(0);
	}	

	default:
		return -EINVAL;
	}
		
}
/*
 * ida_ctlr_ioctl is for passing commands to the controller from userspace.
 * The command block (io) has already been copied to kernel space for us,
 * however, any elements in the sglist need to be copied to kernel space
 * or copied back to userspace.
 *
 * Only root may perform a controller passthru command, however I'm not doing
 * any serious sanity checking on the arguments.  Doing an IDA_WRITE_MEDIA and
 * putting a 64M buffer in the sglist is probably a *bad* idea.
 */
static int ida_ctlr_ioctl(ctlr_info_t *h, int dsk, ida_ioctl_t *io)
{
	int ctlr = h->ctlr;
	cmdlist_t *c;
	void *p = NULL;
	unsigned long flags;
	int error;

	if ((c = cmd_alloc(h, 0)) == NULL)
		return -ENOMEM;
	c->ctlr = ctlr;
	c->hdr.unit = (io->unit & UNITVALID) ? (io->unit & ~UNITVALID) : dsk;
	c->hdr.size = sizeof(rblk_t) >> 2;
	c->size += sizeof(rblk_t);

	c->req.hdr.cmd = io->cmd;
	c->req.hdr.blk = io->blk;
	c->req.hdr.blk_cnt = io->blk_cnt;
	c->type = CMD_IOCTL_PEND;

	/* Pre submit processing */
	switch(io->cmd) {
	case PASSTHRU_A:
		p = kmalloc(io->sg[0].size, GFP_KERNEL);
		if (!p) 
		{ 
			error = -ENOMEM; 
			cmd_free(h, c, 0); 
			return(error);
		}
		if (copy_from_user(p, io->sg[0].addr, io->sg[0].size)) {
			kfree(p);
			cmd_free(h, c, 0); 
			return -EFAULT;
		}
		c->req.hdr.blk = pci_map_single(h->pci_dev, &(io->c), 
				sizeof(ida_ioctl_t), 
				PCI_DMA_BIDIRECTIONAL);
		c->req.sg[0].size = io->sg[0].size;
		c->req.sg[0].addr = pci_map_single(h->pci_dev, p, 
			c->req.sg[0].size, PCI_DMA_BIDIRECTIONAL);
		c->req.hdr.sg_cnt = 1;
		break;
	case IDA_READ:
	case READ_FLASH_ROM:
	case SENSE_CONTROLLER_PERFORMANCE:
		p = kmalloc(io->sg[0].size, GFP_KERNEL);
		if (!p) 
		{ 
                        error = -ENOMEM; 
                        cmd_free(h, c, 0);
                        return(error);
                }

		c->req.sg[0].size = io->sg[0].size;
		c->req.sg[0].addr = pci_map_single(h->pci_dev, p, 
			c->req.sg[0].size, PCI_DMA_BIDIRECTIONAL); 
		c->req.hdr.sg_cnt = 1;
		break;
	case IDA_WRITE:
	case IDA_WRITE_MEDIA:
	case DIAG_PASS_THRU:
	case COLLECT_BUFFER:
	case WRITE_FLASH_ROM:
		p = kmalloc(io->sg[0].size, GFP_KERNEL);
		if (!p) 
 		{ 
                        error = -ENOMEM; 
                        cmd_free(h, c, 0);
                        return(error);
                }
		if (copy_from_user(p, io->sg[0].addr, io->sg[0].size)) {
			kfree(p);
                        cmd_free(h, c, 0);
			return -EFAULT;
		}
		c->req.sg[0].size = io->sg[0].size;
		c->req.sg[0].addr = pci_map_single(h->pci_dev, p, 
			c->req.sg[0].size, PCI_DMA_BIDIRECTIONAL); 
		c->req.hdr.sg_cnt = 1;
		break;
	default:
		c->req.sg[0].size = sizeof(io->c);
		c->req.sg[0].addr = pci_map_single(h->pci_dev,&io->c, 
			c->req.sg[0].size, PCI_DMA_BIDIRECTIONAL);
		c->req.hdr.sg_cnt = 1;
	}
	
	/* Put the request on the tail of the request queue */
	spin_lock_irqsave(IDA_LOCK(ctlr), flags);
	addQ(&h->reqQ, c);
	h->Qdepth++;
	start_io(h);
	spin_unlock_irqrestore(IDA_LOCK(ctlr), flags);

	/* Wait for completion */
	while(c->type != CMD_IOCTL_DONE)
		schedule();

	/* Unmap the DMA  */
	pci_unmap_single(h->pci_dev, c->req.sg[0].addr, c->req.sg[0].size, 
		PCI_DMA_BIDIRECTIONAL);
	/* Post submit processing */
	switch(io->cmd) {
	case PASSTHRU_A:
		pci_unmap_single(h->pci_dev, c->req.hdr.blk,
                                sizeof(ida_ioctl_t),
                                PCI_DMA_BIDIRECTIONAL);
	case IDA_READ:
	case DIAG_PASS_THRU:
	case SENSE_CONTROLLER_PERFORMANCE:
	case READ_FLASH_ROM:
		if (copy_to_user(io->sg[0].addr, p, io->sg[0].size)) {
			kfree(p);
			return -EFAULT;
		}
		/* fall through and free p */
	case IDA_WRITE:
	case IDA_WRITE_MEDIA:
	case COLLECT_BUFFER:
	case WRITE_FLASH_ROM:
		kfree(p);
		break;
	default:;
		/* Nothing to do */
	}

	io->rcode = c->req.hdr.rcode;
	cmd_free(h, c, 0);
	return(0);
}

/*
 * Commands are pre-allocated in a large block.  Here we use a simple bitmap
 * scheme to suballocte them to the driver.  Operations that are not time
 * critical (and can wait for kmalloc and possibly sleep) can pass in NULL
 * as the first argument to get a new command.
 */
static cmdlist_t * cmd_alloc(ctlr_info_t *h, int get_from_pool)
{
	cmdlist_t * c;
	int i;
	dma_addr_t cmd_dhandle;

	if (!get_from_pool) {
		c = (cmdlist_t*)pci_alloc_consistent(h->pci_dev, 
			sizeof(cmdlist_t), &cmd_dhandle);
		if(c==NULL)
			return NULL;
	} else {
		do {
			i = find_first_zero_bit(h->cmd_pool_bits, NR_CMDS);
			if (i == NR_CMDS)
				return NULL;
		} while(test_and_set_bit(i&(BITS_PER_LONG-1), h->cmd_pool_bits+(i/BITS_PER_LONG)) != 0);
		c = h->cmd_pool + i;
		cmd_dhandle = h->cmd_pool_dhandle + i*sizeof(cmdlist_t);
		h->nr_allocs++;
	}

	memset(c, 0, sizeof(cmdlist_t));
	c->busaddr = cmd_dhandle; 
	return c;
}

static void cmd_free(ctlr_info_t *h, cmdlist_t *c, int got_from_pool)
{
	int i;

	if (!got_from_pool) {
		pci_free_consistent(h->pci_dev, sizeof(cmdlist_t), c,
			c->busaddr);
	} else {
		i = c - h->cmd_pool;
		clear_bit(i&(BITS_PER_LONG-1), h->cmd_pool_bits+(i/BITS_PER_LONG));
		h->nr_frees++;
	}
}

/***********************************************************************
    name:        sendcmd
    Send a command to an IDA using the memory mapped FIFO interface
    and wait for it to complete.  
    This routine should only be called at init time.
***********************************************************************/
static int sendcmd(
	__u8	cmd,
	int	ctlr,
	void	*buff,
	size_t	size,
	unsigned int blk,
	unsigned int blkcnt,
	unsigned int log_unit )
{
	cmdlist_t *c;
	int complete;
	unsigned long temp;
	unsigned long i;
	ctlr_info_t *info_p = hba[ctlr];

	c = cmd_alloc(info_p, 1);
	if(!c)
		return IO_ERROR;
	c->ctlr = ctlr;
	c->hdr.unit = log_unit;
	c->hdr.prio = 0;
	c->hdr.size = sizeof(rblk_t) >> 2;
	c->size += sizeof(rblk_t);

	/* The request information. */
	c->req.hdr.next = 0;
	c->req.hdr.rcode = 0;
	c->req.bp = 0;
	c->req.hdr.sg_cnt = 1;
	c->req.hdr.reserved = 0;
	
	if (size == 0)
		c->req.sg[0].size = 512;
	else
		c->req.sg[0].size = size;

	c->req.hdr.blk = blk;
	c->req.hdr.blk_cnt = blkcnt;
	c->req.hdr.cmd = (unsigned char) cmd;
	c->req.sg[0].addr = (__u32) pci_map_single(info_p->pci_dev, 
		buff, c->req.sg[0].size, PCI_DMA_BIDIRECTIONAL);
	/*
	 * Disable interrupt
	 */
	info_p->access.set_intr_mask(info_p, 0);
	/* Make sure there is room in the command FIFO */
	/* Actually it should be completely empty at this time. */
	for (i = 200000; i > 0; i--) {
		temp = info_p->access.fifo_full(info_p);
		if (temp != 0) {
			break;
		}
		udelay(10);
DBG(
		printk(KERN_WARNING "cpqarray ida%d: idaSendPciCmd FIFO full,"
			" waiting!\n", ctlr);
);
	} 
	/*
	 * Send the cmd
	 */
	info_p->access.submit_command(info_p, c);
	complete = pollcomplete(ctlr);
	
	pci_unmap_single(info_p->pci_dev, (dma_addr_t) c->req.sg[0].addr, 
		c->req.sg[0].size, PCI_DMA_BIDIRECTIONAL);
	if (complete != 1) {
		if (complete != c->busaddr) {
			printk( KERN_WARNING
			"cpqarray ida%d: idaSendPciCmd "
		      "Invalid command list address returned! (%08lx)\n",
				ctlr, (unsigned long)complete);
			cmd_free(info_p, c, 1);
			return (IO_ERROR);
		}
	} else {
		printk( KERN_WARNING
			"cpqarray ida%d: idaSendPciCmd Timeout out, "
			"No command list address returned!\n",
			ctlr);
		cmd_free(info_p, c, 1);
		return (IO_ERROR);
	}

	if (c->req.hdr.rcode & 0x00FE) {
		if (!(c->req.hdr.rcode & BIG_PROBLEM)) {
			printk( KERN_WARNING
			"cpqarray ida%d: idaSendPciCmd, error: "
				"Controller failed at init time "
				"cmd: 0x%x, return code = 0x%x\n",
				ctlr, c->req.hdr.cmd, c->req.hdr.rcode);

			cmd_free(info_p, c, 1);
			return (IO_ERROR);
		}
	}
	cmd_free(info_p, c, 1);
	return (IO_OK);
}

/*
 * revalidate_allvol is for online array config utilities.  After a
 * utility reconfigures the drives in the array, it can use this function
 * (through an ioctl) to make the driver zap any previous disk structs for
 * that controller and get new ones.
 *
 * Right now I'm using the getgeometry() function to do this, but this
 * function should probably be finer grained and allow you to revalidate one
 * particualar logical volume (instead of all of them on a particular
 * controller).
 */
static int revalidate_allvol(ctlr_info_t *host)
{
	int ctlr = host->ctlr;
	int i;
	unsigned long flags;

	spin_lock_irqsave(IDA_LOCK(ctlr), flags);
	if (host->usage_count > 1) {
		spin_unlock_irqrestore(IDA_LOCK(ctlr), flags);
		printk(KERN_WARNING "cpqarray: Device busy for volume"
			" revalidation (usage=%d)\n", host->usage_count);
		return -EBUSY;
	}
	host->usage_count++;
	spin_unlock_irqrestore(IDA_LOCK(ctlr), flags);

	/*
	 * Set the partition and block size structures for all volumes
	 * on this controller to zero.  We will reread all of this data
	 */
	set_capacity(ida_gendisk[ctlr][0], 0);
	for (i = 1; i < NWD; i++) {
		struct gendisk *disk = ida_gendisk[ctlr][i];
		if (disk->flags & GENHD_FL_UP)
			del_gendisk(disk);
	}
	memset(host->drv, 0, sizeof(drv_info_t)*NWD);

	/*
	 * Tell the array controller not to give us any interrupts while
	 * we check the new geometry.  Then turn interrupts back on when
	 * we're done.
	 */
	host->access.set_intr_mask(host, 0);
	getgeometry(ctlr);
	host->access.set_intr_mask(host, FIFO_NOT_EMPTY);

	for(i=0; i<NWD; i++) {
		struct gendisk *disk = ida_gendisk[ctlr][i];
		drv_info_t *drv = &host->drv[i];
		if (i && !drv->nr_blks)
			continue;
		blk_queue_hardsect_size(host->queue, drv->blk_size);
		set_capacity(disk, drv->nr_blks);
		disk->queue = host->queue;
		disk->private_data = drv;
		if (i)
			add_disk(disk);
	}

	host->usage_count--;
	return 0;
}

static int ida_revalidate(struct gendisk *disk)
{
	drv_info_t *drv = disk->private_data;
	set_capacity(disk, drv->nr_blks);
	return 0;
}

/********************************************************************
    name: pollcomplete
    Wait polling for a command to complete.
    The memory mapped FIFO is polled for the completion.
    Used only at init time, interrupts disabled.
 ********************************************************************/
static int pollcomplete(int ctlr)
{
	int done;
	int i;

	/* Wait (up to 2 seconds) for a command to complete */

	for (i = 200000; i > 0; i--) {
		done = hba[ctlr]->access.command_completed(hba[ctlr]);
		if (done == 0) {
			udelay(10);	/* a short fixed delay */
		} else
			return (done);
	}
	/* Invalid address to tell caller we ran out of time */
	return 1;
}
/*****************************************************************
    start_fwbk
    Starts controller firmwares background processing. 
    Currently only the Integrated Raid controller needs this done.
    If the PCI mem address registers are written to after this, 
	 data corruption may occur
*****************************************************************/
static void start_fwbk(int ctlr)
{
		id_ctlr_t *id_ctlr_buf; 
	int ret_code;

	if(	(hba[ctlr]->board_id != 0x40400E11)
		&& (hba[ctlr]->board_id != 0x40480E11) )

	/* Not a Integrated Raid, so there is nothing for us to do */
		return;
	printk(KERN_DEBUG "cpqarray: Starting firmware's background"
		" processing\n");
	/* Command does not return anything, but idasend command needs a 
		buffer */
	id_ctlr_buf = (id_ctlr_t *)kmalloc(sizeof(id_ctlr_t), GFP_KERNEL);
	if(id_ctlr_buf==NULL)
	{
		printk(KERN_WARNING "cpqarray: Out of memory. "
			"Unable to start background processing.\n");
		return;
	}		
	ret_code = sendcmd(RESUME_BACKGROUND_ACTIVITY, ctlr, 
		id_ctlr_buf, 0, 0, 0, 0);
	if(ret_code != IO_OK)
		printk(KERN_WARNING "cpqarray: Unable to start"
			" background processing\n");

	kfree(id_ctlr_buf);
}
/*****************************************************************
    getgeometry
    Get ida logical volume geometry from the controller 
    This is a large bit of code which once existed in two flavors,
    It is used only at init time.
*****************************************************************/
static void getgeometry(int ctlr)
{				
	id_log_drv_t *id_ldrive;
	id_ctlr_t *id_ctlr_buf;
	sense_log_drv_stat_t *id_lstatus_buf;
	config_t *sense_config_buf;
	unsigned int log_unit, log_index;
	int ret_code, size;
	drv_info_t *drv;
	ctlr_info_t *info_p = hba[ctlr];
	int i;

	info_p->log_drv_map = 0;	
	
	id_ldrive = (id_log_drv_t *)kmalloc(sizeof(id_log_drv_t), GFP_KERNEL);
	if(id_ldrive == NULL)
	{
		printk( KERN_ERR "cpqarray:  out of memory.\n");
		return;
	}

	id_ctlr_buf = (id_ctlr_t *)kmalloc(sizeof(id_ctlr_t), GFP_KERNEL);
	if(id_ctlr_buf == NULL)
	{
		kfree(id_ldrive);
		printk( KERN_ERR "cpqarray:  out of memory.\n");
		return;
	}

	id_lstatus_buf = (sense_log_drv_stat_t *)kmalloc(sizeof(sense_log_drv_stat_t), GFP_KERNEL);
	if(id_lstatus_buf == NULL)
	{
		kfree(id_ctlr_buf);
		kfree(id_ldrive);
		printk( KERN_ERR "cpqarray:  out of memory.\n");
		return;
	}

	sense_config_buf = (config_t *)kmalloc(sizeof(config_t), GFP_KERNEL);
	if(sense_config_buf == NULL)
	{
		kfree(id_lstatus_buf);
		kfree(id_ctlr_buf);
		kfree(id_ldrive);
		printk( KERN_ERR "cpqarray:  out of memory.\n");
		return;
	}

	memset(id_ldrive, 0, sizeof(id_log_drv_t));
	memset(id_ctlr_buf, 0, sizeof(id_ctlr_t));
	memset(id_lstatus_buf, 0, sizeof(sense_log_drv_stat_t));
	memset(sense_config_buf, 0, sizeof(config_t));

	info_p->phys_drives = 0;
	info_p->log_drv_map = 0;
	info_p->drv_assign_map = 0;
	info_p->drv_spare_map = 0;
	info_p->mp_failed_drv_map = 0;	/* only initialized here */
	/* Get controllers info for this logical drive */
	ret_code = sendcmd(ID_CTLR, ctlr, id_ctlr_buf, 0, 0, 0, 0);
	if (ret_code == IO_ERROR) {
		/*
		 * If can't get controller info, set the logical drive map to 0,
		 * so the idastubopen will fail on all logical drives
		 * on the controller.
		 */
		 /* Free all the buffers and return */ 
		printk(KERN_ERR "cpqarray: error sending ID controller\n");
		kfree(sense_config_buf);
                kfree(id_lstatus_buf);
                kfree(id_ctlr_buf);
                kfree(id_ldrive);
                return;
        }

	info_p->log_drives = id_ctlr_buf->nr_drvs;
	for(i=0;i<4;i++)
		info_p->firm_rev[i] = id_ctlr_buf->firm_rev[i];
	info_p->ctlr_sig = id_ctlr_buf->cfg_sig;

	printk(" (%s)\n", info_p->product_name);
	/*
	 * Initialize logical drive map to zero
	 */
	log_index = 0;
	/*
	 * Get drive geometry for all logical drives
	 */
	if (id_ctlr_buf->nr_drvs > 16)
		printk(KERN_WARNING "cpqarray ida%d:  This driver supports "
			"16 logical drives per controller.\n.  "
			" Additional drives will not be "
			"detected\n", ctlr);

	for (log_unit = 0;
	     (log_index < id_ctlr_buf->nr_drvs)
	     && (log_unit < NWD);
	     log_unit++) {
		struct gendisk *disk = ida_gendisk[ctlr][log_unit];

		size = sizeof(sense_log_drv_stat_t);

		/*
		   Send "Identify logical drive status" cmd
		 */
		ret_code = sendcmd(SENSE_LOG_DRV_STAT,
			     ctlr, id_lstatus_buf, size, 0, 0, log_unit);
		if (ret_code == IO_ERROR) {
			/*
			   If can't get logical drive status, set
			   the logical drive map to 0, so the
			   idastubopen will fail for all logical drives
			   on the controller. 
			 */
			info_p->log_drv_map = 0;	
			printk( KERN_WARNING
			     "cpqarray ida%d: idaGetGeometry - Controller"
				" failed to report status of logical drive %d\n"
			 "Access to this controller has been disabled\n",
				ctlr, log_unit);
			/* Free all the buffers and return */
                	kfree(sense_config_buf);
                	kfree(id_lstatus_buf);
                	kfree(id_ctlr_buf);
                	kfree(id_ldrive);
                	return;
		}
		/*
		   Make sure the logical drive is configured
		 */
		if (id_lstatus_buf->status != LOG_NOT_CONF) {
			ret_code = sendcmd(ID_LOG_DRV, ctlr, id_ldrive,
			       sizeof(id_log_drv_t), 0, 0, log_unit);
			/*
			   If error, the bit for this
			   logical drive won't be set and
			   idastubopen will return error. 
			 */
			if (ret_code != IO_ERROR) {
				drv = &info_p->drv[log_unit];
				drv->blk_size = id_ldrive->blk_size;
				drv->nr_blks = id_ldrive->nr_blks;
				drv->cylinders = id_ldrive->drv.cyl;
				drv->heads = id_ldrive->drv.heads;
				drv->sectors = id_ldrive->drv.sect_per_track;
				info_p->log_drv_map |=	(1 << log_unit);

	printk(KERN_INFO "cpqarray ida/c%dd%d: blksz=%d nr_blks=%d\n",
		ctlr, log_unit, drv->blk_size, drv->nr_blks);
				ret_code = sendcmd(SENSE_CONFIG,
						  ctlr, sense_config_buf,
				 sizeof(config_t), 0, 0, log_unit);
				if (ret_code == IO_ERROR) {
					info_p->log_drv_map = 0;
					/* Free all the buffers and return */
                			printk(KERN_ERR "cpqarray: error sending sense config\n");
                			kfree(sense_config_buf);
                			kfree(id_lstatus_buf);
                			kfree(id_ctlr_buf);
                			kfree(id_ldrive);
                			return;

				}

				sprintf(disk->devfs_name, "ida/c%dd%d", ctlr, log_unit);

				info_p->phys_drives =
				    sense_config_buf->ctlr_phys_drv;
				info_p->drv_assign_map
				    |= sense_config_buf->drv_asgn_map;
				info_p->drv_assign_map
				    |= sense_config_buf->spare_asgn_map;
				info_p->drv_spare_map
				    |= sense_config_buf->spare_asgn_map;
			}	/* end of if no error on id_ldrive */
			log_index = log_index + 1;
		}		/* end of if logical drive configured */
	}			/* end of for log_unit */
	kfree(sense_config_buf);
  	kfree(id_ldrive);
  	kfree(id_lstatus_buf);
	kfree(id_ctlr_buf);
	return;

}

static void __exit cpqarray_exit(void)
{
	int i;

	pci_unregister_driver(&cpqarray_pci_driver);

	/* Double check that all controller entries have been removed */
	for(i=0; i<MAX_CTLR; i++) {
		if (hba[i] != NULL) {
			printk(KERN_WARNING "cpqarray: Removing EISA "
					"controller %d\n", i);
			cpqarray_remove_one_eisa(i);
		}
	}

	devfs_remove("ida");
	remove_proc_entry("cpqarray", proc_root_driver);
}

module_init(cpqarray_init)
module_exit(cpqarray_exit)
