#ifndef __HAL_TXBF_8821B_H__
#define __HAL_TXBF_8821B_H__
#if (BEAMFORMING_SUPPORT == 1)
#if (RTL8821B_SUPPORT == 1)
VOID
HalTxbf8821B_Enter(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8821B_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8821B_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8821B_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
	);

#else
#define HalTxbf8821B_Enter(pDM_VOID, Idx)
#define HalTxbf8821B_Leave(pDM_VOID, Idx)
#define HalTxbf8821B_Status(pDM_VOID, Idx)
#define HalTxbf8821B_FwTxBF(pDM_VOID, Idx)
#endif


#endif

#endif	// #ifndef __HAL_TXBF_8821B_H__								

