/*
 *  Copyright (C) 2000-2002	Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003		Red Hat <alan@redhat.com>
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/bitops.h>
#include <linux/nmi.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/*
 *	Conventional PIO operations for ATA devices
 */

static u8 ide_inb (unsigned long port)
{
	return (u8) inb(port);
}

static u16 ide_inw (unsigned long port)
{
	return (u16) inw(port);
}

static void ide_insw (unsigned long port, void *addr, u32 count)
{
	insw(port, addr, count);
}

static void ide_insl (unsigned long port, void *addr, u32 count)
{
	insl(port, addr, count);
}

static void ide_outb (u8 val, unsigned long port)
{
	outb(val, port);
}

static void ide_outbsync (ide_drive_t *drive, u8 addr, unsigned long port)
{
	outb(addr, port);
}

static void ide_outw (u16 val, unsigned long port)
{
	outw(val, port);
}

static void ide_outsw (unsigned long port, void *addr, u32 count)
{
	outsw(port, addr, count);
}

static void ide_outsl (unsigned long port, void *addr, u32 count)
{
	outsl(port, addr, count);
}

void default_hwif_iops (ide_hwif_t *hwif)
{
	hwif->OUTB	= ide_outb;
	hwif->OUTBSYNC	= ide_outbsync;
	hwif->OUTW	= ide_outw;
	hwif->OUTSW	= ide_outsw;
	hwif->OUTSL	= ide_outsl;
	hwif->INB	= ide_inb;
	hwif->INW	= ide_inw;
	hwif->INSW	= ide_insw;
	hwif->INSL	= ide_insl;
}

/*
 *	MMIO operations, typically used for SATA controllers
 */

static u8 ide_mm_inb (unsigned long port)
{
	return (u8) readb((void __iomem *) port);
}

static u16 ide_mm_inw (unsigned long port)
{
	return (u16) readw((void __iomem *) port);
}

static void ide_mm_insw (unsigned long port, void *addr, u32 count)
{
	__ide_mm_insw((void __iomem *) port, addr, count);
}

static void ide_mm_insl (unsigned long port, void *addr, u32 count)
{
	__ide_mm_insl((void __iomem *) port, addr, count);
}

static void ide_mm_outb (u8 value, unsigned long port)
{
	writeb(value, (void __iomem *) port);
}

static void ide_mm_outbsync (ide_drive_t *drive, u8 value, unsigned long port)
{
	writeb(value, (void __iomem *) port);
}

static void ide_mm_outw (u16 value, unsigned long port)
{
	writew(value, (void __iomem *) port);
}

static void ide_mm_outsw (unsigned long port, void *addr, u32 count)
{
	__ide_mm_outsw((void __iomem *) port, addr, count);
}

static void ide_mm_outsl (unsigned long port, void *addr, u32 count)
{
	__ide_mm_outsl((void __iomem *) port, addr, count);
}

void default_hwif_mmiops (ide_hwif_t *hwif)
{
	hwif->OUTB	= ide_mm_outb;
	/* Most systems will need to override OUTBSYNC, alas however
	   this one is controller specific! */
	hwif->OUTBSYNC	= ide_mm_outbsync;
	hwif->OUTW	= ide_mm_outw;
	hwif->OUTSW	= ide_mm_outsw;
	hwif->OUTSL	= ide_mm_outsl;
	hwif->INB	= ide_mm_inb;
	hwif->INW	= ide_mm_inw;
	hwif->INSW	= ide_mm_insw;
	hwif->INSL	= ide_mm_insl;
}

EXPORT_SYMBOL(default_hwif_mmiops);

void SELECT_DRIVE (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;

	if (hwif->selectproc)
		hwif->selectproc(drive);

	hwif->OUTB(drive->select.all, hwif->io_ports[IDE_SELECT_OFFSET]);
}

void SELECT_MASK (ide_drive_t *drive, int mask)
{
	if (HWIF(drive)->maskproc)
		HWIF(drive)->maskproc(drive, mask);
}

/*
 * Some localbus EIDE interfaces require a special access sequence
 * when using 32-bit I/O instructions to transfer data.  We call this
 * the "vlb_sync" sequence, which consists of three successive reads
 * of the sector count register location, with interrupts disabled
 * to ensure that the reads all happen together.
 */
static void ata_vlb_sync(ide_drive_t *drive, unsigned long port)
{
	(void) HWIF(drive)->INB(port);
	(void) HWIF(drive)->INB(port);
	(void) HWIF(drive)->INB(port);
}

/*
 * This is used for most PIO data transfers *from* the IDE interface
 */
