#include "r8180_dm.h"
#include "r8180_hw.h"
#include "r8180_93cx6.h"

 /*	Return TRUE if we shall perform High Power Mechanism, FALSE otherwise. */
#define RATE_ADAPTIVE_TIMER_PERIOD      300

bool CheckHighPower(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device *ieee = priv->ieee80211;

	if (!priv->bRegHighPowerMechanism)
		return false;

	if (ieee->state == IEEE80211_LINKED_SCANNING)
		return false;

	return true;
}

/*
 *	Description:
 *		Update Tx power level if necessary.
 *		See also DoRxHighPower() and SetTxPowerLevel8185() for reference.
 *
 *	Note:
 *		The reason why we udpate Tx power level here instead of DoRxHighPower()
 *		is the number of IO to change Tx power is much more than channel TR switch
 *		and they are related to OFDM and MAC registers.
 *		So, we don't want to update it so frequently in per-Rx packet base.
 */
static void DoTxHighPower(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u16			HiPwrUpperTh = 0;
	u16			HiPwrLowerTh = 0;
	u8			RSSIHiPwrUpperTh;
	u8			RSSIHiPwrLowerTh;
	u8			u1bTmp;
	char			OfdmTxPwrIdx, CckTxPwrIdx;

	HiPwrUpperTh = priv->RegHiPwrUpperTh;
	HiPwrLowerTh = priv->RegHiPwrLowerTh;

	HiPwrUpperTh = HiPwrUpperTh * 10;
	HiPwrLowerTh = HiPwrLowerTh * 10;
	RSSIHiPwrUpperTh = priv->RegRSSIHiPwrUpperTh;
	RSSIHiPwrLowerTh = priv->RegRSSIHiPwrLowerTh;

	/* lzm add 080826 */
	OfdmTxPwrIdx  = priv->chtxpwr_ofdm[priv->ieee80211->current_network.channel];
	CckTxPwrIdx  = priv->chtxpwr[priv->ieee80211->current_network.channel];

	if ((priv->UndecoratedSmoothedSS > HiPwrUpperTh) ||
		(priv->bCurCCKPkt && (priv->CurCCKRSSI > RSSIHiPwrUpperTh))) {
		/* Stevenl suggested that degrade 8dbm in high power sate. 2007-12-04 Isaiah */

		priv->bToUpdateTxPwr = true;
		u1bTmp = read_nic_byte(dev, CCK_TXAGC);

		/* If it never enter High Power. */
		if (CckTxPwrIdx == u1bTmp) {
			u1bTmp = (u1bTmp > 16) ? (u1bTmp - 16) : 0;  /* 8dbm */
			write_nic_byte(dev, CCK_TXAGC, u1bTmp);

			u1bTmp = read_nic_byte(dev, OFDM_TXAGC);
			u1bTmp = (u1bTmp > 16) ? (u1bTmp - 16) : 0;  /* 8dbm */
			write_nic_byte(dev, OFDM_TXAGC, u1bTmp);
		}

	} else if ((priv->UndecoratedSmoothedSS < HiPwrLowerTh) &&
		(!priv->bCurCCKPkt || priv->CurCCKRSSI < RSSIHiPwrLowerTh)) {
		if (priv->bToUpdateTxPwr) {
			priv->bToUpdateTxPwr = false;
			/* SD3 required. */
			u1bTmp = read_nic_byte(dev, CCK_TXAGC);
			if (u1bTmp < CckTxPwrIdx) {
				write_nic_byte(dev, CCK_TXAGC, CckTxPwrIdx);
			}

			u1bTmp = read_nic_byte(dev, OFDM_TXAGC);
			if (u1bTmp < OfdmTxPwrIdx) {
				write_nic_byte(dev, OFDM_TXAGC, OfdmTxPwrIdx);
			}
		}
	}
}


/*
 *	Description:
 *		Callback function of UpdateTxPowerWorkItem.
 *		Because of some event happened, e.g. CCX TPC, High Power Mechanism,
 *		We update Tx power of current channel again.
 */
void rtl8180_tx_pw_wq(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ieee80211_device *ieee = container_of(dwork, struct ieee80211_device, tx_pw_wq);
	struct net_device *dev = ieee->dev;

	DoTxHighPower(dev);
}


/*
 *	Return TRUE if we shall perform DIG Mechanism, FALSE otherwise.
 */
bool CheckDig(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device *ieee = priv->ieee80211;

	if (!priv->bDigMechanism)
		return false;

	if (ieee->state != IEEE80211_LINKED)
		return false;

	if ((priv->ieee80211->rate / 5) < 36) /* Schedule Dig under all OFDM rates. By Bruce, 2007-06-01. */
		return false;
	return true;
}
/*
 *	Implementation of DIG for Zebra and Zebra2.
 */
