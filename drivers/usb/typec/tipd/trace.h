/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for TI TPS6598x USB Power Delivery controller family
 *
 * Copyright (C) 2020 Purism SPC
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tps6598x

#if !defined(_TPS6598X_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _TPS6598X_TRACE_H_

#include "tps6598x.h"

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#define show_irq_flags(flags) \
	__print_flags_u64(flags, "|", \
		{ TPS_REG_INT_PD_SOFT_RESET,			"PD_SOFT_RESET" }, \
		{ TPS_REG_INT_HARD_RESET,			"HARD_RESET" }, \
		{ TPS_REG_INT_PLUG_EVENT,			"PLUG_EVENT" }, \
		{ TPS_REG_INT_PR_SWAP_COMPLETE,			"PR_SWAP_COMPLETE" }, \
		{ TPS_REG_INT_DR_SWAP_COMPLETE,			"DR_SWAP_COMPLETE" }, \
		{ TPS_REG_INT_RDO_RECEIVED_FROM_SINK,		"RDO_RECEIVED_FROM_SINK" }, \
		{ TPS_REG_INT_BIST,				"BIST" }, \
		{ TPS_REG_INT_OVERCURRENT,			"OVERCURRENT" }, \
		{ TPS_REG_INT_ATTENTION_RECEIVED,		"ATTENTION_RECEIVED" }, \
		{ TPS_REG_INT_VDM_RECEIVED,			"VDM_RECEIVED" }, \
		{ TPS_REG_INT_NEW_CONTRACT_AS_CONSUMER,		"NEW_CONTRACT_AS_CONSUMER" }, \
		{ TPS_REG_INT_NEW_CONTRACT_AS_PROVIDER,		"NEW_CONTRACT_AS_PROVIDER" }, \
		{ TPS_REG_INT_SOURCE_CAP_MESSAGE_READY,		"SOURCE_CAP_MESSAGE_READY" }, \
		{ TPS_REG_INT_SINK_CAP_MESSAGE_READY,		"SINK_CAP_MESSAGE_READY" }, \
		{ TPS_REG_INT_PR_SWAP_REQUESTED,		"PR_SWAP_REQUESTED" }, \
		{ TPS_REG_INT_GOTO_MIN_RECEIVED,		"GOTO_MIN_RECEIVED" }, \
		{ TPS_REG_INT_USB_HOST_PRESENT,			"USB_HOST_PRESENT" }, \
		{ TPS_REG_INT_USB_HOST_PRESENT_NO_LONGER,	"USB_HOST_PRESENT_NO_LONGER" }, \
		{ TPS_REG_INT_HIGH_VOLTAGE_WARNING,		"HIGH_VOLTAGE_WARNING" }, \
		{ TPS_REG_INT_PP_SWITCH_CHANGED,		"PP_SWITCH_CHANGED" }, \
		{ TPS_REG_INT_POWER_STATUS_UPDATE,		"POWER_STATUS_UPDATE" }, \
		{ TPS_REG_INT_DATA_STATUS_UPDATE,		"DATA_STATUS_UPDATE" }, \
		{ TPS_REG_INT_STATUS_UPDATE,			"STATUS_UPDATE" }, \
		{ TPS_REG_INT_PD_STATUS_UPDATE,			"PD_STATUS_UPDATE" }, \
		{ TPS_REG_INT_ADC_LOW_THRESHOLD,		"ADC_LOW_THRESHOLD" }, \
		{ TPS_REG_INT_ADC_HIGH_THRESHOLD,		"ADC_HIGH_THRESHOLD" }, \
		{ TPS_REG_INT_CMD1_COMPLETE,			"CMD1_COMPLETE" }, \
		{ TPS_REG_INT_CMD2_COMPLETE,			"CMD2_COMPLETE" }, \
		{ TPS_REG_INT_ERROR_DEVICE_INCOMPATIBLE,	"ERROR_DEVICE_INCOMPATIBLE" }, \
		{ TPS_REG_INT_ERROR_CANNOT_PROVIDE_PWR,		"ERROR_CANNOT_PROVIDE_PWR" }, \
		{ TPS_REG_INT_ERROR_CAN_PROVIDE_PWR_LATER,	"ERROR_CAN_PROVIDE_PWR_LATER" }, \
		{ TPS_REG_INT_ERROR_POWER_EVENT_OCCURRED,	"ERROR_POWER_EVENT_OCCURRED" }, \
		{ TPS_REG_INT_ERROR_MISSING_GET_CAP_MESSAGE,	"ERROR_MISSING_GET_CAP_MESSAGE" }, \
		{ TPS_REG_INT_ERROR_PROTOCOL_ERROR,		"ERROR_PROTOCOL_ERROR" }, \
		{ TPS_REG_INT_ERROR_MESSAGE_DATA,		"ERROR_MESSAGE_DATA" }, \
		{ TPS_REG_INT_ERROR_DISCHARGE_FAILED,		"ERROR_DISCHARGE_FAILED" }, \
		{ TPS_REG_INT_SRC_TRANSITION,			"SRC_TRANSITION" }, \
		{ TPS_REG_INT_ERROR_UNABLE_TO_SOURCE,		"ERROR_UNABLE_TO_SOURCE" }, \
		{ TPS_REG_INT_VDM_ENTERED_MODE,			"VDM_ENTERED_MODE" }, \
		{ TPS_REG_INT_VDM_MSG_SENT,			"VDM_MSG_SENT" }, \
		{ TPS_REG_INT_DISCOVER_MODES_COMPLETE,		"DISCOVER_MODES_COMPLETE" }, \
		{ TPS_REG_INT_EXIT_MODES_COMPLETE,		"EXIT_MODES_COMPLETE" }, \
		{ TPS_REG_INT_USER_VID_ALT_MODE_ENTERED,	"USER_VID_ALT_MODE_ENTERED" }, \
		{ TPS_REG_INT_USER_VID_ALT_MODE_EXIT,		"USER_VID_ALT_MODE_EXIT" }, \
		{ TPS_REG_INT_USER_VID_ALT_MODE_ATTN_VDM,	"USER_VID_ALT_MODE_ATTN_VDM" }, \
		{ TPS_REG_INT_USER_VID_ALT_MODE_OTHER_VDM,	"USER_VID_ALT_MODE_OTHER_VDM" })

