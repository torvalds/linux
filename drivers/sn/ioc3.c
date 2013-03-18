/*
 * SGI IOC3 master driver and IRQ demuxer
 *
 * Copyright (c) 2005 Stanislaw Skowronek <skylark@linux-mips.org>
 * Heavily based on similar work by:
 *   Brent Casavant <bcasavan@sgi.com> - IOC4 master driver
 *   Pat Gefre <pfg@sgi.com> - IOC3 serial port IRQ demuxer
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/ioc3.h>
#include <linux/rwsem.h>
#include <linux/slab.h>

#define IOC3_PCI_SIZE 0x100000

static LIST_HEAD(ioc3_devices);
static int ioc3_counter;
static DECLARE_RWSEM(ioc3_devices_rwsem);

static struct ioc3_submodule *ioc3_submodules[IOC3_MAX_SUBMODULES];
static struct ioc3_submodule *ioc3_ethernet;
static DEFINE_RWLOCK(ioc3_submodules_lock);

/* NIC probing code */

#define GPCR_MLAN_EN    0x00200000      /* enable MCR to pin 8 */

static inline unsigned mcr_pack(unsigned pulse, unsigned sample)
{
	return (pulse << 10) | (sample << 2);
}

static int nic_wait(struct ioc3_driver_data *idd)
{
	unsigned mcr;

        do {
                mcr = readl(&idd->vma->mcr);
        } while (!(mcr & 2));

        return mcr & 1;
}

static int nic_reset(struct ioc3_driver_data *idd)
{
        int presence;
	unsigned long flags;

	local_irq_save(flags);
	writel(mcr_pack(500, 65), &idd->vma->mcr);
	presence = nic_wait(idd);
	local_irq_restore(flags);

	udelay(500);

        return presence;
}

static int nic_read_bit(struct ioc3_driver_data *idd)
{
	int result;
	unsigned long flags;

	local_irq_save(flags);
	writel(mcr_pack(6, 13), &idd->vma->mcr);
	result = nic_wait(idd);
	local_irq_restore(flags);

	udelay(500);

	return result;
}

static void nic_write_bit(struct ioc3_driver_data *idd, int bit)
{
	if (bit)
		writel(mcr_pack(6, 110), &idd->vma->mcr);
	else
		writel(mcr_pack(80, 30), &idd->vma->mcr);

	nic_wait(idd);
}

static unsigned nic_read_byte(struct ioc3_driver_data *idd)
{
	unsigned result = 0;
	int i;

	for (i = 0; i < 8; i++)
		result = (result >> 1) | (nic_read_bit(idd) << 7);

	return result;
}

static void nic_write_byte(struct ioc3_driver_data *idd, int byte)
{
	int i, bit;

	for (i = 8; i; i--) {
		bit = byte & 1;
		byte >>= 1;

		nic_write_bit(idd, bit);
	}
}

static unsigned long
nic_find(struct ioc3_driver_data *idd, int *last, unsigned long addr)
{
	int a, b, index, disc;

	nic_reset(idd);

	/* Search ROM.  */
	nic_write_byte(idd, 0xF0);

	/* Algorithm from ``Book of iButton Standards''.  */
	for (index = 0, disc = 0; index < 64; index++) {
		a = nic_read_bit(idd);
		b = nic_read_bit(idd);

		if (a && b) {
			printk(KERN_WARNING "IOC3 NIC search failed.\n");
			*last = 0;
			return 0;
		}

		if (!a && !b) {
			if (index == *last) {
				addr |= 1UL << index;
			} else if (index > *last) {
				addr &= ~(1UL << index);
				disc = index;
			} else if ((addr & (1UL << index)) == 0)
				disc = index;
			nic_write_bit(idd, (addr>>index)&1);
			continue;
		} else {
			if (a)
				addr |= 1UL << index;
			else
				addr &= ~(1UL << index);
			nic_write_bit(idd, a);
			continue;
		}
	}
	*last = disc;
	return addr;
}

