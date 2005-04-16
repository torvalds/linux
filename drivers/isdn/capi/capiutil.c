/* $Id: capiutil.c,v 1.13.6.4 2001/09/23 22:24:33 kai Exp $
 *
 * CAPI 2.0 convert capi message to capi message struct
 *
 * From CAPI 2.0 Development Kit AVM 1995 (msg.c)
 * Rewritten for Linux 1996 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/isdn/capiutil.h>

/* from CAPI2.0 DDK AVM Berlin GmbH */

#ifndef CONFIG_ISDN_DRV_AVMB1_VERBOSE_REASON
char *capi_info2str(u16 reason)
{
    return "..";
}
#else
char *capi_info2str(u16 reason)
{
    switch (reason) {

/*-- informative values (corresponding message was processed) -----*/
	case 0x0001:
	   return "NCPI not supported by current protocol, NCPI ignored";
	case 0x0002:
	   return "Flags not supported by current protocol, flags ignored";
	case 0x0003:
	   return "Alert already sent by another application";

/*-- error information concerning CAPI_REGISTER -----*/
	case 0x1001:
	   return "Too many applications";
	case 0x1002:
	   return "Logical block size too small, must be at least 128 Bytes";
	case 0x1003:
	   return "Buffer exceeds 64 kByte";
	case 0x1004:
	   return "Message buffer size too small, must be at least 1024 Bytes";
	case 0x1005:
	   return "Max. number of logical connections not supported";
	case 0x1006:
	   return "Reserved";
	case 0x1007:
	   return "The message could not be accepted because of an internal busy condition";
	case 0x1008:
	   return "OS resource error (no memory ?)";
	case 0x1009:
	   return "CAPI not installed";
	case 0x100A:
	   return "Controller does not support external equipment";
	case 0x100B:
	   return "Controller does only support external equipment";

/*-- error information concerning message exchange functions -----*/
	case 0x1101:
	   return "Illegal application number";
	case 0x1102:
	   return "Illegal command or subcommand or message length less than 12 bytes";
	case 0x1103:
	   return "The message could not be accepted because of a queue full condition !! The error code does not imply that CAPI cannot receive messages directed to another controller, PLCI or NCCI";
	case 0x1104:
	   return "Queue is empty";
	case 0x1105:
	   return "Queue overflow, a message was lost !! This indicates a configuration error. The only recovery from this error is to perform a CAPI_RELEASE";
	case 0x1106:
	   return "Unknown notification parameter";
	case 0x1107:
	   return "The Message could not be accepted because of an internal busy condition";
	case 0x1108:
	   return "OS Resource error (no memory ?)";
	case 0x1109:
	   return "CAPI not installed";
	case 0x110A:
	   return "Controller does not support external equipment";
	case 0x110B:
	   return "Controller does only support external equipment";

/*-- error information concerning resource / coding problems -----*/
	case 0x2001:
	   return "Message not supported in current state";
	case 0x2002:
	   return "Illegal Controller / PLCI / NCCI";
	case 0x2003:
	   return "Out of PLCI";
	case 0x2004:
	   return "Out of NCCI";
	case 0x2005:
	   return "Out of LISTEN";
	case 0x2006:
	   return "Out of FAX resources (protocol T.30)";
	case 0x2007:
	   return "Illegal message parameter coding";

/*-- error information concerning requested services  -----*/
	case 0x3001:
	   return "B1 protocol not supported";
	case 0x3002: 
	   return "B2 protocol not supported";
	case 0x3003: 
	   return "B3 protocol not supported";
	case 0x3004: 
	   return "B1 protocol parameter not supported";
	case 0x3005: 
	   return "B2 protocol parameter not supported";
	case 0x3006: 
	   return "B3 protocol parameter not supported";
	case 0x3007: 
	   return "B protocol combination not supported";
	case 0x3008: 
	   return "NCPI not supported";
	case 0x3009: 
	   return "CIP Value unknown";
	case 0x300A: 
	   return "Flags not supported (reserved bits)";
	case 0x300B: 
	   return "Facility not supported";
	case 0x300C: 
	   return "Data length not supported by current protocol";
	case 0x300D: 
	   return "Reset procedure not supported by current protocol";

/*-- informations about the clearing of a physical connection -----*/
	case 0x3301: 
	   return "Protocol error layer 1 (broken line or B-channel removed by signalling protocol)";
	case 0x3302: 
	   return "Protocol error layer 2";
	case 0x3303: 
	   return "Protocol error layer 3";
	case 0x3304: 
	   return "Another application got that call";
/*-- T.30 specific reasons -----*/
	case 0x3311: 
	   return "Connecting not successful (remote station is no FAX G3 machine)";
	case 0x3312: 
	   return "Connecting not successful (training error)";
	case 0x3313: 
	   return "Disconnected before transfer (remote station does not support transfer mode, e.g. resolution)";
	case 0x3314: 
	   return "Disconnected during transfer (remote abort)";
	case 0x3315: 
	   return "Disconnected during transfer (remote procedure error, e.g. unsuccessful repetition of T.30 commands)";
	case 0x3316: 
	   return "Disconnected during transfer (local tx data underrun)";
	case 0x3317: 
	   return "Disconnected during transfer (local rx data overflow)";
	case 0x3318: 
	   return "Disconnected during transfer (local abort)";
	case 0x3319: 
	   return "Illegal parameter coding (e.g. SFF coding error)";

/*-- disconnect causes from the network according to ETS 300 102-1/Q.931 -----*/
	case 0x3481: return "Unallocated (unassigned) number";
	case 0x3482: return "No route to specified transit network";
	case 0x3483: return "No route to destination";
	case 0x3486: return "Channel unacceptable";
	case 0x3487: 
	   return "Call awarded and being delivered in an established channel";
	case 0x3490: return "Normal call clearing";
	case 0x3491: return "User busy";
	case 0x3492: return "No user responding";
	case 0x3493: return "No answer from user (user alerted)";
	case 0x3495: return "Call rejected";
	case 0x3496: return "Number changed";
	case 0x349A: return "Non-selected user clearing";
	case 0x349B: return "Destination out of order";
	case 0x349C: return "Invalid number format";
	case 0x349D: return "Facility rejected";
	case 0x349E: return "Response to STATUS ENQUIRY";
	case 0x349F: return "Normal, unspecified";
	case 0x34A2: return "No circuit / channel available";
	case 0x34A6: return "Network out of order";
	case 0x34A9: return "Temporary failure";
	case 0x34AA: return "Switching equipment congestion";
	case 0x34AB: return "Access information discarded";
	case 0x34AC: return "Requested circuit / channel not available";
	case 0x34AF: return "Resources unavailable, unspecified";
	case 0x34B1: return "Quality of service unavailable";
	case 0x34B2: return "Requested facility not subscribed";
	case 0x34B9: return "Bearer capability not authorized";
	case 0x34BA: return "Bearer capability not presently available";
	case 0x34BF: return "Service or option not available, unspecified";
	case 0x34C1: return "Bearer capability not implemented";
	case 0x34C2: return "Channel type not implemented";
	case 0x34C5: return "Requested facility not implemented";
	case 0x34C6: return "Only restricted digital information bearer capability is available";
	case 0x34CF: return "Service or option not implemented, unspecified";
	case 0x34D1: return "Invalid call reference value";
	case 0x34D2: return "Identified channel does not exist";
	case 0x34D3: return "A suspended call exists, but this call identity does not";
	case 0x34D4: return "Call identity in use";
	case 0x34D5: return "No call suspended";
	case 0x34D6: return "Call having the requested call identity has been cleared";
	case 0x34D8: return "Incompatible destination";
	case 0x34DB: return "Invalid transit network selection";
	case 0x34DF: return "Invalid message, unspecified";
	case 0x34E0: return "Mandatory information element is missing";
	case 0x34E1: return "Message type non-existent or not implemented";
	case 0x34E2: return "Message not compatible with call state or message type non-existent or not implemented";
	case 0x34E3: return "Information element non-existent or not implemented";
	case 0x34E4: return "Invalid information element contents";
	case 0x34E5: return "Message not compatible with call state";
	case 0x34E6: return "Recovery on timer expiry";
	case 0x34EF: return "Protocol error, unspecified";
	case 0x34FF: return "Interworking, unspecified";

	default: return "No additional information";
    }
}
#endif

