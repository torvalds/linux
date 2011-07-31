/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "../80211core/cprecomp.h"
#include "hpani.h"
#include "hpusb.h"


extern u16_t zfDelayWriteInternalReg(zdev_t *dev, u32_t addr, u32_t val);
extern u16_t zfFlushDelayWrite(zdev_t *dev);

/*
 * Anti noise immunity support.  We track phy errors and react
 * to excessive errors by adjusting the noise immunity parameters.
 */

/******************************************************************************
 *
 * New Ani Algorithm for Station side only
 *
 *****************************************************************************/

#define ZM_HAL_NOISE_IMMUNE_MAX     4   /* Max noise immunity level */
#define ZM_HAL_SPUR_IMMUNE_MAX      7   /* Max spur immunity level */
#define ZM_HAL_FIRST_STEP_MAX       2   /* Max first step level */

#define ZM_HAL_ANI_OFDM_TRIG_HIGH       500
#define ZM_HAL_ANI_OFDM_TRIG_LOW        200
#define ZM_HAL_ANI_CCK_TRIG_HIGH        200
#define ZM_HAL_ANI_CCK_TRIG_LOW         100
#define ZM_HAL_ANI_NOISE_IMMUNE_LVL     4
#define ZM_HAL_ANI_USE_OFDM_WEAK_SIG    TRUE
#define ZM_HAL_ANI_CCK_WEAK_SIG_THR     FALSE
#define ZM_HAL_ANI_SPUR_IMMUNE_LVL      7
#define ZM_HAL_ANI_FIRSTEP_LVL          0
#define ZM_HAL_ANI_RSSI_THR_HIGH        40
#define ZM_HAL_ANI_RSSI_THR_LOW         7
#define ZM_HAL_ANI_PERIOD               100

#define ZM_HAL_EP_RND(x, mul) \
    ((((x)%(mul)) >= ((mul)/2)) ? ((x) + ((mul) - 1)) / (mul) : (x)/(mul))

s32_t BEACON_RSSI(zdev_t *dev)
{
    s32_t rssi;
    struct zsHpPriv *HpPriv;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv *)wd->hpPrivate;

    rssi = ZM_HAL_EP_RND(HpPriv->stats.ast_nodestats.ns_avgbrssi, ZM_HAL_RSSI_EP_MULTIPLIER);

    return rssi;
}

/*
 * Setup ANI handling.  Sets all thresholds and levels to default level AND
 * resets the channel statistics
 */

