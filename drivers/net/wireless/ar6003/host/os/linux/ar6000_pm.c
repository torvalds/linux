//
//
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
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
//
//
//

/*
 * Implementation of system power management 
 */

#include "ar6000_drv.h"
#include <linux/inetdevice.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
#include <linux/platform_device.h>
#endif
#include "wlan_config.h"

#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif

#define WOW_ENABLE_MAX_INTERVAL 1
#define WOW_SET_SCAN_PARAMS     1

extern unsigned int wmitimeout;
extern unsigned int num_device;

#ifdef CONFIG_PM

#if PLAT_WOW_GPIO_PIN || PLAT_WLAN_CHIP_PWD_PIN
#include <linux/gpio.h>
#endif

#if PLAT_WOW_GPIO_PIN
static int wow_irq;
#endif /* PLAT_WOW_GPIO_PIN */

#ifdef CONFIG_HAS_WAKELOCK
struct wake_lock ar6k_suspend_wake_lock;
struct wake_lock ar6k_wow_wake_lock;
#endif
#endif /* CONFIG_PM */

#ifdef ANDROID_ENV
extern void android_ar6k_check_wow_status(AR_SOFTC_T *ar, struct sk_buff *skb, A_BOOL isEvent);
#endif 
#undef ATH_MODULE_NAME
#define ATH_MODULE_NAME pm
#define  ATH_DEBUG_PM       ATH_DEBUG_MAKE_MODULE_MASK(0)

#ifdef DEBUG
static ATH_DEBUG_MASK_DESCRIPTION pm_debug_desc[] = {
    { ATH_DEBUG_PM     , "System power management"},
};

ATH_DEBUG_INSTANTIATE_MODULE_VAR(pm,
                                 "pm",
                                 "System Power Management",
                                 ATH_DEBUG_MASK_DEFAULTS | ATH_DEBUG_PM,
                                 ATH_DEBUG_DESCRIPTION_COUNT(pm_debug_desc),
                                 pm_debug_desc);

#endif /* DEBUG */

A_STATUS ar6000_exit_cut_power_state(AR_SOFTC_T *ar);

#ifdef CONFIG_PM
static void ar6k_send_asleep_event_to_app(AR_SOFTC_DEV_T *arPriv, A_BOOL asleep)
{
    char buf[128];
    union iwreq_data wrqu;

    snprintf(buf, sizeof(buf), "HOST_ASLEEP=%s", asleep ? "asleep" : "awake");
    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = strlen(buf);
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

static void ar6000_wow_resume(AR_SOFTC_T *ar)
{
    HIF_DEVICE_POWER_CHANGE_TYPE  config;
    AR_SOFTC_DEV_T *arPriv;
    AR_SOFTC_STA_T *arSta;
    A_UINT8  i;

    if (ar->arWowState!= WLAN_WOW_STATE_NONE) {
        config = HIF_DEVICE_POWER_UP;
        HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));
    }

    for(i = 0; i < num_device; i++) {
        arPriv = ar->arDev[i];
        arSta  = &arPriv->arSta;

        /*
         * Free all wlan_nodes (scanned APs that we detected previously)
         * because during the time we were suspended, the device may have
         * been moved to a new location and these APs scanned for before
         * suspend may not be valid
         */
        wmi_free_allnodes(arPriv->arWmi);

        if (ar->arWowState!= WLAN_WOW_STATE_NONE) {
            A_UINT16 fg_start_period = (arSta->scParams.fg_start_period==0) ? 1 : arSta->scParams.fg_start_period;
            A_UINT16 bg_period = (arSta->scParams.bg_period==0) ? 60 : arSta->scParams.bg_period;
            WMI_SET_HOST_SLEEP_MODE_CMD hostSleepMode = {TRUE, FALSE};
            ar->arWowState = WLAN_WOW_STATE_NONE;
#ifdef CONFIG_HAS_WAKELOCK
            wake_lock_timeout(&ar6k_wow_wake_lock, 3*HZ);
#endif
            if (wmi_set_host_sleep_mode_cmd(arPriv->arWmi, &hostSleepMode)!=A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup restore host awake\n"));
            }

            if (arPriv->arNetworkType!=AP_NETWORK) {
#if WOW_SET_SCAN_PARAMS
                wmi_scanparams_cmd(arPriv->arWmi, fg_start_period,
                                       arSta->scParams.fg_end_period,
                                       bg_period,
                                       arSta->scParams.minact_chdwell_time,
                                       arSta->scParams.maxact_chdwell_time,
                                       arSta->scParams.pas_chdwell_time,
                                       arSta->scParams.shortScanRatio,
                                       arSta->scParams.scanCtrlFlags,
                                       arSta->scParams.max_dfsch_act_time,
                                       arSta->scParams.maxact_scan_per_ssid);
#else
               (void)fg_start_period;
               (void)bg_period;
#endif 
    
    
#if WOW_ENABLE_MAX_INTERVAL /* we don't do it if the power consumption is already good enough. */
                if (wmi_listeninterval_cmd(arPriv->arWmi, arSta->arListenIntervalT, arSta->arListenIntervalB) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to restore WoW listen interval %d\n", arSta->arListenIntervalT));
                } else {
                    if (wmi_bmisstime_cmd(arPriv->arWmi, arSta->arBmissTimeT, arSta->arBmissTimeB) != A_OK) {
                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to restore WoW bmiss %d\n", arSta->arBmissTimeT));
                    }
                    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("WoW does not invoked. skip resume"));
                }
#endif
            }
            ar6k_send_asleep_event_to_app(arPriv, FALSE);
            AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Resume WoW successfully\n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("WoW does not invoked. skip resume"));
        }
    }

    ar->arWlanPowerState = WLAN_POWER_STATE_ON;
}

