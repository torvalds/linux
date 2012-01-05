/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "common.h"

/*
 *** HARDWARE LIMITATION ***
 *
 * 1) renesas_usbhs has a limited number of controllable devices.
 *    it can control only 9 devices in generally.
 *	see DEVADDn / DCPMAXP / PIPEMAXP.
 *
 * 2) renesas_usbhs pipe number is limited.
 *    the pipe will be re-used for each devices.
 *    so, software should control DATA0/1 sequence of each devices.
 */


/*
 *		image of mod_host
 *
 * +--------+
 * | udev 0 | --> it is used when set address
 * +--------+
 *
 * +--------+					pipes are reused for each uep.
 * | udev 1 |-+- [uep 0 (dcp) ] --+		pipe will be switched when
 * +--------+ |			  |		target device was changed
 *	      +- [uep 1 (bulk)]	--|---+		   +--------------+
 *	      |			  +--------------> | pipe0 (dcp)  |
 *	      +- [uep 2 (bulk)]	--|---|---+	   +--------------+
 *				  |   |	  |	   | pipe1 (isoc) |
 * +--------+			  |   |	  |	   +--------------+
 * | udev 2 |-+- [uep 0 (dcp) ]	--+   +-- |------> | pipe2 (bulk) |
 * +--------+ |			  |   |	  |	   +--------------+
 *	      +- [uep 1 (int) ]	--|-+ |	  +------> | pipe3 (bulk) |
 *				  | | |	  |	   +--------------+
 * +--------+			  | +-|---|------> | pipe4 (int)  |
 * | udev 3 |-+- [uep 0 (dcp) ]	--+   |	  |	   +--------------+
 * +--------+ |			      |	  |	   | ....	  |
 *	      +- [uep 1 (bulk)]	------+	  |	   | ....	  |
 *	      |				  |
 *	      +- [uep 2 (bulk)]-----------+
 */


/*
 *		struct
 */
struct usbhsh_pipe_info {
	unsigned int		usr_cnt; /* see usbhsh_endpoint_alloc() */
};

struct usbhsh_request {
	struct urb		*urb;
	struct usbhs_pkt	pkt;
	struct list_head	ureq_link; /* see hpriv :: ureq_link_xxx */
};

struct usbhsh_device {
	struct usb_device	*usbv;
	struct list_head	ep_list_head; /* list of usbhsh_ep */
};

struct usbhsh_ep {
	struct usbhs_pipe	*pipe;
	struct usbhsh_device	*udev;   /* attached udev */
	struct list_head	ep_list; /* list to usbhsh_device */

	int maxp;
};

#define USBHSH_DEVICE_MAX	10 /* see DEVADDn / DCPMAXP / PIPEMAXP */
#define USBHSH_PORT_MAX		 7 /* see DEVADDn :: HUBPORT */
struct usbhsh_hpriv {
	struct usbhs_mod	mod;
	struct usbhs_pipe	*dcp;

	struct usbhsh_device	udev[USBHSH_DEVICE_MAX];

	struct usbhsh_pipe_info	*pipe_info;
	int			 pipe_size;

	u32	port_stat;	/* USB_PORT_STAT_xxx */

	struct completion	setup_ack_done;

	/* see usbhsh_req_alloc/free */
	struct list_head	ureq_link_active;
	struct list_head	ureq_link_free;
};


static const char usbhsh_hcd_name[] = "renesas_usbhs host";

/*
 *		macro
 */
#define usbhsh_priv_to_hpriv(priv) \
	container_of(usbhs_mod_get(priv, USBHS_HOST), struct usbhsh_hpriv, mod)

#define __usbhsh_for_each_hpipe(start, pos, h, i)	\
	for (i = start, pos = (h)->hpipe + i;		\
	     i < (h)->hpipe_size;			\
	     i++, pos = (h)->hpipe + i)

#define usbhsh_for_each_hpipe(pos, hpriv, i)	\
	__usbhsh_for_each_hpipe(1, pos, hpriv, i)

#define usbhsh_for_each_hpipe_with_dcp(pos, hpriv, i)	\
	__usbhsh_for_each_hpipe(0, pos, hpriv, i)

#define __usbhsh_for_each_udev(start, pos, h, i)	\
	for (i = start, pos = (h)->udev + i;		\
	     i < USBHSH_DEVICE_MAX;			\
	     i++, pos = (h)->udev + i)

#define usbhsh_for_each_udev(pos, hpriv, i)	\
	__usbhsh_for_each_udev(1, pos, hpriv, i)

#define usbhsh_for_each_udev_with_dev0(pos, hpriv, i)	\
	__usbhsh_for_each_udev(0, pos, hpriv, i)

#define usbhsh_hcd_to_hpriv(h)	(struct usbhsh_hpriv *)((h)->hcd_priv)
#define usbhsh_hcd_to_dev(h)	((h)->self.controller)

#define usbhsh_hpriv_to_priv(h)	((h)->mod.priv)
#define usbhsh_hpriv_to_dcp(h)	((h)->dcp)
#define usbhsh_hpriv_to_hcd(h)	\
	container_of((void *)h, struct usb_hcd, hcd_priv)

