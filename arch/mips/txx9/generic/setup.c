/*
 * linux/arch/mips/txx9/generic/setup.c
 *
 * Based on linux/arch/mips/txx9/rbtx4938/setup.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * 2003-2005 (c) MontaVista Software, Inc.
 * (C) Copyright TOSHIBA CORPORATION 2000-2001, 2004-2007
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/pci.h>
#ifdef CONFIG_CPU_TX49XX
#include <asm/txx9/tx4938.h>
#endif

/* EBUSC settings of TX4927, etc. */
struct resource txx9_ce_res[8];
static char txx9_ce_res_name[8][4];	/* "CEn" */

/* pcode, internal register */
unsigned int txx9_pcode;
char txx9_pcode_str[8];
static struct resource txx9_reg_res = {
	.name = txx9_pcode_str,
	.flags = IORESOURCE_MEM,
};
void __init
txx9_reg_res_init(unsigned int pcode, unsigned long base, unsigned long size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(txx9_ce_res); i++) {
		sprintf(txx9_ce_res_name[i], "CE%d", i);
		txx9_ce_res[i].flags = IORESOURCE_MEM;
		txx9_ce_res[i].name = txx9_ce_res_name[i];
	}

	sprintf(txx9_pcode_str, "TX%x", pcode);
	if (base) {
		txx9_reg_res.start = base & 0xfffffffffULL;
		txx9_reg_res.end = (base & 0xfffffffffULL) + (size - 1);
		request_resource(&iomem_resource, &txx9_reg_res);
	}
}

/* clocks */
unsigned int txx9_master_clock;
unsigned int txx9_cpu_clock;
unsigned int txx9_gbus_clock;

int txx9_ccfg_toeon __initdata = 1;

/* Minimum CLK support */

struct clk *clk_get(struct device *dev, const char *id)
{
	if (!strcmp(id, "spi-baseclk"))
		return (struct clk *)((unsigned long)txx9_gbus_clock / 2 / 4);
	if (!strcmp(id, "imbus_clk"))
		return (struct clk *)((unsigned long)txx9_gbus_clock / 2);
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return (unsigned long)clk;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

/* GPIO support */

#ifdef CONFIG_GENERIC_GPIO
int gpio_to_irq(unsigned gpio)
{
	return -EINVAL;
}
EXPORT_SYMBOL(gpio_to_irq);

int irq_to_gpio(unsigned irq)
{
	return -EINVAL;
}
EXPORT_SYMBOL(irq_to_gpio);
#endif

extern struct txx9_board_vec jmr3927_vec;
extern struct txx9_board_vec rbtx4927_vec;
extern struct txx9_board_vec rbtx4937_vec;
extern struct txx9_board_vec rbtx4938_vec;

struct txx9_board_vec *txx9_board_vec __initdata;
static char txx9_system_type[32];

void __init prom_init_cmdline(void)
{
	int argc = (int)fw_arg0;
	char **argv = (char **)fw_arg1;
	int i;			/* Always ignore the "-c" at argv[0] */
#ifdef CONFIG_64BIT
	char *fixed_argv[32];
	for (i = 0; i < argc; i++)
		fixed_argv[i] = (char *)(long)(*((__s32 *)argv + i));
	argv = fixed_argv;
#endif

	/* ignore all built-in args if any f/w args given */
	if (argc > 1)
		*arcs_cmdline = '\0';

	for (i = 1; i < argc; i++) {
		if (i != 1)
			strcat(arcs_cmdline, " ");
		strcat(arcs_cmdline, argv[i]);
	}
}

void __init prom_init(void)
{
#ifdef CONFIG_CPU_TX39XX
	txx9_board_vec = &jmr3927_vec;
#endif
#ifdef CONFIG_CPU_TX49XX
	switch (TX4938_REV_PCODE()) {
#ifdef CONFIG_TOSHIBA_RBTX4927
	case 0x4927:
		txx9_board_vec = &rbtx4927_vec;
		break;
	case 0x4937:
		txx9_board_vec = &rbtx4937_vec;
		break;
#endif
#ifdef CONFIG_TOSHIBA_RBTX4938
	case 0x4938:
		txx9_board_vec = &rbtx4938_vec;
		break;
#endif
	}
#endif

	strcpy(txx9_system_type, txx9_board_vec->system);

	txx9_board_vec->prom_init();
}

void __init prom_free_prom_memory(void)
{
}

const char *get_system_type(void)
{
	return txx9_system_type;
}

char * __init prom_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

/* wrappers */
void __init plat_mem_setup(void)
{
	ioport_resource.start = 0;
	ioport_resource.end = ~0UL;	/* no limit */
	iomem_resource.start = 0;
	iomem_resource.end = ~0UL;	/* no limit */
#ifdef CONFIG_PCI
	pcibios_plat_setup = txx9_pcibios_setup;
#endif
	txx9_board_vec->mem_setup();
}

void __init arch_init_irq(void)
{
	txx9_board_vec->irq_setup();
}

void __init plat_time_init(void)
{
	txx9_board_vec->time_init();
}

static int __init _txx9_arch_init(void)
{
	if (txx9_board_vec->arch_init)
		txx9_board_vec->arch_init();
	return 0;
}
arch_initcall(_txx9_arch_init);

static int __init _txx9_device_init(void)
{
	if (txx9_board_vec->device_init)
		txx9_board_vec->device_init();
	return 0;
}
device_initcall(_txx9_device_init);

int (*txx9_irq_dispatch)(int pending);
asmlinkage void plat_irq_dispatch(void)
{
	int pending = read_c0_status() & read_c0_cause() & ST0_IM;
	int irq = txx9_irq_dispatch(pending);

	if (likely(irq >= 0))
		do_IRQ(irq);
	else
		spurious_interrupt();
}

/* see include/asm-mips/mach-tx39xx/mangle-port.h, for example. */
#ifdef NEEDS_TXX9_SWIZZLE_ADDR_B
static unsigned long __swizzle_addr_none(unsigned long port)
{
	return port;
}
unsigned long (*__swizzle_addr_b)(unsigned long port) = __swizzle_addr_none;
EXPORT_SYMBOL(__swizzle_addr_b);
#endif
