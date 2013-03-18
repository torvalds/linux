/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/
#ifndef CSR_WIFI_LIB_H__
#define CSR_WIFI_LIB_H__

#include "csr_wifi_fsm_event.h"

/*----------------------------------------------------------------------------*
 *  CsrWifiFsmEventInit
 *
 *  DESCRIPTION
 *      Macro to initialise the members of a CsrWifiFsmEvent.
 *----------------------------------------------------------------------------*/
#define CsrWifiFsmEventInit(evt, p_primtype, p_msgtype, p_dst, p_src) \
    (evt)->primtype = p_primtype; \
    (evt)->type = p_msgtype; \
    (evt)->destination = p_dst; \
    (evt)->source = p_src


/*----------------------------------------------------------------------------*
 *  CsrWifiEvent_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrWifiEvent
 *
 *----------------------------------------------------------------------------*/
CsrWifiFsmEvent* CsrWifiEvent_struct(u16 primtype, u16 msgtype, CsrSchedQid dst, CsrSchedQid src);

typedef struct
{
    CsrWifiFsmEvent common;
    u8        value;
} CsrWifiEventCsrUint8;

/*----------------------------------------------------------------------------*
 *  CsrWifiEventCsrUint8_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrWifiEventCsrUint8
 *
 *----------------------------------------------------------------------------*/
CsrWifiEventCsrUint8* CsrWifiEventCsrUint8_struct(u16 primtype, u16 msgtype, CsrSchedQid dst, CsrSchedQid src, u8 value);

typedef struct
{
    CsrWifiFsmEvent common;
    u16       value;
} CsrWifiEventCsrUint16;

/*----------------------------------------------------------------------------*
 *  CsrWifiEventCsrUint16_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrWifiEventCsrUint16
 *
 *----------------------------------------------------------------------------*/
CsrWifiEventCsrUint16* CsrWifiEventCsrUint16_struct(u16 primtype, u16 msgtype, CsrSchedQid dst, CsrSchedQid src, u16 value);

typedef struct
{
    CsrWifiFsmEvent common;
    u32       value;
} CsrWifiEventCsrUint32;

/*----------------------------------------------------------------------------*
 *  CsrWifiEventCsrUint32_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrWifiEventCsrUint32
 *
 *----------------------------------------------------------------------------*/
CsrWifiEventCsrUint32* CsrWifiEventCsrUint32_struct(u16 primtype, u16 msgtype, CsrSchedQid dst, CsrSchedQid src, u32 value);

typedef struct
{
    CsrWifiFsmEvent common;
    u16       value16;
    u8        value8;
} CsrWifiEventCsrUint16CsrUint8;

/*----------------------------------------------------------------------------*
 *  CsrWifiEventCsrUint16CsrUint8_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrWifiEventCsrUint16CsrUint8
 *
 *----------------------------------------------------------------------------*/
CsrWifiEventCsrUint16CsrUint8* CsrWifiEventCsrUint16CsrUint8_struct(u16 primtype, u16 msgtype, CsrSchedQid dst, CsrSchedQid src, u16 value16, u8 value8);

#endif /* CSR_WIFI_LIB_H__ */
