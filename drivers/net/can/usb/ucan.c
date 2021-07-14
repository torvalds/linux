// SPDX-License-Identifier: GPL-2.0

/* Driver for Theobroma Systems UCAN devices, Protocol Version 3
 *
 * Copyright (C) 2018 Theobroma Systems Design und Consulting GmbH
 *
 *
 * General Description:
 *
 * The USB Device uses three Endpoints:
 *
 *   CONTROL Endpoint: Is used the setup the device (start, stop,
 *   info, configure).
 *
 *   IN Endpoint: The device sends CAN Frame Messages and Device
 *   Information using the IN endpoint.
 *
 *   OUT Endpoint: The driver sends configuration requests, and CAN
 *   Frames on the out endpoint.
 *
 * Error Handling:
 *
 *   If error reporting is turned on the device encodes error into CAN
 *   error frames (see uapi/linux/can/error.h) and sends it using the
 *   IN Endpoint. The driver updates statistics and forward it.
 */

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/signal.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/usb.h>

#define UCAN_DRIVER_NAME "ucan"
#define UCAN_MAX_RX_URBS 8
/* the CAN controller needs a while to enable/disable the bus */
#define UCAN_USB_CTL_PIPE_TIMEOUT 1000
/* this driver currently supports protocol version 3 only */
#define UCAN_PROTOCOL_VERSION_MIN 3
#define UCAN_PROTOCOL_VERSION_MAX 3

/* UCAN Message Definitions
 * ------------------------
 *
 *  ucan_message_out_t and ucan_message_in_t define the messages
 *  transmitted on the OUT and IN endpoint.
 *
 *  Multibyte fields are transmitted with little endianness
 *
 *  INTR Endpoint: a single uint32_t storing the current space in the fifo
 *
 *  OUT Endpoint: single message of type ucan_message_out_t is
 *    transmitted on the out endpoint
 *
 *  IN Endpoint: multiple messages ucan_message_in_t concateted in
 *    the following way:
 *
 *	m[n].len <=> the length if message n(including the header in bytes)
 *	m[n] is is aligned to a 4 byte boundary, hence
 *	  offset(m[0])	 := 0;
 *	  offset(m[n+1]) := offset(m[n]) + (m[n].len + 3) & 3
 *
 *	this implies that
 *	  offset(m[n]) % 4 <=> 0
 */

/* Device Global Commands */
enum {
	UCAN_DEVICE_GET_FW_STRING = 0,
};

/* UCAN Commands */
enum {
	/* start the can transceiver - val defines the operation mode */
	UCAN_COMMAND_START = 0,
	/* cancel pending transmissions and stop the can transceiver */
	UCAN_COMMAND_STOP = 1,
	/* send can transceiver into low-power sleep mode */
	UCAN_COMMAND_SLEEP = 2,
	/* wake up can transceiver from low-power sleep mode */
	UCAN_COMMAND_WAKEUP = 3,
	/* reset the can transceiver */
	UCAN_COMMAND_RESET = 4,
	/* get piece of info from the can transceiver - subcmd defines what
	 * piece
	 */
	UCAN_COMMAND_GET = 5,
	/* clear or disable hardware filter - subcmd defines which of the two */
	UCAN_COMMAND_FILTER = 6,
	/* Setup bittiming */
	UCAN_COMMAND_SET_BITTIMING = 7,
	/* recover from bus-off state */
	UCAN_COMMAND_RESTART = 8,
};

/* UCAN_COMMAND_START and UCAN_COMMAND_GET_INFO operation modes (bitmap).
 * Undefined bits must be set to 0.
 */
enum {
	UCAN_MODE_LOOPBACK = BIT(0),
	UCAN_MODE_SILENT = BIT(1),
	UCAN_MODE_3_SAMPLES = BIT(2),
	UCAN_MODE_ONE_SHOT = BIT(3),
	UCAN_MODE_BERR_REPORT = BIT(4),
};

/* UCAN_COMMAND_GET subcommands */
enum {
	UCAN_COMMAND_GET_INFO = 0,
	UCAN_COMMAND_GET_PROTOCOL_VERSION = 1,
};

/* UCAN_COMMAND_FILTER subcommands */
enum {
	UCAN_FILTER_CLEAR = 0,
	UCAN_FILTER_DISABLE = 1,
	UCAN_FILTER_ENABLE = 2,
};

/* OUT endpoint message types */
enum {
	UCAN_OUT_TX = 2,     /* transmit a CAN frame */
};

/* IN endpoint message types */
enum {
	UCAN_IN_TX_COMPLETE = 1,  /* CAN frame transmission completed */
	UCAN_IN_RX = 2,           /* CAN frame received */
};

struct ucan_ctl_cmd_start {
	__le16 mode;         /* OR-ing any of UCAN_MODE_* */
} __packed;

struct ucan_ctl_cmd_set_bittiming {
	__le32 tq;           /* Time quanta (TQ) in nanoseconds */
	__le16 brp;          /* TQ Prescaler */
	__le16 sample_point; /* Samplepoint on tenth percent */
	u8 prop_seg;         /* Propagation segment in TQs */
	u8 phase_seg1;       /* Phase buffer segment 1 in TQs */
	u8 phase_seg2;       /* Phase buffer segment 2 in TQs */
	u8 sjw;              /* Synchronisation jump width in TQs */
} __packed;

struct ucan_ctl_cmd_device_info {
	__le32 freq;         /* Clock Frequency for tq generation */
	u8 tx_fifo;          /* Size of the transmission fifo */
	u8 sjw_max;          /* can_bittiming fields... */
	u8 tseg1_min;
	u8 tseg1_max;
	u8 tseg2_min;
	u8 tseg2_max;
	__le16 brp_inc;
	__le32 brp_min;
	__le32 brp_max;      /* ...can_bittiming fields */
	__le16 ctrlmodes;    /* supported control modes */
	__le16 hwfilter;     /* Number of HW filter banks */
	__le16 rxmboxes;     /* Number of receive Mailboxes */
} __packed;

struct ucan_ctl_cmd_get_protocol_version {
	__le32 version;
} __packed;

