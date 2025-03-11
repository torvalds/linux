/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * (c) 2001, 2006 IBM Corporation.
 */

#ifndef _ASM_POWERPC_UDBG_H
#define _ASM_POWERPC_UDBG_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/init.h>

extern void (*udbg_putc)(char c);
extern void (*udbg_flush)(void);
extern int (*udbg_getc)(void);
extern int (*udbg_getc_poll)(void);

void udbg_puts(const char *s);
int udbg_write(const char *s, int n);

void register_early_udbg_console(void);
void udbg_printf(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
void udbg_progress(char *s, unsigned short hex);

void __init udbg_uart_init_mmio(void __iomem *addr, unsigned int stride);
void __init udbg_uart_init_pio(unsigned long port, unsigned int stride);

void __init udbg_uart_setup(unsigned int speed, unsigned int clock);
unsigned int __init udbg_probe_uart_speed(unsigned int clock);

struct device_node;
void __init udbg_scc_init(int force_scc);
int udbg_adb_init(int force_btext);
void udbg_adb_init_early(void);

void __init udbg_early_init(void);
void __init udbg_init_debug_lpar(void);
void __init udbg_init_debug_lpar_hvsi(void);
void __init udbg_init_pmac_realmode(void);
void __init udbg_init_pas_realmode(void);
void __init udbg_init_rtas_panel(void);
void __init udbg_init_rtas_console(void);
void __init udbg_init_btext(void);
void __init udbg_init_44x_as1(void);
void __init udbg_init_cpm(void);
void __init udbg_init_usbgecko(void);
void __init udbg_init_memcons(void);
void __init udbg_init_ehv_bc(void);
void __init udbg_init_ps3gelic(void);
void __init udbg_init_debug_opal_raw(void);
void __init udbg_init_debug_opal_hvsi(void);
void __init udbg_init_debug_16550(void);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_UDBG_H */
