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
/*  Module Name : mm.c                                                  */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains common functions for handle management     */
/*      frame.                                                          */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"
#include "../hal/hpreg.h"

/* TODO : put all constant tables to a file */
const u8_t zg11bRateTbl[4] = {2, 4, 11, 22};
const u8_t zg11gRateTbl[8] = {12, 18, 24, 36, 48, 72, 96, 108};

/* 0xff => element does not exist */
const u8_t zgElementOffsetTable[] =
{
    4,      /*  0 : asoc req */
    6,      /*  1 : asoc rsp */
    10,     /*  2 : reasoc req*/
    6,      /*  3 : reasoc rsp */
    0,      /*  4 : probe req */
    12,     /*  5 : probe rsp */
    0xff,   /*  6 : reserved */
    0xff,   /*  7 : reserved */
    12,     /*  8 : beacon */
    4,      /*  9 : ATIM */
    0xff,   /* 10 : disasoc */
    6,      /* 11 : auth */
    0xff,   /* 12 : deauth */
    4,      /* 13 : action */
    0xff,   /* 14 : reserved */
    0xff,   /* 15 : reserved */
};

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfFindElement               */
/*      Find a specific element in management frame                     */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : management frame buffer                                   */
/*      eid : target element id                                         */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      byte offset of target element                                   */
/*      or 0xffff if not found                                          */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfFindElement(zdev_t* dev, zbuf_t* buf, u8_t eid)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id, HTEid=0;
    u8_t oui[4] = {0x00, 0x50, 0xf2, 0x01};
    u8_t oui11n[3] = {0x00,0x90,0x4C};
    u8_t HTType = 0;

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);
    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    // jhlee HT 0

    if ((eid == ZM_WLAN_EID_HT_CAPABILITY) ||
        (eid == ZM_WLAN_EID_EXTENDED_HT_CAPABILITY))
    {
        HTEid = eid;
        eid = ZM_WLAN_EID_WPA_IE;
        HTType = 1;
    }


    bufLen = zfwBufGetSize(dev, buf);
    /* Search loop */
    while ((offset+2)<bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == eid)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 && eid != ZM_WLAN_EID_SSID)
            {
                /* Element length error */
                return 0xffff;
            }

            if ( eid == ZM_WLAN_EID_WPA_IE )
            {
                /* avoid sta to be thought use 11n when find a WPA_IE */
                if ( (HTType == 0) && zfRxBufferEqualToStr(dev, buf, oui, offset+2, 4) )
                {
                    return offset;
                }

                // jhlee HT 0
                // CWYang(+)

                if ((HTType == 1) && ( zfRxBufferEqualToStr(dev, buf, oui11n, offset+2, 3) ))
                {
                    if ( zmw_rx_buf_readb(dev, buf, offset+5) == HTEid )
                    {
                        return offset + 5;
                    }
                }

            }
            else
            {
                return offset;
            }
        }
        /* Advance to next element */
        #if 1
        elen = zmw_rx_buf_readb(dev, buf, offset+1);
        #else
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }
        #endif

        offset += (elen+2);
    }
    return 0xffff;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfFindWifiElement           */
/*      Find a specific Wifi element in management frame                */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : management frame buffer                                   */
/*      type : OUI type                                                 */
/*      subType : OUI subtype                                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      byte offset of target element                                   */
/*      or 0xffff if not found                                          */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.1      */
/*                                                                      */
/************************************************************************/
u16_t zfFindWifiElement(zdev_t* dev, zbuf_t* buf, u8_t type, u8_t subtype)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t tmp;

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);

    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);
    /* Search loop */
    while ((offset+2)<bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == ZM_WLAN_EID_WIFI_IE)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if (((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x00)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+3)) == 0x50)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+4)) == 0xF2)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+5)) == type))

            {
                if ( subtype != 0xff )
                {
                    if ( (tmp = zmw_rx_buf_readb(dev, buf, offset+6)) == subtype  )
                    {
                        return offset;
                    }
                }
                else
                {
                    return offset;
                }
            }
        }
        /* Advance to next element */
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }
        offset += (elen+2);
    }
    return 0xffff;
}

u16_t zfRemoveElement(zdev_t* dev, u8_t* buf, u16_t size, u8_t eid)
{
    u16_t offset = 0;
    u16_t elen;
    u8_t  HTEid = 0;
    u8_t  oui[4] = {0x00, 0x50, 0xf2, 0x01};
    u8_t  oui11n[3] = {0x00,0x90,0x4C};
    u8_t  HTType = 0;

    if ((eid == ZM_WLAN_EID_HT_CAPABILITY) ||
        (eid == ZM_WLAN_EID_EXTENDED_HT_CAPABILITY))
    {
        HTEid = eid;
        eid = ZM_WLAN_EID_WPA_IE;
        HTType = 1;
    }

    while (offset < size)
    {
        elen = *(buf+offset+1);

        if (*(buf+offset) == eid)
        {
            if ( eid == ZM_WLAN_EID_WPA_IE )
            {
                if ( (HTType == 0)
                     && (*(buf+offset+2) == oui[0])
                     && (*(buf+offset+3) == oui[1])
                     && (*(buf+offset+4) == oui[2])
                     && (*(buf+offset+5) == oui[3]) )
                {
                    zfMemoryMove(buf+offset, buf+offset+elen+2, size-offset-elen-2);
                    return (size-elen-2);
                }

                if ( (HTType == 1)
                    && (*(buf+offset+2) == oui11n[0])
                    && (*(buf+offset+3) == oui11n[1])
                    && (*(buf+offset+4) == oui11n[2])
                    && (*(buf+offset+5) == HTEid) )
                {
                    zfMemoryMove(buf+offset, buf+offset+elen+2, size-offset-elen-2);
                    return (size-elen-2);
                }
            }
            else
            {
                zfMemoryMove(buf+offset, buf+offset+elen+2, size-offset-elen-2);
                return (size-elen-2);
            }
        }

        offset += (elen+2);
    }

    return size;
}

u16_t zfUpdateElement(zdev_t* dev, u8_t* buf, u16_t size, u8_t* updateeid)
{
    u16_t offset = 0;
    u16_t elen;

    while (offset < size) {
        elen = *(buf+offset+1);

        if (*(buf+offset) == updateeid[0]) {
            if (updateeid[1] <= elen) {
                zfMemoryMove(buf+offset, updateeid, updateeid[1]+2);
                zfMemoryMove(buf+offset+updateeid[1]+2, buf+offset+elen+2, size-offset-elen-2);

                return size-(elen-updateeid[1]);
            } else {
                zfMemoryMove(buf+offset+updateeid[1]+2, buf+offset+elen+2, size-offset-elen-2);
                zfMemoryMove(buf+offset, updateeid, updateeid[1]+2);

                return size+(updateeid[1]-elen);
            }
        }

        offset += (elen+2);
    }

    return size;
}

