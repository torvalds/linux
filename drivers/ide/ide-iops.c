/*
 * linux/drivers/ide/ide-iops.c	Version 0.37	Mar 05, 2003
 *
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

static u32 ide_inl (unsigned long port)
{
	return (u32) inl(port);
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

static void ide_outl (u32 val, unsigned long port)
{
	outl(val, port);
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
	hwif->OUTL	= ide_outl;
	hwif->OUTSW	= ide_outsw;
	hwif->OUTSL	= ide_outsl;
	hwif->INB	= ide_inb;
	hwif->INW	= ide_inw;
	hwif->INL	= ide_inl;
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

static u32 ide_mm_inl (unsigned long port)
{
	return (u32) readl((void __iomem *) port);
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

static void ide_mm_outl (u32 value, unsigned long port)
{
	writel(value, (void __iomem *) port);
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
	hwif->OUTL	= ide_mm_outl;
	hwif->OUTSW	= ide_mm_outsw;
	hwif->OUTSL	= ide_mm_outsl;
	hwif->INB	= ide_mm_inb;
	hwif->INW	= ide_mm_inw;
	hwif->INL	= ide_mm_inl;
	hwif->INSW	= ide_mm_insw;
	hwif->INSL	= ide_mm_insl;
}

EXPORT_SYMBOL(default_hwif_mmiops);

u32 ide_read_24 (ide_drive_t *drive)
{
	u8 hcyl = HWIF(drive)->INB(IDE_HCYL_REG);
	u8 lcyl = HWIF(drive)->INB(IDE_LCYL_REG);
	u8 sect = HWIF(drive)->INB(IDE_SECTOR_REG);
	return (hcyl<<16)|(lcyl<<8)|sect;
}

void SELECT_DRIVE (ide_drive_t *drive)
{
	if (HWIF(drive)->selectproc)
		HWIF(drive)->selectproc(drive);
	HWIF(drive)->OUTB(drive->select.all, IDE_SELECT_REG);
}

EXPORT_SYMBOL(SELECT_DRIVE);

void SELECT_INTERRUPT (ide_drive_t *drive)
{
	if (HWIF(drive)->intrproc)
		HWIF(drive)->intrproc(drive);
	else
		HWIF(drive)->OUTB(drive->ctl|2, IDE_CONTROL_REG);
}

void SELECT_MASK (ide_drive_t *drive, int mask)
{
	if (HWIF(drive)->maskproc)
		HWIF(drive)->maskproc(drive, mask);
}

void QUIRK_LIST (ide_drive_t *drive)
{
	if (HWIF(drive)->quirkproc)
		drive->quirk_list = HWIF(drive)->quirkproc(drive);
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
			ata_vlb_sync(drive, IDE_NSECTOR_REG);
			hwif->INSL(IDE_DATA_REG, buffer, wcount);
			local_irq_restore(flags);
		} else
			hwif->INSL(IDE_DATA_REG, buffer, wcount);
	} else {
		hwif->INSW(IDE_DATA_REG, buffer, wcount<<1);
	}
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
			ata_vlb_sync(drive, IDE_NSECTOR_REG);
			hwif->OUTSL(IDE_DATA_REG, buffer, wcount);
			local_irq_restore(flags);
		} else
			hwif->OUTSL(IDE_DATA_REG, buffer, wcount);
	} else {
		hwif->OUTSW(IDE_DATA_REG, buffer, wcount<<1);
	}
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
		insw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI || CONFIG_Q40 */
	hwif->ata_input_data(drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		hwif->INSW(IDE_DATA_REG, ((u8 *)buffer)+(bytecount & ~0x03), 1);
}

