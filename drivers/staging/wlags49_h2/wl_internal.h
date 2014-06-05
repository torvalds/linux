/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   Header for definitions and macros internal to the drvier.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2003 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ******************************************************************************/

#ifndef __WAVELAN2_H__
#define __WAVELAN2_H__




/*******************************************************************************
 *  include files
 ******************************************************************************/
#ifdef BUS_PCMCIA
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#endif  // BUS_PCMCIA

#include <linux/wireless.h>
#include <net/iw_handler.h>

#include <linux/list.h>

#include <linux/interrupt.h>




/*******************************************************************************
 *  constant definitions
 ******************************************************************************/
#define p_u8    __u8
#define p_s8    __s8
#define p_u16   __u16
#define p_s16   __s16
#define p_u32   __u32
#define p_s32   __s32
#define p_char  char

#define MAX_KEY_LEN         (2 + (13 * 2)) // 0x plus 13 hex digit pairs
#define MB_SIZE             1024
#define MAX_ENC_LEN         104

#define MAX_SCAN_TIME_SEC   8
#define MAX_NAPS            32

#define CFG_MB_INFO         0x0820		//Mail Box Info Block

#define NUM_WDS_PORTS       6

#define WVLAN_MAX_LOOKAHEAD (HCF_MAX_MSG+46) /* as per s0005MIC_4.doc */


/* Min/Max/Default Parameter Values */
#if 0 //;? (HCF_TYPE) & HCF_TYPE_AP
//;? why this difference depending on compile option, seems to me it should depend on runtime if anything
#define PARM_DEFAULT_SSID                       "LinuxAP"
#else
#define PARM_DEFAULT_SSID                       "ANY"
#endif // HCF_TYPE_AP

#define PARM_MIN_NAME_LEN                       1
#define PARM_MAX_NAME_LEN                       32


/* The following definitions pertain to module and profile parameters */
// #define PARM_AP_MODE                            APMode
// #define PARM_NAME_AP_MODE						TEXT("APMode")
// #define PARM_DEFAULT_AP_MODE					FALSE

#define PARM_AUTHENTICATION                     Authentication
#define PARM_NAME_AUTHENTICATION				TEXT("Authentication")
#define PARM_MIN_AUTHENTICATION                 1
#define PARM_MAX_AUTHENTICATION                 2
#define PARM_DEFAULT_AUTHENTICATION             1

#define PARM_AUTH_KEY_MGMT_SUITE                AuthKeyMgmtSuite
#define PARM_NAME_AUTH_KEY_MGMT_SUITE           TEXT("AuthKeyMgmtSuite")
#define PARM_MIN_AUTH_KEY_MGMT_SUITE            0
#define PARM_MAX_AUTH_KEY_MGMT_SUITE            4
#define PARM_DEFAULT_AUTH_KEY_MGMT_SUITE        0

#define PARM_BRSC_2GHZ                          BRSC2GHz
#define PARM_NAME_BRSC_2GHZ                     TEXT("BRSC2GHz")
#define PARM_MIN_BRSC                           0x0000
#define PARM_MAX_BRSC                           0x0FFF
#define PARM_DEFAULT_BRSC_2GHZ                  0x000F

#define PARM_BRSC_5GHZ                          BRSC5GHz
#define PARM_NAME_BRSC_5GHZ                     TEXT("BRSC5GHz")
#define PARM_DEFAULT_BRSC_5GHZ                  0x0150

#define PARM_COEXISTENCE                        Coexistence
#define PARM_NAME_COEXISTENCE                   TEXT("Coexistence")
#define PARM_MIN_COEXISTENCE                    0x0000
#define PARM_MAX_COEXISTENCE                    0x0007
#define PARM_DEFAULT_COEXISTENCE                0x0000

#define PARM_CONFIGURED                         Configured
#define PARM_NAME_CONFIGURED					TEXT("Configured")

#define PARM_CONNECTION_CONTROL                 ConnectionControl
#define PARM_NAME_CONNECTION_CONTROL            TEXT("ConnectionControl")
#define PARM_MIN_CONNECTION_CONTROL             0
#define PARM_MAX_CONNECTION_CONTROL             3
#define PARM_DEFAULT_CONNECTION_CONTROL         2

#define PARM_CREATE_IBSS                        CreateIBSS
#define PARM_NAME_CREATE_IBSS                   TEXT("CreateIBSS")
#define PARM_DEFAULT_CREATE_IBSS                FALSE
#define PARM_DEFAULT_CREATE_IBSS_STR            "N"

#define PARM_DEBUG_FLAG             	    	DebugFlag
#define PARM_NAME_DEBUG_FLAG            		TEXT("DebugFlag")
#define PARM_MIN_DEBUG_FLAG             		0
#define PARM_MAX_DEBUG_FLAG             		0xFFFF
#define PARM_DEFAULT_DEBUG_FLAG         		0xFFFF

#define PARM_DESIRED_SSID                       DesiredSSID
#define PARM_NAME_DESIRED_SSID                  TEXT("DesiredSSID")

#define PARM_DOWNLOAD_FIRMWARE                  DownloadFirmware
#define PARM_NAME_DOWNLOAD_FIRMWARE             TEXT("DownloadFirmware")

#define PARM_DRIVER_ENABLE                      DriverEnable
#define PARM_NAME_DRIVER_ENABLE					TEXT("DriverEnable")
#define PARM_DEFAULT_DRIVER_ENABLE				TRUE

