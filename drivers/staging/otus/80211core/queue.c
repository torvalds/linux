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
/*  Module Name : queue.c                                               */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains queue management functions.                */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"
#include "queue.h"


struct zsQueue* zfQueueCreate(zdev_t* dev, u16_t size)
{
    struct zsQueue* q;

    q = (struct zsQueue*)zfwMemAllocate(dev, sizeof(struct zsQueue)
            + (sizeof(struct zsQueueCell)*(size-1)));
    if (q != NULL)
    {
        q->size = size;
        q->sizeMask = size-1;
        q->head = 0;
        q->tail = 0;
    }
    return q;
}

void zfQueueDestroy(zdev_t* dev, struct zsQueue* q)
{
    u16_t size = sizeof(struct zsQueue) + (sizeof(struct zsQueueCell)*(q->size-1));

    zfQueueFlush(dev, q);
    zfwMemFree(dev, q, size);

    return;
}

u16_t zfQueuePutNcs(zdev_t* dev, struct zsQueue* q, zbuf_t* buf, u32_t tick)
{
    u16_t ret = ZM_ERR_QUEUE_FULL;

    zm_msg0_mm(ZM_LV_1, "zfQueuePutNcs()");

    if (((q->tail+1)&q->sizeMask) != q->head)
    {
        q->cell[q->tail].buf = buf;
        q->cell[q->tail].tick = tick;
        q->tail = (q->tail+1) & q->sizeMask;
        ret = ZM_SUCCESS;
    }

    return ret;
}

u16_t zfQueuePut(zdev_t* dev, struct zsQueue* q, zbuf_t* buf, u32_t tick)
{
    u16_t ret;
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    ret = zfQueuePutNcs(dev, q, buf, tick);

    zmw_leave_critical_section(dev);

    return ret;
}

zbuf_t* zfQueueGet(zdev_t* dev, struct zsQueue* q)
{
    zbuf_t* buf = NULL;
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if (q->head != q->tail)
    {
        buf = q->cell[q->head].buf;
        q->head = (q->head+1) & q->sizeMask;
    }

    zmw_leave_critical_section(dev);

    return buf;
}

u16_t zfCompareDstwithBuf(zdev_t* dev, zbuf_t* buf, u8_t* addr)
{
    u16_t i;
    u8_t dst[6];

    for (i=0; i<6; i++)
    {
        dst[i] = zmw_buf_readb(dev, buf, i);
        if (dst[i] != addr[i])
        {
            return 1+i;
        }
    }

    return 0;
}


zbuf_t* zfQueueGetWithMac(zdev_t* dev, struct zsQueue* q, u8_t* addr, u8_t* mb)
{
    zbuf_t* buf;
    zbuf_t* retBuf = NULL;
    u16_t index, next;
    zmw_declare_for_critical_section();

    *mb = 0;

    zmw_enter_critical_section(dev);

    index = q->head;

    while (1)
    {
        if (index != q->tail)
        {
            buf = q->cell[index].buf;

            //if buf's detination address == input addr
            if (zfCompareDstwithBuf(dev, buf, addr) == 0)
            {
                retBuf = buf;
                //Get it, and trace the whole queue to calculate more bit
                while ((next =((index+1)&q->sizeMask)) != q->tail)
                {
                    q->cell[index].buf = q->cell[next].buf;
                    q->cell[index].tick = q->cell[next].tick;

                    if ((*mb == 0) && (zfCompareDstwithBuf(dev,
                            q->cell[next].buf, addr) == 0))
                    {
                        *mb = 1;
                    }

                    index = next;
                }
                q->tail = (q->tail-1) & q->sizeMask;

                zmw_leave_critical_section(dev);
                return retBuf;
            }
            index = (index + 1) & q->sizeMask;
        } //if (index != q->tail)
        else
        {
            break;
        }
    }

    zmw_leave_critical_section(dev);

    return retBuf;

}

void zfQueueFlush(zdev_t* dev, struct zsQueue* q)
{
    zbuf_t* buf;

    while ((buf = zfQueueGet(dev, q)) != NULL)
    {
        zfwBufFree(dev, buf, 0);
    }

    return;
}

void zfQueueAge(zdev_t* dev, struct zsQueue* q, u32_t tick, u32_t msAge)
{
    zbuf_t* buf;
    u32_t   buftick;
    zmw_declare_for_critical_section();

    while (1)
    {
        buf = NULL;
        zmw_enter_critical_section(dev);

        if (q->head != q->tail)
        {
            buftick = q->cell[q->head].tick;
            if (((tick - buftick)*ZM_MS_PER_TICK) > msAge)
            {
                buf = q->cell[q->head].buf;
                q->head = (q->head+1) & q->sizeMask;
            }
        }

        zmw_leave_critical_section(dev);

        if (buf != NULL)
        {
            zm_msg0_mm(ZM_LV_0, "Age frame in queue!");
            zfwBufFree(dev, buf, 0);
        }
        else
        {
            break;
        }
    }
    return;
}


u8_t zfQueueRemovewithIndex(zdev_t* dev, struct zsQueue* q, u16_t index, u8_t* addr)
{
    u16_t next;
    u8_t mb = 0;

    //trace the whole queue to calculate more bit
    while ((next =((index+1)&q->sizeMask)) != q->tail)
    {
        q->cell[index].buf = q->cell[next].buf;
        q->cell[index].tick = q->cell[next].tick;

        if ((mb == 0) && (zfCompareDstwithBuf(dev,
                q->cell[next].buf, addr) == 0))
        {
            mb = 1;
        }

        index = next;
    }
    q->tail = (q->tail-1) & q->sizeMask;

    return mb;

}

void zfQueueGenerateUapsdTim(zdev_t* dev, struct zsQueue* q,
        u8_t* uniBitMap, u16_t* highestByte)
{
    zbuf_t* psBuf;
    u8_t dst[6];
    u16_t id, aid, index, i;
    u16_t bitPosition;
    u16_t bytePosition;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    index = q->head;

    while (index != q->tail)
    {
        psBuf = q->cell[index].buf;
        for (i=0; i<6; i++)
        {
            dst[i] = zmw_buf_readb(dev, psBuf, i);
        }
        /* TODO : use u8_t* fot MAC address */
        if (((id = zfApFindSta(dev, (u16_t*)dst)) != 0xffff)
                && (wd->ap.staTable[id].psMode != 0))
        {
            /* Calculate PVB only when all AC are delivery-enabled */
            if ((wd->ap.staTable[id].qosInfo & 0xf) == 0xf)
            {
                aid = id + 1;
                bitPosition = (1 << (aid & 0x7));
                bytePosition = (aid >> 3);
                uniBitMap[bytePosition] |= bitPosition;

                if (bytePosition>*highestByte)
                {
                    *highestByte = bytePosition;
                }
            }
            index = (index+1) & q->sizeMask;
        }
        else
        {
            /* Free garbage UAPSD frame */
            zfQueueRemovewithIndex(dev, q, index, dst);
            zfwBufFree(dev, psBuf, 0);
        }
    }
    zmw_leave_critical_section(dev);

    return;
}
