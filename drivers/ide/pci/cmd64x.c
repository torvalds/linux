/*
 * linux/drivers/ide/pci/cmd64x.c		Version 1.50	May 10, 2007
 *
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
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#define DISPLAY_CMD64X_TIMINGS

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
#define CNTRL		0x51
#define   CNTRL_ENA_1ST 	0x04
#define   CNTRL_ENA_2ND 	0x08
#define   CNTRL_DIS_RA0 	0x40
#define   CNTRL_DIS_RA1 	0x80

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
#define   MRDMODE_BLK_CH0	0x10
#define   MRDMODE_BLK_CH1	0x20
#define BMIDESR0	0x72
#define UDIDETCR0	0x73
#define DTPR0		0x74
#define BMIDECR1	0x78
#define BMIDECSR	0x79
#define BMIDESR1	0x7A
#define UDIDETCR1	0x7B
#define DTPR1		0x7C

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_IDE_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 cmd64x_proc = 0;

#define CMD_MAX_DEVS		5

static struct pci_dev *cmd_devs[CMD_MAX_DEVS];
static int n_cmd_devs;

static char * print_cmd64x_get_info (char *buf, struct pci_dev *dev, int index)
{
	char *p = buf;
	u8 reg72 = 0, reg73 = 0;			/* primary */
	u8 reg7a = 0, reg7b = 0;			/* secondary */
	u8 reg50 = 1, reg51 = 1, reg57 = 0, reg71 = 0;	/* extra */

	p += sprintf(p, "\nController: %d\n", index);
	p += sprintf(p, "PCI-%x Chipset.\n", dev->device);

	(void) pci_read_config_byte(dev, CFR,       &reg50);
	(void) pci_read_config_byte(dev, CNTRL,     &reg51);
	(void) pci_read_config_byte(dev, ARTTIM23,  &reg57);
	(void) pci_read_config_byte(dev, MRDMODE,   &reg71);
	(void) pci_read_config_byte(dev, BMIDESR0,  &reg72);
	(void) pci_read_config_byte(dev, UDIDETCR0, &reg73);
	(void) pci_read_config_byte(dev, BMIDESR1,  &reg7a);
	(void) pci_read_config_byte(dev, UDIDETCR1, &reg7b);

	/* PCI0643/6 originally didn't have the primary channel enable bit */
	if ((dev->device == PCI_DEVICE_ID_CMD_643) ||
	    (dev->device == PCI_DEVICE_ID_CMD_646 && dev->revision < 3))
		reg51 |= CNTRL_ENA_1ST;

	p += sprintf(p, "---------------- Primary Channel "
			"---------------- Secondary Channel ------------\n");
	p += sprintf(p, "                 %s                         %s\n",
		 (reg51 & CNTRL_ENA_1ST) ? "enabled " : "disabled",
		 (reg51 & CNTRL_ENA_2ND) ? "enabled " : "disabled");
	p += sprintf(p, "---------------- drive0 --------- drive1 "
			"-------- drive0 --------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:     %s              %s"
			"             %s              %s\n",
		(reg72 & 0x20) ? "yes" : "no ", (reg72 & 0x40) ? "yes" : "no ",
		(reg7a & 0x20) ? "yes" : "no ", (reg7a & 0x40) ? "yes" : "no ");
	p += sprintf(p, "UltraDMA mode:   %s (%c)          %s (%c)",
		( reg73 & 0x01) ? " on" : "off",
		((reg73 & 0x30) == 0x30) ? ((reg73 & 0x04) ? '3' : '0') :
		((reg73 & 0x30) == 0x20) ? ((reg73 & 0x04) ? '3' : '1') :
		((reg73 & 0x30) == 0x10) ? ((reg73 & 0x04) ? '4' : '2') :
		((reg73 & 0x30) == 0x00) ? ((reg73 & 0x04) ? '5' : '2') : '?',
		( reg73 & 0x02) ? " on" : "off",
		((reg73 & 0xC0) == 0xC0) ? ((reg73 & 0x08) ? '3' : '0') :
		((reg73 & 0xC0) == 0x80) ? ((reg73 & 0x08) ? '3' : '1') :
		((reg73 & 0xC0) == 0x40) ? ((reg73 & 0x08) ? '4' : '2') :
		((reg73 & 0xC0) == 0x00) ? ((reg73 & 0x08) ? '5' : '2') : '?');
	p += sprintf(p, "         %s (%c)          %s (%c)\n",
		( reg7b & 0x01) ? " on" : "off",
		((reg7b & 0x30) == 0x30) ? ((reg7b & 0x04) ? '3' : '0') :
		((reg7b & 0x30) == 0x20) ? ((reg7b & 0x04) ? '3' : '1') :
		((reg7b & 0x30) == 0x10) ? ((reg7b & 0x04) ? '4' : '2') :
		((reg7b & 0x30) == 0x00) ? ((reg7b & 0x04) ? '5' : '2') : '?',
		( reg7b & 0x02) ? " on" : "off",
		((reg7b & 0xC0) == 0xC0) ? ((reg7b & 0x08) ? '3' : '0') :
		((reg7b & 0xC0) == 0x80) ? ((reg7b & 0x08) ? '3' : '1') :
		((reg7b & 0xC0) == 0x40) ? ((reg7b & 0x08) ? '4' : '2') :
		((reg7b & 0xC0) == 0x00) ? ((reg7b & 0x08) ? '5' : '2') : '?');
	p += sprintf(p, "Interrupt:       %s, %s                 %s, %s\n",
		(reg71 & MRDMODE_BLK_CH0  ) ? "blocked" : "enabled",
		(reg50 & CFR_INTR_CH0	  ) ? "pending" : "clear  ",
		(reg71 & MRDMODE_BLK_CH1  ) ? "blocked" : "enabled",
		(reg57 & ARTTIM23_INTR_CH1) ? "pending" : "clear  ");

	return (char *)p;
}

