/*
 * wm8350-regmap.c  --  Wolfson Microelectronics WM8350 register map
 *
 * This file splits out the tables describing the defaults and access
 * status of the WM8350 registers since they are rather large.
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/mfd/wm8350/core.h>

#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_0

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8350_mode0_defaults[] = {
	0x17FF,     /* R0   - Reset/ID */
	0x1000,     /* R1   - ID */
	0x0000,     /* R2 */
	0x1002,     /* R3   - System Control 1 */
	0x0004,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27  - Power Up Interrupt Status */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35  - Power Up Interrupt Status Mask */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3B00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - LOUT1 Volume */
	0x00E4,     /* R105 - ROUT1 Volume */
	0x00E4,     /* R106 - LOUT2 Volume */
	0x02E4,     /* R107 - ROUT2 Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 - AIF Test */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x03FC,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0FFC,     /* R134 - GPIO Configuration (i/o) */
	0x0FFC,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0013,     /* R140 - GPIO Function Select 1 */
	0x0000,     /* R141 - GPIO Function Select 2 */
	0x0000,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x002D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0000,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0000,     /* R186 - DCDC3 Control */
	0x0000,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0000,     /* R189 - DCDC4 Control */
	0x0000,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0000,     /* R195 - DCDC6 Control */
	0x0000,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001C,     /* R200 - LDO1 Control */
	0x0000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x001B,     /* R203 - LDO2 Control */
	0x0000,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001B,     /* R206 - LDO3 Control */
	0x0000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001B,     /* R209 - LDO4 Control */
	0x0000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 */
	0x4000,     /* R220 - RAM BIST 1 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 */
	0x0000,     /* R227 */
	0x0000,     /* R228 */
	0x0000,     /* R229 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 */
	0x0000,     /* R232 */
	0x0000,     /* R233 */
	0x0000,     /* R234 */
	0x0000,     /* R235 */
	0x0000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0000,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0000,     /* R243 */
	0x0000,     /* R244 */
	0x0000,     /* R245 */
	0x0000,     /* R246 */
	0x0000,     /* R247 */
	0x0000,     /* R248 */
	0x0000,     /* R249 */
	0x0000,     /* R250 */
	0x0000,     /* R251 */
	0x0000,     /* R252 */
	0x0000,     /* R253 */
	0x0000,     /* R254 */
	0x0000,     /* R255 */
};
#endif

#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_1

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8350_mode1_defaults[] = {
	0x17FF,     /* R0   - Reset/ID */
	0x1000,     /* R1   - ID */
	0x0000,     /* R2 */
	0x1002,     /* R3   - System Control 1 */
	0x0014,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27  - Power Up Interrupt Status */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35  - Power Up Interrupt Status Mask */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3B00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - LOUT1 Volume */
	0x00E4,     /* R105 - ROUT1 Volume */
	0x00E4,     /* R106 - LOUT2 Volume */
	0x02E4,     /* R107 - ROUT2 Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 - AIF Test */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x03FC,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x00FB,     /* R134 - GPIO Configuration (i/o) */
	0x04FE,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0312,     /* R140 - GPIO Function Select 1 */
	0x1003,     /* R141 - GPIO Function Select 2 */
	0x1331,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x002D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x0062,     /* R180 - DCDC1 Control */
	0x0400,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0026,     /* R186 - DCDC3 Control */
	0x0400,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0062,     /* R189 - DCDC4 Control */
	0x0400,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0026,     /* R195 - DCDC6 Control */
	0x0800,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x0006,     /* R200 - LDO1 Control */
	0x0400,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x0006,     /* R203 - LDO2 Control */
	0x0400,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001B,     /* R206 - LDO3 Control */
	0x0000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001B,     /* R209 - LDO4 Control */
	0x0000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 */
	0x4000,     /* R220 - RAM BIST 1 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 */
	0x0000,     /* R227 */
	0x0000,     /* R228 */
	0x0000,     /* R229 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 */
	0x0000,     /* R232 */
	0x0000,     /* R233 */
	0x0000,     /* R234 */
	0x0000,     /* R235 */
	0x0000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0000,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0000,     /* R243 */
	0x0000,     /* R244 */
	0x0000,     /* R245 */
	0x0000,     /* R246 */
	0x0000,     /* R247 */
	0x0000,     /* R248 */
	0x0000,     /* R249 */
	0x0000,     /* R250 */
	0x0000,     /* R251 */
	0x0000,     /* R252 */
	0x0000,     /* R253 */
	0x0000,     /* R254 */
	0x0000,     /* R255 */
};
#endif

#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_2

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8350_mode2_defaults[] = {
	0x17FF,     /* R0   - Reset/ID */
	0x1000,     /* R1   - ID */
	0x0000,     /* R2 */
	0x1002,     /* R3   - System Control 1 */
	0x0014,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27  - Power Up Interrupt Status */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35  - Power Up Interrupt Status Mask */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3B00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - LOUT1 Volume */
	0x00E4,     /* R105 - ROUT1 Volume */
	0x00E4,     /* R106 - LOUT2 Volume */
	0x02E4,     /* R107 - ROUT2 Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 - AIF Test */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x03FC,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x08FB,     /* R134 - GPIO Configuration (i/o) */
	0x0CFE,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0312,     /* R140 - GPIO Function Select 1 */
	0x0003,     /* R141 - GPIO Function Select 2 */
	0x2331,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x002D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0400,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x002E,     /* R186 - DCDC3 Control */
	0x0800,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x000E,     /* R189 - DCDC4 Control */
	0x0800,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0026,     /* R195 - DCDC6 Control */
	0x0C00,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001A,     /* R200 - LDO1 Control */
	0x0800,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x0010,     /* R203 - LDO2 Control */
	0x0800,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x000A,     /* R206 - LDO3 Control */
	0x0C00,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001A,     /* R209 - LDO4 Control */
	0x0800,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 */
	0x4000,     /* R220 - RAM BIST 1 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 */
	0x0000,     /* R227 */
	0x0000,     /* R228 */
	0x0000,     /* R229 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 */
	0x0000,     /* R232 */
	0x0000,     /* R233 */
	0x0000,     /* R234 */
	0x0000,     /* R235 */
	0x0000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0000,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0000,     /* R243 */
	0x0000,     /* R244 */
	0x0000,     /* R245 */
	0x0000,     /* R246 */
	0x0000,     /* R247 */
	0x0000,     /* R248 */
	0x0000,     /* R249 */
	0x0000,     /* R250 */
	0x0000,     /* R251 */
	0x0000,     /* R252 */
	0x0000,     /* R253 */
	0x0000,     /* R254 */
	0x0000,     /* R255 */
};
#endif

