/*
 *  Driver for Microtune MT2060 "Single chip dual conversion broadband tuner"
 *
 *  Copyright (c) 2006 Olivier DANET <odanet@caramail.com>
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

#ifndef MT2060_PRIV_H
#define MT2060_PRIV_H

// Uncomment the #define below to enable spurs checking. The results where quite unconvincing.
// #define MT2060_SPURCHECK

/* This driver is based on the information available in the datasheet of the
   "Comtech SDVBT-3K6M" tuner ( K1000737843.pdf ) which features the MT2060 register map :

   I2C Address : 0x60

   Reg.No |   B7   |   B6   |   B5   |   B4   |   B3   |   B2   |   B1   |   B0   | ( defaults )
   --------------------------------------------------------------------------------
       00 | [              PART             ] | [              REV              ] | R  = 0x63
       01 | [             LNABAND           ] | [              NUM1(5:2)        ] | RW = 0x3F
       02 | [                               DIV1                                ] | RW = 0x74
       03 | FM1CA  | FM1SS  | [  NUM1(1:0)  ] | [              NUM2(3:0)        ] | RW = 0x00
       04 |                                 NUM2(11:4)                          ] | RW = 0x08
       05 | [                               DIV2                       ] |NUM2(12)| RW = 0x93
       06 | L1LK   | [        TAD1          ] | L2LK   | [         TAD2         ] | R
       07 | [                               FMF                                 ] | R
       08 |   ?    | FMCAL  |   ?    |   ?    |   ?    |   ?    |   ?    | TEMP   | R
       09 |   0    |   0    | [    FMGC     ] |   0    | GP02   | GP01   |   0    | RW = 0x20
       0A | ??
       0B |   0    |   0    |   1    |   1    |   0    |   0    | [   VGAG      ] | RW = 0x30
       0C | V1CSE  |   1    |   1    |   1    |   1    |   1    |   1    |   1    | RW = 0xFF
       0D |   1    |   0    | [                      V1CS                       ] | RW = 0xB0
       0E | ??
       0F | ??
       10 | ??
       11 | [             LOTO              ] |   0    |   0    |   1    |   0    | RW = 0x42

       PART    : Part code      : 6 for MT2060
       REV     : Revision code  : 3 for current revision
       LNABAND : Input frequency range : ( See code for details )
       NUM1 / DIV1 / NUM2 / DIV2 : Frequencies programming ( See code for details )
       FM1CA  : Calibration Start Bit
       FM1SS  : Calibration Single Step bit
       L1LK   : LO1 Lock Detect
       TAD1   : Tune Line ADC ( ? )
       L2LK   : LO2 Lock Detect
       TAD2   : Tune Line ADC ( ? )
       FMF    : Estimated first IF Center frequency Offset ( ? )
       FM1CAL : Calibration done bit
       TEMP   : On chip temperature sensor
       FMCG   : Mixer 1 Cap Gain ( ? )
       GP01 / GP02 : Programmable digital outputs. Unconnected pins ?
       V1CSE  : LO1 VCO Automatic Capacitor Select Enable ( ? )
       V1CS   : LO1 Capacitor Selection Value ( ? )
       LOTO   : LO Timeout ( ? )
       VGAG   : Tuner Output gain
*/

#define I2C_ADDRESS 0x60

#define REG_PART_REV   0
#define REG_LO1C1      1
#define REG_LO1C2      2
#define REG_LO2C1      3
#define REG_LO2C2      4
#define REG_LO2C3      5
#define REG_LO_STATUS  6
#define REG_FM_FREQ    7
#define REG_MISC_STAT  8
#define REG_MISC_CTRL  9
#define REG_RESERVED_A 0x0A
#define REG_VGAG       0x0B
#define REG_LO1B1      0x0C
#define REG_LO1B2      0x0D
#define REG_LOTO       0x11

#define PART_REV 0x63 // The current driver works only with PART=6 and REV=3 chips

struct mt2060_priv {
	struct mt2060_config *cfg;
	struct i2c_adapter   *i2c;
	struct i2c_client *client;
	struct mt2060_config config;

	u8 i2c_max_regs;
	u32 frequency;
	u16 if1_freq;
	u8  fmfreq;

	/*
	 * Use REG_MISC_CTRL register for sleep. That drops sleep power usage
	 * about 0.9W (huge!). Register bit meanings are unknown, so let it be
	 * disabled by default to avoid possible regression. Convert driver to
	 * i2c model in order to enable it.
	 */
	bool sleep;
};

#endif
