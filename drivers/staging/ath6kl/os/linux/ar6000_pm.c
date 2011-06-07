/*
 *
 * Copyright (c) 2004-2010 Atheros Communications Inc.
 * All rights reserved.
 *
 * 
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
 *
 */

/*
 * Implementation of system power management
 */

#include "ar6000_drv.h"
#include <linux/inetdevice.h>
#include <linux/platform_device.h>
#include "wlan_config.h"

#define WOW_ENABLE_MAX_INTERVAL 0
#define WOW_SET_SCAN_PARAMS     0

extern unsigned int wmitimeout;
extern wait_queue_head_t arEvent;

#undef ATH_MODULE_NAME
#define ATH_MODULE_NAME pm
#define  ATH_DEBUG_PM       ATH_DEBUG_MAKE_MODULE_MASK(0)

#ifdef DEBUG
static struct ath_debug_mask_description pm_debug_desc[] = {
    { ATH_DEBUG_PM     , "System power management"},
};

ATH_DEBUG_INSTANTIATE_MODULE_VAR(pm,
                                 "pm",
                                 "System Power Management",
                                 ATH_DEBUG_MASK_DEFAULTS | ATH_DEBUG_PM,
                                 ATH_DEBUG_DESCRIPTION_COUNT(pm_debug_desc),
                                 pm_debug_desc);

#endif /* DEBUG */

int ar6000_exit_cut_power_state(struct ar6_softc *ar);

#ifdef CONFIG_PM
static void ar6k_send_asleep_event_to_app(struct ar6_softc *ar, bool asleep)
{
    char buf[128];
    union iwreq_data wrqu;

    snprintf(buf, sizeof(buf), "HOST_ASLEEP=%s", asleep ? "asleep" : "awake");
    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = strlen(buf);
    wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

static void ar6000_wow_resume(struct ar6_softc *ar)
{
    if (ar->arWowState!= WLAN_WOW_STATE_NONE) {
        u16 fg_start_period = (ar->scParams.fg_start_period==0) ? 1 : ar->scParams.fg_start_period;
        u16 bg_period = (ar->scParams.bg_period==0) ? 60 : ar->scParams.bg_period;
        WMI_SET_HOST_SLEEP_MODE_CMD hostSleepMode = {true, false};
        ar->arWowState = WLAN_WOW_STATE_NONE;
        if (wmi_set_host_sleep_mode_cmd(ar->arWmi, &hostSleepMode)!= 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup restore host awake\n"));
        }
#if WOW_SET_SCAN_PARAMS
        wmi_scanparams_cmd(ar->arWmi, fg_start_period,
                                   ar->scParams.fg_end_period,
                                   bg_period,
                                   ar->scParams.minact_chdwell_time,
                                   ar->scParams.maxact_chdwell_time,
                                   ar->scParams.pas_chdwell_time,
                                   ar->scParams.shortScanRatio,
                                   ar->scParams.scanCtrlFlags,
                                   ar->scParams.max_dfsch_act_time,
                                   ar->scParams.maxact_scan_per_ssid);
#else
       (void)fg_start_period;
       (void)bg_period;
#endif


#if WOW_ENABLE_MAX_INTERVAL /* we don't do it if the power consumption is already good enough. */
        if (wmi_listeninterval_cmd(ar->arWmi, ar->arListenIntervalT, ar->arListenIntervalB) == 0) {
        }
#endif
        ar6k_send_asleep_event_to_app(ar, false);
        AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Resume WoW successfully\n"));
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("WoW does not invoked. skip resume"));
    }
    ar->arWlanPowerState = WLAN_POWER_STATE_ON;
}