typedef struct {
	int typ;
	size_t off;
} _cdef;

#define _CBYTE	       1
#define _CWORD	       2
#define _CDWORD        3
#define _CSTRUCT       4
#define _CMSTRUCT      5
#define _CEND	       6

static _cdef cdef[] =
{
    /*00 */ 
 {_CEND},
    /*01 */ 
 {_CEND},
    /*02 */ 
 {_CEND},
    /*03 */ 
 {_CDWORD, offsetof(_cmsg, adr.adrController)},
    /*04 */ 
 {_CMSTRUCT, offsetof(_cmsg, AdditionalInfo)},
    /*05 */ 
 {_CSTRUCT, offsetof(_cmsg, B1configuration)},
    /*06 */ 
 {_CWORD, offsetof(_cmsg, B1protocol)},
    /*07 */ 
 {_CSTRUCT, offsetof(_cmsg, B2configuration)},
    /*08 */ 
 {_CWORD, offsetof(_cmsg, B2protocol)},
    /*09 */ 
 {_CSTRUCT, offsetof(_cmsg, B3configuration)},
    /*0a */ 
 {_CWORD, offsetof(_cmsg, B3protocol)},
    /*0b */ 
 {_CSTRUCT, offsetof(_cmsg, BC)},
    /*0c */ 
 {_CSTRUCT, offsetof(_cmsg, BChannelinformation)},
    /*0d */ 
 {_CMSTRUCT, offsetof(_cmsg, BProtocol)},
    /*0e */ 
 {_CSTRUCT, offsetof(_cmsg, CalledPartyNumber)},
    /*0f */ 
 {_CSTRUCT, offsetof(_cmsg, CalledPartySubaddress)},
    /*10 */ 
 {_CSTRUCT, offsetof(_cmsg, CallingPartyNumber)},
    /*11 */ 
 {_CSTRUCT, offsetof(_cmsg, CallingPartySubaddress)},
    /*12 */ 
 {_CDWORD, offsetof(_cmsg, CIPmask)},
    /*13 */ 
 {_CDWORD, offsetof(_cmsg, CIPmask2)},
    /*14 */ 
 {_CWORD, offsetof(_cmsg, CIPValue)},
    /*15 */ 
 {_CDWORD, offsetof(_cmsg, Class)},
    /*16 */ 
 {_CSTRUCT, offsetof(_cmsg, ConnectedNumber)},
    /*17 */ 
 {_CSTRUCT, offsetof(_cmsg, ConnectedSubaddress)},
    /*18 */ 
 {_CDWORD, offsetof(_cmsg, Data)},
    /*19 */ 
 {_CWORD, offsetof(_cmsg, DataHandle)},
    /*1a */ 
 {_CWORD, offsetof(_cmsg, DataLength)},
    /*1b */ 
 {_CSTRUCT, offsetof(_cmsg, FacilityConfirmationParameter)},
    /*1c */ 
 {_CSTRUCT, offsetof(_cmsg, Facilitydataarray)},
    /*1d */ 
 {_CSTRUCT, offsetof(_cmsg, FacilityIndicationParameter)},
    /*1e */ 
 {_CSTRUCT, offsetof(_cmsg, FacilityRequestParameter)},
    /*1f */ 
 {_CWORD, offsetof(_cmsg, FacilitySelector)},
    /*20 */ 
 {_CWORD, offsetof(_cmsg, Flags)},
    /*21 */ 
 {_CDWORD, offsetof(_cmsg, Function)},
    /*22 */ 
 {_CSTRUCT, offsetof(_cmsg, HLC)},
    /*23 */ 
 {_CWORD, offsetof(_cmsg, Info)},
    /*24 */ 
 {_CSTRUCT, offsetof(_cmsg, InfoElement)},
    /*25 */ 
 {_CDWORD, offsetof(_cmsg, InfoMask)},
    /*26 */ 
 {_CWORD, offsetof(_cmsg, InfoNumber)},
    /*27 */ 
 {_CSTRUCT, offsetof(_cmsg, Keypadfacility)},
    /*28 */ 
 {_CSTRUCT, offsetof(_cmsg, LLC)},
    /*29 */ 
 {_CSTRUCT, offsetof(_cmsg, ManuData)},
    /*2a */ 
 {_CDWORD, offsetof(_cmsg, ManuID)},
    /*2b */ 
 {_CSTRUCT, offsetof(_cmsg, NCPI)},
    /*2c */ 
 {_CWORD, offsetof(_cmsg, Reason)},
    /*2d */ 
 {_CWORD, offsetof(_cmsg, Reason_B3)},
    /*2e */ 
 {_CWORD, offsetof(_cmsg, Reject)},
    /*2f */ 
 {_CSTRUCT, offsetof(_cmsg, Useruserdata)}
};

