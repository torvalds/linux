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
		return "UNKNOWN link state\n";
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
		return "UNKNOWN link state\n";
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

static inline void dwc3_decode_get_status(__u8 t, __u16 i, __u16 l, char *str,
		size_t size)
{
	switch (t & USB_RECIP_MASK) {
	case USB_RECIP_INTERFACE:
		snprintf(str, size, "Get Interface Status(Intf = %d, Length = %d)",
				i, l);
		break;
	case USB_RECIP_ENDPOINT:
		snprintf(str, size, "Get Endpoint Status(ep%d%s)",
			i & ~USB_DIR_IN,
			i & USB_DIR_IN ? "in" : "out");
		break;
	}
}

static inline void dwc3_decode_set_clear_feature(__u8 t, __u8 b, __u16 v,
		__u16 i, char *str, size_t size)
{
	switch (t & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		snprintf(str, size, "%s Device Feature(%s%s)",
			b == USB_REQ_CLEAR_FEATURE ? "Clear" : "Set",
			({char *s;
				switch (v) {
				case USB_DEVICE_SELF_POWERED:
					s = "Self Powered";
					break;
				case USB_DEVICE_REMOTE_WAKEUP:
					s = "Remote Wakeup";
					break;
				case USB_DEVICE_TEST_MODE:
					s = "Test Mode";
					break;
				case USB_DEVICE_U1_ENABLE:
					s = "U1 Enable";
					break;
				case USB_DEVICE_U2_ENABLE:
					s = "U2 Enable";
					break;
				case USB_DEVICE_LTM_ENABLE:
					s = "LTM Enable";
					break;
				default:
					s = "UNKNOWN";
				} s; }),
			v == USB_DEVICE_TEST_MODE ?
			({ char *s;
				switch (i) {
				case TEST_J:
					s = ": TEST_J";
					break;
				case TEST_K:
					s = ": TEST_K";
					break;
				case TEST_SE0_NAK:
					s = ": TEST_SE0_NAK";
					break;
				case TEST_PACKET:
					s = ": TEST_PACKET";
					break;
				case TEST_FORCE_EN:
					s = ": TEST_FORCE_EN";
					break;
				default:
					s = ": UNKNOWN";
				} s; }) : "");
		break;
	case USB_RECIP_INTERFACE:
		snprintf(str, size, "%s Interface Feature(%s)",
			b == USB_REQ_CLEAR_FEATURE ? "Clear" : "Set",
			v == USB_INTRF_FUNC_SUSPEND ?
			"Function Suspend" : "UNKNOWN");
		break;
	case USB_RECIP_ENDPOINT:
		snprintf(str, size, "%s Endpoint Feature(%s ep%d%s)",
			b == USB_REQ_CLEAR_FEATURE ? "Clear" : "Set",
			v == USB_ENDPOINT_HALT ? "Halt" : "UNKNOWN",
			i & ~USB_DIR_IN,
			i & USB_DIR_IN ? "in" : "out");
		break;
	}
}

static inline void dwc3_decode_set_address(__u16 v, char *str, size_t size)
{
	snprintf(str, size, "Set Address(Addr = %02x)", v);
}

static inline void dwc3_decode_get_set_descriptor(__u8 t, __u8 b, __u16 v,
		__u16 i, __u16 l, char *str, size_t size)
{
	snprintf(str, size, "%s %s Descriptor(Index = %d, Length = %d)",
		b == USB_REQ_GET_DESCRIPTOR ? "Get" : "Set",
		({ char *s;
			switch (v >> 8) {
			case USB_DT_DEVICE:
				s = "Device";
				break;
			case USB_DT_CONFIG:
				s = "Configuration";
				break;
			case USB_DT_STRING:
				s = "String";
				break;
			case USB_DT_INTERFACE:
				s = "Interface";
				break;
			case USB_DT_ENDPOINT:
				s = "Endpoint";
				break;
			case USB_DT_DEVICE_QUALIFIER:
				s = "Device Qualifier";
				break;
			case USB_DT_OTHER_SPEED_CONFIG:
				s = "Other Speed Config";
				break;
			case USB_DT_INTERFACE_POWER:
				s = "Interface Power";
				break;
			case USB_DT_OTG:
				s = "OTG";
				break;
			case USB_DT_DEBUG:
				s = "Debug";
				break;
			case USB_DT_INTERFACE_ASSOCIATION:
				s = "Interface Association";
				break;
			case USB_DT_BOS:
				s = "BOS";
				break;
			case USB_DT_DEVICE_CAPABILITY:
				s = "Device Capability";
				break;
			case USB_DT_PIPE_USAGE:
				s = "Pipe Usage";
				break;
			case USB_DT_SS_ENDPOINT_COMP:
				s = "SS Endpoint Companion";
				break;
			case USB_DT_SSP_ISOC_ENDPOINT_COMP:
				s = "SSP Isochronous Endpoint Companion";
				break;
			default:
				s = "UNKNOWN";
				break;
			} s; }), v & 0xff, l);
}


static inline void dwc3_decode_get_configuration(__u16 l, char *str,
		size_t size)
{
	snprintf(str, size, "Get Configuration(Length = %d)", l);
}

static inline void dwc3_decode_set_configuration(__u8 v, char *str, size_t size)
{
	snprintf(str, size, "Set Configuration(Config = %d)", v);
}

static inline void dwc3_decode_get_intf(__u16 i, __u16 l, char *str,
		size_t size)
{
	snprintf(str, size, "Get Interface(Intf = %d, Length = %d)", i, l);
}

static inline void dwc3_decode_set_intf(__u8 v, __u16 i, char *str, size_t size)
{
	snprintf(str, size, "Set Interface(Intf = %d, Alt.Setting = %d)", i, v);
}

