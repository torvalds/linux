/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_wifi_hip_signals.h"
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"


/*
 * ---------------------------------------------------------------------------
 *  get_packed_struct_size
 *
 *      Examine a buffer containing a UniFi signal in wire-format.
 *      The first two bytes contain the signal ID, decode the signal ID and
 *      return the size, in  bytes, of the signal, not including any bulk
 *      data.
 *
 *      WARNING: This function is auto-generated, DO NOT EDIT!
 *
 *  Arguments:
 *      buf     Pointer to buffer to decode.
 *
 *  Returns:
 *      0 if the signal ID is not recognised (i.e. zero length),
 *      otherwise the number of bytes occupied by the signal in the buffer.
 *      This is useful for stepping past the signal to the object in the buffer.
 * ---------------------------------------------------------------------------
 */
s32 get_packed_struct_size(const u8 *buf)
{
    s32 size = 0;
    u16 sig_id;

    sig_id = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(buf);

    size += SIZEOF_UINT16;
    size += SIZEOF_UINT16;
    size += SIZEOF_UINT16;
    switch (sig_id)
    {
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_PACKET_FILTER_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SETKEYS_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONFIG_QUEUE_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_AUTONOMOUS_SCAN_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_BLACKOUT_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_BLACKOUT_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_KEY_SEQUENCE_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SM_START_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_AGGREGATION_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TSPEC_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_WORD16_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
        case CSR_DEBUG_GENERIC_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
        case CSR_MA_PACKET_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT64;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
        case CSR_MLME_SET_TIM_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECTED_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_RX_TRIGGER_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_TRIGGERED_GET_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DELETEKEYS_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_NEXT_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CHANNEL_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_START_AGGREGATION_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            break;
#endif
        case CSR_DEBUG_GENERIC_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_LEAVE_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TRIGGERED_GET_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_MULTICAST_ADDRESS_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_RESET_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_CANCEL_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TRIGGERED_GET_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_PACKET_FILTER_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_RX_TRIGGER_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECT_STATUS_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_LEAVE_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONFIG_QUEUE_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TSPEC_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_MLME_SET_TIM_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_BLACKOUT_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TRIGGERED_GET_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_GENERIC_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
        case CSR_MA_PACKET_CANCEL_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MODIFY_BSS_PARAMETER_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_PAUSE_AUTONOMOUS_SCAN_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MODIFY_BSS_PARAMETER_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_RX_TRIGGER_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_VIF_AVAILABILITY_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CANCEL_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_AUTONOMOUS_SCAN_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_BLACKOUT_ENDED_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_AUTONOMOUS_SCAN_DONE_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_KEY_SEQUENCE_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CHANNEL_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TRIGGERED_GET_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_AUTONOMOUS_SCAN_LOSS_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            break;
#endif
        case CSR_MA_VIF_AVAILABILITY_RESPONSE_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TEMPLATE_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_POWERMGT_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_PERIODIC_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_NEXT_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_AGGREGATION_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_RX_TRIGGER_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_BLACKOUT_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT32;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DELETEKEYS_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_RESET_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_AUTONOMOUS_SCAN_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SM_START_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECT_STATUS_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_AUTONOMOUS_SCAN_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_PERIODIC_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SETKEYS_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 32 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_PAUSE_AUTONOMOUS_SCAN_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_POWERMGT_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_ERROR_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_PERIODIC_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TSPEC_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT32;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_MULTICAST_ADDRESS_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TSPEC_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CANCEL_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_STRING_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TEMPLATE_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_BLOCKACK_ERROR_INDICATION_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_START_AGGREGATION_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += 48 / 8;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_MEASURE_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT32;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_PERIODIC_CONFIRM_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_MEASURE_REQUEST_ID:
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            size += SIZEOF_UINT16;
            break;
#endif
        default:
            size = 0;
    }
    return size;
} /* get_packed_struct_size() */


/*
 * ---------------------------------------------------------------------------
 *  read_unpack_signal
 *
 *      Unpack a wire-format signal into a host-native structure.
 *      This function handles any necessary conversions for endianness and
 *      places no restrictions on packing or alignment for the structure
 *      definition.
 *
 *      WARNING: This function is auto-generated, DO NOT EDIT!
 *
 *  Arguments:
 *      ptr             Signal buffer to unpack.
 *      sig             Pointer to destination structure to populate.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success,
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE if the ID of signal was not recognised.
 * ---------------------------------------------------------------------------
 */
