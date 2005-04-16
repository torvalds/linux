/*
 * linux/arch/arm/mach-omap/common.c
 *
 * Code common to all OMAP machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/hardware/clock.h>
#include <asm/io.h>
#include <asm/mach-types.h>

#include <asm/arch/board.h>
#include <asm/arch/mux.h>
#include <asm/arch/fpga.h>

#include "clock.h"

#define DEBUG 1

struct omap_id {
	u16	jtag_id;	/* Used to determine OMAP type */
	u8	die_rev;	/* Processor revision */
	u32	omap_id;	/* OMAP revision */
	u32	type;		/* Cpu id bits [31:08], cpu class bits [07:00] */
};

/* Register values to detect the OMAP version */
static struct omap_id omap_ids[] __initdata = {
	{ .jtag_id = 0x355f, .die_rev = 0x0, .omap_id = 0x03320000, .type = 0x07300100},
	{ .jtag_id = 0xb55f, .die_rev = 0x0, .omap_id = 0x03320000, .type = 0x07300300},
	{ .jtag_id = 0xb470, .die_rev = 0x0, .omap_id = 0x03310100, .type = 0x15100000},
	{ .jtag_id = 0xb576, .die_rev = 0x0, .omap_id = 0x03320000, .type = 0x16100000},
	{ .jtag_id = 0xb576, .die_rev = 0x2, .omap_id = 0x03320100, .type = 0x16110000},
	{ .jtag_id = 0xb576, .die_rev = 0x3, .omap_id = 0x03320100, .type = 0x16100c00},
	{ .jtag_id = 0xb576, .die_rev = 0x0, .omap_id = 0x03320200, .type = 0x16100d00},
	{ .jtag_id = 0xb613, .die_rev = 0x0, .omap_id = 0x03320300, .type = 0x1610ef00},
	{ .jtag_id = 0xb613, .die_rev = 0x0, .omap_id = 0x03320300, .type = 0x1610ef00},
	{ .jtag_id = 0xb576, .die_rev = 0x1, .omap_id = 0x03320100, .type = 0x16110000},
	{ .jtag_id = 0xb58c, .die_rev = 0x2, .omap_id = 0x03320200, .type = 0x16110b00},
	{ .jtag_id = 0xb58c, .die_rev = 0x3, .omap_id = 0x03320200, .type = 0x16110c00},
	{ .jtag_id = 0xb65f, .die_rev = 0x0, .omap_id = 0x03320400, .type = 0x16212300},
	{ .jtag_id = 0xb65f, .die_rev = 0x1, .omap_id = 0x03320400, .type = 0x16212300},
	{ .jtag_id = 0xb65f, .die_rev = 0x1, .omap_id = 0x03320500, .type = 0x16212300},
	{ .jtag_id = 0xb5f7, .die_rev = 0x0, .omap_id = 0x03330000, .type = 0x17100000},
	{ .jtag_id = 0xb5f7, .die_rev = 0x1, .omap_id = 0x03330100, .type = 0x17100000},
	{ .jtag_id = 0xb5f7, .die_rev = 0x2, .omap_id = 0x03330100, .type = 0x17100000},
};

/*
 * Get OMAP type from PROD_ID.
 * 1710 has the PROD_ID in bits 15:00, not in 16:01 as documented in TRM.
 * 1510 PROD_ID is empty, and 1610 PROD_ID does not make sense.
 * Undocumented register in TEST BLOCK is used as fallback; This seems to
 * work on 1510, 1610 & 1710. The official way hopefully will work in future
 * processors.
 */
static u16 __init omap_get_jtag_id(void)
{
	u32 prod_id, omap_id;

	prod_id = omap_readl(OMAP_PRODUCTION_ID_1);
	omap_id = omap_readl(OMAP32_ID_1);

	/* Check for unusable OMAP_PRODUCTION_ID_1 on 1611B/5912 and 730 */
	if (((prod_id >> 20) == 0) || (prod_id == omap_id))
		prod_id = 0;
	else
		prod_id &= 0xffff;

	if (prod_id)
		return prod_id;

	/* Use OMAP32_ID_1 as fallback */
	prod_id = ((omap_id >> 12) & 0xffff);

	return prod_id;
}

