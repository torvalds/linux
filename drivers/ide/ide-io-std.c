
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

void ide_write_devctl(ide_hwif_t *hwif, u8 ctl)
{
	if (hwif->host_flags & IDE_HFLAG_MMIO)
		writeb(ctl, (void __iomem *)hwif->io_ports.ctl_addr);
	else
		outb(ctl, hwif->io_ports.ctl_addr);
}
EXPORT_SYMBOL_GPL(ide_write_devctl);

void ide_dev_select(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 select = drive->select | ATA_DEVICE_OBS;

	if (hwif->host_flags & IDE_HFLAG_MMIO)
		writeb(select, (void __iomem *)hwif->io_ports.device_addr);
	else
		outb(select, hwif->io_ports.device_addr);
}
EXPORT_SYMBOL_GPL(ide_dev_select);

void ide_tf_load(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	struct ide_taskfile *tf = &cmd->hob;
	void (*tf_outb)(u8 addr, unsigned long port);
	u8 valid = cmd->valid.out.hob;
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;

	if (mmio)
		tf_outb = ide_mm_outb;
	else
		tf_outb = ide_outb;

	if (valid & IDE_VALID_FEATURE)
		tf_outb(tf->feature, io_ports->feature_addr);
	if (valid & IDE_VALID_NSECT)
		tf_outb(tf->nsect, io_ports->nsect_addr);
	if (valid & IDE_VALID_LBAL)
		tf_outb(tf->lbal, io_ports->lbal_addr);
	if (valid & IDE_VALID_LBAM)
		tf_outb(tf->lbam, io_ports->lbam_addr);
	if (valid & IDE_VALID_LBAH)
		tf_outb(tf->lbah, io_ports->lbah_addr);

	tf = &cmd->tf;
	valid = cmd->valid.out.tf;

	if (valid & IDE_VALID_FEATURE)
		tf_outb(tf->feature, io_ports->feature_addr);
	if (valid & IDE_VALID_NSECT)
		tf_outb(tf->nsect, io_ports->nsect_addr);
	if (valid & IDE_VALID_LBAL)
		tf_outb(tf->lbal, io_ports->lbal_addr);
	if (valid & IDE_VALID_LBAM)
		tf_outb(tf->lbam, io_ports->lbam_addr);
	if (valid & IDE_VALID_LBAH)
		tf_outb(tf->lbah, io_ports->lbah_addr);
	if (valid & IDE_VALID_DEVICE)
		tf_outb(tf->device, io_ports->device_addr);
}
EXPORT_SYMBOL_GPL(ide_tf_load);

void ide_tf_read(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	struct ide_taskfile *tf = &cmd->tf;
	u8 (*tf_inb)(unsigned long port);
	u8 valid = cmd->valid.in.tf;
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;

	if (mmio)
		tf_inb  = ide_mm_inb;
	else
		tf_inb  = ide_inb;

	/* be sure we're looking at the low order bits */
	hwif->tp_ops->write_devctl(hwif, ATA_DEVCTL_OBS);

	if (valid & IDE_VALID_ERROR)
		tf->error  = tf_inb(io_ports->feature_addr);
	if (valid & IDE_VALID_NSECT)
		tf->nsect  = tf_inb(io_ports->nsect_addr);
	if (valid & IDE_VALID_LBAL)
		tf->lbal   = tf_inb(io_ports->lbal_addr);
	if (valid & IDE_VALID_LBAM)
		tf->lbam   = tf_inb(io_ports->lbam_addr);
	if (valid & IDE_VALID_LBAH)
		tf->lbah   = tf_inb(io_ports->lbah_addr);
	if (valid & IDE_VALID_DEVICE)
		tf->device = tf_inb(io_ports->device_addr);

	if (cmd->tf_flags & IDE_TFLAG_LBA48) {
		hwif->tp_ops->write_devctl(hwif, ATA_HOB | ATA_DEVCTL_OBS);

		tf = &cmd->hob;
		valid = cmd->valid.in.hob;

		if (valid & IDE_VALID_ERROR)
			tf->error = tf_inb(io_ports->feature_addr);
		if (valid & IDE_VALID_NSECT)
			tf->nsect = tf_inb(io_ports->nsect_addr);
		if (valid & IDE_VALID_LBAL)
			tf->lbal  = tf_inb(io_ports->lbal_addr);
		if (valid & IDE_VALID_LBAM)
			tf->lbam  = tf_inb(io_ports->lbam_addr);
		if (valid & IDE_VALID_LBAH)
			tf->lbah  = tf_inb(io_ports->lbah_addr);
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
	unsigned int words = (len + 1) >> 1;
	u8 io_32bit = drive->io_32bit;
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;

	if (io_32bit) {
		unsigned long uninitialized_var(flags);

		if ((io_32bit & 2) && !mmio) {
			local_irq_save(flags);
			ata_vlb_sync(io_ports->nsect_addr);
		}

		words >>= 1;
		if (mmio)
			__ide_mm_insl((void __iomem *)data_addr, buf, words);
		else
			insl(data_addr, buf, words);

		if ((io_32bit & 2) && !mmio)
			local_irq_restore(flags);

		if (((len + 1) & 3) < 2)
			return;

		buf += len & ~3;
		words = 1;
	}

	if (mmio)
		__ide_mm_insw((void __iomem *)data_addr, buf, words);
	else
		insw(data_addr, buf, words);
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
	unsigned int words = (len + 1) >> 1;
	u8 io_32bit = drive->io_32bit;
	u8 mmio = (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;

	if (io_32bit) {
		unsigned long uninitialized_var(flags);

		if ((io_32bit & 2) && !mmio) {
			local_irq_save(flags);
			ata_vlb_sync(io_ports->nsect_addr);
		}

		words >>= 1;
		if (mmio)
			__ide_mm_outsl((void __iomem *)data_addr, buf, words);
		else
			outsl(data_addr, buf, words);

		if ((io_32bit & 2) && !mmio)
			local_irq_restore(flags);

		if (((len + 1) & 3) < 2)
			return;

		buf += len & ~3;
		words = 1;
	}

	if (mmio)
		__ide_mm_outsw((void __iomem *)data_addr, buf, words);
	else
		outsw(data_addr, buf, words);
}
EXPORT_SYMBOL_GPL(ide_output_data);

const struct ide_tp_ops default_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= ide_read_status,
	.read_altstatus		= ide_read_altstatus,
	.write_devctl		= ide_write_devctl,

	.dev_select		= ide_dev_select,
	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= ide_input_data,
	.output_data		= ide_output_data,
};