#define TPS6598X_STATUS_FLAGS_MASK (GENMASK(31, 0) ^ (TPS_STATUS_CONN_STATE_MASK | \
						      TPS_STATUS_PP_5V0_SWITCH_MASK | \
						      TPS_STATUS_PP_HV_SWITCH_MASK | \
						      TPS_STATUS_PP_EXT_SWITCH_MASK | \
						      TPS_STATUS_PP_CABLE_SWITCH_MASK | \
						      TPS_STATUS_POWER_SOURCE_MASK | \
						      TPS_STATUS_VBUS_STATUS_MASK | \
						      TPS_STATUS_USB_HOST_PRESENT_MASK | \
						      TPS_STATUS_LEGACY_MASK))

#define show_status_conn_state(status) \
	__print_symbolic(TPS_STATUS_CONN_STATE((status)), \
		{ TPS_STATUS_CONN_STATE_CONN_WITH_R_A,	"conn-Ra"  }, \
		{ TPS_STATUS_CONN_STATE_CONN_NO_R_A,	"conn-no-Ra" }, \
		{ TPS_STATUS_CONN_STATE_NO_CONN_R_A,	"no-conn-Ra" },	\
		{ TPS_STATUS_CONN_STATE_DEBUG_CONN,	"debug"	 }, \
		{ TPS_STATUS_CONN_STATE_AUDIO_CONN,	"audio"	 }, \
		{ TPS_STATUS_CONN_STATE_DISABLED,	"disabled" }, \
		{ TPS_STATUS_CONN_STATE_NO_CONN,	"no-conn" })

#define show_status_pp_switch_state(status) \
	__print_symbolic(status, \
		{ TPS_STATUS_PP_SWITCH_STATE_IN,	"in" }, \
		{ TPS_STATUS_PP_SWITCH_STATE_OUT,	"out" }, \
		{ TPS_STATUS_PP_SWITCH_STATE_FAULT,	"fault" }, \
		{ TPS_STATUS_PP_SWITCH_STATE_DISABLED,	"off" })

#define show_status_power_sources(status) \
	__print_symbolic(TPS_STATUS_POWER_SOURCE(status), \
		{ TPS_STATUS_POWER_SOURCE_VBUS,		"vbus" }, \
		{ TPS_STATUS_POWER_SOURCE_VIN_3P3,	"vin-3p3" }, \
		{ TPS_STATUS_POWER_SOURCE_DEAD_BAT,	"dead-battery" }, \
		{ TPS_STATUS_POWER_SOURCE_UNKNOWN,	"unknown" })

#define show_status_vbus_status(status) \
	__print_symbolic(TPS_STATUS_VBUS_STATUS(status), \
		{ TPS_STATUS_VBUS_STATUS_VSAFE0V,	"vSafe0V" }, \
		{ TPS_STATUS_VBUS_STATUS_VSAFE5V,	"vSafe5V" }, \
		{ TPS_STATUS_VBUS_STATUS_PD,		"pd" }, \
		{ TPS_STATUS_VBUS_STATUS_FAULT,		"fault" })