#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_3

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8350_mode3_defaults[] = {
	0x17FF,     /* R0   - Reset/ID */
	0x1000,     /* R1   - ID */
	0x0000,     /* R2 */
	0x1000,     /* R3   - System Control 1 */
	0x0004,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27  - Power Up Interrupt Status */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35  - Power Up Interrupt Status Mask */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3B00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - LOUT1 Volume */
	0x00E4,     /* R105 - ROUT1 Volume */
	0x00E4,     /* R106 - LOUT2 Volume */
	0x02E4,     /* R107 - ROUT2 Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 - AIF Test */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x03FC,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0A7B,     /* R134 - GPIO Configuration (i/o) */
	0x06FE,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x1312,     /* R140 - GPIO Function Select 1 */
	0x1030,     /* R141 - GPIO Function Select 2 */
	0x2231,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x002D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0400,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x000E,     /* R186 - DCDC3 Control */
	0x0400,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0026,     /* R189 - DCDC4 Control */
	0x0400,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0026,     /* R195 - DCDC6 Control */
	0x0400,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001C,     /* R200 - LDO1 Control */
	0x0000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x001C,     /* R203 - LDO2 Control */
	0x0400,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001C,     /* R206 - LDO3 Control */
	0x0400,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001F,     /* R209 - LDO4 Control */
	0x0400,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 */
	0x4000,     /* R220 - RAM BIST 1 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 */
	0x0000,     /* R227 */
	0x0000,     /* R228 */
	0x0000,     /* R229 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 */
	0x0000,     /* R232 */
	0x0000,     /* R233 */
	0x0000,     /* R234 */
	0x0000,     /* R235 */
	0x0000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0000,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0000,     /* R243 */
	0x0000,     /* R244 */
	0x0000,     /* R245 */
	0x0000,     /* R246 */
	0x0000,     /* R247 */
	0x0000,     /* R248 */
	0x0000,     /* R249 */
	0x0000,     /* R250 */
	0x0000,     /* R251 */
	0x0000,     /* R252 */
	0x0000,     /* R253 */
	0x0000,     /* R254 */
	0x0000,     /* R255 */
};
#endif

#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_0

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8351_mode0_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0001,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0004,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x0000,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0FFC,     /* R134 - GPIO Configuration (i/o) */
	0x0FFC,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0013,     /* R140 - GPIO Function Select 1 */
	0x0000,     /* R141 - GPIO Function Select 2 */
	0x0000,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 */
	0x0000,     /* R175 */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0000,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0000,     /* R186 - DCDC3 Control */
	0x0000,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0000,     /* R189 - DCDC4 Control */
	0x0000,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 */
	0x0000,     /* R193 */
	0x0000,     /* R194 */
	0x0000,     /* R195 */
	0x0000,     /* R196 */
	0x0006,     /* R197 */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001C,     /* R200 - LDO1 Control */
	0x0000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x001B,     /* R203 - LDO2 Control */
	0x0000,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001B,     /* R206 - LDO3 Control */
	0x0000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001B,     /* R209 - LDO4 Control */
	0x0000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 - FLL Test 1 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x0000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x1000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
};
#endif

#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_1

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8351_mode1_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0001,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0204,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x0000,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0CFB,     /* R134 - GPIO Configuration (i/o) */
	0x0C1F,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0300,     /* R140 - GPIO Function Select 1 */
	0x1110,     /* R141 - GPIO Function Select 2 */
	0x0013,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 */
	0x0000,     /* R175 */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0C00,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0026,     /* R186 - DCDC3 Control */
	0x0400,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0062,     /* R189 - DCDC4 Control */
	0x0800,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 */
	0x0000,     /* R193 */
	0x0000,     /* R194 */
	0x000A,     /* R195 */
	0x1000,     /* R196 */
	0x0006,     /* R197 */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x0006,     /* R200 - LDO1 Control */
	0x0000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x0010,     /* R203 - LDO2 Control */
	0x0C00,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001F,     /* R206 - LDO3 Control */
	0x0800,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x000A,     /* R209 - LDO4 Control */
	0x0800,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 - FLL Test 1 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x1000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x1000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
};
#endif

#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_2

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8351_mode2_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0001,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0214,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x0110,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x09FA,     /* R134 - GPIO Configuration (i/o) */
	0x0DF6,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x1310,     /* R140 - GPIO Function Select 1 */
	0x0003,     /* R141 - GPIO Function Select 2 */
	0x2000,     /* R142 - GPIO Function Select 3 */
	0x0000,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 */
	0x0000,     /* R175 */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x001A,     /* R180 - DCDC1 Control */
	0x0800,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0056,     /* R186 - DCDC3 Control */
	0x0400,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0026,     /* R189 - DCDC4 Control */
	0x0C00,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 */
	0x0000,     /* R193 */
	0x0000,     /* R194 */
	0x0026,     /* R195 */
	0x0C00,     /* R196 */
	0x0006,     /* R197 */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001C,     /* R200 - LDO1 Control */
	0x0400,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x0010,     /* R203 - LDO2 Control */
	0x0C00,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x0015,     /* R206 - LDO3 Control */
	0x0000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001A,     /* R209 - LDO4 Control */
	0x0000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 - FLL Test 1 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x0000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x1000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
};
#endif

#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_3

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8351_mode3_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0001,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0204,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0010,     /* R129 - GPIO Pin pull up Control */
	0x0000,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0BFB,     /* R134 - GPIO Configuration (i/o) */
	0x0FFD,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0310,     /* R140 - GPIO Function Select 1 */
	0x0001,     /* R141 - GPIO Function Select 2 */
	0x2300,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 */
	0x0000,     /* R175 */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0400,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0026,     /* R186 - DCDC3 Control */
	0x0800,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0062,     /* R189 - DCDC4 Control */
	0x1400,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 */
	0x0000,     /* R193 */
	0x0000,     /* R194 */
	0x0026,     /* R195 */
	0x0400,     /* R196 */
	0x0006,     /* R197 */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x0006,     /* R200 - LDO1 Control */
	0x0C00,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x0016,     /* R203 - LDO2 Control */
	0x0000,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x0019,     /* R206 - LDO3 Control */
	0x0000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001A,     /* R209 - LDO4 Control */
	0x1000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 - FLL Test 1 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x0000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x1000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
};
#endif

