//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#include "ar6000_drv.h"
#include "ieee80211_ioctl.h"
#include "ar6kap_common.h"
#include "targaddrs.h"
#include "a_hci.h"
#include "wlan_config.h"

extern int enablerssicompensation;
A_UINT32 tcmdRxFreq;
extern unsigned int wmitimeout;
extern A_WAITQUEUE_HEAD arEvent;
extern int tspecCompliance;
extern int bmienable;
extern int bypasswmi;
extern int loghci;

static int
ar6000_ioctl_get_roam_tbl(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if(wmi_get_roam_tbl_cmd(ar->arWmi) != A_OK) {
        return -EIO;
    }

    return 0;
}

static int
ar6000_ioctl_get_roam_data(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    /* currently assume only roam times are required */
    if(wmi_get_roam_data_cmd(ar->arWmi, ROAM_DATA_TIME) != A_OK) {
        return -EIO;
    }


    return 0;
}

static int
ar6000_ioctl_set_roam_ctrl(struct net_device *dev, char *userdata)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_ROAM_CTRL_CMD cmd;
    A_UINT8 size = sizeof(cmd);

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, size)) {
        return -EFAULT;
    }

    if (cmd.roamCtrlType == WMI_SET_HOST_BIAS) {
        if (cmd.info.bssBiasInfo.numBss > 1) {
            size += (cmd.info.bssBiasInfo.numBss - 1) * sizeof(WMI_BSS_BIAS);
        }
    }

    if (copy_from_user(&cmd, userdata, size)) {
        return -EFAULT;
    }

    if(wmi_set_roam_ctrl_cmd(ar->arWmi, &cmd, size) != A_OK) {
        return -EIO;
    }

    return 0;
}

static int
ar6000_ioctl_set_powersave_timers(struct net_device *dev, char *userdata)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_POWERSAVE_TIMERS_POLICY_CMD cmd;
    A_UINT8 size = sizeof(cmd);

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, size)) {
        return -EFAULT;
    }

    if (copy_from_user(&cmd, userdata, size)) {
        return -EFAULT;
    }

    if(wmi_set_powersave_timers_cmd(ar->arWmi, &cmd, size) != A_OK) {
        return -EIO;
    }

    return 0;
}

static int
ar6000_ioctl_set_qos_supp(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_QOS_SUPP_CMD cmd;
    A_STATUS ret;

    if ((dev->flags & IFF_UP) != IFF_UP) {
        return -EIO;
    }
    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    ret = wmi_set_qos_supp_cmd(ar->arWmi, cmd.status);

    switch (ret) {
        case A_OK:
            return 0;
        case A_EBUSY :
            return -EBUSY;
        case A_NO_MEMORY:
            return -ENOMEM;
        case A_EINVAL:
        default:
            return -EFAULT;
    }
}

static int
ar6000_ioctl_set_wmm(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_WMM_CMD cmd;
    A_STATUS ret;

    if ((dev->flags & IFF_UP) != IFF_UP) {
        return -EIO;
    }
    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    if (cmd.status == WMI_WMM_ENABLED) {
        ar->arWmmEnabled = TRUE;
    } else {
        ar->arWmmEnabled = FALSE;
    }

    ret = wmi_set_wmm_cmd(ar->arWmi, cmd.status);

    switch (ret) {
        case A_OK:
            return 0;
        case A_EBUSY :
            return -EBUSY;
        case A_NO_MEMORY:
            return -ENOMEM;
        case A_EINVAL:
        default:
            return -EFAULT;
    }
}

static int
ar6000_ioctl_set_txop(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_WMM_TXOP_CMD cmd;
    A_STATUS ret;

    if ((dev->flags & IFF_UP) != IFF_UP) {
        return -EIO;
    }
    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    ret = wmi_set_wmm_txop(ar->arWmi, cmd.txopEnable);

    switch (ret) {
        case A_OK:
            return 0;
        case A_EBUSY :
            return -EBUSY;
        case A_NO_MEMORY:
            return -ENOMEM;
        case A_EINVAL:
        default:
            return -EFAULT;
    }
}

static int
ar6000_ioctl_get_rd(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    A_STATUS ret = 0;

    if ((dev->flags & IFF_UP) != IFF_UP || ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if(copy_to_user((char *)((unsigned int*)rq->ifr_data + 1),
                            &ar->arRegCode, sizeof(ar->arRegCode)))
        ret = -EFAULT;

    return ret;
}

static int
ar6000_ioctl_set_country(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_AP_SET_COUNTRY_CMD cmd;
    A_STATUS ret;

    if ((dev->flags & IFF_UP) != IFF_UP) {
        return -EIO;
    }
    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    ar->ap_profile_flag = 1; /* There is a change in profile */

    ret = wmi_set_country(ar->arWmi, cmd.countryCode);
    A_MEMCPY(ar->ap_country_code, cmd.countryCode, 3);

    switch (ret) {
        case A_OK:
            return 0;
        case A_EBUSY :
            return -EBUSY;
        case A_NO_MEMORY:
            return -ENOMEM;
        case A_EINVAL:
        default:
            return -EFAULT;
    }
}


/* Get power mode command */
static int
ar6000_ioctl_get_power_mode(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_POWER_MODE_CMD power_mode;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    power_mode.powerMode = wmi_get_power_mode_cmd(ar->arWmi);
    if (copy_to_user(rq->ifr_data, &power_mode, sizeof(WMI_POWER_MODE_CMD))) {
        ret = -EFAULT;
    }

    return ret;
}


static int
ar6000_ioctl_set_channelParams(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_CHANNEL_PARAMS_CMD cmd, *cmdp;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if( (ar->arNextMode == AP_NETWORK) && (cmd.numChannels || cmd.scanParam) ) {
        A_PRINTF("ERROR: Only wmode is allowed in AP mode\n");
        return -EIO;
    }

    if (cmd.numChannels > 1) {
        cmdp = A_MALLOC(130);
        if (copy_from_user(cmdp, rq->ifr_data,
                           sizeof (*cmdp) +
                           ((cmd.numChannels - 1) * sizeof(A_UINT16))))
        {
            kfree(cmdp);
            return -EFAULT;
        }
    } else {
        cmdp = &cmd;
    }

    if ((ar->arPhyCapability == WMI_11G_CAPABILITY) &&
        ((cmdp->phyMode == WMI_11A_MODE) || (cmdp->phyMode == WMI_11AG_MODE)))
    {
        ret = -EINVAL;
    }

    if (!ret &&
        (wmi_set_channelParams_cmd(ar->arWmi, cmdp->scanParam, cmdp->phyMode,
                                   cmdp->numChannels, cmdp->channelList)
         != A_OK))
    {
        ret = -EIO;
    }

    if (cmd.numChannels > 1) {
        kfree(cmdp);
    }

    ar->ap_wmode = cmdp->phyMode;
    /* Set the profile change flag to allow a commit cmd */
    ar->ap_profile_flag = 1;

    return ret;
}


static int
ar6000_ioctl_set_snr_threshold(struct net_device *dev, struct ifreq *rq)
{

    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SNR_THRESHOLD_PARAMS_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if( wmi_set_snr_threshold_params(ar->arWmi, &cmd) != A_OK ) {
        ret = -EIO;
    }

    return ret;
}

static int
ar6000_ioctl_set_rssi_threshold(struct net_device *dev, struct ifreq *rq)
{
#define SWAP_THOLD(thold1, thold2) do { \
    USER_RSSI_THOLD tmpThold;           \
    tmpThold.tag = thold1.tag;          \
    tmpThold.rssi = thold1.rssi;        \
    thold1.tag = thold2.tag;            \
    thold1.rssi = thold2.rssi;          \
    thold2.tag = tmpThold.tag;          \
    thold2.rssi = tmpThold.rssi;        \
} while (0)

    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_RSSI_THRESHOLD_PARAMS_CMD cmd;
    USER_RSSI_PARAMS rssiParams;
    A_INT32 i, j;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user((char *)&rssiParams, (char *)((unsigned int *)rq->ifr_data + 1), sizeof(USER_RSSI_PARAMS))) {
        return -EFAULT;
    }
    cmd.weight = rssiParams.weight;
    cmd.pollTime = rssiParams.pollTime;

    A_MEMCPY(ar->rssi_map, &rssiParams.tholds, sizeof(ar->rssi_map));
    /*
     *  only 6 elements, so use bubble sorting, in ascending order
     */
    for (i = 5; i > 0; i--) {
        for (j = 0; j < i; j++) { /* above tholds */
            if (ar->rssi_map[j+1].rssi < ar->rssi_map[j].rssi) {
                SWAP_THOLD(ar->rssi_map[j+1], ar->rssi_map[j]);
            } else if (ar->rssi_map[j+1].rssi == ar->rssi_map[j].rssi) {
                return EFAULT;
            }
        }
    }
    for (i = 11; i > 6; i--) {
        for (j = 6; j < i; j++) { /* below tholds */
            if (ar->rssi_map[j+1].rssi < ar->rssi_map[j].rssi) {
                SWAP_THOLD(ar->rssi_map[j+1], ar->rssi_map[j]);
            } else if (ar->rssi_map[j+1].rssi == ar->rssi_map[j].rssi) {
                return EFAULT;
            }
        }
    }

#ifdef DEBUG
    for (i = 0; i < 12; i++) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("thold[%d].tag: %d, thold[%d].rssi: %d \n",
                i, ar->rssi_map[i].tag, i, ar->rssi_map[i].rssi));
    }
#endif

    if (enablerssicompensation) {
        for (i = 0; i < 6; i++)
            ar->rssi_map[i].rssi = rssi_compensation_reverse_calc(ar, ar->rssi_map[i].rssi, TRUE);
        for (i = 6; i < 12; i++)
            ar->rssi_map[i].rssi = rssi_compensation_reverse_calc(ar, ar->rssi_map[i].rssi, FALSE);
    }

    cmd.thresholdAbove1_Val = ar->rssi_map[0].rssi;
    cmd.thresholdAbove2_Val = ar->rssi_map[1].rssi;
    cmd.thresholdAbove3_Val = ar->rssi_map[2].rssi;
    cmd.thresholdAbove4_Val = ar->rssi_map[3].rssi;
    cmd.thresholdAbove5_Val = ar->rssi_map[4].rssi;
    cmd.thresholdAbove6_Val = ar->rssi_map[5].rssi;
    cmd.thresholdBelow1_Val = ar->rssi_map[6].rssi;
    cmd.thresholdBelow2_Val = ar->rssi_map[7].rssi;
    cmd.thresholdBelow3_Val = ar->rssi_map[8].rssi;
    cmd.thresholdBelow4_Val = ar->rssi_map[9].rssi;
    cmd.thresholdBelow5_Val = ar->rssi_map[10].rssi;
    cmd.thresholdBelow6_Val = ar->rssi_map[11].rssi;

    if( wmi_set_rssi_threshold_params(ar->arWmi, &cmd) != A_OK ) {
        ret = -EIO;
    }

    return ret;
}

static int
ar6000_ioctl_set_lq_threshold(struct net_device *dev, struct ifreq *rq)
{

    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_LQ_THRESHOLD_PARAMS_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int *)rq->ifr_data + 1), sizeof(cmd))) {
        return -EFAULT;
    }

    if( wmi_set_lq_threshold_params(ar->arWmi, &cmd) != A_OK ) {
        ret = -EIO;
    }

    return ret;
}


static int
ar6000_ioctl_set_probedSsid(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_PROBED_SSID_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_probedSsid_cmd(ar->arWmi, cmd.entryIndex, cmd.flag, cmd.ssidLength,
                                  cmd.ssid) != A_OK)
    {
        ret = -EIO;
    }

    return ret;
}

static int
ar6000_ioctl_set_badAp(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_ADD_BAD_AP_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if (cmd.badApIndex > WMI_MAX_BAD_AP_INDEX) {
        return -EIO;
    }

    if (A_MEMCMP(cmd.bssid, null_mac, AR6000_ETH_ADDR_LEN) == 0) {
        /*
         * This is a delete badAP.
         */
        if (wmi_deleteBadAp_cmd(ar->arWmi, cmd.badApIndex) != A_OK) {
            ret = -EIO;
        }
    } else {
        if (wmi_addBadAp_cmd(ar->arWmi, cmd.badApIndex, cmd.bssid) != A_OK) {
            ret = -EIO;
        }
    }

    return ret;
}

static int
ar6000_ioctl_create_qos(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_CREATE_PSTREAM_CMD cmd;
    A_STATUS ret;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    ret = wmi_verify_tspec_params(&cmd, tspecCompliance);
    if (ret == A_OK)
        ret = wmi_create_pstream_cmd(ar->arWmi, &cmd);

    switch (ret) {
        case A_OK:
            return 0;
        case A_EBUSY :
            return -EBUSY;
        case A_NO_MEMORY:
            return -ENOMEM;
        case A_EINVAL:
        default:
            return -EFAULT;
    }
}

static int
ar6000_ioctl_delete_qos(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_DELETE_PSTREAM_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    ret = wmi_delete_pstream_cmd(ar->arWmi, cmd.trafficClass, cmd.tsid);

    switch (ret) {
        case A_OK:
            return 0;
        case A_EBUSY :
            return -EBUSY;
        case A_NO_MEMORY:
            return -ENOMEM;
        case A_EINVAL:
        default:
            return -EFAULT;
    }
}

static int
ar6000_ioctl_get_qos_queue(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    struct ar6000_queuereq qreq;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if( copy_from_user(&qreq, rq->ifr_data,
                  sizeof(struct ar6000_queuereq)))
        return -EFAULT;

    qreq.activeTsids = wmi_get_mapped_qos_queue(ar->arWmi, qreq.trafficClass);

    if (copy_to_user(rq->ifr_data, &qreq,
                 sizeof(struct ar6000_queuereq)))
    {
        ret = -EFAULT;
    }

    return ret;
}

#ifdef CONFIG_HOST_TCMD_SUPPORT
static A_STATUS
ar6000_ioctl_tcmd_get_rx_report(struct net_device *dev,
                                 struct ifreq *rq, A_UINT8 *data, A_UINT32 len)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    A_UINT32    buf[4+TCMD_MAX_RATES];
    int ret = 0;

    if (ar->bIsDestroyProgress) {
        return -EBUSY;
    }

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    if (ar->bIsDestroyProgress) {
        up(&ar->arSem);
        return -EBUSY;
    }

    ar->tcmdRxReport = 0;
    if (wmi_test_cmd(ar->arWmi, data, len) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arEvent, ar->tcmdRxReport != 0, wmitimeout * HZ);

    if (signal_pending(current)) {
        ret = -EINTR;
    }

    buf[0] = ar->tcmdRxTotalPkt;
    buf[1] = ar->tcmdRxRssi;
    buf[2] = ar->tcmdRxcrcErrPkt;
    buf[3] = ar->tcmdRxsecErrPkt;
    A_MEMCPY(((A_UCHAR *)buf)+(4*sizeof(A_UINT32)), ar->tcmdRateCnt, sizeof(ar->tcmdRateCnt));
    A_MEMCPY(((A_UCHAR *)buf)+(4*sizeof(A_UINT32))+(TCMD_MAX_RATES *sizeof(A_UINT16)), ar->tcmdRateCntShortGuard, sizeof(ar->tcmdRateCntShortGuard));

    if (!ret && copy_to_user(rq->ifr_data, buf, sizeof(buf))) {
        ret = -EFAULT;
    }

    up(&ar->arSem);

    return ret;
}