static unsigned char *cpars[] =
{
    /* ALERT_REQ */ [0x01] = "\x03\x04\x0c\x27\x2f\x1c\x01\x01",
    /* CONNECT_REQ */ [0x02] = "\x03\x14\x0e\x10\x0f\x11\x0d\x06\x08\x0a\x05\x07\x09\x01\x0b\x28\x22\x04\x0c\x27\x2f\x1c\x01\x01",
    /* DISCONNECT_REQ */ [0x04] = "\x03\x04\x0c\x27\x2f\x1c\x01\x01",
    /* LISTEN_REQ */ [0x05] = "\x03\x25\x12\x13\x10\x11\x01",
    /* INFO_REQ */ [0x08] = "\x03\x0e\x04\x0c\x27\x2f\x1c\x01\x01",
    /* FACILITY_REQ */ [0x09] = "\x03\x1f\x1e\x01",
    /* SELECT_B_PROTOCOL_REQ */ [0x0a] = "\x03\x0d\x06\x08\x0a\x05\x07\x09\x01\x01",
    /* CONNECT_B3_REQ */ [0x0b] = "\x03\x2b\x01",
    /* DISCONNECT_B3_REQ */ [0x0d] = "\x03\x2b\x01",
    /* DATA_B3_REQ */ [0x0f] = "\x03\x18\x1a\x19\x20\x01",
    /* RESET_B3_REQ */ [0x10] = "\x03\x2b\x01",
    /* ALERT_CONF */ [0x13] = "\x03\x23\x01",
    /* CONNECT_CONF */ [0x14] = "\x03\x23\x01",
    /* DISCONNECT_CONF */ [0x16] = "\x03\x23\x01",
    /* LISTEN_CONF */ [0x17] = "\x03\x23\x01",
    /* MANUFACTURER_REQ */ [0x18] = "\x03\x2a\x15\x21\x29\x01",
    /* INFO_CONF */ [0x1a] = "\x03\x23\x01",
    /* FACILITY_CONF */ [0x1b] = "\x03\x23\x1f\x1b\x01",
    /* SELECT_B_PROTOCOL_CONF */ [0x1c] = "\x03\x23\x01",
    /* CONNECT_B3_CONF */ [0x1d] = "\x03\x23\x01",
    /* DISCONNECT_B3_CONF */ [0x1f] = "\x03\x23\x01",
    /* DATA_B3_CONF */ [0x21] = "\x03\x19\x23\x01",
    /* RESET_B3_CONF */ [0x22] = "\x03\x23\x01",
    /* CONNECT_IND */ [0x26] = "\x03\x14\x0e\x10\x0f\x11\x0b\x28\x22\x04\x0c\x27\x2f\x1c\x01\x01",
    /* CONNECT_ACTIVE_IND */ [0x27] = "\x03\x16\x17\x28\x01",
    /* DISCONNECT_IND */ [0x28] = "\x03\x2c\x01",
    /* MANUFACTURER_CONF */ [0x2a] = "\x03\x2a\x15\x21\x29\x01",
    /* INFO_IND */ [0x2c] = "\x03\x26\x24\x01",
    /* FACILITY_IND */ [0x2d] = "\x03\x1f\x1d\x01",
    /* CONNECT_B3_IND */ [0x2f] = "\x03\x2b\x01",
    /* CONNECT_B3_ACTIVE_IND */ [0x30] = "\x03\x2b\x01",
    /* DISCONNECT_B3_IND */ [0x31] = "\x03\x2d\x2b\x01",
    /* DATA_B3_IND */ [0x33] = "\x03\x18\x1a\x19\x20\x01",
    /* RESET_B3_IND */ [0x34] = "\x03\x2b\x01",
    /* CONNECT_B3_T90_ACTIVE_IND */ [0x35] = "\x03\x2b\x01",
    /* CONNECT_RESP */ [0x38] = "\x03\x2e\x0d\x06\x08\x0a\x05\x07\x09\x01\x16\x17\x28\x04\x0c\x27\x2f\x1c\x01\x01",
    /* CONNECT_ACTIVE_RESP */ [0x39] = "\x03\x01",
    /* DISCONNECT_RESP */ [0x3a] = "\x03\x01",
    /* MANUFACTURER_IND */ [0x3c] = "\x03\x2a\x15\x21\x29\x01",
    /* INFO_RESP */ [0x3e] = "\x03\x01",
    /* FACILITY_RESP */ [0x3f] = "\x03\x1f\x01",
    /* CONNECT_B3_RESP */ [0x41] = "\x03\x2e\x2b\x01",
    /* CONNECT_B3_ACTIVE_RESP */ [0x42] = "\x03\x01",
    /* DISCONNECT_B3_RESP */ [0x43] = "\x03\x01",
    /* DATA_B3_RESP */ [0x45] = "\x03\x19\x01",
    /* RESET_B3_RESP */ [0x46] = "\x03\x01",
    /* CONNECT_B3_T90_ACTIVE_RESP */ [0x47] = "\x03\x01",
    /* MANUFACTURER_RESP */ [0x4e] = "\x03\x2a\x15\x21\x29\x01",
};

