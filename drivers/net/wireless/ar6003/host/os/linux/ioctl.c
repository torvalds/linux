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
#include "a_drv_api.h"
#include "ieee80211_ioctl.h"
#include "ar6kap_common.h"
#include "targaddrs.h"
#include "a_hci.h"
#include "wlan_config.h"
#include "wac_defs.h"
#ifdef P2P
#include "p2p_api.h"
#endif /* P2P */

extern int enablerssicompensation;
A_UINT32 tcmdRxFreq;
extern unsigned int wmitimeout;
extern int tspecCompliance;
extern int bmienable;
extern int bypasswmi;
extern int loghci;

static int
ar6000_ioctl_get_roam_tbl(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if(wmi_get_roam_tbl_cmd(arPriv->arWmi) != A_OK) {
        return -EIO;
    }

    return 0;
}

static int
ar6000_ioctl_get_roam_data(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    /* currently assume only roam times are required */
    if(wmi_get_roam_data_cmd(arPriv->arWmi, ROAM_DATA_TIME) != A_OK) {
        return -EIO;
    }


    return 0;
}

static int
ar6000_ioctl_set_roam_ctrl(struct net_device *dev, char *userdata)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_ROAM_CTRL_CMD cmd;
    A_UINT8 size = sizeof(cmd);
    AR_SOFTC_T *ar = arPriv->arSoftc;

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

    if(wmi_set_roam_ctrl_cmd(arPriv->arWmi, &cmd, size) != A_OK) {
        return -EIO;
    }

    return 0;
}

static int
ar6000_ioctl_set_powersave_timers(struct net_device *dev, char *userdata)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_POWERSAVE_TIMERS_POLICY_CMD cmd;
    A_UINT8 size = sizeof(cmd);
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, size)) {
        return -EFAULT;
    }

    if (copy_from_user(&cmd, userdata, size)) {
        return -EFAULT;
    }

    if(wmi_set_powersave_timers_cmd(arPriv->arWmi, &cmd, size) != A_OK) {
        return -EIO;
    }

    return 0;
}

static int
ar6000_ioctl_set_qos_supp(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_QOS_SUPP_CMD cmd;
    A_STATUS ret;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    ret = wmi_set_qos_supp_cmd(arPriv->arWmi, cmd.status);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_WMM_CMD cmd;
    A_STATUS ret;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    if (cmd.status == WMI_WMM_ENABLED) {
        arPriv->arWmmEnabled = TRUE;
    } else {
        arPriv->arWmmEnabled = FALSE;
    }

    ret = wmi_set_wmm_cmd(arPriv->arWmi, cmd.status);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_WMM_TXOP_CMD cmd;
    A_STATUS ret;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    ret = wmi_set_wmm_txop(arPriv->arWmi, cmd.txopEnable);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    A_STATUS ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if(copy_to_user((char *)((unsigned int*)rq->ifr_data + 1),
                            &arPriv->arRegCode, sizeof(arPriv->arRegCode)))
        ret = -EFAULT;

    return ret;
}

static int
ar6000_ioctl_set_country(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_AP_SET_COUNTRY_CMD cmd;
    A_STATUS ret;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(cmd)))
    {
        return -EFAULT;
    }

    arPriv->ap_profile_flag = 1; /* There is a change in profile */

    ret = wmi_set_country(arPriv->arWmi, cmd.countryCode);
    A_MEMCPY(arPriv->arAp.ap_country_code, cmd.countryCode, 3);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_POWER_MODE_CMD power_mode;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    power_mode.powerMode = wmi_get_power_mode_cmd(arPriv->arWmi);
    if (copy_to_user(rq->ifr_data, &power_mode, sizeof(WMI_POWER_MODE_CMD))) {
        ret = -EFAULT;
    }

    return ret;
}


static int
ar6000_ioctl_set_channelParams(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_CHANNEL_PARAMS_CMD cmd, *cmdp;
    int ret = 0;
    int i = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;
    AR_SOFTC_DEV_T *arTempPriv = NULL;
  

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if( (arPriv->arNextMode == AP_NETWORK) && (cmd.numChannels || cmd.scanParam) ) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ERROR: Only wmode is allowed in AP mode\n"));
        return -EIO;
    }

    if (cmd.numChannels > 1) {
        cmdp = A_MALLOC(130);
        if (copy_from_user(cmdp, rq->ifr_data,
                           sizeof (*cmdp) +
                           ((cmd.numChannels - 1) * sizeof(A_UINT16))))
        {
            A_FREE(cmdp);
            return -EFAULT;
        }
    } else {
        cmdp = &cmd;
    }

    if ((arPriv->arPhyCapability == WMI_11NG_CAPABILITY) &&
        ((cmdp->phyMode == WMI_11A_MODE) || (cmdp->phyMode == WMI_11AG_MODE)))
    {
        ret = -EINVAL;
    }
 
    for(i = 0;i < ar->arConfNumDev;i++){
       arTempPriv = ar->arDev[i];
       if (arTempPriv != arPriv){
          if (((arTempPriv->phymode == WMI_11A_MODE) && (cmdp->phyMode != WMI_11A_MODE)) || (((arTempPriv->phymode != WMI_11A_MODE) && arTempPriv->phymode != WMI_11AG_MODE ) && (cmdp->phyMode == WMI_11A_MODE))){
            ret = -EINVAL;
            break;  
           }
       }  

    }
    if (!ret &&
        (wmi_set_channelParams_cmd(arPriv->arWmi, cmdp->scanParam, cmdp->phyMode,
                                   cmdp->numChannels, cmdp->channelList)
         != A_OK))
    {
        ret = -EIO;
    }

   if (!ret)
    arPriv->phymode = cmdp->phyMode;

    if (cmd.numChannels > 1) {
        A_FREE(cmdp);
    }

    /* Set the profile change flag to allow a commit cmd */
    if (!ret)
    arPriv->ap_profile_flag = 1;

    return ret;
}


static int
ar6000_ioctl_set_snr_threshold(struct net_device *dev, struct ifreq *rq)
{

    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SNR_THRESHOLD_PARAMS_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if( wmi_set_snr_threshold_params(arPriv->arWmi, &cmd) != A_OK ) {
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

    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_STA_T *arSta  = &arPriv->arSta;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
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

    A_MEMCPY(arSta->rssi_map, &rssiParams.tholds, sizeof(arSta->rssi_map));
    /*
     *  only 6 elements, so use bubble sorting, in ascending order
     */
    for (i = 5; i > 0; i--) {
        for (j = 0; j < i; j++) { /* above tholds */
            if (arSta->rssi_map[j+1].rssi < arSta->rssi_map[j].rssi) {
                SWAP_THOLD(arSta->rssi_map[j+1], arSta->rssi_map[j]);
            } else if (arSta->rssi_map[j+1].rssi == arSta->rssi_map[j].rssi) {
                return EFAULT;
            }
        }
    }
    for (i = 11; i > 6; i--) {
        for (j = 6; j < i; j++) { /* below tholds */
            if (arSta->rssi_map[j+1].rssi < arSta->rssi_map[j].rssi) {
                SWAP_THOLD(arSta->rssi_map[j+1], arSta->rssi_map[j]);
            } else if (arSta->rssi_map[j+1].rssi == arSta->rssi_map[j].rssi) {
                return EFAULT;
            }
        }
    }

#ifdef DEBUG
    for (i = 0; i < 12; i++) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("thold[%d].tag: %d, thold[%d].rssi: %d \n",
                i, arSta->rssi_map[i].tag, i, arSta->rssi_map[i].rssi));
    }
#endif

    if (enablerssicompensation) {
        for (i = 0; i < 6; i++)
            arSta->rssi_map[i].rssi = rssi_compensation_reverse_calc(arPriv, arSta->rssi_map[i].rssi, TRUE);
        for (i = 6; i < 12; i++)
            arSta->rssi_map[i].rssi = rssi_compensation_reverse_calc(arPriv, arSta->rssi_map[i].rssi, FALSE);
    }

    cmd.thresholdAbove1_Val = arSta->rssi_map[0].rssi;
    cmd.thresholdAbove2_Val = arSta->rssi_map[1].rssi;
    cmd.thresholdAbove3_Val = arSta->rssi_map[2].rssi;
    cmd.thresholdAbove4_Val = arSta->rssi_map[3].rssi;
    cmd.thresholdAbove5_Val = arSta->rssi_map[4].rssi;
    cmd.thresholdAbove6_Val = arSta->rssi_map[5].rssi;
    cmd.thresholdBelow1_Val = arSta->rssi_map[6].rssi;
    cmd.thresholdBelow2_Val = arSta->rssi_map[7].rssi;
    cmd.thresholdBelow3_Val = arSta->rssi_map[8].rssi;
    cmd.thresholdBelow4_Val = arSta->rssi_map[9].rssi;
    cmd.thresholdBelow5_Val = arSta->rssi_map[10].rssi;
    cmd.thresholdBelow6_Val = arSta->rssi_map[11].rssi;
    
    if( wmi_set_rssi_threshold_params(arPriv->arWmi, &cmd) != A_OK ) {
        ret = -EIO;
    }

    return ret;
}

static int
ar6000_ioctl_set_lq_threshold(struct net_device *dev, struct ifreq *rq)
{

    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_LQ_THRESHOLD_PARAMS_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, (char *)((unsigned int *)rq->ifr_data + 1), sizeof(cmd))) {
        return -EFAULT;
    }

    if( wmi_set_lq_threshold_params(arPriv->arWmi, &cmd) != A_OK ) {
        ret = -EIO;
    }

    return ret;
}


static int
ar6000_ioctl_set_probedSsid(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_PROBED_SSID_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_probedSsid_cmd(arPriv->arWmi, cmd.entryIndex, cmd.flag, cmd.ssidLength,
                                  cmd.ssid) != A_OK)
    {
        ret = -EIO;
    }

    return ret;
}

static int
ar6000_ioctl_set_badAp(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_ADD_BAD_AP_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

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
        if (wmi_deleteBadAp_cmd(arPriv->arWmi, cmd.badApIndex) != A_OK) {
            ret = -EIO;
        }
    } else {
        if (wmi_addBadAp_cmd(arPriv->arWmi, cmd.badApIndex, cmd.bssid) != A_OK) {
            ret = -EIO;
        }
    }

    return ret;
}

static int
ar6000_ioctl_create_qos(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_CREATE_PSTREAM_CMD cmd;
    A_STATUS ret;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }



    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    ret = wmi_verify_tspec_params(&cmd, tspecCompliance);
    if (ret == A_OK)
        ret = wmi_create_pstream_cmd(arPriv->arWmi, &cmd);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_DELETE_PSTREAM_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    ret = wmi_delete_pstream_cmd(arPriv->arWmi, cmd.trafficClass, cmd.tsid);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    struct ar6000_queuereq qreq;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if( copy_from_user(&qreq, rq->ifr_data,
                  sizeof(struct ar6000_queuereq)))
        return -EFAULT;

    qreq.activeTsids = wmi_get_mapped_qos_queue(arPriv->arWmi, qreq.trafficClass);

    if (copy_to_user(rq->ifr_data, &qreq,
                 sizeof(struct ar6000_queuereq)))
    {
        ret = -EFAULT;
    }

    return ret;
}

#ifdef CONFIG_HOST_TCMD_SUPPORT
static A_STATUS
ar6000_ioctl_tcmd_cmd_resp(struct net_device *dev, struct ifreq *rq, A_UINT8 *data, A_UINT32 len)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T *ar = arPriv->arSoftc;
    A_UINT8    buf[4+TC_CMDS_SIZE_MAX];
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
    if (wmi_test_cmd(arPriv->arWmi, data, len) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arPriv->arEvent, ar->tcmdRxReport != 0, wmitimeout * HZ);

    if (signal_pending(current)) {
        ret = -EINTR;
    }

    *(A_UINT16*)&(buf[0]) = ar->tcmdResp.len;
    buf[2] = ar->tcmdResp.ver;
    A_MEMCPY((buf+4), ar->tcmdResp.buf, sizeof(ar->tcmdResp.buf));

    if (!ret && copy_to_user(rq->ifr_data, buf, sizeof(buf))) {
        ret = -EFAULT;
    }

    up(&ar->arSem);

    return ret;
}