static void nic_addr(struct ioc3_driver_data *idd, unsigned long addr)
{
	int index;

	nic_reset(idd);
	nic_write_byte(idd, 0xF0);
	for (index = 0; index < 64; index++) {
		nic_read_bit(idd);
		nic_read_bit(idd);
		nic_write_bit(idd, (addr>>index)&1);
	}
}

static void crc16_byte(unsigned int *crc, unsigned char db)
{
	int i;

	for(i=0;i<8;i++) {
		*crc <<= 1;
		if((db^(*crc>>16)) & 1)
			*crc ^= 0x8005;
		db >>= 1;
	}
	*crc &= 0xFFFF;
}

static unsigned int crc16_area(unsigned char *dbs, int size, unsigned int crc)
{
	while(size--)
		crc16_byte(&crc, *(dbs++));
	return crc;
}

static void crc8_byte(unsigned int *crc, unsigned char db)
{
	int i,f;

	for(i=0;i<8;i++) {
		f = (*crc ^ db) & 1;
		*crc >>= 1;
		db >>= 1;
		if(f)
			*crc ^= 0x8c;
	}
	*crc &= 0xff;
}

static unsigned int crc8_addr(unsigned long addr)
{
	int i;
	unsigned int crc = 0x00;

	for(i=0;i<8;i++)
		crc8_byte(&crc, addr>>(i<<3));
	return crc;
}

static void
read_redir_page(struct ioc3_driver_data *idd, unsigned long addr, int page,
			unsigned char *redir, unsigned char *data)
{
	int loops = 16, i;

	while(redir[page] != 0xFF) {
		page = redir[page]^0xFF;
		loops--;
		if(loops<0) {
			printk(KERN_ERR "IOC3: NIC circular redirection\n");
			return;
		}
	}
	loops = 3;
	while(loops>0) {
		nic_addr(idd, addr);
		nic_write_byte(idd, 0xF0);
		nic_write_byte(idd, (page << 5) & 0xE0);
		nic_write_byte(idd, (page >> 3) & 0x1F);
		for(i=0;i<0x20;i++)
			data[i] = nic_read_byte(idd);
		if(crc16_area(data, 0x20, 0x0000) == 0x800d)
			return;
		loops--;
	}
	printk(KERN_ERR "IOC3: CRC error in data page\n");
	for(i=0;i<0x20;i++)
		data[i] = 0x00;
}

static void
read_redir_map(struct ioc3_driver_data *idd, unsigned long addr,
					 unsigned char *redir)
{
	int i,j,loops = 3,crc_ok;
	unsigned int crc;

	while(loops>0) {
		crc_ok = 1;
		nic_addr(idd, addr);
		nic_write_byte(idd, 0xAA);
		nic_write_byte(idd, 0x00);
		nic_write_byte(idd, 0x01);
		for(i=0;i<64;i+=8) {
			for(j=0;j<8;j++)
				redir[i+j] = nic_read_byte(idd);
			crc = crc16_area(redir+i, 8, (i==0)?0x8707:0x0000);
			crc16_byte(&crc, nic_read_byte(idd));
			crc16_byte(&crc, nic_read_byte(idd));
			if(crc != 0x800d)
				crc_ok = 0;
		}
		if(crc_ok)
			return;
		loops--;
	}
	printk(KERN_ERR "IOC3: CRC error in redirection page\n");
	for(i=0;i<64;i++)
		redir[i] = 0xFF;
}