union ucan_ctl_payload {
	/* Setup Bittiming
	 * bmRequest == UCAN_COMMAND_START
	 */
	struct ucan_ctl_cmd_start cmd_start;
	/* Setup Bittiming
	 * bmRequest == UCAN_COMMAND_SET_BITTIMING
	 */
	struct ucan_ctl_cmd_set_bittiming cmd_set_bittiming;
	/* Get Device Information
	 * bmRequest == UCAN_COMMAND_GET; wValue = UCAN_COMMAND_GET_INFO
	 */
	struct ucan_ctl_cmd_device_info cmd_get_device_info;
	/* Get Protocol Version
	 * bmRequest == UCAN_COMMAND_GET;
	 * wValue = UCAN_COMMAND_GET_PROTOCOL_VERSION
	 */
	struct ucan_ctl_cmd_get_protocol_version cmd_get_protocol_version;

	u8 raw[128];
} __packed;

enum {
	UCAN_TX_COMPLETE_SUCCESS = BIT(0),
};

/* Transmission Complete within ucan_message_in */
struct ucan_tx_complete_entry_t {
	u8 echo_index;
	u8 flags;
} __packed __aligned(0x2);

/* CAN Data message format within ucan_message_in/out */
struct ucan_can_msg {
	/* note DLC is computed by
	 *    msg.len - sizeof (msg.len)
	 *            - sizeof (msg.type)
	 *            - sizeof (msg.can_msg.id)
	 */
	__le32 id;

	union {
		u8 data[CAN_MAX_DLEN];  /* Data of CAN frames */
		u8 dlc;                 /* RTR dlc */
	};
} __packed;

/* OUT Endpoint, outbound messages */
struct ucan_message_out {
	__le16 len; /* Length of the content include header */
	u8 type;    /* UCAN_OUT_TX and friends */
	u8 subtype; /* command sub type */

	union {
		/* Transmit CAN frame
		 * (type == UCAN_TX) && ((msg.can_msg.id & CAN_RTR_FLAG) == 0)
		 * subtype stores the echo id
		 */
		struct ucan_can_msg can_msg;
	} msg;
} __packed __aligned(0x4);

/* IN Endpoint, inbound messages */
struct ucan_message_in {
	__le16 len; /* Length of the content include header */
	u8 type;    /* UCAN_IN_RX and friends */
	u8 subtype; /* command sub type */

	union {
		/* CAN Frame received
		 * (type == UCAN_IN_RX)
		 * && ((msg.can_msg.id & CAN_RTR_FLAG) == 0)
		 */
		struct ucan_can_msg can_msg;

		/* CAN transmission complete
		 * (type == UCAN_IN_TX_COMPLETE)
		 */
		struct ucan_tx_complete_entry_t can_tx_complete_msg[0];
	} __aligned(0x4) msg;
} __packed __aligned(0x4);

/* Macros to calculate message lengths */
#define UCAN_OUT_HDR_SIZE offsetof(struct ucan_message_out, msg)

#define UCAN_IN_HDR_SIZE offsetof(struct ucan_message_in, msg)
#define UCAN_IN_LEN(member) (UCAN_OUT_HDR_SIZE + sizeof(member))

struct ucan_priv;

/* Context Information for transmission URBs */
struct ucan_urb_context {
	struct ucan_priv *up;
	u8 dlc;
	bool allocated;
};

/* Information reported by the USB device */
struct ucan_device_info {
	struct can_bittiming_const bittiming_const;
	u8 tx_fifo;
};

/* Driver private data */
struct ucan_priv {
	/* must be the first member */
	struct can_priv can;

	/* linux USB device structures */
	struct usb_device *udev;
	struct usb_interface *intf;
	struct net_device *netdev;

	/* lock for can->echo_skb (used around
	 * can_put/get/free_echo_skb
	 */
	spinlock_t echo_skb_lock;

	/* usb device information information */
	u8 intf_index;
	u8 in_ep_addr;
	u8 out_ep_addr;
	u16 in_ep_size;

	/* transmission and reception buffers */
	struct usb_anchor rx_urbs;
	struct usb_anchor tx_urbs;

	union ucan_ctl_payload *ctl_msg_buffer;
	struct ucan_device_info device_info;

	/* transmission control information and locks */
	spinlock_t context_lock;
	unsigned int available_tx_urbs;
	struct ucan_urb_context *context_array;
};

static u8 ucan_can_cc_dlc2len(struct ucan_can_msg *msg, u16 len)
{
	if (le32_to_cpu(msg->id) & CAN_RTR_FLAG)
		return can_cc_dlc2len(msg->dlc);
	else
		return can_cc_dlc2len(len - (UCAN_IN_HDR_SIZE + sizeof(msg->id)));
}

static void ucan_release_context_array(struct ucan_priv *up)
{
	if (!up->context_array)
		return;

	/* lock is not needed because, driver is currently opening or closing */
	up->available_tx_urbs = 0;

	kfree(up->context_array);
	up->context_array = NULL;
}

static int ucan_alloc_context_array(struct ucan_priv *up)
{
	int i;

	/* release contexts if any */
	ucan_release_context_array(up);

	up->context_array = kcalloc(up->device_info.tx_fifo,
				    sizeof(*up->context_array),
				    GFP_KERNEL);
	if (!up->context_array) {
		netdev_err(up->netdev,
			   "Not enough memory to allocate tx contexts\n");
		return -ENOMEM;
	}

	for (i = 0; i < up->device_info.tx_fifo; i++) {
		up->context_array[i].allocated = false;
		up->context_array[i].up = up;
	}

	/* lock is not needed because, driver is currently opening */
	up->available_tx_urbs = up->device_info.tx_fifo;

	return 0;
}

static struct ucan_urb_context *ucan_alloc_context(struct ucan_priv *up)
{
	int i;
	unsigned long flags;
	struct ucan_urb_context *ret = NULL;

	if (WARN_ON_ONCE(!up->context_array))
		return NULL;

	/* execute context operation atomically */
	spin_lock_irqsave(&up->context_lock, flags);

	for (i = 0; i < up->device_info.tx_fifo; i++) {
		if (!up->context_array[i].allocated) {
			/* update context */
			ret = &up->context_array[i];
			up->context_array[i].allocated = true;

			/* stop queue if necessary */
			up->available_tx_urbs--;
			if (!up->available_tx_urbs)
				netif_stop_queue(up->netdev);

			break;
		}
	}

	spin_unlock_irqrestore(&up->context_lock, flags);
	return ret;
}

static bool ucan_release_context(struct ucan_priv *up,
				 struct ucan_urb_context *ctx)
{
	unsigned long flags;
	bool ret = false;

	if (WARN_ON_ONCE(!up->context_array))
		return false;

	/* execute context operation atomically */
	spin_lock_irqsave(&up->context_lock, flags);

