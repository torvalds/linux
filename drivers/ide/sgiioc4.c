/*
 * Copyright (c) 2003-2006 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 2008-2009 MontaVista Software, Inc.
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
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/ioc4.h>
#include <linux/io.h>
#include <linux/ide.h>

#define DRV_NAME "SGIIOC4"

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

struct ioc4_dma_regs {
	u32 timing_reg0;
	u32 timing_reg1;
	u32 low_mem_ptr;
	u32 high_mem_ptr;
	u32 low_mem_addr;
	u32 high_mem_addr;
	u32 dev_byte_count;
	u32 mem_byte_count;
	u32 status;
};

/* Each Physical Region Descriptor Entry size is 16 bytes (2 * 64 bits) */
/* IOC4 has only 1 IDE channel */
#define IOC4_PRD_BYTES		16
#define IOC4_PRD_ENTRIES	(PAGE_SIZE / (4 * IOC4_PRD_BYTES))


static void sgiioc4_init_hwif_ports(struct ide_hw *hw,
				    unsigned long data_port,
				    unsigned long ctrl_port,
				    unsigned long irq_port)
{
	unsigned long reg = data_port;
	int i;

	/* Registers are word (32 bit) aligned */
	for (i = 0; i <= 7; i++)
		hw->io_ports_array[i] = reg + i * 4;

	hw->io_ports.ctl_addr = ctrl_port;
	hw->io_ports.irq_addr = irq_port;
}

static int sgiioc4_checkirq(ide_hwif_t *hwif)
{
	unsigned long intr_addr = hwif->io_ports.irq_addr + IOC4_INTR_REG * 4;

	if (readl((void __iomem *)intr_addr) & 0x03)
		return 1;

	return 0;
}

static u8 sgiioc4_read_status(ide_hwif_t *);

static int sgiioc4_clearirq(ide_drive_t *drive)
{
	u32 intr_reg;
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	unsigned long other_ir = io_ports->irq_addr + (IOC4_INTR_REG << 2);

	/* Code to check for PCI error conditions */
	intr_reg = readl((void __iomem *)other_ir);
	if (intr_reg & 0x03) { /* Valid IOC4-IDE interrupt */
		/*
		 * Using sgiioc4_read_status to read the Status register has a
		 * side effect of clearing the interrupt.  The first read should
		 * clear it if it is set.  The second read should return
		 * a "clear" status if it got cleared.  If not, then spin
		 * for a bit trying to clear it.
		 */
		u8 stat = sgiioc4_read_status(hwif);
		int count = 0;

		stat = sgiioc4_read_status(hwif);
		while ((stat & ATA_BUSY) && (count++ < 100)) {
			udelay(1);
			stat = sgiioc4_read_status(hwif);
		}

		if (intr_reg & 0x02) {
			struct pci_dev *dev = to_pci_dev(hwif->dev);
			/* Error when transferring DMA data on PCI bus */
			u32 pci_err_addr_low, pci_err_addr_high,
			    pci_stat_cmd_reg;

			pci_err_addr_low =
				readl((void __iomem *)io_ports->irq_addr);
			pci_err_addr_high =
				readl((void __iomem *)(io_ports->irq_addr + 4));
			pci_read_config_dword(dev, PCI_COMMAND,
					      &pci_stat_cmd_reg);
			printk(KERN_ERR "%s(%s): PCI Bus Error when doing DMA: "
			       "status-cmd reg is 0x%x\n",
			       __func__, drive->name, pci_stat_cmd_reg);
			printk(KERN_ERR "%s(%s): PCI Error Address is 0x%x%x\n",
			       __func__, drive->name,
			       pci_err_addr_high, pci_err_addr_low);
			/* Clear the PCI Error indicator */
			pci_write_config_dword(dev, PCI_COMMAND, 0x00000146);
		}

		/* Clear the Interrupt, Error bits on the IOC4 */
		writel(0x03, (void __iomem *)other_ir);

		intr_reg = readl((void __iomem *)other_ir);
	}

	return intr_reg & 3;
}