#define show_status_usb_host_present(status) \
	__print_symbolic(TPS_STATUS_USB_HOST_PRESENT(status), \
		{ TPS_STATUS_USB_HOST_PRESENT_PD_USB,	 "pd-usb" }, \
		{ TPS_STATUS_USB_HOST_PRESENT_NO_PD,	 "no-pd" }, \
		{ TPS_STATUS_USB_HOST_PRESENT_PD_NO_USB, "pd-no-usb" }, \
		{ TPS_STATUS_USB_HOST_PRESENT_NO,	 "no" })

#define show_status_legacy(status) \
	__print_symbolic(TPS_STATUS_LEGACY(status),	     \
		{ TPS_STATUS_LEGACY_SOURCE,		 "source" }, \
		{ TPS_STATUS_LEGACY_SINK,		 "sink" }, \
		{ TPS_STATUS_LEGACY_NO,			 "no" })

#define show_status_flags(flags) \
	__print_flags((flags & TPS6598X_STATUS_FLAGS_MASK), "|", \
		      { TPS_STATUS_PLUG_PRESENT,	"PLUG_PRESENT" }, \
		      { TPS_STATUS_PLUG_UPSIDE_DOWN,	"UPSIDE_DOWN" }, \
		      { TPS_STATUS_PORTROLE,		"PORTROLE" }, \
		      { TPS_STATUS_DATAROLE,		"DATAROLE" }, \
		      { TPS_STATUS_VCONN,		"VCONN" }, \
		      { TPS_STATUS_OVERCURRENT,		"OVERCURRENT" }, \
		      { TPS_STATUS_GOTO_MIN_ACTIVE,	"GOTO_MIN_ACTIVE" }, \
		      { TPS_STATUS_BIST,		"BIST" }, \
		      { TPS_STATUS_HIGH_VOLAGE_WARNING,	"HIGH_VOLAGE_WARNING" }, \
		      { TPS_STATUS_HIGH_LOW_VOLTAGE_WARNING, "HIGH_LOW_VOLTAGE_WARNING" })

#define show_power_status_source_sink(power_status) \
	__print_symbolic(TPS_POWER_STATUS_SOURCESINK(power_status), \
		{ 1, "sink" }, \
		{ 0, "source" })

#define show_power_status_typec_status(power_status) \
	__print_symbolic(TPS_POWER_STATUS_PWROPMODE(power_status), \
		{ TPS_POWER_STATUS_TYPEC_CURRENT_PD,  "pd" }, \
		{ TPS_POWER_STATUS_TYPEC_CURRENT_3A0, "3.0A" }, \
		{ TPS_POWER_STATUS_TYPEC_CURRENT_1A5, "1.5A" }, \
		{ TPS_POWER_STATUS_TYPEC_CURRENT_USB, "usb" })

#define show_power_status_bc12_status(power_status) \
	__print_symbolic(TPS_POWER_STATUS_BC12_STATUS(power_status), \
		{ TPS_POWER_STATUS_BC12_STATUS_DCP, "dcp" }, \
		{ TPS_POWER_STATUS_BC12_STATUS_CDP, "cdp" }, \
		{ TPS_POWER_STATUS_BC12_STATUS_SDP, "sdp" })

#define TPS_DATA_STATUS_FLAGS_MASK (GENMASK(31, 0) ^ (TPS_DATA_STATUS_DP_PIN_ASSIGNMENT_MASK | \
						      TPS_DATA_STATUS_TBT_CABLE_SPEED_MASK | \
						      TPS_DATA_STATUS_TBT_CABLE_GEN_MASK))

#define show_data_status_flags(data_status) \
	__print_flags(data_status & TPS_DATA_STATUS_FLAGS_MASK, "|", \
		{ TPS_DATA_STATUS_DATA_CONNECTION,	"DATA_CONNECTION" }, \
		{ TPS_DATA_STATUS_UPSIDE_DOWN,		"DATA_UPSIDE_DOWN" }, \
		{ TPS_DATA_STATUS_ACTIVE_CABLE,		"ACTIVE_CABLE" }, \
		{ TPS_DATA_STATUS_USB2_CONNECTION,	"USB2_CONNECTION" }, \
		{ TPS_DATA_STATUS_USB3_CONNECTION,	"USB3_CONNECTION" }, \
		{ TPS_DATA_STATUS_USB3_GEN2,		"USB3_GEN2" }, \
		{ TPS_DATA_STATUS_USB_DATA_ROLE,	"USB_DATA_ROLE" }, \
		{ TPS_DATA_STATUS_DP_CONNECTION,	"DP_CONNECTION" }, \
		{ TPS_DATA_STATUS_DP_SINK,		"DP_SINK" }, \
		{ TPS_DATA_STATUS_TBT_CONNECTION,	"TBT_CONNECTION" }, \
		{ TPS_DATA_STATUS_TBT_TYPE,		"TBT_TYPE" }, \
		{ TPS_DATA_STATUS_OPTICAL_CABLE,	"OPTICAL_CABLE" }, \
		{ TPS_DATA_STATUS_ACTIVE_LINK_TRAIN,	"ACTIVE_LINK_TRAIN" }, \
		{ TPS_DATA_STATUS_FORCE_LSX,		"FORCE_LSX" }, \
		{ TPS_DATA_STATUS_POWER_MISMATCH,	"POWER_MISMATCH" })