static void atapi_output_bytes(ide_drive_t *drive, void *buffer, u32 bytecount)
{
	ide_hwif_t *hwif = HWIF(drive);

	++bytecount;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		outsw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI || CONFIG_Q40 */
	hwif->ata_output_data(drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		hwif->OUTSW(IDE_DATA_REG, ((u8*)buffer)+(bytecount & ~0x03), 1);
}

void default_hwif_transport(ide_hwif_t *hwif)
{
	hwif->ata_input_data		= ata_input_data;
	hwif->ata_output_data		= ata_output_data;
	hwif->atapi_input_bytes		= atapi_input_bytes;
	hwif->atapi_output_bytes	= atapi_output_bytes;
}

/*
 * Beginning of Taskfile OPCODE Library and feature sets.
 */
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

/* FIXME: exported for use by the USB storage (isd200.c) code only */
EXPORT_SYMBOL(ide_fix_driveid);

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

#ifdef CONFIG_IDEPCI_SHARE_IRQ
	/*
	 * We do a passive status test under shared PCI interrupts on
	 * cards that truly share the ATA side interrupt, but may also share
	 * an interrupt with another pci card/device.  We make no assumptions
	 * about possible isa-pnp and pci-pnp issues yet.
	 */
	if (IDE_CONTROL_REG)
		stat = hwif->INB(IDE_ALTSTATUS_REG);
	else
#endif /* CONFIG_IDEPCI_SHARE_IRQ */
		/* Note: this may clear a pending IRQ!! */
		stat = hwif->INB(IDE_STATUS_REG);

	if (stat & BUSY_STAT)
		/* drive busy:  definitely not interrupting */
		return 0;

	/* drive ready: *might* be interrupting */
	return 1;
}

EXPORT_SYMBOL(drive_is_ready);

/*
 * Global for All, and taken from ide-pmac.c. Can be called
 * with spinlock held & IRQs disabled, so don't schedule !
 */
int wait_for_ready (ide_drive_t *drive, int timeout)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= 0;

	while(--timeout) {
		stat = hwif->INB(IDE_STATUS_REG);
		if (!(stat & BUSY_STAT)) {
			if (drive->ready_stat == 0)
				break;
			else if ((stat & drive->ready_stat)||(stat & ERR_STAT))
				break;
		}
		mdelay(1);
	}
	if ((stat & ERR_STAT) || timeout <= 0) {
		if (stat & ERR_STAT) {
			printk(KERN_ERR "%s: wait_for_ready, "
				"error status: %x\n", drive->name, stat);
		}
		return 1;
	}
	return 0;
}

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return 1 after invoking ide_error() -- caller should just return.
 *
 * This routine should get fixed to not hog the cpu during extra long waits..
 * That could be done by busy-waiting for the first jiffy or two, and then
 * setting a timer to wake up at half second intervals thereafter,
 * until timeout is achieved, before timing out.
 */