#define PARM_ENABLE_ENCRYPTION                  EnableEncryption
#define PARM_NAME_ENABLE_ENCRYPTION             TEXT("EnableEncryption")
#define PARM_MIN_ENABLE_ENCRYPTION              0
#define PARM_MAX_ENABLE_ENCRYPTION              7
#define PARM_DEFAULT_ENABLE_ENCRYPTION          0

#define PARM_ENCRYPTION                         Encryption
#define PARM_NAME_ENCRYPTION                    TEXT("Encryption")

#define PARM_EXCLUDE_UNENCRYPTED                ExcludeUnencrypted
#define PARM_NAME_EXCLUDE_UNENCRYPTED           TEXT("ExcludeUnencrypted")
#define PARM_DEFAULT_EXCLUDE_UNENCRYPTED        TRUE
#define PARM_DEFAULT_EXCLUDE_UNENCRYPTED_STR    "N"

#define PARM_INTRA_BSS_RELAY                    IntraBSSRelay
#define PARM_NAME_INTRA_BSS_RELAY               TEXT("IntraBSSRelay")
#define PARM_DEFAULT_INTRA_BSS_RELAY            TRUE
#define PARM_DEFAULT_INTRA_BSS_RELAY_STR        "Y"

#define PARM_KEY1                               Key1
#define PARM_NAME_KEY1                          TEXT("Key1")
#define PARM_KEY2                               Key2
#define PARM_NAME_KEY2                          TEXT("Key2")
#define PARM_KEY3                               Key3
#define PARM_NAME_KEY3                          TEXT("Key3")
#define PARM_KEY4                               Key4
#define PARM_NAME_KEY4                          TEXT("Key4")

//;? #define PARM_KEY_FORMAT                         AsciiHex
//;? #define PARM_NAME_KEY_FORMAT                    TEXT("AsciiHex")

#define PARM_LOAD_BALANCING                     LoadBalancing
#define PARM_NAME_LOAD_BALANCING                TEXT("LoadBalancing")
#define PARM_DEFAULT_LOAD_BALANCING             TRUE
#define PARM_DEFAULT_LOAD_BALANCING_STR         "Y"

#define PARM_MAX_DATA_LENGTH                    MaxDataLength
#define PARM_NAME_MAX_DATA_LENGTH				TEXT("MaxDataLength")

#define PARM_MAX_SLEEP                          MaxSleepDuration
#define PARM_NAME_MAX_SLEEP                     TEXT("MaxSleepDuration")
#define PARM_MIN_MAX_PM_SLEEP                   1								//;?names nearly right?
#define PARM_MAX_MAX_PM_SLEEP                   65535
#define PARM_DEFAULT_MAX_PM_SLEEP               100

#define PARM_MEDIUM_DISTRIBUTION                MediumDistribution
#define PARM_NAME_MEDIUM_DISTRIBUTION           TEXT("MediumDistribution")
#define PARM_DEFAULT_MEDIUM_DISTRIBUTION        TRUE
#define PARM_DEFAULT_MEDIUM_DISTRIBUTION_STR    "Y"

#define PARM_MICROWAVE_ROBUSTNESS               MicroWaveRobustness
#define PARM_NAME_MICROWAVE_ROBUSTNESS          TEXT("MicroWaveRobustness")
#define PARM_DEFAULT_MICROWAVE_ROBUSTNESS       FALSE
#define PARM_DEFAULT_MICROWAVE_ROBUSTNESS_STR   "N"

#define PARM_MULTICAST_PM_BUFFERING             MulticastPMBuffering
#define PARM_NAME_MULTICAST_PM_BUFFERING	    TEXT("MulticastPMBuffering")
#define PARM_DEFAULT_MULTICAST_PM_BUFFERING     TRUE
#define PARM_DEFAULT_MULTICAST_PM_BUFFERING_STR "Y"

#define PARM_MULTICAST_RATE                     MulticastRate
#define PARM_NAME_MULTICAST_RATE                TEXT("MulticastRate")
#ifdef WARP
#define PARM_MIN_MULTICAST_RATE                 0x0001
#define PARM_MAX_MULTICAST_RATE                 0x0fff
#define PARM_DEFAULT_MULTICAST_RATE_2GHZ        0x0004
#define PARM_DEFAULT_MULTICAST_RATE_5GHZ        0x0010
#else
#define PARM_MIN_MULTICAST_RATE                 0x0001
#define PARM_MAX_MULTICAST_RATE                 0x0004
#define PARM_DEFAULT_MULTICAST_RATE_2GHZ        0x0002
#define PARM_DEFAULT_MULTICAST_RATE_5GHZ        0x0000
#endif  // WARP

#define PARM_MULTICAST_RX                       MulticastReceive
#define PARM_NAME_MULTICAST_RX                  TEXT("MulticastReceive")
#define PARM_DEFAULT_MULTICAST_RX               TRUE
#define PARM_DEFAULT_MULTICAST_RX_STR           "Y"

#define PARM_NETWORK_ADDR                       NetworkAddress
#define PARM_NAME_NETWORK_ADDR                  TEXT("NetworkAddress")
#define PARM_DEFAULT_NETWORK_ADDR               { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

#define PARM_NETWORK_TYPE                       NetworkType
#define PARM_NAME_NETWORK_TYPE					TEXT("NetworkType")
#define PARM_DEFAULT_NETWORK_TYPE   			0

