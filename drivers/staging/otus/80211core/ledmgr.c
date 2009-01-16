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

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfLedCtrlType1              */
/*      Traditional single-LED state                                    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.6      */
/*                                                                      */
/************************************************************************/
// bit 15-12 : Toff for Scan state
//     11-8 : Ton for Scan state
//     7 : Reserved
//     6 : mode
//--------------------------------------
//     bit 6 = 0
//     5-4 : Connect state
//           00 => always off
//           01 => always on
//           10 => Idle off, acitve on
//           11 => Idle on, active off
//--------------------------------------
//     bit 6 = 1
//     5-4 : freq
//           00 => 1Hz
//           01 => 0.5Hz
//           10 => 0.25Hz
//           11 => 0.125Hz
//--------------------------------------
//     3 : Power save state
//         0 => always off in power save state
//         1 => works as connect state
//     2 : Disable state
//     1 : Reserved
//     0 : Power-on state
void zfLedCtrlType1(zdev_t* dev)
{
    u16_t i;
    u32_t ton, toff, tmp, period;
    zmw_get_wlan_dev(dev);

    for (i=0; i<ZM_MAX_LED_NUMBER; i++)
    {
        if (zfStaIsConnected(dev) != TRUE)
        {
            //Scan state
            ton = ((wd->ledStruct.ledMode[i] & 0xf00) >> 8) * 5;
            toff = ((wd->ledStruct.ledMode[i] & 0xf000) >> 12) * 5;

            if ((ton + toff) != 0)
            {
                tmp = wd->ledStruct.counter / (ton+toff);
                tmp = wd->ledStruct.counter - (tmp * (ton+toff));
                if (tmp < ton)
                {
                    zfHpLedCtrl(dev, i, 1);
                }
                else
                {
                    zfHpLedCtrl(dev, i, 0);
                }
            }
        }
        else
        {
            if ((zfPowerSavingMgrIsSleeping(dev)) && ((wd->ledStruct.ledMode[i] & 0x8) == 0))
            {
                zfHpLedCtrl(dev, i, 0);
            }
            else
            {
                //Connect state
                if ((wd->ledStruct.ledMode[i] & 0x40) == 0)
                {
                    if ((wd->ledStruct.counter & 1) == 0)
                    {
                        zfHpLedCtrl(dev, i, (wd->ledStruct.ledMode[i] & 0x10) >> 4);
                    }
                    else
                    {
                        if ((wd->ledStruct.txTraffic > 0) || (wd->ledStruct.rxTraffic > 0))
                        {
                            wd->ledStruct.txTraffic = wd->ledStruct.rxTraffic = 0;
                            if ((wd->ledStruct.ledMode[i] & 0x20) != 0)
                            {
                                zfHpLedCtrl(dev, i, ((wd->ledStruct.ledMode[i] & 0x10) >> 4)^1);
                            }
                        }
                    }
                }// if ((wd->ledStruct.ledMode[i] & 0x40) == 0)
                else
                {
                    period = 5 * (1 << ((wd->ledStruct.ledMode[i] & 0x30) >> 4));
                    tmp = wd->ledStruct.counter / (period*2);
                    tmp = wd->ledStruct.counter - (tmp * (period*2));
                    if (tmp < period)
                    {
                        if ((wd->ledStruct.counter & 1) == 0)
                        {
                            zfHpLedCtrl(dev, i, 0);
                        }
                        else
                        {
                            if ((wd->ledStruct.txTraffic > 0) || (wd->ledStruct.rxTraffic > 0))
                            {
                                wd->ledStruct.txTraffic = wd->ledStruct.rxTraffic = 0;
                                zfHpLedCtrl(dev, i, 1);
                            }
                        }
                    }
                    else
                    {
                        if ((wd->ledStruct.counter & 1) == 0)
                        {
                            zfHpLedCtrl(dev, i, 1);
                        }
                        else
                        {
                            if ((wd->ledStruct.txTraffic > 0) || (wd->ledStruct.rxTraffic > 0))
                            {
                                wd->ledStruct.txTraffic = wd->ledStruct.rxTraffic = 0;
                                zfHpLedCtrl(dev, i, 0);
                            }
                        }
                    }
                } //else, if ((wd->ledStruct.ledMode[i] & 0x40) == 0)
            } //else, if (zfPowerSavingMgrIsSleeping(dev))
        } //else : if (zfStaIsConnected(dev) != TRUE)
    } //for (i=0; i<ZM_MAX_LED_NUMBER; i++)
}

