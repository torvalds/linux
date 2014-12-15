/*
 *
 *  Realtek Bluetooth USB driver
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#include <linux/dcache.h>

/*******************************/
#include "rtk_btusb.h"

#if 1
#define RTKBT_DBG(fmt, arg...) printk(KERN_INFO "rtk_btusb: " fmt "\n" , ## arg)
#else
#define RTKBT_DBG(fmt, arg...)
#endif

#if 1
#define RTKBT_ERR(fmt, arg...) printk(KERN_ERR "rtk_btusb: " fmt "\n" , ## arg)
#else
#define RTKBT_ERR(fmt, arg...)
#endif


#if CONFIG_BLUEDROID //for 4.2
#define DEVICE_NAME "rtk_btusb"
static dev_t first; // Global variable for the first device number
static struct cdev c_dev; // Global variable for the character device structure
static struct class *cl; // Global variable for the device class

/* HCI device list */
static DEFINE_RWLOCK(hci_dev_list_lock);
struct hci_dev *Ghdev = NULL;

static struct file_operations btfcd_file_ops_g =
{
    open    : btfcd_open,
    release : btfcd_close,
    read    : btfcd_read,
    write   : btfcd_write,
    poll    : btfcd_poll
};
#endif
/*******************************/

#define VERSION "2.11"


static struct usb_driver btusb_driver;
static struct usb_device_id btusb_table[] = {
	{ .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
					 USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor = 0x0bda,
	  .bInterfaceClass = 0xe0,
	  .bInterfaceSubClass = 0x01,
	  .bInterfaceProtocol = 0x01 },
	{ }
};

static void rtk_free( struct btusb_data *data)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 1)
	kfree(data);
#endif
	return;
}

static struct btusb_data * rtk_alloc(struct usb_interface *intf)
{
	struct btusb_data *data;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 1)
	data = kzalloc(sizeof(*data), GFP_KERNEL);
#else
	data = devm_kzalloc(&intf->dev, sizeof(*data), GFP_KERNEL);
#endif
	return data;
}
MODULE_DEVICE_TABLE(usb, btusb_table);

static int inc_tx(struct btusb_data *data)
{
	unsigned long flags;
	int rv;

	spin_lock_irqsave(&data->txlock, flags);
	rv = test_bit(BTUSB_SUSPENDING, &data->flags);
	if (!rv)
		data->tx_in_flight++;
	spin_unlock_irqrestore(&data->txlock, flags);

	return rv;
}

static void btusb_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = GET_DRV_DATA(hdev);
	int err;
//	struct usb_device    *dev ;
//	RTKBT_DBG("btusb_intr_complete %s urb %p status %d count %d ", hdev->name,
//					urb, urb->status, urb->actual_length);
//	print_event(urb);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	/*******************************/
	// Added by Realtek
	if(!test_bit(BTUSB_NEXT_RX_URB_SUBMITTING, &data->flags))
		return;
	/*******************************/

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (hci_recv_fragment(hdev, HCI_EVENT_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			RTKBT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
		return;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			RTKBT_ERR("btusb_intr_complete %s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	//	dev = urb->dev;
	//	RTKBT_DBG("dev->state = %d ",dev->state);
	}
}

static int btusb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	//RTKBT_DBG("%s", hdev->name);

	if (!data->intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
						btusb_intr_complete, hdev,
						data->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		RTKBT_ERR("btusb_submit_intr_urb %s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_bulk_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = GET_DRV_DATA(hdev);
	int err;
	//struct usb_device    *dev ;

	/*
	RTKBT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);
    */

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	/*******************************/
	// Added by Realtek
	if(!test_bit(BTUSB_NEXT_RX_URB_SUBMITTING, &data->flags))
		return;
	/*******************************/

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (hci_recv_fragment(hdev, HCI_ACLDATA_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			RTKBT_ERR("%s corrupted ACL packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->bulk_anchor);
	usb_mark_last_busy(data->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			RTKBT_ERR("btusb_bulk_complete %s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	//	dev = urb->dev;
	//	RTKBT_DBG("dev->state = %d ",dev->state);
	}
}

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	//RTKBT_DBG("%s", hdev->name);

	if (!data->bulk_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe,
					buf, size, btusb_bulk_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->bulk_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		RTKBT_ERR("btusb_submit_bulk_urb %s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_isoc_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = GET_DRV_DATA(hdev);
	int i, err;

	/*
	RTKBT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);
    */
	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int offset = urb->iso_frame_desc[i].offset;
			unsigned int length = urb->iso_frame_desc[i].actual_length;

			if (urb->iso_frame_desc[i].status)
				continue;

			hdev->stat.byte_rx += length;

			if (hci_recv_fragment(hdev, HCI_SCODATA_PKT,
						urb->transfer_buffer + offset,
								length) < 0) {
				RTKBT_ERR("%s corrupted SCO packet", hdev->name);
				hdev->stat.err_rx++;
			}
		}
	}

	if (!test_bit(BTUSB_ISOC_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			RTKBT_ERR("btusb_isoc_complete %s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
	int i, offset = 0;

	//RTKBT_DBG("len %d mtu %d", len, mtu);

	for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
					i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

static int btusb_submit_isoc_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	//RTKBT_DBG("%s", hdev->name);

	if (!data->isoc_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize) *
						BTUSB_MAX_ISOC_FRAMES;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvisocpipe(data->udev, data->isoc_rx_ep->bEndpointAddress);

	urb->dev      = data->udev;
	urb->pipe     = pipe;
	urb->context  = hdev;
	urb->complete = btusb_isoc_complete;
	urb->interval = data->isoc_rx_ep->bInterval;

	urb->transfer_flags  = URB_FREE_BUFFER | URB_ISO_ASAP;
	urb->transfer_buffer = buf;
	urb->transfer_buffer_length = size;

	__fill_isoc_descriptor(urb, size,
			le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		RTKBT_ERR("btusb_submit_isoc_urb %s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct btusb_data *data = GET_DRV_DATA(hdev);

//	RTKBT_DBG("btusb_tx_complete %s urb %p status %d count %d", hdev->name,
//					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	spin_lock(&data->txlock);
	data->tx_in_flight--;
	spin_unlock(&data->txlock);

	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static void btusb_isoc_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	/*
	RTKBT_DBG("btusb_isoc_tx_complete %s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);
    */
	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static int btusb_open(struct hci_dev *hdev)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);
	int err;

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return err;

	data->intf->needs_remote_wakeup = 1;
	RTKBT_DBG("%s start pm_usage_cnt(0x%x)",__FUNCTION__,atomic_read(&(data->intf ->pm_usage_cnt)));

	/*******************************/
	if (0 == atomic_read(&hdev->promisc))
	{
		RTKBT_ERR("btusb_open hdev->promisc ==0");
		err = -1;
                //goto failed;
	}
	err = download_patch(data->intf);
	if (err < 0) goto failed;
	/*******************************/

	/*******************************/
	// Added by Realtek
	set_bit(BTUSB_NEXT_RX_URB_SUBMITTING, &data->flags);
	/*******************************/

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
		goto done;

	err = btusb_submit_intr_urb(hdev, GFP_KERNEL);
	if (err < 0)
		goto failed;

	err = btusb_submit_bulk_urb(hdev, GFP_KERNEL);
	if (err < 0) {
		mdelay(URB_CANCELING_DELAY_MS);      // Added by Realtek
		usb_kill_anchored_urbs(&data->intr_anchor);
		goto failed;
	}

	set_bit(BTUSB_BULK_RUNNING, &data->flags);
	btusb_submit_bulk_urb(hdev, GFP_KERNEL);

done:
	usb_autopm_put_interface(data->intf);
	RTKBT_DBG("%s end  pm_usage_cnt(0x%x)",__FUNCTION__,atomic_read(&(data->intf ->pm_usage_cnt)));

	return 0;

failed:
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
	clear_bit(HCI_RUNNING, &hdev->flags);
	usb_autopm_put_interface(data->intf);
	RTKBT_ERR("%s failed  pm_usage_cnt(0x%x)",__FUNCTION__,atomic_read(&(data->intf ->pm_usage_cnt)));
	return err;
}

static void btusb_stop_traffic(struct btusb_data *data)
{
	mdelay(URB_CANCELING_DELAY_MS);    // Added by Realtek
	usb_kill_anchored_urbs(&data->intr_anchor);
	usb_kill_anchored_urbs(&data->bulk_anchor);
	usb_kill_anchored_urbs(&data->isoc_anchor);
}

static int btusb_close(struct hci_dev *hdev)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);
	int i,err;

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	RTKBT_DBG("btusb_close");
	/*******************************/
	for (i = 0; i < NUM_REASSEMBLY; i++)
	{
		if(hdev->reassembly[i])
		{
			kfree_skb(hdev->reassembly[i]);
			hdev->reassembly[i] = NULL;
			RTKBT_DBG("%s free ressembly i=%d",__FUNCTION__,i);
		}
	}
	/*******************************/
	cancel_work_sync(&data->work);
	cancel_work_sync(&data->waker);

	/*******************************/
	// Added by Realtek
	clear_bit(BTUSB_NEXT_RX_URB_SUBMITTING, &data->flags);
	/*******************************/

	clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
	clear_bit(BTUSB_BULK_RUNNING, &data->flags);
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);

	btusb_stop_traffic(data);
	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		goto failed;

	data->intf->needs_remote_wakeup = 0;
	usb_autopm_put_interface(data->intf);

failed:
	mdelay(URB_CANCELING_DELAY_MS);     // Added by Realtek
	usb_scuttle_anchored_urbs(&data->deferred);
	return 0;
}

static int btusb_flush(struct hci_dev *hdev)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);

	RTKBT_DBG("%s add delay ",__FUNCTION__);
	mdelay(URB_CANCELING_DELAY_MS);     // Added by Realtek
	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

#if LINUX_VERSION_CODE >=KERNEL_VERSION(3, 13, 0)
static int btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
#else
static int btusb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
#endif

	struct btusb_data *data = GET_DRV_DATA(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;
	int err;

//	RTKBT_DBG("%s", hdev->name);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
#else
skb->dev = (void *) hdev;
#endif

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		print_command(skb);
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
		if (!dr) {
			usb_free_urb(urb);
			return -ENOMEM;
		}

		dr->bRequestType = data->cmdreq_type;
		dr->bRequest     = 0;
		dr->wIndex       = 0;
		dr->wValue       = 0;
		dr->wLength      = __cpu_to_le16(skb->len);

		pipe = usb_sndctrlpipe(data->udev, 0x00);

		usb_fill_control_urb(urb, data->udev, pipe, (void *) dr,
				skb->data, skb->len, btusb_tx_complete, skb);

		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		print_acl(skb,1);
		if (!data->bulk_tx_ep)
			return -ENODEV;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndbulkpipe(data->udev,
					data->bulk_tx_ep->bEndpointAddress);

		usb_fill_bulk_urb(urb, data->udev, pipe,
				skb->data, skb->len, btusb_tx_complete, skb);

		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		if (!data->isoc_tx_ep ||SCO_NUM< 1)
			return -ENODEV;

		urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndisocpipe(data->udev,
					data->isoc_tx_ep->bEndpointAddress);

		usb_fill_int_urb(urb, data->udev, pipe,
				skb->data, skb->len, btusb_isoc_tx_complete,
				skb, data->isoc_tx_ep->bInterval);

		urb->transfer_flags  = URB_ISO_ASAP;

		__fill_isoc_descriptor(urb, skb->len,
				le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));

		hdev->stat.sco_tx++;
		goto skip_waking;

	default:
		return -EILSEQ;
	}

	err = inc_tx(data);
	if (err) {
		usb_anchor_urb(urb, &data->deferred);
		schedule_work(&data->waker);
		err = 0;
		goto done;
	}

skip_waking:
	usb_anchor_urb(urb, &data->tx_anchor);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		RTKBT_ERR("btusb_send_frame %s urb %p submission failed", hdev->name, urb);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(data->udev);
	}
	usb_free_urb(urb);

done:
	return err;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
static void btusb_destruct(struct hci_dev *hdev)
{
	RTKBT_DBG("btusb_destruct %s", hdev->name);
	hci_free_dev(hdev);
}
#endif



static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);

	RTKBT_DBG("%s evt %d", hdev->name, evt);
	RTKBT_DBG("btusb_notify : %s evt %d", hdev->name, evt);

	if (SCO_NUM != data->sco_num) {
		data->sco_num = SCO_NUM;
		schedule_work(&data->work);
	}
}

static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
	struct btusb_data *data = GET_DRV_DATA(hdev);
	struct usb_interface *intf = data->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;

	if (!data->isoc)
		return -ENODEV;

	err = usb_set_interface(data->udev, 1, altsetting);
	if (err < 0) {
		RTKBT_ERR("%s setting interface failed (%d)", hdev->name, -err);
		return err;
	}

	data->isoc_altsetting = altsetting;

	data->isoc_tx_ep = NULL;
	data->isoc_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
			data->isoc_tx_ep = ep_desc;
			continue;
		}

		if (!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
			data->isoc_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->isoc_tx_ep || !data->isoc_rx_ep) {
		RTKBT_ERR("%s invalid SCO descriptors", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static void btusb_work(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, work);
	struct hci_dev *hdev = data->hdev;
	int err;
	int new_alts;
	if (data->sco_num > 0) {
		if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
			err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
			if (err < 0) {
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
				mdelay(URB_CANCELING_DELAY_MS);    // Added by Realtek
				usb_kill_anchored_urbs(&data->isoc_anchor);
				return;
			}

			set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
		}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 1)
		if (hdev->voice_setting & 0x0020) {
			static const int alts[3] = { 2, 4, 5 };
			new_alts = alts[data->sco_num - 1];
		} else {
			new_alts = data->sco_num;
		}
		if (data->isoc_altsetting != new_alts) {
#else
		if (data->isoc_altsetting != 2) {
			new_alts = 2;
#endif

			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			mdelay(URB_CANCELING_DELAY_MS);    // Added by Realtek
			usb_kill_anchored_urbs(&data->isoc_anchor);

			if (__set_isoc_interface(hdev, new_alts) < 0)
				return;
		}

		if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
			if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			else
				btusb_submit_isoc_urb(hdev, GFP_KERNEL);
		}
	} else {
		clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		mdelay(URB_CANCELING_DELAY_MS);      // Added by Realtek
		usb_kill_anchored_urbs(&data->isoc_anchor);

		__set_isoc_interface(hdev, 0);
		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
			usb_autopm_put_interface(data->isoc ? data->isoc : data->intf);
	}
}

