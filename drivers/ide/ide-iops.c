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

static void ide_outb (u8 val, unsigned long port)
{
	outb(val, port);
}

/*
 *	MMIO operations, typically used for SATA controllers
 */

static u8 ide_mm_inb (unsigned long port)
{
	return (u8) readb((void __iomem *) port);
}

static void ide_mm_outb (u8 value, unsigned long port)
{
	writeb(value, (void __iomem *) port);
}

void SELECT_DRIVE (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_port_ops *port_ops = hwif->port_ops;
	ide_task_t task;

	if (port_ops && port_ops->selectproc)
		port_ops->selectproc(drive);

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_OUT_DEVICE;

	drive->hwif->tp_ops->tf_load(drive, &task);
}

void SELECT_MASK(ide_drive_t *drive, int mask)
{
	const struct ide_port_ops *port_ops = drive->hwif->port_ops;

	if (port_ops && port_ops->maskproc)
		port_ops->maskproc(drive, mask);
}

void ide_exec_command(ide_hwif_t *hwif, u8 cmd)
{
	if (hwif->host_flags & IDE_HFLAG_MMIO)
		writeb(cmd, (void __iomem *)hwif->io_ports.command_addr);
	else
		outb(cmd, hwif->io_ports.command_addr);
}
EXPORT_SYMBOL_GPL(ide_exec_command);

u8 ide_read_status(ide_hwif_t *hwif)
{
	if (hwif->host_flags & IDE_HFLAG_MMIO)
		return readb((void __iomem *)hwif->io_ports.status_addr);
	else
		return inb(hwif->io_ports.status_addr);
}
EXPORT_SYMBOL_GPL(ide_read_status);

u8 ide_read_altstatus(ide_hwif_t *hwif)
{
	if (hwif->host_flags & IDE_HFLAG_MMIO)
		return readb((void __iomem *)hwif->io_ports.ctl_addr);
	else
		return inb(hwif->io_ports.ctl_addr);
}
EXPORT_SYMBOL_GPL(ide_read_altstatus);

u8 ide_read_sff_dma_status(ide_hwif_t *hwif)
{
	if (hwif->host_flags & IDE_HFLAG_MMIO)
		return readb((void __iomem *)(hwif->dma_base + ATA_DMA_STATUS));
	else
		return inb(hwif->dma_base + ATA_DMA_STATUS);
}
EXPORT_SYMBOL_GPL(ide_read_sff_dma_status);

void ide_set_irq(ide_hwif_t *hwif, int on)
{
	u8 ctl = ATA_DEVCTL_OBS;

	if (on == 4) { /* hack for SRST */
		ctl |= 4;
		on &= ~4;
	}

	ctl |= on ? 0 : 2;

	if (hwif->host_flags & IDE_HFLAG_MMIO)
		writeb(ctl, (void __iomem *)hwif->io_ports.ctl_addr);
	else
		outb(ctl, hwif->io_ports.ctl_addr);
}
EXPORT_SYMBOL_GPL(ide_set_irq);