void
ar6000_tcmd_rx_report_event(void *devt, A_UINT8 * results, int len)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;
    TCMD_CONT_RX * rx_rep = (TCMD_CONT_RX *)results;

    if (enablerssicompensation) {
        rx_rep->u.report.rssiInDBm = rssi_compensation_calc_tcmd(tcmdRxFreq, rx_rep->u.report.rssiInDBm,rx_rep->u.report.totalPkt);
    }


    ar->tcmdRxTotalPkt = rx_rep->u.report.totalPkt;
    ar->tcmdRxRssi = rx_rep->u.report.rssiInDBm;
    ar->tcmdRxcrcErrPkt = rx_rep->u.report.crcErrPkt;
    ar->tcmdRxsecErrPkt = rx_rep->u.report.secErrPkt;
    ar->tcmdRxReport = 1;
    A_MEMZERO(ar->tcmdRateCnt,  sizeof(ar->tcmdRateCnt));
    A_MEMZERO(ar->tcmdRateCntShortGuard,  sizeof(ar->tcmdRateCntShortGuard));
    A_MEMCPY(ar->tcmdRateCnt, rx_rep->u.report.rateCnt, sizeof(ar->tcmdRateCnt));
    A_MEMCPY(ar->tcmdRateCntShortGuard, rx_rep->u.report.rateCntShortGuard, sizeof(ar->tcmdRateCntShortGuard));

    wake_up(&arEvent);
}
#endif /* CONFIG_HOST_TCMD_SUPPORT*/

static int
ar6000_ioctl_set_error_report_bitmask(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_TARGET_ERROR_REPORT_BITMASK cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    ret = wmi_set_error_report_bitmask(ar->arWmi, cmd.bitmask);

    return  (ret==0 ? ret : -EINVAL);
}

static int
ar6000_clear_target_stats(struct net_device *dev)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    TARGET_STATS *pStats = &ar->arTargetStats;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
       return -EIO;
    }
    AR6000_SPIN_LOCK(&ar->arLock, 0);
    A_MEMZERO(pStats, sizeof(TARGET_STATS));
    AR6000_SPIN_UNLOCK(&ar->arLock, 0);
    return ret;
}

static int
ar6000_ioctl_get_target_stats(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    TARGET_STATS_CMD cmd;
    TARGET_STATS *pStats = &ar->arTargetStats;
    int ret = 0;

    if (ar->bIsDestroyProgress) {
        return -EBUSY;
    }
    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }
    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }
    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }
    if (ar->bIsDestroyProgress) {
        up(&ar->arSem);
        return -EBUSY;
    }

    ar->statsUpdatePending = TRUE;

    if(wmi_get_stats_cmd(ar->arWmi) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arEvent, ar->statsUpdatePending == FALSE, wmitimeout * HZ);

    if (signal_pending(current)) {
        ret = -EINTR;
    }

    if (!ret && copy_to_user(rq->ifr_data, pStats, sizeof(*pStats))) {
        ret = -EFAULT;
    }

    if (cmd.clearStats == 1) {
        ret = ar6000_clear_target_stats(dev);
    }

    up(&ar->arSem);

    return ret;
}

static int
ar6000_ioctl_get_ap_stats(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    A_UINT32 action; /* Allocating only the desired space on the frame. Declaring is as a WMI_AP_MODE_STAT variable results in exceeding the compiler imposed limit on the maximum frame size */
    WMI_AP_MODE_STAT *pStats = &ar->arAPStats;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }
    if (copy_from_user(&action, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(A_UINT32)))
    {
        return -EFAULT;
    }
    if (action == AP_CLEAR_STATS) {
        A_UINT8 i;
        AR6000_SPIN_LOCK(&ar->arLock, 0);
        for(i = 0; i < AP_MAX_NUM_STA; i++) {
            pStats->sta[i].tx_bytes = 0;
            pStats->sta[i].tx_pkts = 0;
            pStats->sta[i].tx_error = 0;
            pStats->sta[i].tx_discard = 0;
            pStats->sta[i].rx_bytes = 0;
            pStats->sta[i].rx_pkts = 0;
            pStats->sta[i].rx_error = 0;
            pStats->sta[i].rx_discard = 0;
        }
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        return ret;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    ar->statsUpdatePending = TRUE;

    if(wmi_get_stats_cmd(ar->arWmi) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arEvent, ar->statsUpdatePending == FALSE, wmitimeout * HZ);

    if (signal_pending(current)) {
        ret = -EINTR;
    }

    if (!ret && copy_to_user(rq->ifr_data, pStats, sizeof(*pStats))) {
        ret = -EFAULT;
    }

    up(&ar->arSem);

    return ret;
}

static int
ar6000_ioctl_set_access_params(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_ACCESS_PARAMS_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_access_params_cmd(ar->arWmi, cmd.ac, cmd.txop, cmd.eCWmin, cmd.eCWmax,
                                  cmd.aifsn) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

    return (ret);
}

static int
ar6000_ioctl_set_disconnect_timeout(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_DISC_TIMEOUT_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_disctimeout_cmd(ar->arWmi, cmd.disconnectTimeout) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

    return (ret);
}

static int
ar6000_xioctl_set_voice_pkt_size(struct net_device *dev, char * userdata)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_VOICE_PKT_SIZE_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_voice_pkt_size_cmd(ar->arWmi, cmd.voicePktSize) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }


    return (ret);
}

static int
ar6000_xioctl_set_max_sp_len(struct net_device *dev, char * userdata)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_MAX_SP_LEN_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_max_sp_len_cmd(ar->arWmi, cmd.maxSPLen) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

    return (ret);
}


static int
ar6000_xioctl_set_bt_status_cmd(struct net_device *dev, char * userdata)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_BT_STATUS_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_bt_status_cmd(ar->arWmi, cmd.streamType, cmd.status) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

    return (ret);
}

static int
ar6000_xioctl_set_bt_params_cmd(struct net_device *dev, char * userdata)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_SET_BT_PARAMS_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_bt_params_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

    return (ret);
}

static int
ar6000_xioctl_set_btcoex_fe_ant_cmd(struct net_device * dev, char * userdata)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
	WMI_SET_BTCOEX_FE_ANT_CMD cmd;
    int ret = 0;

	if (ar->arWmiReady == FALSE) {
		return -EIO;
	}
	if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
		return -EFAULT;
	}

    if (wmi_set_btcoex_fe_ant_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

	return(ret);
}

static int
ar6000_xioctl_set_btcoex_colocated_bt_dev_cmd(struct net_device * dev, char * userdata)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
	WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD cmd;
    int ret = 0;

	if (ar->arWmiReady == FALSE) {
		return -EIO;
	}

	if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
		return -EFAULT;
	}

    if (wmi_set_btcoex_colocated_bt_dev_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

	return(ret);
}

static int
ar6000_xioctl_set_btcoex_btinquiry_page_config_cmd(struct net_device * dev,  char * userdata)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
	WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD cmd;
    int ret = 0;

	if (ar->arWmiReady == FALSE) {
		return -EIO;
	}

	if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
		return -EFAULT;
	}

    if (wmi_set_btcoex_btinquiry_page_config_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

	return(ret);
}

static int
ar6000_xioctl_set_btcoex_sco_config_cmd(struct net_device * dev, char * userdata)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
	WMI_SET_BTCOEX_SCO_CONFIG_CMD cmd;
    int ret = 0;

	if (ar->arWmiReady == FALSE) {
		return -EIO;
	}

	if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
		return -EFAULT;
	}

    if (wmi_set_btcoex_sco_config_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

	return(ret);
}

static int
ar6000_xioctl_set_btcoex_a2dp_config_cmd(struct net_device * dev,
														char * userdata)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
	WMI_SET_BTCOEX_A2DP_CONFIG_CMD cmd;
    int ret = 0;

	if (ar->arWmiReady == FALSE) {
		return -EIO;
	}

	if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
		return -EFAULT;
	}

    if (wmi_set_btcoex_a2dp_config_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

	return(ret);
}

static int
ar6000_xioctl_set_btcoex_aclcoex_config_cmd(struct net_device * dev, char * userdata)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
	WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD cmd;
    int ret = 0;

	if (ar->arWmiReady == FALSE) {
		return -EIO;
	}

	if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
		return -EFAULT;
	}

    if (wmi_set_btcoex_aclcoex_config_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

	return(ret);
}

static int
ar60000_xioctl_set_btcoex_debug_cmd(struct net_device * dev, char * userdata)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
	WMI_SET_BTCOEX_DEBUG_CMD cmd;
    int ret = 0;

	if (ar->arWmiReady == FALSE) {
		return -EIO;
	}

	if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
		return -EFAULT;
	}

    if (wmi_set_btcoex_debug_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }

	return(ret);
}

static int
ar6000_xioctl_set_btcoex_bt_operating_status_cmd(struct net_device * dev, char * userdata)
{
     AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
     WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD cmd;
     int ret = 0;

    if (ar->arWmiReady == FALSE) {
	return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
	return -EFAULT;
    }

    if (wmi_set_btcoex_bt_operating_status_cmd(ar->arWmi, &cmd) == A_OK)
    {
        ret = 0;
    } else {
        ret = -EINVAL;
    }
    return(ret);
}

static int
ar6000_xioctl_get_btcoex_config_cmd(struct net_device * dev, char * userdata,
											struct ifreq *rq)
{

	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    AR6000_BTCOEX_CONFIG btcoexConfig;
    WMI_BTCOEX_CONFIG_EVENT *pbtcoexConfigEv = &ar->arBtcoexConfig;

    int ret = 0;

    if (ar->bIsDestroyProgress) {
            return -EBUSY;
    }
    if (ar->arWmiReady == FALSE) {
            return -EIO;
    }
	if (copy_from_user(&btcoexConfig.configCmd, userdata, sizeof(AR6000_BTCOEX_CONFIG))) {
		return -EFAULT;
	}
    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    if (wmi_get_btcoex_config_cmd(ar->arWmi, (WMI_GET_BTCOEX_CONFIG_CMD *)&btcoexConfig.configCmd) != A_OK)
    {
    	up(&ar->arSem);
    	return -EIO;
    }

    ar->statsUpdatePending = TRUE;

    wait_event_interruptible_timeout(arEvent, ar->statsUpdatePending == FALSE, wmitimeout * HZ);

    if (signal_pending(current)) {
       ret = -EINTR;
    }

    if (!ret && copy_to_user(btcoexConfig.configEvent, pbtcoexConfigEv, sizeof(WMI_BTCOEX_CONFIG_EVENT))) {
            ret = -EFAULT;
    }
    up(&ar->arSem);
    return ret;
}

static int
ar6000_xioctl_get_btcoex_stats_cmd(struct net_device * dev, char * userdata, struct ifreq *rq)
{
	AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    AR6000_BTCOEX_STATS btcoexStats;
    WMI_BTCOEX_STATS_EVENT *pbtcoexStats = &ar->arBtcoexStats;
    int ret = 0;

    if (ar->bIsDestroyProgress) {
            return -EBUSY;
    }
    if (ar->arWmiReady == FALSE) {
            return -EIO;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

	if (copy_from_user(&btcoexStats.statsEvent, userdata, sizeof(AR6000_BTCOEX_CONFIG))) {
		return -EFAULT;
	}

    if (wmi_get_btcoex_stats_cmd(ar->arWmi) != A_OK)
    {
    	up(&ar->arSem);
    	return -EIO;
    }

    ar->statsUpdatePending = TRUE;

    wait_event_interruptible_timeout(arEvent, ar->statsUpdatePending == FALSE, wmitimeout * HZ);

    if (signal_pending(current)) {
       ret = -EINTR;
    }

    if (!ret && copy_to_user(btcoexStats.statsEvent, pbtcoexStats, sizeof(WMI_BTCOEX_STATS_EVENT))) {
            ret = -EFAULT;
    }


    up(&ar->arSem);

	return(ret);
}

#ifdef CONFIG_HOST_GPIO_SUPPORT
struct ar6000_gpio_intr_wait_cmd_s  gpio_intr_results;
/* gpio_reg_results and gpio_data_available are protected by arSem */
static struct ar6000_gpio_register_cmd_s gpio_reg_results;
static A_BOOL gpio_data_available; /* Requested GPIO data available */
static A_BOOL gpio_intr_available; /* GPIO interrupt info available */
static A_BOOL gpio_ack_received;   /* GPIO ack was received */

/* Host-side initialization for General Purpose I/O support */
void ar6000_gpio_init(void)
{
    gpio_intr_available = FALSE;
    gpio_data_available = FALSE;
    gpio_ack_received   = FALSE;
}

/*
 * Called when a GPIO interrupt is received from the Target.
 * intr_values shows which GPIO pins have interrupted.
 * input_values shows a recent value of GPIO pins.
 */
void
ar6000_gpio_intr_rx(A_UINT32 intr_mask, A_UINT32 input_values)
{
    gpio_intr_results.intr_mask = intr_mask;
    gpio_intr_results.input_values = input_values;
    *((volatile A_BOOL *)&gpio_intr_available) = TRUE;
    wake_up(&arEvent);
}

/*
 * This is called when a response is received from the Target
 * for a previous or ar6000_gpio_input_get or ar6000_gpio_register_get
 * call.
 */
void
ar6000_gpio_data_rx(A_UINT32 reg_id, A_UINT32 value)
{
    gpio_reg_results.gpioreg_id = reg_id;
    gpio_reg_results.value = value;
    *((volatile A_BOOL *)&gpio_data_available) = TRUE;
    wake_up(&arEvent);
}

/*
 * This is called when an acknowledgement is received from the Target
 * for a previous or ar6000_gpio_output_set or ar6000_gpio_register_set
 * call.
 */
void
ar6000_gpio_ack_rx(void)
{
    gpio_ack_received = TRUE;
    wake_up(&arEvent);
}

A_STATUS
ar6000_gpio_output_set(struct net_device *dev,
                       A_UINT32 set_mask,
                       A_UINT32 clear_mask,
                       A_UINT32 enable_mask,
                       A_UINT32 disable_mask)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    gpio_ack_received = FALSE;
    return wmi_gpio_output_set(ar->arWmi,
                set_mask, clear_mask, enable_mask, disable_mask);
}

static A_STATUS
ar6000_gpio_input_get(struct net_device *dev)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    *((volatile A_BOOL *)&gpio_data_available) = FALSE;
    return wmi_gpio_input_get(ar->arWmi);
}

static A_STATUS
ar6000_gpio_register_set(struct net_device *dev,
                         A_UINT32 gpioreg_id,
                         A_UINT32 value)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    gpio_ack_received = FALSE;
    return wmi_gpio_register_set(ar->arWmi, gpioreg_id, value);
}

static A_STATUS
ar6000_gpio_register_get(struct net_device *dev,
                         A_UINT32 gpioreg_id)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    *((volatile A_BOOL *)&gpio_data_available) = FALSE;
    return wmi_gpio_register_get(ar->arWmi, gpioreg_id);
}

static A_STATUS
ar6000_gpio_intr_ack(struct net_device *dev,
                     A_UINT32 ack_mask)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    gpio_intr_available = FALSE;
    return wmi_gpio_intr_ack(ar->arWmi, ack_mask);
}
#endif /* CONFIG_HOST_GPIO_SUPPORT */

#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
static struct prof_count_s prof_count_results;
static A_BOOL prof_count_available; /* Requested GPIO data available */

static A_STATUS
prof_count_get(struct net_device *dev)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    *((volatile A_BOOL *)&prof_count_available) = FALSE;
    return wmi_prof_count_get_cmd(ar->arWmi);
}

/*
 * This is called when a response is received from the Target
 * for a previous prof_count_get call.
 */
void
prof_count_rx(A_UINT32 addr, A_UINT32 count)
{
    prof_count_results.addr = addr;
    prof_count_results.count = count;
    *((volatile A_BOOL *)&prof_count_available) = TRUE;
    wake_up(&arEvent);
}
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */


static A_STATUS
ar6000_create_acl_data_osbuf(struct net_device *dev, A_UINT8 *userdata, void **p_osbuf)
{
    void *osbuf = NULL;
    A_UINT8 tmp_space[8];
    HCI_ACL_DATA_PKT *acl;
    A_UINT8 hdr_size, *datap=NULL;
    A_STATUS ret = A_OK;

    /* ACL is in data path. There is a need to create pool
     * mechanism for allocating and freeing NETBUFs - ToDo later.
     */

    *p_osbuf = NULL;
    acl = (HCI_ACL_DATA_PKT *)tmp_space;
    hdr_size = sizeof(acl->hdl_and_flags) + sizeof(acl->data_len);

    do {
        if (a_copy_from_user(acl, userdata, hdr_size)) {
            ret = A_EFAULT;
            break;
        }

        osbuf = A_NETBUF_ALLOC(hdr_size + acl->data_len);
        if (osbuf == NULL) {
           ret = A_NO_MEMORY;
           break;
        }
        A_NETBUF_PUT(osbuf, hdr_size + acl->data_len);
        datap = (A_UINT8 *)A_NETBUF_DATA(osbuf);

        /* Real copy to osbuf */
        acl = (HCI_ACL_DATA_PKT *)(datap);
        A_MEMCPY(acl, tmp_space, hdr_size);
        if (a_copy_from_user(acl->data, userdata + hdr_size, acl->data_len)) {
            ret = A_EFAULT;
            break;
        }
    } while(FALSE);

    if (ret == A_OK) {
        *p_osbuf = osbuf;
    } else {
        A_NETBUF_FREE(osbuf);
    }
    return ret;
}



int
ar6000_ioctl_ap_setparam(AR_SOFTC_T *ar, int param, int value)
{
    int ret=0;

    switch(param) {
        case IEEE80211_PARAM_WPA:
            switch (value) {
                case WPA_MODE_WPA1:
                    ar->arAuthMode = WPA_AUTH;
                    break;
                case WPA_MODE_WPA2:
                    ar->arAuthMode = WPA2_AUTH;
                    break;
                case WPA_MODE_AUTO:
                    ar->arAuthMode = WPA_AUTH | WPA2_AUTH;
                    break;
                case WPA_MODE_NONE:
                    ar->arAuthMode = NONE_AUTH;
                    break;
            }
            break;
        case IEEE80211_PARAM_AUTHMODE:
            if(value == IEEE80211_AUTH_WPA_PSK) {
                if (WPA_AUTH == ar->arAuthMode) {
                    ar->arAuthMode = WPA_PSK_AUTH;
                } else if (WPA2_AUTH == ar->arAuthMode) {
                    ar->arAuthMode = WPA2_PSK_AUTH;
                } else if ((WPA_AUTH | WPA2_AUTH) == ar->arAuthMode) {
                    ar->arAuthMode = WPA_PSK_AUTH | WPA2_PSK_AUTH;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Error -  Setting PSK "\
                        "mode when WPA param was set to %d\n",
                        ar->arAuthMode));
                    ret = -EIO;
                }
            }
            break;
        case IEEE80211_PARAM_UCASTCIPHER:
            ar->arPairwiseCrypto = 0;
            if(value & (1<<IEEE80211_CIPHER_AES_CCM)) {
                ar->arPairwiseCrypto |= AES_CRYPT;
            }
            if(value & (1<<IEEE80211_CIPHER_TKIP)) {
                ar->arPairwiseCrypto |= TKIP_CRYPT;
            }
            if(!ar->arPairwiseCrypto) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                           ("Error - Invalid cipher in WPA \n"));
                ret = -EIO;
            }
            break;
        case IEEE80211_PARAM_PRIVACY:
            if(value == 0) {
                ar->arDot11AuthMode      = OPEN_AUTH;
                ar->arAuthMode           = NONE_AUTH;
                ar->arPairwiseCrypto     = NONE_CRYPT;
                ar->arPairwiseCryptoLen  = 0;
                ar->arGroupCrypto        = NONE_CRYPT;
                ar->arGroupCryptoLen     = 0;
            }
            break;
#ifdef WAPI_ENABLE
        case IEEE80211_PARAM_WAPI:
            A_PRINTF("WAPI Policy: %d\n", value);
            ar->arDot11AuthMode      = OPEN_AUTH;
            ar->arAuthMode           = NONE_AUTH;
            if(value & 0x1) {
                ar->arPairwiseCrypto     = WAPI_CRYPT;
                ar->arGroupCrypto        = WAPI_CRYPT;
            } else {
                ar->arPairwiseCrypto     = NONE_CRYPT;
                ar->arGroupCrypto        = NONE_CRYPT;
            }
            break;
#endif
    }
    return ret;
}

int
ar6000_ioctl_setparam(AR_SOFTC_T *ar, int param, int value)
{
    A_BOOL profChanged = FALSE;
    int ret=0;

    if(ar->arNextMode == AP_NETWORK) {
        ar->ap_profile_flag = 1; /* There is a change in profile */
        switch (param) {
            case IEEE80211_PARAM_WPA:
            case IEEE80211_PARAM_AUTHMODE:
            case IEEE80211_PARAM_UCASTCIPHER:
            case IEEE80211_PARAM_PRIVACY:
            case IEEE80211_PARAM_WAPI:
                ret = ar6000_ioctl_ap_setparam(ar, param, value);
                return ret;
        }
    }

    switch (param) {
        case IEEE80211_PARAM_WPA:
            switch (value) {
                case WPA_MODE_WPA1:
                    ar->arAuthMode = WPA_AUTH;
                    profChanged    = TRUE;
                    break;
                case WPA_MODE_WPA2:
                    ar->arAuthMode = WPA2_AUTH;
                    profChanged    = TRUE;
                    break;
                case WPA_MODE_NONE:
                    ar->arAuthMode = NONE_AUTH;
                    profChanged    = TRUE;
                    break;
            }
            break;
        case IEEE80211_PARAM_AUTHMODE:
            switch(value) {
                case IEEE80211_AUTH_WPA_PSK:
                    if (WPA_AUTH == ar->arAuthMode) {
                        ar->arAuthMode = WPA_PSK_AUTH;
                        profChanged    = TRUE;
                    } else if (WPA2_AUTH == ar->arAuthMode) {
                        ar->arAuthMode = WPA2_PSK_AUTH;
                        profChanged    = TRUE;
                    } else {
                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Error -  Setting PSK "\
                            "mode when WPA param was set to %d\n",
                            ar->arAuthMode));
                        ret = -EIO;
                    }
                    break;
                case IEEE80211_AUTH_WPA_CCKM:
                    if (WPA2_AUTH == ar->arAuthMode) {
                        ar->arAuthMode = WPA2_AUTH_CCKM;
                    } else {
                        ar->arAuthMode = WPA_AUTH_CCKM;
                    }
                    break;
                default:
                    break;
            }
            break;
        case IEEE80211_PARAM_UCASTCIPHER:
            switch (value) {
                case IEEE80211_CIPHER_AES_CCM:
                    ar->arPairwiseCrypto = AES_CRYPT;
                    profChanged          = TRUE;
                    break;
                case IEEE80211_CIPHER_TKIP:
                    ar->arPairwiseCrypto = TKIP_CRYPT;
                    profChanged          = TRUE;
                    break;
                case IEEE80211_CIPHER_WEP:
                    ar->arPairwiseCrypto = WEP_CRYPT;
                    profChanged          = TRUE;
                    break;
                case IEEE80211_CIPHER_NONE:
                    ar->arPairwiseCrypto = NONE_CRYPT;
                    profChanged          = TRUE;
                    break;
            }
            break;
        case IEEE80211_PARAM_UCASTKEYLEN:
            if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(value)) {
                ret = -EIO;
            } else {
                ar->arPairwiseCryptoLen = value;
            }
            break;
        case IEEE80211_PARAM_MCASTCIPHER:
            switch (value) {
                case IEEE80211_CIPHER_AES_CCM:
                    ar->arGroupCrypto = AES_CRYPT;
                    profChanged       = TRUE;
                    break;
                case IEEE80211_CIPHER_TKIP:
                    ar->arGroupCrypto = TKIP_CRYPT;
                    profChanged       = TRUE;
                    break;
                case IEEE80211_CIPHER_WEP:
                    ar->arGroupCrypto = WEP_CRYPT;
                    profChanged       = TRUE;
                    break;
                case IEEE80211_CIPHER_NONE:
                    ar->arGroupCrypto = NONE_CRYPT;
                    profChanged       = TRUE;
                    break;
            }
            break;
        case IEEE80211_PARAM_MCASTKEYLEN:
            if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(value)) {
                ret = -EIO;
            } else {
                ar->arGroupCryptoLen = value;
            }
            break;
        case IEEE80211_PARAM_COUNTERMEASURES:
            if (ar->arWmiReady == FALSE) {
                return -EIO;
            }
            wmi_set_tkip_countermeasures_cmd(ar->arWmi, value);
            break;
        default:
            break;
    }
    if ((ar->arNextMode != AP_NETWORK) && (profChanged == TRUE)) {
        /*
         * profile has changed.  Erase ssid to signal change
         */
        A_MEMZERO(ar->arSsid, sizeof(ar->arSsid));
    }

    return ret;
}

int
ar6000_ioctl_setkey(AR_SOFTC_T *ar, struct ieee80211req_key *ik)
{
    KEY_USAGE keyUsage;
    A_STATUS status;
    CRYPTO_TYPE keyType = NONE_CRYPT;

#ifdef USER_KEYS
    ar->user_saved_keys.keyOk = FALSE;
#endif
    if ( (0 == memcmp(ik->ik_macaddr, null_mac, IEEE80211_ADDR_LEN)) ||
         (0 == memcmp(ik->ik_macaddr, bcast_mac, IEEE80211_ADDR_LEN)) ) {
        keyUsage = GROUP_USAGE;
        if(ar->arNextMode == AP_NETWORK) {
            A_MEMCPY(&ar->ap_mode_bkey, ik,
                     sizeof(struct ieee80211req_key));
#ifdef WAPI_ENABLE
            if(ar->arPairwiseCrypto == WAPI_CRYPT) {
                return ap_set_wapi_key(ar, ik);
            }
#endif
        }
#ifdef USER_KEYS
        A_MEMCPY(&ar->user_saved_keys.bcast_ik, ik,
                 sizeof(struct ieee80211req_key));
#endif
    } else {
        keyUsage = PAIRWISE_USAGE;
#ifdef USER_KEYS
        A_MEMCPY(&ar->user_saved_keys.ucast_ik, ik,
                 sizeof(struct ieee80211req_key));
#endif
#ifdef WAPI_ENABLE
        if(ar->arNextMode == AP_NETWORK) {
            if(ar->arPairwiseCrypto == WAPI_CRYPT) {
                return ap_set_wapi_key(ar, ik);
            }
        }
#endif
    }

    switch (ik->ik_type) {
        case IEEE80211_CIPHER_WEP:
            keyType = WEP_CRYPT;
            break;
        case IEEE80211_CIPHER_TKIP:
            keyType = TKIP_CRYPT;
            break;
        case IEEE80211_CIPHER_AES_CCM:
            keyType = AES_CRYPT;
            break;
        default:
            break;
    }
#ifdef USER_KEYS
    ar->user_saved_keys.keyType = keyType;
#endif
    if (IEEE80211_CIPHER_CCKM_KRK != ik->ik_type) {
        if (NONE_CRYPT == keyType) {
            return -EIO;
        }

        if ((WEP_CRYPT == keyType)&&(!ar->arConnected)) {
             int index = ik->ik_keyix;

            if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(ik->ik_keylen)) {
                return -EIO;
            }

            A_MEMZERO(ar->arWepKeyList[index].arKey,
                            sizeof(ar->arWepKeyList[index].arKey));
            A_MEMCPY(ar->arWepKeyList[index].arKey, ik->ik_keydata, ik->ik_keylen);
            ar->arWepKeyList[index].arKeyLen = ik->ik_keylen;

            if(ik->ik_flags & IEEE80211_KEY_DEFAULT){
                ar->arDefTxKeyIndex = index;
            }

            return 0;
        }

        if (((WPA_PSK_AUTH == ar->arAuthMode) || (WPA2_PSK_AUTH == ar->arAuthMode)) &&
            (GROUP_USAGE & keyUsage))
        {
            A_UNTIMEOUT(&ar->disconnect_timer);
        }

        status = wmi_addKey_cmd(ar->arWmi, ik->ik_keyix, keyType, keyUsage,
                                ik->ik_keylen, (A_UINT8 *)&ik->ik_keyrsc,
                                ik->ik_keydata, KEY_OP_INIT_VAL, ik->ik_macaddr,
                                SYNC_BOTH_WMIFLAG);

        if (status != A_OK) {
            return -EIO;
        }
    } else {
        status = wmi_add_krk_cmd(ar->arWmi, ik->ik_keydata);
    }

#ifdef USER_KEYS
    ar->user_saved_keys.keyOk = TRUE;
#endif

    return 0;
}

int ar6000_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    HIF_DEVICE *hifDevice = ar->arHifDevice;
    int ret = 0, param;
    unsigned int address = 0;
    unsigned int length = 0;
    unsigned char *buffer;
    char *userdata;
    A_UINT32 connectCtrlFlags;


    WMI_SET_AKMP_PARAMS_CMD  akmpParams;
    WMI_SET_PMKID_LIST_CMD   pmkidInfo;

    WMI_SET_HT_CAP_CMD htCap;
    WMI_SET_HT_OP_CMD htOp;

    /*
     * ioctl operations may have to wait for the Target, so we cannot hold rtnl.
     * Prevent the device from disappearing under us and release the lock during
     * the ioctl operation.
     */
    dev_hold(dev);
    rtnl_unlock();

    if (cmd == AR6000_IOCTL_EXTENDED) {
        /*
         * This allows for many more wireless ioctls than would otherwise
         * be available.  Applications embed the actual ioctl command in
         * the first word of the parameter block, and use the command
         * AR6000_IOCTL_EXTENDED_CMD on the ioctl call.
         */
	if (get_user(cmd, (int *)rq->ifr_data)) {
	    ret = -EFAULT;
	    goto ioctl_done;
	}
        userdata = (char *)(((unsigned int *)rq->ifr_data)+1);
        if(is_xioctl_allowed(ar->arNextMode, cmd) != A_OK) {
            A_PRINTF("xioctl: cmd=%d not allowed in this mode\n",cmd);
            ret = -EOPNOTSUPP;
            goto ioctl_done;
    }
    } else {
        A_STATUS ret = is_iwioctl_allowed(ar->arNextMode, cmd);
        if(ret == A_ENOTSUP) {
            A_PRINTF("iwioctl: cmd=0x%x not allowed in this mode\n", cmd);
            ret = -EOPNOTSUPP;
            goto ioctl_done;
        } else if (ret == A_ERROR) {
            /* It is not our ioctl (out of range ioctl) */
            ret = -EOPNOTSUPP;
            goto ioctl_done;
        }
        userdata = (char *)rq->ifr_data;
    }

    if ((ar->arWlanState == WLAN_DISABLED) &&
        ((cmd != AR6000_XIOCTRL_WMI_SET_WLAN_STATE) &&
         (cmd != AR6000_XIOCTL_GET_WLAN_SLEEP_STATE) &&
         (cmd != AR6000_XIOCTL_DIAG_READ) &&
         (cmd != AR6000_XIOCTL_DIAG_WRITE) &&
         (cmd != AR6000_XIOCTL_SET_BT_HW_POWER_STATE) &&
         (cmd != AR6000_XIOCTL_GET_BT_HW_POWER_STATE) &&
         (cmd != AR6000_XIOCTL_ADD_AP_INTERFACE) &&
         (cmd != AR6000_XIOCTL_REMOVE_AP_INTERFACE) &&
         (cmd != AR6000_IOCTL_WMI_GETREV)))
    {
        ret = -EIO;
        goto ioctl_done;
    }

    ret = 0;
    switch(cmd)
    {
        case IEEE80211_IOCTL_SETPARAM:
        {
            int param, value;
            int *ptr = (int *)rq->ifr_ifru.ifru_newname;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else {
                param = *ptr++;
                value = *ptr;
                ret = ar6000_ioctl_setparam(ar,param,value);
            }
            break;
        }
        case IEEE80211_IOCTL_SETKEY:
        {
            struct ieee80211req_key keydata;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&keydata, userdata,
                            sizeof(struct ieee80211req_key))) {
                ret = -EFAULT;
            } else {
                ar6000_ioctl_setkey(ar, &keydata);
            }
            break;
        }
        case IEEE80211_IOCTL_DELKEY:
        case IEEE80211_IOCTL_SETOPTIE:
        {
            //ret = -EIO;
            break;
        }
        case IEEE80211_IOCTL_SETMLME:
        {
            struct ieee80211req_mlme mlme;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&mlme, userdata,
                            sizeof(struct ieee80211req_mlme))) {
                ret = -EFAULT;
            } else {
                switch (mlme.im_op) {
                    case IEEE80211_MLME_AUTHORIZE:
                        A_PRINTF("setmlme AUTHORIZE %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]);
                        break;
                    case IEEE80211_MLME_UNAUTHORIZE:
                        A_PRINTF("setmlme UNAUTHORIZE %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]);
                        break;
                    case IEEE80211_MLME_DEAUTH:
                        A_PRINTF("setmlme DEAUTH %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]);
                        //remove_sta(ar, mlme.im_macaddr);
                        break;
                    case IEEE80211_MLME_DISASSOC:
                        A_PRINTF("setmlme DISASSOC %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]);
                        //remove_sta(ar, mlme.im_macaddr);
                        break;
                    default:
                        ret = 0;
                        goto ioctl_done;
                }

                wmi_ap_set_mlme(ar->arWmi, mlme.im_op, mlme.im_macaddr,
                                mlme.im_reason);
            }
            break;
        }
        case IEEE80211_IOCTL_ADDPMKID:
        {
            struct ieee80211req_addpmkid  req;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&req, userdata, sizeof(struct ieee80211req_addpmkid))) {
                ret = -EFAULT;
            } else {
                A_STATUS status;

                AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("Add pmkid for %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x en=%d\n",
                    req.pi_bssid[0], req.pi_bssid[1], req.pi_bssid[2],
                    req.pi_bssid[3], req.pi_bssid[4], req.pi_bssid[5],
                    req.pi_enable));

                status = wmi_setPmkid_cmd(ar->arWmi, req.pi_bssid, req.pi_pmkid,
                              req.pi_enable);

                if (status != A_OK) {
                    ret = -EIO;
                    goto ioctl_done;
                }
            }
            break;
        }
