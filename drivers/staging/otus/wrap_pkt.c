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
/*  Module Name : wrap_pkt.c                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains wrapper functions for packet handling       */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>

#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
#endif



//extern struct zsWdsStruct wds[ZM_WDS_PORT_NUMBER];
extern struct zsVapStruct vap[ZM_VAP_PORT_NUMBER];


/***** Rx *****/
void zfLnxRecv80211(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo)
{
    u16_t frameType;
    u16_t frameCtrl;
    u16_t frameSubtype;
    zbuf_t *skb1;
    struct usbdrv_private *macp = dev->ml_priv;

    //frameCtrl = zmw_buf_readb(dev, buf, 0);
    frameCtrl = *(u8_t*)((u8_t*)buf->data);
    frameType = frameCtrl & 0xf;
    frameSubtype = frameCtrl & 0xf0;

    if ((frameType == 0x0) && (macp->forwardMgmt))
    {
        switch (frameSubtype)
        {
                /* Beacon */
            case 0x80 :
                /* Probe response */
            case 0x50 :
                skb1 = skb_copy(buf, GFP_ATOMIC);
                if(skb1 != NULL)
                {
                    skb1->dev = dev;
                    skb1->mac_header = skb1->data;
	            skb1->ip_summed = CHECKSUM_NONE;
	            skb1->pkt_type = PACKET_OTHERHOST;
	            skb1->protocol = __constant_htons(0x0019);  /* ETH_P_80211_RAW */
    	            netif_rx(skb1);
	            }
                break;
            default:
                break;
        }
    }

    zfiRecv80211(dev, buf, addInfo);
    return;
}

#define ZM_AVOID_UDP_LARGE_PACKET_FAIL
void zfLnxRecvEth(zdev_t* dev, zbuf_t* buf, u16_t port)
{
    struct usbdrv_private *macp = dev->ml_priv;
#ifdef ZM_AVOID_UDP_LARGE_PACKET_FAIL
    zbuf_t *new_buf;

    //new_buf = dev_alloc_skb(2048);
    new_buf = dev_alloc_skb(buf->len);

#ifdef NET_SKBUFF_DATA_USES_OFFSET
    new_buf->tail = 0;
    new_buf->len = 0;
#else
    new_buf->tail = new_buf->data;
    new_buf->len = 0;
#endif

    skb_put(new_buf, buf->len);
    memcpy(new_buf->data, buf->data, buf->len);

    /* Free buffer */
    dev_kfree_skb_any(buf);

    if (port == 0)
    {
        new_buf->dev = dev;
        new_buf->protocol = eth_type_trans(new_buf, dev);
    }
    else
    {
        /* VAP */
        if (vap[0].dev != NULL)
        {
            new_buf->dev = vap[0].dev;
            new_buf->protocol = eth_type_trans(new_buf, vap[0].dev);
        }
        else
        {
            new_buf->dev = dev;
            new_buf->protocol = eth_type_trans(new_buf, dev);
        }
    }

    new_buf->ip_summed = CHECKSUM_NONE;
    dev->last_rx = jiffies;

    switch(netif_rx(new_buf))
#else
    if (port == 0)
    {
        buf->dev = dev;
        buf->protocol = eth_type_trans(buf, dev);
    }
    else
    {
        /* VAP */
        if (vap[0].dev != NULL)
        {
            buf->dev = vap[0].dev;
            buf->protocol = eth_type_trans(buf, vap[0].dev);
        }
        else
        {
            buf->dev = dev;
            buf->protocol = eth_type_trans(buf, dev);
        }
    }

    buf->ip_summed = CHECKSUM_NONE;
    dev->last_rx = jiffies;

    switch(netif_rx(buf))
#endif
    {
    case NET_RX_BAD:
    case NET_RX_DROP:
    case NET_RX_CN_MOD:
    case NET_RX_CN_HIGH:
        break;
    default:
            macp->drv_stats.net_stats.rx_packets++;
            macp->drv_stats.net_stats.rx_bytes += buf->len;
        break;
    }

    return;
}

/* Leave an empty line below to remove warning message on some compiler */
