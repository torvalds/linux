/*
 * PIC32 pinctrl driver
 *
 * Joshua Henderson, <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#ifndef PINCTRL_PINCTRL_PIC32_H
#define PINCTRL_PINCTRL_PIC32_H

/* PORT Registers */
#define ANSEL_REG	0x00
#define TRIS_REG	0x10
#define PORT_REG	0x20
#define LAT_REG		0x30
#define ODCU_REG	0x40
#define CNPU_REG	0x50
#define CNPD_REG	0x60
#define CNCON_REG	0x70
#define CNEN_REG	0x80
#define CNSTAT_REG	0x90
#define CNNE_REG	0xA0
#define CNF_REG		0xB0

/* Input PPS Registers */
#define INT1R 0x04
#define INT2R 0x08
#define INT3R 0x0C
#define INT4R 0x10
#define T2CKR 0x18
#define T3CKR 0x1C
#define T4CKR 0x20
#define T5CKR 0x24
#define T6CKR 0x28
#define T7CKR 0x2C
#define T8CKR 0x30
#define T9CKR 0x34
#define IC1R 0x38
#define IC2R 0x3C
#define IC3R 0x40
#define IC4R 0x44
#define IC5R 0x48
#define IC6R 0x4C
#define IC7R 0x50
#define IC8R 0x54
#define IC9R 0x58
#define OCFAR 0x60
#define U1RXR 0x68
#define U1CTSR 0x6C
#define U2RXR 0x70
#define U2CTSR 0x74
#define U3RXR 0x78
#define U3CTSR 0x7C
#define U4RXR 0x80
#define U4CTSR 0x84
#define U5RXR 0x88
#define U5CTSR 0x8C
#define U6RXR 0x90
#define U6CTSR 0x94
#define SDI1R 0x9C
#define SS1INR 0xA0
#define SDI2R 0xA8
#define SS2INR 0xAC
#define SDI3R 0xB4
#define SS3INR 0xB8
#define SDI4R 0xC0
#define SS4INR 0xC4
#define SDI5R 0xCC
#define SS5INR 0xD0
#define SDI6R 0xD8
#define SS6INR 0xDC
#define C1RXR 0xE0
#define C2RXR 0xE4
#define REFCLKI1R 0xE8
#define REFCLKI3R 0xF0
#define REFCLKI4R 0xF4

/* Output PPS Registers */
#define RPA14R 0x138
#define RPA15R 0x13C
#define RPB0R 0x140
#define RPB1R 0x144
#define RPB2R 0x148
#define RPB3R 0x14C
#define RPB5R 0x154
#define RPB6R 0x158
#define RPB7R 0x15C
#define RPB8R 0x160
#define RPB9R 0x164
#define RPB10R 0x168
#define RPB14R 0x178
#define RPB15R 0x17C
#define RPC1R 0x184
#define RPC2R 0x188
#define RPC3R 0x18C
#define RPC4R 0x190
#define RPC13R 0x1B4
#define RPC14R 0x1B8
#define RPD0R 0x1C0
#define RPD1R 0x1C4
#define RPD2R 0x1C8
#define RPD3R 0x1CC
#define RPD4R 0x1D0
#define RPD5R 0x1D4
#define RPD6R 0x1D8
#define RPD7R 0x1DC
#define RPD9R 0x1E4
#define RPD10R 0x1E8
#define RPD11R 0x1EC
#define RPD12R 0x1F0
#define RPD14R 0x1F8
#define RPD15R 0x1FC
#define RPE3R 0x20C
#define RPE5R 0x214
#define RPE8R 0x220
#define RPE9R 0x224
#define RPF0R 0x240
#define RPF1R 0x244
#define RPF2R 0x248
#define RPF3R 0x24C
#define RPF4R 0x250
#define RPF5R 0x254
#define RPF8R 0x260
#define RPF12R 0x270
#define RPF13R 0x274
#define RPG0R 0x280
#define RPG1R 0x284
#define RPG6R 0x298
#define RPG7R 0x29C
#define RPG8R 0x2A0
#define RPG9R 0x2A4

#endif  /* PINCTRL_PINCTRL_PIC32_H */
