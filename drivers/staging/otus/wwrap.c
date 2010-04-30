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
/*  Module Name : wwrap.c                                               */
/*  Abstract                                                            */
/*      This module contains wrapper functions.                         */
/*                                                                      */
/*  NOTES                                                               */
/*      Platform dependent.                                             */
/*                                                                      */

/* Please include your header files here */
#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>
#include <linux/slab.h>
#include <net/iw_handler.h>

extern void zfiRecv80211(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo);
extern void zfCoreRecv(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo);
extern void zfIdlChkRsp(zdev_t* dev, u32_t* rsp, u16_t rspLen);
extern void zfIdlRsp(zdev_t* dev, u32_t *rsp, u16_t rspLen);



//extern struct zsWdsStruct wds[ZM_WDS_PORT_NUMBER];
extern struct zsVapStruct vap[ZM_VAP_PORT_NUMBER];

u32_t zfLnxUsbSubmitTxData(zdev_t* dev);
u32_t zfLnxUsbIn(zdev_t* dev, urb_t *urb, zbuf_t *buf);
u32_t zfLnxSubmitRegInUrb(zdev_t *dev);
u32_t zfLnxUsbSubmitBulkUrb(urb_t *urb, struct usb_device *usb, u16_t epnum, u16_t direction,
        void *transfer_buffer, int buffer_length, usb_complete_t complete, void *context);
u32_t zfLnxUsbSubmitIntUrb(urb_t *urb, struct usb_device *usb, u16_t epnum, u16_t direction,
        void *transfer_buffer, int buffer_length, usb_complete_t complete, void *context,
        u32_t interval);

u16_t zfLnxGetFreeTxUrb(zdev_t *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;
    u16_t idx;
    unsigned long irqFlag;

    spin_lock_irqsave(&macp->cs_lock, irqFlag);

    //idx = ((macp->TxUrbTail + 1) & (ZM_MAX_TX_URB_NUM - 1));

    //if (idx != macp->TxUrbHead)
    if (macp->TxUrbCnt != 0)
    {
        idx = macp->TxUrbTail;
        macp->TxUrbTail = ((macp->TxUrbTail + 1) & (ZM_MAX_TX_URB_NUM - 1));
        macp->TxUrbCnt--;
    }
    else
    {
        //printk(KERN_ERR "macp->TxUrbCnt: %d\n", macp->TxUrbCnt);
        idx = 0xffff;
    }

    spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
    return idx;
}

void zfLnxPutTxUrb(zdev_t *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;
    u16_t idx;
    unsigned long irqFlag;

    spin_lock_irqsave(&macp->cs_lock, irqFlag);

    idx = ((macp->TxUrbHead + 1) & (ZM_MAX_TX_URB_NUM - 1));

    //if (idx != macp->TxUrbTail)
    if (macp->TxUrbCnt < ZM_MAX_TX_URB_NUM)
    {
        macp->TxUrbHead = idx;
        macp->TxUrbCnt++;
    }
    else
    {
        printk("UsbTxUrbQ inconsistent: TxUrbHead: %d, TxUrbTail: %d\n",
                macp->TxUrbHead, macp->TxUrbTail);
    }

    spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
}

u16_t zfLnxCheckTxBufferCnt(zdev_t *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;
    u16_t TxBufCnt;
    unsigned long irqFlag;

    spin_lock_irqsave(&macp->cs_lock, irqFlag);

    TxBufCnt = macp->TxBufCnt;

    spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
    return TxBufCnt;
}

UsbTxQ_t *zfLnxGetUsbTxBuffer(zdev_t *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;
    u16_t idx;
    UsbTxQ_t *TxQ;
    unsigned long irqFlag;

    spin_lock_irqsave(&macp->cs_lock, irqFlag);

    idx = ((macp->TxBufHead+1) & (ZM_MAX_TX_BUF_NUM - 1));

    //if (idx != macp->TxBufTail)
    if (macp->TxBufCnt > 0)
    {
        //printk("CWY - zfwGetUsbTxBuffer ,macp->TxBufCnt = %d\n", macp->TxBufCnt);
        TxQ = (UsbTxQ_t *)&(macp->UsbTxBufQ[macp->TxBufHead]);
        macp->TxBufHead = ((macp->TxBufHead+1) & (ZM_MAX_TX_BUF_NUM - 1));
        macp->TxBufCnt--;
    }
    else
    {
        if (macp->TxBufHead != macp->TxBufTail)
        {
            printk(KERN_ERR "zfwGetUsbTxBuf UsbTxBufQ inconsistent: TxBufHead: %d, TxBufTail: %d\n",
                    macp->TxBufHead, macp->TxBufTail);
        }

        spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
        return NULL;
    }

    spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
    return TxQ;
}

