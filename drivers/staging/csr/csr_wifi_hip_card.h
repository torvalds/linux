/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 ******************************************************************************
 * FILE : csr_wifi_hip_card.h
 *
 * PURPOSE : Defines abstract interface for hardware specific functions.
 *           Note, this is a different file from one of the same name in the
 *           Windows driver.
 *
 *****************************************************************************
 */
#ifndef __CARD_H__
#define __CARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "csr_wifi_hip_card_sdio.h"
#include "csr_wifi_hip_signals.h"
#include "csr_wifi_hip_unifi_udi.h"


/*****************************************************************************
 * CardEnableInt -
 */
CsrResult CardEnableInt(card_t *card);

/*****************************************************************************
 * CardGenInt -
 */
CsrResult CardGenInt(card_t *card);

/*****************************************************************************
 * CardPendingInt -
 */
CsrResult CardPendingInt(card_t *card, CsrBool *pintr);

/*****************************************************************************
 * CardDisableInt -
 */
CsrResult CardDisableInt(card_t *card);

/*****************************************************************************
 * CardClearInt -
 */
CsrResult CardClearInt(card_t *card);

/*****************************************************************************
 * CardDisable -
 */
void CardDisable(card_t *card);

/*****************************************************************************
 * CardIntEnabled -
 */
CsrResult CardIntEnabled(card_t *card, CsrBool *enabled);

/*****************************************************************************
 * CardGetDataSlotSize
 */
u16 CardGetDataSlotSize(card_t *card);

/*****************************************************************************
 * CardWriteBulkData -
 */
CsrResult CardWriteBulkData(card_t *card, card_signal_t *csptr, unifi_TrafficQueue queue);


/*****************************************************************************
 * CardClearFromHostDataSlot -
 */
void CardClearFromHostDataSlot(card_t *card, const s16 aSlotNum);

#ifdef CSR_WIFI_REQUEUE_PACKET_TO_HAL
/*****************************************************************************
 * CardClearFromHostDataSlotWithoutFreeingBulkData - Clear the data stot
 * without freeing the bulk data
 */

void CardClearFromHostDataSlotWithoutFreeingBulkData(card_t *card, const s16 aSlotNum);
#endif

/*****************************************************************************
 * CardGetFreeFromHostDataSlots -
 */
u16 CardGetFreeFromHostDataSlots(card_t *card);

u16 CardAreAllFromHostDataSlotsEmpty(card_t *card);

CsrResult card_start_processor(card_t *card, enum unifi_dbg_processors_select which);

CsrResult card_wait_for_firmware_to_start(card_t *card, u32 *paddr);

CsrResult unifi_dl_firmware(card_t *card, void *arg);
CsrResult unifi_dl_patch(card_t *card, void *arg, u32 boot_ctrl);
CsrResult unifi_do_loader_op(card_t *card, u32 op_addr, u8 opcode);
void* unifi_dl_fw_read_start(card_t *card, s8 is_fw);

CsrResult unifi_coredump_handle_request(card_t *card);

CsrResult ConvertCsrSdioToCsrHipResult(card_t *card, CsrResult csrResult);
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
void unifi_debug_log_to_buf(const CsrCharString *fmt, ...);
void unifi_debug_string_to_buf(const CsrCharString *str);
void unifi_debug_hex_to_buf(const CsrCharString *buff, u16 length);
#endif


#ifdef __cplusplus
}
#endif

#endif /* __CARD_H__ */
