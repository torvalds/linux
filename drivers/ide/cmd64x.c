/*
 * cmd64x.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998		Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998		David S. Miller (davem@redhat.com)
 *
 * Copyright (C) 1999-2002	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2007		MontaVista Software, Inc. <source@mvista.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#define DRV_NAME "cmd64x"

#define CMD_DEBUG 0

#if CMD_DEBUG
#define cmdprintk(x...)	printk(x)
#else
#define cmdprintk(x...)
#endif

/*
 * CMD64x specific registers definition.
 */
#define CFR		0x50
#define   CFR_INTR_CH0		0x04

#define	CMDTIM		0x52
#define	ARTTIM0		0x53
#define	DRWTIM0		0x54
#define ARTTIM1 	0x55
#define DRWTIM1		0x56
#define ARTTIM23	0x57
#define   ARTTIM23_DIS_RA2	0x04
#define   ARTTIM23_DIS_RA3	0x08
#define   ARTTIM23_INTR_CH1	0x10
#define DRWTIM2		0x58
#define BRST		0x59
#define DRWTIM3		0x5b

#define BMIDECR0	0x70
#define MRDMODE		0x71
#define   MRDMODE_INTR_CH0	0x04
#define   MRDMODE_INTR_CH1	0x08
#define UDIDETCR0	0x73
#define DTPR0		0x74
#define BMIDECR1	0x78
#define BMIDECSR	0x79
#define UDIDETCR1	0x7B
#define DTPR1		0x7C

static u8 quantize_timing(int timing, int quant)
{
	return (timing + quant - 1) / quant;
}

/*
 * This routine calculates active/recovery counts and then writes them into
 * the chipset registers.
 */
static void program_cycle_times (ide_drive_t *drive, int cycle_time, int active_time)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	int clock_time = 1000 / (ide_pci_clk ? ide_pci_clk : 33);
	u8  cycle_count, active_count, recovery_count, drwtim;
	static const u8 recovery_values[] =
		{15, 15, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0};
	static const u8 drwtim_regs[4] = {DRWTIM0, DRWTIM1, DRWTIM2, DRWTIM3};

	cmdprintk("program_cycle_times parameters: total=%d, active=%d\n",
		  cycle_time, active_time);

	cycle_count	= quantize_timing( cycle_time, clock_time);
	active_count	= quantize_timing(active_time, clock_time);
	recovery_count	= cycle_count - active_count;

	/*
	 * In case we've got too long recovery phase, try to lengthen
	 * the active phase
	 */
	if (recovery_count > 16) {
		active_count += recovery_count - 16;
		recovery_count = 16;
	}
	if (active_count > 16)		/* shouldn't actually happen... */
	 	active_count = 16;

	cmdprintk("Final counts: total=%d, active=%d, recovery=%d\n",
		  cycle_count, active_count, recovery_count);

	/*
	 * Convert values to internal chipset representation
	 */
	recovery_count = recovery_values[recovery_count];
 	active_count  &= 0x0f;

	/* Program the active/recovery counts into the DRWTIM register */
	drwtim = (active_count << 4) | recovery_count;
	(void) pci_write_config_byte(dev, drwtim_regs[drive->dn], drwtim);
	cmdprintk("Write 0x%02x to reg 0x%x\n", drwtim, drwtim_regs[drive->dn]);
}

/*
 * This routine writes into the chipset registers
 * PIO setup/active/recovery timings.
 */