static void sgiioc4_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned long ioc4_dma_addr = hwif->dma_base + IOC4_DMA_CTRL * 4;
	unsigned int reg = readl((void __iomem *)ioc4_dma_addr);
	unsigned int temp_reg = reg | IOC4_S_DMA_START;

	writel(temp_reg, (void __iomem *)ioc4_dma_addr);
}

static u32 sgiioc4_ide_dma_stop(ide_hwif_t *hwif, u64 dma_base)
{
	unsigned long ioc4_dma_addr = dma_base + IOC4_DMA_CTRL * 4;
	u32	ioc4_dma;
	int	count;

	count = 0;
	ioc4_dma = readl((void __iomem *)ioc4_dma_addr);
	while ((ioc4_dma & IOC4_S_DMA_STOP) && (count++ < 200)) {
		udelay(1);
		ioc4_dma = readl((void __iomem *)ioc4_dma_addr);
	}
	return ioc4_dma;
}

/* Stops the IOC4 DMA Engine */
static int sgiioc4_dma_end(ide_drive_t *drive)
{
	u32 ioc4_dma, bc_dev, bc_mem, num, valid = 0, cnt = 0;
	ide_hwif_t *hwif = drive->hwif;
	unsigned long dma_base = hwif->dma_base;
	int dma_stat = 0;
	unsigned long *ending_dma = ide_get_hwifdata(hwif);

	writel(IOC4_S_DMA_STOP, (void __iomem *)(dma_base + IOC4_DMA_CTRL * 4));

	ioc4_dma = sgiioc4_ide_dma_stop(hwif, dma_base);

	if (ioc4_dma & IOC4_S_DMA_STOP) {
		printk(KERN_ERR
		       "%s(%s): IOC4 DMA STOP bit is still 1 :"
		       "ioc4_dma_reg 0x%x\n",
		       __func__, drive->name, ioc4_dma);
		dma_stat = 1;
	}

	/*
	 * The IOC4 will DMA 1's to the ending DMA area to indicate that
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
		printk(KERN_ERR "%s(%s) : DMA incomplete\n", __func__,
		       drive->name);
		dma_stat = 1;
	}

	bc_dev = readl((void __iomem *)(dma_base + IOC4_BC_DEV * 4));
	bc_mem = readl((void __iomem *)(dma_base + IOC4_BC_MEM * 4));

	if ((bc_dev & 0x01FF) || (bc_mem & 0x1FF)) {
		if (bc_dev > bc_mem + 8) {
			printk(KERN_ERR
			       "%s(%s): WARNING!! byte_count_dev %d "
			       "!= byte_count_mem %d\n",
			       __func__, drive->name, bc_dev, bc_mem);
		}
	}

	return dma_stat;
}

static void sgiioc4_set_dma_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
}

/* Returns 1 if DMA IRQ issued, 0 otherwise */
static int sgiioc4_dma_test_irq(ide_drive_t *drive)
{
	return sgiioc4_checkirq(drive->hwif);
}

static void sgiioc4_dma_host_set(ide_drive_t *drive, int on)
{
	if (!on)
		sgiioc4_clearirq(drive);
}

static void sgiioc4_resetproc(ide_drive_t *drive)
{
	struct ide_cmd *cmd = &drive->hwif->cmd;

	sgiioc4_dma_end(drive);
	ide_dma_unmap_sg(drive, cmd);
	sgiioc4_clearirq(drive);
}

static void sgiioc4_dma_lost_irq(ide_drive_t *drive)
{
	sgiioc4_resetproc(drive);

	ide_dma_lost_irq(drive);
}

static u8 sgiioc4_read_status(ide_hwif_t *hwif)
{
	unsigned long port = hwif->io_ports.status_addr;
	u8 reg = (u8) readb((void __iomem *) port);

	if (!(reg & ATA_BUSY)) {	/* Not busy... check for interrupt */
		unsigned long other_ir = port - 0x110;
		unsigned int intr_reg = (u32) readl((void __iomem *) other_ir);

		/* Clear the Interrupt, Error bits on the IOC4 */
		if (intr_reg & 0x03) {
			writel(0x03, (void __iomem *) other_ir);
			intr_reg = (u32) readl((void __iomem *) other_ir);
		}
	}

	return reg;
}

/* Creates a DMA map for the scatter-gather list entries */
static int __devinit ide_dma_sgiioc4(ide_hwif_t *hwif,
				     const struct ide_port_info *d)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned long dma_base = pci_resource_start(dev, 0) + IOC4_DMA_OFFSET;
	int num_ports = sizeof(struct ioc4_dma_regs);
	void *pad;

	printk(KERN_INFO "    %s: MMIO-DMA\n", hwif->name);

	if (request_mem_region(dma_base, num_ports, hwif->name) == NULL) {
		printk(KERN_ERR "%s(%s) -- ERROR: addresses 0x%08lx to 0x%08lx "
		       "already in use\n", __func__, hwif->name,
		       dma_base, dma_base + num_ports - 1);
		return -1;
	}

	hwif->dma_base = (unsigned long)hwif->io_ports.irq_addr +
			 IOC4_DMA_OFFSET;

	hwif->sg_max_nents = IOC4_PRD_ENTRIES;

	hwif->prd_max_nents = IOC4_PRD_ENTRIES;
	hwif->prd_ent_size = IOC4_PRD_BYTES;

	if (ide_allocate_dma_engine(hwif))
		goto dma_pci_alloc_failure;

	pad = pci_alloc_consistent(dev, IOC4_IDE_CACHELINE_SIZE,
				   (dma_addr_t *)&hwif->extra_base);
	if (pad) {
		ide_set_hwifdata(hwif, pad);
		return 0;
	}

	ide_release_dma_engine(hwif);

	printk(KERN_ERR "%s(%s) -- ERROR: Unable to allocate DMA maps\n",
	       __func__, hwif->name);
	printk(KERN_INFO "%s: changing from DMA to PIO mode", hwif->name);

dma_pci_alloc_failure:
	release_mem_region(dma_base, num_ports);

	return -1;
}