u16_t zfFindSuperGElement(zdev_t* dev, zbuf_t* buf, u8_t type)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t super_feature;
    u8_t ouiSuperG[6] = {0x00,0x03,0x7f,0x01, 0x01, 0x00};

    zmw_get_wlan_dev(dev);

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);
    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);
    /* Search loop */
    while ((offset+2)<bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == ZM_WLAN_EID_VENDOR_PRIVATE)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if (zfRxBufferEqualToStr(dev, buf, ouiSuperG, offset+2, 6) && ( zmw_rx_buf_readb(dev, buf, offset+1) >= 6))
            {
                /* super_feature 0:useFastFrame, 1:useCompression, 2:useTurboPrime */
                super_feature= zmw_rx_buf_readb(dev, buf, offset+8);
                if ((super_feature & 0x01) || (super_feature & 0x02) || (super_feature & 0x04))
                {
                    return offset;
                }
            }
        }
        /* Advance to next element */
        #if 1
        elen = zmw_rx_buf_readb(dev, buf, offset+1);
        #else
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }
        #endif

        offset += (elen+2);
    }
    return 0xffff;
}

u16_t zfFindXRElement(zdev_t* dev, zbuf_t* buf, u8_t type)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t ouixr[6] = {0x00,0x03,0x7f,0x03, 0x01, 0x00};

    zmw_get_wlan_dev(dev);

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);
    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);
    /* Search loop */
    while ((offset+2)<bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == ZM_WLAN_EID_VENDOR_PRIVATE)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if (zfRxBufferEqualToStr(dev, buf, ouixr, offset+2, 6) && ( zmw_rx_buf_readb(dev, buf, offset+1) >= 6))
            {
                return offset;
            }
        }
        /* Advance to next element */
        #if 1
        elen = zmw_rx_buf_readb(dev, buf, offset+1);
        #else
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }
        #endif

        offset += (elen+2);
    }
    return 0xffff;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfMmAddIeSupportRate        */
/*      Add information element Support Rate to buffer.                 */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*      eid : element ID                                                */
/*      rateSet :  CCK or OFDM                                          */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfMmAddIeSupportRate(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t eid, u8_t rateSet)
{
    u8_t len = 0;
    u16_t i;

    zmw_get_wlan_dev(dev);

    //if ( (rateSet == ZM_RATE_SET_OFDM)&&((wd->gRate & 0xff) == 0) )
    //{
    //    return offset;
    //}

    /* Information : Support Rate */
    if ( rateSet == ZM_RATE_SET_CCK )
    {
        for (i=0; i<4; i++)
        {
            if ((wd->bRate & (0x1<<i)) == (0x1<<i))
            //if ((0xf & (0x1<<i)) == (0x1<<i))
            {
                zmw_tx_buf_writeb(dev, buf, offset+len+2,
                                     zg11bRateTbl[i]+((wd->bRateBasic & (0x1<<i))<<(7-i)));
                len++;
            }
        }
    }
    else if ( rateSet == ZM_RATE_SET_OFDM )
    {
        for (i=0; i<8; i++)
        {
            if ((wd->gRate & (0x1<<i)) == (0x1<<i))
            //if ((0xff & (0x1<<i)) == (0x1<<i))
            {
                zmw_tx_buf_writeb(dev, buf, offset+len+2,
                                     zg11gRateTbl[i]+((wd->gRateBasic & (0x1<<i))<<(7-i)));
                len++;
            }
        }
    }

    if (len > 0)
    {
        /* Element ID */
        zmw_tx_buf_writeb(dev, buf, offset, eid);

        /* Element Length */
        zmw_tx_buf_writeb(dev, buf, offset+1, len);

        /* Return value */
        offset += (2+len);
    }

    return offset;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfMmAddIeDs                 */
/*      Add information element DS to buffer.                           */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfMmAddIeDs(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    zmw_get_wlan_dev(dev);

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_DS);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, 1);

    /* Information : DS */
    zmw_tx_buf_writeb(dev, buf, offset++,
                         zfChFreqToNum(wd->frequency, NULL));

    return offset;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfMmAddIeErp                */
/*      Add information element ERP to buffer.                          */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfMmAddIeErp(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    zmw_get_wlan_dev(dev);

    /* Element ID */
    zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_ERP);

    /* Element Length */
    zmw_tx_buf_writeb(dev, buf, offset++, 1);

    /* Information : ERP */
    zmw_tx_buf_writeb(dev, buf, offset++, wd->erpElement);

    return offset;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfMmAddIeWpa                */
/*      Add information element WPA to buffer.                          */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Yuan-Gu Wei         ZyDAS Technology Corporation    2006.2      */
/*                                                                      */
/************************************************************************/
u16_t zfMmAddIeWpa(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t apId)
{
    //struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev);
    int i;

    zmw_get_wlan_dev(dev);

    /* Element ID */
    //zmw_inttx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_WPA_IE);

    /* Element Length */
    //zmw_inttx_buf_writeb(dev, buf, offset++, wd->ap.wpaLen);
    for(i = 0; i < wd->ap.wpaLen[apId]; i++)
    {
        /* Information : WPA */
        zmw_tx_buf_writeb(dev, buf, offset++, wd->ap.wpaIe[apId][i]);
    }

    return offset;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfMmAddHTCapability         */
/*      Add HT Capability Infomation to buffer.                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Chao-Wen Yang     ZyDAS Technology Corporation       2006.06    */
/*                                                                      */
/************************************************************************/
u16_t zfMmAddHTCapability(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    u8_t OUI[3] = {0x0,0x90,0x4C};
    u16_t i;

    zmw_get_wlan_dev(dev);

    /* Prob ID */
    zmw_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_WPA_IE);

    if ( wd->wlanMode == ZM_MODE_AP )
    {
        /* Element Length */
        zmw_buf_writeb(dev, buf, offset++, wd->ap.HTCap.Data.Length + 4);

        /* OUI Data */
        for (i = 0; i < 3; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, OUI[i]);
        }

        /* Element Type ID */
        zmw_buf_writeb(dev, buf, offset++, wd->ap.HTCap.Data.ElementID);

        /* HT Capability Data */
        for (i = 0; i < 26; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, wd->ap.HTCap.Byte[i+2]);
        }
    }
    else
    {
        /* Element Length */
        zmw_buf_writeb(dev, buf, offset++, wd->sta.HTCap.Data.Length + 4);

        /* OUI Data */
        for (i = 0; i < 3; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, OUI[i]);
        }

        /* Element Type ID */
        zmw_buf_writeb(dev, buf, offset++, wd->sta.HTCap.Data.ElementID);

        /* HT Capability Data */
        for (i = 0; i < 26; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, wd->sta.HTCap.Byte[i+2]);
        }
    }

    return offset;
}


