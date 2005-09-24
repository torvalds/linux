/* $Id: cmd64x.c,v 1.21 2000/01/30 23:23:16
 *
 * linux/drivers/ide/pci/cmd64x.c		Version 1.30	Sept 10, 2002
 *
 * cmd64x.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Note, this driver is not used at all on other systems because
 *           there the "BIOS" has done all of the following already.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998		Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998		David S. Miller (davem@redhat.com)
 *
 * Copyright (C) 1999-2002	Andre Hedrick <andre@linux-ide.org>
 */

#include <linux/config.h>
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
#define   CFR_INTR_CH0		0x02
#define CNTRL		0x51
#define	  CNTRL_DIS_RA0		0x40
#define   CNTRL_DIS_RA1		0x80
#define	  CNTRL_ENA_2ND		0x08

#define	CMDTIM		0x52
#define	ARTTIM0		0x53
#define	DRWTIM0		0x54
#define ARTTIM1 	0x55
#define DRWTIM1		0x56
#define ARTTIM23	0x57
#define   ARTTIM23_DIS_RA2	0x04
#define   ARTTIM23_DIS_RA3	0x08
#define   ARTTIM23_INTR_CH1	0x10
#define ARTTIM2		0x57
#define ARTTIM3		0x57
#define DRWTIM23	0x58
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

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 cmd64x_proc = 0;

#define CMD_MAX_DEVS		5

static struct pci_dev *cmd_devs[CMD_MAX_DEVS];
static int n_cmd_devs;

static char * print_cmd64x_get_info (char *buf, struct pci_dev *dev, int index)
{
	char *p = buf;

	u8 reg53 = 0, reg54 = 0, reg55 = 0, reg56 = 0;	/* primary */
	u8 reg57 = 0, reg58 = 0, reg5b;			/* secondary */
	u8 reg72 = 0, reg73 = 0;			/* primary */
	u8 reg7a = 0, reg7b = 0;			/* secondary */
	u8 reg50 = 0, reg71 = 0;			/* extra */

	p += sprintf(p, "\nController: %d\n", index);
	p += sprintf(p, "CMD%x Chipset.\n", dev->device);
	(void) pci_read_config_byte(dev, CFR,       &reg50);
	(void) pci_read_config_byte(dev, ARTTIM0,   &reg53);
	(void) pci_read_config_byte(dev, DRWTIM0,   &reg54);
	(void) pci_read_config_byte(dev, ARTTIM1,   &reg55);
	(void) pci_read_config_byte(dev, DRWTIM1,   &reg56);
	(void) pci_read_config_byte(dev, ARTTIM2,   &reg57);
	(void) pci_read_config_byte(dev, DRWTIM2,   &reg58);
	(void) pci_read_config_byte(dev, DRWTIM3,   &reg5b);
	(void) pci_read_config_byte(dev, MRDMODE,   &reg71);
	(void) pci_read_config_byte(dev, BMIDESR0,  &reg72);
	(void) pci_read_config_byte(dev, UDIDETCR0, &reg73);
	(void) pci_read_config_byte(dev, BMIDESR1,  &reg7a);
	(void) pci_read_config_byte(dev, UDIDETCR1, &reg7b);

	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "                %sabled           "
			"              %sabled\n",
		(reg72&0x80)?"dis":" en",
		(reg7a&0x80)?"dis":" en");
	p += sprintf(p, "--------------- drive0 "
		"--------- drive1 -------- drive0 "
		"---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s"
			"             %s               %s\n",
		(reg72&0x20)?"yes":"no ", (reg72&0x40)?"yes":"no ",
		(reg7a&0x20)?"yes":"no ", (reg7a&0x40)?"yes":"no ");

	p += sprintf(p, "DMA Mode:       %s(%s)          %s(%s)",
		(reg72&0x20)?((reg73&0x01)?"UDMA":" DMA"):" PIO",
		(reg72&0x20)?(
			((reg73&0x30)==0x30)?(((reg73&0x35)==0x35)?"3":"0"):
			((reg73&0x20)==0x20)?(((reg73&0x25)==0x25)?"3":"1"):
			((reg73&0x10)==0x10)?(((reg73&0x15)==0x15)?"4":"2"):
			((reg73&0x00)==0x00)?(((reg73&0x05)==0x05)?"5":"2"):
			"X"):"?",
		(reg72&0x40)?((reg73&0x02)?"UDMA":" DMA"):" PIO",
		(reg72&0x40)?(
			((reg73&0xC0)==0xC0)?(((reg73&0xC5)==0xC5)?"3":"0"):
			((reg73&0x80)==0x80)?(((reg73&0x85)==0x85)?"3":"1"):
			((reg73&0x40)==0x40)?(((reg73&0x4A)==0x4A)?"4":"2"):
			((reg73&0x00)==0x00)?(((reg73&0x0A)==0x0A)?"5":"2"):
			"X"):"?");
	p += sprintf(p, "         %s(%s)           %s(%s)\n",
		(reg7a&0x20)?((reg7b&0x01)?"UDMA":" DMA"):" PIO",
		(reg7a&0x20)?(
			((reg7b&0x30)==0x30)?(((reg7b&0x35)==0x35)?"3":"0"):
			((reg7b&0x20)==0x20)?(((reg7b&0x25)==0x25)?"3":"1"):
			((reg7b&0x10)==0x10)?(((reg7b&0x15)==0x15)?"4":"2"):
			((reg7b&0x00)==0x00)?(((reg7b&0x05)==0x05)?"5":"2"):
			"X"):"?",
		(reg7a&0x40)?((reg7b&0x02)?"UDMA":" DMA"):" PIO",
		(reg7a&0x40)?(
			((reg7b&0xC0)==0xC0)?(((reg7b&0xC5)==0xC5)?"3":"0"):
			((reg7b&0x80)==0x80)?(((reg7b&0x85)==0x85)?"3":"1"):
			((reg7b&0x40)==0x40)?(((reg7b&0x4A)==0x4A)?"4":"2"):
			((reg7b&0x00)==0x00)?(((reg7b&0x0A)==0x0A)?"5":"2"):
			"X"):"?" );
	p += sprintf(p, "PIO Mode:       %s                %s"
			"               %s                 %s\n",
			"?", "?", "?", "?");
	p += sprintf(p, "                %s                     %s\n",
		(reg50 & CFR_INTR_CH0) ? "interrupting" : "polling     ",
		(reg57 & ARTTIM23_INTR_CH1) ? "interrupting" : "polling");
	p += sprintf(p, "                %s                          %s\n",
		(reg71 & MRDMODE_INTR_CH0) ? "pending" : "clear  ",
		(reg71 & MRDMODE_INTR_CH1) ? "pending" : "clear");
	p += sprintf(p, "                %s                          %s\n",
		(reg71 & MRDMODE_BLK_CH0) ? "blocked" : "enabled",
		(reg71 & MRDMODE_BLK_CH1) ? "blocked" : "enabled");

	return (char *)p;
}