static void btusb_waker(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, waker);
	int err;

	err = usb_autopm_get_interface(data->intf);
	RTKBT_DBG("%s start  pm_usage_cnt(0x%x)",__FUNCTION__,atomic_read(&(data->intf ->pm_usage_cnt)));
	if (err < 0)
		return;

	usb_autopm_put_interface(data->intf);
	RTKBT_DBG("%s end  pm_usage_cnt(0x%x)",__FUNCTION__,atomic_read(&(data->intf ->pm_usage_cnt)));
}

static int btusb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct btusb_data *data;
	struct hci_dev *hdev;
	int i, err,flag1,flag2;
	struct usb_device *udev;
	udev = interface_to_usbdev(intf);


	RTKBT_DBG("btusb_probe intf->cur_altsetting->desc.bInterfaceNumber=%d",intf->cur_altsetting->desc.bInterfaceNumber);

	/* interface numbers are hardcoded in the spec */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	/*******************************/
	flag1=device_can_wakeup(&udev->dev);
	flag2=device_may_wakeup(&udev->dev);
	RTKBT_DBG("btusb_probe can_wakeup=%x	 flag2=%x",flag1,flag2);
	//device_wakeup_enable(&udev->dev);
	/*device_wakeup_disable(&udev->dev);
	flag1=device_can_wakeup(&udev->dev);
	flag2=device_may_wakeup(&udev->dev);
	RTKBT_DBG("btusb_probe can_wakeup=%x	 flag2=%x",flag1,flag2);
	*/
	err = patch_add(intf);
	if (err < 0) return -1;
	/*******************************/

	data = rtk_alloc(intf);
	if (!data)
		return -ENOMEM;


	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			data->intr_ep = ep_desc;
			continue;
		}

		if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->bulk_tx_ep = ep_desc;
			continue;
		}

		if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->bulk_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep) {
		rtk_free(data);
		return -ENODEV;
	}

	data->cmdreq_type = USB_TYPE_CLASS;

	data->udev = interface_to_usbdev(intf);
	data->intf = intf;

	spin_lock_init(&data->lock);

	INIT_WORK(&data->work, btusb_work);
	INIT_WORK(&data->waker, btusb_waker);
	spin_lock_init(&data->txlock);

	init_usb_anchor(&data->tx_anchor);
	init_usb_anchor(&data->intr_anchor);
	init_usb_anchor(&data->bulk_anchor);
	init_usb_anchor(&data->isoc_anchor);
	init_usb_anchor(&data->deferred);

	hdev = hci_alloc_dev();
	if (!hdev) {
		rtk_free(data);
		return -ENOMEM;
	}

	HDEV_BUS = HCI_USB;

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = btusb_open;
	hdev->close    = btusb_close;
	hdev->flush    = btusb_flush;
	hdev->send     = btusb_send_frame;
	hdev->notify   = btusb_notify;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
	hci_set_drvdata(hdev, data);
#else
	hdev->driver_data = data;
	hdev->destruct = btusb_destruct;
	hdev->owner = THIS_MODULE;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 1)
	if (!reset)
		set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);
	RTKBT_DBG("set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);");
#endif
#endif
	/* Interface numbers are hardcoded in the specification */
	data->isoc = usb_ifnum_to_if(data->udev, 1);

	if (data->isoc) {
		err = usb_driver_claim_interface(&btusb_driver,
							data->isoc, data);
		if (err < 0) {
			hci_free_dev(hdev);
			rtk_free(data);
			return err;
		}
	}

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		rtk_free(data);
		return err;
	}

	usb_set_intfdata(intf, data);

#if CONFIG_BLUEDROID //for 4.2
	btfcd_init();
#endif
	return 0;
}

static void btusb_disconnect(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev;
	struct usb_device *udev;
	udev = interface_to_usbdev(intf);

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return;

	if (!data)
		return;

	RTKBT_DBG("btusb_disconnect");
	/*******************************/
	patch_remove(intf);
	/*******************************/

	hdev = data->hdev;
#if CONFIG_BLUEDROID //for 4.2
#else //for bluez
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
	__hci_dev_hold(hdev);
#endif
#endif
	usb_set_intfdata(data->intf, NULL);

	if (data->isoc)
		usb_set_intfdata(data->isoc, NULL);

	hci_unregister_dev(hdev);

	if (intf == data->isoc)
		usb_driver_release_interface(&btusb_driver, data->intf);
	else if (data->isoc)
		usb_driver_release_interface(&btusb_driver, data->isoc);

#if CONFIG_BLUEDROID //for 4.2
#else //for bluez
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
	__hci_dev_put(hdev);
#endif
#endif

	hci_free_dev(hdev);
	rtk_free(data);

#if CONFIG_BLUEDROID //for 4.2
	btfcd_exit();
#endif

}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct btusb_data *data = usb_get_intfdata(intf);

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return 0;

	/*******************************/
	RTKBT_DBG("btusb_suspend message.event=0x%x,data->suspend_count=%d",message.event,data->suspend_count);
	if (!test_bit(HCI_RUNNING, &data->hdev->flags))
	{
		RTKBT_DBG("btusb_suspend-----bt is off");
		set_btoff(data->intf);
	}
	/*******************************/

	if (data->suspend_count++)
		return 0;

	spin_lock_irq(&data->txlock);
	if (!((message.event & PM_EVENT_AUTO) && data->tx_in_flight)) {
		set_bit(BTUSB_SUSPENDING, &data->flags);
		spin_unlock_irq(&data->txlock);
	} else {
		spin_unlock_irq(&data->txlock);
		data->suspend_count--;
		return -EBUSY;
	}

	cancel_work_sync(&data->work);

	/*******************************/
	// Added by Realtek
	clear_bit(BTUSB_NEXT_RX_URB_SUBMITTING, &data->flags);
	/*******************************/

	btusb_stop_traffic(data);
	mdelay(URB_CANCELING_DELAY_MS);      // Added by Realtek
	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static void play_deferred(struct btusb_data *data)
{
	struct urb *urb;
	int err;

	while ((urb = usb_get_from_anchor(&data->deferred))) {

	       /************************************/
		usb_anchor_urb(urb, &data->tx_anchor);
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0) {
			RTKBT_ERR("play_deferred urb %p submission failed",  urb);
			kfree(urb->setup_packet);
			usb_unanchor_urb(urb);
		} else {
			usb_mark_last_busy(data->udev);
		}
		usb_free_urb(urb);
		/************************************/
		data->tx_in_flight++;
	}
	mdelay(URB_CANCELING_DELAY_MS);     // Added by Realtek
	usb_scuttle_anchored_urbs(&data->deferred);
}