static void DIG_Zebra(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u16			CCKFalseAlarm, OFDMFalseAlarm;
	u16			OfdmFA1, OfdmFA2;
	int			InitialGainStep = 7; /* The number of initial gain stages. */
	int			LowestGainStage = 4; /* The capable lowest stage of performing dig workitem. */
	u32			AwakePeriodIn2Sec = 0;

	CCKFalseAlarm = (u16)(priv->FalseAlarmRegValue & 0x0000ffff);
	OFDMFalseAlarm = (u16)((priv->FalseAlarmRegValue >> 16) & 0x0000ffff);
	OfdmFA1 =  0x15;
	OfdmFA2 = ((u16)(priv->RegDigOfdmFaUpTh)) << 8;

	/* The number of initial gain steps is different, by Bruce, 2007-04-13. */
	if (priv->InitialGain == 0) { /* autoDIG */
		/* Advised from SD3 DZ */
		priv->InitialGain = 4; /* In 87B, m74dBm means State 4 (m82dBm) */
	}
	/* Advised from SD3 DZ */
	OfdmFA1 = 0x20;

#if 1 /* lzm reserved 080826 */
	AwakePeriodIn2Sec = (2000 - priv->DozePeriodInPast2Sec);
	priv->DozePeriodInPast2Sec = 0;

	if (AwakePeriodIn2Sec) {
		OfdmFA1 = (u16)((OfdmFA1 * AwakePeriodIn2Sec) / 2000);
		OfdmFA2 = (u16)((OfdmFA2 * AwakePeriodIn2Sec) / 2000);
	} else {
		;
	}
#endif

	InitialGainStep = 8;
	LowestGainStage = priv->RegBModeGainStage; /* Lowest gain stage. */

	if (OFDMFalseAlarm > OfdmFA1) {
		if (OFDMFalseAlarm > OfdmFA2) {
			priv->DIG_NumberFallbackVote++;
			if (priv->DIG_NumberFallbackVote > 1) {
				/* serious OFDM  False Alarm, need fallback */
				if (priv->InitialGain < InitialGainStep) {
					priv->InitialGainBackUp = priv->InitialGain;

					priv->InitialGain = (priv->InitialGain + 1);
					UpdateInitialGain(dev);
				}
				priv->DIG_NumberFallbackVote = 0;
				priv->DIG_NumberUpgradeVote = 0;
			}
		} else {
			if (priv->DIG_NumberFallbackVote)
				priv->DIG_NumberFallbackVote--;
		}
		priv->DIG_NumberUpgradeVote = 0;
	} else {
		if (priv->DIG_NumberFallbackVote)
			priv->DIG_NumberFallbackVote--;
		priv->DIG_NumberUpgradeVote++;

		if (priv->DIG_NumberUpgradeVote > 9) {
			if (priv->InitialGain > LowestGainStage) { /* In 87B, m78dBm means State 4 (m864dBm) */
				priv->InitialGainBackUp = priv->InitialGain;

				priv->InitialGain = (priv->InitialGain - 1);
				UpdateInitialGain(dev);
			}
			priv->DIG_NumberFallbackVote = 0;
			priv->DIG_NumberUpgradeVote = 0;
		}
	}
}

/*
 *	Dispatch DIG implementation according to RF.
 */
static void DynamicInitGain(struct net_device *dev)
{
	DIG_Zebra(dev);
}

void rtl8180_hw_dig_wq(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ieee80211_device *ieee = container_of(dwork, struct ieee80211_device, hw_dig_wq);
	struct net_device *dev = ieee->dev;
	struct r8180_priv *priv = ieee80211_priv(dev);

	/* Read CCK and OFDM False Alarm. */
	priv->FalseAlarmRegValue = read_nic_dword(dev, CCK_FALSE_ALARM);


	/* Adjust Initial Gain dynamically. */
	DynamicInitGain(dev);

}

static int IncludedInSupportedRates(struct r8180_priv *priv, u8 TxRate)
{
	u8 rate_len;
	u8 rate_ex_len;
	u8                      RateMask = 0x7F;
	u8                      idx;
	unsigned short          Found = 0;
	u8                      NaiveTxRate = TxRate&RateMask;

	rate_len = priv->ieee80211->current_network.rates_len;
	rate_ex_len = priv->ieee80211->current_network.rates_ex_len;
	for (idx = 0; idx < rate_len; idx++) {
		if ((priv->ieee80211->current_network.rates[idx] & RateMask) == NaiveTxRate) {
			Found = 1;
			goto found_rate;
		}
	}
	for (idx = 0; idx < rate_ex_len; idx++) {
		if ((priv->ieee80211->current_network.rates_ex[idx] & RateMask) == NaiveTxRate) {
			Found = 1;
			goto found_rate;
		}
	}
	return Found;
found_rate:
	return Found;
}

/*
 *	Get the Tx rate one degree up form the input rate in the supported rates.
 *	Return the upgrade rate if it is successed, otherwise return the input rate.
 */
static u8 GetUpgradeTxRate(struct net_device *dev, u8 rate)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8                      UpRate;

	/* Upgrade 1 degree. */
	switch (rate) {
	case 108: /* Up to 54Mbps. */
		UpRate = 108;
		break;

	case 96: /* Up to 54Mbps. */
		UpRate = 108;
		break;

	case 72: /* Up to 48Mbps. */
		UpRate = 96;
		break;

	case 48: /* Up to 36Mbps. */
		UpRate = 72;
		break;

	case 36: /* Up to 24Mbps. */
		UpRate = 48;
		break;

	case 22: /* Up to 18Mbps. */
		UpRate = 36;
		break;

	case 11: /* Up to 11Mbps. */
		UpRate = 22;
		break;

	case 4: /* Up to 5.5Mbps. */
		UpRate = 11;
		break;

	case 2: /* Up to 2Mbps. */
		UpRate = 4;
		break;

	default:
		printk("GetUpgradeTxRate(): Input Tx Rate(%d) is undefined!\n", rate);
		return rate;
	}
	/* Check if the rate is valid. */
	if (IncludedInSupportedRates(priv, UpRate)) {
		return UpRate;
	} else {
		return rate;
	}
	return rate;
}
/*
 *	Get the Tx rate one degree down form the input rate in the supported rates.
 *	Return the degrade rate if it is successed, otherwise return the input rate.
 */