static void read_nic(struct ioc3_driver_data *idd, unsigned long addr)
{
	unsigned char redir[64];
	unsigned char data[64],part[32];
	int i,j;

	/* read redirections */
	read_redir_map(idd, addr, redir);
	/* read data pages */
	read_redir_page(idd, addr, 0, redir, data);
	read_redir_page(idd, addr, 1, redir, data+32);
	/* assemble the part # */
	j=0;
	for(i=0;i<19;i++)
		if(data[i+11] != ' ')
			part[j++] = data[i+11];
	for(i=0;i<6;i++)
		if(data[i+32] != ' ')
			part[j++] = data[i+32];
	part[j] = 0;
	/* skip Octane power supplies */
	if(!strncmp(part, "060-0035-", 9))
		return;
	if(!strncmp(part, "060-0038-", 9))
		return;
	strcpy(idd->nic_part, part);
	/* assemble the serial # */
	j=0;
	for(i=0;i<10;i++)
		if(data[i+1] != ' ')
			idd->nic_serial[j++] = data[i+1];
	idd->nic_serial[j] = 0;
}

static void read_mac(struct ioc3_driver_data *idd, unsigned long addr)
{
	int i, loops = 3;
	unsigned char data[13];

	while(loops>0) {
		nic_addr(idd, addr);
		nic_write_byte(idd, 0xF0);
		nic_write_byte(idd, 0x00);
		nic_write_byte(idd, 0x00);
		nic_read_byte(idd);
		for(i=0;i<13;i++)
			data[i] = nic_read_byte(idd);
		if(crc16_area(data, 13, 0x0000) == 0x800d) {
			for(i=10;i>4;i--)
				idd->nic_mac[10-i] = data[i];
			return;
		}
		loops--;
	}
	printk(KERN_ERR "IOC3: CRC error in MAC address\n");
	for(i=0;i<6;i++)
		idd->nic_mac[i] = 0x00;
}

static void probe_nic(struct ioc3_driver_data *idd)
{
        int save = 0, loops = 3;
        unsigned long first, addr;

        writel(GPCR_MLAN_EN, &idd->vma->gpcr_s);

        while(loops>0) {
                idd->nic_part[0] = 0;
                idd->nic_serial[0] = 0;
                addr = first = nic_find(idd, &save, 0);
                if(!first)
                        return;
                while(1) {
                        if(crc8_addr(addr))
                                break;
                        else {
                                switch(addr & 0xFF) {
                                case 0x0B:
                                        read_nic(idd, addr);
                                        break;
                                case 0x09:
                                case 0x89:
                                case 0x91:
                                        read_mac(idd, addr);
                                        break;
                                }
                        }
                        addr = nic_find(idd, &save, addr);
                        if(addr == first)
                                return;
                }
                loops--;
        }
        printk(KERN_ERR "IOC3: CRC error in NIC address\n");
}

/* Interrupts */

static void write_ireg(struct ioc3_driver_data *idd, uint32_t val, int which)
{
	unsigned long flags;

	spin_lock_irqsave(&idd->ir_lock, flags);
	switch (which) {
	case IOC3_W_IES:
		writel(val, &idd->vma->sio_ies);
		break;
	case IOC3_W_IEC:
		writel(val, &idd->vma->sio_iec);
		break;
	}
	spin_unlock_irqrestore(&idd->ir_lock, flags);
}
static inline uint32_t get_pending_intrs(struct ioc3_driver_data *idd)
{
	unsigned long flag;
	uint32_t intrs = 0;

	spin_lock_irqsave(&idd->ir_lock, flag);
	intrs = readl(&idd->vma->sio_ir);
	intrs &= readl(&idd->vma->sio_ies);
	spin_unlock_irqrestore(&idd->ir_lock, flag);
	return intrs;
}