/******************************************************************************/
/*                                                                            */
/*    FUNCTION DESCRIPTION                  zfLedCtrlType2                    */
/*      Customize for Netgear Dual-LED state ((bug#31292))                    */
/*                                                                            */
/*      1. Status:  When dongle does not connect to 2.4G or 5G but in site    */
/*                  survey/association                                        */
/*         LED status: Slow blinking, Amber then Blue per 500ms               */
/*      2. Status:	Connection at 2.4G in site survey/association             */
/*         LED status: Slow blinking, Amber/off per 500ms                     */
/*      3. Status:	Connection at 5G in site survey/association               */
/*         LED status: Slow blinking, Blue/off per 500ms                      */
/*      4. Status:	When transfer the packet                                  */
/*         LED status: Blink per packet, including TX and RX                  */
/*      5. Status:	When linking is established but no traffic                */
/*         LED status: Always on                                              */
/*      6. Status:	When linking is dropped but no re-connection              */
/*         LED status: Always off                                             */
/*      7. Status:	From one connection(2.4G or 5G) to change to another band */
/*         LED status: Amber/Blue =>Slow blinking, Amber then Blue per 500ms  */
/*                                                                            */
/*    INPUTS                                                                  */
/*      dev : device pointer                                                  */
/*                                                                            */
/*    OUTPUTS                                                                 */
/*      None                                                                  */
/*                                                                            */
/*    AUTHOR                                                                  */
/*      Shang-Chun Liu        Atheros Communications, INC.    2007.11         */
/*                                                                            */
/******************************************************************************/
void zfLedCtrlType2_scan(zdev_t* dev);

void zfLedCtrlType2(zdev_t* dev)
{
    u32_t ton, toff, tmp, period;
    u16_t OperateLED;
    zmw_get_wlan_dev(dev);

    if (zfStaIsConnected(dev) != TRUE)
    {
        // Disconnect state
        if(wd->ledStruct.counter % 4 != 0)
    	{
      	    // Update LED each 400ms(4*100)
      	    // Prevent this situation
            //              _______         ___
            // LED[0] ON   |       |       | x |
            // ------ OFF->+-+-+-+-+-+-+-+-+-+-+-+->>>...
            // LED[1] ON
            //
            return;
        }

        if (((wd->state == ZM_WLAN_STATE_DISABLED) && (wd->sta.bChannelScan))
            || ((wd->state != ZM_WLAN_STATE_DISABLED) && (wd->sta.bAutoReconnect)))
        {
            // Scan/AutoReconnect state
            zfLedCtrlType2_scan(dev);
        }
        else
        {
            // Neither Connected nor Scan
            zfHpLedCtrl(dev, 0, 0);
            zfHpLedCtrl(dev, 1, 0);
        }
    }
    else
    {
        if( wd->sta.bChannelScan )
        {
            // Scan state
            if(wd->ledStruct.counter % 4 != 0)
                return;
            zfLedCtrlType2_scan(dev);
            return;
        }

        if(wd->frequency < 3000)
        {
            OperateLED = 0;     // LED[0]: work on 2.4G (b/g band)
            zfHpLedCtrl(dev, 1, 0);
        }
        else
        {
            OperateLED = 1;     // LED[1]: work on 5G (a band)
            zfHpLedCtrl(dev, 0, 0);
        }

        if ((zfPowerSavingMgrIsSleeping(dev)) && ((wd->ledStruct.ledMode[OperateLED] & 0x8) == 0))
        {
            // If Sleeping, turn OFF
            zfHpLedCtrl(dev, OperateLED, 0);
        }
        else
        {
            //Connect state
            if ((wd->ledStruct.counter & 1) == 0)   // even
            {
                // No traffic, always ON
                zfHpLedCtrl(dev, OperateLED, 1);
            }
            else       // odd
            {
                if ((wd->ledStruct.txTraffic > 0) || (wd->ledStruct.rxTraffic > 0))
                {
                    // If have traffic, turn OFF
		            //                   _____   _   _   _   _____
		            // LED[Operate] ON        | | | | | | | |
		            // ------------ OFF->-+-+-+-+-+-+-+-+-+-+-+-+-+->>>...
		            //
                    wd->ledStruct.txTraffic = wd->ledStruct.rxTraffic = 0;
                    zfHpLedCtrl(dev, OperateLED, 0);
                }
            }
        }
    }
}