u16_t zfLnxPutUsbTxBuffer(zdev_t *dev, u8_t *hdr, u16_t hdrlen,
        u8_t *snap, u16_t snapLen, u8_t *tail, u16_t tailLen,
        zbuf_t *buf, u16_t offset)
{
    struct usbdrv_private *macp = dev->ml_priv;
    u16_t idx;
    UsbTxQ_t *TxQ;
    unsigned long irqFlag;

    spin_lock_irqsave(&macp->cs_lock, irqFlag);

    idx = ((macp->TxBufTail+1) & (ZM_MAX_TX_BUF_NUM - 1));

    /* For Tx debug */
    //zm_assert(macp->TxBufCnt >= 0); // deleted because of always true

    //if (idx != macp->TxBufHead)
    if (macp->TxBufCnt < ZM_MAX_TX_BUF_NUM)
    {
        //printk("CWY - zfwPutUsbTxBuffer ,macp->TxBufCnt = %d\n", macp->TxBufCnt);
        TxQ = (UsbTxQ_t *)&(macp->UsbTxBufQ[macp->TxBufTail]);
        memcpy(TxQ->hdr, hdr, hdrlen);
        TxQ->hdrlen = hdrlen;
        memcpy(TxQ->snap, snap, snapLen);
        TxQ->snapLen = snapLen;
        memcpy(TxQ->tail, tail, tailLen);
        TxQ->tailLen = tailLen;
        TxQ->buf = buf;
        TxQ->offset = offset;

        macp->TxBufTail = ((macp->TxBufTail+1) & (ZM_MAX_TX_BUF_NUM - 1));
        macp->TxBufCnt++;
    }
    else
    {
        printk(KERN_ERR "zfLnxPutUsbTxBuffer UsbTxBufQ inconsistent: TxBufHead: %d, TxBufTail: %d, TxBufCnt: %d\n",
            macp->TxBufHead, macp->TxBufTail, macp->TxBufCnt);
        spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
        return 0xffff;
    }

    spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
    return 0;
}

zbuf_t *zfLnxGetUsbRxBuffer(zdev_t *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;
    //u16_t idx;
    zbuf_t *buf;
    unsigned long irqFlag;

    spin_lock_irqsave(&macp->cs_lock, irqFlag);

    //idx = ((macp->RxBufHead+1) & (ZM_MAX_RX_URB_NUM - 1));

    //if (idx != macp->RxBufTail)
    if (macp->RxBufCnt != 0)
    {
        buf = macp->UsbRxBufQ[macp->RxBufHead];
        macp->RxBufHead = ((macp->RxBufHead+1) & (ZM_MAX_RX_URB_NUM - 1));
        macp->RxBufCnt--;
    }
    else
    {
        printk("RxBufQ inconsistent: RxBufHead: %d, RxBufTail: %d\n",
                macp->RxBufHead, macp->RxBufTail);
        spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
        return NULL;
    }

    spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
    return buf;
}

u32_t zfLnxPutUsbRxBuffer(zdev_t *dev, zbuf_t *buf)
{
    struct usbdrv_private *macp = dev->ml_priv;
    u16_t idx;
    unsigned long irqFlag;

    spin_lock_irqsave(&macp->cs_lock, irqFlag);

    idx = ((macp->RxBufTail+1) & (ZM_MAX_RX_URB_NUM - 1));

    //if (idx != macp->RxBufHead)
    if (macp->RxBufCnt != ZM_MAX_RX_URB_NUM)
    {
        macp->UsbRxBufQ[macp->RxBufTail] = buf;
        macp->RxBufTail = idx;
        macp->RxBufCnt++;
    }
    else
    {
        printk("RxBufQ inconsistent: RxBufHead: %d, RxBufTail: %d\n",
                macp->RxBufHead, macp->RxBufTail);
        spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
        return 0xffff;
    }

    spin_unlock_irqrestore(&macp->cs_lock, irqFlag);
    return 0;
}

void zfLnxUsbDataOut_callback(urb_t *urb)
{
    zdev_t* dev = urb->context;
    //UsbTxQ_t *TxData;

    /* Give the urb back */
    zfLnxPutTxUrb(dev);

    /* Check whether there is any pending buffer needed */
    /* to be sent */
    if (zfLnxCheckTxBufferCnt(dev) != 0)
    {
        //TxData = zfwGetUsbTxBuffer(dev);

        //if (TxData == NULL)
        //{
        //    printk("Get a NULL buffer from zfwGetUsbTxBuffer\n");
        //    return;
        //}
        //else
        //{
            zfLnxUsbSubmitTxData(dev);
        //}
    }
}