	/* context was not allocated, maybe the device sent garbage */
	if (ctx->allocated) {
		ctx->allocated = false;

		/* check if the queue needs to be woken */
		if (!up->available_tx_urbs)
			netif_wake_queue(up->netdev);
		up->available_tx_urbs++;

		ret = true;
	}

	spin_unlock_irqrestore(&up->context_lock, flags);
	return ret;
}

static int ucan_ctrl_command_out(struct ucan_priv *up,
				 u8 cmd, u16 subcmd, u16 datalen)
{
	return usb_control_msg(up->udev,
			       usb_sndctrlpipe(up->udev, 0),
			       cmd,
			       USB_DIR_OUT | USB_TYPE_VENDOR |
						USB_RECIP_INTERFACE,
			       subcmd,
			       up->intf_index,
			       up->ctl_msg_buffer,
			       datalen,
			       UCAN_USB_CTL_PIPE_TIMEOUT);
}

static int ucan_device_request_in(struct ucan_priv *up,
				  u8 cmd, u16 subcmd, u16 datalen)
{
	return usb_control_msg(up->udev,
			       usb_rcvctrlpipe(up->udev, 0),
			       cmd,
			       USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       subcmd,
			       0,
			       up->ctl_msg_buffer,
			       datalen,
			       UCAN_USB_CTL_PIPE_TIMEOUT);
}

/* Parse the device information structure reported by the device and
 * setup private variables accordingly
 */
static void ucan_parse_device_info(struct ucan_priv *up,
				   struct ucan_ctl_cmd_device_info *device_info)
{
	struct can_bittiming_const *bittiming =
		&up->device_info.bittiming_const;
	u16 ctrlmodes;

	/* store the data */
	up->can.clock.freq = le32_to_cpu(device_info->freq);
	up->device_info.tx_fifo = device_info->tx_fifo;
	strcpy(bittiming->name, "ucan");
	bittiming->tseg1_min = device_info->tseg1_min;
	bittiming->tseg1_max = device_info->tseg1_max;
	bittiming->tseg2_min = device_info->tseg2_min;
	bittiming->tseg2_max = device_info->tseg2_max;
	bittiming->sjw_max = device_info->sjw_max;
	bittiming->brp_min = le32_to_cpu(device_info->brp_min);
	bittiming->brp_max = le32_to_cpu(device_info->brp_max);
	bittiming->brp_inc = le16_to_cpu(device_info->brp_inc);

	ctrlmodes = le16_to_cpu(device_info->ctrlmodes);

	up->can.ctrlmode_supported = 0;

	if (ctrlmodes & UCAN_MODE_LOOPBACK)
		up->can.ctrlmode_supported |= CAN_CTRLMODE_LOOPBACK;
	if (ctrlmodes & UCAN_MODE_SILENT)
		up->can.ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;
	if (ctrlmodes & UCAN_MODE_3_SAMPLES)
		up->can.ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;
	if (ctrlmodes & UCAN_MODE_ONE_SHOT)
		up->can.ctrlmode_supported |= CAN_CTRLMODE_ONE_SHOT;
	if (ctrlmodes & UCAN_MODE_BERR_REPORT)
		up->can.ctrlmode_supported |= CAN_CTRLMODE_BERR_REPORTING;
}

/* Handle a CAN error frame that we have received from the device.
 * Returns true if the can state has changed.
 */
static bool ucan_handle_error_frame(struct ucan_priv *up,
				    struct ucan_message_in *m,
				    canid_t canid)
{
	enum can_state new_state = up->can.state;
	struct net_device_stats *net_stats = &up->netdev->stats;
	struct can_device_stats *can_stats = &up->can.can_stats;

	if (canid & CAN_ERR_LOSTARB)
		can_stats->arbitration_lost++;

	if (canid & CAN_ERR_BUSERROR)
		can_stats->bus_error++;

	if (canid & CAN_ERR_ACK)
		net_stats->tx_errors++;

	if (canid & CAN_ERR_BUSOFF)
		new_state = CAN_STATE_BUS_OFF;

	/* controller problems, details in data[1] */
	if (canid & CAN_ERR_CRTL) {
		u8 d1 = m->msg.can_msg.data[1];

		if (d1 & CAN_ERR_CRTL_RX_OVERFLOW)
			net_stats->rx_over_errors++;

		/* controller state bits: if multiple are set the worst wins */
		if (d1 & CAN_ERR_CRTL_ACTIVE)
			new_state = CAN_STATE_ERROR_ACTIVE;

		if (d1 & (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING))
			new_state = CAN_STATE_ERROR_WARNING;

		if (d1 & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE))
			new_state = CAN_STATE_ERROR_PASSIVE;
	}

	/* protocol error, details in data[2] */
	if (canid & CAN_ERR_PROT) {
		u8 d2 = m->msg.can_msg.data[2];

		if (d2 & CAN_ERR_PROT_TX)
			net_stats->tx_errors++;
		else
			net_stats->rx_errors++;
	}

	/* no state change - we are done */
	if (up->can.state == new_state)
		return false;

	/* we switched into a better state */
	if (up->can.state > new_state) {
		up->can.state = new_state;
		return true;
	}

	/* we switched into a worse state */
	up->can.state = new_state;
	switch (new_state) {
	case CAN_STATE_BUS_OFF:
		can_stats->bus_off++;
		can_bus_off(up->netdev);
		break;
	case CAN_STATE_ERROR_PASSIVE:
		can_stats->error_passive++;
		break;
	case CAN_STATE_ERROR_WARNING:
		can_stats->error_warning++;
		break;
	default:
		break;
	}
	return true;
}

/* Callback on reception of a can frame via the IN endpoint
 *
 * This function allocates an skb and transferres it to the Linux
 * network stack
 */
