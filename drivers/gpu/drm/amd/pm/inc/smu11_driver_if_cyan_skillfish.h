/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef __SMU11_DRIVER_IF_CYAN_SKILLFISH_H__
#define __SMU11_DRIVER_IF_CYAN_SKILLFISH_H__

// *** IMPORTANT ***
// Always increment the interface version if
// any structure is changed in this file
#define MP1_DRIVER_IF_VERSION 0x8

#define TABLE_BIOS_IF            0 // Called by BIOS
#define TABLE_WATERMARKS         1 // Called by Driver; defined here, but not used, for backward compatible
#define TABLE_PMSTATUSLOG        3 // Called by Tools for Agm logging
#define TABLE_DPMCLOCKS          4 // Called by Driver; defined here, but not used, for backward compatible
#define TABLE_MOMENTARY_PM       5 // Called by Tools; defined here, but not used, for backward compatible
#define TABLE_SMU_METRICS        6 // Called by Driver
#define TABLE_COUNT              7

typedef struct SmuMetricsTable_t {
	//CPU status
	uint16_t CoreFrequency[6];              //[MHz]
	uint32_t CorePower[6];                  //[mW]
	uint16_t CoreTemperature[6];            //[centi-Celsius]
	uint16_t L3Frequency[2];                //[MHz]
	uint16_t L3Temperature[2];              //[centi-Celsius]
	uint16_t C0Residency[6];                //Percentage

	// GFX status
	uint16_t GfxclkFrequency;               //[MHz]
	uint16_t GfxTemperature;                //[centi-Celsius]

	// SOC IP info
	uint16_t SocclkFrequency;               //[MHz]
	uint16_t VclkFrequency;                 //[MHz]
	uint16_t DclkFrequency;                 //[MHz]
	uint16_t MemclkFrequency;               //[MHz]

	// power, VF info for CPU/GFX telemetry rails, and then socket power total
	uint32_t Voltage[2];                    //[mV] indices: VDDCR_VDD, VDDCR_GFX
	uint32_t Current[2];                    //[mA] indices: VDDCR_VDD, VDDCR_GFX
	uint32_t Power[2];                      //[mW] indices: VDDCR_VDD, VDDCR_GFX
	uint32_t CurrentSocketPower;            //[mW]

	uint16_t SocTemperature;                //[centi-Celsius]
	uint16_t EdgeTemperature;
	uint16_t ThrottlerStatus;
	uint16_t Spare;

} SmuMetricsTable_t;

typedef struct SmuMetrics_t {
	SmuMetricsTable_t Current;
	SmuMetricsTable_t Average;
	uint32_t SampleStartTime;
	uint32_t SampleStopTime;
	uint32_t Accnt;
} SmuMetrics_t;

#endif
