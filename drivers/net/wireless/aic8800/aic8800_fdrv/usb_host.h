/**
 * usb_host.h
 *
 * USB host function declarations
 *
 * Copyright (C) AICSemi 2018-2020
 */


#ifndef _USB_HOST_H_
#define _USB_HOST_H_

#include "lmac_types.h"
#include "aicwf_usb.h"

#define USB_TXQUEUE_CNT     NX_TXQ_CNT
#define USB_TXDESC_CNT      NX_TXDESC_CNT


/// Definition of the IPC Host environment structure.
struct usb_host_env_tag
{
    // Index used that points to the first free TX desc
    uint32_t txdesc_free_idx[USB_TXQUEUE_CNT];
    // Index used that points to the first used TX desc
    uint32_t txdesc_used_idx[USB_TXQUEUE_CNT];
    // Array storing the currently pushed host ids, per IPC queue
    //uint64_t tx_host_id[USB_TXQUEUE_CNT][USB_TXDESC_CNT];
    unsigned long tx_host_id[USB_TXQUEUE_CNT][USB_TXDESC_CNT];

    /// Pointer to the attached object (used in callbacks and register accesses)
    void *pthis;
};

extern void aicwf_usb_host_init(struct usb_host_env_tag *env,
                  void *cb, void *shared_env_ptr, void *pthis);

extern void aicwf_usb_host_txdesc_push(struct usb_host_env_tag *env, const int queue_idx, const uint64_t host_id);

extern void aicwf_usb_host_tx_cfm_handler(struct usb_host_env_tag *env, u32 *data);
extern int aicwf_rwnx_usb_platform_init(struct aic_usb_dev *usbdev);

#endif