static u8 GetDegradeTxRate(struct net_device *dev, u8 rate)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8                      DownRate;

	/* Upgrade 1 degree. */
	switch (rate) {
	case 108: /* Down to 48Mbps. */
		DownRate = 96;
		break;

	case 96: /* Down to 36Mbps. */
		DownRate = 72;
		break;

	case 72: /* Down to 24Mbps. */
		DownRate = 48;
		break;

	case 48: /* Down to 18Mbps. */
		DownRate = 36;
		break;

	case 36: /* Down to 11Mbps. */
		DownRate = 22;
		break;

	case 22: /* Down to 5.5Mbps. */
		DownRate = 11;
		break;

	case 11: /* Down to 2Mbps. */
		DownRate = 4;
		break;

	case 4: /* Down to 1Mbps. */
		DownRate = 2;
		break;

	case 2: /* Down to 1Mbps. */
		DownRate = 2;
		break;

	default:
		printk("GetDegradeTxRate(): Input Tx Rate(%d) is undefined!\n", rate);
		return rate;
	}
	/* Check if the rate is valid. */
	if (IncludedInSupportedRates(priv, DownRate)) {
		return DownRate;
	} else {
		return rate;
	}
	return rate;
}
/*
 *      Helper function to determine if specified data rate is
 *      CCK rate.
 */

static bool MgntIsCckRate(u16 rate)
{
	bool bReturn = false;

	if ((rate <= 22) && (rate != 12) && (rate != 18)) {
		bReturn = true;
	}

	return bReturn;
}
/*
 *	Description:
 *		Tx Power tracking mechanism routine on 87SE.
 */
