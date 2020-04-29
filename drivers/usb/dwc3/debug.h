// SPDX-License-Identifier: GPL-2.0
/**
 * debug.h - DesignWare USB3 DRD Controller Debug Header
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 */

#ifndef __DWC3_DEBUG_H
#define __DWC3_DEBUG_H

#include "core.h"

/**
 * dwc3_gadget_ep_cmd_string - returns endpoint command string
 * @cmd: command code
 */
static inline const char *
dwc3_gadget_ep_cmd_string(u8 cmd)
{
	switch (cmd) {
	case DWC3_DEPCMD_DEPSTARTCFG:
		return "Start New Configuration";
	case DWC3_DEPCMD_ENDTRANSFER:
		return "End Transfer";
	case DWC3_DEPCMD_UPDATETRANSFER:
		return "Update Transfer";
	case DWC3_DEPCMD_STARTTRANSFER:
		return "Start Transfer";
	case DWC3_DEPCMD_CLEARSTALL:
		return "Clear Stall";
	case DWC3_DEPCMD_SETSTALL:
		return "Set Stall";
	case DWC3_DEPCMD_GETEPSTATE:
		return "Get Endpoint State";
	case DWC3_DEPCMD_SETTRANSFRESOURCE:
		return "Set Endpoint Transfer Resource";
	case DWC3_DEPCMD_SETEPCONFIG:
		return "Set Endpoint Configuration";
	default:
		return "UNKNOWN command";
	}
}

/**
 * dwc3_gadget_generic_cmd_string - returns generic command string
 * @cmd: command code
 */
static inline const char *
dwc3_gadget_generic_cmd_string(u8 cmd)
{
	switch (cmd) {
	case DWC3_DGCMD_SET_LMP:
		return "Set LMP";
	case DWC3_DGCMD_SET_PERIODIC_PAR:
		return "Set Periodic Parameters";
	case DWC3_DGCMD_XMIT_FUNCTION:
		return "Transmit Function Wake Device Notification";
	case DWC3_DGCMD_SET_SCRATCHPAD_ADDR_LO:
		return "Set Scratchpad Buffer Array Address Lo";
	case DWC3_DGCMD_SET_SCRATCHPAD_ADDR_HI:
		return "Set Scratchpad Buffer Array Address Hi";
	case DWC3_DGCMD_SELECTED_FIFO_FLUSH:
		return "Selected FIFO Flush";
	case DWC3_DGCMD_ALL_FIFO_FLUSH:
		return "All FIFO Flush";
	case DWC3_DGCMD_SET_ENDPOINT_NRDY:
		return "Set Endpoint NRDY";
	case DWC3_DGCMD_RUN_SOC_BUS_LOOPBACK:
		return "Run SoC Bus Loopback Test";
	default:
		return "UNKNOWN";
	}
}

/**
 * dwc3_gadget_link_string - returns link name
 * @link_state: link state code
 */
static inline const char *
dwc3_gadget_link_string(enum dwc3_link_state link_state)
{
	switch (link_state) {
	case DWC3_LINK_STATE_U0:
		return "U0";
	case DWC3_LINK_STATE_U1:
		return "U1";
	case DWC3_LINK_STATE_U2:
		return "U2";
	case DWC3_LINK_STATE_U3:
		return "U3";
	case DWC3_LINK_STATE_SS_DIS:
		return "SS.Disabled";
	case DWC3_LINK_STATE_RX_DET:
		return "RX.Detect";
	case DWC3_LINK_STATE_SS_INACT:
		return "SS.Inactive";
	case DWC3_LINK_STATE_POLL:
		return "Polling";
	case DWC3_LINK_STATE_RECOV:
		return "Recovery";
	case DWC3_LINK_STATE_HRESET:
		return "Hot Reset";
	case DWC3_LINK_STATE_CMPLY:
		return "Compliance";
	case DWC3_LINK_STATE_LPBK:
		return "Loopback";
	case DWC3_LINK_STATE_RESET:
		return "Reset";
	case DWC3_LINK_STATE_RESUME:
		return "Resume";
	default:
		return "UNKNOWN link state";
	}
}

/**
 * dwc3_gadget_hs_link_string - returns highspeed and below link name
 * @link_state: link state code
 */