void zfHpAniAttach(zdev_t *dev)
{
    u32_t i;
    struct zsHpPriv *HpPriv;

    const int totalSizeDesired[] = { -55, -55, -55, -55, -62 };
    const int coarseHigh[]       = { -14, -14, -14, -14, -12 };
    const int coarseLow[]        = { -64, -64, -64, -64, -70 };
    const int firpwr[]           = { -78, -78, -78, -78, -80 };

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv *)wd->hpPrivate;

    for (i = 0; i < 5; i++) {
	HpPriv->totalSizeDesired[i] = totalSizeDesired[i];
        HpPriv->coarseHigh[i] = coarseHigh[i];
        HpPriv->coarseLow[i] = coarseLow[i];
        HpPriv->firpwr[i] = firpwr[i];
    }

    /* owl has phy counters */
    HpPriv->hasHwPhyCounters = 1;

    memset((char *)&HpPriv->ani, 0, sizeof(HpPriv->ani));
    for (i = 0; i < ARRAY_SIZE(HpPriv->ani); i++) {
        /* New ANI stuff */
        HpPriv->ani[i].ofdmTrigHigh = ZM_HAL_ANI_OFDM_TRIG_HIGH;
        HpPriv->ani[i].ofdmTrigLow = ZM_HAL_ANI_OFDM_TRIG_LOW;
        HpPriv->ani[i].cckTrigHigh = ZM_HAL_ANI_CCK_TRIG_HIGH;
        HpPriv->ani[i].cckTrigLow = ZM_HAL_ANI_CCK_TRIG_LOW;
        HpPriv->ani[i].rssiThrHigh = ZM_HAL_ANI_RSSI_THR_HIGH;
        HpPriv->ani[i].rssiThrLow = ZM_HAL_ANI_RSSI_THR_LOW;
        HpPriv->ani[i].ofdmWeakSigDetectOff = !ZM_HAL_ANI_USE_OFDM_WEAK_SIG;
        HpPriv->ani[i].cckWeakSigThreshold = ZM_HAL_ANI_CCK_WEAK_SIG_THR;
        HpPriv->ani[i].spurImmunityLevel = ZM_HAL_ANI_SPUR_IMMUNE_LVL;
        HpPriv->ani[i].firstepLevel = ZM_HAL_ANI_FIRSTEP_LVL;
        if (HpPriv->hasHwPhyCounters) {
            HpPriv->ani[i].ofdmPhyErrBase = 0;//AR_PHY_COUNTMAX - ZM_HAL_ANI_OFDM_TRIG_HIGH;
            HpPriv->ani[i].cckPhyErrBase = 0;//AR_PHY_COUNTMAX - ZM_HAL_ANI_CCK_TRIG_HIGH;
        }
    }
    if (HpPriv->hasHwPhyCounters) {
        //zm_debug_msg2("Setting OfdmErrBase = 0x", HpPriv->ani[0].ofdmPhyErrBase);
        //zm_debug_msg2("Setting cckErrBase = 0x", HpPriv->ani[0].cckPhyErrBase);
        //OS_REG_WRITE(ah, AR_PHY_ERR_1, HpPriv->ani[0].ofdmPhyErrBase);
        //OS_REG_WRITE(ah, AR_PHY_ERR_2, HpPriv->ani[0].cckPhyErrBase);
    }
    HpPriv->aniPeriod = ZM_HAL_ANI_PERIOD;
    //if (ath_hal_enableANI)
    HpPriv->procPhyErr |= ZM_HAL_PROCESS_ANI;

    HpPriv->stats.ast_nodestats.ns_avgbrssi = ZM_RSSI_DUMMY_MARKER;
    HpPriv->stats.ast_nodestats.ns_avgrssi = ZM_RSSI_DUMMY_MARKER;
    HpPriv->stats.ast_nodestats.ns_avgtxrssi = ZM_RSSI_DUMMY_MARKER;
}

/*
 * Control Adaptive Noise Immunity Parameters
 */