/*
 * Get OMAP revision from DIE_REV.
 * Early 1710 processors may have broken OMAP_DIE_ID, it contains PROD_ID.
 * Undocumented register in the TEST BLOCK is used as fallback.
 * REVISIT: This does not seem to work on 1510
 */
static u8 __init omap_get_die_rev(void)
{
	u32 die_rev;

	die_rev = omap_readl(OMAP_DIE_ID_1);

	/* Check for broken OMAP_DIE_ID on early 1710 */
	if (((die_rev >> 12) & 0xffff) == omap_get_jtag_id())
		die_rev = 0;

	die_rev = (die_rev >> 17) & 0xf;
	if (die_rev)
		return die_rev;

	die_rev = (omap_readl(OMAP32_ID_1) >> 28) & 0xf;

	return die_rev;
}

static void __init omap_check_revision(void)
{
	int i;
	u16 jtag_id;
	u8 die_rev;
	u32 omap_id;
	u8 cpu_type;

	jtag_id = omap_get_jtag_id();
	die_rev = omap_get_die_rev();
	omap_id = omap_readl(OMAP32_ID_0);

#ifdef DEBUG
	printk("OMAP_DIE_ID_0: 0x%08x\n", omap_readl(OMAP_DIE_ID_0));
	printk("OMAP_DIE_ID_1: 0x%08x DIE_REV: %i\n",
		omap_readl(OMAP_DIE_ID_1),
	       (omap_readl(OMAP_DIE_ID_1) >> 17) & 0xf);
	printk("OMAP_PRODUCTION_ID_0: 0x%08x\n", omap_readl(OMAP_PRODUCTION_ID_0));
	printk("OMAP_PRODUCTION_ID_1: 0x%08x JTAG_ID: 0x%04x\n",
		omap_readl(OMAP_PRODUCTION_ID_1),
		omap_readl(OMAP_PRODUCTION_ID_1) & 0xffff);
	printk("OMAP32_ID_0: 0x%08x\n", omap_readl(OMAP32_ID_0));
	printk("OMAP32_ID_1: 0x%08x\n", omap_readl(OMAP32_ID_1));
	printk("JTAG_ID: 0x%04x DIE_REV: %i\n", jtag_id, die_rev);
#endif

	system_serial_high = omap_readl(OMAP_DIE_ID_0);
	system_serial_low = omap_readl(OMAP_DIE_ID_1);

	/* First check only the major version in a safe way */
	for (i = 0; i < ARRAY_SIZE(omap_ids); i++) {
		if (jtag_id == (omap_ids[i].jtag_id)) {
			system_rev = omap_ids[i].type;
			break;
		}
	}

	/* Check if we can find the die revision */
	for (i = 0; i < ARRAY_SIZE(omap_ids); i++) {
		if (jtag_id == omap_ids[i].jtag_id && die_rev == omap_ids[i].die_rev) {
			system_rev = omap_ids[i].type;
			break;
		}
	}

	/* Finally check also the omap_id */
	for (i = 0; i < ARRAY_SIZE(omap_ids); i++) {
		if (jtag_id == omap_ids[i].jtag_id
		    && die_rev == omap_ids[i].die_rev
		    && omap_id == omap_ids[i].omap_id) {
			system_rev = omap_ids[i].type;
			break;
		}
	}

	/* Add the cpu class info (7xx, 15xx, 16xx, 24xx) */
	cpu_type = system_rev >> 24;

	switch (cpu_type) {
	case 0x07:
		system_rev |= 0x07;
		break;
	case 0x15:
		system_rev |= 0x15;
		break;
	case 0x16:
	case 0x17:
		system_rev |= 0x16;
		break;
	case 0x24:
		system_rev |= 0x24;
		break;
	default:
		printk("Unknown OMAP cpu type: 0x%02x\n", cpu_type);
	}

	printk("OMAP%04x", system_rev >> 16);
	if ((system_rev >> 8) & 0xff)
		printk("%x", (system_rev >> 8) & 0xff);
	printk(" revision %i handled as %02xxx id: %08x%08x\n",
	       die_rev, system_rev & 0xff, system_serial_low,
	       system_serial_high);
}

