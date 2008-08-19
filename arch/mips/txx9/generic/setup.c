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
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/reboot.h>
#include <asm/r4kcache.h>
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

	txx9_pcode = pcode;
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

#ifdef CONFIG_CPU_TX39XX
/* don't enable by default - see errata */
int txx9_ccfg_toeon __initdata;
#else
int txx9_ccfg_toeon __initdata = 1;
#endif

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

#define BOARD_VEC(board)	extern struct txx9_board_vec board;
#include <asm/txx9/boards.h>
#undef BOARD_VEC

struct txx9_board_vec *txx9_board_vec __initdata;
static char txx9_system_type[32];

static struct txx9_board_vec *board_vecs[] __initdata = {
#define BOARD_VEC(board)	&board,
#include <asm/txx9/boards.h>
#undef BOARD_VEC
};

static struct txx9_board_vec *__init find_board_byname(const char *name)
{
	int i;

	/* search board_vecs table */
	for (i = 0; i < ARRAY_SIZE(board_vecs); i++) {
		if (strstr(board_vecs[i]->system, name))
			return board_vecs[i];
	}
	return NULL;
}

static void __init prom_init_cmdline(void)
{
	int argc = (int)fw_arg0;
	int *argv32 = (int *)fw_arg1;
	int i;			/* Always ignore the "-c" at argv[0] */
	char builtin[CL_SIZE];

	/* ignore all built-in args if any f/w args given */
	/*
	 * But if built-in strings was started with '+', append them
	 * to command line args.  If built-in was started with '-',
	 * ignore all f/w args.
	 */
	builtin[0] = '\0';
	if (arcs_cmdline[0] == '+')
		strcpy(builtin, arcs_cmdline + 1);
	else if (arcs_cmdline[0] == '-') {
		strcpy(builtin, arcs_cmdline + 1);
		argc = 0;
	} else if (argc <= 1)
		strcpy(builtin, arcs_cmdline);
	arcs_cmdline[0] = '\0';

	for (i = 1; i < argc; i++) {
		char *str = (char *)(long)argv32[i];
		if (i != 1)
			strcat(arcs_cmdline, " ");
		if (strchr(str, ' ')) {
			strcat(arcs_cmdline, "\"");
			strcat(arcs_cmdline, str);
			strcat(arcs_cmdline, "\"");
		} else
			strcat(arcs_cmdline, str);
	}
	/* append saved builtin args */
	if (builtin[0]) {
		if (arcs_cmdline[0])
			strcat(arcs_cmdline, " ");
		strcat(arcs_cmdline, builtin);
	}
}

static int txx9_ic_disable __initdata;
static int txx9_dc_disable __initdata;

#if defined(CONFIG_CPU_TX49XX)
/* flush all cache on very early stage (before 4k_cache_init) */
static void __init early_flush_dcache(void)
{
	unsigned int conf = read_c0_config();
	unsigned int dc_size = 1 << (12 + ((conf & CONF_DC) >> 6));
	unsigned int linesz = 32;
	unsigned long addr, end;

	end = INDEX_BASE + dc_size / 4;
	/* 4way, waybit=0 */
	for (addr = INDEX_BASE; addr < end; addr += linesz) {
		cache_op(Index_Writeback_Inv_D, addr | 0);
		cache_op(Index_Writeback_Inv_D, addr | 1);
		cache_op(Index_Writeback_Inv_D, addr | 2);
		cache_op(Index_Writeback_Inv_D, addr | 3);
	}
}