int ide_wait_stat (ide_startstop_t *startstop, ide_drive_t *drive, u8 good, u8 bad, unsigned long timeout)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 stat;
	int i;
	unsigned long flags;
 
	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		*startstop = ide_stopped;
		return 1;
	}

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	if ((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) {
		local_irq_set(flags);
		timeout += jiffies;
		while ((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) {
			if (time_after(jiffies, timeout)) {
				/*
				 * One last read after the timeout in case
				 * heavy interrupt load made us not make any
				 * progress during the timeout..
				 */
				stat = hwif->INB(IDE_STATUS_REG);
				if (!(stat & BUSY_STAT))
					break;

				local_irq_restore(flags);
				*startstop = ide_error(drive, "status timeout", stat);
				return 1;
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
		if (OK_STAT((stat = hwif->INB(IDE_STATUS_REG)), good, bad))
			return 0;
	}
	*startstop = ide_error(drive, "status error", stat);
	return 1;
}

EXPORT_SYMBOL(ide_wait_stat);

/*
 *  All hosts that use the 80c ribbon must use!
 *  The name is derived from upper byte of word 93 and the 80c ribbon.
 */
u8 eighty_ninty_three (ide_drive_t *drive)
{
	if(HWIF(drive)->udma_four == 0)
		return 0;

	/* Check for SATA but only if we are ATA5 or higher */
	if (drive->id->hw_config == 0 && (drive->id->major_rev_num & 0x7FE0))
		return 1;
	if (!(drive->id->hw_config & 0x6000))
		return 0;
#ifndef CONFIG_IDEDMA_IVB
	if(!(drive->id->hw_config & 0x4000))
		return 0;
#endif /* CONFIG_IDEDMA_IVB */
	return 1;
}

EXPORT_SYMBOL(eighty_ninty_three);

int ide_ata66_check (ide_drive_t *drive, ide_task_t *args)
{
	if ((args->tfRegister[IDE_COMMAND_OFFSET] == WIN_SETFEATURES) &&
	    (args->tfRegister[IDE_SECTOR_OFFSET] > XFER_UDMA_2) &&
	    (args->tfRegister[IDE_FEATURE_OFFSET] == SETFEATURES_XFER)) {
#ifndef CONFIG_IDEDMA_IVB
		if ((drive->id->hw_config & 0x6000) == 0) {
#else /* !CONFIG_IDEDMA_IVB */
		if (((drive->id->hw_config & 0x2000) == 0) ||
		    ((drive->id->hw_config & 0x4000) == 0)) {
#endif /* CONFIG_IDEDMA_IVB */
			printk("%s: Speed warnings UDMA 3/4/5 is not "
				"functional.\n", drive->name);
			return 1;
		}
		if (!HWIF(drive)->udma_four) {
			printk("%s: Speed warnings UDMA 3/4/5 is not "
				"functional.\n",
				HWIF(drive)->name);
			return 1;
		}
	}
	return 0;
}

/*
 * Backside of HDIO_DRIVE_CMD call of SETFEATURES_XFER.
 * 1 : Safe to update drive->id DMA registers.
 * 0 : OOPs not allowed.
 */
int set_transfer (ide_drive_t *drive, ide_task_t *args)
{
	if ((args->tfRegister[IDE_COMMAND_OFFSET] == WIN_SETFEATURES) &&
	    (args->tfRegister[IDE_SECTOR_OFFSET] >= XFER_SW_DMA_0) &&
	    (args->tfRegister[IDE_FEATURE_OFFSET] == SETFEATURES_XFER) &&
	    (drive->id->dma_ultra ||
	     drive->id->dma_mword ||
	     drive->id->dma_1word))
		return 1;

	return 0;
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static u8 ide_auto_reduce_xfer (ide_drive_t *drive)
{
	if (!drive->crc_count)
		return drive->current_speed;
	drive->crc_count = 0;

	switch(drive->current_speed) {
		case XFER_UDMA_7:	return XFER_UDMA_6;
		case XFER_UDMA_6:	return XFER_UDMA_5;
		case XFER_UDMA_5:	return XFER_UDMA_4;
		case XFER_UDMA_4:	return XFER_UDMA_3;
		case XFER_UDMA_3:	return XFER_UDMA_2;
		case XFER_UDMA_2:	return XFER_UDMA_1;
		case XFER_UDMA_1:	return XFER_UDMA_0;
			/*
			 * OOPS we do not goto non Ultra DMA modes
			 * without iCRC's available we force
			 * the system to PIO and make the user
			 * invoke the ATA-1 ATA-2 DMA modes.
			 */
		case XFER_UDMA_0:
		default:		return XFER_PIO_4;
	}
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

/*
 * Update the 
 */
int ide_driveid_update (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id;
#if 0
	id = kmalloc(SECTOR_WORDS*4, GFP_ATOMIC);
	if (!id)
		return 0;

	taskfile_lib_get_identify(drive, (char *)&id);

	ide_fix_driveid(id);
	if (id) {
		drive->id->dma_ultra = id->dma_ultra;
		drive->id->dma_mword = id->dma_mword;
		drive->id->dma_1word = id->dma_1word;
		/* anything more ? */
		kfree(id);
	}
	return 1;
#else
	/*
	 * Re-read drive->id for possible DMA mode
	 * change (copied from ide-probe.c)
	 */
	unsigned long timeout, flags;

	SELECT_MASK(drive, 1);
	if (IDE_CONTROL_REG)
		hwif->OUTB(drive->ctl,IDE_CONTROL_REG);
	msleep(50);
	hwif->OUTB(WIN_IDENTIFY, IDE_COMMAND_REG);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (time_after(jiffies, timeout)) {
			SELECT_MASK(drive, 0);
			return 0;	/* drive timed-out */
		}
		msleep(50);	/* give drive a breather */
	} while (hwif->INB(IDE_ALTSTATUS_REG) & BUSY_STAT);
	msleep(50);	/* wait for IRQ and DRQ_STAT */
	if (!OK_STAT(hwif->INB(IDE_STATUS_REG),DRQ_STAT,BAD_R_STAT)) {
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
	ata_input_data(drive, id, SECTOR_WORDS);
	(void) hwif->INB(IDE_STATUS_REG);	/* clear drive IRQ */
	local_irq_enable();
	local_irq_restore(flags);
	ide_fix_driveid(id);
	if (id) {
		drive->id->dma_ultra = id->dma_ultra;
		drive->id->dma_mword = id->dma_mword;
		drive->id->dma_1word = id->dma_1word;
		/* anything more ? */
		kfree(id);
	}

	return 1;
#endif
}

/*
 * Similar to ide_wait_stat(), except it never calls ide_error internally.
 * This is a kludge to handle the new ide_config_drive_speed() function,
 * and should not otherwise be used anywhere.  Eventually, the tuneproc's
 * should be updated to return ide_startstop_t, in which case we can get
 * rid of this abomination again.  :)   -ml
 *
 * It is gone..........
 *
 * const char *msg == consider adding for verbose errors.
 */
int ide_config_drive_speed (ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	int	i, error	= 1;
	u8 stat;

//	while (HWGROUP(drive)->busy)
//		msleep(50);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->ide_dma_check)	 /* check if host supports DMA */
		hwif->ide_dma_host_off(drive);
#endif

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
	if (IDE_CONTROL_REG)
		hwif->OUTB(drive->ctl | 2, IDE_CONTROL_REG);
	hwif->OUTB(speed, IDE_NSECTOR_REG);
	hwif->OUTB(SETFEATURES_XFER, IDE_FEATURE_REG);
	hwif->OUTB(WIN_SETFEATURES, IDE_COMMAND_REG);
	if ((IDE_CONTROL_REG) && (drive->quirk_list == 2))
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	udelay(1);
	/*
	 * Wait for drive to become non-BUSY
	 */
	if ((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) {
		unsigned long flags, timeout;
		local_irq_set(flags);
		timeout = jiffies + WAIT_CMD;
		while ((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) {
			if (time_after(jiffies, timeout))
				break;
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
		if (OK_STAT((stat = hwif->INB(IDE_STATUS_REG)), DRIVE_READY, BUSY_STAT|DRQ_STAT|ERR_STAT)) {
			error = 0;
			break;
		}
	}

	SELECT_MASK(drive, 0);

	enable_irq(hwif->irq);

	if (error) {
		(void) ide_dump_status(drive, "set_drive_speed_status", stat);
		return error;
	}

	drive->id->dma_ultra &= ~0xFF00;
	drive->id->dma_mword &= ~0x0F00;
	drive->id->dma_1word &= ~0x0F00;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed >= XFER_SW_DMA_0)
		hwif->ide_dma_host_on(drive);
	else if (hwif->ide_dma_check)	/* check if host supports DMA */
		hwif->ide_dma_off_quietly(drive);
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

EXPORT_SYMBOL(ide_config_drive_speed);


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

	if (hwgroup->handler != NULL) {
		printk(KERN_CRIT "%s: ide_set_handler: handler not null; "
			"old=%p, new=%p\n",
			drive->name, hwgroup->handler, handler);
	}
	hwgroup->handler	= handler;
	hwgroup->expiry		= expiry;
	hwgroup->timer.expires	= jiffies + timeout;
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
 
void ide_execute_command(ide_drive_t *drive, task_ioreg_t cmd, ide_handler_t *handler, unsigned timeout, ide_expiry_t *expiry)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	ide_hwif_t *hwif = HWIF(drive);
	
	spin_lock_irqsave(&ide_lock, flags);
	
	BUG_ON(hwgroup->handler);
	hwgroup->handler	= handler;
	hwgroup->expiry		= expiry;
	hwgroup->timer.expires	= jiffies + timeout;
	add_timer(&hwgroup->timer);
	hwif->OUTBSYNC(drive, cmd, IDE_COMMAND_REG);
	/* Drive takes 400nS to respond, we must avoid the IRQ being
	   serviced before that. 
	   
	   FIXME: we could skip this delay with care on non shared
	   devices 
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
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat;

	SELECT_DRIVE(drive);
	udelay (10);

	if (OK_STAT(stat = hwif->INB(IDE_STATUS_REG), 0, BUSY_STAT)) {
		printk("%s: ATAPI reset complete\n", drive->name);
	} else {
		if (time_before(jiffies, hwgroup->poll_timeout)) {
			BUG_ON(HWGROUP(drive)->handler != NULL);
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

	if (!OK_STAT(tmp = hwif->INB(IDE_STATUS_REG), 0, BUSY_STAT)) {
		if (time_before(jiffies, hwgroup->poll_timeout)) {
			BUG_ON(HWGROUP(drive)->handler != NULL);
			ide_set_handler(drive, &reset_pollfunc, HZ/20, NULL);
			/* continue polling */
			return ide_started;
		}
		printk("%s: reset timed-out, status=0x%02x\n", hwif->name, tmp);
		drive->failures++;
	} else  {
		printk("%s: reset: ", hwif->name);
		if ((tmp = hwif->INB(IDE_ERROR_REG)) == 1) {
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

static void check_dma_crc(ide_drive_t *drive)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (drive->crc_count) {
		(void) HWIF(drive)->ide_dma_off_quietly(drive);
		ide_set_xfer_rate(drive, ide_auto_reduce_xfer(drive));
		if (drive->current_speed >= XFER_SW_DMA_0)
			(void) HWIF(drive)->ide_dma_on(drive);
	} else
		(void)__ide_dma_off(drive);
#endif
}

static void ide_disk_pre_reset(ide_drive_t *drive)
{
	int legacy = (drive->id->cfs_enable_2 & 0x0400) ? 0 : 1;

	drive->special.all = 0;
	drive->special.b.set_geometry = legacy;
	drive->special.b.recalibrate  = legacy;
	if (OK_TO_RESET_CONTROLLER)
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

	if (!drive->keep_settings) {
		if (drive->using_dma) {
			check_dma_crc(drive);
		} else {
			drive->unmask = 0;
			drive->io_32bit = 0;
		}
		return;
	}
	if (drive->using_dma)
		check_dma_crc(drive);

	if (HWIF(drive)->pre_reset != NULL)
		HWIF(drive)->pre_reset(drive);

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
		hwif->OUTBSYNC(drive, WIN_SRST, IDE_COMMAND_REG);
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

#if OK_TO_RESET_CONTROLLER
	if (!IDE_CONTROL_REG) {
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
	hwif->OUTBSYNC(drive, drive->ctl|6,IDE_CONTROL_REG);
	/* more than enough time */
	udelay(10);
	if (drive->quirk_list == 2) {
		/* clear SRST and nIEN */
		hwif->OUTBSYNC(drive, drive->ctl, IDE_CONTROL_REG);
	} else {
		/* clear SRST, leave nIEN */
		hwif->OUTBSYNC(drive, drive->ctl|2, IDE_CONTROL_REG);
	}
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
	if (hwif->resetproc != NULL) {
		hwif->resetproc(drive);
	}
	
#endif	/* OK_TO_RESET_CONTROLLER */

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
 * to report a non-busy status, see comments in probe_hwif().
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