static void cmd64x_tune_pio(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	struct ide_timing *t	= ide_timing_find_mode(XFER_PIO_0 + pio);
	unsigned int cycle_time;
	u8 setup_count, arttim = 0;

	static const u8 setup_values[] = {0x40, 0x40, 0x40, 0x80, 0, 0xc0};
	static const u8 arttim_regs[4] = {ARTTIM0, ARTTIM1, ARTTIM23, ARTTIM23};

	cycle_time = ide_pio_cycle_time(drive, pio);

	program_cycle_times(drive, cycle_time, t->active);

	setup_count = quantize_timing(t->setup,
			1000 / (ide_pci_clk ? ide_pci_clk : 33));

	/*
	 * The primary channel has individual address setup timing registers
	 * for each drive and the hardware selects the slowest timing itself.
	 * The secondary channel has one common register and we have to select
	 * the slowest address setup timing ourselves.
	 */
	if (hwif->channel) {
		ide_drive_t *pair = ide_get_pair_dev(drive);

		drive->drive_data = setup_count;

		if (pair)
			setup_count = max_t(u8, setup_count, pair->drive_data);
	}

	if (setup_count > 5)		/* shouldn't actually happen... */
		setup_count = 5;
	cmdprintk("Final address setup count: %d\n", setup_count);

	/*
	 * Program the address setup clocks into the ARTTIM registers.
	 * Avoid clearing the secondary channel's interrupt bit.
	 */
	(void) pci_read_config_byte (dev, arttim_regs[drive->dn], &arttim);
	if (hwif->channel)
		arttim &= ~ARTTIM23_INTR_CH1;
	arttim &= ~0xc0;
	arttim |= setup_values[setup_count];
	(void) pci_write_config_byte(dev, arttim_regs[drive->dn], arttim);
	cmdprintk("Write 0x%02x to reg 0x%x\n", arttim, arttim_regs[drive->dn]);
}

/*
 * Attempts to set drive's PIO mode.
 * Special cases are 8: prefetch off, 9: prefetch on (both never worked)
 */

static void cmd64x_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	/*
	 * Filter out the prefetch control values
	 * to prevent PIO5 from being programmed
	 */
	if (pio == 8 || pio == 9)
		return;

	cmd64x_tune_pio(drive, pio);
}

static void cmd64x_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u8 unit			= drive->dn & 0x01;
	u8 regU = 0, pciU	= hwif->channel ? UDIDETCR1 : UDIDETCR0;

	if (speed >= XFER_SW_DMA_0) {
		(void) pci_read_config_byte(dev, pciU, &regU);
		regU &= ~(unit ? 0xCA : 0x35);
	}

	switch(speed) {
	case XFER_UDMA_5:
		regU |= unit ? 0x0A : 0x05;
		break;
	case XFER_UDMA_4:
		regU |= unit ? 0x4A : 0x15;
		break;
	case XFER_UDMA_3:
		regU |= unit ? 0x8A : 0x25;
		break;
	case XFER_UDMA_2:
		regU |= unit ? 0x42 : 0x11;
		break;
	case XFER_UDMA_1:
		regU |= unit ? 0x82 : 0x21;
		break;
	case XFER_UDMA_0:
		regU |= unit ? 0xC2 : 0x31;
		break;
	case XFER_MW_DMA_2:
		program_cycle_times(drive, 120, 70);
		break;
	case XFER_MW_DMA_1:
		program_cycle_times(drive, 150, 80);
		break;
	case XFER_MW_DMA_0:
		program_cycle_times(drive, 480, 215);
		break;
	}

	if (speed >= XFER_SW_DMA_0)
		(void) pci_write_config_byte(dev, pciU, regU);
}

static int cmd648_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	unsigned long base	= hwif->dma_base - (hwif->channel * 8);
	int err			= ide_dma_end(drive);
	u8  irq_mask		= hwif->channel ? MRDMODE_INTR_CH1 :
						  MRDMODE_INTR_CH0;
	u8  mrdmode		= inb(base + 1);

	/* clear the interrupt bit */
	outb((mrdmode & ~(MRDMODE_INTR_CH0 | MRDMODE_INTR_CH1)) | irq_mask,
	     base + 1);

	return err;
}

static int cmd64x_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	int irq_reg		= hwif->channel ? ARTTIM23 : CFR;
	u8  irq_mask		= hwif->channel ? ARTTIM23_INTR_CH1 :
						  CFR_INTR_CH0;
	u8  irq_stat		= 0;
	int err			= ide_dma_end(drive);

	(void) pci_read_config_byte(dev, irq_reg, &irq_stat);
	/* clear the interrupt bit */
	(void) pci_write_config_byte(dev, irq_reg, irq_stat | irq_mask);

	return err;
}

static int cmd648_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	unsigned long base	= hwif->dma_base - (hwif->channel * 8);
	u8 irq_mask		= hwif->channel ? MRDMODE_INTR_CH1 :
						  MRDMODE_INTR_CH0;
	u8 dma_stat		= inb(hwif->dma_base + ATA_DMA_STATUS);
	u8 mrdmode		= inb(base + 1);

