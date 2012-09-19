/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 *****************************************************************************
 *
 * FILE: csr_wifi_hip_signals.h
 *
 * PURPOSE:
 *      Header file wrapping the auto-generated code in csr_wifi_hip_sigs.h
 *      and csr_wifi_hip_signals.c -
 *      csr_wifi_hip_sigs.h provides structures defining UniFi signals and
 *      csr_wifi_hip_signals.c provides SigGetSize() and SigGetDataRefs().
 *
 *****************************************************************************
 */
#ifndef __CSR_WIFI_HIP_SIGNALS_H__
#define __CSR_WIFI_HIP_SIGNALS_H__

#include <linux/types.h>
#include "csr_wifi_hip_sigs.h"


/****************************************************************************/
/* INFORMATION ELEMENTS */
/****************************************************************************/

/* Information Element ID's - shouldn't be in here, but nowhere better yet */
#define IE_SSID_ID                       0
#define IE_SUPPORTED_RATES_ID            1
#define IE_FH_PARAM_SET_ID               2
#define IE_DS_PARAM_SET_ID               3
#define IE_CF_PARAM_SET_ID               4
#define IE_TIM_ID                        5
#define IE_IBSS_PARAM_SET_ID             6
#define IE_COUNTRY_ID                    7
#define IE_HOPPING_PATTERN_PARAMS_ID     8
#define IE_HOPPING_PATTERN_TABLE_ID      9
#define IE_REQUEST_ID                    10
#define IE_QBSS_LOAD_ID                  11
#define IE_EDCA_PARAM_SET_ID             12
#define IE_TRAFFIC_SPEC_ID               13
#define IE_TRAFFIC_CLASS_ID              14
#define IE_SCHEDULE_ID                   15
#define IE_CHALLENGE_TEXT_ID             16
#define IE_POWER_CONSTRAINT_ID           32
#define IE_POWER_CAPABILITY_ID           33
#define IE_TPC_REQUEST_ID                34
#define IE_TPC_REPORT_ID                 35
#define IE_SUPPORTED_CHANNELS_ID         36
#define IE_CHANNEL_SWITCH_ANNOUNCE_ID    37
#define IE_MEASUREMENT_REQUEST_ID        38
#define IE_MEASUREMENT_REPORT_ID         39
#define IE_QUIET_ID                      40
#define IE_IBSS_DFS_ID                   41
#define IE_ERP_INFO_ID                   42
#define IE_TS_DELAY_ID                   43
#define IE_TCLAS_PROCESSING_ID           44
#define IE_QOS_CAPABILITY_ID             46
#define IE_RSN_ID                        48
#define IE_EXTENDED_SUPPORTED_RATES_ID   50
#define IE_AP_CHANNEL_REPORT_ID          52
#define IE_RCPI_ID                       53
#define IE_WPA_ID                       221


/* The maximum number of data references in a signal structure */
#define UNIFI_MAX_DATA_REFERENCES 2

/* The space to allow for a wire-format signal structure */
#define UNIFI_PACKED_SIGBUF_SIZE   64


/******************************************************************************/
/* SIGNAL PARAMETER VALUES */
/******************************************************************************/

/* ifIndex */
#define UNIFI_IF_2G4 1
#define UNIFI_IF_5G  2

/* SendProcessId */
#define HOST_PROC_ID 0xc000

#define SIG_CAP_ESS             0x0001
#define SIG_CAP_IBSS            0x0002
#define SIG_CAP_CF_POLLABLE     0x0004
#define SIG_CAP_CF_POLL_REQUEST 0x0008
#define SIG_CAP_PRIVACY         0x0010
#define SIG_CAP_SHORT_PREAMBLE  0x0020
#define SIG_CAP_DSSSOFDM        0x2000

/******************************************************************************/
/* FUNCTION DECLARATIONS */
/******************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/******************************************************************************
 * SigGetNumDataRefs - Retrieve pointers to data-refs from a signal.
 *
 * PARAMETERS:
 *   aSignal  - Pointer to signal to retrieve the data refs of.
 *   aDataRef - Address of a pointer to the structure that the data refs
 *              pointers will be stored.
 *
 * RETURNS:
 *   The number of data-refs in the signal.
 */
s32 SigGetDataRefs(CSR_SIGNAL *aSignal, CSR_DATAREF **aDataRef);

/******************************************************************************
 * SigGetSize - Retrieve the size (in bytes) of a given signal.
 *
 * PARAMETERS:
 *   aSignal  - Pointer to signal to retrieve size of.
 *
 * RETURNS:
 *   The size (in bytes) of the given signal.
 */
s32 SigGetSize(const CSR_SIGNAL *aSignal);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __CSR_WIFI_HIP_SIGNALS_H__ */