#define PARM_OWN_ATIM_WINDOW                    OwnATIMWindow
#define PARM_NAME_OWN_ATIM_WINDOW				TEXT("OwnATIMWindow")
#define PARM_MIN_OWN_ATIM_WINDOW                0
#define PARM_MAX_OWN_ATIM_WINDOW                100
#define PARM_DEFAULT_OWN_ATIM_WINDOW            0

#define PARM_OWN_BEACON_INTERVAL                OwnBeaconInterval
#define PARM_NAME_OWN_BEACON_INTERVAL           TEXT("OwnBeaconInterval")
#define PARM_MIN_OWN_BEACON_INTERVAL            20
#define PARM_MAX_OWN_BEACON_INTERVAL            200
#define PARM_DEFAULT_OWN_BEACON_INTERVAL        100

#define PARM_OWN_CHANNEL                        OwnChannel
#define PARM_NAME_OWN_CHANNEL                   TEXT("OwnChannel")
#define PARM_MIN_OWN_CHANNEL                    1
#define PARM_MAX_OWN_CHANNEL                    161
#define PARM_DEFAULT_OWN_CHANNEL                10

#define PARM_OWN_DTIM_PERIOD                    OwnDTIMPeriod
#define PARM_NAME_OWN_DTIM_PERIOD	            TEXT("OwnDTIMPeriod")
#define PARM_MIN_OWN_DTIM_PERIOD                1
#define PARM_MAX_OWN_DTIM_PERIOD                65535
#define PARM_DEFAULT_OWN_DTIM_PERIOD            1

#define PARM_OWN_NAME                           OwnName
#define PARM_NAME_OWN_NAME                      TEXT("OwnName")
#define PARM_DEFAULT_OWN_NAME                   "Linux"

#define PARM_OWN_SSID                           OwnSSID
#define PARM_NAME_OWN_SSID                      TEXT("OwnSSID")

#define PARM_PM_ENABLED                         PMEnabled
#define PARM_NAME_PM_ENABLED                    TEXT("PMEnabled")
#define PARM_MAX_PM_ENABLED						3

#define PARM_PMEPS                              PMEPS
#define PARM_NAME_PMEPS                         TEXT("PMEPS")

#define PARM_PM_HOLDOVER_DURATION               PMHoldoverDuration
#define PARM_NAME_PM_HOLDOVER_DURATION          TEXT("PMHoldoverDuration")
#define PARM_MIN_PM_HOLDOVER_DURATION           1
#define PARM_MAX_PM_HOLDOVER_DURATION           1000
#define PARM_DEFAULT_PM_HOLDOVER_DURATION       100

#define PARM_PM_MODE                            PowerMode
#define PARM_NAME_PM_MODE                       TEXT("PowerMode")

#define PARM_PORT_TYPE                          PortType
#define PARM_NAME_PORT_TYPE                     TEXT("PortType")
#define PARM_MIN_PORT_TYPE                      1
#define PARM_MAX_PORT_TYPE                      3
#define PARM_DEFAULT_PORT_TYPE                  1

#define PARM_PROMISCUOUS_MODE                   PromiscuousMode
#define PARM_NAME_PROMISCUOUS_MODE              TEXT("PromiscuousMode")
#define PARM_DEFAULT_PROMISCUOUS_MODE           FALSE
#define PARM_DEFAULT_PROMISCUOUS_MODE_STR       "N"

#define PARM_REJECT_ANY                         RejectANY
#define PARM_NAME_REJECT_ANY				    TEXT("RejectANY")
#define PARM_DEFAULT_REJECT_ANY                 FALSE
#define PARM_DEFAULT_REJECT_ANY_STR             "N"

#define PARM_RTS_THRESHOLD                      RTSThreshold
#define PARM_NAME_RTS_THRESHOLD                 TEXT("RTSThreshold")
#define PARM_MIN_RTS_THRESHOLD                  0
#define PARM_MAX_RTS_THRESHOLD                  2347
#define PARM_DEFAULT_RTS_THRESHOLD              2347

#define PARM_RTS_THRESHOLD1                     RTSThreshold1
#define PARM_NAME_RTS_THRESHOLD1                TEXT("RTSThreshold1")
#define PARM_RTS_THRESHOLD2                     RTSThreshold2
#define PARM_NAME_RTS_THRESHOLD2                TEXT("RTSThreshold2")
#define PARM_RTS_THRESHOLD3                     RTSThreshold3
#define PARM_NAME_RTS_THRESHOLD3                TEXT("RTSThreshold3")
#define PARM_RTS_THRESHOLD4                     RTSThreshold4
#define PARM_NAME_RTS_THRESHOLD4                TEXT("RTSThreshold4")
#define PARM_RTS_THRESHOLD5                     RTSThreshold5
#define PARM_NAME_RTS_THRESHOLD5                TEXT("RTSThreshold5")
#define PARM_RTS_THRESHOLD6                     RTSThreshold6
#define PARM_NAME_RTS_THRESHOLD6                TEXT("RTSThreshold6")

#define PARM_SRSC_2GHZ                          SRSC2GHz
#define PARM_NAME_SRSC_2GHZ                     TEXT("SRSC2GHz")
#define PARM_MIN_SRSC                           0x0000
#define PARM_MAX_SRSC                           0x0FFF
#define PARM_DEFAULT_SRSC_2GHZ                  0x0FFF

