/* $Id: divasmain.c,v 1.55.4.6 2005/02/09 19:28:20 armin Exp $
 *
 * Low level driver for Eicon DIVA Server ISDN cards.
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/kmod.h>

#include "platform.h"
#undef ID_MASK
#undef N_DATA
#include "pc.h"
#include "di_defs.h"
#include "divasync.h"
#include "diva.h"
#include "di.h"
#include "io.h"
#include "xdi_msg.h"
#include "xdi_adapter.h"
#include "xdi_vers.h"
#include "diva_dma.h"
#include "diva_pci.h"

static char *main_revision = "$Revision: 1.55.4.6 $";

static int major;

static int dbgmask;

MODULE_DESCRIPTION("Kernel driver for Eicon DIVA Server cards");
MODULE_AUTHOR("Cytronics & Melware, Eicon Networks");
MODULE_LICENSE("GPL");

module_param(dbgmask, int, 0);
MODULE_PARM_DESC(dbgmask, "initial debug mask");

static char *DRIVERNAME =
    "Eicon DIVA Server driver (http://www.melware.net)";
static char *DRIVERLNAME = "divas";
static char *DEVNAME = "Divas";
char *DRIVERRELEASE_DIVAS = "2.0";

extern irqreturn_t diva_os_irq_wrapper(int irq, void *context,
				struct pt_regs *regs);
extern int create_divas_proc(void);
extern void remove_divas_proc(void);
extern void diva_get_vserial_number(PISDN_ADAPTER IoAdapter, char *buf);
extern int divasfunc_init(int dbgmask);
extern void divasfunc_exit(void);

typedef struct _diva_os_thread_dpc {
	struct tasklet_struct divas_task;
	diva_os_soft_isr_t *psoft_isr;
} diva_os_thread_dpc_t;

/* --------------------------------------------------------------------------
    PCI driver interface section
   -------------------------------------------------------------------------- */
/*
  vendor, device	Vendor and device ID to match (or PCI_ANY_ID)
  subvendor,	Subsystem vendor and device ID to match (or PCI_ANY_ID)
  subdevice
  class,		Device class to match. The class_mask tells which bits
  class_mask	of the class are honored during the comparison.
  driver_data	Data private to the driver.
  */

#if !defined(PCI_DEVICE_ID_EICON_MAESTRAP_2)
#define PCI_DEVICE_ID_EICON_MAESTRAP_2       0xE015
#endif

#if !defined(PCI_DEVICE_ID_EICON_4BRI_VOIP)
#define PCI_DEVICE_ID_EICON_4BRI_VOIP        0xE016
#endif

#if !defined(PCI_DEVICE_ID_EICON_4BRI_2_VOIP)
#define PCI_DEVICE_ID_EICON_4BRI_2_VOIP      0xE017
#endif

#if !defined(PCI_DEVICE_ID_EICON_BRI2M_2)
#define PCI_DEVICE_ID_EICON_BRI2M_2          0xE018
#endif

#if !defined(PCI_DEVICE_ID_EICON_MAESTRAP_2_VOIP)
#define PCI_DEVICE_ID_EICON_MAESTRAP_2_VOIP  0xE019
#endif

#if !defined(PCI_DEVICE_ID_EICON_2F)
#define PCI_DEVICE_ID_EICON_2F               0xE01A
#endif

#if !defined(PCI_DEVICE_ID_EICON_BRI2M_2_VOIP)
#define PCI_DEVICE_ID_EICON_BRI2M_2_VOIP     0xE01B
#endif

/*
  This table should be sorted by PCI device ID
  */
