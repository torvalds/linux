/*
 * arch/sh/boards/landisk/setup.c
 *
 * Copyright (C) 2000 Kazumoto Kojima
 * Copyright (C) 2002 Paul Mundt
 *
 * I-O DATA Device, Inc. LANDISK Support.
 *
 * Modified for LANDISK by
 * Atom Create Engineering Co., Ltd. 2002.
 *
 * modifed by kogiidena
 * 2005.09.16
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/landisk/iodata_landisk.h>
#include <asm/io.h>

void landisk_time_init(void);
void init_landisk_IRQ(void);

int landisk_ledparam;
int landisk_buzzerparam;
int landisk_arch;

/* cycle the led's in the clasic knightrider/sun pattern */
static void heartbeat_landisk(void)
{
	static unsigned int cnt = 0, blink = 0x00, period = 25;
        volatile u8 *p = (volatile u8 *)PA_LED;
	char data;

        if ((landisk_ledparam & 0x080) == 0)
		return;

	cnt += 1;

        if (cnt < period)
		return;

	cnt = 0;
	blink++;

	data = (blink & 0x01) ? (landisk_ledparam >> 16) : 0;
	data |= (blink & 0x02) ? (landisk_ledparam >> 8) : 0;
	data |= landisk_ledparam;

	/* buzzer */
	if (landisk_buzzerparam & 0x1) {
		data |= 0x80;
	} else {
		data &= 0x7f;
	}
	*p = data;

        if (((landisk_ledparam & 0x007f7f00) == 0) &&
             (landisk_buzzerparam == 0))
		landisk_ledparam &= (~0x0080);

	landisk_buzzerparam >>= 1;
}

static void landisk_power_off(void)
{
        ctrl_outb(0x01, PA_SHUTDOWN);
}

static void check_usl5p(void)
{
        volatile u8 *p = (volatile u8 *)PA_LED;
        u8 tmp1, tmp2;

        tmp1 = *p;
        *p = 0x40;
        tmp2 = *p;
        *p = tmp1;

        landisk_arch = (tmp2 == 0x40);
        if (landisk_arch == 1) {
                /* arch == usl-5p */
                landisk_ledparam = 0x00000380;
                landisk_ledparam |= (tmp1 & 0x07c);
        } else {
                /* arch == landisk */
                landisk_ledparam = 0x02000180;
                landisk_ledparam |= 0x04;
        }
}

void *area5_io_base;
void *area6_io_base;

static int __init landisk_cf_init(void)
{
	pgprot_t prot;
	unsigned long paddrbase, psize;

	/* open I/O area window */
	paddrbase = virt_to_phys((void *)PA_AREA5_IO);
	psize = PAGE_SIZE;
	prot = PAGE_KERNEL_PCC(1, _PAGE_PCC_IO16);
	area5_io_base = p3_ioremap(paddrbase, psize, prot.pgprot);
	if (!area5_io_base) {
		printk("allocate_cf_area : can't open CF I/O window!\n");
		return -ENOMEM;
	}

	paddrbase = virt_to_phys((void *)PA_AREA6_IO);
	psize = PAGE_SIZE;
	prot = PAGE_KERNEL_PCC(0, _PAGE_PCC_IO16);
	area6_io_base = p3_ioremap(paddrbase, psize, prot.pgprot);
	if (!area6_io_base) {
		printk("allocate_cf_area : can't open HDD I/O window!\n");
		return -ENOMEM;
	}

	printk(KERN_INFO "Allocate Area5/6 success.\n");

	/* XXX : do we need attribute and common-memory area also? */

	return 0;
}

static void __init landisk_setup(char **cmdline_p)
{
	device_initcall(landisk_cf_init);

	landisk_buzzerparam = 0;
	check_usl5p();

	printk(KERN_INFO "I-O DATA DEVICE, INC. \"LANDISK Series\" support.\n");

	board_time_init = landisk_time_init;
	pm_power_off = landisk_power_off;
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_landisk __initmv = {
	.mv_name = "LANDISK",
	.mv_setup = landisk_setup,
	.mv_nr_irqs = 72,
	.mv_inb = landisk_inb,
	.mv_inw = landisk_inw,
	.mv_inl = landisk_inl,
	.mv_outb = landisk_outb,
	.mv_outw = landisk_outw,
	.mv_outl = landisk_outl,
	.mv_inb_p = landisk_inb_p,
	.mv_inw_p = landisk_inw,
	.mv_inl_p = landisk_inl,
	.mv_outb_p = landisk_outb_p,
	.mv_outw_p = landisk_outw,
	.mv_outl_p = landisk_outl,
	.mv_insb = landisk_insb,
	.mv_insw = landisk_insw,
	.mv_insl = landisk_insl,
	.mv_outsb = landisk_outsb,
	.mv_outsw = landisk_outsw,
	.mv_outsl = landisk_outsl,
	.mv_ioport_map = landisk_ioport_map,
	.mv_init_irq = init_landisk_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat = heartbeat_landisk,
#endif
};
ALIAS_MV(landisk)