#ifdef CONFIG_HOST_TCMD_SUPPORT
        case AR6000_XIOCTL_TCMD_CONT_TX:
            {
                TCMD_CONT_TX txCmd;

                if ((ar->tcmdPm == TCMD_PM_SLEEP) ||
                    (ar->tcmdPm == TCMD_PM_DEEPSLEEP))
                {
                    A_PRINTF("Can NOT send tx tcmd when target is asleep! \n");
                    ret = -EFAULT;
                    goto ioctl_done;
                }

                if(copy_from_user(&txCmd, userdata, sizeof(TCMD_CONT_TX))) {
                    ret = -EFAULT;
                    goto ioctl_done;
                } else {
                    wmi_test_cmd(ar->arWmi,(A_UINT8 *)&txCmd, sizeof(TCMD_CONT_TX));
                }
            }
            break;
        case AR6000_XIOCTL_TCMD_CONT_RX:
            {
                TCMD_CONT_RX rxCmd;

                if ((ar->tcmdPm == TCMD_PM_SLEEP) ||
                    (ar->tcmdPm == TCMD_PM_DEEPSLEEP))
                {
                    A_PRINTF("Can NOT send rx tcmd when target is asleep! \n");
                    ret = -EFAULT;
                    goto ioctl_done;
                }
                if(copy_from_user(&rxCmd, userdata, sizeof(TCMD_CONT_RX))) {
                    ret = -EFAULT;
                    goto ioctl_done;
                }

                switch(rxCmd.act)
                {
                    case TCMD_CONT_RX_PROMIS:
                    case TCMD_CONT_RX_FILTER:
                    case TCMD_CONT_RX_SETMAC:
                    case TCMD_CONT_RX_SET_ANT_SWITCH_TABLE:
                         wmi_test_cmd(ar->arWmi,(A_UINT8 *)&rxCmd,
                                                sizeof(TCMD_CONT_RX));
                         tcmdRxFreq = rxCmd.u.para.freq;
                         break;
                    case TCMD_CONT_RX_REPORT:
                         ar6000_ioctl_tcmd_get_rx_report(dev, rq,
                         (A_UINT8 *)&rxCmd, sizeof(TCMD_CONT_RX));
                         break;
                    default:
                         A_PRINTF("Unknown Cont Rx mode: %d\n",rxCmd.act);
                         ret = -EINVAL;
                         goto ioctl_done;
                }
            }
            break;
        case AR6000_XIOCTL_TCMD_PM:
            {
                TCMD_PM pmCmd;

                if(copy_from_user(&pmCmd, userdata, sizeof(TCMD_PM))) {
                    ret = -EFAULT;
                    goto ioctl_done;
                }
                ar->tcmdPm = pmCmd.mode;
                wmi_test_cmd(ar->arWmi, (A_UINT8*)&pmCmd, sizeof(TCMD_PM));
            }
            break;