static void ar6000_wow_suspend(struct ar6_softc *ar)
{
#define WOW_LIST_ID 1
    if (ar->arNetworkType != AP_NETWORK) {
        /* Setup WoW for unicast & Arp request for our own IP
        disable background scan. Set listen interval into 1000 TUs
        Enable keepliave for 110 seconds
        */
        struct in_ifaddr **ifap = NULL;
        struct in_ifaddr *ifa = NULL;
        struct in_device *in_dev;
        u8 macMask[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        int status;
        WMI_ADD_WOW_PATTERN_CMD addWowCmd = { .filter = { 0 } };
        WMI_DEL_WOW_PATTERN_CMD delWowCmd;
        WMI_SET_HOST_SLEEP_MODE_CMD hostSleepMode = {false, true};
        WMI_SET_WOW_MODE_CMD wowMode = {    .enable_wow = true,
                                            .hostReqDelay = 500 };/*500 ms delay*/

        if (ar->arWowState!= WLAN_WOW_STATE_NONE) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("System already go into wow mode!\n"));
            return;
        }

        ar6000_TxDataCleanup(ar); /* IMPORTANT, otherwise there will be 11mA after listen interval as 1000*/

#if WOW_ENABLE_MAX_INTERVAL /* we don't do it if the power consumption is already good enough. */
        if (wmi_listeninterval_cmd(ar->arWmi, A_MAX_WOW_LISTEN_INTERVAL, 0) == 0) {
        }
#endif

#if WOW_SET_SCAN_PARAMS
        status = wmi_scanparams_cmd(ar->arWmi, 0xFFFF, 0, 0xFFFF, 0, 0, 0, 0, 0, 0, 0);
#endif
        /* clear up our WoW pattern first */
        delWowCmd.filter_list_id = WOW_LIST_ID;
        delWowCmd.filter_id = 0;
        wmi_del_wow_pattern_cmd(ar->arWmi, &delWowCmd);

        /* setup unicast packet pattern for WoW */
        if (ar->arNetDev->dev_addr[1]) {
            addWowCmd.filter_list_id = WOW_LIST_ID;
            addWowCmd.filter_size = 6; /* MAC address */
            addWowCmd.filter_offset = 0;
            status = wmi_add_wow_pattern_cmd(ar->arWmi, &addWowCmd, ar->arNetDev->dev_addr, macMask, addWowCmd.filter_size);
            if (status) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to add WoW pattern\n"));
            }
        }
        /* setup ARP request for our own IP */
        if ((in_dev = __in_dev_get_rtnl(ar->arNetDev)) != NULL) {
            for (ifap = &in_dev->ifa_list; (ifa = *ifap) != NULL; ifap = &ifa->ifa_next) {
                if (!strcmp(ar->arNetDev->name, ifa->ifa_label)) {
                    break; /* found */
                }
            }
        }
        if (ifa && ifa->ifa_local) {
            WMI_SET_IP_CMD ipCmd;
            memset(&ipCmd, 0, sizeof(ipCmd));
            ipCmd.ips[0] = ifa->ifa_local;
            status = wmi_set_ip_cmd(ar->arWmi, &ipCmd);
            if (status) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup IP for ARP agent\n"));
            }
        }

#ifndef ATH6K_CONFIG_OTA_MODE
        wmi_powermode_cmd(ar->arWmi, REC_POWER);
#endif

        status = wmi_set_wow_mode_cmd(ar->arWmi, &wowMode);
        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to enable wow mode\n"));
        }
        ar6k_send_asleep_event_to_app(ar, true);

        status = wmi_set_host_sleep_mode_cmd(ar->arWmi, &hostSleepMode);
        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to set host asleep\n"));
        }

        ar->arWowState = WLAN_WOW_STATE_SUSPENDING;
        if (ar->arTxPending[ar->arControlEp]) {
            u32 timeleft = wait_event_interruptible_timeout(arEvent,
            ar->arTxPending[ar->arControlEp] == 0, wmitimeout * HZ);
            if (!timeleft || signal_pending(current)) {
               /* what can I do? wow resume at once */
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup WoW. Pending wmi control data %d\n", ar->arTxPending[ar->arControlEp]));
            }
        }

        status = hifWaitForPendingRecv(ar->arHifDevice);

        ar->arWowState = WLAN_WOW_STATE_SUSPENDED;
        ar->arWlanPowerState = WLAN_POWER_STATE_WOW;
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Not allowed to go to WOW at this moment.\n"));
    }
}