void TxPwrTracking87SE(struct net_device *dev)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	u8	tmpu1Byte, CurrentThermal, Idx;
	char	CckTxPwrIdx, OfdmTxPwrIdx;

	tmpu1Byte = read_nic_byte(dev, EN_LPF_CAL);
	CurrentThermal = (tmpu1Byte & 0xf0) >> 4; /*[ 7:4]: thermal meter indication. */
	CurrentThermal = (CurrentThermal > 0x0c) ? 0x0c : CurrentThermal;/* lzm add 080826 */

	if (CurrentThermal != priv->ThermalMeter) {
		/* Update Tx Power level on each channel. */
		for (Idx = 1; Idx < 15; Idx++) {
			CckTxPwrIdx = priv->chtxpwr[Idx];
			OfdmTxPwrIdx = priv->chtxpwr_ofdm[Idx];

			if (CurrentThermal > priv->ThermalMeter) {
				/* higher thermal meter. */
				CckTxPwrIdx += (CurrentThermal - priv->ThermalMeter) * 2;
				OfdmTxPwrIdx += (CurrentThermal - priv->ThermalMeter) * 2;

				if (CckTxPwrIdx > 35)
					CckTxPwrIdx = 35; /* Force TxPower to maximal index. */
				if (OfdmTxPwrIdx > 35)
					OfdmTxPwrIdx = 35;
			} else {
				/* lower thermal meter. */
				CckTxPwrIdx -= (priv->ThermalMeter - CurrentThermal) * 2;
				OfdmTxPwrIdx -= (priv->ThermalMeter - CurrentThermal) * 2;

				if (CckTxPwrIdx < 0)
					CckTxPwrIdx = 0;
				if (OfdmTxPwrIdx < 0)
					OfdmTxPwrIdx = 0;
			}

			/* Update TxPower level on CCK and OFDM resp. */
			priv->chtxpwr[Idx] = CckTxPwrIdx;
			priv->chtxpwr_ofdm[Idx] = OfdmTxPwrIdx;
		}

		/* Update TxPower level immediately. */
		rtl8225z2_SetTXPowerLevel(dev, priv->ieee80211->current_network.channel);
	}
	priv->ThermalMeter = CurrentThermal;
}
static void StaRateAdaptive87SE(struct net_device *dev)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	unsigned long	CurrTxokCnt;
	u16		CurrRetryCnt;
	u16		CurrRetryRate;
	unsigned long	CurrRxokCnt;
	bool		bTryUp = false;
	bool		bTryDown = false;
	u8		TryUpTh = 1;
	u8		TryDownTh = 2;
	u32		TxThroughput;
	long		CurrSignalStrength;
	bool		bUpdateInitialGain = false;
	u8		u1bOfdm = 0, u1bCck = 0;
	char		OfdmTxPwrIdx, CckTxPwrIdx;

	priv->RateAdaptivePeriod = RATE_ADAPTIVE_TIMER_PERIOD;


	CurrRetryCnt	= priv->CurrRetryCnt;
	CurrTxokCnt	= priv->NumTxOkTotal - priv->LastTxokCnt;
	CurrRxokCnt	= priv->ieee80211->NumRxOkTotal - priv->LastRxokCnt;
	CurrSignalStrength = priv->Stats_RecvSignalPower;
	TxThroughput = (u32)(priv->NumTxOkBytesTotal - priv->LastTxOKBytes);
	priv->LastTxOKBytes = priv->NumTxOkBytesTotal;
	priv->CurrentOperaRate = priv->ieee80211->rate / 5;
	/* 2 Compute retry ratio. */
	if (CurrTxokCnt > 0) {
		CurrRetryRate = (u16)(CurrRetryCnt * 100 / CurrTxokCnt);
	} else {
	/* It may be serious retry. To distinguish serious retry or no packets modified by Bruce */
		CurrRetryRate = (u16)(CurrRetryCnt * 100 / 1);
	}

	priv->LastRetryCnt = priv->CurrRetryCnt;
	priv->LastTxokCnt = priv->NumTxOkTotal;
	priv->LastRxokCnt = priv->ieee80211->NumRxOkTotal;
	priv->CurrRetryCnt = 0;

	/* 2No Tx packets, return to init_rate or not? */
	if (CurrRetryRate == 0 && CurrTxokCnt == 0) {
		/*
		 * After 9 (30*300ms) seconds in this condition, we try to raise rate.
		 */
		priv->TryupingCountNoData++;

		/* [TRC Dell Lab] Extend raised period from 4.5sec to 9sec, Isaiah 2008-02-15 18:00 */
		if (priv->TryupingCountNoData > 30) {
			priv->TryupingCountNoData = 0;
			priv->CurrentOperaRate = GetUpgradeTxRate(dev, priv->CurrentOperaRate);
			/* Reset Fail Record */
			priv->LastFailTxRate = 0;
			priv->LastFailTxRateSS = -200;
			priv->FailTxRateCount = 0;
		}
		goto SetInitialGain;
	} else {
		priv->TryupingCountNoData = 0; /*Reset trying up times. */
	}


	/*
	 * For Netgear case, I comment out the following signal strength estimation,
	 * which can results in lower rate to transmit when sample is NOT enough (e.g. PING request).
	 *
	 * Restructure rate adaptive as the following main stages:
	 * (1) Add retry threshold in 54M upgrading condition with signal strength.
	 * (2) Add the mechanism to degrade to CCK rate according to signal strength
	 *		and retry rate.
	 * (3) Remove all Initial Gain Updates over OFDM rate. To avoid the complicated
	 *		situation, Initial Gain Update is upon on DIG mechanism except CCK rate.
	 * (4) Add the mechanism of trying to upgrade tx rate.
	 * (5) Record the information of upping tx rate to avoid trying upping tx rate constantly.
	 *
	 */

	/*
	 * 11Mbps or 36Mbps
	 * Check more times in these rate(key rates).
	 */
	if (priv->CurrentOperaRate == 22 || priv->CurrentOperaRate == 72)
		TryUpTh += 9;
	/*
	 * Let these rates down more difficult.
	 */
	if (MgntIsCckRate(priv->CurrentOperaRate) || priv->CurrentOperaRate == 36)
		TryDownTh += 1;

	/* 1 Adjust Rate. */
	if (priv->bTryuping == true) {
		/* 2 For Test Upgrading mechanism
		 * Note:
		 *	Sometimes the throughput is upon on the capability between the AP and NIC,
		 *	thus the low data rate does not improve the performance.
		 *	We randomly upgrade the data rate and check if the retry rate is improved.
		 */

		/* Upgrading rate did not improve the retry rate, fallback to the original rate. */
		if ((CurrRetryRate > 25) && TxThroughput < priv->LastTxThroughput) {
			/*Not necessary raising rate, fall back rate. */
			bTryDown = true;
		} else {
			priv->bTryuping = false;
		}
	} else if (CurrSignalStrength > -47 && (CurrRetryRate < 50)) {
		/*
		 * 2For High Power
		 *
		 * Return to highest data rate, if signal strength is good enough.
		 * SignalStrength threshold(-50dbm) is for RTL8186.
		 * Revise SignalStrength threshold to -51dbm.
		 */
		/* Also need to check retry rate for safety, by Bruce, 2007-06-05. */
		if (priv->CurrentOperaRate != priv->ieee80211->current_network.HighestOperaRate) {
			bTryUp = true;
			/* Upgrade Tx Rate directly. */
			priv->TryupingCount += TryUpTh;
		}

	} else if (CurrTxokCnt > 9 && CurrTxokCnt < 100 && CurrRetryRate >= 600) {
		/*
		 *2 For Serious Retry
		 *
		 * Traffic is not busy but our Tx retry is serious.
		 */
		bTryDown = true;
		/* Let Rate Mechanism to degrade tx rate directly. */
		priv->TryDownCountLowData += TryDownTh;
	} else if (priv->CurrentOperaRate == 108) {
		/* 2For 54Mbps */
		/* Air Link */
		if ((CurrRetryRate > 26) && (priv->LastRetryRate > 25)) {
			bTryDown = true;
		}
		/* Cable Link */
		else if ((CurrRetryRate > 17) && (priv->LastRetryRate > 16) && (CurrSignalStrength > -72)) {
			bTryDown = true;
		}

		if (bTryDown && (CurrSignalStrength < -75)) /* cable link */
			priv->TryDownCountLowData += TryDownTh;
	} else if (priv->CurrentOperaRate == 96) {
		/* 2For 48Mbps */
		/* Air Link */
		if (((CurrRetryRate > 48) && (priv->LastRetryRate > 47))) {
			bTryDown = true;
		} else if (((CurrRetryRate > 21) && (priv->LastRetryRate > 20)) && (CurrSignalStrength > -74)) { /* Cable Link */
			/* Down to rate 36Mbps. */
			bTryDown = true;
		} else if ((CurrRetryRate > (priv->LastRetryRate + 50)) && (priv->FailTxRateCount > 2)) {
			bTryDown = true;
			priv->TryDownCountLowData += TryDownTh;
		} else if ((CurrRetryRate < 8) && (priv->LastRetryRate < 8)) { /* TO DO: need to consider (RSSI) */
			bTryUp = true;
		}

		if (bTryDown && (CurrSignalStrength < -75)) {
			priv->TryDownCountLowData += TryDownTh;
		}
	} else if (priv->CurrentOperaRate == 72) {
		/* 2For 36Mbps */
		if ((CurrRetryRate > 43) && (priv->LastRetryRate > 41)) {
			/* Down to rate 24Mbps. */
			bTryDown = true;
		} else if ((CurrRetryRate > (priv->LastRetryRate + 50)) && (priv->FailTxRateCount > 2)) {
			bTryDown = true;
			priv->TryDownCountLowData += TryDownTh;
		} else if ((CurrRetryRate < 15) &&  (priv->LastRetryRate < 16)) { /* TO DO: need to consider (RSSI) */
			bTryUp = true;
		}

		if (bTryDown && (CurrSignalStrength < -80))
			priv->TryDownCountLowData += TryDownTh;

	} else if (priv->CurrentOperaRate == 48) {
		/* 2For 24Mbps */
		/* Air Link */
		if (((CurrRetryRate > 63) && (priv->LastRetryRate > 62))) {
			bTryDown = true;
		} else if (((CurrRetryRate > 33) && (priv->LastRetryRate > 32)) && (CurrSignalStrength > -82)) { /* Cable Link */
			bTryDown = true;
		} else if ((CurrRetryRate > (priv->LastRetryRate + 50)) && (priv->FailTxRateCount > 2)) {
			bTryDown = true;
			priv->TryDownCountLowData += TryDownTh;
		} else if ((CurrRetryRate < 20) && (priv->LastRetryRate < 21)) { /* TO DO: need to consider (RSSI) */
			bTryUp = true;
		}

		if (bTryDown && (CurrSignalStrength < -82))
			priv->TryDownCountLowData += TryDownTh;

	} else if (priv->CurrentOperaRate == 36) {
		if (((CurrRetryRate > 85) && (priv->LastRetryRate > 86))) {
			bTryDown = true;
		} else if ((CurrRetryRate > (priv->LastRetryRate + 50)) && (priv->FailTxRateCount > 2)) {
			bTryDown = true;
			priv->TryDownCountLowData += TryDownTh;
		} else if ((CurrRetryRate < 22) && (priv->LastRetryRate < 23)) { /* TO DO: need to consider (RSSI) */
			bTryUp = true;
		}
	} else if (priv->CurrentOperaRate == 22) {
		/* 2For 11Mbps */
		if (CurrRetryRate > 95) {
			bTryDown = true;
		} else if ((CurrRetryRate < 29) && (priv->LastRetryRate < 30)) { /*TO DO: need to consider (RSSI) */
			bTryUp = true;
		}
	} else if (priv->CurrentOperaRate == 11) {
		/* 2For 5.5Mbps */
		if (CurrRetryRate > 149) {
			bTryDown = true;
		} else if ((CurrRetryRate < 60) && (priv->LastRetryRate < 65)) {
			bTryUp = true;
		}
	} else if (priv->CurrentOperaRate == 4) {
		/* 2For 2 Mbps */
		if ((CurrRetryRate > 99) && (priv->LastRetryRate > 99)) {
			bTryDown = true;
		} else if ((CurrRetryRate < 65) && (priv->LastRetryRate < 70)) {
			bTryUp = true;
		}
	} else if (priv->CurrentOperaRate == 2) {
		/* 2For 1 Mbps */
		if ((CurrRetryRate < 70) && (priv->LastRetryRate < 75)) {
			bTryUp = true;
		}
	}

	if (bTryUp && bTryDown)
		printk("StaRateAdaptive87B(): Tx Rate tried upping and downing simultaneously!\n");

	/* 1 Test Upgrading Tx Rate
	 * Sometimes the cause of the low throughput (high retry rate) is the compatibility between the AP and NIC.
	 * To test if the upper rate may cause lower retry rate, this mechanism randomly occurs to test upgrading tx rate.
	 */
	if (!bTryUp && !bTryDown && (priv->TryupingCount == 0) && (priv->TryDownCountLowData == 0)
		&& priv->CurrentOperaRate != priv->ieee80211->current_network.HighestOperaRate && priv->FailTxRateCount < 2) {
		if (jiffies % (CurrRetryRate + 101) == 0) {
			bTryUp = true;
			priv->bTryuping = true;
		}
	}

	/* 1 Rate Mechanism */
	if (bTryUp) {
		priv->TryupingCount++;
		priv->TryDownCountLowData = 0;

		/*
		 * Check more times if we need to upgrade indeed.
		 * Because the largest value of pHalData->TryupingCount is 0xFFFF and
		 * the largest value of pHalData->FailTxRateCount is 0x14,
		 * this condition will be satisfied at most every 2 min.
		 */

		if ((priv->TryupingCount > (TryUpTh + priv->FailTxRateCount * priv->FailTxRateCount)) ||
			(CurrSignalStrength > priv->LastFailTxRateSS) || priv->bTryuping) {
			priv->TryupingCount = 0;
			/*
			 * When transferring from CCK to OFDM, DIG is an important issue.
			 */
			if (priv->CurrentOperaRate == 22)
				bUpdateInitialGain = true;

			/*
			 * The difference in throughput between 48Mbps and 36Mbps is 8M.
			 * So, we must be careful in this rate scale. Isaiah 2008-02-15.
			 */
			if (((priv->CurrentOperaRate == 72) || (priv->CurrentOperaRate == 48) || (priv->CurrentOperaRate == 36)) &&
				(priv->FailTxRateCount > 2))
				priv->RateAdaptivePeriod = (RATE_ADAPTIVE_TIMER_PERIOD / 2);

			/* (1)To avoid upgrade frequently to the fail tx rate, add the FailTxRateCount into the threshold. */
			/* (2)If the signal strength is increased, it may be able to upgrade. */

			priv->CurrentOperaRate = GetUpgradeTxRate(dev, priv->CurrentOperaRate);

			if (priv->CurrentOperaRate == 36) {
				priv->bUpdateARFR = true;
				write_nic_word(dev, ARFR, 0x0F8F); /* bypass 12/9/6 */
			} else if (priv->bUpdateARFR) {
				priv->bUpdateARFR = false;
				write_nic_word(dev, ARFR, 0x0FFF); /* set 1M ~ 54Mbps. */
			}

			/* Update Fail Tx rate and count. */
			if (priv->LastFailTxRate != priv->CurrentOperaRate) {
				priv->LastFailTxRate = priv->CurrentOperaRate;
				priv->FailTxRateCount = 0;
				priv->LastFailTxRateSS = -200; /* Set lowest power. */
			}
		}
	} else {
		if (priv->TryupingCount > 0)
			priv->TryupingCount--;
	}

	if (bTryDown) {
		priv->TryDownCountLowData++;
		priv->TryupingCount = 0;

		/* Check if Tx rate can be degraded or Test trying upgrading should fallback. */
		if (priv->TryDownCountLowData > TryDownTh || priv->bTryuping) {
			priv->TryDownCountLowData = 0;
			priv->bTryuping = false;
			/* Update fail information. */
			if (priv->LastFailTxRate == priv->CurrentOperaRate) {
				priv->FailTxRateCount++;
				/* Record the Tx fail rate signal strength. */
				if (CurrSignalStrength > priv->LastFailTxRateSS)
					priv->LastFailTxRateSS = CurrSignalStrength;
			} else {
				priv->LastFailTxRate = priv->CurrentOperaRate;
				priv->FailTxRateCount = 1;
				priv->LastFailTxRateSS = CurrSignalStrength;
			}
			priv->CurrentOperaRate = GetDegradeTxRate(dev, priv->CurrentOperaRate);

			/* Reduce chariot training time at weak signal strength situation. SD3 ED demand. */
			if ((CurrSignalStrength < -80) && (priv->CurrentOperaRate > 72)) {
				priv->CurrentOperaRate = 72;
			}

			if (priv->CurrentOperaRate == 36) {
				priv->bUpdateARFR = true;
				write_nic_word(dev, ARFR, 0x0F8F); /* bypass 12/9/6 */
			} else if (priv->bUpdateARFR) {
				priv->bUpdateARFR = false;
				write_nic_word(dev, ARFR, 0x0FFF); /* set 1M ~ 54Mbps. */
			}

			/*
			 * When it is CCK rate, it may need to update initial gain to receive lower power packets.
			 */
			if (MgntIsCckRate(priv->CurrentOperaRate)) {
				bUpdateInitialGain = true;
			}
		}
	} else {
		if (priv->TryDownCountLowData > 0)
			priv->TryDownCountLowData--;
	}

	/*
	 * Keep the Tx fail rate count to equal to 0x15 at most.
	 * Reduce the fail count at least to 10 sec if tx rate is tending stable.
	 */
	if (priv->FailTxRateCount >= 0x15 ||
		(!bTryUp && !bTryDown && priv->TryDownCountLowData == 0 && priv->TryupingCount && priv->FailTxRateCount > 0x6)) {
		priv->FailTxRateCount--;
	}


	OfdmTxPwrIdx  = priv->chtxpwr_ofdm[priv->ieee80211->current_network.channel];
	CckTxPwrIdx  = priv->chtxpwr[priv->ieee80211->current_network.channel];

	/* Mac0x9e increase 2 level in 36M~18M situation */
	if ((priv->CurrentOperaRate < 96) && (priv->CurrentOperaRate > 22)) {
		u1bCck = read_nic_byte(dev, CCK_TXAGC);
		u1bOfdm = read_nic_byte(dev, OFDM_TXAGC);

		/* case 1: Never enter High power */
		if (u1bCck == CckTxPwrIdx) {
			if (u1bOfdm != (OfdmTxPwrIdx + 2)) {
			priv->bEnhanceTxPwr = true;
			u1bOfdm = ((u1bOfdm + 2) > 35) ? 35 : (u1bOfdm + 2);
			write_nic_byte(dev, OFDM_TXAGC, u1bOfdm);
			}
		} else if (u1bCck < CckTxPwrIdx) {
		/* case 2: enter high power */
			if (!priv->bEnhanceTxPwr) {
				priv->bEnhanceTxPwr = true;
				u1bOfdm = ((u1bOfdm + 2) > 35) ? 35 : (u1bOfdm + 2);
				write_nic_byte(dev, OFDM_TXAGC, u1bOfdm);
			}
		}
	} else if (priv->bEnhanceTxPwr) {  /* 54/48/11/5.5/2/1 */
		u1bCck = read_nic_byte(dev, CCK_TXAGC);
		u1bOfdm = read_nic_byte(dev, OFDM_TXAGC);

		/* case 1: Never enter High power */
		if (u1bCck == CckTxPwrIdx) {
			priv->bEnhanceTxPwr = false;
			write_nic_byte(dev, OFDM_TXAGC, OfdmTxPwrIdx);
		}
		/* case 2: enter high power */
		else if (u1bCck < CckTxPwrIdx) {
			priv->bEnhanceTxPwr = false;
			u1bOfdm = ((u1bOfdm - 2) > 0) ? (u1bOfdm - 2) : 0;
			write_nic_byte(dev, OFDM_TXAGC, u1bOfdm);
		}
	}

	/*
	 * We need update initial gain when we set tx rate "from OFDM to CCK" or
	 * "from CCK to OFDM".
	 */