/*
 * ----------------------------------------------------------------------------
 * OMAP I/O mapping
 *
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 * ----------------------------------------------------------------------------
 */

static struct map_desc omap_io_desc[] __initdata = {
 { IO_VIRT,      	IO_PHYS,             IO_SIZE,        	   MT_DEVICE },
};

#ifdef CONFIG_ARCH_OMAP730
static struct map_desc omap730_io_desc[] __initdata = {
 { OMAP730_DSP_BASE,    OMAP730_DSP_START,    OMAP730_DSP_SIZE,    MT_DEVICE },
 { OMAP730_DSPREG_BASE, OMAP730_DSPREG_START, OMAP730_DSPREG_SIZE, MT_DEVICE },
 { OMAP730_SRAM_BASE,   OMAP730_SRAM_START,   OMAP730_SRAM_SIZE,   MT_DEVICE }
};
#endif

#ifdef CONFIG_ARCH_OMAP1510
static struct map_desc omap1510_io_desc[] __initdata = {
 { OMAP1510_DSP_BASE,    OMAP1510_DSP_START,    OMAP1510_DSP_SIZE,    MT_DEVICE },
 { OMAP1510_DSPREG_BASE, OMAP1510_DSPREG_START, OMAP1510_DSPREG_SIZE, MT_DEVICE },
 { OMAP1510_SRAM_BASE,   OMAP1510_SRAM_START,   OMAP1510_SRAM_SIZE,   MT_DEVICE }
};
#endif

#if defined(CONFIG_ARCH_OMAP16XX)
static struct map_desc omap1610_io_desc[] __initdata = {
 { OMAP16XX_DSP_BASE,    OMAP16XX_DSP_START,    OMAP16XX_DSP_SIZE,    MT_DEVICE },
 { OMAP16XX_DSPREG_BASE, OMAP16XX_DSPREG_START, OMAP16XX_DSPREG_SIZE, MT_DEVICE },
 { OMAP16XX_SRAM_BASE,   OMAP16XX_SRAM_START,   OMAP1610_SRAM_SIZE,   MT_DEVICE }
};

static struct map_desc omap5912_io_desc[] __initdata = {
 { OMAP16XX_DSP_BASE,    OMAP16XX_DSP_START,    OMAP16XX_DSP_SIZE,    MT_DEVICE },
 { OMAP16XX_DSPREG_BASE, OMAP16XX_DSPREG_START, OMAP16XX_DSPREG_SIZE, MT_DEVICE },
/*
 * The OMAP5912 has 250kByte internal SRAM. Because the mapping is baseed on page
 * size (4kByte), it seems that the last 2kByte (=0x800) of the 250kByte are not mapped.
 * Add additional 2kByte (0x800) so that the last page is mapped and the last 2kByte
 * can be used.
 */
 { OMAP16XX_SRAM_BASE,   OMAP16XX_SRAM_START,   OMAP5912_SRAM_SIZE + 0x800,   MT_DEVICE }
};
#endif

static int initialized = 0;

static void __init _omap_map_io(void)
{
	initialized = 1;

	/* We have to initialize the IO space mapping before we can run
	 * cpu_is_omapxxx() macros. */
	iotable_init(omap_io_desc, ARRAY_SIZE(omap_io_desc));
	omap_check_revision();

#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		iotable_init(omap730_io_desc, ARRAY_SIZE(omap730_io_desc));
	}
#endif
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		iotable_init(omap1510_io_desc, ARRAY_SIZE(omap1510_io_desc));
	}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (cpu_is_omap1610() || cpu_is_omap1710()) {
		iotable_init(omap1610_io_desc, ARRAY_SIZE(omap1610_io_desc));
	}
	if (cpu_is_omap5912()) {
		iotable_init(omap5912_io_desc, ARRAY_SIZE(omap5912_io_desc));
	}