#define usbhsh_ep_to_uep(u)	((u)->hcpriv)
#define usbhsh_uep_to_pipe(u)	((u)->pipe)
#define usbhsh_uep_to_udev(u)	((u)->udev)
#define usbhsh_urb_to_ureq(u)	((u)->hcpriv)
#define usbhsh_urb_to_usbv(u)	((u)->dev)

#define usbhsh_usbv_to_udev(d)	dev_get_drvdata(&(d)->dev)

#define usbhsh_udev_to_usbv(h)	((h)->usbv)

#define usbhsh_pipe_info(p)	((p)->mod_private)

#define usbhsh_device_number(h, d)	((int)((d) - (h)->udev))
#define usbhsh_device_nth(h, d)		((h)->udev + d)
#define usbhsh_device0(h)		usbhsh_device_nth(h, 0)

#define usbhsh_port_stat_init(h)	((h)->port_stat = 0)
#define usbhsh_port_stat_set(h, s)	((h)->port_stat |= (s))
#define usbhsh_port_stat_clear(h, s)	((h)->port_stat &= ~(s))
#define usbhsh_port_stat_get(h)		((h)->port_stat)

#define usbhsh_pkt_to_req(p)	\
	container_of((void *)p, struct usbhsh_request, pkt)

/*
 *		req alloc/free
 */
static void usbhsh_req_list_init(struct usbhsh_hpriv *hpriv)
{
	INIT_LIST_HEAD(&hpriv->ureq_link_active);
	INIT_LIST_HEAD(&hpriv->ureq_link_free);
}

static void usbhsh_req_list_quit(struct usbhsh_hpriv *hpriv)
{
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	struct usbhsh_request *ureq, *next;

	/* kfree all active ureq */
	list_for_each_entry_safe(ureq, next,
				 &hpriv->ureq_link_active,
				 ureq_link) {
		dev_err(dev, "active ureq (%p) is force freed\n", ureq);
		kfree(ureq);
	}

	/* kfree all free ureq */
	list_for_each_entry_safe(ureq, next, &hpriv->ureq_link_free, ureq_link)
		kfree(ureq);
}

static struct usbhsh_request *usbhsh_req_alloc(struct usbhsh_hpriv *hpriv,
					       struct urb *urb,
					       gfp_t mem_flags)
{
	struct usbhsh_request *ureq;
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);

	if (list_empty(&hpriv->ureq_link_free)) {
		/*
		 * create new one if there is no free ureq
		 */
		ureq = kzalloc(sizeof(struct usbhsh_request), mem_flags);
		if (ureq)
			INIT_LIST_HEAD(&ureq->ureq_link);
	} else {
		/*
		 * reuse "free" ureq if exist
		 */
		ureq = list_entry(hpriv->ureq_link_free.next,
				  struct usbhsh_request,
				  ureq_link);
		if (ureq)
			list_del_init(&ureq->ureq_link);
	}

	if (!ureq) {
		dev_err(dev, "ureq alloc fail\n");
		return NULL;
	}

	usbhs_pkt_init(&ureq->pkt);

	/*
	 * push it to "active" list
	 */
	list_add_tail(&ureq->ureq_link, &hpriv->ureq_link_active);
	ureq->urb = urb;

	return ureq;
}

static void usbhsh_req_free(struct usbhsh_hpriv *hpriv,
			    struct usbhsh_request *ureq)
{
	struct usbhs_pkt *pkt = &ureq->pkt;

	usbhs_pkt_init(pkt);

	/*
	 * removed from "active" list,
	 * and push it to "free" list
	 */
	ureq->urb = NULL;
	list_del_init(&ureq->ureq_link);
	list_add_tail(&ureq->ureq_link, &hpriv->ureq_link_free);
}

/*
 *		device control
 */

static int usbhsh_device_has_endpoint(struct usbhsh_device *udev)
{
	return !list_empty(&udev->ep_list_head);
}

static struct usbhsh_device *usbhsh_device_alloc(struct usbhsh_hpriv *hpriv,
						 struct urb *urb)
{
	struct usbhsh_device *udev = NULL;
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	struct usb_device *usbv = usbhsh_urb_to_usbv(urb);
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	int i;

	/*
	 * device 0
	 */
	if (0 == usb_pipedevice(urb->pipe)) {
		udev = usbhsh_device0(hpriv);
		goto usbhsh_device_find;
	}

	/*
	 * find unused device
	 */
	usbhsh_for_each_udev(udev, hpriv, i) {
		if (usbhsh_udev_to_usbv(udev))
			continue;
		goto usbhsh_device_find;
	}

	dev_err(dev, "no free usbhsh_device\n");

	return NULL;

usbhsh_device_find:
	if (usbhsh_device_has_endpoint(udev))
		dev_warn(dev, "udev have old endpoint\n");

	/* uep will be attached */
	INIT_LIST_HEAD(&udev->ep_list_head);