static inline const char *
dwc3_gadget_hs_link_string(enum dwc3_link_state link_state)
{
	switch (link_state) {
	case DWC3_LINK_STATE_U0:
		return "On";
	case DWC3_LINK_STATE_U2:
		return "Sleep";
	case DWC3_LINK_STATE_U3:
		return "Suspend";
	case DWC3_LINK_STATE_SS_DIS:
		return "Disconnected";
	case DWC3_LINK_STATE_RX_DET:
		return "Early Suspend";
	case DWC3_LINK_STATE_RECOV:
		return "Recovery";
	case DWC3_LINK_STATE_RESET:
		return "Reset";
	case DWC3_LINK_STATE_RESUME:
		return "Resume";
	default:
		return "UNKNOWN link state";
	}
}

/**
 * dwc3_trb_type_string - returns TRB type as a string
 * @type: the type of the TRB
 */
static inline const char *dwc3_trb_type_string(unsigned int type)
{
	switch (type) {
	case DWC3_TRBCTL_NORMAL:
		return "normal";
	case DWC3_TRBCTL_CONTROL_SETUP:
		return "setup";
	case DWC3_TRBCTL_CONTROL_STATUS2:
		return "status2";
	case DWC3_TRBCTL_CONTROL_STATUS3:
		return "status3";
	case DWC3_TRBCTL_CONTROL_DATA:
		return "data";
	case DWC3_TRBCTL_ISOCHRONOUS_FIRST:
		return "isoc-first";
	case DWC3_TRBCTL_ISOCHRONOUS:
		return "isoc";
	case DWC3_TRBCTL_LINK_TRB:
		return "link";
	default:
		return "UNKNOWN";
	}
}

static inline const char *dwc3_ep0_state_string(enum dwc3_ep0_state state)
{
	switch (state) {
	case EP0_UNCONNECTED:
		return "Unconnected";
	case EP0_SETUP_PHASE:
		return "Setup Phase";
	case EP0_DATA_PHASE:
		return "Data Phase";
	case EP0_STATUS_PHASE:
		return "Status Phase";
	default:
		return "UNKNOWN";
	}
}

/**
 * dwc3_gadget_event_string - returns event name
 * @event: the event code
 */
