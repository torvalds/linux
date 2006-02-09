/*
 * Copyright (c) 2003-2006 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/ioc4.h>
#include <asm/io.h>

#include <linux/ide.h>

/* IOC4 Specific Definitions */
#define IOC4_CMD_OFFSET		0x100
#define IOC4_CTRL_OFFSET	0x120
#define IOC4_DMA_OFFSET		0x140
#define IOC4_INTR_OFFSET	0x0

#define IOC4_TIMING		0x00
#define IOC4_DMA_PTR_L		0x01
#define IOC4_DMA_PTR_H		0x02
#define IOC4_DMA_ADDR_L		0x03
#define IOC4_DMA_ADDR_H		0x04
#define IOC4_BC_DEV		0x05
#define IOC4_BC_MEM		0x06
#define	IOC4_DMA_CTRL		0x07
#define	IOC4_DMA_END_ADDR	0x08

/* Bits in the IOC4 Control/Status Register */
#define	IOC4_S_DMA_START	0x01
#define	IOC4_S_DMA_STOP		0x02
#define	IOC4_S_DMA_DIR		0x04
#define	IOC4_S_DMA_ACTIVE	0x08
#define	IOC4_S_DMA_ERROR	0x10
#define	IOC4_ATA_MEMERR		0x02

/* Read/Write Directions */
#define	IOC4_DMA_WRITE		0x04
#define	IOC4_DMA_READ		0x00

/* Interrupt Register Offsets */
#define IOC4_INTR_REG		0x03
#define	IOC4_INTR_SET		0x05
#define	IOC4_INTR_CLEAR		0x07

#define IOC4_IDE_CACHELINE_SIZE	128
#define IOC4_CMD_CTL_BLK_SIZE	0x20
#define IOC4_SUPPORTED_FIRMWARE_REV 46

typedef struct {
	u32 timing_reg0;
	u32 timing_reg1;
	u32 low_mem_ptr;
	u32 high_mem_ptr;
	u32 low_mem_addr;
	u32 high_mem_addr;
	u32 dev_byte_count;
	u32 mem_byte_count;
	u32 status;
} ioc4_dma_regs_t;

/* Each Physical Region Descriptor Entry size is 16 bytes (2 * 64 bits) */
/* IOC4 has only 1 IDE channel */
#define IOC4_PRD_BYTES       16
#define IOC4_PRD_ENTRIES     (PAGE_SIZE /(4*IOC4_PRD_BYTES))


static void
sgiioc4_init_hwif_ports(hw_regs_t * hw, unsigned long data_port,
			unsigned long ctrl_port, unsigned long irq_port)
{
	unsigned long reg = data_port;
	int i;

	/* Registers are word (32 bit) aligned */
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hw->io_ports[i] = reg + i * 4;

	if (ctrl_port)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;

	if (irq_port)
		hw->io_ports[IDE_IRQ_OFFSET] = irq_port;
}

static void
sgiioc4_maskproc(ide_drive_t * drive, int mask)
{
	ide_hwif_t *hwif = HWIF(drive);
	hwif->OUTB(mask ? (drive->ctl | 2) : (drive->ctl & ~2),
		   IDE_CONTROL_REG);
}


static int
sgiioc4_checkirq(ide_hwif_t * hwif)
{
	u8 intr_reg =
	    hwif->INL(hwif->io_ports[IDE_IRQ_OFFSET] + IOC4_INTR_REG * 4);

	if (intr_reg & 0x03)
		return 1;

	return 0;
}