static int cmd64x_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int i;

	p += sprintf(p, "\n");
	for (i = 0; i < n_cmd_devs; i++) {
		struct pci_dev *dev	= cmd_devs[i];
		p = print_cmd64x_get_info(p, dev, i);
	}
	return p-buffer;	/* => must be less than 4k! */
}

#endif	/* defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS) */

/*
 * Registers and masks for easy access by drive index:
 */
#if 0
static u8 prefetch_regs[4]  = {CNTRL, CNTRL, ARTTIM23, ARTTIM23};
static u8 prefetch_masks[4] = {CNTRL_DIS_RA0, CNTRL_DIS_RA1, ARTTIM23_DIS_RA2, ARTTIM23_DIS_RA3};
#endif

/*
 * This routine writes the prepared setup/active/recovery counts
 * for a drive into the cmd646 chipset registers to active them.
 */
static void program_drive_counts (ide_drive_t *drive, int setup_count, int active_count, int recovery_count)
{
	unsigned long flags;
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	ide_drive_t *drives = HWIF(drive)->drives;
	u8 temp_b;
	static const u8 setup_counts[] = {0x40, 0x40, 0x40, 0x80, 0, 0xc0};
	static const u8 recovery_counts[] =
		{15, 15, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0};
	static const u8 arttim_regs[2][2] = {
			{ ARTTIM0, ARTTIM1 },
			{ ARTTIM23, ARTTIM23 }
		};
	static const u8 drwtim_regs[2][2] = {
			{ DRWTIM0, DRWTIM1 },
			{ DRWTIM2, DRWTIM3 }
		};
	int channel = (int) HWIF(drive)->channel;
	int slave = (drives != drive);  /* Is this really the best way to determine this?? */

	cmdprintk("program_drive_count parameters = s(%d),a(%d),r(%d),p(%d)\n",
		setup_count, active_count, recovery_count, drive->present);
	/*
	 * Set up address setup count registers.
	 * Primary interface has individual count/timing registers for
	 * each drive.  Secondary interface has one common set of registers,
	 * for address setup so we merge these timings, using the slowest
	 * value.
	 */
	if (channel) {
		drive->drive_data = setup_count;
		setup_count = max(drives[0].drive_data,
					drives[1].drive_data);
		cmdprintk("Secondary interface, setup_count = %d\n",
					setup_count);
	}

	/*
	 * Convert values to internal chipset representation
	 */
	setup_count = (setup_count > 5) ? 0xc0 : (int) setup_counts[setup_count];
	active_count &= 0xf; /* Remember, max value is 16 */
	recovery_count = (int) recovery_counts[recovery_count];

	cmdprintk("Final values = %d,%d,%d\n",
		setup_count, active_count, recovery_count);

	/*
	 * Now that everything is ready, program the new timings
	 */
	local_irq_save(flags);
	/*
	 * Program the address_setup clocks into ARTTIM reg,
	 * and then the active/recovery counts into the DRWTIM reg
	 */
	(void) pci_read_config_byte(dev, arttim_regs[channel][slave], &temp_b);
	(void) pci_write_config_byte(dev, arttim_regs[channel][slave],
		((u8) setup_count) | (temp_b & 0x3f));
	(void) pci_write_config_byte(dev, drwtim_regs[channel][slave],
		(u8) ((active_count << 4) | recovery_count));
	cmdprintk ("Write %x to %x\n",
		((u8) setup_count) | (temp_b & 0x3f),
		arttim_regs[channel][slave]);
	cmdprintk ("Write %x to %x\n",
		(u8) ((active_count << 4) | recovery_count),
		drwtim_regs[channel][slave]);
	local_irq_restore(flags);
}

