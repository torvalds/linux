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
#include <linux/isdn/capiutil.h>
#include <linux/slab.h>

/* from CAPI2.0 DDK AVM Berlin GmbH */

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

#define byteTLcpy(x, y)         *(u8 *)(x) = *(u8 *)(y);
#define wordTLcpy(x, y)         *(u16 *)(x) = *(u16 *)(y);
#define dwordTLcpy(x, y)        memcpy(x, y, 4);
#define structTLcpy(x, y, l)    memcpy(x, y, l)
#define structTLcpyovl(x, y, l) memmove(x, y, l)

#define byteTRcpy(x, y)         *(u8 *)(y) = *(u8 *)(x);
#define wordTRcpy(x, y)         *(u16 *)(y) = *(u16 *)(x);
#define dwordTRcpy(x, y)        memcpy(y, x, 4);
#define structTRcpy(x, y, l)    memcpy(y, x, l)
#define structTRcpyovl(x, y, l) memmove(y, x, l)

/*-------------------------------------------------------*/
static unsigned command_2_index(unsigned c, unsigned sc)
{
	if (c & 0x80)
		c = 0x9 + (c & 0x0f);
	else if (c == 0x41)
		c = 0x9 + 0x1;
	if (c > 0x18)
		c = 0x00;
	return (sc & 3) * (0x9 + 0x9) + c;
}

/**
 * capi_cmd2par() - find parameter string for CAPI 2.0 command/subcommand
 * @cmd:	command number
 * @subcmd:	subcommand number
 *
 * Return value: static string, NULL if command/subcommand unknown
 */

static unsigned char *capi_cmd2par(u8 cmd, u8 subcmd)
{
	return cpars[command_2_index(cmd, subcmd)];
}

/*-------------------------------------------------------*/
#define TYP (cdef[cmsg->par[cmsg->p]].typ)
#define OFF (((u8 *)cmsg) + cdef[cmsg->par[cmsg->p]].off)