/*-------------------------------------------------------*/

#define byteTLcpy(x,y)        *(u8 *)(x)=*(u8 *)(y);
#define wordTLcpy(x,y)        *(u16 *)(x)=*(u16 *)(y);
#define dwordTLcpy(x,y)       memcpy(x,y,4);
#define structTLcpy(x,y,l)    memcpy (x,y,l)
#define structTLcpyovl(x,y,l) memmove (x,y,l)

#define byteTRcpy(x,y)        *(u8 *)(y)=*(u8 *)(x);
#define wordTRcpy(x,y)        *(u16 *)(y)=*(u16 *)(x);
#define dwordTRcpy(x,y)       memcpy(y,x,4);
#define structTRcpy(x,y,l)    memcpy (y,x,l)
#define structTRcpyovl(x,y,l) memmove (y,x,l)

/*-------------------------------------------------------*/
static unsigned command_2_index(unsigned c, unsigned sc)
{
	if (c & 0x80)
		c = 0x9 + (c & 0x0f);
	else if (c <= 0x0f);
	else if (c == 0x41)
		c = 0x9 + 0x1;
	else if (c == 0xff)
		c = 0x00;
	return (sc & 3) * (0x9 + 0x9) + c;
}

/*-------------------------------------------------------*/
#define TYP (cdef[cmsg->par[cmsg->p]].typ)
#define OFF (((u8 *)cmsg)+cdef[cmsg->par[cmsg->p]].off)