u16_t zfMmAddPreNHTCapability(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    //u8_t OUI[3] = {0x0,0x90,0x4C};
    u16_t i;

    zmw_get_wlan_dev(dev);

    /* Prob ID */
    zmw_buf_writeb(dev, buf, offset++, ZM_WLAN_PREN2_EID_HTCAPABILITY);

    if ( wd->wlanMode == ZM_MODE_AP )
    {
        /* Element Length */
        zmw_buf_writeb(dev, buf, offset++, wd->ap.HTCap.Data.Length);

        /* HT Capability Data */
        for (i = 0; i < 26; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, wd->ap.HTCap.Byte[i+2]);
        }
    }
    else
    {
        /* Element Length */
        zmw_buf_writeb(dev, buf, offset++, wd->sta.HTCap.Data.Length);

        /* HT Capability Data */
        for (i = 0; i < 26; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, wd->sta.HTCap.Byte[i+2]);
        }
    }

    return offset;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                zfMmAddExtendedHTCapability   */
/*      Add Extended HT Capability Infomation to buffer.                */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer to add information element                         */
/*      offset : add information element from this offset               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      buffer offset after adding information element                  */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Chao-Wen Yang     ZyDAS Technology Corporation       2006.06    */
/*                                                                      */
/************************************************************************/
u16_t zfMmAddExtendedHTCapability(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    u8_t OUI[3] = {0x0,0x90,0x4C};
    u16_t i;

    zmw_get_wlan_dev(dev);

    /* Prob ID */
    zmw_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_WPA_IE);

    if ( wd->wlanMode == ZM_MODE_AP )
    {
        /* Element Length */
        zmw_buf_writeb(dev, buf, offset++, wd->ap.ExtHTCap.Data.Length + 4);

        /* OUI Data */
        for (i = 0; i < 3; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, OUI[i]);
        }

        /* Element Type ID */
        zmw_buf_writeb(dev, buf, offset++, wd->ap.ExtHTCap.Data.ElementID);

        /* HT Capability Data */
        for (i = 0; i < 22; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, wd->ap.ExtHTCap.Byte[i+2]);
        }
    }
    else
    {
        /* Element Length */
        zmw_buf_writeb(dev, buf, offset++, wd->sta.ExtHTCap.Data.Length + 4);

        /* OUI Data */
        for (i = 0; i < 3; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, OUI[i]);
        }

        /* Element Type ID */
        zmw_buf_writeb(dev, buf, offset++, wd->sta.ExtHTCap.Data.ElementID);

        /* HT Capability Data */
        for (i = 0; i < 22; i++)
        {
            zmw_buf_writeb(dev, buf, offset++, wd->sta.ExtHTCap.Byte[i+2]);
        }
    }

    return offset;
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfSendMmFrame               */
/*      Send management frame.                                          */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      frameType : management frame type                               */
/*      dst : destination MAC address                                   */
/*      p1 : parameter 1                                                */
/*      p2 : parameter 2                                                */
/*      p3 : parameter 3                                                */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
/* probe req : p1=> bWithSSID, p2=>R, p3=>R                                      */
/* probe rsp : p1=>R, p2=>R, p3=>VAP ID(AP)                             */
/* deauth : p1=>Reason Code, p2=>R, p3=>VAP ID(AP)                      */
/* Disasoc : p1=>Reason Code, p2=>R, p3=>VAP ID(AP)                     */
/* ATIM : p1=>R, p2=>R, p3=>R                                           */
/* (re)asoc rsp : p1=>Status Code, p2=>AID, p3=>VAP ID(AP)              */
/* asoc req : p1=>R, p2=>R, p3=>R                                       */
/* reasoc req : p1=>AP MAC[0], p2=>AP MAC[1], p3=>AP MAC[2]             */
/* auth : p1=>low=Algorithm, high=Transaction, p2=>Status, p3=>VAP ID   */
void zfSendMmFrame(zdev_t* dev, u8_t frameType, u16_t* dst,
                   u32_t p1, u32_t p2, u32_t p3)
{
    zbuf_t* buf;
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    u16_t offset = 0;
    u16_t hlen = 32;
    u16_t header[(24+25+1)/2];
    u16_t vap = 0;
    u16_t i;
    u8_t encrypt = 0;
    u16_t aid;

    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zm_msg2_mm(ZM_LV_2, "Send mm frame, type=", frameType);
    /* TBD : Maximum size of management frame */
    if ((buf = zfwBufAllocate(dev, 1024)) == NULL)
    {
        zm_msg0_mm(ZM_LV_0, "Alloc mm buf Fail!");
        return;
    }

    //Reserve room for wlan header
    offset = hlen;

    switch (frameType)
    {
        case ZM_WLAN_FRAME_TYPE_PROBEREQ :
            offset = zfSendProbeReq(dev, buf, offset, (u8_t) p1);
            break;

        case ZM_WLAN_FRAME_TYPE_PROBERSP :
            zm_msg0_mm(ZM_LV_3, "probe rsp");
            /* 24-31 Time Stamp : hardware WON'T fill this field */
            zmw_tx_buf_writeh(dev, buf, offset, 0);
            zmw_tx_buf_writeh(dev, buf, offset+2, 0);
            zmw_tx_buf_writeh(dev, buf, offset+4, 0);
            zmw_tx_buf_writeh(dev, buf, offset+6, 0);
            offset+=8;

            /* Beacon Interval */
            zmw_tx_buf_writeh(dev, buf, offset, wd->beaconInterval);
            offset+=2;

            if (wd->wlanMode == ZM_MODE_AP)
            {
                vap = (u16_t) p3;
                /* Capability */
                zmw_tx_buf_writeh(dev, buf, offset, wd->ap.capab[vap]);
                offset+=2;
                /* SSID */
                offset = zfApAddIeSsid(dev, buf, offset, vap);
            }
            else
            {
                /* Capability */
                zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.capability[0]);
                zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.capability[1]);
                /* SSID */
                offset = zfStaAddIeSsid(dev, buf, offset);
            }

            /* Support Rate */
            if ( wd->frequency < 3000 )
            {
            offset = zfMmAddIeSupportRate(dev, buf, offset,
                                          ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_CCK);
            }
            else
            {
                offset = zfMmAddIeSupportRate(dev, buf, offset,
                                          ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_OFDM);
            }

            /* DS parameter set */
            offset = zfMmAddIeDs(dev, buf, offset);

            /* TODO ¡G IBSS */
            if ( wd->wlanMode == ZM_MODE_IBSS )
            {
                offset = zfStaAddIeIbss(dev, buf, offset);

                if (wd->frequency < 3000)
                {
                    if( wd->wfc.bIbssGMode
                        && (wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)) )    // Only accompany with enabling a mode .
                    {
                        /* ERP Information */
                        wd->erpElement = 0;
                        offset = zfMmAddIeErp(dev, buf, offset);

                        /* Enable G Mode */
                        /* Extended Supported Rates */
                   	    offset = zfMmAddIeSupportRate(dev, buf, offset,
                                ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
            	    }
        	    }
            }


            if ((wd->wlanMode == ZM_MODE_AP)
                 && (wd->ap.wlanType[vap] != ZM_WLAN_TYPE_PURE_B))
            {
                /* ERP Information */
                offset = zfMmAddIeErp(dev, buf, offset);

                /* Extended Supported Rates */
		if ( wd->frequency < 3000 )
                {
                offset = zfMmAddIeSupportRate(dev, buf, offset,
                                   ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
            }
            }

            /* ERP Information */
            //offset = zfMmAddIeErp(dev, buf, offset);

            /* Extended Supported Rates */
            //offset = zfMmAddIeSupportRate(dev, buf, offset,
            //                              ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);

            /* TODO : RSN */
            if (wd->wlanMode == ZM_MODE_AP && wd->ap.wpaSupport[vap] == 1)
            {
                offset = zfMmAddIeWpa(dev, buf, offset, vap);
            }
            else if ( wd->wlanMode == ZM_MODE_IBSS && wd->sta.authMode == ZM_AUTH_MODE_WPA2PSK)
            {
                offset = zfwStaAddIeWpaRsn(dev, buf, offset, ZM_WLAN_FRAME_TYPE_AUTH);
            }

            /* WME Parameters */
            if (wd->wlanMode == ZM_MODE_AP)
            {
                if (wd->ap.qosMode == 1)
                {
                    offset = zfApAddIeWmePara(dev, buf, offset, vap);
                }
            }

            if ( wd->wlanMode != ZM_MODE_IBSS )
            {
            // jhlee HT 0
            //CWYang(+)
                /* TODO : Need to check if it is ok */
            /* HT Capabilities Info */
            offset = zfMmAddHTCapability(dev, buf, offset);
            //CWYang(+)
            /* Extended HT Capabilities Info */
            offset = zfMmAddExtendedHTCapability(dev, buf, offset);
            }

            if ( wd->sta.ibssAdditionalIESize )
                offset = zfStaAddIbssAdditionalIE(dev, buf, offset);
            break;

        case ZM_WLAN_FRAME_TYPE_AUTH :
            if (p1 == 0x30001)
            {
                hlen += 4;
                offset += 4;        // for reserving wep header
                encrypt = 1;
            }

            /* Algotrithm Number */
            zmw_tx_buf_writeh(dev, buf, offset, (u16_t)(p1&0xffff));
            offset+=2;

            /* Transaction Number */
            zmw_tx_buf_writeh(dev, buf, offset, (u16_t)(p1>>16));
            offset+=2;

            /* Status Code */
            zmw_tx_buf_writeh(dev, buf, offset, (u16_t)p2);
            offset+=2;

            if (wd->wlanMode == ZM_MODE_AP)
            {
                vap = (u16_t) p3;
            }

            /* Challenge Text => share-2 or share-3 */
            if (p1 == 0x20001)
            {
                if (p2 == 0) //Status == success
                {
                    zmw_buf_writeh(dev, buf, offset, 0x8010);
                    offset+=2;
                    /* share-2 : AP generate challenge text */
                    for (i=0; i<128; i++)
                    {
                        wd->ap.challengeText[i] = (u8_t)zfGetRandomNumber(dev, 0);
                    }
                    zfCopyToIntTxBuffer(dev, buf, wd->ap.challengeText, offset, 128);
                    offset += 128;
                }
            }
            else if (p1 == 0x30001)
            {
                /* share-3 : STA return challenge Text */
                zfCopyToIntTxBuffer(dev, buf, wd->sta.challengeText, offset, wd->sta.challengeText[1]+2);
                offset += (wd->sta.challengeText[1]+2);
            }

            break;

        case ZM_WLAN_FRAME_TYPE_ASOCREQ :
        case ZM_WLAN_FRAME_TYPE_REASOCREQ :
            /* Capability */
            zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.capability[0]);
            zmw_tx_buf_writeb(dev, buf, offset++, wd->sta.capability[1]);

            /* Listen Interval */
            zmw_tx_buf_writeh(dev, buf, offset, 0x0005);
            offset+=2;

            /* Reassocaited Request : Current AP address */
            if (frameType == ZM_WLAN_FRAME_TYPE_REASOCREQ)
            {
            zmw_tx_buf_writeh(dev, buf, offset, wd->sta.bssid[0]);
                offset+=2;
            zmw_tx_buf_writeh(dev, buf, offset, wd->sta.bssid[1]);
                offset+=2;
            zmw_tx_buf_writeh(dev, buf, offset, wd->sta.bssid[2]);
                offset+=2;
            }

            /* SSID */
            offset = zfStaAddIeSsid(dev, buf, offset);


            if ( wd->sta.currentFrequency < 3000 )
            {
                /* Support Rate */
                offset = zfMmAddIeSupportRate(dev, buf, offset, ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_CCK);
            }
            else
            {
                /* Support Rate */
                offset = zfMmAddIeSupportRate(dev, buf, offset, ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_OFDM);
            }

            if ((wd->sta.capability[1] & ZM_BIT_0) == 1)
            {   //spectrum management flag enable
                offset = zfStaAddIePowerCap(dev, buf, offset);
                offset = zfStaAddIeSupportCh(dev, buf, offset);
            }

            if (wd->sta.currentFrequency < 3000)
            {
                /* Extended Supported Rates */
                if (wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N))
                {
                    offset = zfMmAddIeSupportRate(dev, buf, offset, ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
                }
            }


            //offset = zfStaAddIeWpaRsn(dev, buf, offset, frameType);
            //Move to wrapper function, for OS difference--CWYang(m)
            //for windows wrapper, zfwStaAddIeWpaRsn() should be below:
            //u16_t zfwStaAddIeWpaRsn(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t frameType)
            //{
            //    return zfStaAddIeWpaRsn(dev, buf, offset, frameType);
            //}
            offset = zfwStaAddIeWpaRsn(dev, buf, offset, frameType);

