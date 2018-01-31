/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	__PHYDMCCX_H__
#define    __PHYDMCCX_H__

#define CCX_EN 1

#define SET_NHM_SETTING 		0
#define STORE_NHM_SETTING 		1
#define RESTORE_NHM_SETTING 	2

/*
#define NHM_EXCLUDE_CCA 			0
#define NHM_INCLUDE_CCA 			1
#define NHM_EXCLUDE_TXON 			0
#define NHM_INCLUDE_TXON			1
*/

typedef enum NHM_inexclude_cca {
	NHM_EXCLUDE_CCA,
	NHM_INCLUDE_CCA
}NHM_INEXCLUDE_CCA;

typedef enum NHM_inexclude_txon {
	NHM_EXCLUDE_TXON,
	NHM_INCLUDE_TXON
}NHM_INEXCLUDE_TXON;


typedef struct _CCX_INFO{

	/*Settings*/
	u1Byte					NHM_th[11];
	u2Byte					NHM_period;				/* 4us per unit */
	u2Byte					CLM_period;				/* 4us per unit */
	NHM_INEXCLUDE_TXON		NHM_inexclude_txon;
	NHM_INEXCLUDE_CCA		NHM_inexclude_cca;

	/*Previous Settings*/
	u1Byte					NHM_th_restore[11];
	u2Byte					NHM_period_restore;				/* 4us per unit */
	u2Byte					CLM_period_restore;				/* 4us per unit */
	NHM_INEXCLUDE_TXON		NHM_inexclude_txon_restore;
	NHM_INEXCLUDE_CCA		NHM_inexclude_cca_restore;
	
	/*Report*/
	u1Byte		NHM_result[12];
	u2Byte		NHM_duration;
	u2Byte		CLM_result;


	BOOLEAN		echo_NHM_en;
	BOOLEAN		echo_CLM_en;
	u1Byte		echo_IGI;
	
}CCX_INFO, *PCCX_INFO;

/*NHM*/

VOID
phydm_NHMsetting(
	IN		PVOID		pDM_VOID,
	u1Byte	NHMsetting
);

VOID
phydm_NHMtrigger(
	IN		PVOID		pDM_VOID
);

VOID
phydm_getNHMresult(
	IN		PVOID		pDM_VOID
);

BOOLEAN
phydm_checkNHMready(
	IN		PVOID		pDM_VOID
);

/*CLM*/

VOID
phydm_CLMsetting(
	IN		PVOID			pDM_VOID
);

VOID
phydm_CLMtrigger(
	IN		PVOID			pDM_VOID
);

BOOLEAN
phydm_checkCLMready(
	IN		PVOID			pDM_VOID
);

VOID
phydm_getCLMresult(
	IN		PVOID			pDM_VOID
);


#endif
