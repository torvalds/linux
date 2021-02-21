/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence CDNSP DRD Driver.
 *
 * Copyright (C) 2020 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *
 */
#ifndef __LINUX_CDNSP_DEBUG
#define __LINUX_CDNSP_DEBUG

static inline const char *cdnsp_trb_comp_code_string(u8 status)
{
	switch (status) {
	case COMP_INVALID:
		return "Invalid";
	case COMP_SUCCESS:
		return "Success";
	case COMP_DATA_BUFFER_ERROR:
		return "Data Buffer Error";
	case COMP_BABBLE_DETECTED_ERROR:
		return "Babble Detected";
	case COMP_TRB_ERROR:
		return "TRB Error";
	case COMP_RESOURCE_ERROR:
		return "Resource Error";
	case COMP_NO_SLOTS_AVAILABLE_ERROR:
		return "No Slots Available Error";
	case COMP_INVALID_STREAM_TYPE_ERROR:
		return "Invalid Stream Type Error";
	case COMP_SLOT_NOT_ENABLED_ERROR:
		return "Slot Not Enabled Error";
	case COMP_ENDPOINT_NOT_ENABLED_ERROR:
		return "Endpoint Not Enabled Error";
	case COMP_SHORT_PACKET:
		return "Short Packet";
	case COMP_RING_UNDERRUN:
		return "Ring Underrun";
	case COMP_RING_OVERRUN:
		return "Ring Overrun";
	case COMP_VF_EVENT_RING_FULL_ERROR:
		return "VF Event Ring Full Error";
	case COMP_PARAMETER_ERROR:
		return "Parameter Error";
	case COMP_CONTEXT_STATE_ERROR:
		return "Context State Error";
	case COMP_EVENT_RING_FULL_ERROR:
		return "Event Ring Full Error";
	case COMP_INCOMPATIBLE_DEVICE_ERROR:
		return "Incompatible Device Error";
	case COMP_MISSED_SERVICE_ERROR:
		return "Missed Service Error";
	case COMP_COMMAND_RING_STOPPED:
		return "Command Ring Stopped";
	case COMP_COMMAND_ABORTED:
		return "Command Aborted";
	case COMP_STOPPED:
		return "Stopped";
	case COMP_STOPPED_LENGTH_INVALID:
		return "Stopped - Length Invalid";
	case COMP_STOPPED_SHORT_PACKET:
		return "Stopped - Short Packet";
	case COMP_MAX_EXIT_LATENCY_TOO_LARGE_ERROR:
		return "Max Exit Latency Too Large Error";
	case COMP_ISOCH_BUFFER_OVERRUN:
		return "Isoch Buffer Overrun";
	case COMP_EVENT_LOST_ERROR:
		return "Event Lost Error";
	case COMP_UNDEFINED_ERROR:
		return "Undefined Error";
	case COMP_INVALID_STREAM_ID_ERROR:
		return "Invalid Stream ID Error";
	default:
		return "Unknown!!";
	}
}

static inline const char *cdnsp_trb_type_string(u8 type)
{
	switch (type) {
	case TRB_NORMAL:
		return "Normal";
	case TRB_SETUP:
		return "Setup Stage";
	case TRB_DATA:
		return "Data Stage";
	case TRB_STATUS:
		return "Status Stage";
	case TRB_ISOC:
		return "Isoch";
	case TRB_LINK:
		return "Link";
	case TRB_EVENT_DATA:
		return "Event Data";
	case TRB_TR_NOOP:
		return "No-Op";
	case TRB_ENABLE_SLOT:
		return "Enable Slot Command";
	case TRB_DISABLE_SLOT:
		return "Disable Slot Command";
	case TRB_ADDR_DEV:
		return "Address Device Command";
	case TRB_CONFIG_EP:
		return "Configure Endpoint Command";
	case TRB_EVAL_CONTEXT:
		return "Evaluate Context Command";
	case TRB_RESET_EP:
		return "Reset Endpoint Command";
	case TRB_STOP_RING:
		return "Stop Ring Command";
	case TRB_SET_DEQ:
		return "Set TR Dequeue Pointer Command";
	case TRB_RESET_DEV:
		return "Reset Device Command";
	case TRB_FORCE_HEADER:
		return "Force Header Command";
	case TRB_CMD_NOOP:
		return "No-Op Command";
	case TRB_TRANSFER:
		return "Transfer Event";
	case TRB_COMPLETION:
		return "Command Completion Event";
	case TRB_PORT_STATUS:
		return "Port Status Change Event";
	case TRB_HC_EVENT:
		return "Device Controller Event";
	case TRB_MFINDEX_WRAP:
		return "MFINDEX Wrap Event";
	case TRB_ENDPOINT_NRDY:
		return "Endpoint Not ready";
	case TRB_HALT_ENDPOINT:
		return "Halt Endpoint";
	case TRB_FLUSH_ENDPOINT:
		return "FLush Endpoint";
	default:
		return "UNKNOWN";
	}
}

