#ifndef CSR_LIB_H__
#define CSR_LIB_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"
#include "csr_prim_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    CsrPrim type;
} CsrEvent;

/*----------------------------------------------------------------------------*
 *  CsrEvent_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEvent
 *
 *----------------------------------------------------------------------------*/
CsrEvent *CsrEvent_struct(u16 primtype, u16 msgtype);

typedef struct
{
    CsrPrim  type;
    u8 value;
} CsrEventCsrUint8;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint8_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint8
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint8 *CsrEventCsrUint8_struct(u16 primtype, u16 msgtype, u8 value);

typedef struct
{
    CsrPrim   type;
    u16 value;
} CsrEventCsrUint16;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint16_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint16
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint16 *CsrEventCsrUint16_struct(u16 primtype, u16 msgtype, u16 value);

typedef struct
{
    CsrPrim   type;
    u16 value1;
    u8  value2;
} CsrEventCsrUint16CsrUint8;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint16CsrUint8_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint16CsrUint8
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint16CsrUint8 *CsrEventCsrUint16CsrUint8_struct(u16 primtype, u16 msgtype, u16 value1, u8 value2);

typedef struct
{
    CsrPrim   type;
    u16 value1;
    u16 value2;
} CsrEventCsrUint16CsrUint16;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint16CsrUint16_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint16CsrUint16
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint16CsrUint16 *CsrEventCsrUint16CsrUint16_struct(u16 primtype, u16 msgtype, u16 value1, u16 value2);

typedef struct
{
    CsrPrim   type;
    u16 value1;
    CsrUint32 value2;
} CsrEventCsrUint16CsrUint32;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint16_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint16
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint16CsrUint32 *CsrEventCsrUint16CsrUint32_struct(u16 primtype, u16 msgtype, u16 value1, CsrUint32 value2);

typedef struct
{
    CsrPrim        type;
    u16      value1;
    CsrCharString *value2;
} CsrEventCsrUint16CsrCharString;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint16CsrCharString_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint16CsrCharString
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint16CsrCharString *CsrEventCsrUint16CsrCharString_struct(u16 primtype, u16 msgtype, u16 value1, CsrCharString *value2);

typedef struct
{
    CsrPrim   type;
    CsrUint32 value;
} CsrEventCsrUint32;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint32_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint32
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint32 *CsrEventCsrUint32_struct(u16 primtype, u16 msgtype, CsrUint32 value);

typedef struct
{
    CsrPrim   type;
    CsrUint32 value1;
    u16 value2;
} CsrEventCsrUint32CsrUint16;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint32CsrUint16_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint32CsrUint16
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint32CsrUint16 *CsrEventCsrUint32CsrUint16_struct(u16 primtype, u16 msgtype, CsrUint32 value1, CsrUint32 value2);

typedef struct
{
    CsrPrim        type;
    CsrUint32      value1;
    CsrCharString *value2;
} CsrEventCsrUint32CsrCharString;

/*----------------------------------------------------------------------------*
 *  CsrEventCsrUint32CsrCharString_struct
 *
 *  DESCRIPTION
 *      Generic message creator.
 *      Allocates and fills in a message with the signature CsrEventCsrUint32CsrCharString
 *
 *----------------------------------------------------------------------------*/
CsrEventCsrUint32CsrCharString *CsrEventCsrUint32CsrCharString_struct(u16 primtype, u16 msgtype, CsrUint32 value1, CsrCharString *value2);

#ifdef __cplusplus
}
#endif

#endif /* CSR_LIB_H__ */