static int btusb_resume(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev = data->hdev;
	int err = 0;

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return 0;

	/*******************************/
	RTKBT_DBG("btusb_resume data->suspend_count=%d",data->suspend_count);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
	{
		RTKBT_DBG("btusb_resume-----bt is off,download patch");
		download_patch(intf);
	}
	else
	        RTKBT_DBG("btusb_resume,----bt is on");
	/*******************************/
	if (--data->suspend_count)
		return 0;

	/*******************************/
	// Added by Realtek
	set_bit(BTUSB_NEXT_RX_URB_SUBMITTING, &data->flags);
	/*******************************/

	if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
		err = btusb_submit_intr_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_INTR_RUNNING, &data->flags);
			goto failed;
		}
	}

	if (test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
		err = btusb_submit_bulk_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_BULK_RUNNING, &data->flags);
			goto failed;
		}

		btusb_submit_bulk_urb(hdev, GFP_NOIO);
	}

	if (test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
		if (btusb_submit_isoc_urb(hdev, GFP_NOIO) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		else
			btusb_submit_isoc_urb(hdev, GFP_NOIO);
	}

	spin_lock_irq(&data->txlock);
	play_deferred(data);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);
	schedule_work(&data->work);

	return 0;

failed:
	mdelay(URB_CANCELING_DELAY_MS);      // Added by Realtek
	usb_scuttle_anchored_urbs(&data->deferred);
//done:
	spin_lock_irq(&data->txlock);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);

	return err;
}
#endif

static struct usb_driver btusb_driver = {
	.name		= "rtk_btusb",
	.probe		= btusb_probe,
	.disconnect	= btusb_disconnect,
#ifdef CONFIG_PM
	.suspend	= btusb_suspend,
	.resume		= btusb_resume,
#endif
#if  CONFIG_RESET_RESUME
	.reset_resume = btusb_resume,
#endif
	.id_table	= btusb_table,
	.supports_autosuspend = 1,
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 1)
	.disable_hub_initiated_lpm = 1,
#endif
};

static int __init btusb_init(void)
{
	RTKBT_DBG("Realtek Bluetooth USB driver ver %s", VERSION);
	return usb_register(&btusb_driver);
}

static void __exit btusb_exit(void)
{
	RTKBT_DBG(KERN_INFO "rtk_btusb: btusb_exit");
	usb_deregister(&btusb_driver);
}