static void ucan_rx_can_msg(struct ucan_priv *up, struct ucan_message_in *m)
{
	int len;
	canid_t canid;
	struct can_frame *cf;
	struct sk_buff *skb;
	struct net_device_stats *stats = &up->netdev->stats;

	/* get the contents of the length field */
	len = le16_to_cpu(m->len);

	/* check sanity */
	if (len < UCAN_IN_HDR_SIZE + sizeof(m->msg.can_msg.id)) {
		netdev_warn(up->netdev, "invalid input message len: %d\n", len);
		return;
	}

	/* handle error frames */
	canid = le32_to_cpu(m->msg.can_msg.id);
	if (canid & CAN_ERR_FLAG) {
		bool busstate_changed = ucan_handle_error_frame(up, m, canid);

		/* if berr-reporting is off only state changes get through */
		if (!(up->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) &&
		    !busstate_changed)
			return;
	} else {
		canid_t canid_mask;
		/* compute the mask for canid */
		canid_mask = CAN_RTR_FLAG;
		if (canid & CAN_EFF_FLAG)
			canid_mask |= CAN_EFF_MASK | CAN_EFF_FLAG;
		else
			canid_mask |= CAN_SFF_MASK;

		if (canid & ~canid_mask)
			netdev_warn(up->netdev,
				    "unexpected bits set (canid %x, mask %x)",
				    canid, canid_mask);

		canid &= canid_mask;
	}

	/* allocate skb */
	skb = alloc_can_skb(up->netdev, &cf);
	if (!skb)
		return;

	/* fill the can frame */
	cf->can_id = canid;

	/* compute DLC taking RTR_FLAG into account */
	cf->len = ucan_can_cc_dlc2len(&m->msg.can_msg, len);

	/* copy the payload of non RTR frames */
	if (!(cf->can_id & CAN_RTR_FLAG) || (cf->can_id & CAN_ERR_FLAG))
		memcpy(cf->data, m->msg.can_msg.data, cf->len);

	/* don't count error frames as real packets */
	stats->rx_packets++;
	stats->rx_bytes += cf->len;

	/* pass it to Linux */
	netif_rx(skb);
}

/* callback indicating completed transmission */
static void ucan_tx_complete_msg(struct ucan_priv *up,
				 struct ucan_message_in *m)
{
	unsigned long flags;
	u16 count, i;
	u8 echo_index, dlc;
	u16 len = le16_to_cpu(m->len);

	struct ucan_urb_context *context;

	if (len < UCAN_IN_HDR_SIZE || (len % 2 != 0)) {
		netdev_err(up->netdev, "invalid tx complete length\n");
		return;
	}

	count = (len - UCAN_IN_HDR_SIZE) / 2;
	for (i = 0; i < count; i++) {
		/* we did not submit such echo ids */
		echo_index = m->msg.can_tx_complete_msg[i].echo_index;
		if (echo_index >= up->device_info.tx_fifo) {
			up->netdev->stats.tx_errors++;
			netdev_err(up->netdev,
				   "invalid echo_index %d received\n",
				   echo_index);
			continue;
		}

		/* gather information from the context */
		context = &up->context_array[echo_index];
		dlc = READ_ONCE(context->dlc);

		/* Release context and restart queue if necessary.
		 * Also check if the context was allocated
		 */
		if (!ucan_release_context(up, context))
			continue;

		spin_lock_irqsave(&up->echo_skb_lock, flags);
		if (m->msg.can_tx_complete_msg[i].flags &
		    UCAN_TX_COMPLETE_SUCCESS) {
			/* update statistics */
			up->netdev->stats.tx_packets++;
			up->netdev->stats.tx_bytes += dlc;
			can_get_echo_skb(up->netdev, echo_index, NULL);
		} else {
			up->netdev->stats.tx_dropped++;
			can_free_echo_skb(up->netdev, echo_index, NULL);
		}
		spin_unlock_irqrestore(&up->echo_skb_lock, flags);
	}
}

/* callback on reception of a USB message */
static void ucan_read_bulk_callback(struct urb *urb)
{
	int ret;
	int pos;
	struct ucan_priv *up = urb->context;
	struct net_device *netdev = up->netdev;
	struct ucan_message_in *m;

	/* the device is not up and the driver should not receive any
	 * data on the bulk in pipe
	 */
	if (WARN_ON(!up->context_array)) {
		usb_free_coherent(up->udev,
				  up->in_ep_size,
				  urb->transfer_buffer,
				  urb->transfer_dma);
		return;
	}

	/* check URB status */
	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -EPIPE:
	case -EPROTO:
	case -ESHUTDOWN:
	case -ETIME:
		/* urb is not resubmitted -> free dma data */
		usb_free_coherent(up->udev,
				  up->in_ep_size,
				  urb->transfer_buffer,
				  urb->transfer_dma);
		netdev_dbg(up->netdev, "not resubmitting urb; status: %d\n",
			   urb->status);
		return;
	default:
		goto resubmit;
	}

	/* sanity check */
	if (!netif_device_present(netdev))
		return;

	/* iterate over input */
	pos = 0;
	while (pos < urb->actual_length) {
		int len;

		/* check sanity (length of header) */
		if ((urb->actual_length - pos) < UCAN_IN_HDR_SIZE) {
			netdev_warn(up->netdev,
				    "invalid message (short; no hdr; l:%d)\n",
				    urb->actual_length);
			goto resubmit;
		}

		/* setup the message address */
		m = (struct ucan_message_in *)
			((u8 *)urb->transfer_buffer + pos);
		len = le16_to_cpu(m->len);

		/* check sanity (length of content) */
		if (urb->actual_length - pos < len) {
			netdev_warn(up->netdev,
				    "invalid message (short; no data; l:%d)\n",
				    urb->actual_length);
			print_hex_dump(KERN_WARNING,
				       "raw data: ",
				       DUMP_PREFIX_ADDRESS,
				       16,
				       1,
				       urb->transfer_buffer,
				       urb->actual_length,
				       true);

			goto resubmit;
		}

		switch (m->type) {
		case UCAN_IN_RX:
			ucan_rx_can_msg(up, m);
			break;
		case UCAN_IN_TX_COMPLETE:
			ucan_tx_complete_msg(up, m);
			break;
		default:
			netdev_warn(up->netdev,
				    "invalid message (type; t:%d)\n",
				    m->type);
			break;
		}

		/* proceed to next message */
		pos += len;
		/* align to 4 byte boundary */
		pos = round_up(pos, 4);
	}

resubmit:
	/* resubmit urb when done */
	usb_fill_bulk_urb(urb, up->udev,
			  usb_rcvbulkpipe(up->udev,
					  up->in_ep_addr),
			  urb->transfer_buffer,
			  up->in_ep_size,
			  ucan_read_bulk_callback,
			  up);

	usb_anchor_urb(urb, &up->rx_urbs);
	ret = usb_submit_urb(urb, GFP_ATOMIC);

	if (ret < 0) {
		netdev_err(up->netdev,
			   "failed resubmitting read bulk urb: %d\n",
			   ret);

		usb_unanchor_urb(urb);
		usb_free_coherent(up->udev,
				  up->in_ep_size,
				  urb->transfer_buffer,
				  urb->transfer_dma);

		if (ret == -ENODEV)
			netif_device_detach(netdev);
	}
}