#define PARM_SRSC_5GHZ                          SRSC5GHz
#define PARM_NAME_SRSC_5GHZ                     TEXT("SRSC5GHz")
#define PARM_DEFAULT_SRSC_5GHZ                  0x0FF0

#define PARM_SYSTEM_SCALE                       SystemScale
#define PARM_NAME_SYSTEM_SCALE                  TEXT("SystemScale")
#define PARM_MIN_SYSTEM_SCALE                   1
#define PARM_MAX_SYSTEM_SCALE                   5
#define PARM_DEFAULT_SYSTEM_SCALE               1

#define PARM_TX_KEY                             TxKey
#define PARM_NAME_TX_KEY                        TEXT("TxKey")
#define PARM_MIN_TX_KEY                         1
#define PARM_MAX_TX_KEY                         4
#define PARM_DEFAULT_TX_KEY                     1

#define PARM_TX_POW_LEVEL                       TxPowLevel
#define PARM_NAME_TX_POW_LEVEL                  TEXT("TxPowLevel")
#define PARM_MIN_TX_POW_LEVEL                   1   // 20 dBm
#define PARM_MAX_TX_POW_LEVEL                   6   //  8 dBm
#define PARM_DEFAULT_TX_POW_LEVEL               3   // 15 dBm

#define PARM_TX_RATE                            TxRateControl
#define PARM_NAME_TX_RATE                       TEXT("TxRateControl")
#define PARM_MIN_TX_RATE                        0x0001
#ifdef WARP
#define PARM_MAX_TX_RATE                        0x0FFF
#define PARM_DEFAULT_TX_RATE_2GHZ               0x0FFF
#define PARM_DEFAULT_TX_RATE_5GHZ               0x0FF0
#else
#define PARM_MAX_TX_RATE                        0x0007
#define PARM_DEFAULT_TX_RATE_2GHZ               0x0003
#define PARM_DEFAULT_TX_RATE_5GHZ               0x0000
#endif  // WARP

#define PARM_TX_RATE1                           TxRateControl1
#define PARM_NAME_TX_RATE1                      TEXT("TxRateControl1")
#define PARM_TX_RATE2                           TxRateControl2
#define PARM_NAME_TX_RATE2                      TEXT("TxRateControl2")
#define PARM_TX_RATE3                           TxRateControl3
#define PARM_NAME_TX_RATE3                      TEXT("TxRateControl3")
#define PARM_TX_RATE4                           TxRateControl4
#define PARM_NAME_TX_RATE4                      TEXT("TxRateControl4")
#define PARM_TX_RATE5                           TxRateControl5
#define PARM_NAME_TX_RATE5                      TEXT("TxRateControl5")
#define PARM_TX_RATE6                           TxRateControl6
#define PARM_NAME_TX_RATE6                      TEXT("TxRateControl6")

#define PARM_VENDORDESCRIPTION                  VendorDescription
#define PARM_NAME_VENDORDESCRIPTION				TEXT("VendorDescription")

#define PARM_WDS_ADDRESS                        WDSAddress
#define PARM_NAME_WDS_ADDRESS					TEXT("WDSAddress")

#define PARM_WDS_ADDRESS1                       WDSAddress1
#define PARM_NAME_WDS_ADDRESS1					TEXT("WDSAddress1")
#define PARM_WDS_ADDRESS2                       WDSAddress2
#define PARM_NAME_WDS_ADDRESS2					TEXT("WDSAddress2")
#define PARM_WDS_ADDRESS3                       WDSAddress3
#define PARM_NAME_WDS_ADDRESS3					TEXT("WDSAddress3")
#define PARM_WDS_ADDRESS4                       WDSAddress4
#define PARM_NAME_WDS_ADDRESS4					TEXT("WDSAddress4")
#define PARM_WDS_ADDRESS5                       WDSAddress5
#define PARM_NAME_WDS_ADDRESS5					TEXT("WDSAddress5")
#define PARM_WDS_ADDRESS6                       WDSAddress6
#define PARM_NAME_WDS_ADDRESS6					TEXT("WDSAddress6")

/*
#define PARM_LONG_RETRY_LIMIT                   LongRetryLimit
#define PARM_NAME_LONG_RETRY_LIMIT              TEXT("LongRetryLimit")
#define PARM_MIN_LONG_RETRY_LIMIT               1
#define PARM_MAX_LONG_RETRY_LIMIT               15
#define PARM_DEFAULT_LONG_RETRY_LIMIT           3


#define PARM_PROBE_DATA_RATES                   ProbeDataRates
#define PARM_NAME_PROBE_DATA_RATES              TEXT("ProbeDataRates")
#define PARM_MIN_PROBE_DATA_RATES               0x0000
#define PARM_MAX_PROBE_DATA_RATES               0x0FFF
#define PARM_DEFAULT_PROBE_DATA_RATES_2GHZ      0x0002
#define PARM_DEFAULT_PROBE_DATA_RATES_5GHZ      0x0010

#define PARM_SHORT_RETRY_LIMIT                  ShortRetryLimit
#define PARM_NAME_SHORT_RETRY_LIMIT             TEXT("ShortRetryLimit")
#define PARM_MIN_SHORT_RETRY_LIMIT              1
#define PARM_MAX_SHORT_RETRY_LIMIT              15
#define PARM_DEFAULT_SHORT_RETRY_LIMIT          7


*/