#define show_data_status_dp_pin_assignment(data_status) \
	__print_symbolic(TPS_DATA_STATUS_DP_SPEC_PIN_ASSIGNMENT(data_status), \
		{ TPS_DATA_STATUS_DP_SPEC_PIN_ASSIGNMENT_E, "E" }, \
		{ TPS_DATA_STATUS_DP_SPEC_PIN_ASSIGNMENT_F, "F" }, \
		{ TPS_DATA_STATUS_DP_SPEC_PIN_ASSIGNMENT_C, "C" }, \
		{ TPS_DATA_STATUS_DP_SPEC_PIN_ASSIGNMENT_D, "D" }, \
		{ TPS_DATA_STATUS_DP_SPEC_PIN_ASSIGNMENT_A, "A" }, \
		{ TPS_DATA_STATUS_DP_SPEC_PIN_ASSIGNMENT_B, "B" })

#define maybe_show_data_status_dp_pin_assignment(data_status) \
	(data_status & TPS_DATA_STATUS_DP_CONNECTION ? \
	 show_data_status_dp_pin_assignment(data_status) : "")

TRACE_EVENT(tps6598x_irq,
	    TP_PROTO(u64 event1,
		     u64 event2),
	    TP_ARGS(event1, event2),

	    TP_STRUCT__entry(
			     __field(u64, event1)
			     __field(u64, event2)
			     ),

	    TP_fast_assign(
			   __entry->event1 = event1;
			   __entry->event2 = event2;
			   ),

	    TP_printk("event1=%s, event2=%s",
		      show_irq_flags(__entry->event1),
		      show_irq_flags(__entry->event2))
);

TRACE_EVENT(tps6598x_status,
	    TP_PROTO(u32 status),
	    TP_ARGS(status),

	    TP_STRUCT__entry(
			     __field(u32, status)
			     ),

	    TP_fast_assign(
			   __entry->status = status;
			   ),

	    TP_printk("conn: %s, pp_5v0: %s, pp_hv: %s, pp_ext: %s, pp_cable: %s, "
		      "pwr-src: %s, vbus: %s, usb-host: %s, legacy: %s, flags: %s",
		      show_status_conn_state(__entry->status),
		      show_status_pp_switch_state(TPS_STATUS_PP_5V0_SWITCH(__entry->status)),
		      show_status_pp_switch_state(TPS_STATUS_PP_HV_SWITCH(__entry->status)),
		      show_status_pp_switch_state(TPS_STATUS_PP_EXT_SWITCH(__entry->status)),
		      show_status_pp_switch_state(TPS_STATUS_PP_CABLE_SWITCH(__entry->status)),
		      show_status_power_sources(__entry->status),
		      show_status_vbus_status(__entry->status),
		      show_status_usb_host_present(__entry->status),
		      show_status_legacy(__entry->status),
		      show_status_flags(__entry->status)
		    )
);

TRACE_EVENT(tps6598x_power_status,
	    TP_PROTO(u16 power_status),
	    TP_ARGS(power_status),

	    TP_STRUCT__entry(
			     __field(u16, power_status)
			     ),

	    TP_fast_assign(
			   __entry->power_status = power_status;
			   ),

	    TP_printk("conn: %d, pwr-role: %s, typec: %s, bc: %s",
		      !!TPS_POWER_STATUS_CONNECTION(__entry->power_status),
		      show_power_status_source_sink(__entry->power_status),
		      show_power_status_typec_status(__entry->power_status),
		      show_power_status_bc12_status(__entry->power_status)
		    )
);

TRACE_EVENT(tps6598x_data_status,
	    TP_PROTO(u32 data_status),
	    TP_ARGS(data_status),

	    TP_STRUCT__entry(
			     __field(u32, data_status)
			     ),

	    TP_fast_assign(
			   __entry->data_status = data_status;
			   ),

	    TP_printk("%s%s%s",
		      show_data_status_flags(__entry->data_status),
		      __entry->data_status & TPS_DATA_STATUS_DP_CONNECTION ? ", DP pinout " : "",
		      maybe_show_data_status_dp_pin_assignment(__entry->data_status)
		    )
);

#endif /* _TPS6598X_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
