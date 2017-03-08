#ifndef __HAL_TXBF_8822B_H__
#define __HAL_TXBF_8822B_H__

#if (RTL8822B_SUPPORT == 1)
#if (BEAMFORMING_SUPPORT == 1)

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

#if (defined(CONFIG_BB_TXBF_API))
VOID
phydm_8822btxbf_rfmode(
	IN PVOID		pDM_VOID,
	IN u1Byte	SUBFeeCnt,
	IN u1Byte	MUBFeeCnt
	);

VOID
phydm_8822b_sutxbfer_workaroud(
	IN PVOID		pDM_VOID,
	IN BOOLEAN	EnableSUBfer,
	IN u1Byte	Nc,
	IN u1Byte	Nr,
	IN u1Byte	Ng,
	IN u1Byte	CB,
	IN u1Byte	BW,
	IN BOOLEAN	isVHT
	);

#else
#define phydm_8822btxbf_rfmode(pDM_VOID, SUBFeeCnt, MUBFeeCnt)
#define phydm_8822b_sutxbfer_workaroud(pDM_VOID, EnableSUBfer, Nc, Nr, Ng, CB, BW, isVHT)
#endif

#else
#define HalTxbf8822B_Init(pDM_VOID)		
#define HalTxbf8822B_Enter(pDM_VOID, Idx)
#define HalTxbf8822B_Leave(pDM_VOID, Idx)
#define HalTxbf8822B_Status(pDM_VOID, Idx)
#define HalTxbf8822B_FwTxBF(pDM_VOID, Idx)
#define HalTxbf8822B_ConfigGtab(pDM_VOID)

#endif
#endif