static void ar6000_wow_suspend(AR_SOFTC_T *ar)
{
    struct net_device *ndev;
    AR_SOFTC_DEV_T *arPriv;
    AR_SOFTC_STA_T *arSta;
    A_INT32 i, j;
    A_STATUS status;
#define WOW_LIST_ID 1
    /* Setup WoW for unicast & Arp request for our own IP
     * disable background scan. Set listen interval into 1000 TUs
     * Enable keepliave for 110 seconds
     */
    struct in_ifaddr **ifap = NULL;
    struct in_ifaddr *ifa = NULL;
    HIF_DEVICE_POWER_CHANGE_TYPE  config;
    struct in_device *in_dev;
    WMI_ADD_WOW_PATTERN_CMD addWowCmd = { .filter = { 0 } };
    WMI_DEL_WOW_PATTERN_CMD delWowCmd;
    WMI_SET_HOST_SLEEP_MODE_CMD hostSleepMode = {FALSE, TRUE};
    WMI_SET_WOW_MODE_CMD wowMode = {    .enable_wow = TRUE,
                                        .hostReqDelay = 500 };/*500 ms delay*/

    if (ar->arWowState!= WLAN_WOW_STATE_NONE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("System already go into wow mode!\n"));
        return;
    }

    ar6000_TxDataCleanup(ar); /* IMPORTANT, otherwise there will be 11mA after listen interval as 1000*/

    for(j = 0; j< num_device; j++) 
    {
        arPriv = ar->arDev[j];
        arSta = &arPriv->arSta;
        ndev = arPriv->arNetDev;
        /* clear up our WoW pattern first */
        for (i=0; i<WOW_MAX_FILTERS_PER_LIST; ++i) {
            delWowCmd.filter_list_id = WOW_LIST_ID;
            delWowCmd.filter_id = i;
            wmi_del_wow_pattern_cmd(arPriv->arWmi, &delWowCmd);
        }

        if (arPriv->arNetworkType == AP_NETWORK) {
        /* setup all unicast IP packet pattern for WoW */
#if WLAN_CONFIG_SIMPLE_WOW_AP_MODE
        /* IP packets except boradcast */
        A_UINT8 allData[] = { 0x08 }; /* Either IP 0x0800, ARP 0x0806 or EAPOL-like 0x8800 */
        A_UINT8 allMask[] = { 0x7f };
        A_MEMZERO(&addWowCmd, sizeof(addWowCmd));
        addWowCmd.filter_list_id = WOW_LIST_ID;
        addWowCmd.filter_size = sizeof(allMask); 
        addWowCmd.filter_offset = 20;
        status = wmi_add_wow_pattern_cmd(ar->arWmi, &addWowCmd, allData, allMask, addWowCmd.filter_size);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add WoW simple pattern for AP mode\n"));
        }
#else
        /* Unicast IP, EAPOL-like and ARP packets */
        A_UINT8 unicastData[] = { 
            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x08, };
        A_UINT8 unicastMask[] = { 
            0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7f };
        A_UINT8 discoverData[] = { 0xe0, 0x00, 0x00, 0xf8 }; 
        A_UINT8 discoverMask[] = { 0xf0, 0x00, 0x00, 0xf8 };  
        A_UINT8 arpData[] = { 0x08, 0x06 };
        A_UINT8 arpMask[] = { 0xff, 0xff };
        A_UINT8 dhcpData[] = { 
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x08, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x43 /* port 67 */
        };
        A_UINT8 dhcpMask[] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0xff, 0xff /* port 67 */
        };
        A_MEMZERO(&addWowCmd, sizeof(addWowCmd));
        addWowCmd.filter_list_id = WOW_LIST_ID;
        addWowCmd.filter_size = sizeof(unicastMask); 
        addWowCmd.filter_offset = 0;
        status = wmi_add_wow_pattern_cmd(arPriv->arWmi, &addWowCmd, unicastData, unicastMask, addWowCmd.filter_size);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add WoW unicast IP pattern for AP mode\n"));
        }
        /* setup all ARP packet pattern for WoW */
        A_MEMZERO(&addWowCmd, sizeof(addWowCmd));
        addWowCmd.filter_list_id = WOW_LIST_ID;
        addWowCmd.filter_size = sizeof(arpMask); 
        addWowCmd.filter_offset = 20;
        status = wmi_add_wow_pattern_cmd(arPriv->arWmi, &addWowCmd, arpData, arpMask, addWowCmd.filter_size);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add WoW ARP pattern for AP mode\n"));
        }
        /* Setup multicast pattern for mDNS 224.0.0.251, SSDP 239.255.255.250 and LLMNR  224.0.0.252*/
        A_MEMZERO(&addWowCmd, sizeof(addWowCmd));
        addWowCmd.filter_list_id = WOW_LIST_ID;
        addWowCmd.filter_size = sizeof(discoverMask); 
        addWowCmd.filter_offset = 38;
        status = wmi_add_wow_pattern_cmd(arPriv->arWmi, &addWowCmd, discoverData, discoverMask, addWowCmd.filter_size);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add mDNS/SSDP/LLMNR pattern for AP mode WoW\n"));
        }
        /* setup all DHCP broadcast packet pattern for WoW */
        A_MEMZERO(&addWowCmd, sizeof(addWowCmd));
        addWowCmd.filter_list_id = WOW_LIST_ID;
        addWowCmd.filter_size = sizeof(dhcpMask); 
        addWowCmd.filter_offset = 0;
        status = wmi_add_wow_pattern_cmd(arPriv->arWmi, &addWowCmd, dhcpData, dhcpMask, addWowCmd.filter_size);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add WoW DHCP broadcast pattern for AP mode\n"));
        }
