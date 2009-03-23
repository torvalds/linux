/*
 * GPIOs and interrupts for Palm Tungsten|E2 Handheld Computer
 *
 * Author:
 *		Carlos Eduardo Medaglia Dyonisio <cadu@nerdfeliz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _INCLUDE_PALMTE2_H_
#define _INCLUDE_PALMTE2_H_

/** HERE ARE GPIOs **/

/* GPIOs */
#define GPIO_NR_PALMTE2_SD_DETECT_N		10
#define GPIO_NR_PALMTE2_SD_POWER		55
#define GPIO_NR_PALMTE2_SD_READONLY		51

/* KEYS */
#define GPIO_NR_PALMTE2_KEY_NOTES	5
#define GPIO_NR_PALMTE2_KEY_TASKS	7
#define GPIO_NR_PALMTE2_KEY_CALENDAR	11
#define GPIO_NR_PALMTE2_KEY_CONTACTS	13
#define GPIO_NR_PALMTE2_KEY_CENTER	14
#define GPIO_NR_PALMTE2_KEY_LEFT	19
#define GPIO_NR_PALMTE2_KEY_RIGHT	20
#define GPIO_NR_PALMTE2_KEY_DOWN	21
#define GPIO_NR_PALMTE2_KEY_UP		22

/** HERE ARE INIT VALUES **/

/* BACKLIGHT */
#define PALMTE2_MAX_INTENSITY		0xFE
#define PALMTE2_DEFAULT_INTENSITY	0x7E
#define PALMTE2_LIMIT_MASK		0x7F
#define PALMTE2_PRESCALER		0x3F
#define PALMTE2_PERIOD_NS		3500

#endif
