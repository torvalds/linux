/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
 */
#ifndef __ASM_SIBYTE_SENTOSA_H
#define __ASM_SIBYTE_SENTOSA_H

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_int.h>

#ifdef CONFIG_SIBYTE_SENTOSA
#define SIBYTE_BOARD_NAME "BCM91250E (Sentosa)"
#endif
#ifdef CONFIG_SIBYTE_RHONE
#define SIBYTE_BOARD_NAME "BCM91125E (Rhone)"
#endif

/* Generic bus chip selects */
#ifdef CONFIG_SIBYTE_RHONE
#define LEDS_CS		6
#define LEDS_PHYS	0x1d0a0000
#endif

/* GPIOs */
#define K_GPIO_DBG_LED	0

#endif /* __ASM_SIBYTE_SENTOSA_H */