/*
 * Attempts to set the interface PIO mode.
 * The preferred method of selecting PIO modes (e.g. mode 4) is 
 * "echo 'piomode:4' > /proc/ide/hdx/settings".  Special cases are
 * 8: prefetch off, 9: prefetch on, 255: auto-select best mode.
 * Called with 255 at boot time.
 */

static void cmd64x_tuneproc (ide_drive_t *drive, u8 mode_wanted)
{
	int setup_time, active_time, recovery_time;
	int clock_time, pio_mode, cycle_time;
	u8 recovery_count2, cycle_count;
	int setup_count, active_count, recovery_count;
	int bus_speed = system_bus_clock();
	/*byte b;*/
	ide_pio_data_t  d;

	switch (mode_wanted) {
		case 8: /* set prefetch off */
		case 9: /* set prefetch on */
			mode_wanted &= 1;
			/*set_prefetch_mode(index, mode_wanted);*/
			cmdprintk("%s: %sabled cmd640 prefetch\n",
				drive->name, mode_wanted ? "en" : "dis");
			return;
	}

	mode_wanted = ide_get_best_pio_mode (drive, mode_wanted, 5, &d);
	pio_mode = d.pio_mode;
	cycle_time = d.cycle_time;

	/*
	 * I copied all this complicated stuff from cmd640.c and made a few
	 * minor changes.  For now I am just going to pray that it is correct.
	 */
	if (pio_mode > 5)
		pio_mode = 5;
	setup_time  = ide_pio_timings[pio_mode].setup_time;
	active_time = ide_pio_timings[pio_mode].active_time;
	recovery_time = cycle_time - (setup_time + active_time);
	clock_time = 1000 / bus_speed;
	cycle_count = (cycle_time + clock_time - 1) / clock_time;

	setup_count = (setup_time + clock_time - 1) / clock_time;

	active_count = (active_time + clock_time - 1) / clock_time;

	recovery_count = (recovery_time + clock_time - 1) / clock_time;
	recovery_count2 = cycle_count - (setup_count + active_count);
	if (recovery_count2 > recovery_count)
		recovery_count = recovery_count2;
	if (recovery_count > 16) {
		active_count += recovery_count - 16;
		recovery_count = 16;
	}
	if (active_count > 16)
		active_count = 16; /* maximum allowed by cmd646 */

	/*
	 * In a perfect world, we might set the drive pio mode here
	 * (using WIN_SETFEATURE) before continuing.
	 *
	 * But we do not, because:
	 *	1) this is the wrong place to do it
	 *		(proper is do_special() in ide.c)
	 * 	2) in practice this is rarely, if ever, necessary
	 */
	program_drive_counts (drive, setup_count, active_count, recovery_count);

	cmdprintk("%s: selected cmd646 PIO mode%d : %d (%dns)%s, "
		"clocks=%d/%d/%d\n",
		drive->name, pio_mode, mode_wanted, cycle_time,
		d.overridden ? " (overriding vendor mode)" : "",
		setup_count, active_count, recovery_count);
}