static inline void dwc3_decode_synch_frame(__u16 i, __u16 l, char *str,
		size_t size)
{
	snprintf(str, size, "Synch Frame(Endpoint = %d, Length = %d)", i, l);
}

static inline void dwc3_decode_set_sel(__u16 l, char *str, size_t size)
{
	snprintf(str, size, "Set SEL(Length = %d)", l);
}

static inline void dwc3_decode_set_isoch_delay(__u8 v, char *str, size_t size)
{
	snprintf(str, size, "Set Isochronous Delay(Delay = %d ns)", v);
}

/**
 * dwc3_decode_ctrl - returns a string represetion of ctrl request
 */
static inline const char *dwc3_decode_ctrl(char *str, size_t size,
		__u8 bRequestType, __u8 bRequest, __u16 wValue, __u16 wIndex,
		__u16 wLength)
{
	switch (bRequest) {
	case USB_REQ_GET_STATUS:
		dwc3_decode_get_status(bRequestType, wIndex, wLength, str,
				size);
		break;
	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		dwc3_decode_set_clear_feature(bRequestType, bRequest, wValue,
				wIndex, str, size);
		break;
	case USB_REQ_SET_ADDRESS:
		dwc3_decode_set_address(wValue, str, size);
		break;
	case USB_REQ_GET_DESCRIPTOR:
	case USB_REQ_SET_DESCRIPTOR:
		dwc3_decode_get_set_descriptor(bRequestType, bRequest, wValue,
				wIndex, wLength, str, size);
		break;
	case USB_REQ_GET_CONFIGURATION:
		dwc3_decode_get_configuration(wLength, str, size);
		break;
	case USB_REQ_SET_CONFIGURATION:
		dwc3_decode_set_configuration(wValue, str, size);
		break;
	case USB_REQ_GET_INTERFACE:
		dwc3_decode_get_intf(wIndex, wLength, str, size);
		break;
	case USB_REQ_SET_INTERFACE:
		dwc3_decode_set_intf(wValue, wIndex, str, size);
		break;
	case USB_REQ_SYNCH_FRAME:
		dwc3_decode_synch_frame(wIndex, wLength, str, size);
		break;
	case USB_REQ_SET_SEL:
		dwc3_decode_set_sel(wLength, str, size);
		break;
	case USB_REQ_SET_ISOCH_DELAY:
		dwc3_decode_set_isoch_delay(wValue, str, size);
		break;
	default:
		snprintf(str, size, "%02x %02x %02x %02x %02x %02x %02x %02x",
			bRequestType, bRequest,
			cpu_to_le16(wValue) & 0xff,
			cpu_to_le16(wValue) >> 8,
			cpu_to_le16(wIndex) & 0xff,
			cpu_to_le16(wIndex) >> 8,
			cpu_to_le16(wLength) & 0xff,
			cpu_to_le16(wLength) >> 8);
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
	int ret;

	ret = snprintf(str, size, "ep%d%s: ", epnum >> 1,
			(epnum & 1) ? "in" : "out");
	if (ret < 0)
		return "UNKNOWN";

	status = event->status;

	switch (event->endpoint_event) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		len = strlen(str);
		snprintf(str + len, size - len, "Transfer Complete (%c%c%c)",
				status & DEPEVT_STATUS_SHORT ? 'S' : 's',
				status & DEPEVT_STATUS_IOC ? 'I' : 'i',
				status & DEPEVT_STATUS_LST ? 'L' : 'l');

		len = strlen(str);

		if (epnum <= 1)
			snprintf(str + len, size - len, " [%s]",
					dwc3_ep0_state_string(ep0state));
		break;
	case DWC3_DEPEVT_XFERINPROGRESS:
		len = strlen(str);

		snprintf(str + len, size - len, "Transfer In Progress [%d] (%c%c%c)",
				event->parameters,
				status & DEPEVT_STATUS_SHORT ? 'S' : 's',
				status & DEPEVT_STATUS_IOC ? 'I' : 'i',
				status & DEPEVT_STATUS_LST ? 'M' : 'm');
		break;
	case DWC3_DEPEVT_XFERNOTREADY:
		len = strlen(str);

		snprintf(str + len, size - len, "Transfer Not Ready [%d]%s",
				event->parameters,
				status & DEPEVT_STATUS_TRANSFER_ACTIVE ?
				" (Active)" : " (Not Active)");

		len = strlen(str);

		/* Control Endpoints */
		if (epnum <= 1) {
			int phase = DEPEVT_STATUS_CONTROL_PHASE(event->status);

			switch (phase) {
			case DEPEVT_STATUS_CONTROL_DATA:
				snprintf(str + ret, size - ret,
						" [Data Phase]");
				break;
			case DEPEVT_STATUS_CONTROL_STATUS:
				snprintf(str + ret, size - ret,
						" [Status Phase]");
			}
		}
		break;
	case DWC3_DEPEVT_RXTXFIFOEVT:
		snprintf(str + ret, size - ret, "FIFO");
		break;
	case DWC3_DEPEVT_STREAMEVT:
		status = event->status;

		switch (status) {
		case DEPEVT_STREAMEVT_FOUND:
			snprintf(str + ret, size - ret, " Stream %d Found",
					event->parameters);
			break;
		case DEPEVT_STREAMEVT_NOTFOUND:
		default:
			snprintf(str + ret, size - ret, " Stream Not Found");
			break;
		}

		break;
	case DWC3_DEPEVT_EPCMDCMPLT:
		snprintf(str + ret, size - ret, "Endpoint Command Complete");
		break;
	default:
		snprintf(str, size, "UNKNOWN");
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