void ide_tf_load(ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	struct ide_taskfile *tf = &task->tf;
	void (*tf_outb)(u8 addr, unsigned long port);
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;
	u8 HIHI = (task->tf_flags & IDE_TFLAG_LBA48) ? 0xE0 : 0xEF;

	if (mmio)
		tf_outb = ide_mm_outb;
	else
		tf_outb = ide_outb;

	if (task->tf_flags & IDE_TFLAG_FLAGGED)
		HIHI = 0xFF;

	if (task->tf_flags & IDE_TFLAG_OUT_DATA) {
		u16 data = (tf->hob_data << 8) | tf->data;

		if (mmio)
			writew(data, (void __iomem *)io_ports->data_addr);
		else
			outw(data, io_ports->data_addr);
	}

	if (task->tf_flags & IDE_TFLAG_OUT_HOB_FEATURE)
		tf_outb(tf->hob_feature, io_ports->feature_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_NSECT)
		tf_outb(tf->hob_nsect, io_ports->nsect_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_LBAL)
		tf_outb(tf->hob_lbal, io_ports->lbal_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_LBAM)
		tf_outb(tf->hob_lbam, io_ports->lbam_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_HOB_LBAH)
		tf_outb(tf->hob_lbah, io_ports->lbah_addr);

	if (task->tf_flags & IDE_TFLAG_OUT_FEATURE)
		tf_outb(tf->feature, io_ports->feature_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_NSECT)
		tf_outb(tf->nsect, io_ports->nsect_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_LBAL)
		tf_outb(tf->lbal, io_ports->lbal_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_LBAM)
		tf_outb(tf->lbam, io_ports->lbam_addr);
	if (task->tf_flags & IDE_TFLAG_OUT_LBAH)
		tf_outb(tf->lbah, io_ports->lbah_addr);

	if (task->tf_flags & IDE_TFLAG_OUT_DEVICE)
		tf_outb((tf->device & HIHI) | drive->select.all,
			 io_ports->device_addr);
}
EXPORT_SYMBOL_GPL(ide_tf_load);

void ide_tf_read(ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	struct ide_taskfile *tf = &task->tf;
	void (*tf_outb)(u8 addr, unsigned long port);
	u8 (*tf_inb)(unsigned long port);
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;

	if (mmio) {
		tf_outb = ide_mm_outb;
		tf_inb  = ide_mm_inb;
	} else {
		tf_outb = ide_outb;
		tf_inb  = ide_inb;
	}

	if (task->tf_flags & IDE_TFLAG_IN_DATA) {
		u16 data;

		if (mmio)
			data = readw((void __iomem *)io_ports->data_addr);
		else
			data = inw(io_ports->data_addr);

		tf->data = data & 0xff;
		tf->hob_data = (data >> 8) & 0xff;
	}

	/* be sure we're looking at the low order bits */
	tf_outb(ATA_DEVCTL_OBS & ~0x80, io_ports->ctl_addr);

	if (task->tf_flags & IDE_TFLAG_IN_FEATURE)
		tf->feature = tf_inb(io_ports->feature_addr);
	if (task->tf_flags & IDE_TFLAG_IN_NSECT)
		tf->nsect  = tf_inb(io_ports->nsect_addr);
	if (task->tf_flags & IDE_TFLAG_IN_LBAL)
		tf->lbal   = tf_inb(io_ports->lbal_addr);
	if (task->tf_flags & IDE_TFLAG_IN_LBAM)
		tf->lbam   = tf_inb(io_ports->lbam_addr);
	if (task->tf_flags & IDE_TFLAG_IN_LBAH)
		tf->lbah   = tf_inb(io_ports->lbah_addr);
	if (task->tf_flags & IDE_TFLAG_IN_DEVICE)
		tf->device = tf_inb(io_ports->device_addr);

	if (task->tf_flags & IDE_TFLAG_LBA48) {
		tf_outb(ATA_DEVCTL_OBS | 0x80, io_ports->ctl_addr);

		if (task->tf_flags & IDE_TFLAG_IN_HOB_FEATURE)
			tf->hob_feature = tf_inb(io_ports->feature_addr);
		if (task->tf_flags & IDE_TFLAG_IN_HOB_NSECT)
			tf->hob_nsect   = tf_inb(io_ports->nsect_addr);
		if (task->tf_flags & IDE_TFLAG_IN_HOB_LBAL)
			tf->hob_lbal    = tf_inb(io_ports->lbal_addr);
		if (task->tf_flags & IDE_TFLAG_IN_HOB_LBAM)
			tf->hob_lbam    = tf_inb(io_ports->lbam_addr);
		if (task->tf_flags & IDE_TFLAG_IN_HOB_LBAH)
			tf->hob_lbah    = tf_inb(io_ports->lbah_addr);
	}
}
EXPORT_SYMBOL_GPL(ide_tf_read);