#ifdef ZM_ENABLE_CENC
            /* CENC */
            //if (wd->sta.encryMode == ZM_CENC)
            offset = zfStaAddIeCenc(dev, buf, offset);
#endif //ZM_ENABLE_CENC
            if (((wd->sta.wmeEnabled & ZM_STA_WME_ENABLE_BIT) != 0) //WME enabled
              && ((wd->sta.apWmeCapability & 0x1) != 0)) //WME AP
            {
                if (((wd->sta.apWmeCapability & 0x80) != 0) //UAPSD AP
                 && ((wd->sta.wmeEnabled & ZM_STA_UAPSD_ENABLE_BIT) != 0)) //UAPSD enabled
                {
                    offset = zfStaAddIeWmeInfo(dev, buf, offset, wd->sta.wmeQosInfo);
                }
                else
                {
                    offset = zfStaAddIeWmeInfo(dev, buf, offset, 0);
                }
            }
            // jhlee HT 0
            //CWYang(+)
            if (wd->sta.EnableHT != 0)
            {
                #ifndef ZM_DISABLE_AMSDU8K_SUPPORT
                    //Support 8K A-MSDU
                    if (wd->sta.wepStatus == ZM_ENCRYPTION_WEP_DISABLED)
                    {
                        wd->sta.HTCap.Data.HtCapInfo |= HTCAP_MaxAMSDULength;
                    }
                    else
                    {
                        wd->sta.HTCap.Data.HtCapInfo &= (~HTCAP_MaxAMSDULength);
                    }
                #else
                    //Support 4K A-MSDU
                    wd->sta.HTCap.Data.HtCapInfo &= (~HTCAP_MaxAMSDULength);
                #endif

                /* HT Capabilities Info */
                if (wd->BandWidth40 == 1) {
                    wd->sta.HTCap.Data.HtCapInfo |= HTCAP_SupChannelWidthSet;
                }
                else {
                    wd->sta.HTCap.Data.HtCapInfo &= ~HTCAP_SupChannelWidthSet;
                    //wd->sta.HTCap.Data.HtCapInfo |= HTCAP_SupChannelWidthSet;
                }

                wd->sta.HTCap.Data.AMPDUParam &= ~HTCAP_MaxRxAMPDU3;
                wd->sta.HTCap.Data.AMPDUParam |= HTCAP_MaxRxAMPDU3;
                wd->sta.HTCap.Data.MCSSet[1] = 0xFF; // MCS 8 ~ 15
                offset = zfMmAddHTCapability(dev, buf, offset);
                offset = zfMmAddPreNHTCapability(dev, buf, offset);
                //CWYang(+)
                /* Extended HT Capabilities Info */
                //offset = zfMmAddExtendedHTCapability(dev, buf, offset);
            }


            //Store asoc request frame body, for VISTA only
            wd->sta.asocReqFrameBodySize = ((offset - hlen) >
                    ZM_CACHED_FRAMEBODY_SIZE)?
                    ZM_CACHED_FRAMEBODY_SIZE:(offset - hlen);
            for (i=0; i<wd->sta.asocReqFrameBodySize; i++)
            {
                wd->sta.asocReqFrameBody[i] = zmw_tx_buf_readb(dev, buf, i + hlen);
            }
            break;

        case ZM_WLAN_FRAME_TYPE_ASOCRSP :
        case ZM_WLAN_FRAME_TYPE_REASOCRSP :
            vap = (u16_t) p3;

            /* Capability */
            zmw_tx_buf_writeh(dev, buf, offset, wd->ap.capab[vap]);
            offset+=2;

            /* Status Code */
            zmw_tx_buf_writeh(dev, buf, offset, (u16_t)p1);
            offset+=2;

            /* AID */
            zmw_tx_buf_writeh(dev, buf, offset, (u16_t)(p2|0xc000));
            offset+=2;


            if ( wd->frequency < 3000 )
            {
            /* Support Rate */
            offset = zfMmAddIeSupportRate(dev, buf, offset, ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_CCK);

            /* Extended Supported Rates */
            offset = zfMmAddIeSupportRate(dev, buf, offset, ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
            }
            else
            {
                /* Support Rate */
                offset = zfMmAddIeSupportRate(dev, buf, offset, ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_OFDM);
            }



            /* WME Parameters */
            if (wd->wlanMode == ZM_MODE_AP)
            {
                /* TODO : if WME STA then send WME parameter element */
                if (wd->ap.qosMode == 1)
                {
                    offset = zfApAddIeWmePara(dev, buf, offset, vap);
                }
            }
            // jhlee HT 0
            //CWYang(+)
            /* HT Capabilities Info */
            offset = zfMmAddHTCapability(dev, buf, offset);
            //CWYang(+)
            /* Extended HT Capabilities Info */
            offset = zfMmAddExtendedHTCapability(dev, buf, offset);
            break;

        case ZM_WLAN_FRAME_TYPE_ATIM :
            /* NULL frame */
            /* TODO : add two dumb bytes temporarily */
            offset += 2;
            break;

        case ZM_WLAN_FRAME_TYPE_QOS_NULL :
            zmw_buf_writeh(dev, buf, offset, 0x0010);
            offset += 2;
            break;

        case ZM_WLAN_DATA_FRAME :
            break;

        case ZM_WLAN_FRAME_TYPE_DISASOC :
        case ZM_WLAN_FRAME_TYPE_DEAUTH :
            if (wd->wlanMode == ZM_MODE_AP)
            {
              vap = (u16_t) p3;

              if ((aid = zfApFindSta(dev, dst)) != 0xffff)
              {
                  zmw_enter_critical_section(dev);
                  /* Clear STA table */
                  wd->ap.staTable[aid].valid = 0;

                  zmw_leave_critical_section(dev);

                  if (wd->zfcbDisAsocNotify != NULL)
                  {
                      wd->zfcbDisAsocNotify(dev, (u8_t*)dst, vap);
                  }
              }
            }
            /* Reason Code */
            zmw_tx_buf_writeh(dev, buf, offset, (u16_t)p1);
            offset+=2;
            break;
    }

    zfwBufSetSize(dev, buf, offset);

    zm_msg2_mm(ZM_LV_2, "management frame body size=", offset-hlen);

    //Copy wlan header
    zfTxGenMmHeader(dev, frameType, dst, header, offset-hlen, buf, vap, encrypt);
    for (i=0; i<(hlen>>1); i++)
    {
        zmw_tx_buf_writeh(dev, buf, i*2, header[i]);
    }

    /* Get buffer DMA address */
    //if ((addrTblSize = zfwBufMapDma(dev, buf, &addrTbl)) == 0)
    //if ((addrTblSize = zfwMapTxDma(dev, buf, &addrTbl)) == 0)
    //{
    //    goto zlError;
    //}

    zm_msg2_mm(ZM_LV_2, "offset=", offset);
    zm_msg2_mm(ZM_LV_2, "hlen=", hlen);
    //zm_msg2_mm(ZM_LV_2, "addrTblSize=", addrTblSize);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.len[0]=", addrTbl.len[0]);
    //zm_msg2_mm(ZM_LV_2, "addrTbl.physAddrl[0]=", addrTbl.physAddrl[0]);
    //zm_msg2_mm(ZM_LV_2, "buf->data=", buf->data);

    #if 0
    if ((err = zfHpSend(dev, NULL, 0, NULL, 0, NULL, 0, buf, 0,
            ZM_INTERNAL_ALLOC_BUF, 0, 0xff)) != ZM_SUCCESS)
    {
        goto zlError;
    }
    #else
    zfPutVmmq(dev, buf);
    zfPushVtxq(dev);
    #endif

    return;
