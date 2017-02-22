/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

/* We'll start to rename these registers once we have a better
 * understanding of their meaning.
 */
#define REG_000 0x000
#define REG_001 0x001
#define REG_002 0x002
#define REG_003 0x003

#define AU0828_SENSORCTRL_100 0x100
#define AU0828_SENSORCTRL_VBI_103 0x103

/* I2C registers */
#define AU0828_I2C_TRIGGER_200		0x200
#define AU0828_I2C_STATUS_201		0x201
#define AU0828_I2C_CLK_DIVIDER_202	0x202
#define AU0828_I2C_DEST_ADDR_203	0x203
#define AU0828_I2C_WRITE_FIFO_205	0x205
#define AU0828_I2C_READ_FIFO_209	0x209
#define AU0828_I2C_MULTIBYTE_MODE_2FF	0x2ff

/* Audio registers */
#define AU0828_AUDIOCTRL_50C 0x50C

#define REG_600 0x600

/*********************************************************************/
/* Here are constants for values associated with the above registers */

/* I2C Trigger (Reg 0x200) */
#define AU0828_I2C_TRIGGER_WRITE	0x01
#define AU0828_I2C_TRIGGER_READ		0x20
#define AU0828_I2C_TRIGGER_HOLD		0x40

/* I2C Status (Reg 0x201) */
#define AU0828_I2C_STATUS_READ_DONE	0x01
#define AU0828_I2C_STATUS_NO_READ_ACK	0x02
#define AU0828_I2C_STATUS_WRITE_DONE	0x04
#define AU0828_I2C_STATUS_NO_WRITE_ACK	0x08
#define AU0828_I2C_STATUS_BUSY		0x10

/* I2C Clock Divider (Reg 0x202) */
#define AU0828_I2C_CLK_250KHZ 0x07
#define AU0828_I2C_CLK_100KHZ 0x14
#define AU0828_I2C_CLK_30KHZ  0x40
#define AU0828_I2C_CLK_20KHZ  0x60
