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

#include "cprecomp.h"

/* zfAddFreqChangeReq should be called inside the critical section */
static void zfAddFreqChangeReq(zdev_t* dev, u16_t frequency, u8_t bw40,
        u8_t extOffset, zfpFreqChangeCompleteCb cb)
{
    zmw_get_wlan_dev(dev);

//printk("zfAddFreqChangeReq  freqReqQueueTail%d\n", wd->freqCtrl.freqReqQueueTail);
    wd->freqCtrl.freqReqQueue[wd->freqCtrl.freqReqQueueTail] = frequency;
    wd->freqCtrl.freqReqBw40[wd->freqCtrl.freqReqQueueTail] = bw40;
    wd->freqCtrl.freqReqExtOffset[wd->freqCtrl.freqReqQueueTail] = extOffset;
    wd->freqCtrl.freqChangeCompCb[wd->freqCtrl.freqReqQueueTail] = cb;
    wd->freqCtrl.freqReqQueueTail++;
    if ( wd->freqCtrl.freqReqQueueTail >= ZM_MAX_FREQ_REQ_QUEUE )
    {
        wd->freqCtrl.freqReqQueueTail = 0;
    }
}

void zfCoreSetFrequencyV2(zdev_t* dev, u16_t frequency, zfpFreqChangeCompleteCb cb)
{
    zfCoreSetFrequencyEx(dev, frequency, 0, 0, cb);
}

void zfCoreSetFrequencyExV2(zdev_t* dev, u16_t frequency, u8_t bw40,
        u8_t extOffset, zfpFreqChangeCompleteCb cb, u8_t forceSetFreq)
{
    u8_t setFreqImmed = 0;
    u8_t initRF = 0;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zm_msg1_scan(ZM_LV_1, "Freq=", frequency);

    zmw_enter_critical_section(dev);
    if ((wd->sta.currentFrequency == frequency)
        && (wd->sta.currentBw40 == bw40)
        && (wd->sta.currentExtOffset == extOffset))
    {
        if ( forceSetFreq == 0 && wd->sta.flagFreqChanging == 0 )
        {
            goto done;
        }
    }
#ifdef ZM_FB50
    /*if(frequency!=2437) {
        zmw_leave_critical_section(dev);
        return;
    }*/
#endif

    zfAddFreqChangeReq(dev, frequency, bw40, extOffset, cb);

//    zm_assert( wd->sta.flagFreqChanging == 0 );
    //wd->sta.flagFreqChanging = 1;
    if ( wd->sta.flagFreqChanging == 0 )
    {
        if ((wd->sta.currentBw40 != bw40) || (wd->sta.currentExtOffset != extOffset))
        {
            initRF = 1;
        }
        wd->sta.currentFrequency = frequency;
        wd->sta.currentBw40 = bw40;
        wd->sta.currentExtOffset = extOffset;
        setFreqImmed = 1;
    }
    wd->sta.flagFreqChanging++;

    zmw_leave_critical_section(dev);

    if ( setFreqImmed )
    {
        //zfHpSetFrequency(dev, frequency, 0);
        if ( forceSetFreq )
        { // Cold reset to reset the frequency after scanning !
            zm_debug_msg0("#6_1 20070917");
            zm_debug_msg0("It is happen!!! No error message");
            zfHpSetFrequencyEx(dev, frequency, bw40, extOffset, 2);
        }
        else
        {
        zfHpSetFrequencyEx(dev, frequency, bw40, extOffset, initRF);
        }

        if (    zfStaIsConnected(dev)
             && (frequency == wd->frequency)) {
            wd->sta.connPowerInHalfDbm = zfHpGetTransmitPower(dev);
        }
    }
    return;

done:
    zmw_leave_critical_section(dev);

    if ( cb != NULL )
    {
        cb(dev);
    }
    zfPushVtxq(dev);
    return;
}

void zfCoreSetFrequencyEx(zdev_t* dev, u16_t frequency, u8_t bw40,
        u8_t extOffset, zfpFreqChangeCompleteCb cb)
{
    zfCoreSetFrequencyExV2(dev, frequency, bw40, extOffset, cb, 0);
}

void zfCoreSetFrequency(zdev_t* dev, u16_t frequency)
{
    zfCoreSetFrequencyV2(dev, frequency, NULL);
}

