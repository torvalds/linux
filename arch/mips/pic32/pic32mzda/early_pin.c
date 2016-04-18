/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 */
#include <asm/io.h>

#include "early_pin.h"

#define PPS_BASE 0x1f800000

/* Input PPS Registers */
#define INT1R 0x1404
#define INT2R 0x1408
#define INT3R 0x140C
#define INT4R 0x1410
#define T2CKR 0x1418
#define T3CKR 0x141C
#define T4CKR 0x1420
#define T5CKR 0x1424
#define T6CKR 0x1428
#define T7CKR 0x142C
#define T8CKR 0x1430
#define T9CKR 0x1434
#define IC1R 0x1438
#define IC2R 0x143C
#define IC3R 0x1440
#define IC4R 0x1444
#define IC5R 0x1448
#define IC6R 0x144C
#define IC7R 0x1450
#define IC8R 0x1454
#define IC9R 0x1458
#define OCFAR 0x1460
#define U1RXR 0x1468
#define U1CTSR 0x146C
#define U2RXR 0x1470
#define U2CTSR 0x1474
#define U3RXR 0x1478
#define U3CTSR 0x147C
#define U4RXR 0x1480
#define U4CTSR 0x1484
#define U5RXR 0x1488
#define U5CTSR 0x148C
#define U6RXR 0x1490
#define U6CTSR 0x1494
#define SDI1R 0x149C
#define SS1R 0x14A0
#define SDI2R 0x14A8
#define SS2R 0x14AC
#define SDI3R 0x14B4
#define SS3R 0x14B8
#define SDI4R 0x14C0
#define SS4R 0x14C4
#define SDI5R 0x14CC
#define SS5R 0x14D0
#define SDI6R 0x14D8
#define SS6R 0x14DC
#define C1RXR 0x14E0
#define C2RXR 0x14E4
#define REFCLKI1R 0x14E8
#define REFCLKI3R 0x14F0
#define REFCLKI4R 0x14F4

static const struct
{
	int function;
	int reg;
} input_pin_reg[] = {
	{ IN_FUNC_INT3, INT3R },
	{ IN_FUNC_T2CK, T2CKR },
	{ IN_FUNC_T6CK, T6CKR },
	{ IN_FUNC_IC3, IC3R  },
	{ IN_FUNC_IC7, IC7R },
	{ IN_FUNC_U1RX, U1RXR },
	{ IN_FUNC_U2CTS, U2CTSR },
	{ IN_FUNC_U5RX, U5RXR },
	{ IN_FUNC_U6CTS, U6CTSR },
	{ IN_FUNC_SDI1, SDI1R },
	{ IN_FUNC_SDI3, SDI3R },
	{ IN_FUNC_SDI5, SDI5R },
	{ IN_FUNC_SS6, SS6R },
	{ IN_FUNC_REFCLKI1, REFCLKI1R },
	{ IN_FUNC_INT4, INT4R },
	{ IN_FUNC_T5CK, T5CKR },
	{ IN_FUNC_T7CK, T7CKR },
	{ IN_FUNC_IC4, IC4R },
	{ IN_FUNC_IC8, IC8R },
	{ IN_FUNC_U3RX, U3RXR },
	{ IN_FUNC_U4CTS, U4CTSR },
	{ IN_FUNC_SDI2, SDI2R },
	{ IN_FUNC_SDI4, SDI4R },
	{ IN_FUNC_C1RX, C1RXR },
	{ IN_FUNC_REFCLKI4, REFCLKI4R },
	{ IN_FUNC_INT2, INT2R },
	{ IN_FUNC_T3CK, T3CKR },
	{ IN_FUNC_T8CK, T8CKR },
	{ IN_FUNC_IC2, IC2R },
	{ IN_FUNC_IC5, IC5R },
	{ IN_FUNC_IC9, IC9R },
	{ IN_FUNC_U1CTS, U1CTSR },
	{ IN_FUNC_U2RX, U2RXR },
	{ IN_FUNC_U5CTS, U5CTSR },
	{ IN_FUNC_SS1, SS1R },
	{ IN_FUNC_SS3, SS3R },
	{ IN_FUNC_SS4, SS4R },
	{ IN_FUNC_SS5, SS5R },
	{ IN_FUNC_C2RX, C2RXR },
	{ IN_FUNC_INT1, INT1R },
	{ IN_FUNC_T4CK, T4CKR },
	{ IN_FUNC_T9CK, T9CKR },
	{ IN_FUNC_IC1, IC1R },
	{ IN_FUNC_IC6, IC6R },
	{ IN_FUNC_U3CTS, U3CTSR },
	{ IN_FUNC_U4RX, U4RXR },
	{ IN_FUNC_U6RX, U6RXR },
	{ IN_FUNC_SS2, SS2R },
	{ IN_FUNC_SDI6, SDI6R },
	{ IN_FUNC_OCFA, OCFAR },
	{ IN_FUNC_REFCLKI3, REFCLKI3R },
};

