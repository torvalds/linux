/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-netx/include/mach/hardware.h
 *
 * Copyright (C) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#define NETX_IO_PHYS	0x00100000
#define NETX_IO_VIRT	0xe0000000
#define NETX_IO_SIZE	0x00100000

#define SRAM_INTERNAL_PHYS_0 0x00000
#define SRAM_INTERNAL_PHYS_1 0x08000
#define SRAM_INTERNAL_PHYS_2 0x10000
#define SRAM_INTERNAL_PHYS_3 0x18000
#define SRAM_INTERNAL_PHYS(no) ((no) * 0x8000)

#define XPEC_MEM_SIZE 0x4000
#define XMAC_MEM_SIZE 0x1000
#define SRAM_MEM_SIZE 0x8000

#define io_p2v(x) IOMEM((x) - NETX_IO_PHYS + NETX_IO_VIRT)
#define io_v2p(x) ((x) - NETX_IO_VIRT + NETX_IO_PHYS)

#endif