/*******************************************************************************
 *  state definitions
 ******************************************************************************/
/* The following constants are used to track state the device */
#define WL_FRIMWARE_PRESENT     1 // Download if needed
#define WL_FRIMWARE_NOT_PRESENT	0 // Skip over download, assume its already there
#define WL_HANDLING_INT         1 // Actually call the HCF to switch interrupts on/off
#define WL_NOT_HANDLING_INT     0 // Not yet handling interrupts, do not switch on/off

/*******************************************************************************
 *  macro definitions
 ******************************************************************************/
/* The following macro ensures that no symbols are exported, minimizing the
   chance of a symbol collision in the kernel */
// EXPORT_NO_SYMBOLS;

#define NELEM(arr) (sizeof(arr) / sizeof(arr[0]))

#define WVLAN_VALID_MAC_ADDRESS( x ) \
((x[0]!=0xFF) && (x[1]!=0xFF) && (x[2]!=0xFF) && (x[3]!=0xFF) && (x[4]!=0xFF) && (x[5]!=0xFF))




/*******************************************************************************
 * type definitions
 ******************************************************************************/
#undef FALSE
#undef TRUE

typedef enum
{
	FALSE = 0,
	TRUE  = 1
}
bool_t;


typedef struct _ScanResult
{
	//hcf_16        len;
	//hcf_16        typ;
	int             scan_complete;
	int             num_aps;
	SCAN_RS_STRCT   APTable [MAX_NAPS];
}
ScanResult;


typedef struct _LINK_STATUS_STRCT
{
	hcf_16  len;
	hcf_16  typ;
	hcf_16  linkStatus;     /* 1..5 */
}
LINK_STATUS_STRCT;


typedef struct _ASSOC_STATUS_STRCT
{
	hcf_16  len;
	hcf_16  typ;
	hcf_16  assocStatus;            /* 1..3 */
	hcf_8   staAddr[ETH_ALEN];
	hcf_8   oldApAddr[ETH_ALEN];
}
ASSOC_STATUS_STRCT;


typedef struct _SECURITY_STATUS_STRCT
{
	hcf_16  len;
	hcf_16  typ;
	hcf_16  securityStatus;     /* 1..3 */
	hcf_8   staAddr[ETH_ALEN];
	hcf_16  reason;
}
SECURITY_STATUS_STRCT;

#define WVLAN_WMP_PDU_TYPE_LT_REQ       0x00
#define WVLAN_WMP_PDU_TYPE_LT_RSP       0x01
#define WVLAN_WMP_PDU_TYPE_APL_REQ      0x02
#define WVLAN_WMP_PDU_TYPE_APL_RSP      0x03

typedef struct wvlan_eth_hdr
{
	unsigned char   dst[ETH_ALEN];           /* Destination address. */
	unsigned char   src[ETH_ALEN];           /* Source address. */
	unsigned short  len;                    /* Length of the PDU. */
}
WVLAN_ETH_HDR, *PWVLAN_ETH_HDR;

typedef struct wvlan_llc_snap
{
	unsigned char   dsap;                   /* DSAP (0xAA) */
	unsigned char   ssap;                   /* SSAP (0xAA) */
	unsigned char   ctrl;                   /* Control (0x03) */
	unsigned char   oui[3];                 /* Organization Unique ID (00-60-1d). */
	unsigned char   specid[2];              /* Specific ID code (00-01). */
}
WVLAN_LLC_SNAP, *PWVLAN_LLC_SNAP;


typedef struct wvlan_lt_hdr
{
	unsigned char   version;                /* Version (0x00) */
	unsigned char   type;                   /* PDU type: 0-req/1-resp. */
	unsigned short  id;                     /* Identifier to associate resp to req. */
}
WVLAN_LT_HDR, *PWVLAN_LT_HDR;


typedef struct wvlan_wmp_hdr
{
	unsigned char   version;                /* Version  */
	unsigned char   type;                   /* PDU type */
}
WVLAN_WMP_HDR, *PWVLAN_WMP_HDR;


#define FILLER_SIZE             1554
#define TEST_PATTERN_SIZE       54


typedef struct wvlan_lt_req
{
	unsigned char   Filler[TEST_PATTERN_SIZE];   /* minimal length of 54 bytes */
}
WVLAN_LT_REQ, *PWVLAN_LT_REQ;


typedef struct wvlan_lt_rsp
{
	char           name[32];
	/* Measured Data */
	unsigned char  signal;
	unsigned char  noise;
	unsigned char  rxFlow;
	unsigned char  dataRate;
	unsigned short protocol;
	/* Capabilities */
	unsigned char  station;
	unsigned char  dataRateCap;
	unsigned char  powerMgmt[4];
	unsigned char  robustness[4];
	unsigned char  scaling;
	unsigned char  reserved[5];
}
WVLAN_LT_RSP, *PWVLAN_LT_RSP;