#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_0

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8352_mode0_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0002,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0004,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x0000,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0FFC,     /* R134 - GPIO Configuration (i/o) */
	0x0FFC,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0013,     /* R140 - GPIO Function Select 1 */
	0x0000,     /* R141 - GPIO Function Select 2 */
	0x0000,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0000,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0000,     /* R186 - DCDC3 Control */
	0x0000,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0000,     /* R189 - DCDC4 Control */
	0x0000,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0000,     /* R195 - DCDC6 Control */
	0x0000,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001C,     /* R200 - LDO1 Control */
	0x0000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x001B,     /* R203 - LDO2 Control */
	0x0000,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001B,     /* R206 - LDO3 Control */
	0x0000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001B,     /* R209 - LDO4 Control */
	0x0000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x0000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x5000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
	0x5100,     /* R252 */
	0x1000,     /* R253 - DCDC6 Test Controls */
};
#endif

#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_1

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8352_mode1_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0002,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0204,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x0000,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0BFB,     /* R134 - GPIO Configuration (i/o) */
	0x0FFF,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0300,     /* R140 - GPIO Function Select 1 */
	0x0000,     /* R141 - GPIO Function Select 2 */
	0x2300,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x0062,     /* R180 - DCDC1 Control */
	0x0400,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0006,     /* R186 - DCDC3 Control */
	0x0800,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x0006,     /* R189 - DCDC4 Control */
	0x0C00,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0026,     /* R195 - DCDC6 Control */
	0x1000,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x0002,     /* R200 - LDO1 Control */
	0x0000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x001A,     /* R203 - LDO2 Control */
	0x0000,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001F,     /* R206 - LDO3 Control */
	0x0000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001F,     /* R209 - LDO4 Control */
	0x0000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x0000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x5000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
	0x5100,     /* R252 */
	0x1000,     /* R253 - DCDC6 Test Controls */
};
#endif

#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_2

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8352_mode2_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0002,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0204,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0000,     /* R129 - GPIO Pin pull up Control */
	0x0110,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x09DA,     /* R134 - GPIO Configuration (i/o) */
	0x0DD6,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x1310,     /* R140 - GPIO Function Select 1 */
	0x0033,     /* R141 - GPIO Function Select 2 */
	0x2000,     /* R142 - GPIO Function Select 3 */
	0x0000,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x000E,     /* R180 - DCDC1 Control */
	0x0800,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0056,     /* R186 - DCDC3 Control */
	0x1800,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x000E,     /* R189 - DCDC4 Control */
	0x1000,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0026,     /* R195 - DCDC6 Control */
	0x0C00,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001C,     /* R200 - LDO1 Control */
	0x0000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x0006,     /* R203 - LDO2 Control */
	0x0400,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x001C,     /* R206 - LDO3 Control */
	0x1400,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x001A,     /* R209 - LDO4 Control */
	0x0000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x0000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x5000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
	0x5100,     /* R252 */
	0x1000,     /* R253 - DCDC6 Test Controls */
};
#endif

#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_3

#undef WM8350_HAVE_CONFIG_MODE
#define WM8350_HAVE_CONFIG_MODE