static irqreturn_t ioc3_intr_io(int irq, void *arg)
{
	unsigned long flags;
	struct ioc3_driver_data *idd = arg;
	int handled = 1, id;
	unsigned int pending;

	read_lock_irqsave(&ioc3_submodules_lock, flags);

	if(idd->dual_irq && readb(&idd->vma->eisr)) {
		/* send Ethernet IRQ to the driver */
		if(ioc3_ethernet && idd->active[ioc3_ethernet->id] &&
						ioc3_ethernet->intr) {
			handled = handled && !ioc3_ethernet->intr(ioc3_ethernet,
							idd, 0);
		}
	}
	pending = get_pending_intrs(idd);	/* look at the IO IRQs */

	for(id=0;id<IOC3_MAX_SUBMODULES;id++) {
		if(idd->active[id] && ioc3_submodules[id]
				&& (pending & ioc3_submodules[id]->irq_mask)
				&& ioc3_submodules[id]->intr) {
			write_ireg(idd, ioc3_submodules[id]->irq_mask,
							IOC3_W_IEC);
			if(!ioc3_submodules[id]->intr(ioc3_submodules[id],
				   idd, pending & ioc3_submodules[id]->irq_mask))
				pending &= ~ioc3_submodules[id]->irq_mask;
			if (ioc3_submodules[id]->reset_mask)
				write_ireg(idd, ioc3_submodules[id]->irq_mask,
							IOC3_W_IES);
		}
	}
	read_unlock_irqrestore(&ioc3_submodules_lock, flags);
	if(pending) {
		printk(KERN_WARNING
		  "IOC3: Pending IRQs 0x%08x discarded and disabled\n",pending);
		write_ireg(idd, pending, IOC3_W_IEC);
		handled = 1;
	}
	return handled?IRQ_HANDLED:IRQ_NONE;
}

static irqreturn_t ioc3_intr_eth(int irq, void *arg)
{
	unsigned long flags;
	struct ioc3_driver_data *idd = (struct ioc3_driver_data *)arg;
	int handled = 1;

	if(!idd->dual_irq)
		return IRQ_NONE;
	read_lock_irqsave(&ioc3_submodules_lock, flags);
	if(ioc3_ethernet && idd->active[ioc3_ethernet->id]
				&& ioc3_ethernet->intr)
		handled = handled && !ioc3_ethernet->intr(ioc3_ethernet, idd, 0);
	read_unlock_irqrestore(&ioc3_submodules_lock, flags);
	return handled?IRQ_HANDLED:IRQ_NONE;
}

void ioc3_enable(struct ioc3_submodule *is,
				struct ioc3_driver_data *idd, unsigned int irqs)
{
	write_ireg(idd, irqs & is->irq_mask, IOC3_W_IES);
}

void ioc3_ack(struct ioc3_submodule *is, struct ioc3_driver_data *idd,
				unsigned int irqs)
{
	writel(irqs & is->irq_mask, &idd->vma->sio_ir);
}

void ioc3_disable(struct ioc3_submodule *is,
				struct ioc3_driver_data *idd, unsigned int irqs)
{
	write_ireg(idd, irqs & is->irq_mask, IOC3_W_IEC);
}

void ioc3_gpcr_set(struct ioc3_driver_data *idd, unsigned int val)
{
	unsigned long flags;
	spin_lock_irqsave(&idd->gpio_lock, flags);
	writel(val, &idd->vma->gpcr_s);
	spin_unlock_irqrestore(&idd->gpio_lock, flags);
}

/* Keep it simple, stupid! */
static int find_slot(void **tab, int max)
{
	int i;
	for(i=0;i<max;i++)
		if(!(tab[i]))
			return i;
	return -1;
}

