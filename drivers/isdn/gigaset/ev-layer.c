/*
 * Stuff used by all variants of the driver
 *
 * Copyright (c) 2001 by Stefan Eilers <Eilers.Stefan@epost.de>,
 *                       Hansjoerg Lipp <hjlipp@web.de>,
 *                       Tilman Schmidt <tilman@imap.cc>.
 *
 * =====================================================================
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 * =====================================================================
 * ToDo: ...
 * =====================================================================
 * Version: $Id: ev-layer.c,v 1.4.2.18 2006/02/04 18:28:16 hjlipp Exp $
 * =====================================================================
 */

#include "gigaset.h"

/* ========================================================== */
/* bit masks for pending commands */
#define PC_INIT       0x004
#define PC_DLE0       0x008
#define PC_DLE1       0x010
#define PC_CID        0x080
#define PC_NOCID      0x100
#define PC_HUP        0x002
#define PC_DIAL       0x001
#define PC_ACCEPT     0x040
#define PC_SHUTDOWN   0x020
#define PC_CIDMODE    0x200
#define PC_UMMODE     0x400

/* types of modem responses */
#define RT_NOTHING 0
#define RT_ZSAU    1
#define RT_RING    2
#define RT_NUMBER  3
#define RT_STRING  4
#define RT_HEX     5
#define RT_ZCAU    6

/* Possible ASCII responses */
#define RSP_OK           0
//#define RSP_BUSY       1
//#define RSP_CONNECT    2
#define RSP_ZGCI         3
#define RSP_RING         4
#define RSP_ZAOC         5
#define RSP_ZCSTR        6
#define RSP_ZCFGT        7
#define RSP_ZCFG         8
#define RSP_ZCCR         9
#define RSP_EMPTY        10
#define RSP_ZLOG         11
#define RSP_ZCAU         12
#define RSP_ZMWI         13
#define RSP_ZABINFO      14
#define RSP_ZSMLSTCHG    15
#define RSP_VAR          100
#define RSP_ZSAU         (RSP_VAR + VAR_ZSAU)
#define RSP_ZDLE         (RSP_VAR + VAR_ZDLE)
#define RSP_ZVLS         (RSP_VAR + VAR_ZVLS)
#define RSP_ZCTP         (RSP_VAR + VAR_ZCTP)
#define RSP_STR          (RSP_VAR + VAR_NUM)
#define RSP_NMBR         (RSP_STR + STR_NMBR)
#define RSP_ZCPN         (RSP_STR + STR_ZCPN)
#define RSP_ZCON         (RSP_STR + STR_ZCON)
#define RSP_ZBC          (RSP_STR + STR_ZBC)
#define RSP_ZHLC         (RSP_STR + STR_ZHLC)
#define RSP_ERROR       -1       /* ERROR              */
#define RSP_WRONG_CID   -2       /* unknown cid in cmd */
//#define RSP_EMPTY     -3
#define RSP_UNKNOWN     -4       /* unknown response   */
#define RSP_FAIL        -5       /* internal error     */
#define RSP_INVAL       -6       /* invalid response   */

#define RSP_NONE        -19
#define RSP_STRING      -20
#define RSP_NULL        -21
//#define RSP_RETRYFAIL -22
//#define RSP_RETRY     -23
//#define RSP_SKIP      -24
#define RSP_INIT        -27
#define RSP_ANY         -26
#define RSP_LAST        -28
#define RSP_NODEV       -9

/* actions for process_response */
#define ACT_NOTHING		0
#define ACT_SETDLE1		1
#define ACT_SETDLE0		2
#define ACT_FAILINIT		3
#define ACT_HUPMODEM		4
#define ACT_CONFIGMODE		5
#define ACT_INIT		6
#define ACT_DLE0		7
#define ACT_DLE1		8
#define ACT_FAILDLE0		9
#define ACT_FAILDLE1		10
#define ACT_RING		11
#define ACT_CID			12
#define ACT_FAILCID		13
#define ACT_SDOWN		14
#define ACT_FAILSDOWN		15
#define ACT_DEBUG		16
#define ACT_WARN		17
#define ACT_DIALING		18
#define ACT_ABORTDIAL		19
#define ACT_DISCONNECT		20
#define ACT_CONNECT		21
#define ACT_REMOTEREJECT	22
#define ACT_CONNTIMEOUT         23
#define ACT_REMOTEHUP		24
#define ACT_ABORTHUP		25
#define ACT_ICALL		26
#define ACT_ACCEPTED		27
#define ACT_ABORTACCEPT		28
#define ACT_TIMEOUT		29
#define ACT_GETSTRING		30
#define ACT_SETVER		31
#define ACT_FAILVER		32
#define ACT_GOTVER		33
#define ACT_TEST		34
#define ACT_ERROR		35
#define ACT_ABORTCID		36
#define ACT_ZCAU		37
#define ACT_NOTIFY_BC_DOWN      38
#define ACT_NOTIFY_BC_UP        39
#define ACT_DIAL                40
#define ACT_ACCEPT              41
#define ACT_PROTO_L2            42
#define ACT_HUP                 43
#define ACT_IF_LOCK             44
#define ACT_START               45
#define ACT_STOP                46
#define ACT_FAKEDLE0            47
#define ACT_FAKEHUP             48
#define ACT_FAKESDOWN           49
#define ACT_SHUTDOWN            50
#define ACT_PROC_CIDMODE        51
#define ACT_UMODESET            52
#define ACT_FAILUMODE           53
#define ACT_CMODESET            54
#define ACT_FAILCMODE           55
#define ACT_IF_VER              56
#define ACT_CMD			100

/* at command sequences */
#define SEQ_NONE      0
#define SEQ_INIT      100
#define SEQ_DLE0      200
#define SEQ_DLE1      250
#define SEQ_CID       300
#define SEQ_NOCID     350
#define SEQ_HUP       400
#define SEQ_DIAL      600
#define SEQ_ACCEPT    720
#define SEQ_SHUTDOWN  500
#define SEQ_CIDMODE   10
#define SEQ_UMMODE    11


// 100: init, 200: dle0, 250:dle1, 300: get cid (dial), 350: "hup" (no cid), 400: hup, 500: reset, 600: dial, 700: ring
struct reply_t gigaset_tab_nocid_m10x[]= /* with dle mode */
{
	/* resp_code, min_ConState, max_ConState, parameter, new_ConState, timeout, action, command */

	/* initialize device, set cid mode if possible */
	//{RSP_INIT,     -1, -1,100,                900, 0, {ACT_TEST}},
	//{RSP_ERROR,   900,900, -1,                  0, 0, {ACT_FAILINIT}},
	//{RSP_OK,      900,900, -1,                100, INIT_TIMEOUT,
	//                                                  {ACT_TIMEOUT}},

	{RSP_INIT,     -1, -1,SEQ_INIT,           100, INIT_TIMEOUT,
	                                                  {ACT_TIMEOUT}},                /* wait until device is ready */

	{EV_TIMEOUT,  100,100, -1,                101, 3, {0},             "Z\r"},       /* device in transparent mode? try to initialize it. */
	{RSP_OK,      101,103, -1,                120, 5, {ACT_GETSTRING}, "+GMR\r"},    /* get version */

	{EV_TIMEOUT,  101,101, -1,                102, 5, {0},             "Z\r"},       /* timeout => try once again. */
	{RSP_ERROR,   101,101, -1,                102, 5, {0},             "Z\r"},       /* error => try once again. */

	{EV_TIMEOUT,  102,102, -1,                108, 5, {ACT_SETDLE1},   "^SDLE=0\r"}, /* timeout => try again in DLE mode. */
	{RSP_OK,      108,108, -1,                104,-1},
	{RSP_ZDLE,    104,104,  0,                103, 5, {0},             "Z\r"},
	{EV_TIMEOUT,  104,104, -1,                  0, 0, {ACT_FAILINIT}},
	{RSP_ERROR,   108,108, -1,                  0, 0, {ACT_FAILINIT}},

	{EV_TIMEOUT,  108,108, -1,                105, 2, {ACT_SETDLE0,
	                                                   ACT_HUPMODEM,
	                                                   ACT_TIMEOUT}},                /* still timeout => connection in unimodem mode? */
	{EV_TIMEOUT,  105,105, -1,                103, 5, {0},             "Z\r"},

	{RSP_ERROR,   102,102, -1,                107, 5, {0},             "^GETPRE\r"}, /* ERROR on ATZ => maybe in config mode? */
	{RSP_OK,      107,107, -1,                  0, 0, {ACT_CONFIGMODE}},
	{RSP_ERROR,   107,107, -1,                  0, 0, {ACT_FAILINIT}},
	{EV_TIMEOUT,  107,107, -1,                  0, 0, {ACT_FAILINIT}},

	{RSP_ERROR,   103,103, -1,                  0, 0, {ACT_FAILINIT}},
	{EV_TIMEOUT,  103,103, -1,                  0, 0, {ACT_FAILINIT}},

	{RSP_STRING,  120,120, -1,                121,-1, {ACT_SETVER}},

	{EV_TIMEOUT,  120,121, -1,                  0, 0, {ACT_FAILVER, ACT_INIT}},
	{RSP_ERROR,   120,121, -1,                  0, 0, {ACT_FAILVER, ACT_INIT}},
	{RSP_OK,      121,121, -1,                  0, 0, {ACT_GOTVER,  ACT_INIT}},
#if 0
	{EV_TIMEOUT,  120,121, -1,                130, 5, {ACT_FAILVER},   "^SGCI=1\r"},
	{RSP_ERROR,   120,121, -1,                130, 5, {ACT_FAILVER},   "^SGCI=1\r"},
	{RSP_OK,      121,121, -1,                130, 5, {ACT_GOTVER},    "^SGCI=1\r"},