typedef struct wvlan_rx_wmp_hdr
{
	unsigned short status;
	unsigned short reserved1[2];
	unsigned char  silence;
	unsigned char  signal;
	unsigned char  rate;
	unsigned char  rxFlow;
	unsigned short reserved2[2];
	unsigned short frameControl;
	unsigned short duration;
	unsigned short address1[3];
	unsigned short address2[3];
	unsigned short address3[3];
	unsigned short sequenceControl;
	unsigned short address4[3];
#ifndef HERMES25	//;?just to be on the safe side of inherited but not comprehended code #ifdef HERMES2
	unsigned short seems_to_be_unused_reserved3[5];  //;?
	unsigned short seems_to_be_unused_reserved4;	 //;?
#endif // HERMES25
	unsigned short HeaderDataLen;
}
WVLAN_RX_WMP_HDR, *PWVLAN_RX_WMP_HDR;


typedef struct wvlan_linktest_req_pdu
{
	WVLAN_ETH_HDR     ethHdr;
	WVLAN_LLC_SNAP    llcSnap;
	WVLAN_LT_HDR      ltHdr;
	WVLAN_LT_REQ      ltReq;
}
WVLAN_LINKTEST_REQ_PDU, *PWVLAN_LINKTEST_REQ_PDU;


typedef struct wvlan_linktest_rsp_pdu
{
	WVLAN_RX_WMP_HDR  wmpRxHdr;
	WVLAN_ETH_HDR     ethHdr;
	WVLAN_LLC_SNAP    llcSnap;
	WVLAN_LT_HDR      ltHdr;
	WVLAN_LT_RSP      ltRsp;
}
WVLAN_LINKTEST_RSP_PDU, *PWVLAN_LINKTEST_RSP_PDU;


typedef struct _LINKTEST_RSP_STRCT
{
	hcf_16                   len;
	hcf_16                   typ;
	WVLAN_LINKTEST_RSP_PDU   ltRsp;
}
LINKTEST_RSP_STRCT;


typedef struct wvlan_wmp_rsp_pdu
{
	WVLAN_RX_WMP_HDR  wmpRxHdr;
	WVLAN_ETH_HDR     ethHdr;
	WVLAN_LLC_SNAP    llcSnap;
	WVLAN_WMP_HDR     wmpHdr;
}
WVLAN_WMP_RSP_PDU, *PWVLAN_WMP_RSP_PDU;


typedef struct _WMP_RSP_STRCT
{
	hcf_16              len;
	hcf_16              typ;
	WVLAN_WMP_RSP_PDU   wmpRsp;
}
WMP_RSP_STRCT;


typedef struct _PROBE_RESP
{
	// first part: 802.11
	hcf_16	length;
	hcf_16	infoType;
	hcf_16	reserved0;
	//hcf_8	signal;
	hcf_8	silence;
	hcf_8	signal;     // Moved signal here as signal/noise values were flipped
	hcf_8	rxFlow;
	hcf_8	rate;
	hcf_16	reserved1[2];

	// second part:
	hcf_16	frameControl;
	hcf_16	durID;
	hcf_8	address1[6];
	hcf_8	address2[6];
	hcf_8	BSSID[6];					//! this is correct, right ?
	hcf_16	sequence;
	hcf_8	address4[6];

#ifndef WARP
	hcf_8	reserved2[12];
#endif // WARP

	hcf_16	dataLength;
										// the information in the next 3 fields (DA/SA/LenType) is actually not filled in.
	hcf_8	DA[6];
	hcf_8	SA[6];

#ifdef WARP
	hcf_8   channel;
	hcf_8   band;
#else
	hcf_16	lenType;
#endif  // WARP

	hcf_8	timeStamp[8];
	hcf_16	beaconInterval;
	hcf_16	capability;
	hcf_8	rawData[200];				//! <<< think about this number !
	hcf_16	flags;
}
PROBE_RESP, *PPROBE_RESP;


typedef struct _ProbeResult
{
	int         scan_complete;
	int         num_aps;
	PROBE_RESP  ProbeTable[MAX_NAPS];
}
ProbeResult;

/* Definitions used to parse capabilities out of the probe responses */
#define CAPABILITY_ESS      0x0001
#define CAPABILITY_IBSS     0x0002
#define CAPABILITY_PRIVACY  0x0010

/* Definitions used to parse the Information Elements out of probe responses */
#define DS_INFO_ELEM                        0x03
#define GENERIC_INFO_ELEM                   0xdd
#define WPA_MAX_IE_LEN                      40
#define WPA_SELECTOR_LEN                    4
#define WPA_OUI_TYPE                        { 0x00, 0x50, 0xf2, 1 }
#define WPA_VERSION                         1
#define WPA_AUTH_KEY_MGMT_UNSPEC_802_1X     { 0x00, 0x50, 0xf2, 1 }
#define WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X   { 0x00, 0x50, 0xf2, 2 }
#define WPA_CIPHER_SUITE_NONE               { 0x00, 0x50, 0xf2, 0 }
#define WPA_CIPHER_SUITE_WEP40              { 0x00, 0x50, 0xf2, 1 }
#define WPA_CIPHER_SUITE_TKIP               { 0x00, 0x50, 0xf2, 2 }
#define WPA_CIPHER_SUITE_WRAP               { 0x00, 0x50, 0xf2, 3 }
#define WPA_CIPHER_SUITE_CCMP               { 0x00, 0x50, 0xf2, 4 }
#define WPA_CIPHER_SUITE_WEP104             { 0x00, 0x50, 0xf2, 5 }

typedef enum wvlan_drv_mode
{
	WVLAN_DRV_MODE_NO_DOWNLOAD,     /* this is the same as STA for Hermes 1    */
				                    /* it is also only applicable for Hermes 1 */
	WVLAN_DRV_MODE_STA,
	WVLAN_DRV_MODE_AP,
	WVLAN_DRV_MODE_MAX
}
WVLAN_DRV_MODE, *PWVLAN_DRV_MODE;