void zfLedCtrlType2_scan(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    // When doing scan, blink(Amber/Blue) and off per 500ms (about 400ms in our driver)
    //               _______                         _______
    // LED[0] ON    |       |       8       12      |       |
    // ------ OFF->-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+->>>...
    // LED[1] ON    0       4       |_______|       0       3
    //

    switch(wd->ledStruct.counter % 16)
    {
        case 0:   // case 0~3, LED[0] on
            if(wd->supportMode & ZM_WIRELESS_MODE_24)
            {
                zfHpLedCtrl(dev, 0, 1);
                zfHpLedCtrl(dev, 1, 0);
            }
            else
            {
                zfHpLedCtrl(dev, 1, 1);
                zfHpLedCtrl(dev, 0, 0);
            }
            break;

        case 8:   // case 8~11, LED[1] on
            if(wd->supportMode & ZM_WIRELESS_MODE_5)
            {
                zfHpLedCtrl(dev, 1, 1);
                zfHpLedCtrl(dev, 0, 0);
            }
            else
            {
                zfHpLedCtrl(dev, 0, 1);
                zfHpLedCtrl(dev, 1, 0);
            }
            break;

        default:  // others, all off
            zfHpLedCtrl(dev, 0, 0);
            zfHpLedCtrl(dev, 1, 0);
            break;
    }
}

/**********************************************************************************/
/*                                                                                */
/*    FUNCTION DESCRIPTION                  zfLedCtrlType3                        */
/*      Customize for Netgear Single-LED state ((bug#32243))                      */
/*                                                                                */
/*  ¡EOff: when the adapter is disabled or hasn't started to associate with AP    */
/*         yet.                                                                          */
/*  ¡EOn: Once adpater associate with AP successfully                             */
/*  ¡ESlow blinking: whenever adapters do site-survey or try to associate with AP */
/*    - If there is a connection already, and adapters do site-survey or          */
/*      re-associate action, the LED should keep LED backgraoud as ON, thus       */
/*      the blinking behavior SHOULD be OFF (200ms) - ON (800ms) and continue this*/
/*      cycle.                                                                    */
/*    - If there is no connection yet, and adapters start to do site-survey or    */
/*      associate action, the LED should keep LED background as OFF, thus the     */
/*      blinking behavior SHOULD be ON (200ms) - OFF (800ms) and continue this    */
/*      cycle.                                                                    */
/*    - For the case that associate fail, adpater should keep associating, and the*/
/*      LED should also keep slow blinking.                                       */
/*  ¡EQuick blinking: to blink OFF-ON cycle for each time that traffic packet is  */
/*    received or is transmitted.                                                 */
/*                                                                                */
/*    INPUTS                                                                      */
/*      dev : device pointer                                                      */
/*                                                                                */
/*    OUTPUTS                                                                     */
/*      None                                                                      */
/*                                                                                */
/*    AUTHOR                                                                      */
/*      Shang-Chun Liu        Atheros Communications, INC.    2008.01             */
/*                                                                                */
/**********************************************************************************/
void zfLedCtrlType3_scan(zdev_t* dev, u16_t isConnect);

void zfLedCtrlType3(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    if (zfStaIsConnected(dev) != TRUE)
    {
        // Disconnect state
        if(wd->ledStruct.counter % 2 != 0)
    	{
      	    // Update LED each 200ms(2*100)
      	    // Prevent this situation
            //              ___     _
            // LED[0] ON   |   |   |x|
            // ------ OFF->+-+-+-+-+-+-+->>>...
            //
            return;
        }

        if (((wd->state == ZM_WLAN_STATE_DISABLED) && (wd->sta.bChannelScan))
            || ((wd->state != ZM_WLAN_STATE_DISABLED) && (wd->sta.bAutoReconnect)))
        {
            // Scan/AutoReconnect state
            zfLedCtrlType3_scan(dev, 0);
        }
        else
        {
            // Neither Connected nor Scan
            zfHpLedCtrl(dev, 0, 0);
            zfHpLedCtrl(dev, 1, 0);
        }
    }
    else
    {
        if( wd->sta.bChannelScan )
        {
            // Scan state
            if(wd->ledStruct.counter % 2 != 0)
                return;
            zfLedCtrlType3_scan(dev, 1);
            return;
        }

        if ((zfPowerSavingMgrIsSleeping(dev)) && ((wd->ledStruct.ledMode[0] & 0x8) == 0))
        {
            // If Sleeping, turn OFF
            zfHpLedCtrl(dev, 0, 0);
            zfHpLedCtrl(dev, 1, 0);
        }
        else
        {
            //Connect state
            if ((wd->ledStruct.counter & 1) == 0)   // even
            {
                // No traffic, always ON
                zfHpLedCtrl(dev, 0, 1);
                zfHpLedCtrl(dev, 1, 1);
            }
            else       // odd
            {
                if ((wd->ledStruct.txTraffic > 0) || (wd->ledStruct.rxTraffic > 0))
                {
                    // If have traffic, turn OFF
		            //                   _____   _   _   _   _____
		            // LED[Operate] ON        | | | | | | | |
		            // ------------ OFF->-+-+-+-+-+-+-+-+-+-+-+-+-+->>>...
		            //
                    wd->ledStruct.txTraffic = wd->ledStruct.rxTraffic = 0;
                    zfHpLedCtrl(dev, 0, 0);
                    zfHpLedCtrl(dev, 1, 0);
                }
            }
        }
    }
}

