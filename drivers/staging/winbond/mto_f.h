extern void MTO_Init(PWB32_ADAPTER);
extern void MTO_PeriodicTimerExpired(PWB32_ADAPTER);
extern void MTO_SetDTORateRange(PWB32_ADAPTER, u8 *, u8);
extern u8 MTO_GetTxRate(MTO_FUNC_INPUT, u32 fpdu_len);
extern u8 MTO_GetTxFallbackRate(MTO_FUNC_INPUT);
extern void MTO_SetTxCount(MTO_FUNC_INPUT, u8 t0, u8 index);

