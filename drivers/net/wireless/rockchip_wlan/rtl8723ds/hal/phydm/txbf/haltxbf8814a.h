#ifndef __HAL_TXBF_8814A_H__
#define __HAL_TXBF_8814A_H__

#if (RTL8814A_SUPPORT == 1)
#if (BEAMFORMING_SUPPORT == 1)

VOID
HalTxbf8814A_setNDPArate(
	IN PVOID			pDM_VOID,
	IN u1Byte	BW,
	IN u1Byte	Rate
);

u1Byte
halTxbf8814A_GetNtx(
	IN PVOID			pDM_VOID
	);

VOID
HalTxbf8814A_Enter(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8814A_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8814A_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);

VOID
HalTxbf8814A_ResetTxPath(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8814A_GetTxRate(
	IN PVOID			pDM_VOID
	);

VOID
HalTxbf8814A_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
	);

#else

#define HalTxbf8814A_setNDPArate(pDM_VOID,	BW,	Rate)
#define halTxbf8814A_GetNtx(pDM_VOID) 0
#define HalTxbf8814A_Enter(pDM_VOID, Idx)
#define HalTxbf8814A_Leave(pDM_VOID, Idx)
#define HalTxbf8814A_Status(pDM_VOID, Idx)
#define HalTxbf8814A_ResetTxPath(pDM_VOID,	Idx)
#define HalTxbf8814A_GetTxRate(pDM_VOID)
#define HalTxbf8814A_FwTxBF(pDM_VOID,	Idx)

#endif

#else

#define HalTxbf8814A_setNDPArate(pDM_VOID,	BW,	Rate)
#define halTxbf8814A_GetNtx(pDM_VOID) 0
#define HalTxbf8814A_Enter(pDM_VOID, Idx)
#define HalTxbf8814A_Leave(pDM_VOID, Idx)
#define HalTxbf8814A_Status(pDM_VOID, Idx)
#define HalTxbf8814A_ResetTxPath(pDM_VOID,	Idx)
#define HalTxbf8814A_GetTxRate(pDM_VOID)
#define HalTxbf8814A_FwTxBF(pDM_VOID,	Idx)
#endif

#endif