static void jumpcstruct(_cmsg * cmsg)
{
	unsigned layer;
	for (cmsg->p++, layer = 1; layer;) {
		/* $$$$$ assert (cmsg->p); */
		cmsg->p++;
		switch (TYP) {
		case _CMSTRUCT:
			layer++;
			break;
		case _CEND:
			layer--;
			break;
		}
	}
}
/*-------------------------------------------------------*/
static void pars_2_message(_cmsg * cmsg)
{

	for (; TYP != _CEND; cmsg->p++) {
		switch (TYP) {
		case _CBYTE:
			byteTLcpy(cmsg->m + cmsg->l, OFF);
			cmsg->l++;
			break;
		case _CWORD:
			wordTLcpy(cmsg->m + cmsg->l, OFF);
			cmsg->l += 2;
			break;
		case _CDWORD:
			dwordTLcpy(cmsg->m + cmsg->l, OFF);
			cmsg->l += 4;
			break;
		case _CSTRUCT:
			if (*(u8 **) OFF == 0) {
				*(cmsg->m + cmsg->l) = '\0';
				cmsg->l++;
			} else if (**(_cstruct *) OFF != 0xff) {
				structTLcpy(cmsg->m + cmsg->l, *(_cstruct *) OFF, 1 + **(_cstruct *) OFF);
				cmsg->l += 1 + **(_cstruct *) OFF;
			} else {
				_cstruct s = *(_cstruct *) OFF;
				structTLcpy(cmsg->m + cmsg->l, s, 3 + *(u16 *) (s + 1));
				cmsg->l += 3 + *(u16 *) (s + 1);
			}
			break;
		case _CMSTRUCT:
/*----- Metastruktur 0 -----*/
			if (*(_cmstruct *) OFF == CAPI_DEFAULT) {
				*(cmsg->m + cmsg->l) = '\0';
				cmsg->l++;
				jumpcstruct(cmsg);
			}
/*----- Metastruktur wird composed -----*/
			else {
				unsigned _l = cmsg->l;
				unsigned _ls;
				cmsg->l++;
				cmsg->p++;
				pars_2_message(cmsg);
				_ls = cmsg->l - _l - 1;
				if (_ls < 255)
					(cmsg->m + _l)[0] = (u8) _ls;
				else {
					structTLcpyovl(cmsg->m + _l + 3, cmsg->m + _l + 1, _ls);
					(cmsg->m + _l)[0] = 0xff;
					wordTLcpy(cmsg->m + _l + 1, &_ls);
				}
			}
			break;
		}
	}
}