/* Register an IOC3 submodule */
int ioc3_register_submodule(struct ioc3_submodule *is)
{
	struct ioc3_driver_data *idd;
	int alloc_id;
	unsigned long flags;

	write_lock_irqsave(&ioc3_submodules_lock, flags);
	alloc_id = find_slot((void **)ioc3_submodules, IOC3_MAX_SUBMODULES);
	if(alloc_id != -1) {
		ioc3_submodules[alloc_id] = is;
		if(is->ethernet) {
			if(ioc3_ethernet==NULL)
				ioc3_ethernet=is;
			else
				printk(KERN_WARNING
				  "IOC3 Ethernet module already registered!\n");
		}
	}
	write_unlock_irqrestore(&ioc3_submodules_lock, flags);

	if(alloc_id == -1) {
		printk(KERN_WARNING "Increase IOC3_MAX_SUBMODULES!\n");
		return -ENOMEM;
	}

	is->id=alloc_id;

	/* Initialize submodule for each IOC3 */
	if (!is->probe)
		return 0;

	down_read(&ioc3_devices_rwsem);
	list_for_each_entry(idd, &ioc3_devices, list) {
		/* set to 1 for IRQs in probe */
		idd->active[alloc_id] = 1;
		idd->active[alloc_id] = !is->probe(is, idd);
	}
	up_read(&ioc3_devices_rwsem);

	return 0;
}

/* Unregister an IOC3 submodule */
void ioc3_unregister_submodule(struct ioc3_submodule *is)
{
	struct ioc3_driver_data *idd;
	unsigned long flags;

	write_lock_irqsave(&ioc3_submodules_lock, flags);
	if(ioc3_submodules[is->id]==is)
		ioc3_submodules[is->id]=NULL;
	else
		printk(KERN_WARNING
			"IOC3 submodule %s has wrong ID.\n",is->name);
	if(ioc3_ethernet==is)
		ioc3_ethernet = NULL;
	write_unlock_irqrestore(&ioc3_submodules_lock, flags);

	/* Remove submodule for each IOC3 */
	down_read(&ioc3_devices_rwsem);
	list_for_each_entry(idd, &ioc3_devices, list)
		if(idd->active[is->id]) {
			if(is->remove)
				if(is->remove(is, idd))
					printk(KERN_WARNING
					       "%s: IOC3 submodule %s remove failed "
					       "for pci_dev %s.\n",
					       __func__, module_name(is->owner),
					       pci_name(idd->pdev));
			idd->active[is->id] = 0;
			if(is->irq_mask)
				write_ireg(idd, is->irq_mask, IOC3_W_IEC);
		}
	up_read(&ioc3_devices_rwsem);
}

/*********************
 * Device management *
 *********************/

static char *ioc3_class_names[] = { "unknown", "IP27 BaseIO", "IP30 system",
			"MENET 1/2/3", "MENET 4", "CADduo", "Altix Serial" };

static int ioc3_class(struct ioc3_driver_data *idd)
{
	int res = IOC3_CLASS_NONE;
	/* NIC-based logic */
	if(!strncmp(idd->nic_part, "030-0891-", 9))
		res = IOC3_CLASS_BASE_IP30;
	if(!strncmp(idd->nic_part, "030-1155-", 9))
		res = IOC3_CLASS_CADDUO;
	if(!strncmp(idd->nic_part, "030-1657-", 9))
		res = IOC3_CLASS_SERIAL;
	if(!strncmp(idd->nic_part, "030-1664-", 9))
		res = IOC3_CLASS_SERIAL;
	/* total random heuristics */
#ifdef CONFIG_SGI_IP27
	if(!idd->nic_part[0])
		res = IOC3_CLASS_BASE_IP27;
#endif
	/* print educational message */
	printk(KERN_INFO "IOC3 part: [%s], serial: [%s] => class %s\n",
			idd->nic_part, idd->nic_serial, ioc3_class_names[res]);
	return res;
}
/* Adds a new instance of an IOC3 card */
static int ioc3_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct ioc3_driver_data *idd;
	uint32_t pcmd;
	int ret, id;

	/* Enable IOC3 and take ownership of it */
	if ((ret = pci_enable_device(pdev))) {
		printk(KERN_WARNING
		       "%s: Failed to enable IOC3 device for pci_dev %s.\n",
		       __func__, pci_name(pdev));
		goto out;
	}
	pci_set_master(pdev);

