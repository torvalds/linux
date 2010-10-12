/*
 *  Texas Instrument's Bluetooth Driver For Shared Transport.
 *
 *  Bluetooth Driver acts as interface between HCI CORE and
 *  TI Shared Transport Layer.
 *
 *  Copyright (C) 2009 Texas Instruments
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
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

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "st.h"
#include "bt_drv.h"

/* Define this macro to get debug msg */
#undef DEBUG

#ifdef DEBUG
#define BT_DRV_DBG(fmt, arg...)  printk(KERN_INFO "(btdrv):"fmt"\n" , ## arg)
#define BTDRV_API_START()        printk(KERN_INFO "(btdrv): %s Start\n", \
	__func__)
#define BTDRV_API_EXIT(errno)    printk(KERN_INFO "(btdrv): %s Exit(%d)\n", \
	__func__, errno)
#else
#define BT_DRV_DBG(fmt, arg...)
#define BTDRV_API_START()
#define BTDRV_API_EXIT(errno)
#endif

#define BT_DRV_ERR(fmt, arg...)  printk(KERN_ERR "(btdrv):"fmt"\n" , ## arg)

static int reset;
static struct hci_st *hst;

/* Increments HCI counters based on pocket ID (cmd,acl,sco) */
static inline void hci_st_tx_complete(struct hci_st *hst, int pkt_type)
{
	struct hci_dev *hdev;

	BTDRV_API_START();

	hdev = hst->hdev;

	/* Update HCI stat counters */
	switch (pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.cmd_tx++;
		break;
	}

	BTDRV_API_EXIT(0);
}

/* ------- Interfaces to Shared Transport ------ */

/* Called by ST layer to indicate protocol registration completion
 * status.hci_st_open() function will wait for signal from this
 * API when st_register() function returns ST_PENDING.
 */
static void hci_st_registration_completion_cb(void *priv_data, char data)
{
	struct hci_st *lhst = (struct hci_st *)priv_data;
	BTDRV_API_START();

	/* hci_st_open() function needs value of 'data' to know
	 * the registration status(success/fail),So have a back
	 * up of it.
	 */
	lhst->streg_cbdata = data;

	/* Got a feedback from ST for BT driver registration
	 * request.Wackup hci_st_open() function to continue
	 * it's open operation.
	 */
	complete(&lhst->wait_for_btdrv_reg_completion);

	BTDRV_API_EXIT(0);
}

/* Called by Shared Transport layer when receive data is
 * available */
static long hci_st_receive(void *priv_data, struct sk_buff *skb)
{
	int err;
	int len;
	struct hci_st *lhst = (struct hci_st *)priv_data;

	BTDRV_API_START();

	err = 0;
	len = 0;

	if (skb == NULL) {
		BT_DRV_ERR("Invalid SKB received from ST");
		BTDRV_API_EXIT(-EFAULT);
		return -EFAULT;
	}
	if (!lhst) {
		kfree_skb(skb);
		BT_DRV_ERR("Invalid hci_st memory,freeing SKB");
		BTDRV_API_EXIT(-EFAULT);
		return -EFAULT;
	}
	if (!test_bit(BT_DRV_RUNNING, &lhst->flags)) {
		kfree_skb(skb);
		BT_DRV_ERR("Device is not running,freeing SKB");
		BTDRV_API_EXIT(-EINVAL);
		return -EINVAL;
	}

	len = skb->len;
	skb->dev = (struct net_device *)lhst->hdev;

	/* Forward skb to HCI CORE layer */
	err = hci_recv_frame(skb);
	if (err) {
		kfree_skb(skb);
		BT_DRV_ERR("Unable to push skb to HCI CORE(%d),freeing SKB",
			   err);
		BTDRV_API_EXIT(err);
		return err;
	}
	lhst->hdev->stat.byte_rx += len;

	BTDRV_API_EXIT(0);
	return 0;
}

/* ------- Interfaces to HCI layer ------ */