typedef enum wvlan_port_state
{
	WVLAN_PORT_STATE_ENABLED,
	WVLAN_PORT_STATE_DISABLED,
	WVLAN_PORT_STATE_CONNECTED
}
WVLAN_PORT_STATE, *PWVLAN_PORT_STATE;

/*
typedef enum wvlan_connect_state
{
	WVLAN_CONNECT_STATE_CONNECTED,
	WVLAN_CONNECT_STATE_DISCONNECTED
}
WVLAN_CONNECT_STATE, *PWVLAN_CONNECT_STATE;
*/

typedef enum wvlan_pm_state
{
	WVLAN_PM_STATE_DISABLED,
	WVLAN_PM_STATE_ENHANCED,
	WVLAN_PM_STATE_STANDARD
}
WVLAN_PM_STATE, *PWVLAN_PM_STATE;


typedef struct wvlan_frame
{
	struct sk_buff  *skb;       /* sk_buff for frame. */
	hcf_16          port;       /* MAC port for the frame. */
	hcf_16          len;        /* Length of the frame. */
}
WVLAN_FRAME, *PWVLAN_FRAME;


typedef struct wvlan_lframe
{
	struct list_head    node;   /* Node in the list */
	WVLAN_FRAME	        frame;  /* Frame. */
}
WVLAN_LFRAME, *PWVLAN_LFRAME;



#define DEFAULT_NUM_TX_FRAMES           48
#define TX_Q_LOW_WATER_MARK             (DEFAULT_NUM_TX_FRAMES/3)

#define WVLAN_MAX_TX_QUEUES             1


#ifdef USE_WDS

typedef struct wvlan_wds_if
{
	struct net_device           *dev;
	int                         is_registered;
	int                         netif_queue_on;
	struct net_device_stats     stats;
	hcf_16                      rtsThreshold;
	hcf_16                      txRateCntl;
	hcf_8                       wdsAddress[ETH_ALEN];
} WVLAN_WDS_IF, *PWVLAN_WDS_IF;

#endif  // USE_WDS



#define NUM_RX_DESC 5
#define NUM_TX_DESC 5

typedef struct dma_strct
{
	DESC_STRCT  *tx_packet[NUM_TX_DESC];
	DESC_STRCT  *rx_packet[NUM_RX_DESC];
	DESC_STRCT  *rx_reclaim_desc, *tx_reclaim_desc; // Descriptors for host-reclaim purposes (see HCF)
	int         tx_rsc_ind; // DMA Tx resource indicator is maintained in the MSF, not in the HCF
	int         rx_rsc_ind; // Also added rx resource indicator so that cleanup can be performed if alloc fails
	int         status;
} DMA_STRCT;