#ifdef DEBUG
	printk("%s: dma_stat: 0x%02x mrdmode: 0x%02x irq_mask: 0x%02x\n",
	       drive->name, dma_stat, mrdmode, irq_mask);
#endif
	if (!(mrdmode & irq_mask))
		return 0;

	/* return 1 if INTR asserted */
	if (dma_stat & 4)
		return 1;

	return 0;
}

static int cmd64x_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	int irq_reg		= hwif->channel ? ARTTIM23 : CFR;
	u8  irq_mask		= hwif->channel ? ARTTIM23_INTR_CH1 :
						  CFR_INTR_CH0;
	u8  dma_stat		= inb(hwif->dma_base + ATA_DMA_STATUS);
	u8  irq_stat		= 0;

	(void) pci_read_config_byte(dev, irq_reg, &irq_stat);

#ifdef DEBUG
	printk("%s: dma_stat: 0x%02x irq_stat: 0x%02x irq_mask: 0x%02x\n",
	       drive->name, dma_stat, irq_stat, irq_mask);
#endif
	if (!(irq_stat & irq_mask))
		return 0;

	/* return 1 if INTR asserted */
	if (dma_stat & 4)
		return 1;

	return 0;
}

/*
 * ASUS P55T2P4D with CMD646 chipset revision 0x01 requires the old
 * event order for DMA transfers.
 */

static int cmd646_1_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 dma_stat = 0, dma_cmd = 0;

	drive->waiting_for_dma = 0;
	/* get DMA status */
	dma_stat = inb(hwif->dma_base + ATA_DMA_STATUS);
	/* read DMA command state */
	dma_cmd = inb(hwif->dma_base + ATA_DMA_CMD);
	/* stop DMA */
	outb(dma_cmd & ~1, hwif->dma_base + ATA_DMA_CMD);
	/* clear the INTR & ERROR bits */
	outb(dma_stat | 6, hwif->dma_base + ATA_DMA_STATUS);
	/* and free any DMA resources */
	ide_destroy_dmatable(drive);
	/* verify good DMA status */
	return (dma_stat & 7) != 4;
}

static unsigned int init_chipset_cmd64x(struct pci_dev *dev)
{
	u8 mrdmode = 0;

	/* Set a good latency timer and cache line size value. */
	(void) pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	/* FIXME: pci_set_master() to ensure a good latency timer value */

	/*
	 * Enable interrupts, select MEMORY READ LINE for reads.
	 *
	 * NOTE: although not mentioned in the PCI0646U specs,
	 * bits 0-1 are write only and won't be read back as
	 * set or not -- PCI0646U2 specs clarify this point.
	 */
	(void) pci_read_config_byte (dev, MRDMODE, &mrdmode);
	mrdmode &= ~0x30;
	(void) pci_write_config_byte(dev, MRDMODE, (mrdmode | 0x02));

	return 0;
}

static u8 cmd64x_cable_detect(ide_hwif_t *hwif)
{
	struct pci_dev  *dev	= to_pci_dev(hwif->dev);
	u8 bmidecsr = 0, mask	= hwif->channel ? 0x02 : 0x01;

	switch (dev->device) {
	case PCI_DEVICE_ID_CMD_648:
	case PCI_DEVICE_ID_CMD_649:
 		pci_read_config_byte(dev, BMIDECSR, &bmidecsr);
		return (bmidecsr & mask) ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
	default:
		return ATA_CBL_PATA40;
	}
}

static const struct ide_port_ops cmd64x_port_ops = {
	.set_pio_mode		= cmd64x_set_pio_mode,
	.set_dma_mode		= cmd64x_set_dma_mode,
	.cable_detect		= cmd64x_cable_detect,
};

static const struct ide_dma_ops cmd64x_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= cmd64x_dma_end,
	.dma_test_irq		= cmd64x_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timeout		= ide_dma_timeout,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

static const struct ide_dma_ops cmd646_rev1_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= cmd646_1_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timeout		= ide_dma_timeout,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

static const struct ide_dma_ops cmd648_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= cmd648_dma_end,
	.dma_test_irq		= cmd648_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timeout		= ide_dma_timeout,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