static inline const char *cdnsp_ring_type_string(enum cdnsp_ring_type type)
{
	switch (type) {
	case TYPE_CTRL:
		return "CTRL";
	case TYPE_ISOC:
		return "ISOC";
	case TYPE_BULK:
		return "BULK";
	case TYPE_INTR:
		return "INTR";
	case TYPE_STREAM:
		return "STREAM";
	case TYPE_COMMAND:
		return "CMD";
	case TYPE_EVENT:
		return "EVENT";
	}

	return "UNKNOWN";
}

static inline char *cdnsp_slot_state_string(u32 state)
{
	switch (state) {
	case SLOT_STATE_ENABLED:
		return "enabled/disabled";
	case SLOT_STATE_DEFAULT:
		return "default";
	case SLOT_STATE_ADDRESSED:
		return "addressed";
	case SLOT_STATE_CONFIGURED:
		return "configured";
	default:
		return "reserved";
	}
}

static inline const char *cdnsp_decode_trb(char *str, size_t size, u32 field0,
					   u32 field1, u32 field2, u32 field3)
{
	int ep_id = TRB_TO_EP_INDEX(field3) - 1;
	int type = TRB_FIELD_TO_TYPE(field3);
	unsigned int ep_num;
	int ret = 0;
	u32 temp;

	ep_num = DIV_ROUND_UP(ep_id, 2);

	switch (type) {
	case TRB_LINK:
		ret += snprintf(str, size,
				"LINK %08x%08x intr %ld type '%s' flags %c:%c:%c:%c",
				field1, field0, GET_INTR_TARGET(field2),
				cdnsp_trb_type_string(type),
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CHAIN ? 'C' : 'c',
				field3 & TRB_TC ? 'T' : 't',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_TRANSFER:
	case TRB_COMPLETION:
	case TRB_PORT_STATUS:
	case TRB_HC_EVENT:
		ret += snprintf(str, size,
				"ep%d%s(%d) type '%s' TRB %08x%08x status '%s'"
				" len %ld slot %ld flags %c:%c",
				ep_num, ep_id % 2 ? "out" : "in",
				TRB_TO_EP_INDEX(field3),
				cdnsp_trb_type_string(type), field1, field0,
				cdnsp_trb_comp_code_string(GET_COMP_CODE(field2)),
				EVENT_TRB_LEN(field2), TRB_TO_SLOT_ID(field3),
				field3 & EVENT_DATA ? 'E' : 'e',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_MFINDEX_WRAP:
		ret += snprintf(str, size, "%s: flags %c",
				cdnsp_trb_type_string(type),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_SETUP:
		ret += snprintf(str, size,
				"type '%s' bRequestType %02x bRequest %02x "
				"wValue %02x%02x wIndex %02x%02x wLength %d "
				"length %ld TD size %ld intr %ld Setup ID %ld "
				"flags %c:%c:%c",
				cdnsp_trb_type_string(type),
				field0 & 0xff,
				(field0 & 0xff00) >> 8,
				(field0 & 0xff000000) >> 24,
				(field0 & 0xff0000) >> 16,
				(field1 & 0xff00) >> 8,
				field1 & 0xff,
				(field1 & 0xff000000) >> 16 |
				(field1 & 0xff0000) >> 16,
				TRB_LEN(field2), GET_TD_SIZE(field2),
				GET_INTR_TARGET(field2),
				TRB_SETUPID_TO_TYPE(field3),
				field3 & TRB_IDT ? 'D' : 'd',
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_DATA:
		ret += snprintf(str, size,
				"type '%s' Buffer %08x%08x length %ld TD size %ld "
				"intr %ld flags %c:%c:%c:%c:%c:%c:%c",
				cdnsp_trb_type_string(type),
				field1, field0, TRB_LEN(field2),
				GET_TD_SIZE(field2),
				GET_INTR_TARGET(field2),
				field3 & TRB_IDT ? 'D' : 'i',
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CHAIN ? 'C' : 'c',
				field3 & TRB_NO_SNOOP ? 'S' : 's',
				field3 & TRB_ISP ? 'I' : 'i',
				field3 & TRB_ENT ? 'E' : 'e',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_STATUS:
		ret += snprintf(str, size,
				"Buffer %08x%08x length %ld TD size %ld intr"
				"%ld type '%s' flags %c:%c:%c:%c",
				field1, field0, TRB_LEN(field2),
				GET_TD_SIZE(field2),
				GET_INTR_TARGET(field2),
				cdnsp_trb_type_string(type),
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CHAIN ? 'C' : 'c',
				field3 & TRB_ENT ? 'E' : 'e',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_NORMAL:
	case TRB_ISOC:
	case TRB_EVENT_DATA:
	case TRB_TR_NOOP:
		ret += snprintf(str, size,
				"type '%s' Buffer %08x%08x length %ld "
				"TD size %ld intr %ld "
				"flags %c:%c:%c:%c:%c:%c:%c:%c:%c",
				cdnsp_trb_type_string(type),
				field1, field0, TRB_LEN(field2),
				GET_TD_SIZE(field2),
				GET_INTR_TARGET(field2),
				field3 & TRB_BEI ? 'B' : 'b',
				field3 & TRB_IDT ? 'T' : 't',
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CHAIN ? 'C' : 'c',
				field3 & TRB_NO_SNOOP ? 'S' : 's',
				field3 & TRB_ISP ? 'I' : 'i',
				field3 & TRB_ENT ? 'E' : 'e',
				field3 & TRB_CYCLE ? 'C' : 'c',
				!(field3 & TRB_EVENT_INVALIDATE) ? 'V' : 'v');
		break;
	case TRB_CMD_NOOP:
	case TRB_ENABLE_SLOT:
		ret += snprintf(str, size, "%s: flags %c",
				cdnsp_trb_type_string(type),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_DISABLE_SLOT:
		ret += snprintf(str, size, "%s: slot %ld flags %c",
				cdnsp_trb_type_string(type),
				TRB_TO_SLOT_ID(field3),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_ADDR_DEV:
		ret += snprintf(str, size,
				"%s: ctx %08x%08x slot %ld flags %c:%c",
				cdnsp_trb_type_string(type), field1, field0,
				TRB_TO_SLOT_ID(field3),
				field3 & TRB_BSR ? 'B' : 'b',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_CONFIG_EP:
		ret += snprintf(str, size,
				"%s: ctx %08x%08x slot %ld flags %c:%c",
				cdnsp_trb_type_string(type), field1, field0,
				TRB_TO_SLOT_ID(field3),
				field3 & TRB_DC ? 'D' : 'd',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_EVAL_CONTEXT:
		ret += snprintf(str, size,
				"%s: ctx %08x%08x slot %ld flags %c",
				cdnsp_trb_type_string(type), field1, field0,
				TRB_TO_SLOT_ID(field3),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_RESET_EP:
	case TRB_HALT_ENDPOINT:
	case TRB_FLUSH_ENDPOINT:
		ret += snprintf(str, size,
				"%s: ep%d%s(%d) ctx %08x%08x slot %ld flags %c",
				cdnsp_trb_type_string(type),
				ep_num, ep_id % 2 ? "out" : "in",
				TRB_TO_EP_INDEX(field3), field1, field0,
				TRB_TO_SLOT_ID(field3),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_STOP_RING:
		ret += snprintf(str, size,
				"%s: ep%d%s(%d) slot %ld sp %d flags %c",
				cdnsp_trb_type_string(type),
				ep_num, ep_id % 2 ? "out" : "in",
				TRB_TO_EP_INDEX(field3),
				TRB_TO_SLOT_ID(field3),
				TRB_TO_SUSPEND_PORT(field3),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_SET_DEQ:
		ret += snprintf(str, size,
				"%s: ep%d%s(%d) deq %08x%08x stream %ld slot %ld  flags %c",
				cdnsp_trb_type_string(type),
				ep_num, ep_id % 2 ? "out" : "in",
				TRB_TO_EP_INDEX(field3), field1, field0,
				TRB_TO_STREAM_ID(field2),
				TRB_TO_SLOT_ID(field3),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_RESET_DEV:
		ret += snprintf(str, size, "%s: slot %ld flags %c",
				cdnsp_trb_type_string(type),
				TRB_TO_SLOT_ID(field3),
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_ENDPOINT_NRDY:
		temp  = TRB_TO_HOST_STREAM(field2);

		ret += snprintf(str, size,
				"%s: ep%d%s(%d) H_SID %x%s%s D_SID %lx flags %c:%c",
				cdnsp_trb_type_string(type),
				ep_num, ep_id % 2 ? "out" : "in",
				TRB_TO_EP_INDEX(field3), temp,
				temp == STREAM_PRIME_ACK ? "(PRIME)" : "",
				temp == STREAM_REJECTED ? "(REJECTED)" : "",
				TRB_TO_DEV_STREAM(field0),
				field3 & TRB_STAT ? 'S' : 's',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	default:
		ret += snprintf(str, size,
				"type '%s' -> raw %08x %08x %08x %08x",
				cdnsp_trb_type_string(type),
				field0, field1, field2, field3);
	}

	return str;
}

static inline const char *cdnsp_decode_slot_context(u32 info, u32 info2,
						    u32 int_target, u32 state)
{
	static char str[1024];
	int ret = 0;
	u32 speed;
	char *s;

	speed = info & DEV_SPEED;

	switch (speed) {
	case SLOT_SPEED_FS:
		s = "full-speed";
		break;
	case SLOT_SPEED_HS:
		s = "high-speed";
		break;
	case SLOT_SPEED_SS:
		s = "super-speed";
		break;
	case SLOT_SPEED_SSP:
		s = "super-speed plus";
		break;
	default:
		s = "UNKNOWN speed";
	}

	ret = sprintf(str, "%s Ctx Entries %d",
		      s, (info & LAST_CTX_MASK) >> 27);

	ret += sprintf(str + ret, " [Intr %ld] Addr %ld State %s",
		       GET_INTR_TARGET(int_target), state & DEV_ADDR_MASK,
		       cdnsp_slot_state_string(GET_SLOT_STATE(state)));

	return str;
}

static inline const char *cdnsp_portsc_link_state_string(u32 portsc)
{
	switch (portsc & PORT_PLS_MASK) {
	case XDEV_U0:
		return "U0";
	case XDEV_U1:
		return "U1";
	case XDEV_U2:
		return "U2";
	case XDEV_U3:
		return "U3";
	case XDEV_DISABLED:
		return "Disabled";
	case XDEV_RXDETECT:
		return "RxDetect";
	case XDEV_INACTIVE:
		return "Inactive";
	case XDEV_POLLING:
		return "Polling";
	case XDEV_RECOVERY:
		return "Recovery";
	case XDEV_HOT_RESET:
		return "Hot Reset";
	case XDEV_COMP_MODE:
		return "Compliance mode";
	case XDEV_TEST_MODE:
		return "Test mode";
	case XDEV_RESUME:
		return "Resume";
	default:
		break;
	}

	return "Unknown";
}

static inline const char *cdnsp_decode_portsc(char *str, size_t size,
					      u32 portsc)
{
	int ret;

	ret = snprintf(str, size, "%s %s %s Link:%s PortSpeed:%d ",
		       portsc & PORT_POWER ? "Powered" : "Powered-off",
		       portsc & PORT_CONNECT ? "Connected" : "Not-connected",
		       portsc & PORT_PED ? "Enabled" : "Disabled",
		       cdnsp_portsc_link_state_string(portsc),
		       DEV_PORT_SPEED(portsc));

	if (portsc & PORT_RESET)
		ret += snprintf(str + ret, size - ret, "In-Reset ");

	ret += snprintf(str + ret, size - ret, "Change: ");
	if (portsc & PORT_CSC)
		ret += snprintf(str + ret, size - ret, "CSC ");
	if (portsc & PORT_WRC)
		ret += snprintf(str + ret, size - ret, "WRC ");
	if (portsc & PORT_RC)
		ret += snprintf(str + ret, size - ret, "PRC ");
	if (portsc & PORT_PLC)
		ret += snprintf(str + ret, size - ret, "PLC ");
	if (portsc & PORT_CEC)
		ret += snprintf(str + ret, size - ret, "CEC ");
	ret += snprintf(str + ret, size - ret, "Wake: ");
	if (portsc & PORT_WKCONN_E)
		ret += snprintf(str + ret, size - ret, "WCE ");
	if (portsc & PORT_WKDISC_E)
		ret += snprintf(str + ret, size - ret, "WDE ");

	return str;
}

static inline const char *cdnsp_ep_state_string(u8 state)
{
	switch (state) {
	case EP_STATE_DISABLED:
		return "disabled";
	case EP_STATE_RUNNING:
		return "running";
	case EP_STATE_HALTED:
		return "halted";
	case EP_STATE_STOPPED:
		return "stopped";
	case EP_STATE_ERROR:
		return "error";
	default:
		return "INVALID";
	}
}

static inline const char *cdnsp_ep_type_string(u8 type)
{
	switch (type) {
	case ISOC_OUT_EP:
		return "Isoc OUT";
	case BULK_OUT_EP:
		return "Bulk OUT";
	case INT_OUT_EP:
		return "Int OUT";
	case CTRL_EP:
		return "Ctrl";
	case ISOC_IN_EP:
		return "Isoc IN";
	case BULK_IN_EP:
		return "Bulk IN";
	case INT_IN_EP:
		return "Int IN";
	default:
		return "INVALID";
	}
}

static inline const char *cdnsp_decode_ep_context(char *str, size_t size,
						  u32 info, u32 info2,
						  u64 deq, u32 tx_info)
{
	u8 max_pstr, ep_state, interval, ep_type, burst, cerr, mult;
	bool lsa, hid;
	u16 maxp, avg;
	u32 esit;
	int ret;

	esit = CTX_TO_MAX_ESIT_PAYLOAD_HI(info) << 16 |
	       CTX_TO_MAX_ESIT_PAYLOAD_LO(tx_info);

	ep_state = info & EP_STATE_MASK;
	max_pstr = CTX_TO_EP_MAXPSTREAMS(info);
	interval = CTX_TO_EP_INTERVAL(info);
	mult = CTX_TO_EP_MULT(info) + 1;
	lsa = !!(info & EP_HAS_LSA);

	cerr = (info2 & (3 << 1)) >> 1;
	ep_type = CTX_TO_EP_TYPE(info2);
	hid = !!(info2 & (1 << 7));
	burst = CTX_TO_MAX_BURST(info2);
	maxp = MAX_PACKET_DECODED(info2);

	avg = EP_AVG_TRB_LENGTH(tx_info);

	ret = snprintf(str, size, "State %s mult %d max P. Streams %d %s",
		       cdnsp_ep_state_string(ep_state), mult,
		       max_pstr, lsa ? "LSA " : "");

	ret += snprintf(str + ret, size - ret,
			"interval %d us max ESIT payload %d CErr %d ",
			(1 << interval) * 125, esit, cerr);

	ret += snprintf(str + ret, size - ret,
			"Type %s %sburst %d maxp %d deq %016llx ",
			cdnsp_ep_type_string(ep_type), hid ? "HID" : "",
			burst, maxp, deq);

	ret += snprintf(str + ret, size - ret, "avg trb len %d", avg);

	return str;
}

#endif /*__LINUX_CDNSP_DEBUG*/