const u16 wm8352_mode3_defaults[] = {
	0x6143,     /* R0   - Reset/ID */
	0x0000,     /* R1   - ID */
	0x0002,     /* R2   - Revision */
	0x1C02,     /* R3   - System Control 1 */
	0x0204,     /* R4   - System Control 2 */
	0x0000,     /* R5   - System Hibernate */
	0x8A00,     /* R6   - Interface Control */
	0x0000,     /* R7 */
	0x8000,     /* R8   - Power mgmt (1) */
	0x0000,     /* R9   - Power mgmt (2) */
	0x0000,     /* R10  - Power mgmt (3) */
	0x2000,     /* R11  - Power mgmt (4) */
	0x0E00,     /* R12  - Power mgmt (5) */
	0x0000,     /* R13  - Power mgmt (6) */
	0x0000,     /* R14  - Power mgmt (7) */
	0x0000,     /* R15 */
	0x0000,     /* R16  - RTC Seconds/Minutes */
	0x0100,     /* R17  - RTC Hours/Day */
	0x0101,     /* R18  - RTC Date/Month */
	0x1400,     /* R19  - RTC Year */
	0x0000,     /* R20  - Alarm Seconds/Minutes */
	0x0000,     /* R21  - Alarm Hours/Day */
	0x0000,     /* R22  - Alarm Date/Month */
	0x0320,     /* R23  - RTC Time Control */
	0x0000,     /* R24  - System Interrupts */
	0x0000,     /* R25  - Interrupt Status 1 */
	0x0000,     /* R26  - Interrupt Status 2 */
	0x0000,     /* R27 */
	0x0000,     /* R28  - Under Voltage Interrupt status */
	0x0000,     /* R29  - Over Current Interrupt status */
	0x0000,     /* R30  - GPIO Interrupt Status */
	0x0000,     /* R31  - Comparator Interrupt Status */
	0x3FFF,     /* R32  - System Interrupts Mask */
	0x0000,     /* R33  - Interrupt Status 1 Mask */
	0x0000,     /* R34  - Interrupt Status 2 Mask */
	0x0000,     /* R35 */
	0x0000,     /* R36  - Under Voltage Interrupt status Mask */
	0x0000,     /* R37  - Over Current Interrupt status Mask */
	0x0000,     /* R38  - GPIO Interrupt Status Mask */
	0x0000,     /* R39  - Comparator Interrupt Status Mask */
	0x0040,     /* R40  - Clock Control 1 */
	0x0000,     /* R41  - Clock Control 2 */
	0x3A00,     /* R42  - FLL Control 1 */
	0x7086,     /* R43  - FLL Control 2 */
	0xC226,     /* R44  - FLL Control 3 */
	0x0000,     /* R45  - FLL Control 4 */
	0x0000,     /* R46 */
	0x0000,     /* R47 */
	0x0000,     /* R48  - DAC Control */
	0x0000,     /* R49 */
	0x00C0,     /* R50  - DAC Digital Volume L */
	0x00C0,     /* R51  - DAC Digital Volume R */
	0x0000,     /* R52 */
	0x0040,     /* R53  - DAC LR Rate */
	0x0000,     /* R54  - DAC Clock Control */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x0000,     /* R57 */
	0x4000,     /* R58  - DAC Mute */
	0x0000,     /* R59  - DAC Mute Volume */
	0x0000,     /* R60  - DAC Side */
	0x0000,     /* R61 */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x8000,     /* R64  - ADC Control */
	0x0000,     /* R65 */
	0x00C0,     /* R66  - ADC Digital Volume L */
	0x00C0,     /* R67  - ADC Digital Volume R */
	0x0000,     /* R68  - ADC Divider */
	0x0000,     /* R69 */
	0x0040,     /* R70  - ADC LR Rate */
	0x0000,     /* R71 */
	0x0303,     /* R72  - Input Control */
	0x0000,     /* R73  - IN3 Input Control */
	0x0000,     /* R74  - Mic Bias Control */
	0x0000,     /* R75 */
	0x0000,     /* R76  - Output Control */
	0x0000,     /* R77  - Jack Detect */
	0x0000,     /* R78  - Anti Pop Control */
	0x0000,     /* R79 */
	0x0040,     /* R80  - Left Input Volume */
	0x0040,     /* R81  - Right Input Volume */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0800,     /* R88  - Left Mixer Control */
	0x1000,     /* R89  - Right Mixer Control */
	0x0000,     /* R90 */
	0x0000,     /* R91 */
	0x0000,     /* R92  - OUT3 Mixer Control */
	0x0000,     /* R93  - OUT4 Mixer Control */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96  - Output Left Mixer Volume */
	0x0000,     /* R97  - Output Right Mixer Volume */
	0x0000,     /* R98  - Input Mixer Volume L */
	0x0000,     /* R99  - Input Mixer Volume R */
	0x0000,     /* R100 - Input Mixer Volume */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x00E4,     /* R104 - OUT1L Volume */
	0x00E4,     /* R105 - OUT1R Volume */
	0x00E4,     /* R106 - OUT2L Volume */
	0x02E4,     /* R107 - OUT2R Volume */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 - BEEP Volume */
	0x0A00,     /* R112 - AI Formating */
	0x0000,     /* R113 - ADC DAC COMP */
	0x0020,     /* R114 - AI ADC Control */
	0x0020,     /* R115 - AI DAC Control */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x1FFF,     /* R128 - GPIO Debounce */
	0x0010,     /* R129 - GPIO Pin pull up Control */
	0x0000,     /* R130 - GPIO Pull down Control */
	0x0000,     /* R131 - GPIO Interrupt Mode */
	0x0000,     /* R132 */
	0x0000,     /* R133 - GPIO Control */
	0x0BFB,     /* R134 - GPIO Configuration (i/o) */
	0x0FFD,     /* R135 - GPIO Pin Polarity / Type */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0310,     /* R140 - GPIO Function Select 1 */
	0x0001,     /* R141 - GPIO Function Select 2 */
	0x2300,     /* R142 - GPIO Function Select 3 */
	0x0003,     /* R143 - GPIO Function Select 4 */
	0x0000,     /* R144 - Digitiser Control (1) */
	0x0002,     /* R145 - Digitiser Control (2) */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x7000,     /* R152 - AUX1 Readback */
	0x7000,     /* R153 - AUX2 Readback */
	0x7000,     /* R154 - AUX3 Readback */
	0x7000,     /* R155 - AUX4 Readback */
	0x0000,     /* R156 - USB Voltage Readback */
	0x0000,     /* R157 - LINE Voltage Readback */
	0x0000,     /* R158 - BATT Voltage Readback */
	0x0000,     /* R159 - Chip Temp Readback */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 - Generic Comparator Control */
	0x0000,     /* R164 - Generic comparator 1 */
	0x0000,     /* R165 - Generic comparator 2 */
	0x0000,     /* R166 - Generic comparator 3 */
	0x0000,     /* R167 - Generic comparator 4 */
	0xA00F,     /* R168 - Battery Charger Control 1 */
	0x0B06,     /* R169 - Battery Charger Control 2 */
	0x0000,     /* R170 - Battery Charger Control 3 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Current Sink Driver A */
	0x0000,     /* R173 - CSA Flash control */
	0x0000,     /* R174 - Current Sink Driver B */
	0x0000,     /* R175 - CSB Flash control */
	0x0000,     /* R176 - DCDC/LDO requested */
	0x032D,     /* R177 - DCDC Active options */
	0x0000,     /* R178 - DCDC Sleep options */
	0x0025,     /* R179 - Power-check comparator */
	0x0006,     /* R180 - DCDC1 Control */
	0x0400,     /* R181 - DCDC1 Timeouts */
	0x1006,     /* R182 - DCDC1 Low Power */
	0x0018,     /* R183 - DCDC2 Control */
	0x0000,     /* R184 - DCDC2 Timeouts */
	0x0000,     /* R185 */
	0x0050,     /* R186 - DCDC3 Control */
	0x0C00,     /* R187 - DCDC3 Timeouts */
	0x0006,     /* R188 - DCDC3 Low Power */
	0x000E,     /* R189 - DCDC4 Control */
	0x0400,     /* R190 - DCDC4 Timeouts */
	0x0006,     /* R191 - DCDC4 Low Power */
	0x0008,     /* R192 - DCDC5 Control */
	0x0000,     /* R193 - DCDC5 Timeouts */
	0x0000,     /* R194 */
	0x0029,     /* R195 - DCDC6 Control */
	0x0800,     /* R196 - DCDC6 Timeouts */
	0x0006,     /* R197 - DCDC6 Low Power */
	0x0000,     /* R198 */
	0x0003,     /* R199 - Limit Switch Control */
	0x001D,     /* R200 - LDO1 Control */
	0x1000,     /* R201 - LDO1 Timeouts */
	0x001C,     /* R202 - LDO1 Low Power */
	0x0017,     /* R203 - LDO2 Control */
	0x1000,     /* R204 - LDO2 Timeouts */
	0x001C,     /* R205 - LDO2 Low Power */
	0x0006,     /* R206 - LDO3 Control */
	0x1000,     /* R207 - LDO3 Timeouts */
	0x001C,     /* R208 - LDO3 Low Power */
	0x0010,     /* R209 - LDO4 Control */
	0x1000,     /* R210 - LDO4 Timeouts */
	0x001C,     /* R211 - LDO4 Low Power */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 - VCC_FAULT Masks */
	0x001F,     /* R216 - Main Bandgap Control */
	0x0000,     /* R217 - OSC Control */
	0x9000,     /* R218 - RTC Tick Control */
	0x0000,     /* R219 - Security1 */
	0x4000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 - Signal overrides */
	0x0000,     /* R225 - DCDC/LDO status */
	0x0000,     /* R226 - Charger Overides/status */
	0x0000,     /* R227 - misc overrides */
	0x0000,     /* R228 - Supply overrides/status 1 */
	0x0000,     /* R229 - Supply overrides/status 2 */
	0xE000,     /* R230 - GPIO Pin Status */
	0x0000,     /* R231 - comparotor overrides */
	0x0000,     /* R232 */
	0x0000,     /* R233 - State Machine status */
	0x1200,     /* R234 */
	0x0000,     /* R235 */
	0x8000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0003,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0004,     /* R243 */
	0x0300,     /* R244 */
	0x0000,     /* R245 */
	0x0200,     /* R246 */
	0x0000,     /* R247 */
	0x1000,     /* R248 - DCDC1 Test Controls */
	0x5000,     /* R249 */
	0x1000,     /* R250 - DCDC3 Test Controls */
	0x1000,     /* R251 - DCDC4 Test Controls */
	0x5100,     /* R252 */
	0x1000,     /* R253 - DCDC6 Test Controls */
};
#endif

