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

extern void udbg_puts(const char *s);
extern int udbg_write(const char *s, int n);

extern void register_early_udbg_console(void);
extern void udbg_printf(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void udbg_progress(char *s, unsigned short hex);

void __init udbg_uart_init_mmio(void __iomem *addr, unsigned int stride);
void __init udbg_uart_init_pio(unsigned long port, unsigned int stride);

void __init udbg_uart_setup(unsigned int speed, unsigned int clock);
unsigned int __init udbg_probe_uart_speed(unsigned int clock);

struct device_node;
void __init udbg_scc_init(int force_scc);
extern int udbg_adb_init(int force_btext);
extern void udbg_adb_init_early(void);

extern void __init udbg_early_init(void);
extern void __init udbg_init_debug_lpar(void);
extern void __init udbg_init_debug_lpar_hvsi(void);
extern void __init udbg_init_pmac_realmode(void);
extern void __init udbg_init_maple_realmode(void);
extern void __init udbg_init_pas_realmode(void);
extern void __init udbg_init_rtas_panel(void);
extern void __init udbg_init_rtas_console(void);
extern void __init udbg_init_debug_beat(void);
extern void __init udbg_init_btext(void);
extern void __init udbg_init_44x_as1(void);
extern void __init udbg_init_40x_realmode(void);
extern void __init udbg_init_cpm(void);
extern void __init udbg_init_usbgecko(void);
extern void __init udbg_init_memcons(void);
extern void __init udbg_init_ehv_bc(void);
extern void __init udbg_init_ps3gelic(void);
extern void __init udbg_init_debug_opal_raw(void);
extern void __init udbg_init_debug_opal_hvsi(void);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_UDBG_H */
