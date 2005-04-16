/*
 *  linux/drivers/ide/legacy/ali14xx.c		Version 0.03	Feb 09, 1996
 *
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
 */

/*
 * ALI M14xx chipset EIDE controller
 *
 * Works for ALI M1439/1443/1445/1487/1489 chipsets.
 *
 * Adapted from code developed by derekn@vw.ece.cmu.edu.  -ml
 * Derek's notes follow:
 *
 * I think the code should be pretty understandable,
 * but I'll be happy to (try to) answer questions.
 *
 * The critical part is in the setupDrive function.  The initRegisters
 * function doesn't seem to be necessary, but the DOS driver does it, so
 * I threw it in.
 *
 * I've only tested this on my system, which only has one disk.  I posted
 * it to comp.sys.linux.hardware, so maybe some other people will try it
 * out.
 *
 * Derek Noonburg  (derekn@ece.cmu.edu)
 * 95-sep-26
 *
 * Update 96-jul-13:
 *
 * I've since upgraded to two disks and a CD-ROM, with no trouble, and
 * I've also heard from several others who have used it successfully.
 * This driver appears to work with both the 1443/1445 and the 1487/1489
 * chipsets.  I've added support for PIO mode 4 for the 1487.  This
 * seems to work just fine on the 1443 also, although I'm not sure it's
 * advertised as supporting mode 4.  (I've been running a WDC AC21200 in
 * mode 4 for a while now with no trouble.)  -Derek
 */

#undef REALLY_SLOW_IO           /* most systems can safely undef this */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

/* port addresses for auto-detection */
#define ALI_NUM_PORTS 4
static int ports[ALI_NUM_PORTS] __initdata = {0x074, 0x0f4, 0x034, 0x0e4};

/* register initialization data */
typedef struct { u8 reg, data; } RegInitializer;

static RegInitializer initData[] __initdata = {
	{0x01, 0x0f}, {0x02, 0x00}, {0x03, 0x00}, {0x04, 0x00},
	{0x05, 0x00}, {0x06, 0x00}, {0x07, 0x2b}, {0x0a, 0x0f},
	{0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},
	{0x29, 0x00}, {0x2a, 0x00}, {0x2f, 0x00}, {0x2b, 0x00},
	{0x2c, 0x00}, {0x2d, 0x00}, {0x2e, 0x00}, {0x30, 0x00},
	{0x31, 0x00}, {0x32, 0x00}, {0x33, 0x00}, {0x34, 0xff},
	{0x35, 0x03}, {0x00, 0x00}
};

#define ALI_MAX_PIO 4

/* timing parameter registers for each drive */
static struct { u8 reg1, reg2, reg3, reg4; } regTab[4] = {
	{0x03, 0x26, 0x04, 0x27},     /* drive 0 */
	{0x05, 0x28, 0x06, 0x29},     /* drive 1 */
	{0x2b, 0x30, 0x2c, 0x31},     /* drive 2 */
	{0x2d, 0x32, 0x2e, 0x33},     /* drive 3 */
};

static int basePort;	/* base port address */
static int regPort;	/* port for register number */
static int dataPort;	/* port for register data */
static u8 regOn;	/* output to base port to access registers */
static u8 regOff;	/* output to base port to close registers */

/*------------------------------------------------------------------------*/

/*
 * Read a controller register.
 */
static inline u8 inReg (u8 reg)
{
	outb_p(reg, regPort);
	return inb(dataPort);
}

/*
 * Write a controller register.
 */
static void outReg (u8 data, u8 reg)
{
	outb_p(reg, regPort);
	outb_p(data, dataPort);
}

/*
 * Set PIO mode for the specified drive.
 * This function computes timing parameters
 * and sets controller registers accordingly.
 */