/* callback after transmission of a USB message */
static void ucan_write_bulk_callback(struct urb *urb)
{
	unsigned long flags;
	struct ucan_priv *up;
	struct ucan_urb_context *context = urb->context;

	/* get the urb context */
	if (WARN_ON_ONCE(!context))
		return;

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev,
			  sizeof(struct ucan_message_out),
			  urb->transfer_buffer,
			  urb->transfer_dma);

	up = context->up;
	if (WARN_ON_ONCE(!up))
		return;

	/* sanity check */
	if (!netif_device_present(up->netdev))
		return;

	/* transmission failed (USB - the device will not send a TX complete) */
	if (urb->status) {
		netdev_warn(up->netdev,
			    "failed to transmit USB message to device: %d\n",
			     urb->status);

		/* update counters an cleanup */
		spin_lock_irqsave(&up->echo_skb_lock, flags);
		can_free_echo_skb(up->netdev, context - up->context_array, NULL);
		spin_unlock_irqrestore(&up->echo_skb_lock, flags);

		up->netdev->stats.tx_dropped++;

		/* release context and restart the queue if necessary */
		if (!ucan_release_context(up, context))
			netdev_err(up->netdev,
				   "urb failed, failed to release context\n");
	}
}

static void ucan_cleanup_rx_urbs(struct ucan_priv *up, struct urb **urbs)
{
	int i;

	for (i = 0; i < UCAN_MAX_RX_URBS; i++) {
		if (urbs[i]) {
			usb_unanchor_urb(urbs[i]);
			usb_free_coherent(up->udev,
					  up->in_ep_size,
					  urbs[i]->transfer_buffer,
					  urbs[i]->transfer_dma);
			usb_free_urb(urbs[i]);
		}
	}

	memset(urbs, 0, sizeof(*urbs) * UCAN_MAX_RX_URBS);
}

static int ucan_prepare_and_anchor_rx_urbs(struct ucan_priv *up,
					   struct urb **urbs)
{
	int i;

	memset(urbs, 0, sizeof(*urbs) * UCAN_MAX_RX_URBS);

	for (i = 0; i < UCAN_MAX_RX_URBS; i++) {
		void *buf;

		urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!urbs[i])
			goto err;

		buf = usb_alloc_coherent(up->udev,
					 up->in_ep_size,
					 GFP_KERNEL, &urbs[i]->transfer_dma);
		if (!buf) {
			/* cleanup this urb */
			usb_free_urb(urbs[i]);
			urbs[i] = NULL;
			goto err;
		}

		usb_fill_bulk_urb(urbs[i], up->udev,
				  usb_rcvbulkpipe(up->udev,
						  up->in_ep_addr),
				  buf,
				  up->in_ep_size,
				  ucan_read_bulk_callback,
				  up);

		urbs[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		usb_anchor_urb(urbs[i], &up->rx_urbs);
	}
	return 0;

err:
	/* cleanup other unsubmitted urbs */
	ucan_cleanup_rx_urbs(up, urbs);
	return -ENOMEM;
}

/* Submits rx urbs with the semantic: Either submit all, or cleanup
 * everything. I case of errors submitted urbs are killed and all urbs in
 * the array are freed. I case of no errors every entry in the urb
 * array is set to NULL.
 */
static int ucan_submit_rx_urbs(struct ucan_priv *up, struct urb **urbs)
{
	int i, ret;

	/* Iterate over all urbs to submit. On success remove the urb
	 * from the list.
	 */
	for (i = 0; i < UCAN_MAX_RX_URBS; i++) {
		ret = usb_submit_urb(urbs[i], GFP_KERNEL);
		if (ret) {
			netdev_err(up->netdev,
				   "could not submit urb; code: %d\n",
				   ret);
			goto err;
		}

		/* Anchor URB and drop reference, USB core will take
		 * care of freeing it
		 */
		usb_free_urb(urbs[i]);
		urbs[i] = NULL;
	}
	return 0;

err:
	/* Cleanup unsubmitted urbs */
	ucan_cleanup_rx_urbs(up, urbs);

	/* Kill urbs that are already submitted */
	usb_kill_anchored_urbs(&up->rx_urbs);

	return ret;
}