static void jumpcstruct(_cmsg *cmsg)
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
static void pars_2_message(_cmsg *cmsg)
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
			if (*(u8 **) OFF == NULL) {
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

/**
 * capi_cmsg2message() - assemble CAPI 2.0 message from _cmsg structure
 * @cmsg:	_cmsg structure
 * @msg:	buffer for assembled message
 *
 * Return value: 0 for success
 */

unsigned capi_cmsg2message(_cmsg *cmsg, u8 *msg)
{
	cmsg->m = msg;
	cmsg->l = 8;
	cmsg->p = 0;
	cmsg->par = capi_cmd2par(cmsg->Command, cmsg->Subcommand);
	if (!cmsg->par)
		return 1;	/* invalid command/subcommand */

	pars_2_message(cmsg);

	wordTLcpy(msg + 0, &cmsg->l);
	byteTLcpy(cmsg->m + 4, &cmsg->Command);
	byteTLcpy(cmsg->m + 5, &cmsg->Subcommand);
	wordTLcpy(cmsg->m + 2, &cmsg->ApplId);
	wordTLcpy(cmsg->m + 6, &cmsg->Messagenumber);

	return 0;
}

/*-------------------------------------------------------*/
static void message_2_pars(_cmsg *cmsg)
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

/**
 * capi_message2cmsg() - disassemble CAPI 2.0 message into _cmsg structure
 * @cmsg:	_cmsg structure
 * @msg:	buffer for assembled message
 *
 * Return value: 0 for success
 */

unsigned capi_message2cmsg(_cmsg *cmsg, u8 *msg)
{
	memset(cmsg, 0, sizeof(_cmsg));
	cmsg->m = msg;
	cmsg->l = 8;
	cmsg->p = 0;
	byteTRcpy(cmsg->m + 4, &cmsg->Command);
	byteTRcpy(cmsg->m + 5, &cmsg->Subcommand);
	cmsg->par = capi_cmd2par(cmsg->Command, cmsg->Subcommand);
	if (!cmsg->par)
		return 1;	/* invalid command/subcommand */

	message_2_pars(cmsg);

	wordTRcpy(msg + 0, &cmsg->l);
	wordTRcpy(cmsg->m + 2, &cmsg->ApplId);
	wordTRcpy(cmsg->m + 6, &cmsg->Messagenumber);

	return 0;
}

/**
 * capi_cmsg_header() - initialize header part of _cmsg structure
 * @cmsg:	_cmsg structure
 * @_ApplId:	ApplID field value
 * @_Command:	Command field value
 * @_Subcommand:	Subcommand field value
 * @_Messagenumber:	Message Number field value
 * @_Controller:	Controller/PLCI/NCCI field value
 *
 * Return value: 0 for success
 */

unsigned capi_cmsg_header(_cmsg *cmsg, u16 _ApplId,
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

/**
 * capi_cmd2str() - convert CAPI 2.0 command/subcommand number to name
 * @cmd:	command number
 * @subcmd:	subcommand number
 *
 * Return value: static string
 */

char *capi_cmd2str(u8 cmd, u8 subcmd)
{
	char *result;

	result = mnames[command_2_index(cmd, subcmd)];
	if (result == NULL)
		result = "INVALID_COMMAND";
	return result;
}


/*-------------------------------------------------------*/

#ifdef CONFIG_CAPI_TRACE

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



#include <stdarg.h>

/*-------------------------------------------------------*/
static _cdebbuf *bufprint(_cdebbuf *cdb, char *fmt, ...)
{
	va_list f;
	size_t n, r;

	if (!cdb)
		return NULL;
	va_start(f, fmt);
	r = cdb->size - cdb->pos;
	n = vsnprintf(cdb->p, r, fmt, f);
	va_end(f);
	if (n >= r) {
		/* truncated, need bigger buffer */
		size_t ns = 2 * cdb->size;
		u_char *nb;

		while ((ns - cdb->pos) <= n)
			ns *= 2;
		nb = kmalloc(ns, GFP_ATOMIC);
		if (!nb) {
			cdebbuf_free(cdb);
			return NULL;
		}
		memcpy(nb, cdb->buf, cdb->pos);
		kfree(cdb->buf);
		nb[cdb->pos] = 0;
		cdb->buf = nb;
		cdb->p = cdb->buf + cdb->pos;
		cdb->size = ns;
		va_start(f, fmt);
		r = cdb->size - cdb->pos;
		n = vsnprintf(cdb->p, r, fmt, f);
		va_end(f);
	}
	cdb->p += n;
	cdb->pos += n;
	return cdb;
}

static _cdebbuf *printstructlen(_cdebbuf *cdb, u8 *m, unsigned len)
{
	unsigned hex = 0;

	if (!cdb)
		return NULL;
	for (; len; len--, m++)
		if (isalnum(*m) || *m == ' ') {
			if (hex)
				cdb = bufprint(cdb, ">");
			cdb = bufprint(cdb, "%c", *m);
			hex = 0;
		} else {
			if (!hex)
				cdb = bufprint(cdb, "<%02x", *m);
			else
				cdb = bufprint(cdb, " %02x", *m);
			hex = 1;
		}
	if (hex)
		cdb = bufprint(cdb, ">");
	return cdb;
}

static _cdebbuf *printstruct(_cdebbuf *cdb, u8 *m)
{
	unsigned len;

	if (m[0] != 0xff) {
		len = m[0];
		m += 1;
	} else {
		len = ((u16 *) (m + 1))[0];
		m += 3;
	}
	cdb = printstructlen(cdb, m, len);
	return cdb;
}

/*-------------------------------------------------------*/
#define NAME (pnames[cmsg->par[cmsg->p]])

static _cdebbuf *protocol_message_2_pars(_cdebbuf *cdb, _cmsg *cmsg, int level)
{
	if (!cmsg->par)
		return NULL;	/* invalid command/subcommand */

	for (; TYP != _CEND; cmsg->p++) {
		int slen = 29 + 3 - level;
		int i;

		if (!cdb)
			return NULL;
		cdb = bufprint(cdb, "  ");
		for (i = 0; i < level - 1; i++)
			cdb = bufprint(cdb, " ");

		switch (TYP) {
		case _CBYTE:
			cdb = bufprint(cdb, "%-*s = 0x%x\n", slen, NAME, *(u8 *) (cmsg->m + cmsg->l));
			cmsg->l++;
			break;
		case _CWORD:
			cdb = bufprint(cdb, "%-*s = 0x%x\n", slen, NAME, *(u16 *) (cmsg->m + cmsg->l));
			cmsg->l += 2;
			break;
		case _CDWORD:
			cdb = bufprint(cdb, "%-*s = 0x%lx\n", slen, NAME, *(u32 *) (cmsg->m + cmsg->l));
			cmsg->l += 4;
			break;
		case _CSTRUCT:
			cdb = bufprint(cdb, "%-*s = ", slen, NAME);
			if (cmsg->m[cmsg->l] == '\0')
				cdb = bufprint(cdb, "default");
			else
				cdb = printstruct(cdb, cmsg->m + cmsg->l);
			cdb = bufprint(cdb, "\n");
			if (cmsg->m[cmsg->l] != 0xff)
				cmsg->l += 1 + cmsg->m[cmsg->l];
			else
				cmsg->l += 3 + *(u16 *) (cmsg->m + cmsg->l + 1);

			break;

		case _CMSTRUCT:
/*----- Metastruktur 0 -----*/
			if (cmsg->m[cmsg->l] == '\0') {
				cdb = bufprint(cdb, "%-*s = default\n", slen, NAME);
				cmsg->l++;
				jumpcstruct(cmsg);
			} else {
				char *name = NAME;
				unsigned _l = cmsg->l;
				cdb = bufprint(cdb, "%-*s\n", slen, name);
				cmsg->l = (cmsg->m + _l)[0] == 255 ? cmsg->l + 3 : cmsg->l + 1;
				cmsg->p++;
				cdb = protocol_message_2_pars(cdb, cmsg, level + 1);
			}
			break;
		}
	}
	return cdb;
}
/*-------------------------------------------------------*/

static _cdebbuf *g_debbuf;
static u_long g_debbuf_lock;
static _cmsg *g_cmsg;

static _cdebbuf *cdebbuf_alloc(void)
{
	_cdebbuf *cdb;

	if (likely(!test_and_set_bit(1, &g_debbuf_lock))) {
		cdb = g_debbuf;
		goto init;
	} else
		cdb = kmalloc(sizeof(_cdebbuf), GFP_ATOMIC);
	if (!cdb)
		return NULL;
	cdb->buf = kmalloc(CDEBUG_SIZE, GFP_ATOMIC);
	if (!cdb->buf) {
		kfree(cdb);
		return NULL;
	}
	cdb->size = CDEBUG_SIZE;
init:
	cdb->buf[0] = 0;
	cdb->p = cdb->buf;
	cdb->pos = 0;
	return cdb;
}

/**
 * cdebbuf_free() - free CAPI debug buffer
 * @cdb:	buffer to free
 */

void cdebbuf_free(_cdebbuf *cdb)
{
	if (likely(cdb == g_debbuf)) {
		test_and_clear_bit(1, &g_debbuf_lock);
		return;
	}
	if (likely(cdb))
		kfree(cdb->buf);
	kfree(cdb);
}


/**
 * capi_message2str() - format CAPI 2.0 message for printing
 * @msg:	CAPI 2.0 message
 *
 * Allocates a CAPI debug buffer and fills it with a printable representation
 * of the CAPI 2.0 message in @msg.
 * Return value: allocated debug buffer, NULL on error
 * The returned buffer should be freed by a call to cdebbuf_free() after use.
 */

_cdebbuf *capi_message2str(u8 *msg)
{
	_cdebbuf *cdb;
	_cmsg	*cmsg;

	cdb = cdebbuf_alloc();
	if (unlikely(!cdb))
		return NULL;
	if (likely(cdb == g_debbuf))
		cmsg = g_cmsg;
	else
		cmsg = kmalloc(sizeof(_cmsg), GFP_ATOMIC);
	if (unlikely(!cmsg)) {
		cdebbuf_free(cdb);
		return NULL;
	}
	cmsg->m = msg;
	cmsg->l = 8;
	cmsg->p = 0;
	byteTRcpy(cmsg->m + 4, &cmsg->Command);
	byteTRcpy(cmsg->m + 5, &cmsg->Subcommand);
	cmsg->par = capi_cmd2par(cmsg->Command, cmsg->Subcommand);

	cdb = bufprint(cdb, "%-26s ID=%03d #0x%04x LEN=%04d\n",
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       ((unsigned short *) msg)[1],
		       ((unsigned short *) msg)[3],
		       ((unsigned short *) msg)[0]);

	cdb = protocol_message_2_pars(cdb, cmsg, 1);
	if (unlikely(cmsg != g_cmsg))
		kfree(cmsg);
	return cdb;
}

/**
 * capi_cmsg2str() - format _cmsg structure for printing
 * @cmsg:	_cmsg structure
 *
 * Allocates a CAPI debug buffer and fills it with a printable representation
 * of the CAPI 2.0 message stored in @cmsg by a previous call to
 * capi_cmsg2message() or capi_message2cmsg().
 * Return value: allocated debug buffer, NULL on error
 * The returned buffer should be freed by a call to cdebbuf_free() after use.
 */

_cdebbuf *capi_cmsg2str(_cmsg *cmsg)
{
	_cdebbuf *cdb;

	if (!cmsg->m)
		return NULL;	/* no message */
	cdb = cdebbuf_alloc();
	if (!cdb)
		return NULL;
	cmsg->l = 8;
	cmsg->p = 0;
	cdb = bufprint(cdb, "%s ID=%03d #0x%04x LEN=%04d\n",
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       ((u16 *) cmsg->m)[1],
		       ((u16 *) cmsg->m)[3],
		       ((u16 *) cmsg->m)[0]);
	cdb = protocol_message_2_pars(cdb, cmsg, 1);
	return cdb;
}

int __init cdebug_init(void)
{
	g_cmsg = kmalloc(sizeof(_cmsg), GFP_KERNEL);
	if (!g_cmsg)
		return -ENOMEM;
	g_debbuf = kmalloc(sizeof(_cdebbuf), GFP_KERNEL);
	if (!g_debbuf) {
		kfree(g_cmsg);
		return -ENOMEM;
	}
	g_debbuf->buf = kmalloc(CDEBUG_GSIZE, GFP_KERNEL);
	if (!g_debbuf->buf) {
		kfree(g_cmsg);
		kfree(g_debbuf);
		return -ENOMEM;
	}
	g_debbuf->size = CDEBUG_GSIZE;
	g_debbuf->buf[0] = 0;
	g_debbuf->p = g_debbuf->buf;
	g_debbuf->pos = 0;
	return 0;
}

void __exit cdebug_exit(void)
{
	if (g_debbuf)
		kfree(g_debbuf->buf);
	kfree(g_debbuf);
	kfree(g_cmsg);
}

#else /* !CONFIG_CAPI_TRACE */

static _cdebbuf g_debbuf = {"CONFIG_CAPI_TRACE not enabled", NULL, 0, 0};

_cdebbuf *capi_message2str(u8 *msg)
{
	return &g_debbuf;
}

_cdebbuf *capi_cmsg2str(_cmsg *cmsg)
{
	return &g_debbuf;
}

void cdebbuf_free(_cdebbuf *cdb)
{
}

int __init cdebug_init(void)
{
	return 0;
}

void __exit cdebug_exit(void)
{
}

#endif

EXPORT_SYMBOL(cdebbuf_free);
EXPORT_SYMBOL(capi_cmsg2message);
EXPORT_SYMBOL(capi_message2cmsg);
EXPORT_SYMBOL(capi_cmsg_header);
EXPORT_SYMBOL(capi_cmd2str);
EXPORT_SYMBOL(capi_cmsg2str);
EXPORT_SYMBOL(capi_message2str);