	{RSP_OK,      130,130, -1,                  0, 0, {ACT_INIT}},
	{RSP_ERROR,   130,130, -1,                  0, 0, {ACT_FAILINIT}},
	{EV_TIMEOUT,  130,130, -1,                  0, 0, {ACT_FAILINIT}},
#endif

	/* leave dle mode */
	{RSP_INIT,      0,  0,SEQ_DLE0,           201, 5, {0},             "^SDLE=0\r"},
	{RSP_OK,      201,201, -1,                202,-1},
	//{RSP_ZDLE,    202,202,  0,                202, 0, {ACT_ERROR}},//DELETE
	{RSP_ZDLE,    202,202,  0,                  0, 0, {ACT_DLE0}},
	{RSP_NODEV,   200,249, -1,                  0, 0, {ACT_FAKEDLE0}},
	{RSP_ERROR,   200,249, -1,                  0, 0, {ACT_FAILDLE0}},
	{EV_TIMEOUT,  200,249, -1,                  0, 0, {ACT_FAILDLE0}},

	/* enter dle mode */
	{RSP_INIT,      0,  0,SEQ_DLE1,           251, 5, {0},             "^SDLE=1\r"},
	{RSP_OK,      251,251, -1,                252,-1},
	{RSP_ZDLE,    252,252,  1,                  0, 0, {ACT_DLE1}},
	{RSP_ERROR,   250,299, -1,                  0, 0, {ACT_FAILDLE1}},
	{EV_TIMEOUT,  250,299, -1,                  0, 0, {ACT_FAILDLE1}},

	/* incoming call */
	{RSP_RING,     -1, -1, -1,                 -1,-1, {ACT_RING}},

	/* get cid */
	//{RSP_INIT,      0,  0,300,                901, 0, {ACT_TEST}},
	//{RSP_ERROR,   901,901, -1,                  0, 0, {ACT_FAILCID}},
	//{RSP_OK,      901,901, -1,                301, 5, {0},             "^SGCI?\r"},

	{RSP_INIT,      0,  0,SEQ_CID,            301, 5, {0},             "^SGCI?\r"},
	{RSP_OK,      301,301, -1,                302,-1},
	{RSP_ZGCI,    302,302, -1,                  0, 0, {ACT_CID}},
	{RSP_ERROR,   301,349, -1,                  0, 0, {ACT_FAILCID}},
	{EV_TIMEOUT,  301,349, -1,                  0, 0, {ACT_FAILCID}},

	/* enter cid mode */
	{RSP_INIT,      0,  0,SEQ_CIDMODE,        150, 5, {0},             "^SGCI=1\r"},
	{RSP_OK,      150,150, -1,                  0, 0, {ACT_CMODESET}},
	{RSP_ERROR,   150,150, -1,                  0, 0, {ACT_FAILCMODE}},
	{EV_TIMEOUT,  150,150, -1,                  0, 0, {ACT_FAILCMODE}},

	/* leave cid mode */
	//{RSP_INIT,      0,  0,SEQ_UMMODE,         160, 5, {0},             "^SGCI=0\r"},
	{RSP_INIT,      0,  0,SEQ_UMMODE,         160, 5, {0},             "Z\r"},
	{RSP_OK,      160,160, -1,                  0, 0, {ACT_UMODESET}},
	{RSP_ERROR,   160,160, -1,                  0, 0, {ACT_FAILUMODE}},
	{EV_TIMEOUT,  160,160, -1,                  0, 0, {ACT_FAILUMODE}},

	/* abort getting cid */
	{RSP_INIT,      0,  0,SEQ_NOCID,            0, 0, {ACT_ABORTCID}},

	/* reset */
#if 0
	{RSP_INIT,      0,  0,SEQ_SHUTDOWN,       503, 5, {0},             "^SGCI=0\r"},
	{RSP_OK,      503,503, -1,                504, 5, {0},             "Z\r"},
#endif
	{RSP_INIT,      0,  0,SEQ_SHUTDOWN,       504, 5, {0},             "Z\r"},
	{RSP_OK,      504,504, -1,                  0, 0, {ACT_SDOWN}},
	{RSP_ERROR,   501,599, -1,                  0, 0, {ACT_FAILSDOWN}},
	{EV_TIMEOUT,  501,599, -1,                  0, 0, {ACT_FAILSDOWN}},
	{RSP_NODEV,   501,599, -1,                  0, 0, {ACT_FAKESDOWN}},

	{EV_PROC_CIDMODE,-1, -1, -1,               -1,-1, {ACT_PROC_CIDMODE}}, //FIXME
	{EV_IF_LOCK,   -1, -1, -1,                 -1,-1, {ACT_IF_LOCK}}, //FIXME
	{EV_IF_VER,    -1, -1, -1,                 -1,-1, {ACT_IF_VER}}, //FIXME
	{EV_START,     -1, -1, -1,                 -1,-1, {ACT_START}}, //FIXME
	{EV_STOP,      -1, -1, -1,                 -1,-1, {ACT_STOP}}, //FIXME
	{EV_SHUTDOWN,  -1, -1, -1,                 -1,-1, {ACT_SHUTDOWN}}, //FIXME

	/* misc. */
	{RSP_EMPTY,    -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZCFGT,    -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZCFG,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZLOG,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZMWI,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZABINFO,  -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZSMLSTCHG,-1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME

	{RSP_ZCAU,     -1, -1, -1,                 -1,-1, {ACT_ZCAU}},
	{RSP_NONE,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}},
	{RSP_ANY,      -1, -1, -1,                 -1,-1, {ACT_WARN}},
	{RSP_LAST}
};

// 600: start dialing, 650: dial in progress, 800: connection is up, 700: ring, 400: hup, 750: accepted icall
struct reply_t gigaset_tab_cid_m10x[] = /* for M10x */
{
	/* resp_code, min_ConState, max_ConState, parameter, new_ConState, timeout, action, command */

	/* dial */
	{EV_DIAL,      -1, -1, -1,                 -1,-1, {ACT_DIAL}}, //FIXME
	{RSP_INIT,      0,  0,SEQ_DIAL,           601, 5, {ACT_CMD+AT_BC}},
	{RSP_OK,      601,601, -1,                602, 5, {ACT_CMD+AT_HLC}},
	{RSP_NULL,    602,602, -1,                603, 5, {ACT_CMD+AT_PROTO}},
	{RSP_OK,      602,602, -1,                603, 5, {ACT_CMD+AT_PROTO}},
	{RSP_OK,      603,603, -1,                604, 5, {ACT_CMD+AT_TYPE}},
	{RSP_OK,      604,604, -1,                605, 5, {ACT_CMD+AT_MSN}},
	{RSP_OK,      605,605, -1,                606, 5, {ACT_CMD+AT_ISO}},
	{RSP_NULL,    605,605, -1,                606, 5, {ACT_CMD+AT_ISO}},
	{RSP_OK,      606,606, -1,                607, 5, {0},             "+VLS=17\r"}, /* set "Endgeraetemodus" */
	{RSP_OK,      607,607, -1,                608,-1},
	//{RSP_ZSAU,    608,608,ZSAU_PROCEEDING,    608, 0, {ACT_ERROR}},//DELETE
	{RSP_ZSAU,    608,608,ZSAU_PROCEEDING,    609, 5, {ACT_CMD+AT_DIAL}},
	{RSP_OK,      609,609, -1,                650, 0, {ACT_DIALING}},

	{RSP_ZVLS,    608,608, 17,                 -1,-1, {ACT_DEBUG}},
	{RSP_ZCTP,    609,609, -1,                 -1,-1, {ACT_DEBUG}},
	{RSP_ZCPN,    609,609, -1,                 -1,-1, {ACT_DEBUG}},
	{RSP_ERROR,   601,609, -1,                  0, 0, {ACT_ABORTDIAL}},
	{EV_TIMEOUT,  601,609, -1,                  0, 0, {ACT_ABORTDIAL}},

	/* dialing */
	{RSP_ZCTP,    650,650, -1,                 -1,-1, {ACT_DEBUG}},
	{RSP_ZCPN,    650,650, -1,                 -1,-1, {ACT_DEBUG}},
	{RSP_ZSAU,    650,650,ZSAU_CALL_DELIVERED, -1,-1, {ACT_DEBUG}}, /* some devices don't send this */

	/* connection established  */
	{RSP_ZSAU,    650,650,ZSAU_ACTIVE,        800,-1, {ACT_CONNECT}}, //FIXME -> DLE1
	{RSP_ZSAU,    750,750,ZSAU_ACTIVE,        800,-1, {ACT_CONNECT}}, //FIXME -> DLE1

	{EV_BC_OPEN,  800,800, -1,                800,-1, {ACT_NOTIFY_BC_UP}}, //FIXME new constate + timeout

	/* remote hangup */
	{RSP_ZSAU,    650,650,ZSAU_DISCONNECT_IND,  0, 0, {ACT_REMOTEREJECT}},
	{RSP_ZSAU,    750,750,ZSAU_DISCONNECT_IND,  0, 0, {ACT_REMOTEHUP}},
	{RSP_ZSAU,    800,800,ZSAU_DISCONNECT_IND,  0, 0, {ACT_REMOTEHUP}},