/* zfRemoveFreqChangeReq SHOULD NOT be called inside the critical section */
static void zfRemoveFreqChangeReq(zdev_t* dev)
{
    zfpFreqChangeCompleteCb cb = NULL;
    u16_t frequency;
    u8_t bw40;
    u8_t extOffset;
    u16_t compFreq = 0;
    u8_t compBw40 = 0;
    u8_t compExtOffset = 0;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if (wd->freqCtrl.freqReqQueueHead != wd->freqCtrl.freqReqQueueTail)
    {
        zm_msg1_scan(ZM_LV_1, "Freq=",
                wd->freqCtrl.freqReqQueue[wd->freqCtrl.freqReqQueueHead]);
        compFreq = wd->freqCtrl.freqReqQueue[wd->freqCtrl.freqReqQueueHead];
        compBw40 = wd->freqCtrl.freqReqBw40[wd->freqCtrl.freqReqQueueHead];
        compExtOffset = wd->freqCtrl.freqReqExtOffset[wd->freqCtrl.freqReqQueueHead];

        wd->freqCtrl.freqReqQueue[wd->freqCtrl.freqReqQueueHead] = 0;
        cb = wd->freqCtrl.freqChangeCompCb[wd->freqCtrl.freqReqQueueHead];
        wd->freqCtrl.freqReqQueueHead++;
        if ( wd->freqCtrl.freqReqQueueHead >= ZM_MAX_FREQ_REQ_QUEUE )
        {
            wd->freqCtrl.freqReqQueueHead = 0;
        }
    }
    zmw_leave_critical_section(dev);

    if ( cb != NULL )
    {
        cb(dev);
    }

    zmw_enter_critical_section(dev);
    while (wd->freqCtrl.freqReqQueue[wd->freqCtrl.freqReqQueueHead] != 0)
    {
        frequency = wd->freqCtrl.freqReqQueue[wd->freqCtrl.freqReqQueueHead];
        bw40 = wd->freqCtrl.freqReqBw40[wd->freqCtrl.freqReqQueueHead];
        extOffset=wd->freqCtrl.freqReqExtOffset[wd->freqCtrl.freqReqQueueHead];
        if ((compFreq == frequency)
            && (compBw40 == bw40)
            && (compExtOffset == extOffset))
        {
            /* Duplicated frequency command */
            zm_msg1_scan(ZM_LV_1, "Duplicated Freq=", frequency);

            cb = wd->freqCtrl.freqChangeCompCb[wd->freqCtrl.freqReqQueueHead];
            wd->freqCtrl.freqReqQueue[wd->freqCtrl.freqReqQueueHead] = 0;
            wd->freqCtrl.freqReqQueueHead++;

            if ( wd->freqCtrl.freqReqQueueHead >= ZM_MAX_FREQ_REQ_QUEUE )
            {
                wd->freqCtrl.freqReqQueueHead = 0;
            }

            if ( wd->sta.flagFreqChanging != 0 )
            {
                wd->sta.flagFreqChanging--;
            }

            zmw_leave_critical_section(dev);
            if ( cb != NULL )
            {
                cb(dev);
            }
            zmw_enter_critical_section(dev);
        }
        else
        {
            u8_t    initRF = 0;
            if ((wd->sta.currentBw40 != bw40) || (wd->sta.currentExtOffset != extOffset))
            {
                initRF = 1;
            }
            wd->sta.currentFrequency = frequency;
            wd->sta.currentBw40 = bw40;
            wd->sta.currentExtOffset = extOffset;
            zmw_leave_critical_section(dev);

            zfHpSetFrequencyEx(dev, frequency, bw40, extOffset, initRF);
            if (    zfStaIsConnected(dev)
                && (frequency == wd->frequency)) {
                wd->sta.connPowerInHalfDbm = zfHpGetTransmitPower(dev);
            }

            return;
        }
    }
    zmw_leave_critical_section(dev);

    return;
}

void zfCoreSetFrequencyComplete(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zm_msg1_scan(ZM_LV_1, "flagFreqChanging=", wd->sta.flagFreqChanging);

    zmw_enter_critical_section(dev);
    //wd->sta.flagFreqChanging = 0;
    if ( wd->sta.flagFreqChanging != 0 )
    {
        wd->sta.flagFreqChanging--;
    }

    zmw_leave_critical_section(dev);

    zfRemoveFreqChangeReq(dev);

    zfPushVtxq(dev);
    return;
}

void zfReSetCurrentFrequency(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    zm_debug_msg0("It is happen!!! No error message");

    zfCoreSetFrequencyExV2(dev, wd->frequency, 0, 0, NULL, 1);
}