#endif /* WLAN_CONFIG_SIMPLE_WOW_AP_MODE */
    } else { 
        /* station mode */
        A_UINT8 macMask[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        A_UINT16 wow_listen_interval = A_MAX_WOW_LISTEN_INTERVAL;
#if WOW_ENABLE_MAX_INTERVAL /* we don't do it if the power consumption is already good enough. */
        if (wmi_listeninterval_cmd(arPriv->arWmi, wow_listen_interval, 0) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup WoW listen interval %d\n", wow_listen_interval));
        } else {
            /* 
             * The default bmiss is 1500ms when listen interval is 100ms 
             * We set listen interval x 15 times as bmiss time here
             */
            A_UINT16 bmissTime = wow_listen_interval*15;
            if (bmissTime > MAX_BMISS_TIME) {
                bmissTime = MAX_BMISS_TIME;
            }
            if (wmi_bmisstime_cmd(arPriv->arWmi, bmissTime, 0) != A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup WoW bmiss %d\n", bmissTime));
            }
        }
#else
        (void)wow_listen_interval;
#endif

#if WOW_SET_SCAN_PARAMS
        status = wmi_scanparams_cmd(arPriv->arWmi, 0xFFFF, 0, 0xFFFF, 0, 0, 0, 0, 0, 0, 0);
#endif
        /* setup unicast packet pattern for WoW */
        if (ndev->dev_addr[1]) {
            A_MEMZERO(&addWowCmd, sizeof(addWowCmd));
            addWowCmd.filter_list_id = WOW_LIST_ID;
            addWowCmd.filter_size = 6; /* MAC address */
            addWowCmd.filter_offset = 0;
            status = wmi_add_wow_pattern_cmd(arPriv->arWmi, &addWowCmd, ndev->dev_addr, macMask, addWowCmd.filter_size);
            if (status != A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add WoW pattern\n"));
            }
        }

        /* Setup multicast pattern for mDNS 224.0.0.251, SSDP 239.255.255.250 and LLMNR  224.0.0.252*/
        if ( ndev->flags & IFF_ALLMULTI || 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
                 (ndev->flags & IFF_MULTICAST && ndev->mc_count>0) )
#else
                 (ndev->flags & IFF_MULTICAST && netdev_mc_count(ndev)>0) )
#endif
        {
            A_UINT8 discoverData[] = { 0xe0, 0x00, 0x00, 0xf8 }; 
            A_UINT8 discoverMask[] = { 0xf0, 0x00, 0x00, 0xf8 };  
            A_MEMZERO(&addWowCmd, sizeof(addWowCmd));
            addWowCmd.filter_list_id = WOW_LIST_ID;
            addWowCmd.filter_size = sizeof(discoverMask); 
            addWowCmd.filter_offset = 38;
            status = wmi_add_wow_pattern_cmd(arPriv->arWmi, &addWowCmd, discoverData, discoverMask, addWowCmd.filter_size);
            if (status != A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add mDNS/SSDP/LLMNR pattern for WoW\n"));
            }
        }

        ar6k_send_asleep_event_to_app(arPriv, TRUE);
    }

    ar->arWowState = WLAN_WOW_STATE_SUSPENDING;

    /* setup ARP request for our own IP */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
    if ((in_dev = __in_dev_get_rtnl(ndev)) != NULL) {
        for (ifap = &in_dev->ifa_list; (ifa = *ifap) != NULL; ifap = &ifa->ifa_next) {
            if (!strcmp(ndev->name, ifa->ifa_label)) {
                break; /* found */
            }
        }
    }
#endif

    if (ifa && ifa->ifa_local) {
        WMI_SET_IP_CMD ipCmd;
        memset(&ipCmd, 0, sizeof(ipCmd));
        ipCmd.ips[0] = ifa->ifa_local;
        status = wmi_set_ip_cmd(arPriv->arWmi, &ipCmd);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup IP for ARP agent\n"));
        }
    }

#ifndef ATH6K_CONFIG_OTA_MODE
    wmi_powermode_cmd(arPriv->arWmi, REC_POWER);