u8_t zfHpAniControl(zdev_t *dev, ZM_HAL_ANI_CMD cmd, int param)
{
    typedef s32_t TABLE[];
    struct zsHpPriv *HpPriv;
    struct zsAniState *aniState;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv *)wd->hpPrivate;
    aniState = HpPriv->curani;

    switch (cmd)
    {
    case ZM_HAL_ANI_NOISE_IMMUNITY_LEVEL:
    {
        u32_t level = param;

        if (level >= ARRAY_SIZE(HpPriv->totalSizeDesired)) {
          zm_debug_msg1("level out of range, desired level : ", level);
          zm_debug_msg1("max level : ", ARRAY_SIZE(HpPriv->totalSizeDesired));
          return FALSE;
        }

        zfDelayWriteInternalReg(dev, AR_PHY_DESIRED_SZ,
                (HpPriv->regPHYDesiredSZ & ~AR_PHY_DESIRED_SZ_TOT_DES)
                | ((HpPriv->totalSizeDesired[level] << AR_PHY_DESIRED_SZ_TOT_DES_S)
                & AR_PHY_DESIRED_SZ_TOT_DES));
        zfDelayWriteInternalReg(dev, AR_PHY_AGC_CTL1,
                (HpPriv->regPHYAgcCtl1 & ~AR_PHY_AGC_CTL1_COARSE_LOW)
                | ((HpPriv->coarseLow[level] << AR_PHY_AGC_CTL1_COARSE_LOW_S)
                & AR_PHY_AGC_CTL1_COARSE_LOW));
        zfDelayWriteInternalReg(dev, AR_PHY_AGC_CTL1,
                (HpPriv->regPHYAgcCtl1 & ~AR_PHY_AGC_CTL1_COARSE_HIGH)
                | ((HpPriv->coarseHigh[level] << AR_PHY_AGC_CTL1_COARSE_HIGH_S)
                & AR_PHY_AGC_CTL1_COARSE_HIGH));
        zfDelayWriteInternalReg(dev, AR_PHY_FIND_SIG,
                (HpPriv->regPHYFindSig & ~AR_PHY_FIND_SIG_FIRPWR)
                | ((HpPriv->firpwr[level] << AR_PHY_FIND_SIG_FIRPWR_S)
                & AR_PHY_FIND_SIG_FIRPWR));
        zfFlushDelayWrite(dev);

        if (level > aniState->noiseImmunityLevel)
            HpPriv->stats.ast_ani_niup++;
        else if (level < aniState->noiseImmunityLevel)
            HpPriv->stats.ast_ani_nidown++;
        aniState->noiseImmunityLevel = (u8_t)level;
        break;
    }
    case ZM_HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION:
    {
        const TABLE m1ThreshLow   = { 127,   50 };
        const TABLE m2ThreshLow   = { 127,   40 };
        const TABLE m1Thresh      = { 127, 0x4d };
        const TABLE m2Thresh      = { 127, 0x40 };
        const TABLE m2CountThr    = {  31,   16 };
        const TABLE m2CountThrLow = {  63,   48 };
        u32_t on = param ? 1 : 0;

        zfDelayWriteInternalReg(dev, AR_PHY_SFCORR_LOW,
                (HpPriv->regPHYSfcorrLow & ~AR_PHY_SFCORR_LOW_M1_THRESH_LOW)
                | ((m1ThreshLow[on] << AR_PHY_SFCORR_LOW_M1_THRESH_LOW_S)
                & AR_PHY_SFCORR_LOW_M1_THRESH_LOW));
        zfDelayWriteInternalReg(dev, AR_PHY_SFCORR_LOW,
                (HpPriv->regPHYSfcorrLow & ~AR_PHY_SFCORR_LOW_M2_THRESH_LOW)
                | ((m2ThreshLow[on] << AR_PHY_SFCORR_LOW_M2_THRESH_LOW_S)
                & AR_PHY_SFCORR_LOW_M2_THRESH_LOW));
        zfDelayWriteInternalReg(dev, AR_PHY_SFCORR,
                (HpPriv->regPHYSfcorr & ~AR_PHY_SFCORR_M1_THRESH)
                | ((m1Thresh[on] << AR_PHY_SFCORR_M1_THRESH_S)
                & AR_PHY_SFCORR_M1_THRESH));
        zfDelayWriteInternalReg(dev, AR_PHY_SFCORR,
                (HpPriv->regPHYSfcorr & ~AR_PHY_SFCORR_M2_THRESH)
                | ((m2Thresh[on] << AR_PHY_SFCORR_M2_THRESH_S)
                & AR_PHY_SFCORR_M2_THRESH));
        zfDelayWriteInternalReg(dev, AR_PHY_SFCORR,
                (HpPriv->regPHYSfcorr & ~AR_PHY_SFCORR_M2COUNT_THR)
                | ((m2CountThr[on] << AR_PHY_SFCORR_M2COUNT_THR_S)
                & AR_PHY_SFCORR_M2COUNT_THR));
        zfDelayWriteInternalReg(dev, AR_PHY_SFCORR_LOW,
                (HpPriv->regPHYSfcorrLow & ~AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW)
                | ((m2CountThrLow[on] << AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW_S)
                & AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW));

        if (on)
        {
            zfDelayWriteInternalReg(dev, AR_PHY_SFCORR_LOW,
                    HpPriv->regPHYSfcorrLow | AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
        }
        else
        {
            zfDelayWriteInternalReg(dev, AR_PHY_SFCORR_LOW,
                    HpPriv->regPHYSfcorrLow & ~AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
        }
        zfFlushDelayWrite(dev);
        if (!on != aniState->ofdmWeakSigDetectOff)
        {
            if (on)
                HpPriv->stats.ast_ani_ofdmon++;
            else
                HpPriv->stats.ast_ani_ofdmoff++;
            aniState->ofdmWeakSigDetectOff = !on;
        }
        break;
    }
    case ZM_HAL_ANI_CCK_WEAK_SIGNAL_THR:
    {
        const TABLE weakSigThrCck = { 8, 6 };
        u32_t high = param ? 1 : 0;

        zfDelayWriteInternalReg(dev, AR_PHY_CCK_DETECT,
                (HpPriv->regPHYCckDetect & ~AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK)
                | ((weakSigThrCck[high] << AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK_S)
                & AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK));
        zfFlushDelayWrite(dev);
        if (high != aniState->cckWeakSigThreshold)
        {
            if (high)
                HpPriv->stats.ast_ani_cckhigh++;
            else
                HpPriv->stats.ast_ani_ccklow++;
            aniState->cckWeakSigThreshold = (u8_t)high;
        }
        break;
    }
    case ZM_HAL_ANI_FIRSTEP_LEVEL:
    {
        const TABLE firstep = { 0, 4, 8 };
        u32_t level = param;

        if (level >= ARRAY_SIZE(firstep))
        {
            zm_debug_msg1("level out of range, desired level : ", level);
            zm_debug_msg1("max level : ", ARRAY_SIZE(firstep));
            return FALSE;
        }
        zfDelayWriteInternalReg(dev, AR_PHY_FIND_SIG,
                (HpPriv->regPHYFindSig & ~AR_PHY_FIND_SIG_FIRSTEP)
                | ((firstep[level] << AR_PHY_FIND_SIG_FIRSTEP_S)
                & AR_PHY_FIND_SIG_FIRSTEP));
        zfFlushDelayWrite(dev);
        if (level > aniState->firstepLevel)
            HpPriv->stats.ast_ani_stepup++;
        else if (level < aniState->firstepLevel)
            HpPriv->stats.ast_ani_stepdown++;
        aniState->firstepLevel = (u8_t)level;
        break;
    }
    case ZM_HAL_ANI_SPUR_IMMUNITY_LEVEL:
    {
        const TABLE cycpwrThr1 = { 2, 4, 6, 8, 10, 12, 14, 16 };
        u32_t level = param;

        if (level >= ARRAY_SIZE(cycpwrThr1))
        {
            zm_debug_msg1("level out of range, desired level : ", level);
            zm_debug_msg1("max level : ", ARRAY_SIZE(cycpwrThr1));
            return FALSE;
        }
        zfDelayWriteInternalReg(dev, AR_PHY_TIMING5,
                (HpPriv->regPHYTiming5 & ~AR_PHY_TIMING5_CYCPWR_THR1)
                | ((cycpwrThr1[level] << AR_PHY_TIMING5_CYCPWR_THR1_S)
                & AR_PHY_TIMING5_CYCPWR_THR1));
        zfFlushDelayWrite(dev);
        if (level > aniState->spurImmunityLevel)
            HpPriv->stats.ast_ani_spurup++;
        else if (level < aniState->spurImmunityLevel)
            HpPriv->stats.ast_ani_spurdown++;
        aniState->spurImmunityLevel = (u8_t)level;
        break;
    }
    case ZM_HAL_ANI_PRESENT:
        break;
#ifdef AH_PRIVATE_DIAG
    case ZM_HAL_ANI_MODE:
        if (param == 0)
        {
            HpPriv->procPhyErr &= ~ZM_HAL_PROCESS_ANI;
            /* Turn off HW counters if we have them */
            zfHpAniDetach(dev);
            //zfHpSetRxFilter(dev, zfHpGetRxFilter(dev) &~ HAL_RX_FILTER_PHYERR);
        }
        else
        {           /* normal/auto mode */
            HpPriv->procPhyErr |= ZM_HAL_PROCESS_ANI;
            if (HpPriv->hasHwPhyCounters)
            {
                //zfHpSetRxFilter(dev, zfHpGetRxFilter(dev) &~ HAL_RX_FILTER_PHYERR);
            }
            else
            {
                //zfHpSetRxFilter(dev, zfHpGetRxFilter(dev) | HAL_RX_FILTER_PHYERR);
            }
        }
        break;
    case ZM_HAL_ANI_PHYERR_RESET:
        HpPriv->stats.ast_ani_ofdmerrs = 0;
        HpPriv->stats.ast_ani_cckerrs = 0;
        break;
#endif /* AH_PRIVATE_DIAG */
    default:
        zm_debug_msg1("invalid cmd ", cmd);
        return FALSE;
    }
    return TRUE;
}

void zfHpAniRestart(zdev_t* dev)
{
    struct zsAniState *aniState;
    struct zsHpPriv *HpPriv;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv*)wd->hpPrivate;
    aniState = HpPriv->curani;

    aniState->listenTime = 0;
    if (HpPriv->hasHwPhyCounters)
    {
        //if (aniState->ofdmTrigHigh > AR_PHY_COUNTMAX)
        //{
        //    aniState->ofdmPhyErrBase = 0;
        //    zm_debug_msg0("OFDM Trigger is too high for hw counters");
        //}
        //else
        //    aniState->ofdmPhyErrBase = AR_PHY_COUNTMAX - aniState->ofdmTrigHigh;
        //if (aniState->cckTrigHigh > AR_PHY_COUNTMAX)
        //{
        //    aniState->cckPhyErrBase = 0;
        //    zm_debug_msg0("CCK Trigger is too high for hw counters");
        //}
        //else
        //    aniState->cckPhyErrBase = AR_PHY_COUNTMAX - aniState->cckTrigHigh;
        //zm_debug_msg2("Writing ofdmbase = 0x", aniState->ofdmPhyErrBase);
        //zm_debug_msg2("Writing cckbase = 0x", aniState->cckPhyErrBase);
        //OS_REG_WRITE(ah, AR_PHY_ERR_1, aniState->ofdmPhyErrBase);
        //OS_REG_WRITE(ah, AR_PHY_ERR_2, aniState->cckPhyErrBase);
        //OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
        //OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);
        aniState->ofdmPhyErrBase = 0;
        aniState->cckPhyErrBase = 0;
    }
    aniState->ofdmPhyErrCount = 0;
    aniState->cckPhyErrCount = 0;
}