/*
 * Some localbus EIDE interfaces require a special access sequence
 * when using 32-bit I/O instructions to transfer data.  We call this
 * the "vlb_sync" sequence, which consists of three successive reads
 * of the sector count register location, with interrupts disabled
 * to ensure that the reads all happen together.
 */
static void ata_vlb_sync(unsigned long port)
{
	(void)inb(port);
	(void)inb(port);
	(void)inb(port);
}

/*
 * This is used for most PIO data transfers *from* the IDE interface
 *
 * These routines will round up any request for an odd number of bytes,
 * so if an odd len is specified, be sure that there's at least one
 * extra byte allocated for the buffer.
 */
void ide_input_data(ide_drive_t *drive, struct request *rq, void *buf,
		    unsigned int len)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	unsigned long data_addr = io_ports->data_addr;
	u8 io_32bit = drive->io_32bit;
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;

	len++;

	if (io_32bit) {
		unsigned long uninitialized_var(flags);

		if ((io_32bit & 2) && !mmio) {
			local_irq_save(flags);
			ata_vlb_sync(io_ports->nsect_addr);
		}

		if (mmio)
			__ide_mm_insl((void __iomem *)data_addr, buf, len / 4);
		else
			insl(data_addr, buf, len / 4);

		if ((io_32bit & 2) && !mmio)
			local_irq_restore(flags);

		if ((len & 3) >= 2) {
			if (mmio)
				__ide_mm_insw((void __iomem *)data_addr,
						(u8 *)buf + (len & ~3), 1);
			else
				insw(data_addr, (u8 *)buf + (len & ~3), 1);
		}
	} else {
		if (mmio)
			__ide_mm_insw((void __iomem *)data_addr, buf, len / 2);
		else
			insw(data_addr, buf, len / 2);
	}
}
EXPORT_SYMBOL_GPL(ide_input_data);

/*
 * This is used for most PIO data transfers *to* the IDE interface
 */
void ide_output_data(ide_drive_t *drive, struct request *rq, void *buf,
		     unsigned int len)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	unsigned long data_addr = io_ports->data_addr;
	u8 io_32bit = drive->io_32bit;
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;

	if (io_32bit) {
		unsigned long uninitialized_var(flags);

		if ((io_32bit & 2) && !mmio) {
			local_irq_save(flags);
			ata_vlb_sync(io_ports->nsect_addr);
		}

		if (mmio)
			__ide_mm_outsl((void __iomem *)data_addr, buf, len / 4);
		else
			outsl(data_addr, buf, len / 4);

		if ((io_32bit & 2) && !mmio)
			local_irq_restore(flags);

		if ((len & 3) >= 2) {
			if (mmio)
				__ide_mm_outsw((void __iomem *)data_addr,
						 (u8 *)buf + (len & ~3), 1);
			else
				outsw(data_addr, (u8 *)buf + (len & ~3), 1);
		}
	} else {
		if (mmio)
			__ide_mm_outsw((void __iomem *)data_addr, buf, len / 2);
		else
			outsw(data_addr, buf, len / 2);
	}
}
EXPORT_SYMBOL_GPL(ide_output_data);

u8 ide_read_error(ide_drive_t *drive)
{
	ide_task_t task;

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_IN_FEATURE;

	drive->hwif->tp_ops->tf_read(drive, &task);

	return task.tf.error;
}
EXPORT_SYMBOL_GPL(ide_read_error);

void ide_read_bcount_and_ireason(ide_drive_t *drive, u16 *bcount, u8 *ireason)
{
	ide_task_t task;

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_IN_LBAH | IDE_TFLAG_IN_LBAM |
			IDE_TFLAG_IN_NSECT;

	drive->hwif->tp_ops->tf_read(drive, &task);

	*bcount = (task.tf.lbah << 8) | task.tf.lbam;
	*ireason = task.tf.nsect & 3;
}
EXPORT_SYMBOL_GPL(ide_read_bcount_and_ireason);