static const struct ide_port_info cmd64x_chipsets[] __devinitdata = {
	{	/* 0: CMD643 */
		.name		= DRV_NAME,
		.init_chipset	= init_chipset_cmd64x,
		.enablebits	= {{0x00,0x00,0x00}, {0x51,0x08,0x08}},
		.port_ops	= &cmd64x_port_ops,
		.dma_ops	= &cmd64x_dma_ops,
		.host_flags	= IDE_HFLAG_CLEAR_SIMPLEX |
				  IDE_HFLAG_ABUSE_PREFETCH,
		.pio_mask	= ATA_PIO5,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= 0x00, /* no udma */
	},
	{	/* 1: CMD646 */
		.name		= DRV_NAME,
		.init_chipset	= init_chipset_cmd64x,
		.enablebits	= {{0x51,0x04,0x04}, {0x51,0x08,0x08}},
		.port_ops	= &cmd64x_port_ops,
		.dma_ops	= &cmd648_dma_ops,
		.host_flags	= IDE_HFLAG_SERIALIZE |
				  IDE_HFLAG_ABUSE_PREFETCH,
		.pio_mask	= ATA_PIO5,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA2,
	},
	{	/* 2: CMD648 */
		.name		= DRV_NAME,
		.init_chipset	= init_chipset_cmd64x,
		.enablebits	= {{0x51,0x04,0x04}, {0x51,0x08,0x08}},
		.port_ops	= &cmd64x_port_ops,
		.dma_ops	= &cmd648_dma_ops,
		.host_flags	= IDE_HFLAG_ABUSE_PREFETCH,
		.pio_mask	= ATA_PIO5,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA4,
	},
	{	/* 3: CMD649 */
		.name		= DRV_NAME,
		.init_chipset	= init_chipset_cmd64x,
		.enablebits	= {{0x51,0x04,0x04}, {0x51,0x08,0x08}},
		.port_ops	= &cmd64x_port_ops,
		.dma_ops	= &cmd648_dma_ops,
		.host_flags	= IDE_HFLAG_ABUSE_PREFETCH,
		.pio_mask	= ATA_PIO5,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
	}
};

static int __devinit cmd64x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct ide_port_info d;
	u8 idx = id->driver_data;

	d = cmd64x_chipsets[idx];

	if (idx == 1) {
		/*
		 * UltraDMA only supported on PCI646U and PCI646U2, which
		 * correspond to revisions 0x03, 0x05 and 0x07 respectively.
		 * Actually, although the CMD tech support people won't
		 * tell me the details, the 0x03 revision cannot support
		 * UDMA correctly without hardware modifications, and even
		 * then it only works with Quantum disks due to some
		 * hold time assumptions in the 646U part which are fixed
		 * in the 646U2.
		 *
		 * So we only do UltraDMA on revision 0x05 and 0x07 chipsets.
		 */
		if (dev->revision < 5) {
			d.udma_mask = 0x00;
			/*
			 * The original PCI0646 didn't have the primary
			 * channel enable bit, it appeared starting with
			 * PCI0646U (i.e. revision ID 3).
			 */
			if (dev->revision < 3) {
				d.enablebits[0].reg = 0;
				if (dev->revision == 1)
					d.dma_ops = &cmd646_rev1_dma_ops;
				else
					d.dma_ops = &cmd64x_dma_ops;
			}
		}
	}

	return ide_pci_init_one(dev, &d, NULL);
}

static const struct pci_device_id cmd64x_pci_tbl[] = {
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_643), 0 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_646), 1 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_648), 2 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_649), 3 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, cmd64x_pci_tbl);

static struct pci_driver cmd64x_pci_driver = {
	.name		= "CMD64x_IDE",
	.id_table	= cmd64x_pci_tbl,
	.probe		= cmd64x_init_one,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init cmd64x_ide_init(void)
{
	return ide_pci_register_driver(&cmd64x_pci_driver);
}

static void __exit cmd64x_ide_exit(void)
{
	pci_unregister_driver(&cmd64x_pci_driver);
}

module_init(cmd64x_ide_init);
module_exit(cmd64x_ide_exit);

MODULE_AUTHOR("Eddie Dost, David Miller, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for CMD64x IDE");
MODULE_LICENSE("GPL");
