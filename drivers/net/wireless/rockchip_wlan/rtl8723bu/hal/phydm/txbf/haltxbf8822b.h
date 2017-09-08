#ifndef __HAL_TXBF_8822B_H__
#define __HAL_TXBF_8822B_H__
#if (BEAMFORMING_SUPPORT == 1)
#if (RTL8822B_SUPPORT == 1)

VOID
HalTxbf8822B_Init(
	IN PVOID			pDM_VOID
	);

VOID
HalTxbf8822B_Enter(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8822B_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbf8822B_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);

VOID
HalTxbf8822B_ConfigGtab(
	IN PVOID			pDM_VOID
	);

VOID
HalTxbf8822B_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
	);
#else
#define HalTxbf8822B_Init(pDM_VOID)		
#define HalTxbf8822B_Enter(pDM_VOID, Idx)
#define HalTxbf8822B_Leave(pDM_VOID, Idx)
#define HalTxbf8822B_Status(pDM_VOID, Idx)
#define HalTxbf8822B_FwTxBF(pDM_VOID, Idx)
#define HalTxbf8822B_ConfigGtab(pDM_VOID)
#endif


#endif
#endif