SetInitialGain:
	if (bUpdateInitialGain) {
		if (MgntIsCckRate(priv->CurrentOperaRate)) { /* CCK */
			if (priv->InitialGain > priv->RegBModeGainStage) {
				priv->InitialGainBackUp = priv->InitialGain;

				if (CurrSignalStrength < -85) /* Low power, OFDM [0x17] = 26. */
					/* SD3 SYs suggest that CurrSignalStrength < -65, ofdm 0x17=26. */
					priv->InitialGain = priv->RegBModeGainStage;

				else if (priv->InitialGain > priv->RegBModeGainStage + 1)
					priv->InitialGain -= 2;

				else
					priv->InitialGain--;

				printk("StaRateAdaptive87SE(): update init_gain to index %d for date rate %d\n", priv->InitialGain, priv->CurrentOperaRate);
				UpdateInitialGain(dev);
			}
		} else { /* OFDM */
			if (priv->InitialGain < 4) {
				priv->InitialGainBackUp = priv->InitialGain;

				priv->InitialGain++;
				printk("StaRateAdaptive87SE(): update init_gain to index %d for date rate %d\n", priv->InitialGain, priv->CurrentOperaRate);
				UpdateInitialGain(dev);
			}
		}
	}

	/* Record the related info */
	priv->LastRetryRate = CurrRetryRate;
	priv->LastTxThroughput = TxThroughput;
	priv->ieee80211->rate = priv->CurrentOperaRate * 5;
}

