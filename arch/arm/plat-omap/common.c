/*
 * linux/arch/arm/plat-omap/common.c
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
#include <linux/clk.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/setup.h>

#include <asm/arch/board.h>
#include <asm/arch/mux.h>
#include <asm/arch/fpga.h>

#include <asm/arch/clock.h>

#define NO_LENGTH_CHECK 0xffffffff

unsigned char omap_bootloader_tag[512];
int omap_bootloader_tag_len;

struct omap_board_config_kernel *omap_board_config;
int omap_board_config_size;

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
	const struct omap_serial_console_config *con_info;
	const struct omap_uart_config *uart_info;
	static char speed[11], *opt = NULL;
	int line, i, uart_idx;

	uart_info = omap_get_config(OMAP_TAG_UART, struct omap_uart_config);
	con_info = omap_get_config(OMAP_TAG_SERIAL_CONSOLE,
					struct omap_serial_console_config);
	if (uart_info == NULL || con_info == NULL)
		return 0;

	if (con_info->console_uart == 0)
		return 0;

	if (con_info->console_speed) {
		snprintf(speed, sizeof(speed), "%u", con_info->console_speed);
		opt = speed;
	}

	uart_idx = con_info->console_uart - 1;
	if (uart_idx >= OMAP_MAX_NR_PORTS) {
		printk(KERN_INFO "Console: external UART#%d. "
			"Not adding it as console this time.\n",
			uart_idx + 1);
		return 0;
	}
	if (!(uart_info->enabled_uarts & (1 << uart_idx))) {
		printk(KERN_ERR "Console: Selected UART#%d is "
			"not enabled for this platform\n",
			uart_idx + 1);
		return -1;
	}
	line = 0;
	for (i = 0; i < uart_idx; i++) {
		if (uart_info->enabled_uarts & (1 << i))
			line++;
	}
	return add_preferred_console("ttyS", line, opt);
}
console_initcall(omap_add_serial_console);