static int
sgiioc4_clearirq(ide_drive_t * drive)
{
	u32 intr_reg;
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long other_ir =
	    hwif->io_ports[IDE_IRQ_OFFSET] + (IOC4_INTR_REG << 2);

	/* Code to check for PCI error conditions */
	intr_reg = hwif->INL(other_ir);
	if (intr_reg & 0x03) { /* Valid IOC4-IDE interrupt */
		/*
		 * Using hwif->INB to read the IDE_STATUS_REG has a side effect
		 * of clearing the interrupt.  The first read should clear it
		 * if it is set.  The second read should return a "clear" status
		 * if it got cleared.  If not, then spin for a bit trying to
		 * clear it.
		 */
		u8 stat = hwif->INB(IDE_STATUS_REG);
		int count = 0;
		stat = hwif->INB(IDE_STATUS_REG);
		while ((stat & 0x80) && (count++ < 100)) {
			udelay(1);
			stat = hwif->INB(IDE_STATUS_REG);
		}

		if (intr_reg & 0x02) {
			/* Error when transferring DMA data on PCI bus */
			u32 pci_err_addr_low, pci_err_addr_high,
			    pci_stat_cmd_reg;

			pci_err_addr_low =
				hwif->INL(hwif->io_ports[IDE_IRQ_OFFSET]);
			pci_err_addr_high =
				hwif->INL(hwif->io_ports[IDE_IRQ_OFFSET] + 4);
			pci_read_config_dword(hwif->pci_dev, PCI_COMMAND,
					      &pci_stat_cmd_reg);
			printk(KERN_ERR
			       "%s(%s) : PCI Bus Error when doing DMA:"
				   " status-cmd reg is 0x%x\n",
			       __FUNCTION__, drive->name, pci_stat_cmd_reg);
			printk(KERN_ERR
			       "%s(%s) : PCI Error Address is 0x%x%x\n",
			       __FUNCTION__, drive->name,
			       pci_err_addr_high, pci_err_addr_low);
			/* Clear the PCI Error indicator */
			pci_write_config_dword(hwif->pci_dev, PCI_COMMAND,
					       0x00000146);
		}

		/* Clear the Interrupt, Error bits on the IOC4 */
		hwif->OUTL(0x03, other_ir);

		intr_reg = hwif->INL(other_ir);
	}

	return intr_reg & 3;
}

static void sgiioc4_ide_dma_start(ide_drive_t * drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int reg = hwif->INL(hwif->dma_base + IOC4_DMA_CTRL * 4);
	unsigned int temp_reg = reg | IOC4_S_DMA_START;

	hwif->OUTL(temp_reg, hwif->dma_base + IOC4_DMA_CTRL * 4);
}

static u32
sgiioc4_ide_dma_stop(ide_hwif_t *hwif, u64 dma_base)
{
	u32	ioc4_dma;
	int	count;

	count = 0;
	ioc4_dma = hwif->INL(dma_base + IOC4_DMA_CTRL * 4);
	while ((ioc4_dma & IOC4_S_DMA_STOP) && (count++ < 200)) {
		udelay(1);
		ioc4_dma = hwif->INL(dma_base + IOC4_DMA_CTRL * 4);
	}
	return ioc4_dma;
}

/* Stops the IOC4 DMA Engine */
static int
sgiioc4_ide_dma_end(ide_drive_t * drive)
{
	u32 ioc4_dma, bc_dev, bc_mem, num, valid = 0, cnt = 0;
	ide_hwif_t *hwif = HWIF(drive);
	u64 dma_base = hwif->dma_base;
	int dma_stat = 0;
	unsigned long *ending_dma = (unsigned long *) hwif->dma_base2;

	hwif->OUTL(IOC4_S_DMA_STOP, dma_base + IOC4_DMA_CTRL * 4);

	ioc4_dma = sgiioc4_ide_dma_stop(hwif, dma_base);

	if (ioc4_dma & IOC4_S_DMA_STOP) {
		printk(KERN_ERR
		       "%s(%s): IOC4 DMA STOP bit is still 1 :"
		       "ioc4_dma_reg 0x%x\n",
		       __FUNCTION__, drive->name, ioc4_dma);
		dma_stat = 1;
	}

	/*
	 * The IOC4 will DMA 1's to the ending dma area to indicate that
	 * previous data DMA is complete.  This is necessary because of relaxed
	 * ordering between register reads and DMA writes on the Altix.
	 */
	while ((cnt++ < 200) && (!valid)) {
		for (num = 0; num < 16; num++) {
			if (ending_dma[num]) {
				valid = 1;
				break;
			}
		}
		udelay(1);
	}
	if (!valid) {
		printk(KERN_ERR "%s(%s) : DMA incomplete\n", __FUNCTION__,
		       drive->name);
		dma_stat = 1;
	}

	bc_dev = hwif->INL(dma_base + IOC4_BC_DEV * 4);
	bc_mem = hwif->INL(dma_base + IOC4_BC_MEM * 4);

	if ((bc_dev & 0x01FF) || (bc_mem & 0x1FF)) {
		if (bc_dev > bc_mem + 8) {
			printk(KERN_ERR
			       "%s(%s): WARNING!! byte_count_dev %d "
			       "!= byte_count_mem %d\n",
			       __FUNCTION__, drive->name, bc_dev, bc_mem);
		}
	}

	drive->waiting_for_dma = 0;
	ide_destroy_dmatable(drive);

	return dma_stat;
}