static A_STATUS
ar6000_ioctl_tcmd_get_rx_report(struct net_device *dev,
                                 struct ifreq *rq, A_UINT8 *data, A_UINT32 len)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T *ar = arPriv->arSoftc;
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
    if (wmi_test_cmd(arPriv->arWmi, data, len) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arPriv->arEvent, ar->tcmdRxReport != 0, wmitimeout * HZ);

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
ar6000_tcmd_rx_report_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 * results, int len)
{
    
    AR_SOFTC_T *ar = arPriv->arSoftc;
    TCMD_CONT_RX * rx_rep = (TCMD_CONT_RX *)results;

    if (TC_CMD_RESP == rx_rep->act) {
        TC_CMDS *tCmd = (TC_CMDS *)results;
        ar->tcmdResp.len = tCmd->hdr.u.parm.length;
        ar->tcmdResp.ver = tCmd->hdr.u.parm.version;
        A_MEMZERO(ar->tcmdResp.buf, sizeof(ar->tcmdResp.buf));
        A_MEMCPY(ar->tcmdResp.buf, tCmd->buf, sizeof(ar->tcmdResp.buf));
        ar->tcmdRxReport = 1;
    }
    else { /*(rx_rep->act == TCMD_CONT_RX_REPORT) */ 
    if (enablerssicompensation) {
        rx_rep->u.report.rssiInDBm = rssi_compensation_calc_tcmd(ar, tcmdRxFreq, rx_rep->u.report.rssiInDBm,rx_rep->u.report.totalPkt);
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
    }

    wake_up(&arPriv->arEvent);
}
#endif /* CONFIG_HOST_TCMD_SUPPORT*/

static int
ar6000_ioctl_set_error_report_bitmask(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_TARGET_ERROR_REPORT_BITMASK cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    ret = wmi_set_error_report_bitmask(arPriv->arWmi, cmd.bitmask);

    return  (ret==0 ? ret : -EINVAL);
}

static int
ar6000_clear_target_stats(struct net_device *dev)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    TARGET_STATS *pStats = &arPriv->arTargetStats;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
    A_MEMZERO(pStats, sizeof(TARGET_STATS));
    AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
    return ret;
}

static int
ar6000_ioctl_get_target_stats(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar    = arPriv->arSoftc;
    TARGET_STATS_CMD cmd;
    TARGET_STATS *pStats = &arPriv->arTargetStats;
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

    arPriv->statsUpdatePending = TRUE;

    if(wmi_get_stats_cmd(arPriv->arWmi) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arPriv->arEvent, arPriv->statsUpdatePending == FALSE, wmitimeout * HZ);

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

    a_meminfo_report(FALSE);

    return ret;
}

static int
ar6000_ioctl_get_ap_stats(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    A_UINT32 action; /* Allocating only the desired space on the frame. Declaring is as a WMI_AP_MODE_STAT variable results in exceeding the compiler imposed limit on the maximum frame size */
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;
    WMI_PER_STA_STAT *pStats = ar->arAPStats;
    WMI_AP_MODE_STAT ret_stat;
    A_UINT8 i, j=0;
        
    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&action, (char *)((unsigned int*)rq->ifr_data + 1),
                                sizeof(A_UINT32)))
    {
        return -EFAULT;
    }
    if (action == AP_CLEAR_STATS) {
        AR6000_SPIN_LOCK(&ar->arLock, 0);
        for(i = 0; i < NUM_CONN; i++) {
            if(ar->connTbl[i].arPriv == arPriv) {
                pStats[i].tx_bytes = 0;
                pStats[i].tx_pkts = 0;
                pStats[i].tx_error = 0;
                pStats[i].tx_discard = 0;
                pStats[i].rx_bytes = 0;
                pStats[i].rx_pkts = 0;
                pStats[i].rx_error = 0;
                pStats[i].rx_discard = 0;
            }
        }
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        return ret;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    arPriv->statsUpdatePending = TRUE;

    if(wmi_get_stats_cmd(arPriv->arWmi) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arPriv->arEvent, arPriv->statsUpdatePending == FALSE, wmitimeout * HZ);

    if (signal_pending(current)) {
        ret = -EINTR;
    }
    
    A_MEMZERO(&ret_stat, sizeof(ret_stat));
    for(i = 0; i < NUM_CONN; i++) {
        if(ar->connTbl[i].arPriv == arPriv) {
            ret_stat.sta[j].aid          = pStats[i].aid;
            ret_stat.sta[j].tx_bytes     = pStats[i].tx_bytes;
            ret_stat.sta[j].tx_pkts      = pStats[i].tx_pkts;
            ret_stat.sta[j].tx_error     = pStats[i].tx_error;
            ret_stat.sta[j].tx_discard   = pStats[i].tx_discard;
            ret_stat.sta[j].rx_bytes     = pStats[i].rx_bytes;
            ret_stat.sta[j].rx_pkts      = pStats[i].rx_pkts;
            ret_stat.sta[j].rx_error     = pStats[i].rx_error;
            ret_stat.sta[j].rx_discard   = pStats[i].rx_discard;
            j++;
        }
    }
    
    if (!ret && copy_to_user(rq->ifr_data, &ret_stat, sizeof(ret_stat))) {
        ret = -EFAULT;
    }

    up(&ar->arSem);

    return ret;
}

static int
ar6000_ioctl_set_access_params(struct net_device *dev, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_ACCESS_PARAMS_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_access_params_cmd(arPriv->arWmi, cmd.ac, cmd.txop, cmd.eCWmin, cmd.eCWmax,
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_DISC_TIMEOUT_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, rq->ifr_data, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_disctimeout_cmd(arPriv->arWmi, cmd.disconnectTimeout) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_VOICE_PKT_SIZE_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_voice_pkt_size_cmd(arPriv->arWmi, cmd.voicePktSize) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_MAX_SP_LEN_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_max_sp_len_cmd(arPriv->arWmi, cmd.maxSPLen) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BT_STATUS_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_bt_status_cmd(arPriv->arWmi, cmd.streamType, cmd.status) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BT_PARAMS_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_bt_params_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BTCOEX_FE_ANT_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_btcoex_fe_ant_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_btcoex_colocated_bt_dev_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_btcoex_btinquiry_page_config_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BTCOEX_SCO_CONFIG_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_btcoex_sco_config_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BTCOEX_A2DP_CONFIG_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_btcoex_a2dp_config_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_btcoex_aclcoex_config_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    WMI_SET_BTCOEX_DEBUG_CMD cmd;
    int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_btcoex_debug_cmd(arPriv->arWmi, &cmd) == A_OK)
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
     WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD cmd;
     int ret = 0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }


    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
    return -EFAULT;
    }

    if (wmi_set_btcoex_bt_operating_status_cmd(arPriv->arWmi, &cmd) == A_OK)
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

    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR6000_BTCOEX_CONFIG btcoexConfig;
    WMI_BTCOEX_CONFIG_EVENT *pbtcoexConfigEv = &arPriv->arBtcoexConfig;

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

    if (wmi_get_btcoex_config_cmd(arPriv->arWmi, (WMI_GET_BTCOEX_CONFIG_CMD *)&btcoexConfig.configCmd) != A_OK)
    {
        up(&ar->arSem);
        return -EIO;
    }

    arPriv->statsUpdatePending = TRUE;

    wait_event_interruptible_timeout(arPriv->arEvent, arPriv->statsUpdatePending == FALSE, wmitimeout * HZ);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR6000_BTCOEX_STATS btcoexStats;
    WMI_BTCOEX_STATS_EVENT *pbtcoexStats = &arPriv->arBtcoexStats;
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

    if (wmi_get_btcoex_stats_cmd(arPriv->arWmi) != A_OK)
    {
        up(&ar->arSem);
        return -EIO;
    }

    arPriv->statsUpdatePending = TRUE;

    wait_event_interruptible_timeout(arPriv->arEvent, arPriv->statsUpdatePending == FALSE, wmitimeout * HZ);

    if (signal_pending(current)) {
       ret = -EINTR;
    }

    if (!ret && copy_to_user(btcoexStats.statsEvent, pbtcoexStats, sizeof(WMI_BTCOEX_STATS_EVENT))) {
            ret = -EFAULT;
    }


    up(&ar->arSem);

    return(ret);
}

static int
ar6000_xioctl_set_excess_tx_retry_thres_cmd(struct net_device * dev, char * userdata)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    WMI_SET_EXCESS_TX_RETRY_THRES_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
        return -EFAULT;
    }

    if (wmi_set_excess_tx_retry_thres_cmd(arPriv->arWmi, &cmd) != A_OK)
    {
        ret = -EINVAL;
    }
    return(ret);
}

static int
ar6000_xioctl_wac_ctrl_req_get_cmd(struct net_device * dev, char * userdata, struct ifreq *rq)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    WMI_WAC_CTRL_REQ_CMD cmd;
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

    if (copy_from_user(&cmd, userdata, sizeof(WMI_WAC_CTRL_REQ_CMD))) {
        return -EFAULT;
    }

    if (wmi_wac_ctrl_req_cmd(arPriv->arWmi, &cmd) != A_OK)
    {
        up(&ar->arSem);
        return -EIO;
    }

    arPriv->statsUpdatePending = TRUE;

    wait_event_interruptible_timeout(arPriv->arEvent, arPriv->statsUpdatePending == FALSE, wmitimeout * HZ);

    if (signal_pending(current)) {
       ret = -EINTR;
    }

    if (!ret && copy_to_user(rq->ifr_data, &arPriv->wacInfo, sizeof(WMI_GET_WAC_INFO))) {
            ret = -EFAULT;
    }

    up(&ar->arSem);

    return(ret);
}

static int
ar6000_xioctl_set_passphrase_cmd(struct net_device * dev, char * userdata)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta  = &arPriv->arSta;
    WMI_SET_PASSPHRASE_CMD cmd;
    int ret = 0;

    if (ar->arWmiReady == FALSE) {
    return -EIO;
    }

    if (copy_from_user(&cmd, userdata, sizeof(cmd))) {
    return -EFAULT;
    }

    if (wmi_set_passphrase_cmd(arPriv->arWmi, &cmd) == A_OK)
    {
        /* enable WPA offload */
        arSta->arConnectCtrlFlags |= CONNECT_DO_WPA_OFFLOAD;
        ret = 0;
    } else {
        ret = -EINVAL;
    }
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
ar6000_gpio_intr_rx(AR_SOFTC_DEV_T *arPriv, A_UINT32 intr_mask, A_UINT32 input_values)
{
    gpio_intr_results.intr_mask = intr_mask;
    gpio_intr_results.input_values = input_values;
    *((volatile A_BOOL *)&gpio_intr_available) = TRUE;
    wake_up(&arPriv->arEvent);
}

/*
 * This is called when a response is received from the Target
 * for a previous or ar6000_gpio_input_get or ar6000_gpio_register_get
 * call.
 */
void
ar6000_gpio_data_rx(AR_SOFTC_DEV_T *arPriv, A_UINT32 reg_id, A_UINT32 value)
{
    gpio_reg_results.gpioreg_id = reg_id;
    gpio_reg_results.value = value;
    *((volatile A_BOOL *)&gpio_data_available) = TRUE;
    wake_up(&arPriv->arEvent);
}