/* Initializes the IOC4 DMA Engine */
static void sgiioc4_configure_for_dma(int dma_direction, ide_drive_t *drive)
{
	u32 ioc4_dma;
	ide_hwif_t *hwif = drive->hwif;
	unsigned long dma_base = hwif->dma_base;
	unsigned long ioc4_dma_addr = dma_base + IOC4_DMA_CTRL * 4;
	u32 dma_addr, ending_dma_addr;

	ioc4_dma = readl((void __iomem *)ioc4_dma_addr);

	if (ioc4_dma & IOC4_S_DMA_ACTIVE) {
		printk(KERN_WARNING "%s(%s): Warning!! DMA from previous "
		       "transfer was still active\n", __func__, drive->name);
		writel(IOC4_S_DMA_STOP, (void __iomem *)ioc4_dma_addr);
		ioc4_dma = sgiioc4_ide_dma_stop(hwif, dma_base);

		if (ioc4_dma & IOC4_S_DMA_STOP)
			printk(KERN_ERR "%s(%s): IOC4 DMA STOP bit is "
			       "still 1\n", __func__, drive->name);
	}

	ioc4_dma = readl((void __iomem *)ioc4_dma_addr);
	if (ioc4_dma & IOC4_S_DMA_ERROR) {
		printk(KERN_WARNING "%s(%s): Warning!! DMA Error during "
		       "previous transfer, status 0x%x\n",
		       __func__, drive->name, ioc4_dma);
		writel(IOC4_S_DMA_STOP, (void __iomem *)ioc4_dma_addr);
		ioc4_dma = sgiioc4_ide_dma_stop(hwif, dma_base);

		if (ioc4_dma & IOC4_S_DMA_STOP)
			printk(KERN_ERR "%s(%s): IOC4 DMA STOP bit is "
			       "still 1\n", __func__, drive->name);
	}

	/* Address of the Scatter Gather List */
	dma_addr = cpu_to_le32(hwif->dmatable_dma);
	writel(dma_addr, (void __iomem *)(dma_base + IOC4_DMA_PTR_L * 4));

	/* Address of the Ending DMA */
	memset(ide_get_hwifdata(hwif), 0, IOC4_IDE_CACHELINE_SIZE);
	ending_dma_addr = cpu_to_le32(hwif->extra_base);
	writel(ending_dma_addr, (void __iomem *)(dma_base +
						 IOC4_DMA_END_ADDR * 4));

	writel(dma_direction, (void __iomem *)ioc4_dma_addr);
}