/* Open the network device */
static int ucan_open(struct net_device *netdev)
{
	int ret, ret_cleanup;
	u16 ctrlmode;
	struct urb *urbs[UCAN_MAX_RX_URBS];
	struct ucan_priv *up = netdev_priv(netdev);

	ret = ucan_alloc_context_array(up);
	if (ret)
		return ret;

	/* Allocate and prepare IN URBS - allocated and anchored
	 * urbs are stored in urbs[] for clean
	 */
	ret = ucan_prepare_and_anchor_rx_urbs(up, urbs);
	if (ret)
		goto err_contexts;

	/* Check the control mode */
	ctrlmode = 0;
	if (up->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		ctrlmode |= UCAN_MODE_LOOPBACK;
	if (up->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		ctrlmode |= UCAN_MODE_SILENT;
	if (up->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		ctrlmode |= UCAN_MODE_3_SAMPLES;
	if (up->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		ctrlmode |= UCAN_MODE_ONE_SHOT;

	/* Enable this in any case - filtering is down within the
	 * receive path
	 */
	ctrlmode |= UCAN_MODE_BERR_REPORT;
	up->ctl_msg_buffer->cmd_start.mode = cpu_to_le16(ctrlmode);

	/* Driver is ready to receive data - start the USB device */
	ret = ucan_ctrl_command_out(up, UCAN_COMMAND_START, 0, 2);
	if (ret < 0) {
		netdev_err(up->netdev,
			   "could not start device, code: %d\n",
			   ret);
		goto err_reset;
	}

	/* Call CAN layer open */
	ret = open_candev(netdev);
	if (ret)
		goto err_stop;

	/* Driver is ready to receive data. Submit RX URBS */
	ret = ucan_submit_rx_urbs(up, urbs);
	if (ret)
		goto err_stop;

	up->can.state = CAN_STATE_ERROR_ACTIVE;

	/* Start the network queue */
	netif_start_queue(netdev);

	return 0;

err_stop:
	/* The device have started already stop it */
	ret_cleanup = ucan_ctrl_command_out(up, UCAN_COMMAND_STOP, 0, 0);
	if (ret_cleanup < 0)
		netdev_err(up->netdev,
			   "could not stop device, code: %d\n",
			   ret_cleanup);

err_reset:
	/* The device might have received data, reset it for
	 * consistent state
	 */
	ret_cleanup = ucan_ctrl_command_out(up, UCAN_COMMAND_RESET, 0, 0);
	if (ret_cleanup < 0)
		netdev_err(up->netdev,
			   "could not reset device, code: %d\n",
			   ret_cleanup);

	/* clean up unsubmitted urbs */
	ucan_cleanup_rx_urbs(up, urbs);

err_contexts:
	ucan_release_context_array(up);
	return ret;
}

static struct urb *ucan_prepare_tx_urb(struct ucan_priv *up,
				       struct ucan_urb_context *context,
				       struct can_frame *cf,
				       u8 echo_index)
{
	int mlen;
	struct urb *urb;
	struct ucan_message_out *m;

	/* create a URB, and a buffer for it, and copy the data to the URB */
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		netdev_err(up->netdev, "no memory left for URBs\n");
		return NULL;
	}

	m = usb_alloc_coherent(up->udev,
			       sizeof(struct ucan_message_out),
			       GFP_ATOMIC,
			       &urb->transfer_dma);
	if (!m) {
		netdev_err(up->netdev, "no memory left for USB buffer\n");
		usb_free_urb(urb);
		return NULL;
	}

	/* build the USB message */
	m->type = UCAN_OUT_TX;
	m->msg.can_msg.id = cpu_to_le32(cf->can_id);

	if (cf->can_id & CAN_RTR_FLAG) {
		mlen = UCAN_OUT_HDR_SIZE +
			offsetof(struct ucan_can_msg, dlc) +
			sizeof(m->msg.can_msg.dlc);
		m->msg.can_msg.dlc = cf->len;
	} else {
		mlen = UCAN_OUT_HDR_SIZE +
			sizeof(m->msg.can_msg.id) + cf->len;
		memcpy(m->msg.can_msg.data, cf->data, cf->len);
	}
	m->len = cpu_to_le16(mlen);

	context->dlc = cf->len;

	m->subtype = echo_index;

	/* build the urb */
	usb_fill_bulk_urb(urb, up->udev,
			  usb_sndbulkpipe(up->udev,
					  up->out_ep_addr),
			  m, mlen, ucan_write_bulk_callback, context);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return urb;
}

static void ucan_clean_up_tx_urb(struct ucan_priv *up, struct urb *urb)
{
	usb_free_coherent(up->udev, sizeof(struct ucan_message_out),
			  urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb(urb);
}

/* callback when Linux needs to send a can frame */
static netdev_tx_t ucan_start_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	unsigned long flags;
	int ret;
	u8 echo_index;
	struct urb *urb;
	struct ucan_urb_context *context;
	struct ucan_priv *up = netdev_priv(netdev);
	struct can_frame *cf = (struct can_frame *)skb->data;

	/* check skb */
	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	/* allocate a context and slow down tx path, if fifo state is low */
	context = ucan_alloc_context(up);
	echo_index = context - up->context_array;

	if (WARN_ON_ONCE(!context))
		return NETDEV_TX_BUSY;

	/* prepare urb for transmission */
	urb = ucan_prepare_tx_urb(up, context, cf, echo_index);
	if (!urb)
		goto drop;

	/* put the skb on can loopback stack */
	spin_lock_irqsave(&up->echo_skb_lock, flags);
	can_put_echo_skb(skb, up->netdev, echo_index, 0);
	spin_unlock_irqrestore(&up->echo_skb_lock, flags);

	/* transmit it */
	usb_anchor_urb(urb, &up->tx_urbs);
	ret = usb_submit_urb(urb, GFP_ATOMIC);

	/* cleanup urb */
	if (ret) {
		/* on error, clean up */
		usb_unanchor_urb(urb);
		ucan_clean_up_tx_urb(up, urb);
		if (!ucan_release_context(up, context))
			netdev_err(up->netdev,
				   "xmit err: failed to release context\n");

		/* remove the skb from the echo stack - this also
		 * frees the skb
		 */
		spin_lock_irqsave(&up->echo_skb_lock, flags);
		can_free_echo_skb(up->netdev, echo_index, NULL);
		spin_unlock_irqrestore(&up->echo_skb_lock, flags);

		if (ret == -ENODEV) {
			netif_device_detach(up->netdev);
		} else {
			netdev_warn(up->netdev,
				    "xmit err: failed to submit urb %d\n",
				    ret);
			up->netdev->stats.tx_dropped++;
		}
		return NETDEV_TX_OK;
	}

	netif_trans_update(netdev);

	/* release ref, as we do not need the urb anymore */
	usb_free_urb(urb);

	return NETDEV_TX_OK;

drop:
	if (!ucan_release_context(up, context))
		netdev_err(up->netdev,
			   "xmit drop: failed to release context\n");
	dev_kfree_skb(skb);
	up->netdev->stats.tx_dropped++;

	return NETDEV_TX_OK;
}

/* Device goes down
 *
 * Clean up used resources
 */
static int ucan_close(struct net_device *netdev)
{
	int ret;
	struct ucan_priv *up = netdev_priv(netdev);

	up->can.state = CAN_STATE_STOPPED;

	/* stop sending data */
	usb_kill_anchored_urbs(&up->tx_urbs);

	/* stop receiving data */
	usb_kill_anchored_urbs(&up->rx_urbs);

	/* stop and reset can device */
	ret = ucan_ctrl_command_out(up, UCAN_COMMAND_STOP, 0, 0);
	if (ret < 0)
		netdev_err(up->netdev,
			   "could not stop device, code: %d\n",
			   ret);

	ret = ucan_ctrl_command_out(up, UCAN_COMMAND_RESET, 0, 0);
	if (ret < 0)
		netdev_err(up->netdev,
			   "could not reset device, code: %d\n",
			   ret);

	netif_stop_queue(netdev);

	ucan_release_context_array(up);

	close_candev(up->netdev);
	return 0;
}

/* CAN driver callbacks */
static const struct net_device_ops ucan_netdev_ops = {
	.ndo_open = ucan_open,
	.ndo_stop = ucan_close,
	.ndo_start_xmit = ucan_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

/* Request to set bittiming
 *
 * This function generates an USB set bittiming message and transmits
 * it to the device
 */
static int ucan_set_bittiming(struct net_device *netdev)
{
	int ret;
	struct ucan_priv *up = netdev_priv(netdev);
	struct ucan_ctl_cmd_set_bittiming *cmd_set_bittiming;

	cmd_set_bittiming = &up->ctl_msg_buffer->cmd_set_bittiming;
	cmd_set_bittiming->tq = cpu_to_le32(up->can.bittiming.tq);
	cmd_set_bittiming->brp = cpu_to_le16(up->can.bittiming.brp);
	cmd_set_bittiming->sample_point =
	    cpu_to_le16(up->can.bittiming.sample_point);
	cmd_set_bittiming->prop_seg = up->can.bittiming.prop_seg;
	cmd_set_bittiming->phase_seg1 = up->can.bittiming.phase_seg1;
	cmd_set_bittiming->phase_seg2 = up->can.bittiming.phase_seg2;
	cmd_set_bittiming->sjw = up->can.bittiming.sjw;

	ret = ucan_ctrl_command_out(up, UCAN_COMMAND_SET_BITTIMING, 0,
				    sizeof(*cmd_set_bittiming));
	return (ret < 0) ? ret : 0;
}

/* Restart the device to get it out of BUS-OFF state.
 * Called when the user runs "ip link set can1 type can restart".
 */
static int ucan_set_mode(struct net_device *netdev, enum can_mode mode)
{
	int ret;
	unsigned long flags;
	struct ucan_priv *up = netdev_priv(netdev);

	switch (mode) {
	case CAN_MODE_START:
		netdev_dbg(up->netdev, "restarting device\n");

		ret = ucan_ctrl_command_out(up, UCAN_COMMAND_RESTART, 0, 0);
		up->can.state = CAN_STATE_ERROR_ACTIVE;

		/* check if queue can be restarted,
		 * up->available_tx_urbs must be protected by the
		 * lock
		 */
		spin_lock_irqsave(&up->context_lock, flags);

		if (up->available_tx_urbs > 0)
			netif_wake_queue(up->netdev);

		spin_unlock_irqrestore(&up->context_lock, flags);

		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

/* Probe the device, reset it and gather general device information */
static int ucan_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	int ret;
	int i;
	u32 protocol_version;
	struct usb_device *udev;
	struct net_device *netdev;
	struct usb_host_interface *iface_desc;
	struct ucan_priv *up;
	struct usb_endpoint_descriptor *ep;
	u16 in_ep_size;
	u16 out_ep_size;
	u8 in_ep_addr;
	u8 out_ep_addr;
	union ucan_ctl_payload *ctl_msg_buffer;
	char firmware_str[sizeof(union ucan_ctl_payload) + 1];

	udev = interface_to_usbdev(intf);

	/* Stage 1 - Interface Parsing
	 * ---------------------------
	 *
	 * Identifie the device USB interface descriptor and its
	 * endpoints. Probing is aborted on errors.
	 */

	/* check if the interface is sane */
	iface_desc = intf->cur_altsetting;
	if (!iface_desc)
		return -ENODEV;

	dev_info(&udev->dev,
		 "%s: probing device on interface #%d\n",
		 UCAN_DRIVER_NAME,
		 iface_desc->desc.bInterfaceNumber);

	/* interface sanity check */
	if (iface_desc->desc.bNumEndpoints != 2) {
		dev_err(&udev->dev,
			"%s: invalid EP count (%d)",
			UCAN_DRIVER_NAME, iface_desc->desc.bNumEndpoints);
		goto err_firmware_needs_update;
	}

	/* check interface endpoints */
	in_ep_addr = 0;
	out_ep_addr = 0;
	in_ep_size = 0;
	out_ep_size = 0;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		ep = &iface_desc->endpoint[i].desc;

		if (((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) != 0) &&
		    ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		     USB_ENDPOINT_XFER_BULK)) {
			/* In Endpoint */
			in_ep_addr = ep->bEndpointAddress;
			in_ep_addr &= USB_ENDPOINT_NUMBER_MASK;
			in_ep_size = le16_to_cpu(ep->wMaxPacketSize);
		} else if (((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
			    0) &&
			   ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			    USB_ENDPOINT_XFER_BULK)) {
			/* Out Endpoint */
			out_ep_addr = ep->bEndpointAddress;
			out_ep_addr &= USB_ENDPOINT_NUMBER_MASK;
			out_ep_size = le16_to_cpu(ep->wMaxPacketSize);
		}
	}

	/* check if interface is sane */
	if (!in_ep_addr || !out_ep_addr) {
		dev_err(&udev->dev, "%s: invalid endpoint configuration\n",
			UCAN_DRIVER_NAME);
		goto err_firmware_needs_update;
	}
	if (in_ep_size < sizeof(struct ucan_message_in)) {
		dev_err(&udev->dev, "%s: invalid in_ep MaxPacketSize\n",
			UCAN_DRIVER_NAME);
		goto err_firmware_needs_update;
	}
	if (out_ep_size < sizeof(struct ucan_message_out)) {
		dev_err(&udev->dev, "%s: invalid out_ep MaxPacketSize\n",
			UCAN_DRIVER_NAME);
		goto err_firmware_needs_update;
	}

	/* Stage 2 - Device Identification
	 * -------------------------------
	 *
	 * The device interface seems to be a ucan device. Do further
	 * compatibility checks. On error probing is aborted, on
	 * success this stage leaves the ctl_msg_buffer with the
	 * reported contents of a GET_INFO command (supported
	 * bittimings, tx_fifo depth). This information is used in
	 * Stage 3 for the final driver initialisation.
	 */

	/* Prepare Memory for control transferes */
	ctl_msg_buffer = devm_kzalloc(&udev->dev,
				      sizeof(union ucan_ctl_payload),
				      GFP_KERNEL);
	if (!ctl_msg_buffer) {
		dev_err(&udev->dev,
			"%s: failed to allocate control pipe memory\n",
			UCAN_DRIVER_NAME);
		return -ENOMEM;
	}

	/* get protocol version
	 *
	 * note: ucan_ctrl_command_* wrappers cannot be used yet
	 * because `up` is initialised in Stage 3
	 */
	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      UCAN_COMMAND_GET,
			      USB_DIR_IN | USB_TYPE_VENDOR |
					USB_RECIP_INTERFACE,
			      UCAN_COMMAND_GET_PROTOCOL_VERSION,
			      iface_desc->desc.bInterfaceNumber,
			      ctl_msg_buffer,
			      sizeof(union ucan_ctl_payload),
			      UCAN_USB_CTL_PIPE_TIMEOUT);

	/* older firmware version do not support this command - those
	 * are not supported by this drive
	 */
	if (ret != 4) {
		dev_err(&udev->dev,
			"%s: could not read protocol version, ret=%d\n",
			UCAN_DRIVER_NAME, ret);
		if (ret >= 0)
			ret = -EINVAL;
		goto err_firmware_needs_update;
	}

	/* this driver currently supports protocol version 3 only */
	protocol_version =
		le32_to_cpu(ctl_msg_buffer->cmd_get_protocol_version.version);
	if (protocol_version < UCAN_PROTOCOL_VERSION_MIN ||
	    protocol_version > UCAN_PROTOCOL_VERSION_MAX) {
		dev_err(&udev->dev,
			"%s: device protocol version %d is not supported\n",
			UCAN_DRIVER_NAME, protocol_version);
		goto err_firmware_needs_update;
	}

	/* request the device information and store it in ctl_msg_buffer
	 *
	 * note: ucan_ctrl_command_* wrappers cannot be used yet
	 * because `up` is initialised in Stage 3
	 */
	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      UCAN_COMMAND_GET,
			      USB_DIR_IN | USB_TYPE_VENDOR |
					USB_RECIP_INTERFACE,
			      UCAN_COMMAND_GET_INFO,
			      iface_desc->desc.bInterfaceNumber,
			      ctl_msg_buffer,
			      sizeof(ctl_msg_buffer->cmd_get_device_info),
			      UCAN_USB_CTL_PIPE_TIMEOUT);

	if (ret < 0) {
		dev_err(&udev->dev, "%s: failed to retrieve device info\n",
			UCAN_DRIVER_NAME);
		goto err_firmware_needs_update;
	}
	if (ret < sizeof(ctl_msg_buffer->cmd_get_device_info)) {
		dev_err(&udev->dev, "%s: device reported invalid device info\n",
			UCAN_DRIVER_NAME);
		goto err_firmware_needs_update;
	}
	if (ctl_msg_buffer->cmd_get_device_info.tx_fifo == 0) {
		dev_err(&udev->dev,
			"%s: device reported invalid tx-fifo size\n",
			UCAN_DRIVER_NAME);
		goto err_firmware_needs_update;
	}

	/* Stage 3 - Driver Initialisation
	 * -------------------------------
	 *
	 * Register device to Linux, prepare private structures and
	 * reset the device.
	 */

	/* allocate driver resources */
	netdev = alloc_candev(sizeof(struct ucan_priv),
			      ctl_msg_buffer->cmd_get_device_info.tx_fifo);
	if (!netdev) {
		dev_err(&udev->dev,
			"%s: cannot allocate candev\n", UCAN_DRIVER_NAME);
		return -ENOMEM;
	}

	up = netdev_priv(netdev);

	/* initialize data */
	up->udev = udev;
	up->intf = intf;
	up->netdev = netdev;
	up->intf_index = iface_desc->desc.bInterfaceNumber;
	up->in_ep_addr = in_ep_addr;
	up->out_ep_addr = out_ep_addr;
	up->in_ep_size = in_ep_size;
	up->ctl_msg_buffer = ctl_msg_buffer;
	up->context_array = NULL;
	up->available_tx_urbs = 0;

	up->can.state = CAN_STATE_STOPPED;
	up->can.bittiming_const = &up->device_info.bittiming_const;
	up->can.do_set_bittiming = ucan_set_bittiming;
	up->can.do_set_mode = &ucan_set_mode;
	spin_lock_init(&up->context_lock);
	spin_lock_init(&up->echo_skb_lock);
	netdev->netdev_ops = &ucan_netdev_ops;

	usb_set_intfdata(intf, up);
	SET_NETDEV_DEV(netdev, &intf->dev);

	/* parse device information
	 * the data retrieved in Stage 2 is still available in
	 * up->ctl_msg_buffer
	 */
	ucan_parse_device_info(up, &ctl_msg_buffer->cmd_get_device_info);

	/* just print some device information - if available */
	ret = ucan_device_request_in(up, UCAN_DEVICE_GET_FW_STRING, 0,
				     sizeof(union ucan_ctl_payload));
	if (ret > 0) {
		/* copy string while ensuring zero terminiation */
		strncpy(firmware_str, up->ctl_msg_buffer->raw,
			sizeof(union ucan_ctl_payload));
		firmware_str[sizeof(union ucan_ctl_payload)] = '\0';
	} else {
		strcpy(firmware_str, "unknown");
	}

	/* device is compatible, reset it */
	ret = ucan_ctrl_command_out(up, UCAN_COMMAND_RESET, 0, 0);
	if (ret < 0)
		goto err_free_candev;

	init_usb_anchor(&up->rx_urbs);
	init_usb_anchor(&up->tx_urbs);

	up->can.state = CAN_STATE_STOPPED;

	/* register the device */
	ret = register_candev(netdev);
	if (ret)
		goto err_free_candev;

	/* initialisation complete, log device info */
	netdev_info(up->netdev, "registered device\n");
	netdev_info(up->netdev, "firmware string: %s\n", firmware_str);

	/* success */
	return 0;

err_free_candev:
	free_candev(netdev);
	return ret;

err_firmware_needs_update:
	dev_err(&udev->dev,
		"%s: probe failed; try to update the device firmware\n",
		UCAN_DRIVER_NAME);
	return -ENODEV;
}

/* disconnect the device */
static void ucan_disconnect(struct usb_interface *intf)
{
	struct ucan_priv *up = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (up) {
		unregister_netdev(up->netdev);
		free_candev(up->netdev);
	}
}

static struct usb_device_id ucan_table[] = {
	/* Mule (soldered onto compute modules) */
	{USB_DEVICE_INTERFACE_NUMBER(0x2294, 0x425a, 0)},
	/* Seal (standalone USB stick) */
	{USB_DEVICE_INTERFACE_NUMBER(0x2294, 0x425b, 0)},
	{} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ucan_table);
/* driver callbacks */
static struct usb_driver ucan_driver = {
	.name = UCAN_DRIVER_NAME,
	.probe = ucan_probe,
	.disconnect = ucan_disconnect,
	.id_table = ucan_table,
};

module_usb_driver(ucan_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Martin Elshuber <martin.elshuber@theobroma-systems.com>");
MODULE_AUTHOR("Jakob Unterwurzacher <jakob.unterwurzacher@theobroma-systems.com>");
MODULE_DESCRIPTION("Driver for Theobroma Systems UCAN devices");