#if 0
zlError:

    zfwBufFree(dev, buf, 0);
    return;
#endif
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfProcessManagement         */
/*      Process received management frame.                              */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : received management frame buffer                           */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
void zfProcessManagement(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo) //CWYang(m)
{
    u8_t frameType;
    u16_t ta[3];
    u16_t ra[3];
    u16_t vap = 0, index = 0;
    //u16_t i;

    zmw_get_wlan_dev(dev);

    ra[0] = zmw_rx_buf_readh(dev, buf, 4);
    ra[1] = zmw_rx_buf_readh(dev, buf, 6);
    ra[2] = zmw_rx_buf_readh(dev, buf, 8);

    ta[0] = zmw_rx_buf_readh(dev, buf, 10);
    ta[1] = zmw_rx_buf_readh(dev, buf, 12);
    ta[2] = zmw_rx_buf_readh(dev, buf, 14);

    frameType = zmw_rx_buf_readb(dev, buf, 0);

    if (wd->wlanMode == ZM_MODE_AP)
    {
#if 1
        vap = 0;
        if ((ra[0] & 0x1) != 1)
        {
            /* AP : Find virtual AP */
            if ((index = zfApFindSta(dev, ta)) != 0xffff)
            {
                vap = wd->ap.staTable[index].vap;
            }
        }
        zm_msg2_mm(ZM_LV_2, "vap=", vap);
#endif

        /* Dispatch by frame type */
        switch (frameType)
        {
                /* Beacon */
            case ZM_WLAN_FRAME_TYPE_BEACON :
                zfApProcessBeacon(dev, buf);
                break;
                /* Authentication */
            case ZM_WLAN_FRAME_TYPE_AUTH :
                zfApProcessAuth(dev, buf, ta, vap);
                break;
                /* Association request */
            case ZM_WLAN_FRAME_TYPE_ASOCREQ :
                /* Reassociation request */
            case ZM_WLAN_FRAME_TYPE_REASOCREQ :
                zfApProcessAsocReq(dev, buf, ta, vap);
                break;
                /* Association response */
            case ZM_WLAN_FRAME_TYPE_ASOCRSP :
                //zfApProcessAsocRsp(dev, buf);
                break;
                /* Deauthentication */
            case ZM_WLAN_FRAME_TYPE_DEAUTH :
                zfApProcessDeauth(dev, buf, ta, vap);
                break;
                /* Disassociation */
            case ZM_WLAN_FRAME_TYPE_DISASOC :
                zfApProcessDisasoc(dev, buf, ta, vap);
                break;
                /* Probe request */
            case ZM_WLAN_FRAME_TYPE_PROBEREQ :
                zfProcessProbeReq(dev, buf, ta);
                break;
                /* Probe response */
            case ZM_WLAN_FRAME_TYPE_PROBERSP :
                zfApProcessProbeRsp(dev, buf, AddInfo);
                break;
                /* Action */
            case ZM_WLAN_FRAME_TYPE_ACTION :
                zfApProcessAction(dev, buf);
                break;
        }
    }
    else //if ((wd->wlanMode == ZM_MODE_INFRASTRUCTURE) || (wd->wlanMode == ZM_MODE_IBSS))
    {
        /* Dispatch by frame type */
        switch (frameType)
        {
                /* Beacon */
            case ZM_WLAN_FRAME_TYPE_BEACON :
                /* if enable 802.11h and current channel is silent but receive beacon from other AP */
                if (((wd->regulationTable.allowChannel[wd->regulationTable.CurChIndex].channelFlags
                        & ZM_REG_FLAG_CHANNEL_CSA) != 0) && wd->sta.DFSEnable)
                {
                    wd->regulationTable.allowChannel[wd->regulationTable.CurChIndex].channelFlags
                            &= ~(ZM_REG_FLAG_CHANNEL_CSA & ZM_REG_FLAG_CHANNEL_PASSIVE);
                }
                zfStaProcessBeacon(dev, buf, AddInfo); //CWYang(m)
                break;
                /* Authentication */
            case ZM_WLAN_FRAME_TYPE_AUTH :
                /* TODO : vap parameter is useless in STA mode, get rid of it */
                zfStaProcessAuth(dev, buf, ta, 0);
                break;
                /* Association request */
            case ZM_WLAN_FRAME_TYPE_ASOCREQ :
                /* TODO : vap parameter is useless in STA mode, get rid of it */
                zfStaProcessAsocReq(dev, buf, ta, 0);
                break;
                /* Association response */
            case ZM_WLAN_FRAME_TYPE_ASOCRSP :
                /* Reassociation request */
            case ZM_WLAN_FRAME_TYPE_REASOCRSP :
                zfStaProcessAsocRsp(dev, buf);
                break;
                /* Deauthentication */
            case ZM_WLAN_FRAME_TYPE_DEAUTH :
                zm_debug_msg0("Deauthentication received");
                zfStaProcessDeauth(dev, buf);
                break;
                /* Disassociation */
            case ZM_WLAN_FRAME_TYPE_DISASOC :
                zm_debug_msg0("Disassociation received");
                zfStaProcessDisasoc(dev, buf);
                break;
                /* Probe request */
            case ZM_WLAN_FRAME_TYPE_PROBEREQ :
                zfProcessProbeReq(dev, buf, ta);
                break;
                /* Probe response */
            case ZM_WLAN_FRAME_TYPE_PROBERSP :
                /* if enable 802.11h and current channel is silent but receive probe response from other AP */
                if (((wd->regulationTable.allowChannel[wd->regulationTable.CurChIndex].channelFlags
                        & ZM_REG_FLAG_CHANNEL_CSA) != 0) && wd->sta.DFSEnable)
                {
                    wd->regulationTable.allowChannel[wd->regulationTable.CurChIndex].channelFlags
                            &= ~(ZM_REG_FLAG_CHANNEL_CSA & ZM_REG_FLAG_CHANNEL_PASSIVE);
                }
                zfStaProcessProbeRsp(dev, buf, AddInfo);
                break;

            case ZM_WLAN_FRAME_TYPE_ATIM:
                zfStaProcessAtim(dev, buf);
                break;
                /* Action */
            case ZM_WLAN_FRAME_TYPE_ACTION :
                zm_msg0_mm(ZM_LV_2, "ProcessActionMgtFrame");
                zfStaProcessAction(dev, buf);
                break;
        }
    }
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfProcessProbeReq           */
/*      Process probe request management frame.                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : auth frame buffer                                         */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
void zfProcessProbeReq(zdev_t* dev, zbuf_t* buf, u16_t* src)
{
    u16_t offset;
    u8_t len;
    u16_t i, j;
    u8_t ch;
    u16_t sendFlag;

    zmw_get_wlan_dev(dev);

    /* check mode : AP/IBSS */
    if ((wd->wlanMode != ZM_MODE_AP) && (wd->wlanMode != ZM_MODE_IBSS))
    {
        zm_msg0_mm(ZM_LV_3, "Ignore probe req");
        return;
    }

    if ((wd->wlanMode != ZM_MODE_AP) && (wd->sta.adapterState == ZM_STA_STATE_DISCONNECT))
    {
        zm_msg0_mm(ZM_LV_3, "Packets dropped due to disconnect state");
        return;
    }

    if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_PROBERSP, src, 0, 0, 0);

        return;
    }

    /* check SSID */
    if ((offset = zfFindElement(dev, buf, ZM_WLAN_EID_SSID)) == 0xffff)
    {
        zm_msg0_mm(ZM_LV_3, "probe req SSID not found");
        return;
    }

    len = zmw_rx_buf_readb(dev, buf, offset+1);

    for (i=0; i<ZM_MAX_AP_SUPPORT; i++)
    {
        if ((wd->ap.apBitmap & (1<<i)) != 0)
        {
            zm_msg1_mm(ZM_LV_3, "len=", len);
            sendFlag = 0;
            /* boardcast SSID */
            if (len == 0)
            {
                if (wd->ap.hideSsid[i] == 0)
                {
                    sendFlag = 1;
                }
            }
            /* Not broadcast SSID */
            else if (wd->ap.ssidLen[i] == len)
            {
                for (j=0; j<len; j++)
                {
                    if ((ch = zmw_rx_buf_readb(dev, buf, offset+2+j))
                            != wd->ap.ssid[i][j])
                    {
                        break;
                    }
                }
                if (j == len)
                {
                    sendFlag = 1;
                }
            }
            if (sendFlag == 1)
            {
                /* Send probe response */
                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_PROBERSP, src, i, 0, i);
            }
        }
    }
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfProcessProbeRsp           */
/*      Process probe response management frame.                        */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : auth frame buffer                                         */
/*  AddInfo : Rx Header and Rx Mac Status                               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Aress Yang          ZyDAS Technology Corporation    2006.11     */
/*                                                                      */
/************************************************************************/
void zfProcessProbeRsp(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo)
{
    /* Gather scan result */
    /* Parse TIM and send PS-POLL in power saving mode */
    struct zsWlanProbeRspFrameHeader*  pProbeRspHeader;
    struct zsBssInfo* pBssInfo;
    u8_t   pBuf[sizeof(struct zsWlanProbeRspFrameHeader)];
    int    res;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zfCopyFromRxBuffer(dev, buf, pBuf, 0,
                       sizeof(struct zsWlanProbeRspFrameHeader));
    pProbeRspHeader = (struct zsWlanProbeRspFrameHeader*) pBuf;

    zmw_enter_critical_section(dev);

    //zm_debug_msg1("bss count = ", wd->sta.bssList.bssCount);

    pBssInfo = zfStaFindBssInfo(dev, buf, pProbeRspHeader);

    //if ( i == wd->sta.bssList.bssCount )
    if ( pBssInfo == NULL )
    {
        /* Allocate a new entry if BSS not in the scan list */
        pBssInfo = zfBssInfoAllocate(dev);
        if (pBssInfo != NULL)
        {
            res = zfStaInitBssInfo(dev, buf, pProbeRspHeader, pBssInfo, AddInfo, 0);
            //zfDumpSSID(pBssInfo->ssid[1], &(pBssInfo->ssid[2]));
            if ( res != 0 )
            {
                zfBssInfoFree(dev, pBssInfo);
            }
            else
            {
                zfBssInfoInsertToList(dev, pBssInfo);
            }
        }
    }
    else
    {
        res = zfStaInitBssInfo(dev, buf, pProbeRspHeader, pBssInfo, AddInfo, 1);
        if (res == 2)
        {
            zfBssInfoRemoveFromList(dev, pBssInfo);
            zfBssInfoFree(dev, pBssInfo);
        }
        else if ( wd->wlanMode == ZM_MODE_IBSS )
        {
            int idx;

            // It would reset the alive counter if the peer station is found!
            zfStaFindFreeOpposite(dev, (u16_t *)pBssInfo->macaddr, &idx);
        }
    }

    zmw_leave_critical_section(dev);

    return;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfSendProbeReq              */
/*      Send probe request management frame.                            */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Ji-Huang Lee        ZyDAS Technology Corporation    2005.11     */
/*                                                                      */
/************************************************************************/

u16_t zfSendProbeReq(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t bWithSSID)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();


    /* SSID */
    if (bWithSSID == 0)  /* broadcast ssid */
    {
        //zmw_leave_critical_section(dev);
        zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_SSID);
        zmw_tx_buf_writeb(dev, buf, offset++, 0);   /* length = 0 */
    }
    else
    {
        zmw_enter_critical_section(dev);
        if (wd->ws.probingSsidList[bWithSSID-1].ssidLen == 0)
        {
            zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_SSID);
            zmw_tx_buf_writeb(dev, buf, offset++, 0);   /* length = 0 */
        }
        else
        {
            zmw_tx_buf_writeb(dev, buf, offset++, ZM_WLAN_EID_SSID);
            zmw_tx_buf_writeb(dev, buf, offset++,
                    wd->ws.probingSsidList[bWithSSID-1].ssidLen);
            zfCopyToIntTxBuffer(dev, buf,
                    wd->ws.probingSsidList[bWithSSID-1].ssid,
                    offset,
                    wd->ws.probingSsidList[bWithSSID-1].ssidLen); /* ssid */
            offset += wd->ws.probingSsidList[bWithSSID-1].ssidLen;
        }
        zmw_leave_critical_section(dev);
    }

    /* Supported rates */
    if ( wd->sta.currentFrequency < 3000 )
    {   /* 802.11b+g */
        offset = zfMmAddIeSupportRate(dev, buf, offset,
                                      ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_CCK);

        if (wd->supportMode & (ZM_WIRELESS_MODE_24_54|ZM_WIRELESS_MODE_24_N)) {
            if (wd->wlanMode == ZM_MODE_IBSS) {
                if (wd->wfc.bIbssGMode) {
                    offset = zfMmAddIeSupportRate(dev, buf, offset,
                                      ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
                }
            } else {
                offset = zfMmAddIeSupportRate(dev, buf, offset,
                                      ZM_WLAN_EID_EXTENDED_RATE, ZM_RATE_SET_OFDM);
            }
        }
    }
    else
    {  /* 802.11a */
        offset = zfMmAddIeSupportRate(dev, buf, offset,
                                      ZM_WLAN_EID_SUPPORT_RATE, ZM_RATE_SET_OFDM);
    }

    return offset;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfUpdateDefaultQosParameter */
/*      Update TxQs CWMIN, CWMAX, AIFS and TXO to WME default value.    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      mode : 0=>STA, 1=>AP                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2006.6      */
/*                                                                      */
/************************************************************************/
void zfUpdateDefaultQosParameter(zdev_t* dev, u8_t mode)
{
    u16_t cwmin[5];
    u16_t cwmax[5];
    u16_t aifs[5];
    u16_t txop[5];

    /* WMM parameter for STA */
    /* Best Effor */
    cwmin[0] = 15;
    cwmax[0] = 1023;
    aifs[0] = 3 * 9 + 10;
    txop[0] = 0;
    /* Back Ground */
    cwmin[1] = 15;
    cwmax[1] = 1023;
    aifs[1] = 7 * 9 + 10;
    txop[1] = 0;
    /* VIDEO */
    cwmin[2] = 7;
    cwmax[2] = 15;
    aifs[2] = 2 * 9 + 10;
    txop[2] = 94;
    /* VOICE */
    cwmin[3] = 3;
    cwmax[3] = 7;
    aifs[3] = 2 * 9 + 10;
    txop[3] = 47;
    /* Special TxQ */
    cwmin[4] = 3;
    cwmax[4] = 7;
    aifs[4] = 2 * 9 + 10;
    txop[4] = 0;

    /* WMM parameter for AP */
    if (mode == 1)
    {
        cwmax[0] = 63;
        aifs[3] = 1 * 9 + 10;
        aifs[4] = 1 * 9 + 10;
    }
    zfHpUpdateQosParameter(dev, cwmin, cwmax, aifs, txop);
}

u16_t zfFindATHExtCap(zdev_t* dev, zbuf_t* buf, u8_t type, u8_t subtype)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t tmp;

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);

    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);

    /* Search loop */
    while ((offset+2)<bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == ZM_WLAN_EID_WIFI_IE)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if (((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x00)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+3)) == 0x03)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+4)) == 0x7f)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+5)) == type))

            {
                if ( subtype != 0xff )
                {
                    if ( (tmp = zmw_rx_buf_readb(dev, buf, offset+6)) == subtype  )
                    {
                        return offset;
                    }
                }
                else
                {
                    return offset;
                }
            }
        }

        /* Advance to next element */
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }
        offset += (elen+2);
    }
    return 0xffff;
}