#endif /* CONFIG_HOST_TCMD_SUPPORT */

        case AR6000_XIOCTL_BMI_DONE:
            if(bmienable)
            {
                rtnl_lock(); /* ar6000_init expects to be called holding rtnl lock */
                ret = ar6000_init(dev);
                rtnl_unlock();
            }
            else
            {
                ret = BMIDone(hifDevice);
            }
            break;

        case AR6000_XIOCTL_BMI_READ_MEMORY:
	     if (get_user(address, (unsigned int *)userdata) ||
		get_user(length, (unsigned int *)userdata + 1)) {
		ret = -EFAULT;
		break;
	    }

            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Read Memory (address: 0x%x, length: %d)\n",
                             address, length));
            if ((buffer = (unsigned char *)A_MALLOC(length)) != NULL) {
                A_MEMZERO(buffer, length);
                ret = BMIReadMemory(hifDevice, address, buffer, length);
                if (copy_to_user(rq->ifr_data, buffer, length)) {
                    ret = -EFAULT;
                }
                A_FREE(buffer);
            } else {
                ret = -ENOMEM;
            }
            break;

        case AR6000_XIOCTL_BMI_WRITE_MEMORY:
	     if (get_user(address, (unsigned int *)userdata) ||
		get_user(length, (unsigned int *)userdata + 1)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Write Memory (address: 0x%x, length: %d)\n",
                             address, length));
            if ((buffer = (unsigned char *)A_MALLOC(length)) != NULL) {
                A_MEMZERO(buffer, length);
                if (copy_from_user(buffer, &userdata[sizeof(address) +
                                   sizeof(length)], length))
                {
                    ret = -EFAULT;
                } else {
                    ret = BMIWriteMemory(hifDevice, address, buffer, length);
                }
                A_FREE(buffer);
            } else {
                ret = -ENOMEM;
            }
            break;

        case AR6000_XIOCTL_BMI_TEST:
           AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("No longer supported\n"));
           ret = -EOPNOTSUPP;
           break;

        case AR6000_XIOCTL_BMI_EXECUTE:
	     if (get_user(address, (unsigned int *)userdata) ||
		get_user(param, (unsigned int *)userdata + 1)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Execute (address: 0x%x, param: %d)\n",
                             address, param));
            ret = BMIExecute(hifDevice, address, (A_UINT32*)&param);
	    /* return value */
	    if (put_user(param, (unsigned int *)rq->ifr_data)) {
		ret = -EFAULT;
		break;
	    }
            break;

        case AR6000_XIOCTL_BMI_SET_APP_START:
	    if (get_user(address, (unsigned int *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Set App Start (address: 0x%x)\n", address));
            ret = BMISetAppStart(hifDevice, address);
            break;

        case AR6000_XIOCTL_BMI_READ_SOC_REGISTER:
	    if (get_user(address, (unsigned int *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            ret = BMIReadSOCRegister(hifDevice, address, (A_UINT32*)&param);
	    /* return value */
	    if (put_user(param, (unsigned int *)rq->ifr_data)) {
		ret = -EFAULT;
		break;
	    }
            break;

        case AR6000_XIOCTL_BMI_WRITE_SOC_REGISTER:
	    if (get_user(address, (unsigned int *)userdata) ||
		get_user(param, (unsigned int *)userdata + 1)) {
		ret = -EFAULT;
		break;
	    }
            ret = BMIWriteSOCRegister(hifDevice, address, param);
            break;

#ifdef HTC_RAW_INTERFACE
        case AR6000_XIOCTL_HTC_RAW_OPEN:
            ret = A_OK;
            if (!arRawIfEnabled(ar)) {
                /* make sure block size is set in case the target was reset since last
                  * BMI phase (i.e. flashup downloads) */
                ret = ar6000_set_htc_params(ar->arHifDevice,
                                            ar->arTargetType,
                                            0,  /* use default yield */
                                            0   /* use default number of HTC ctrl buffers */
                                            );
                if (A_FAILED(ret)) {
                    break;
                }
                /* Terminate the BMI phase */
                ret = BMIDone(hifDevice);
                if (ret == A_OK) {
                    ret = ar6000_htc_raw_open(ar);
                }
            }
            break;

        case AR6000_XIOCTL_HTC_RAW_CLOSE:
            if (arRawIfEnabled(ar)) {
                ret = ar6000_htc_raw_close(ar);
                arRawIfEnabled(ar) = FALSE;
            } else {
                ret = A_ERROR;
            }
            break;

        case AR6000_XIOCTL_HTC_RAW_READ:
            if (arRawIfEnabled(ar)) {
                unsigned int streamID;
		if (get_user(streamID, (unsigned int *)userdata) ||
		    get_user(length, (unsigned int *)userdata + 1)) {
		    ret = -EFAULT;
		    break;
		}
                buffer = (unsigned char*)rq->ifr_data + sizeof(length);
                ret = ar6000_htc_raw_read(ar, (HTC_RAW_STREAM_ID)streamID,
                                          (char*)buffer, length);
		if (put_user(ret, (unsigned int *)rq->ifr_data)) {
		    ret = -EFAULT;
		    break;
		}
            } else {
                ret = A_ERROR;
            }
            break;

        case AR6000_XIOCTL_HTC_RAW_WRITE:
            if (arRawIfEnabled(ar)) {
                unsigned int streamID;
		if (get_user(streamID, (unsigned int *)userdata) ||
		    get_user(length, (unsigned int *)userdata + 1)) {
		    ret = -EFAULT;
		    break;
		}
                buffer = (unsigned char*)userdata + sizeof(streamID) + sizeof(length);
                ret = ar6000_htc_raw_write(ar, (HTC_RAW_STREAM_ID)streamID,
                                           (char*)buffer, length);
		if (put_user(ret, (unsigned int *)rq->ifr_data)) {
		    ret = -EFAULT;
		    break;
		}
            } else {
                ret = A_ERROR;
            }
            break;
#endif /* HTC_RAW_INTERFACE */

        case AR6000_XIOCTL_BMI_LZ_STREAM_START:
	    if (get_user(address, (unsigned int *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Start Compressed Stream (address: 0x%x)\n", address));
            ret = BMILZStreamStart(hifDevice, address);
            break;

        case AR6000_XIOCTL_BMI_LZ_DATA:
	    if (get_user(length, (unsigned int *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Send Compressed Data (length: %d)\n", length));
            if ((buffer = (unsigned char *)A_MALLOC(length)) != NULL) {
                A_MEMZERO(buffer, length);
                if (copy_from_user(buffer, &userdata[sizeof(length)], length))
                {
                    ret = -EFAULT;
                } else {
                    ret = BMILZData(hifDevice, buffer, length);
                }
                A_FREE(buffer);
            } else {
                ret = -ENOMEM;
            }
            break;

#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
        /*
         * Optional support for Target-side profiling.
         * Not needed in production.
         */

        /* Configure Target-side profiling */
        case AR6000_XIOCTL_PROF_CFG:
        {
            A_UINT32 period;
            A_UINT32 nbins;
	    if (get_user(period, (unsigned int *)userdata) ||
		get_user(nbins, (unsigned int *)userdata + 1)) {
		ret = -EFAULT;
		break;
	    }

            if (wmi_prof_cfg_cmd(ar->arWmi, period, nbins) != A_OK) {
                ret = -EIO;
            }

            break;
        }

        /* Start a profiling bucket/bin at the specified address */
        case AR6000_XIOCTL_PROF_ADDR_SET:
        {
            A_UINT32 addr;
	    if (get_user(addr, (unsigned int *)userdata)) {
		ret = -EFAULT;
		break;
	    }

            if (wmi_prof_addr_set_cmd(ar->arWmi, addr) != A_OK) {
                ret = -EIO;
            }

            break;
        }

        /* START Target-side profiling */
        case AR6000_XIOCTL_PROF_START:
            wmi_prof_start_cmd(ar->arWmi);
            break;

        /* STOP Target-side profiling */
        case AR6000_XIOCTL_PROF_STOP:
            wmi_prof_stop_cmd(ar->arWmi);
            break;
        case AR6000_XIOCTL_PROF_COUNT_GET:
        {
            if (ar->bIsDestroyProgress) {
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }
            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }

            prof_count_available = FALSE;
            ret = prof_count_get(dev);
            if (ret != A_OK) {
                up(&ar->arSem);
                ret = -EIO;
                goto ioctl_done;
            }

            /* Wait for Target to respond. */
            wait_event_interruptible(arEvent, prof_count_available);
            if (signal_pending(current)) {
                ret = -EINTR;
            } else {
                if (copy_to_user(userdata, &prof_count_results,
                                 sizeof(prof_count_results)))
                {
                    ret = -EFAULT;
                }
            }
            up(&ar->arSem);
            break;
        }
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */

        case AR6000_IOCTL_WMI_GETREV:
        {
            if (copy_to_user(rq->ifr_data, &ar->arVersion,
                             sizeof(ar->arVersion)))
            {
                ret = -EFAULT;
            }
            break;
        }
        case AR6000_IOCTL_WMI_SETPWR:
        {
            WMI_POWER_MODE_CMD pwrModeCmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&pwrModeCmd, userdata,
                                   sizeof(pwrModeCmd)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_powermode_cmd(ar->arWmi, pwrModeCmd.powerMode)
                       != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_IOCTL_WMI_SET_IBSS_PM_CAPS:
        {
            WMI_IBSS_PM_CAPS_CMD ibssPmCaps;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&ibssPmCaps, userdata,
                                   sizeof(ibssPmCaps)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_ibsspmcaps_cmd(ar->arWmi, ibssPmCaps.power_saving, ibssPmCaps.ttl,
                    ibssPmCaps.atim_windows, ibssPmCaps.timeout_value) != A_OK)
                {
                    ret = -EIO;
                }
                AR6000_SPIN_LOCK(&ar->arLock, 0);
                ar->arIbssPsEnable = ibssPmCaps.power_saving;
                AR6000_SPIN_UNLOCK(&ar->arLock, 0);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_AP_PS:
        {
            WMI_AP_PS_CMD apPsCmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&apPsCmd, userdata,
                                   sizeof(apPsCmd)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_apps_cmd(ar->arWmi, apPsCmd.psType, apPsCmd.idle_time,
                    apPsCmd.ps_period, apPsCmd.sleep_period) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_IOCTL_WMI_SET_PMPARAMS:
        {
            WMI_POWER_PARAMS_CMD pmParams;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&pmParams, userdata,
                                      sizeof(pmParams)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_pmparams_cmd(ar->arWmi, pmParams.idle_period,
                                     pmParams.pspoll_number,
                                     pmParams.dtim_policy,
                                     pmParams.tx_wakeup_policy,
                                     pmParams.num_tx_to_wakeup,
#if WLAN_CONFIG_IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN
                                     IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN 
#else
                                     SEND_POWER_SAVE_FAIL_EVENT_ALWAYS
#endif
                                     ) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_IOCTL_WMI_SETSCAN:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&ar->scParams, userdata,
                                      sizeof(ar->scParams)))
            {
                ret = -EFAULT;
            } else {
                if (CAN_SCAN_IN_CONNECT(ar->scParams.scanCtrlFlags)) {
                    ar->arSkipScan = FALSE;
                } else {
                    ar->arSkipScan = TRUE;
                }

                if (wmi_scanparams_cmd(ar->arWmi, ar->scParams.fg_start_period,
                                       ar->scParams.fg_end_period,
                                       ar->scParams.bg_period,
                                       ar->scParams.minact_chdwell_time,
                                       ar->scParams.maxact_chdwell_time,
                                       ar->scParams.pas_chdwell_time,
                                       ar->scParams.shortScanRatio,
                                       ar->scParams.scanCtrlFlags,
                                       ar->scParams.max_dfsch_act_time,
                                       ar->scParams.maxact_scan_per_ssid) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_IOCTL_WMI_SETLISTENINT:
        {
            WMI_LISTEN_INT_CMD listenCmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&listenCmd, userdata,
                                      sizeof(listenCmd)))
            {
                ret = -EFAULT;
            } else {
                    if (wmi_listeninterval_cmd(ar->arWmi, listenCmd.listenInterval, listenCmd.numBeacons) != A_OK) {
                        ret = -EIO;
                    } else {
                        AR6000_SPIN_LOCK(&ar->arLock, 0);
                        ar->arListenIntervalT = listenCmd.listenInterval;
                        ar->arListenIntervalB = listenCmd.numBeacons;
                        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
                    }

                }
            break;
        }
        case AR6000_IOCTL_WMI_SET_BMISS_TIME:
        {
            WMI_BMISS_TIME_CMD bmissCmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&bmissCmd, userdata,
                                      sizeof(bmissCmd)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_bmisstime_cmd(ar->arWmi, bmissCmd.bmissTime, bmissCmd.numBeacons) != A_OK) {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_IOCTL_WMI_SETBSSFILTER:
        {
            WMI_BSS_FILTER_CMD filt;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&filt, userdata,
                                   sizeof(filt)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_bssfilter_cmd(ar->arWmi, filt.bssFilter, filt.ieMask)
                        != A_OK) {
                    ret = -EIO;
                } else {
                    ar->arUserBssFilter = param;
                }
            }
            break;
        }

        case AR6000_IOCTL_WMI_SET_SNRTHRESHOLD:
        {
            ret = ar6000_ioctl_set_snr_threshold(dev, rq);
            break;
        }
        case AR6000_XIOCTL_WMI_SET_RSSITHRESHOLD:
        {
            ret = ar6000_ioctl_set_rssi_threshold(dev, rq);
            break;
        }
        case AR6000_XIOCTL_WMI_CLR_RSSISNR:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            }
            ret = wmi_clr_rssi_snr(ar->arWmi);
            break;
        }
        case AR6000_XIOCTL_WMI_SET_LQTHRESHOLD:
        {
            ret = ar6000_ioctl_set_lq_threshold(dev, rq);
            break;
        }
        case AR6000_XIOCTL_WMI_SET_LPREAMBLE:
        {
            WMI_SET_LPREAMBLE_CMD setLpreambleCmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&setLpreambleCmd, userdata,
                                   sizeof(setLpreambleCmd)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_lpreamble_cmd(ar->arWmi, setLpreambleCmd.status,
#if WLAN_CONFIG_DONOT_IGNORE_BARKER_IN_ERP 
                           WMI_DONOT_IGNORE_BARKER_IN_ERP
#else
                           WMI_IGNORE_BARKER_IN_ERP
#endif
                ) != A_OK)
                {
                    ret = -EIO;
                }
            }

            break;
        }
        case AR6000_XIOCTL_WMI_SET_RTS:
        {
            WMI_SET_RTS_CMD rtsCmd;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&rtsCmd, userdata,
                                   sizeof(rtsCmd)))
            {
                ret = -EFAULT;
            } else {
                ar->arRTS = rtsCmd.threshold;
                if (wmi_set_rts_cmd(ar->arWmi, rtsCmd.threshold)
                       != A_OK)
                {
                    ret = -EIO;
                }
            }

            break;
        }
        case AR6000_XIOCTL_WMI_SET_WMM:
        {
            ret = ar6000_ioctl_set_wmm(dev, rq);
            break;
        }
       case AR6000_XIOCTL_WMI_SET_QOS_SUPP:
        {
            ret = ar6000_ioctl_set_qos_supp(dev, rq);
            break;
        }
        case AR6000_XIOCTL_WMI_SET_TXOP:
        {
            ret = ar6000_ioctl_set_txop(dev, rq);
            break;
        }
        case AR6000_XIOCTL_WMI_GET_RD:
        {
            ret = ar6000_ioctl_get_rd(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_SET_CHANNELPARAMS:
        {
            ret = ar6000_ioctl_set_channelParams(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_SET_PROBEDSSID:
        {
            ret = ar6000_ioctl_set_probedSsid(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_SET_BADAP:
        {
            ret = ar6000_ioctl_set_badAp(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_CREATE_QOS:
        {
            ret = ar6000_ioctl_create_qos(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_DELETE_QOS:
        {
            ret = ar6000_ioctl_delete_qos(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_GET_QOS_QUEUE:
        {
            ret = ar6000_ioctl_get_qos_queue(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_GET_TARGET_STATS:
        {
            ret = ar6000_ioctl_get_target_stats(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_SET_ERROR_REPORT_BITMASK:
        {
            ret = ar6000_ioctl_set_error_report_bitmask(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_SET_ASSOC_INFO:
        {
            WMI_SET_ASSOC_INFO_CMD cmd;
            A_UINT8 assocInfo[WMI_MAX_ASSOC_INFO_LEN];

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
		break;
	    }

	    if (get_user(cmd.ieType, userdata)) {
		ret = -EFAULT;
		break;
	    }
	    if (cmd.ieType >= WMI_MAX_ASSOC_INFO_TYPE) {
		ret = -EIO;
		break;
	    }

	    if (get_user(cmd.bufferSize, userdata + 1) ||
		(cmd.bufferSize > WMI_MAX_ASSOC_INFO_LEN) ||
		copy_from_user(assocInfo, userdata + 2, cmd.bufferSize)) {
		ret = -EFAULT;
		break;
	    }
	    if (wmi_associnfo_cmd(ar->arWmi, cmd.ieType,
				  cmd.bufferSize, assocInfo) != A_OK) {
		ret = -EIO;
		break;
	    }
            break;
        }
        case AR6000_IOCTL_WMI_SET_ACCESS_PARAMS:
        {
            ret = ar6000_ioctl_set_access_params(dev, rq);
            break;
        }
        case AR6000_IOCTL_WMI_SET_DISC_TIMEOUT:
        {
            ret = ar6000_ioctl_set_disconnect_timeout(dev, rq);
            break;
        }
        case AR6000_XIOCTL_FORCE_TARGET_RESET:
        {
            if (ar->arHtcTarget)
            {
//                HTCForceReset(htcTarget);
            }
            else
            {
                AR_DEBUG_PRINTF(ATH_DEBUG_WARN,("ar6000_ioctl cannot attempt reset.\n"));
            }
            break;
        }
        case AR6000_XIOCTL_TARGET_INFO:
        case AR6000_XIOCTL_CHECK_TARGET_READY: /* backwards compatibility */
        {
            /* If we made it to here, then the Target exists and is ready. */

            if (cmd == AR6000_XIOCTL_TARGET_INFO) {
                if (copy_to_user((A_UINT32 *)rq->ifr_data, &ar->arVersion.target_ver,
                                 sizeof(ar->arVersion.target_ver)))
                {
                    ret = -EFAULT;
                }
                if (copy_to_user(((A_UINT32 *)rq->ifr_data)+1, &ar->arTargetType,
                                 sizeof(ar->arTargetType)))
                {
                    ret = -EFAULT;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_HB_CHALLENGE_RESP_PARAMS:
        {
            WMI_SET_HB_CHALLENGE_RESP_PARAMS_CMD hbparam;

            if (copy_from_user(&hbparam, userdata, sizeof(hbparam)))
            {
                ret = -EFAULT;
            } else {
                AR6000_SPIN_LOCK(&ar->arLock, 0);
                /* Start a cyclic timer with the parameters provided. */
                if (hbparam.frequency) {
                    ar->arHBChallengeResp.frequency = hbparam.frequency;
                }
                if (hbparam.threshold) {
                    ar->arHBChallengeResp.missThres = hbparam.threshold;
                }

                /* Delete the pending timer and start a new one */
                if (timer_pending(&ar->arHBChallengeResp.timer)) {
                    A_UNTIMEOUT(&ar->arHBChallengeResp.timer);
                }
                A_TIMEOUT_MS(&ar->arHBChallengeResp.timer, ar->arHBChallengeResp.frequency * 1000, 0);
                AR6000_SPIN_UNLOCK(&ar->arLock, 0);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_GET_HB_CHALLENGE_RESP:
        {
            A_UINT32 cookie;

            if (copy_from_user(&cookie, userdata, sizeof(cookie))) {
                ret = -EFAULT;
                goto ioctl_done;
            }

            /* Send the challenge on the control channel */
            if (wmi_get_challenge_resp_cmd(ar->arWmi, cookie, APP_HB_CHALLENGE) != A_OK) {
                ret = -EIO;
                goto ioctl_done;
            }
            break;
        }
#ifdef USER_KEYS
        case AR6000_XIOCTL_USER_SETKEYS:
        {

            ar->user_savedkeys_stat = USER_SAVEDKEYS_STAT_RUN;

            if (copy_from_user(&ar->user_key_ctrl, userdata,
                               sizeof(ar->user_key_ctrl)))
            {
                ret = -EFAULT;
                goto ioctl_done;
            }

            A_PRINTF("ar6000 USER set key %x\n", ar->user_key_ctrl);
            break;
        }
#endif /* USER_KEYS */

#ifdef CONFIG_HOST_GPIO_SUPPORT
        case AR6000_XIOCTL_GPIO_OUTPUT_SET:
        {
            struct ar6000_gpio_output_set_cmd_s gpio_output_set_cmd;

            if (ar->bIsDestroyProgress) {
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }
            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }

            if (copy_from_user(&gpio_output_set_cmd, userdata,
                                sizeof(gpio_output_set_cmd)))
            {
                ret = -EFAULT;
            } else {
                ret = ar6000_gpio_output_set(dev,
                                             gpio_output_set_cmd.set_mask,
                                             gpio_output_set_cmd.clear_mask,
                                             gpio_output_set_cmd.enable_mask,
                                             gpio_output_set_cmd.disable_mask);
                if (ret != A_OK) {
                    ret = EIO;
                }
            }
            up(&ar->arSem);
            break;
        }
        case AR6000_XIOCTL_GPIO_INPUT_GET:
        {
            if (ar->bIsDestroyProgress) {
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }
            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }

            ret = ar6000_gpio_input_get(dev);
            if (ret != A_OK) {
                up(&ar->arSem);
                ret = -EIO;
                goto ioctl_done;
            }

            /* Wait for Target to respond. */
            wait_event_interruptible(arEvent, gpio_data_available);
            if (signal_pending(current)) {
                ret = -EINTR;
            } else {
                A_ASSERT(gpio_reg_results.gpioreg_id == GPIO_ID_NONE);

                if (copy_to_user(userdata, &gpio_reg_results.value,
                                 sizeof(gpio_reg_results.value)))
                {
                    ret = -EFAULT;
                }
            }
            up(&ar->arSem);
            break;
        }
        case AR6000_XIOCTL_GPIO_REGISTER_SET:
        {
            struct ar6000_gpio_register_cmd_s gpio_register_cmd;

            if (ar->bIsDestroyProgress) {
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }
            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }

            if (copy_from_user(&gpio_register_cmd, userdata,
                                sizeof(gpio_register_cmd)))
            {
                ret = -EFAULT;
            } else {
                ret = ar6000_gpio_register_set(dev,
                                               gpio_register_cmd.gpioreg_id,
                                               gpio_register_cmd.value);
                if (ret != A_OK) {
                    ret = EIO;
                }

                /* Wait for acknowledgement from Target */
                wait_event_interruptible(arEvent, gpio_ack_received);
                if (signal_pending(current)) {
                    ret = -EINTR;
                }
            }
            up(&ar->arSem);
            break;
        }
        case AR6000_XIOCTL_GPIO_REGISTER_GET:
        {
            struct ar6000_gpio_register_cmd_s gpio_register_cmd;

            if (ar->bIsDestroyProgress) {
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }
            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }

            if (copy_from_user(&gpio_register_cmd, userdata,
                                sizeof(gpio_register_cmd)))
            {
                ret = -EFAULT;
            } else {
                ret = ar6000_gpio_register_get(dev, gpio_register_cmd.gpioreg_id);
                if (ret != A_OK) {
                    up(&ar->arSem);
                    ret = -EIO;
                    goto ioctl_done;
                }

                /* Wait for Target to respond. */
                wait_event_interruptible(arEvent, gpio_data_available);
                if (signal_pending(current)) {
                    ret = -EINTR;
                } else {
                    A_ASSERT(gpio_register_cmd.gpioreg_id == gpio_reg_results.gpioreg_id);
                    if (copy_to_user(userdata, &gpio_reg_results,
                                     sizeof(gpio_reg_results)))
                    {
                        ret = -EFAULT;
                    }
                }
            }
            up(&ar->arSem);
            break;
        }
        case AR6000_XIOCTL_GPIO_INTR_ACK:
        {
            struct ar6000_gpio_intr_ack_cmd_s gpio_intr_ack_cmd;

            if (ar->bIsDestroyProgress) {
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }
            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }

            if (copy_from_user(&gpio_intr_ack_cmd, userdata,
                                sizeof(gpio_intr_ack_cmd)))
            {
                ret = -EFAULT;
            } else {
                ret = ar6000_gpio_intr_ack(dev, gpio_intr_ack_cmd.ack_mask);
                if (ret != A_OK) {
                    ret = EIO;
                }
            }
            up(&ar->arSem);
            break;
        }
        case AR6000_XIOCTL_GPIO_INTR_WAIT:
        {
            /* Wait for Target to report an interrupt. */
            wait_event_interruptible(arEvent, gpio_intr_available);

            if (signal_pending(current)) {
                ret = -EINTR;
            } else {
                if (copy_to_user(userdata, &gpio_intr_results,
                                 sizeof(gpio_intr_results)))
                {
                    ret = -EFAULT;
                }
            }
            break;
        }
#endif /* CONFIG_HOST_GPIO_SUPPORT */

        case AR6000_XIOCTL_DBGLOG_CFG_MODULE:
        {
            struct ar6000_dbglog_module_config_s config;

            if (copy_from_user(&config, userdata, sizeof(config))) {
                ret = -EFAULT;
                goto ioctl_done;
            }

            /* Send the challenge on the control channel */
            if (wmi_config_debug_module_cmd(ar->arWmi, config.mmask,
                                            config.tsr, config.rep,
                                            config.size, config.valid) != A_OK)
            {
                ret = -EIO;
                goto ioctl_done;
            }
            break;
        }

        case AR6000_XIOCTL_DBGLOG_GET_DEBUG_LOGS:
        {
            /* Send the challenge on the control channel */
            if (ar6000_dbglog_get_debug_logs(ar) != A_OK)
            {
                ret = -EIO;
                goto ioctl_done;
            }
            break;
        }

        case AR6000_XIOCTL_SET_ADHOC_BSSID:
        {
            WMI_SET_ADHOC_BSSID_CMD adhocBssid;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&adhocBssid, userdata,
                                      sizeof(adhocBssid)))
            {
                ret = -EFAULT;
            } else if (A_MEMCMP(adhocBssid.bssid, bcast_mac,
                                AR6000_ETH_ADDR_LEN) == 0)
            {
                ret = -EFAULT;
            } else {

                A_MEMCPY(ar->arReqBssid, adhocBssid.bssid, sizeof(ar->arReqBssid));
        }
            break;
        }

        case AR6000_XIOCTL_SET_OPT_MODE:
        {
        WMI_SET_OPT_MODE_CMD optModeCmd;
            AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&optModeCmd, userdata,
                                      sizeof(optModeCmd)))
            {
                ret = -EFAULT;
            } else if (ar->arConnected && optModeCmd.optMode == SPECIAL_ON) {
                ret = -EFAULT;

            } else if (wmi_set_opt_mode_cmd(ar->arWmi, optModeCmd.optMode)
                       != A_OK)
            {
                ret = -EIO;
            }
            break;
        }

        case AR6000_XIOCTL_OPT_SEND_FRAME:
        {
        WMI_OPT_TX_FRAME_CMD optTxFrmCmd;
            A_UINT8 data[MAX_OPT_DATA_LEN];

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&optTxFrmCmd, userdata,
                                      sizeof(optTxFrmCmd)))
            {
                ret = -EFAULT;
            } else if (copy_from_user(data,
                                      userdata+sizeof(WMI_OPT_TX_FRAME_CMD)-1,
                                      optTxFrmCmd.optIEDataLen))
            {
                ret = -EFAULT;
            } else {
                ret = wmi_opt_tx_frame_cmd(ar->arWmi,
                                           optTxFrmCmd.frmType,
                                           optTxFrmCmd.dstAddr,
                                           optTxFrmCmd.bssid,
                                           optTxFrmCmd.optIEDataLen,
                                           data);
            }

            break;
        }
        case AR6000_XIOCTL_WMI_SETRETRYLIMITS:
        {
            WMI_SET_RETRY_LIMITS_CMD setRetryParams;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&setRetryParams, userdata,
                                      sizeof(setRetryParams)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_retry_limits_cmd(ar->arWmi, setRetryParams.frameType,
                                          setRetryParams.trafficClass,
                                          setRetryParams.maxRetries,
                                          setRetryParams.enableNotify) != A_OK)
                {
                    ret = -EIO;
                }
                AR6000_SPIN_LOCK(&ar->arLock, 0);
                ar->arMaxRetries = setRetryParams.maxRetries;
                AR6000_SPIN_UNLOCK(&ar->arLock, 0);
            }
            break;
        }

        case AR6000_XIOCTL_SET_BEACON_INTVAL:
        {
            WMI_BEACON_INT_CMD bIntvlCmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&bIntvlCmd, userdata,
                       sizeof(bIntvlCmd)))
            {
                ret = -EFAULT;
            } else if (wmi_set_adhoc_bconIntvl_cmd(ar->arWmi, bIntvlCmd.beaconInterval)
                        != A_OK)
            {
                ret = -EIO;
            }
            if(ret == 0) {
                ar->ap_beacon_interval = bIntvlCmd.beaconInterval;
                ar->ap_profile_flag = 1; /* There is a change in profile */
            }
            break;
        }
        case IEEE80211_IOCTL_SETAUTHALG:
        {
            AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
            struct ieee80211req_authalg req;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&req, userdata,
                       sizeof(struct ieee80211req_authalg)))
            {
                ret = -EFAULT;
            } else {
                if (req.auth_alg & AUTH_ALG_OPEN_SYSTEM) {
                    ar->arDot11AuthMode  |= OPEN_AUTH;
                    ar->arPairwiseCrypto  = NONE_CRYPT;
                    ar->arGroupCrypto     = NONE_CRYPT;
                }
                if (req.auth_alg & AUTH_ALG_SHARED_KEY) {
                    ar->arDot11AuthMode  |= SHARED_AUTH;
                    ar->arPairwiseCrypto  = WEP_CRYPT;
                    ar->arGroupCrypto     = WEP_CRYPT;
                    ar->arAuthMode        = NONE_AUTH;
                }
                if (req.auth_alg == AUTH_ALG_LEAP) {
                    ar->arDot11AuthMode   = LEAP_AUTH;
                }
            }
            break;
        }

        case AR6000_XIOCTL_SET_VOICE_PKT_SIZE:
            ret = ar6000_xioctl_set_voice_pkt_size(dev, userdata);
            break;

        case AR6000_XIOCTL_SET_MAX_SP:
            ret = ar6000_xioctl_set_max_sp_len(dev, userdata);
            break;

        case AR6000_XIOCTL_WMI_GET_ROAM_TBL:
            ret = ar6000_ioctl_get_roam_tbl(dev, rq);
            break;
        case AR6000_XIOCTL_WMI_SET_ROAM_CTRL:
            ret = ar6000_ioctl_set_roam_ctrl(dev, userdata);
            break;
        case AR6000_XIOCTRL_WMI_SET_POWERSAVE_TIMERS:
            ret = ar6000_ioctl_set_powersave_timers(dev, userdata);
            break;
        case AR6000_XIOCTRL_WMI_GET_POWER_MODE:
            ret = ar6000_ioctl_get_power_mode(dev, rq);
            break;
        case AR6000_XIOCTRL_WMI_SET_WLAN_STATE:
        {
            AR6000_WLAN_STATE state;
	    if (get_user(state, (unsigned int *)userdata))
		ret = -EFAULT;
	    else if (ar6000_set_wlan_state(ar, state) != A_OK)
                ret = -EIO;
            break;
        }
        case AR6000_XIOCTL_WMI_GET_ROAM_DATA:
            ret = ar6000_ioctl_get_roam_data(dev, rq);
            break;

        case AR6000_XIOCTL_WMI_SET_BT_STATUS:
            ret = ar6000_xioctl_set_bt_status_cmd(dev, userdata);
            break;

        case AR6000_XIOCTL_WMI_SET_BT_PARAMS:
            ret = ar6000_xioctl_set_bt_params_cmd(dev, userdata);
            break;

		case AR6000_XIOCTL_WMI_SET_BTCOEX_FE_ANT:
			ret = ar6000_xioctl_set_btcoex_fe_ant_cmd(dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_SET_BTCOEX_COLOCATED_BT_DEV:
			ret = ar6000_xioctl_set_btcoex_colocated_bt_dev_cmd(dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG:
			ret = ar6000_xioctl_set_btcoex_btinquiry_page_config_cmd(dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_SET_BTCOEX_SCO_CONFIG:
			ret = ar6000_xioctl_set_btcoex_sco_config_cmd( dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_SET_BTCOEX_A2DP_CONFIG:
			ret = ar6000_xioctl_set_btcoex_a2dp_config_cmd(dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_SET_BTCOEX_ACLCOEX_CONFIG:
			ret = ar6000_xioctl_set_btcoex_aclcoex_config_cmd(dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_SET_BTCOEX_DEBUG:
			ret = ar60000_xioctl_set_btcoex_debug_cmd(dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_SET_BT_OPERATING_STATUS:
			ret = ar6000_xioctl_set_btcoex_bt_operating_status_cmd(dev, userdata);
			break;

		case AR6000_XIOCTL_WMI_GET_BTCOEX_CONFIG:
			ret = ar6000_xioctl_get_btcoex_config_cmd(dev, userdata, rq);
			break;

		case AR6000_XIOCTL_WMI_GET_BTCOEX_STATS:
			ret = ar6000_xioctl_get_btcoex_stats_cmd(dev, userdata, rq);
			break;

        case AR6000_XIOCTL_WMI_STARTSCAN:
        {
            WMI_START_SCAN_CMD setStartScanCmd, *cmdp;

            if (ar->arWmiReady == FALSE) {
                    ret = -EIO;
                } else if (copy_from_user(&setStartScanCmd, userdata,
                                          sizeof(setStartScanCmd)))
                {
                    ret = -EFAULT;
                } else {
                    if (setStartScanCmd.numChannels > 1) {
                        cmdp = A_MALLOC(130);
                        if (copy_from_user(cmdp, userdata,
                                           sizeof (*cmdp) +
                                           ((setStartScanCmd.numChannels - 1) *
                                           sizeof(A_UINT16))))
                        {
                            kfree(cmdp);
                            ret = -EFAULT;
                            goto ioctl_done;
                        }
                    } else {
                        cmdp = &setStartScanCmd;
                    }

                    if (wmi_startscan_cmd(ar->arWmi, cmdp->scanType,
                                          cmdp->forceFgScan,
                                          cmdp->isLegacy,
                                          cmdp->homeDwellTime,
                                          cmdp->forceScanInterval,
                                          cmdp->numChannels,
                                          cmdp->channelList) != A_OK)
                    {
                        ret = -EIO;
                    }
                }
            break;
        }
        case AR6000_XIOCTL_WMI_SETFIXRATES:
        {
            WMI_FIX_RATES_CMD setFixRatesCmd;
            A_STATUS returnStatus;

            if (ar->arWmiReady == FALSE) {
                    ret = -EIO;
                } else if (copy_from_user(&setFixRatesCmd, userdata,
                                          sizeof(setFixRatesCmd)))
                {
                    ret = -EFAULT;
                } else {
                    returnStatus = wmi_set_fixrates_cmd(ar->arWmi, setFixRatesCmd.fixRateMask);
                    if (returnStatus == A_EINVAL) {
                        ret = -EINVAL;
                    } else if(returnStatus != A_OK) {
                        ret = -EIO;
                    } else {
                        ar->ap_profile_flag = 1; /* There is a change in profile */
                    }
                }
            break;
        }

        case AR6000_XIOCTL_WMI_GETFIXRATES:
        {
            WMI_FIX_RATES_CMD getFixRatesCmd;
            AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
            int ret = 0;

            if (ar->bIsDestroyProgress) {
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }

            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }
            /* Used copy_from_user/copy_to_user to access user space data */
            if (copy_from_user(&getFixRatesCmd, userdata, sizeof(getFixRatesCmd))) {
                ret = -EFAULT;
            } else {
                ar->arRateMask = 0xFFFFFFFF;

                if (wmi_get_ratemask_cmd(ar->arWmi) != A_OK) {
                    up(&ar->arSem);
                    ret = -EIO;
                    goto ioctl_done;
                }

                wait_event_interruptible_timeout(arEvent, ar->arRateMask != 0xFFFFFFFF, wmitimeout * HZ);

                if (signal_pending(current)) {
                    ret = -EINTR;
                }

                if (!ret) {
                    getFixRatesCmd.fixRateMask = ar->arRateMask;
                }

                if(copy_to_user(userdata, &getFixRatesCmd, sizeof(getFixRatesCmd))) {
                   ret = -EFAULT;
                }

                up(&ar->arSem);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_AUTHMODE:
        {
            WMI_SET_AUTH_MODE_CMD setAuthMode;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&setAuthMode, userdata,
                                      sizeof(setAuthMode)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_authmode_cmd(ar->arWmi, setAuthMode.mode) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_REASSOCMODE:
        {
            WMI_SET_REASSOC_MODE_CMD setReassocMode;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&setReassocMode, userdata,
                                      sizeof(setReassocMode)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_reassocmode_cmd(ar->arWmi, setReassocMode.mode) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_DIAG_READ:
        {
            A_UINT32 addr, data;
	    if (get_user(addr, (unsigned int *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            addr = TARG_VTOP(ar->arTargetType, addr);
            if (ar6000_ReadRegDiag(ar->arHifDevice, &addr, &data) != A_OK) {
                ret = -EIO;
            }
	    if (put_user(data, (unsigned int *)userdata + 1)) {
		ret = -EFAULT;
		break;
	    }
            break;
        }
        case AR6000_XIOCTL_DIAG_WRITE:
        {
            A_UINT32 addr, data;
	    if (get_user(addr, (unsigned int *)userdata) ||
		get_user(data, (unsigned int *)userdata + 1)) {
		ret = -EFAULT;
		break;
	    }
            addr = TARG_VTOP(ar->arTargetType, addr);
            if (ar6000_WriteRegDiag(ar->arHifDevice, &addr, &data) != A_OK) {
                ret = -EIO;
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_KEEPALIVE:
        {
             WMI_SET_KEEPALIVE_CMD setKeepAlive;
             if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
                 goto ioctl_done;
             } else if (copy_from_user(&setKeepAlive, userdata,
                        sizeof(setKeepAlive))){
                 ret = -EFAULT;
             } else {
                 if (wmi_set_keepalive_cmd(ar->arWmi, setKeepAlive.keepaliveInterval) != A_OK) {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_WMI_SET_PARAMS:
        {
             WMI_SET_PARAMS_CMD cmd;
             if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
                 goto ioctl_done;
             } else if (copy_from_user(&cmd, userdata,
                        sizeof(cmd))){
                 ret = -EFAULT;
             } else if (copy_from_user(&cmd, userdata,
                        sizeof(cmd) + cmd.length))
            {
                ret = -EFAULT;
            } else {
                 if (wmi_set_params_cmd(ar->arWmi, cmd.opcode, cmd.length, cmd.buffer) != A_OK) {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_WMI_SET_MCAST_FILTER:
        {
             WMI_SET_MCAST_FILTER_CMD cmd;
             if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
                 goto ioctl_done;
             } else if (copy_from_user(&cmd, userdata,
                        sizeof(cmd))){
                 ret = -EFAULT;
             } else {
                 if (wmi_set_mcast_filter_cmd(ar->arWmi, cmd.multicast_mac[0],
                                                                                     cmd.multicast_mac[1],
                                                                                     cmd.multicast_mac[2],
                                                                                     cmd.multicast_mac[3]) != A_OK) {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_WMI_DEL_MCAST_FILTER:
        {
             WMI_SET_MCAST_FILTER_CMD cmd;
             if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
                 goto ioctl_done;
             } else if (copy_from_user(&cmd, userdata,
                        sizeof(cmd))){
                 ret = -EFAULT;
             } else {
                 if (wmi_del_mcast_filter_cmd(ar->arWmi, cmd.multicast_mac[0],
                                                                                     cmd.multicast_mac[1],
                                                                                     cmd.multicast_mac[2],
                                                                                     cmd.multicast_mac[3]) != A_OK) {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_WMI_MCAST_FILTER:
        {
             WMI_MCAST_FILTER_CMD cmd;
             if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
                 goto ioctl_done;
             } else if (copy_from_user(&cmd, userdata,
                        sizeof(cmd))){
                 ret = -EFAULT;
             } else {
                 if (wmi_mcast_filter_cmd(ar->arWmi, cmd.enable)  != A_OK) {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_WMI_GET_KEEPALIVE:
        {
            AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
            WMI_GET_KEEPALIVE_CMD getKeepAlive;
            int ret = 0;
            if (ar->bIsDestroyProgress) {
                ret =-EBUSY;
                goto ioctl_done;
            }
            if (ar->arWmiReady == FALSE) {
               ret = -EIO;
               goto ioctl_done;
            }
            if (down_interruptible(&ar->arSem)) {
                ret = -ERESTARTSYS;
                goto ioctl_done;
            }
            if (ar->bIsDestroyProgress) {
                up(&ar->arSem);
                ret = -EBUSY;
                goto ioctl_done;
            }
            if (copy_from_user(&getKeepAlive, userdata,sizeof(getKeepAlive))) {
               ret = -EFAULT;
            } else {
            getKeepAlive.keepaliveInterval = wmi_get_keepalive_cmd(ar->arWmi);
            ar->arKeepaliveConfigured = 0xFF;
            if (wmi_get_keepalive_configured(ar->arWmi) != A_OK){
                up(&ar->arSem);
                ret = -EIO;
                goto ioctl_done;
            }
            wait_event_interruptible_timeout(arEvent, ar->arKeepaliveConfigured != 0xFF, wmitimeout * HZ);
            if (signal_pending(current)) {
                ret = -EINTR;
            }

            if (!ret) {
                getKeepAlive.configured = ar->arKeepaliveConfigured;
            }
            if (copy_to_user(userdata, &getKeepAlive, sizeof(getKeepAlive))) {
               ret = -EFAULT;
            }
            up(&ar->arSem);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_APPIE:
        {
            WMI_SET_APPIE_CMD appIEcmd;
            A_UINT8           appIeInfo[IEEE80211_APPIE_FRAME_MAX_LEN];
            A_UINT32            fType,ieLen;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            }
	    if (get_user(fType, (A_UINT32 *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            appIEcmd.mgmtFrmType = fType;
            if (appIEcmd.mgmtFrmType >= IEEE80211_APPIE_NUM_OF_FRAME) {
                ret = -EIO;
            } else {
		if (get_user(ieLen, (A_UINT32 *)(userdata + 4))) {
		    ret = -EFAULT;
		    break;
		}
                appIEcmd.ieLen = ieLen;
                A_PRINTF("WPSIE: Type-%d, Len-%d\n",appIEcmd.mgmtFrmType, appIEcmd.ieLen);
                if (appIEcmd.ieLen > IEEE80211_APPIE_FRAME_MAX_LEN) {
                    ret = -EIO;
                    break;
                }
                if (copy_from_user(appIeInfo, userdata + 8, appIEcmd.ieLen)) {
                    ret = -EFAULT;
                } else {
                    if (wmi_set_appie_cmd(ar->arWmi, appIEcmd.mgmtFrmType,
                                          appIEcmd.ieLen,  appIeInfo) != A_OK)
                    {
                        ret = -EIO;
                    }
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_MGMT_FRM_RX_FILTER:
        {
            WMI_BSS_FILTER_CMD cmd;
            A_UINT32    filterType;

            if (copy_from_user(&filterType, userdata, sizeof(A_UINT32)))
            {
                ret = -EFAULT;
                goto ioctl_done;
            }
            if (filterType & (IEEE80211_FILTER_TYPE_BEACON |
                                    IEEE80211_FILTER_TYPE_PROBE_RESP))
            {
                cmd.bssFilter = ALL_BSS_FILTER;
            } else {
                cmd.bssFilter = NONE_BSS_FILTER;
            }
            if (wmi_bssfilter_cmd(ar->arWmi, cmd.bssFilter, 0) != A_OK) {
                ret = -EIO;
            } else {
                ar->arUserBssFilter = cmd.bssFilter;
            }

            AR6000_SPIN_LOCK(&ar->arLock, 0);
            ar->arMgmtFilter = filterType;
            AR6000_SPIN_UNLOCK(&ar->arLock, 0);
            break;
        }
        case AR6000_XIOCTL_WMI_SET_WSC_STATUS:
        {
            A_UINT32    wsc_status;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            } else if (copy_from_user(&wsc_status, userdata, sizeof(A_UINT32)))
            {
                ret = -EFAULT;
                goto ioctl_done;
            }
            if (wmi_set_wsc_status_cmd(ar->arWmi, wsc_status) != A_OK) {
                ret = -EIO;
            }
            break;
        }
        case AR6000_XIOCTL_BMI_ROMPATCH_INSTALL:
        {
            A_UINT32 ROM_addr;
            A_UINT32 RAM_addr;
            A_UINT32 nbytes;
            A_UINT32 do_activate;
            A_UINT32 rompatch_id;

	    if (get_user(ROM_addr, (A_UINT32 *)userdata) ||
		get_user(RAM_addr, (A_UINT32 *)userdata + 1) ||
		get_user(nbytes, (A_UINT32 *)userdata + 2) ||
		get_user(do_activate, (A_UINT32 *)userdata + 3)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Install rompatch from ROM: 0x%x to RAM: 0x%x  length: %d\n",
                             ROM_addr, RAM_addr, nbytes));
            ret = BMIrompatchInstall(hifDevice, ROM_addr, RAM_addr,
                                        nbytes, do_activate, &rompatch_id);
            if (ret == A_OK) {
		/* return value */
		if (put_user(rompatch_id, (unsigned int *)rq->ifr_data)) {
		    ret = -EFAULT;
		    break;
		}
            }
            break;
        }

        case AR6000_XIOCTL_BMI_ROMPATCH_UNINSTALL:
        {
            A_UINT32 rompatch_id;

	    if (get_user(rompatch_id, (A_UINT32 *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("UNinstall rompatch_id %d\n", rompatch_id));
            ret = BMIrompatchUninstall(hifDevice, rompatch_id);
            break;
        }

        case AR6000_XIOCTL_BMI_ROMPATCH_ACTIVATE:
        case AR6000_XIOCTL_BMI_ROMPATCH_DEACTIVATE:
        {
            A_UINT32 rompatch_count;

	    if (get_user(rompatch_count, (A_UINT32 *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Change rompatch activation count=%d\n", rompatch_count));
            length = sizeof(A_UINT32) * rompatch_count;
            if ((buffer = (unsigned char *)A_MALLOC(length)) != NULL) {
                A_MEMZERO(buffer, length);
                if (copy_from_user(buffer, &userdata[sizeof(rompatch_count)], length))
                {
                    ret = -EFAULT;
                } else {
                    if (cmd == AR6000_XIOCTL_BMI_ROMPATCH_ACTIVATE) {
                        ret = BMIrompatchActivate(hifDevice, rompatch_count, (A_UINT32 *)buffer);
                    } else {
                        ret = BMIrompatchDeactivate(hifDevice, rompatch_count, (A_UINT32 *)buffer);
                    }
                }
                A_FREE(buffer);
            } else {
                ret = -ENOMEM;
            }

            break;
        }
        case AR6000_XIOCTL_SET_IP:
        {
            WMI_SET_IP_CMD setIP;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&setIP, userdata,
                                      sizeof(setIP)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_ip_cmd(ar->arWmi,
                                &setIP) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }

        case AR6000_XIOCTL_WMI_SET_HOST_SLEEP_MODE:
        {
            WMI_SET_HOST_SLEEP_MODE_CMD setHostSleepMode;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&setHostSleepMode, userdata,
                                      sizeof(setHostSleepMode)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_host_sleep_mode_cmd(ar->arWmi,
                                &setHostSleepMode) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_SET_WOW_MODE:
        {
            WMI_SET_WOW_MODE_CMD setWowMode;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&setWowMode, userdata,
                                      sizeof(setWowMode)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_wow_mode_cmd(ar->arWmi,
                                &setWowMode) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_GET_WOW_LIST:
        {
            WMI_GET_WOW_LIST_CMD getWowList;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&getWowList, userdata,
                                      sizeof(getWowList)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_get_wow_list_cmd(ar->arWmi,
                                &getWowList) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_ADD_WOW_PATTERN:
        {
#define WOW_PATTERN_SIZE 64
#define WOW_MASK_SIZE 64

            WMI_ADD_WOW_PATTERN_CMD cmd;
            A_UINT8 mask_data[WOW_PATTERN_SIZE]={0};
            A_UINT8 pattern_data[WOW_PATTERN_SIZE]={0};

            do {
                if (ar->arWmiReady == FALSE) {
                    ret = -EIO;
                    break;        
                } 
                if(copy_from_user(&cmd, userdata,
                            sizeof(WMI_ADD_WOW_PATTERN_CMD))) 
                {
                    ret = -EFAULT;
                    break;        
                }
                if (copy_from_user(pattern_data,
                                      userdata + 3,
                                      cmd.filter_size)) 
                {
                    ret = -EFAULT;
                    break;        
                }
                if (copy_from_user(mask_data,
                                  (userdata + 3 + cmd.filter_size),
                                  cmd.filter_size))
                {
                    ret = -EFAULT;
                    break;
                }
                if (wmi_add_wow_pattern_cmd(ar->arWmi,
                            &cmd, pattern_data, mask_data, cmd.filter_size) != A_OK)
                {
                    ret = -EIO;
                }
            } while(FALSE);
#undef WOW_PATTERN_SIZE
#undef WOW_MASK_SIZE
            break;
        }
        case AR6000_XIOCTL_WMI_DEL_WOW_PATTERN:
        {
            WMI_DEL_WOW_PATTERN_CMD delWowPattern;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&delWowPattern, userdata,
                                      sizeof(delWowPattern)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_del_wow_pattern_cmd(ar->arWmi,
                                &delWowPattern) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_DUMP_HTC_CREDIT_STATE:
            if (ar->arHtcTarget != NULL) {
#ifdef ATH_DEBUG_MODULE
                HTCDumpCreditStates(ar->arHtcTarget);
#endif /* ATH_DEBUG_MODULE */
#ifdef HTC_EP_STAT_PROFILING
                {
                    HTC_ENDPOINT_STATS stats;
                    int i;

                    for (i = 0; i < 5; i++) {
                        if (HTCGetEndpointStatistics(ar->arHtcTarget,
                                                     i,
                                                     HTC_EP_STAT_SAMPLE_AND_CLEAR,
                                                     &stats)) {
                            A_PRINTF(KERN_ALERT"------- Profiling Endpoint : %d \n", i);
                            A_PRINTF(KERN_ALERT"TxCreditLowIndications : %d \n", stats.TxCreditLowIndications);
                            A_PRINTF(KERN_ALERT"TxIssued : %d \n", stats.TxIssued);
                            A_PRINTF(KERN_ALERT"TxDropped: %d \n", stats.TxDropped);
                            A_PRINTF(KERN_ALERT"TxPacketsBundled : %d \n", stats.TxPacketsBundled);
                            A_PRINTF(KERN_ALERT"TxBundles : %d \n", stats.TxBundles);
                            A_PRINTF(KERN_ALERT"TxCreditRpts : %d \n", stats.TxCreditRpts);
                            A_PRINTF(KERN_ALERT"TxCreditsRptsFromRx : %d \n", stats.TxCreditRptsFromRx);
                            A_PRINTF(KERN_ALERT"TxCreditsRptsFromOther : %d \n", stats.TxCreditRptsFromOther);
                            A_PRINTF(KERN_ALERT"TxCreditsRptsFromEp0 : %d \n", stats.TxCreditRptsFromEp0);
                            A_PRINTF(KERN_ALERT"TxCreditsFromRx : %d \n", stats.TxCreditsFromRx);
                            A_PRINTF(KERN_ALERT"TxCreditsFromOther : %d \n", stats.TxCreditsFromOther);
                            A_PRINTF(KERN_ALERT"TxCreditsFromEp0 : %d \n", stats.TxCreditsFromEp0);
                            A_PRINTF(KERN_ALERT"TxCreditsConsummed : %d \n", stats.TxCreditsConsummed);
                            A_PRINTF(KERN_ALERT"TxCreditsReturned : %d \n", stats.TxCreditsReturned);
                            A_PRINTF(KERN_ALERT"RxReceived : %d \n", stats.RxReceived);
                            A_PRINTF(KERN_ALERT"RxPacketsBundled : %d \n", stats.RxPacketsBundled);
                            A_PRINTF(KERN_ALERT"RxLookAheads : %d \n", stats.RxLookAheads);
                            A_PRINTF(KERN_ALERT"RxBundleLookAheads : %d \n", stats.RxBundleLookAheads);
                            A_PRINTF(KERN_ALERT"RxBundleIndFromHdr : %d \n", stats.RxBundleIndFromHdr);
                            A_PRINTF(KERN_ALERT"RxAllocThreshHit : %d \n", stats.RxAllocThreshHit);
                            A_PRINTF(KERN_ALERT"RxAllocThreshBytes : %d \n", stats.RxAllocThreshBytes);
                            A_PRINTF(KERN_ALERT"---- \n");

                        }
            }
                }
#endif
            }
            break;
        case AR6000_XIOCTL_TRAFFIC_ACTIVITY_CHANGE:
            if (ar->arHtcTarget != NULL) {
                struct ar6000_traffic_activity_change data;

                if (copy_from_user(&data, userdata, sizeof(data)))
                {
                    ret = -EFAULT;
                    goto ioctl_done;
                }
                    /* note, this is used for testing (mbox ping testing), indicate activity
                     * change using the stream ID as the traffic class */
                ar6000_indicate_tx_activity(ar,
                                            (A_UINT8)data.StreamID,
                                            data.Active ? TRUE : FALSE);
            }
            break;
        case AR6000_XIOCTL_WMI_SET_CONNECT_CTRL_FLAGS:
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&connectCtrlFlags, userdata,
                                      sizeof(connectCtrlFlags)))
            {
                ret = -EFAULT;
            } else {
                ar->arConnectCtrlFlags = connectCtrlFlags;
            }
            break;
        case AR6000_XIOCTL_WMI_SET_AKMP_PARAMS:
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&akmpParams, userdata,
                                      sizeof(WMI_SET_AKMP_PARAMS_CMD)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_set_akmp_params_cmd(ar->arWmi, &akmpParams) != A_OK) {
                    ret = -EIO;
                }
            }
            break;
        case AR6000_XIOCTL_WMI_SET_PMKID_LIST:
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else {
                if (copy_from_user(&pmkidInfo.numPMKID, userdata,
                                      sizeof(pmkidInfo.numPMKID)))
                {
                    ret = -EFAULT;
                    break;
                }
                if (copy_from_user(&pmkidInfo.pmkidList,
                                   userdata + sizeof(pmkidInfo.numPMKID),
                                   pmkidInfo.numPMKID * sizeof(WMI_PMKID)))
                {
                    ret = -EFAULT;
                    break;
                }
                if (wmi_set_pmkid_list_cmd(ar->arWmi, &pmkidInfo) != A_OK) {
                    ret = -EIO;
                }
            }
            break;
        case AR6000_XIOCTL_WMI_GET_PMKID_LIST:
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else  {
                if (wmi_get_pmkid_list_cmd(ar->arWmi) != A_OK) {
                    ret = -EIO;
                }
            }
            break;
        case AR6000_XIOCTL_WMI_ABORT_SCAN:
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            }
            ret = wmi_abort_scan_cmd(ar->arWmi);
            break;
        case AR6000_XIOCTL_AP_HIDDEN_SSID:
        {
            A_UINT8    hidden_ssid;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&hidden_ssid, userdata, sizeof(hidden_ssid))) {
                ret = -EFAULT;
            } else {
                wmi_ap_set_hidden_ssid(ar->arWmi, hidden_ssid);
                ar->ap_hidden_ssid = hidden_ssid;
                ar->ap_profile_flag = 1; /* There is a change in profile */
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_STA_LIST:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else {
                A_UINT8 i;
                ap_get_sta_t temp;
                A_MEMZERO(&temp, sizeof(temp));
                for(i=0;i<AP_MAX_NUM_STA;i++) {
                    A_MEMCPY(temp.sta[i].mac, ar->sta_list[i].mac, ATH_MAC_LEN);
                    temp.sta[i].aid = ar->sta_list[i].aid;
                    temp.sta[i].keymgmt = ar->sta_list[i].keymgmt;
                    temp.sta[i].ucipher = ar->sta_list[i].ucipher;
                    temp.sta[i].auth = ar->sta_list[i].auth;
                }
                if(copy_to_user((ap_get_sta_t *)rq->ifr_data, &temp,
                                 sizeof(ar->sta_list))) {
                    ret = -EFAULT;
                }
            }
            break;
        }
        case AR6000_XIOCTL_AP_SET_NUM_STA:
        {
            A_UINT8    num_sta;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&num_sta, userdata, sizeof(num_sta))) {
                ret = -EFAULT;
            } else if(num_sta > AP_MAX_NUM_STA) {
                /* value out of range */
                ret = -EINVAL;
            } else {
                wmi_ap_set_num_sta(ar->arWmi, num_sta);
            }
            break;
        }
        case AR6000_XIOCTL_AP_SET_ACL_POLICY:
        {
            A_UINT8    policy;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&policy, userdata, sizeof(policy))) {
                ret = -EFAULT;
            } else if(policy == ar->g_acl.policy) {
                /* No change in policy */
            } else {
                if(!(policy & AP_ACL_RETAIN_LIST_MASK)) {
                    /* clear ACL list */
                    memset(&ar->g_acl,0,sizeof(WMI_AP_ACL));
                }
                ar->g_acl.policy = policy;
                wmi_ap_set_acl_policy(ar->arWmi, policy);
            }
            break;
        }
        case AR6000_XIOCTL_AP_SET_ACL_MAC:
        {
            WMI_AP_ACL_MAC_CMD    acl;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&acl, userdata, sizeof(acl))) {
                ret = -EFAULT;
            } else {
                if(acl_add_del_mac(&ar->g_acl, &acl)) {
                    wmi_ap_acl_mac_list(ar->arWmi, &acl);
                } else {
                    A_PRINTF("ACL list error\n");
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_ACL_LIST:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((WMI_AP_ACL *)rq->ifr_data, &ar->g_acl,
                                 sizeof(WMI_AP_ACL))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_COMMIT_CONFIG:
        {
            ret = ar6000_ap_mode_profile_commit(ar);
            break;
        }
        case IEEE80211_IOCTL_GETWPAIE:
        {
            struct ieee80211req_wpaie wpaie;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&wpaie, userdata, sizeof(wpaie))) {
                ret = -EFAULT;
            } else if (ar6000_ap_mode_get_wpa_ie(ar, &wpaie)) {
                ret = -EFAULT;
            } else if(copy_to_user(userdata, &wpaie, sizeof(wpaie))) {
                ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_CONN_INACT_TIME:
        {
            A_UINT32    period;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&period, userdata, sizeof(period))) {
                ret = -EFAULT;
            } else {
                wmi_ap_conn_inact_time(ar->arWmi, period);
            }
            break;
        }
        case AR6000_XIOCTL_AP_PROT_SCAN_TIME:
        {
            WMI_AP_PROT_SCAN_TIME_CMD  bgscan;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&bgscan, userdata, sizeof(bgscan))) {
                ret = -EFAULT;
            } else {
                wmi_ap_bgscan_time(ar->arWmi, bgscan.period_min, bgscan.dwell_ms);
            }
            break;
        }
        case AR6000_XIOCTL_AP_SET_COUNTRY:
        {
            ret = ar6000_ioctl_set_country(dev, rq);
            break;
        }
        case AR6000_XIOCTL_AP_SET_DTIM:
        {
            WMI_AP_SET_DTIM_CMD  d;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&d, userdata, sizeof(d))) {
                ret = -EFAULT;
            } else {
                if(d.dtim > 0 && d.dtim < 11) {
                    ar->ap_dtim_period = d.dtim;
                    wmi_ap_set_dtim(ar->arWmi, d.dtim);
                    ar->ap_profile_flag = 1; /* There is a change in profile */
                } else {
                    A_PRINTF("DTIM out of range. Valid range is [1-10]\n");
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_TARGET_EVENT_REPORT:
        {
            WMI_SET_TARGET_EVENT_REPORT_CMD evtCfgCmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            }
            if (copy_from_user(&evtCfgCmd, userdata,
                               sizeof(evtCfgCmd))) {
                ret = -EFAULT;
                break;
            }
            ret = wmi_set_target_event_report_cmd(ar->arWmi, &evtCfgCmd);
            break;
        }
        case AR6000_XIOCTL_AP_INTRA_BSS_COMM:
        {
            A_UINT8    intra=0;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&intra, userdata, sizeof(intra))) {
                ret = -EFAULT;
            } else {
                ar->intra_bss = (intra?1:0);
            }
            break;
        }
        case AR6000_XIOCTL_DUMP_MODULE_DEBUG_INFO:
        {
            struct drv_debug_module_s moduleinfo;

            if (copy_from_user(&moduleinfo, userdata, sizeof(moduleinfo))) {
                ret = -EFAULT;
                break;
            }

            a_dump_module_debug_info_by_name(moduleinfo.modulename);
            ret = 0;
            break;
        }
        case AR6000_XIOCTL_MODULE_DEBUG_SET_MASK:
        {
            struct drv_debug_module_s moduleinfo;

            if (copy_from_user(&moduleinfo, userdata, sizeof(moduleinfo))) {
                ret = -EFAULT;
                break;
            }

            if (A_FAILED(a_set_module_mask(moduleinfo.modulename, moduleinfo.mask))) {
                ret = -EFAULT;
            }

            break;
        }
        case AR6000_XIOCTL_MODULE_DEBUG_GET_MASK:
        {
            struct drv_debug_module_s moduleinfo;

            if (copy_from_user(&moduleinfo, userdata, sizeof(moduleinfo))) {
                ret = -EFAULT;
                break;
            }

            if (A_FAILED(a_get_module_mask(moduleinfo.modulename, &moduleinfo.mask))) {
                ret = -EFAULT;
                break;
            }

            if (copy_to_user(userdata, &moduleinfo, sizeof(moduleinfo))) {
                ret = -EFAULT;
                break;
            }

            break;
        }
#ifdef ATH_AR6K_11N_SUPPORT
        case AR6000_XIOCTL_DUMP_RCV_AGGR_STATS:
        {
            PACKET_LOG *copy_of_pkt_log;

            aggr_dump_stats(ar->aggr_cntxt, &copy_of_pkt_log);
            if (copy_to_user(rq->ifr_data, copy_of_pkt_log, sizeof(PACKET_LOG))) {
                ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_SETUP_AGGR:
        {
            WMI_ADDBA_REQ_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
                ret = -EFAULT;
            } else {
                wmi_setup_aggr_cmd(ar->arWmi, cmd.tid);
            }
        }
        break;

        case AR6000_XIOCTL_DELE_AGGR:
        {
            WMI_DELBA_REQ_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
                ret = -EFAULT;
            } else {
                wmi_delete_aggr_cmd(ar->arWmi, cmd.tid, cmd.is_sender_initiator);
            }
        }
        break;

        case AR6000_XIOCTL_ALLOW_AGGR:
        {
            WMI_ALLOW_AGGR_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
                ret = -EFAULT;
            } else {
                wmi_allow_aggr_cmd(ar->arWmi, cmd.tx_allow_aggr, cmd.rx_allow_aggr);
            }
        }
        break;

        case AR6000_XIOCTL_SET_HT_CAP:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&htCap, userdata,
                                      sizeof(htCap)))
            {
                ret = -EFAULT;
            } else {

                if (wmi_set_ht_cap_cmd(ar->arWmi, &htCap) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_SET_HT_OP:
        {
             if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&htOp, userdata,
                                      sizeof(htOp)))
            {
                 ret = -EFAULT;
             } else {

                if (wmi_set_ht_op_cmd(ar->arWmi, htOp.sta_chan_width) != A_OK)
                {
                     ret = -EIO;
               }
             }
             break;
        }
#endif
        case AR6000_XIOCTL_ACL_DATA:
        {
            void *osbuf = NULL;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (ar6000_create_acl_data_osbuf(dev, (A_UINT8*)userdata, &osbuf) != A_OK) {
                     ret = -EIO;
            } else {
                if (wmi_data_hdr_add(ar->arWmi, osbuf, DATA_MSGTYPE, 0, WMI_DATA_HDR_DATA_TYPE_ACL,0,NULL) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("XIOCTL_ACL_DATA - wmi_data_hdr_add failed\n"));
                } else {
                    /* Send data buffer over HTC */
                    ar6000_acl_data_tx(osbuf, ar->arNetDev);
                }
            }
            break;
        }
        case AR6000_XIOCTL_HCI_CMD:
        {
            char tmp_buf[512];
            A_INT8 i;
            WMI_HCI_CMD *cmd = (WMI_HCI_CMD *)tmp_buf;
            A_UINT8 size;

            size = sizeof(cmd->cmd_buf_sz);
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(cmd, userdata, size)) {
                 ret = -EFAULT;
            } else if(copy_from_user(cmd->buf, userdata + size, cmd->cmd_buf_sz)) {
                    ret = -EFAULT;
            } else {
                if (wmi_send_hci_cmd(ar->arWmi, cmd->buf, cmd->cmd_buf_sz) != A_OK) {
                     ret = -EIO;
                }else if(loghci) {
                    A_PRINTF_LOG("HCI Command To PAL --> \n");
                    for(i = 0; i < cmd->cmd_buf_sz; i++) {
                        A_PRINTF_LOG("0x%02x ",cmd->buf[i]);
                        if((i % 10) == 0) {
                            A_PRINTF_LOG("\n");
                        }
                    }
                    A_PRINTF_LOG("\n");
                    A_PRINTF_LOG("==================================\n");
                }
            }
            break;
        }
        case AR6000_XIOCTL_WLAN_CONN_PRECEDENCE:
        {
            WMI_SET_BT_WLAN_CONN_PRECEDENCE cmd;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
                ret = -EFAULT;
            } else {
                if (cmd.precedence == BT_WLAN_CONN_PRECDENCE_WLAN ||
                            cmd.precedence == BT_WLAN_CONN_PRECDENCE_PAL) {
                    if ( wmi_set_wlan_conn_precedence_cmd(ar->arWmi, cmd.precedence) != A_OK) {
                        ret = -EIO;
                    }
                } else {
                    ret = -EINVAL;
                }
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_STAT:
        {
            ret = ar6000_ioctl_get_ap_stats(dev, rq);
            break;
        }
        case AR6000_XIOCTL_SET_TX_SELECT_RATES:
        {
            WMI_SET_TX_SELECT_RATES_CMD masks;

             if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&masks, userdata,
                                      sizeof(masks)))
            {
                 ret = -EFAULT;
             } else {

                if (wmi_set_tx_select_rates_cmd(ar->arWmi, masks.rateMasks) != A_OK)
                {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_AP_GET_HIDDEN_SSID:
        {
            WMI_AP_HIDDEN_SSID_CMD ssid;
            ssid.hidden_ssid = ar->ap_hidden_ssid;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((WMI_AP_HIDDEN_SSID_CMD *)rq->ifr_data,
                                    &ssid, sizeof(WMI_AP_HIDDEN_SSID_CMD))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_COUNTRY:
        {
            WMI_AP_SET_COUNTRY_CMD cty;
            A_MEMCPY(cty.countryCode, ar->ap_country_code, 3);

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((WMI_AP_SET_COUNTRY_CMD *)rq->ifr_data,
                                    &cty, sizeof(WMI_AP_SET_COUNTRY_CMD))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_WMODE:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((A_UINT8 *)rq->ifr_data,
                                    &ar->ap_wmode, sizeof(A_UINT8))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_DTIM:
        {
            WMI_AP_SET_DTIM_CMD dtim;
            dtim.dtim = ar->ap_dtim_period;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((WMI_AP_SET_DTIM_CMD *)rq->ifr_data,
                                    &dtim, sizeof(WMI_AP_SET_DTIM_CMD))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_BINTVL:
        {
            WMI_BEACON_INT_CMD bi;
            bi.beaconInterval = ar->ap_beacon_interval;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((WMI_BEACON_INT_CMD *)rq->ifr_data,
                                    &bi, sizeof(WMI_BEACON_INT_CMD))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_RTS:
        {
            WMI_SET_RTS_CMD rts;
            rts.threshold = ar->arRTS;
	     
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((WMI_SET_RTS_CMD *)rq->ifr_data,
                                    &rts, sizeof(WMI_SET_RTS_CMD))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_FETCH_TARGET_REGS:
        {
            A_UINT32 targregs[AR6003_FETCH_TARG_REGS_COUNT];

            if (ar->arTargetType == TARGET_TYPE_AR6003) {
                ar6k_FetchTargetRegs(hifDevice, targregs);
                if (copy_to_user((A_UINT32 *)rq->ifr_data, &targregs, sizeof(targregs)))
                {
                    ret = -EFAULT;
                }
            } else {
                ret = -EOPNOTSUPP;
            }
            break;
        }
        case AR6000_XIOCTL_AP_SET_11BG_RATESET:
        {
            WMI_AP_SET_11BG_RATESET_CMD  rate;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&rate, userdata, sizeof(rate))) {
                ret = -EFAULT;
            } else {
                wmi_ap_set_rateset(ar->arWmi, rate.rateset);
            }
            break;
        }
        case AR6000_XIOCTL_GET_WLAN_SLEEP_STATE:
        {
            WMI_REPORT_SLEEP_STATE_EVENT  wmiSleepEvent ;

            if (ar->arWlanState == WLAN_ENABLED) {
                wmiSleepEvent.sleepState = WMI_REPORT_SLEEP_STATUS_IS_AWAKE;
            } else {
                wmiSleepEvent.sleepState = WMI_REPORT_SLEEP_STATUS_IS_DEEP_SLEEP;
            }
            rq->ifr_ifru.ifru_ivalue = ar->arWlanState; /* return value */

            ar6000_send_event_to_app(ar, WMI_REPORT_SLEEP_STATE_EVENTID, (A_UINT8*)&wmiSleepEvent,
                                     sizeof(WMI_REPORT_SLEEP_STATE_EVENTID));
            break;
        }
#ifdef CONFIG_PM
        case AR6000_XIOCTL_SET_BT_HW_POWER_STATE:
        {
            unsigned int state;
	    if (get_user(state, (unsigned int *)userdata)) {
		ret = -EFAULT;
		break;
	    }
            if (ar6000_set_bt_hw_state(ar, state)!=A_OK) {
                ret = -EIO;
            }       
        }
            break;
        case AR6000_XIOCTL_GET_BT_HW_POWER_STATE:
            rq->ifr_ifru.ifru_ivalue = !ar->arBTOff; /* return value */
            break;
#endif

        case AR6000_XIOCTL_WMI_SET_TX_SGI_PARAM:
        {
             WMI_SET_TX_SGI_PARAM_CMD SGICmd;

             if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
             } else if (copy_from_user(&SGICmd, userdata,
                                       sizeof(SGICmd))){
                 ret = -EFAULT;
             } else{
                     if (wmi_SGI_cmd(ar->arWmi, SGICmd.sgiMask, SGICmd.sgiPERThreshold) != A_OK) {
                         ret = -EIO;
                     }

             }
             break;
        }

        case AR6000_XIOCTL_ADD_AP_INTERFACE:
#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
        {
            char ap_ifname[IFNAMSIZ] = {0,};
            if (copy_from_user(ap_ifname, userdata, IFNAMSIZ)) {
                ret = -EFAULT;
            } else {
                if (ar6000_add_ap_interface(ar, ap_ifname) != A_OK) {
                    ret = -EIO;
                } 
            }
        }
#else
            ret = -EOPNOTSUPP;
#endif
            break;
        case AR6000_XIOCTL_REMOVE_AP_INTERFACE:
#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
            if (ar6000_remove_ap_interface(ar) != A_OK) {
                ret = -EIO;
            } 
#else
            ret = -EOPNOTSUPP;
#endif
            break;

        default:
            ret = -EOPNOTSUPP;
    }

ioctl_done:
    rtnl_lock(); /* restore rtnl state */
    dev_put(dev);

    return ret;
}

A_UINT8 mac_cmp_wild(A_UINT8 *mac, A_UINT8 *new_mac, A_UINT8 wild, A_UINT8 new_wild)
{
    A_UINT8 i;

    for(i=0;i<ATH_MAC_LEN;i++) {
        if((wild & 1<<i) && (new_wild & 1<<i)) continue;
        if(mac[i] != new_mac[i]) return 1;
    }
    if((A_MEMCMP(new_mac, null_mac, 6)==0) && new_wild &&
        (wild != new_wild)) {
        return 1;
    }

    return 0;
}

A_UINT8    acl_add_del_mac(WMI_AP_ACL *a, WMI_AP_ACL_MAC_CMD *acl)
{
    A_INT8    already_avail=-1, free_slot=-1, i;

    /* To check whether this mac is already there in our list */
    for(i=AP_ACL_SIZE-1;i>=0;i--)
    {
        if(mac_cmp_wild(a->acl_mac[i], acl->mac, a->wildcard[i],
            acl->wildcard)==0)
                already_avail = i;

        if(!((1 << i) & a->index))
            free_slot = i;
    }

    if(acl->action == ADD_MAC_ADDR)
    {
        /* Dont add mac if it is already available */
        if((already_avail >= 0) || (free_slot == -1))
            return 0;

        A_MEMCPY(a->acl_mac[free_slot], acl->mac, ATH_MAC_LEN);
        a->index = a->index | (1 << free_slot);
        acl->index = free_slot;
        a->wildcard[free_slot] = acl->wildcard;
        return 1;
    }
    else if(acl->action == DEL_MAC_ADDR)
    {
        if(acl->index > AP_ACL_SIZE)
            return 0;

        if(!(a->index & (1 << acl->index)))
            return 0;

        A_MEMZERO(a->acl_mac[acl->index],ATH_MAC_LEN);
        a->index = a->index & ~(1 << acl->index);
        a->wildcard[acl->index] = 0;
        return 1;
    }

    return 0;
}
