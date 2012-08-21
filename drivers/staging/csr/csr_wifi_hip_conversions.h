/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 *
 * FILE: csr_wifi_hip_conversions.h
 *
 * PURPOSE:
 *      This header file provides the macros for converting to and from
 *      wire format.
 *      These macros *MUST* work for little-endian AND big-endian hosts.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef __CSR_WIFI_HIP_CONVERSIONS_H__
#define __CSR_WIFI_HIP_CONVERSIONS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SIZEOF_UINT16           2
#define SIZEOF_UINT32           4
#define SIZEOF_UINT64           8

#define SIZEOF_SIGNAL_HEADER    6
#define SIZEOF_DATAREF          4


/*
 * Macro to retrieve the signal ID from a wire-format signal.
 */
#define GET_SIGNAL_ID(_buf)     CSR_GET_UINT16_FROM_LITTLE_ENDIAN((_buf))

/*
 * Macros to retrieve and set the DATAREF fields in a packed (i.e. wire-format)
 * HIP signal.
 */
#define GET_PACKED_DATAREF_SLOT(_buf, _ref)                             \
    CSR_GET_UINT16_FROM_LITTLE_ENDIAN(((_buf) + SIZEOF_SIGNAL_HEADER + ((_ref) * SIZEOF_DATAREF) + 0))

#define GET_PACKED_DATAREF_LEN(_buf, _ref)                              \
    CSR_GET_UINT16_FROM_LITTLE_ENDIAN(((_buf) + SIZEOF_SIGNAL_HEADER + ((_ref) * SIZEOF_DATAREF) + 2))

#define SET_PACKED_DATAREF_SLOT(_buf, _ref, _slot)                      \
    CSR_COPY_UINT16_TO_LITTLE_ENDIAN((_slot), ((_buf) + SIZEOF_SIGNAL_HEADER + ((_ref) * SIZEOF_DATAREF) + 0))

#define SET_PACKED_DATAREF_LEN(_buf, _ref, _len)                        \
    CSR_COPY_UINT16_TO_LITTLE_ENDIAN((_len), ((_buf) + SIZEOF_SIGNAL_HEADER + ((_ref) * SIZEOF_DATAREF) + 2))

#define GET_PACKED_MA_PACKET_REQUEST_FRAME_PRIORITY(_buf)              \
    CSR_GET_UINT16_FROM_LITTLE_ENDIAN(((_buf) + SIZEOF_SIGNAL_HEADER + UNIFI_MAX_DATA_REFERENCES * SIZEOF_DATAREF + 8))

#define GET_PACKED_MA_PACKET_REQUEST_HOST_TAG(_buf)                     \
    CSR_GET_UINT32_FROM_LITTLE_ENDIAN(((_buf) + SIZEOF_SIGNAL_HEADER + UNIFI_MAX_DATA_REFERENCES * SIZEOF_DATAREF + 4))

#define GET_PACKED_MA_PACKET_CONFIRM_HOST_TAG(_buf)                     \
    CSR_GET_UINT32_FROM_LITTLE_ENDIAN(((_buf) + SIZEOF_SIGNAL_HEADER + UNIFI_MAX_DATA_REFERENCES * SIZEOF_DATAREF + 8))

#define GET_PACKED_MA_PACKET_CONFIRM_TRANSMISSION_STATUS(_buf)                     \
    CSR_GET_UINT16_FROM_LITTLE_ENDIAN(((_buf) + SIZEOF_SIGNAL_HEADER + UNIFI_MAX_DATA_REFERENCES * SIZEOF_DATAREF + 2))


s32 get_packed_struct_size(const u8 *buf);
CsrResult read_unpack_signal(const u8 *ptr, CSR_SIGNAL *sig);
CsrResult write_pack(const CSR_SIGNAL *sig, u8 *ptr, u16 *sig_len);

#ifdef __cplusplus
}
#endif

#endif /* __CSR_WIFI_HIP_CONVERSIONS_H__ */