#endif

	/* REVISIT: Refer to OMAP5910 Errata, Advisory SYS_1: "Timeout Abort
	 * on a Posted Write in the TIPB Bridge".
	 */
	omap_writew(0x0, MPU_PUBLIC_TIPB_CNTL);
	omap_writew(0x0, MPU_PRIVATE_TIPB_CNTL);

	/* Must init clocks early to assure that timer interrupt works
	 */
	clk_init();
}

/*
 * This should only get called from board specific init
 */
void omap_map_io(void)
{
	if (!initialized)
		_omap_map_io();
}

static inline unsigned int omap_serial_in(struct plat_serial8250_port *up,
					  int offset)
{
	offset <<= up->regshift;
	return (unsigned int)__raw_readb(up->membase + offset);
}

static inline void omap_serial_outp(struct plat_serial8250_port *p, int offset,
				    int value)
{
	offset <<= p->regshift;
	__raw_writeb(value, p->membase + offset);
}

/*
 * Internal UARTs need to be initialized for the 8250 autoconfig to work
 * properly. Note that the TX watermark initialization may not be needed
 * once the 8250.c watermark handling code is merged.
 */
static void __init omap_serial_reset(struct plat_serial8250_port *p)
{
	omap_serial_outp(p, UART_OMAP_MDR1, 0x07);	/* disable UART */
	omap_serial_outp(p, UART_OMAP_SCR, 0x08);	/* TX watermark */
	omap_serial_outp(p, UART_OMAP_MDR1, 0x00);	/* enable UART */

	if (!cpu_is_omap1510()) {
		omap_serial_outp(p, UART_OMAP_SYSC, 0x01);
		while (!(omap_serial_in(p, UART_OMAP_SYSC) & 0x01));
	}
}

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.membase	= (char*)IO_ADDRESS(OMAP_UART1_BASE),
		.mapbase	= (unsigned long)OMAP_UART1_BASE,
		.irq		= INT_UART1,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= OMAP16XX_BASE_BAUD * 16,
	},
	{
		.membase	= (char*)IO_ADDRESS(OMAP_UART2_BASE),
		.mapbase	= (unsigned long)OMAP_UART2_BASE,
		.irq		= INT_UART2,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= OMAP16XX_BASE_BAUD * 16,
	},
	{
		.membase	= (char*)IO_ADDRESS(OMAP_UART3_BASE),
		.mapbase	= (unsigned long)OMAP_UART3_BASE,
		.irq		= INT_UART3,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= OMAP16XX_BASE_BAUD * 16,
	},
	{ },
};

static struct platform_device serial_device = {
	.name			= "serial8250",
	.id			= 0,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
};

/*
 * Note that on Innovator-1510 UART2 pins conflict with USB2.
 * By default UART2 does not work on Innovator-1510 if you have
 * USB OHCI enabled. To use UART2, you must disable USB2 first.
 */
void __init omap_serial_init(int ports[OMAP_MAX_NR_PORTS])
{
	int i;

	if (cpu_is_omap730()) {
		serial_platform_data[0].regshift = 0;
		serial_platform_data[1].regshift = 0;
		serial_platform_data[0].irq = INT_730_UART_MODEM_1;
		serial_platform_data[1].irq = INT_730_UART_MODEM_IRDA_2;
	}

	if (cpu_is_omap1510()) {
		serial_platform_data[0].uartclk = OMAP1510_BASE_BAUD * 16;
		serial_platform_data[1].uartclk = OMAP1510_BASE_BAUD * 16;
		serial_platform_data[2].uartclk = OMAP1510_BASE_BAUD * 16;
	}

	for (i = 0; i < OMAP_MAX_NR_PORTS; i++) {
		unsigned char reg;

		if (ports[i] == 0) {
			serial_platform_data[i].membase = 0;
			serial_platform_data[i].mapbase = 0;
			continue;
		}

		switch (i) {
		case 0:
			if (cpu_is_omap1510()) {
				omap_cfg_reg(UART1_TX);
				omap_cfg_reg(UART1_RTS);
				if (machine_is_omap_innovator()) {
					reg = fpga_read(OMAP1510_FPGA_POWER);
					reg |= OMAP1510_FPGA_PCR_COM1_EN;
					fpga_write(reg, OMAP1510_FPGA_POWER);
					udelay(10);
				}
			}
			break;
		case 1:
			if (cpu_is_omap1510()) {
				omap_cfg_reg(UART2_TX);
				omap_cfg_reg(UART2_RTS);
				if (machine_is_omap_innovator()) {
					reg = fpga_read(OMAP1510_FPGA_POWER);
					reg |= OMAP1510_FPGA_PCR_COM2_EN;
					fpga_write(reg, OMAP1510_FPGA_POWER);
					udelay(10);
				}
			}
			break;
		case 2:
			if (cpu_is_omap1510()) {
				omap_cfg_reg(UART3_TX);
				omap_cfg_reg(UART3_RX);
			}
			if (cpu_is_omap1710()) {
				clk_enable(clk_get(0, "uart3_ck"));
			}
			break;
		}
		omap_serial_reset(&serial_platform_data[i]);
	}
}