int ar6000_suspend_ev(void *context)
{
    int status = 0;
    struct ar6_softc *ar = (struct ar6_softc *)context;
    s16 pmmode = ar->arSuspendConfig;
wow_not_connected:
    switch (pmmode) {
    case WLAN_SUSPEND_WOW:
        if (ar->arWmiReady && ar->arWlanState==WLAN_ENABLED && ar->arConnected) {
            ar6000_wow_suspend(ar);
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
        status = ar6000_update_wlan_pwr_state(ar, WLAN_DISABLED, true);
        if (ar->arWlanPowerState==WLAN_POWER_STATE_ON ||
            ar->arWlanPowerState==WLAN_POWER_STATE_WOW) {
            AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Strange suspend state for not wow mode %d", ar->arWlanPowerState));
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_PM,("%s:Suspend for %d mode pwr %d status %d\n", __func__, pmmode, ar->arWlanPowerState, status));
        status = (ar->arWlanPowerState == WLAN_POWER_STATE_CUT_PWR) ? 0 : A_EBUSY;
        break;
    }

    ar->scan_triggered = 0;
    return status;
}

int ar6000_resume_ev(void *context)
{
    struct ar6_softc *ar = (struct ar6_softc *)context;
    u16 powerState = ar->arWlanPowerState;

    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: enter previous state %d wowState %d\n", __func__, powerState, ar->arWowState));
    switch (powerState) {
    case WLAN_POWER_STATE_WOW:
        ar6000_wow_resume(ar);
        break;
    case WLAN_POWER_STATE_CUT_PWR:
        /* fall through */
    case WLAN_POWER_STATE_DEEP_SLEEP:
        ar6000_update_wlan_pwr_state(ar, WLAN_ENABLED, true);
        AR_DEBUG_PRINTF(ATH_DEBUG_PM,("%s:Resume for %d mode pwr %d\n", __func__, powerState, ar->arWlanPowerState));
        break;
    case WLAN_POWER_STATE_ON:
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Strange SDIO bus power mode!!\n"));
        break;
    }
    return 0;
}

void ar6000_check_wow_status(struct ar6_softc *ar, struct sk_buff *skb, bool isEvent)
{
    if (ar->arWowState!=WLAN_WOW_STATE_NONE) {
        if (ar->arWowState==WLAN_WOW_STATE_SUSPENDING) {
            AR_DEBUG_PRINTF(ATH_DEBUG_PM,("\n%s: Received IRQ while we are wow suspending!!!\n\n", __func__));
            return;
        }
        /* Wow resume from irq interrupt */
        AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: WoW resume from irq thread status %d\n", __func__, ar->arWlanPowerState));
        ar6000_wow_resume(ar);
    }
}

int ar6000_power_change_ev(void *context, u32 config)
{
    struct ar6_softc *ar = (struct ar6_softc *)context;
    int status = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: power change event callback %d \n", __func__, config));
    switch (config) {
       case HIF_DEVICE_POWER_UP:
            ar6000_restart_endpoint(ar->arNetDev);
            status = 0;
            break;
       case HIF_DEVICE_POWER_DOWN:
       case HIF_DEVICE_POWER_CUT:
            status = 0;
            break;
    }
    return status;
}

#endif /* CONFIG_PM */

int
ar6000_setup_cut_power_state(struct ar6_softc *ar,  AR6000_WLAN_STATE state)
{
    int                      status = 0;
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

            /* Change the state to ON */
            ar->arWlanPowerState = WLAN_POWER_STATE_ON;


            /* Indicate POWER_UP to HIF */
            config = HIF_DEVICE_POWER_UP;
            status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));

            if (status == A_PENDING) {
            } else if (status == 0) {
                ar6000_restart_endpoint(ar->arNetDev);
                status = 0;
            }
        } else if (state == WLAN_DISABLED) {


            /* Already in cut power state.. exit */
            if (ar->arWlanPowerState == WLAN_POWER_STATE_CUT_PWR) {
                break;
            }
            ar6000_stop_endpoint(ar->arNetDev, true, false);

            config = HIF_DEVICE_POWER_CUT;
            status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_POWER_STATE_CHANGE,
                                &config,
                                sizeof(HIF_DEVICE_POWER_CHANGE_TYPE));

            ar->arWlanPowerState = WLAN_POWER_STATE_CUT_PWR;
        }
    } while (0);

    return status;
}