#endif

    status = wmi_set_wow_mode_cmd(arPriv->arWmi, &wowMode);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to enable wow mode\n"));
    }
    
    status = wmi_set_host_sleep_mode_cmd(arPriv->arWmi, &hostSleepMode);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to set host asleep\n"));
    }

    if (ar->arTxPending[ar->arControlEp]) {
        A_UINT32 timeleft = wait_event_interruptible_timeout(arPriv->arEvent,
        ar->arTxPending[ar->arControlEp] == 0, wmitimeout * HZ);
        if (!timeleft || signal_pending(current)) {
           /* what can I do? wow resume at once */
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup WoW. Pending wmi control data %d\n", ar->arTxPending[ar->arControlEp]));
        }
    }
    }

    status = hifWaitForPendingRecv(ar->arHifDevice);

    config = HIF_DEVICE_POWER_DOWN;
    status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));

    ar->arWowState = WLAN_WOW_STATE_SUSPENDED;
    ar->arWlanPowerState = WLAN_POWER_STATE_WOW;
    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Setup WoW successfully\n"));
}

A_STATUS ar6000_suspend_ev(void *context)
{
    A_STATUS status = A_OK;
    AR_SOFTC_T *ar = (AR_SOFTC_T *)context;
    A_INT16 pmmode = ar->arSuspendConfig;
    A_INT32 i;
    A_BOOL needWow = FALSE;
wow_not_connected:
    switch (pmmode) {
    case WLAN_SUSPEND_WOW:
        for(i = 0; i< num_device; i++)  {
            if (ar->arDev[i]->arConnected) {
                needWow = TRUE;
                break;
            }
        }
        if (ar->arWmiReady && ar->arWlanState==WLAN_ENABLED && needWow) {
            ar6000_wow_suspend(ar);
            status = A_EBUSY;
            AR_DEBUG_PRINTF(ATH_DEBUG_PM,("%s:Suspend for wow mode %d\n", __func__, ar->arWlanPowerState));
        } else {
            pmmode = ar->arWow2Config;
            goto wow_not_connected;
        }
        break;
    case WLAN_SUSPEND_CUT_PWR:
        /* fall through */
    case WLAN_SUSPEND_CUT_PWR_IF_BT_OFF:
        /* fall through */
    case WLAN_SUSPEND_DEEP_SLEEP:
        /* fall through */
    default:
        status = ar6000_update_wlan_pwr_state(ar, WLAN_DISABLED, TRUE);        
        if (ar->arWlanPowerState==WLAN_POWER_STATE_ON || 
            ar->arWlanPowerState==WLAN_POWER_STATE_WOW) {
            AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Strange suspend state for not wow mode %d", ar->arWlanPowerState));
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_PM,("%s:Suspend for %d mode pwr %d status %d\n", __func__, pmmode, ar->arWlanPowerState, status));
        if (ar->arWlanPowerState==WLAN_POWER_STATE_CUT_PWR) {
            status = A_OK; 
        } else {
            status = A_EBUSY; /* don't let mmc call sdio_init after resume */
        } 
        break;
    }

    for(i = 0; i < num_device; i++) {
        AR_SOFTC_DEV_T *arPriv = ar->arDev[i];
        AR_SOFTC_STA_T *arSta = &arPriv->arSta;
        if (arSta->scan_triggered) {
            ar6000_scanComplete_event(arPriv, A_OK);
        }
    }
    return status;
}

A_STATUS ar6000_resume_ev(void *context)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)context;
    A_UINT16 powerState = ar->arWlanPowerState;

#ifdef CONFIG_HAS_WAKELOCK
    wake_lock(&ar6k_suspend_wake_lock);
#endif
    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: enter previous state %d wowState %d\n", __func__, powerState, ar->arWowState));
    switch (powerState) {
    case WLAN_POWER_STATE_WOW:
        ar6000_wow_resume(ar);
        break;
    case WLAN_POWER_STATE_CUT_PWR:
        /* fall through */
    case WLAN_POWER_STATE_DEEP_SLEEP:
        ar6000_update_wlan_pwr_state(ar, WLAN_ENABLED, TRUE);
        AR_DEBUG_PRINTF(ATH_DEBUG_PM,("%s:Resume for %d mode pwr %d\n", __func__, powerState, ar->arWlanPowerState));
        break;
    case WLAN_POWER_STATE_ON:
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Strange SDIO bus power mode!!\n"));
        break;
    }
#ifdef CONFIG_HAS_WAKELOCK
    wake_unlock(&ar6k_suspend_wake_lock);
#endif
    return A_OK;
}

void ar6000_check_wow_status(AR_SOFTC_T *ar, struct sk_buff *skb, A_BOOL isEvent)
{
    if (ar->arWowState!=WLAN_WOW_STATE_NONE) {
        if (ar->arWowState==WLAN_WOW_STATE_SUSPENDING) {
            AR_DEBUG_PRINTF(ATH_DEBUG_PM,("\n%s: Received IRQ while we are wow suspending!!!\n\n", __func__));
            return;
        }
        /* Wow resume from irq interrupt */
        AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: WoW resume from irq thread status %d\n", __func__, ar->arWlanPowerState));
        ar6000_wow_resume(ar);
    } else {
#ifdef ANDROID_ENV
        android_ar6k_check_wow_status(ar, skb, isEvent);
#endif 
    }
}

A_STATUS ar6000_power_change_ev(void *context, A_UINT32 config)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)context;
    A_STATUS status = A_OK;
  
    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: power change event callback %d \n", __func__, config));
    switch (config) {
       case HIF_DEVICE_POWER_UP:
            ar6000_restart_endpoint(ar);
#ifdef ANDROID_ENV
            ar->arResumeDone = TRUE;
            wake_up(&(ar->arDev[0]->arEvent));
#endif /* ANDROID_ENV */
            status = A_OK;
            break;
       case HIF_DEVICE_POWER_DOWN:
       case HIF_DEVICE_POWER_CUT:
            status = A_OK;
            break;
    }
    return status;
}