	/*
	 * usbhsh_usbv_to_udev()
	 * usbhsh_udev_to_usbv()
	 * will be enable
	 */
	dev_set_drvdata(&usbv->dev, udev);
	udev->usbv = usbv;

	/* set device config */
	usbhs_set_device_speed(priv,
			       usbhsh_device_number(hpriv, udev),
			       usbhsh_device_number(hpriv, udev),
			       0, /* FIXME no parent */
			       usbv->speed);

	dev_dbg(dev, "%s [%d](%p)\n", __func__,
		usbhsh_device_number(hpriv, udev), udev);

	return udev;
}

static void usbhsh_device_free(struct usbhsh_hpriv *hpriv,
			       struct usbhsh_device *udev)
{
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	struct usb_device *usbv = usbhsh_udev_to_usbv(udev);

	dev_dbg(dev, "%s [%d](%p)\n", __func__,
		usbhsh_device_number(hpriv, udev), udev);

	if (usbhsh_device_has_endpoint(udev))
		dev_warn(dev, "udev still have endpoint\n");

	/*
	 * usbhsh_usbv_to_udev()
	 * usbhsh_udev_to_usbv()
	 * will be disable
	 */
	dev_set_drvdata(&usbv->dev, NULL);
	udev->usbv = NULL;
}

/*
 *		end-point control
 */
struct usbhsh_ep *usbhsh_endpoint_alloc(struct usbhsh_hpriv *hpriv,
					struct usbhsh_device *udev,
					struct usb_host_endpoint *ep,
					int dir_in_req,
					gfp_t mem_flags)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct usbhsh_ep *uep;
	struct usbhsh_pipe_info *info;
	struct usbhs_pipe *pipe, *best_pipe;
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	struct usb_endpoint_descriptor *desc = &ep->desc;
	int type, i, dir_in;
	unsigned int min_usr;

	dir_in_req = !!dir_in_req;

	uep = kzalloc(sizeof(struct usbhsh_ep), mem_flags);
	if (!uep) {
		dev_err(dev, "usbhsh_ep alloc fail\n");
		return NULL;
	}

	if (usb_endpoint_xfer_control(desc)) {
		best_pipe = usbhsh_hpriv_to_dcp(hpriv);
		goto usbhsh_endpoint_alloc_find_pipe;
	}

	/*
	 * find best pipe for endpoint
	 * see
	 *	HARDWARE LIMITATION
	 */
	type = usb_endpoint_type(desc);
	min_usr = ~0;
	best_pipe = NULL;
	usbhs_for_each_pipe(pipe, priv, i) {
		if (!usbhs_pipe_type_is(pipe, type))
			continue;

		dir_in = !!usbhs_pipe_is_dir_in(pipe);
		if (0 != (dir_in - dir_in_req))
			continue;

		info = usbhsh_pipe_info(pipe);

		if (min_usr > info->usr_cnt) {
			min_usr		= info->usr_cnt;
			best_pipe	= pipe;
		}
	}

	if (unlikely(!best_pipe)) {
		dev_err(dev, "couldn't find best pipe\n");
		kfree(uep);
		return NULL;
	}
usbhsh_endpoint_alloc_find_pipe:
	/*
	 * init uep
	 */
	uep->pipe	= best_pipe;
	uep->maxp	= usb_endpoint_maxp(desc);
	usbhsh_uep_to_udev(uep)	= udev;
	usbhsh_ep_to_uep(ep)	= uep;

	/*
	 * update pipe user count
	 */
	info = usbhsh_pipe_info(best_pipe);
	info->usr_cnt++;

	/* init this endpoint, and attach it to udev */
	INIT_LIST_HEAD(&uep->ep_list);
	list_add_tail(&uep->ep_list, &udev->ep_list_head);

	/*
	 * usbhs_pipe_config_update() should be called after
	 * usbhs_device_config()
	 * see
	 *  DCPMAXP/PIPEMAXP
	 */
	usbhs_pipe_sequence_data0(uep->pipe);
	usbhs_pipe_config_update(uep->pipe,
				 usbhsh_device_number(hpriv, udev),
				 usb_endpoint_num(desc),
				 uep->maxp);

	dev_dbg(dev, "%s [%d-%s](%p)\n", __func__,
		usbhsh_device_number(hpriv, udev),
		usbhs_pipe_name(uep->pipe), uep);

	return uep;
}

void usbhsh_endpoint_free(struct usbhsh_hpriv *hpriv,
			  struct usb_host_endpoint *ep)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	struct usbhsh_ep *uep = usbhsh_ep_to_uep(ep);
	struct usbhsh_pipe_info *info;

	if (!uep)
		return;

	dev_dbg(dev, "%s [%d-%s](%p)\n", __func__,
		usbhsh_device_number(hpriv, usbhsh_uep_to_udev(uep)),
		usbhs_pipe_name(uep->pipe), uep);

	info = usbhsh_pipe_info(uep->pipe);
	info->usr_cnt--;

	/* remove this endpoint from udev */
	list_del_init(&uep->ep_list);

	usbhsh_uep_to_udev(uep) = NULL;
	usbhsh_ep_to_uep(ep) = NULL;

	kfree(uep);
}