CsrResult read_unpack_signal(const u8 *ptr, CSR_SIGNAL *sig)
{
    s32 index = 0;

    sig->SignalPrimitiveHeader.SignalId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
    index += SIZEOF_UINT16;

    sig->SignalPrimitiveHeader.ReceiverProcessId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
    index += SIZEOF_UINT16;

    sig->SignalPrimitiveHeader.SenderProcessId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
    index += SIZEOF_UINT16;

    switch (sig->SignalPrimitiveHeader.SignalId)
    {
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_PACKET_FILTER_CONFIRM_ID:
            sig->u.MlmeSetPacketFilterConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SETKEYS_CONFIRM_ID:
            sig->u.MlmeSetkeysConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONFIG_QUEUE_CONFIRM_ID:
            sig->u.MlmeConfigQueueConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_AUTONOMOUS_SCAN_CONFIRM_ID:
            sig->u.MlmeAddAutonomousScanConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanConfirm.AutonomousScanId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_BLACKOUT_CONFIRM_ID:
            sig->u.MlmeAddBlackoutConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutConfirm.BlackoutId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_BLACKOUT_REQUEST_ID:
            sig->u.MlmeDelBlackoutRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutRequest.BlackoutId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_KEY_SEQUENCE_CONFIRM_ID:
            sig->u.MlmeGetKeySequenceConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[0] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[1] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[2] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[3] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[4] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[5] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[6] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[7] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SM_START_CONFIRM_ID:
            sig->u.MlmeSmStartConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_AGGREGATION_CONFIRM_ID:
            sig->u.MlmeStopAggregationConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeStopAggregationConfirm.PeerQstaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeStopAggregationConfirm.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationConfirm.Direction = (CSR_DIRECTION) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TSPEC_REQUEST_ID:
            sig->u.MlmeDelTspecRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecRequest.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecRequest.Direction = (CSR_DIRECTION) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_WORD16_INDICATION_ID:
            sig->u.DebugWord16Indication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[0] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[1] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[2] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[3] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[4] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[5] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[6] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[7] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[8] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[9] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[10] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[11] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[12] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[13] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[14] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugWord16Indication.DebugWords[15] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_DEBUG_GENERIC_CONFIRM_ID:
            sig->u.DebugGenericConfirm.DebugVariable.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugVariable.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[0] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[1] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[2] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[3] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[4] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[5] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[6] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericConfirm.DebugWords[7] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_MA_PACKET_INDICATION_ID:
            sig->u.MaPacketIndication.Data.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.Data.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MaPacketIndication.LocalTime.x, &ptr[index], 64 / 8);
            index += 64 / 8;
            sig->u.MaPacketIndication.Ifindex = (CSR_IFINTERFACE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.Channel = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.ReceptionStatus = (CSR_RECEPTION_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.Rssi = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.Snr = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketIndication.ReceivedRate = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_MLME_SET_TIM_REQUEST_ID:
            sig->u.MlmeSetTimRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimRequest.AssociationId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimRequest.TimValue = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECTED_INDICATION_ID:
            sig->u.MlmeConnectedIndication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectedIndication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectedIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectedIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectedIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectedIndication.ConnectionStatus = (CSR_CONNECTION_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeConnectedIndication.PeerMacAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_RX_TRIGGER_REQUEST_ID:
            sig->u.MlmeDelRxTriggerRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerRequest.TriggerId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_TRIGGERED_GET_INDICATION_ID:
            sig->u.MlmeTriggeredGetIndication.MibAttributeValue.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeTriggeredGetIndication.MibAttributeValue.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeTriggeredGetIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeTriggeredGetIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeTriggeredGetIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeTriggeredGetIndication.Status = (CSR_MIB_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeTriggeredGetIndication.ErrorIndex = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeTriggeredGetIndication.TriggeredId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_REQUEST_ID:
            sig->u.MlmeScanRequest.ChannelList.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.ChannelList.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.InformationElements.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.InformationElements.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.Ifindex = (CSR_IFINTERFACE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.ScanType = (CSR_SCAN_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.ProbeDelay = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MlmeScanRequest.MinChannelTime = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanRequest.MaxChannelTime = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DELETEKEYS_CONFIRM_ID:
            sig->u.MlmeDeletekeysConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_NEXT_REQUEST_ID:
            sig->u.MlmeGetNextRequest.MibAttribute.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextRequest.MibAttribute.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CHANNEL_CONFIRM_ID:
            sig->u.MlmeSetChannelConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_START_AGGREGATION_REQUEST_ID:
            sig->u.MlmeStartAggregationRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeStartAggregationRequest.PeerQstaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeStartAggregationRequest.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.Direction = (CSR_DIRECTION) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.StartingSequenceNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.BufferSize = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationRequest.BlockAckTimeout = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_REQUEST_ID:
            sig->u.MlmeHlSyncRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeHlSyncRequest.GroupAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            break;
#endif
        case CSR_DEBUG_GENERIC_REQUEST_ID:
            sig->u.DebugGenericRequest.DebugVariable.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugVariable.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[0] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[1] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[2] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[3] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[4] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[5] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[6] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericRequest.DebugWords[7] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_LEAVE_CONFIRM_ID:
            sig->u.MlmeLeaveConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TRIGGERED_GET_REQUEST_ID:
            sig->u.MlmeDelTriggeredGetRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetRequest.TriggeredId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_MULTICAST_ADDRESS_REQUEST_ID:
            sig->u.MlmeAddMulticastAddressRequest.Data.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressRequest.Data.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressRequest.NumberOfMulticastGroupAddresses = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_RESET_REQUEST_ID:
            sig->u.MlmeResetRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeResetRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeResetRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeResetRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeResetRequest.StaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeResetRequest.SetDefaultMib = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_CANCEL_REQUEST_ID:
            sig->u.MlmeScanCancelRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanCancelRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanCancelRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanCancelRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanCancelRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TRIGGERED_GET_CONFIRM_ID:
            sig->u.MlmeAddTriggeredGetConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetConfirm.TriggeredId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_PACKET_FILTER_REQUEST_ID:
            sig->u.MlmeSetPacketFilterRequest.InformationElements.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterRequest.InformationElements.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterRequest.PacketFilterMode = (CSR_PACKET_FILTER_MODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetPacketFilterRequest.ArpFilterAddress = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_RX_TRIGGER_CONFIRM_ID:
            sig->u.MlmeDelRxTriggerConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerConfirm.TriggerId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelRxTriggerConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECT_STATUS_REQUEST_ID:
            sig->u.MlmeConnectStatusRequest.InformationElements.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusRequest.InformationElements.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusRequest.ConnectionStatus = (CSR_CONNECTION_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeConnectStatusRequest.StaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeConnectStatusRequest.AssociationId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusRequest.AssociationCapabilityInformation = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_LEAVE_REQUEST_ID:
            sig->u.MlmeLeaveRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeLeaveRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONFIG_QUEUE_REQUEST_ID:
            sig->u.MlmeConfigQueueRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.QueueIndex = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.Aifs = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.Cwmin = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.Cwmax = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConfigQueueRequest.TxopLimit = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TSPEC_CONFIRM_ID:
            sig->u.MlmeDelTspecConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecConfirm.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTspecConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MLME_SET_TIM_CONFIRM_ID:
            sig->u.MlmeSetTimConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetTimConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_INDICATION_ID:
            sig->u.MlmeMeasureIndication.MeasurementReportSet.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureIndication.MeasurementReportSet.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureIndication.DialogToken = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_BLACKOUT_CONFIRM_ID:
            sig->u.MlmeDelBlackoutConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutConfirm.BlackoutId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelBlackoutConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TRIGGERED_GET_CONFIRM_ID:
            sig->u.MlmeDelTriggeredGetConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelTriggeredGetConfirm.TriggeredId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_GENERIC_INDICATION_ID:
            sig->u.DebugGenericIndication.DebugVariable.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugVariable.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[0] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[1] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[2] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[3] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[4] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[5] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[6] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugGenericIndication.DebugWords[7] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_MA_PACKET_CANCEL_REQUEST_ID:
            sig->u.MaPacketCancelRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketCancelRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketCancelRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketCancelRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketCancelRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketCancelRequest.HostTag = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MODIFY_BSS_PARAMETER_CONFIRM_ID:
            sig->u.MlmeModifyBssParameterConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_PAUSE_AUTONOMOUS_SCAN_CONFIRM_ID:
            sig->u.MlmePauseAutonomousScanConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanConfirm.AutonomousScanId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_REQUEST_ID:
            sig->u.MaPacketRequest.Data.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketRequest.Data.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketRequest.TransmitRate = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketRequest.HostTag = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MaPacketRequest.Priority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MaPacketRequest.Ra.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MaPacketRequest.TransmissionControl = (CSR_TRANSMISSION_CONTROL) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MODIFY_BSS_PARAMETER_REQUEST_ID:
            sig->u.MlmeModifyBssParameterRequest.Data.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterRequest.Data.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterRequest.BeaconPeriod = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterRequest.DtimPeriod = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeModifyBssParameterRequest.CapabilityInformation = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeModifyBssParameterRequest.Bssid.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeModifyBssParameterRequest.RtsThreshold = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_RX_TRIGGER_REQUEST_ID:
            sig->u.MlmeAddRxTriggerRequest.InformationElements.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerRequest.InformationElements.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerRequest.TriggerId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerRequest.Priority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_VIF_AVAILABILITY_INDICATION_ID:
            sig->u.MaVifAvailabilityIndication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityIndication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityIndication.Multicast = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CANCEL_REQUEST_ID:
            sig->u.MlmeHlSyncCancelRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncCancelRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncCancelRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncCancelRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeHlSyncCancelRequest.GroupAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_AUTONOMOUS_SCAN_REQUEST_ID:
            sig->u.MlmeDelAutonomousScanRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanRequest.AutonomousScanId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_BLACKOUT_ENDED_INDICATION_ID:
            sig->u.MlmeBlackoutEndedIndication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlackoutEndedIndication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlackoutEndedIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlackoutEndedIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlackoutEndedIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlackoutEndedIndication.BlackoutId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_AUTONOMOUS_SCAN_DONE_INDICATION_ID:
            sig->u.MlmeAutonomousScanDoneIndication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanDoneIndication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanDoneIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanDoneIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanDoneIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanDoneIndication.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanDoneIndication.AutonomousScanId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_KEY_SEQUENCE_REQUEST_ID:
            sig->u.MlmeGetKeySequenceRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceRequest.KeyId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetKeySequenceRequest.KeyType = (CSR_KEY_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeGetKeySequenceRequest.Address.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CHANNEL_REQUEST_ID:
            sig->u.MlmeSetChannelRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelRequest.Ifindex = (CSR_IFINTERFACE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelRequest.Channel = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeSetChannelRequest.Address.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeSetChannelRequest.AvailabilityDuration = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetChannelRequest.AvailabilityInterval = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_CONFIRM_ID:
            sig->u.MlmeMeasureConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureConfirm.DialogToken = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TRIGGERED_GET_REQUEST_ID:
            sig->u.MlmeAddTriggeredGetRequest.MibAttribute.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetRequest.MibAttribute.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTriggeredGetRequest.TriggeredId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_AUTONOMOUS_SCAN_LOSS_INDICATION_ID:
            sig->u.MlmeAutonomousScanLossIndication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanLossIndication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanLossIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanLossIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAutonomousScanLossIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeAutonomousScanLossIndication.Bssid.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            break;
#endif
        case CSR_MA_VIF_AVAILABILITY_RESPONSE_ID:
            sig->u.MaVifAvailabilityResponse.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityResponse.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityResponse.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityResponse.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityResponse.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaVifAvailabilityResponse.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TEMPLATE_REQUEST_ID:
            sig->u.MlmeAddTemplateRequest.Data1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateRequest.Data1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateRequest.Data2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateRequest.Data2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateRequest.FrameType = (CSR_FRAME_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateRequest.MinTransmitRate = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_POWERMGT_CONFIRM_ID:
            sig->u.MlmePowermgtConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_PERIODIC_CONFIRM_ID:
            sig->u.MlmeAddPeriodicConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicConfirm.PeriodicId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_CONFIRM_ID:
            sig->u.MlmeGetConfirm.MibAttributeValue.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetConfirm.MibAttributeValue.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetConfirm.Status = (CSR_MIB_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetConfirm.ErrorIndex = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_NEXT_CONFIRM_ID:
            sig->u.MlmeGetNextConfirm.MibAttributeValue.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextConfirm.MibAttributeValue.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextConfirm.Status = (CSR_MIB_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetNextConfirm.ErrorIndex = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_AGGREGATION_REQUEST_ID:
            sig->u.MlmeStopAggregationRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeStopAggregationRequest.PeerQstaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeStopAggregationRequest.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopAggregationRequest.Direction = (CSR_DIRECTION) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_RX_TRIGGER_CONFIRM_ID:
            sig->u.MlmeAddRxTriggerConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerConfirm.TriggerId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddRxTriggerConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_BLACKOUT_REQUEST_ID:
            sig->u.MlmeAddBlackoutRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.BlackoutId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.BlackoutType = (CSR_BLACKOUT_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.BlackoutSource = (CSR_BLACKOUT_SOURCE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddBlackoutRequest.BlackoutStartReference = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MlmeAddBlackoutRequest.BlackoutPeriod = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MlmeAddBlackoutRequest.BlackoutDuration = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            CsrMemCpy(sig->u.MlmeAddBlackoutRequest.PeerStaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeAddBlackoutRequest.BlackoutCount = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DELETEKEYS_REQUEST_ID:
            sig->u.MlmeDeletekeysRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysRequest.KeyId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDeletekeysRequest.KeyType = (CSR_KEY_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeDeletekeysRequest.Address.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_RESET_CONFIRM_ID:
            sig->u.MlmeResetConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeResetConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeResetConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeResetConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeResetConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CONFIRM_ID:
            sig->u.MlmeHlSyncConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeHlSyncConfirm.GroupAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeHlSyncConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_AUTONOMOUS_SCAN_REQUEST_ID:
            sig->u.MlmeAddAutonomousScanRequest.ChannelList.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.ChannelList.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.InformationElements.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.InformationElements.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.AutonomousScanId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.Ifindex = (CSR_IFINTERFACE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.ChannelStartingFactor = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.ScanType = (CSR_SCAN_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.ProbeDelay = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MlmeAddAutonomousScanRequest.MinChannelTime = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddAutonomousScanRequest.MaxChannelTime = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_REQUEST_ID:
            sig->u.MlmeSetRequest.MibAttributeValue.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetRequest.MibAttributeValue.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SM_START_REQUEST_ID:
            sig->u.MlmeSmStartRequest.Beacon.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.Beacon.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.BssParameters.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.BssParameters.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.Ifindex = (CSR_IFINTERFACE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.Channel = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeSmStartRequest.InterfaceAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            CsrMemCpy(sig->u.MlmeSmStartRequest.Bssid.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeSmStartRequest.BeaconPeriod = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.DtimPeriod = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSmStartRequest.CapabilityInformation = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECT_STATUS_CONFIRM_ID:
            sig->u.MlmeConnectStatusConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeConnectStatusConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_AUTONOMOUS_SCAN_CONFIRM_ID:
            sig->u.MlmeDelAutonomousScanConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelAutonomousScanConfirm.AutonomousScanId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_PERIODIC_REQUEST_ID:
            sig->u.MlmeDelPeriodicRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicRequest.PeriodicId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SETKEYS_REQUEST_ID:
            sig->u.MlmeSetkeysRequest.Key.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.Key.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.Length = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.KeyId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.KeyType = (CSR_KEY_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeSetkeysRequest.Address.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeSetkeysRequest.SequenceNumber[0] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.SequenceNumber[1] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.SequenceNumber[2] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.SequenceNumber[3] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.SequenceNumber[4] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.SequenceNumber[5] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.SequenceNumber[6] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetkeysRequest.SequenceNumber[7] = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(&sig->u.MlmeSetkeysRequest.CipherSuiteSelector, &ptr[index], 32 / 8);
            index += 32 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_PAUSE_AUTONOMOUS_SCAN_REQUEST_ID:
            sig->u.MlmePauseAutonomousScanRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanRequest.AutonomousScanId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePauseAutonomousScanRequest.Pause = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_REQUEST_ID:
            sig->u.MlmeGetRequest.MibAttribute.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetRequest.MibAttribute.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeGetRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_POWERMGT_REQUEST_ID:
            sig->u.MlmePowermgtRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.PowerManagementMode = (CSR_POWER_MANAGEMENT_MODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.ReceiveDtims = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.ListenInterval = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmePowermgtRequest.TrafficWindow = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_ERROR_INDICATION_ID:
            sig->u.MaPacketErrorIndication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketErrorIndication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketErrorIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketErrorIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketErrorIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MaPacketErrorIndication.PeerQstaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MaPacketErrorIndication.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketErrorIndication.SequenceNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_PERIODIC_REQUEST_ID:
            sig->u.MlmeAddPeriodicRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.PeriodicId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.MaximumLatency = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MlmeAddPeriodicRequest.PeriodicSchedulingMode = (CSR_PERIODIC_SCHEDULING_MODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.WakeHost = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddPeriodicRequest.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TSPEC_REQUEST_ID:
            sig->u.MlmeAddTspecRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.Direction = (CSR_DIRECTION) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.PsScheme = (CSR_PS_SCHEME) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.MediumTime = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecRequest.ServiceStartTime = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MlmeAddTspecRequest.ServiceInterval = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            sig->u.MlmeAddTspecRequest.MinimumDataRate = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_MULTICAST_ADDRESS_CONFIRM_ID:
            sig->u.MlmeAddMulticastAddressConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddMulticastAddressConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TSPEC_CONFIRM_ID:
            sig->u.MlmeAddTspecConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecConfirm.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTspecConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CANCEL_CONFIRM_ID:
            sig->u.MlmeHlSyncCancelConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncCancelConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncCancelConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncCancelConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeHlSyncCancelConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_CONFIRM_ID:
            sig->u.MlmeScanConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeScanConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_STRING_INDICATION_ID:
            sig->u.DebugStringIndication.DebugMessage.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugStringIndication.DebugMessage.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugStringIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.DebugStringIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TEMPLATE_CONFIRM_ID:
            sig->u.MlmeAddTemplateConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateConfirm.FrameType = (CSR_FRAME_TYPE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeAddTemplateConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_BLOCKACK_ERROR_INDICATION_ID:
            sig->u.MlmeBlockackErrorIndication.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlockackErrorIndication.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlockackErrorIndication.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlockackErrorIndication.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlockackErrorIndication.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeBlockackErrorIndication.ResultCode = (CSR_REASON_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeBlockackErrorIndication.PeerQstaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CONFIRM_ID:
            sig->u.MlmeSetConfirm.MibAttributeValue.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetConfirm.MibAttributeValue.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetConfirm.Status = (CSR_MIB_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeSetConfirm.ErrorIndex = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_REQUEST_ID:
            sig->u.MlmeMeasureRequest.MeasurementRequestSet.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureRequest.MeasurementRequestSet.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeMeasureRequest.DialogToken = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_START_AGGREGATION_CONFIRM_ID:
            sig->u.MlmeStartAggregationConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(sig->u.MlmeStartAggregationConfirm.PeerQstaAddress.x, &ptr[index], 48 / 8);
            index += 48 / 8;
            sig->u.MlmeStartAggregationConfirm.UserPriority = (CSR_PRIORITY) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationConfirm.Direction = (CSR_DIRECTION) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStartAggregationConfirm.SequenceNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_MEASURE_CONFIRM_ID:
            sig->u.MlmeStopMeasureConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureConfirm.DialogToken = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_CONFIRM_ID:
            sig->u.MaPacketConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.TransmissionStatus = (CSR_TRANSMISSION_STATUS) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.RetryCount = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.Rate = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MaPacketConfirm.HostTag = CSR_GET_UINT32_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT32;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_PERIODIC_CONFIRM_ID:
            sig->u.MlmeDelPeriodicConfirm.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicConfirm.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicConfirm.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicConfirm.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicConfirm.VirtualInterfaceIdentifier = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicConfirm.PeriodicId = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeDelPeriodicConfirm.ResultCode = (CSR_RESULT_CODE) CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_MEASURE_REQUEST_ID:
            sig->u.MlmeStopMeasureRequest.Dummydataref1.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureRequest.Dummydataref1.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureRequest.Dummydataref2.SlotNumber = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureRequest.Dummydataref2.DataLength = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            sig->u.MlmeStopMeasureRequest.DialogToken = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif

        default:
            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }
    return CSR_RESULT_SUCCESS;
} /* read_unpack_signal() */


/*
 * ---------------------------------------------------------------------------
 *  write_pack
 *
 *      Convert a signal structure, in host-native format, to the
 *      little-endian wire format specified in the UniFi Host Interface
 *      Protocol Specification.
 *
 *      WARNING: This function is auto-generated, DO NOT EDIT!
 *
 *  Arguments:
 *      sig             Pointer to signal structure to pack.
 *      ptr             Destination buffer to pack into.
 *      sig_len         Returns the length of the packed signal, i.e. the
 *                      number of bytes written to ptr.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success,
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE if the ID of signal was not recognised.
 * ---------------------------------------------------------------------------
 */
CsrResult write_pack(const CSR_SIGNAL *sig, u8 *ptr, u16 *sig_len)
{
    s16 index = 0;

    CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->SignalPrimitiveHeader.SignalId, ptr + index);
    index += SIZEOF_UINT16;

    CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->SignalPrimitiveHeader.ReceiverProcessId, ptr + index);
    index += SIZEOF_UINT16;

    CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->SignalPrimitiveHeader.SenderProcessId, ptr + index);
    index += SIZEOF_UINT16;

    switch (sig->SignalPrimitiveHeader.SignalId)
    {
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_PACKET_FILTER_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SETKEYS_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONFIG_QUEUE_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_AUTONOMOUS_SCAN_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanConfirm.AutonomousScanId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_BLACKOUT_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutConfirm.BlackoutId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_BLACKOUT_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutRequest.BlackoutId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_KEY_SEQUENCE_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[0], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[1], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[2], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[3], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[4], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[5], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[6], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceConfirm.SequenceNumber[7], ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SM_START_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_AGGREGATION_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeStopAggregationConfirm.PeerQstaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.Direction, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TSPEC_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecRequest.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecRequest.Direction, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_WORD16_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[0], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[1], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[2], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[3], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[4], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[5], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[6], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[7], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[8], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[9], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[10], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[11], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[12], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[13], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[14], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugWord16Indication.DebugWords[15], ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_DEBUG_GENERIC_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugVariable.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugVariable.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[0], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[1], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[2], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[3], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[4], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[5], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[6], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericConfirm.DebugWords[7], ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_MA_PACKET_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Data.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Data.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MaPacketIndication.LocalTime.x, 64 / 8);
            index += 64 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Ifindex, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Channel, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.ReceptionStatus, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Rssi, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.Snr, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketIndication.ReceivedRate, ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_MLME_SET_TIM_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimRequest.AssociationId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimRequest.TimValue, ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECTED_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectedIndication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectedIndication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectedIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectedIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectedIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectedIndication.ConnectionStatus, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeConnectedIndication.PeerMacAddress.x, 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_RX_TRIGGER_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerRequest.TriggerId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_TRIGGERED_GET_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.MibAttributeValue.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.MibAttributeValue.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.Status, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.ErrorIndex, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeTriggeredGetIndication.TriggeredId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.ChannelList.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.ChannelList.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.InformationElements.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.InformationElements.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.Ifindex, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.ScanType, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.ProbeDelay, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.MinChannelTime, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanRequest.MaxChannelTime, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DELETEKEYS_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_NEXT_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextRequest.MibAttribute.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextRequest.MibAttribute.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CHANNEL_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_START_AGGREGATION_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeStartAggregationRequest.PeerQstaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.Direction, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.StartingSequenceNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.BufferSize, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationRequest.BlockAckTimeout, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeHlSyncRequest.GroupAddress.x, 48 / 8);
            index += 48 / 8;
            break;
#endif
        case CSR_DEBUG_GENERIC_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugVariable.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugVariable.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[0], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[1], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[2], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[3], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[4], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[5], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[6], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericRequest.DebugWords[7], ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_LEAVE_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TRIGGERED_GET_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetRequest.TriggeredId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_MULTICAST_ADDRESS_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressRequest.Data.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressRequest.Data.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressRequest.NumberOfMulticastGroupAddresses, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_RESET_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeResetRequest.StaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetRequest.SetDefaultMib, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_CANCEL_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanCancelRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanCancelRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanCancelRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanCancelRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanCancelRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TRIGGERED_GET_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetConfirm.TriggeredId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_PACKET_FILTER_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterRequest.InformationElements.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterRequest.InformationElements.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterRequest.PacketFilterMode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeSetPacketFilterRequest.ArpFilterAddress, ptr + index);
            index += SIZEOF_UINT32;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_RX_TRIGGER_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerConfirm.TriggerId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelRxTriggerConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECT_STATUS_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.InformationElements.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.InformationElements.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.ConnectionStatus, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeConnectStatusRequest.StaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.AssociationId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusRequest.AssociationCapabilityInformation, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_LEAVE_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeLeaveRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONFIG_QUEUE_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.QueueIndex, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.Aifs, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.Cwmin, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.Cwmax, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConfigQueueRequest.TxopLimit, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TSPEC_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecConfirm.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTspecConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MLME_SET_TIM_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetTimConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureIndication.MeasurementReportSet.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureIndication.MeasurementReportSet.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureIndication.DialogToken, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_BLACKOUT_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutConfirm.BlackoutId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelBlackoutConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_TRIGGERED_GET_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelTriggeredGetConfirm.TriggeredId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_GENERIC_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugVariable.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugVariable.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[0], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[1], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[2], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[3], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[4], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[5], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[6], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugGenericIndication.DebugWords[7], ptr + index);
            index += SIZEOF_UINT16;
            break;
        case CSR_MA_PACKET_CANCEL_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketCancelRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketCancelRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketCancelRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketCancelRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketCancelRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MaPacketCancelRequest.HostTag, ptr + index);
            index += SIZEOF_UINT32;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MODIFY_BSS_PARAMETER_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_PAUSE_AUTONOMOUS_SCAN_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanConfirm.AutonomousScanId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.Data.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.Data.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.TransmitRate, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.HostTag, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.Priority, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MaPacketRequest.Ra.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketRequest.TransmissionControl, ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MODIFY_BSS_PARAMETER_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.Data.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.Data.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.BeaconPeriod, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.DtimPeriod, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.CapabilityInformation, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeModifyBssParameterRequest.Bssid.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeModifyBssParameterRequest.RtsThreshold, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_RX_TRIGGER_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerRequest.InformationElements.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerRequest.InformationElements.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerRequest.TriggerId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerRequest.Priority, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_VIF_AVAILABILITY_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityIndication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityIndication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityIndication.Multicast, ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CANCEL_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeHlSyncCancelRequest.GroupAddress.x, 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_AUTONOMOUS_SCAN_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanRequest.AutonomousScanId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_BLACKOUT_ENDED_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlackoutEndedIndication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlackoutEndedIndication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlackoutEndedIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlackoutEndedIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlackoutEndedIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlackoutEndedIndication.BlackoutId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_AUTONOMOUS_SCAN_DONE_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanDoneIndication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanDoneIndication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanDoneIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanDoneIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanDoneIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanDoneIndication.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanDoneIndication.AutonomousScanId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_KEY_SEQUENCE_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceRequest.KeyId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetKeySequenceRequest.KeyType, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeGetKeySequenceRequest.Address.x, 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CHANNEL_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.Ifindex, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.Channel, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeSetChannelRequest.Address.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.AvailabilityDuration, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetChannelRequest.AvailabilityInterval, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureConfirm.DialogToken, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TRIGGERED_GET_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetRequest.MibAttribute.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetRequest.MibAttribute.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTriggeredGetRequest.TriggeredId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_AUTONOMOUS_SCAN_LOSS_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanLossIndication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanLossIndication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanLossIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanLossIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAutonomousScanLossIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeAutonomousScanLossIndication.Bssid.x, 48 / 8);
            index += 48 / 8;
            break;
#endif
        case CSR_MA_VIF_AVAILABILITY_RESPONSE_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityResponse.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityResponse.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityResponse.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityResponse.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityResponse.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaVifAvailabilityResponse.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TEMPLATE_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateRequest.Data1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateRequest.Data1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateRequest.Data2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateRequest.Data2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateRequest.FrameType, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateRequest.MinTransmitRate, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_POWERMGT_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_PERIODIC_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicConfirm.PeriodicId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetConfirm.MibAttributeValue.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetConfirm.MibAttributeValue.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetConfirm.Status, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetConfirm.ErrorIndex, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_NEXT_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextConfirm.MibAttributeValue.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextConfirm.MibAttributeValue.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextConfirm.Status, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetNextConfirm.ErrorIndex, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_AGGREGATION_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeStopAggregationRequest.PeerQstaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationRequest.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopAggregationRequest.Direction, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_RX_TRIGGER_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerConfirm.TriggerId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddRxTriggerConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_BLACKOUT_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.BlackoutId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.BlackoutType, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.BlackoutSource, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.BlackoutStartReference, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.BlackoutPeriod, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.BlackoutDuration, ptr + index);
            index += SIZEOF_UINT32;
            CsrMemCpy(ptr + index, sig->u.MlmeAddBlackoutRequest.PeerStaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddBlackoutRequest.BlackoutCount, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DELETEKEYS_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysRequest.KeyId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDeletekeysRequest.KeyType, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeDeletekeysRequest.Address.x, 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_RESET_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeResetConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeHlSyncConfirm.GroupAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_AUTONOMOUS_SCAN_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.ChannelList.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.ChannelList.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.InformationElements.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.InformationElements.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.AutonomousScanId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.Ifindex, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.ChannelStartingFactor, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.ScanType, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.ProbeDelay, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.MinChannelTime, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddAutonomousScanRequest.MaxChannelTime, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetRequest.MibAttributeValue.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetRequest.MibAttributeValue.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SM_START_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.Beacon.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.Beacon.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.BssParameters.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.BssParameters.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.Ifindex, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.Channel, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeSmStartRequest.InterfaceAddress.x, 48 / 8);
            index += 48 / 8;
            CsrMemCpy(ptr + index, sig->u.MlmeSmStartRequest.Bssid.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.BeaconPeriod, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.DtimPeriod, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSmStartRequest.CapabilityInformation, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_CONNECT_STATUS_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeConnectStatusConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_AUTONOMOUS_SCAN_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelAutonomousScanConfirm.AutonomousScanId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_PERIODIC_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicRequest.PeriodicId, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SETKEYS_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.Key.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.Key.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.Length, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.KeyId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.KeyType, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeSetkeysRequest.Address.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[0], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[1], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[2], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[3], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[4], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[5], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[6], ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetkeysRequest.SequenceNumber[7], ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, &sig->u.MlmeSetkeysRequest.CipherSuiteSelector, 32 / 8);
            index += 32 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_PAUSE_AUTONOMOUS_SCAN_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanRequest.AutonomousScanId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePauseAutonomousScanRequest.Pause, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_GET_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetRequest.MibAttribute.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetRequest.MibAttribute.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeGetRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_POWERMGT_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.PowerManagementMode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.ReceiveDtims, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.ListenInterval, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmePowermgtRequest.TrafficWindow, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_ERROR_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketErrorIndication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketErrorIndication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketErrorIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketErrorIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketErrorIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MaPacketErrorIndication.PeerQstaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketErrorIndication.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketErrorIndication.SequenceNumber, ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_PERIODIC_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.PeriodicId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.MaximumLatency, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.PeriodicSchedulingMode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.WakeHost, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddPeriodicRequest.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TSPEC_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.Direction, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.PsScheme, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.MediumTime, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.ServiceStartTime, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.ServiceInterval, ptr + index);
            index += SIZEOF_UINT32;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecRequest.MinimumDataRate, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_MULTICAST_ADDRESS_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddMulticastAddressConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TSPEC_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecConfirm.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTspecConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_HL_SYNC_CANCEL_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeHlSyncCancelConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SCAN_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeScanConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_DEBUG_STRING_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugStringIndication.DebugMessage.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugStringIndication.DebugMessage.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugStringIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.DebugStringIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_ADD_TEMPLATE_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateConfirm.FrameType, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeAddTemplateConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_BLOCKACK_ERROR_INDICATION_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlockackErrorIndication.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlockackErrorIndication.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlockackErrorIndication.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlockackErrorIndication.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlockackErrorIndication.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeBlockackErrorIndication.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeBlockackErrorIndication.PeerQstaAddress.x, 48 / 8);
            index += 48 / 8;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_SET_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetConfirm.MibAttributeValue.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetConfirm.MibAttributeValue.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetConfirm.Status, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeSetConfirm.ErrorIndex, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_MEASURE_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureRequest.MeasurementRequestSet.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureRequest.MeasurementRequestSet.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeMeasureRequest.DialogToken, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_START_AGGREGATION_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CsrMemCpy(ptr + index, sig->u.MlmeStartAggregationConfirm.PeerQstaAddress.x, 48 / 8);
            index += 48 / 8;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.UserPriority, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.Direction, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStartAggregationConfirm.SequenceNumber, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_MEASURE_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureConfirm.DialogToken, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
        case CSR_MA_PACKET_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.TransmissionStatus, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.RetryCount, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.Rate, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT32_TO_LITTLE_ENDIAN(sig->u.MaPacketConfirm.HostTag, ptr + index);
            index += SIZEOF_UINT32;
            break;
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_DEL_PERIODIC_CONFIRM_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicConfirm.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicConfirm.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicConfirm.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicConfirm.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicConfirm.VirtualInterfaceIdentifier, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicConfirm.PeriodicId, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeDelPeriodicConfirm.ResultCode, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif
#ifdef CSR_WIFI_HIP_FULL_SIGNAL_SET
        case CSR_MLME_STOP_MEASURE_REQUEST_ID:
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureRequest.Dummydataref1.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureRequest.Dummydataref1.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureRequest.Dummydataref2.SlotNumber, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureRequest.Dummydataref2.DataLength, ptr + index);
            index += SIZEOF_UINT16;
            CSR_COPY_UINT16_TO_LITTLE_ENDIAN(sig->u.MlmeStopMeasureRequest.DialogToken, ptr + index);
            index += SIZEOF_UINT16;
            break;
#endif

        default:
            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    *sig_len = index;

    return CSR_RESULT_SUCCESS;
} /* write_pack() */