#ifdef USE_64BIT_DMA
        ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
        if (!ret) {
                ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
                if (ret < 0) {
                        printk(KERN_WARNING "%s: Unable to obtain 64 bit DMA "
                               "for consistent allocations\n",
				__func__);
                }
	}
#endif

	/* Set up per-IOC3 data */
	idd = kzalloc(sizeof(struct ioc3_driver_data), GFP_KERNEL);
	if (!idd) {
		printk(KERN_WARNING
		       "%s: Failed to allocate IOC3 data for pci_dev %s.\n",
		       __func__, pci_name(pdev));
		ret = -ENODEV;
		goto out_idd;
	}
	spin_lock_init(&idd->ir_lock);
	spin_lock_init(&idd->gpio_lock);
	idd->pdev = pdev;

	/* Map all IOC3 registers.  These are shared between subdevices
	 * so the main IOC3 module manages them.
	 */
	idd->pma = pci_resource_start(pdev, 0);
	if (!idd->pma) {
		printk(KERN_WARNING
		       "%s: Unable to find IOC3 resource "
		       "for pci_dev %s.\n",
		       __func__, pci_name(pdev));
		ret = -ENODEV;
		goto out_pci;
	}
	if (!request_mem_region(idd->pma, IOC3_PCI_SIZE, "ioc3")) {
		printk(KERN_WARNING
		       "%s: Unable to request IOC3 region "
		       "for pci_dev %s.\n",
		       __func__, pci_name(pdev));
		ret = -ENODEV;
		goto out_pci;
	}
	idd->vma = ioremap(idd->pma, IOC3_PCI_SIZE);
	if (!idd->vma) {
		printk(KERN_WARNING
		       "%s: Unable to remap IOC3 region "
		       "for pci_dev %s.\n",
		       __func__, pci_name(pdev));
		ret = -ENODEV;
		goto out_misc_region;
	}

	/* Track PCI-device specific data */
	pci_set_drvdata(pdev, idd);
	down_write(&ioc3_devices_rwsem);
	list_add_tail(&idd->list, &ioc3_devices);
	idd->id = ioc3_counter++;
	up_write(&ioc3_devices_rwsem);

	idd->gpdr_shadow = readl(&idd->vma->gpdr);

	/* Read IOC3 NIC contents */
	probe_nic(idd);

	/* Detect IOC3 class */
	idd->class = ioc3_class(idd);

	/* Initialize IOC3 */
       pci_read_config_dword(pdev, PCI_COMMAND, &pcmd);
       pci_write_config_dword(pdev, PCI_COMMAND,
                               pcmd | PCI_COMMAND_MEMORY |
                               PCI_COMMAND_PARITY | PCI_COMMAND_SERR |
                               PCI_SCR_DROP_MODE_EN);

	write_ireg(idd, ~0, IOC3_W_IEC);
	writel(~0, &idd->vma->sio_ir);

	/* Set up IRQs */
	if(idd->class == IOC3_CLASS_BASE_IP30
				|| idd->class == IOC3_CLASS_BASE_IP27) {
		writel(0, &idd->vma->eier);
		writel(~0, &idd->vma->eisr);

		idd->dual_irq = 1;
		if (!request_irq(pdev->irq, ioc3_intr_eth, IRQF_SHARED,
				 "ioc3-eth", (void *)idd)) {
			idd->irq_eth = pdev->irq;
		} else {
			printk(KERN_WARNING
			       "%s : request_irq fails for IRQ 0x%x\n ",
			       __func__, pdev->irq);
		}
		if (!request_irq(pdev->irq+2, ioc3_intr_io, IRQF_SHARED,
				 "ioc3-io", (void *)idd)) {
			idd->irq_io = pdev->irq+2;
		} else {
			printk(KERN_WARNING
			       "%s : request_irq fails for IRQ 0x%x\n ",
			       __func__, pdev->irq+2);
		}
	} else {
		if (!request_irq(pdev->irq, ioc3_intr_io, IRQF_SHARED,
				 "ioc3", (void *)idd)) {
			idd->irq_io = pdev->irq;
		} else {
			printk(KERN_WARNING
			       "%s : request_irq fails for IRQ 0x%x\n ",
			       __func__, pdev->irq);
		}
	}

	/* Add this IOC3 to all submodules */
	for(id=0;id<IOC3_MAX_SUBMODULES;id++)
		if(ioc3_submodules[id] && ioc3_submodules[id]->probe) {
			idd->active[id] = 1;
			idd->active[id] = !ioc3_submodules[id]->probe
						(ioc3_submodules[id], idd);
		}

	printk(KERN_INFO "IOC3 Master Driver loaded for %s\n", pci_name(pdev));

	return 0;