int
ar6000_setup_deep_sleep_state(struct ar6_softc *ar, AR6000_WLAN_STATE state)
{
    int status = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("%s: Deep sleep %d %d \n", __func__,state, ar->arWlanPowerState));
#ifdef CONFIG_PM
    AR_DEBUG_PRINTF(ATH_DEBUG_PM, ("Wlan OFF %d BT OFf %d \n", ar->arWlanOff, ar->arBTOff));
#endif
    do {
        WMI_SET_HOST_SLEEP_MODE_CMD hostSleepMode;

        if (state == WLAN_ENABLED) {
            u16 fg_start_period;

            /* Not in deep sleep state.. exit */
            if (ar->arWlanPowerState != WLAN_POWER_STATE_DEEP_SLEEP) {
                if (ar->arWlanPowerState != WLAN_POWER_STATE_ON) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Strange state when we resume from deep sleep %d\n", ar->arWlanPowerState));
                }
                break;
            }

            fg_start_period = (ar->scParams.fg_start_period==0) ? 1 : ar->scParams.fg_start_period;
            hostSleepMode.awake = true;
            hostSleepMode.asleep = false;

            if ((status=wmi_set_host_sleep_mode_cmd(ar->arWmi, &hostSleepMode)) != 0) {
                break;
            }

            /* Change the state to ON */
            ar->arWlanPowerState = WLAN_POWER_STATE_ON;

                /* Enable foreground scanning */
                if ((status=wmi_scanparams_cmd(ar->arWmi, fg_start_period,
                                        ar->scParams.fg_end_period,
                                        ar->scParams.bg_period,
                                        ar->scParams.minact_chdwell_time,
                                        ar->scParams.maxact_chdwell_time,
                                        ar->scParams.pas_chdwell_time,
                                        ar->scParams.shortScanRatio,
                                        ar->scParams.scanCtrlFlags,
                                        ar->scParams.max_dfsch_act_time,
                                        ar->scParams.maxact_scan_per_ssid)) != 0)
                {
                    break;
                }

            if (ar->arNetworkType != AP_NETWORK)
            {
                if (ar->arSsidLen) {
                    if (ar6000_connect_to_ap(ar) != 0) {
                        /* no need to report error if connection failed */
                        break;
                    }
                }
            }
        } else if (state == WLAN_DISABLED){
            WMI_SET_WOW_MODE_CMD wowMode = { .enable_wow = false };

            /* Already in deep sleep state.. exit */
            if (ar->arWlanPowerState != WLAN_POWER_STATE_ON) {
                if (ar->arWlanPowerState != WLAN_POWER_STATE_DEEP_SLEEP) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Strange state when we suspend for deep sleep %d\n", ar->arWlanPowerState));
                }
                break;
            }

            if (ar->arNetworkType != AP_NETWORK)
            {
                /* Disconnect from the AP and disable foreground scanning */
                AR6000_SPIN_LOCK(&ar->arLock, 0);
                if (ar->arConnected == true || ar->arConnectPending == true) {
                    AR6000_SPIN_UNLOCK(&ar->arLock, 0);
                    wmi_disconnect_cmd(ar->arWmi);
                } else {
                    AR6000_SPIN_UNLOCK(&ar->arLock, 0);
                }
            }

            ar->scan_triggered = 0;

            if ((status=wmi_scanparams_cmd(ar->arWmi, 0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0, 0)) != 0) {
                break;
            }

            /* make sure we disable wow for deep sleep */
            if ((status=wmi_set_wow_mode_cmd(ar->arWmi, &wowMode))!= 0)
            {
                break;
            }

            ar6000_TxDataCleanup(ar);
