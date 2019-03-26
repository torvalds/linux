/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
 * Copyright (c) 2016-2017 Erik Stromdahl <erik.stromdahl@gmail.com>
 */

#ifndef _USB_H_
#define _USB_H_

/* constants */
#define TX_URB_COUNT               32
#define RX_URB_COUNT               32
#define ATH10K_USB_RX_BUFFER_SIZE  4096

#define ATH10K_USB_PIPE_INVALID ATH10K_USB_PIPE_MAX

/* USB endpoint definitions */
#define ATH10K_USB_EP_ADDR_APP_CTRL_IN          0x81
#define ATH10K_USB_EP_ADDR_APP_DATA_IN          0x82
#define ATH10K_USB_EP_ADDR_APP_DATA2_IN         0x83
#define ATH10K_USB_EP_ADDR_APP_INT_IN           0x84

#define ATH10K_USB_EP_ADDR_APP_CTRL_OUT         0x01
#define ATH10K_USB_EP_ADDR_APP_DATA_LP_OUT      0x02
#define ATH10K_USB_EP_ADDR_APP_DATA_MP_OUT      0x03
#define ATH10K_USB_EP_ADDR_APP_DATA_HP_OUT      0x04

/* diagnostic command defnitions */
#define ATH10K_USB_CONTROL_REQ_SEND_BMI_CMD        1
#define ATH10K_USB_CONTROL_REQ_RECV_BMI_RESP       2
#define ATH10K_USB_CONTROL_REQ_DIAG_CMD            3
#define ATH10K_USB_CONTROL_REQ_DIAG_RESP           4

#define ATH10K_USB_CTRL_DIAG_CC_READ               0
#define ATH10K_USB_CTRL_DIAG_CC_WRITE              1

#define ATH10K_USB_IS_BULK_EP(attr) (((attr) & 3) == 0x02)
#define ATH10K_USB_IS_INT_EP(attr)  (((attr) & 3) == 0x03)
#define ATH10K_USB_IS_ISOC_EP(attr) (((attr) & 3) == 0x01)
#define ATH10K_USB_IS_DIR_IN(addr)  ((addr) & 0x80)

struct ath10k_usb_ctrl_diag_cmd_write {
	__le32 cmd;
	__le32 address;
	__le32 value;
	__le32 padding;
} __packed;

struct ath10k_usb_ctrl_diag_cmd_read {
	__le32 cmd;
	__le32 address;
} __packed;

struct ath10k_usb_ctrl_diag_resp_read {
	u8 value[4];
} __packed;

/* tx/rx pipes for usb */
enum ath10k_usb_pipe_id {
	ATH10K_USB_PIPE_TX_CTRL = 0,
	ATH10K_USB_PIPE_TX_DATA_LP,
	ATH10K_USB_PIPE_TX_DATA_MP,
	ATH10K_USB_PIPE_TX_DATA_HP,
	ATH10K_USB_PIPE_RX_CTRL,
	ATH10K_USB_PIPE_RX_DATA,
	ATH10K_USB_PIPE_RX_DATA2,
	ATH10K_USB_PIPE_RX_INT,
	ATH10K_USB_PIPE_MAX
};

struct ath10k_usb_pipe {
	struct list_head urb_list_head;
	struct usb_anchor urb_submitted;
	u32 urb_alloc;
	u32 urb_cnt;
	u32 urb_cnt_thresh;
	unsigned int usb_pipe_handle;
	u32 flags;
	u8 ep_address;
	u8 logical_pipe_num;
	struct ath10k_usb *ar_usb;
	u16 max_packet_size;
	struct work_struct io_complete_work;
	struct sk_buff_head io_comp_queue;
	struct usb_endpoint_descriptor *ep_desc;
};

#define ATH10K_USB_PIPE_FLAG_TX BIT(0)

/* usb device object */
struct ath10k_usb {
	/* protects pipe->urb_list_head and  pipe->urb_cnt */
	spinlock_t cs_lock;

	struct usb_device *udev;
	struct usb_interface *interface;
	struct ath10k_usb_pipe pipes[ATH10K_USB_PIPE_MAX];
	u8 *diag_cmd_buffer;
	u8 *diag_resp_buffer;
	struct ath10k *ar;
};

/* usb urb object */
struct ath10k_urb_context {
	struct list_head link;
	struct ath10k_usb_pipe *pipe;
	struct sk_buff *skb;
	struct ath10k *ar;
};

static inline struct ath10k_usb *ath10k_usb_priv(struct ath10k *ar)
{
	return (struct ath10k_usb *)ar->drv_priv;
}

#endif