void zfHpAniOfdmErrTrigger(zdev_t* dev)
{
    struct zsAniState *aniState;
    s32_t rssi;
    struct zsHpPriv *HpPriv;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv*)wd->hpPrivate;

    //HALASSERT(chan != NULL);

    if ((HpPriv->procPhyErr & ZM_HAL_PROCESS_ANI) == 0)
        return;

    aniState = HpPriv->curani;
    /* First, raise noise immunity level, up to max */
    if (aniState->noiseImmunityLevel < ZM_HAL_NOISE_IMMUNE_MAX)
    {
        zfHpAniControl(dev, ZM_HAL_ANI_NOISE_IMMUNITY_LEVEL, aniState->noiseImmunityLevel + 1);
        return;
    }
    /* then, raise spur immunity level, up to max */
    if (aniState->spurImmunityLevel < ZM_HAL_SPUR_IMMUNE_MAX)
    {
        zfHpAniControl(dev, ZM_HAL_ANI_SPUR_IMMUNITY_LEVEL, aniState->spurImmunityLevel + 1);
        return;
    }
    rssi = BEACON_RSSI(dev);
    if (rssi > aniState->rssiThrHigh)
    {
        /*
         * Beacon rssi is high, can turn off ofdm weak sig detect.
         */
        if (!aniState->ofdmWeakSigDetectOff)
        {
            zfHpAniControl(dev, ZM_HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION, FALSE);
            zfHpAniControl(dev, ZM_HAL_ANI_SPUR_IMMUNITY_LEVEL, 0);
            return;
        }
        /*
         * If weak sig detect is already off, as last resort, raise
         * first step level
         */
        if (aniState->firstepLevel < ZM_HAL_FIRST_STEP_MAX)
        {
            zfHpAniControl(dev, ZM_HAL_ANI_FIRSTEP_LEVEL, aniState->firstepLevel + 1);
            return;
        }
    }
    else if (rssi > aniState->rssiThrLow)
    {
        /*
         * Beacon rssi in mid range, need ofdm weak signal detect,
         * but we can raise firststepLevel
         */
        if (aniState->ofdmWeakSigDetectOff)
            zfHpAniControl(dev, ZM_HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION, TRUE);
        if (aniState->firstepLevel < ZM_HAL_FIRST_STEP_MAX)
            zfHpAniControl(dev, ZM_HAL_ANI_FIRSTEP_LEVEL, aniState->firstepLevel + 1);
        return;
    }
    else
    {
        /*
         * Beacon rssi is low, if in 11b/g mode, turn off ofdm
         * weak sign detction and zero firstepLevel to maximize
         * CCK sensitivity
         */
        if (wd->frequency < 3000)
        {
            if (!aniState->ofdmWeakSigDetectOff)
                zfHpAniControl(dev, ZM_HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION, FALSE);
            if (aniState->firstepLevel > 0)
                zfHpAniControl(dev, ZM_HAL_ANI_FIRSTEP_LEVEL, 0);
            return;
        }
    }
}