static void ali14xx_tune_drive (ide_drive_t *drive, u8 pio)
{
	int driveNum;
	int time1, time2;
	u8 param1, param2, param3, param4;
	unsigned long flags;
	ide_pio_data_t d;
	int bus_speed = system_bus_clock();

	pio = ide_get_best_pio_mode(drive, pio, ALI_MAX_PIO, &d);

	/* calculate timing, according to PIO mode */
	time1 = d.cycle_time;
	time2 = ide_pio_timings[pio].active_time;
	param3 = param1 = (time2 * bus_speed + 999) / 1000;
	param4 = param2 = (time1 * bus_speed + 999) / 1000 - param1;
	if (pio < 3) {
		param3 += 8;
		param4 += 8;
	}
	printk(KERN_DEBUG "%s: PIO mode%d, t1=%dns, t2=%dns, cycles = %d+%d, %d+%d\n",
		drive->name, pio, time1, time2, param1, param2, param3, param4);

	/* stuff timing parameters into controller registers */
	driveNum = (HWIF(drive)->index << 1) + drive->select.b.unit;
	spin_lock_irqsave(&ide_lock, flags);
	outb_p(regOn, basePort);
	outReg(param1, regTab[driveNum].reg1);
	outReg(param2, regTab[driveNum].reg2);
	outReg(param3, regTab[driveNum].reg3);
	outReg(param4, regTab[driveNum].reg4);
	outb_p(regOff, basePort);
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * Auto-detect the IDE controller port.
 */
static int __init findPort (void)
{
	int i;
	u8 t;
	unsigned long flags;

	local_irq_save(flags);
	for (i = 0; i < ALI_NUM_PORTS; ++i) {
		basePort = ports[i];
		regOff = inb(basePort);
		for (regOn = 0x30; regOn <= 0x33; ++regOn) {
			outb_p(regOn, basePort);
			if (inb(basePort) == regOn) {
				regPort = basePort + 4;
				dataPort = basePort + 8;
				t = inReg(0) & 0xf0;
				outb_p(regOff, basePort);
				local_irq_restore(flags);
				if (t != 0x50)
					return 0;
				return 1;  /* success */
			}
		}
		outb_p(regOff, basePort);
	}
	local_irq_restore(flags);
	return 0;
}

/*
 * Initialize controller registers with default values.
 */
static int __init initRegisters (void) {
	RegInitializer *p;
	u8 t;
	unsigned long flags;

	local_irq_save(flags);
	outb_p(regOn, basePort);
	for (p = initData; p->reg != 0; ++p)
		outReg(p->data, p->reg);
	outb_p(0x01, regPort);
	t = inb(regPort) & 0x01;
	outb_p(regOff, basePort);
	local_irq_restore(flags);
	return t;
}

static int __init ali14xx_probe(void)
{
	ide_hwif_t *hwif, *mate;

	printk(KERN_DEBUG "ali14xx: base=0x%03x, regOn=0x%02x.\n",
			  basePort, regOn);

	/* initialize controller registers */
	if (!initRegisters()) {
		printk(KERN_ERR "ali14xx: Chip initialization failed.\n");
		return 1;
	}

	hwif = &ide_hwifs[0];
	mate = &ide_hwifs[1];

	hwif->chipset = ide_ali14xx;
	hwif->tuneproc = &ali14xx_tune_drive;
	hwif->mate = mate;

	mate->chipset = ide_ali14xx;
	mate->tuneproc = &ali14xx_tune_drive;
	mate->mate = hwif;
	mate->channel = 1;

	probe_hwif_init(hwif);
	probe_hwif_init(mate);

	create_proc_ide_interfaces();

	return 0;
}

/* Can be called directly from ide.c. */
int __init ali14xx_init(void)
{
	/* auto-detect IDE controller port */
	if (findPort()) {
		if (ali14xx_probe())
			return -ENODEV;
		return 0;
	}
	printk(KERN_ERR "ali14xx: not found.\n");
	return -ENODEV;
}

#ifdef MODULE
module_init(ali14xx_init);
#endif

MODULE_AUTHOR("see local file");
MODULE_DESCRIPTION("support of ALI 14XX IDE chipsets");
MODULE_LICENSE("GPL");