/*
 * This is called when an acknowledgement is received from the Target
 * for a previous or ar6000_gpio_output_set or ar6000_gpio_register_set
 * call.
 */
void
ar6000_gpio_ack_rx(AR_SOFTC_DEV_T *arPriv)
{
    gpio_ack_received = TRUE;
    wake_up(&arPriv->arEvent);
}

A_STATUS
ar6000_gpio_output_set(struct net_device *dev,
                       A_UINT32 set_mask,
                       A_UINT32 clear_mask,
                       A_UINT32 enable_mask,
                       A_UINT32 disable_mask)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    gpio_ack_received = FALSE;
    return wmi_gpio_output_set(arPriv->arWmi,
                set_mask, clear_mask, enable_mask, disable_mask);
}

static A_STATUS
ar6000_gpio_input_get(struct net_device *dev)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    *((volatile A_BOOL *)&gpio_data_available) = FALSE;
    return wmi_gpio_input_get(arPriv->arWmi);
}

static A_STATUS
ar6000_gpio_register_set(struct net_device *dev,
                         A_UINT32 gpioreg_id,
                         A_UINT32 value)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    gpio_ack_received = FALSE;
    return wmi_gpio_register_set(arPriv->arWmi, gpioreg_id, value);
}

static A_STATUS
ar6000_gpio_register_get(struct net_device *dev,
                         A_UINT32 gpioreg_id)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    *((volatile A_BOOL *)&gpio_data_available) = FALSE;
    return wmi_gpio_register_get(arPriv->arWmi, gpioreg_id);
}

static A_STATUS
ar6000_gpio_intr_ack(struct net_device *dev,
                     A_UINT32 ack_mask)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    gpio_intr_available = FALSE;
    return wmi_gpio_intr_ack(arPriv->arWmi, ack_mask);
}
#endif /* CONFIG_HOST_GPIO_SUPPORT */

#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
static struct prof_count_s prof_count_results;
static A_BOOL prof_count_available; /* Requested GPIO data available */

static A_STATUS
prof_count_get(struct net_device *dev)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    *((volatile A_BOOL *)&prof_count_available) = FALSE;
    return wmi_prof_count_get_cmd(arPriv->arWmi);
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
ar6000_ioctl_ap_setparam(AR_SOFTC_DEV_T *arPriv, int param, int value)
{
    int ret=0;

    switch(param) {
        case IEEE80211_PARAM_WPA:
            switch (value) {
                case WPA_MODE_WPA1:
                    arPriv->arAuthMode = WMI_WPA_AUTH;
                    break;
                case WPA_MODE_WPA2:
                    arPriv->arAuthMode = WMI_WPA2_AUTH;
                    break;
                case WPA_MODE_AUTO:
                    arPriv->arAuthMode = WMI_WPA_AUTH | WMI_WPA2_AUTH;
                    break;
                case WPA_MODE_NONE:
                    arPriv->arAuthMode = WMI_NONE_AUTH;
                    break;
            }
            break;
        case IEEE80211_PARAM_AUTHMODE:
            if(value == IEEE80211_AUTH_WPA_PSK) {
                if (WMI_WPA_AUTH == arPriv->arAuthMode) {
                    arPriv->arAuthMode = WMI_WPA_PSK_AUTH;
                } else if (WMI_WPA2_AUTH == arPriv->arAuthMode) {
                    arPriv->arAuthMode = WMI_WPA2_PSK_AUTH;
                } else if ((WMI_WPA_AUTH | WMI_WPA2_AUTH) == arPriv->arAuthMode) {
                    arPriv->arAuthMode = WMI_WPA_PSK_AUTH | WMI_WPA2_PSK_AUTH;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Error -  Setting PSK "\
                        "mode when WPA param was set to %d\n",
                        arPriv->arAuthMode));
                    ret = -EIO;
                }
            }
            break;
        case IEEE80211_PARAM_UCASTCIPHER:
            arPriv->arPairwiseCrypto = 0;
            if(value & (1<<IEEE80211_CIPHER_AES_CCM)) {
                arPriv->arPairwiseCrypto |= AES_CRYPT;
            }
            if(value & (1<<IEEE80211_CIPHER_TKIP)) {
                arPriv->arPairwiseCrypto |= TKIP_CRYPT;
            }
            if(!arPriv->arPairwiseCrypto) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                           ("Error - Invalid cipher in WPA \n"));
                ret = -EIO;
            }
            break;
        case IEEE80211_PARAM_PRIVACY:
            if(value == 0) {
                arPriv->arDot11AuthMode      = OPEN_AUTH;
                arPriv->arAuthMode           = WMI_NONE_AUTH;
                arPriv->arPairwiseCrypto     = NONE_CRYPT;
                arPriv->arPairwiseCryptoLen  = 0;
                arPriv->arGroupCrypto        = NONE_CRYPT;
                arPriv->arGroupCryptoLen     = 0;
            }
            break;
#ifdef WAPI_ENABLE
        case IEEE80211_PARAM_WAPI:
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("WAPI Policy: %d\n", value));
            arPriv->arDot11AuthMode      = OPEN_AUTH;
            arPriv->arAuthMode           = WMI_NONE_AUTH;
            if(value & 0x1) {
                arPriv->arPairwiseCrypto     = WAPI_CRYPT;
                arPriv->arGroupCrypto        = WAPI_CRYPT;
            } else {
                arPriv->arPairwiseCrypto     = NONE_CRYPT;
                arPriv->arGroupCrypto        = NONE_CRYPT;
            }
            break;
#endif
    }
    return ret;
}

int
ar6000_ioctl_setparam(AR_SOFTC_DEV_T *arPriv, int param, int value)
{
    A_BOOL profChanged = FALSE;
    int ret=0;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if(arPriv->arNextMode == AP_NETWORK) {
        arPriv->ap_profile_flag = 1; /* There is a change in profile */
        switch (param) {
            case IEEE80211_PARAM_WPA:
            case IEEE80211_PARAM_AUTHMODE:
            case IEEE80211_PARAM_UCASTCIPHER:
            case IEEE80211_PARAM_PRIVACY:
            case IEEE80211_PARAM_WAPI:
                ret = ar6000_ioctl_ap_setparam(arPriv, param, value);
                return ret;
        }
    }

    switch (param) {
        case IEEE80211_PARAM_WPA:
            switch (value) {
                case WPA_MODE_WPA1:
                    arPriv->arAuthMode = WMI_WPA_AUTH;
                    profChanged    = TRUE;
                    break;
                case WPA_MODE_WPA2:
                    arPriv->arAuthMode = WMI_WPA2_AUTH;
                    profChanged    = TRUE;
                    break;
                case WPA_MODE_NONE:
                    arPriv->arAuthMode = WMI_NONE_AUTH;
                    profChanged    = TRUE;
                    break;
            }
            break;
        case IEEE80211_PARAM_AUTHMODE:
            switch(value) {
                case IEEE80211_AUTH_WPA_PSK:
                    if (WMI_WPA_AUTH == arPriv->arAuthMode) {
                        arPriv->arAuthMode = WMI_WPA_PSK_AUTH;
                        profChanged    = TRUE;
                    } else if (WMI_WPA2_AUTH == arPriv->arAuthMode) {
                        arPriv->arAuthMode = WMI_WPA2_PSK_AUTH;
                        profChanged    = TRUE;
                    } else {
                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Error -  Setting PSK "\
                            "mode when WPA param was set to %d\n",
                            arPriv->arAuthMode));
                        ret = -EIO;
                    }
                    break;
                case IEEE80211_AUTH_WPA_CCKM:
                    if (WMI_WPA2_AUTH == arPriv->arAuthMode) {
                        arPriv->arAuthMode = WMI_WPA2_AUTH_CCKM;
                    } else {
                        arPriv->arAuthMode = WMI_WPA_AUTH_CCKM;
                    }
                    break;
                default:
                    break;
            }
            break;
        case IEEE80211_PARAM_UCASTCIPHER:
            switch (value) {
                case IEEE80211_CIPHER_AES_CCM:
                    arPriv->arPairwiseCrypto = AES_CRYPT;
                    profChanged          = TRUE;
                    break;
                case IEEE80211_CIPHER_TKIP:
                    arPriv->arPairwiseCrypto = TKIP_CRYPT;
                    profChanged          = TRUE;
                    break;
                case IEEE80211_CIPHER_WEP:
                    arPriv->arPairwiseCrypto = WEP_CRYPT;
                    profChanged          = TRUE;
                    break;
                case IEEE80211_CIPHER_NONE:
                    arPriv->arPairwiseCrypto = NONE_CRYPT;
                    profChanged          = TRUE;
                    break;
            }
            break;
        case IEEE80211_PARAM_UCASTKEYLEN:
            if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(value)) {
                ret = -EIO;
            } else {
                arPriv->arPairwiseCryptoLen = value;
            }
            break;
        case IEEE80211_PARAM_MCASTCIPHER:
            switch (value) {
                case IEEE80211_CIPHER_AES_CCM:
                    arPriv->arGroupCrypto = AES_CRYPT;
                    profChanged       = TRUE;
                    break;
                case IEEE80211_CIPHER_TKIP:
                    arPriv->arGroupCrypto = TKIP_CRYPT;
                    profChanged       = TRUE;
                    break;
                case IEEE80211_CIPHER_WEP:
                    arPriv->arGroupCrypto = WEP_CRYPT;
                    profChanged       = TRUE;
                    break;
                case IEEE80211_CIPHER_NONE:
                    arPriv->arGroupCrypto = NONE_CRYPT;
                    profChanged       = TRUE;
                    break;
            }
            break;
        case IEEE80211_PARAM_MCASTKEYLEN:
            if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(value)) {
                ret = -EIO;
            } else {
                arPriv->arGroupCryptoLen = value;
            }
            break;
        case IEEE80211_PARAM_COUNTERMEASURES:
            if (ar->arWmiReady == FALSE) {
                return -EIO;
            }
            wmi_set_tkip_countermeasures_cmd(arPriv->arWmi, value);
            break;
        default:
            break;
    }
    if ((arPriv->arNextMode != AP_NETWORK) && (profChanged == TRUE)) {
        /*
         * profile has changed.  Erase ssid to signal change
         */
        A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
    }

    return ret;
}

int
ar6000_sendkey(AR_SOFTC_DEV_T *arPriv, struct ieee80211req_key *ik, KEY_USAGE keyUsage)
{
    A_STATUS status;
    CRYPTO_TYPE keyType = NONE_CRYPT;
   
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
    
    if (IEEE80211_CIPHER_CCKM_KRK != ik->ik_type) {
        if (NONE_CRYPT == keyType) {
            return A_ERROR;
        }

        if ((WEP_CRYPT == keyType)&&(!arPriv->arConnected)) {
             int index = ik->ik_keyix;

            if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(ik->ik_keylen)) {
                return A_ERROR;
            }

            A_MEMZERO(arPriv->arWepKeyList[index].arKey,
                            sizeof(arPriv->arWepKeyList[index].arKey));
            A_MEMCPY(arPriv->arWepKeyList[index].arKey, ik->ik_keydata, ik->ik_keylen);
            arPriv->arWepKeyList[index].arKeyLen = ik->ik_keylen;

            if(ik->ik_flags & IEEE80211_KEY_DEFAULT){
                arPriv->arDefTxKeyIndex = index;
            }

            return A_OK;
        }

        status = wmi_addKey_cmd(arPriv->arWmi, ik->ik_keyix, keyType, keyUsage,
                                ik->ik_keylen, (A_UINT8 *)&ik->ik_keyrsc,
                                ik->ik_keydata, KEY_OP_INIT_VAL, ik->ik_macaddr,
                                SYNC_BOTH_WMIFLAG);

    } else {
        status = wmi_add_krk_cmd(arPriv->arWmi, ik->ik_keydata);
    }
    
    return status;
}

