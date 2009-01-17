#ifndef __WINBOND_MTO_F_H
#define __WINBOND_MTO_F_H

#include "core.h"

extern void MTO_Init(struct wbsoft_priv *);
extern void MTO_PeriodicTimerExpired(struct wbsoft_priv *);
extern void MTO_SetDTORateRange(struct wbsoft_priv *, u8 *, u8);
extern u8 MTO_GetTxRate(MTO_FUNC_INPUT, u32 fpdu_len);
extern u8 MTO_GetTxFallbackRate(MTO_FUNC_INPUT);
extern void MTO_SetTxCount(MTO_FUNC_INPUT, u8 t0, u8 index);

#endif
