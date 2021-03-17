/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2000,2001,2002,2003,2004 Broadcom Corporation
 */
#ifndef __ASM_SIBYTE_BIGSUR_H
#define __ASM_SIBYTE_BIGSUR_H

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/bcm1480_int.h>

#ifdef CONFIG_SIBYTE_BIGSUR
#define SIBYTE_BOARD_NAME "BCM91x80A/B (BigSur)"
#define SIBYTE_HAVE_PCMCIA 1
#define SIBYTE_HAVE_IDE	   1
#endif

/* Generic bus chip selects */
#define LEDS_CS		3
#define LEDS_PHYS	0x100a0000

#ifdef SIBYTE_HAVE_IDE
#define IDE_CS		4
#define IDE_PHYS	0x100b0000
#define K_GPIO_GB_IDE	4
#define K_INT_GB_IDE	(K_INT_GPIO_0 + K_GPIO_GB_IDE)
#endif

#ifdef SIBYTE_HAVE_PCMCIA
#define PCMCIA_CS	6
#define PCMCIA_PHYS	0x11000000
#define K_GPIO_PC_READY 9
#define K_INT_PC_READY	(K_INT_GPIO_0 + K_GPIO_PC_READY)
#endif

#endif /* __ASM_SIBYTE_BIGSUR_H */