int
ar6000_ioctl_setkey(AR_SOFTC_DEV_T *arPriv, struct ieee80211req_key *ik)
{
    KEY_USAGE keyUsage;
    A_STATUS status;
    CRYPTO_TYPE keyType = NONE_CRYPT;

    if ( (0 == memcmp(ik->ik_macaddr, null_mac, IEEE80211_ADDR_LEN)) ||
         (0 == memcmp(ik->ik_macaddr, bcast_mac, IEEE80211_ADDR_LEN)) ) {
        keyUsage = GROUP_USAGE;
    } else {
        keyUsage = PAIRWISE_USAGE;
    }

    if(arPriv->arNextMode == AP_NETWORK) {
        AR_SOFTC_AP_T   *arAp = &arPriv->arAp;
        
        if (keyUsage == GROUP_USAGE) {
            A_MEMCPY(&arAp->ap_mode_bkey, ik, sizeof(struct ieee80211req_key));
        }

        #ifdef WAPI_ENABLE
        if(arPriv->arPairwiseCrypto == WAPI_CRYPT) {
            return ap_set_wapi_key(arPriv, ik);
        }
        #endif

        status = ar6000_sendkey(arPriv, ik, keyUsage);
        
    } else {
        AR_SOFTC_STA_T   *arSta = &arPriv->arSta;
        
        #ifdef USER_KEYS
        arSta->user_saved_keys.keyOk = FALSE;
        arSta->user_saved_keys.keyType = keyType;
        if (keyUsage == GROUP_USAGE) {
            A_MEMCPY(&arSta->user_saved_keys.bcast_ik, ik,
                     sizeof(struct ieee80211req_key));
        } else {
            A_MEMCPY(&arSta->user_saved_keys.ucast_ik, ik,
                     sizeof(struct ieee80211req_key));
        }
        #endif
        
        if (((WMI_WPA_PSK_AUTH == arPriv->arAuthMode) || 
            (WMI_WPA2_PSK_AUTH == arPriv->arAuthMode)) &&
            (GROUP_USAGE & keyUsage))
        {
            A_UNTIMEOUT(&arSta->disconnect_timer);
        }
        
        status = ar6000_sendkey(arPriv, ik, keyUsage);
        
        #ifdef USER_KEYS
        if (status == A_OK) {
            arSta->user_saved_keys.keyOk = TRUE;
        }
        #endif
    }

    if (status != A_OK) {
        return -EIO;
    }

    return 0;
}

int ar6000_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar         =  arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta      = &arPriv->arSta;
    AR_SOFTC_AP_T  *arAp       = &arPriv->arAp;  
    HIF_DEVICE *hifDevice = ar->arHifDevice;
    int ret = 0, param;
    unsigned int address = 0;
    unsigned int length = 0;
    unsigned char *buffer;
    char *userdata;

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
        get_user(cmd, (int *)rq->ifr_data);
        userdata = (char *)(((unsigned int *)rq->ifr_data)+1);
        if(is_xioctl_allowed(arPriv->arNextMode,
                    arPriv->arNetworkSubType, cmd) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("xioctl: cmd=%d not allowed in this mode\n",cmd));
            ret = -EOPNOTSUPP;
            goto ioctl_done;
        }
    } else {
        A_STATUS ret = is_iwioctl_allowed(arPriv->arNextMode, cmd);
        if(ret == A_ENOTSUP) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("iwioctl: cmd=0x%x not allowed in this mode\n", cmd));
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
         (cmd != AR6000_IOCTL_WMI_GETREV) &&
         (cmd != AR6000_XIOCTL_RESUME_DRIVER)))
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
                ret = ar6000_ioctl_setparam(arPriv,param,value);
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
                ar6000_ioctl_setkey(arPriv, &keydata);
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
                        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("setmlme AUTHORIZE %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]));
                        break;
                    case IEEE80211_MLME_UNAUTHORIZE:
                        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("setmlme UNAUTHORIZE %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]));
                        break;
                    case IEEE80211_MLME_DEAUTH:
                        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("setmlme DEAUTH %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]));
                        //remove_sta(ar, mlme.im_macaddr);
                        break;
                    case IEEE80211_MLME_DISASSOC:
                        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("setmlme DISASSOC %02X:%02X\n",
                            mlme.im_macaddr[4], mlme.im_macaddr[5]));
                        //remove_sta(ar, mlme.im_macaddr);
                        break;
                    default:
                        ret = 0;
                        goto ioctl_done;
                }

                wmi_ap_set_mlme(arPriv->arWmi, mlme.im_op, mlme.im_macaddr,
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

                status = wmi_setPmkid_cmd(arPriv->arWmi, req.pi_bssid, req.pi_pmkid,
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
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Can NOT send tx tcmd when target is asleep! \n"));
                    ret = -EOPNOTSUPP;
                    goto ioctl_done;
                }

                if(copy_from_user(&txCmd, userdata, sizeof(TCMD_CONT_TX))) {
                    ret = -EFAULT;
                    goto ioctl_done;
                } else {
                    wmi_test_cmd(arPriv->arWmi,(A_UINT8 *)&txCmd, sizeof(TCMD_CONT_TX));
                }
            }
            break;
        case AR6000_XIOCTL_TCMD_CONT_RX:
            {
                TCMD_CONT_RX rxCmd;

                if ((ar->tcmdPm == TCMD_PM_SLEEP) ||
                    (ar->tcmdPm == TCMD_PM_DEEPSLEEP))
                {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Can NOT send rx tcmd when target is asleep! \n"));
                    ret = -EOPNOTSUPP;
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
                         wmi_test_cmd(arPriv->arWmi,(A_UINT8 *)&rxCmd,
                                                sizeof(TCMD_CONT_RX));
                         tcmdRxFreq = rxCmd.u.para.freq;
                         break;
                    case TCMD_CONT_RX_REPORT:
                         ar6000_ioctl_tcmd_get_rx_report(dev, rq,
                         (A_UINT8 *)&rxCmd, sizeof(TCMD_CONT_RX));
                         break;
                    default:
                         AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unknown Cont Rx mode: %d\n",rxCmd.act));
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
                wmi_test_cmd(arPriv->arWmi, (A_UINT8*)&pmCmd, sizeof(TCMD_PM));
            }
            break;
          
        case AR6000_XIOCTL_TCMD_CMDS:
            {
                TC_CMDS cmdsCmd;
                if(copy_from_user(&cmdsCmd, userdata, sizeof(TC_CMDS))) {
                    ret = -EFAULT;
                    goto ioctl_done;
                }
                ar6000_ioctl_tcmd_cmd_resp(dev, rq, (A_UINT8 *)&cmdsCmd, sizeof(TC_CMDS));
#if 0
                wmi_test_cmd(arPriv->arWmi, (A_UINT8*)&cmdsCmd, sizeof(TC_CMDS));
#endif
            }
            break;

        case AR6000_XIOCTL_TCMD_SETREG:
            {
                TCMD_SET_REG setRegCmd;

                if(copy_from_user(&setRegCmd, userdata, sizeof(TCMD_SET_REG))) {
                    ret = -EFAULT;
                    goto ioctl_done;
                }
                wmi_test_cmd(arPriv->arWmi, (A_UINT8*)&setRegCmd, sizeof(TCMD_SET_REG));
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
            get_user(address, (unsigned int *)userdata);
            get_user(length, (unsigned int *)userdata + 1);
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
            get_user(address, (unsigned int *)userdata);
            get_user(length, (unsigned int *)userdata + 1);
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
            get_user(address, (unsigned int *)userdata);
            get_user(param, (unsigned int *)userdata + 1);
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Execute (address: 0x%x, param: %d)\n",
                             address, param));
            ret = BMIExecute(hifDevice, address, (A_UINT32*)&param);
            put_user(param, (unsigned int *)rq->ifr_data); /* return value */
            break;

        case AR6000_XIOCTL_BMI_SET_APP_START:
            get_user(address, (unsigned int *)userdata);
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Set App Start (address: 0x%x)\n", address));
            ret = BMISetAppStart(hifDevice, address);
            break;

        case AR6000_XIOCTL_BMI_READ_SOC_REGISTER:
            get_user(address, (unsigned int *)userdata);
            ret = BMIReadSOCRegister(hifDevice, address, (A_UINT32*)&param);
            put_user(param, (unsigned int *)rq->ifr_data); /* return value */
            break;

        case AR6000_XIOCTL_BMI_WRITE_SOC_REGISTER:
            get_user(address, (unsigned int *)userdata);
            get_user(param, (unsigned int *)userdata + 1);
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
                get_user(streamID, (unsigned int *)userdata);
                get_user(length, (unsigned int *)userdata + 1);
                buffer = (unsigned char*)rq->ifr_data + sizeof(length);
                ret = ar6000_htc_raw_read(ar, (HTC_RAW_STREAM_ID)streamID,
                                          (char*)buffer, length);
                put_user(ret, (unsigned int *)rq->ifr_data);
            } else {
                ret = A_ERROR;
            }
            break;

        case AR6000_XIOCTL_HTC_RAW_WRITE:
            if (arRawIfEnabled(ar)) {
                unsigned int streamID;
                get_user(streamID, (unsigned int *)userdata);
                get_user(length, (unsigned int *)userdata + 1);
                buffer = (unsigned char*)userdata + sizeof(streamID) + sizeof(length);
                ret = ar6000_htc_raw_write(ar, (HTC_RAW_STREAM_ID)streamID,
                                           (char*)buffer, length);
                put_user(ret, (unsigned int *)rq->ifr_data);
            } else {
                ret = A_ERROR;
            }
            break;