void zfLnxUsbDataIn_callback(urb_t *urb)
{
    zdev_t* dev = urb->context;
    struct usbdrv_private *macp = dev->ml_priv;
    zbuf_t *buf;
    zbuf_t *new_buf;
    int status;

#if ZM_USB_STREAM_MODE == 1
    static int remain_len = 0, check_pad = 0, check_len = 0;
    int index = 0;
    int chk_idx;
    u16_t pkt_len;
    u16_t pkt_tag;
    u16_t ii;
    zbuf_t *rxBufPool[8];
    u16_t rxBufPoolIndex = 0;
#endif

    /* Check status for URB */
    if (urb->status != 0){
        printk("zfLnxUsbDataIn_callback() : status=0x%x\n", urb->status);
        if ((urb->status != -ENOENT) && (urb->status != -ECONNRESET)
            && (urb->status != -ESHUTDOWN))
        {
                if (urb->status == -EPIPE){
                    //printk(KERN_ERR "nonzero read bulk status received: -EPIPE");
                    status = -1;
                }

                if (urb->status == -EPROTO){
                    //printk(KERN_ERR "nonzero read bulk status received: -EPROTO");
                    status = -1;
                }
        }

        //printk(KERN_ERR "urb->status: 0x%08x\n", urb->status);

        /* Dequeue skb buffer */
        buf = zfLnxGetUsbRxBuffer(dev);
        dev_kfree_skb_any(buf);
        #if 0
        /* Enqueue skb buffer */
        zfLnxPutUsbRxBuffer(dev, buf);

        /* Submit a Rx urb */
        zfLnxUsbIn(dev, urb, buf);
        #endif
        return;
    }

    if (urb->actual_length == 0)
    {
        printk(KERN_ERR "Get an URB whose length is zero");
        status = -1;
    }

    /* Dequeue skb buffer */
    buf = zfLnxGetUsbRxBuffer(dev);

    //zfwBufSetSize(dev, buf, urb->actual_length);
#ifdef NET_SKBUFF_DATA_USES_OFFSET
    buf->tail = 0;
    buf->len = 0;
#else
    buf->tail = buf->data;
    buf->len = 0;
#endif

    BUG_ON((buf->tail + urb->actual_length) > buf->end);

    skb_put(buf, urb->actual_length);

#if ZM_USB_STREAM_MODE == 1
    if (remain_len != 0)
    {
        zbuf_t *remain_buf = macp->reamin_buf;

        index = remain_len;
        remain_len -= check_pad;

        /*  Copy data */
        memcpy(&(remain_buf->data[check_len]), buf->data, remain_len);
        check_len += remain_len;
        remain_len = 0;

        rxBufPool[rxBufPoolIndex++] = remain_buf;
    }

    while(index < urb->actual_length)
    {
        pkt_len = buf->data[index] + (buf->data[index+1] << 8);
        pkt_tag = buf->data[index+2] + (buf->data[index+3] << 8);

        if (pkt_tag == 0x4e00)
        {
            int pad_len;

            //printk("Get a packet, index: %d, pkt_len: 0x%04x\n", index, pkt_len);
            #if 0
            /* Dump data */
            for (ii = index; ii < pkt_len+4;)
            {
                printk("%02x ", (buf->data[ii] & 0xff));

                if ((++ii % 16) == 0)
                    printk("\n");
            }

            printk("\n");
            #endif

            pad_len = 4 - (pkt_len & 0x3);

            if(pad_len == 4)
                pad_len = 0;

            chk_idx = index;
            index = index + 4 + pkt_len + pad_len;

            if (index > ZM_MAX_RX_BUFFER_SIZE)
            {
                remain_len = index - ZM_MAX_RX_BUFFER_SIZE; // - pad_len;
                check_len = ZM_MAX_RX_BUFFER_SIZE - chk_idx - 4;
                check_pad = pad_len;

                /* Allocate a skb buffer */
                //new_buf = zfwBufAllocate(dev, ZM_MAX_RX_BUFFER_SIZE);
                new_buf = dev_alloc_skb(ZM_MAX_RX_BUFFER_SIZE);

                /* Set skb buffer length */
            #ifdef NET_SKBUFF_DATA_USES_OFFSET
                new_buf->tail = 0;
                new_buf->len = 0;
            #else
                new_buf->tail = new_buf->data;
                new_buf->len = 0;
            #endif

                skb_put(new_buf, pkt_len);

                /* Copy the buffer */
                memcpy(new_buf->data, &(buf->data[chk_idx+4]), check_len);

                /* Record the buffer pointer */
                macp->reamin_buf = new_buf;
            }
            else
            {
        #ifdef ZM_DONT_COPY_RX_BUFFER
                if (rxBufPoolIndex == 0)
                {
                    new_buf = skb_clone(buf, GFP_ATOMIC);

                    new_buf->data = &(buf->data[chk_idx+4]);
                    new_buf->len = pkt_len;
                }
                else
                {
        #endif
                /* Allocate a skb buffer */
                new_buf = dev_alloc_skb(ZM_MAX_RX_BUFFER_SIZE);

                /* Set skb buffer length */
            #ifdef NET_SKBUFF_DATA_USES_OFFSET
                new_buf->tail = 0;
                new_buf->len = 0;
            #else
                new_buf->tail = new_buf->data;
                new_buf->len = 0;
            #endif

                skb_put(new_buf, pkt_len);

                /* Copy the buffer */
                memcpy(new_buf->data, &(buf->data[chk_idx+4]), pkt_len);

        #ifdef ZM_DONT_COPY_RX_BUFFER
                }
        #endif
                rxBufPool[rxBufPoolIndex++] = new_buf;
            }
        }
        else
        {
            printk(KERN_ERR "Can't find tag, pkt_len: 0x%04x, tag: 0x%04x\n", pkt_len, pkt_tag);

            /* Free buffer */
            dev_kfree_skb_any(buf);

            /* Allocate a skb buffer */
            new_buf = dev_alloc_skb(ZM_MAX_RX_BUFFER_SIZE);

            /* Enqueue skb buffer */
            zfLnxPutUsbRxBuffer(dev, new_buf);

            /* Submit a Rx urb */
            zfLnxUsbIn(dev, urb, new_buf);

            return;
        }
    }

    /* Free buffer */
    dev_kfree_skb_any(buf);
#endif

    /* Allocate a skb buffer */
    new_buf = dev_alloc_skb(ZM_MAX_RX_BUFFER_SIZE);

    /* Enqueue skb buffer */
    zfLnxPutUsbRxBuffer(dev, new_buf);

    /* Submit a Rx urb */
    zfLnxUsbIn(dev, urb, new_buf);

#if ZM_USB_STREAM_MODE == 1
    for(ii = 0; ii < rxBufPoolIndex; ii++)
    {
        macp->usbCbFunctions.zfcbUsbRecv(dev, rxBufPool[ii]);
    }
#else
    /* pass data to upper layer */
    macp->usbCbFunctions.zfcbUsbRecv(dev, buf);
#endif
}

