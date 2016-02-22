/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#ifndef _EVENT_MANAGER_H_
#define _EVENT_MANAGER_H_

#include "power_state.h"
#include "pp_power_source.h"
#include "hardwaremanager.h"
#include "pp_asicblocks.h"

struct pp_eventmgr;
enum amd_pp_event;

enum PEM_EventDataValid {
	PEM_EventDataValid_RequestedStateID = 0,
	PEM_EventDataValid_RequestedUILabel,
	PEM_EventDataValid_NewPowerState,
	PEM_EventDataValid_RequestedPowerSource,
	PEM_EventDataValid_RequestedClocks,
	PEM_EventDataValid_CurrentTemperature,
	PEM_EventDataValid_AsicBlocks,
	PEM_EventDataValid_ODParameters,
	PEM_EventDataValid_PXAdapterPrefs,
	PEM_EventDataValid_PXUserPrefs,
	PEM_EventDataValid_PXSwitchReason,
	PEM_EventDataValid_PXSwitchPhase,
	PEM_EventDataValid_HdVideo,
	PEM_EventDataValid_BacklightLevel,
	PEM_EventDatavalid_VariBrightParams,
	PEM_EventDataValid_VariBrightLevel,
	PEM_EventDataValid_VariBrightImmediateChange,
	PEM_EventDataValid_PercentWhite,
	PEM_EventDataValid_SdVideo,
	PEM_EventDataValid_HTLinkChangeReason,
	PEM_EventDataValid_HWBlocks,
	PEM_EventDataValid_RequestedThermalState,
	PEM_EventDataValid_MvcVideo,
	PEM_EventDataValid_Max
};

typedef enum PEM_EventDataValid PEM_EventDataValid;

/* Number of bits in ULONG variable */
#define PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD (sizeof(unsigned long)*8)

/* Number of ULONG entries used by event data valid bits */
#define PEM_MAX_NUM_EVENTDATAVALID_ULONG_ENTRIES                                 \
		((PEM_EventDataValid_Max + PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD - 1) /  \
		PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD)

static inline void pem_set_event_data_valid(unsigned long *fields, PEM_EventDataValid valid_field)
{
	fields[valid_field / PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD] |=
		(1UL << (valid_field % PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD));
}

static inline void pem_unset_event_data_valid(unsigned long *fields, PEM_EventDataValid valid_field)
{
	fields[valid_field / PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD] &=
		~(1UL << (valid_field % PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD));
}

static inline unsigned long pem_is_event_data_valid(const unsigned long *fields, PEM_EventDataValid valid_field)
{
	return fields[valid_field / PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD] &
		(1UL << (valid_field % PEM_MAX_NUM_EVENTDATAVALID_BITS_PER_FIELD));
}

struct pem_event_data {
	unsigned long	valid_fields[100];
	unsigned long   requested_state_id;
	enum PP_StateUILabel requested_ui_label;
	struct pp_power_state  *pnew_power_state;
	enum pp_power_source  requested_power_source;
	struct PP_Clocks       requested_clocks;
	bool         skip_state_adjust_rules;
	struct phm_asic_blocks  asic_blocks;
	/* to doPP_ThermalState requestedThermalState;
	enum ThermalStateRequestSrc requestThermalStateSrc;
	PP_Temperature  currentTemperature;*/

};

int pem_handle_event(struct pp_eventmgr *eventmgr, enum amd_pp_event event,
		     struct pem_event_data *event_data);

bool pem_is_hw_access_blocked(struct pp_eventmgr *eventmgr);

#endif /* _EVENT_MANAGER_H_ */