#if PLAT_WOW_GPIO_PIN
static irqreturn_t
ar6000_wow_irq(int irq, void *dev_id)
{
    gpio_clear_detect_status(wow_irq);
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock_timeout(&ar6k_wow_wake_lock, 3*HZ);
#else
    /* TODO: What should I do if there is no wake lock?? */
#endif
    return IRQ_HANDLED;
}
#endif /* PLAT_WOW_GPIO_PIN */

#if PLAT_WLAN_CHIP_PWD_PIN
void plat_setup_power_stub(AR_SOFTC_T *ar, int on, int detect) 
{
#if 0//def RK29
extern int wifi_turn_on_card(void);
extern int wifi_turn_off_card(void);
extern int rk29sdk_wifi_set_carddetect(int val);
extern int wifi_power_up_wifi(void);
extern int wifi_power_down_wifi(void);

 printk("%s :: on %d, detect %d\n", __func__, on, detect);
	if (on) {
		wifi_turn_on_card();
		wifi_power_up_wifi();
	} else {
		wifi_turn_off_card();
		wifi_power_down_wifi();
	}
	/* rk29sdk_wifi_set_carddetect(detect ? 1 : 0); */

#else
    A_BOOL chip_pwd_low_val;

    if (on) {
        chip_pwd_low_val = 1; 
    } else {
        chip_pwd_low_val = 0;
    }

    if (gpio_request(PLAT_WLAN_CHIP_PWD_PIN,  "wlan_chip_pwd_l")!=0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Cannot request CHIP_PWD GPIO"));
    } else {
        gpio_direction_output(PLAT_WLAN_CHIP_PWD_PIN, 0);/* WLAN_CHIP_PWD */
        gpio_set_value(PLAT_WLAN_CHIP_PWD_PIN, chip_pwd_low_val);
        A_MDELAY(100);
        gpio_free(PLAT_WLAN_CHIP_PWD_PIN);
   }
#endif
}
#endif /* PLAT_WLAN_CHIP_PWD_PIN */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
static int ar6000_pm_probe(struct platform_device *pdev)
{
    plat_setup_power(NULL, 1, 1);
    return 0;
}

static int ar6000_pm_remove(struct platform_device *pdev)
{
#ifdef RK29
    plat_setup_power(NULL, 0, 0);
#else
    plat_setup_power(NULL, 0, 1);
#endif
    return 0;
}

static int ar6000_pm_suspend(struct platform_device *pdev, pm_message_t state)
{
    return 0;
}

static int ar6000_pm_resume(struct platform_device *pdev)
{
    int i;
    extern struct net_device *ar6000_devices[MAX_AR6000];
    for (i=0; ar6000_devices[i]; ++i) {
        AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);
        AR_SOFTC_T *ar = (AR_SOFTC_T *)arPriv->arSoftc;
        if (ar && ar->arPlatPowerOff) {
            A_BOOL wlanOff = ar->arWlanOff;
            A_UINT16 powerState = ar->arWlanPowerState;
            A_BOOL btOff = ar->arBTOff;

            if (!wlanOff) {
               if (powerState == WLAN_POWER_STATE_CUT_PWR) {
                   plat_setup_power(ar, 1, 0);
                   ar->arPlatPowerOff = FALSE;
               }
            }
#ifdef CONFIG_PM
           else if (wlanOff) {
               A_BOOL allowCutPwr = ((!ar->arBTSharing) || btOff);
               if ((powerState==WLAN_POWER_STATE_CUT_PWR) && (!allowCutPwr)) {
                  plat_setup_power(ar, 1, 0);
                   ar->arPlatPowerOff = FALSE;       
               }
           }
#endif /* CONFIG_PM */
        }
    }
    return 0;
}

static struct platform_driver ar6000_pm_device = {
    .probe      = ar6000_pm_probe,
    .remove     = ar6000_pm_remove,
    .suspend    = ar6000_pm_suspend,
    .resume     = ar6000_pm_resume,
    .driver     = {
        .name = "wlan_ar6000_pm_dev",
    },
};
#endif 
#endif /* CONFIG_PM */

