//------------------------------------------------------------------------------
// Copyright (c) 2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef COMMON_DRV_H_
#define COMMON_DRV_H_

#include "hif.h"
#include "htc_packet.h"
#include "htc_api.h"

/* structure that is the state information for the default credit distribution callback
 * drivers should instantiate (zero-init as well) this structure in their driver instance
 * and pass it as a context to the HTC credit distribution functions */
struct common_credit_state_info {
    int TotalAvailableCredits;      /* total credits in the system at startup */
    int CurrentFreeCredits;         /* credits available in the pool that have not been
                                       given out to endpoints */
    struct htc_endpoint_credit_dist *pLowestPriEpDist;  /* pointer to the lowest priority endpoint dist struct */
};

struct hci_transport_callbacks {
    s32 (*setupTransport)(void *ar);
    void (*cleanupTransport)(void *ar);
};

struct hci_transport_misc_handles {
   void *netDevice;
   void *hifDevice;
   void *htcHandle;
};

/* HTC TX packet tagging definitions */
#define AR6K_CONTROL_PKT_TAG    HTC_TX_PACKET_TAG_USER_DEFINED
#define AR6K_DATA_PKT_TAG       (AR6K_CONTROL_PKT_TAG + 1)

#define AR6002_VERSION_REV1     0x20000086
#define AR6002_VERSION_REV2     0x20000188
#define AR6003_VERSION_REV1     0x300002ba
#define AR6003_VERSION_REV2     0x30000384

#define AR6002_CUST_DATA_SIZE 112
#define AR6003_CUST_DATA_SIZE 16

#ifdef __cplusplus
extern "C" {
#endif

/* OS-independent APIs */
int ar6000_setup_credit_dist(HTC_HANDLE HTCHandle, struct common_credit_state_info *pCredInfo);

int ar6000_ReadRegDiag(struct hif_device *hifDevice, u32 *address, u32 *data);

int ar6000_WriteRegDiag(struct hif_device *hifDevice, u32 *address, u32 *data);

int ar6000_ReadDataDiag(struct hif_device *hifDevice, u32 address,  u8 *data, u32 length);

int ar6000_reset_device(struct hif_device *hifDevice, u32 TargetType, bool waitForCompletion, bool coldReset);

void ar6000_dump_target_assert_info(struct hif_device *hifDevice, u32 TargetType);

int ar6000_set_htc_params(struct hif_device *hifDevice,
                               u32 TargetType,
                               u32 MboxIsrYieldValue,
                               u8 HtcControlBuffers);

int ar6000_prepare_target(struct hif_device *hifDevice,
                               u32 TargetType,
                               u32 TargetVersion);

int ar6000_set_hci_bridge_flags(struct hif_device *hifDevice,
                                     u32 TargetType,
                                     u32 Flags);

void ar6000_copy_cust_data_from_target(struct hif_device *hifDevice, u32 TargetType);

u8 *ar6000_get_cust_data_buffer(u32 TargetType);

int ar6000_setBTState(void *context, u8 *pInBuf, u32 InBufSize);

int ar6000_setDevicePowerState(void *context, u8 *pInBuf, u32 InBufSize);

int ar6000_setWowMode(void *context, u8 *pInBuf, u32 InBufSize);

int ar6000_setHostMode(void *context, u8 *pInBuf, u32 InBufSize);

#ifdef __cplusplus
}
#endif

#endif /*COMMON_DRV_H_*/