static int
sgiioc4_ide_dma_check(ide_drive_t * drive)
{
	if (ide_config_drive_speed(drive, XFER_MW_DMA_2) != 0) {
		printk(KERN_INFO
		       "Couldnot set %s in Multimode-2 DMA mode | "
			   "Drive %s using PIO instead\n",
		       drive->name, drive->name);
		drive->using_dma = 0;
	} else
		drive->using_dma = 1;

	return 0;
}

static int
sgiioc4_ide_dma_on(ide_drive_t * drive)
{
	drive->using_dma = 1;

	return HWIF(drive)->ide_dma_host_on(drive);
}

static int
sgiioc4_ide_dma_off_quietly(ide_drive_t * drive)
{
	drive->using_dma = 0;

	return HWIF(drive)->ide_dma_host_off(drive);
}

/* returns 1 if dma irq issued, 0 otherwise */
static int
sgiioc4_ide_dma_test_irq(ide_drive_t * drive)
{
	return sgiioc4_checkirq(HWIF(drive));
}

static int
sgiioc4_ide_dma_host_on(ide_drive_t * drive)
{
	if (drive->using_dma)
		return 0;

	return 1;
}

static int
sgiioc4_ide_dma_host_off(ide_drive_t * drive)
{
	sgiioc4_clearirq(drive);

	return 0;
}

static int
sgiioc4_ide_dma_lostirq(ide_drive_t * drive)
{
	HWIF(drive)->resetproc(drive);

	return __ide_dma_lostirq(drive);
}

static void
sgiioc4_resetproc(ide_drive_t * drive)
{
	sgiioc4_ide_dma_end(drive);
	sgiioc4_clearirq(drive);
}

static u8
sgiioc4_INB(unsigned long port)
{
	u8 reg = (u8) inb(port);

	if ((port & 0xFFF) == 0x11C) {	/* Status register of IOC4 */
		if (reg & 0x51) {	/* Not busy...check for interrupt */
			unsigned long other_ir = port - 0x110;
			unsigned int intr_reg = (u32) inl(other_ir);

			/* Clear the Interrupt, Error bits on the IOC4 */
			if (intr_reg & 0x03) {
				outl(0x03, other_ir);
				intr_reg = (u32) inl(other_ir);
			}
		}
	}

	return reg;
}

/* Creates a dma map for the scatter-gather list entries */
static void __devinit
ide_dma_sgiioc4(ide_hwif_t * hwif, unsigned long dma_base)
{
	int num_ports = sizeof (ioc4_dma_regs_t);

	printk(KERN_INFO "%s: BM-DMA at 0x%04lx-0x%04lx\n", hwif->name,
	       dma_base, dma_base + num_ports - 1);

	if (!request_region(dma_base, num_ports, hwif->name)) {
		printk(KERN_ERR
		       "%s(%s) -- ERROR, Addresses 0x%p to 0x%p "
		       "ALREADY in use\n",
		       __FUNCTION__, hwif->name, (void *) dma_base,
		       (void *) dma_base + num_ports - 1);
		goto dma_alloc_failure;
	}

	hwif->dma_base = dma_base;
	hwif->dmatable_cpu = pci_alloc_consistent(hwif->pci_dev,
					  IOC4_PRD_ENTRIES * IOC4_PRD_BYTES,
					  &hwif->dmatable_dma);

	if (!hwif->dmatable_cpu)
		goto dma_alloc_failure;

	hwif->sg_max_nents = IOC4_PRD_ENTRIES;

	hwif->dma_base2 = (unsigned long)
		pci_alloc_consistent(hwif->pci_dev,
				     IOC4_IDE_CACHELINE_SIZE,
				     (dma_addr_t *) &(hwif->dma_status));

	if (!hwif->dma_base2)
		goto dma_base2alloc_failure;

	return;

dma_base2alloc_failure:
	pci_free_consistent(hwif->pci_dev,
			    IOC4_PRD_ENTRIES * IOC4_PRD_BYTES,
			    hwif->dmatable_cpu, hwif->dmatable_dma);
	printk(KERN_INFO
	       "%s() -- Error! Unable to allocate DMA Maps for drive %s\n",
	       __FUNCTION__, hwif->name);
	printk(KERN_INFO
	       "Changing from DMA to PIO mode for Drive %s\n", hwif->name);

dma_alloc_failure:
	/* Disable DMA because we couldnot allocate any DMA maps */
	hwif->autodma = 0;
	hwif->atapi_dma = 0;
}