static inline const char *dwc3_gadget_event_string(char *str, size_t size,
		const struct dwc3_event_devt *event)
{
	enum dwc3_link_state state = event->event_info & DWC3_LINK_STATE_MASK;

	switch (event->type) {
	case DWC3_DEVICE_EVENT_DISCONNECT:
		snprintf(str, size, "Disconnect: [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_RESET:
		snprintf(str, size, "Reset [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_CONNECT_DONE:
		snprintf(str, size, "Connection Done [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_LINK_STATUS_CHANGE:
		snprintf(str, size, "Link Change [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_WAKEUP:
		snprintf(str, size, "WakeUp [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_EOPF:
		snprintf(str, size, "End-Of-Frame [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_SOF:
		snprintf(str, size, "Start-Of-Frame [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_ERRATIC_ERROR:
		snprintf(str, size, "Erratic Error [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_CMD_CMPL:
		snprintf(str, size, "Command Complete [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_OVERFLOW:
		snprintf(str, size, "Overflow [%s]",
				dwc3_gadget_link_string(state));
		break;
	default:
		snprintf(str, size, "UNKNOWN");
	}

	return str;
}

/**
 * dwc3_ep_event_string - returns event name
 * @event: then event code
 */
static inline const char *dwc3_ep_event_string(char *str, size_t size,
		const struct dwc3_event_depevt *event, u32 ep0state)
{
	u8 epnum = event->endpoint_number;
	size_t len;
	int status;

	len = scnprintf(str, size, "ep%d%s: ", epnum >> 1,
			(epnum & 1) ? "in" : "out");

	status = event->status;

	switch (event->endpoint_event) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		len += scnprintf(str + len, size - len,
				"Transfer Complete (%c%c%c)",
				status & DEPEVT_STATUS_SHORT ? 'S' : 's',
				status & DEPEVT_STATUS_IOC ? 'I' : 'i',
				status & DEPEVT_STATUS_LST ? 'L' : 'l');

		if (epnum <= 1)
			scnprintf(str + len, size - len, " [%s]",
					dwc3_ep0_state_string(ep0state));
		break;
	case DWC3_DEPEVT_XFERINPROGRESS:
		scnprintf(str + len, size - len,
				"Transfer In Progress [%d] (%c%c%c)",
				event->parameters,
				status & DEPEVT_STATUS_SHORT ? 'S' : 's',
				status & DEPEVT_STATUS_IOC ? 'I' : 'i',
				status & DEPEVT_STATUS_LST ? 'M' : 'm');
		break;
	case DWC3_DEPEVT_XFERNOTREADY:
		len += scnprintf(str + len, size - len,
				"Transfer Not Ready [%d]%s",
				event->parameters,
				status & DEPEVT_STATUS_TRANSFER_ACTIVE ?
				" (Active)" : " (Not Active)");

		/* Control Endpoints */
		if (epnum <= 1) {
			int phase = DEPEVT_STATUS_CONTROL_PHASE(event->status);

			switch (phase) {
			case DEPEVT_STATUS_CONTROL_DATA:
				scnprintf(str + len, size - len,
						" [Data Phase]");
				break;
			case DEPEVT_STATUS_CONTROL_STATUS:
				scnprintf(str + len, size - len,
						" [Status Phase]");
			}
		}
		break;
	case DWC3_DEPEVT_RXTXFIFOEVT:
		scnprintf(str + len, size - len, "FIFO");
		break;
	case DWC3_DEPEVT_STREAMEVT:
		status = event->status;

		switch (status) {
		case DEPEVT_STREAMEVT_FOUND:
			scnprintf(str + len, size - len, " Stream %d Found",
					event->parameters);
			break;
		case DEPEVT_STREAMEVT_NOTFOUND:
		default:
			scnprintf(str + len, size - len, " Stream Not Found");
			break;
		}

		break;
	case DWC3_DEPEVT_EPCMDCMPLT:
		scnprintf(str + len, size - len, "Endpoint Command Complete");
		break;
	default:
		scnprintf(str + len, size - len, "UNKNOWN");
	}

	return str;
}

/**
 * dwc3_gadget_event_type_string - return event name
 * @event: the event code
 */
static inline const char *dwc3_gadget_event_type_string(u8 event)
{
	switch (event) {
	case DWC3_DEVICE_EVENT_DISCONNECT:
		return "Disconnect";
	case DWC3_DEVICE_EVENT_RESET:
		return "Reset";
	case DWC3_DEVICE_EVENT_CONNECT_DONE:
		return "Connect Done";
	case DWC3_DEVICE_EVENT_LINK_STATUS_CHANGE:
		return "Link Status Change";
	case DWC3_DEVICE_EVENT_WAKEUP:
		return "Wake-Up";
	case DWC3_DEVICE_EVENT_HIBER_REQ:
		return "Hibernation";
	case DWC3_DEVICE_EVENT_EOPF:
		return "End of Periodic Frame";
	case DWC3_DEVICE_EVENT_SOF:
		return "Start of Frame";
	case DWC3_DEVICE_EVENT_ERRATIC_ERROR:
		return "Erratic Error";
	case DWC3_DEVICE_EVENT_CMD_CMPL:
		return "Command Complete";
	case DWC3_DEVICE_EVENT_OVERFLOW:
		return "Overflow";
	default:
		return "UNKNOWN";
	}
}

static inline const char *dwc3_decode_event(char *str, size_t size, u32 event,
		u32 ep0state)
{
	const union dwc3_event evt = (union dwc3_event) event;

	if (evt.type.is_devspec)
		return dwc3_gadget_event_string(str, size, &evt.devt);
	else
		return dwc3_ep_event_string(str, size, &evt.depevt, ep0state);
}

static inline const char *dwc3_ep_cmd_status_string(int status)
{
	switch (status) {
	case -ETIMEDOUT:
		return "Timed Out";
	case 0:
		return "Successful";
	case DEPEVT_TRANSFER_NO_RESOURCE:
		return "No Resource";
	case DEPEVT_TRANSFER_BUS_EXPIRY:
		return "Bus Expiry";
	default:
		return "UNKNOWN";
	}
}

static inline const char *dwc3_gadget_generic_cmd_status_string(int status)
{
	switch (status) {
	case -ETIMEDOUT:
		return "Timed Out";
	case 0:
		return "Successful";
	case 1:
		return "Error";
	default:
		return "UNKNOWN";
	}
}


#ifdef CONFIG_DEBUG_FS
extern void dwc3_debugfs_init(struct dwc3 *);
extern void dwc3_debugfs_exit(struct dwc3 *);
#else
static inline void dwc3_debugfs_init(struct dwc3 *d)
{  }
static inline void dwc3_debugfs_exit(struct dwc3 *d)
{  }
#endif
#endif /* __DWC3_DEBUG_H */
