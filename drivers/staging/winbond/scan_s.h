#ifndef __WINBOND_SCAN_S_H
#define __WINBOND_SCAN_S_H

#include <linux/types.h>
#include "localpara.h"

/*
 * SCAN task global CONSTANTS, STRUCTURES, variables
 */

/* define the msg type of SCAN module */
#define SCANMSG_SCAN_REQ		0x01
#define SCANMSG_BEACON			0x02
#define SCANMSG_PROBE_RESPONSE		0x03
#define SCANMSG_TIMEOUT			0x04
#define SCANMSG_TXPROBE_FAIL		0x05
#define SCANMSG_ENABLE_BGSCAN		0x06
#define SCANMSG_STOP_SCAN		0x07

/*
 * BSS Type =>conform to
 * IBSS             : ToDS/FromDS = 00
 * Infrastructure   : ToDS/FromDS = 01
 */
#define IBSS_NET			0
#define ESS_NET				1
#define ANYBSS_NET			2

/* Scan Type */
#define ACTIVE_SCAN			0
#define PASSIVE_SCAN			1

/*
 * ===========================================================
 *	scan.h
 *		Define the related definitions of scan module
 *
 * ===========================================================
 */

/* Define the state of scan module */
#define SCAN_INACTIVE			0
#define WAIT_PROBE_DELAY		1
#define WAIT_RESPONSE_MIN		2
#define WAIT_RESPONSE_MAX_ACTIVE	3
#define WAIT_BEACON_MAX_PASSIVE		4
#define SCAN_COMPLETE			5
#define BG_SCAN				6
#define BG_SCANNING			7


/*
 * The value will load from EEPROM
 * If 0xff is set in EEPOM, the driver will use SCAN_MAX_CHNL_TIME instead.
 * The definition is in WbHal.h
 */
#endif