/*
 * Access masks.
 */

const struct wm8350_reg_access wm8350_reg_io_map[] = {
	/*  read    write volatile */
	{ 0xFFFF, 0xFFFF, 0xFFFF }, /* R0   - Reset/ID */
	{ 0x7CFF, 0x0C00, 0x7FFF }, /* R1   - ID */
	{ 0x007F, 0x0000, 0x0000 }, /* R2   - ROM Mask ID */
	{ 0xBE3B, 0xBE3B, 0x8000 }, /* R3   - System Control 1 */
	{ 0xFEF7, 0xFEF7, 0xF800 }, /* R4   - System Control 2 */
	{ 0x80FF, 0x80FF, 0x8000 }, /* R5   - System Hibernate */
	{ 0xFB0E, 0xFB0E, 0x0000 }, /* R6   - Interface Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R7 */
	{ 0xE537, 0xE537, 0xFFFF }, /* R8   - Power mgmt (1) */
	{ 0x0FF3, 0x0FF3, 0xFFFF }, /* R9   - Power mgmt (2) */
	{ 0x008F, 0x008F, 0xFFFF }, /* R10  - Power mgmt (3) */
	{ 0x6D3C, 0x6D3C, 0xFFFF }, /* R11  - Power mgmt (4) */
	{ 0x1F8F, 0x1F8F, 0xFFFF }, /* R12  - Power mgmt (5) */
	{ 0x8F3F, 0x8F3F, 0xFFFF }, /* R13  - Power mgmt (6) */
	{ 0x0003, 0x0003, 0xFFFF }, /* R14  - Power mgmt (7) */
	{ 0x0000, 0x0000, 0x0000 }, /* R15 */
	{ 0x7F7F, 0x7F7F, 0xFFFF }, /* R16  - RTC Seconds/Minutes */
	{ 0x073F, 0x073F, 0xFFFF }, /* R17  - RTC Hours/Day */
	{ 0x1F3F, 0x1F3F, 0xFFFF }, /* R18  - RTC Date/Month */
	{ 0x3FFF, 0x00FF, 0xFFFF }, /* R19  - RTC Year */
	{ 0x7F7F, 0x7F7F, 0x0000 }, /* R20  - Alarm Seconds/Minutes */
	{ 0x0F3F, 0x0F3F, 0x0000 }, /* R21  - Alarm Hours/Day */
	{ 0x1F3F, 0x1F3F, 0x0000 }, /* R22  - Alarm Date/Month */
	{ 0xEF7F, 0xEA7F, 0xFFFF }, /* R23  - RTC Time Control */
	{ 0x3BFF, 0x0000, 0xFFFF }, /* R24  - System Interrupts */
	{ 0xFEE7, 0x0000, 0xFFFF }, /* R25  - Interrupt Status 1 */
	{ 0x35FF, 0x0000, 0xFFFF }, /* R26  - Interrupt Status 2 */
	{ 0x0F3F, 0x0000, 0xFFFF }, /* R27  - Power Up Interrupt Status */
	{ 0x0F3F, 0x0000, 0xFFFF }, /* R28  - Under Voltage Interrupt status */
	{ 0x8000, 0x0000, 0xFFFF }, /* R29  - Over Current Interrupt status */
	{ 0x1FFF, 0x0000, 0xFFFF }, /* R30  - GPIO Interrupt Status */
	{ 0xEF7F, 0x0000, 0xFFFF }, /* R31  - Comparator Interrupt Status */
	{ 0x3FFF, 0x3FFF, 0x0000 }, /* R32  - System Interrupts Mask */
	{ 0xFEE7, 0xFEE7, 0x0000 }, /* R33  - Interrupt Status 1 Mask */
	{ 0xF5FF, 0xF5FF, 0x0000 }, /* R34  - Interrupt Status 2 Mask */
	{ 0x0F3F, 0x0F3F, 0x0000 }, /* R35  - Power Up Interrupt Status Mask */
	{ 0x0F3F, 0x0F3F, 0x0000 }, /* R36  - Under Voltage Int status Mask */
	{ 0x8000, 0x8000, 0x0000 }, /* R37  - Over Current Int status Mask */
	{ 0x1FFF, 0x1FFF, 0x0000 }, /* R38  - GPIO Interrupt Status Mask */
	{ 0xEF7F, 0xEF7F, 0x0000 }, /* R39  - Comparator IntStatus Mask */
	{ 0xC9F7, 0xC9F7, 0xFFFF }, /* R40  - Clock Control 1 */
	{ 0x8001, 0x8001, 0x0000 }, /* R41  - Clock Control 2 */
	{ 0xFFF7, 0xFFF7, 0xFFFF }, /* R42  - FLL Control 1 */
	{ 0xFBFF, 0xFBFF, 0x0000 }, /* R43  - FLL Control 2 */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R44  - FLL Control 3 */
	{ 0x0033, 0x0033, 0x0000 }, /* R45  - FLL Control 4 */
	{ 0x0000, 0x0000, 0x0000 }, /* R46 */
	{ 0x0000, 0x0000, 0x0000 }, /* R47 */
	{ 0x3033, 0x3033, 0x0000 }, /* R48  - DAC Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R49 */
	{ 0x81FF, 0x81FF, 0xFFFF }, /* R50  - DAC Digital Volume L */
	{ 0x81FF, 0x81FF, 0xFFFF }, /* R51  - DAC Digital Volume R */
	{ 0x0000, 0x0000, 0x0000 }, /* R52 */
	{ 0x0FFF, 0x0FFF, 0xFFFF }, /* R53  - DAC LR Rate */
	{ 0x0017, 0x0017, 0x0000 }, /* R54  - DAC Clock Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R55 */
	{ 0x0000, 0x0000, 0x0000 }, /* R56 */
	{ 0x0000, 0x0000, 0x0000 }, /* R57 */
	{ 0x4000, 0x4000, 0x0000 }, /* R58  - DAC Mute */
	{ 0x7000, 0x7000, 0x0000 }, /* R59  - DAC Mute Volume */
	{ 0x3C00, 0x3C00, 0x0000 }, /* R60  - DAC Side */
	{ 0x0000, 0x0000, 0x0000 }, /* R61 */
	{ 0x0000, 0x0000, 0x0000 }, /* R62 */
	{ 0x0000, 0x0000, 0x0000 }, /* R63 */
	{ 0x8303, 0x8303, 0xFFFF }, /* R64  - ADC Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R65 */
	{ 0x81FF, 0x81FF, 0xFFFF }, /* R66  - ADC Digital Volume L */
	{ 0x81FF, 0x81FF, 0xFFFF }, /* R67  - ADC Digital Volume R */
	{ 0x0FFF, 0x0FFF, 0x0000 }, /* R68  - ADC Divider */
	{ 0x0000, 0x0000, 0x0000 }, /* R69 */
	{ 0x0FFF, 0x0FFF, 0xFFFF }, /* R70  - ADC LR Rate */
	{ 0x0000, 0x0000, 0x0000 }, /* R71 */
	{ 0x0707, 0x0707, 0xFFFF }, /* R72  - Input Control */
	{ 0xC0C0, 0xC0C0, 0xFFFF }, /* R73  - IN3 Input Control */
	{ 0xC09F, 0xC09F, 0xFFFF }, /* R74  - Mic Bias Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R75 */
	{ 0x0F15, 0x0F15, 0xFFFF }, /* R76  - Output Control */
	{ 0xC000, 0xC000, 0xFFFF }, /* R77  - Jack Detect */
	{ 0x03FF, 0x03FF, 0x0000 }, /* R78  - Anti Pop Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R79 */
	{ 0xE1FC, 0xE1FC, 0x8000 }, /* R80  - Left Input Volume */
	{ 0xE1FC, 0xE1FC, 0x8000 }, /* R81  - Right Input Volume */
	{ 0x0000, 0x0000, 0x0000 }, /* R82 */
	{ 0x0000, 0x0000, 0x0000 }, /* R83 */
	{ 0x0000, 0x0000, 0x0000 }, /* R84 */
	{ 0x0000, 0x0000, 0x0000 }, /* R85 */
	{ 0x0000, 0x0000, 0x0000 }, /* R86 */
	{ 0x0000, 0x0000, 0x0000 }, /* R87 */
	{ 0x9807, 0x9807, 0xFFFF }, /* R88  - Left Mixer Control */
	{ 0x980B, 0x980B, 0xFFFF }, /* R89  - Right Mixer Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R90 */
	{ 0x0000, 0x0000, 0x0000 }, /* R91 */
	{ 0x8909, 0x8909, 0xFFFF }, /* R92  - OUT3 Mixer Control */
	{ 0x9E07, 0x9E07, 0xFFFF }, /* R93  - OUT4 Mixer Control */
	{ 0x0000, 0x0000, 0x0000 }, /* R94 */
	{ 0x0000, 0x0000, 0x0000 }, /* R95 */
	{ 0x0EEE, 0x0EEE, 0x0000 }, /* R96  - Output Left Mixer Volume */
	{ 0xE0EE, 0xE0EE, 0x0000 }, /* R97  - Output Right Mixer Volume */
	{ 0x0E0F, 0x0E0F, 0x0000 }, /* R98  - Input Mixer Volume L */
	{ 0xE0E1, 0xE0E1, 0x0000 }, /* R99  - Input Mixer Volume R */
	{ 0x800E, 0x800E, 0x0000 }, /* R100 - Input Mixer Volume */
	{ 0x0000, 0x0000, 0x0000 }, /* R101 */
	{ 0x0000, 0x0000, 0x0000 }, /* R102 */
	{ 0x0000, 0x0000, 0x0000 }, /* R103 */
	{ 0xE1FC, 0xE1FC, 0xFFFF }, /* R104 - LOUT1 Volume */
	{ 0xE1FC, 0xE1FC, 0xFFFF }, /* R105 - ROUT1 Volume */
	{ 0xE1FC, 0xE1FC, 0xFFFF }, /* R106 - LOUT2 Volume */
	{ 0xE7FC, 0xE7FC, 0xFFFF }, /* R107 - ROUT2 Volume */
	{ 0x0000, 0x0000, 0x0000 }, /* R108 */
	{ 0x0000, 0x0000, 0x0000 }, /* R109 */
	{ 0x0000, 0x0000, 0x0000 }, /* R110 */
	{ 0x80E0, 0x80E0, 0xFFFF }, /* R111 - BEEP Volume */
	{ 0xBF00, 0xBF00, 0x0000 }, /* R112 - AI Formating */
	{ 0x00F1, 0x00F1, 0x0000 }, /* R113 - ADC DAC COMP */
	{ 0x00F8, 0x00F8, 0x0000 }, /* R114 - AI ADC Control */
	{ 0x40FB, 0x40FB, 0x0000 }, /* R115 - AI DAC Control */
	{ 0x7C30, 0x7C30, 0x0000 }, /* R116 - AIF Test */
	{ 0x0000, 0x0000, 0x0000 }, /* R117 */
	{ 0x0000, 0x0000, 0x0000 }, /* R118 */
	{ 0x0000, 0x0000, 0x0000 }, /* R119 */
	{ 0x0000, 0x0000, 0x0000 }, /* R120 */
	{ 0x0000, 0x0000, 0x0000 }, /* R121 */
	{ 0x0000, 0x0000, 0x0000 }, /* R122 */
	{ 0x0000, 0x0000, 0x0000 }, /* R123 */
	{ 0x0000, 0x0000, 0x0000 }, /* R124 */
	{ 0x0000, 0x0000, 0x0000 }, /* R125 */
	{ 0x0000, 0x0000, 0x0000 }, /* R126 */
	{ 0x0000, 0x0000, 0x0000 }, /* R127 */
	{ 0x1FFF, 0x1FFF, 0x0000 }, /* R128 - GPIO Debounce */
	{ 0x1FFF, 0x1FFF, 0x0000 }, /* R129 - GPIO Pin pull up Control */
	{ 0x1FFF, 0x1FFF, 0x0000 }, /* R130 - GPIO Pull down Control */
	{ 0x1FFF, 0x1FFF, 0x0000 }, /* R131 - GPIO Interrupt Mode */
	{ 0x0000, 0x0000, 0x0000 }, /* R132 */
	{ 0x00C0, 0x00C0, 0x0000 }, /* R133 - GPIO Control */
	{ 0x1FFF, 0x1FFF, 0x0000 }, /* R134 - GPIO Configuration (i/o) */
	{ 0x1FFF, 0x1FFF, 0x0000 }, /* R135 - GPIO Pin Polarity / Type */
	{ 0x0000, 0x0000, 0x0000 }, /* R136 */
	{ 0x0000, 0x0000, 0x0000 }, /* R137 */
	{ 0x0000, 0x0000, 0x0000 }, /* R138 */
	{ 0x0000, 0x0000, 0x0000 }, /* R139 */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R140 - GPIO Function Select 1 */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R141 - GPIO Function Select 2 */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R142 - GPIO Function Select 3 */
	{ 0x000F, 0x000F, 0x0000 }, /* R143 - GPIO Function Select 4 */
	{ 0xF0FF, 0xF0FF, 0xA000 }, /* R144 - Digitiser Control (1) */
	{ 0x3707, 0x3707, 0x0000 }, /* R145 - Digitiser Control (2) */
	{ 0x0000, 0x0000, 0x0000 }, /* R146 */
	{ 0x0000, 0x0000, 0x0000 }, /* R147 */
	{ 0x0000, 0x0000, 0x0000 }, /* R148 */
	{ 0x0000, 0x0000, 0x0000 }, /* R149 */
	{ 0x0000, 0x0000, 0x0000 }, /* R150 */
	{ 0x0000, 0x0000, 0x0000 }, /* R151 */
	{ 0x7FFF, 0x7000, 0xFFFF }, /* R152 - AUX1 Readback */
	{ 0x7FFF, 0x7000, 0xFFFF }, /* R153 - AUX2 Readback */
	{ 0x7FFF, 0x7000, 0xFFFF }, /* R154 - AUX3 Readback */
	{ 0x7FFF, 0x7000, 0xFFFF }, /* R155 - AUX4 Readback */
	{ 0x0FFF, 0x0000, 0xFFFF }, /* R156 - USB Voltage Readback */
	{ 0x0FFF, 0x0000, 0xFFFF }, /* R157 - LINE Voltage Readback */
	{ 0x0FFF, 0x0000, 0xFFFF }, /* R158 - BATT Voltage Readback */
	{ 0x0FFF, 0x0000, 0xFFFF }, /* R159 - Chip Temp Readback */
	{ 0x0000, 0x0000, 0x0000 }, /* R160 */
	{ 0x0000, 0x0000, 0x0000 }, /* R161 */
	{ 0x0000, 0x0000, 0x0000 }, /* R162 */
	{ 0x000F, 0x000F, 0x0000 }, /* R163 - Generic Comparator Control */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R164 - Generic comparator 1 */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R165 - Generic comparator 2 */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R166 - Generic comparator 3 */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R167 - Generic comparator 4 */
	{ 0xBFFF, 0xBFFF, 0x8000 }, /* R168 - Battery Charger Control 1 */
	{ 0xFFFF, 0x4FFF, 0xB000 }, /* R169 - Battery Charger Control 2 */
	{ 0x007F, 0x007F, 0x0000 }, /* R170 - Battery Charger Control 3 */
	{ 0x0000, 0x0000, 0x0000 }, /* R171 */
	{ 0x903F, 0x903F, 0xFFFF }, /* R172 - Current Sink Driver A */
	{ 0xE333, 0xE333, 0xFFFF }, /* R173 - CSA Flash control */
	{ 0x903F, 0x903F, 0xFFFF }, /* R174 - Current Sink Driver B */
	{ 0xE333, 0xE333, 0xFFFF }, /* R175 - CSB Flash control */
	{ 0x8F3F, 0x8F3F, 0xFFFF }, /* R176 - DCDC/LDO requested */
	{ 0x332D, 0x332D, 0x0000 }, /* R177 - DCDC Active options */
	{ 0x002D, 0x002D, 0x0000 }, /* R178 - DCDC Sleep options */
	{ 0x5177, 0x5177, 0x8000 }, /* R179 - Power-check comparator */
	{ 0x047F, 0x047F, 0x0000 }, /* R180 - DCDC1 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R181 - DCDC1 Timeouts */
	{ 0x737F, 0x737F, 0x0000 }, /* R182 - DCDC1 Low Power */
	{ 0x535B, 0x535B, 0x0000 }, /* R183 - DCDC2 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R184 - DCDC2 Timeouts */
	{ 0x0000, 0x0000, 0x0000 }, /* R185 */
	{ 0x047F, 0x047F, 0x0000 }, /* R186 - DCDC3 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R187 - DCDC3 Timeouts */
	{ 0x737F, 0x737F, 0x0000 }, /* R188 - DCDC3 Low Power */
	{ 0x047F, 0x047F, 0x0000 }, /* R189 - DCDC4 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R190 - DCDC4 Timeouts */
	{ 0x737F, 0x737F, 0x0000 }, /* R191 - DCDC4 Low Power */
	{ 0x535B, 0x535B, 0x0000 }, /* R192 - DCDC5 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R193 - DCDC5 Timeouts */
	{ 0x0000, 0x0000, 0x0000 }, /* R194 */
	{ 0x047F, 0x047F, 0x0000 }, /* R195 - DCDC6 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R196 - DCDC6 Timeouts */
	{ 0x737F, 0x737F, 0x0000 }, /* R197 - DCDC6 Low Power */
	{ 0x0000, 0x0000, 0x0000 }, /* R198 */
	{ 0xFFD3, 0xFFD3, 0x0000 }, /* R199 - Limit Switch Control */
	{ 0x441F, 0x441F, 0x0000 }, /* R200 - LDO1 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R201 - LDO1 Timeouts */
	{ 0x331F, 0x331F, 0x0000 }, /* R202 - LDO1 Low Power */
	{ 0x441F, 0x441F, 0x0000 }, /* R203 - LDO2 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R204 - LDO2 Timeouts */
	{ 0x331F, 0x331F, 0x0000 }, /* R205 - LDO2 Low Power */
	{ 0x441F, 0x441F, 0x0000 }, /* R206 - LDO3 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R207 - LDO3 Timeouts */
	{ 0x331F, 0x331F, 0x0000 }, /* R208 - LDO3 Low Power */
	{ 0x441F, 0x441F, 0x0000 }, /* R209 - LDO4 Control */
	{ 0xFFC0, 0xFFC0, 0x0000 }, /* R210 - LDO4 Timeouts */
	{ 0x331F, 0x331F, 0x0000 }, /* R211 - LDO4 Low Power */
	{ 0x0000, 0x0000, 0x0000 }, /* R212 */
	{ 0x0000, 0x0000, 0x0000 }, /* R213 */
	{ 0x0000, 0x0000, 0x0000 }, /* R214 */
	{ 0x8F3F, 0x8F3F, 0x0000 }, /* R215 - VCC_FAULT Masks */
	{ 0xFF3F, 0xE03F, 0x0000 }, /* R216 - Main Bandgap Control */
	{ 0xEF2F, 0xE02F, 0x0000 }, /* R217 - OSC Control */
	{ 0xF3FF, 0xB3FF, 0xc000 }, /* R218 - RTC Tick Control */
	{ 0xFFFF, 0xFFFF, 0x0000 }, /* R219 - Security */
	{ 0x09FF, 0x01FF, 0x0000 }, /* R220 - RAM BIST 1 */
	{ 0x0000, 0x0000, 0x0000 }, /* R221 */
	{ 0xFFFF, 0xFFFF, 0xFFFF }, /* R222 */
	{ 0xFFFF, 0xFFFF, 0xFFFF }, /* R223 */
	{ 0x0000, 0x0000, 0x0000 }, /* R224 */
	{ 0x8F3F, 0x0000, 0xFFFF }, /* R225 - DCDC/LDO status */
	{ 0x0000, 0x0000, 0xFFFF }, /* R226 - Charger status */
	{ 0x34FE, 0x0000, 0xFFFF }, /* R227 */
	{ 0x0000, 0x0000, 0x0000 }, /* R228 */
	{ 0x0000, 0x0000, 0x0000 }, /* R229 */
	{ 0xFFFF, 0x1FFF, 0xFFFF }, /* R230 - GPIO Pin Status */
	{ 0xFFFF, 0x1FFF, 0xFFFF }, /* R231 */
	{ 0xFFFF, 0x1FFF, 0xFFFF }, /* R232 */
	{ 0xFFFF, 0x1FFF, 0xFFFF }, /* R233 */
	{ 0x0000, 0x0000, 0x0000 }, /* R234 */
	{ 0x0000, 0x0000, 0x0000 }, /* R235 */
	{ 0x0000, 0x0000, 0x0000 }, /* R236 */
	{ 0x0000, 0x0000, 0x0000 }, /* R237 */
	{ 0x0000, 0x0000, 0x0000 }, /* R238 */
	{ 0x0000, 0x0000, 0x0000 }, /* R239 */
	{ 0x0000, 0x0000, 0x0000 }, /* R240 */
	{ 0x0000, 0x0000, 0x0000 }, /* R241 */
	{ 0x0000, 0x0000, 0x0000 }, /* R242 */
	{ 0x0000, 0x0000, 0x0000 }, /* R243 */
	{ 0x0000, 0x0000, 0x0000 }, /* R244 */
	{ 0x0000, 0x0000, 0x0000 }, /* R245 */
	{ 0x0000, 0x0000, 0x0000 }, /* R246 */
	{ 0x0000, 0x0000, 0x0000 }, /* R247 */
	{ 0xFFFF, 0x0010, 0xFFFF }, /* R248 */
	{ 0x0000, 0x0000, 0x0000 }, /* R249 */
	{ 0xFFFF, 0x0010, 0xFFFF }, /* R250 */
	{ 0xFFFF, 0x0010, 0xFFFF }, /* R251 */
	{ 0x0000, 0x0000, 0x0000 }, /* R252 */
	{ 0xFFFF, 0x0010, 0xFFFF }, /* R253 */
	{ 0x0000, 0x0000, 0x0000 }, /* R254 */
	{ 0x0000, 0x0000, 0x0000 }, /* R255 */
};