u16_t zfFindBrdcmMrvlRlnkExtCap(zdev_t* dev, zbuf_t* buf)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t tmp;

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);

    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);

    /* Search loop */
    while ((offset+2)<bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == ZM_WLAN_EID_WIFI_IE)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if (((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x00)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+3)) == 0x10)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+4)) == 0x18))

            {
                return offset;
            }
            else if (((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x00)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+3)) == 0x50)
                    && ((tmp = zmw_rx_buf_readb(dev, buf, offset+4)) == 0x43))

            {
                return offset;
            }
        }
        else if ((id = zmw_rx_buf_readb(dev, buf, offset)) == 0x7F)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if ((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x01)

            {
                return offset;
            }
        }

        /* Advance to next element */
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }
        offset += (elen+2);
    }
    return 0xffff;
}

u16_t zfFindMarvelExtCap(zdev_t* dev, zbuf_t* buf)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t tmp;

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);

    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);

    /* Search loop */
    while ((offset+2)<bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == ZM_WLAN_EID_WIFI_IE)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1))>(bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if (((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x00)
                  && ((tmp = zmw_rx_buf_readb(dev, buf, offset+3)) == 0x50)
                  && ((tmp = zmw_rx_buf_readb(dev, buf, offset+4)) == 0x43))

            {
                return offset;
            }
        }

        /* Advance to next element */
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }
        offset += (elen+2);
    }
    return 0xffff;
}