void zfHpAniCckErrTrigger(zdev_t* dev)
{
    struct zsAniState *aniState;
    s32_t rssi;
    struct zsHpPriv *HpPriv;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv*)wd->hpPrivate;

    //HALASSERT(chan != NULL);

    if ((HpPriv->procPhyErr & ZM_HAL_PROCESS_ANI) == 0)
        return;

    /* first, raise noise immunity level, up to max */
    aniState = HpPriv->curani;
    if (aniState->noiseImmunityLevel < ZM_HAL_NOISE_IMMUNE_MAX)
    {
        zfHpAniControl(dev, ZM_HAL_ANI_NOISE_IMMUNITY_LEVEL,
                 aniState->noiseImmunityLevel + 1);
        return;
    }
    rssi = BEACON_RSSI(dev);
    if (rssi >  aniState->rssiThrLow)
    {
        /*
         * Beacon signal in mid and high range, raise firsteplevel.
         */
        if (aniState->firstepLevel < ZM_HAL_FIRST_STEP_MAX)
            zfHpAniControl(dev, ZM_HAL_ANI_FIRSTEP_LEVEL, aniState->firstepLevel + 1);
    }
    else
    {
        /*
         * Beacon rssi is low, zero firstepLevel to maximize
         * CCK sensitivity.
         */
        if (wd->frequency < 3000)
        {
            if (aniState->firstepLevel > 0)
                zfHpAniControl(dev, ZM_HAL_ANI_FIRSTEP_LEVEL, 0);
        }
    }
}