/* Macros used in DMA support */
/* get bus address of {rx,tx}dma structure member, in little-endian byte order */
#define WL_DMA_BUS_ADDR_LE(str, i, mem) \
	cpu_to_le32(str##_dma_addr[(i)] + ((hcf_8 *)&str[(i)]->mem - (hcf_8 *)str[(i)]))


struct wl_private
{

#ifdef BUS_PCMCIA
	struct pcmcia_device	    *link;
#endif // BUS_PCMCIA


	struct net_device           *dev;
//	struct net_device           *dev_next;
	spinlock_t                  slock;
	struct tasklet_struct       task;
	struct net_device_stats     stats;


#ifdef WIRELESS_EXT
	struct iw_statistics        wstats;
//	int                         spy_number;
//	u_char                      spy_address[IW_MAX_SPY][ETH_ALEN];
//	struct iw_quality           spy_stat[IW_MAX_SPY];
	struct iw_spy_data          spy_data;
	struct iw_public_data	wireless_data;
#endif // WIRELESS_EXT


	IFB_STRCT                   hcfCtx;
//;? struct timer_list			timer_oor;
//;? hcf_16						timer_oor_cnt;
	u_long						wlags49_type;		//controls output in /proc/wlags49
	u_long                      flags;
	hcf_16						DebugFlag;
	int                         is_registered;
	int                         is_handling_int;
	int                         firmware_present;
	CFG_DRV_INFO_STRCT          driverInfo;
	CFG_IDENTITY_STRCT          driverIdentity;
	CFG_FW_IDENTITY_STRCT       StationIdentity;
	CFG_PRI_IDENTITY_STRCT      PrimaryIdentity;
	CFG_PRI_IDENTITY_STRCT      NICIdentity;

	ltv_t                       ltvRecord;
	u_long                      txBytes;
	hcf_16                      maxPort;        /* 0 for STA, 6 for AP */

	/* Elements used for async notification from hardware */
	RID_LOG_STRCT				RidList[10];
	ltv_t                       updatedRecord;
	PROBE_RESP				    ProbeResp;
	ASSOC_STATUS_STRCT          assoc_stat;
	SECURITY_STATUS_STRCT       sec_stat;

	u_char                      lookAheadBuf[WVLAN_MAX_LOOKAHEAD];

	hcf_8                       PortType;           // 1 - 3 (1 [Normal] | 3 [AdHoc])
	hcf_16                      Channel;            // 0 - 14 (0)
	hcf_16                      TxRateControl[2];
	hcf_8                       DistanceBetweenAPs; // 1 - 3 (1)
	hcf_16                      RTSThreshold;       // 0 - 2347 (2347)
	hcf_16                      PMEnabled;          // 0 - 2, 8001 - 8002 (0)
	hcf_8                       MicrowaveRobustness;// 0 - 1 (0)
	hcf_8                       CreateIBSS;         // 0 - 1 (0)
	hcf_8                       MulticastReceive;   // 0 - 1 (1)
	hcf_16                      MaxSleepDuration;   // 0 - 65535 (100)
	hcf_8                       MACAddress[ETH_ALEN];
	char                        NetworkName[HCF_MAX_NAME_LEN+1];
	char                        StationName[HCF_MAX_NAME_LEN+1];
	hcf_8                       EnableEncryption;   // 0 - 1 (0)
	char                        Key1[MAX_KEY_LEN+1];
	char                        Key2[MAX_KEY_LEN+1];
	char                        Key3[MAX_KEY_LEN+1];
	char                        Key4[MAX_KEY_LEN+1];
	hcf_8                       TransmitKeyID;      // 1 - 4 (1)
	CFG_DEFAULT_KEYS_STRCT	    DefaultKeys;
	u_char                      mailbox[MB_SIZE];
	char                        szEncryption[MAX_ENC_LEN];

	hcf_16                      driverEnable;
	hcf_16                      wolasEnable;
	hcf_16                      atimWindow;
	hcf_16                      holdoverDuration;
	hcf_16                      MulticastRate[2];

	hcf_16                      authentication; // is this AP specific?
	hcf_16                      promiscuousMode;
	WVLAN_DRV_MODE              DownloadFirmware;   // 0 - 2 (0 [None] | 1 [STA] | 2 [AP])

	char						fw_image_filename[MAX_LINE_SIZE+1];

	hcf_16                      AuthKeyMgmtSuite;

	hcf_16                      loadBalancing;
	hcf_16                      mediumDistribution;
	hcf_16                      txPowLevel;
	//hcf_16                      shortRetryLimit;
	//hcf_16                      longRetryLimit;
	hcf_16                      srsc[2];
	hcf_16                      brsc[2];
	hcf_16                      connectionControl;
	//hcf_16                      probeDataRates[2];
	hcf_16                      ownBeaconInterval;
	hcf_16                      coexistence;

	WVLAN_FRAME                 txF;
	WVLAN_LFRAME                txList[DEFAULT_NUM_TX_FRAMES];
	struct list_head            txFree;
	struct list_head            txQ[WVLAN_MAX_TX_QUEUES];
	int                         netif_queue_on;
	int                         txQ_count;
	DESC_STRCT                  desc_rx;
	DESC_STRCT                  desc_tx;

	WVLAN_PORT_STATE            portState;

	ScanResult                  scan_results;
	ProbeResult                 probe_results;
	int                         probe_num_aps;

	int                         use_dma;
	DMA_STRCT                   dma;
#ifdef USE_RTS
	int                         useRTS;
#endif  // USE_RTS
	hcf_8                       DTIMPeriod;         // 1 - 255 (1)
	hcf_16                      multicastPMBuffering;
	hcf_8                       RejectAny;          // 0 - 1 (0)
	hcf_8                       ExcludeUnencrypted; // 0 - 1 (1)
	hcf_16                      intraBSSRelay;
#ifdef USE_WDS
	WVLAN_WDS_IF                wds_port[NUM_WDS_PORTS];
#endif // USE_WDS

	/* Track whether the card is using WEP encryption or WPA
	 * so we know what to disable next time through.
	 *  IW_ENCODE_ALG_NONE, IW_ENCODE_ALG_WEP, IW_ENCODE_ALG_TKIP
	 */
	int wext_enc;
}; // wl_private

#define wl_priv(dev) ((struct wl_private *) netdev_priv(dev))

/********************************************************************/
/* Locking and synchronization functions                            */
/********************************************************************/

/* These functions *must* be inline or they will break horribly on
 * SPARC, due to its weird semantics for save/restore flags. extern
 * inline should prevent the kernel from linking or module from
 * loading if they are not inlined. */
static inline void wl_lock(struct wl_private *lp,
	                       unsigned long *flags)
{
	spin_lock_irqsave(&lp->slock, *flags);
}

static inline void wl_unlock(struct wl_private *lp,
	                          unsigned long *flags)
{
	spin_unlock_irqrestore(&lp->slock, *flags);
}

/********************************************************************/
/* Interrupt enable disable functions                               */
/********************************************************************/

static inline void wl_act_int_on(struct wl_private *lp)
{
	/*
	 * Only do something when the driver is handling
	 * interrupts. Handling starts at wl_open and
	 * ends at wl_close when not in RTS mode
	 */
	if(lp->is_handling_int == WL_HANDLING_INT) {
		hcf_action( &lp->hcfCtx, HCF_ACT_INT_ON );
	}
}

static inline void wl_act_int_off(struct wl_private *lp)
{
	/*
	 * Only do something when the driver is handling
	 * interrupts. Handling starts at wl_open and
	 * ends at wl_close when not in RTS mode
	 */
	if(lp->is_handling_int == WL_HANDLING_INT) {
		hcf_action( &lp->hcfCtx, HCF_ACT_INT_OFF );
	}
}

#endif  // __WAVELAN2_H__