/* Initializes the IOC4 DMA Engine */
static void
sgiioc4_configure_for_dma(int dma_direction, ide_drive_t * drive)
{
	u32 ioc4_dma;
	ide_hwif_t *hwif = HWIF(drive);
	u64 dma_base = hwif->dma_base;
	u32 dma_addr, ending_dma_addr;

	ioc4_dma = hwif->INL(dma_base + IOC4_DMA_CTRL * 4);

	if (ioc4_dma & IOC4_S_DMA_ACTIVE) {
		printk(KERN_WARNING
			"%s(%s):Warning!! DMA from previous transfer was still active\n",
		       __FUNCTION__, drive->name);
		hwif->OUTL(IOC4_S_DMA_STOP, dma_base + IOC4_DMA_CTRL * 4);
		ioc4_dma = sgiioc4_ide_dma_stop(hwif, dma_base);

		if (ioc4_dma & IOC4_S_DMA_STOP)
			printk(KERN_ERR
			       "%s(%s) : IOC4 Dma STOP bit is still 1\n",
			       __FUNCTION__, drive->name);
	}

	ioc4_dma = hwif->INL(dma_base + IOC4_DMA_CTRL * 4);
	if (ioc4_dma & IOC4_S_DMA_ERROR) {
		printk(KERN_WARNING
		       "%s(%s) : Warning!! - DMA Error during Previous"
		       " transfer | status 0x%x\n",
		       __FUNCTION__, drive->name, ioc4_dma);
		hwif->OUTL(IOC4_S_DMA_STOP, dma_base + IOC4_DMA_CTRL * 4);
		ioc4_dma = sgiioc4_ide_dma_stop(hwif, dma_base);

		if (ioc4_dma & IOC4_S_DMA_STOP)
			printk(KERN_ERR
			       "%s(%s) : IOC4 DMA STOP bit is still 1\n",
			       __FUNCTION__, drive->name);
	}

	/* Address of the Scatter Gather List */
	dma_addr = cpu_to_le32(hwif->dmatable_dma);
	hwif->OUTL(dma_addr, dma_base + IOC4_DMA_PTR_L * 4);

	/* Address of the Ending DMA */
	memset((unsigned int *) hwif->dma_base2, 0, IOC4_IDE_CACHELINE_SIZE);
	ending_dma_addr = cpu_to_le32(hwif->dma_status);
	hwif->OUTL(ending_dma_addr, dma_base + IOC4_DMA_END_ADDR * 4);

	hwif->OUTL(dma_direction, dma_base + IOC4_DMA_CTRL * 4);
	drive->waiting_for_dma = 1;
}

/* IOC4 Scatter Gather list Format 					 */
/* 128 Bit entries to support 64 bit addresses in the future		 */
/* The Scatter Gather list Entry should be in the BIG-ENDIAN Format	 */
/* --------------------------------------------------------------------- */
/* | Upper 32 bits - Zero           |	 	Lower 32 bits- address | */
/* --------------------------------------------------------------------- */
/* | Upper 32 bits - Zero	    |EOL| 15 unused     | 16 Bit Length| */
/* --------------------------------------------------------------------- */
/* Creates the scatter gather list, DMA Table */
static unsigned int
sgiioc4_build_dma_table(ide_drive_t * drive, struct request *rq, int ddir)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int *table = hwif->dmatable_cpu;
	unsigned int count = 0, i = 1;
	struct scatterlist *sg;

	hwif->sg_nents = i = ide_build_sglist(drive, rq);

	if (!i)
		return 0;	/* sglist of length Zero */

	sg = hwif->sg_table;
	while (i && sg_dma_len(sg)) {
		dma_addr_t cur_addr;
		int cur_len;
		cur_addr = sg_dma_address(sg);
		cur_len = sg_dma_len(sg);

		while (cur_len) {
			if (count++ >= IOC4_PRD_ENTRIES) {
				printk(KERN_WARNING
				       "%s: DMA table too small\n",
				       drive->name);
				goto use_pio_instead;
			} else {
				u32 bcount =
				    0x10000 - (cur_addr & 0xffff);

				if (bcount > cur_len)
					bcount = cur_len;

				/* put the addr, length in
				 * the IOC4 dma-table format */
				*table = 0x0;
				table++;
				*table = cpu_to_be32(cur_addr);
				table++;
				*table = 0x0;
				table++;

				*table = cpu_to_be32(bcount);
				table++;

				cur_addr += bcount;
				cur_len -= bcount;
			}
		}

		sg++;
		i--;
	}

	if (count) {
		table--;
		*table |= cpu_to_be32(0x80000000);
		return count;
	}