static struct pci_device_id divas_pci_tbl[] = {
/* Diva Server BRI-2M PCI 0xE010 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_MAESTRA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_MAESTRA_PCI},
/* Diva Server 4BRI-8M PCI 0xE012 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_MAESTRAQ,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_Q_8M_PCI},
/* Diva Server 4BRI-8M 2.0 PCI 0xE013 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_MAESTRAQ_U,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_Q_8M_V2_PCI},
/* Diva Server PRI-30M PCI 0xE014 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_MAESTRAP,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_P_30M_PCI},
/* Diva Server PRI 2.0 adapter 0xE015 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_MAESTRAP_2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_P_30M_V2_PCI},
/* Diva Server Voice 4BRI-8M PCI 0xE016 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_4BRI_VOIP,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_VOICE_Q_8M_PCI},
/* Diva Server Voice 4BRI-8M 2.0 PCI 0xE017 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_4BRI_2_VOIP,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI},
/* Diva Server BRI-2M 2.0 PCI 0xE018 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_BRI2M_2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_B_2M_V2_PCI},
/* Diva Server Voice PRI 2.0 PCI 0xE019 */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_MAESTRAP_2_VOIP,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 CARDTYPE_DIVASRV_VOICE_P_30M_V2_PCI},
/* Diva Server 2FX 0xE01A */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_2F,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_B_2F_PCI},
/* Diva Server Voice BRI-2M 2.0 PCI 0xE01B */
	{PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_BRI2M_2_VOIP,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, CARDTYPE_DIVASRV_VOICE_B_2M_V2_PCI},
	{0,}			/* 0 terminated list. */
};
MODULE_DEVICE_TABLE(pci, divas_pci_tbl);

static int divas_init_one(struct pci_dev *pdev,
			  const struct pci_device_id *ent);
static void __devexit divas_remove_one(struct pci_dev *pdev);

static struct pci_driver diva_pci_driver = {
	.name     = "divas",
	.probe    = divas_init_one,
	.remove   = __devexit_p(divas_remove_one),
	.id_table = divas_pci_tbl,
};

/*********************************************************
 ** little helper functions
 *********************************************************/
static char *getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "1.0";
	return rev;
}

void diva_log_info(unsigned char *format, ...)
{
	va_list args;
	unsigned char line[160];

	va_start(args, format);
	vsprintf(line, format, args);
	va_end(args);

	printk(KERN_INFO "%s: %s\n", DRIVERLNAME, line);
}

void divas_get_version(char *p)
{
	char tmprev[32];

	strcpy(tmprev, main_revision);
	sprintf(p, "%s: %s(%s) %s(%s) major=%d\n", DRIVERLNAME, DRIVERRELEASE_DIVAS,
		getrev(tmprev), diva_xdi_common_code_build, DIVA_BUILD, major);
}

/* --------------------------------------------------------------------------
    PCI Bus services  
   -------------------------------------------------------------------------- */
byte diva_os_get_pci_bus(void *pci_dev_handle)
{
	struct pci_dev *pdev = (struct pci_dev *) pci_dev_handle;
	return ((byte) pdev->bus->number);
}

byte diva_os_get_pci_func(void *pci_dev_handle)
{
	struct pci_dev *pdev = (struct pci_dev *) pci_dev_handle;
	return ((byte) pdev->devfn);
}

unsigned long divasa_get_pci_irq(unsigned char bus, unsigned char func,
				 void *pci_dev_handle)
{
	unsigned char irq = 0;
	struct pci_dev *dev = (struct pci_dev *) pci_dev_handle;

	irq = dev->irq;

	return ((unsigned long) irq);
}

unsigned long divasa_get_pci_bar(unsigned char bus, unsigned char func,
				 int bar, void *pci_dev_handle)
{
	unsigned long ret = 0;
	struct pci_dev *dev = (struct pci_dev *) pci_dev_handle;

	if (bar < 6) {
		ret = dev->resource[bar].start;
	}

	DBG_TRC(("GOT BAR[%d]=%08x", bar, ret));

	{
		unsigned long type = (ret & 0x00000001);
		if (type & PCI_BASE_ADDRESS_SPACE_IO) {
			DBG_TRC(("  I/O"));
			ret &= PCI_BASE_ADDRESS_IO_MASK;
		} else {
			DBG_TRC(("  memory"));
			ret &= PCI_BASE_ADDRESS_MEM_MASK;
		}
		DBG_TRC(("  final=%08x", ret));
	}

	return (ret);
}