/*
 *		queue push/pop
 */
static void usbhsh_queue_done(struct usbhs_priv *priv, struct usbhs_pkt *pkt)
{
	struct usbhsh_request *ureq = usbhsh_pkt_to_req(pkt);
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct urb *urb = ureq->urb;
	struct device *dev = usbhs_priv_to_dev(priv);

	dev_dbg(dev, "%s\n", __func__);

	if (!urb) {
		dev_warn(dev, "pkt doesn't have urb\n");
		return;
	}

	urb->actual_length = pkt->actual;
	usbhsh_req_free(hpriv, ureq);
	usbhsh_urb_to_ureq(urb) = NULL;

	usb_hcd_unlink_urb_from_ep(hcd, urb);
	usb_hcd_giveback_urb(hcd, urb, 0);
}

static int usbhsh_queue_push(struct usb_hcd *hcd,
			     struct usbhs_pipe *pipe,
			     struct urb *urb)
{
	struct usbhsh_request *ureq = usbhsh_urb_to_ureq(urb);
	struct usbhs_pkt *pkt = &ureq->pkt;
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	void *buf;
	int len;

	if (usb_pipeisoc(urb->pipe)) {
		dev_err(dev, "pipe iso is not supported now\n");
		return -EIO;
	}

	if (usb_pipein(urb->pipe))
		pipe->handler = &usbhs_fifo_pio_pop_handler;
	else
		pipe->handler = &usbhs_fifo_pio_push_handler;

	buf = (void *)(urb->transfer_buffer + urb->actual_length);
	len = urb->transfer_buffer_length - urb->actual_length;

	dev_dbg(dev, "%s\n", __func__);
	usbhs_pkt_push(pipe, pkt, usbhsh_queue_done,
		       buf, len, (urb->transfer_flags & URB_ZERO_PACKET));
	usbhs_pkt_start(pipe);

	return 0;
}

/*
 *		DCP setup stage
 */
static int usbhsh_is_request_address(struct urb *urb)
{
	struct usb_ctrlrequest *cmd;

	cmd = (struct usb_ctrlrequest *)urb->setup_packet;

	if ((DeviceOutRequest    == cmd->bRequestType << 8) &&
	    (USB_REQ_SET_ADDRESS == cmd->bRequest))
		return 1;
	else
		return 0;
}

static void usbhsh_setup_stage_packet_push(struct usbhsh_hpriv *hpriv,
					   struct urb *urb,
					   struct usbhs_pipe *pipe)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct usb_ctrlrequest req;
	struct device *dev = usbhs_priv_to_dev(priv);

	/*
	 * wait setup packet ACK
	 * see
	 *	usbhsh_irq_setup_ack()
	 *	usbhsh_irq_setup_err()
	 */
	init_completion(&hpriv->setup_ack_done);

	/* copy original request */
	memcpy(&req, urb->setup_packet, sizeof(struct usb_ctrlrequest));

	/*
	 * renesas_usbhs can not use original usb address.
	 * see HARDWARE LIMITATION.
	 * modify usb address here.
	 */
	if (usbhsh_is_request_address(urb)) {
		/* FIXME */
		req.wValue = 1;
		dev_dbg(dev, "create new address - %d\n", req.wValue);
	}

	/* set request */
	usbhs_usbreq_set_val(priv, &req);

	/*
	 * wait setup packet ACK
	 */
	wait_for_completion(&hpriv->setup_ack_done);

	dev_dbg(dev, "%s done\n", __func__);
}

/*
 *		DCP data stage
 */
static void usbhsh_data_stage_packet_done(struct usbhs_priv *priv,
					  struct usbhs_pkt *pkt)
{
	struct usbhsh_request *ureq = usbhsh_pkt_to_req(pkt);
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct urb *urb = ureq->urb;

	/* this ureq was connected to urb when usbhsh_urb_enqueue()  */

	usbhsh_req_free(hpriv, ureq);
	usbhsh_urb_to_ureq(urb) = NULL;
}

static void usbhsh_data_stage_packet_push(struct usbhsh_hpriv *hpriv,
					  struct urb *urb,
					  struct usbhs_pipe *pipe)
{
	struct usbhsh_request *ureq;
	struct usbhs_pkt *pkt;

	/*
	 * FIXME
	 *
	 * data stage uses ureq which is connected to urb
	 * see usbhsh_urb_enqueue() :: alloc new request.
	 * it will be freed in usbhsh_data_stage_packet_done()
	 */
	ureq	= usbhsh_urb_to_ureq(urb);
	pkt	= &ureq->pkt;

	if (usb_pipein(urb->pipe))
		pipe->handler = &usbhs_dcp_data_stage_in_handler;
	else
		pipe->handler = &usbhs_dcp_data_stage_out_handler;

	usbhs_pkt_push(pipe, pkt,
		       usbhsh_data_stage_packet_done,
		       urb->transfer_buffer,
		       urb->transfer_buffer_length,
		       (urb->transfer_flags & URB_ZERO_PACKET));
}

/*
 *		DCP status stage
 */