static int __init omap_init(void)
{
	return platform_device_register(&serial_device);
}
arch_initcall(omap_init);

#define NO_LENGTH_CHECK 0xffffffff

extern int omap_bootloader_tag_len;
extern u8 omap_bootloader_tag[];

struct omap_board_config_kernel *omap_board_config;
int omap_board_config_size = 0;

static const void *get_config(u16 tag, size_t len, int skip, size_t *len_out)
{
	struct omap_board_config_kernel *kinfo = NULL;
	int i;

#ifdef CONFIG_OMAP_BOOT_TAG
	struct omap_board_config_entry *info = NULL;

	if (omap_bootloader_tag_len > 4)
		info = (struct omap_board_config_entry *) omap_bootloader_tag;
	while (info != NULL) {
		u8 *next;

		if (info->tag == tag) {
			if (skip == 0)
				break;
			skip--;
		}

		if ((info->len & 0x03) != 0) {
			/* We bail out to avoid an alignment fault */
			printk(KERN_ERR "OMAP peripheral config: Length (%d) not word-aligned (tag %04x)\n",
			       info->len, info->tag);
			return NULL;
		}
		next = (u8 *) info + sizeof(*info) + info->len;
		if (next >= omap_bootloader_tag + omap_bootloader_tag_len)
			info = NULL;
		else
			info = (struct omap_board_config_entry *) next;
	}
	if (info != NULL) {
		/* Check the length as a lame attempt to check for
		 * binary inconsistancy. */
		if (len != NO_LENGTH_CHECK) {
			/* Word-align len */
			if (len & 0x03)
				len = (len + 3) & ~0x03;
			if (info->len != len) {
				printk(KERN_ERR "OMAP peripheral config: Length mismatch with tag %x (want %d, got %d)\n",
				       tag, len, info->len);
				return NULL;
			}
		}
		if (len_out != NULL)
			*len_out = info->len;
		return info->data;
	}
#endif
	/* Try to find the config from the board-specific structures
	 * in the kernel. */
	for (i = 0; i < omap_board_config_size; i++) {
		if (omap_board_config[i].tag == tag) {
			kinfo = &omap_board_config[i];
			break;
		}
	}
	if (kinfo == NULL)
		return NULL;
	return kinfo->data;
}

const void *__omap_get_config(u16 tag, size_t len, int nr)
{
        return get_config(tag, len, nr, NULL);
}
EXPORT_SYMBOL(__omap_get_config);

const void *omap_get_var_config(u16 tag, size_t *len)
{
        return get_config(tag, NO_LENGTH_CHECK, 0, len);
}
EXPORT_SYMBOL(omap_get_var_config);

static int __init omap_add_serial_console(void)
{
	const struct omap_uart_config *info;

	info = omap_get_config(OMAP_TAG_UART, struct omap_uart_config);
	if (info != NULL && info->console_uart) {
		static char speed[11], *opt = NULL;

		if (info->console_speed) {
			snprintf(speed, sizeof(speed), "%u", info->console_speed);
			opt = speed;
		}
		return add_preferred_console("ttyS", info->console_uart - 1, opt);
	}
	return 0;
}
console_initcall(omap_add_serial_console);