void PCIwrite(byte bus, byte func, int offset, void *data, int length,
	      void *pci_dev_handle)
{
	struct pci_dev *dev = (struct pci_dev *) pci_dev_handle;

	switch (length) {
	case 1:		/* byte */
		pci_write_config_byte(dev, offset,
				      *(unsigned char *) data);
		break;
	case 2:		/* word */
		pci_write_config_word(dev, offset,
				      *(unsigned short *) data);
		break;
	case 4:		/* dword */
		pci_write_config_dword(dev, offset,
				       *(unsigned int *) data);
		break;

	default:		/* buffer */
		if (!(length % 4) && !(length & 0x03)) {	/* Copy as dword */
			dword *p = (dword *) data;
			length /= 4;

			while (length--) {
				pci_write_config_dword(dev, offset,
						       *(unsigned int *)
						       p++);
			}
		} else {	/* copy as byte stream */
			byte *p = (byte *) data;

			while (length--) {
				pci_write_config_byte(dev, offset,
						      *(unsigned char *)
						      p++);
			}
		}
	}
}

void PCIread(byte bus, byte func, int offset, void *data, int length,
	     void *pci_dev_handle)
{
	struct pci_dev *dev = (struct pci_dev *) pci_dev_handle;

	switch (length) {
	case 1:		/* byte */
		pci_read_config_byte(dev, offset, (unsigned char *) data);
		break;
	case 2:		/* word */
		pci_read_config_word(dev, offset, (unsigned short *) data);
		break;
	case 4:		/* dword */
		pci_read_config_dword(dev, offset, (unsigned int *) data);
		break;

	default:		/* buffer */
		if (!(length % 4) && !(length & 0x03)) {	/* Copy as dword */
			dword *p = (dword *) data;
			length /= 4;

			while (length--) {
				pci_read_config_dword(dev, offset,
						      (unsigned int *)
						      p++);
			}
		} else {	/* copy as byte stream */
			byte *p = (byte *) data;

			while (length--) {
				pci_read_config_byte(dev, offset,
						     (unsigned char *)
						     p++);
			}
		}
	}
}

/*
  Init map with DMA pages. It is not problem if some allocations fail -
  the channels that will not get one DMA page will use standard PIO
  interface
  */
static void *diva_pci_alloc_consistent(struct pci_dev *hwdev,
				       size_t size,
				       dma_addr_t * dma_handle,
				       void **addr_handle)
{
	void *addr = pci_alloc_consistent(hwdev, size, dma_handle);

	*addr_handle = addr;

	return (addr);
}

void diva_init_dma_map(void *hdev,
		       struct _diva_dma_map_entry **ppmap, int nentries)
{
	struct pci_dev *pdev = (struct pci_dev *) hdev;
	struct _diva_dma_map_entry *pmap =
	    diva_alloc_dma_map(hdev, nentries);

	if (pmap) {
		int i;
		dma_addr_t dma_handle;
		void *cpu_addr;
		void *addr_handle;

		for (i = 0; i < nentries; i++) {
			if (!(cpu_addr = diva_pci_alloc_consistent(pdev,
								   PAGE_SIZE,
								   &dma_handle,
								   &addr_handle)))
			{
				break;
			}
			diva_init_dma_map_entry(pmap, i, cpu_addr,
						(dword) dma_handle,
						addr_handle);
			DBG_TRC(("dma map alloc [%d]=(%08lx:%08x:%08lx)",
				 i, (unsigned long) cpu_addr,
				 (dword) dma_handle,
				 (unsigned long) addr_handle))}
	}

	*ppmap = pmap;
}

/*
  Free all contained in the map entries and memory used by the map
  Should be always called after adapter removal from DIDD array
  */
void diva_free_dma_map(void *hdev, struct _diva_dma_map_entry *pmap)
{
	struct pci_dev *pdev = (struct pci_dev *) hdev;
	int i;
	dword phys_addr;
	void *cpu_addr;
	dma_addr_t dma_handle;
	void *addr_handle;

	for (i = 0; (pmap != 0); i++) {
		diva_get_dma_map_entry(pmap, i, &cpu_addr, &phys_addr);
		if (!cpu_addr) {
			break;
		}
		addr_handle = diva_get_entry_handle(pmap, i);
		dma_handle = (dma_addr_t) phys_addr;
		pci_free_consistent(pdev, PAGE_SIZE, addr_handle,
				    dma_handle);
		DBG_TRC(("dma map free [%d]=(%08lx:%08x:%08lx)", i,
			 (unsigned long) cpu_addr, (dword) dma_handle,
			 (unsigned long) addr_handle))
	}

	diva_free_dma_mapping(pmap);
}