out_misc_region:
	release_mem_region(idd->pma, IOC3_PCI_SIZE);
out_pci:
	kfree(idd);
out_idd:
	pci_disable_device(pdev);
out:
	return ret;
}

/* Removes a particular instance of an IOC3 card. */
static void ioc3_remove(struct pci_dev *pdev)
{
	int id;
	struct ioc3_driver_data *idd;

	idd = pci_get_drvdata(pdev);

	/* Remove this IOC3 from all submodules */
	for(id=0;id<IOC3_MAX_SUBMODULES;id++)
		if(idd->active[id]) {
			if(ioc3_submodules[id] && ioc3_submodules[id]->remove)
				if(ioc3_submodules[id]->remove(ioc3_submodules[id],
								idd))
					printk(KERN_WARNING
					       "%s: IOC3 submodule 0x%s remove failed "
					       "for pci_dev %s.\n",
						__func__,
						module_name(ioc3_submodules[id]->owner),
					        pci_name(pdev));
			idd->active[id] = 0;
		}

	/* Clear and disable all IRQs */
	write_ireg(idd, ~0, IOC3_W_IEC);
	writel(~0, &idd->vma->sio_ir);

	/* Release resources */
	free_irq(idd->irq_io, (void *)idd);
	if(idd->dual_irq)
		free_irq(idd->irq_eth, (void *)idd);
	iounmap(idd->vma);
	release_mem_region(idd->pma, IOC3_PCI_SIZE);

	/* Disable IOC3 and relinquish */
	pci_disable_device(pdev);

	/* Remove and free driver data */
	down_write(&ioc3_devices_rwsem);
	list_del(&idd->list);
	up_write(&ioc3_devices_rwsem);
	kfree(idd);
}

static struct pci_device_id ioc3_id_table[] = {
	{PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3, PCI_ANY_ID, PCI_ANY_ID},
	{0}
};

static struct pci_driver ioc3_driver = {
	.name = "IOC3",
	.id_table = ioc3_id_table,
	.probe = ioc3_probe,
	.remove = ioc3_remove,
};

MODULE_DEVICE_TABLE(pci, ioc3_id_table);

/*********************
 * Module management *
 *********************/

/* Module load */
static int __init ioc3_init(void)
{
	if (ia64_platform_is("sn2"))
		return pci_register_driver(&ioc3_driver);
	return -ENODEV;
}

/* Module unload */
static void __exit ioc3_exit(void)
{
	pci_unregister_driver(&ioc3_driver);
}

module_init(ioc3_init);
module_exit(ioc3_exit);

MODULE_AUTHOR("Stanislaw Skowronek <skylark@linux-mips.org>");
MODULE_DESCRIPTION("PCI driver for SGI IOC3");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL_GPL(ioc3_register_submodule);
EXPORT_SYMBOL_GPL(ioc3_unregister_submodule);
EXPORT_SYMBOL_GPL(ioc3_ack);
EXPORT_SYMBOL_GPL(ioc3_gpcr_set);
EXPORT_SYMBOL_GPL(ioc3_disable);
EXPORT_SYMBOL_GPL(ioc3_enable);