/* IOC4 Scatter Gather list Format					 */
/* 128 Bit entries to support 64 bit addresses in the future		 */
/* The Scatter Gather list Entry should be in the BIG-ENDIAN Format	 */
/* --------------------------------------------------------------------- */
/* | Upper 32 bits - Zero	    |		Lower 32 bits- address | */
/* --------------------------------------------------------------------- */
/* | Upper 32 bits - Zero	    |EOL| 15 unused     | 16 Bit Length| */
/* --------------------------------------------------------------------- */
/* Creates the scatter gather list, DMA Table				 */

static int sgiioc4_build_dmatable(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned int *table = hwif->dmatable_cpu;
	unsigned int count = 0, i = cmd->sg_nents;
	struct scatterlist *sg = hwif->sg_table;

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
				return 0;
			} else {
				u32 bcount =
				    0x10000 - (cur_addr & 0xffff);

				if (bcount > cur_len)
					bcount = cur_len;

				/*
				 * Put the address, length in
				 * the IOC4 dma-table format
				 */
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

		sg = sg_next(sg);
		i--;
	}

	if (count) {
		table--;
		*table |= cpu_to_be32(0x80000000);
		return count;
	}

	return 0;		/* revert to PIO for this request */
}

static int sgiioc4_dma_setup(ide_drive_t *drive, struct ide_cmd *cmd)
{
	int ddir;
	u8 write = !!(cmd->tf_flags & IDE_TFLAG_WRITE);

	if (sgiioc4_build_dmatable(drive, cmd) == 0)
		/* try PIO instead of DMA */
		return 1;

	if (write)
		/* Writes TO the IOC4 FROM Main Memory */
		ddir = IOC4_DMA_READ;
	else
		/* Writes FROM the IOC4 TO Main Memory */
		ddir = IOC4_DMA_WRITE;

	sgiioc4_configure_for_dma(ddir, drive);

	return 0;
}

static const struct ide_tp_ops sgiioc4_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= sgiioc4_read_status,
	.read_altstatus		= ide_read_altstatus,
	.write_devctl		= ide_write_devctl,

	.dev_select		= ide_dev_select,
	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= ide_input_data,
	.output_data		= ide_output_data,
};

static const struct ide_port_ops sgiioc4_port_ops = {
	.set_dma_mode		= sgiioc4_set_dma_mode,
	/* reset DMA engine, clear IRQs */
	.resetproc		= sgiioc4_resetproc,
};

static const struct ide_dma_ops sgiioc4_dma_ops = {
	.dma_host_set		= sgiioc4_dma_host_set,
	.dma_setup		= sgiioc4_dma_setup,
	.dma_start		= sgiioc4_dma_start,
	.dma_end		= sgiioc4_dma_end,
	.dma_test_irq		= sgiioc4_dma_test_irq,
	.dma_lost_irq		= sgiioc4_dma_lost_irq,
};

static const struct ide_port_info sgiioc4_port_info __devinitconst = {
	.name			= DRV_NAME,
	.chipset		= ide_pci,
	.init_dma		= ide_dma_sgiioc4,
	.tp_ops			= &sgiioc4_tp_ops,
	.port_ops		= &sgiioc4_port_ops,
	.dma_ops		= &sgiioc4_dma_ops,
	.host_flags		= IDE_HFLAG_MMIO,
	.irq_flags		= IRQF_SHARED,
	.mwdma_mask		= ATA_MWDMA2_ONLY,
};