/*********************************************************
 ** I/O port utilities  
 *********************************************************/

int
diva_os_register_io_port(void *adapter, int on, unsigned long port,
			 unsigned long length, const char *name, int id)
{
	if (on) {
		if (!request_region(port, length, name)) {
			DBG_ERR(("A: I/O: can't register port=%08x", port))
			return (-1);
		}
	} else {
		release_region(port, length);
	}
	return (0);
}

void __iomem *divasa_remap_pci_bar(diva_os_xdi_adapter_t *a, int id, unsigned long bar, unsigned long area_length)
{
	void __iomem *ret = ioremap(bar, area_length);
	DBG_TRC(("remap(%08x)->%p", bar, ret));
	return (ret);
}

void divasa_unmap_pci_bar(void __iomem *bar)
{
	if (bar) {
		iounmap(bar);
	}
}

/*********************************************************
 ** I/O port access 
 *********************************************************/
byte __inline__ inpp(void __iomem *addr)
{
	return (inb((unsigned long) addr));
}

word __inline__ inppw(void __iomem *addr)
{
	return (inw((unsigned long) addr));
}

void __inline__ inppw_buffer(void __iomem *addr, void *P, int length)
{
	insw((unsigned long) addr, (word *) P, length >> 1);
}

void __inline__ outppw_buffer(void __iomem *addr, void *P, int length)
{
	outsw((unsigned long) addr, (word *) P, length >> 1);
}

void __inline__ outppw(void __iomem *addr, word w)
{
	outw(w, (unsigned long) addr);
}

void __inline__ outpp(void __iomem *addr, word p)
{
	outb(p, (unsigned long) addr);
}

/* --------------------------------------------------------------------------
    IRQ request / remove  
   -------------------------------------------------------------------------- */
int diva_os_register_irq(void *context, byte irq, const char *name)
{
	int result = request_irq(irq, diva_os_irq_wrapper,
				 SA_INTERRUPT | SA_SHIRQ, name, context);
	return (result);
}

void diva_os_remove_irq(void *context, byte irq)
{
	free_irq(irq, context);
}

/* --------------------------------------------------------------------------
    DPC framework implementation
   -------------------------------------------------------------------------- */
static void diva_os_dpc_proc(unsigned long context)
{
	diva_os_thread_dpc_t *psoft_isr = (diva_os_thread_dpc_t *) context;
	diva_os_soft_isr_t *pisr = psoft_isr->psoft_isr;

	(*(pisr->callback)) (pisr, pisr->callback_context);
}

int diva_os_initialize_soft_isr(diva_os_soft_isr_t * psoft_isr,
				diva_os_soft_isr_callback_t callback,
				void *callback_context)
{
	diva_os_thread_dpc_t *pdpc;

	pdpc = (diva_os_thread_dpc_t *) diva_os_malloc(0, sizeof(*pdpc));
	if (!(psoft_isr->object = pdpc)) {
		return (-1);
	}
	memset(pdpc, 0x00, sizeof(*pdpc));
	psoft_isr->callback = callback;
	psoft_isr->callback_context = callback_context;
	pdpc->psoft_isr = psoft_isr;
	tasklet_init(&pdpc->divas_task, diva_os_dpc_proc, (unsigned long)pdpc);

	return (0);
}

int diva_os_schedule_soft_isr(diva_os_soft_isr_t * psoft_isr)
{
	if (psoft_isr && psoft_isr->object) {
		diva_os_thread_dpc_t *pdpc =
		    (diva_os_thread_dpc_t *) psoft_isr->object;

		tasklet_schedule(&pdpc->divas_task);
	}

	return (1);
}

int diva_os_cancel_soft_isr(diva_os_soft_isr_t * psoft_isr)
{
	return (0);
}

void diva_os_remove_soft_isr(diva_os_soft_isr_t * psoft_isr)
{
	if (psoft_isr && psoft_isr->object) {
		diva_os_thread_dpc_t *pdpc =
		    (diva_os_thread_dpc_t *) psoft_isr->object;
		void *mem;

		tasklet_kill(&pdpc->divas_task);
		flush_scheduled_work();
		mem = psoft_isr->object;
		psoft_isr->object = NULL;
		diva_os_free(0, mem);
	}
}

