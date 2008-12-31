//============================================================================
//  MLMEMIB.H -
//
//  Description:
//    Get and Set some of MLME MIB attributes.
//
//  Revision history:
//  --------------------------------------------------------------------------
//           20030117  PD43 Austin Liu
//                     Initial release
//
//  Copyright (c) 2003 Winbond Electronics Corp. All rights reserved.
//============================================================================

#ifndef _MLME_MIB_H
#define _MLME_MIB_H

//============================================================================
// MLMESetExcludeUnencrypted --
//
// Description:
//   Set the dot11ExcludeUnencrypted value.
//
// Arguments:
//   Adapter        - The pointer to the miniport adapter context.
//   ExUnencrypted  - unsigned char type. The value to be set.
//
// Return values:
//   None.
//============================================================================
#define MLMESetExcludeUnencrypted(Adapter, ExUnencrypted)     \
{                                                              \
    (Adapter)->sLocalPara.ExcludeUnencrypted = ExUnencrypted;             \
}

//============================================================================
// MLMEGetExcludeUnencrypted --
//
// Description:
//   Get the dot11ExcludeUnencrypted value.
//
// Arguments:
//   Adapter        - The pointer to the miniport adapter context.
//
// Return values:
//   unsigned char type. The current dot11ExcludeUnencrypted value.
//============================================================================
#define MLMEGetExcludeUnencrypted(Adapter) ((unsigned char) (Adapter)->sLocalPara.ExcludeUnencrypted)

//============================================================================
// MLMESetMaxReceiveLifeTime --
//
// Description:
//   Set the dot11MaxReceiveLifeTime value.
//
// Arguments:
//   Adapter        - The pointer to the miniport adapter context.
//   ReceiveLifeTime- u32 type. The value to be set.
//
// Return values:
//   None.
//============================================================================
#define MLMESetMaxReceiveLifeTime(Adapter, ReceiveLifeTime)    \
{                                                               \
    (Adapter)->Mds.MaxReceiveTime = ReceiveLifeTime;                \
}

//============================================================================
// MLMESetMaxReceiveLifeTime --
//
// Description:
//   Get the dot11MaxReceiveLifeTime value.
//
// Arguments:
//   Adapter        - The pointer to the miniport adapter context.
//
// Return values:
//   u32 type. The current dot11MaxReceiveLifeTime value.
//============================================================================
#define MLMEGetMaxReceiveLifeTime(Adapter) ((u32) (Adapter)->Mds.MaxReceiveTime)

#endif