void zfLedCtrlType3_scan(zdev_t* dev, u16_t isConnect)
{
    u32_t ton, toff, tmp;
    zmw_get_wlan_dev(dev);

    // Doing scan when :
    // 1. Disconnected: ON (200ms) - OFF (800ms) (200ms-600ms in our driver)
    //               ___             ___             ___
    // LED[0] ON    |   |           |   |           |   |
    // ------ OFF->-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+->>>...
    //              0   2   4   6   8  10  12  14  16
    // 2. Connected:   ON (800ms) - OFF (200ms) (600ms-200ms in our driver)
    //               ___________     ___________     ______
    // LED[0] ON    |           |   |           |   |
    // ------ OFF->-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+->>>...
    //              0   2   4   6   8  10  12  14  16

    //Scan state
    if(!isConnect)
        ton = 2, toff = 6;
    else
        ton = 6, toff = 2;

    if ((ton + toff) != 0)
    {
        tmp = wd->ledStruct.counter % (ton+toff);
       if (tmp < ton)
        {
            zfHpLedCtrl(dev, 0, 1);
            zfHpLedCtrl(dev, 1, 1);
        }
        else
        {
            zfHpLedCtrl(dev, 0, 0);
            zfHpLedCtrl(dev, 1, 0);
        }
    }
}

/******************************************************************************/
/*                                                                            */
/*    FUNCTION DESCRIPTION                  zfLedCtrl_BlinkWhenScan_Alpha     */
/*      Customize for Alpha/DLink LED                                         */
/*      - Blink LED 12 times within 3 seconds when doing Active Scan          */
/*	                      ___   ___   ___   ___                               */
/*	      LED[0] ON      |   | |   | |   | |   |                              */
/*	      -------OFF->-+-+-+-+-+-+-+-+-+-+-+-+-+--+-->>>...                   */
/*                                                                            */
/*    INPUTS                                                                  */
/*      dev : device pointer                                                  */
/*                                                                            */
/*    OUTPUTS                                                                 */
/*      None                                                                  */
/*                                                                            */
/*    AUTHOR                                                                  */
/*      Shang-Chun Liu        Atheros Communications, INC.    2007.11         */
/*                                                                            */
/******************************************************************************/
void zfLedCtrl_BlinkWhenScan_Alpha(zdev_t* dev)
{
    static u32_t counter = 0;
    zmw_get_wlan_dev(dev);

    if(counter > 34)        // counter for 3 sec
    {
        wd->ledStruct.LEDCtrlFlag &= ~(u8_t)ZM_LED_CTRL_FLAG_ALPHA;
        counter = 0;
    }

    if( (counter % 3) < 2)
        zfHpLedCtrl(dev, 0, 1);
    else
        zfHpLedCtrl(dev, 0, 0);

    counter++;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfLed100msCtrl              */
/*      LED 100 milliseconds timer.                                     */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.6      */
/*                                                                      */
/************************************************************************/
void zfLed100msCtrl(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);

    wd->ledStruct.counter++;

    if(wd->ledStruct.LEDCtrlFlag)
    {
        switch(wd->ledStruct.LEDCtrlFlag) {
        case ZM_LED_CTRL_FLAG_ALPHA:
            zfLedCtrl_BlinkWhenScan_Alpha(dev);
        break;
        }
    }
    else
    {
        switch(wd->ledStruct.LEDCtrlType) {
        case 1:			// Traditional 1 LED
            zfLedCtrlType1(dev);
        break;

        case 2:			// Dual-LEDs for Netgear
            zfLedCtrlType2(dev);
        break;

        case 3:			// Single-LED for Netgear (WN111v2)
            zfLedCtrlType3(dev);
        break;

        default:
            zfLedCtrlType1(dev);
        break;
        }
    }
}