/*
 * kernel/user space copy functions
 */
static int
xdi_copy_to_user(void *os_handle, void __user *dst, const void *src, int length)
{
	if (copy_to_user(dst, src, length)) {
		return (-EFAULT);
	}
	return (length);
}

static int
xdi_copy_from_user(void *os_handle, void *dst, const void __user *src, int length)
{
	if (copy_from_user(dst, src, length)) {
		return (-EFAULT);
	}
	return (length);
}

/*
 * device node operations
 */
static int divas_open(struct inode *inode, struct file *file)
{
	return (0);
}

static int divas_release(struct inode *inode, struct file *file)
{
	if (file->private_data) {
		diva_xdi_close_adapter(file->private_data, file);
	}
	return (0);
}

static ssize_t divas_write(struct file *file, const char __user *buf,
			   size_t count, loff_t * ppos)
{
	int ret = -EINVAL;

	if (!file->private_data) {
		file->private_data = diva_xdi_open_adapter(file, buf,
							   count,
							   xdi_copy_from_user);
	}
	if (!file->private_data) {
		return (-ENODEV);
	}

	ret = diva_xdi_write(file->private_data, file,
			     buf, count, xdi_copy_from_user);
	switch (ret) {
	case -1:		/* Message should be removed from rx mailbox first */
		ret = -EBUSY;
		break;
	case -2:		/* invalid adapter was specified in this call */
		ret = -ENOMEM;
		break;
	case -3:
		ret = -ENXIO;
		break;
	}
	DBG_TRC(("write: ret %d", ret));
	return (ret);
}

static ssize_t divas_read(struct file *file, char __user *buf,
			  size_t count, loff_t * ppos)
{
	int ret = -EINVAL;

	if (!file->private_data) {
		file->private_data = diva_xdi_open_adapter(file, buf,
							   count,
							   xdi_copy_from_user);
	}
	if (!file->private_data) {
		return (-ENODEV);
	}

	ret = diva_xdi_read(file->private_data, file,
			    buf, count, xdi_copy_to_user);
	switch (ret) {
	case -1:		/* RX mailbox is empty */
		ret = -EAGAIN;
		break;
	case -2:		/* no memory, mailbox was cleared, last command is failed */
		ret = -ENOMEM;
		break;
	case -3:		/* can't copy to user, retry */
		ret = -EFAULT;
		break;
	}
	DBG_TRC(("read: ret %d", ret));
	return (ret);
}

static unsigned int divas_poll(struct file *file, poll_table * wait)
{
	if (!file->private_data) {
		return (POLLERR);
	}
	return (POLLIN | POLLRDNORM);
}

static struct file_operations divas_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = divas_read,
	.write   = divas_write,
	.poll    = divas_poll,
	.open    = divas_open,
	.release = divas_release
};

static void divas_unregister_chrdev(void)
{
	devfs_remove(DEVNAME);
	unregister_chrdev(major, DEVNAME);
}

static int DIVA_INIT_FUNCTION divas_register_chrdev(void)
{
	if ((major = register_chrdev(0, DEVNAME, &divas_fops)) < 0)
	{
		printk(KERN_ERR "%s: failed to create /dev entry.\n",
		       DRIVERLNAME);
		return (0);
	}
	devfs_mk_cdev(MKDEV(major, 0), S_IFCHR|S_IRUSR|S_IWUSR, DEVNAME);

	return (1);
}

/* --------------------------------------------------------------------------
    PCI driver section
   -------------------------------------------------------------------------- */