/*-------------------------------------------------------*/
unsigned capi_cmsg2message(_cmsg * cmsg, u8 * msg)
{
	cmsg->m = msg;
	cmsg->l = 8;
	cmsg->p = 0;
	cmsg->par = cpars[command_2_index(cmsg->Command, cmsg->Subcommand)];

	pars_2_message(cmsg);

	wordTLcpy(msg + 0, &cmsg->l);
	byteTLcpy(cmsg->m + 4, &cmsg->Command);
	byteTLcpy(cmsg->m + 5, &cmsg->Subcommand);
	wordTLcpy(cmsg->m + 2, &cmsg->ApplId);
	wordTLcpy(cmsg->m + 6, &cmsg->Messagenumber);

	return 0;
}

/*-------------------------------------------------------*/
static void message_2_pars(_cmsg * cmsg)
{
	for (; TYP != _CEND; cmsg->p++) {

		switch (TYP) {
		case _CBYTE:
			byteTRcpy(cmsg->m + cmsg->l, OFF);
			cmsg->l++;
			break;
		case _CWORD:
			wordTRcpy(cmsg->m + cmsg->l, OFF);
			cmsg->l += 2;
			break;
		case _CDWORD:
			dwordTRcpy(cmsg->m + cmsg->l, OFF);
			cmsg->l += 4;
			break;
		case _CSTRUCT:
			*(u8 **) OFF = cmsg->m + cmsg->l;

			if (cmsg->m[cmsg->l] != 0xff)
				cmsg->l += 1 + cmsg->m[cmsg->l];
			else
				cmsg->l += 3 + *(u16 *) (cmsg->m + cmsg->l + 1);
			break;
		case _CMSTRUCT:
/*----- Metastruktur 0 -----*/
			if (cmsg->m[cmsg->l] == '\0') {
				*(_cmstruct *) OFF = CAPI_DEFAULT;
				cmsg->l++;
				jumpcstruct(cmsg);
			} else {
				unsigned _l = cmsg->l;
				*(_cmstruct *) OFF = CAPI_COMPOSE;
				cmsg->l = (cmsg->m + _l)[0] == 255 ? cmsg->l + 3 : cmsg->l + 1;
				cmsg->p++;
				message_2_pars(cmsg);
			}
			break;
		}
	}
}

/*-------------------------------------------------------*/
unsigned capi_message2cmsg(_cmsg * cmsg, u8 * msg)
{
	memset(cmsg, 0, sizeof(_cmsg));
	cmsg->m = msg;
	cmsg->l = 8;
	cmsg->p = 0;
	byteTRcpy(cmsg->m + 4, &cmsg->Command);
	byteTRcpy(cmsg->m + 5, &cmsg->Subcommand);
	cmsg->par = cpars[command_2_index(cmsg->Command, cmsg->Subcommand)];

	message_2_pars(cmsg);

	wordTRcpy(msg + 0, &cmsg->l);
	wordTRcpy(cmsg->m + 2, &cmsg->ApplId);
	wordTRcpy(cmsg->m + 6, &cmsg->Messagenumber);

	return 0;
}

/*-------------------------------------------------------*/
unsigned capi_cmsg_header(_cmsg * cmsg, u16 _ApplId,
			  u8 _Command, u8 _Subcommand,
			  u16 _Messagenumber, u32 _Controller)
{
	memset(cmsg, 0, sizeof(_cmsg));
	cmsg->ApplId = _ApplId;
	cmsg->Command = _Command;
	cmsg->Subcommand = _Subcommand;
	cmsg->Messagenumber = _Messagenumber;
	cmsg->adr.adrController = _Controller;
	return 0;
}

/*-------------------------------------------------------*/