static int cmd64x_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int i;

	for (i = 0; i < n_cmd_devs; i++) {
		struct pci_dev *dev	= cmd_devs[i];
		p = print_cmd64x_get_info(p, dev, i);
	}
	return p-buffer;	/* => must be less than 4k! */
}

#endif	/* defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_IDE_PROC_FS) */

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
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	int clock_time		= 1000 / system_bus_clock();
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
 * This routine selects drive's best PIO mode and writes into the chipset
 * registers setup/active/recovery timings.
 */
static u8 cmd64x_tune_pio (ide_drive_t *drive, u8 mode_wanted)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int cycle_time;
	u8 pio_mode, setup_count, arttim = 0;
	static const u8 setup_values[] = {0x40, 0x40, 0x40, 0x80, 0, 0xc0};
	static const u8 arttim_regs[4] = {ARTTIM0, ARTTIM1, ARTTIM23, ARTTIM23};

	pio_mode = ide_get_best_pio_mode(drive, mode_wanted, 5);
	cycle_time = ide_pio_cycle_time(drive, pio_mode);

	cmdprintk("%s: PIO mode wanted %d, selected %d (%d ns)\n",
		  drive->name, mode_wanted, pio_mode, cycle_time);

	program_cycle_times(drive, cycle_time,
			    ide_pio_timings[pio_mode].active_time);

	setup_count = quantize_timing(ide_pio_timings[pio_mode].setup_time,
				      1000 / system_bus_clock());

	/*
	 * The primary channel has individual address setup timing registers
	 * for each drive and the hardware selects the slowest timing itself.
	 * The secondary channel has one common register and we have to select
	 * the slowest address setup timing ourselves.
	 */
	if (hwif->channel) {
		ide_drive_t *drives = hwif->drives;

		drive->drive_data = setup_count;
		setup_count = max(drives[0].drive_data, drives[1].drive_data);
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

	return pio_mode;
}

/*
 * Attempts to set drive's PIO mode.
 * Special cases are 8: prefetch off, 9: prefetch on (both never worked),
 * and 255: auto-select best mode (used at boot time).
 */
static void cmd64x_tune_drive (ide_drive_t *drive, u8 pio)
{
	/*
	 * Filter out the prefetch control values
	 * to prevent PIO5 from being programmed
	 */
	if (pio == 8 || pio == 9)
		return;

	pio = cmd64x_tune_pio(drive, pio);
	(void) ide_config_drive_speed(drive, XFER_PIO_0 + pio);
}

