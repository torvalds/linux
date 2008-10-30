#ifndef __WINBOND_MTO_F_H
#define __WINBOND_MTO_F_H

#include "adapter.h"

extern void MTO_Init(struct wb35_adapter *);
extern void MTO_PeriodicTimerExpired(struct wb35_adapter *);
extern void MTO_SetDTORateRange(struct wb35_adapter *, u8 *, u8);
extern u8 MTO_GetTxRate(MTO_FUNC_INPUT, u32 fpdu_len);
extern u8 MTO_GetTxFallbackRate(MTO_FUNC_INPUT);
extern void MTO_SetTxCount(MTO_FUNC_INPUT, u8 t0, u8 index);

#endif