void zfLnxUsbRegOut_callback(urb_t *urb)
{
    //dev_t* dev = urb->context;

    //printk(KERN_ERR "zfwUsbRegOut_callback\n");
}

void zfLnxUsbRegIn_callback(urb_t *urb)
{
    zdev_t* dev = urb->context;
    u32_t rsp[64/4];
    int status;
    struct usbdrv_private *macp = dev->ml_priv;

    /* Check status for URB */
    if (urb->status != 0){
        printk("zfLnxUsbRegIn_callback() : status=0x%x\n", urb->status);
        if ((urb->status != -ENOENT) && (urb->status != -ECONNRESET)
            && (urb->status != -ESHUTDOWN))
        {
                if (urb->status == -EPIPE){
                    //printk(KERN_ERR "nonzero read bulk status received: -EPIPE");
                    status = -1;
                }

                if (urb->status == -EPROTO){
                    //printk(KERN_ERR "nonzero read bulk status received: -EPROTO");
                    status = -1;
                }
        }

        //printk(KERN_ERR "urb->status: 0x%08x\n", urb->status);
        return;
    }

    if (urb->actual_length == 0)
    {
        printk(KERN_ERR "Get an URB whose length is zero");
        status = -1;
    }

    /* Copy data into respone buffer */
    memcpy(rsp, macp->regUsbReadBuf, urb->actual_length);

    /* Notify to upper layer */
    //zfIdlChkRsp(dev, rsp, (u16_t)urb->actual_length);
    //zfiUsbRegIn(dev, rsp, (u16_t)urb->actual_length);
    macp->usbCbFunctions.zfcbUsbRegIn(dev, rsp, (u16_t)urb->actual_length);

    /* Issue another USB IN URB */
    zfLnxSubmitRegInUrb(dev);
}