void rtl8180_rate_adapter(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ieee80211_device *ieee = container_of(dwork, struct ieee80211_device, rate_adapter_wq);
	struct net_device *dev = ieee->dev;
	StaRateAdaptive87SE(dev);
}
void timer_rate_adaptive(unsigned long data)
{
	struct r8180_priv *priv = ieee80211_priv((struct net_device *)data);
	if (!priv->up) {
		return;
	}
	if ((priv->ieee80211->iw_mode != IW_MODE_MASTER)
			&& (priv->ieee80211->state == IEEE80211_LINKED) &&
			(priv->ForcedDataRate == 0)) {
		queue_work(priv->ieee80211->wq, (void *)&priv->ieee80211->rate_adapter_wq);
	}
	priv->rateadapter_timer.expires = jiffies + MSECS(priv->RateAdaptivePeriod);
	add_timer(&priv->rateadapter_timer);
}

void SwAntennaDiversityRxOk8185(struct net_device *dev, u8 SignalStrength)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

	priv->AdRxOkCnt++;

	if (priv->AdRxSignalStrength != -1) {
		priv->AdRxSignalStrength = ((priv->AdRxSignalStrength * 7) + (SignalStrength * 3)) / 10;
	} else { /* Initialization case. */
		priv->AdRxSignalStrength = SignalStrength;
	}

	if (priv->LastRxPktAntenna) /* Main antenna. */
		priv->AdMainAntennaRxOkCnt++;
	else	 /* Aux antenna. */
		priv->AdAuxAntennaRxOkCnt++;
}
 /*	Change Antenna Switch. */