static char *mnames[] =
{
	[0x01] = "ALERT_REQ",
	[0x02] = "CONNECT_REQ",
	[0x04] = "DISCONNECT_REQ",
	[0x05] = "LISTEN_REQ",
	[0x08] = "INFO_REQ",
	[0x09] = "FACILITY_REQ",
	[0x0a] = "SELECT_B_PROTOCOL_REQ",
	[0x0b] = "CONNECT_B3_REQ",
	[0x0d] = "DISCONNECT_B3_REQ",
	[0x0f] = "DATA_B3_REQ",
	[0x10] = "RESET_B3_REQ",
	[0x13] = "ALERT_CONF",
	[0x14] = "CONNECT_CONF",
	[0x16] = "DISCONNECT_CONF",
	[0x17] = "LISTEN_CONF",
	[0x18] = "MANUFACTURER_REQ",
	[0x1a] = "INFO_CONF",
	[0x1b] = "FACILITY_CONF",
	[0x1c] = "SELECT_B_PROTOCOL_CONF",
	[0x1d] = "CONNECT_B3_CONF",
	[0x1f] = "DISCONNECT_B3_CONF",
	[0x21] = "DATA_B3_CONF",
	[0x22] = "RESET_B3_CONF",
	[0x26] = "CONNECT_IND",
	[0x27] = "CONNECT_ACTIVE_IND",
	[0x28] = "DISCONNECT_IND",
	[0x2a] = "MANUFACTURER_CONF",
	[0x2c] = "INFO_IND",
	[0x2d] = "FACILITY_IND",
	[0x2f] = "CONNECT_B3_IND",
	[0x30] = "CONNECT_B3_ACTIVE_IND",
	[0x31] = "DISCONNECT_B3_IND",
	[0x33] = "DATA_B3_IND",
	[0x34] = "RESET_B3_IND",
	[0x35] = "CONNECT_B3_T90_ACTIVE_IND",
	[0x38] = "CONNECT_RESP",
	[0x39] = "CONNECT_ACTIVE_RESP",
	[0x3a] = "DISCONNECT_RESP",
	[0x3c] = "MANUFACTURER_IND",
	[0x3e] = "INFO_RESP",
	[0x3f] = "FACILITY_RESP",
	[0x41] = "CONNECT_B3_RESP",
	[0x42] = "CONNECT_B3_ACTIVE_RESP",
	[0x43] = "DISCONNECT_B3_RESP",
	[0x45] = "DATA_B3_RESP",
	[0x46] = "RESET_B3_RESP",
	[0x47] = "CONNECT_B3_T90_ACTIVE_RESP",
	[0x4e] = "MANUFACTURER_RESP"
};

char *capi_cmd2str(u8 cmd, u8 subcmd)
{
	return mnames[command_2_index(cmd, subcmd)];
}


/*-------------------------------------------------------*/
/*-------------------------------------------------------*/

static char *pnames[] =
{
    /*00 */ NULL,
    /*01 */ NULL,
    /*02 */ NULL,
    /*03 */ "Controller/PLCI/NCCI",
    /*04 */ "AdditionalInfo",
    /*05 */ "B1configuration",
    /*06 */ "B1protocol",
    /*07 */ "B2configuration",
    /*08 */ "B2protocol",
    /*09 */ "B3configuration",
    /*0a */ "B3protocol",
    /*0b */ "BC",
    /*0c */ "BChannelinformation",
    /*0d */ "BProtocol",
    /*0e */ "CalledPartyNumber",
    /*0f */ "CalledPartySubaddress",
    /*10 */ "CallingPartyNumber",
    /*11 */ "CallingPartySubaddress",
    /*12 */ "CIPmask",
    /*13 */ "CIPmask2",
    /*14 */ "CIPValue",
    /*15 */ "Class",
    /*16 */ "ConnectedNumber",
    /*17 */ "ConnectedSubaddress",
    /*18 */ "Data32",
    /*19 */ "DataHandle",
    /*1a */ "DataLength",
    /*1b */ "FacilityConfirmationParameter",
    /*1c */ "Facilitydataarray",
    /*1d */ "FacilityIndicationParameter",
    /*1e */ "FacilityRequestParameter",
    /*1f */ "FacilitySelector",
    /*20 */ "Flags",
    /*21 */ "Function",
    /*22 */ "HLC",
    /*23 */ "Info",
    /*24 */ "InfoElement",
    /*25 */ "InfoMask",
    /*26 */ "InfoNumber",
    /*27 */ "Keypadfacility",
    /*28 */ "LLC",
    /*29 */ "ManuData",
    /*2a */ "ManuID",
    /*2b */ "NCPI",
    /*2c */ "Reason",
    /*2d */ "Reason_B3",
    /*2e */ "Reject",
    /*2f */ "Useruserdata"
};


static char buf[8192];
static char *p = NULL;

#include <stdarg.h>

/*-------------------------------------------------------*/
static void bufprint(char *fmt,...)
{
	va_list f;
	va_start(f, fmt);
	vsprintf(p, fmt, f);
	va_end(f);
	p += strlen(p);
}