void zfHpAniLowerImmunity(zdev_t* dev)
{
    struct zsAniState *aniState;
    s32_t rssi;
    struct zsHpPriv *HpPriv;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv*)wd->hpPrivate;
    aniState = HpPriv->curani;

    rssi = BEACON_RSSI(dev);
    if (rssi > aniState->rssiThrHigh)
    {
        /*
         * Beacon signal is high, leave ofdm weak signal detection off
         * or it may oscillate. Let it fall through.
         */
    }
    else if (rssi > aniState->rssiThrLow)
    {
        /*
         * Beacon rssi in mid range, turn on ofdm weak signal
         * detection or lower first step level.
         */
        if (aniState->ofdmWeakSigDetectOff)
        {
            zfHpAniControl(dev, ZM_HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION, TRUE);
            return;
        }
        if (aniState->firstepLevel > 0)
        {
            zfHpAniControl(dev, ZM_HAL_ANI_FIRSTEP_LEVEL, aniState->firstepLevel - 1);
            return;
        }
    }
    else
    {
        /*
         * Beacon rssi is low, reduce first step level.
         */
        if (aniState->firstepLevel > 0)
        {
            zfHpAniControl(dev, ZM_HAL_ANI_FIRSTEP_LEVEL, aniState->firstepLevel - 1);
            return;
        }
    }
    /* then lower spur immunity level, down to zero */
    if (aniState->spurImmunityLevel > 0)
    {
        zfHpAniControl(dev, ZM_HAL_ANI_SPUR_IMMUNITY_LEVEL, aniState->spurImmunityLevel - 1);
        return;
    }
    /*
     * if all else fails, lower noise immunity level down to a min value
     * zero for now
     */
    if (aniState->noiseImmunityLevel > 0)
    {
        zfHpAniControl(dev, ZM_HAL_ANI_NOISE_IMMUNITY_LEVEL, aniState->noiseImmunityLevel - 1);
        return;
    }
}

