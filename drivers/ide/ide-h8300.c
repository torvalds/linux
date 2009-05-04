/*
 * H8/300 generic IDE interface
 */

#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#define DRV_NAME "ide-h8300"

#define bswap(d) \
({					\
	u16 r;				\
	__asm__("mov.b %w1,r1h\n\t"	\
		"mov.b %x1,r1l\n\t"	\
		"mov.w r1,%0"		\
		:"=r"(r)		\
		:"r"(d)			\
		:"er1");		\
	(r);				\
})

static void mm_outsw(unsigned long addr, void *buf, u32 len)
{
	unsigned short *bp = (unsigned short *)buf;
	for (; len > 0; len--, bp++)
		*(volatile u16 *)addr = bswap(*bp);
}

static void mm_insw(unsigned long addr, void *buf, u32 len)
{
	unsigned short *bp = (unsigned short *)buf;
	for (; len > 0; len--, bp++)
		*bp = bswap(*(volatile u16 *)addr);
}

static void h8300_input_data(ide_drive_t *drive, struct ide_cmd *cmd,
			     void *buf, unsigned int len)
{
	mm_insw(drive->hwif->io_ports.data_addr, buf, (len + 1) / 2);
}

static void h8300_output_data(ide_drive_t *drive, struct ide_cmd *cmd,
			      void *buf, unsigned int len)
{
	mm_outsw(drive->hwif->io_ports.data_addr, buf, (len + 1) / 2);
}

static const struct ide_tp_ops h8300_tp_ops = {
	.exec_command		= ide_exec_command,
	.read_status		= ide_read_status,
	.read_altstatus		= ide_read_altstatus,
	.write_devctl		= ide_write_devctl,

	.dev_select		= ide_dev_select,
	.tf_load		= ide_tf_load,
	.tf_read		= ide_tf_read,

	.input_data		= h8300_input_data,
	.output_data		= h8300_output_data,
};

#define H8300_IDE_GAP (2)

static inline void hw_setup(hw_regs_t *hw)
{
	int i;

	memset(hw, 0, sizeof(hw_regs_t));
	for (i = 0; i <= 7; i++)
		hw->io_ports_array[i] = CONFIG_H8300_IDE_BASE + H8300_IDE_GAP*i;
	hw->io_ports.ctl_addr = CONFIG_H8300_IDE_ALT;
	hw->irq = EXT_IRQ0 + CONFIG_H8300_IDE_IRQ;
	hw->chipset = ide_generic;
}

static const struct ide_port_info h8300_port_info = {
	.tp_ops			= &h8300_tp_ops,
	.host_flags		= IDE_HFLAG_NO_IO_32BIT | IDE_HFLAG_NO_DMA,
};

static int __init h8300_ide_init(void)
{
	hw_regs_t hw, *hws[] = { &hw, NULL, NULL, NULL };

	printk(KERN_INFO DRV_NAME ": H8/300 generic IDE interface\n");

	if (!request_region(CONFIG_H8300_IDE_BASE, H8300_IDE_GAP*8, "ide-h8300"))
		goto out_busy;
	if (!request_region(CONFIG_H8300_IDE_ALT, H8300_IDE_GAP, "ide-h8300")) {
		release_region(CONFIG_H8300_IDE_BASE, H8300_IDE_GAP*8);
		goto out_busy;
	}

	hw_setup(&hw);

	return ide_host_add(&h8300_port_info, hws, NULL);

out_busy:
	printk(KERN_ERR "ide-h8300: IDE I/F resource already used.\n");

	return -EBUSY;
}

module_init(h8300_ide_init);

MODULE_LICENSE("GPL");