static u8 cmd64x_ratemask (ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	u8 mode = 0;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_649:
			mode = 3;
			break;
		case PCI_DEVICE_ID_CMD_648:
			mode = 2;
			break;
		case PCI_DEVICE_ID_CMD_643:
			return 0;

		case PCI_DEVICE_ID_CMD_646:
		{
			unsigned int class_rev	= 0;
			pci_read_config_dword(dev,
				PCI_CLASS_REVISION, &class_rev);
			class_rev &= 0xff;
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
			switch(class_rev) {
				case 0x07:
				case 0x05:
					return 1;
				case 0x03:
				case 0x01:
				default:
					return 0;
			}
		}
	}
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

static void config_cmd64x_chipset_for_pio (ide_drive_t *drive, u8 set_speed)
{
	u8 speed	= 0x00;
	u8 set_pio	= ide_get_best_pio_mode(drive, 4, 5, NULL);

	cmd64x_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed)
		(void) ide_config_drive_speed(drive, speed);
}

static void config_chipset_for_pio (ide_drive_t *drive, u8 set_speed)
{
	config_cmd64x_chipset_for_pio(drive, set_speed);
}

static int cmd64x_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8 unit			= (drive->select.b.unit & 0x01);
	u8 regU = 0, pciU	= (hwif->channel) ? UDIDETCR1 : UDIDETCR0;
	u8 regD = 0, pciD	= (hwif->channel) ? BMIDESR1 : BMIDESR0;

	u8 speed	= ide_rate_filter(cmd64x_ratemask(drive), xferspeed);

	if (speed > XFER_PIO_4) {
		(void) pci_read_config_byte(dev, pciD, &regD);
		(void) pci_read_config_byte(dev, pciU, &regU);
		regD &= ~(unit ? 0x40 : 0x20);
		regU &= ~(unit ? 0xCA : 0x35);
		(void) pci_write_config_byte(dev, pciD, regD);
		(void) pci_write_config_byte(dev, pciU, regU);
		(void) pci_read_config_byte(dev, pciD, &regD);
		(void) pci_read_config_byte(dev, pciU, &regU);
	}

	switch(speed) {
		case XFER_UDMA_5:	regU |= (unit ? 0x0A : 0x05); break;
		case XFER_UDMA_4:	regU |= (unit ? 0x4A : 0x15); break;
		case XFER_UDMA_3:	regU |= (unit ? 0x8A : 0x25); break;
		case XFER_UDMA_2:	regU |= (unit ? 0x42 : 0x11); break;
		case XFER_UDMA_1:	regU |= (unit ? 0x82 : 0x21); break;
		case XFER_UDMA_0:	regU |= (unit ? 0xC2 : 0x31); break;
		case XFER_MW_DMA_2:	regD |= (unit ? 0x40 : 0x10); break;
		case XFER_MW_DMA_1:	regD |= (unit ? 0x80 : 0x20); break;
		case XFER_MW_DMA_0:	regD |= (unit ? 0xC0 : 0x30); break;
		case XFER_SW_DMA_2:	regD |= (unit ? 0x40 : 0x10); break;
		case XFER_SW_DMA_1:	regD |= (unit ? 0x80 : 0x20); break;
		case XFER_SW_DMA_0:	regD |= (unit ? 0xC0 : 0x30); break;
		case XFER_PIO_4:	cmd64x_tuneproc(drive, 4); break;
		case XFER_PIO_3:	cmd64x_tuneproc(drive, 3); break;
		case XFER_PIO_2:	cmd64x_tuneproc(drive, 2); break;
		case XFER_PIO_1:	cmd64x_tuneproc(drive, 1); break;
		case XFER_PIO_0:	cmd64x_tuneproc(drive, 0); break;

		default:
			return 1;
	}

	if (speed > XFER_PIO_4) {
		(void) pci_write_config_byte(dev, pciU, regU);
		regD |= (unit ? 0x40 : 0x20);
		(void) pci_write_config_byte(dev, pciD, regD);
	}

	return (ide_config_drive_speed(drive, speed));
}