#define CLOCK_RATE 44000    /* XXX use mac_usec or similar */
/* convert HW counter values to ms using 11g clock rate, goo9d enough
   for 11a and Turbo */

/*
 * Return an approximation of the time spent ``listening'' by
 * deducting the cycles spent tx'ing and rx'ing from the total
 * cycle count since our last call.  A return value <0 indicates
 * an invalid/inconsistent time.
 */
s32_t zfHpAniGetListenTime(zdev_t* dev)
{
    struct zsAniState *aniState;
    u32_t txFrameCount, rxFrameCount, cycleCount;
    s32_t listenTime;
    struct zsHpPriv *HpPriv;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv*)wd->hpPrivate;

    txFrameCount = 0;//OS_REG_READ(ah, AR_TFCNT);
    rxFrameCount = 0;//OS_REG_READ(ah, AR_RFCNT);
    cycleCount = 0;//OS_REG_READ(ah, AR_CCCNT);

    aniState = HpPriv->curani;
    if (aniState->cycleCount == 0 || aniState->cycleCount > cycleCount)
    {
        /*
         * Cycle counter wrap (or initial call); it's not possible
         * to accurately calculate a value because the registers
         * right shift rather than wrap--so punt and return 0.
         */
        listenTime = 0;
        HpPriv->stats.ast_ani_lzero++;
    }
    else
    {
        s32_t ccdelta = cycleCount - aniState->cycleCount;
        s32_t rfdelta = rxFrameCount - aniState->rxFrameCount;
        s32_t tfdelta = txFrameCount - aniState->txFrameCount;
        listenTime = (ccdelta - rfdelta - tfdelta) / CLOCK_RATE;
    }
    aniState->cycleCount = cycleCount;
    aniState->txFrameCount = txFrameCount;
    aniState->rxFrameCount = rxFrameCount;
    return listenTime;
}

/*
 * Do periodic processing.  This routine is called from the
 * driver's rx interrupt handler after processing frames.
 */
