/**
 * aicwf_usb.h
 *
 * USB function declarations
 *
 * Copyright (C) AICSemi 2018-2020
 */

#ifndef _AICWF_USB_H_
#define _AICWF_USB_H_

#include <linux/usb.h>
#include "rwnx_cmds.h"

#ifdef AICWF_USB_SUPPORT

/* USB Device ID */
#define USB_VENDOR_ID_AIC                0xA69C

#ifndef CONFIG_USB_BT
#define USB_PRODUCT_ID_AIC8800               0x8800
#else
#define USB_PRODUCT_ID_AIC8801				0x8801
#define USB_PRODUCT_ID_AIC8800DC			0x88dc
#define USB_PRODUCT_ID_AIC8800DW            0x88dd
#endif

enum AICWF_IC{
	PRODUCT_ID_AIC8801	=	0,
	PRODUCT_ID_AIC8800DC,
	PRODUCT_ID_AIC8800DW,
	PRODUCT_ID_AIC8800D80
};


#define AICWF_USB_RX_URBS               (200)//(200)
#ifdef CONFIG_USB_MSG_IN_EP
#define AICWF_USB_MSG_RX_URBS           (100)
#endif
#ifdef CONFIG_USB_TX_AGGR
#define TXQLEN                          (2048*4)
#define AICWF_USB_TX_URBS               (50)
#else
#define AICWF_USB_TX_URBS               200//(100)
#endif
#define AICWF_USB_TX_LOW_WATER         (AICWF_USB_TX_URBS/4)//25%
#define AICWF_USB_TX_HIGH_WATER        (AICWF_USB_TX_LOW_WATER*3)//75%
#ifdef CONFIG_USB_RX_AGGR
#define AICWF_USB_MAX_PKT_SIZE          (2048*30)
#else
#define AICWF_USB_MAX_PKT_SIZE          (2048)
#endif
#define AICWF_USB_FC_PERSTA_HIGH_WATER		64
#define AICWF_USB_FC_PERSTA_LOW_WATER		16


typedef enum {
    USB_TYPE_DATA         = 0X00,
    USB_TYPE_CFG          = 0X10,
    USB_TYPE_CFG_CMD_RSP  = 0X11,
    USB_TYPE_CFG_DATA_CFM = 0X12,
    USB_TYPE_CFG_PRINT    = 0X13
} usb_type;

enum aicwf_usb_state {
    USB_DOWN_ST,
    USB_UP_ST,
    USB_SLEEP_ST
};

struct aicwf_usb_buf {
    struct list_head list;
    struct aic_usb_dev *usbdev;
    struct urb *urb;
    struct sk_buff *skb;
#ifdef CONFIG_PREALLOC_RX_SKB
    struct rx_buff *rx_buff;
#endif
    #ifdef CONFIG_USB_NO_TRANS_DMA_MAP
    u8 *data_buf;
    dma_addr_t data_dma_trans_addr;
    #endif
    bool cfm;
    #ifdef CONFIG_USB_TX_AGGR
    u8 aggr_cnt;
    #endif
	u8* usb_align_data;
};

struct aic_usb_dev {
    struct rwnx_hw *rwnx_hw;
    struct aicwf_bus *bus_if;
    struct usb_device *udev;
    struct device *dev;
    struct aicwf_rx_priv* rx_priv;
    enum aicwf_usb_state state;
    struct rwnx_cmd_mgr cmd_mgr;

#ifdef CONFIG_USB_TX_AGGR
    struct aicwf_tx_priv *tx_priv;
#endif

    struct usb_anchor rx_submitted;
    struct work_struct rx_urb_work;
#ifdef CONFIG_USB_MSG_IN_EP
	struct usb_anchor msg_rx_submitted;
	struct work_struct msg_rx_urb_work;
#endif

    spinlock_t rx_free_lock;
    spinlock_t tx_free_lock;
    spinlock_t tx_post_lock;
    spinlock_t tx_flow_lock;
#ifdef CONFIG_USB_MSG_IN_EP
	spinlock_t msg_rx_free_lock;
#endif

    struct list_head rx_free_list;
    struct list_head tx_free_list;
    struct list_head tx_post_list;
#ifdef CONFIG_USB_MSG_IN_EP
	struct list_head msg_rx_free_list;
#endif

    uint bulk_in_pipe;
    uint bulk_out_pipe;
#ifdef CONFIG_USB_MSG_OUT_EP
    uint msg_out_pipe;
#endif
#ifdef CONFIG_USB_MSG_IN_EP
	uint msg_in_pipe;
#endif

    int tx_free_count;
    int tx_post_count;

    struct aicwf_usb_buf usb_tx_buf[AICWF_USB_TX_URBS];
    struct aicwf_usb_buf usb_rx_buf[AICWF_USB_RX_URBS];
#ifdef CONFIG_USB_MSG_IN_EP
	struct aicwf_usb_buf usb_msg_rx_buf[AICWF_USB_MSG_RX_URBS];
#endif

    int msg_finished;
    wait_queue_head_t msg_wait;
    ulong msg_busy;
    struct urb *msg_out_urb;
    #ifdef CONFIG_USB_NO_TRANS_DMA_MAP
    dma_addr_t cmd_dma_trans_addr;
    #endif

#ifdef CONFIG_RX_TASKLET//AIDEN tasklet
	struct tasklet_struct recv_tasklet;
#endif
#ifdef CONFIG_TX_TASKLET//AIDEN tasklet
	struct tasklet_struct xmit_tasklet;
#endif
	u16 chipid;
    bool tbusy;
};

extern void aicwf_usb_exit(void);
extern void aicwf_usb_register(void);
extern void aicwf_usb_tx_flowctrl(struct rwnx_hw *rwnx_hw, bool state);
#ifdef CONFIG_USB_MSG_IN_EP
int usb_msg_busrx_thread(void *data);
#endif
int usb_bustx_thread(void *data);
int usb_busrx_thread(void *data);


extern void aicwf_hostif_ready(void);

#endif /* AICWF_USB_SUPPORT */
#endif /* _AICWF_USB_H_       */