static int __devinit sgiioc4_ide_setup_pci_device(struct pci_dev *dev)
{
	unsigned long cmd_base, irqport;
	unsigned long bar0, cmd_phys_base, ctl;
	void __iomem *virt_base;
	struct ide_hw hw, *hws[] = { &hw };
	int rc;

	/* Get the CmdBlk and CtrlBlk base registers */
	bar0 = pci_resource_start(dev, 0);
	virt_base = pci_ioremap_bar(dev, 0);
	if (virt_base == NULL) {
		printk(KERN_ERR "%s: Unable to remap BAR 0 address: 0x%lx\n",
				DRV_NAME, bar0);
		return -ENOMEM;
	}
	cmd_base = (unsigned long)virt_base + IOC4_CMD_OFFSET;
	ctl = (unsigned long)virt_base + IOC4_CTRL_OFFSET;
	irqport = (unsigned long)virt_base + IOC4_INTR_OFFSET;

	cmd_phys_base = bar0 + IOC4_CMD_OFFSET;
	if (request_mem_region(cmd_phys_base, IOC4_CMD_CTL_BLK_SIZE,
			       DRV_NAME) == NULL) {
		printk(KERN_ERR "%s %s -- ERROR: addresses 0x%08lx to 0x%08lx "
		       "already in use\n", DRV_NAME, pci_name(dev),
		       cmd_phys_base, cmd_phys_base + IOC4_CMD_CTL_BLK_SIZE);
		rc = -EBUSY;
		goto req_mem_rgn_err;
	}

	/* Initialize the IO registers */
	memset(&hw, 0, sizeof(hw));
	sgiioc4_init_hwif_ports(&hw, cmd_base, ctl, irqport);
	hw.irq = dev->irq;
	hw.dev = &dev->dev;

	/* Initialize chipset IRQ registers */
	writel(0x03, (void __iomem *)(irqport + IOC4_INTR_SET * 4));

	rc = ide_host_add(&sgiioc4_port_info, hws, 1, NULL);
	if (!rc)
		return 0;

	release_mem_region(cmd_phys_base, IOC4_CMD_CTL_BLK_SIZE);
req_mem_rgn_err:
	iounmap(virt_base);
	return rc;
}

static unsigned int __devinit pci_init_sgiioc4(struct pci_dev *dev)
{
	int ret;

	printk(KERN_INFO "%s: IDE controller at PCI slot %s, revision %d\n",
			 DRV_NAME, pci_name(dev), dev->revision);

	if (dev->revision < IOC4_SUPPORTED_FIRMWARE_REV) {
		printk(KERN_ERR "Skipping %s IDE controller in slot %s: "
				"firmware is obsolete - please upgrade to "
				"revision46 or higher\n",
				DRV_NAME, pci_name(dev));
		ret = -EAGAIN;
		goto out;
	}
	ret = sgiioc4_ide_setup_pci_device(dev);
out:
	return ret;
}

int __devinit ioc4_ide_attach_one(struct ioc4_driver_data *idd)
{
	/*
	 * PCI-RT does not bring out IDE connection.
	 * Do not attach to this particular IOC4.
	 */
	if (idd->idd_variant == IOC4_VARIANT_PCI_RT)
		return 0;

	return pci_init_sgiioc4(idd->idd_pdev);
}

static struct ioc4_submodule __devinitdata ioc4_ide_submodule = {
	.is_name = "IOC4_ide",
	.is_owner = THIS_MODULE,
	.is_probe = ioc4_ide_attach_one,
};

static int __init ioc4_ide_init(void)
{
	return ioc4_register_submodule(&ioc4_ide_submodule);
}

late_initcall(ioc4_ide_init); /* Call only after IDE init is done */

MODULE_AUTHOR("Aniket Malatpure/Jeremy Higdon");
MODULE_DESCRIPTION("IDE PCI driver module for SGI IOC4 Base-IO Card");
MODULE_LICENSE("GPL");
