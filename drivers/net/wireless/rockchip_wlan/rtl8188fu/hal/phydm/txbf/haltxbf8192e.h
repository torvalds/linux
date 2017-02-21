#ifndef __HAL_TXBF_8192E_H__
#define __HAL_TXBF_8192E_H__

#if (BEAMFORMING_SUPPORT == 1)
#if (RTL8192E_SUPPORT == 1)
VOID
HalTxbf8192E_setNDPArate(
	IN PVOID			pDM_VOID,
	IN u1Byte	BW,
	IN u1Byte	Rate
);

VOID
HalTxbf8192E_Enter(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8192E_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8192E_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8192E_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
	);
#else

#define HalTxbf8192E_setNDPArate(pDM_VOID, BW, Rate)
#define HalTxbf8192E_Enter(pDM_VOID, Idx)
#define HalTxbf8192E_Leave(pDM_VOID, Idx)
#define HalTxbf8192E_Status(pDM_VOID, Idx)
#define HalTxbf8192E_FwTxBF(pDM_VOID, Idx)

#endif

#endif

#endif