static void usbhsh_status_stage_packet_push(struct usbhsh_hpriv *hpriv,
					    struct urb *urb,
					    struct usbhs_pipe *pipe)
{
	struct usbhsh_request *ureq;
	struct usbhs_pkt *pkt;

	/*
	 * FIXME
	 *
	 * status stage uses allocated ureq.
	 * it will be freed on usbhsh_queue_done()
	 */
	ureq	= usbhsh_req_alloc(hpriv, urb, GFP_KERNEL);
	pkt	= &ureq->pkt;

	if (usb_pipein(urb->pipe))
		pipe->handler = &usbhs_dcp_status_stage_in_handler;
	else
		pipe->handler = &usbhs_dcp_status_stage_out_handler;

	usbhs_pkt_push(pipe, pkt,
		       usbhsh_queue_done,
		       NULL,
		       urb->transfer_buffer_length,
		       0);
}

static int usbhsh_dcp_queue_push(struct usb_hcd *hcd,
				 struct usbhsh_hpriv *hpriv,
				 struct usbhs_pipe *pipe,
				 struct urb *urb)
{
	struct device *dev = usbhsh_hcd_to_dev(hcd);

	dev_dbg(dev, "%s\n", __func__);

	/*
	 * setup stage
	 *
	 * usbhsh_send_setup_stage_packet() wait SACK/SIGN
	 */
	usbhsh_setup_stage_packet_push(hpriv, urb, pipe);

	/*
	 * data stage
	 *
	 * It is pushed only when urb has buffer.
	 */
	if (urb->transfer_buffer_length)
		usbhsh_data_stage_packet_push(hpriv, urb, pipe);

	/*
	 * status stage
	 */
	usbhsh_status_stage_packet_push(hpriv, urb, pipe);

	/*
	 * start pushed packets
	 */
	usbhs_pkt_start(pipe);

	return 0;
}

/*
 *		dma map functions
 */
static int usbhsh_dma_map_ctrl(struct usbhs_pkt *pkt, int map)
{
	return 0;
}

/*
 *		for hc_driver
 */
static int usbhsh_host_start(struct usb_hcd *hcd)
{
	return 0;
}

static void usbhsh_host_stop(struct usb_hcd *hcd)
{
}

static int usbhsh_urb_enqueue(struct usb_hcd *hcd,
			      struct urb *urb,
			      gfp_t mem_flags)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	struct usb_device *usbv = usbhsh_urb_to_usbv(urb);
	struct usb_host_endpoint *ep = urb->ep;
	struct usbhsh_request *ureq;
	struct usbhsh_device *udev, *new_udev = NULL;
	struct usbhs_pipe *pipe;
	struct usbhsh_ep *uep;
	int is_dir_in = usb_pipein(urb->pipe);

	int ret;

	dev_dbg(dev, "%s (%s)\n", __func__, is_dir_in ? "in" : "out");

	ret = usb_hcd_link_urb_to_ep(hcd, urb);
	if (ret)
		goto usbhsh_urb_enqueue_error_not_linked;

	/*
	 * get udev
	 */
	udev = usbhsh_usbv_to_udev(usbv);
	if (!udev) {
		new_udev = usbhsh_device_alloc(hpriv, urb);
		if (!new_udev)
			goto usbhsh_urb_enqueue_error_not_linked;

		udev = new_udev;
	}

	/*
	 * get uep
	 */
	uep = usbhsh_ep_to_uep(ep);
	if (!uep) {
		uep = usbhsh_endpoint_alloc(hpriv, udev, ep,
					    is_dir_in, mem_flags);
		if (!uep)
			goto usbhsh_urb_enqueue_error_free_device;
	}
	pipe = usbhsh_uep_to_pipe(uep);

	/*
	 * alloc new request
	 */
	ureq = usbhsh_req_alloc(hpriv, urb, mem_flags);
	if (unlikely(!ureq)) {
		ret = -ENOMEM;
		goto usbhsh_urb_enqueue_error_free_endpoint;
	}
	usbhsh_urb_to_ureq(urb) = ureq;

	/*
	 * push packet
	 */
	if (usb_pipecontrol(urb->pipe))
		usbhsh_dcp_queue_push(hcd, hpriv, pipe, urb);
	else
		usbhsh_queue_push(hcd, pipe, urb);

	return 0;

usbhsh_urb_enqueue_error_free_endpoint:
	usbhsh_endpoint_free(hpriv, ep);
usbhsh_urb_enqueue_error_free_device:
	if (new_udev)
		usbhsh_device_free(hpriv, new_udev);
usbhsh_urb_enqueue_error_not_linked:

	dev_dbg(dev, "%s error\n", __func__);

	return ret;
}

static int usbhsh_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	struct usbhsh_request *ureq = usbhsh_urb_to_ureq(urb);

	if (ureq) {
		usbhsh_req_free(hpriv, ureq);
		usbhsh_urb_to_ureq(urb) = NULL;
	}

	return 0;
}