void zfHpAniArPoll(zdev_t* dev, u32_t listenTime, u32_t phyCnt1, u32_t phyCnt2)
{
    struct zsAniState *aniState;
    //s32_t listenTime;
    struct zsHpPriv *HpPriv;

    zmw_get_wlan_dev(dev);
    HpPriv = (struct zsHpPriv*)wd->hpPrivate;

    /*
     * Since we're called from end of rx tasklet, we also check for
     * AR processing now
     */

    aniState = HpPriv->curani;
    //HpPriv->stats.ast_nodestats = *stats;       /* XXX optimize? */

    //listenTime = zfHpAniGetListenTime(dev);
    //if (listenTime < 0)
    //{
    //    HpPriv->stats.ast_ani_lneg++;
    //    /* restart ANI period if listenTime is invalid */
    //    zfHpAniRestart(dev);
    //    return;
    //}
    /* XXX beware of overflow? */
    aniState->listenTime += listenTime;

    if (HpPriv->hasHwPhyCounters)
    {
        //u32_t phyCnt1, phyCnt2;
        u32_t ofdmPhyErrCnt, cckPhyErrCnt;

        /* NB: these are not reset-on-read */
        //phyCnt1 = 0;//OS_REG_READ(ah, AR_PHY_ERR_1);
        //phyCnt2 = 0;//OS_REG_READ(ah, AR_PHY_ERR_2);
        /* XXX sometimes zero, why? */
        //if (phyCnt1 < aniState->ofdmPhyErrBase ||
        //    phyCnt2 < aniState->cckPhyErrBase)
        //{
        //    if (phyCnt1 < aniState->ofdmPhyErrBase)
        //    {
        //        zm_debug_msg2("phyCnt1 = 0x", phyCnt1);
        //        zm_debug_msg2("resetting counter value to 0x", aniState->ofdmPhyErrBase);
        //        //OS_REG_WRITE(ah, AR_PHY_ERR_1, aniState->ofdmPhyErrBase);
        //        //OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
        //    }
        //    if (phyCnt2 < aniState->cckPhyErrBase)
        //    {
        //        zm_debug_msg2("phyCnt2 = 0x", phyCnt2);
        //        zm_debug_msg2("resetting counter value to 0x", aniState->cckPhyErrBase);
        //        //OS_REG_WRITE(ah, AR_PHY_ERR_2, aniState->cckPhyErrBase);
        //        //OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);
        //    }
        //    return;     /* XXX */
        //}
        /* NB: only use ast_ani_*errs with AH_PRIVATE_DIAG */
        //ofdmPhyErrCnt = phyCnt1 - aniState->ofdmPhyErrBase;
        //HpPriv->stats.ast_ani_ofdmerrs += ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
        //aniState->ofdmPhyErrCount = ofdmPhyErrCnt;
        ofdmPhyErrCnt = phyCnt1;
        HpPriv->stats.ast_ani_ofdmerrs += ofdmPhyErrCnt;
        aniState->ofdmPhyErrCount += ofdmPhyErrCnt;

        //cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
        //HpPriv->stats.ast_ani_cckerrs += cckPhyErrCnt - aniState->cckPhyErrCount;
        //aniState->cckPhyErrCount = cckPhyErrCnt;
        cckPhyErrCnt = phyCnt2;
        HpPriv->stats.ast_ani_cckerrs += cckPhyErrCnt;
        aniState->cckPhyErrCount += cckPhyErrCnt;
    }
    /*
     * If ani is not enabled, return after we've collected
     * statistics
     */
    if ((HpPriv->procPhyErr & ZM_HAL_PROCESS_ANI) == 0)
        return;
    if (aniState->listenTime > 5 * HpPriv->aniPeriod)
    {
        /*
         * Check to see if need to lower immunity if
         * 5 aniPeriods have passed
         */
        if (aniState->ofdmPhyErrCount <= aniState->listenTime *
             aniState->ofdmTrigLow/1000 &&
            aniState->cckPhyErrCount <= aniState->listenTime *
             aniState->cckTrigLow/1000)
            zfHpAniLowerImmunity(dev);
        zfHpAniRestart(dev);
    }
    else if (aniState->listenTime > HpPriv->aniPeriod)
    {
        /* check to see if need to raise immunity */
        if (aniState->ofdmPhyErrCount > aniState->listenTime *
            aniState->ofdmTrigHigh / 1000)
        {
            zfHpAniOfdmErrTrigger(dev);
            zfHpAniRestart(dev);
        }
        else if (aniState->cckPhyErrCount > aniState->listenTime *
               aniState->cckTrigHigh / 1000)
        {
            zfHpAniCckErrTrigger(dev);
            zfHpAniRestart(dev);
        }
    }
}