u16_t zfFindBroadcomExtCap(zdev_t* dev, zbuf_t* buf)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t tmp;

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);

    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);

    /* Search loop */
    while((offset+2) < bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == ZM_WLAN_EID_WIFI_IE)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) > (bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if (elen == 0)
            {
                return 0xffff;
            }

            if ( ((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x00)
                 && ((tmp = zmw_rx_buf_readb(dev, buf, offset+3)) == 0x10)
                 && ((tmp = zmw_rx_buf_readb(dev, buf, offset+4)) == 0x18) )
            {
                return offset;
            }
        }

        /* Advance to next element */
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }

        offset += (elen+2);
    }

    return 0xffff;
}

u16_t zfFindRlnkExtCap(zdev_t* dev, zbuf_t* buf)
{
    u8_t subType;
    u16_t offset;
    u16_t bufLen;
    u16_t elen;
    u8_t id;
    u8_t tmp;

    /* Get offset of first element */
    subType = (zmw_rx_buf_readb(dev, buf, 0) >> 4);

    if ((offset = zgElementOffsetTable[subType]) == 0xff)
    {
        zm_assert(0);
    }

    /* Plus wlan header */
    offset += 24;

    bufLen = zfwBufGetSize(dev, buf);

    /* Search loop */
    while((offset+2) < bufLen)                   // including element ID and length (2bytes)
    {
        /* Search target element */
        if ((id = zmw_rx_buf_readb(dev, buf, offset)) == 0x7F)
        {
            /* Bingo */
            if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) > (bufLen - offset))
            {
                /* Element length error */
                return 0xffff;
            }

            if ( elen == 0 )
            {
                return 0xffff;
            }

            if ((tmp = zmw_rx_buf_readb(dev, buf, offset+2)) == 0x01)

            {
                return offset;
            }
        }

        /* Advance to next element */
        if ((elen = zmw_rx_buf_readb(dev, buf, offset+1)) == 0)
        {
            return 0xffff;
        }

        offset += (elen+2);
    }

    return 0xffff;
}