bool SetAntenna8185(struct net_device *dev, u8 u1bAntennaIndex)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	bool bAntennaSwitched = false;

	switch (u1bAntennaIndex) {
	case 0:
		/* Mac register, main antenna */
		write_nic_byte(dev, ANTSEL, 0x03);
		/* base band */
		write_phy_cck(dev, 0x11, 0x9b); /* Config CCK RX antenna. */
		write_phy_ofdm(dev, 0x0d, 0x5c); /* Config OFDM RX antenna. */

		bAntennaSwitched = true;
		break;

	case 1:
		/* Mac register, aux antenna */
		write_nic_byte(dev, ANTSEL, 0x00);
		/* base band */
		write_phy_cck(dev, 0x11, 0xbb); /* Config CCK RX antenna. */
		write_phy_ofdm(dev, 0x0d, 0x54); /* Config OFDM RX antenna. */

		bAntennaSwitched = true;

		break;

	default:
		printk("SetAntenna8185: unknown u1bAntennaIndex(%d)\n", u1bAntennaIndex);
		break;
	}

	if (bAntennaSwitched)
		priv->CurrAntennaIndex = u1bAntennaIndex;

	return bAntennaSwitched;
}
 /*	Toggle Antenna switch. */
bool SwitchAntenna(struct net_device *dev)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

	bool		bResult;

	if (priv->CurrAntennaIndex == 0) {
		bResult = SetAntenna8185(dev, 1);
	} else {
		bResult = SetAntenna8185(dev, 0);
	}

	return bResult;
}
/*
 * Engine of SW Antenna Diversity mechanism.
 * Since 8187 has no Tx part information,
 * this implementation is only dependend on Rx part information.
 */