#endif /* HTC_RAW_INTERFACE */

        case AR6000_XIOCTL_BMI_LZ_STREAM_START:
            get_user(address, (unsigned int *)userdata);
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Start Compressed Stream (address: 0x%x)\n", address));
            ret = BMILZStreamStart(hifDevice, address);
            break;

        case AR6000_XIOCTL_BMI_LZ_DATA:
            get_user(length, (unsigned int *)userdata);
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
            get_user(period, (unsigned int *)userdata);
            get_user(nbins, (unsigned int *)userdata + 1);

            if (wmi_prof_cfg_cmd(arPriv->arWmi, period, nbins) != A_OK) {
                ret = -EIO;
            }

            break;
        }

        /* Start a profiling bucket/bin at the specified address */
        case AR6000_XIOCTL_PROF_ADDR_SET:
        {
            A_UINT32 addr;
            get_user(addr, (unsigned int *)userdata);

            if (wmi_prof_addr_set_cmd(arPriv->arWmi, addr) != A_OK) {
                ret = -EIO;
            }

            break;
        }

        /* START Target-side profiling */
        case AR6000_XIOCTL_PROF_START:
            wmi_prof_start_cmd(arPriv->arWmi);
            break;

        /* STOP Target-side profiling */
        case AR6000_XIOCTL_PROF_STOP:
            wmi_prof_stop_cmd(arPriv->arWmi);
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
            wait_event_interruptible(arPriv->arEvent, prof_count_available);
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
                if (wmi_powermode_cmd(arPriv->arWmi, pwrModeCmd.powerMode)
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
                if (wmi_ibsspmcaps_cmd(arPriv->arWmi, ibssPmCaps.power_saving, ibssPmCaps.ttl,
                    ibssPmCaps.atim_windows, ibssPmCaps.timeout_value) != A_OK)
                {
                    ret = -EIO;
                }
                AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
                arPriv->arSta.arIbssPsEnable = ibssPmCaps.power_saving;
                AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
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
                if (wmi_apps_cmd(arPriv->arWmi, apPsCmd.psType, apPsCmd.idle_time,
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
                if (wmi_pmparams_cmd(arPriv->arWmi, pmParams.idle_period,
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
            } else if (copy_from_user(&arSta->scParams, userdata,
                                      sizeof(arSta->scParams)))
            {
                ret = -EFAULT;
            } else {
                if (CAN_SCAN_IN_CONNECT(arSta->scParams.scanCtrlFlags)) {
                    arSta->arSkipScan = FALSE;
                } else {
                    arSta->arSkipScan = TRUE;
                }

                if (wmi_scanparams_cmd(arPriv->arWmi, arSta->scParams.fg_start_period,
                                       arSta->scParams.fg_end_period,
                                       arSta->scParams.bg_period,
                                       arSta->scParams.minact_chdwell_time,
                                       arSta->scParams.maxact_chdwell_time,
                                       arSta->scParams.pas_chdwell_time,
                                       arSta->scParams.shortScanRatio,
                                       arSta->scParams.scanCtrlFlags,
                                       arSta->scParams.max_dfsch_act_time,
                                       arSta->scParams.maxact_scan_per_ssid) != A_OK)
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
                    if (wmi_listeninterval_cmd(arPriv->arWmi, listenCmd.listenInterval, listenCmd.numBeacons) != A_OK) {
                        ret = -EIO;
                    } else {
                        AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
                        arSta->arListenIntervalT = listenCmd.listenInterval;
                        arSta->arListenIntervalB = listenCmd.numBeacons;
                        AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
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
                if (wmi_bmisstime_cmd(arPriv->arWmi, bmissCmd.bmissTime, bmissCmd.numBeacons) != A_OK) {
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
                if (wmi_bssfilter_cmd(arPriv->arWmi, filt.bssFilter, filt.ieMask)
                        != A_OK) {
                    ret = -EIO;
                } else {
                    arSta->arUserBssFilter = filt.bssFilter;
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
            ret = wmi_clr_rssi_snr(arPriv->arWmi);
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
                if (wmi_set_lpreamble_cmd(arPriv->arWmi, setLpreambleCmd.status,
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
                if(arPriv->arNetworkType == AP_NETWORK) {
                    arAp->arRTS = rtsCmd.threshold;
                }
                if (wmi_set_rts_cmd(arPriv->arWmi, rtsCmd.threshold)
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
            } else {
                get_user(cmd.ieType, userdata);
                if (cmd.ieType >= WMI_MAX_ASSOC_INFO_TYPE) {
                    ret = -EIO;
                } else {
                    get_user(cmd.bufferSize, userdata + 1);
                    if (cmd.bufferSize > WMI_MAX_ASSOC_INFO_LEN) {
                        ret = -EFAULT;
                        break;
                    }
                    if (copy_from_user(assocInfo, userdata + 2,
                                       cmd.bufferSize))
                    {
                        ret = -EFAULT;
                    } else {
                        if (wmi_associnfo_cmd(arPriv->arWmi, cmd.ieType,
                                                 cmd.bufferSize,
                                                 assocInfo) != A_OK)
                        {
                            ret = -EIO;
                        }
                    }
                }
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
            if (wmi_get_challenge_resp_cmd(arPriv->arWmi, cookie, APP_HB_CHALLENGE) != A_OK) {
                ret = -EIO;
                goto ioctl_done;
            }
            break;
        }
#ifdef USER_KEYS
        case AR6000_XIOCTL_USER_SETKEYS:
        {

            arSta->user_savedkeys_stat = USER_SAVEDKEYS_STAT_RUN;

            if (copy_from_user(&arSta->user_key_ctrl, userdata,
                               sizeof(arSta->user_key_ctrl)))
            {
                ret = -EFAULT;
                goto ioctl_done;
            }

            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ar6000 USER set key %x\n", arSta->user_key_ctrl));
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
            wait_event_interruptible(arPriv->arEvent, gpio_data_available);
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
                wait_event_interruptible(arPriv->arEvent, gpio_ack_received);
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
                wait_event_interruptible(arPriv->arEvent, gpio_data_available);
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
            wait_event_interruptible(arPriv->arEvent, gpio_intr_available);

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
            if (wmi_config_debug_module_cmd(arPriv->arWmi, config.mmask,
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

                A_MEMCPY(arSta->arReqBssid, adhocBssid.bssid, sizeof(arSta->arReqBssid));
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
                if (wmi_set_retry_limits_cmd(arPriv->arWmi, setRetryParams.frameType,
                                          setRetryParams.trafficClass,
                                          setRetryParams.maxRetries,
                                          setRetryParams.enableNotify) != A_OK)
                {
                    ret = -EIO;
                }
                AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
                arPriv->arMaxRetries = setRetryParams.maxRetries;
                AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
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
            } else if (wmi_set_adhoc_bconIntvl_cmd(arPriv->arWmi, bIntvlCmd.beaconInterval)
                        != A_OK)
            {
                ret = -EIO;
            }
            if(ret == 0) {
                arAp->ap_beacon_interval = bIntvlCmd.beaconInterval;
                arPriv->ap_profile_flag = 1; /* There is a change in profile */
            }
            break;
        }
        case IEEE80211_IOCTL_SETAUTHALG:
        {
            struct ieee80211req_authalg req;

           if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&req, userdata,
                       sizeof(struct ieee80211req_authalg)))
            {
                ret = -EFAULT;
            } else {
                if (req.auth_alg & AUTH_ALG_OPEN_SYSTEM) {
                    arPriv->arDot11AuthMode  |= OPEN_AUTH;
                    arPriv->arPairwiseCrypto  = NONE_CRYPT;
                    arPriv->arGroupCrypto     = NONE_CRYPT;
                }
                if (req.auth_alg & AUTH_ALG_SHARED_KEY) {
                    arPriv->arDot11AuthMode  |= SHARED_AUTH;
                    arPriv->arPairwiseCrypto  = WEP_CRYPT;
                    arPriv->arGroupCrypto     = WEP_CRYPT;
                    arPriv->arAuthMode        = WMI_NONE_AUTH;
                }
                if (req.auth_alg == AUTH_ALG_LEAP) {
                    arPriv->arDot11AuthMode   = LEAP_AUTH;
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
            get_user(state, (unsigned int *)userdata);
            if (ar6000_set_wlan_state(ar, state)!=A_OK) {
                ret = -EIO;
            }       
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
                            A_FREE(cmdp);
                            ret = -EFAULT;
                            goto ioctl_done;
                        }
                    } else {
                        cmdp = &setStartScanCmd;
                    }

                    if (wmi_startscan_cmd(arPriv->arWmi, cmdp->scanType,
                                          cmdp->forceFgScan,
                                          cmdp->isLegacy,
                                          cmdp->homeDwellTime,
                                          cmdp->forceScanInterval,
                                          cmdp->numChannels,
                                          cmdp->channelList) != A_OK)
                    {
                        ret = -EIO;
                    }
                    if (setStartScanCmd.numChannels > 1) {
                        A_FREE(cmdp);
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
                    returnStatus = wmi_set_fixrates_cmd(arPriv->arWmi, setFixRatesCmd.fixRateMask);
                    if (returnStatus == A_EINVAL) {
                        ret = -EINVAL;
                    } else if(returnStatus != A_OK) {
                        ret = -EIO;
                    } else {
                        arPriv->ap_profile_flag = 1; /* There is a change in profile */
                    }
                }
            break;
        }

        case AR6000_XIOCTL_WMI_GETFIXRATES:
        {
            WMI_FIX_RATES_CMD getFixRatesCmd;
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
                arPriv->arRateMask[0] = 0xFFFFFFFF;
                arPriv->arRateMask[1] = 0xFFFFFFFF;

                if (wmi_get_ratemask_cmd(arPriv->arWmi) != A_OK) {
                    up(&ar->arSem);
                    ret = -EIO;
                    goto ioctl_done;
                }

                wait_event_interruptible_timeout(arPriv->arEvent, (arPriv->arRateMask[0] != 0xFFFFFFFF) && 
                                             (arPriv->arRateMask[1] != 0xFFFFFFFF), wmitimeout * HZ);

                if (signal_pending(current)) {
                    ret = -EINTR;
                }

                if (!ret) {
                    getFixRatesCmd.fixRateMask[0] = arPriv->arRateMask[0];
                    getFixRatesCmd.fixRateMask[1] = arPriv->arRateMask[1];
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
                                      sizeof(setAuthMode))) {
                ret = -EFAULT;
            } else {
                if (wmi_set_authmode_cmd(arPriv->arWmi, setAuthMode.mode) != A_OK)
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
                                      sizeof(setReassocMode))) {
                ret = -EFAULT;
            } else {
                if (wmi_set_reassocmode_cmd(arPriv->arWmi, setReassocMode.mode) != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_DIAG_READ:
        {
            A_UINT32 addr, data;
            get_user(addr, (unsigned int *)userdata);
            addr = TARG_VTOP(ar->arTargetType, addr);
            if (ar6000_ReadRegDiag(ar->arHifDevice, &addr, &data) != A_OK) {
                ret = -EIO;
            }
            put_user(data, (unsigned int *)userdata + 1);
            break;
        }
        case AR6000_XIOCTL_DIAG_WRITE:
        {
            A_UINT32 addr, data;
            get_user(addr, (unsigned int *)userdata);
            get_user(data, (unsigned int *)userdata + 1);
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
                 if (wmi_set_keepalive_cmd(arPriv->arWmi, setKeepAlive.keepaliveInterval) != A_OK) {
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
                 if (wmi_set_params_cmd(arPriv->arWmi, cmd.opcode, cmd.length, cmd.buffer) != A_OK) {
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
                 if (wmi_set_mcast_filter_cmd(arPriv->arWmi, &cmd.multicast_mac[0]) != A_OK) {
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
                 if (wmi_del_mcast_filter_cmd(arPriv->arWmi, &cmd.multicast_mac[0]) != A_OK) {
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
                 if (wmi_mcast_filter_cmd(arPriv->arWmi, cmd.enable)  != A_OK) {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_WMI_GET_KEEPALIVE:
        {
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
            getKeepAlive.keepaliveInterval = wmi_get_keepalive_cmd(arPriv->arWmi);
            arSta->arKeepaliveConfigured = 0xFF;
            if (wmi_get_keepalive_configured(arPriv->arWmi) != A_OK){
                up(&ar->arSem);
                ret = -EIO;
                goto ioctl_done;
            }
            wait_event_interruptible_timeout(arPriv->arEvent, arSta->arKeepaliveConfigured != 0xFF, wmitimeout * HZ);
            if (signal_pending(current)) {
                ret = -EINTR;
            }

            if (!ret) {
                getKeepAlive.configured = arSta->arKeepaliveConfigured;
            }
            if (copy_to_user(userdata, &getKeepAlive, sizeof(getKeepAlive))) {
               ret = -EFAULT;
            }
            }
            up(&ar->arSem);
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
            get_user(fType, (A_UINT32 *)userdata);
            appIEcmd.mgmtFrmType = fType;
            if (appIEcmd.mgmtFrmType >= IEEE80211_APPIE_NUM_OF_FRAME) {
                ret = -EIO;
            } else {
                get_user(ieLen, (A_UINT32 *)(userdata + 4));
                appIEcmd.ieLen = ieLen;
                AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("WPSIE: Type-%d, Len-%d\n",appIEcmd.mgmtFrmType, appIEcmd.ieLen));
                if (appIEcmd.ieLen > IEEE80211_APPIE_FRAME_MAX_LEN) {
                    ret = -EIO;
                    break;
                }
                if (copy_from_user(appIeInfo, userdata + 8, appIEcmd.ieLen)) {
                    ret = -EFAULT;
                } else {
                    if (wmi_set_appie_cmd(arPriv->arWmi, appIEcmd.mgmtFrmType,
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
            if (wmi_bssfilter_cmd(arPriv->arWmi, cmd.bssFilter, 0) != A_OK) {
                ret = -EIO;
            } else {
                arSta->arUserBssFilter = cmd.bssFilter;
            }

            AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
            arSta->arMgmtFilter = filterType;
            AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
            break;
        }
        case AR6000_XIOCTL_WMI_SET_WSC_STATUS:
        {
            A_UINT32    wsc_status;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                goto ioctl_done;
            } else if (copy_from_user(&wsc_status, userdata, sizeof(A_UINT32))) {
                ret = -EFAULT;
                goto ioctl_done;
            }
            if (wmi_set_wsc_status_cmd(arPriv->arWmi, wsc_status) != A_OK) {
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

            get_user(ROM_addr, (A_UINT32 *)userdata);
            get_user(RAM_addr, (A_UINT32 *)userdata + 1);
            get_user(nbytes, (A_UINT32 *)userdata + 2);
            get_user(do_activate, (A_UINT32 *)userdata + 3);
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Install rompatch from ROM: 0x%x to RAM: 0x%x  length: %d\n",
                             ROM_addr, RAM_addr, nbytes));
            ret = BMIrompatchInstall(hifDevice, ROM_addr, RAM_addr,
                                        nbytes, do_activate, &rompatch_id);
            if (ret == A_OK) {
                put_user(rompatch_id, (unsigned int *)rq->ifr_data); /* return value */
            }
            break;
        }

        case AR6000_XIOCTL_BMI_ROMPATCH_UNINSTALL:
        {
            A_UINT32 rompatch_id;

            get_user(rompatch_id, (A_UINT32 *)userdata);
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("UNinstall rompatch_id %d\n", rompatch_id));
            ret = BMIrompatchUninstall(hifDevice, rompatch_id);
            break;
        }

        case AR6000_XIOCTL_BMI_ROMPATCH_ACTIVATE:
        case AR6000_XIOCTL_BMI_ROMPATCH_DEACTIVATE:
        {
            A_UINT32 rompatch_count;

            get_user(rompatch_count, (A_UINT32 *)userdata);
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
                                      sizeof(setIP))) {
                ret = -EFAULT;
            } else {
                if (wmi_set_ip_cmd(arPriv->arWmi,
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
                                      sizeof(setHostSleepMode))) {
                ret = -EFAULT;
            } else {
                if (wmi_set_host_sleep_mode_cmd(arPriv->arWmi,
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
                                      sizeof(setWowMode))) {
                ret = -EFAULT;
            } else {
                if (wmi_set_wow_mode_cmd(arPriv->arWmi,
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
                                      sizeof(getWowList))) {
                ret = -EFAULT;
            } else {
                if (wmi_get_wow_list_cmd(arPriv->arWmi,
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
                if (wmi_add_wow_pattern_cmd(arPriv->arWmi,
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
                                      sizeof(delWowPattern))) {
                ret = -EFAULT;
            } else {
                if (wmi_del_wow_pattern_cmd(arPriv->arWmi,
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
                ar6000_indicate_tx_activity(arPriv,
                                            (A_UINT8)data.StreamID,
                                            data.Active ? TRUE : FALSE);
            }
            break;
        case AR6000_XIOCTL_WMI_SET_CONNECT_CTRL_FLAGS:
            {
                A_UINT32 connectCtrlFlags;
                if (ar->arWmiReady == FALSE) {
                    ret = -EIO;
                } else if (copy_from_user(&connectCtrlFlags, userdata,
                                      sizeof(connectCtrlFlags)))
                {
                    ret = -EFAULT;
                } else {
                    arSta->arConnectCtrlFlags = connectCtrlFlags;
                }
            }
            break;
        case AR6000_XIOCTL_WMI_SET_AKMP_PARAMS:
            {
                WMI_SET_AKMP_PARAMS_CMD  akmpParams;
            
                if (ar->arWmiReady == FALSE) {
                    ret = -EIO;
                } else if (copy_from_user(&akmpParams, userdata,
                                      sizeof(WMI_SET_AKMP_PARAMS_CMD)))
                {
                    ret = -EFAULT;
                } else {
                    if (wmi_set_akmp_params_cmd(arPriv->arWmi, &akmpParams) != A_OK) {
                        ret = -EIO;
                    }
                }
                break;
            }
        case AR6000_XIOCTL_WMI_SET_PMKID_LIST:
            {
                WMI_SET_PMKID_LIST_CMD   pmkidInfo;
                if (ar->arWmiReady == FALSE) {
                   ret = -EIO;
                   break;
                } 
                if (copy_from_user(&pmkidInfo.numPMKID, userdata,
                                      sizeof(pmkidInfo.numPMKID))) {
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
                if (wmi_set_pmkid_list_cmd(arPriv->arWmi, &pmkidInfo) != A_OK) {
                    ret = -EIO;
                }
                break;
            }
        case AR6000_XIOCTL_WMI_GET_PMKID_LIST:
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (wmi_get_pmkid_list_cmd(arPriv->arWmi) != A_OK) {
                    ret = -EIO;
                }
            break;
        case AR6000_XIOCTL_WMI_ABORT_SCAN:
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            }
            ret = wmi_abort_scan_cmd(arPriv->arWmi);
            break;
        case AR6000_XIOCTL_AP_HIDDEN_SSID:
        {
            A_UINT8    hidden_ssid;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&hidden_ssid, userdata, sizeof(hidden_ssid))) {
                ret = -EFAULT;
            } else {
                wmi_ap_set_hidden_ssid(arPriv->arWmi, hidden_ssid);
                arAp->ap_hidden_ssid = hidden_ssid;
                arPriv->ap_profile_flag = 1; /* There is a change in profile */
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_STA_LIST:
        {
            WMI_SET_HT_CAP_CMD htCap;
            
            htCap.band = A_BAND_24GHZ;
            if(arPriv->phymode == WMI_11A_MODE) {
                htCap.band = A_BAND_5GHZ;
            }            
            wmi_get_ht_cap_cmd(arPriv->arWmi, &htCap);
            
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else {
                A_UINT8 i, j=0;
                ap_get_sta_t temp;
                A_MEMZERO(&temp, sizeof(temp));
                for(i=0;i<NUM_CONN;i++) {
                    if(ar->connTbl[i].arPriv == arPriv) {
                        A_MEMCPY(temp.sta[j].mac, ar->connTbl[i].mac, ATH_MAC_LEN);
                        temp.sta[j].aid     = ar->connTbl[i].aid;
                        temp.sta[j].keymgmt = ar->connTbl[i].keymgmt;
                        temp.sta[j].ucipher = ar->connTbl[i].ucipher;
                        temp.sta[j].auth    = ar->connTbl[i].auth;
                        temp.sta[j].wmode   = ar->connTbl[i].wmode;
                        if(htCap.enable == 2) {
                            /* Set MSB to indicate 11n-only mode */
                            temp.sta[j].wmode |= 0x80;
                        }
                        j++;
                    }
                }
                if(copy_to_user((ap_get_sta_t *)rq->ifr_data, &temp, sizeof(temp))) {
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
            } else {
                ret = ar6000_ap_set_num_sta(ar, arPriv, num_sta);
            }
            break;
        }
        case AR6000_XIOCTL_AP_SET_DFS:
        {
#ifdef ATH_SUPPORT_DFS
            A_UINT8    enable;
            if (copy_from_user(&enable, userdata, sizeof(enable))) {
                ret = -EFAULT;
            } else {
                wmi_ap_set_dfs(arPriv->arWmi, enable);
            }
#else
            ret = -EIO;
#endif
            break;
        }
 
        case AR6000_XIOCTL_AP_SET_ACL_POLICY:
        {
            A_UINT8    policy;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&policy, userdata, sizeof(policy))) {
                ret = -EFAULT;
            } else {
                if(!(policy & AP_ACL_RETAIN_LIST_MASK)) {
                    /* clear ACL list */
                    memset(&arAp->g_acl,0,sizeof(WMI_AP_ACL));
                }
                arAp->g_acl.policy = policy;
                wmi_ap_set_acl_policy(arPriv->arWmi, policy);
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
                if(acl_add_del_mac(&arAp->g_acl, &acl)) {
                    wmi_ap_acl_mac_list(arPriv->arWmi, &acl);
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ACL list error\n"));
                    ret = -EIO;
                }
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_ACL_LIST:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if(copy_to_user((WMI_AP_ACL *)rq->ifr_data, &arAp->g_acl,
                                 sizeof(WMI_AP_ACL))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_COMMIT_CONFIG:
        {
            ret = ar6000_ap_mode_profile_commit(arPriv);
            break;
        }
        case IEEE80211_IOCTL_GETWPAIE:
        {
            struct ieee80211req_wpaie wpaie;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&wpaie, userdata, sizeof(wpaie))) {
                ret = -EFAULT;
            } else if (ar6000_ap_mode_get_wpa_ie(arPriv, &wpaie)) {
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
                wmi_ap_conn_inact_time(arPriv->arWmi, period);
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
                wmi_ap_bgscan_time(arPriv->arWmi, bgscan.period_min, bgscan.dwell_ms);
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
                    arAp->ap_dtim_period = d.dtim;
                    wmi_ap_set_dtim(arPriv->arWmi, d.dtim);
                    arPriv->ap_profile_flag = 1; /* There is a change in profile */
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("DTIM out of range. Valid range is [1-10]\n"));
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
            ret = wmi_set_target_event_report_cmd(arPriv->arWmi, &evtCfgCmd);
            break;
        }
        case AR6000_XIOCTL_AP_CTRL_BSS_COMM:
        {
            A_UINT8    intra=0;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&intra, userdata, sizeof(intra))) {
                ret = -EFAULT;
            } else {
                if(intra & 0x80) { /* interbss */
                    ar->inter_bss = ( (intra & 0xF) ? 1 : 0 );
                } else {
                    arAp->intra_bss = ( intra ? 1 : 0 );
                }
            }
            /* If P2P is enabled on this device, also indicate intra_bss setting to the firmware
             * so that it can be reflected in the Group Capability bit of the p2p-go.
             */
#ifdef P2P
            {
                NETWORK_SUBTYPE networkSubType = arPriv->arNetworkSubType;

                if (networkSubType == SUBTYPE_P2PDEV ||
                    networkSubType == SUBTYPE_P2PCLIENT ||
                    networkSubType == SUBTYPE_P2PGO) {

                    WMI_P2P_SET_CMD set_p2p_config;
                    A_MEMZERO(&set_p2p_config, sizeof(WMI_P2P_SET_CMD));

                    set_p2p_config.config_id = WMI_P2P_CONFID_INTRA_BSS; 
                    set_p2p_config.val.intra_bss.flag = intra;

                    wmi_p2p_set_cmd(arPriv->arWmi, &set_p2p_config);
                }
            }
#endif /* P2P */
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

            aggr_dump_stats(ar->connTbl[0].conn_aggr, &copy_of_pkt_log);
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
                wmi_setup_aggr_cmd(arPriv->arWmi, cmd.tid);
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
                wmi_delete_aggr_cmd(arPriv->arWmi, cmd.tid, cmd.is_sender_initiator);
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
                wmi_allow_aggr_cmd(arPriv->arWmi, cmd.tx_allow_aggr, cmd.rx_allow_aggr);
            }
        }
        break;

        case AR6000_XIOCTL_SET_HT_CAP:
        {
            WMI_SET_HT_CAP_CMD htCap;
            
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&htCap, userdata, sizeof(htCap))) {
                ret = -EFAULT;
            } else if (wmi_set_ht_cap_cmd(arPriv->arWmi, &htCap) != A_OK) {
                    ret = -EIO;
            }
            break;
        }

        case AR6000_XIOCTL_GET_HT_CAP:
        {
            WMI_SET_HT_CAP_CMD htCap;
            
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&htCap, userdata, sizeof(htCap))) {
                ret = -EFAULT;
            } else if (wmi_get_ht_cap_cmd(arPriv->arWmi, &htCap) != A_OK) {
                ret = -EIO;
            } else if(copy_to_user((WMI_SET_HT_CAP_CMD *)rq->ifr_data, 
                            &htCap, sizeof(htCap))) {
                ret = -EFAULT;
            }
            break;
        }

        case AR6000_XIOCTL_SET_HT_OP:
        {
            WMI_SET_HT_OP_CMD htOp;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&htOp, userdata,
                                      sizeof(htOp))){
                 ret = -EFAULT;
             } else {

                if (wmi_set_ht_op_cmd(arPriv->arWmi, htOp.sta_chan_width) != A_OK)
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
                if (wmi_data_hdr_add(arPriv->arWmi, osbuf, DATA_MSGTYPE, 0, WMI_DATA_HDR_DATA_TYPE_ACL,0,NULL) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("XIOCTL_ACL_DATA - wmi_data_hdr_add failed\n"));
                } else {
                    /* Send data buffer over HTC */
                    ar6000_acl_data_tx(osbuf, arPriv);
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
                if (wmi_send_hci_cmd(arPriv->arWmi, cmd->buf, cmd->cmd_buf_sz) != A_OK) {
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
                    if ( wmi_set_wlan_conn_precedence_cmd(arPriv->arWmi, cmd.precedence) != A_OK) {
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
                                      sizeof(masks))) {
                 ret = -EFAULT;
             } else {

                if (wmi_set_tx_select_rates_cmd(arPriv->arWmi, masks.rateMasks) != A_OK)
                {
                     ret = -EIO;
               }
             }
             break;
        }
        case AR6000_XIOCTL_AP_GET_HIDDEN_SSID:
        {
            WMI_AP_HIDDEN_SSID_CMD ssid;
            ssid.hidden_ssid = arAp->ap_hidden_ssid;

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
            A_MEMCPY(cty.countryCode, arAp->ap_country_code, 3);
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
				    &arPriv->phymode, sizeof(A_UINT8))) {
                    ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_DTIM:
        {
            WMI_AP_SET_DTIM_CMD dtim;
            dtim.dtim = arAp->ap_dtim_period;

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
            bi.beaconInterval = arAp->ap_beacon_interval;

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
            rts.threshold = arAp->arRTS;
         
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
                wmi_ap_set_rateset(arPriv->arWmi, rate.rateset);
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
            ar6000_send_event_to_app(arPriv, WMI_REPORT_SLEEP_STATE_EVENTID, (A_UINT8*)&wmiSleepEvent,
                                     sizeof(WMI_REPORT_SLEEP_STATE_EVENTID));
            break;
        }
#ifdef P2P
        case AR6000_XIOCTL_WMI_P2P_DISCOVER:
        {
            WMI_BSS_FILTER_CMD filt;

            /*Issue the WMI_FIND CMD*/
            WMI_P2P_FIND_CMD find_param;
            
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
                break;
            }
            A_MEMZERO(&filt, sizeof(WMI_BSS_FILTER_CMD));
            /*Set BSS filter to ALL*/
            filt.bssFilter = ALL_BSS_FILTER;

            if (wmi_bssfilter_cmd(arPriv->arWmi, filt.bssFilter, filt.ieMask)
                    != A_OK) {
                ret = -EIO;
            } else {
                arSta->arUserBssFilter = filt.bssFilter;
                if (copy_from_user(&find_param, userdata, sizeof(WMI_P2P_FIND_CMD))) {
                    ret = -EFAULT;
                } else {
                    p2p_clear_peers_reported_flag(A_WMI_GET_P2P_CTX(arPriv));
                    wmi_p2p_discover(arPriv->arWmi, &find_param);
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_STOP_FIND:
        {
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else {
                wmi_p2p_stop_find(arPriv->arWmi);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_CANCEL:
        {
            wmi_p2p_cancel(arPriv->arWmi);
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_LISTEN:
        {
            A_UINT32 timeout;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&timeout, userdata, sizeof(timeout))) {
                ret = -EFAULT;
            } else {
                wmi_p2p_listen(arPriv->arWmi, timeout);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_GO_NEG:
        {
            /*Issue the WMI_GO_NEG CMD*/
            WMI_P2P_GO_NEG_START_CMD go_param;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&go_param, userdata, sizeof(WMI_P2P_GO_NEG_START_CMD))) {
                ret = -EFAULT;
            } else { 
                if (p2p_go_neg_start(A_WMI_GET_P2P_CTX(arPriv), &go_param)
                        != A_OK) {
                    ret = -EFAULT;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_AUTH_GO_NEG:
        {
            WMI_P2P_GO_NEG_START_CMD go_neg_auth_param;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&go_neg_auth_param, userdata, sizeof(WMI_P2P_GO_NEG_START_CMD))) {
                ret = -EFAULT;
            } else { 
                if (p2p_auth_go_neg(A_WMI_GET_P2P_CTX(arPriv),
                            &go_neg_auth_param) != A_OK) {
                    ret = -EFAULT;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_REJECT:
        {
            A_UINT8 p2p_reject_peer[IEEE80211_ADDR_LEN];

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(p2p_reject_peer, userdata,
                            IEEE80211_ADDR_LEN)) {
                ret = -EFAULT;
            } else { 
                if (p2p_peer_reject(A_WMI_GET_P2P_CTX(arPriv),
                         p2p_reject_peer) != A_OK) {
                ret = -EFAULT;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_CONFIG:
        {
            WMI_P2P_SET_CONFIG_CMD set_p2p_config;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&set_p2p_config, userdata, sizeof(WMI_P2P_SET_CONFIG_CMD))) {
                ret = -EFAULT;
            } else { 
                wmi_p2p_set_config(arPriv->arWmi, &set_p2p_config);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_WPS_CONFIG:
        {
            WMI_WPS_SET_CONFIG_CMD set_wps_config;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&set_wps_config, userdata, sizeof(WMI_WPS_SET_CONFIG_CMD))) {
                ret = -EFAULT;
            } else {
                wmi_wps_set_config(arPriv->arWmi, &set_wps_config);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_FINDNODE:
        {
            A_UINT8 macaddr[AR6000_ETH_ADDR_LEN];
            bss_t *ni;
            if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
            } else if (copy_from_user(macaddr, userdata,AR6000_ETH_ADDR_LEN)) {
                 ret = -EFAULT;
            } else {
                 ni = wmi_find_node(arPriv->arWmi, macaddr);
                 if (ni) {
                     if(copy_to_user((A_UINT16 *)rq->ifr_data, 
                                    &ni->ni_cie.ie_chan, sizeof(A_UINT16))) {
                         ret = -EFAULT;
                     }
                 }
                 else {
                       ret = -EFAULT;
                 }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_GRP_INIT:
        {
            WMI_P2P_GRP_INIT_CMD p2p_grp_init_cmd;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&p2p_grp_init_cmd, userdata,
                                sizeof(WMI_P2P_GRP_INIT_CMD))) {
                ret = -EFAULT;
            } else {
                wmi_p2p_grp_init_cmd(arPriv->arWmi, &p2p_grp_init_cmd);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_GRP_FORMATION_DONE:
        {
            WMI_P2P_GRP_FORMATION_DONE_CMD p2p_grp_done_cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&p2p_grp_done_cmd, userdata,
                                sizeof(WMI_P2P_GRP_FORMATION_DONE_CMD))) {
                ret = -EFAULT;
            } else {
                wmi_p2p_grp_done_cmd(arPriv->arWmi, &p2p_grp_done_cmd);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_INVITE:
        {
            WMI_P2P_INVITE_CMD p2p_invite_param;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&p2p_invite_param, userdata,
                                 sizeof(WMI_P2P_INVITE_CMD))) {
                ret = -EFAULT;
            } else {
                if (p2p_invite_cmd(A_WMI_GET_P2P_CTX(arPriv), &p2p_invite_param)
                    != A_OK) {
                    ret = -EFAULT;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_PROV_DISC:
        {
            A_UINT8 peer[IEEE80211_ADDR_LEN];
            A_UINT16 wps_method;
            A_UINT8 buf[8];

            A_MEMZERO(peer, IEEE80211_ADDR_LEN);

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(buf, userdata, 8)) {
                ret = -EFAULT;
            } else { 
                A_MEMCPY(peer, buf, IEEE80211_ADDR_LEN);
                wps_method = (*(A_UINT16 *)(&buf[6]));

                if (p2p_prov_disc_req(A_WMI_GET_P2P_CTX(arPriv), peer, wps_method) != A_OK) {
                    ret = -EFAULT;
                }
            }

            break;
        }
        case AR6000_XIOCTL_WMI_P2P_GET_IF_ADDR:
        {
            A_UINT8 buf[12];
            const A_UINT8 zero_mac[] = {0,0,0,0,0,0};

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(buf, userdata, 12)) {
                ret = -EFAULT;
            } else {
                if (p2p_get_ifaddr(A_WMI_GET_P2P_CTX(arPriv),
                        buf) == A_OK) {
                    if(copy_to_user((A_UINT8 *)rq->ifr_data, 
                                buf+6, IEEE80211_ADDR_LEN)) {
                        ret = -EFAULT;
                    }
                } else {
                    if(copy_to_user((A_UINT8 *)rq->ifr_data, 
                                zero_mac, IEEE80211_ADDR_LEN)) {
                        ret = -EFAULT;
                    }
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_GET_DEV_ADDR:
        {
            A_UINT8 buf[12];
            const A_UINT8 zero_mac[] = {0,0,0,0,0,0};

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(buf, userdata, 12)) {
                ret = -EFAULT;
            } else {
                if (p2p_get_devaddr(A_WMI_GET_P2P_CTX(arPriv),
                        buf) == A_OK) {
                    if(copy_to_user((A_UINT8 *)rq->ifr_data, 
                                buf+6, IEEE80211_ADDR_LEN)) {
                        ret = -EFAULT;
                    }
                } else {
                    if(copy_to_user((A_UINT8 *)rq->ifr_data, 
                                zero_mac, IEEE80211_ADDR_LEN)) {
                        ret = -EFAULT;
                    }
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_SET:
        {
            WMI_P2P_SET_CMD set_p2p_config;
            A_MEMZERO(&set_p2p_config, sizeof(WMI_P2P_SET_CMD));

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&set_p2p_config, userdata, sizeof(WMI_P2P_SET_CMD))) {
                ret = -EFAULT;
            } else { 
                wmi_p2p_set_cmd(arPriv->arWmi, &set_p2p_config);
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_PEER:
        {
            A_UINT8 buf[12];
            const A_UINT8 zero_mac[] = {0,0,0,0,0,0};

            A_UINT8 peer_info_buf[1000];
            A_UINT32 peer_info_buf_used;

            int first_element;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(buf, userdata, 12)) {
                ret = -EFAULT;
            } else {
                /*
                 * Check the "next" value set in driver_ar6003.c of the supplicant.
                 * This determines whether we have a "P2P_PEER FIRST" (if = 1) or
                 * "P2P_PEER NEXT-<addr>" (if = 2) command, or just a plain p2p_peer <addr>
                 * command (if = 0)
                 */
                if (buf[6] != 0) {
                    if (buf[6] == 1) {
                        first_element = 1;
                    } else {
                        first_element = 0;
                    }
                    peer_info_buf_used = p2p_get_next_addr(A_WMI_GET_P2P_CTX(arPriv), buf, peer_info_buf, sizeof(peer_info_buf), first_element);
                    if (peer_info_buf_used == 0) {
                        ret = -ENODEV;
		    }
                    *((A_UINT32 *)rq->ifr_data) = peer_info_buf_used;
                    if(copy_to_user(((A_UINT32 *)(rq->ifr_data)+1),
                                peer_info_buf, peer_info_buf_used)){
                        ret = -EFAULT;
                    }
                } else {
                    if (p2p_peer(A_WMI_GET_P2P_CTX(arPriv),
                            buf, *(buf+6)) == A_OK) {
                        peer_info_buf_used = p2p_get_peer_info(A_WMI_GET_P2P_CTX(arPriv), buf,
                                                             peer_info_buf, sizeof(peer_info_buf));
                        *((A_UINT32 *)rq->ifr_data) = peer_info_buf_used;
                        if(copy_to_user(((A_UINT32 *)(rq->ifr_data)+1),
                                    peer_info_buf, peer_info_buf_used)) {
                            ret = -EFAULT;
                        }
                    } else {
                        if(copy_to_user((A_UINT16 *)rq->ifr_data, 
                                    zero_mac, IEEE80211_ADDR_LEN)) {
                            ret = -EFAULT;
                        }
                    }
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_FLUSH:
        {
            p2p_free_all_devices(A_WMI_GET_P2P_CTX(arPriv));
            p2p_free_all_sd_queries(A_WMI_GET_P2P_CTX(arPriv));
            break;
        }
        case AR6000_XIOCTL_WMI_GET_GO_PARAMS:
        {
            A_UINT8 go_dev_addr[AR6000_ETH_ADDR_LEN];
            struct {
                A_UINT16 oper_freq;
                A_UINT8 ssid[WMI_MAX_SSID_LEN];
                A_UINT8 ssid_len;
            } go_params;

            A_MEMZERO(&go_params, sizeof(go_params));

            if (ar->arWmiReady == FALSE) {
                 ret = -EIO;
            } else if (copy_from_user(go_dev_addr,
                         userdata, AR6000_ETH_ADDR_LEN)) {
                 ret = -EFAULT;
            } else {
                 if (wmi_p2p_get_go_params(A_WMI_GET_P2P_CTX(arPriv),
                     go_dev_addr, &go_params.oper_freq, go_params.ssid,
                          &go_params.ssid_len) == A_OK) {
                     if(copy_to_user((A_UINT16 *)rq->ifr_data, 
                                    &go_params, sizeof(go_params))) {
                         ret = -EFAULT;
                     }
                 } else {
                       ret = -EFAULT;
                 }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_AUTH_INVITE:
        {
            A_UINT8 auth_peer[IEEE80211_ADDR_LEN]={0,0,0,0,0,0};

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(auth_peer, userdata,IEEE80211_ADDR_LEN)) {
                ret = -EFAULT;
            } else { 
                if (p2p_auth_invite(A_WMI_GET_P2P_CTX(arPriv),
                            auth_peer) != A_OK) {
                    ret = -EFAULT;
                }
            }
            break;
        }
        case AR6000_XIOCTL_WMI_P2P_SDPD_TX_CMD:
        {
            WMI_P2P_SDPD_TX_CMD sdpd_tx_cmd;
            A_UINT32 qid = 0;

            A_MEMZERO(&sdpd_tx_cmd, sizeof(WMI_P2P_SDPD_TX_CMD));

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&sdpd_tx_cmd, userdata,
                             sizeof(WMI_P2P_SDPD_TX_CMD))) {
                ret = -EFAULT;
            } else {
                if (p2p_sdpd_tx_cmd(A_WMI_GET_P2P_CTX(arPriv),
                        &sdpd_tx_cmd, &qid) !=
                        A_OK) {
                    ret = -EFAULT;
                } else {
                    if(copy_to_user((A_UINT8 *)rq->ifr_data, 
                                (void *)&qid, sizeof(A_UINT32))) {
                        ret = -EFAULT;
                    }
                }
            }
            break;
        }
        case AR6000_XIOTCL_WMI_P2P_SD_CANCEL_REQUEST:
        {
            A_UINT32 qid;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&qid, userdata, sizeof(qid))) {
                ret = -EFAULT;
            } else if (p2p_sd_cancel_request(A_WMI_GET_P2P_CTX(arPriv),qid) !=
                        A_OK) {
                ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_WMI_GET_P2P_IE:
        {
            A_UINT8 buf[12];
            const A_UINT8 zero_mac[] = {0,0,0,0,0,0};
            A_UINT8 * p2p_buf = NULL;
            A_UINT8  p2p_buf_len = 0;

            
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(buf, userdata, 12)) {
                ret = -EFAULT;
            } else {

                if (p2p_peer(A_WMI_GET_P2P_CTX(arPriv),
                        buf, *(buf+6)) == A_OK) {
             
                     p2p_get_device_p2p_buf(A_WMI_GET_P2P_CTX(arPriv),buf, &p2p_buf, &p2p_buf_len);

                    if(p2p_buf) {
                        *((A_UINT8 *)rq->ifr_data) = p2p_buf_len;

                        if(copy_to_user(((A_UINT8 *)(rq->ifr_data)+1),
                                        p2p_buf, p2p_buf_len)) {
                            ret = -EFAULT;
                        }
                    }
                } else {
                    if(copy_to_user((A_UINT16 *)rq->ifr_data, 
                                zero_mac, IEEE80211_ADDR_LEN)) {
                        ret = -EFAULT;
                    }
                }
            }
            break;
        }
#endif /* P2P */

#ifdef CONFIG_PM
        case AR6000_XIOCTL_SET_BT_HW_POWER_STATE:
        {
            unsigned int state;
            get_user(state, (unsigned int *)userdata);
            if (ar6000_set_bt_hw_state(ar, state)!=A_OK) {
                ret = -EIO;
            }       
            break;
        }
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
                    if (wmi_SGI_cmd(arPriv->arWmi, SGICmd.sgiMask, SGICmd.sgiPERThreshold) != A_OK) {
                        ret = -EIO;
                    }

            }
            break;
        }

        case AR6000_XIOCTL_WMI_SET_PASSPHRASE:
        {
            ret = ar6000_xioctl_set_passphrase_cmd(dev, userdata);
            break;
        }

        case AR6000_XIOCTL_WMI_SET_EXCESS_TX_RETRY_THRES:
        {
            ret = ar6000_xioctl_set_excess_tx_retry_thres_cmd(dev, userdata);
            break;
        }

        case AR6000_XIOCTL_WMI_ENABLE_WAC_PARAM:
        {
            WMI_WAC_ENABLE_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd)))
            {
                ret = -EFAULT;
            } else {
                if ( cmd.enable & 0x80 ) {
                    cmd.enable &= ~0x80;
                    ar6000_send_generic_event_to_app(arPriv, WMI_ENABLE_WAC_CMDID, 
                                                    (A_UINT8*)&cmd, sizeof(WMI_WAC_ENABLE_CMD));
                }
                else {
                    if (wmi_wac_enable_cmd(arPriv->arWmi, &cmd)
                           != A_OK)
                    {
                        ret = -EIO;
                    }
                }
            }
            break;
        }

        case AR6000_XIOCTL_WAC_SCAN_REPLY:
        {
            WMI_WAC_SCAN_REPLY_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd)))
            {
                ret = -EFAULT;
            } else {
                if (wmi_wac_scan_reply_cmd(arPriv->arWmi, cmd.cmdid)
                       != A_OK)
                {
                    ret = -EIO;
                }
            }
            break;
        }

        case AR6000_XIOCTL_WMI_WAC_CTRL_REQ:
        {
            WMI_WAC_CTRL_REQ_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd)))
            {
                ret = -EFAULT;
            } else {
                if ( WAC_SET == cmd.req ) {
                    if (wmi_wac_ctrl_req_cmd(arPriv->arWmi, &cmd)
                           != A_OK)
                    {
                        ret = -EIO;
                    }
                }
                else if ( WAC_GET == cmd.req ) {
                    ret = ar6000_xioctl_wac_ctrl_req_get_cmd(dev, userdata, rq);
                }
            }
            break;
        }

        case AR6000_XIOCTL_WMI_SET_WPA_OFFLOAD_STATE:
        {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
            A_UINT8 wpaOffloadState = 0;

            if (copy_from_user(&wpaOffloadState, userdata, sizeof(A_UINT8))) {
                ret = -EFAULT;
            } else {
                arSta->wpaOffloadEnabled = (wpaOffloadState) ? TRUE : FALSE;
            }
#else
            ret = -EOPNOTSUPP;
#endif /* LINUX_VERSION_CODE >= 2.6.27 */
            break;
        }

        case AR6000_XIOCTL_BMI_NVRAM_PROCESS:
        {
            A_UCHAR seg_name[BMI_NVRAM_SEG_NAME_SZ+1];
            A_UINT32 rv = 0;

            if (copy_from_user(seg_name, userdata, sizeof(seg_name))) {
                ret = -EFAULT;
                break;
            }

            seg_name[BMI_NVRAM_SEG_NAME_SZ] = '\0';
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Process NVRAM segment: %s\n", seg_name));

            if (BMInvramProcess(hifDevice, seg_name, &rv) != A_OK) {
                ret = -EIO;
            }
            put_user(rv, (unsigned int *)rq->ifr_data); /* return value */

            break;
        }

        case AR6000_XIOCTL_AP_ACS_DISABLE_HI_CHANNELS:
        {
            A_UINT32    acs;
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&acs, userdata, sizeof(acs))) {
                ret = -EFAULT;
            } else {
                ar->arAcsDisableHiChannel = acs;
            }
            break;
        }

        case AR6000_XIOCTL_WMI_FORCE_ASSERT:
        {
            if (wmi_force_target_assert(arPriv->arWmi) != A_OK)
            {
                    ret = -EIO;
            }
            break;
        }
        
        case AR6000_XIOCTL_WMI_SET_DIVERSITY_PARAM:
        {
            WMI_DIV_PARAMS_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(cmd)))
            {
                ret = -EFAULT;
            } else 
            {
                   if (wmi_set_div_param_cmd(arPriv->arWmi, cmd.divIdleTime, cmd.antRssiThresh, cmd.divEnable, cmd.active_treshold_rate)
                           != A_OK)
                    {
                        ret = -EIO;
                    }

            }
            break;
        }
        case AR6000_XIOCTL_AP_GET_NUM_STA:
        {
            A_UINT8    num_sta, ret_num_sta;
            
            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&num_sta, userdata, sizeof(num_sta))) {
                ret = -EFAULT;
            } else {
                if(num_sta & 0x80) {
                    ret_num_sta = ar->gNumSta;
                } else {
                    ret_num_sta = arPriv->num_sta;
                }
                if(copy_to_user((A_UINT8 *)rq->ifr_data,
                                    &ret_num_sta, sizeof(A_UINT8))) {
                    ret = -EFAULT;
                }
            }
            break;
        }
#ifdef CONFIG_PM
        case AR6000_XIOCTL_SUSPEND_DRIVER:
        {
            ar6000_suspend_ev(ar);
            break;
        }
        case AR6000_XIOCTL_RESUME_DRIVER:
        {
            ar6000_resume_ev(ar);
            break;
        }
#endif
        case AR6000_XIOCTL_GET_SUBMODE:
        {
            if (copy_to_user((A_UINT8 *)rq->ifr_data, &arPriv->arNetworkSubType,
                             sizeof(A_UINT8)))
            {
                ret = -EFAULT;
            }
            break;
        }
        case AR6000_XIOCTL_WMI_AP_SET_APSD:
        {
            WMI_AP_SET_APSD_CMD cmd;

            if (ar->arWmiReady == FALSE) {
                ret = -EIO;
            } else if (copy_from_user(&cmd, userdata, sizeof(WMI_AP_SET_APSD_CMD))) {
                ret = -EFAULT;
            } else  { 
                  if(wmi_ap_set_apsd(arPriv->arWmi, cmd.enable) != A_OK) {
                      ret = -EIO;
                  } else {
                      arPriv->ap_profile_flag = 1; /* There is a change in profile */
                  }
            } 
            break;
        }

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