static void usbhsh_endpoint_disable(struct usb_hcd *hcd,
				    struct usb_host_endpoint *ep)
{
	struct usbhsh_ep *uep = usbhsh_ep_to_uep(ep);
	struct usbhsh_device *udev;
	struct usbhsh_hpriv *hpriv;

	/*
	 * this function might be called manytimes by same hcd/ep
	 * in-endpoitn == out-endpoint if ep == dcp.
	 */
	if (!uep)
		return;

	udev	= usbhsh_uep_to_udev(uep);
	hpriv	= usbhsh_hcd_to_hpriv(hcd);

	usbhsh_endpoint_free(hpriv, ep);
	ep->hcpriv = NULL;

	/*
	 * if there is no endpoint,
	 * free device
	 */
	if (!usbhsh_device_has_endpoint(udev))
		usbhsh_device_free(hpriv, udev);
}

static int usbhsh_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	int roothub_id = 1; /* only 1 root hub */

	/*
	 * does port stat was changed ?
	 * check USB_PORT_STAT_C_xxx << 16
	 */
	if (usbhsh_port_stat_get(hpriv) & 0xFFFF0000)
		*buf = (1 << roothub_id);
	else
		*buf = 0;

	dev_dbg(dev, "%s (%02x)\n", __func__, *buf);

	return !!(*buf);
}

static int __usbhsh_hub_hub_feature(struct usbhsh_hpriv *hpriv,
				    u16 typeReq, u16 wValue,
				    u16 wIndex, char *buf, u16 wLength)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);

	switch (wValue) {
	case C_HUB_OVER_CURRENT:
	case C_HUB_LOCAL_POWER:
		dev_dbg(dev, "%s :: C_HUB_xx\n", __func__);
		return 0;
	}

	return -EPIPE;
}

static int __usbhsh_hub_port_feature(struct usbhsh_hpriv *hpriv,
				     u16 typeReq, u16 wValue,
				     u16 wIndex, char *buf, u16 wLength)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	int enable = (typeReq == SetPortFeature);
	int speed, i, timeout = 128;
	int roothub_id = 1; /* only 1 root hub */

	/* common error */
	if (wIndex > roothub_id || wLength != 0)
		return -EPIPE;

	/* check wValue */
	switch (wValue) {
	case USB_PORT_FEAT_POWER:
		usbhs_vbus_ctrl(priv, enable);
		dev_dbg(dev, "%s :: USB_PORT_FEAT_POWER\n", __func__);
		break;

	case USB_PORT_FEAT_ENABLE:
	case USB_PORT_FEAT_SUSPEND:
	case USB_PORT_FEAT_C_ENABLE:
	case USB_PORT_FEAT_C_SUSPEND:
	case USB_PORT_FEAT_C_CONNECTION:
	case USB_PORT_FEAT_C_OVER_CURRENT:
	case USB_PORT_FEAT_C_RESET:
		dev_dbg(dev, "%s :: USB_PORT_FEAT_xxx\n", __func__);
		break;

	case USB_PORT_FEAT_RESET:
		if (!enable)
			break;

		usbhsh_port_stat_clear(hpriv,
				       USB_PORT_STAT_HIGH_SPEED |
				       USB_PORT_STAT_LOW_SPEED);

		usbhs_bus_send_reset(priv);
		msleep(20);
		usbhs_bus_send_sof_enable(priv);

		for (i = 0; i < timeout ; i++) {
			switch (usbhs_bus_get_speed(priv)) {
			case USB_SPEED_LOW:
				speed = USB_PORT_STAT_LOW_SPEED;
				goto got_usb_bus_speed;
			case USB_SPEED_HIGH:
				speed = USB_PORT_STAT_HIGH_SPEED;
				goto got_usb_bus_speed;
			case USB_SPEED_FULL:
				speed = 0;
				goto got_usb_bus_speed;
			}

			msleep(20);
		}
		return -EPIPE;

got_usb_bus_speed:
		usbhsh_port_stat_set(hpriv, speed);
		usbhsh_port_stat_set(hpriv, USB_PORT_STAT_ENABLE);

		dev_dbg(dev, "%s :: USB_PORT_FEAT_RESET (speed = %d)\n",
			__func__, speed);

		/* status change is not needed */
		return 0;

	default:
		return -EPIPE;
	}

	/* set/clear status */
	if (enable)
		usbhsh_port_stat_set(hpriv, (1 << wValue));
	else
		usbhsh_port_stat_clear(hpriv, (1 << wValue));

	return 0;
}

