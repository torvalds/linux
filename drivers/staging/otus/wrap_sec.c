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
/*  Module Name : wrap_sec.c                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains wrapper functions for CENC.                 */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>
#include <net/iw_handler.h>

#ifdef ZM_ENABLE_CENC
extern int zfLnxCencSendMsg(struct sock *netlink_sk, u_int8_t *msg, int len);

u16_t zfLnxCencAsocNotify(zdev_t* dev, u16_t* macAddr, u8_t* body, u16_t bodySize, u16_t port)
{
    struct usbdrv_private *macp = (struct usbdrv_private *)dev->priv;
    struct zydas_cenc_sta_info cenc_info;
    //struct sock *netlink_sk;
    u8_t ie_len;
    int ii;

    /* Create NETLINK socket */
    //netlink_sk = netlink_kernel_create(NETLINK_USERSOCK, NULL);

    if (macp->netlink_sk == NULL)
    {
        printk(KERN_ERR "NETLINK Socket is NULL\n");
        return -1;
    }

    memset(&cenc_info, 0, sizeof(cenc_info));

    //memcpy(cenc_info.gsn, vap->iv_cencmsk_keys.wk_txiv, ZM_CENC_IV_LEN);
    zfiWlanQueryGSN(dev, cenc_info.gsn, port);
    cenc_info.datalen += ZM_CENC_IV_LEN;
    ie_len = body[1] + 2;
    memcpy(cenc_info.wie, body, ie_len);
    cenc_info.datalen += ie_len;

    memcpy(cenc_info.sta_mac, macAddr, 6);
    cenc_info.msg_type = ZM_CENC_WAI_REQUEST;
    cenc_info.datalen += 6 + 2;

    printk(KERN_ERR "===== zfwCencSendMsg, bodySize: %d =====\n", bodySize);

    for(ii = 0; ii < bodySize; ii++)
    {
        printk(KERN_ERR "%02x ", body[ii]);

        if ((ii & 0xf) == 0xf)
        {
            printk(KERN_ERR "\n");
        }
    }

    zfLnxCencSendMsg(macp->netlink_sk, (u8_t *)&cenc_info, cenc_info.datalen+4);

    /* Close NETLINK socket */
    //sock_release(netlink_sk);

    return 0;
}
#endif //ZM_ENABLE_CENC

u8_t zfwCencHandleBeaconProbrespon(zdev_t* dev, u8_t *pWIEc,
        u8_t *pPeerSSIDc, u8_t *pPeerAddrc)
{
    return 0;
}

u8_t zfwGetPktEncExemptionActionType(zdev_t* dev, zbuf_t* buf)
{
    return ZM_ENCRYPTION_EXEMPT_NO_EXEMPTION;
}

void copyToIntTxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* src,
                         u16_t offset, u16_t length)
{
    u16_t i;

    for(i=0; i<length;i++)
    {
        //zmw_tx_buf_writeb(dev, buf, offset+i, src[i]);
        *(u8_t*)((u8_t*)buf->data+offset+i) = src[i];
    }
}

u16_t zfwStaAddIeWpaRsn(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t frameType)
{
    struct usbdrv_private *macp = dev->ml_priv;
    //zm_msg1_mm(ZM_LV_0, "CWY - add wpaie content Length : ", macp->supIe[1]);
    if (macp->supIe[1] != 0)
    {
        copyToIntTxBuffer(dev, buf, macp->supIe, offset, macp->supIe[1]+2);
        //memcpy(buf->data[offset], macp->supIe, macp->supIe[1]+2);
        offset += (macp->supIe[1]+2);
    }

    return offset;
}

/* Leave an empty line below to remove warning message on some compiler */