static void ata_input_data(ide_drive_t *drive, void *buffer, u32 wcount)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 io_32bit		= drive->io_32bit;

	if (io_32bit) {
		if (io_32bit & 2) {
			unsigned long flags;

			local_irq_save(flags);
			ata_vlb_sync(drive, hwif->io_ports[IDE_NSECTOR_OFFSET]);
			hwif->INSL(hwif->io_ports[IDE_DATA_OFFSET], buffer,
				   wcount);
			local_irq_restore(flags);
		} else
			hwif->INSL(hwif->io_ports[IDE_DATA_OFFSET], buffer,
				   wcount);
	} else
		hwif->INSW(hwif->io_ports[IDE_DATA_OFFSET], buffer,
			   wcount << 1);
}

/*
 * This is used for most PIO data transfers *to* the IDE interface
 */
static void ata_output_data(ide_drive_t *drive, void *buffer, u32 wcount)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 io_32bit		= drive->io_32bit;

	if (io_32bit) {
		if (io_32bit & 2) {
			unsigned long flags;

			local_irq_save(flags);
			ata_vlb_sync(drive, hwif->io_ports[IDE_NSECTOR_OFFSET]);
			hwif->OUTSL(hwif->io_ports[IDE_DATA_OFFSET], buffer,
				    wcount);
			local_irq_restore(flags);
		} else
			hwif->OUTSL(hwif->io_ports[IDE_DATA_OFFSET], buffer,
				    wcount);
	} else
		hwif->OUTSW(hwif->io_ports[IDE_DATA_OFFSET], buffer,
			    wcount << 1);
}

/*
 * The following routines are mainly used by the ATAPI drivers.
 *
 * These routines will round up any request for an odd number of bytes,
 * so if an odd bytecount is specified, be sure that there's at least one
 * extra byte allocated for the buffer.
 */