	/* hangup */
	{EV_HUP,       -1, -1, -1,                 -1,-1, {ACT_HUP}}, //FIXME
	{RSP_INIT,     -1, -1,SEQ_HUP,            401, 5, {0},             "+VLS=0\r"}, /* hang up */ //-1,-1?
	{RSP_OK,      401,401, -1,                402, 5},
	{RSP_ZVLS,    402,402,  0,                403, 5},
	{RSP_ZSAU,    403,403,ZSAU_DISCONNECT_REQ, -1,-1, {ACT_DEBUG}}, /* if not remote hup */
	//{RSP_ZSAU,    403,403,ZSAU_NULL,          401, 0, {ACT_ERROR}}, //DELETE//FIXME -> DLE0 // should we do this _before_ hanging up for base driver?
	{RSP_ZSAU,    403,403,ZSAU_NULL,            0, 0, {ACT_DISCONNECT}}, //FIXME -> DLE0 // should we do this _before_ hanging up for base driver?
	{RSP_NODEV,   401,403, -1,                  0, 0, {ACT_FAKEHUP}}, //FIXME -> DLE0 // should we do this _before_ hanging up for base driver?
	{RSP_ERROR,   401,401, -1,                  0, 0, {ACT_ABORTHUP}},
	{EV_TIMEOUT,  401,403, -1,                  0, 0, {ACT_ABORTHUP}},

	{EV_BC_CLOSED,  0,  0, -1,                  0,-1, {ACT_NOTIFY_BC_DOWN}}, //FIXME new constate + timeout

	/* ring */
	{RSP_ZBC,     700,700, -1,                 -1,-1, {0}},
	{RSP_ZHLC,    700,700, -1,                 -1,-1, {0}},
	{RSP_NMBR,    700,700, -1,                 -1,-1, {0}},
	{RSP_ZCPN,    700,700, -1,                 -1,-1, {0}},
	{RSP_ZCTP,    700,700, -1,                 -1,-1, {0}},
	{EV_TIMEOUT,  700,700, -1,               720,720, {ACT_ICALL}},
	{EV_BC_CLOSED,720,720, -1,                  0,-1, {ACT_NOTIFY_BC_DOWN}},

	/*accept icall*/
	{EV_ACCEPT,    -1, -1, -1,                 -1,-1, {ACT_ACCEPT}}, //FIXME
	{RSP_INIT,    720,720,SEQ_ACCEPT,         721, 5, {ACT_CMD+AT_PROTO}},
	{RSP_OK,      721,721, -1,                722, 5, {ACT_CMD+AT_ISO}},
	{RSP_OK,      722,722, -1,                723, 5, {0},             "+VLS=17\r"}, /* set "Endgeraetemodus" */
	{RSP_OK,      723,723, -1,                724, 5, {0}},
	{RSP_ZVLS,    724,724, 17,                750,50, {ACT_ACCEPTED}},
	{RSP_ERROR,   721,729, -1,                  0, 0, {ACT_ABORTACCEPT}},
	{EV_TIMEOUT,  721,729, -1,                  0, 0, {ACT_ABORTACCEPT}},
	{RSP_ZSAU,    700,729,ZSAU_NULL,            0, 0, {ACT_ABORTACCEPT}},
	{RSP_ZSAU,    700,729,ZSAU_ACTIVE,          0, 0, {ACT_ABORTACCEPT}},
	{RSP_ZSAU,    700,729,ZSAU_DISCONNECT_IND,  0, 0, {ACT_ABORTACCEPT}},

	{EV_TIMEOUT,  750,750, -1,                  0, 0, {ACT_CONNTIMEOUT}},

	/* misc. */
	{EV_PROTO_L2,  -1, -1, -1,                 -1,-1, {ACT_PROTO_L2}}, //FIXME

	{RSP_ZCON,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZCCR,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZAOC,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME
	{RSP_ZCSTR,    -1, -1, -1,                 -1,-1, {ACT_DEBUG}}, //FIXME

	{RSP_ZCAU,     -1, -1, -1,                 -1,-1, {ACT_ZCAU}},
	{RSP_NONE,     -1, -1, -1,                 -1,-1, {ACT_DEBUG}},
	{RSP_ANY,      -1, -1, -1,                 -1,-1, {ACT_WARN}},
	{RSP_LAST}
};


#if 0
static struct reply_t tab_nocid[]= /* no dle mode */ //FIXME aenderungen uebernehmen
{
	/* resp_code, min_ConState, max_ConState, parameter, new_ConState, timeout, action, command */

	{RSP_ANY,      -1, -1, -1,                 -1,-1, ACT_WARN,         NULL},
	{RSP_LAST,0,0,0,0,0,0}
};

static struct reply_t tab_cid[] = /* no dle mode */ //FIXME aenderungen uebernehmen
{
	/* resp_code, min_ConState, max_ConState, parameter, new_ConState, timeout, action, command */

	{RSP_ANY,      -1, -1, -1,                 -1,-1, ACT_WARN,         NULL},
	{RSP_LAST,0,0,0,0,0,0}
};
#endif

static struct resp_type_t resp_type[]=
{
	/*{"",          RSP_EMPTY,  RT_NOTHING},*/
	{"OK",        RSP_OK,     RT_NOTHING},
	{"ERROR",     RSP_ERROR,  RT_NOTHING},
	{"ZSAU",      RSP_ZSAU,   RT_ZSAU},
	{"ZCAU",      RSP_ZCAU,   RT_ZCAU},
	{"RING",      RSP_RING,   RT_RING},
	{"ZGCI",      RSP_ZGCI,   RT_NUMBER},
	{"ZVLS",      RSP_ZVLS,   RT_NUMBER},
	{"ZCTP",      RSP_ZCTP,   RT_NUMBER},
	{"ZDLE",      RSP_ZDLE,   RT_NUMBER},
	{"ZCFGT",     RSP_ZCFGT,  RT_NUMBER},
	{"ZCCR",      RSP_ZCCR,   RT_NUMBER},
	{"ZMWI",      RSP_ZMWI,   RT_NUMBER},
	{"ZHLC",      RSP_ZHLC,   RT_STRING},
	{"ZBC",       RSP_ZBC,    RT_STRING},
	{"NMBR",      RSP_NMBR,   RT_STRING},
	{"ZCPN",      RSP_ZCPN,   RT_STRING},
	{"ZCON",      RSP_ZCON,   RT_STRING},
	{"ZAOC",      RSP_ZAOC,   RT_STRING},
	{"ZCSTR",     RSP_ZCSTR,  RT_STRING},
	{"ZCFG",      RSP_ZCFG,   RT_HEX},
	{"ZLOG",      RSP_ZLOG,   RT_NOTHING},
	{"ZABINFO",   RSP_ZABINFO, RT_NOTHING},
	{"ZSMLSTCHG", RSP_ZSMLSTCHG, RT_NOTHING},
	{NULL,0,0}
};

/*
 * Get integer from char-pointer
 */
static int isdn_getnum(char *p)
{
	int v = -1;

	IFNULLRETVAL(p, -1);

	dbg(DEBUG_TRANSCMD, "string: %s", p);

	while (*p >= '0' && *p <= '9')
		v = ((v < 0) ? 0 : (v * 10)) + (int) ((*p++) - '0');
	if (*p)
		v = -1; /* invalid Character */
	return v;
}

/*
 * Get integer from char-pointer
 */
static int isdn_gethex(char *p)
{
	int v = 0;
	int c;

	IFNULLRETVAL(p, -1);

	dbg(DEBUG_TRANSCMD, "string: %s", p);

	if (!*p)
		return -1;

	do {
		if (v > (INT_MAX - 15) / 16)
			return -1;
		c = *p;
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'f')
			c -= 'a' - 10;
		else if (c >= 'A' && c <= 'F')
			c -= 'A' - 10;
		else
			return -1;
		v = v * 16 + c;
	} while (*++p);

	return v;
}

static inline void new_index(atomic_t *index, int max)
{
	if (atomic_read(index) == max)	//FIXME race?
		atomic_set(index, 0);
	else
		atomic_inc(index);
}

/* retrieve CID from parsed response
 * returns 0 if no CID, -1 if invalid CID, or CID value 1..65535
 */
static int cid_of_response(char *s)
{
	int cid;

	if (s[-1] != ';')
		return 0;	/* no CID separator */
	cid = isdn_getnum(s);
	if (cid < 0)
		return 0;	/* CID not numeric */
	if (cid < 1 || cid > 65535)
		return -1;	/* CID out of range */
	return cid;
	//FIXME is ;<digit>+ at end of non-CID response really impossible?
}

/* This function will be called via task queue from the callback handler.
 * We received a modem response and have to handle it..
 */
