/* linux/arch/arm/mach-s5p64x0/include/mach/uncompress.h
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - uncompress code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <mach/map.h>

/*
 * cannot use commonly <plat/uncompress.h>
 * because uart base of S5P6440 and S5P6450 is different
 */

typedef unsigned int upf_t;	/* cannot include linux/serial_core.h */

/* uart setup */

unsigned int fifo_mask;
unsigned int fifo_max;

/* forward declerations */

static void arch_detect_cpu(void);

/* defines for UART registers */

#include <plat/regs-serial.h>
#include <plat/regs-watchdog.h>

/* working in physical space... */
#undef S3C2410_WDOGREG
#define S3C2410_WDOGREG(x) ((S3C24XX_PA_WATCHDOG + (x)))

/* how many bytes we allow into the FIFO at a time in FIFO mode */
#define FIFO_MAX	 (14)

unsigned long uart_base;

static __inline__ void get_uart_base(void)
{
	unsigned int chipid;

	chipid = *(const volatile unsigned int __force *) 0xE0100118;

	uart_base = S3C_UART_OFFSET * CONFIG_S3C_LOWLEVEL_UART_PORT;

	if ((chipid & 0xff000) == 0x50000)
		uart_base += 0xEC800000;
	else
		uart_base += 0xEC000000;
}

static __inline__ void uart_wr(unsigned int reg, unsigned int val)
{
	volatile unsigned int *ptr;

	get_uart_base();
	ptr = (volatile unsigned int *)(reg + uart_base);
	*ptr = val;
}

static __inline__ unsigned int uart_rd(unsigned int reg)
{
	volatile unsigned int *ptr;

	get_uart_base();
	ptr = (volatile unsigned int *)(reg + uart_base);
	return *ptr;
}

/*
 * we can deal with the case the UARTs are being run
 * in FIFO mode, so that we don't hold up our execution
 * waiting for tx to happen...
 */

static void putc(int ch)
{
	if (uart_rd(S3C2410_UFCON) & S3C2410_UFCON_FIFOMODE) {
		int level;

		while (1) {
			level = uart_rd(S3C2410_UFSTAT);
			level &= fifo_mask;

			if (level < fifo_max)
				break;
		}

	} else {
		/* not using fifos */

		while ((uart_rd(S3C2410_UTRSTAT) & S3C2410_UTRSTAT_TXE) != S3C2410_UTRSTAT_TXE)
			barrier();
	}

	/* write byte to transmission register */
	uart_wr(S3C2410_UTXH, ch);
}

static inline void flush(void)
{
}

#define __raw_writel(d, ad)			\
	do {							\
		*((volatile unsigned int __force *)(ad)) = (d); \
	} while (0)

/*
 * CONFIG_S3C_BOOT_WATCHDOG
 *
 * Simple boot-time watchdog setup, to reboot the system if there is
 * any problem with the boot process
 */

#ifdef CONFIG_S3C_BOOT_WATCHDOG

#define WDOG_COUNT (0xff00)

static inline void arch_decomp_wdog(void)
{
	__raw_writel(WDOG_COUNT, S3C2410_WTCNT);
}

static void arch_decomp_wdog_start(void)
{
	__raw_writel(WDOG_COUNT, S3C2410_WTDAT);
	__raw_writel(WDOG_COUNT, S3C2410_WTCNT);
	__raw_writel(S3C2410_WTCON_ENABLE | S3C2410_WTCON_DIV128 | S3C2410_WTCON_RSTEN | S3C2410_WTCON_PRESCALE(0x80), S3C2410_WTCON);
}

#else
#define arch_decomp_wdog_start()
#define arch_decomp_wdog()
#endif

#ifdef CONFIG_S3C_BOOT_ERROR_RESET

static void arch_decomp_error(const char *x)
{
	putstr("\n\n");
	putstr(x);
	putstr("\n\n -- System resetting\n");

	__raw_writel(0x4000, S3C2410_WTDAT);
	__raw_writel(0x4000, S3C2410_WTCNT);
	__raw_writel(S3C2410_WTCON_ENABLE | S3C2410_WTCON_DIV128 | S3C2410_WTCON_RSTEN | S3C2410_WTCON_PRESCALE(0x40), S3C2410_WTCON);

	while(1);
}

#define arch_error arch_decomp_error
#endif

#ifdef CONFIG_S3C_BOOT_UART_FORCE_FIFO
static inline void arch_enable_uart_fifo(void)
{
	u32 fifocon = uart_rd(S3C2410_UFCON);

	if (!(fifocon & S3C2410_UFCON_FIFOMODE)) {
		fifocon |= S3C2410_UFCON_RESETBOTH;
		uart_wr(S3C2410_UFCON, fifocon);

		/* wait for fifo reset to complete */
		while (1) {
			fifocon = uart_rd(S3C2410_UFCON);
			if (!(fifocon & S3C2410_UFCON_RESETBOTH))
				break;
		}
	}
}
#else
#define arch_enable_uart_fifo() do { } while(0)
#endif

static void arch_decomp_setup(void)
{
	/*
	 * we may need to setup the uart(s) here if we are not running
	 * on an BAST... the BAST will have left the uarts configured
	 * after calling linux.
	 */

	arch_detect_cpu();
	arch_decomp_wdog_start();

	/*
	 * Enable the UART FIFOs if they where not enabled and our
	 * configuration says we should turn them on.
	 */

	arch_enable_uart_fifo();
}



static void arch_detect_cpu(void)
{
	/* we do not need to do any cpu detection here at the moment. */
}

#endif /* __ASM_ARCH_UNCOMPRESS_H */