static void __init txx9_cache_fixup(void)
{
	unsigned int conf;

	conf = read_c0_config();
	/* flush and disable */
	if (txx9_ic_disable) {
		conf |= TX49_CONF_IC;
		write_c0_config(conf);
	}
	if (txx9_dc_disable) {
		early_flush_dcache();
		conf |= TX49_CONF_DC;
		write_c0_config(conf);
	}

	/* enable cache */
	conf = read_c0_config();
	if (!txx9_ic_disable)
		conf &= ~TX49_CONF_IC;
	if (!txx9_dc_disable)
		conf &= ~TX49_CONF_DC;
	write_c0_config(conf);

	if (conf & TX49_CONF_IC)
		pr_info("TX49XX I-Cache disabled.\n");
	if (conf & TX49_CONF_DC)
		pr_info("TX49XX D-Cache disabled.\n");
}
#elif defined(CONFIG_CPU_TX39XX)
/* flush all cache on very early stage (before tx39_cache_init) */
static void __init early_flush_dcache(void)
{
	unsigned int conf = read_c0_config();
	unsigned int dc_size = 1 << (10 + ((conf & TX39_CONF_DCS_MASK) >>
					   TX39_CONF_DCS_SHIFT));
	unsigned int linesz = 16;
	unsigned long addr, end;

	end = INDEX_BASE + dc_size / 2;
	/* 2way, waybit=0 */
	for (addr = INDEX_BASE; addr < end; addr += linesz) {
		cache_op(Index_Writeback_Inv_D, addr | 0);
		cache_op(Index_Writeback_Inv_D, addr | 1);
	}
}

static void __init txx9_cache_fixup(void)
{
	unsigned int conf;

	conf = read_c0_config();
	/* flush and disable */
	if (txx9_ic_disable) {
		conf &= ~TX39_CONF_ICE;
		write_c0_config(conf);
	}
	if (txx9_dc_disable) {
		early_flush_dcache();
		conf &= ~TX39_CONF_DCE;
		write_c0_config(conf);
	}

	/* enable cache */
	conf = read_c0_config();
	if (!txx9_ic_disable)
		conf |= TX39_CONF_ICE;
	if (!txx9_dc_disable)
		conf |= TX39_CONF_DCE;
	write_c0_config(conf);

	if (!(conf & TX39_CONF_ICE))
		pr_info("TX39XX I-Cache disabled.\n");
	if (!(conf & TX39_CONF_DCE))
		pr_info("TX39XX D-Cache disabled.\n");
}
#else
static inline void txx9_cache_fixup(void)
{
}
#endif

static void __init preprocess_cmdline(void)
{
	char cmdline[CL_SIZE];
	char *s;

	strcpy(cmdline, arcs_cmdline);
	s = cmdline;
	arcs_cmdline[0] = '\0';
	while (s && *s) {
		char *str = strsep(&s, " ");
		if (strncmp(str, "board=", 6) == 0) {
			txx9_board_vec = find_board_byname(str + 6);
			continue;
		} else if (strncmp(str, "masterclk=", 10) == 0) {
			unsigned long val;
			if (strict_strtoul(str + 10, 10, &val) == 0)
				txx9_master_clock = val;
			continue;
		} else if (strcmp(str, "icdisable") == 0) {
			txx9_ic_disable = 1;
			continue;
		} else if (strcmp(str, "dcdisable") == 0) {
			txx9_dc_disable = 1;
			continue;
		} else if (strcmp(str, "toeoff") == 0) {
			txx9_ccfg_toeon = 0;
			continue;
		} else if (strcmp(str, "toeon") == 0) {
			txx9_ccfg_toeon = 1;
			continue;
		}
		if (arcs_cmdline[0])
			strcat(arcs_cmdline, " ");
		strcat(arcs_cmdline, str);
	}

	txx9_cache_fixup();
}

static void __init select_board(void)
{
	const char *envstr;

	/* first, determine by "board=" argument in preprocess_cmdline() */
	if (txx9_board_vec)
		return;
	/* next, determine by "board" envvar */
	envstr = prom_getenv("board");
	if (envstr) {
		txx9_board_vec = find_board_byname(envstr);
		if (txx9_board_vec)
			return;
	}

	/* select "default" board */
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
}

