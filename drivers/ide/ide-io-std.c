
#include <linux/kernel.h>
#include <linux/ide.h>

#if defined(CONFIG_ARM) || defined(CONFIG_M68K) || defined(CONFIG_MIPS) || \
    defined(CONFIG_PARISC) || defined(CONFIG_PPC) || defined(CONFIG_SPARC)
#include <asm/ide.h>
#else
#include <asm-generic/ide_iops.h>
#endif

/*
 *	Conventional PIO operations for ATA devices
 */

static u8 ide_inb(unsigned long port)
{
	return (u8) inb(port);
}

static void ide_outb(u8 val, unsigned long port)
{
	outb(val, port);
}

/*
 *	MMIO operations, typically used for SATA controllers
 */

static u8 ide_mm_inb(unsigned long port)
{
	return (u8) readb((void __iomem *) port);
}

static void ide_mm_outb(u8 value, unsigned long port)
{
	writeb(value, (void __iomem *) port);
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

void ide_tf_load(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	struct ide_taskfile *tf = &cmd->tf;
	void (*tf_outb)(u8 addr, unsigned long port);
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;
	u8 HIHI = (cmd->tf_flags & IDE_TFLAG_LBA48) ? 0xE0 : 0xEF;

	if (mmio)
		tf_outb = ide_mm_outb;
	else
		tf_outb = ide_outb;

	if (cmd->ftf_flags & IDE_FTFLAG_FLAGGED)
		HIHI = 0xFF;

	if (cmd->ftf_flags & IDE_FTFLAG_OUT_DATA) {
		u16 data = (tf->hob_data << 8) | tf->data;

		if (mmio)
			writew(data, (void __iomem *)io_ports->data_addr);
		else
			outw(data, io_ports->data_addr);
	}

	if (cmd->tf_flags & IDE_TFLAG_OUT_HOB_FEATURE)
		tf_outb(tf->hob_feature, io_ports->feature_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_HOB_NSECT)
		tf_outb(tf->hob_nsect, io_ports->nsect_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_HOB_LBAL)
		tf_outb(tf->hob_lbal, io_ports->lbal_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_HOB_LBAM)
		tf_outb(tf->hob_lbam, io_ports->lbam_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_HOB_LBAH)
		tf_outb(tf->hob_lbah, io_ports->lbah_addr);

	if (cmd->tf_flags & IDE_TFLAG_OUT_FEATURE)
		tf_outb(tf->feature, io_ports->feature_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_NSECT)
		tf_outb(tf->nsect, io_ports->nsect_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_LBAL)
		tf_outb(tf->lbal, io_ports->lbal_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_LBAM)
		tf_outb(tf->lbam, io_ports->lbam_addr);
	if (cmd->tf_flags & IDE_TFLAG_OUT_LBAH)
		tf_outb(tf->lbah, io_ports->lbah_addr);

	if (cmd->tf_flags & IDE_TFLAG_OUT_DEVICE)
		tf_outb((tf->device & HIHI) | drive->select,
			 io_ports->device_addr);
}
EXPORT_SYMBOL_GPL(ide_tf_load);

void ide_tf_read(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	struct ide_taskfile *tf = &cmd->tf;
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

	if (cmd->ftf_flags & IDE_FTFLAG_IN_DATA) {
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

	if (cmd->tf_flags & IDE_TFLAG_IN_FEATURE)
		tf->feature = tf_inb(io_ports->feature_addr);
	if (cmd->tf_flags & IDE_TFLAG_IN_NSECT)
		tf->nsect  = tf_inb(io_ports->nsect_addr);
	if (cmd->tf_flags & IDE_TFLAG_IN_LBAL)
		tf->lbal   = tf_inb(io_ports->lbal_addr);
	if (cmd->tf_flags & IDE_TFLAG_IN_LBAM)
		tf->lbam   = tf_inb(io_ports->lbam_addr);
	if (cmd->tf_flags & IDE_TFLAG_IN_LBAH)
		tf->lbah   = tf_inb(io_ports->lbah_addr);
	if (cmd->tf_flags & IDE_TFLAG_IN_DEVICE)
		tf->device = tf_inb(io_ports->device_addr);

	if (cmd->tf_flags & IDE_TFLAG_LBA48) {
		tf_outb(ATA_DEVCTL_OBS | 0x80, io_ports->ctl_addr);

		if (cmd->tf_flags & IDE_TFLAG_IN_HOB_FEATURE)
			tf->hob_feature = tf_inb(io_ports->feature_addr);
		if (cmd->tf_flags & IDE_TFLAG_IN_HOB_NSECT)
			tf->hob_nsect   = tf_inb(io_ports->nsect_addr);
		if (cmd->tf_flags & IDE_TFLAG_IN_HOB_LBAL)
			tf->hob_lbal    = tf_inb(io_ports->lbal_addr);
		if (cmd->tf_flags & IDE_TFLAG_IN_HOB_LBAM)
			tf->hob_lbam    = tf_inb(io_ports->lbam_addr);
		if (cmd->tf_flags & IDE_TFLAG_IN_HOB_LBAH)
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
void ide_input_data(ide_drive_t *drive, struct ide_cmd *cmd, void *buf,
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
void ide_output_data(ide_drive_t *drive, struct ide_cmd *cmd, void *buf,
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

const struct ide_tp_ops default_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= ide_read_status,
	.read_altstatus		= ide_read_altstatus,

	.set_irq		= ide_set_irq,

	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= ide_input_data,
	.output_data		= ide_output_data,
};
