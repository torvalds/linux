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
//   adapter        - The pointer to the miniport adapter context.
//   ExUnencrypted  - unsigned char type. The value to be set.
//
// Return values:
//   None.
//============================================================================
#define MLMESetExcludeUnencrypted(adapter, ExUnencrypted)     \
{                                                              \
    (adapter)->sLocalPara.ExcludeUnencrypted = ExUnencrypted;             \
}

//============================================================================
// MLMEGetExcludeUnencrypted --
//
// Description:
//   Get the dot11ExcludeUnencrypted value.
//
// Arguments:
//   adapter        - The pointer to the miniport adapter context.
//
// Return values:
//   unsigned char type. The current dot11ExcludeUnencrypted value.
//============================================================================
#define MLMEGetExcludeUnencrypted(adapter) ((unsigned char) (adapter)->sLocalPara.ExcludeUnencrypted)

//============================================================================
// MLMESetMaxReceiveLifeTime --
//
// Description:
//   Set the dot11MaxReceiveLifeTime value.
//
// Arguments:
//   adapter        - The pointer to the miniport adapter context.
//   ReceiveLifeTime- u32 type. The value to be set.
//
// Return values:
//   None.
//============================================================================
#define MLMESetMaxReceiveLifeTime(adapter, ReceiveLifeTime)    \
{                                                               \
    (adapter)->Mds.MaxReceiveTime = ReceiveLifeTime;                \
}

//============================================================================
// MLMESetMaxReceiveLifeTime --
//
// Description:
//   Get the dot11MaxReceiveLifeTime value.
//
// Arguments:
//   adapter        - The pointer to the miniport adapter context.
//
// Return values:
//   u32 type. The current dot11MaxReceiveLifeTime value.
//============================================================================
#define MLMEGetMaxReceiveLifeTime(adapter) ((u32) (adapter)->Mds.MaxReceiveTime)

#endif


