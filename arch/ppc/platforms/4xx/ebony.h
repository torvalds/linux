/*
 * arch/ppc/platforms/ebony.h
 *
 * Ebony board definitions
 *
 * Matt Porter <mporter@mvista.com>
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_EBONY_H__
#define __ASM_EBONY_H__

#include <linux/config.h>
#include <platforms/4xx/ibm440gp.h>

/* F/W TLB mapping used in bootloader glue to reset EMAC */
#define PPC44x_EMAC0_MR0	0xE0000800

/* Where to find the MAC info */
#define OPENBIOS_MAC_BASE	0xfffffe0c
#define OPENBIOS_MAC_OFFSET	0x0c

/* Default clock rates for Rev. B and Rev. C silicon */
#define EBONY_440GP_RB_SYSCLK	33000000
#define EBONY_440GP_RC_SYSCLK	400000000

/* RTC/NVRAM location */
#define EBONY_RTC_ADDR		0x0000000148000000ULL
#define EBONY_RTC_SIZE		0x2000

/* Flash */
#define EBONY_FPGA_ADDR		0x0000000148300000ULL
#define EBONY_BOOT_SMALL_FLASH(x)	(x & 0x20)
#define EBONY_ONBRD_FLASH_EN(x)		(x & 0x02)
#define EBONY_FLASH_SEL(x)		(x & 0x01)
#define EBONY_SMALL_FLASH_LOW1	0x00000001ff800000ULL
#define EBONY_SMALL_FLASH_LOW2	0x00000001ff880000ULL
#define EBONY_SMALL_FLASH_HIGH1	0x00000001fff00000ULL
#define EBONY_SMALL_FLASH_HIGH2	0x00000001fff80000ULL
#define EBONY_SMALL_FLASH_SIZE	0x80000
#define EBONY_LARGE_FLASH_LOW	0x00000001ff800000ULL
#define EBONY_LARGE_FLASH_HIGH	0x00000001ffc00000ULL
#define EBONY_LARGE_FLASH_SIZE	0x400000

#define EBONY_SMALL_FLASH_BASE	0x00000001fff80000ULL
#define EBONY_LARGE_FLASH_BASE	0x00000001ff800000ULL

/*
 * Serial port defines
 */

#if defined(__BOOTER__)
/* OpenBIOS defined UART mappings, used by bootloader shim */
#define UART0_IO_BASE	0xE0000200
#define UART1_IO_BASE	0xE0000300
#else
/* head_44x.S created UART mapping, used before early_serial_setup.
 * We cannot use default OpenBIOS UART mappings because they
 * don't work for configurations with more than 512M RAM.    --ebs
 */
#define UART0_IO_BASE	0xF0000200
#define UART1_IO_BASE	0xF0000300
#endif

/* external Epson SG-615P */
#define BASE_BAUD	691200

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (void*)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)

/* PCI support */
#define EBONY_PCI_LOWER_IO	0x00000000
#define EBONY_PCI_UPPER_IO	0x0000ffff
#define EBONY_PCI_LOWER_MEM	0x80002000
#define EBONY_PCI_UPPER_MEM	0xffffefff

#define EBONY_PCI_CFGREGS_BASE	0x000000020ec00000
#define EBONY_PCI_CFGA_PLB32	0x0ec00000
#define EBONY_PCI_CFGD_PLB32	0x0ec00004

#define EBONY_PCI_IO_BASE	0x0000000208000000ULL
#define EBONY_PCI_IO_SIZE	0x00010000
#define EBONY_PCI_MEM_OFFSET	0x00000000

#endif				/* __ASM_EBONY_H__ */
#endif				/* __KERNEL__ */