u32_t zfLnxSubmitRegInUrb(zdev_t *dev)
{
    u32_t ret;
    struct usbdrv_private *macp = dev->ml_priv;

    /* Submit a rx urb */
    //ret = zfLnxUsbSubmitBulkUrb(macp->RegInUrb, macp->udev,
    //        USB_REG_IN_PIPE, USB_DIR_IN, macp->regUsbReadBuf,
    //        ZM_USB_REG_MAX_BUF_SIZE, zfLnxUsbRegIn_callback, dev);
    //CWYang(-)
    //if (ret != 0)
    //    printk("zfwUsbSubmitBulkUrb fail, status: 0x%08x\n", (int)ret);

    ret = zfLnxUsbSubmitIntUrb(macp->RegInUrb, macp->udev,
            USB_REG_IN_PIPE, USB_DIR_IN, macp->regUsbReadBuf,
            ZM_USB_REG_MAX_BUF_SIZE, zfLnxUsbRegIn_callback, dev, 1);

    return ret;
}

u32_t zfLnxUsbSubmitTxData(zdev_t* dev)
{
    u32_t i;
    u32_t ret;
    u16_t freeTxUrb;
    u8_t *puTxBuf = NULL;
    UsbTxQ_t *TxData;
    int len = 0;
    struct usbdrv_private *macp = dev->ml_priv;
#if ZM_USB_TX_STREAM_MODE == 1
    u8_t               ii;
    u16_t              offset = 0;
    u16_t              usbTxAggCnt;
    u16_t              *pUsbTxHdr;
    UsbTxQ_t           *TxQPool[ZM_MAX_TX_AGGREGATE_NUM];
#endif

    /* First check whether there is a free URB */
    freeTxUrb = zfLnxGetFreeTxUrb(dev);

    /* If there is no any free Tx Urb */
    if (freeTxUrb == 0xffff)
    {
        //printk(KERN_ERR "Can't get free Tx Urb\n");
        //printk("CWY - Can't get free Tx Urb\n");
        return 0xffff;
    }

#if ZM_USB_TX_STREAM_MODE == 1
    usbTxAggCnt = zfLnxCheckTxBufferCnt(dev);

    if (usbTxAggCnt >= ZM_MAX_TX_AGGREGATE_NUM)
    {
       usbTxAggCnt = ZM_MAX_TX_AGGREGATE_NUM;
    }
    else
    {
       usbTxAggCnt = 1;
    }

    //printk("usbTxAggCnt: %d\n", usbTxAggCnt);
#endif

#if ZM_USB_TX_STREAM_MODE == 1
    for(ii = 0; ii < usbTxAggCnt; ii++)
    {
#endif
    /* Dequeue the packet from UsbTxBufQ */
    TxData = zfLnxGetUsbTxBuffer(dev);
    if (TxData == NULL)
    {
        /* Give the urb back */
        zfLnxPutTxUrb(dev);
        return 0xffff;
    }

    /* Point to the freeTxUrb buffer */
    puTxBuf = macp->txUsbBuf[freeTxUrb];

#if ZM_USB_TX_STREAM_MODE == 1
    puTxBuf += offset;
    pUsbTxHdr = (u16_t *)puTxBuf;

    /* Add the packet length and tag information */
    *pUsbTxHdr++ = TxData->hdrlen + TxData->snapLen +
             (TxData->buf->len - TxData->offset) +  TxData->tailLen;

    *pUsbTxHdr++ = 0x697e;

    puTxBuf += 4;
#endif // #ifdef ZM_USB_TX_STREAM_MODE

    /* Copy WLAN header and packet buffer into USB buffer */
    for(i = 0; i < TxData->hdrlen; i++)
    {
        *puTxBuf++ = TxData->hdr[i];
    }

    /* Copy SNAP header */
    for(i = 0; i < TxData->snapLen; i++)
    {
        *puTxBuf++ = TxData->snap[i];
    }

    /* Copy packet buffer */
    for(i = 0; i < TxData->buf->len - TxData->offset; i++)
    {
    	//*puTxBuf++ = zmw_rx_buf_readb(dev, TxData->buf, i);
    	*puTxBuf++ = *(u8_t*)((u8_t*)TxData->buf->data+i+TxData->offset);
    }

    /* Copy tail */
    for(i = 0; i < TxData->tailLen; i++)
    {
        *puTxBuf++ = TxData->tail[i];
    }

    len = TxData->hdrlen+TxData->snapLen+TxData->buf->len+TxData->tailLen-TxData->offset;

    #if 0
    if (TxData->hdrlen != 0)
    {
        puTxBuf = macp->txUsbBuf[freeTxUrb];
        for (i = 0; i < len; i++)
        {
            printk("%02x ", puTxBuf[i]);
            if (i % 16 == 15)
                printk("\n");
        }
        printk("\n");
    }
    #endif
    #if 0
    /* For debug purpose */
    if(TxData->hdr[9] & 0x40)
    {
        int i;
        u16_t ctrlLen = TxData->hdr[0] + (TxData->hdr[1] << 8);

        if (ctrlLen != len + 4)
        {
        /* Dump control setting */
        for(i = 0; i < 8; i++)
        {
            printk(KERN_ERR "0x%02x ", TxData->hdr[i]);
        }
        printk(KERN_ERR "\n");

        printk(KERN_ERR "ctrLen: %d, hdrLen: %d, snapLen: %d\n", ctrlLen, TxData->hdrlen, TxData->snapLen);
        printk(KERN_ERR "bufLen: %d, tailLen: %d, len: %d\n", TxData->buf->len, TxData->tailLen, len);
        }
    }
    #endif

#if ZM_USB_TX_STREAM_MODE == 1
    // Add the Length and Tag
    len += 4;

    //printk("%d packet, length: %d\n", ii+1, len);

    if (ii < (ZM_MAX_TX_AGGREGATE_NUM-1))
    {
        /* Pad the buffer to firmware descriptor boundary */
        offset += (((len-1) / 4) + 1) * 4;
    }

    if (ii == (ZM_MAX_TX_AGGREGATE_NUM-1))
    {
        len += offset;
    }

    TxQPool[ii] = TxData;

    //DbgPrint("%d packet, offset: %d\n", ii+1, pUsbTxTransfer->offset);

    /* free packet */
    //zfBufFree(dev, txData->buf);
    }
#endif
    //printk("CWY - call zfwUsbSubmitBulkUrb(), len = 0x%d\n", len);
    /* Submit a tx urb */
    ret = zfLnxUsbSubmitBulkUrb(macp->WlanTxDataUrb[freeTxUrb], macp->udev,
            USB_WLAN_TX_PIPE, USB_DIR_OUT, macp->txUsbBuf[freeTxUrb],
            len, zfLnxUsbDataOut_callback, dev);
    //CWYang(-)
    //if (ret != 0)
    //    printk("zfwUsbSubmitBulkUrb fail, status: 0x%08x\n", (int)ret);

    /* free packet */
    //dev_kfree_skb_any(TxData->buf);
#if ZM_USB_TX_STREAM_MODE == 1
    for(ii = 0; ii < usbTxAggCnt; ii++)
        macp->usbCbFunctions.zfcbUsbOutComplete(dev, TxQPool[ii]->buf, 1, TxQPool[ii]->hdr);
#else
    macp->usbCbFunctions.zfcbUsbOutComplete(dev, TxData->buf, 1, TxData->hdr);
#endif

    return ret;
}