static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed	= ide_dma_speed(drive, cmd64x_ratemask(drive));

	config_chipset_for_pio(drive, !speed);

	if (!speed)
		return 0;

	if(ide_set_xfer_rate(drive, speed))
		return 0; 

	if (!drive->init_speed)
		drive->init_speed = speed;

	return ide_dma_enable(drive);
}

static int cmd64x_config_drive_for_dma (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	if ((id != NULL) && ((id->capability & 1) != 0) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		config_chipset_for_pio(drive, 1);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}

static int cmd64x_alt_dma_status (struct pci_dev *dev)
{
	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_648:
		case PCI_DEVICE_ID_CMD_649:
			return 1;
		default:
			break;
	}
	return 0;
}

static int cmd64x_ide_dma_end (ide_drive_t *drive)
{
	u8 dma_stat = 0, dma_cmd = 0;
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	drive->waiting_for_dma = 0;
	/* read DMA command state */
	dma_cmd = hwif->INB(hwif->dma_command);
	/* stop DMA */
	hwif->OUTB((dma_cmd & ~1), hwif->dma_command);
	/* get DMA status */
	dma_stat = hwif->INB(hwif->dma_status);
	/* clear the INTR & ERROR bits */
	hwif->OUTB(dma_stat|6, hwif->dma_status);
	if (cmd64x_alt_dma_status(dev)) {
		u8 dma_intr	= 0;
		u8 dma_mask	= (hwif->channel) ? ARTTIM23_INTR_CH1 :
						    CFR_INTR_CH0;
		u8 dma_reg	= (hwif->channel) ? ARTTIM2 : CFR;
		(void) pci_read_config_byte(dev, dma_reg, &dma_intr);
		/* clear the INTR bit */
		(void) pci_write_config_byte(dev, dma_reg, dma_intr|dma_mask);
	}
	/* purge DMA mappings */
	ide_destroy_dmatable(drive);
	/* verify good DMA status */
	return (dma_stat & 7) != 4;
}

static int cmd64x_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif		= HWIF(drive);
	struct pci_dev *dev		= hwif->pci_dev;
        u8 dma_alt_stat = 0, mask	= (hwif->channel) ? MRDMODE_INTR_CH1 :
							    MRDMODE_INTR_CH0;
	u8 dma_stat = hwif->INB(hwif->dma_status);

	(void) pci_read_config_byte(dev, MRDMODE, &dma_alt_stat);
#ifdef DEBUG
	printk("%s: dma_stat: 0x%02x dma_alt_stat: "
		"0x%02x mask: 0x%02x\n", drive->name,
		dma_stat, dma_alt_stat, mask);
#endif
	if (!(dma_alt_stat & mask))
		return 0;

	/* return 1 if INTR asserted */
	if ((dma_stat & 4) == 4)
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
	dma_stat = hwif->INB(hwif->dma_status);
	/* read DMA command state */
	dma_cmd = hwif->INB(hwif->dma_command);
	/* stop DMA */
	hwif->OUTB((dma_cmd & ~1), hwif->dma_command);
	/* clear the INTR & ERROR bits */
	hwif->OUTB(dma_stat|6, hwif->dma_status);
	/* and free any DMA resources */
	ide_destroy_dmatable(drive);
	/* verify good DMA status */
	return (dma_stat & 7) != 4;
}

static unsigned int __devinit init_chipset_cmd64x(struct pci_dev *dev, const char *name)
{
	u32 class_rev = 0;
	u8 mrdmode = 0;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

#ifdef __i386__
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk(KERN_INFO "%s: ROM enabled at 0x%08lx\n", name, dev->resource[PCI_ROM_RESOURCE].start);
	}