#ifndef ATH6K_CONFIG_OTA_MODE
            wmi_powermode_cmd(ar->arWmi, REC_POWER);
#endif

            hostSleepMode.awake = false;
            hostSleepMode.asleep = true;
            if ((status=wmi_set_host_sleep_mode_cmd(ar->arWmi, &hostSleepMode))!= 0) {
                break;
            }
            if (ar->arTxPending[ar->arControlEp]) {
                u32 timeleft = wait_event_interruptible_timeout(arEvent,
                                ar->arTxPending[ar->arControlEp] == 0, wmitimeout * HZ);
                if (!timeleft || signal_pending(current)) {
                    status = A_ERROR;
                    break;
                }
            }
            status = hifWaitForPendingRecv(ar->arHifDevice);

            ar->arWlanPowerState = WLAN_POWER_STATE_DEEP_SLEEP;
        }
    } while (0);

    if (status) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to enter/exit deep sleep %d\n", state));
    }

    return status;
}

int
ar6000_update_wlan_pwr_state(struct ar6_softc *ar, AR6000_WLAN_STATE state, bool pmEvent)
{
    int status = 0;
    u16 powerState, oldPowerState;
    AR6000_WLAN_STATE oldstate = ar->arWlanState;
    bool wlanOff = ar->arWlanOff;
#ifdef CONFIG_PM
    bool btOff = ar->arBTOff;
#endif /* CONFIG_PM */

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
            bool allowCutPwr = ((!ar->arBTSharing) || btOff);
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
            bool suspendCutPwr = (ar->arSuspendConfig == WLAN_SUSPEND_CUT_PWR ||
                                    (ar->arSuspendConfig == WLAN_SUSPEND_WOW &&
                                        ar->arWow2Config==WLAN_SUSPEND_CUT_PWR));
            bool suspendCutIfBtOff = ((ar->arSuspendConfig ==
                                            WLAN_SUSPEND_CUT_PWR_IF_BT_OFF ||
                                        (ar->arSuspendConfig == WLAN_SUSPEND_WOW &&
                                         ar->arWow2Config==WLAN_SUSPEND_CUT_PWR_IF_BT_OFF)) &&
                                        (!ar->arBTSharing || btOff));
            if ((suspendCutPwr) ||
                (suspendCutIfBtOff) ||
                (ar->arWlanState==WLAN_POWER_STATE_CUT_PWR))
            {
                powerState = WLAN_POWER_STATE_CUT_PWR;
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

    if (status) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Fail to setup WLAN state %d\n", ar->arWlanState));
        ar->arWlanState = oldstate;
    } else if (status == 0) {
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
        }
    }
    up(&ar->arSem);
    return status;
}

int
ar6000_set_bt_hw_state(struct ar6_softc *ar, u32 enable)
{
#ifdef CONFIG_PM
    bool off = (enable == 0);
    int status;
    if (ar->arBTOff == off) {
        return 0;
    }
    ar->arBTOff = off;
    status = ar6000_update_wlan_pwr_state(ar, ar->arWlanOff ? WLAN_DISABLED : WLAN_ENABLED, false);
    return status;
#else
    return 0;
#endif
}

int
ar6000_set_wlan_state(struct ar6_softc *ar, AR6000_WLAN_STATE state)
{
    int status;
    bool off = (state == WLAN_DISABLED);
    if (ar->arWlanOff == off) {
        return 0;
    }
    ar->arWlanOff = off;
    status = ar6000_update_wlan_pwr_state(ar, state, false);
    return status;
}