u32_t zfLnxUsbIn(zdev_t* dev, urb_t *urb, zbuf_t *buf)
{
    u32_t ret;
    struct usbdrv_private *macp = dev->ml_priv;

    /* Submit a rx urb */
    ret = zfLnxUsbSubmitBulkUrb(urb, macp->udev, USB_WLAN_RX_PIPE,
            USB_DIR_IN, buf->data, ZM_MAX_RX_BUFFER_SIZE,
            zfLnxUsbDataIn_callback, dev);
    //CWYang(-)
    //if (ret != 0)
    //    printk("zfwUsbSubmitBulkUrb fail, status: 0x%08x\n", (int)ret);

    return ret;
}

u32_t zfLnxUsbWriteReg(zdev_t* dev, u32_t* cmd, u16_t cmdLen)
{
    struct usbdrv_private *macp = dev->ml_priv;
    u32_t ret;

#ifdef ZM_CONFIG_BIG_ENDIAN
    int ii = 0;

    for(ii=0; ii<(cmdLen>>2); ii++)
	cmd[ii] = cpu_to_le32(cmd[ii]);
#endif

    memcpy(macp->regUsbWriteBuf, cmd, cmdLen);

    /* Issue an USB Out transfer */
    /* Submit a tx urb */
    ret = zfLnxUsbSubmitIntUrb(macp->RegOutUrb, macp->udev,
            USB_REG_OUT_PIPE, USB_DIR_OUT, macp->regUsbWriteBuf,
            cmdLen, zfLnxUsbRegOut_callback, dev, 1);

    return ret;
}


u32_t zfLnxUsbOut(zdev_t* dev, u8_t *hdr, u16_t hdrlen, u8_t *snap, u16_t snapLen,
        u8_t *tail, u16_t tailLen, zbuf_t *buf, u16_t offset)
{
    u32_t ret;
    struct usbdrv_private *macp = dev->ml_priv;

    /* Check length of tail buffer */
    //zm_assert((tailLen <= 16));

    /* Enqueue the packet into UsbTxBufQ */
    if (zfLnxPutUsbTxBuffer(dev, hdr, hdrlen, snap, snapLen, tail, tailLen, buf, offset) == 0xffff)
    {
        /* free packet */
        //printk("CWY - zfwPutUsbTxBuffer Error, free packet\n");
        //dev_kfree_skb_any(buf);
        macp->usbCbFunctions.zfcbUsbOutComplete(dev, buf, 0, hdr);
        return 0xffff;
    }

    //return 0;
    //printk("CWY - call zfwUsbSubmitTxData()\n");
    ret = zfLnxUsbSubmitTxData(dev);
    return ret;
}

