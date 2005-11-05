/*
 * arch/ppc/platforms/4xx/redwood6.c
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/ppc4xx_pic.h>
#include <linux/delay.h>
#include <asm/machdep.h>

/*
 * Define external IRQ senses and polarities.
 */
unsigned char ppc4xx_uic_ext_irq_cfg[] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 7 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 8 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 9 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 3 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 4 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 5 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext Int 6 */
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= SMC91111_BASE_ADDR,
		.end	= SMC91111_BASE_ADDR + SMC91111_REG_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SMC91111_IRQ,
		.end	= SMC91111_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct platform_device *redwood6_devs[] __initdata = {
	&smc91x_device,
};

static int __init
redwood6_platform_add_devices(void)
{
	return platform_add_devices(redwood6_devs, ARRAY_SIZE(redwood6_devs));
}


void __init
redwood6_setup_arch(void)
{
#ifdef CONFIG_IDE
	void *xilinx, *xilinx_1, *xilinx_2;
	unsigned short us_reg5;
#endif

	ppc4xx_setup_arch();

#ifdef CONFIG_IDE
	xilinx = (unsigned long) ioremap(IDE_XLINUX_MUX_BASE, 0x10);
	/* init xilinx control registers - enable ide mux, clear reset bit */
	if (!xilinx) {
		printk(KERN_CRIT
		       "redwood6_setup_arch() xilinxi ioremap failed\n");
		return;
	}
	xilinx_1 = xilinx + 0xa;
	xilinx_2 = xilinx + 0xe;

	us_reg5 = readb(xilinx_1);
	writeb(0x01d1, xilinx_1);
	writeb(0x0008, xilinx_2);

	udelay(10 * 1000);

	writeb(0x01d1, xilinx_1);
	writeb(0x0008, xilinx_2);
#endif

#ifdef DEBUG_BRINGUP
	bd_t *bip = (bd_t *) __res;
	printk("\n");
	printk("machine\t: %s\n", PPC4xx_MACHINE_NAME);
	printk("\n");
	printk("bi_s_version\t %s\n", bip->bi_s_version);
	printk("bi_r_version\t %s\n", bip->bi_r_version);
	printk("bi_memsize\t 0x%8.8x\t %dMBytes\n", bip->bi_memsize,
	       bip->bi_memsize / (1024 * 1000));
	printk("bi_enetaddr %d\t %2.2x%2.2x%2.2x-%2.2x%2.2x%2.2x\n", 0,
	       bip->bi_enetaddr[0], bip->bi_enetaddr[1], bip->bi_enetaddr[2],
	       bip->bi_enetaddr[3], bip->bi_enetaddr[4], bip->bi_enetaddr[5]);

	printk("bi_intfreq\t 0x%8.8x\t clock:\t %dMhz\n",
	       bip->bi_intfreq, bip->bi_intfreq / 1000000);

	printk("bi_busfreq\t 0x%8.8x\t plb bus clock:\t %dMHz\n",
	       bip->bi_busfreq, bip->bi_busfreq / 1000000);
	printk("bi_tbfreq\t 0x%8.8x\t TB freq:\t %dMHz\n",
	       bip->bi_tbfreq, bip->bi_tbfreq / 1000000);

	printk("\n");
#endif

	/* Identify the system */
	printk(KERN_INFO "IBM Redwood6 (STBx25XX) Platform\n");
	printk(KERN_INFO
	       "Port by MontaVista Software, Inc. (source@mvista.com)\n");

	device_initcall(redwood6_platform_add_devices);
}

void __init
redwood6_map_io(void)
{
	int i;

	ppc4xx_map_io();
	for (i = 0; i < 16; i++) {
		unsigned long v, p;

		/* 0x400x0000 -> 0xe00x0000 */
		p = 0x40000000 | (i << 16);
		v = STBx25xx_IO_BASE | (i << 16);

		io_block_mapping(v, p, PAGE_SIZE,
				 _PAGE_NO_CACHE | pgprot_val(PAGE_KERNEL) |
				 _PAGE_GUARDED);
	}
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = redwood6_setup_arch;
	ppc_md.setup_io_mappings = redwood6_map_io;
}