A_STATUS 
ar6000_setup_cut_power_state(struct ar6_softc *ar,  AR6000_WLAN_STATE state)
{
    A_STATUS                      status = A_OK;
    HIF_DEVICE_POWER_CHANGE_TYPE  config;

    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: Cut power %d %d \n", __func__,state, ar->arWlanPowerState));
#ifdef CONFIG_PM
    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Wlan OFF %d BT OFf %d \n", ar->arWlanOff, ar->arBTOff));
#endif
    do {
        if (state == WLAN_ENABLED) {
            /* Not in cut power state.. exit */
            if (ar->arWlanPowerState != WLAN_POWER_STATE_CUT_PWR) {
                break;
            }

            if (ar->arPlatPowerOff) {
                plat_setup_power(ar, 1, 0);
                ar->arPlatPowerOff = FALSE;
            }

            /* Change the state to ON */
            ar->arWlanPowerState = WLAN_POWER_STATE_ON;

#ifdef ANDROID_ENV
            ar->arResumeDone = FALSE;
#endif /* ANDROID_ENV */
    
            /* Indicate POWER_UP to HIF */
            config = HIF_DEVICE_POWER_UP; 
            status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));

            if (status == A_PENDING) {
#ifdef ANDROID_ENV
                 /* Wait for resume done event */
                A_UINT32 timeleft = wait_event_interruptible_timeout(ar->arDev[0]->arEvent,
                            (ar->arResumeDone == TRUE), wmitimeout * HZ);
                if (!timeleft || signal_pending(current)) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000 : Failed to get resume done event \n"));
                    status = A_ERROR;
                    break;
                }
#endif
                status = A_OK;
            } else if (status == A_OK) {
                ar6000_restart_endpoint(ar);
#ifdef ANDROID_ENV
                ar->arResumeDone = TRUE;
#endif /* ANDROID_ENV */
                status = A_OK;
            }
        } else if (state == WLAN_DISABLED) {


            /* Already in cut power state.. exit */
            if (ar->arWlanPowerState == WLAN_POWER_STATE_CUT_PWR) {
                break;
            }
            ar6000_stop_endpoint(ar, TRUE, FALSE);

            config = HIF_DEVICE_POWER_CUT; 
            status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));

            plat_setup_power(ar, 0, 0);
            ar->arPlatPowerOff = TRUE;

            ar->arWlanPowerState = WLAN_POWER_STATE_CUT_PWR;
        }
    } while (0);

    return status;
}

A_STATUS 
ar6000_setup_deep_sleep_state(struct ar6_softc *ar, AR6000_WLAN_STATE state)
{
    A_STATUS status = A_OK;

    HIF_DEVICE_POWER_CHANGE_TYPE  config;
    AR_SOFTC_DEV_T *arPriv;
    AR_SOFTC_STA_T *arSta;
    A_UINT8 i;

    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: Deep sleep %d %d \n", __func__,state, ar->arWlanPowerState));
#ifdef CONFIG_PM
    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Wlan OFF %d BT OFf %d \n", ar->arWlanOff, ar->arBTOff));
#endif
    do {
        WMI_SET_HOST_SLEEP_MODE_CMD hostSleepMode;

        if (state == WLAN_ENABLED) {
            A_UINT16 fg_start_period;

            /* Not in deep sleep state.. exit */
            if (ar->arWlanPowerState != WLAN_POWER_STATE_DEEP_SLEEP) {
                if (ar->arWlanPowerState != WLAN_POWER_STATE_ON) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Strange state when we resume from deep sleep %d\n", ar->arWlanPowerState));
                }                
                break;
            }

            /* Indicate POWER_UP to HIF */
            config = HIF_DEVICE_POWER_UP;
            status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));
            
            for(i = 0; i < num_device; i++)
            {
                arPriv = ar->arDev[i];
                arSta  = &arPriv->arSta;

                /*
                 * Free all wlan_nodes (scanned APs that we detected previously)
                 * because during the time we were suspended, the device may have
                 * been moved to a new location and these APs scanned for before
                 * suspend may not be valid
                 */
                wmi_free_allnodes(arPriv->arWmi);

                fg_start_period = (arSta->scParams.fg_start_period==0) ? 1 : arSta->scParams.fg_start_period;
                hostSleepMode.awake = TRUE;
                hostSleepMode.asleep = FALSE;

                if ((status=wmi_set_host_sleep_mode_cmd(arPriv->arWmi, &hostSleepMode)) != A_OK) {
                    break;    
                }

                /* Change the state to ON */
                ar->arWlanPowerState = WLAN_POWER_STATE_ON;

                /* Enable foreground scanning */
                if ((status=wmi_scanparams_cmd(arPriv->arWmi, fg_start_period,
                                        arSta->scParams.fg_end_period,
                                        arSta->scParams.bg_period,
                                        arSta->scParams.minact_chdwell_time,
                                        arSta->scParams.maxact_chdwell_time,
                                        arSta->scParams.pas_chdwell_time,
                                        arSta->scParams.shortScanRatio,
                                        arSta->scParams.scanCtrlFlags,
                                        arSta->scParams.max_dfsch_act_time,
                                        arSta->scParams.maxact_scan_per_ssid)) != A_OK) 
                {
                    break;
                }

                if (arPriv->arSsidLen) {
                    if (ar6000_connect_to_ap(arPriv) != A_OK) {
                    /* no need to report error if connection failed */
                        break;
                    }
                }
            }
        } else if (state == WLAN_DISABLED){
            WMI_SET_WOW_MODE_CMD wowMode = { .enable_wow = FALSE };

            /* Already in deep sleep state.. exit */
            if (ar->arWlanPowerState != WLAN_POWER_STATE_ON) {
                if (ar->arWlanPowerState != WLAN_POWER_STATE_DEEP_SLEEP) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Strange state when we suspend for deep sleep %d\n", ar->arWlanPowerState));
                }    
                break;
            }

            for(i = 0; i < num_device; i++)
            {
                arPriv = ar->arDev[i];
                arSta  = &arPriv->arSta;
                /* make sure we disable wow for deep sleep */
                if ((status=wmi_set_wow_mode_cmd(arPriv->arWmi, &wowMode))!=A_OK) {
                    break;
                }

                /* Disconnect from the AP and disable foreground scanning */
                AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
                if (arPriv->arConnected == TRUE || arSta->arConnectPending == TRUE) {
                    AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
                    ar6000_disconnect(arPriv);
                } else {
                    AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
                }

                if ((status=wmi_scanparams_cmd(arPriv->arWmi, 0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0, 0)) != A_OK) {
                    break;
                }
                ar6000_TxDataCleanup(ar);
#ifndef ATH6K_CONFIG_OTA_MODE
                wmi_powermode_cmd(arPriv->arWmi, REC_POWER);
#endif

                hostSleepMode.awake = FALSE;
                hostSleepMode.asleep = TRUE;
                if ((status=wmi_set_host_sleep_mode_cmd(arPriv->arWmi, &hostSleepMode))!=A_OK) {
                    break;
                }
                if (ar->arTxPending[ar->arControlEp]) {
                   A_UINT32 timeleft = wait_event_interruptible_timeout(arPriv->arEvent,
                                    ar->arTxPending[ar->arControlEp] == 0, wmitimeout * HZ);
                   if (!timeleft || signal_pending(current)) {
                        status = A_ERROR;
                        break;
                   }
                }
            }   
            status = hifWaitForPendingRecv(ar->arHifDevice);

            config = HIF_DEVICE_POWER_DOWN;
            status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));

            ar->arWlanPowerState = WLAN_POWER_STATE_DEEP_SLEEP;
        }
    } while (0);

    if (status!=A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to enter/exit deep sleep %d\n", state));
    }   
    
    return status;
}