static int __usbhsh_hub_get_status(struct usbhsh_hpriv *hpriv,
				   u16 typeReq, u16 wValue,
				   u16 wIndex, char *buf, u16 wLength)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct usb_hub_descriptor *desc = (struct usb_hub_descriptor *)buf;
	struct device *dev = usbhs_priv_to_dev(priv);
	int roothub_id = 1; /* only 1 root hub */

	switch (typeReq) {
	case GetHubStatus:
		dev_dbg(dev, "%s :: GetHubStatus\n", __func__);

		*buf = 0x00;
		break;

	case GetPortStatus:
		if (wIndex != roothub_id)
			return -EPIPE;

		dev_dbg(dev, "%s :: GetPortStatus\n", __func__);
		*(__le32 *)buf = cpu_to_le32(usbhsh_port_stat_get(hpriv));
		break;

	case GetHubDescriptor:
		desc->bDescriptorType		= 0x29;
		desc->bHubContrCurrent		= 0;
		desc->bNbrPorts			= roothub_id;
		desc->bDescLength		= 9;
		desc->bPwrOn2PwrGood		= 0;
		desc->wHubCharacteristics	= cpu_to_le16(0x0011);
		desc->u.hs.DeviceRemovable[0]	= (roothub_id << 1);
		desc->u.hs.DeviceRemovable[1]	= ~0;
		dev_dbg(dev, "%s :: GetHubDescriptor\n", __func__);
		break;
	}

	return 0;
}

static int usbhsh_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			      u16 wIndex, char *buf, u16 wLength)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	int ret = -EPIPE;

	switch (typeReq) {

	/* Hub Feature */
	case ClearHubFeature:
	case SetHubFeature:
		ret = __usbhsh_hub_hub_feature(hpriv, typeReq,
					       wValue, wIndex, buf, wLength);
		break;

	/* Port Feature */
	case SetPortFeature:
	case ClearPortFeature:
		ret = __usbhsh_hub_port_feature(hpriv, typeReq,
						wValue, wIndex, buf, wLength);
		break;

	/* Get status */
	case GetHubStatus:
	case GetPortStatus:
	case GetHubDescriptor:
		ret = __usbhsh_hub_get_status(hpriv, typeReq,
					      wValue, wIndex, buf, wLength);
		break;
	}

	dev_dbg(dev, "typeReq = %x, ret = %d, port_stat = %x\n",
		typeReq, ret, usbhsh_port_stat_get(hpriv));

	return ret;
}

static struct hc_driver usbhsh_driver = {
	.description =		usbhsh_hcd_name,
	.hcd_priv_size =	sizeof(struct usbhsh_hpriv),

	/*
	 * generic hardware linkage
	 */
	.flags =		HCD_USB2,

	.start =		usbhsh_host_start,
	.stop =			usbhsh_host_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		usbhsh_urb_enqueue,
	.urb_dequeue =		usbhsh_urb_dequeue,
	.endpoint_disable =	usbhsh_endpoint_disable,

	/*
	 * root hub
	 */
	.hub_status_data =	usbhsh_hub_status_data,
	.hub_control =		usbhsh_hub_control,
};

/*
 *		interrupt functions
 */
static int usbhsh_irq_attch(struct usbhs_priv *priv,
			    struct usbhs_irq_state *irq_state)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct device *dev = usbhs_priv_to_dev(priv);

	dev_dbg(dev, "device attached\n");

	usbhsh_port_stat_set(hpriv, USB_PORT_STAT_CONNECTION);
	usbhsh_port_stat_set(hpriv, USB_PORT_STAT_C_CONNECTION << 16);

	return 0;
}

static int usbhsh_irq_dtch(struct usbhs_priv *priv,
			   struct usbhs_irq_state *irq_state)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct device *dev = usbhs_priv_to_dev(priv);

	dev_dbg(dev, "device detached\n");

	usbhsh_port_stat_clear(hpriv, USB_PORT_STAT_CONNECTION);
	usbhsh_port_stat_set(hpriv, USB_PORT_STAT_C_CONNECTION << 16);

	return 0;
}

static int usbhsh_irq_setup_ack(struct usbhs_priv *priv,
				struct usbhs_irq_state *irq_state)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct device *dev = usbhs_priv_to_dev(priv);

	dev_dbg(dev, "setup packet OK\n");

	complete(&hpriv->setup_ack_done); /* see usbhsh_urb_enqueue() */

	return 0;
}

static int usbhsh_irq_setup_err(struct usbhs_priv *priv,
				struct usbhs_irq_state *irq_state)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct device *dev = usbhs_priv_to_dev(priv);

	dev_dbg(dev, "setup packet Err\n");

	complete(&hpriv->setup_ack_done); /* see usbhsh_urb_enqueue() */

	return 0;
}

/*
 *		module start/stop
 */
static void usbhsh_pipe_init_for_host(struct usbhs_priv *priv)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct usbhsh_pipe_info *pipe_info = hpriv->pipe_info;
	struct usbhs_pipe *pipe;
	u32 *pipe_type = usbhs_get_dparam(priv, pipe_type);
	int pipe_size = usbhs_get_dparam(priv, pipe_size);
	int old_type, dir_in, i;

	/* init all pipe */
	old_type = USB_ENDPOINT_XFER_CONTROL;
	for (i = 0; i < pipe_size; i++) {
		pipe_info[i].usr_cnt	= 0;

		/*
		 * data "output" will be finished as soon as possible,
		 * but there is no guaranty at data "input" case.
		 *
		 * "input" needs "standby" pipe.
		 * So, "input" direction pipe > "output" direction pipe
		 * is good idea.
		 *
		 * 1st USB_ENDPOINT_XFER_xxx will be output direction,
		 * and the other will be input direction here.
		 *
		 * ex)
		 * ...
		 * USB_ENDPOINT_XFER_ISOC -> dir out
		 * USB_ENDPOINT_XFER_ISOC -> dir in
		 * USB_ENDPOINT_XFER_BULK -> dir out
		 * USB_ENDPOINT_XFER_BULK -> dir in
		 * USB_ENDPOINT_XFER_BULK -> dir in
		 * ...
		 */
		dir_in = (pipe_type[i] == old_type);
		old_type = pipe_type[i];

		if (USB_ENDPOINT_XFER_CONTROL == pipe_type[i]) {
			pipe = usbhs_dcp_malloc(priv);
			usbhsh_hpriv_to_dcp(hpriv) = pipe;
		} else {
			pipe = usbhs_pipe_malloc(priv,
						 pipe_type[i],
						 dir_in);
		}

		pipe->mod_private = pipe_info + i;
	}
}