use_pio_instead:
	pci_unmap_sg(hwif->pci_dev, hwif->sg_table, hwif->sg_nents,
		     hwif->sg_dma_direction);

	return 0;		/* revert to PIO for this request */
}

static int sgiioc4_ide_dma_setup(ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	unsigned int count = 0;
	int ddir;

	if (rq_data_dir(rq))
		ddir = PCI_DMA_TODEVICE;
	else
		ddir = PCI_DMA_FROMDEVICE;

	if (!(count = sgiioc4_build_dma_table(drive, rq, ddir))) {
		/* try PIO instead of DMA */
		ide_map_sg(drive, rq);
		return 1;
	}

	if (rq_data_dir(rq))
		/* Writes TO the IOC4 FROM Main Memory */
		ddir = IOC4_DMA_READ;
	else
		/* Writes FROM the IOC4 TO Main Memory */
		ddir = IOC4_DMA_WRITE;

	sgiioc4_configure_for_dma(ddir, drive);

	return 0;
}

static void __devinit
ide_init_sgiioc4(ide_hwif_t * hwif)
{
	hwif->mmio = 2;
	hwif->autodma = 1;
	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x0;	/* Disable Ultra DMA */
	hwif->mwdma_mask = 0x2;	/* Multimode-2 DMA  */
	hwif->swdma_mask = 0x2;
	hwif->tuneproc = NULL;	/* Sets timing for PIO mode */
	hwif->speedproc = NULL;	/* Sets timing for DMA &/or PIO modes */
	hwif->selectproc = NULL;/* Use the default routine to select drive */
	hwif->reset_poll = NULL;/* No HBA specific reset_poll needed */
	hwif->pre_reset = NULL;	/* No HBA specific pre_set needed */
	hwif->resetproc = &sgiioc4_resetproc;/* Reset DMA engine,
						clear interrupts */
	hwif->intrproc = NULL;	/* Enable or Disable interrupt from drive */
	hwif->maskproc = &sgiioc4_maskproc;	/* Mask on/off NIEN register */
	hwif->quirkproc = NULL;
	hwif->busproc = NULL;

	hwif->dma_setup = &sgiioc4_ide_dma_setup;
	hwif->dma_start = &sgiioc4_ide_dma_start;
	hwif->ide_dma_end = &sgiioc4_ide_dma_end;
	hwif->ide_dma_check = &sgiioc4_ide_dma_check;
	hwif->ide_dma_on = &sgiioc4_ide_dma_on;
	hwif->ide_dma_off_quietly = &sgiioc4_ide_dma_off_quietly;
	hwif->ide_dma_test_irq = &sgiioc4_ide_dma_test_irq;
	hwif->ide_dma_host_on = &sgiioc4_ide_dma_host_on;
	hwif->ide_dma_host_off = &sgiioc4_ide_dma_host_off;
	hwif->ide_dma_lostirq = &sgiioc4_ide_dma_lostirq;
	hwif->ide_dma_timeout = &__ide_dma_timeout;
	hwif->INB = &sgiioc4_INB;
}