void gigaset_handle_modem_response(struct cardstate *cs)
{
	unsigned char *argv[MAX_REC_PARAMS + 1];
	int params;
	int i, j;
	struct resp_type_t *rt;
	int curarg;
	unsigned long flags;
	unsigned next, tail, head;
	struct event_t *event;
	int resp_code;
	int param_type;
	int abort;
	size_t len;
	int cid;
	int rawstring;

	IFNULLRET(cs);

	len = cs->cbytes;
	if (!len) {
		/* ignore additional LFs/CRs (M10x config mode or cx100) */
		dbg(DEBUG_MCMD, "skipped EOL [%02X]", cs->respdata[len]);
		return;
	}
	cs->respdata[len] = 0;
	dbg(DEBUG_TRANSCMD, "raw string: '%s'", cs->respdata);
	argv[0] = cs->respdata;
	params = 1;
	if (cs->at_state.getstring) {
		/* getstring only allowed without cid at the moment */
		cs->at_state.getstring = 0;
		rawstring = 1;
		cid = 0;
	} else {
		/* parse line */
		for (i = 0; i < len; i++)
			switch (cs->respdata[i]) {
			case ';':
			case ',':
			case '=':
				if (params > MAX_REC_PARAMS) {
					warn("too many parameters in response");
					/* need last parameter (might be CID) */
					params--;
				}
				argv[params++] = cs->respdata + i + 1;
			}

		rawstring = 0;
		cid = params > 1 ? cid_of_response(argv[params-1]) : 0;
		if (cid < 0) {
			gigaset_add_event(cs, &cs->at_state, RSP_INVAL,
			                  NULL, 0, NULL);
			return;
		}

		for (j = 1; j < params; ++j)
			argv[j][-1] = 0;

		dbg(DEBUG_TRANSCMD, "CMD received: %s", argv[0]);
		if (cid) {
			--params;
			dbg(DEBUG_TRANSCMD, "CID: %s", argv[params]);
		}
		dbg(DEBUG_TRANSCMD, "available params: %d", params - 1);
		for (j = 1; j < params; j++)
			dbg(DEBUG_TRANSCMD, "param %d: %s", j, argv[j]);
	}

	spin_lock_irqsave(&cs->ev_lock, flags);
	head = atomic_read(&cs->ev_head);
	tail = atomic_read(&cs->ev_tail);

	abort = 1;
	curarg = 0;
	while (curarg < params) {
		next = (tail + 1) % MAX_EVENTS;
		if (unlikely(next == head)) {
			err("event queue full");
			break;
		}

		event = cs->events + tail;
		event->at_state = NULL;
		event->cid = cid;
		event->ptr = NULL;
		event->arg = NULL;
		tail = next;

		if (rawstring) {
			resp_code = RSP_STRING;
			param_type = RT_STRING;
		} else {
			for (rt = resp_type; rt->response; ++rt)
				if (!strcmp(argv[curarg], rt->response))
					break;

			if (!rt->response) {
				event->type = RSP_UNKNOWN;
				warn("unknown modem response: %s",
				     argv[curarg]);
				break;
			}

			resp_code = rt->resp_code;
			param_type = rt->type;
			++curarg;
		}

		event->type = resp_code;

		switch (param_type) {
		case RT_NOTHING:
			break;
		case RT_RING:
			if (!cid) {
				err("received RING without CID!");
				event->type = RSP_INVAL;
				abort = 1;
			} else {
				event->cid = 0;
				event->parameter = cid;
				abort = 0;
			}
			break;
		case RT_ZSAU:
			if (curarg >= params) {
				event->parameter = ZSAU_NONE;
				break;
			}
			if (!strcmp(argv[curarg], "OUTGOING_CALL_PROCEEDING"))
				event->parameter = ZSAU_OUTGOING_CALL_PROCEEDING;
			else if (!strcmp(argv[curarg], "CALL_DELIVERED"))
				event->parameter = ZSAU_CALL_DELIVERED;
			else if (!strcmp(argv[curarg], "ACTIVE"))
				event->parameter = ZSAU_ACTIVE;
			else if (!strcmp(argv[curarg], "DISCONNECT_IND"))
				event->parameter = ZSAU_DISCONNECT_IND;
			else if (!strcmp(argv[curarg], "NULL"))
				event->parameter = ZSAU_NULL;
			else if (!strcmp(argv[curarg], "DISCONNECT_REQ"))
				event->parameter = ZSAU_DISCONNECT_REQ;
			else {
				event->parameter = ZSAU_UNKNOWN;
				warn("%s: unknown parameter %s after ZSAU",
				     __func__, argv[curarg]);
			}
			++curarg;
			break;
		case RT_STRING:
			if (curarg < params) {
				len = strlen(argv[curarg]) + 1;
				event->ptr = kmalloc(len, GFP_ATOMIC);
				if (event->ptr)
					memcpy(event->ptr, argv[curarg], len);
				else
					err("no memory for string!");
				++curarg;
			}
#ifdef CONFIG_GIGASET_DEBUG
			if (!event->ptr)
				dbg(DEBUG_CMD, "string==NULL");
			else
				dbg(DEBUG_CMD,
				    "string==%s", (char *) event->ptr);
#endif
			break;
		case RT_ZCAU:
			event->parameter = -1;
			if (curarg + 1 < params) {
				i = isdn_gethex(argv[curarg]);
				j = isdn_gethex(argv[curarg + 1]);
				if (i >= 0 && i < 256 && j >= 0 && j < 256)
					event->parameter = (unsigned) i << 8
					                   | j;
				curarg += 2;
			} else
				curarg = params - 1;
			break;
		case RT_NUMBER:
		case RT_HEX:
			if (curarg < params) {
				if (param_type == RT_HEX)
					event->parameter =
						isdn_gethex(argv[curarg]);
				else
					event->parameter =
						isdn_getnum(argv[curarg]);
				++curarg;
			} else
				event->parameter = -1;
#ifdef CONFIG_GIGASET_DEBUG
			dbg(DEBUG_CMD, "parameter==%d", event->parameter);
#endif
			break;
		}

		if (resp_code == RSP_ZDLE)
			cs->dle = event->parameter;

		if (abort)
			break;
	}

	atomic_set(&cs->ev_tail, tail);
	spin_unlock_irqrestore(&cs->ev_lock, flags);

	if (curarg != params)
		dbg(DEBUG_ANY, "invalid number of processed parameters: %d/%d",
		    curarg, params);
}
EXPORT_SYMBOL_GPL(gigaset_handle_modem_response);

/* disconnect
 * process closing of connection associated with given AT state structure
 */
static void disconnect(struct at_state_t **at_state_p)
{
	unsigned long flags;
	struct bc_state *bcs;
	struct cardstate *cs;

	IFNULLRET(at_state_p);
	IFNULLRET(*at_state_p);
	bcs = (*at_state_p)->bcs;
	cs = (*at_state_p)->cs;
	IFNULLRET(cs);

	new_index(&(*at_state_p)->seq_index, MAX_SEQ_INDEX);

	/* revert to selected idle mode */
	if (!atomic_read(&cs->cidmode)) {
		cs->at_state.pending_commands |= PC_UMMODE;
		atomic_set(&cs->commands_pending, 1); //FIXME
		dbg(DEBUG_CMD, "Scheduling PC_UMMODE");
	}

	if (bcs) {
		/* B channel assigned: invoke hardware specific handler */
		cs->ops->close_bchannel(bcs);
	} else {
		/* no B channel assigned: just deallocate */
		spin_lock_irqsave(&cs->lock, flags);
		list_del(&(*at_state_p)->list);
		kfree(*at_state_p);
		*at_state_p = NULL;
		spin_unlock_irqrestore(&cs->lock, flags);
	}
}

/* get_free_channel
 * get a free AT state structure: either one of those associated with the
 * B channels of the Gigaset device, or if none of those is available,
 * a newly allocated one with bcs=NULL
 * The structure should be freed by calling disconnect() after use.
 */
static inline struct at_state_t *get_free_channel(struct cardstate *cs,
                                                  int cid)