static int cmd64x_tune_chipset (ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 unit			= drive->dn & 0x01;
	u8 regU = 0, pciU	= hwif->channel ? UDIDETCR1 : UDIDETCR0;

	speed = ide_rate_filter(drive, speed);

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
	case XFER_PIO_5:
	case XFER_PIO_4:
	case XFER_PIO_3:
	case XFER_PIO_2:
	case XFER_PIO_1:
	case XFER_PIO_0:
		(void) cmd64x_tune_pio(drive, speed - XFER_PIO_0);
		break;
	default:
		return 1;
	}

	if (speed >= XFER_SW_DMA_0)
		(void) pci_write_config_byte(dev, pciU, regU);

	return ide_config_drive_speed(drive, speed);
}

static int cmd64x_config_drive_for_dma (ide_drive_t *drive)
{
	if (ide_tune_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive))
		cmd64x_tune_drive(drive, 255);

	return -1;
}

static int cmd648_ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	int err			= __ide_dma_end(drive);
	u8  irq_mask		= hwif->channel ? MRDMODE_INTR_CH1 :
						  MRDMODE_INTR_CH0;
	u8  mrdmode		= inb(hwif->dma_master + 0x01);

	/* clear the interrupt bit */
	outb(mrdmode | irq_mask, hwif->dma_master + 0x01);

	return err;
}

static int cmd64x_ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int irq_reg		= hwif->channel ? ARTTIM23 : CFR;
	u8  irq_mask		= hwif->channel ? ARTTIM23_INTR_CH1 :
						  CFR_INTR_CH0;
	u8  irq_stat		= 0;
	int err			= __ide_dma_end(drive);

	(void) pci_read_config_byte(dev, irq_reg, &irq_stat);
	/* clear the interrupt bit */
	(void) pci_write_config_byte(dev, irq_reg, irq_stat | irq_mask);

	return err;
}

static int cmd648_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 irq_mask		= hwif->channel ? MRDMODE_INTR_CH1 :
						  MRDMODE_INTR_CH0;
	u8 dma_stat		= inb(hwif->dma_status);
	u8 mrdmode		= inb(hwif->dma_master + 0x01);

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

static int cmd64x_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int irq_reg		= hwif->channel ? ARTTIM23 : CFR;
	u8  irq_mask		= hwif->channel ? ARTTIM23_INTR_CH1 :
						  CFR_INTR_CH0;
	u8  dma_stat		= inb(hwif->dma_status);
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

static int cmd646_1_ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 dma_stat = 0, dma_cmd = 0;

	drive->waiting_for_dma = 0;
	/* get DMA status */
	dma_stat = inb(hwif->dma_status);
	/* read DMA command state */
	dma_cmd = inb(hwif->dma_command);
	/* stop DMA */
	outb(dma_cmd & ~1, hwif->dma_command);
	/* clear the INTR & ERROR bits */
	outb(dma_stat | 6, hwif->dma_status);
	/* and free any DMA resources */
	ide_destroy_dmatable(drive);
	/* verify good DMA status */
	return (dma_stat & 7) != 4;
}

static unsigned int __devinit init_chipset_cmd64x(struct pci_dev *dev, const char *name)
{
	u8 mrdmode = 0;

	if (dev->device == PCI_DEVICE_ID_CMD_646) {
		u8 rev = 0;

		pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

		switch (rev) {
		case 0x07:
		case 0x05:
			printk("%s: UltraDMA capable\n", name);
			break;
		case 0x03:
		default:
			printk("%s: MultiWord DMA force limited\n", name);
			break;
		case 0x01:
			printk("%s: MultiWord DMA limited, "
			       "IRQ workaround enabled\n", name);
			break;
		}
	}

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

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_IDE_PROC_FS)

	cmd_devs[n_cmd_devs++] = dev;

	if (!cmd64x_proc) {
		cmd64x_proc = 1;
		ide_pci_create_host_proc("cmd64x", cmd64x_get_info);
	}
#endif /* DISPLAY_CMD64X_TIMINGS && CONFIG_IDE_PROC_FS */

	return 0;
}