static void printstructlen(u8 * m, unsigned len)
{
	unsigned hex = 0;
	for (; len; len--, m++)
		if (isalnum(*m) || *m == ' ') {
			if (hex)
				bufprint(">");
			bufprint("%c", *m);
			hex = 0;
		} else {
			if (!hex)
				bufprint("<%02x", *m);
			else
				bufprint(" %02x", *m);
			hex = 1;
		}
	if (hex)
		bufprint(">");
}

static void printstruct(u8 * m)
{
	unsigned len;
	if (m[0] != 0xff) {
		len = m[0];
		m += 1;
	} else {
		len = ((u16 *) (m + 1))[0];
		m += 3;
	}
	printstructlen(m, len);
}

/*-------------------------------------------------------*/
#define NAME (pnames[cmsg->par[cmsg->p]])

static void protocol_message_2_pars(_cmsg * cmsg, int level)
{
	for (; TYP != _CEND; cmsg->p++) {
		int slen = 29 + 3 - level;
		int i;

		bufprint("  ");
		for (i = 0; i < level - 1; i++)
			bufprint(" ");

		switch (TYP) {
		case _CBYTE:
			bufprint("%-*s = 0x%x\n", slen, NAME, *(u8 *) (cmsg->m + cmsg->l));
			cmsg->l++;
			break;
		case _CWORD:
			bufprint("%-*s = 0x%x\n", slen, NAME, *(u16 *) (cmsg->m + cmsg->l));
			cmsg->l += 2;
			break;
		case _CDWORD:
			bufprint("%-*s = 0x%lx\n", slen, NAME, *(u32 *) (cmsg->m + cmsg->l));
			cmsg->l += 4;
			break;
		case _CSTRUCT:
			bufprint("%-*s = ", slen, NAME);
			if (cmsg->m[cmsg->l] == '\0')
				bufprint("default");
			else
				printstruct(cmsg->m + cmsg->l);
			bufprint("\n");
			if (cmsg->m[cmsg->l] != 0xff)
				cmsg->l += 1 + cmsg->m[cmsg->l];
			else
				cmsg->l += 3 + *(u16 *) (cmsg->m + cmsg->l + 1);

			break;

		case _CMSTRUCT:
/*----- Metastruktur 0 -----*/
			if (cmsg->m[cmsg->l] == '\0') {
				bufprint("%-*s = default\n", slen, NAME);
				cmsg->l++;
				jumpcstruct(cmsg);
			} else {
				char *name = NAME;
				unsigned _l = cmsg->l;
				bufprint("%-*s\n", slen, name);
				cmsg->l = (cmsg->m + _l)[0] == 255 ? cmsg->l + 3 : cmsg->l + 1;
				cmsg->p++;
				protocol_message_2_pars(cmsg, level + 1);
			}
			break;
		}
	}
}
/*-------------------------------------------------------*/
char *capi_message2str(u8 * msg)
{

	_cmsg cmsg;
	p = buf;
	p[0] = 0;

	cmsg.m = msg;
	cmsg.l = 8;
	cmsg.p = 0;
	byteTRcpy(cmsg.m + 4, &cmsg.Command);
	byteTRcpy(cmsg.m + 5, &cmsg.Subcommand);
	cmsg.par = cpars[command_2_index(cmsg.Command, cmsg.Subcommand)];

	bufprint("%-26s ID=%03d #0x%04x LEN=%04d\n",
		 mnames[command_2_index(cmsg.Command, cmsg.Subcommand)],
		 ((unsigned short *) msg)[1],
		 ((unsigned short *) msg)[3],
		 ((unsigned short *) msg)[0]);

	protocol_message_2_pars(&cmsg, 1);
	return buf;
}

char *capi_cmsg2str(_cmsg * cmsg)
{
	p = buf;
	p[0] = 0;
	cmsg->l = 8;
	cmsg->p = 0;
	bufprint("%s ID=%03d #0x%04x LEN=%04d\n",
		 mnames[command_2_index(cmsg->Command, cmsg->Subcommand)],
		 ((u16 *) cmsg->m)[1],
		 ((u16 *) cmsg->m)[3],
		 ((u16 *) cmsg->m)[0]);
	protocol_message_2_pars(cmsg, 1);
	return buf;
}

EXPORT_SYMBOL(capi_cmsg2message);
EXPORT_SYMBOL(capi_message2cmsg);
EXPORT_SYMBOL(capi_cmsg_header);
EXPORT_SYMBOL(capi_cmd2str);
EXPORT_SYMBOL(capi_cmsg2str);
EXPORT_SYMBOL(capi_message2str);
EXPORT_SYMBOL(capi_info2str);
