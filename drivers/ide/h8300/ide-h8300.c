/*
 * H8/300 generic IDE interface
 */

#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

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

static void mm_outw(u16 d, unsigned long a)
{
	__asm__("mov.b %w0,r2h\n\t"
		"mov.b %x0,r2l\n\t"
		"mov.w r2,@%1"
		:
		:"r"(d),"r"(a)
		:"er2");
}

static u16 mm_inw(unsigned long a)
{
	register u16 r __asm__("er0");
	__asm__("mov.w @%1,r2\n\t"
		"mov.b r2l,%x0\n\t"
		"mov.b r2h,%w0"
		:"=r"(r)
		:"r"(a)
		:"er2");
	return r;
}

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

#define H8300_IDE_GAP (2)

static inline void hw_setup(hw_regs_t *hw)
{
	int i;

	memset(hw, 0, sizeof(hw_regs_t));
	for (i = 0; i <= IDE_STATUS_OFFSET; i++)
		hw->io_ports[i] = CONFIG_H8300_IDE_BASE + H8300_IDE_GAP*i;
	hw->io_ports[IDE_CONTROL_OFFSET] = CONFIG_H8300_IDE_ALT;
	hw->irq = EXT_IRQ0 + CONFIG_H8300_IDE_IRQ;
	hw->chipset = ide_generic;
}

static inline void hwif_setup(ide_hwif_t *hwif)
{
	default_hwif_iops(hwif);

	hwif->mmio  = 1;
	hwif->OUTW  = mm_outw;
	hwif->OUTSW = mm_outsw;
	hwif->INW   = mm_inw;
	hwif->INSW  = mm_insw;
	hwif->OUTSL = NULL;
	hwif->INSL  = NULL;
}

static int __init h8300_ide_init(void)
{
	hw_regs_t hw;
	ide_hwif_t *hwif;
	int index;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

	if (!request_region(CONFIG_H8300_IDE_BASE, H8300_IDE_GAP*8, "ide-h8300"))
		goto out_busy;
	if (!request_region(CONFIG_H8300_IDE_ALT, H8300_IDE_GAP, "ide-h8300")) {
		release_region(CONFIG_H8300_IDE_BASE, H8300_IDE_GAP*8);
		goto out_busy;
	}

	hw_setup(&hw);

	/* register if */
	hwif = ide_find_port(hw.io_ports[IDE_DATA_OFFSET]);
	if (hwif == NULL) {
		printk(KERN_ERR "ide-h8300: IDE I/F register failed\n");
		return -ENOENT;
	}

	index = hwif->index;
	ide_init_port_data(hwif, index);
	ide_init_port_hw(hwif, &hw);
	hwif_setup(hwif);
	hwif->host_flags = IDE_HFLAG_NO_IO_32BIT;
	printk(KERN_INFO "ide%d: H8/300 generic IDE interface\n", index);

	idx[0] = index;

	ide_device_add(idx, NULL);

	return 0;

out_busy:
	printk(KERN_ERR "ide-h8300: IDE I/F resource already used.\n");

	return -EBUSY;
}

module_init(h8300_ide_init);

MODULE_LICENSE("GPL");
