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

#ifndef _PUB_USB_H
#define _PUB_USB_H

#include "../oal_dt.h"

#define ZM_HAL_80211_MODE_AP              0
#define ZM_HAL_80211_MODE_STA             1
#define ZM_HAL_80211_MODE_IBSS_GENERAL    2
#define ZM_HAL_80211_MODE_IBSS_WPA2PSK    3

/* USB module description                                               */
/* Queue Management                                                     */
/* 80211core requires OAL to implement a transmission queue in OAL's    */
/* USB module. Because there is only limited on-chip memory, so USB     */
/* data transfer may be pending until on-chip memory is available.      */
/* 80211core also requires OAL's USB module to provide two functions    */
/* zfwUsbGetFreeTxQSize() and zfwUsbGetMaxTxQSize() for 80211core to    */
/* query the status of this transmission queue. The main purpose of     */
/* this queue is for QoS/WMM. Though there are hardware priority        */
/* queues on the chip, and also software priority queues in the         */
/* 80211core. There is still one and only one USB channel. So           */
/* 80211core will use the information that zfwUsbGetFreeTxQSize()       */
/* returned to schedule the traffic from the software priority          */
/* queues to the hardware priority queues. For example, if 80211core    */
/* found that USB transmission queue is going to be full, it will       */
/* not allow packets with lower priority to enter the USB channel.      */


/* Structure for USB call back functions */
struct zfCbUsbFuncTbl {
    void (*zfcbUsbRecv)(zdev_t *dev, zbuf_t *buf);
    void (*zfcbUsbRegIn)(zdev_t* dev, u32_t* rsp, u16_t rspLen);
    void (*zfcbUsbOutComplete)(zdev_t* dev, zbuf_t *buf, u8_t status, u8_t *hdr);
    void (*zfcbUsbRegOutComplete)(zdev_t* dev);
};

/* Call back functions                                                  */
/* Below are the functions that should be called by the OAL             */

/* When data is available in endpoint 3, OAL shall embed the data in */
/* zbuf_t and supply to 80211core by calling this function           */
/* void (*zfcbUsbRecv)(zdev_t *dev, zbuf_t *buf); */

/* When data is available in endpoint 2, OAL shall call this function */
/* void (*zfcbUsbRegIn)(zdev_t* dev, u32_t* rsp, u16_t rspLen); */

/* When USB data transfer completed in endpoint 1, OAL shall call this function */
/* void (*zfcbUsbOutComplete)(zdev_t* dev, zbuf_t *buf, u8_t status, u8_t *hdr); */


/* Call out functions                                                   */
/* Below are the functions that supply by the OAL for 80211core to      */
/* manipulate the USB                                                   */

/* Return OAL's USB TxQ size */
extern u32_t zfwUsbGetMaxTxQSize(zdev_t* dev);

/* Return OAL's TxQ available size */
extern u32_t zfwUsbGetFreeTxQSize(zdev_t* dev);

/* Register call back function */
extern void zfwUsbRegisterCallBack(zdev_t* dev, struct zfCbUsbFuncTbl *zfUsbFunc);

/* Enable USB interrupt endpoint */
extern u32_t zfwUsbEnableIntEpt(zdev_t *dev, u8_t endpt);

/* Enable USB Rx endpoint */
extern int zfwUsbEnableRxEpt(zdev_t* dev, u8_t endpt);

/* 80211core call this function to send a USB request over endpoint 0 */
extern u32_t zfwUsbSubmitControl(zdev_t* dev, u8_t req, u16_t value,
        u16_t index, void *data, u32_t size);
extern u32_t zfwUsbSubmitControlIo(zdev_t* dev, u8_t req, u8_t reqtype,
        u16_t value, u16_t index, void *data, u32_t size);

/* 80211core call this function to transfer data out over endpoint 1 */
extern void zfwUsbCmd(zdev_t* dev, u8_t endpt, u32_t* cmd, u16_t cmdLen);

/* 80211core call this function to transfer data out over endpoint 4 */
extern u32_t zfwUsbSend(zdev_t* dev, u8_t endpt, u8_t *hdr, u16_t hdrlen, u8_t *snap, u16_t snapLen,
                u8_t *tail, u16_t tailLen, zbuf_t *buf, u16_t offset);

/* 80211core call this function to set USB configuration */
extern u32_t zfwUsbSetConfiguration(zdev_t *dev, u16_t value);

#endif