static bool wm8350_readable(struct device *dev, unsigned int reg)
{
	return wm8350_reg_io_map[reg].readable;
}

static bool wm8350_writeable(struct device *dev, unsigned int reg)
{
	struct wm8350 *wm8350 = dev_get_drvdata(dev);

	if (!wm8350->unlocked) {
		if ((reg >= WM8350_GPIO_FUNCTION_SELECT_1 &&
		     reg <= WM8350_GPIO_FUNCTION_SELECT_4) ||
		    (reg >= WM8350_BATTERY_CHARGER_CONTROL_1 &&
		     reg <= WM8350_BATTERY_CHARGER_CONTROL_3))
			return false;
	}

	return wm8350_reg_io_map[reg].writable;
}

static bool wm8350_volatile(struct device *dev, unsigned int reg)
{
	return wm8350_reg_io_map[reg].vol;
}

static bool wm8350_precious(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8350_SYSTEM_INTERRUPTS:
	case WM8350_INT_STATUS_1:
	case WM8350_INT_STATUS_2:
	case WM8350_POWER_UP_INT_STATUS:
	case WM8350_UNDER_VOLTAGE_INT_STATUS:
	case WM8350_OVER_CURRENT_INT_STATUS:
	case WM8350_GPIO_INT_STATUS:
	case WM8350_COMPARATOR_INT_STATUS:
		return true;

	default:
		return false;
	}
}

const struct regmap_config wm8350_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.cache_type = REGCACHE_RBTREE,

	.max_register = WM8350_MAX_REGISTER,
	.readable_reg = wm8350_readable,
	.writeable_reg = wm8350_writeable,
	.volatile_reg = wm8350_volatile,
	.precious_reg = wm8350_precious,
};