static void atapi_input_bytes(ide_drive_t *drive, void *buffer, u32 bytecount)
{
	ide_hwif_t *hwif = HWIF(drive);

	++bytecount;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		insw_swapw(hwif->io_ports[IDE_DATA_OFFSET], buffer,
			   bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI || CONFIG_Q40 */
	hwif->ata_input_data(drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		hwif->INSW(hwif->io_ports[IDE_DATA_OFFSET],
			   (u8 *)buffer + (bytecount & ~0x03), 1);
}

static void atapi_output_bytes(ide_drive_t *drive, void *buffer, u32 bytecount)
{
	ide_hwif_t *hwif = HWIF(drive);

	++bytecount;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		outsw_swapw(hwif->io_ports[IDE_DATA_OFFSET], buffer,
			    bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI || CONFIG_Q40 */
	hwif->ata_output_data(drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		hwif->OUTSW(hwif->io_ports[IDE_DATA_OFFSET],
			    (u8 *)buffer + (bytecount & ~0x03), 1);
}

void default_hwif_transport(ide_hwif_t *hwif)
{
	hwif->ata_input_data		= ata_input_data;
	hwif->ata_output_data		= ata_output_data;
	hwif->atapi_input_bytes		= atapi_input_bytes;
	hwif->atapi_output_bytes	= atapi_output_bytes;
}

void ide_fix_driveid (struct hd_driveid *id)
{
#ifndef __LITTLE_ENDIAN
# ifdef __BIG_ENDIAN
	int i;
	u16 *stringcast;

	id->config         = __le16_to_cpu(id->config);
	id->cyls           = __le16_to_cpu(id->cyls);
	id->reserved2      = __le16_to_cpu(id->reserved2);
	id->heads          = __le16_to_cpu(id->heads);
	id->track_bytes    = __le16_to_cpu(id->track_bytes);
	id->sector_bytes   = __le16_to_cpu(id->sector_bytes);
	id->sectors        = __le16_to_cpu(id->sectors);
	id->vendor0        = __le16_to_cpu(id->vendor0);
	id->vendor1        = __le16_to_cpu(id->vendor1);
	id->vendor2        = __le16_to_cpu(id->vendor2);
	stringcast = (u16 *)&id->serial_no[0];
	for (i = 0; i < (20/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	id->buf_type       = __le16_to_cpu(id->buf_type);
	id->buf_size       = __le16_to_cpu(id->buf_size);
	id->ecc_bytes      = __le16_to_cpu(id->ecc_bytes);
	stringcast = (u16 *)&id->fw_rev[0];
	for (i = 0; i < (8/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	stringcast = (u16 *)&id->model[0];
	for (i = 0; i < (40/2); i++)
		stringcast[i] = __le16_to_cpu(stringcast[i]);
	id->dword_io       = __le16_to_cpu(id->dword_io);
	id->reserved50     = __le16_to_cpu(id->reserved50);
	id->field_valid    = __le16_to_cpu(id->field_valid);
	id->cur_cyls       = __le16_to_cpu(id->cur_cyls);
	id->cur_heads      = __le16_to_cpu(id->cur_heads);
	id->cur_sectors    = __le16_to_cpu(id->cur_sectors);
	id->cur_capacity0  = __le16_to_cpu(id->cur_capacity0);
	id->cur_capacity1  = __le16_to_cpu(id->cur_capacity1);
	id->lba_capacity   = __le32_to_cpu(id->lba_capacity);
	id->dma_1word      = __le16_to_cpu(id->dma_1word);
	id->dma_mword      = __le16_to_cpu(id->dma_mword);
	id->eide_pio_modes = __le16_to_cpu(id->eide_pio_modes);
	id->eide_dma_min   = __le16_to_cpu(id->eide_dma_min);
	id->eide_dma_time  = __le16_to_cpu(id->eide_dma_time);
	id->eide_pio       = __le16_to_cpu(id->eide_pio);
	id->eide_pio_iordy = __le16_to_cpu(id->eide_pio_iordy);
	for (i = 0; i < 2; ++i)
		id->words69_70[i] = __le16_to_cpu(id->words69_70[i]);
	for (i = 0; i < 4; ++i)
		id->words71_74[i] = __le16_to_cpu(id->words71_74[i]);
	id->queue_depth    = __le16_to_cpu(id->queue_depth);
	for (i = 0; i < 4; ++i)
		id->words76_79[i] = __le16_to_cpu(id->words76_79[i]);
	id->major_rev_num  = __le16_to_cpu(id->major_rev_num);
	id->minor_rev_num  = __le16_to_cpu(id->minor_rev_num);
	id->command_set_1  = __le16_to_cpu(id->command_set_1);
	id->command_set_2  = __le16_to_cpu(id->command_set_2);
	id->cfsse          = __le16_to_cpu(id->cfsse);
	id->cfs_enable_1   = __le16_to_cpu(id->cfs_enable_1);
	id->cfs_enable_2   = __le16_to_cpu(id->cfs_enable_2);
	id->csf_default    = __le16_to_cpu(id->csf_default);
	id->dma_ultra      = __le16_to_cpu(id->dma_ultra);
	id->trseuc         = __le16_to_cpu(id->trseuc);
	id->trsEuc         = __le16_to_cpu(id->trsEuc);
	id->CurAPMvalues   = __le16_to_cpu(id->CurAPMvalues);
	id->mprc           = __le16_to_cpu(id->mprc);
	id->hw_config      = __le16_to_cpu(id->hw_config);
	id->acoustic       = __le16_to_cpu(id->acoustic);
	id->msrqs          = __le16_to_cpu(id->msrqs);
	id->sxfert         = __le16_to_cpu(id->sxfert);
	id->sal            = __le16_to_cpu(id->sal);
	id->spg            = __le32_to_cpu(id->spg);
	id->lba_capacity_2 = __le64_to_cpu(id->lba_capacity_2);
	for (i = 0; i < 22; i++)
		id->words104_125[i]   = __le16_to_cpu(id->words104_125[i]);
	id->last_lun       = __le16_to_cpu(id->last_lun);
	id->word127        = __le16_to_cpu(id->word127);
	id->dlf            = __le16_to_cpu(id->dlf);
	id->csfo           = __le16_to_cpu(id->csfo);
	for (i = 0; i < 26; i++)
		id->words130_155[i] = __le16_to_cpu(id->words130_155[i]);
	id->word156        = __le16_to_cpu(id->word156);
	for (i = 0; i < 3; i++)
		id->words157_159[i] = __le16_to_cpu(id->words157_159[i]);
	id->cfa_power      = __le16_to_cpu(id->cfa_power);
	for (i = 0; i < 14; i++)
		id->words161_175[i] = __le16_to_cpu(id->words161_175[i]);
	for (i = 0; i < 31; i++)
		id->words176_205[i] = __le16_to_cpu(id->words176_205[i]);
	for (i = 0; i < 48; i++)
		id->words206_254[i] = __le16_to_cpu(id->words206_254[i]);
	id->integrity_word  = __le16_to_cpu(id->integrity_word);
# else
#  error "Please fix <asm/byteorder.h>"
# endif
#endif
}

/*
 * ide_fixstring() cleans up and (optionally) byte-swaps a text string,
 * removing leading/trailing blanks and compressing internal blanks.
 * It is primarily used to tidy up the model name/number fields as
 * returned by the WIN_[P]IDENTIFY commands.
 */

void ide_fixstring (u8 *s, const int bytecount, const int byteswap)
{
	u8 *p = s, *end = &s[bytecount & ~1]; /* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to host byte order */
		for (p = end ; p != s;) {
			unsigned short *pp = (unsigned short *) (p -= 2);
			*pp = ntohs(*pp);
		}
	}
	/* strip leading blanks */
	while (s != end && *s == ' ')
		++s;
	/* compress internal blanks and strip trailing blanks */
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}
	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}

EXPORT_SYMBOL(ide_fixstring);

/*
 * Needed for PCI irq sharing
 */
int drive_is_ready (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= 0;

	if (drive->waiting_for_dma)
		return hwif->ide_dma_test_irq(drive);

#if 0
	/* need to guarantee 400ns since last command was issued */
	udelay(1);
#endif

	/*
	 * We do a passive status test under shared PCI interrupts on
	 * cards that truly share the ATA side interrupt, but may also share
	 * an interrupt with another pci card/device.  We make no assumptions
	 * about possible isa-pnp and pci-pnp issues yet.
	 */
	if (hwif->io_ports[IDE_CONTROL_OFFSET])
		stat = ide_read_altstatus(drive);
	else
		/* Note: this may clear a pending IRQ!! */
		stat = ide_read_status(drive);

	if (stat & BUSY_STAT)
		/* drive busy:  definitely not interrupting */
		return 0;

	/* drive ready: *might* be interrupting */
	return 1;
}

EXPORT_SYMBOL(drive_is_ready);

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return error -- caller may then invoke ide_error().
 *
 * This routine should get fixed to not hog the cpu during extra long waits..
 * That could be done by busy-waiting for the first jiffy or two, and then
 * setting a timer to wake up at half second intervals thereafter,
 * until timeout is achieved, before timing out.
 */
static int __ide_wait_stat(ide_drive_t *drive, u8 good, u8 bad, unsigned long timeout, u8 *rstat)
{
	unsigned long flags;
	int i;
	u8 stat;

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	stat = ide_read_status(drive);

	if (stat & BUSY_STAT) {
		local_irq_set(flags);
		timeout += jiffies;
		while ((stat = ide_read_status(drive)) & BUSY_STAT) {
			if (time_after(jiffies, timeout)) {
				/*
				 * One last read after the timeout in case
				 * heavy interrupt load made us not make any
				 * progress during the timeout..
				 */
				stat = ide_read_status(drive);
				if (!(stat & BUSY_STAT))
					break;

				local_irq_restore(flags);
				*rstat = stat;
				return -EBUSY;
			}
		}
		local_irq_restore(flags);
	}
	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		stat = ide_read_status(drive);

		if (OK_STAT(stat, good, bad)) {
			*rstat = stat;
			return 0;
		}
	}
	*rstat = stat;
	return -EFAULT;
}

/*
 * In case of error returns error value after doing "*startstop = ide_error()".
 * The caller should return the updated value of "startstop" in this case,
 * "startstop" is unchanged when the function returns 0.
 */
int ide_wait_stat(ide_startstop_t *startstop, ide_drive_t *drive, u8 good, u8 bad, unsigned long timeout)
{
	int err;
	u8 stat;

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		*startstop = ide_stopped;
		return 1;
	}

	err = __ide_wait_stat(drive, good, bad, timeout, &stat);

	if (err) {
		char *s = (err == -EBUSY) ? "status timeout" : "status error";
		*startstop = ide_error(drive, s, stat);
	}

	return err;
}

EXPORT_SYMBOL(ide_wait_stat);

/**
 *	ide_in_drive_list	-	look for drive in black/white list
 *	@id: drive identifier
 *	@drive_table: list to inspect
 *
 *	Look for a drive in the blacklist and the whitelist tables
 *	Returns 1 if the drive is found in the table.
 */

int ide_in_drive_list(struct hd_driveid *id, const struct drive_list_entry *drive_table)
{
	for ( ; drive_table->id_model; drive_table++)
		if ((!strcmp(drive_table->id_model, id->model)) &&
		    (!drive_table->id_firmware ||
		     strstr(id->fw_rev, drive_table->id_firmware)))
			return 1;
	return 0;
}

EXPORT_SYMBOL_GPL(ide_in_drive_list);

/*
 * Early UDMA66 devices don't set bit14 to 1, only bit13 is valid.
 * We list them here and depend on the device side cable detection for them.
 *
 * Some optical devices with the buggy firmwares have the same problem.
 */
static const struct drive_list_entry ivb_list[] = {
	{ "QUANTUM FIREBALLlct10 05"	, "A03.0900"	},
	{ "TSSTcorp CDDVDW SH-S202J"	, "SB00"	},
	{ "TSSTcorp CDDVDW SH-S202J"	, "SB01"	},
	{ "TSSTcorp CDDVDW SH-S202N"	, "SB00"	},
	{ "TSSTcorp CDDVDW SH-S202N"	, "SB01"	},
	{ NULL				, NULL		}
};

/*
 *  All hosts that use the 80c ribbon must use!
 *  The name is derived from upper byte of word 93 and the 80c ribbon.
 */
u8 eighty_ninty_three (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct hd_driveid *id = drive->id;
	int ivb = ide_in_drive_list(id, ivb_list);

	if (hwif->cbl == ATA_CBL_PATA40_SHORT)
		return 1;

	if (ivb)
		printk(KERN_DEBUG "%s: skipping word 93 validity check\n",
				  drive->name);

	if (ide_dev_is_sata(id) && !ivb)
		return 1;

	if (hwif->cbl != ATA_CBL_PATA80 && !ivb)
		goto no_80w;

	/*
	 * FIXME:
	 * - change master/slave IDENTIFY order
	 * - force bit13 (80c cable present) check also for !ivb devices
	 *   (unless the slave device is pre-ATA3)
	 */
	if ((id->hw_config & 0x4000) || (ivb && (id->hw_config & 0x2000)))
		return 1;

no_80w:
	if (drive->udma33_warned == 1)
		return 0;

	printk(KERN_WARNING "%s: %s side 80-wire cable detection failed, "
			    "limiting max speed to UDMA33\n",
			    drive->name,
			    hwif->cbl == ATA_CBL_PATA80 ? "drive" : "host");

	drive->udma33_warned = 1;

	return 0;
}

int ide_driveid_update(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct hd_driveid *id;
	unsigned long timeout, flags;
	u8 stat;

	/*
	 * Re-read drive->id for possible DMA mode
	 * change (copied from ide-probe.c)
	 */

	SELECT_MASK(drive, 1);
	ide_set_irq(drive, 1);
	msleep(50);
	hwif->OUTB(WIN_IDENTIFY, hwif->io_ports[IDE_COMMAND_OFFSET]);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (time_after(jiffies, timeout)) {
			SELECT_MASK(drive, 0);
			return 0;	/* drive timed-out */
		}

		msleep(50);	/* give drive a breather */
		stat = ide_read_altstatus(drive);
	} while (stat & BUSY_STAT);

	msleep(50);	/* wait for IRQ and DRQ_STAT */
	stat = ide_read_status(drive);

	if (!OK_STAT(stat, DRQ_STAT, BAD_R_STAT)) {
		SELECT_MASK(drive, 0);
		printk("%s: CHECK for good STATUS\n", drive->name);
		return 0;
	}
	local_irq_save(flags);
	SELECT_MASK(drive, 0);
	id = kmalloc(SECTOR_WORDS*4, GFP_ATOMIC);
	if (!id) {
		local_irq_restore(flags);
		return 0;
	}
	hwif->ata_input_data(drive, id, SECTOR_WORDS);
	(void)ide_read_status(drive);	/* clear drive IRQ */
	local_irq_enable();
	local_irq_restore(flags);
	ide_fix_driveid(id);
	if (id) {
		drive->id->dma_ultra = id->dma_ultra;
		drive->id->dma_mword = id->dma_mword;
		drive->id->dma_1word = id->dma_1word;
		/* anything more ? */
		kfree(id);

		if (drive->using_dma && ide_id_dma_bug(drive))
			ide_dma_off(drive);
	}

	return 1;
}

int ide_config_drive_speed(ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif = drive->hwif;
	int error = 0;
	u8 stat;

//	while (HWGROUP(drive)->busy)
//		msleep(50);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_host_set)	/* check if host supports DMA */
		hwif->dma_host_set(drive, 0);
#endif

	/* Skip setting PIO flow-control modes on pre-EIDE drives */
	if ((speed & 0xf8) == XFER_PIO_0 && !(drive->id->capability & 0x08))
		goto skip;

	/*
	 * Don't use ide_wait_cmd here - it will
	 * attempt to set_geometry and recalibrate,
	 * but for some reason these don't work at
	 * this point (lost interrupt).
	 */
        /*
         * Select the drive, and issue the SETFEATURES command
         */
	disable_irq_nosync(hwif->irq);
	
	/*
	 *	FIXME: we race against the running IRQ here if
	 *	this is called from non IRQ context. If we use
	 *	disable_irq() we hang on the error path. Work
	 *	is needed.
	 */
	 
	udelay(1);
	SELECT_DRIVE(drive);
	SELECT_MASK(drive, 0);
	udelay(1);
	ide_set_irq(drive, 0);
	hwif->OUTB(speed, hwif->io_ports[IDE_NSECTOR_OFFSET]);
	hwif->OUTB(SETFEATURES_XFER, hwif->io_ports[IDE_FEATURE_OFFSET]);
	hwif->OUTBSYNC(drive, WIN_SETFEATURES,
		       hwif->io_ports[IDE_COMMAND_OFFSET]);
	if (drive->quirk_list == 2)
		ide_set_irq(drive, 1);

	error = __ide_wait_stat(drive, drive->ready_stat,
				BUSY_STAT|DRQ_STAT|ERR_STAT,
				WAIT_CMD, &stat);

	SELECT_MASK(drive, 0);

	enable_irq(hwif->irq);

	if (error) {
		(void) ide_dump_status(drive, "set_drive_speed_status", stat);
		return error;
	}

	drive->id->dma_ultra &= ~0xFF00;
	drive->id->dma_mword &= ~0x0F00;
	drive->id->dma_1word &= ~0x0F00;

 skip:
#ifdef CONFIG_BLK_DEV_IDEDMA
	if ((speed >= XFER_SW_DMA_0 || (hwif->host_flags & IDE_HFLAG_VDMA)) &&
	    drive->using_dma)
		hwif->dma_host_set(drive, 1);
	else if (hwif->dma_host_set)	/* check if host supports DMA */
		ide_dma_off_quietly(drive);
#endif

	switch(speed) {
		case XFER_UDMA_7:   drive->id->dma_ultra |= 0x8080; break;
		case XFER_UDMA_6:   drive->id->dma_ultra |= 0x4040; break;
		case XFER_UDMA_5:   drive->id->dma_ultra |= 0x2020; break;
		case XFER_UDMA_4:   drive->id->dma_ultra |= 0x1010; break;
		case XFER_UDMA_3:   drive->id->dma_ultra |= 0x0808; break;
		case XFER_UDMA_2:   drive->id->dma_ultra |= 0x0404; break;
		case XFER_UDMA_1:   drive->id->dma_ultra |= 0x0202; break;
		case XFER_UDMA_0:   drive->id->dma_ultra |= 0x0101; break;
		case XFER_MW_DMA_2: drive->id->dma_mword |= 0x0404; break;
		case XFER_MW_DMA_1: drive->id->dma_mword |= 0x0202; break;
		case XFER_MW_DMA_0: drive->id->dma_mword |= 0x0101; break;
		case XFER_SW_DMA_2: drive->id->dma_1word |= 0x0404; break;
		case XFER_SW_DMA_1: drive->id->dma_1word |= 0x0202; break;
		case XFER_SW_DMA_0: drive->id->dma_1word |= 0x0101; break;
		default: break;
	}
	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;
	return error;
}

/*
 * This should get invoked any time we exit the driver to
 * wait for an interrupt response from a drive.  handler() points
 * at the appropriate code to handle the next interrupt, and a
 * timer is started to prevent us from waiting forever in case
 * something goes wrong (see the ide_timer_expiry() handler later on).
 *
 * See also ide_execute_command
 */
static void __ide_set_handler (ide_drive_t *drive, ide_handler_t *handler,
		      unsigned int timeout, ide_expiry_t *expiry)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);

	BUG_ON(hwgroup->handler);
	hwgroup->handler	= handler;
	hwgroup->expiry		= expiry;
	hwgroup->timer.expires	= jiffies + timeout;
	hwgroup->req_gen_timer	= hwgroup->req_gen;
	add_timer(&hwgroup->timer);
}

void ide_set_handler (ide_drive_t *drive, ide_handler_t *handler,
		      unsigned int timeout, ide_expiry_t *expiry)
{
	unsigned long flags;
	spin_lock_irqsave(&ide_lock, flags);
	__ide_set_handler(drive, handler, timeout, expiry);
	spin_unlock_irqrestore(&ide_lock, flags);
}

EXPORT_SYMBOL(ide_set_handler);
 
/**
 *	ide_execute_command	-	execute an IDE command
 *	@drive: IDE drive to issue the command against
 *	@command: command byte to write
 *	@handler: handler for next phase
 *	@timeout: timeout for command
 *	@expiry:  handler to run on timeout
 *
 *	Helper function to issue an IDE command. This handles the
 *	atomicity requirements, command timing and ensures that the 
 *	handler and IRQ setup do not race. All IDE command kick off
 *	should go via this function or do equivalent locking.
 */

void ide_execute_command(ide_drive_t *drive, u8 cmd, ide_handler_t *handler,
			 unsigned timeout, ide_expiry_t *expiry)
{
	unsigned long flags;
	ide_hwif_t *hwif = HWIF(drive);

	spin_lock_irqsave(&ide_lock, flags);
	__ide_set_handler(drive, handler, timeout, expiry);
	hwif->OUTBSYNC(drive, cmd, hwif->io_ports[IDE_COMMAND_OFFSET]);
	/*
	 * Drive takes 400nS to respond, we must avoid the IRQ being
	 * serviced before that.
	 *
	 * FIXME: we could skip this delay with care on non shared devices
	 */
	ndelay(400);
	spin_unlock_irqrestore(&ide_lock, flags);
}

EXPORT_SYMBOL(ide_execute_command);


/* needed below */
static ide_startstop_t do_reset1 (ide_drive_t *, int);

/*
 * atapi_reset_pollfunc() gets invoked to poll the interface for completion every 50ms
 * during an atapi drive reset operation. If the drive has not yet responded,
 * and we have not yet hit our maximum waiting time, then the timer is restarted
 * for another 50ms.
 */
static ide_startstop_t atapi_reset_pollfunc (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	u8 stat;

	SELECT_DRIVE(drive);
	udelay (10);
	stat = ide_read_status(drive);

	if (OK_STAT(stat, 0, BUSY_STAT))
		printk("%s: ATAPI reset complete\n", drive->name);
	else {
		if (time_before(jiffies, hwgroup->poll_timeout)) {
			ide_set_handler(drive, &atapi_reset_pollfunc, HZ/20, NULL);
			/* continue polling */
			return ide_started;
		}
		/* end of polling */
		hwgroup->polling = 0;
		printk("%s: ATAPI reset timed-out, status=0x%02x\n",
				drive->name, stat);
		/* do it the old fashioned way */
		return do_reset1(drive, 1);
	}
	/* done polling */
	hwgroup->polling = 0;
	hwgroup->resetting = 0;
	return ide_stopped;
}

/*
 * reset_pollfunc() gets invoked to poll the interface for completion every 50ms
 * during an ide reset operation. If the drives have not yet responded,
 * and we have not yet hit our maximum waiting time, then the timer is restarted
 * for another 50ms.
 */
static ide_startstop_t reset_pollfunc (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	ide_hwif_t *hwif	= HWIF(drive);
	u8 tmp;

	if (hwif->reset_poll != NULL) {
		if (hwif->reset_poll(drive)) {
			printk(KERN_ERR "%s: host reset_poll failure for %s.\n",
				hwif->name, drive->name);
			return ide_stopped;
		}
	}

	tmp = ide_read_status(drive);

	if (!OK_STAT(tmp, 0, BUSY_STAT)) {
		if (time_before(jiffies, hwgroup->poll_timeout)) {
			ide_set_handler(drive, &reset_pollfunc, HZ/20, NULL);
			/* continue polling */
			return ide_started;
		}
		printk("%s: reset timed-out, status=0x%02x\n", hwif->name, tmp);
		drive->failures++;
	} else  {
		printk("%s: reset: ", hwif->name);
		tmp = ide_read_error(drive);

		if (tmp == 1) {
			printk("success\n");
			drive->failures = 0;
		} else {
			drive->failures++;
			printk("master: ");
			switch (tmp & 0x7f) {
				case 1: printk("passed");
					break;
				case 2: printk("formatter device error");
					break;
				case 3: printk("sector buffer error");
					break;
				case 4: printk("ECC circuitry error");
					break;
				case 5: printk("controlling MPU error");
					break;
				default:printk("error (0x%02x?)", tmp);
			}
			if (tmp & 0x80)
				printk("; slave: failed");
			printk("\n");
		}
	}
	hwgroup->polling = 0;	/* done polling */
	hwgroup->resetting = 0; /* done reset attempt */
	return ide_stopped;
}

static void ide_disk_pre_reset(ide_drive_t *drive)
{
	int legacy = (drive->id->cfs_enable_2 & 0x0400) ? 0 : 1;

	drive->special.all = 0;
	drive->special.b.set_geometry = legacy;
	drive->special.b.recalibrate  = legacy;
	drive->mult_count = 0;
	if (!drive->keep_settings && !drive->using_dma)
		drive->mult_req = 0;
	if (drive->mult_req != drive->mult_count)
		drive->special.b.set_multmode = 1;
}

static void pre_reset(ide_drive_t *drive)
{
	if (drive->media == ide_disk)
		ide_disk_pre_reset(drive);
	else
		drive->post_reset = 1;

	if (drive->using_dma) {
		if (drive->crc_count)
			ide_check_dma_crc(drive);
		else
			ide_dma_off(drive);
	}

	if (!drive->keep_settings) {
		if (!drive->using_dma) {
			drive->unmask = 0;
			drive->io_32bit = 0;
		}
		return;
	}

	if (HWIF(drive)->pre_reset != NULL)
		HWIF(drive)->pre_reset(drive);

	if (drive->current_speed != 0xff)
		drive->desired_speed = drive->current_speed;
	drive->current_speed = 0xff;
}

/*
 * do_reset1() attempts to recover a confused drive by resetting it.
 * Unfortunately, resetting a disk drive actually resets all devices on
 * the same interface, so it can really be thought of as resetting the
 * interface rather than resetting the drive.
 *
 * ATAPI devices have their own reset mechanism which allows them to be
 * individually reset without clobbering other devices on the same interface.
 *
 * Unfortunately, the IDE interface does not generate an interrupt to let
 * us know when the reset operation has finished, so we must poll for this.
 * Equally poor, though, is the fact that this may a very long time to complete,
 * (up to 30 seconds worstcase).  So, instead of busy-waiting here for it,
 * we set a timer to poll at 50ms intervals.
 */
static ide_startstop_t do_reset1 (ide_drive_t *drive, int do_not_try_atapi)
{
	unsigned int unit;
	unsigned long flags;
	ide_hwif_t *hwif;
	ide_hwgroup_t *hwgroup;
	u8 ctl;

	spin_lock_irqsave(&ide_lock, flags);
	hwif = HWIF(drive);
	hwgroup = HWGROUP(drive);

	/* We must not reset with running handlers */
	BUG_ON(hwgroup->handler != NULL);

	/* For an ATAPI device, first try an ATAPI SRST. */
	if (drive->media != ide_disk && !do_not_try_atapi) {
		hwgroup->resetting = 1;
		pre_reset(drive);
		SELECT_DRIVE(drive);
		udelay (20);
		hwif->OUTBSYNC(drive, WIN_SRST,
			       hwif->io_ports[IDE_COMMAND_OFFSET]);
		ndelay(400);
		hwgroup->poll_timeout = jiffies + WAIT_WORSTCASE;
		hwgroup->polling = 1;
		__ide_set_handler(drive, &atapi_reset_pollfunc, HZ/20, NULL);
		spin_unlock_irqrestore(&ide_lock, flags);
		return ide_started;
	}

	/*
	 * First, reset any device state data we were maintaining
	 * for any of the drives on this interface.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit)
		pre_reset(&hwif->drives[unit]);

	if (hwif->io_ports[IDE_CONTROL_OFFSET] == 0) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return ide_stopped;
	}

	hwgroup->resetting = 1;
	/*
	 * Note that we also set nIEN while resetting the device,
	 * to mask unwanted interrupts from the interface during the reset.
	 * However, due to the design of PC hardware, this will cause an
	 * immediate interrupt due to the edge transition it produces.
	 * This single interrupt gives us a "fast poll" for drives that
	 * recover from reset very quickly, saving us the first 50ms wait time.
	 */
	/* set SRST and nIEN */
	hwif->OUTBSYNC(drive, drive->ctl|6, hwif->io_ports[IDE_CONTROL_OFFSET]);
	/* more than enough time */
	udelay(10);
	if (drive->quirk_list == 2)
		ctl = drive->ctl;	/* clear SRST and nIEN */
	else
		ctl = drive->ctl | 2;	/* clear SRST, leave nIEN */
	hwif->OUTBSYNC(drive, ctl, hwif->io_ports[IDE_CONTROL_OFFSET]);
	/* more than enough time */
	udelay(10);
	hwgroup->poll_timeout = jiffies + WAIT_WORSTCASE;
	hwgroup->polling = 1;
	__ide_set_handler(drive, &reset_pollfunc, HZ/20, NULL);

	/*
	 * Some weird controller like resetting themselves to a strange
	 * state when the disks are reset this way. At least, the Winbond
	 * 553 documentation says that
	 */
	if (hwif->resetproc)
		hwif->resetproc(drive);

	spin_unlock_irqrestore(&ide_lock, flags);
	return ide_started;
}

/*
 * ide_do_reset() is the entry point to the drive/interface reset code.
 */

ide_startstop_t ide_do_reset (ide_drive_t *drive)
{
	return do_reset1(drive, 0);
}

EXPORT_SYMBOL(ide_do_reset);

/*
 * ide_wait_not_busy() waits for the currently selected device on the hwif
 * to report a non-busy status, see comments in ide_probe_port().
 */
int ide_wait_not_busy(ide_hwif_t *hwif, unsigned long timeout)
{
	u8 stat = 0;

	while(timeout--) {
		/*
		 * Turn this into a schedule() sleep once I'm sure
		 * about locking issues (2.5 work ?).
		 */
		mdelay(1);
		stat = hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
		if ((stat & BUSY_STAT) == 0)
			return 0;
		/*
		 * Assume a value of 0xff means nothing is connected to
		 * the interface and it doesn't implement the pull-down
		 * resistor on D7.
		 */
		if (stat == 0xff)
			return -ENODEV;
		touch_softlockup_watchdog();
		touch_nmi_watchdog();
	}
	return -EBUSY;
}

EXPORT_SYMBOL_GPL(ide_wait_not_busy);

