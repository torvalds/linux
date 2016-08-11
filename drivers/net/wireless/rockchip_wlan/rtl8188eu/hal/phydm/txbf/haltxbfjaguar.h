#ifndef __HAL_TXBF_JAGUAR_H__
#define __HAL_TXBF_JAGUAR_H__

#if (BEAMFORMING_SUPPORT == 1)
#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
VOID
HalTxbf8812A_setNDPArate(
	IN PVOID			pDM_VOID,
	IN u1Byte	BW,
	IN u1Byte	Rate
);


VOID
HalTxbfJaguar_Enter(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbfJaguar_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbfJaguar_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	);


VOID
HalTxbfJaguar_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
	);


VOID
HalTxbfJaguar_Patch(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Operation
	);


VOID
HalTxbfJaguar_Clk_8812A(
	IN PVOID			pDM_VOID
	);

#else

#define HalTxbf8812A_setNDPArate(pDM_VOID,	BW,	Rate)
#define HalTxbfJaguar_Enter(pDM_VOID, Idx)
#define HalTxbfJaguar_Leave(pDM_VOID, Idx)
#define HalTxbfJaguar_Status(pDM_VOID, Idx)
#define HalTxbfJaguar_FwTxBF(pDM_VOID,	Idx)
#define HalTxbfJaguar_Patch(pDM_VOID, Operation)
#define HalTxbfJaguar_Clk_8812A(pDM_VOID)
#endif

#endif				
#endif	// #ifndef __HAL_TXBF_JAGUAR_H__								