static int __devinit divas_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	void *pdiva = NULL;
	u8 pci_latency;
	u8 new_latency = 32;

	DBG_TRC(("%s bus: %08x fn: %08x insertion.\n",
		 CardProperties[ent->driver_data].Name,
		 pdev->bus->number, pdev->devfn))
	printk(KERN_INFO "%s: %s bus: %08x fn: %08x insertion.\n",
		DRIVERLNAME, CardProperties[ent->driver_data].Name,
		pdev->bus->number, pdev->devfn);

	if (pci_enable_device(pdev)) {
		DBG_TRC(("%s: %s bus: %08x fn: %08x device init failed.\n",
			 DRIVERLNAME,
			 CardProperties[ent->driver_data].Name,
			 pdev->bus->number,
			 pdev->devfn))
		printk(KERN_ERR
			"%s: %s bus: %08x fn: %08x device init failed.\n",
			DRIVERLNAME,
			CardProperties[ent->driver_data].
			Name, pdev->bus->number,
			pdev->devfn);
		return (-EIO);
	}

	pci_set_master(pdev);

	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &pci_latency);
	if (!pci_latency) {
		DBG_TRC(("%s: bus: %08x fn: %08x fix latency.\n",
			 DRIVERLNAME, pdev->bus->number, pdev->devfn))
		printk(KERN_INFO
			"%s: bus: %08x fn: %08x fix latency.\n",
			 DRIVERLNAME, pdev->bus->number, pdev->devfn);
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, new_latency);
	}

	if (!(pdiva = diva_driver_add_card(pdev, ent->driver_data))) {
		DBG_TRC(("%s: %s bus: %08x fn: %08x card init failed.\n",
			 DRIVERLNAME,
			 CardProperties[ent->driver_data].Name,
			 pdev->bus->number,
			 pdev->devfn))
		printk(KERN_ERR
			"%s: %s bus: %08x fn: %08x card init failed.\n",
			DRIVERLNAME,
			CardProperties[ent->driver_data].
			Name, pdev->bus->number,
			pdev->devfn);
		return (-EIO);
	}

	pci_set_drvdata(pdev, pdiva);

	return (0);
}

static void __devexit divas_remove_one(struct pci_dev *pdev)
{
	void *pdiva = pci_get_drvdata(pdev);

	DBG_TRC(("bus: %08x fn: %08x removal.\n",
		 pdev->bus->number, pdev->devfn))
	printk(KERN_INFO "%s: bus: %08x fn: %08x removal.\n",
		DRIVERLNAME, pdev->bus->number, pdev->devfn);

	if (pdiva) {
		diva_driver_remove_card(pdiva);
	}

}

/* --------------------------------------------------------------------------
    Driver Load / Startup  
   -------------------------------------------------------------------------- */
static int DIVA_INIT_FUNCTION divas_init(void)
{
	char tmprev[50];
	int ret = 0;

	printk(KERN_INFO "%s\n", DRIVERNAME);
	printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE_DIVAS);
	strcpy(tmprev, main_revision);
	printk("%s  Build: %s(%s)\n", getrev(tmprev),
	       diva_xdi_common_code_build, DIVA_BUILD);
	printk(KERN_INFO "%s: support for: ", DRIVERLNAME);
#ifdef CONFIG_ISDN_DIVAS_BRIPCI
	printk("BRI/PCI ");
#endif
#ifdef CONFIG_ISDN_DIVAS_PRIPCI
	printk("PRI/PCI ");
#endif
	printk("adapters\n");

	if (!divasfunc_init(dbgmask)) {
		printk(KERN_ERR "%s: failed to connect to DIDD.\n",
		       DRIVERLNAME);
		ret = -EIO;
		goto out;
	}

	if (!divas_register_chrdev()) {
#ifdef MODULE
		divasfunc_exit();
#endif
		ret = -EIO;
		goto out;
	}

	if (!create_divas_proc()) {
#ifdef MODULE
		remove_divas_proc();
		divas_unregister_chrdev();
		divasfunc_exit();
#endif
		printk(KERN_ERR "%s: failed to create proc entry.\n",
		       DRIVERLNAME);
		ret = -EIO;
		goto out;
	}

	if ((ret = pci_register_driver(&diva_pci_driver))) {
#ifdef MODULE
		remove_divas_proc();
		divas_unregister_chrdev();
		divasfunc_exit();
#endif
		printk(KERN_ERR "%s: failed to init pci driver.\n",
		       DRIVERLNAME);
		goto out;
	}
	printk(KERN_INFO "%s: started with major %d\n", DRIVERLNAME, major);

      out:
	return (ret);
}

/* --------------------------------------------------------------------------
    Driver Unload
   -------------------------------------------------------------------------- */
static void DIVA_EXIT_FUNCTION divas_exit(void)
{
	pci_unregister_driver(&diva_pci_driver);
	remove_divas_proc();
	divas_unregister_chrdev();
	divasfunc_exit();

	printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divas_init);
module_exit(divas_exit);
