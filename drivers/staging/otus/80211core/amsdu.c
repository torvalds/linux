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
/*    FUNCTION DESCRIPTION                  zfGetAmsduSubFrame          */
/*      Get a subframe from a-MSDU.                                     */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : A-MSDU frame buffer                                       */
/*      offset : offset of subframe in the A-MSDU                       */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      NULL or subframe                                                */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
zbuf_t* zfGetAmsduSubFrame(zdev_t* dev, zbuf_t* buf, u16_t* offset)
{
    u16_t subframeLen;
    u16_t amsduLen = zfwBufGetSize(dev, buf);
    zbuf_t* newBuf;

    ZM_PERFORMANCE_RX_AMSDU(dev, buf, amsduLen);

    /* Verify A-MSDU length */
    if (amsduLen < (*offset + 14))
    {
        return NULL;
    }

    /* Locate A-MSDU subframe by offset and verify subframe length */
    subframeLen = (zmw_buf_readb(dev, buf, *offset + 12) << 8) +
                  zmw_buf_readb(dev, buf, *offset + 13);
    if (subframeLen == 0)
    {
        return NULL;
    }

    /* Verify A-MSDU subframe length */
    if ((*offset+14+subframeLen) <= amsduLen)
    {
        /* Allocate a new buffer */
        if ((newBuf = zfwBufAllocate(dev, 24+2+subframeLen)) != NULL)
        {
#ifdef ZM_ENABLE_NATIVE_WIFI
            /* Copy and convert subframe to wlan frame format */
            /* SHALL NOT INCLUDE QOS and AMSDU header. Ray 20070807 For Vista */
            zfRxBufferCopy(dev, newBuf, buf, 0, 0, 24);
            zfRxBufferCopy(dev, newBuf, buf, 24, *offset+14, subframeLen);
            zfwBufSetSize(dev, newBuf, 24+subframeLen);
#else
            /* Copy subframe to new buffer */
            zfRxBufferCopy(dev, newBuf, buf, 0, *offset, 14+subframeLen);
            zfwBufSetSize(dev, newBuf, 14+subframeLen);
#endif
            /* Update offset */
            *offset += (((14+subframeLen)+3) & 0xfffc);

            /* Return buffer pointer */
            return newBuf;
        }
    }
    return NULL;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfDeAmsdu                   */
/*      De-AMSDU.                                                       */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : A-MSDU frame buffer                                       */
/*      vap : VAP port                                                  */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.2      */
/*                                                                      */
/************************************************************************/
void zfDeAmsdu(zdev_t* dev, zbuf_t* buf, u16_t vap, u8_t encryMode)
{
    u16_t offset = ZM_SIZE_OF_WLAN_DATA_HEADER+ZM_SIZE_OF_QOS_CTRL;
    zbuf_t* subframeBuf;
    zmw_get_wlan_dev(dev);

    ZM_BUFFER_TRACE(dev, buf)

    if (encryMode == ZM_AES || encryMode == ZM_TKIP)
    {
        offset += (ZM_SIZE_OF_IV + ZM_SIZE_OF_EXT_IV);
    }
    else if (encryMode == ZM_WEP64 || encryMode == ZM_WEP128)
    {
        offset += ZM_SIZE_OF_IV;
    }

    /* Repeatly calling zfGetAmsduSubFrame() until NULL returned */
    while ((subframeBuf = zfGetAmsduSubFrame(dev, buf, &offset)) != NULL)
    {
        wd->commTally.NotifyNDISRxFrmCnt++;
        if (wd->zfcbRecvEth != NULL)
    	{
            wd->zfcbRecvEth(dev, subframeBuf, (u8_t)vap);
            ZM_PERFORMANCE_RX_MSDU(dev, wd->tick);
        }
    }
    zfwBufFree(dev, buf, 0);

    return;
}