A_STATUS 
ar6000_update_wlan_pwr_state(struct ar6_softc *ar, AR6000_WLAN_STATE state, A_BOOL pmEvent)
{
    A_STATUS status = A_OK;
    A_UINT16 powerState, oldPowerState;
    AR6000_WLAN_STATE oldstate = ar->arWlanState;
    A_BOOL wlanOff = ar->arWlanOff;
#ifdef CONFIG_PM
    A_BOOL btOff = ar->arBTOff;
#endif /* CONFIG_PM */
    A_UINT8 i;
    AR_SOFTC_DEV_T *arPriv;

    if ((state!=WLAN_DISABLED && state!=WLAN_ENABLED)) {
        return A_ERROR;
    }

    if (ar->bIsDestroyProgress) {
        return A_EBUSY;
    }

    if (down_interruptible(&ar->arSem)) {
        return A_ERROR;
    }

    if (ar->bIsDestroyProgress) {
        up(&ar->arSem); 
        return A_EBUSY;
    }

    ar->arWlanState = wlanOff ? WLAN_DISABLED : state;
    oldPowerState = ar->arWlanPowerState;
    if (state == WLAN_ENABLED) {
        powerState = ar->arWlanPowerState;
        AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("WLAN PWR set to ENABLE^^\n"));
        if (!wlanOff) {
            if (powerState == WLAN_POWER_STATE_DEEP_SLEEP) {
                status = ar6000_setup_deep_sleep_state(ar, WLAN_ENABLED);
            } else if (powerState == WLAN_POWER_STATE_CUT_PWR) {
                status = ar6000_setup_cut_power_state(ar, WLAN_ENABLED);
            }
        } 
#ifdef CONFIG_PM
        else if (pmEvent && wlanOff) {
            A_BOOL allowCutPwr = ((!ar->arBTSharing) || btOff);
            if ((powerState==WLAN_POWER_STATE_CUT_PWR) && (!allowCutPwr)) {
                /* Come out of cut power */
                ar6000_setup_cut_power_state(ar, WLAN_ENABLED);
                status = ar6000_setup_deep_sleep_state(ar, WLAN_DISABLED);
            }
        }
#endif /* CONFIG_PM */
    } else if (state == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("WLAN PWR set to DISABLED~\n"));
        powerState = WLAN_POWER_STATE_DEEP_SLEEP;
#ifdef CONFIG_PM
        if (pmEvent) {  /* disable due to suspend */
            A_BOOL suspendCutPwr = (ar->arSuspendConfig == WLAN_SUSPEND_CUT_PWR ||
                                    (ar->arSuspendConfig == WLAN_SUSPEND_WOW && 
                                        ar->arWow2Config==WLAN_SUSPEND_CUT_PWR));
            A_BOOL suspendCutIfBtOff = ((ar->arSuspendConfig == 
                                            WLAN_SUSPEND_CUT_PWR_IF_BT_OFF ||
                                        (ar->arSuspendConfig == WLAN_SUSPEND_WOW && 
                                         ar->arWow2Config==WLAN_SUSPEND_CUT_PWR_IF_BT_OFF)) &&
                                        (!ar->arBTSharing || btOff));
            if ((suspendCutPwr) ||
                (suspendCutIfBtOff) ||
                (ar->arWlanPowerState==WLAN_POWER_STATE_CUT_PWR))
            {
                if (!wlanOff || ar->arWlanPowerState!=WLAN_POWER_STATE_DEEP_SLEEP) {
                    powerState = WLAN_POWER_STATE_CUT_PWR;
                }
            }
        } else {
            if ((wlanOff) && 
                (ar->arWlanOffConfig == WLAN_OFF_CUT_PWR) &&
                (!ar->arBTSharing || btOff)) 
            {
                /* For BT clock sharing designs, CUT_POWER depend on BT state */  
                powerState = WLAN_POWER_STATE_CUT_PWR;
            }
        }