void __init prom_init(void)
{
	prom_init_cmdline();
	preprocess_cmdline();
	select_board();

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

const char *__init prom_getenv(const char *name)
{
	const s32 *str = (const s32 *)fw_arg2;

	if (!str)
		return NULL;
	/* YAMON style ("name", "value" pairs) */
	while (str[0] && str[1]) {
		if (!strcmp((const char *)(unsigned long)str[0], name))
			return (const char *)(unsigned long)str[1];
		str += 2;
	}
	return NULL;
}

static void __noreturn txx9_machine_halt(void)
{
	local_irq_disable();
	clear_c0_status(ST0_IM);
	while (1) {
		if (cpu_wait) {
			(*cpu_wait)();
			if (cpu_has_counter) {
				/*
				 * Clear counter interrupt while it
				 * breaks WAIT instruction even if
				 * masked.
				 */
				write_c0_compare(0);
			}
		}
	}
}

/* Watchdog support */
void __init txx9_wdt_init(unsigned long base)
{
	struct resource res = {
		.start	= base,
		.end	= base + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	};
	platform_device_register_simple("txx9wdt", -1, &res, 1);
}

/* SPI support */
void __init txx9_spi_init(int busid, unsigned long base, int irq)
{
	struct resource res[] = {
		{
			.start	= base,
			.end	= base + 0x20 - 1,
			.flags	= IORESOURCE_MEM,
		}, {
			.start	= irq,
			.flags	= IORESOURCE_IRQ,
		},
	};
	platform_device_register_simple("spi_txx9", busid,
					res, ARRAY_SIZE(res));
}

void __init txx9_ethaddr_init(unsigned int id, unsigned char *ethaddr)
{
	struct platform_device *pdev =
		platform_device_alloc("tc35815-mac", id);
	if (!pdev ||
	    platform_device_add_data(pdev, ethaddr, 6) ||
	    platform_device_add(pdev))
		platform_device_put(pdev);
}

void __init txx9_sio_init(unsigned long baseaddr, int irq,
			  unsigned int line, unsigned int sclk, int nocts)
{
#ifdef CONFIG_SERIAL_TXX9
	struct uart_port req;

	memset(&req, 0, sizeof(req));
	req.line = line;
	req.iotype = UPIO_MEM;
	req.membase = ioremap(baseaddr, 0x24);
	req.mapbase = baseaddr;
	req.irq = irq;
	if (!nocts)
		req.flags |= UPF_BUGGY_UART /*HAVE_CTS_LINE*/;
	if (sclk) {
		req.flags |= UPF_MAGIC_MULTIPLIER /*USE_SCLK*/;
		req.uartclk = sclk;
	} else
		req.uartclk = TXX9_IMCLK;
	early_serial_txx9_setup(&req);
#endif /* CONFIG_SERIAL_TXX9 */
}

#ifdef CONFIG_EARLY_PRINTK
static void __init null_prom_putchar(char c)
{
}
void (*txx9_prom_putchar)(char c) __initdata = null_prom_putchar;

void __init prom_putchar(char c)
{
	txx9_prom_putchar(c);
}

static void __iomem *early_txx9_sio_port;

static void __init early_txx9_sio_putchar(char c)
{
#define TXX9_SICISR	0x0c
#define TXX9_SITFIFO	0x1c
#define TXX9_SICISR_TXALS	0x00000002
	while (!(__raw_readl(early_txx9_sio_port + TXX9_SICISR) &
		 TXX9_SICISR_TXALS))
		;
	__raw_writel(c, early_txx9_sio_port + TXX9_SITFIFO);
}

void __init txx9_sio_putchar_init(unsigned long baseaddr)
{
	early_txx9_sio_port = ioremap(baseaddr, 0x24);
	txx9_prom_putchar = early_txx9_sio_putchar;
}
#endif /* CONFIG_EARLY_PRINTK */

/* wrappers */
void __init plat_mem_setup(void)
{
	ioport_resource.start = 0;
	ioport_resource.end = ~0UL;	/* no limit */
	iomem_resource.start = 0;
	iomem_resource.end = ~0UL;	/* no limit */

	/* fallback restart/halt routines */
	_machine_restart = (void (*)(char *))txx9_machine_halt;
	_machine_halt = txx9_machine_halt;
	pm_power_off = txx9_machine_halt;

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
#ifdef CONFIG_CPU_TX49XX
	mips_hpt_frequency = txx9_cpu_clock / 2;
#endif
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
