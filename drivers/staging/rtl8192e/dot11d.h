#ifndef INC_DOT11D_H
#define INC_DOT11D_H

#ifdef ENABLE_DOT11D
#include "ieee80211.h"

typedef struct _CHNL_TXPOWER_TRIPLE {
	u8 FirstChnl;
	u8  NumChnls;
	u8  MaxTxPowerInDbm;
} CHNL_TXPOWER_TRIPLE, *PCHNL_TXPOWER_TRIPLE;

typedef enum _DOT11D_STATE {
	DOT11D_STATE_NONE = 0,
	DOT11D_STATE_LEARNED,
	DOT11D_STATE_DONE,
} DOT11D_STATE;

/**
 * struct _RT_DOT11D_INFO
 * @CountryIeLen: value greater than 0 if @CountryIeBuf contains
 * 	          valid country information element.
 * @chanell_map: holds channel values
 *		0 - invalid,
 *		1 - valid (active scan),
 *	 	2 - valid (passive scan)
 * @CountryIeSrcAddr - Source AP of the country IE
 */

typedef struct _RT_DOT11D_INFO {
	bool bEnabled;

	u16 CountryIeLen;
	u8 CountryIeBuf[MAX_IE_LEN];
	u8 CountryIeSrcAddr[6];
	u8 CountryIeWatchdog;

	u8 channel_map[MAX_CHANNEL_NUMBER+1];
	u8 MaxTxPwrDbmList[MAX_CHANNEL_NUMBER+1];

	DOT11D_STATE State;
} RT_DOT11D_INFO, *PRT_DOT11D_INFO;

static inline void cpMacAddr(unsigned char *des, unsigned char *src)
{
	memcpy(des, src, 6);
}

#define GET_DOT11D_INFO(__pIeeeDev) ((PRT_DOT11D_INFO) \
			((__pIeeeDev)->pDot11dInfo))

#define IS_DOT11D_ENABLE(__pIeeeDev) GET_DOT11D_INFO(__pIeeeDev)->bEnabled
#define IS_COUNTRY_IE_VALID(__pIeeeDev) \
			(GET_DOT11D_INFO(__pIeeeDev)->CountryIeLen > 0)

#define IS_EQUAL_CIE_SRC(__pIeeeDev, __pTa) \
		eqMacAddr(GET_DOT11D_INFO(__pIeeeDev)->CountryIeSrcAddr, __pTa)

#define UPDATE_CIE_SRC(__pIeeeDev, __pTa) \
		cpMacAddr(GET_DOT11D_INFO(__pIeeeDev)->CountryIeSrcAddr, __pTa)

#define IS_COUNTRY_IE_CHANGED(__pIeeeDev, __Ie) \
	(((__Ie).Length == 0 || (__Ie).Length != \
	GET_DOT11D_INFO(__pIeeeDev)->CountryIeLen) ? FALSE : \
	(!memcmp(GET_DOT11D_INFO(__pIeeeDev)->CountryIeBuf, \
	(__Ie).Octet, (__Ie).Length)))

#define CIE_WATCHDOG_TH 1
#define GET_CIE_WATCHDOG(__pIeeeDev) GET_DOT11D_INFO(__pIeeeDev)->CountryIeWatchdog
#define RESET_CIE_WATCHDOG(__pIeeeDev) GET_CIE_WATCHDOG(__pIeeeDev) = 0
#define UPDATE_CIE_WATCHDOG(__pIeeeDev) ++GET_CIE_WATCHDOG(__pIeeeDev)

#define IS_DOT11D_STATE_DONE(__pIeeeDev) \
		(GET_DOT11D_INFO(__pIeeeDev)->State == DOT11D_STATE_DONE)


void Dot11d_Init(struct ieee80211_device *dev);

void Dot11d_Reset(struct ieee80211_device *dev);

void Dot11d_UpdateCountryIe(struct ieee80211_device *dev, u8 *pTaddr,
		u16 CoutryIeLen, u8 *pCoutryIe);

u8 DOT11D_GetMaxTxPwrInDbm(struct ieee80211_device *dev, u8 channel);

void DOT11D_ScanComplete(struct ieee80211_device *dev);

int IsLegalChannel(struct ieee80211_device *dev, u8 channel);

int ToLegalChannel(struct ieee80211_device *dev, u8 channel);

#endif /* ENABLE_DOT11D */
#endif /* INC_DOT11D_H */