void zfLnxInitUsbTxQ(zdev_t* dev)
{
    struct usbdrv_private *macp = dev->ml_priv;

    printk(KERN_ERR "zfwInitUsbTxQ\n");

    /* Zero memory for UsbTxBufQ */
    memset(macp->UsbTxBufQ, 0, sizeof(UsbTxQ_t) * ZM_MAX_TX_URB_NUM);

    macp->TxBufHead = 0;
    macp->TxBufTail = 0;
    macp->TxUrbHead = 0;
    macp->TxUrbTail = 0;
    macp->TxUrbCnt = ZM_MAX_TX_URB_NUM;
}

void zfLnxInitUsbRxQ(zdev_t* dev)
{
    u16_t i;
    zbuf_t *buf;
    struct usbdrv_private *macp = dev->ml_priv;

    /* Zero memory for UsbRxBufQ */
    memset(macp->UsbRxBufQ, 0, sizeof(zbuf_t *) * ZM_MAX_RX_URB_NUM);

    macp->RxBufHead = 0;

    for (i = 0; i < ZM_MAX_RX_URB_NUM; i++)
    {
        //buf = zfwBufAllocate(dev, ZM_MAX_RX_BUFFER_SIZE);
        buf = dev_alloc_skb(ZM_MAX_RX_BUFFER_SIZE);
        macp->UsbRxBufQ[i] = buf;
    }

    //macp->RxBufTail = ZM_MAX_RX_URB_NUM - 1;
    macp->RxBufTail = 0;

    /* Submit all Rx urbs */
    for (i = 0; i < ZM_MAX_RX_URB_NUM; i++)
    {
        zfLnxPutUsbRxBuffer(dev, macp->UsbRxBufQ[i]);
        zfLnxUsbIn(dev, macp->WlanRxDataUrb[i], macp->UsbRxBufQ[i]);
    }
}



u32_t zfLnxUsbSubmitBulkUrb(urb_t *urb, struct usb_device *usb, u16_t epnum, u16_t direction,
        void *transfer_buffer, int buffer_length, usb_complete_t complete, void *context)
{
    u32_t ret;

    if(direction == USB_DIR_OUT)
    {
        usb_fill_bulk_urb(urb, usb, usb_sndbulkpipe(usb, epnum),
                transfer_buffer, buffer_length, complete, context);

        urb->transfer_flags |= URB_ZERO_PACKET;
    }
    else
    {
        usb_fill_bulk_urb(urb, usb, usb_rcvbulkpipe(usb, epnum),
                transfer_buffer, buffer_length, complete, context);
    }

    if (epnum == 4)
    {
        if (urb->hcpriv)
        {
            //printk("CWY - urb->hcpriv set by unknown reason, reset it\n");
            //urb->hcpriv = 0;
        }
    }

    ret = usb_submit_urb(urb, GFP_ATOMIC);
    if ((epnum == 4) & (ret != 0))
    {
        //printk("CWY - ret = %x\n", ret);
    }
    return ret;
}

u32_t zfLnxUsbSubmitIntUrb(urb_t *urb, struct usb_device *usb, u16_t epnum, u16_t direction,
        void *transfer_buffer, int buffer_length, usb_complete_t complete, void *context,
        u32_t interval)
{
    u32_t ret;

    if(direction == USB_DIR_OUT)
    {
        usb_fill_int_urb(urb, usb, usb_sndbulkpipe(usb, epnum),
                transfer_buffer, buffer_length, complete, context, interval);
    }
    else
    {
        usb_fill_int_urb(urb, usb, usb_rcvbulkpipe(usb, epnum),
                transfer_buffer, buffer_length, complete, context, interval);
    }

    ret = usb_submit_urb(urb, GFP_ATOMIC);

    return ret;
}

