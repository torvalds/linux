/*
 * enumerations for HopeRf rf69 radio module
 *
 * Copyright (C) 2016 Wolf-Entwicklungen
 *	Marcus Wolf <linux@wolf-entwicklungen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef RF69_ENUM_H
#define RF69_ENUM_H

enum option_on_off {
	OPTION_OFF,
	OPTION_ON
};

enum mode {
	mode_sleep,
	standby,
	synthesizer,
	transmit,
	receive
};

enum dataMode {
	packet,
	continuous,
	continuousNoSync
};

enum modulation {
	OOK,
	FSK
};

enum mod_shaping {
	SHAPING_OFF,
	SHAPING_1_0,
	SHAPING_0_5,
	SHAPING_0_3,
	SHAPING_BR,
	SHAPING_2BR
};

enum paRamp {
	ramp3400,
	ramp2000,
	ramp1000,
	ramp500,
	ramp250,
	ramp125,
	ramp100,
	ramp62,
	ramp50,
	ramp40,
	ramp31,
	ramp25,
	ramp20,
	ramp15,
	ramp12,
	ramp10
};

enum antennaImpedance {
	fiftyOhm,
	twohundretOhm
};

enum lnaGain {
	automatic,
	max,
	maxMinus6,
	maxMinus12,
	maxMinus24,
	maxMinus36,
	maxMinus48,
	undefined
};

enum dccPercent {
	dcc16Percent,
	dcc8Percent,
	dcc4Percent,
	dcc2Percent,
	dcc1Percent,
	dcc0_5Percent,
	dcc0_25Percent,
	dcc0_125Percent
};

enum mantisse {
	mantisse16,
	mantisse20,
	mantisse24
};

enum thresholdType {
	fixed,
	peak,
	average
};

enum thresholdStep {
	step_0_5db,
	step_1_0db,
	step_1_5db,
	step_2_0db,
	step_3_0db,
	step_4_0db,
	step_5_0db,
	step_6_0db
};

enum thresholdDecrement {
	dec_every8th,
	dec_every4th,
	dec_every2nd,
	dec_once,
	dec_twice,
	dec_4times,
	dec_8times,
	dec_16times
};

enum flag {
	modeSwitchCompleted,
	readyToReceive,
	readyToSend,
	pllLocked,
	rssiExceededThreshold,
	timeout,
	automode,
	syncAddressMatch,
	fifoFull,
//	fifoNotEmpty, collision with next enum; replaced by following enum...
	fifoEmpty,
	fifoLevelBelowThreshold,
	fifoOverrun,
	packetSent,
	payloadReady,
	crcOk,
	batteryLow
};

enum fifoFillCondition {
	afterSyncInterrupt,
	always
};

enum packetFormat {
	packetLengthFix,
	packetLengthVar
};

enum txStartCondition {
	fifoLevel,
	fifoNotEmpty
};

enum addressFiltering {
	filteringOff,
	nodeAddress,
	nodeOrBroadcastAddress
};

enum dagc {
	normalMode,
	improve,
	improve4LowModulationIndex
};

#endif