const struct ide_tp_ops default_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= ide_read_status,
	.read_altstatus		= ide_read_altstatus,
	.read_sff_dma_status	= ide_read_sff_dma_status,

	.set_irq		= ide_set_irq,

	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= ide_input_data,
	.output_data		= ide_output_data,
};

void ide_fix_driveid(u16 *id)
{
#ifndef __LITTLE_ENDIAN
# ifdef __BIG_ENDIAN
	int i;

	for (i = 0; i < 256; i++)
		id[i] = __le16_to_cpu(id[i]);
# else
#  error "Please fix <asm/byteorder.h>"
# endif
#endif
}

/*
 * ide_fixstring() cleans up and (optionally) byte-swaps a text string,
 * removing leading/trailing blanks and compressing internal blanks.
 * It is primarily used to tidy up the model name/number fields as
 * returned by the ATA_CMD_ID_ATA[PI] commands.
 */

void ide_fixstring (u8 *s, const int bytecount, const int byteswap)
{
	u8 *p = s, *end = &s[bytecount & ~1]; /* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to host byte order */
		for (p = end ; p != s;)
			be16_to_cpus((u16 *)(p -= 2));
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
		return hwif->dma_ops->dma_test_irq(drive);

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
	if (hwif->io_ports.ctl_addr)
		stat = hwif->tp_ops->read_altstatus(hwif);
	else
		/* Note: this may clear a pending IRQ!! */
		stat = hwif->tp_ops->read_status(hwif);

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
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	unsigned long flags;
	int i;
	u8 stat;

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	stat = tp_ops->read_status(hwif);

	if (stat & BUSY_STAT) {
		local_irq_set(flags);
		timeout += jiffies;
		while ((stat = tp_ops->read_status(hwif)) & BUSY_STAT) {
			if (time_after(jiffies, timeout)) {
				/*
				 * One last read after the timeout in case
				 * heavy interrupt load made us not make any
				 * progress during the timeout..
				 */
				stat = tp_ops->read_status(hwif);
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
		stat = tp_ops->read_status(hwif);

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
 *	@table: list to inspect
 *
 *	Look for a drive in the blacklist and the whitelist tables
 *	Returns 1 if the drive is found in the table.
 */

int ide_in_drive_list(u16 *id, const struct drive_list_entry *table)
{
	for ( ; table->id_model; table++)
		if ((!strcmp(table->id_model, (char *)&id[ATA_ID_PROD])) &&
		    (!table->id_firmware ||
		     strstr((char *)&id[ATA_ID_FW_REV], table->id_firmware)))
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
	{ "TSSTcorp CDDVDW SH-S202H"	, "SB00"	},
	{ "TSSTcorp CDDVDW SH-S202H"	, "SB01"	},
	{ NULL				, NULL		}
};

/*
 *  All hosts that use the 80c ribbon must use!
 *  The name is derived from upper byte of word 93 and the 80c ribbon.
 */
u8 eighty_ninty_three (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u16 *id = drive->id;
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
	if ((id[ATA_ID_HW_CONFIG] & 0x4000) ||
	    (ivb && (id[ATA_ID_HW_CONFIG] & 0x2000)))
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
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	u16 *id;
	unsigned long timeout, flags;
	u8 stat;

	/*
	 * Re-read drive->id for possible DMA mode
	 * change (copied from ide-probe.c)
	 */

	SELECT_MASK(drive, 1);
	tp_ops->set_irq(hwif, 0);
	msleep(50);
	tp_ops->exec_command(hwif, ATA_CMD_ID_ATA);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (time_after(jiffies, timeout)) {
			SELECT_MASK(drive, 0);
			return 0;	/* drive timed-out */
		}

		msleep(50);	/* give drive a breather */
		stat = tp_ops->read_altstatus(hwif);
	} while (stat & BUSY_STAT);

	msleep(50);	/* wait for IRQ and DRQ_STAT */
	stat = tp_ops->read_status(hwif);

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
	tp_ops->input_data(drive, NULL, id, SECTOR_SIZE);
	(void)tp_ops->read_status(hwif);	/* clear drive IRQ */
	local_irq_enable();
	local_irq_restore(flags);
	ide_fix_driveid(id);

	drive->id[ATA_ID_UDMA_MODES]  = id[ATA_ID_UDMA_MODES];
	drive->id[ATA_ID_MWDMA_MODES] = id[ATA_ID_MWDMA_MODES];
	drive->id[ATA_ID_SWDMA_MODES] = id[ATA_ID_SWDMA_MODES];
	/* anything more ? */

	kfree(id);

	if (drive->using_dma && ide_id_dma_bug(drive))
		ide_dma_off(drive);

	return 1;
}

int ide_config_drive_speed(ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	u16 *id = drive->id, i;
	int error = 0;
	u8 stat;
	ide_task_t task;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_ops)	/* check if host supports DMA */
		hwif->dma_ops->dma_host_set(drive, 0);
#endif

	/* Skip setting PIO flow-control modes on pre-EIDE drives */
	if ((speed & 0xf8) == XFER_PIO_0 && ata_id_has_iordy(drive->id) == 0)
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
	tp_ops->set_irq(hwif, 0);

	memset(&task, 0, sizeof(task));
	task.tf_flags = IDE_TFLAG_OUT_FEATURE | IDE_TFLAG_OUT_NSECT;
	task.tf.feature = SETFEATURES_XFER;
	task.tf.nsect   = speed;

	tp_ops->tf_load(drive, &task);

	tp_ops->exec_command(hwif, ATA_CMD_SET_FEATURES);

	if (drive->quirk_list == 2)
		tp_ops->set_irq(hwif, 1);

	error = __ide_wait_stat(drive, drive->ready_stat,
				BUSY_STAT|DRQ_STAT|ERR_STAT,
				WAIT_CMD, &stat);

	SELECT_MASK(drive, 0);

	enable_irq(hwif->irq);

	if (error) {
		(void) ide_dump_status(drive, "set_drive_speed_status", stat);
		return error;
	}

	id[ATA_ID_UDMA_MODES]  &= ~0xFF00;
	id[ATA_ID_MWDMA_MODES] &= ~0x0F00;
	id[ATA_ID_SWDMA_MODES] &= ~0x0F00;

 skip:
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed >= XFER_SW_DMA_0 && drive->using_dma)
		hwif->dma_ops->dma_host_set(drive, 1);
	else if (hwif->dma_ops)	/* check if host supports DMA */
		ide_dma_off_quietly(drive);
#endif

	if (speed >= XFER_UDMA_0) {
		i = 1 << (speed - XFER_UDMA_0);
		id[ATA_ID_UDMA_MODES] |= (i << 8 | i);
	} else if (speed >= XFER_MW_DMA_0) {
		i = 1 << (speed - XFER_MW_DMA_0);
		id[ATA_ID_MWDMA_MODES] |= (i << 8 | i);
	} else if (speed >= XFER_SW_DMA_0) {
		i = 1 << (speed - XFER_SW_DMA_0);
		id[ATA_ID_SWDMA_MODES] |= (i << 8 | i);
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
	hwif->tp_ops->exec_command(hwif, cmd);
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

void ide_execute_pkt_cmd(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned long flags;

	spin_lock_irqsave(&ide_lock, flags);
	hwif->tp_ops->exec_command(hwif, ATA_CMD_PACKET);
	ndelay(400);
	spin_unlock_irqrestore(&ide_lock, flags);
}
EXPORT_SYMBOL_GPL(ide_execute_pkt_cmd);

static inline void ide_complete_drive_reset(ide_drive_t *drive, int err)
{
	struct request *rq = drive->hwif->hwgroup->rq;

	if (rq && blk_special_request(rq) && rq->cmd[0] == REQ_DRIVE_RESET)
		ide_end_request(drive, err ? err : 1, 0);
}

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
	ide_hwif_t *hwif = drive->hwif;
	ide_hwgroup_t *hwgroup = hwif->hwgroup;
	u8 stat;

	SELECT_DRIVE(drive);
	udelay (10);
	stat = hwif->tp_ops->read_status(hwif);

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
	ide_complete_drive_reset(drive, 0);
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
	const struct ide_port_ops *port_ops = hwif->port_ops;
	u8 tmp;
	int err = 0;

	if (port_ops && port_ops->reset_poll) {
		err = port_ops->reset_poll(drive);
		if (err) {
			printk(KERN_ERR "%s: host reset_poll failure for %s.\n",
				hwif->name, drive->name);
			goto out;
		}
	}

	tmp = hwif->tp_ops->read_status(hwif);

	if (!OK_STAT(tmp, 0, BUSY_STAT)) {
		if (time_before(jiffies, hwgroup->poll_timeout)) {
			ide_set_handler(drive, &reset_pollfunc, HZ/20, NULL);
			/* continue polling */
			return ide_started;
		}
		printk("%s: reset timed-out, status=0x%02x\n", hwif->name, tmp);
		drive->failures++;
		err = -EIO;
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
			err = -EIO;
		}
	}
out:
	hwgroup->polling = 0;	/* done polling */
	ide_complete_drive_reset(drive, err);
	return ide_stopped;
}

static void ide_disk_pre_reset(ide_drive_t *drive)
{
	int legacy = (drive->id[ATA_ID_CFS_ENABLE_2] & 0x0400) ? 0 : 1;

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
	const struct ide_port_ops *port_ops = drive->hwif->port_ops;

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

	if (port_ops && port_ops->pre_reset)
		port_ops->pre_reset(drive);

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
	struct ide_io_ports *io_ports;
	const struct ide_tp_ops *tp_ops;
	const struct ide_port_ops *port_ops;

	spin_lock_irqsave(&ide_lock, flags);
	hwif = HWIF(drive);
	hwgroup = HWGROUP(drive);

	io_ports = &hwif->io_ports;

	tp_ops = hwif->tp_ops;

	/* We must not reset with running handlers */
	BUG_ON(hwgroup->handler != NULL);

	/* For an ATAPI device, first try an ATAPI SRST. */
	if (drive->media != ide_disk && !do_not_try_atapi) {
		pre_reset(drive);
		SELECT_DRIVE(drive);
		udelay (20);
		tp_ops->exec_command(hwif, ATA_CMD_DEV_RESET);
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

	if (io_ports->ctl_addr == 0) {
		spin_unlock_irqrestore(&ide_lock, flags);
		ide_complete_drive_reset(drive, -ENXIO);
		return ide_stopped;
	}

	/*
	 * Note that we also set nIEN while resetting the device,
	 * to mask unwanted interrupts from the interface during the reset.
	 * However, due to the design of PC hardware, this will cause an
	 * immediate interrupt due to the edge transition it produces.
	 * This single interrupt gives us a "fast poll" for drives that
	 * recover from reset very quickly, saving us the first 50ms wait time.
	 *
	 * TODO: add ->softreset method and stop abusing ->set_irq
	 */
	/* set SRST and nIEN */
	tp_ops->set_irq(hwif, 4);
	/* more than enough time */
	udelay(10);
	/* clear SRST, leave nIEN (unless device is on the quirk list) */
	tp_ops->set_irq(hwif, drive->quirk_list == 2);
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
	port_ops = hwif->port_ops;
	if (port_ops && port_ops->resetproc)
		port_ops->resetproc(drive);

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
		stat = hwif->tp_ops->read_status(hwif);
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