static int __devinit
sgiioc4_ide_setup_pci_device(struct pci_dev *dev, ide_pci_device_t * d)
{
	unsigned long base, ctl, dma_base, irqport;
	ide_hwif_t *hwif;
	int h;

	/*
	 * Find an empty HWIF; if none available, return -ENOMEM.
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		hwif = &ide_hwifs[h];
		if (hwif->chipset == ide_unknown)
			break;
	}
	if (h == MAX_HWIFS) {
		printk(KERN_ERR "%s: too many IDE interfaces, no room in table\n", d->name);
		return -ENOMEM;
	}

	/*  Get the CmdBlk and CtrlBlk Base Registers */
	base = pci_resource_start(dev, 0) + IOC4_CMD_OFFSET;
	ctl = pci_resource_start(dev, 0) + IOC4_CTRL_OFFSET;
	irqport = pci_resource_start(dev, 0) + IOC4_INTR_OFFSET;
	dma_base = pci_resource_start(dev, 0) + IOC4_DMA_OFFSET;

	if (!request_region(base, IOC4_CMD_CTL_BLK_SIZE, hwif->name)) {
		printk(KERN_ERR
			"%s : %s -- ERROR, Port Addresses "
			"0x%p to 0x%p ALREADY in use\n",
		       __FUNCTION__, hwif->name, (void *) base,
		       (void *) base + IOC4_CMD_CTL_BLK_SIZE);
		return -ENOMEM;
	}

	if (hwif->io_ports[IDE_DATA_OFFSET] != base) {
		/* Initialize the IO registers */
		sgiioc4_init_hwif_ports(&hwif->hw, base, ctl, irqport);
		memcpy(hwif->io_ports, hwif->hw.io_ports,
		       sizeof (hwif->io_ports));
		hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET];
	}

	hwif->irq = dev->irq;
	hwif->chipset = ide_pci;
	hwif->pci_dev = dev;
	hwif->channel = 0;	/* Single Channel chip */
	hwif->cds = (struct ide_pci_device_s *) d;
	hwif->gendev.parent = &dev->dev;/* setup proper ancestral information */

	/* Initializing chipset IRQ Registers */
	hwif->OUTL(0x03, irqport + IOC4_INTR_SET * 4);

	ide_init_sgiioc4(hwif);

	if (dma_base)
		ide_dma_sgiioc4(hwif, dma_base);
	else
		printk(KERN_INFO "%s: %s Bus-Master DMA disabled\n",
		       hwif->name, d->name);

	if (probe_hwif_init(hwif))
		return -EIO;

	/* Create /proc/ide entries */
	create_proc_ide_interfaces();

	return 0;
}

static unsigned int __devinit
pci_init_sgiioc4(struct pci_dev *dev, ide_pci_device_t * d)
{
	unsigned int class_rev;
	int ret;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	printk(KERN_INFO "%s: IDE controller at PCI slot %s, revision %d\n",
			d->name, pci_name(dev), class_rev);
	if (class_rev < IOC4_SUPPORTED_FIRMWARE_REV) {
		printk(KERN_ERR "Skipping %s IDE controller in slot %s: "
			"firmware is obsolete - please upgrade to revision"
			"46 or higher\n", d->name, pci_name(dev));
		ret = -EAGAIN;
		goto out;
	}
	ret = sgiioc4_ide_setup_pci_device(dev, d);
out:
	return ret;
}

static ide_pci_device_t sgiioc4_chipsets[] __devinitdata = {
	{
	 /* Channel 0 */
	 .name = "SGIIOC4",
	 .init_hwif = ide_init_sgiioc4,
	 .init_dma = ide_dma_sgiioc4,
	 .channels = 1,
	 .autodma = AUTODMA,
	 /* SGI IOC4 doesn't have enablebits. */
	 .bootable = ON_BOARD,
	}
};

int
ioc4_ide_attach_one(struct ioc4_driver_data *idd)
{
	return pci_init_sgiioc4(idd->idd_pdev,
				&sgiioc4_chipsets[idd->idd_pci_id->driver_data]);
}

static struct ioc4_submodule ioc4_ide_submodule = {
	.is_name = "IOC4_ide",
	.is_owner = THIS_MODULE,
	.is_probe = ioc4_ide_attach_one,
/*	.is_remove = ioc4_ide_remove_one,	*/
};

static int __devinit
ioc4_ide_init(void)
{
	return ioc4_register_submodule(&ioc4_ide_submodule);
}

static void __devexit
ioc4_ide_exit(void)
{
	ioc4_unregister_submodule(&ioc4_ide_submodule);
}

module_init(ioc4_ide_init);
module_exit(ioc4_ide_exit);

MODULE_AUTHOR("Aniket Malatpure - Silicon Graphics Inc. (SGI)");
MODULE_DESCRIPTION("IDE PCI driver module for SGI IOC4 Base-IO Card");
MODULE_LICENSE("GPL");