/* Called from HCI core to initialize the device */
static int hci_st_open(struct hci_dev *hdev)
{
	static struct st_proto_s hci_st_proto;
	unsigned long timeleft;
	int err;

	BTDRV_API_START();

	err = 0;

	BT_DRV_DBG("%s %p", hdev->name, hdev);

	/* Already registered with ST ? */
	if (test_bit(BT_ST_REGISTERED, &hst->flags)) {
		BT_DRV_ERR("Registered with ST already,open called again?");
		BTDRV_API_EXIT(0);
		return 0;
	}

	/* Populate BT driver info required by ST */
	memset(&hci_st_proto, 0, sizeof(hci_st_proto));

	/* BT driver ID */
	hci_st_proto.type = ST_BT;

	/* Receive function which called from ST */
	hci_st_proto.recv = hci_st_receive;

	/* Packet match function may used in future */
	hci_st_proto.match_packet = NULL;

	/* Callback to be called when registration is pending */
	hci_st_proto.reg_complete_cb = hci_st_registration_completion_cb;

	/* This is write function pointer of ST. BT driver will make use of this
	 * for sending any packets to chip. ST will assign and give to us, so
	 * make it as NULL */
	hci_st_proto.write = NULL;

	/* send in the hst to be received at registration complete callback
	 * and during st's receive
	 */
	hci_st_proto.priv_data = hst;

	/* Register with ST layer */
	err = st_register(&hci_st_proto);
	if (err == -EINPROGRESS) {
		/* Prepare wait-for-completion handler data structures.
		 * Needed to syncronize this and st_registration_completion_cb()
		 * functions.
		 */
		init_completion(&hst->wait_for_btdrv_reg_completion);

		/* Reset ST registration callback status flag , this value
		 * will be updated in hci_st_registration_completion_cb()
		 * function whenever it called from ST driver.
		 */
		hst->streg_cbdata = -EINPROGRESS;

		/* ST is busy with other protocol registration(may be busy with
		 * firmware download).So,Wait till the registration callback
		 * (passed as a argument to st_register() function) getting
		 * called from ST.
		 */
		BT_DRV_DBG(" %s waiting for reg completion signal from ST",
			   __func__);

		timeleft =
		    wait_for_completion_timeout
		    (&hst->wait_for_btdrv_reg_completion,
		     msecs_to_jiffies(BT_REGISTER_TIMEOUT));
		if (!timeleft) {
			BT_DRV_ERR("Timeout(%ld sec),didn't get reg"
				   "completion signal from ST",
				   BT_REGISTER_TIMEOUT / 1000);
			BTDRV_API_EXIT(-ETIMEDOUT);
			return -ETIMEDOUT;
		}

		/* Is ST registration callback called with ERROR value? */
		if (hst->streg_cbdata != 0) {
			BT_DRV_ERR("ST reg completion CB called with invalid"
				   "status %d", hst->streg_cbdata);
			BTDRV_API_EXIT(-EAGAIN);
			return -EAGAIN;
		}
		err = 0;
	} else if (err == -1) {
		BT_DRV_ERR("st_register failed %d", err);
		BTDRV_API_EXIT(-EAGAIN);
		return -EAGAIN;
	}

	/* Do we have proper ST write function? */
	if (hci_st_proto.write != NULL) {
		/* We need this pointer for sending any Bluetooth pkts */
		hst->st_write = hci_st_proto.write;
	} else {
		BT_DRV_ERR("failed to get ST write func pointer");

		/* Undo registration with ST */
		err = st_unregister(ST_BT);
		if (err < 0)
			BT_DRV_ERR("st_unregister failed %d", err);

		hst->st_write = NULL;
		BTDRV_API_EXIT(-EAGAIN);
		return -EAGAIN;
	}

	/* Registration with ST layer is completed successfully,
	 * now chip is ready to accept commands from HCI CORE.
	 * Mark HCI Device flag as RUNNING
	 */
	set_bit(HCI_RUNNING, &hdev->flags);

	/* Registration with ST successful */
	set_bit(BT_ST_REGISTERED, &hst->flags);

	BTDRV_API_EXIT(err);
	return err;
}

/* Close device */
static int hci_st_close(struct hci_dev *hdev)
{
	int err;

	BTDRV_API_START();

	err = 0;

	/* Unregister from ST layer */
	if (test_and_clear_bit(BT_ST_REGISTERED, &hst->flags)) {
		err = st_unregister(ST_BT);
		if (err != 0) {
			BT_DRV_ERR("st_unregister failed %d", err);
			BTDRV_API_EXIT(-EBUSY);
			return -EBUSY;
		}
	}

	hst->st_write = NULL;

	/* ST layer would have moved chip to inactive state.
	 * So,clear HCI device RUNNING flag.
	 */
	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags)) {
		BTDRV_API_EXIT(0);
		return 0;
	}

	BTDRV_API_EXIT(err);
	return err;
}