void pic32_pps_input(int function, int pin)
{
	void __iomem *pps_base = ioremap_nocache(PPS_BASE, 0xF4);
	int i;

	for (i = 0; i < ARRAY_SIZE(input_pin_reg); i++) {
		if (input_pin_reg[i].function == function) {
			__raw_writel(pin, pps_base + input_pin_reg[i].reg);
			return;
		}
	}

	iounmap(pps_base);
}

/* Output PPS Registers */
#define RPA14R 0x1538
#define RPA15R 0x153C
#define RPB0R 0x1540
#define RPB1R 0x1544
#define RPB2R 0x1548
#define RPB3R 0x154C
#define RPB5R 0x1554
#define RPB6R 0x1558
#define RPB7R 0x155C
#define RPB8R 0x1560
#define RPB9R 0x1564
#define RPB10R 0x1568
#define RPB14R 0x1578
#define RPB15R 0x157C
#define RPC1R 0x1584
#define RPC2R 0x1588
#define RPC3R 0x158C
#define RPC4R 0x1590
#define RPC13R 0x15B4
#define RPC14R 0x15B8
#define RPD0R 0x15C0
#define RPD1R 0x15C4
#define RPD2R 0x15C8
#define RPD3R 0x15CC
#define RPD4R 0x15D0
#define RPD5R 0x15D4
#define RPD6R 0x15D8
#define RPD7R 0x15DC
#define RPD9R 0x15E4
#define RPD10R 0x15E8
#define RPD11R 0x15EC
#define RPD12R 0x15F0
#define RPD14R 0x15F8
#define RPD15R 0x15FC
#define RPE3R 0x160C
#define RPE5R 0x1614
#define RPE8R 0x1620
#define RPE9R 0x1624
#define RPF0R 0x1640
#define RPF1R 0x1644
#define RPF2R 0x1648
#define RPF3R 0x164C
#define RPF4R 0x1650
#define RPF5R 0x1654
#define RPF8R 0x1660
#define RPF12R 0x1670
#define RPF13R 0x1674
#define RPG0R 0x1680
#define RPG1R 0x1684
#define RPG6R 0x1698
#define RPG7R 0x169C
#define RPG8R 0x16A0
#define RPG9R 0x16A4

static const struct
{
	int pin;
	int reg;
} output_pin_reg[] = {
	{ OUT_RPD2, RPD2R },
	{ OUT_RPG8, RPG8R },
	{ OUT_RPF4, RPF4R },
	{ OUT_RPD10, RPD10R },
	{ OUT_RPF1, RPF1R },
	{ OUT_RPB9, RPB9R },
	{ OUT_RPB10, RPB10R },
	{ OUT_RPC14, RPC14R },
	{ OUT_RPB5, RPB5R },
	{ OUT_RPC1, RPC1R },
	{ OUT_RPD14, RPD14R },
	{ OUT_RPG1, RPG1R },
	{ OUT_RPA14, RPA14R },
	{ OUT_RPD6, RPD6R },
	{ OUT_RPD3, RPD3R },
	{ OUT_RPG7, RPG7R },
	{ OUT_RPF5, RPF5R },
	{ OUT_RPD11, RPD11R },
	{ OUT_RPF0, RPF0R },
	{ OUT_RPB1, RPB1R },
	{ OUT_RPE5, RPE5R },
	{ OUT_RPC13, RPC13R },
	{ OUT_RPB3, RPB3R },
	{ OUT_RPC4, RPC4R },
	{ OUT_RPD15, RPD15R },
	{ OUT_RPG0, RPG0R },
	{ OUT_RPA15, RPA15R },
	{ OUT_RPD7, RPD7R },
	{ OUT_RPD9, RPD9R },
	{ OUT_RPG6, RPG6R },
	{ OUT_RPB8, RPB8R },
	{ OUT_RPB15, RPB15R },
	{ OUT_RPD4, RPD4R },
	{ OUT_RPB0, RPB0R },
	{ OUT_RPE3, RPE3R },
	{ OUT_RPB7, RPB7R },
	{ OUT_RPF12, RPF12R },
	{ OUT_RPD12, RPD12R },
	{ OUT_RPF8, RPF8R },
	{ OUT_RPC3, RPC3R },
	{ OUT_RPE9, RPE9R },
	{ OUT_RPD1, RPD1R },
	{ OUT_RPG9, RPG9R },
	{ OUT_RPB14, RPB14R },
	{ OUT_RPD0, RPD0R },
	{ OUT_RPB6, RPB6R },
	{ OUT_RPD5, RPD5R },
	{ OUT_RPB2, RPB2R },
	{ OUT_RPF3, RPF3R },
	{ OUT_RPF13, RPF13R },
	{ OUT_RPC2, RPC2R },
	{ OUT_RPE8, RPE8R },
	{ OUT_RPF2, RPF2R },
};

void pic32_pps_output(int function, int pin)
{
	void __iomem *pps_base = ioremap_nocache(PPS_BASE, 0x170);
	int i;

	for (i = 0; i < ARRAY_SIZE(output_pin_reg); i++) {
		if (output_pin_reg[i].pin == pin) {
			__raw_writel(function,
				pps_base + output_pin_reg[i].reg);
			return;
		}
	}

	iounmap(pps_base);
}