#ifdef ZM_ENABLE_CENC
int zfLnxCencSendMsg(struct sock *netlink_sk, u_int8_t *msg, int len)
{
#define COMMTYPE_GROUP   8
#define WAI_K_MSG        0x11

	int ret = -1;
	int size;
	unsigned char *old_tail;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char *pos = NULL;

	size = NLMSG_SPACE(len);
	skb = alloc_skb(size, GFP_ATOMIC);

	if(skb == NULL)
	{
		printk("dev_alloc_skb failure \n");
		goto out;
	}
	old_tail = skb->tail;

	/*填写数据报相关信息*/
	nlh = NLMSG_PUT(skb, 0, 0, WAI_K_MSG, size-sizeof(*nlh));
	pos = NLMSG_DATA(nlh);
	memset(pos, 0, len);

	/*传输到用户空间的数据*/
	memcpy(pos, msg,  len);
	/*计算经过字节对其后的数据实际长度*/
	nlh->nlmsg_len = skb->tail - old_tail;
	NETLINK_CB(skb).dst_group = COMMTYPE_GROUP;
	netlink_broadcast(netlink_sk, skb, 0, COMMTYPE_GROUP, GFP_ATOMIC);
	ret = 0;
out:
	return ret;
nlmsg_failure: /*NLMSG_PUT 失败，则撤销套接字缓存*/
	kfree_skb(skb);
	goto out;

#undef COMMTYPE_GROUP
#undef WAI_K_MSG
}
#endif //ZM_ENABLE_CENC

/* Simply return 0xffff if VAP function is not supported */
u16_t zfLnxGetVapId(zdev_t* dev)
{
    u16_t i;

    for (i=0; i<ZM_VAP_PORT_NUMBER; i++)
    {
        if (vap[i].dev == dev)
        {
            return i;
        }
    }
    return 0xffff;
}

u32_t zfwReadReg(zdev_t* dev, u32_t offset)
{
    return 0;
}

#ifndef INIT_WORK
#define work_struct tq_struct

#define schedule_work(a)  schedule_task(a)

#define flush_scheduled_work  flush_scheduled_tasks
#define INIT_WORK(_wq, _routine, _data)  INIT_TQUEUE(_wq, _routine, _data)
#define PREPARE_WORK(_wq, _routine, _data)  PREPARE_TQUEUE(_wq, _routine, _data)
#endif

#define KEVENT_WATCHDOG        0x00000001

u32_t smp_kevent_Lock = 0;

void kevent(struct work_struct *work)
{
    struct usbdrv_private *macp =
               container_of(work, struct usbdrv_private, kevent);
    zdev_t *dev = macp->device;

    if (test_and_set_bit(0, (void *)&smp_kevent_Lock))
    {
        //schedule_work(&macp->kevent);
        return;
    }

    down(&macp->ioctl_sem);

    if (test_and_clear_bit(KEVENT_WATCHDOG, &macp->kevent_flags))
    {
    extern u16_t zfHpStartRecv(zdev_t *dev);
        //zfiHwWatchDogReinit(dev);
        printk(("\n ************ Hw watchDog occur!! ************** \n"));
        zfiWlanSuspend(dev);
        zfiWlanResume(dev,0);
        zfHpStartRecv(dev);
    }

    clear_bit(0, (void *)&smp_kevent_Lock);
    up(&macp->ioctl_sem);
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                 zfLnxCreateThread            */
/*      Create a Thread                                                 */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      always 0                                                        */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Yuan-Gu Wei         Atheros Communications, INC.    2007.3      */
/*                                                                      */
/************************************************************************/
u8_t zfLnxCreateThread(zdev_t *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;

    /* Create Mutex and keventd */
    INIT_WORK(&macp->kevent, kevent);
    init_MUTEX(&macp->ioctl_sem);

    return 0;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                 zfLnxSignalThread            */
/*      Signal Thread with Flag                                         */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      flag : signal thread flag                                       */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      none                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Yuan-Gu Wei         Atheros Communications, INC.    2007.3      */
/*                                                                      */
/************************************************************************/
void zfLnxSignalThread(zdev_t *dev, int flag)
{
    struct usbdrv_private *macp = dev->ml_priv;

    if (macp == NULL)
    {
        printk("macp is NULL\n");
        return;
    }

    if (0 && macp->kevent_ready != 1)
    {
        printk("Kevent not ready\n");
        return;
    }

    set_bit(flag, &macp->kevent_flags);

    if (!schedule_work(&macp->kevent))
    {
        //Fails is Normal
        //printk(KERN_ERR "schedule_task failed, flag = %x\n", flag);
    }
}

/* Notify wrapper todo redownload firmware and reinit procedure when */
/* hardware watchdog occur : zfiHwWatchDogReinit() */
void zfLnxWatchDogNotify(zdev_t* dev)
{
    zfLnxSignalThread(dev, KEVENT_WATCHDOG);
}

/* Query Durantion of Active Scan */
void zfwGetActiveScanDur(zdev_t* dev, u8_t* Dur)
{
    *Dur = 30; // default 30 ms
}

void zfwGetShowZeroLengthSSID(zdev_t* dev, u8_t* Dur)
{
    *Dur = 0;
}