/* Called from HCI CORE , Sends frames to Shared Transport */
static int hci_st_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev;
	struct hci_st *hst;
	long len;

	BTDRV_API_START();

	if (skb == NULL) {
		BT_DRV_ERR("Invalid skb received from HCI CORE");
		BTDRV_API_EXIT(-ENOMEM);
		return -ENOMEM;
	}
	hdev = (struct hci_dev *)skb->dev;
	if (!hdev) {
		BT_DRV_ERR("SKB received for invalid HCI Device (hdev=NULL)");
		BTDRV_API_EXIT(-ENODEV);
		return -ENODEV;
	}
	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		BT_DRV_ERR("Device is not running");
		BTDRV_API_EXIT(-EBUSY);
		return -EBUSY;
	}

	hst = (struct hci_st *)hdev->driver_data;

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);

	BT_DRV_DBG(" %s: type %d len %d", hdev->name, bt_cb(skb)->pkt_type,
		   skb->len);

	/* Insert skb to shared transport layer's transmit queue.
	 * Freeing skb memory is taken care in shared transport layer,
	 * so don't free skb memory here.
	 */
	if (!hst->st_write) {
		kfree_skb(skb);
		BT_DRV_ERR(" Can't write to ST, st_write null?");
		BTDRV_API_EXIT(-EAGAIN);
		return -EAGAIN;
	}
	len = hst->st_write(skb);
	if (len < 0) {
		/* Something went wrong in st write , free skb memory */
		kfree_skb(skb);
		BT_DRV_ERR(" ST write failed (%ld)", len);
		BTDRV_API_EXIT(-EAGAIN);
		return -EAGAIN;
	}

	/* ST accepted our skb. So, Go ahead and do rest */
	hdev->stat.byte_tx += len;
	hci_st_tx_complete(hst, bt_cb(skb)->pkt_type);

	BTDRV_API_EXIT(0);
	return 0;
}

static void hci_st_destruct(struct hci_dev *hdev)
{
	BTDRV_API_START();

	if (!hdev) {
		BT_DRV_ERR("Destruct called with invalid HCI Device"
			   "(hdev=NULL)");
		BTDRV_API_EXIT(0);
		return;
	}

	BT_DRV_DBG("%s", hdev->name);

	/* free hci_st memory */
	if (hdev->driver_data != NULL)
		kfree(hdev->driver_data);

	BTDRV_API_EXIT(0);
	return;
}

/* Creates new HCI device */
static int hci_st_register_dev(struct hci_st *hst)
{
	struct hci_dev *hdev;

	BTDRV_API_START();

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_DRV_ERR("Can't allocate HCI device");
		BTDRV_API_EXIT(-ENOMEM);
		return -ENOMEM;
	}
	BT_DRV_DBG(" HCI device allocated. hdev= %p", hdev);

	hst->hdev = hdev;
	hdev->bus = HCI_UART;
	hdev->driver_data = hst;
	hdev->open = hci_st_open;
	hdev->close = hci_st_close;
	hdev->flush = NULL;
	hdev->send = hci_st_send_frame;
	hdev->destruct = hci_st_destruct;
	hdev->owner = THIS_MODULE;

	if (reset)
		set_bit(HCI_QUIRK_NO_RESET, &hdev->quirks);

	if (hci_register_dev(hdev) < 0) {
		BT_DRV_ERR("Can't register HCI device");
		hci_free_dev(hdev);
		BTDRV_API_EXIT(-ENODEV);
		return -ENODEV;
	}

	BT_DRV_DBG(" HCI device registered. hdev= %p", hdev);
	BTDRV_API_EXIT(0);
	return 0;
}

/* ------- Module Init interface ------ */

static int __init bt_drv_init(void)
{
	int err;

	BTDRV_API_START();

	err = 0;

	BT_DRV_DBG(" Bluetooth Driver Version %s", VERSION);

	/* Allocate local resource memory */
	hst = kzalloc(sizeof(struct hci_st), GFP_KERNEL);
	if (!hst) {
		BT_DRV_ERR("Can't allocate control structure");
		BTDRV_API_EXIT(-ENFILE);
		return -ENFILE;
	}

	/* Expose "hciX" device to user space */
	err = hci_st_register_dev(hst);
	if (err) {
		/* Release local resource memory */
		kfree(hst);

		BT_DRV_ERR("Unable to expose hci0 device(%d)", err);
		BTDRV_API_EXIT(err);
		return err;
	}
	set_bit(BT_DRV_RUNNING, &hst->flags);

	BTDRV_API_EXIT(err);
	return err;
}

/* ------- Module Exit interface ------ */

static void __exit bt_drv_exit(void)
{
	BTDRV_API_START();

	/* Deallocate local resource's memory  */
	if (hst) {
		struct hci_dev *hdev = hst->hdev;

		if (hdev == NULL) {
			BT_DRV_ERR("Invalid hdev memory");
			kfree(hst);
		} else {
			hci_st_close(hdev);
			if (test_and_clear_bit(BT_DRV_RUNNING, &hst->flags)) {
				/* Remove HCI device (hciX) created
				 * in module init.
				 */
				hci_unregister_dev(hdev);

				/* Free HCI device memory */
				hci_free_dev(hdev);
			}
		}
	}
	BTDRV_API_EXIT(0);
}

module_init(bt_drv_init);
module_exit(bt_drv_exit);

/* ------ Module Info ------ */

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");
MODULE_AUTHOR("Raja Mani <raja_mani@ti.com>");
MODULE_DESCRIPTION("Bluetooth Driver for TI Shared Transport" VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