static u8 __devinit ata66_cmd64x(ide_hwif_t *hwif)
{
	struct pci_dev  *dev	= hwif->pci_dev;
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

static void __devinit init_hwif_cmd64x(ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	u8 rev			= 0;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	hwif->tuneproc  = &cmd64x_tune_drive;
	hwif->speedproc = &cmd64x_tune_chipset;

	hwif->drives[0].autotune = hwif->drives[1].autotune = 1;

	if (!hwif->dma_base)
		return;

	hwif->atapi_dma  = 1;
	hwif->mwdma_mask = 0x07;
	hwif->ultra_mask = hwif->cds->udma_mask;

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
	if (dev->device == PCI_DEVICE_ID_CMD_646 && rev < 5)
		hwif->ultra_mask = 0x00;

	hwif->ide_dma_check = &cmd64x_config_drive_for_dma;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		hwif->cbl = ata66_cmd64x(hwif);

	switch (dev->device) {
	case PCI_DEVICE_ID_CMD_648:
	case PCI_DEVICE_ID_CMD_649:
	alt_irq_bits:
		hwif->ide_dma_end	= &cmd648_ide_dma_end;
		hwif->ide_dma_test_irq	= &cmd648_ide_dma_test_irq;
		break;
	case PCI_DEVICE_ID_CMD_646:
		hwif->chipset = ide_cmd646;
		if (rev == 0x01) {
			hwif->ide_dma_end = &cmd646_1_ide_dma_end;
			break;
		} else if (rev >= 0x03)
			goto alt_irq_bits;
		/* fall thru */
	default:
		hwif->ide_dma_end	= &cmd64x_ide_dma_end;
		hwif->ide_dma_test_irq	= &cmd64x_ide_dma_test_irq;
		break;
	}

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->drives[1].autodma = hwif->autodma;
}

static int __devinit init_setup_cmd64x(struct pci_dev *dev, ide_pci_device_t *d)
{
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_cmd646(struct pci_dev *dev, ide_pci_device_t *d)
{
	/*
	 * The original PCI0646 didn't have the primary channel enable bit,
	 * it appeared starting with PCI0646U (i.e. revision ID 3).
	 */
	if (dev->revision < 3)
		d->enablebits[0].reg = 0;

	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t cmd64x_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "CMD643",
		.init_setup	= init_setup_cmd64x,
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.autodma	= AUTODMA,
		.enablebits	= {{0x00,0x00,0x00}, {0x51,0x08,0x08}},
		.bootable	= ON_BOARD,
		.pio_mask	= ATA_PIO5,
		.udma_mask	= 0x00, /* no udma */
	},{	/* 1 */
		.name		= "CMD646",
		.init_setup	= init_setup_cmd646,
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.autodma	= AUTODMA,
		.enablebits	= {{0x51,0x04,0x04}, {0x51,0x08,0x08}},
		.bootable	= ON_BOARD,
		.pio_mask	= ATA_PIO5,
		.udma_mask	= 0x07, /* udma0-2 */
	},{	/* 2 */
		.name		= "CMD648",
		.init_setup	= init_setup_cmd64x,
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.autodma	= AUTODMA,
		.enablebits	= {{0x51,0x04,0x04}, {0x51,0x08,0x08}},
		.bootable	= ON_BOARD,
		.pio_mask	= ATA_PIO5,
		.udma_mask	= 0x1f, /* udma0-4 */
	},{	/* 3 */
		.name		= "CMD649",
		.init_setup	= init_setup_cmd64x,
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.autodma	= AUTODMA,
		.enablebits	= {{0x51,0x04,0x04}, {0x51,0x08,0x08}},
		.bootable	= ON_BOARD,
		.pio_mask	= ATA_PIO5,
		.udma_mask	= 0x3f, /* udma0-5 */
	}
};

/*
 * We may have to modify enablebits for PCI0646, so we'd better pass
 * a local copy of the ide_pci_device_t structure down the call chain...
 */
static int __devinit cmd64x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t d = cmd64x_chipsets[id->driver_data];

	return d.init_setup(dev, &d);
}

static struct pci_device_id cmd64x_pci_tbl[] = {
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_643, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_646, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_648, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_CMD_649, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, cmd64x_pci_tbl);

static struct pci_driver driver = {
	.name		= "CMD64x_IDE",
	.id_table	= cmd64x_pci_tbl,
	.probe		= cmd64x_init_one,
};

static int __init cmd64x_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(cmd64x_ide_init);

MODULE_AUTHOR("Eddie Dost, David Miller, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for CMD64x IDE");
MODULE_LICENSE("GPL");