/* cids: >0: siemens-cid
	  0: without cid
	 -1: no cid assigned yet
*/
{
	unsigned long flags;
	int i;
	struct at_state_t *ret;

	for (i = 0; i < cs->channels; ++i)
		if (gigaset_get_channel(cs->bcs + i)) {
			ret = &cs->bcs[i].at_state;
			ret->cid = cid;
			return ret;
		}

	spin_lock_irqsave(&cs->lock, flags);
	ret = kmalloc(sizeof(struct at_state_t), GFP_ATOMIC);
	if (ret) {
		gigaset_at_init(ret, NULL, cs, cid);
		list_add(&ret->list, &cs->temp_at_states);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	return ret;
}

static void init_failed(struct cardstate *cs, int mode)
{
	int i;
	struct at_state_t *at_state;

	cs->at_state.pending_commands &= ~PC_INIT;
	atomic_set(&cs->mode, mode);
	atomic_set(&cs->mstate, MS_UNINITIALIZED);
	gigaset_free_channels(cs);
	for (i = 0; i < cs->channels; ++i) {
		at_state = &cs->bcs[i].at_state;
		if (at_state->pending_commands & PC_CID) {
			at_state->pending_commands &= ~PC_CID;
			at_state->pending_commands |= PC_NOCID;
			atomic_set(&cs->commands_pending, 1);
		}
	}
}

static void schedule_init(struct cardstate *cs, int state)
{
	if (cs->at_state.pending_commands & PC_INIT) {
		dbg(DEBUG_CMD, "not scheduling PC_INIT again");
		return;
	}
	atomic_set(&cs->mstate, state);
	atomic_set(&cs->mode, M_UNKNOWN);
	gigaset_block_channels(cs);
	cs->at_state.pending_commands |= PC_INIT;
	atomic_set(&cs->commands_pending, 1);
	dbg(DEBUG_CMD, "Scheduling PC_INIT");
}

/* Add "AT" to a command, add the cid, dle encode it, send the result to the hardware. */
static void send_command(struct cardstate *cs, const char *cmd, int cid,
                         int dle, gfp_t kmallocflags)
{
	size_t cmdlen, buflen;
	char *cmdpos, *cmdbuf, *cmdtail;

	cmdlen = strlen(cmd);
	buflen = 11 + cmdlen;

	if (likely(buflen > cmdlen)) {
		cmdbuf = kmalloc(buflen, kmallocflags);
		if (likely(cmdbuf != NULL)) {
			cmdpos = cmdbuf + 9;
			cmdtail = cmdpos + cmdlen;
			memcpy(cmdpos, cmd, cmdlen);

			if (cid > 0 && cid <= 65535) {
				do {
					*--cmdpos = '0' + cid % 10;
					cid /= 10;
					++cmdlen;
				} while (cid);
			}

			cmdlen += 2;
			*--cmdpos = 'T';
			*--cmdpos = 'A';

			if (dle) {
				cmdlen += 4;
				*--cmdpos = '(';
				*--cmdpos = 0x10;
				*cmdtail++ = 0x10;
				*cmdtail++ = ')';
			}

			cs->ops->write_cmd(cs, cmdpos, cmdlen, NULL);
			kfree(cmdbuf);
		} else
			err("no memory for command buffer");
	} else
		err("overflow in buflen");
}

static struct at_state_t *at_state_from_cid(struct cardstate *cs, int cid)
{
	struct at_state_t *at_state;
	int i;
	unsigned long flags;

	if (cid == 0)
		return &cs->at_state;

	for (i = 0; i < cs->channels; ++i)
		if (cid == cs->bcs[i].at_state.cid)
			return &cs->bcs[i].at_state;

	spin_lock_irqsave(&cs->lock, flags);

	list_for_each_entry(at_state, &cs->temp_at_states, list)
		if (cid == at_state->cid) {
			spin_unlock_irqrestore(&cs->lock, flags);
			return at_state;
		}

	spin_unlock_irqrestore(&cs->lock, flags);

	return NULL;
}

static void bchannel_down(struct bc_state *bcs)
{
	IFNULLRET(bcs);
	IFNULLRET(bcs->cs);

	if (bcs->chstate & CHS_B_UP) {
		bcs->chstate &= ~CHS_B_UP;
		gigaset_i4l_channel_cmd(bcs, ISDN_STAT_BHUP);
	}

	if (bcs->chstate & (CHS_D_UP | CHS_NOTIFY_LL)) {
		bcs->chstate &= ~(CHS_D_UP | CHS_NOTIFY_LL);
		gigaset_i4l_channel_cmd(bcs, ISDN_STAT_DHUP);
	}

	gigaset_free_channel(bcs);

	gigaset_bcs_reinit(bcs);
}

static void bchannel_up(struct bc_state *bcs)
{
	IFNULLRET(bcs);

	if (!(bcs->chstate & CHS_D_UP)) {
		notice("%s: D channel not up", __func__);
		bcs->chstate |= CHS_D_UP;
		gigaset_i4l_channel_cmd(bcs, ISDN_STAT_DCONN);
	}

	if (bcs->chstate & CHS_B_UP) {
		notice("%s: B channel already up", __func__);
		return;
	}

	bcs->chstate |= CHS_B_UP;
	gigaset_i4l_channel_cmd(bcs, ISDN_STAT_BCONN);
}

static void start_dial(struct at_state_t *at_state, void *data, int seq_index)
{
	struct bc_state *bcs = at_state->bcs;
	struct cardstate *cs = at_state->cs;
	int retval;

	bcs->chstate |= CHS_NOTIFY_LL;
	//atomic_set(&bcs->status, BCS_INIT);

	if (atomic_read(&at_state->seq_index) != seq_index)
		goto error;

	retval = gigaset_isdn_setup_dial(at_state, data);
	if (retval != 0)
		goto error;


	at_state->pending_commands |= PC_CID;
	dbg(DEBUG_CMD, "Scheduling PC_CID");
//#ifdef GIG_MAYINITONDIAL
//	if (atomic_read(&cs->MState) == MS_UNKNOWN) {
//		cs->at_state.pending_commands |= PC_INIT;
//		dbg(DEBUG_CMD, "Scheduling PC_INIT");
//	}
//#endif
	atomic_set(&cs->commands_pending, 1); //FIXME
	return;

error:
	at_state->pending_commands |= PC_NOCID;
	dbg(DEBUG_CMD, "Scheduling PC_NOCID");
	atomic_set(&cs->commands_pending, 1); //FIXME
	return;
}

static void start_accept(struct at_state_t *at_state)
{
	struct cardstate *cs = at_state->cs;
	int retval;

	retval = gigaset_isdn_setup_accept(at_state);

	if (retval == 0) {
		at_state->pending_commands |= PC_ACCEPT;
		dbg(DEBUG_CMD, "Scheduling PC_ACCEPT");
		atomic_set(&cs->commands_pending, 1); //FIXME
	} else {
		//FIXME
		at_state->pending_commands |= PC_HUP;
		dbg(DEBUG_CMD, "Scheduling PC_HUP");
		atomic_set(&cs->commands_pending, 1); //FIXME
	}
}

static void do_start(struct cardstate *cs)
{
	gigaset_free_channels(cs);

	if (atomic_read(&cs->mstate) != MS_LOCKED)
		schedule_init(cs, MS_INIT);

	gigaset_i4l_cmd(cs, ISDN_STAT_RUN);
	                                // FIXME: not in locked mode
	                                // FIXME 2: only after init sequence

	cs->waiting = 0;
	wake_up(&cs->waitqueue);
}

static void finish_shutdown(struct cardstate *cs)
{
	if (atomic_read(&cs->mstate) != MS_LOCKED) {
		atomic_set(&cs->mstate, MS_UNINITIALIZED);
		atomic_set(&cs->mode, M_UNKNOWN);
	}

	/* The rest is done by cleanup_cs () in user mode. */

	cs->cmd_result = -ENODEV;
	cs->waiting = 0;
	wake_up_interruptible(&cs->waitqueue);
}

static void do_shutdown(struct cardstate *cs)
{
	gigaset_block_channels(cs);

	if (atomic_read(&cs->mstate) == MS_READY) {
		atomic_set(&cs->mstate, MS_SHUTDOWN);
		cs->at_state.pending_commands |= PC_SHUTDOWN;
		atomic_set(&cs->commands_pending, 1); //FIXME
		dbg(DEBUG_CMD, "Scheduling PC_SHUTDOWN"); //FIXME
		//gigaset_schedule_event(cs); //FIXME
	} else
		finish_shutdown(cs);
}

static void do_stop(struct cardstate *cs)
{
	do_shutdown(cs);
}

/* Entering cid mode or getting a cid failed:
 * try to initialize the device and try again.
 *
 * channel >= 0: getting cid for the channel failed
 * channel < 0:  entering cid mode failed
 *
 * returns 0 on failure
 */
static int reinit_and_retry(struct cardstate *cs, int channel)
{
	int i;

	if (--cs->retry_count <= 0)
		return 0;

	for (i = 0; i < cs->channels; ++i)
		if (cs->bcs[i].at_state.cid > 0)
			return 0;

	if (channel < 0)
		warn("Could not enter cid mode. Reinit device and try again.");
	else {
		warn("Could not get a call id. Reinit device and try again.");
		cs->bcs[channel].at_state.pending_commands |= PC_CID;
	}
	schedule_init(cs, MS_INIT);
	return 1;
}

static int at_state_invalid(struct cardstate *cs,
                            struct at_state_t *test_ptr)
{
	unsigned long flags;
	unsigned channel;
	struct at_state_t *at_state;
	int retval = 0;

	spin_lock_irqsave(&cs->lock, flags);

	if (test_ptr == &cs->at_state)
		goto exit;

	list_for_each_entry(at_state, &cs->temp_at_states, list)
		if (at_state == test_ptr)
			goto exit;

	for (channel = 0; channel < cs->channels; ++channel)
		if (&cs->bcs[channel].at_state == test_ptr)
			goto exit;

	retval = 1;
exit:
	spin_unlock_irqrestore(&cs->lock, flags);
	return retval;
}

static void handle_icall(struct cardstate *cs, struct bc_state *bcs,
			 struct at_state_t **p_at_state)
{
	int retval;
	struct at_state_t *at_state = *p_at_state;

	retval = gigaset_isdn_icall(at_state);
	switch (retval) {
	case ICALL_ACCEPT:
		break;
	default:
		err("internal error: disposition=%d", retval);
		/* --v-- fall through --v-- */
	case ICALL_IGNORE:
	case ICALL_REJECT:
		/* hang up actively
		 * Device doc says that would reject the call.
		 * In fact it doesn't.
		 */
		at_state->pending_commands |= PC_HUP;
		atomic_set(&cs->commands_pending, 1);
		break;
	}
}

static int do_lock(struct cardstate *cs)
{
	int mode;
	int i;

	switch (atomic_read(&cs->mstate)) {
	case MS_UNINITIALIZED:
	case MS_READY:
		if (cs->cur_at_seq || !list_empty(&cs->temp_at_states) ||
		    cs->at_state.pending_commands)
			return -EBUSY;

		for (i = 0; i < cs->channels; ++i)
			if (cs->bcs[i].at_state.pending_commands)
				return -EBUSY;

		if (!gigaset_get_channels(cs))
			return -EBUSY;

		break;
	case MS_LOCKED:
		//retval = -EACCES;
		break;
	default:
		return -EBUSY;
	}

	mode = atomic_read(&cs->mode);
	atomic_set(&cs->mstate, MS_LOCKED);
	atomic_set(&cs->mode, M_UNKNOWN);
	//FIXME reset card state / at states / bcs states

	return mode;
}

static int do_unlock(struct cardstate *cs)
{
	if (atomic_read(&cs->mstate) != MS_LOCKED)
		return -EINVAL;

	atomic_set(&cs->mstate, MS_UNINITIALIZED);
	atomic_set(&cs->mode, M_UNKNOWN);
	gigaset_free_channels(cs);
	//FIXME reset card state / at states / bcs states
	if (atomic_read(&cs->connected))
		schedule_init(cs, MS_INIT);

	return 0;
}

static void do_action(int action, struct cardstate *cs,
		      struct bc_state *bcs,
		      struct at_state_t **p_at_state, char **pp_command,
		      int *p_genresp, int *p_resp_code,
		      struct event_t *ev)
{
	struct at_state_t *at_state = *p_at_state;
	struct at_state_t *at_state2;
	unsigned long flags;

	int channel;

	unsigned char *s, *e;
	int i;
	unsigned long val;

	switch (action) {
	case ACT_NOTHING:
		break;
	case ACT_TIMEOUT:
		at_state->waiting = 1;
		break;
	case ACT_INIT:
		//FIXME setup everything
		cs->at_state.pending_commands &= ~PC_INIT;
		cs->cur_at_seq = SEQ_NONE;
		atomic_set(&cs->mode, M_UNIMODEM);
		if (!atomic_read(&cs->cidmode)) {
			gigaset_free_channels(cs);
			atomic_set(&cs->mstate, MS_READY);
			break;
		}
		cs->at_state.pending_commands |= PC_CIDMODE;
		atomic_set(&cs->commands_pending, 1); //FIXME
		dbg(DEBUG_CMD, "Scheduling PC_CIDMODE");
		break;
	case ACT_FAILINIT:
		warn("Could not initialize the device.");
		cs->dle = 0;
		init_failed(cs, M_UNKNOWN);
		cs->cur_at_seq = SEQ_NONE;
		break;
	case ACT_CONFIGMODE:
		init_failed(cs, M_CONFIG);
		cs->cur_at_seq = SEQ_NONE;
		break;
	case ACT_SETDLE1:
		cs->dle = 1;
		/* cs->inbuf[0].inputstate |= INS_command | INS_DLE_command; */
		cs->inbuf[0].inputstate &=
			~(INS_command | INS_DLE_command);
		break;
	case ACT_SETDLE0:
		cs->dle = 0;
		cs->inbuf[0].inputstate =
			(cs->inbuf[0].inputstate & ~INS_DLE_command)
			| INS_command;
		break;
	case ACT_CMODESET:
		if (atomic_read(&cs->mstate) == MS_INIT ||
		    atomic_read(&cs->mstate) == MS_RECOVER) {
			gigaset_free_channels(cs);
			atomic_set(&cs->mstate, MS_READY);
		}
		atomic_set(&cs->mode, M_CID);
		cs->cur_at_seq = SEQ_NONE;
		break;
	case ACT_UMODESET:
		atomic_set(&cs->mode, M_UNIMODEM);
		cs->cur_at_seq = SEQ_NONE;
		break;
	case ACT_FAILCMODE:
		cs->cur_at_seq = SEQ_NONE;
		if (atomic_read(&cs->mstate) == MS_INIT ||
		    atomic_read(&cs->mstate) == MS_RECOVER) {
			init_failed(cs, M_UNKNOWN);
			break;
		}
		if (!reinit_and_retry(cs, -1))
			schedule_init(cs, MS_RECOVER);
		break;
	case ACT_FAILUMODE:
		cs->cur_at_seq = SEQ_NONE;
		schedule_init(cs, MS_RECOVER);
		break;
	case ACT_HUPMODEM:
		/* send "+++" (hangup in unimodem mode) */
		cs->ops->write_cmd(cs, "+++", 3, NULL);
		break;
	case ACT_RING:
		/* get fresh AT state structure for new CID */
		at_state2 = get_free_channel(cs, ev->parameter);
		if (!at_state2) {
			warn("RING ignored: "
			     "could not allocate channel structure");
			break;
		}

		/* initialize AT state structure
		 * note that bcs may be NULL if no B channel is free
		 */
		at_state2->ConState = 700;
		kfree(at_state2->str_var[STR_NMBR]);
		at_state2->str_var[STR_NMBR] = NULL;
		kfree(at_state2->str_var[STR_ZCPN]);
		at_state2->str_var[STR_ZCPN] = NULL;
		kfree(at_state2->str_var[STR_ZBC]);
		at_state2->str_var[STR_ZBC] = NULL;
		kfree(at_state2->str_var[STR_ZHLC]);
		at_state2->str_var[STR_ZHLC] = NULL;
		at_state2->int_var[VAR_ZCTP] = -1;

		spin_lock_irqsave(&cs->lock, flags);
		at_state2->timer_expires = RING_TIMEOUT;
		at_state2->timer_active = 1;
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case ACT_ICALL:
		handle_icall(cs, bcs, p_at_state);
		at_state = *p_at_state;
		break;
	case ACT_FAILSDOWN:
		warn("Could not shut down the device.");
		/* fall through */
	case ACT_FAKESDOWN:
	case ACT_SDOWN:
		cs->cur_at_seq = SEQ_NONE;
		finish_shutdown(cs);
		break;
	case ACT_CONNECT:
		if (cs->onechannel) {
			at_state->pending_commands |= PC_DLE1;
			atomic_set(&cs->commands_pending, 1);
			break;
		}
		bcs->chstate |= CHS_D_UP;
		gigaset_i4l_channel_cmd(bcs, ISDN_STAT_DCONN);
		cs->ops->init_bchannel(bcs);
		break;
	case ACT_DLE1:
		cs->cur_at_seq = SEQ_NONE;
		bcs = cs->bcs + cs->curchannel;

		bcs->chstate |= CHS_D_UP;
		gigaset_i4l_channel_cmd(bcs, ISDN_STAT_DCONN);
		cs->ops->init_bchannel(bcs);
		break;
	case ACT_FAKEHUP:
		at_state->int_var[VAR_ZSAU] = ZSAU_NULL;
		/* fall through */
	case ACT_DISCONNECT:
		cs->cur_at_seq = SEQ_NONE;
		at_state->cid = -1;
		if (bcs && cs->onechannel && cs->dle) {
			/* Check for other open channels not needed:
			 * DLE only used for M10x with one B channel.
			 */
			at_state->pending_commands |= PC_DLE0;
			atomic_set(&cs->commands_pending, 1);
		} else {
			disconnect(p_at_state);
			at_state = *p_at_state;
		}
		break;
	case ACT_FAKEDLE0:
		at_state->int_var[VAR_ZDLE] = 0;
		cs->dle = 0;
		/* fall through */
	case ACT_DLE0:
		cs->cur_at_seq = SEQ_NONE;
		at_state2 = &cs->bcs[cs->curchannel].at_state;
		disconnect(&at_state2);
		break;
	case ACT_ABORTHUP:
		cs->cur_at_seq = SEQ_NONE;
		warn("Could not hang up.");
		at_state->cid = -1;
		if (bcs && cs->onechannel)
			at_state->pending_commands |= PC_DLE0;
		else {
			disconnect(p_at_state);
			at_state = *p_at_state;
		}
		schedule_init(cs, MS_RECOVER);
		break;
	case ACT_FAILDLE0:
		cs->cur_at_seq = SEQ_NONE;
		warn("Could not leave DLE mode.");
		at_state2 = &cs->bcs[cs->curchannel].at_state;
		disconnect(&at_state2);
		schedule_init(cs, MS_RECOVER);
		break;
	case ACT_FAILDLE1:
		cs->cur_at_seq = SEQ_NONE;
		warn("Could not enter DLE mode. Try to hang up.");
		channel = cs->curchannel;
		cs->bcs[channel].at_state.pending_commands |= PC_HUP;
		atomic_set(&cs->commands_pending, 1);
		break;

	case ACT_CID: /* got cid; start dialing */
		cs->cur_at_seq = SEQ_NONE;
		channel = cs->curchannel;
		if (ev->parameter > 0 && ev->parameter <= 65535) {
			cs->bcs[channel].at_state.cid = ev->parameter;
			cs->bcs[channel].at_state.pending_commands |=
				PC_DIAL;
			atomic_set(&cs->commands_pending, 1);
			break;
		}
		/* fall through */
	case ACT_FAILCID:
		cs->cur_at_seq = SEQ_NONE;
		channel = cs->curchannel;
		if (!reinit_and_retry(cs, channel)) {
			warn("Could not get a call id. Dialing not possible");
			at_state2 = &cs->bcs[channel].at_state;
			disconnect(&at_state2);
		}
		break;
	case ACT_ABORTCID:
		cs->cur_at_seq = SEQ_NONE;
		at_state2 = &cs->bcs[cs->curchannel].at_state;
		disconnect(&at_state2);
		break;

	case ACT_DIALING:
	case ACT_ACCEPTED:
		cs->cur_at_seq = SEQ_NONE;
		break;

	case ACT_ABORTACCEPT:	/* hangup/error/timeout during ICALL processing */
		disconnect(p_at_state);
		at_state = *p_at_state;
		break;

	case ACT_ABORTDIAL:	/* error/timeout during dial preparation */
		cs->cur_at_seq = SEQ_NONE;
		at_state->pending_commands |= PC_HUP;
		atomic_set(&cs->commands_pending, 1);
		break;

	case ACT_REMOTEREJECT:	/* DISCONNECT_IND after dialling */
	case ACT_CONNTIMEOUT:	/* timeout waiting for ZSAU=ACTIVE */
	case ACT_REMOTEHUP:	/* DISCONNECT_IND with established connection */
		at_state->pending_commands |= PC_HUP;
		atomic_set(&cs->commands_pending, 1);
		break;
	case ACT_GETSTRING: /* warning: RING, ZDLE, ... are not handled properly any more */
		at_state->getstring = 1;
		break;
	case ACT_SETVER:
		if (!ev->ptr) {
			*p_genresp = 1;
			*p_resp_code = RSP_ERROR;
			break;
		}
		s = ev->ptr;

		if (!strcmp(s, "OK")) {
			*p_genresp = 1;
			*p_resp_code = RSP_ERROR;
			break;
		}

		for (i = 0; i < 4; ++i) {
			val = simple_strtoul(s, (char **) &e, 10);
			if (val > INT_MAX || e == s)
				break;
			if (i == 3) {
				if (*e)
					break;
			} else if (*e != '.')
				break;
			else
				s = e + 1;
			cs->fwver[i] = val;
		}
		if (i != 4) {
			*p_genresp = 1;
			*p_resp_code = RSP_ERROR;
			break;
		}
		/*at_state->getstring = 1;*/
		cs->gotfwver = 0;
		break;
	case ACT_GOTVER:
		if (cs->gotfwver == 0) {
			cs->gotfwver = 1;
			dbg(DEBUG_ANY,
			    "firmware version %02d.%03d.%02d.%02d",
			    cs->fwver[0], cs->fwver[1],
			    cs->fwver[2], cs->fwver[3]);
			break;
		}
		/* fall through */
	case ACT_FAILVER:
		cs->gotfwver = -1;
		err("could not read firmware version.");
		break;
#ifdef CONFIG_GIGASET_DEBUG
	case ACT_ERROR:
		*p_genresp = 1;
		*p_resp_code = RSP_ERROR;
		break;
	case ACT_TEST:
		{
			static int count = 3; //2; //1;
			*p_genresp = 1;
			*p_resp_code = count ? RSP_ERROR : RSP_OK;
			if (count > 0)
				--count;
		}
		break;
#endif
	case ACT_DEBUG:
		dbg(DEBUG_ANY, "%s: resp_code %d in ConState %d",
			__func__, ev->type, at_state->ConState);
		break;
	case ACT_WARN:
		warn("%s: resp_code %d in ConState %d!",
			__func__, ev->type, at_state->ConState);
		break;
	case ACT_ZCAU:
		warn("cause code %04x in connection state %d.",
		     ev->parameter, at_state->ConState);
		break;

	/* events from the LL */
	case ACT_DIAL:
		start_dial(at_state, ev->ptr, ev->parameter);
		break;
	case ACT_ACCEPT:
		start_accept(at_state);
		break;
	case ACT_PROTO_L2:
		dbg(DEBUG_CMD,
		    "set protocol to %u", (unsigned) ev->parameter);
		at_state->bcs->proto2 = ev->parameter;
		break;
	case ACT_HUP:
		at_state->pending_commands |= PC_HUP;
		atomic_set(&cs->commands_pending, 1); //FIXME
		dbg(DEBUG_CMD, "Scheduling PC_HUP");
		break;

	/* hotplug events */
	case ACT_STOP:
		do_stop(cs);
		break;
	case ACT_START:
		do_start(cs);
		break;

	/* events from the interface */ // FIXME without ACT_xxxx?
	case ACT_IF_LOCK:
		cs->cmd_result = ev->parameter ? do_lock(cs) : do_unlock(cs);
		cs->waiting = 0;
		wake_up(&cs->waitqueue);
		break;
	case ACT_IF_VER:
		if (ev->parameter != 0)
			cs->cmd_result = -EINVAL;
		else if (cs->gotfwver != 1) {
			cs->cmd_result = -ENOENT;
		} else {
			memcpy(ev->arg, cs->fwver, sizeof cs->fwver);
			cs->cmd_result = 0;
		}
		cs->waiting = 0;
		wake_up(&cs->waitqueue);
		break;

	/* events from the proc file system */ // FIXME without ACT_xxxx?
	case ACT_PROC_CIDMODE:
		if (ev->parameter != atomic_read(&cs->cidmode)) {
			atomic_set(&cs->cidmode, ev->parameter);
			if (ev->parameter) {
				cs->at_state.pending_commands |= PC_CIDMODE;
				dbg(DEBUG_CMD, "Scheduling PC_CIDMODE");
			} else {
				cs->at_state.pending_commands |= PC_UMMODE;
				dbg(DEBUG_CMD, "Scheduling PC_UMMODE");
			}
			atomic_set(&cs->commands_pending, 1);
		}
		cs->waiting = 0;
		wake_up(&cs->waitqueue);
		break;

	/* events from the hardware drivers */
	case ACT_NOTIFY_BC_DOWN:
		bchannel_down(bcs);
		break;
	case ACT_NOTIFY_BC_UP:
		bchannel_up(bcs);
		break;
	case ACT_SHUTDOWN:
		do_shutdown(cs);
		break;


	default:
		if (action >= ACT_CMD && action < ACT_CMD + AT_NUM) {
			*pp_command = at_state->bcs->commands[action - ACT_CMD];
			if (!*pp_command) {
				*p_genresp = 1;
				*p_resp_code = RSP_NULL;
			}
		} else
			err("%s: action==%d!", __func__, action);
	}
}

/* State machine to do the calling and hangup procedure */
static void process_event(struct cardstate *cs, struct event_t *ev)
{
	struct bc_state *bcs;
	char *p_command = NULL;
	struct reply_t *rep;
	int rcode;
	int genresp = 0;
	int resp_code = RSP_ERROR;
	int sendcid;
	struct at_state_t *at_state;
	int index;
	int curact;
	unsigned long flags;

	IFNULLRET(cs);
	IFNULLRET(ev);

	if (ev->cid >= 0) {
		at_state = at_state_from_cid(cs, ev->cid);
		if (!at_state) {
			gigaset_add_event(cs, &cs->at_state, RSP_WRONG_CID,
			                  NULL, 0, NULL);
			return;
		}
	} else {
		at_state = ev->at_state;
		if (at_state_invalid(cs, at_state)) {
			dbg(DEBUG_ANY,
			    "event for invalid at_state %p", at_state);
			return;
		}
	}

	dbg(DEBUG_CMD,
	    "connection state %d, event %d", at_state->ConState, ev->type);

	bcs = at_state->bcs;
	sendcid = at_state->cid;

	/* Setting the pointer to the dial array */
	rep = at_state->replystruct;
	IFNULLRET(rep);

	if (ev->type == EV_TIMEOUT) {
		if (ev->parameter != atomic_read(&at_state->timer_index)
		    || !at_state->timer_active) {
			ev->type = RSP_NONE; /* old timeout */
			dbg(DEBUG_ANY, "old timeout");
		} else if (!at_state->waiting)
			dbg(DEBUG_ANY, "timeout occured");
		else
			dbg(DEBUG_ANY, "stopped waiting");
	}

	/* if the response belongs to a variable in at_state->int_var[VAR_XXXX] or at_state->str_var[STR_XXXX], set it */
	if (ev->type >= RSP_VAR && ev->type < RSP_VAR + VAR_NUM) {
		index = ev->type - RSP_VAR;
		at_state->int_var[index] = ev->parameter;
	} else if (ev->type >= RSP_STR && ev->type < RSP_STR + STR_NUM) {
		index = ev->type - RSP_STR;
		kfree(at_state->str_var[index]);
		at_state->str_var[index] = ev->ptr;
		ev->ptr = NULL; /* prevent process_events() from deallocating ptr */
	}

	if (ev->type == EV_TIMEOUT || ev->type == RSP_STRING)
		at_state->getstring = 0;

	/* Search row in dial array which matches modem response and current constate */
	for (;; rep++) {
		rcode = rep->resp_code;
		/* dbg (DEBUG_ANY, "rcode %d", rcode); */
		if (rcode == RSP_LAST) {
			/* found nothing...*/
			warn("%s: rcode=RSP_LAST: resp_code %d in ConState %d!",
				__func__, ev->type, at_state->ConState);
			return;
		}
		if ((rcode == RSP_ANY || rcode == ev->type)
		  && ((int) at_state->ConState >= rep->min_ConState)
		  && (rep->max_ConState < 0
		      || (int) at_state->ConState <= rep->max_ConState)
		  && (rep->parameter < 0 || rep->parameter == ev->parameter))
			break;
	}

	p_command = rep->command;

	at_state->waiting = 0;
	for (curact = 0; curact < MAXACT; ++curact) {
		/* The row tells us what we should do  ..
		 */
		do_action(rep->action[curact], cs, bcs, &at_state, &p_command, &genresp, &resp_code, ev);
		if (!at_state)
			break; /* may be freed after disconnect */
	}

	if (at_state) {
		/* Jump to the next con-state regarding the array */
		if (rep->new_ConState >= 0)
			at_state->ConState = rep->new_ConState;

		if (genresp) {
			spin_lock_irqsave(&cs->lock, flags);
			at_state->timer_expires = 0; //FIXME
			at_state->timer_active = 0; //FIXME
			spin_unlock_irqrestore(&cs->lock, flags);
			gigaset_add_event(cs, at_state, resp_code, NULL, 0, NULL);
		} else {
			/* Send command to modem if not NULL... */
			if (p_command/*rep->command*/) {
				if (atomic_read(&cs->connected))
					send_command(cs, p_command,
					             sendcid, cs->dle,
					             GFP_ATOMIC);
				else
					gigaset_add_event(cs, at_state,
					                  RSP_NODEV,
					                  NULL, 0, NULL);
			}

			spin_lock_irqsave(&cs->lock, flags);
			if (!rep->timeout) {
				at_state->timer_expires = 0;
				at_state->timer_active = 0;
			} else if (rep->timeout > 0) { /* new timeout */
				at_state->timer_expires = rep->timeout * 10;
				at_state->timer_active = 1;
				new_index(&at_state->timer_index,
				          MAX_TIMER_INDEX);
			}
			spin_unlock_irqrestore(&cs->lock, flags);
		}
	}
}

static void schedule_sequence(struct cardstate *cs,
			      struct at_state_t *at_state, int sequence)
{
	cs->cur_at_seq = sequence;
	gigaset_add_event(cs, at_state, RSP_INIT, NULL, sequence, NULL);
}

static void process_command_flags(struct cardstate *cs)
{
	struct at_state_t *at_state = NULL;
	struct bc_state *bcs;
	int i;
	int sequence;

	IFNULLRET(cs);

	atomic_set(&cs->commands_pending, 0);

	if (cs->cur_at_seq) {
		dbg(DEBUG_CMD, "not searching scheduled commands: busy");
		return;
	}

	dbg(DEBUG_CMD, "searching scheduled commands");

	sequence = SEQ_NONE;

	/* clear pending_commands and hangup channels on shutdown */
	if (cs->at_state.pending_commands & PC_SHUTDOWN) {
		cs->at_state.pending_commands &= ~PC_CIDMODE;
		for (i = 0; i < cs->channels; ++i) {
			bcs = cs->bcs + i;
			at_state = &bcs->at_state;
			at_state->pending_commands &=
				~(PC_DLE1 | PC_ACCEPT | PC_DIAL);
			if (at_state->cid > 0)
				at_state->pending_commands |= PC_HUP;
			if (at_state->pending_commands & PC_CID) {
				at_state->pending_commands |= PC_NOCID;
				at_state->pending_commands &= ~PC_CID;
			}
		}
	}

	/* clear pending_commands and hangup channels on reset */
	if (cs->at_state.pending_commands & PC_INIT) {
		cs->at_state.pending_commands &= ~PC_CIDMODE;
		for (i = 0; i < cs->channels; ++i) {
			bcs = cs->bcs + i;
			at_state = &bcs->at_state;
			at_state->pending_commands &=
				~(PC_DLE1 | PC_ACCEPT | PC_DIAL);
			if (at_state->cid > 0)
				at_state->pending_commands |= PC_HUP;
			if (atomic_read(&cs->mstate) == MS_RECOVER) {
				if (at_state->pending_commands & PC_CID) {
					at_state->pending_commands |= PC_NOCID;
					at_state->pending_commands &= ~PC_CID;
				}
			}
		}
	}

	/* only switch back to unimodem mode, if no commands are pending and no channels are up */
	if (cs->at_state.pending_commands == PC_UMMODE
	    && !atomic_read(&cs->cidmode)
	    && list_empty(&cs->temp_at_states)
	    && atomic_read(&cs->mode) == M_CID) {
		sequence = SEQ_UMMODE;
		at_state = &cs->at_state;
		for (i = 0; i < cs->channels; ++i) {
			bcs = cs->bcs + i;
			if (bcs->at_state.pending_commands ||
			    bcs->at_state.cid > 0) {
				sequence = SEQ_NONE;
				break;
			}
		}
	}
	cs->at_state.pending_commands &= ~PC_UMMODE;
	if (sequence != SEQ_NONE) {
		schedule_sequence(cs, at_state, sequence);
		return;
	}

	for (i = 0; i < cs->channels; ++i) {
		bcs = cs->bcs + i;
		if (bcs->at_state.pending_commands & PC_HUP) {
			bcs->at_state.pending_commands &= ~PC_HUP;
			if (bcs->at_state.pending_commands & PC_CID) {
				/* not yet dialing: PC_NOCID is sufficient */
				bcs->at_state.pending_commands |= PC_NOCID;
				bcs->at_state.pending_commands &= ~PC_CID;
			} else {
				schedule_sequence(cs, &bcs->at_state, SEQ_HUP);
				return;
			}
		}
		if (bcs->at_state.pending_commands & PC_NOCID) {
			bcs->at_state.pending_commands &= ~PC_NOCID;
			cs->curchannel = bcs->channel;
			schedule_sequence(cs, &cs->at_state, SEQ_NOCID);
			return;
		} else if (bcs->at_state.pending_commands & PC_DLE0) {
			bcs->at_state.pending_commands &= ~PC_DLE0;
			cs->curchannel = bcs->channel;
			schedule_sequence(cs, &cs->at_state, SEQ_DLE0);
			return;
		}
	}

	list_for_each_entry(at_state, &cs->temp_at_states, list)
		if (at_state->pending_commands & PC_HUP) {
			at_state->pending_commands &= ~PC_HUP;
			schedule_sequence(cs, at_state, SEQ_HUP);
			return;
		}

	if (cs->at_state.pending_commands & PC_INIT) {
		cs->at_state.pending_commands &= ~PC_INIT;
		cs->dle = 0; //FIXME
		cs->inbuf->inputstate = INS_command;
		//FIXME reset card state (or -> LOCK0)?
		schedule_sequence(cs, &cs->at_state, SEQ_INIT);
		return;
	}
	if (cs->at_state.pending_commands & PC_SHUTDOWN) {
		cs->at_state.pending_commands &= ~PC_SHUTDOWN;
		schedule_sequence(cs, &cs->at_state, SEQ_SHUTDOWN);
		return;
	}
	if (cs->at_state.pending_commands & PC_CIDMODE) {
		cs->at_state.pending_commands &= ~PC_CIDMODE;
		if (atomic_read(&cs->mode) == M_UNIMODEM) {
#if 0
			cs->retry_count = 2;
#else
			cs->retry_count = 1;
#endif
			schedule_sequence(cs, &cs->at_state, SEQ_CIDMODE);
			return;
		}
	}

	for (i = 0; i < cs->channels; ++i) {
		bcs = cs->bcs + i;
		if (bcs->at_state.pending_commands & PC_DLE1) {
			bcs->at_state.pending_commands &= ~PC_DLE1;
			cs->curchannel = bcs->channel;
			schedule_sequence(cs, &cs->at_state, SEQ_DLE1);
			return;
		}
		if (bcs->at_state.pending_commands & PC_ACCEPT) {
			bcs->at_state.pending_commands &= ~PC_ACCEPT;
			schedule_sequence(cs, &bcs->at_state, SEQ_ACCEPT);
			return;
		}
		if (bcs->at_state.pending_commands & PC_DIAL) {
			bcs->at_state.pending_commands &= ~PC_DIAL;
			schedule_sequence(cs, &bcs->at_state, SEQ_DIAL);
			return;
		}
		if (bcs->at_state.pending_commands & PC_CID) {
			switch (atomic_read(&cs->mode)) {
			case M_UNIMODEM:
				cs->at_state.pending_commands |= PC_CIDMODE;
				dbg(DEBUG_CMD, "Scheduling PC_CIDMODE");
				atomic_set(&cs->commands_pending, 1);
				return;
#ifdef GIG_MAYINITONDIAL
			case M_UNKNOWN:
				schedule_init(cs, MS_INIT);
				return;
#endif
			}
			bcs->at_state.pending_commands &= ~PC_CID;
			cs->curchannel = bcs->channel;
#ifdef GIG_RETRYCID
			cs->retry_count = 2;
#else
			cs->retry_count = 1;
#endif
			schedule_sequence(cs, &cs->at_state, SEQ_CID);
			return;
		}
	}
}

static void process_events(struct cardstate *cs)
{
	struct event_t *ev;
	unsigned head, tail;
	int i;
	int check_flags = 0;
	int was_busy;

	/* no locking needed (only one reader) */
	head = atomic_read(&cs->ev_head);

	for (i = 0; i < 2 * MAX_EVENTS; ++i) {
		tail = atomic_read(&cs->ev_tail);
		if (tail == head) {
			if (!check_flags && !atomic_read(&cs->commands_pending))
				break;
			check_flags = 0;
			process_command_flags(cs);
			tail = atomic_read(&cs->ev_tail);
			if (tail == head) {
				if (!atomic_read(&cs->commands_pending))
					break;
				continue;
			}
		}

		ev = cs->events + head;
		was_busy = cs->cur_at_seq != SEQ_NONE;
		process_event(cs, ev);
		kfree(ev->ptr);
		ev->ptr = NULL;
		if (was_busy && cs->cur_at_seq == SEQ_NONE)
			check_flags = 1;

		head = (head + 1) % MAX_EVENTS;
		atomic_set(&cs->ev_head, head);
	}

	if (i == 2 * MAX_EVENTS) {
		err("infinite loop in process_events; aborting.");
	}
}

/* tasklet scheduled on any event received from the Gigaset device
 * parameter:
 *	data	ISDN controller state structure
 */
void gigaset_handle_event(unsigned long data)
{
	struct cardstate *cs = (struct cardstate *) data;

	IFNULLRET(cs);
	IFNULLRET(cs->inbuf);

	/* handle incoming data on control/common channel */
	if (atomic_read(&cs->inbuf->head) != atomic_read(&cs->inbuf->tail)) {
		dbg(DEBUG_INTR, "processing new data");
		cs->ops->handle_input(cs->inbuf);
	}

	process_events(cs);
}