#endif /* CONFIG_PM */

        if (powerState == WLAN_POWER_STATE_DEEP_SLEEP) {
            if (ar->arWlanPowerState == WLAN_POWER_STATE_CUT_PWR) {
                AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Load firmware before set to deep sleep\n"));
                ar6000_setup_cut_power_state(ar, WLAN_ENABLED);
            }
            status = ar6000_setup_deep_sleep_state(ar, WLAN_DISABLED);
        } else if (powerState == WLAN_POWER_STATE_CUT_PWR) {
            status = ar6000_setup_cut_power_state(ar, WLAN_DISABLED);
        }

    }

    if (status!=A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup WLAN state %d\n", ar->arWlanState));
        ar->arWlanState = oldstate;
    } else if (status == A_OK) {
        WMI_REPORT_SLEEP_STATE_EVENT  wmiSleepEvent, *pSleepEvent = NULL;
        if ((ar->arWlanPowerState == WLAN_POWER_STATE_ON) && (oldPowerState != WLAN_POWER_STATE_ON)) {
            wmiSleepEvent.sleepState = WMI_REPORT_SLEEP_STATUS_IS_AWAKE;
            pSleepEvent = &wmiSleepEvent;
        } else if ((ar->arWlanPowerState != WLAN_POWER_STATE_ON) && (oldPowerState == WLAN_POWER_STATE_ON)) {
            wmiSleepEvent.sleepState = WMI_REPORT_SLEEP_STATUS_IS_DEEP_SLEEP;
            pSleepEvent = &wmiSleepEvent;
        }
        if (pSleepEvent) {
            AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("SENT WLAN Sleep Event %d\n", wmiSleepEvent.sleepState));
            for(i = 0; i < num_device; i++)
            {
                arPriv = ar->arDev[i];
                ar6000_send_event_to_app(arPriv, WMI_REPORT_SLEEP_STATE_EVENTID, (A_UINT8*)pSleepEvent, 
                                         sizeof(WMI_REPORT_SLEEP_STATE_EVENTID));
            }
        }
    }
    up(&ar->arSem);
    return status;
}

A_STATUS 
ar6000_set_bt_hw_state(struct ar6_softc *ar, A_UINT32 enable)
{
#ifdef CONFIG_PM
    A_BOOL off = (enable == 0);
    A_STATUS status;
    if (ar->arBTOff == off) {
        return A_OK;
    }
    ar->arBTOff = off;
    status = ar6000_update_wlan_pwr_state(ar, ar->arWlanOff ? WLAN_DISABLED : WLAN_ENABLED, FALSE);
    return status;
#else
    return A_OK;
#endif 
}

A_STATUS 
ar6000_set_wlan_state(struct ar6_softc *ar, AR6000_WLAN_STATE state)
{
    A_INT32 i;
    A_STATUS status;
    A_BOOL off = (state == WLAN_DISABLED);
    if (ar->arWlanOff == off) {
        return A_OK;
    }
    ar->arWlanOff = off;
    status = ar6000_update_wlan_pwr_state(ar, state, FALSE);
    for(i = 0; i < num_device; i++) {
        AR_SOFTC_DEV_T *arPriv = ar->arDev[i];
        AR_SOFTC_STA_T *arSta = &arPriv->arSta;
        if (arSta->scan_triggered) {
            ar6000_scanComplete_event(arPriv, A_OK);
        }
    }
    return status;
}

void ar6000_pm_init()
{
    A_REGISTER_MODULE_DEBUG_INFO(pm);
#ifdef CONFIG_PM
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock_init(&ar6k_suspend_wake_lock, WAKE_LOCK_SUSPEND, "ar6k_suspend");
    wake_lock_init(&ar6k_wow_wake_lock, WAKE_LOCK_SUSPEND, "ar6k_wow");
#endif
    /* 
     * Register ar6000_pm_device into system.
     * We should also add platform_device into the first item of array
     * of devices[] in file arch/xxx/mach-xxx/board-xxxx.c
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
    if (platform_driver_register(&ar6000_pm_device)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000: fail to register the power control driver.\n"));
    }
#endif

#if PLAT_WOW_GPIO_PIN
    wow_irq = gpio_to_irq(PLAT_WOW_GPIO_PIN);
    if (wow_irq) {
        int ret;
        ret = request_irq(wow_irq, ar6000_wow_irq,
                        IRQF_SHARED | IRQF_TRIGGER_RISING,
                        "ar6000" "sdiowakeup", &wow_irq);
        if (!ret) {
            ret = enable_irq_wake(wow_irq);
            if (ret < 0) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Couldn't enable WoW IRQ as wakeup interrupt"));
            }
        }
    }
#endif /* PLAT_WOW_GPIO_PIN */
#endif /* CONFIG_PM */
}

void ar6000_pm_exit()
{
#ifdef CONFIG_PM
#if PLAT_WOW_GPIO_PIN
    if (wow_irq) {
        if (disable_irq_wake(wow_irq)) {
             AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Couldn't disable hostwake IRQ wakeup mode\n"));
        }
        free_irq(wow_irq, &wow_irq);
        wow_irq = 0;
    }
#endif /* PLAT_WOW_GPIO_PIN */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
    platform_driver_unregister(&ar6000_pm_device);
#endif
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock_destroy(&ar6k_suspend_wake_lock);
    wake_lock_destroy(&ar6k_wow_wake_lock);
#endif
#endif /* CONFIG_PM */
}