#endif

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_643:
			break;
		case PCI_DEVICE_ID_CMD_646:
			printk(KERN_INFO "%s: chipset revision 0x%02X, ", name, class_rev);
			switch(class_rev) {
				case 0x07:
				case 0x05:
					printk("UltraDMA Capable");
					break;
				case 0x03:
					printk("MultiWord DMA Force Limited");
					break;
				case 0x01:
				default:
					printk("MultiWord DMA Limited, IRQ workaround enabled");
					break;
				}
			printk("\n");
                        break;
		case PCI_DEVICE_ID_CMD_648:
		case PCI_DEVICE_ID_CMD_649:
			break;
		default:
			break;
	}

	/* Set a good latency timer and cache line size value. */
	(void) pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	/* FIXME: pci_set_master() to ensure a good latency timer value */

	/* Setup interrupts. */
	(void) pci_read_config_byte(dev, MRDMODE, &mrdmode);
	mrdmode &= ~(0x30);
	(void) pci_write_config_byte(dev, MRDMODE, mrdmode);

	/* Use MEMORY READ LINE for reads.
	 * NOTE: Although not mentioned in the PCI0646U specs,
	 *       these bits are write only and won't be read
	 *       back as set or not.  The PCI0646U2 specs clarify
	 *       this point.
	 */
	(void) pci_write_config_byte(dev, MRDMODE, mrdmode | 0x02);

	/* Set reasonable active/recovery/address-setup values. */
	(void) pci_write_config_byte(dev, ARTTIM0,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM0,  0x3f);
	(void) pci_write_config_byte(dev, ARTTIM1,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM1,  0x3f);
#ifdef __i386__
	(void) pci_write_config_byte(dev, ARTTIM23, 0x1c);
#else
	(void) pci_write_config_byte(dev, ARTTIM23, 0x5c);
#endif
	(void) pci_write_config_byte(dev, DRWTIM23, 0x3f);
	(void) pci_write_config_byte(dev, DRWTIM3,  0x3f);
#ifdef CONFIG_PPC
	(void) pci_write_config_byte(dev, UDIDETCR0, 0xf0);
#endif /* CONFIG_PPC */

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS)

	cmd_devs[n_cmd_devs++] = dev;

	if (!cmd64x_proc) {
		cmd64x_proc = 1;
		ide_pci_create_host_proc("cmd64x", cmd64x_get_info);
	}
#endif /* DISPLAY_CMD64X_TIMINGS && CONFIG_PROC_FS */

	return 0;
}

static unsigned int __devinit ata66_cmd64x(ide_hwif_t *hwif)
{
	u8 ata66 = 0, mask = (hwif->channel) ? 0x02 : 0x01;

	switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_CMD_643:
		case PCI_DEVICE_ID_CMD_646:
			return ata66;
		default:
			break;
	}
	pci_read_config_byte(hwif->pci_dev, BMIDECSR, &ata66);
	return (ata66 & mask) ? 1 : 0;
}

static void __devinit init_hwif_cmd64x(ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int class_rev;

	hwif->autodma = 0;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	hwif->tuneproc  = &cmd64x_tuneproc;
	hwif->speedproc = &cmd64x_tune_chipset;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;

	hwif->ultra_mask = 0x3f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	if (dev->device == PCI_DEVICE_ID_CMD_643)
		hwif->ultra_mask = 0x80;
	if (dev->device == PCI_DEVICE_ID_CMD_646)
		hwif->ultra_mask = (class_rev > 0x04) ? 0x07 : 0x80;
	if (dev->device == PCI_DEVICE_ID_CMD_648)
		hwif->ultra_mask = 0x1f;

	hwif->ide_dma_check = &cmd64x_config_drive_for_dma;
	if (!(hwif->udma_four))
		hwif->udma_four = ata66_cmd64x(hwif);

	if (dev->device == PCI_DEVICE_ID_CMD_646) {
		hwif->chipset = ide_cmd646;
		if (class_rev == 0x01) {
			hwif->ide_dma_end = &cmd646_1_ide_dma_end;
		} else {
			hwif->ide_dma_end = &cmd64x_ide_dma_end;
			hwif->ide_dma_test_irq = &cmd64x_ide_dma_test_irq;
		}
	} else {
		hwif->ide_dma_end = &cmd64x_ide_dma_end;
		hwif->ide_dma_test_irq = &cmd64x_ide_dma_test_irq;
	}


	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t cmd64x_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "CMD643",
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 1 */
		.name		= "CMD646",
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x00,0x00,0x00}, {0x51,0x80,0x80}},
		.bootable	= ON_BOARD,
	},{	/* 2 */
		.name		= "CMD648",
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	},{	/* 3 */
		.name		= "CMD649",
		.init_chipset	= init_chipset_cmd64x,
		.init_hwif	= init_hwif_cmd64x,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
	}
};

static int __devinit cmd64x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &cmd64x_chipsets[id->driver_data]);
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

static int cmd64x_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(cmd64x_ide_init);

MODULE_AUTHOR("Eddie Dost, David Miller, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for CMD64x IDE");
MODULE_LICENSE("GPL");
