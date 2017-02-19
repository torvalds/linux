/**
 * debug.h - DesignWare USB3 DRD Controller Debug Header
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
		return "UNKNOWN link state\n";
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
static inline const char *
dwc3_gadget_event_string(const struct dwc3_event_devt *event)
{
	static char str[256];
	enum dwc3_link_state state = event->event_info & DWC3_LINK_STATE_MASK;

	switch (event->type) {
	case DWC3_DEVICE_EVENT_DISCONNECT:
		sprintf(str, "Disconnect: [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_RESET:
		sprintf(str, "Reset [%s]", dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_CONNECT_DONE:
		sprintf(str, "Connection Done [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_LINK_STATUS_CHANGE:
		sprintf(str, "Link Change [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_WAKEUP:
		sprintf(str, "WakeUp [%s]", dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_EOPF:
		sprintf(str, "End-Of-Frame [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_SOF:
		sprintf(str, "Start-Of-Frame [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_ERRATIC_ERROR:
		sprintf(str, "Erratic Error [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_CMD_CMPL:
		sprintf(str, "Command Complete [%s]",
				dwc3_gadget_link_string(state));
		break;
	case DWC3_DEVICE_EVENT_OVERFLOW:
		sprintf(str, "Overflow [%s]", dwc3_gadget_link_string(state));
		break;
	default:
		sprintf(str, "UNKNOWN");
	}

	return str;
}

/**
 * dwc3_ep_event_string - returns event name
 * @event: then event code
 */
static inline const char *
dwc3_ep_event_string(const struct dwc3_event_depevt *event, u32 ep0state)
{
	u8 epnum = event->endpoint_number;
	static char str[256];
	size_t len;
	int status;
	int ret;

	ret = sprintf(str, "ep%d%s: ", epnum >> 1,
			(epnum & 1) ? "in" : "out");
	if (ret < 0)
		return "UNKNOWN";

	switch (event->endpoint_event) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		strcat(str, "Transfer Complete");
		len = strlen(str);

		if (epnum <= 1)
			sprintf(str + len, " [%s]", dwc3_ep0_state_string(ep0state));
		break;
	case DWC3_DEPEVT_XFERINPROGRESS:
		strcat(str, "Transfer In-Progress");
		break;
	case DWC3_DEPEVT_XFERNOTREADY:
		strcat(str, "Transfer Not Ready");
		status = event->status & DEPEVT_STATUS_TRANSFER_ACTIVE;
		strcat(str, status ? " (Active)" : " (Not Active)");

		/* Control Endpoints */
		if (epnum <= 1) {
			int phase = DEPEVT_STATUS_CONTROL_PHASE(event->status);

			switch (phase) {
			case DEPEVT_STATUS_CONTROL_DATA:
				strcat(str, " [Data Phase]");
				break;
			case DEPEVT_STATUS_CONTROL_STATUS:
				strcat(str, " [Status Phase]");
			}
		}
		break;
	case DWC3_DEPEVT_RXTXFIFOEVT:
		strcat(str, "FIFO");
		break;
	case DWC3_DEPEVT_STREAMEVT:
		status = event->status;

		switch (status) {
		case DEPEVT_STREAMEVT_FOUND:
			sprintf(str + ret, " Stream %d Found",
					event->parameters);
			break;
		case DEPEVT_STREAMEVT_NOTFOUND:
		default:
			strcat(str, " Stream Not Found");
			break;
		}

		break;
	case DWC3_DEPEVT_EPCMDCMPLT:
		strcat(str, "Endpoint Command Complete");
		break;
	default:
		sprintf(str, "UNKNOWN");
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

static inline const char *dwc3_decode_event(u32 event, u32 ep0state)
{
	const union dwc3_event evt = (union dwc3_event) event;

	if (evt.type.is_devspec)
		return dwc3_gadget_event_string(&evt.devt);
	else
		return dwc3_ep_event_string(&evt.depevt, ep0state);
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