module_init(btusb_init);
module_exit(btusb_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("Realtek Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");



/*******************************
**    Reasil patch code
********************************/
#define CMD_CMP_EVT		0x0e
#define PKT_LEN			300
#define MSG_TO			1000    //us
#define PATCH_SEG_MAX	252
#define DATA_END		0x80
#define DOWNLOAD_OPCODE	0xfc20
#define BTOFF_OPCODE	0xfc28
#define TRUE			1
#define FALSE			0
#define CMD_HDR_LEN		sizeof(struct hci_command_hdr)
#define EVT_HDR_LEN		sizeof(struct hci_event_hdr)
#define CMD_CMP_LEN		sizeof(struct hci_ev_cmd_complete)


enum rtk_endpoit {
	CTRL_EP = 0,
	INTR_EP = 1,
	BULK_EP = 2,
	ISOC_EP = 3
};

typedef struct {
	uint16_t	prod_id;
	uint16_t	lmp_sub;
        char          *mp_patch_name;
	char		*patch_name;
	char		*config_name;
	uint8_t		*fw_cache;
	int			fw_len;
} patch_info;

typedef struct {
	struct list_head		list_node;
	struct usb_interface	*intf;
	struct usb_device		*udev;
	struct notifier_block	pm_notifier;
	patch_info				*patch_entry;
} dev_data;

typedef struct {
	dev_data	*dev_entry;
	int			pipe_in, pipe_out;
	uint8_t		*send_pkt;
	uint8_t		*rcv_pkt;
	struct hci_command_hdr		*cmd_hdr;
	struct hci_event_hdr		*evt_hdr;
	struct hci_ev_cmd_complete	*cmd_cmp;
	uint8_t		*req_para, *rsp_para;
	uint8_t		*fw_data;
	int			pkt_len, fw_len;
} xchange_data;

typedef struct {
	uint8_t index;
	uint8_t data[PATCH_SEG_MAX];
} __attribute__((packed)) download_cp;

typedef struct {
	uint8_t status;
	uint8_t index;
} __attribute__((packed)) download_rp;

#define RTK_VENDOR_CONFIG_MAGIC 0x8723ab55
struct rtk_bt_vendor_config_entry{
    uint16_t offset;
    uint8_t entry_len;
    uint8_t entry_data[0];
} __attribute__ ((packed));


struct rtk_bt_vendor_config{
    uint32_t signature;
    uint16_t data_len;
    struct rtk_bt_vendor_config_entry entry[0];
} __attribute__ ((packed));


static dev_data* dev_data_find(struct usb_interface* intf);
static patch_info* get_patch_entry(struct usb_device* udev);
static int rtkbt_pm_notify(struct notifier_block* notifier, ulong pm_event, void* unused);
static int load_firmware(dev_data* dev_entry, uint8_t** buff);
static void init_xdata(xchange_data* xdata, dev_data* dev_entry);
static int check_fw_version(xchange_data* xdata);
static int get_firmware(xchange_data* xdata);
static int download_data(xchange_data* xdata);
static int send_hci_cmd(xchange_data* xdata);
static int rcv_hci_evt(xchange_data* xdata);
static uint8_t rtk_get_eversion(dev_data * dev_entry);

static uint8_t gEVersion = 0xFF;


static patch_info patch_table[] = {
//{pid, lmp ,fw_name,config_name,fw_cache,fw_len}
    { 0x1724, 0x1200,  "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //Rtl8723A
    { 0x8723, 0x1200, "mp_rtl8723a_fw",      "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //8723AE
    { 0xA723, 0x1200, "mp_rtl8723a_fw",      "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //8723AE for LI
    { 0x0723, 0x1200, "mp_rtl8723a_fw",      "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //8723AE

    { 0x0724, 0x1200, "mp_rtl8723a_fw",      "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //8723AU
    { 0x8725, 0x1200, "mp_rtl8723a_fw",      "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //8723AU
    { 0x872A, 0x1200, "mp_rtl8723a_fw",      "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //8723AU
    { 0x872B, 0x1200, "mp_rtl8723a_fw",      "rtl8723a_fw", "rtl8723a_config", NULL, 0 }, //8723AU

    { 0xA761, 0x8761, "mp_rtl8761a_fw",     "rtl8761au_fw",              "rtl8761a_config", NULL, 0 }, //Rtl8761AU only
    { 0x818B, 0x8761, "mp_rtl8761a_fw",     "rtl8761aw8192eu_fw", "rtl8761a_config", NULL, 0 }, //Rtl8761Aw + 8192EU
    { 0x818C, 0x8761, "mp_rtl8761a_fw",     "rtl8761aw8192eu_fw", "rtl8761a_config", NULL, 0 }, //Rtl8761Aw + 8192EU
    { 0x8760, 0x8761, "mp_rtl8761a_fw",     "rtl8761au8192ee_fw", "rtl8761a_config", NULL, 0 }, //Rtl8761AU + 8192EE
    { 0xB761, 0x8761, "mp_rtl8761a_fw",     "rtl8761au8192ee_fw", "rtl8761a_config", NULL, 0 }, //Rtl8761AU + 8192EE
    { 0x8761, 0x8761, "mp_rtl8761a_fw",     "rtl8761au8192ee_fw", "rtl8761a_config", NULL, 0 }, //Rtl8761AU + 8192EE for LI
    { 0x8A60, 0x8761, "mp_rtl8761a_fw",      "rtl8761au8812ae_fw", "rtl8761a_config", NULL, 0 }, //Rtl8761AU + 8812AE

    { 0x8821, 0x8821, "mp_rtl8821a_fw",      "rtl8821a_fw",     "rtl8821a_config", NULL, 0 },  //Rtl8821AE
    { 0x0821, 0x8821, "mp_rtl8821a_fw",      "rtl8821a_fw",     "rtl8821a_config", NULL, 0 },  //Rtl8821AE
    { 0x0823, 0x8821, "mp_rtl8821a_fw",      "rtl8821a_fw",     "rtl8821a_config", NULL, 0 },  //Rtl8821AU

    { 0xb720, 0x8723, "mp_rtl8723b_fw",       "rtl8723b_fw", "rtl8723bu_config", NULL, 0 },  //Rtl8723BU
    { 0xb72A, 0x8723,  "mp_rtl8723b_fw",      "rtl8723b_fw", "rtl8723bu_config", NULL, 0 },  //Rtl8723BU
    { 0xb728, 0x8723,  "mp_rtl8723b_fw",      "rtl8723b_fw", "rtl8723b_config", NULL, 0 },  //Rtl8723BE for LC
    { 0xb723, 0x8723,  "mp_rtl8723b_fw",      "rtl8723b_fw", "rtl8723b_config", NULL, 0 },  //Rtl8723BE
    { 0xb72B, 0x8723,  "mp_rtl8723b_fw",      "rtl8723b_fw", "rtl8723b_config", NULL, 0 }, //Rtl8723BE
    { 0xb002, 0x8723,  "mp_rtl8723b_fw",      "rtl8723b_fw", "rtl8723b_config", NULL, 0 }, //Rtl8723BE
    { 0xb001, 0x8723, "mp_rtl8723b_fw",       "rtl8723b_fw", "rtl8723b_config", NULL, 0 },  //Rtl8723BE for hp

    { 0, 0, NULL,NULL, NULL, 0 }

  //  { 0, 0x1200, "rtl8723a_fw", "rtl8723a_config", NULL, 0 } //Rtl8723AU & Rtl8723AE by default
};

static LIST_HEAD(dev_data_list);

int patch_add(struct usb_interface* intf)
{
	dev_data	*dev_entry;
	struct usb_device *udev;

	RTKBT_DBG("patch_add");
	dev_entry = dev_data_find(intf);
	if (NULL != dev_entry)
	{
		return -1;
	}

	udev = interface_to_usbdev(intf);
#if BTUSB_RPM
	RTKBT_DBG("auto suspend is enabled");
	usb_enable_autosuspend(udev);
	pm_runtime_set_autosuspend_delay(&(udev->dev),2000);
#endif

	dev_entry = kzalloc(sizeof(dev_data), GFP_KERNEL);
	dev_entry->intf = intf;
	dev_entry->udev = udev;
	dev_entry->pm_notifier.notifier_call = rtkbt_pm_notify;
	dev_entry->patch_entry = get_patch_entry(udev);
	if(NULL == dev_entry->patch_entry)
	{
		kfree(dev_entry);
		return -1;
	}
	list_add(&dev_entry->list_node, &dev_data_list);
	register_pm_notifier(&dev_entry->pm_notifier);

	return 0;
}

void patch_remove(struct usb_interface* intf)
{
	dev_data *dev_entry;
	struct usb_device *udev;

	udev = interface_to_usbdev(intf);
#if BTUSB_RPM
	usb_disable_autosuspend(udev);
#endif

	dev_entry = dev_data_find(intf);
	if (NULL == dev_entry)
	{
		return;
	}

	RTKBT_DBG("patch_remove");
	list_del(&dev_entry->list_node);
	unregister_pm_notifier(&dev_entry->pm_notifier);
	kfree(dev_entry);
}

int download_patch(struct usb_interface* intf)
{
	dev_data		*dev_entry;
	xchange_data	*xdata = NULL;
	uint8_t			*fw_buf;
	int				ret_val;

	RTKBT_DBG("download_patch start");
	dev_entry = dev_data_find(intf);
	if (NULL == dev_entry)
	{
		ret_val = -1;
		RTKBT_ERR("NULL == dev_entry");
		goto patch_end;
	}

        xdata = kzalloc(sizeof(xchange_data), GFP_KERNEL);
	if(NULL == xdata)
	{
		ret_val = -1;
		RTKBT_DBG("NULL == xdata");
		goto patch_end;
	}

	init_xdata(xdata, dev_entry);
	ret_val = check_fw_version(xdata);
	if (ret_val != 0)
	{
		if(gEVersion == 0xFF) {
			RTKBT_DBG("global_version is not set, get it!");
			gEVersion=rtk_get_eversion(dev_entry);
		}
		goto patch_end;
	}

	ret_val = get_firmware(xdata);
	if (ret_val < 0)
	{
		RTKBT_ERR("get_firmware failed!");
		goto patch_end;
	}
	fw_buf = xdata->fw_data;

	ret_val = download_data(xdata);
	if (ret_val < 0)
	{
		RTKBT_ERR("download_data failed!");
		goto patch_fail;
	}

	ret_val = check_fw_version(xdata);
	if (ret_val <= 0)
	{
		ret_val = -1;
		goto patch_fail;
	}

	ret_val = 0;
patch_fail:
	kfree(fw_buf);
patch_end:
	if(xdata != NULL)
	{
                if(xdata->send_pkt)
			kfree(xdata->send_pkt);
		if(xdata->rcv_pkt)
			kfree(xdata->rcv_pkt);
        	kfree(xdata);
	}
	RTKBT_DBG("Rtk patch end %d", ret_val);
	return ret_val;
}

int set_btoff(struct usb_interface* intf)
{
	dev_data		*dev_entry;
	xchange_data	*xdata = NULL;
	int				ret_val;

	RTKBT_DBG("set_btoff");
	dev_entry = dev_data_find(intf);
	if (NULL == dev_entry)
	{
		return -1;
	}

       xdata = kzalloc(sizeof(xchange_data), GFP_KERNEL);
	if(NULL == xdata)
	{
		ret_val = -1;
		RTKBT_DBG("NULL == xdata");
              return ret_val;
       }

	init_xdata(xdata, dev_entry);


	xdata->cmd_hdr->opcode = cpu_to_le16(BTOFF_OPCODE);
	xdata->cmd_hdr->plen = 1;
	xdata->pkt_len = CMD_HDR_LEN + 1;
	xdata->send_pkt[CMD_HDR_LEN] = 1;

	ret_val = send_hci_cmd(xdata);
	if (ret_val < 0)
	{
		goto tagEnd;
	}

	ret_val = rcv_hci_evt(xdata);
	if (ret_val < 0)
	{
		goto tagEnd;
	}

tagEnd:
	if(xdata != NULL)
	{
                if(xdata->send_pkt)
			kfree(xdata->send_pkt);
		if(xdata->rcv_pkt)
			kfree(xdata->rcv_pkt);
        	kfree(xdata);
	}

	RTKBT_DBG("set_btoff done");

	return ret_val;
}


dev_data* dev_data_find(struct usb_interface* intf)
{
	dev_data *dev_entry;

	list_for_each_entry(dev_entry, &dev_data_list, list_node)
	{
		if (dev_entry->intf == intf)
		{
			return dev_entry;
		}
	}

	return NULL;
}

patch_info* get_patch_entry(struct usb_device* udev)
{
	patch_info	*patch_entry;
	uint16_t	pid;

	patch_entry = patch_table;
	pid = le16_to_cpu(udev->descriptor.idProduct);
	RTKBT_DBG("pid = 0x%x", pid);
	while (pid != patch_entry->prod_id)
	{
		if (0 == patch_entry->prod_id)
		{
			RTKBT_DBG("get_patch_entry =NULL, can not find device pid in patch_table");
			return NULL;	//break;
		}
		patch_entry++;
	}

	return patch_entry;
}

int rtkbt_pm_notify(
	struct notifier_block* notifier,
	ulong	pm_event,
	void*	unused)
{
	dev_data	*dev_entry;
	patch_info	*patch_entry;
	struct usb_device *udev;

	dev_entry = container_of(notifier, dev_data, pm_notifier);
	patch_entry = dev_entry->patch_entry;
	udev = dev_entry->udev;
	RTKBT_DBG("rtkbt_pm_notify pm_event =%ld",pm_event);
	switch (pm_event)
	{
		case PM_SUSPEND_PREPARE:
		case PM_HIBERNATION_PREPARE:
			patch_entry->fw_len = load_firmware(dev_entry, &patch_entry->fw_cache);
			if (patch_entry->fw_len <= 0)
			{
				RTKBT_DBG("rtkbt_pm_notify return NOTIFY_BAD");
				return NOTIFY_BAD;
			}

			if (!device_may_wakeup(&udev->dev))
			{
				#if (CONFIG_RESET_RESUME || CONFIG_BLUEDROID)
					RTKBT_DBG("remote wakeup not support, reset_resume support ");
				#else
					dev_entry->intf->needs_binding = 1;
					RTKBT_DBG("remote wakeup not support, set intf->needs_binding = 1");
				#endif
			}
			break;

		case PM_POST_SUSPEND:
		case PM_POST_HIBERNATION:
		case PM_POST_RESTORE:
			if (patch_entry->fw_len > 0)
			{
				kfree(patch_entry->fw_cache);
				patch_entry->fw_cache = NULL;
				patch_entry->fw_len = 0;
			}
#if BTUSB_RPM
			usb_disable_autosuspend(udev);
			usb_enable_autosuspend(udev);
			pm_runtime_set_autosuspend_delay(&(udev->dev),2000);
#endif
			break;

		default:
			break;
	}

	return NOTIFY_DONE;
}

int rtk_parse_config_file(unsigned char* config_buf, int* filelen, char bt_addr[6])
{
    struct rtk_bt_vendor_config* config = (struct rtk_bt_vendor_config*)config_buf;
    uint16_t config_len = config->data_len, temp = 0;
    struct rtk_bt_vendor_config_entry* entry = config->entry;
    unsigned int i = 0;
    //uint32_t baudrate = 0;
    uint32_t config_has_bdaddr = 0;

   if(config==NULL)
	return 0;
    if (config->signature != RTK_VENDOR_CONFIG_MAGIC)
    {
        RTKBT_ERR("config signature magic number(%x) is not set to RTK_VENDOR_CONFIG_MAGIC", config->signature);
        return 0;
    }

    if (config_len != *filelen - sizeof(struct rtk_bt_vendor_config))
    {
        RTKBT_ERR("config len(%x) is not right(%x)", config_len, *filelen-sizeof(struct rtk_bt_vendor_config));
        return 0;
    }

    for (i=0; i<config_len;)
    {

        switch(entry->offset)
        {
            int j = 0;
            case 0x3c:
            {
                config_has_bdaddr = 1;
                for (j=0; j<entry->entry_len; j++)
                    entry->entry_data[j] = bt_addr[entry->entry_len - 1- j];
                RTKBT_DBG("rtk_parse_config_file: config has bdaddr");
                break;
            }

            default:
                RTKBT_DBG("config offset(%x),length(%x)", entry->offset, entry->entry_len);
                break;
        }
        temp = entry->entry_len + sizeof(struct rtk_bt_vendor_config_entry);
        i += temp;
        entry = (struct rtk_bt_vendor_config_entry*)((uint8_t*)entry + temp);
    }

    return 1;
}
int load_firmware(dev_data* dev_entry, uint8_t** buff)
{
	const struct firmware	*fw;
	struct usb_device		*udev;
	patch_info	*patch_entry;
	char		*fw_name;
	int			fw_len = 0, ret_val;

	int config_len = 0 ,buf_len =-1;
	uint8_t* buf = *buff, *config_file_buf = NULL;
   	uint8_t* epatch_buf = NULL;

	struct rtk_epatch* epatch_info = NULL;
        uint8_t need_download_fw = 1;
	struct rtk_extension_entry patch_lmp = {0};
	struct rtk_epatch_entry current_entry = {0};
	uint16_t lmp_version ;
	// read bt mac address from file to  vnd_local_bd_addr
        uint8_t vnd_local_bd_addr[6]={0x01,0x02,0x03,0x04,0x05,0x06};

	RTKBT_DBG("load_firmware start");
	udev = dev_entry->udev;
	patch_entry = dev_entry->patch_entry;
	lmp_version = patch_entry->lmp_sub;


	RTKBT_ERR("lmp_version = 0x%04x", lmp_version);

	fw_name = patch_entry->config_name;
	ret_val = request_firmware(&fw, fw_name, &udev->dev);
	if (ret_val < 0)
		config_len = 0;
	else
	{
		config_file_buf = kzalloc(fw->size, GFP_KERNEL);
		if (NULL == config_file_buf) goto alloc_fail;
			memcpy(config_file_buf, fw->data, fw->size);
		config_len = fw->size;
                rtk_parse_config_file(config_file_buf, &config_len, vnd_local_bd_addr);

	}

	release_firmware(fw);
	fw_name = patch_entry->patch_name;
	ret_val = request_firmware(&fw, fw_name, &udev->dev);
	if (ret_val < 0)
	{
		fw_len = 0;
		kfree(config_file_buf);
		config_file_buf= NULL;
		goto fw_fail;
	}
	epatch_buf = kzalloc(fw->size, GFP_KERNEL);
	if (NULL == epatch_buf) goto alloc_fail;
	memcpy(epatch_buf, fw->data, fw->size);
	buf_len = fw->size + config_len;

	if(lmp_version == ROM_LMP_8723a)
	{
		RTKBT_ERR("This is 8723a, use old patch style!");
		if(memcmp(epatch_buf, RTK_EPATCH_SIGNATURE, 8) == 0)
		{
			RTKBT_ERR("8723as Check signature error!");
			need_download_fw = 0;
	}
		else
		{
			if (!(buf = kzalloc(buf_len, GFP_KERNEL))) {
				RTKBT_ERR("Can't alloc memory for fw&config");
				buf_len = -1;
			}
			else
			{
				RTKBT_DBG("8723as, fw copy direct");
				memcpy(buf,epatch_buf,buf_len);
				kfree(epatch_buf);
				epatch_buf = NULL;
				if (config_len)
				{
					memcpy(&buf[buf_len - config_len], config_file_buf, config_len);
				}
			}
		}
	}

	else
	{
		RTKBT_ERR("This is not 8723a, use new patch style!");
		//Get version from ROM
		gEVersion = rtk_get_eversion(dev_entry);  //gEVersion is set.
		RTKBT_DBG("gEVersion=%d", gEVersion);
		if(gEVersion == 0xFE) {
			RTKBT_DBG("gEVersion=%d", gEVersion);
			need_download_fw = 0;
			fw_len = 0;
			goto alloc_fail;
		}

		//check Extension Section Field
		if(memcmp(epatch_buf + buf_len-config_len-4 ,Extension_Section_SIGNATURE,4) != 0)
		{
			RTKBT_ERR("Check Extension_Section_SIGNATURE error! do not download fw");
			need_download_fw = 0;
		}
		else
		{
			uint8_t *temp;
			temp = epatch_buf+buf_len-config_len-5;
			do{
				if(*temp == 0x00)
				{
					patch_lmp.opcode = *temp;
					patch_lmp.length = *(temp-1);
					if ((patch_lmp.data = kzalloc(patch_lmp.length, GFP_KERNEL)))
					{
						memcpy(patch_lmp.data,temp-2,patch_lmp.length);
					}
					RTKBT_DBG("opcode = 0x%x",patch_lmp.opcode);
					RTKBT_DBG("length = 0x%x",patch_lmp.length);
					RTKBT_DBG("data = 0x%x",*(patch_lmp.data));
					break;
				}
				temp -= *(temp-1)+2;
			}while(*temp != 0xFF);

			if(lmp_version != project_id[*(patch_lmp.data)])
			{
				RTKBT_ERR("lmp_version is %x, project_id is %x, does not match!!!",lmp_version,project_id[*(patch_lmp.data)]);
				need_download_fw = 0;
			}
			else
			{
				RTKBT_DBG("lmp_version is %x, project_id is %x, match!",lmp_version, project_id[*(patch_lmp.data)]);

				if(memcmp(epatch_buf, RTK_EPATCH_SIGNATURE, 8) != 0)
				{
					RTKBT_DBG("Check signature error!");
					need_download_fw = 0;
				}
				else
				{
					int i = 0;
					epatch_info = (struct rtk_epatch*)epatch_buf;
					RTKBT_DBG("fm_version = 0x%x",epatch_info->fm_version);
					RTKBT_DBG("number_of_total_patch = %d",epatch_info->number_of_total_patch);

					//get right epatch entry
					for(i=0; i<epatch_info->number_of_total_patch; i++)
					{
						if(*(uint16_t*)(epatch_buf+14+2*i) == gEVersion + 1)
						{
							current_entry.chipID = gEVersion + 1;
							current_entry.patch_length = *(uint16_t*)(epatch_buf+14+2*epatch_info->number_of_total_patch+2*i);
							current_entry.start_offset = *(uint32_t*)(epatch_buf+14+4*epatch_info->number_of_total_patch+4*i);
							break;
						}
					}
					RTKBT_DBG("chipID = %d",current_entry.chipID);
					RTKBT_DBG("patch_length = 0x%x",current_entry.patch_length);
					RTKBT_DBG("start_offset = 0x%x",current_entry.start_offset);

					//get right eversion patch: buf, buf_len
					buf_len = current_entry.patch_length + config_len;
					RTKBT_DBG("buf_len = 0x%x",buf_len);

					if (!(buf = kzalloc(buf_len, GFP_KERNEL))) {
						RTKBT_ERR("Can't alloc memory for multi fw&config");
						buf_len = -1;
					}
					else
					{
						memcpy(buf,&epatch_buf[current_entry.start_offset],current_entry.patch_length);
						memcpy(&buf[current_entry.patch_length-4],&epatch_info->fm_version,4);
					}
					kfree(epatch_buf);
						epatch_buf = NULL;

					if (config_len)
					{
						memcpy(&buf[buf_len - config_len], config_file_buf, config_len);
					}
				}
			}
		}
	}

       if (config_file_buf)
        	 kfree(config_file_buf);

	RTKBT_ERR("Fw:%s exists, config file:%s exists", (buf_len > 0) ? "":"not", (config_len>0)?"":"not");
	if (buf && (buf_len > 0) && (need_download_fw))
	{
		fw_len = buf_len;
		*buff = buf;
	}

	RTKBT_DBG("load_firmware done");

alloc_fail:
	release_firmware(fw);
fw_fail:
	return fw_len;
}

void init_xdata(
	xchange_data*	xdata,
	dev_data*		dev_entry)
{
	memset(xdata, 0, sizeof(xchange_data));
	xdata->dev_entry = dev_entry;
	xdata->pipe_in = usb_rcvintpipe(dev_entry->udev, INTR_EP);
	xdata->pipe_out = usb_sndctrlpipe(dev_entry->udev, CTRL_EP);
        xdata->send_pkt = kzalloc(PKT_LEN, GFP_KERNEL);
	xdata->rcv_pkt = kzalloc(PKT_LEN, GFP_KERNEL);
	xdata->cmd_hdr = (struct hci_command_hdr*)(xdata->send_pkt);
	xdata->evt_hdr = (struct hci_event_hdr*)(xdata->rcv_pkt);
	xdata->cmd_cmp = (struct hci_ev_cmd_complete*)(xdata->rcv_pkt + EVT_HDR_LEN);
	xdata->req_para = xdata->send_pkt + CMD_HDR_LEN;
	xdata->rsp_para = xdata->rcv_pkt + EVT_HDR_LEN + CMD_CMP_LEN;
}

int check_fw_version(xchange_data* xdata)
{
	struct hci_rp_read_local_version *read_ver_rsp;
	patch_info	*patch_entry;
	int			ret_val;

	xdata->cmd_hdr->opcode = cpu_to_le16(HCI_OP_READ_LOCAL_VERSION);
	xdata->cmd_hdr->plen = 0;
	xdata->pkt_len = CMD_HDR_LEN;

	ret_val = send_hci_cmd(xdata);
	if (ret_val < 0)
	{
		goto version_end;
	}

	ret_val = rcv_hci_evt(xdata);
	if (ret_val < 0)
	{
		goto version_end;
	}

	patch_entry = xdata->dev_entry->patch_entry;
	read_ver_rsp = (struct hci_rp_read_local_version*)(xdata->rsp_para);
	RTKBT_DBG("check_fw_version : read_ver_rsp->lmp_subver = 0x%x",read_ver_rsp->lmp_subver);
	RTKBT_DBG("check_fw_version : patch_entry->lmp_sub = 0x%x",patch_entry->lmp_sub);
	if (patch_entry->lmp_sub != read_ver_rsp->lmp_subver)
	{
		return 1;
	}

	ret_val = 0;
version_end:
	return ret_val;
}

uint8_t rtk_get_eversion(dev_data * dev_entry)
{
	struct rtk_eversion_evt *eversion;
	patch_info	*patch_entry;
	int			ret_val = 0;
	xchange_data* xdata = NULL;

	RTKBT_DBG("rtk_get_eversion::gEVersion=%d", gEVersion);
	if(gEVersion != 0xFF && gEVersion != 0xFE) {
		RTKBT_DBG("gEVersion != 0xFF, return it directly!");
		return gEVersion;
	}

	xdata = kzalloc(sizeof(xchange_data), GFP_KERNEL);
	if(NULL == xdata)
	{
		ret_val = 0xFE;
		RTKBT_DBG("NULL == xdata");
              return ret_val;
       }

	init_xdata(xdata, dev_entry);

	xdata->cmd_hdr->opcode = cpu_to_le16(HCI_VENDOR_READ_RTK_ROM_VERISION);
	xdata->cmd_hdr->plen = 0;
	xdata->pkt_len = CMD_HDR_LEN;

	ret_val = send_hci_cmd(xdata);
	if (ret_val < 0)
	{
		ret_val = 0xFE;
		goto version_end;
	}

	ret_val = rcv_hci_evt(xdata);
	if (ret_val < 0)
	{
		ret_val = 0xFE;
		goto version_end;
	}

	patch_entry = xdata->dev_entry->patch_entry;
	eversion = (struct rtk_eversion_evt*)(xdata->rsp_para);
	RTKBT_DBG("rtk_get_eversion : eversion->status = 0x%x, eversion->version = 0x%x",eversion->status, eversion->version);
	if (eversion->status)
	{
		ret_val = 0;
	//	global_eversion = 0;
	}
	else
	{
		ret_val =  eversion->version;
	//	global_eversion = eversion->version;
	}

version_end:
	if(xdata != NULL)
	{
                if(xdata->send_pkt)
			kfree(xdata->send_pkt);
		if(xdata->rcv_pkt)
			kfree(xdata->rcv_pkt);
        	kfree(xdata);
	}
	return ret_val;

}

int get_firmware(xchange_data* xdata)
{
	dev_data	*dev_entry;
	patch_info	*patch_entry;
	RTKBT_DBG("get_firmware start");

	dev_entry = xdata->dev_entry;
	patch_entry = dev_entry->patch_entry;
	if (patch_entry->fw_len > 0)
	{
		xdata->fw_data = kzalloc(patch_entry->fw_len, GFP_KERNEL);
		if (NULL == xdata->fw_data) return -ENOMEM;
		memcpy(xdata->fw_data, patch_entry->fw_cache, patch_entry->fw_len);
		xdata->fw_len = patch_entry->fw_len;
	}
	else
	{
		xdata->fw_len = load_firmware(dev_entry, &xdata->fw_data);
		if (xdata->fw_len <= 0) return -1;
	}
	RTKBT_DBG("get_firmware done");
	return 0;
}

int download_data(xchange_data* xdata)
{
	download_cp *cmd_para;
	download_rp *evt_para;
	uint8_t		*pcur;
	int			pkt_len, frag_num, frag_len;
	int			i, ret_val;

	RTKBT_DBG("download_data start");

	cmd_para = (download_cp*)xdata->req_para;
	evt_para = (download_rp*)xdata->rsp_para;
	pcur = xdata->fw_data;
	pkt_len = CMD_HDR_LEN + sizeof(download_cp);
	frag_num = xdata->fw_len / PATCH_SEG_MAX + 1;
	frag_len = PATCH_SEG_MAX;

	for (i = 0; i < frag_num; i++)
	{
		cmd_para->index = i;
		if (i == (frag_num - 1))
		{
			cmd_para->index |= DATA_END;
			frag_len = xdata->fw_len % PATCH_SEG_MAX;
			pkt_len -= (PATCH_SEG_MAX - frag_len);
		}
		xdata->cmd_hdr->opcode = cpu_to_le16(DOWNLOAD_OPCODE);
		xdata->cmd_hdr->plen = sizeof(uint8_t) + frag_len;
		xdata->pkt_len = pkt_len;
		memcpy(cmd_para->data, pcur, frag_len);

		ret_val = send_hci_cmd(xdata);
		if (ret_val < 0)
		{
			return ret_val;
		}

		ret_val = rcv_hci_evt(xdata);
		if (ret_val < 0)
		{
			return ret_val;
		}
		if (0 != evt_para->status)
		{
			return -1;
		}

		pcur += PATCH_SEG_MAX;
	}

	RTKBT_DBG("download_data done");
	return xdata->fw_len;
}

int send_hci_cmd(xchange_data* xdata)
{
	int ret_val;

	ret_val = usb_control_msg(
		xdata->dev_entry->udev, xdata->pipe_out,
		0, USB_TYPE_CLASS, 0, 0,
		(void*)(xdata->send_pkt),
		xdata->pkt_len, MSG_TO);

	return ret_val;
}

int rcv_hci_evt(xchange_data* xdata)
{
	int ret_len = 0, ret_val = 0;
	int i;   // Added by Realtek

	while (1)
	{

		// **************************** Modifed by Realtek (begin)
		for(i = 0; i < 5; i++)   // Try to send USB interrupt message 5 times.
		{
		ret_val = usb_interrupt_msg(
			xdata->dev_entry->udev, xdata->pipe_in,
			(void*)(xdata->rcv_pkt), PKT_LEN,
			&ret_len, MSG_TO);
			if(ret_val >= 0)
				break;
		}
		// **************************** Modifed by Realtek (end)

		if (ret_val < 0)
		{
			return ret_val;
		}


	      if (CMD_CMP_EVT == xdata->evt_hdr->evt)
	       {
	           if (xdata->cmd_hdr->opcode == xdata->cmd_cmp->opcode)
	              return ret_len;
	       }


	}
}

void print_acl (struct sk_buff *skb,int dataOut)
{
#if PRINT_ACL_DATA
	uint wlength = skb->len;
	uint icount=0;
	u16* handle = (u16*)(skb->data);
	u16 dataLen=*(handle+1);
	u8* acl_data =(u8*)(skb->data);
//if (0==dataOut)
	printk("%d handle:%04x,len:%d,",dataOut,*handle,dataLen);
//else
//	printk("In handle:%04x,len:%d,",*handle,dataLen);
/*	for(icount=4;(icount<wlength)&&(icount<32);icount++)
		{
			printk("%02x ",*(acl_data+icount) );
		}
	printk("\n");
*/
#endif
}
void print_command(struct sk_buff *skb)
{
#if PRINT_CMD_EVENT
	uint wlength = skb->len;
	uint icount=0;
	u16* opcode = (u16*)(skb->data);
	u8* cmd_data =(u8*)(skb->data);
	u8 paramLen=*(cmd_data+2);

	switch (*opcode) {
	case HCI_OP_INQUIRY:
		printk("HCI_OP_INQUIRY");
		break;
	case HCI_OP_INQUIRY_CANCEL:
		printk("HCI_OP_INQUIRY_CANCEL");
		break;
	case HCI_OP_EXIT_PERIODIC_INQ:
		printk("HCI_OP_EXIT_PERIODIC_INQ");
		break;
	case HCI_OP_CREATE_CONN:
		printk("HCI_OP_CREATE_CONN");
		break;
	case HCI_OP_DISCONNECT:
		printk("HCI_OP_DISCONNECT");
		break;
	case HCI_OP_CREATE_CONN_CANCEL:
		printk("HCI_OP_CREATE_CONN_CANCEL");
		break;
	case HCI_OP_ACCEPT_CONN_REQ:
		printk("HCI_OP_ACCEPT_CONN_REQ");
		break;
	case HCI_OP_REJECT_CONN_REQ:
		printk("HCI_OP_REJECT_CONN_REQ");
		break;
	case HCI_OP_AUTH_REQUESTED:
		printk("HCI_OP_AUTH_REQUESTED");
		break;
	case HCI_OP_SET_CONN_ENCRYPT:
		printk("HCI_OP_SET_CONN_ENCRYPT");
		break;
	case HCI_OP_REMOTE_NAME_REQ:
		printk("HCI_OP_REMOTE_NAME_REQ");
		break;
	case HCI_OP_READ_REMOTE_FEATURES:
		printk("HCI_OP_READ_REMOTE_FEATURES");
		break;
	case HCI_OP_SNIFF_MODE:
		printk("HCI_OP_SNIFF_MODE");
		break;
	case HCI_OP_EXIT_SNIFF_MODE:
		printk("HCI_OP_EXIT_SNIFF_MODE");
		break;
	case HCI_OP_SWITCH_ROLE:
		printk("HCI_OP_SWITCH_ROLE");
		break;
	case HCI_OP_SNIFF_SUBRATE:
		printk("HCI_OP_SNIFF_SUBRATE");
		break;
	case HCI_OP_RESET:
		printk("HCI_OP_RESET");
		break;
	default:
		printk("CMD");
		break;
	}
	printk(":%04x,len:%d,",*opcode,paramLen);
	for(icount=3;(icount<wlength)&&(icount<24);icount++)
		{
			printk("%02x ",*(cmd_data+icount) );
		}
	printk("\n");

#endif
}
void print_event(struct sk_buff *skb)
{
#if PRINT_CMD_EVENT
	uint wlength = skb->len;
	uint icount=0;
	u8* opcode = (u8*)(skb->data);
	u8 paramLen=*(opcode+1);

	switch (*opcode) {
	case HCI_EV_INQUIRY_COMPLETE:
		printk("HCI_EV_INQUIRY_COMPLETE");
		break;
	case HCI_EV_INQUIRY_RESULT:
		printk("HCI_EV_INQUIRY_RESULT");
		break;
	case HCI_EV_CONN_COMPLETE:
		printk("HCI_EV_CONN_COMPLETE");
		break;
	case HCI_EV_CONN_REQUEST:
		printk("HCI_EV_CONN_REQUEST");
		break;
	case HCI_EV_DISCONN_COMPLETE:
		printk("HCI_EV_DISCONN_COMPLETE");
		break;
	case HCI_EV_AUTH_COMPLETE:
		printk("HCI_EV_AUTH_COMPLETE");
		break;
	case HCI_EV_REMOTE_NAME:
		printk("HCI_EV_REMOTE_NAME");
		break;
	case HCI_EV_ENCRYPT_CHANGE:
		printk("HCI_EV_ENCRYPT_CHANGE");
		break;
	case HCI_EV_CHANGE_LINK_KEY_COMPLETE:
		printk("HCI_EV_CHANGE_LINK_KEY_COMPLETE");
		break;
	case HCI_EV_REMOTE_FEATURES:
		printk("HCI_EV_REMOTE_FEATURES");
		break;
	case HCI_EV_REMOTE_VERSION:
		printk("HCI_EV_REMOTE_VERSION");
		break;
	case HCI_EV_QOS_SETUP_COMPLETE:
		printk("HCI_EV_QOS_SETUP_COMPLETE");
		break;
	case HCI_EV_CMD_COMPLETE:
		printk("HCI_EV_CMD_COMPLETE");
		break;
	case HCI_EV_CMD_STATUS:
		printk("HCI_EV_CMD_STATUS");
		break;
	case HCI_EV_ROLE_CHANGE:
		printk("HCI_EV_ROLE_CHANGE");
		break;
	case HCI_EV_NUM_COMP_PKTS:
		printk("HCI_EV_NUM_COMP_PKTS");
		break;
	case HCI_EV_MODE_CHANGE:
		printk("HCI_EV_MODE_CHANGE");
		break;
	case HCI_EV_PIN_CODE_REQ:
		printk("HCI_EV_PIN_CODE_REQ");
		break;
	case HCI_EV_LINK_KEY_REQ:
		printk("HCI_EV_LINK_KEY_REQ");
		break;
	case HCI_EV_LINK_KEY_NOTIFY:
		printk("HCI_EV_LINK_KEY_NOTIFY");
		break;
	case HCI_EV_CLOCK_OFFSET:
		printk("HCI_EV_CLOCK_OFFSET");
		break;
	case HCI_EV_PKT_TYPE_CHANGE:
		printk("HCI_EV_PKT_TYPE_CHANGE");
		break;
	case HCI_EV_PSCAN_REP_MODE:
		printk("HCI_EV_PSCAN_REP_MODE");
		break;
	case HCI_EV_INQUIRY_RESULT_WITH_RSSI:
		printk("HCI_EV_INQUIRY_RESULT_WITH_RSSI");
		break;
	case HCI_EV_REMOTE_EXT_FEATURES:
		printk("HCI_EV_REMOTE_EXT_FEATURES");
		break;
	case HCI_EV_SYNC_CONN_COMPLETE:
		printk("HCI_EV_SYNC_CONN_COMPLETE");
		break;
	case HCI_EV_SYNC_CONN_CHANGED:
		printk("HCI_EV_SYNC_CONN_CHANGED");
		break;
	case HCI_EV_SNIFF_SUBRATE:
		printk("HCI_EV_SNIFF_SUBRATE");
		break;
	case HCI_EV_EXTENDED_INQUIRY_RESULT:
		printk("HCI_EV_EXTENDED_INQUIRY_RESULT");
		break;
	case HCI_EV_IO_CAPA_REQUEST:
		printk("HCI_EV_IO_CAPA_REQUEST");
		break;
	case HCI_EV_SIMPLE_PAIR_COMPLETE:
		printk("HCI_EV_SIMPLE_PAIR_COMPLETE");
		break;
	case HCI_EV_REMOTE_HOST_FEATURES:
		printk("HCI_EV_REMOTE_HOST_FEATURES");
		break;
	default:
		printk("event");
		break;
	}
	printk(":%02x,len:%d,",*opcode,paramLen);
	for(icount=2;(icount<wlength)&&(icount<24);icount++)
	{
			printk("%02x ",*(opcode+icount) );
	}
	printk("\n");

#endif

}

#if CONFIG_BLUEDROID //for 4.2
//=========================================
/*
 * Realtek - Add for Android 4.2 (fake character device)
 */
static int
btfcd_open(struct inode *inode_p,
           struct file  *file_p)

{
	struct btusb_data *data;
	struct hci_dev *hdev;

	RTKBT_DBG("btfcd open\n");

	hdev = hci_dev_get(0);
	if (!hdev)
		return -1;
	data = GET_DRV_DATA(hdev);

	skb_queue_head_init(&data->readq);
	init_waitqueue_head(&data->read_wait);

	atomic_inc(&hdev->promisc);
	file_p->private_data = data;

	hci_dev_open(0);
	return nonseekable_open(inode_p, file_p);
}

static int
btfcd_close(struct inode  *inode_p,
            struct file   *file_p)
{
  struct hci_dev *hdev;

  RTKBT_DBG("btfcd close\n");

  hdev = hci_dev_get(0);
  atomic_dec(&hdev->promisc);

  hci_dev_close(0);

  file_p->private_data = NULL;
  return SUCCESS;
}

static inline ssize_t usb_put_user(struct btusb_data *data,
			struct sk_buff *skb, char __user *buf, int count)
{
	char __user *ptr = buf;
	int len, total = 0;

	len = min_t(unsigned int, skb->len, count);

	if (copy_to_user(ptr, skb->data, len))
		return -EFAULT;

	total += len;

	return total;
}


static struct sk_buff *rtk_skb_queue[QUEUE_SIZE];
static int rtk_skb_queue_front = -1;
static int rtk_skb_queue_rear = -1;

static void enqueue(struct sk_buff *skb)
{
    if(rtk_skb_queue_front == (rtk_skb_queue_rear+1)%QUEUE_SIZE)
    {
    	RTKBT_ERR("queue is full\n");
    }
    else
    {
        if(rtk_skb_queue_front==-1)
        	rtk_skb_queue_front=rtk_skb_queue_rear=0;
        else
        	rtk_skb_queue_rear=(rtk_skb_queue_rear+1)%QUEUE_SIZE;
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
        rtk_skb_queue[rtk_skb_queue_rear] = __pskb_copy(skb, 1, GFP_ATOMIC);
#else
        rtk_skb_queue[rtk_skb_queue_rear] = pskb_copy(skb, GFP_ATOMIC);
#endif

    }
}


static struct sk_buff* dequeue(void)
{
	struct sk_buff *skb;
    if(rtk_skb_queue_front == -1)
    {
    	RTKBT_ERR("queue is empty\n");
        return NULL;
    }
    else
    {
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
    	skb = __pskb_copy(rtk_skb_queue[rtk_skb_queue_front], 1, GFP_ATOMIC);
#else
    	skb = pskb_copy(rtk_skb_queue[rtk_skb_queue_front], GFP_ATOMIC);
#endif
        kfree_skb(rtk_skb_queue[rtk_skb_queue_front]);
        if(rtk_skb_queue_front==rtk_skb_queue_rear)
        	rtk_skb_queue_front=rtk_skb_queue_rear=-1;
        else
        	rtk_skb_queue_front = (rtk_skb_queue_front+1)%QUEUE_SIZE;
        return skb;
    }
    return NULL;
}

static int is_queue_empty(void)
{
	return (rtk_skb_queue_front == -1)?1:0;
}

static ssize_t
btfcd_read(struct file  *file_p,
           char __user  *buf_p,
           size_t       count,
           loff_t       *pos_p)
{
	struct btusb_data *data;
	struct sk_buff *skb;
	ssize_t ret = 0;

	//RTKBT_DBG("==========btfcd_read\n");

	while (count) {
		data = GET_DRV_DATA(hci_dev_get(0));
		if (!is_queue_empty()) {
			skb = dequeue();
		} else {
			skb = NULL;
		}
		if (skb) {
		    ret = usb_put_user(data, skb, buf_p, count);
		    kfree_skb(skb);

		    skb = NULL;
		    if (ret < 0){
		    	RTKBT_ERR("==========btfcd copy_to_user return -1 \n");
		    }
			break;
		}

		ret = wait_event_interruptible(data->read_wait, !is_queue_empty());
		if (ret < 0){
			//RTKBT_DBG("I return read %d \n", ret);
			break;
		}
    }
	return ret;
} /* End of btfcd_read */

static ssize_t
btfcd_write(struct file  *file_p,
            const char __user  *buf_p,
            size_t       count,
            loff_t       *pos_p)
{
	struct btusb_data *data = file_p->private_data;
	struct sk_buff *skb;

	//RTKBT_DBG("==========btfcd_write\n");

	if (count > HCI_MAX_FRAME_SIZE)
		return -EINVAL;

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	if (copy_from_user(skb_put(skb, count), buf_p, count)) {
		printk("==========btfcd copy_from_user return -1 \n");
		kfree_skb(skb);
		return -EFAULT;
	}

        if (!data || !data->hdev) {
                return -EFAULT;
        }

	skb->dev = (void *) data->hdev;
	bt_cb(skb)->pkt_type = *((__u8 *) skb->data);
	skb_pull(skb, 1);

	data->hdev->send(skb);

	return count;
} /* End of btfcd_write */

static unsigned int btfcd_poll(struct file *file, poll_table *wait)
{
	struct btusb_data *data;
	data = GET_DRV_DATA(hci_dev_get(0));

	//RTKBT_DBG("==========btfcd_poll\n");

	poll_wait(file, &data->read_wait, wait);

	if (!is_queue_empty())
		return POLLIN | POLLRDNORM;

	return POLLOUT | POLLWRNORM;
} /* End of btfcd_poll */

static int btfcd_init(void)
{
    printk("Registering file operations for btfcd driver\n");

    /*
     * Register btusb as a character device driver with linux OS and return
     * ERROR on failure
     */
    if ((cl = class_create(THIS_MODULE, DEVICE_NAME)) == NULL)
    {
        return ERROR;
    }
    if (alloc_chrdev_region(&first, 0, 1, DEVICE_NAME) < 0)
    {
    	class_destroy(cl);
        return ERROR;
    }
    if (device_create(cl, NULL, first, NULL, DEVICE_NAME) == NULL)
    {
    	unregister_chrdev_region(first, 1);
    	class_destroy(cl);
        return ERROR;
    }
    cdev_init(&c_dev, &btfcd_file_ops_g);
    if (cdev_add(&c_dev, first, 1) == -1)
    {
    	device_destroy(cl, first);
    	unregister_chrdev_region(first, 1);
    	class_destroy(cl);
        return ERROR;
    }

    printk("Class driver initialized successfully\n");

    return SUCCESS;
} /* end of btfcd_init() */

static void btfcd_exit(void)
{
    printk("Unregister btfcd as a character device driver\n");

    /*
     * Unregister btusb as a character device driver with linux OS and on
     * failure try again unregistring
     */
    device_destroy(cl, first);
    cdev_del(&c_dev);
    unregister_chrdev_region(first, 1);
    class_destroy(cl);

    printk("Exiting btfcd driver\n");

    return ;
} /* end of btfcd_exit() */

static void hci_send_to_stack(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct sk_buff *rtk_skb_copy = NULL;
	struct btusb_data *data;

	//RTKBT_DBG("hci_send_to_stack\n");

	if (!hdev) {
		RTKBT_ERR("Frame for unknown HCI device (hdev=NULL)");
		return;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		RTKBT_ERR("HCI not running");
		return;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
	rtk_skb_copy = __pskb_copy(skb, 1, GFP_ATOMIC);
#else
	rtk_skb_copy = pskb_copy(skb, GFP_ATOMIC);
#endif
	if (!rtk_skb_copy) {
		RTKBT_ERR("copy skb error");
		return;
	}

	memcpy(skb_push(rtk_skb_copy, 1), &bt_cb(skb)->pkt_type, 1);
	enqueue(rtk_skb_copy);
	kfree_skb(rtk_skb_copy);
	data = GET_DRV_DATA(hci_dev_get(0));
	wake_up_interruptible(&data->read_wait);

    return ;
}
/*
 * Realtek - Add for Android 4.2 (fake character device) end
 */
//=========================================

//=========================================
/*
 * Realtek - Integrate from hci_core.c
 */

/* Get HCI device by index.
 * Device is held on return. */
static struct hci_dev *hci_dev_get(int index)
{
	//RTKBT_DBG("hci_dev_get index=%d", index);

	if (index !=0)
		return NULL;

	if (Ghdev==NULL)
		RTKBT_ERR("hci_dev_get Ghdev==NULL");
	return Ghdev;
}

/* ---- HCI ioctl helpers ---- */
static int hci_dev_open(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	//RTKBT_DBG("hci_dev_open dev=%d ", dev);
	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	//RTKBT_DBG("%s %p", hdev->name, hdev);

	//RTKBT_DBG("hci_dev_open ------------1");
	if (test_bit(HCI_UNREGISTER, &hdev->dev_flags)) {
		ret = -ENODEV;
		goto done;
	}


	if (test_bit(HCI_UP, &hdev->flags)) {
		ret = -EALREADY;
		goto done;
	}

	//RTKBT_DBG("hci_dev_open ------------1");
	if (hdev->open(hdev)) {
		ret = -EIO;
		goto done;
	}

	//RTKBT_DBG("hci_dev_open ------------2");
	set_bit(HCI_UP, &hdev->flags);
done:
	return ret;
}

static int hci_dev_do_close(struct hci_dev *hdev)
{
	//RTKBT_DBG(" hci_dev_do_close %s %p", hdev->name, hdev);
	if (hdev->flush)
		hdev->flush(hdev);
	/* After this point our queues are empty
	 * and no tasks are scheduled. */
	hdev->close(hdev);
	/* Clear flags */
	hdev->flags = 0;
	return 0;
}
static int hci_dev_close(__u16 dev)
{
	struct hci_dev *hdev;
	int err;
	//RTKBT_DBG(" hci_dev_close");
	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	err = hci_dev_do_close(hdev);

	return err;
}

static struct hci_dev *hci_alloc_dev(void)
{
	struct hci_dev *hdev;

	hdev = kzalloc(sizeof(struct hci_dev), GFP_KERNEL);
	if (!hdev)
		return NULL;

	return hdev;
}

/* Free HCI device */
static void hci_free_dev(struct hci_dev *hdev)
{
    //	skb_queue_purge(&hdev->driver_init);
    //RTKBT_DBG("hci_free_dev-----1 ");
	/* will free via device release */
    //	put_device(&hdev->dev);
    kfree(hdev);
    //RTKBT_DBG("hci_free_dev-----2 ");
}

/* Register HCI device */
static int hci_register_dev(struct hci_dev *hdev)
{
	int i, id;

	RTKBT_DBG("hci_register_dev %p name %s bus %d", hdev, hdev->name, hdev->bus);
	/* Do not allow HCI_AMP devices to register at index 0,
	 * so the index can be used as the AMP controller ID.
	 */
	id = (hdev->dev_type == HCI_BREDR) ? 0 : 1;

	RTKBT_DBG("id=%d ",id);

	write_lock(&hci_dev_list_lock);

	sprintf(hdev->name, "hci%d", id);
	RTKBT_DBG("hdev->name=%s ",hdev->name);
	hdev->id = id;
	mutex_init(&hdev->lock);

	hdev->flags = 0;
	hdev->dev_flags = 0;
	for (i = 0; i < NUM_REASSEMBLY; i++)
		hdev->reassembly[i] = NULL;

	memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));
	atomic_set(&hdev->promisc, 0);

	if(Ghdev)
		RTKBT_ERR(" ====Ghdev not null !!!!");
	Ghdev =hdev;
	if(Ghdev)
		RTKBT_ERR(" ===================hci_register_dev success");
	write_unlock(&hci_dev_list_lock);

	return id;
}

/* Unregister HCI device */
static void hci_unregister_dev(struct hci_dev *hdev)
{
	int i;

	RTKBT_DBG("hci_unregister_dev hdev%p name %s bus %d", hdev, hdev->name, hdev->bus);
	set_bit(HCI_UNREGISTER, &hdev->dev_flags);

	write_lock(&hci_dev_list_lock);
	Ghdev = NULL;
	write_unlock(&hci_dev_list_lock);

	hci_dev_do_close(hdev);
	for (i = 0; i < NUM_REASSEMBLY; i++)
		kfree_skb(hdev->reassembly[i]);
}

/* Receive frame from HCI drivers */
static int hci_recv_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
//	RTKBT_DBG("hci_recv_frame hdev->flags =%d",hdev->flags);
	if (!hdev || (!test_bit(HCI_UP, &hdev->flags)
				&& !test_bit(HCI_INIT, &hdev->flags))) {
		kfree_skb(skb);
		return -ENXIO;
	}

	/* Incomming skb */
	bt_cb(skb)->incoming = 1;

	/* Time stamp */
	__net_timestamp(skb);

	if (atomic_read(&hdev->promisc)) {
			/* Send copy to the sockets */
		        //RTKBT_DBG("send data to stack");
			hci_send_to_stack(hdev, skb);
	}
	kfree_skb(skb);
	return 0;
}

static int hci_reassembly(struct hci_dev *hdev, int type, void *data,
						  int count, __u8 index)
{
	int len = 0;
	int hlen = 0;
	int remain = count;
	struct sk_buff *skb;
	struct bt_skb_cb *scb;

	if ((type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT) ||
				index >= NUM_REASSEMBLY)
		return -EILSEQ;

	skb = hdev->reassembly[index];

	//RTKBT_DBG("hci_reassembly");
	if (!skb) {
		switch (type) {
		case HCI_ACLDATA_PKT:
			len = HCI_MAX_FRAME_SIZE;
			hlen = HCI_ACL_HDR_SIZE;
			break;
		case HCI_EVENT_PKT:
			len = HCI_MAX_EVENT_SIZE;
			hlen = HCI_EVENT_HDR_SIZE;
			break;
		case HCI_SCODATA_PKT:
			len = HCI_MAX_SCO_SIZE;
			hlen = HCI_SCO_HDR_SIZE;
			break;
		}

		skb = bt_skb_alloc(len, GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;

		scb = (void *) skb->cb;
		scb->expect = hlen;
		scb->pkt_type = type;

		skb->dev = (void *) hdev;
		hdev->reassembly[index] = skb;
	}

	while (count) {
		scb = (void *) skb->cb;
		len = min_t(uint, scb->expect, count);

		memcpy(skb_put(skb, len), data, len);

		count -= len;
		data += len;
		scb->expect -= len;
		remain = count;

		switch (type) {
		case HCI_EVENT_PKT:
			if (skb->len == HCI_EVENT_HDR_SIZE) {
				struct hci_event_hdr *h = hci_event_hdr(skb);
				scb->expect = h->plen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;

		case HCI_ACLDATA_PKT:
			if (skb->len  == HCI_ACL_HDR_SIZE) {
				struct hci_acl_hdr *h = hci_acl_hdr(skb);
				scb->expect = __le16_to_cpu(h->dlen);

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;

		case HCI_SCODATA_PKT:
			if (skb->len == HCI_SCO_HDR_SIZE) {
				struct hci_sco_hdr *h = hci_sco_hdr(skb);
				scb->expect = h->dlen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;
		}

		if (scb->expect == 0) {
			/* Complete frame */

			if(HCI_ACLDATA_PKT==type )
				print_acl(skb,0);
			if(HCI_EVENT_PKT==type)
				print_event(skb);

			bt_cb(skb)->pkt_type = type;
			hci_recv_frame(skb);

			hdev->reassembly[index] = NULL;
			return remain;
		}
	}

	return remain;
}

static int hci_recv_fragment(struct hci_dev *hdev, int type, void *data, int count)
{
	int rem = 0;

	if (type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT)
		return -EILSEQ;

	while (count) {
		rem = hci_reassembly(hdev, type, data, count, type - 1);
		if (rem < 0)
			return rem;

		data += (count - rem);
		count = rem;
	}

	return rem;
}
/*
 * Realtek - Integrate from hci_core.c end
 */
//=========================================
#endif