static int usbhsh_start(struct usbhs_priv *priv)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct device *dev = usbhs_priv_to_dev(priv);
	int ret;

	/* add hcd */
	ret = usb_add_hcd(hcd, 0, 0);
	if (ret < 0)
		return 0;

	/*
	 * pipe initialize and enable DCP
	 */
	usbhs_pipe_init(priv,
			usbhsh_dma_map_ctrl);
	usbhs_fifo_init(priv);
	usbhsh_pipe_init_for_host(priv);

	/*
	 * system config enble
	 * - HI speed
	 * - host
	 * - usb module
	 */
	usbhs_sys_hispeed_ctrl(priv, 1);
	usbhs_sys_host_ctrl(priv, 1);
	usbhs_sys_usb_ctrl(priv, 1);

	/*
	 * enable irq callback
	 */
	mod->irq_attch		= usbhsh_irq_attch;
	mod->irq_dtch		= usbhsh_irq_dtch;
	mod->irq_sack		= usbhsh_irq_setup_ack;
	mod->irq_sign		= usbhsh_irq_setup_err;
	usbhs_irq_callback_update(priv, mod);

	dev_dbg(dev, "start host\n");

	return ret;
}

static int usbhsh_stop(struct usbhs_priv *priv)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct device *dev = usbhs_priv_to_dev(priv);

	/*
	 * disable irq callback
	 */
	mod->irq_attch	= NULL;
	mod->irq_dtch	= NULL;
	mod->irq_sack	= NULL;
	mod->irq_sign	= NULL;
	usbhs_irq_callback_update(priv, mod);

	usb_remove_hcd(hcd);

	/* disable sys */
	usbhs_sys_hispeed_ctrl(priv, 0);
	usbhs_sys_host_ctrl(priv, 0);
	usbhs_sys_usb_ctrl(priv, 0);

	dev_dbg(dev, "quit host\n");

	return 0;
}

int usbhs_mod_host_probe(struct usbhs_priv *priv)
{
	struct usbhsh_hpriv *hpriv;
	struct usb_hcd *hcd;
	struct usbhsh_pipe_info *pipe_info;
	struct usbhsh_device *udev;
	struct device *dev = usbhs_priv_to_dev(priv);
	int pipe_size = usbhs_get_dparam(priv, pipe_size);
	int i;

	/* initialize hcd */
	hcd = usb_create_hcd(&usbhsh_driver, dev, usbhsh_hcd_name);
	if (!hcd) {
		dev_err(dev, "Failed to create hcd\n");
		return -ENOMEM;
	}
	hcd->has_tt = 1; /* for low/full speed */

	pipe_info = kzalloc(sizeof(*pipe_info) * pipe_size, GFP_KERNEL);
	if (!pipe_info) {
		dev_err(dev, "Could not allocate pipe_info\n");
		goto usbhs_mod_host_probe_err;
	}

	/*
	 * CAUTION
	 *
	 * There is no guarantee that it is possible to access usb module here.
	 * Don't accesses to it.
	 * The accesse will be enable after "usbhsh_start"
	 */

	hpriv = usbhsh_hcd_to_hpriv(hcd);

	/*
	 * register itself
	 */
	usbhs_mod_register(priv, &hpriv->mod, USBHS_HOST);

	/* init hpriv */
	hpriv->mod.name		= "host";
	hpriv->mod.start	= usbhsh_start;
	hpriv->mod.stop		= usbhsh_stop;
	hpriv->pipe_info	= pipe_info;
	hpriv->pipe_size	= pipe_size;
	usbhsh_req_list_init(hpriv);
	usbhsh_port_stat_init(hpriv);

	/* init all device */
	usbhsh_for_each_udev_with_dev0(udev, hpriv, i) {
		udev->usbv	= NULL;
		INIT_LIST_HEAD(&udev->ep_list_head);
	}

	dev_info(dev, "host probed\n");

	return 0;

usbhs_mod_host_probe_err:
	usb_put_hcd(hcd);

	return -ENOMEM;
}

int usbhs_mod_host_remove(struct usbhs_priv *priv)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);

	usbhsh_req_list_quit(hpriv);

	usb_put_hcd(hcd);

	return 0;
}
