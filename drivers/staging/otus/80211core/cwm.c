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
/*                                                                      */
/*  Module Name : cwm.c                                                 */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains channel width related functions.           */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/

#include "cprecomp.h"



void zfCwmInit(zdev_t* dev) {
    //u16_t i;
    zmw_get_wlan_dev(dev);

    switch (wd->wlanMode) {
    case ZM_MODE_AP:
        wd->cwm.cw_mode = CWM_MODE2040;
        wd->cwm.cw_width = CWM_WIDTH40;
        wd->cwm.cw_enable = 1;
        break;
    case ZM_MODE_INFRASTRUCTURE:
    case ZM_MODE_PSEUDO:
    case ZM_MODE_IBSS:
    default:
        wd->cwm.cw_mode = CWM_MODE2040;
        wd->cwm.cw_width = CWM_WIDTH20;
        wd->cwm.cw_enable = 1;
        break;
    }
}


void zfCoreCwmBusy(zdev_t* dev, u16_t busy)
{

    zmw_get_wlan_dev(dev);

    zm_msg1_mm(ZM_LV_0, "CwmBusy=", busy);

    if(wd->cwm.cw_mode == CWM_MODE20) {
        wd->cwm.cw_width = CWM_WIDTH20;
        return;
    }

    if(wd->cwm.cw_mode == CWM_MODE40) {
        wd->cwm.cw_width = CWM_WIDTH40;
        return;
    }

    if (busy) {
        wd->cwm.cw_width = CWM_WIDTH20;
        return;
    }


    if((wd->wlanMode == ZM_MODE_INFRASTRUCTURE || wd->wlanMode == ZM_MODE_PSEUDO ||
        wd->wlanMode == ZM_MODE_IBSS)) {
        if ((wd->sta.ie.HtCap.HtCapInfo & HTCAP_SupChannelWidthSet) &&
            (wd->sta.ie.HtInfo.ChannelInfo & ExtHtCap_RecomTxWidthSet) &&
            (wd->sta.ie.HtInfo.ChannelInfo & ExtHtCap_ExtChannelOffsetAbove)) {

            wd->cwm.cw_width = CWM_WIDTH40;
        }
        else {
            wd->cwm.cw_width = CWM_WIDTH20;
        }

        return;
    }

    if(wd->wlanMode == ZM_MODE_AP) {
        wd->cwm.cw_width = CWM_WIDTH40;
    }

}




u16_t zfCwmIsExtChanBusy(u32_t ctlBusy, u32_t extBusy)
{
    u32_t busy; /* percentage */
    u32_t cycleTime, ctlClear;

    cycleTime = 1280000; //1.28 seconds

    if (cycleTime > ctlBusy) {
        ctlClear = cycleTime - ctlBusy;
    }
    else
    {
        ctlClear = 0;
    }

    /* Compute ratio of extension channel busy to control channel clear
     * as an approximation to extension channel cleanliness.
     *
     * According to the hardware folks, ext rxclear is undefined
     * if the ctrl rxclear is de-asserted (i.e. busy)
     */
    if (ctlClear) {
        busy = (extBusy * 100) / ctlClear;
    } else {
        busy = 0;
    }
    if (busy > ATH_CWM_EXTCH_BUSY_THRESHOLD) {
        return TRUE;
    }

    return FALSE;
}