void SwAntennaDiversity(struct net_device *dev)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	bool   bSwCheckSS = false;
	if (bSwCheckSS) {
		priv->AdTickCount++;

		printk("(1) AdTickCount: %d, AdCheckPeriod: %d\n",
			priv->AdTickCount, priv->AdCheckPeriod);
		printk("(2) AdRxSignalStrength: %ld, AdRxSsThreshold: %ld\n",
			priv->AdRxSignalStrength, priv->AdRxSsThreshold);
	}

	/* Case 1. No Link. */
	if (priv->ieee80211->state != IEEE80211_LINKED) {
		priv->bAdSwitchedChecking = false;
		/* I switch antenna here to prevent any one of antenna is broken before link established, 2006.04.18, by rcnjko.. */
		SwitchAntenna(dev);

	  /* Case 2. Linked but no packet receive.d */
	} else if (priv->AdRxOkCnt == 0) {
		priv->bAdSwitchedChecking = false;
		SwitchAntenna(dev);

	  /* Case 3. Evaluate last antenna switch action and undo it if necessary. */
	} else if (priv->bAdSwitchedChecking == true) {
		priv->bAdSwitchedChecking = false;

		/* Adjust Rx signal strength threshold. */
		priv->AdRxSsThreshold = (priv->AdRxSignalStrength + priv->AdRxSsBeforeSwitched) / 2;

		priv->AdRxSsThreshold = (priv->AdRxSsThreshold > priv->AdMaxRxSsThreshold) ?
					priv->AdMaxRxSsThreshold : priv->AdRxSsThreshold;
		if (priv->AdRxSignalStrength < priv->AdRxSsBeforeSwitched) {
		/* Rx signal strength is not improved after we swtiched antenna. => Swich back. */
			/* Increase Antenna Diversity checking period due to bad decision. */
			priv->AdCheckPeriod *= 2;
			/* Increase Antenna Diversity checking period. */
			if (priv->AdCheckPeriod > priv->AdMaxCheckPeriod)
				priv->AdCheckPeriod = priv->AdMaxCheckPeriod;

			/* Wrong decision => switch back. */
			SwitchAntenna(dev);
		} else {
		/* Rx Signal Strength is improved. */

			/* Reset Antenna Diversity checking period to its min value. */
			priv->AdCheckPeriod = priv->AdMinCheckPeriod;
		}

	}
	/* Case 4. Evaluate if we shall switch antenna now. */
	/* Cause Table Speed is very fast in TRC Dell Lab, we check it every time. */
	else {
		priv->AdTickCount = 0;

		/*
		 * <Roger_Notes> We evaluate RxOk counts for each antenna first and than
		 * evaluate signal strength.
		 * The following operation can overcome the disability of CCA on both two antennas
		 * When signal strength was extremely low or high.
		 * 2008.01.30.
		 */

		/*
		 * Evaluate RxOk count from each antenna if we shall switch default antenna now.
		 */
		if ((priv->AdMainAntennaRxOkCnt < priv->AdAuxAntennaRxOkCnt)
			&& (priv->CurrAntennaIndex == 0)) {
		/* We set Main antenna as default but RxOk count was less than Aux ones. */

			/* Switch to Aux antenna. */
			SwitchAntenna(dev);
			priv->bHWAdSwitched = true;
		} else if ((priv->AdAuxAntennaRxOkCnt < priv->AdMainAntennaRxOkCnt)
			&& (priv->CurrAntennaIndex == 1)) {
		/* We set Aux antenna as default but RxOk count was less than Main ones. */

			/* Switch to Main antenna. */
			SwitchAntenna(dev);
			priv->bHWAdSwitched = true;
		} else {
		/* Default antenna is better. */

			/* Still need to check current signal strength. */
			priv->bHWAdSwitched = false;
		}
		/*
		 * <Roger_Notes> We evaluate Rx signal strength ONLY when default antenna
		 * didn't change by HW evaluation.
		 * 2008.02.27.
		 *
		 * [TRC Dell Lab] SignalStrength is inaccuracy. Isaiah 2008-03-05
		 * For example, Throughput of aux is better than main antenna(about 10M v.s 2M),
		 * but AdRxSignalStrength is less than main.
		 * Our guess is that main antenna have lower throughput and get many change
		 * to receive more CCK packets(ex.Beacon) which have stronger SignalStrength.
		 */
		if ((!priv->bHWAdSwitched) && (bSwCheckSS)) {
			/* Evaluate Rx signal strength if we shall switch antenna now. */
			if (priv->AdRxSignalStrength < priv->AdRxSsThreshold) {
			/* Rx signal strength is weak => Switch Antenna. */
				priv->AdRxSsBeforeSwitched = priv->AdRxSignalStrength;
				priv->bAdSwitchedChecking = true;

				SwitchAntenna(dev);
			} else {
			/* Rx signal strength is OK. */
				priv->bAdSwitchedChecking = false;
				/* Increase Rx signal strength threshold if necessary. */
				if ((priv->AdRxSignalStrength > (priv->AdRxSsThreshold + 10)) && /* Signal is much stronger than current threshold */
					priv->AdRxSsThreshold <= priv->AdMaxRxSsThreshold) { /* Current threhold is not yet reach upper limit. */

					priv->AdRxSsThreshold = (priv->AdRxSsThreshold + priv->AdRxSignalStrength) / 2;
					priv->AdRxSsThreshold = (priv->AdRxSsThreshold > priv->AdMaxRxSsThreshold) ?
								priv->AdMaxRxSsThreshold : priv->AdRxSsThreshold;/* +by amy 080312 */
				}

				/* Reduce Antenna Diversity checking period if possible. */
				if (priv->AdCheckPeriod > priv->AdMinCheckPeriod)
					priv->AdCheckPeriod /= 2;
			}
		}
	}
	/* Reset antenna diversity Rx related statistics. */
	priv->AdRxOkCnt = 0;
	priv->AdMainAntennaRxOkCnt = 0;
	priv->AdAuxAntennaRxOkCnt = 0;
}

 /*	Return TRUE if we shall perform Tx Power Tracking Mechanism, FALSE otherwise. */
bool CheckTxPwrTracking(struct net_device *dev)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

	if (!priv->bTxPowerTrack)
		return false;

	/* if 87SE is in High Power , don't do Tx Power Tracking. asked by SD3 ED. 2008-08-08 Isaiah */
	if (priv->bToUpdateTxPwr)
		return false;

	return true;
}


 /*	Timer callback function of SW Antenna Diversity. */
void SwAntennaDiversityTimerCallback(struct net_device *dev)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	RT_RF_POWER_STATE rtState;

	 /* We do NOT need to switch antenna while RF is off. */
	rtState = priv->eRFPowerState;
	do {
		if (rtState == eRfOff) {
			break;
		} else if (rtState == eRfSleep) {
			/* Don't access BB/RF under Disable PLL situation. */
			break;
		}
		SwAntennaDiversity(dev);

	} while (false);

	if (priv->up) {
		priv->SwAntennaDiversityTimer.expires = jiffies + MSECS(ANTENNA_DIVERSITY_TIMER_PERIOD);
		add_timer(&priv->SwAntennaDiversityTimer);
	}
}

