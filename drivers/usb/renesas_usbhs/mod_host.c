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
 * +--------+ |			  |		other device requested
 *	      +- [uep 1 (bulk)]	--|---+		   +--------------+
 *	      |			  +--------------> | pipe0 (dcp)  |
 *	      +- [uep 2 (bulk)]	-@    |		   +--------------+
 *				      |		   | pipe1 (isoc) |
 * +--------+			      |		   +--------------+
 * | udev 2 |-+- [uep 0 (dcp) ]	-@    +----------> | pipe2 (bulk) |
 * +--------+ |					   +--------------+
 *	      +- [uep 1 (int) ]	----+	  +------> | pipe3 (bulk) |
 *				    |	  |	   +--------------+
 * +--------+			    +-----|------> | pipe4 (int)  |
 * | udev 3 |-+- [uep 0 (dcp) ]	-@	  |	   +--------------+
 * +--------+ |				  |	   | ....	  |
 *	      +- [uep 1 (bulk)]	-@	  |	   | ....	  |
 *	      |				  |
 *	      +- [uep 2 (bulk)]-----------+
 *
 * @ :	uep requested free pipe, but all have been used.
 *	now it is waiting for free pipe
 */


/*
 *		struct
 */
struct usbhsh_request {
	struct urb		*urb;
	struct usbhs_pkt	pkt;
};

struct usbhsh_device {
	struct usb_device	*usbv;
	struct list_head	ep_list_head; /* list of usbhsh_ep */
};

struct usbhsh_ep {
	struct usbhs_pipe	*pipe;   /* attached pipe */
	struct usbhsh_device	*udev;   /* attached udev */
	struct usb_host_endpoint *ep;
	struct list_head	ep_list; /* list to usbhsh_device */
	unsigned int		counter; /* pipe attach counter */
};

#define USBHSH_DEVICE_MAX	10 /* see DEVADDn / DCPMAXP / PIPEMAXP */
#define USBHSH_PORT_MAX		 7 /* see DEVADDn :: HUBPORT */
struct usbhsh_hpriv {
	struct usbhs_mod	mod;
	struct usbhs_pipe	*dcp;

	struct usbhsh_device	udev[USBHSH_DEVICE_MAX];

	u32	port_stat;	/* USB_PORT_STAT_xxx */

	struct completion	setup_ack_done;
};


static const char usbhsh_hcd_name[] = "renesas_usbhs host";

/*
 *		macro
 */
#define usbhsh_priv_to_hpriv(priv) \
	container_of(usbhs_mod_get(priv, USBHS_HOST), struct usbhsh_hpriv, mod)

#define __usbhsh_for_each_udev(start, pos, h, i)	\
	for ((i) = start;						\
	     ((i) < USBHSH_DEVICE_MAX) && ((pos) = (h)->udev + (i));	\
	     (i)++)

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
#define usbhsh_uep_to_ep(u)	((u)->ep)

#define usbhsh_urb_to_ureq(u)	((u)->hcpriv)
#define usbhsh_urb_to_usbv(u)	((u)->dev)

#define usbhsh_usbv_to_udev(d)	dev_get_drvdata(&(d)->dev)

#define usbhsh_udev_to_usbv(h)	((h)->usbv)
#define usbhsh_udev_is_used(h)	usbhsh_udev_to_usbv(h)

#define usbhsh_pipe_to_uep(p)	((p)->mod_private)

#define usbhsh_device_parent(d)		(usbhsh_usbv_to_udev((d)->usbv->parent))
#define usbhsh_device_hubport(d)	((d)->usbv->portnum)
#define usbhsh_device_number(h, d)	((int)((d) - (h)->udev))
#define usbhsh_device_nth(h, d)		((h)->udev + d)
#define usbhsh_device0(h)		usbhsh_device_nth(h, 0)

#define usbhsh_port_stat_init(h)	((h)->port_stat = 0)
#define usbhsh_port_stat_set(h, s)	((h)->port_stat |= (s))
#define usbhsh_port_stat_clear(h, s)	((h)->port_stat &= ~(s))
#define usbhsh_port_stat_get(h)		((h)->port_stat)

#define usbhsh_pkt_to_ureq(p)	\
	container_of((void *)p, struct usbhsh_request, pkt)

/*
 *		req alloc/free
 */
static struct usbhsh_request *usbhsh_ureq_alloc(struct usbhsh_hpriv *hpriv,
					       struct urb *urb,
					       gfp_t mem_flags)
{
	struct usbhsh_request *ureq;
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);

	ureq = kzalloc(sizeof(struct usbhsh_request), mem_flags);
	if (!ureq) {
		dev_err(dev, "ureq alloc fail\n");
		return NULL;
	}

	usbhs_pkt_init(&ureq->pkt);
	ureq->urb = urb;
	usbhsh_urb_to_ureq(urb) = ureq;

	return ureq;
}

static void usbhsh_ureq_free(struct usbhsh_hpriv *hpriv,
			    struct usbhsh_request *ureq)
{
	usbhsh_urb_to_ureq(ureq->urb) = NULL;
	ureq->urb = NULL;

	kfree(ureq);
}

/*
 *		status
 */
static int usbhsh_is_running(struct usbhsh_hpriv *hpriv)
{
	/*
	 * we can decide some device is attached or not
	 * by checking mod.irq_attch
	 * see
	 *	usbhsh_irq_attch()
	 *	usbhsh_irq_dtch()
	 */
	return (hpriv->mod.irq_attch == NULL);
}

/*
 *		pipe control
 */
static void usbhsh_endpoint_sequence_save(struct usbhsh_hpriv *hpriv,
					  struct urb *urb,
					  struct usbhs_pkt *pkt)
{
	int len = urb->actual_length;
	int maxp = usb_endpoint_maxp(&urb->ep->desc);
	int t = 0;

	/* DCP is out of sequence control */
	if (usb_pipecontrol(urb->pipe))
		return;

	/*
	 * renesas_usbhs pipe has a limitation in a number.
	 * So, driver should re-use the limited pipe for each device/endpoint.
	 * DATA0/1 sequence should be saved for it.
	 * see [image of mod_host]
	 *     [HARDWARE LIMITATION]
	 */

	/*
	 * next sequence depends on actual_length
	 *
	 * ex) actual_length = 1147, maxp = 512
	 * data0 : 512
	 * data1 : 512
	 * data0 : 123
	 * data1 is the next sequence
	 */
	t = len / maxp;
	if (len % maxp)
		t++;
	if (pkt->zero)
		t++;
	t %= 2;

	if (t)
		usb_dotoggle(urb->dev,
			     usb_pipeendpoint(urb->pipe),
			     usb_pipeout(urb->pipe));
}

static struct usbhsh_device *usbhsh_device_get(struct usbhsh_hpriv *hpriv,
					       struct urb *urb);

static int usbhsh_pipe_attach(struct usbhsh_hpriv *hpriv,
			      struct urb *urb)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct usbhsh_ep *uep = usbhsh_ep_to_uep(urb->ep);
	struct usbhsh_device *udev = usbhsh_device_get(hpriv, urb);
	struct usbhs_pipe *pipe;
	struct usb_endpoint_descriptor *desc = &urb->ep->desc;
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;
	int dir_in_req = !!usb_pipein(urb->pipe);
	int is_dcp = usb_endpoint_xfer_control(desc);
	int i, dir_in;
	int ret = -EBUSY;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	/*
	 * if uep has been attached to pipe,
	 * reuse it
	 */
	if (usbhsh_uep_to_pipe(uep)) {
		ret = 0;
		goto usbhsh_pipe_attach_done;
	}

	usbhs_for_each_pipe_with_dcp(pipe, priv, i) {

		/* check pipe type */
		if (!usbhs_pipe_type_is(pipe, usb_endpoint_type(desc)))
			continue;

		/* check pipe direction if normal pipe */
		if (!is_dcp) {
			dir_in = !!usbhs_pipe_is_dir_in(pipe);
			if (0 != (dir_in - dir_in_req))
				continue;
		}

		/* check pipe is free */
		if (usbhsh_pipe_to_uep(pipe))
			continue;

		/*
		 * attach pipe to uep
		 *
		 * usbhs_pipe_config_update() should be called after
		 * usbhs_set_device_config()
		 * see
		 *  DCPMAXP/PIPEMAXP
		 */
		usbhsh_uep_to_pipe(uep)		= pipe;
		usbhsh_pipe_to_uep(pipe)	= uep;

		usbhs_pipe_config_update(pipe,
					 usbhsh_device_number(hpriv, udev),
					 usb_endpoint_num(desc),
					 usb_endpoint_maxp(desc));

		dev_dbg(dev, "%s [%d-%d(%s:%s)]\n", __func__,
			usbhsh_device_number(hpriv, udev),
			usb_endpoint_num(desc),
			usbhs_pipe_name(pipe),
			dir_in_req ? "in" : "out");

		ret = 0;
		break;
	}

usbhsh_pipe_attach_done:
	if (0 == ret)
		uep->counter++;

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	return ret;
}

static void usbhsh_pipe_detach(struct usbhsh_hpriv *hpriv,
			       struct usbhsh_ep *uep)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct usbhs_pipe *pipe;
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;

	if (unlikely(!uep)) {
		dev_err(dev, "no uep\n");
		return;
	}

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	pipe = usbhsh_uep_to_pipe(uep);

	if (unlikely(!pipe)) {
		dev_err(dev, "uep doens't have pipe\n");
	} else if (1 == uep->counter--) { /* last user */
		struct usb_host_endpoint *ep = usbhsh_uep_to_ep(uep);
		struct usbhsh_device *udev = usbhsh_uep_to_udev(uep);

		/* detach pipe from uep */
		usbhsh_uep_to_pipe(uep)		= NULL;
		usbhsh_pipe_to_uep(pipe)	= NULL;

		dev_dbg(dev, "%s [%d-%d(%s)]\n", __func__,
			usbhsh_device_number(hpriv, udev),
			usb_endpoint_num(&ep->desc),
			usbhs_pipe_name(pipe));
	}

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/
}

/*
 *		endpoint control
 */
static int usbhsh_endpoint_attach(struct usbhsh_hpriv *hpriv,
				  struct urb *urb,
				  gfp_t mem_flags)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct usbhsh_device *udev = usbhsh_device_get(hpriv, urb);
	struct usb_host_endpoint *ep = urb->ep;
	struct usbhsh_ep *uep;
	struct device *dev = usbhs_priv_to_dev(priv);
	struct usb_endpoint_descriptor *desc = &ep->desc;
	unsigned long flags;

	uep = kzalloc(sizeof(struct usbhsh_ep), mem_flags);
	if (!uep) {
		dev_err(dev, "usbhsh_ep alloc fail\n");
		return -ENOMEM;
	}

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	/*
	 * init endpoint
	 */
	uep->counter = 0;
	INIT_LIST_HEAD(&uep->ep_list);
	list_add_tail(&uep->ep_list, &udev->ep_list_head);

	usbhsh_uep_to_udev(uep)	= udev;
	usbhsh_uep_to_ep(uep)	= ep;
	usbhsh_ep_to_uep(ep)	= uep;

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	dev_dbg(dev, "%s [%d-%d]\n", __func__,
		usbhsh_device_number(hpriv, udev),
		usb_endpoint_num(desc));

	return 0;
}

static void usbhsh_endpoint_detach(struct usbhsh_hpriv *hpriv,
				   struct usb_host_endpoint *ep)
{
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	struct usbhsh_ep *uep = usbhsh_ep_to_uep(ep);
	unsigned long flags;

	if (!uep)
		return;

	dev_dbg(dev, "%s [%d-%d]\n", __func__,
		usbhsh_device_number(hpriv, usbhsh_uep_to_udev(uep)),
		usb_endpoint_num(&ep->desc));

	if (usbhsh_uep_to_pipe(uep))
		usbhsh_pipe_detach(hpriv, uep);

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	/* remove this endpoint from udev */
	list_del_init(&uep->ep_list);

	usbhsh_uep_to_udev(uep)	= NULL;
	usbhsh_uep_to_ep(uep)	= NULL;
	usbhsh_ep_to_uep(ep)	= NULL;

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	kfree(uep);
}

static void usbhsh_endpoint_detach_all(struct usbhsh_hpriv *hpriv,
				       struct usbhsh_device *udev)
{
	struct usbhsh_ep *uep, *next;

	list_for_each_entry_safe(uep, next, &udev->ep_list_head, ep_list)
		usbhsh_endpoint_detach(hpriv, usbhsh_uep_to_ep(uep));
}

/*
 *		device control
 */
static int usbhsh_connected_to_rhdev(struct usb_hcd *hcd,
				     struct usbhsh_device *udev)
{
	struct usb_device *usbv = usbhsh_udev_to_usbv(udev);

	return hcd->self.root_hub == usbv->parent;
}

static int usbhsh_device_has_endpoint(struct usbhsh_device *udev)
{
	return !list_empty(&udev->ep_list_head);
}

static struct usbhsh_device *usbhsh_device_get(struct usbhsh_hpriv *hpriv,
					       struct urb *urb)
{
	struct usb_device *usbv = usbhsh_urb_to_usbv(urb);
	struct usbhsh_device *udev = usbhsh_usbv_to_udev(usbv);

	/* usbhsh_device_attach() is still not called */
	if (!udev)
		return NULL;

	/* if it is device0, return it */
	if (0 == usb_pipedevice(urb->pipe))
		return usbhsh_device0(hpriv);

	/* return attached device */
	return udev;
}

static struct usbhsh_device *usbhsh_device_attach(struct usbhsh_hpriv *hpriv,
						 struct urb *urb)
{
	struct usbhsh_device *udev = NULL;
	struct usbhsh_device *udev0 = usbhsh_device0(hpriv);
	struct usbhsh_device *pos;
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	struct usb_device *usbv = usbhsh_urb_to_usbv(urb);
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	unsigned long flags;
	u16 upphub, hubport;
	int i;

	/*
	 * This function should be called only while urb is pointing to device0.
	 * It will attach unused usbhsh_device to urb (usbv),
	 * and initialize device0.
	 * You can use usbhsh_device_get() to get "current" udev,
	 * and usbhsh_usbv_to_udev() is for "attached" udev.
	 */
	if (0 != usb_pipedevice(urb->pipe)) {
		dev_err(dev, "%s fail: urb isn't pointing device0\n", __func__);
		return NULL;
	}

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	/*
	 * find unused device
	 */
	usbhsh_for_each_udev(pos, hpriv, i) {
		if (usbhsh_udev_is_used(pos))
			continue;
		udev = pos;
		break;
	}

	if (udev) {
		/*
		 * usbhsh_usbv_to_udev()
		 * usbhsh_udev_to_usbv()
		 * will be enable
		 */
		dev_set_drvdata(&usbv->dev, udev);
		udev->usbv = usbv;
	}

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	if (!udev) {
		dev_err(dev, "no free usbhsh_device\n");
		return NULL;
	}

	if (usbhsh_device_has_endpoint(udev)) {
		dev_warn(dev, "udev have old endpoint\n");
		usbhsh_endpoint_detach_all(hpriv, udev);
	}

	if (usbhsh_device_has_endpoint(udev0)) {
		dev_warn(dev, "udev0 have old endpoint\n");
		usbhsh_endpoint_detach_all(hpriv, udev0);
	}

	/* uep will be attached */
	INIT_LIST_HEAD(&udev0->ep_list_head);
	INIT_LIST_HEAD(&udev->ep_list_head);

	/*
	 * set device0 config
	 */
	usbhs_set_device_config(priv,
				0, 0, 0, usbv->speed);

	/*
	 * set new device config
	 */
	upphub	= 0;
	hubport	= 0;
	if (!usbhsh_connected_to_rhdev(hcd, udev)) {
		/* if udev is not connected to rhdev, it means parent is Hub */
		struct usbhsh_device *parent = usbhsh_device_parent(udev);

		upphub	= usbhsh_device_number(hpriv, parent);
		hubport	= usbhsh_device_hubport(udev);

		dev_dbg(dev, "%s connecte to Hub [%d:%d](%p)\n", __func__,
			upphub, hubport, parent);
	}

	usbhs_set_device_config(priv,
			       usbhsh_device_number(hpriv, udev),
			       upphub, hubport, usbv->speed);

	dev_dbg(dev, "%s [%d](%p)\n", __func__,
		usbhsh_device_number(hpriv, udev), udev);

	return udev;
}

static void usbhsh_device_detach(struct usbhsh_hpriv *hpriv,
			       struct usbhsh_device *udev)
{
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	struct usb_device *usbv = usbhsh_udev_to_usbv(udev);
	unsigned long flags;

	dev_dbg(dev, "%s [%d](%p)\n", __func__,
		usbhsh_device_number(hpriv, udev), udev);

	if (usbhsh_device_has_endpoint(udev)) {
		dev_warn(dev, "udev still have endpoint\n");
		usbhsh_endpoint_detach_all(hpriv, udev);
	}

	/*
	 * There is nothing to do if it is device0.
	 * see
	 *  usbhsh_device_attach()
	 *  usbhsh_device_get()
	 */
	if (0 == usbhsh_device_number(hpriv, udev))
		return;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	/*
	 * usbhsh_usbv_to_udev()
	 * usbhsh_udev_to_usbv()
	 * will be disable
	 */
	dev_set_drvdata(&usbv->dev, NULL);
	udev->usbv = NULL;

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/
}

/*
 *		queue push/pop
 */
static void usbhsh_queue_done(struct usbhs_priv *priv, struct usbhs_pkt *pkt)
{
	struct usbhsh_request *ureq = usbhsh_pkt_to_ureq(pkt);
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);
	struct urb *urb = ureq->urb;
	struct device *dev = usbhs_priv_to_dev(priv);
	int status = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!urb) {
		dev_warn(dev, "pkt doesn't have urb\n");
		return;
	}

	if (!usbhsh_is_running(hpriv))
		status = -ESHUTDOWN;

	urb->actual_length = pkt->actual;

	usbhsh_endpoint_sequence_save(hpriv, urb, pkt);
	usbhsh_ureq_free(hpriv, ureq);

	usbhsh_pipe_detach(hpriv, usbhsh_ep_to_uep(urb->ep));

	usb_hcd_unlink_urb_from_ep(hcd, urb);
	usb_hcd_giveback_urb(hcd, urb, status);
}

static int usbhsh_queue_push(struct usb_hcd *hcd,
			     struct urb *urb,
			     gfp_t mem_flags)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	struct usbhsh_ep *uep = usbhsh_ep_to_uep(urb->ep);
	struct usbhs_pipe *pipe = usbhsh_uep_to_pipe(uep);
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	struct usbhsh_request *ureq;
	void *buf;
	int len, sequence;

	if (usb_pipeisoc(urb->pipe)) {
		dev_err(dev, "pipe iso is not supported now\n");
		return -EIO;
	}

	/* this ureq will be freed on usbhsh_queue_done() */
	ureq = usbhsh_ureq_alloc(hpriv, urb, mem_flags);
	if (unlikely(!ureq)) {
		dev_err(dev, "ureq alloc fail\n");
		return -ENOMEM;
	}

	if (usb_pipein(urb->pipe))
		pipe->handler = &usbhs_fifo_dma_pop_handler;
	else
		pipe->handler = &usbhs_fifo_dma_push_handler;

	buf = (void *)(urb->transfer_buffer + urb->actual_length);
	len = urb->transfer_buffer_length - urb->actual_length;

	sequence = usb_gettoggle(urb->dev,
				 usb_pipeendpoint(urb->pipe),
				 usb_pipeout(urb->pipe));

	dev_dbg(dev, "%s\n", __func__);
	usbhs_pkt_push(pipe, &ureq->pkt, usbhsh_queue_done,
		       buf, len, (urb->transfer_flags & URB_ZERO_PACKET),
		       sequence);

	usbhs_pkt_start(pipe);

	return 0;
}

static void usbhsh_queue_force_pop(struct usbhs_priv *priv,
				   struct usbhs_pipe *pipe)
{
	struct usbhs_pkt *pkt;

	while (1) {
		pkt = usbhs_pkt_pop(pipe, NULL);
		if (!pkt)
			break;

		/*
		 * if all packet are gone, usbhsh_endpoint_disable()
		 * will be called.
		 * then, attached device/endpoint/pipe will be detached
		 */
		usbhsh_queue_done(priv, pkt);
	}
}

static void usbhsh_queue_force_pop_all(struct usbhs_priv *priv)
{
	struct usbhs_pipe *pos;
	int i;

	usbhs_for_each_pipe_with_dcp(pos, priv, i)
		usbhsh_queue_force_pop(priv, pos);
}

/*
 *		DCP setup stage
 */
static int usbhsh_is_request_address(struct urb *urb)
{
	struct usb_ctrlrequest *req;

	req = (struct usb_ctrlrequest *)urb->setup_packet;

	if ((DeviceOutRequest    == req->bRequestType << 8) &&
	    (USB_REQ_SET_ADDRESS == req->bRequest))
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
	 * modify usb address here to use attached device.
	 * see usbhsh_device_attach()
	 */
	if (usbhsh_is_request_address(urb)) {
		struct usb_device *usbv = usbhsh_urb_to_usbv(urb);
		struct usbhsh_device *udev = usbhsh_usbv_to_udev(usbv);

		/* udev is a attached device */
		req.wValue = usbhsh_device_number(hpriv, udev);
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
	struct usbhsh_request *ureq = usbhsh_pkt_to_ureq(pkt);
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);

	/* this ureq was connected to urb when usbhsh_urb_enqueue()  */

	usbhsh_ureq_free(hpriv, ureq);
}

static int usbhsh_data_stage_packet_push(struct usbhsh_hpriv *hpriv,
					 struct urb *urb,
					 struct usbhs_pipe *pipe,
					 gfp_t mem_flags)

{
	struct usbhsh_request *ureq;

	/* this ureq will be freed on usbhsh_data_stage_packet_done() */
	ureq = usbhsh_ureq_alloc(hpriv, urb, mem_flags);
	if (unlikely(!ureq))
		return -ENOMEM;

	if (usb_pipein(urb->pipe))
		pipe->handler = &usbhs_dcp_data_stage_in_handler;
	else
		pipe->handler = &usbhs_dcp_data_stage_out_handler;

	usbhs_pkt_push(pipe, &ureq->pkt,
		       usbhsh_data_stage_packet_done,
		       urb->transfer_buffer,
		       urb->transfer_buffer_length,
		       (urb->transfer_flags & URB_ZERO_PACKET),
		       -1);

	return 0;
}

/*
 *		DCP status stage
 */
static int usbhsh_status_stage_packet_push(struct usbhsh_hpriv *hpriv,
					    struct urb *urb,
					    struct usbhs_pipe *pipe,
					    gfp_t mem_flags)
{
	struct usbhsh_request *ureq;

	/* This ureq will be freed on usbhsh_queue_done() */
	ureq = usbhsh_ureq_alloc(hpriv, urb, mem_flags);
	if (unlikely(!ureq))
		return -ENOMEM;

	if (usb_pipein(urb->pipe))
		pipe->handler = &usbhs_dcp_status_stage_in_handler;
	else
		pipe->handler = &usbhs_dcp_status_stage_out_handler;

	usbhs_pkt_push(pipe, &ureq->pkt,
		       usbhsh_queue_done,
		       NULL,
		       urb->transfer_buffer_length,
		       0, -1);

	return 0;
}

static int usbhsh_dcp_queue_push(struct usb_hcd *hcd,
				 struct urb *urb,
				 gfp_t mflags)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	struct usbhsh_ep *uep = usbhsh_ep_to_uep(urb->ep);
	struct usbhs_pipe *pipe = usbhsh_uep_to_pipe(uep);
	struct device *dev = usbhsh_hcd_to_dev(hcd);
	int ret;

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
	if (urb->transfer_buffer_length) {
		ret = usbhsh_data_stage_packet_push(hpriv, urb, pipe, mflags);
		if (ret < 0) {
			dev_err(dev, "data stage failed\n");
			return ret;
		}
	}

	/*
	 * status stage
	 */
	ret = usbhsh_status_stage_packet_push(hpriv, urb, pipe, mflags);
	if (ret < 0) {
		dev_err(dev, "status stage failed\n");
		return ret;
	}

	/*
	 * start pushed packets
	 */
	usbhs_pkt_start(pipe);

	return 0;
}

/*
 *		dma map functions
 */
static int usbhsh_dma_map_ctrl(struct device *dma_dev, struct usbhs_pkt *pkt,
			       int map)
{
	if (map) {
		struct usbhsh_request *ureq = usbhsh_pkt_to_ureq(pkt);
		struct urb *urb = ureq->urb;

		/* it can not use scatter/gather */
		if (urb->num_sgs)
			return -EINVAL;

		pkt->dma = urb->transfer_dma;
		if (!pkt->dma)
			return -EINVAL;
	}

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
	struct usb_host_endpoint *ep = urb->ep;
	struct usbhsh_device *new_udev = NULL;
	int is_dir_in = usb_pipein(urb->pipe);
	int ret;

	dev_dbg(dev, "%s (%s)\n", __func__, is_dir_in ? "in" : "out");

	if (!usbhsh_is_running(hpriv)) {
		ret = -EIO;
		dev_err(dev, "host is not running\n");
		goto usbhsh_urb_enqueue_error_not_linked;
	}

	ret = usb_hcd_link_urb_to_ep(hcd, urb);
	if (ret) {
		dev_err(dev, "urb link failed\n");
		goto usbhsh_urb_enqueue_error_not_linked;
	}

	/*
	 * attach udev if needed
	 * see [image of mod_host]
	 */
	if (!usbhsh_device_get(hpriv, urb)) {
		new_udev = usbhsh_device_attach(hpriv, urb);
		if (!new_udev) {
			ret = -EIO;
			dev_err(dev, "device attach failed\n");
			goto usbhsh_urb_enqueue_error_not_linked;
		}
	}

	/*
	 * attach endpoint if needed
	 * see [image of mod_host]
	 */
	if (!usbhsh_ep_to_uep(ep)) {
		ret = usbhsh_endpoint_attach(hpriv, urb, mem_flags);
		if (ret < 0) {
			dev_err(dev, "endpoint attach failed\n");
			goto usbhsh_urb_enqueue_error_free_device;
		}
	}

	/*
	 * attach pipe to endpoint
	 * see [image of mod_host]
	 */
	ret = usbhsh_pipe_attach(hpriv, urb);
	if (ret < 0) {
		dev_err(dev, "pipe attach failed\n");
		goto usbhsh_urb_enqueue_error_free_endpoint;
	}

	/*
	 * push packet
	 */
	if (usb_pipecontrol(urb->pipe))
		ret = usbhsh_dcp_queue_push(hcd, urb, mem_flags);
	else
		ret = usbhsh_queue_push(hcd, urb, mem_flags);

	return ret;

usbhsh_urb_enqueue_error_free_endpoint:
	usbhsh_endpoint_detach(hpriv, ep);
usbhsh_urb_enqueue_error_free_device:
	if (new_udev)
		usbhsh_device_detach(hpriv, new_udev);
usbhsh_urb_enqueue_error_not_linked:

	dev_dbg(dev, "%s error\n", __func__);

	return ret;
}

static int usbhsh_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	struct usbhsh_request *ureq = usbhsh_urb_to_ureq(urb);

	if (ureq) {
		struct usbhs_priv *priv = usbhsh_hpriv_to_priv(hpriv);
		struct usbhs_pkt *pkt = &ureq->pkt;

		usbhs_pkt_pop(pkt->pipe, pkt);
		usbhsh_queue_done(priv, pkt);
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
	 * in-endpoint == out-endpoint if ep == dcp.
	 */
	if (!uep)
		return;

	udev	= usbhsh_uep_to_udev(uep);
	hpriv	= usbhsh_hcd_to_hpriv(hcd);

	usbhsh_endpoint_detach(hpriv, ep);

	/*
	 * if there is no endpoint,
	 * free device
	 */
	if (!usbhsh_device_has_endpoint(udev))
		usbhsh_device_detach(hpriv, udev);
}

static int usbhsh_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct usbhsh_hpriv *hpriv = usbhsh_hcd_to_hpriv(hcd);
	int roothub_id = 1; /* only 1 root hub */

	/*
	 * does port stat was changed ?
	 * check USB_PORT_STAT_C_xxx << 16
	 */
	if (usbhsh_port_stat_get(hpriv) & 0xFFFF0000)
		*buf = (1 << roothub_id);
	else
		*buf = 0;

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

		usbhsh_queue_force_pop_all(priv);

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
		desc->bDescriptorType		= USB_DT_HUB;
		desc->bHubContrCurrent		= 0;
		desc->bNbrPorts			= roothub_id;
		desc->bDescLength		= 9;
		desc->bPwrOn2PwrGood		= 0;
		desc->wHubCharacteristics	=
			cpu_to_le16(HUB_CHAR_INDV_PORT_LPSM | HUB_CHAR_NO_OCPM);
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

static int usbhsh_bus_nop(struct usb_hcd *hcd)
{
	/* nothing to do */
	return 0;
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
	.bus_suspend =		usbhsh_bus_nop,
	.bus_resume =		usbhsh_bus_nop,
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

	/*
	 * attch interrupt might happen infinitely on some device
	 * (on self power USB hub ?)
	 * disable it here.
	 *
	 * usbhsh_is_running() becomes effective
	 * according to this process.
	 * see
	 *	usbhsh_is_running()
	 *	usbhsh_urb_enqueue()
	 */
	hpriv->mod.irq_attch = NULL;
	usbhs_irq_callback_update(priv, &hpriv->mod);

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

	/*
	 * enable attch interrupt again
	 *
	 * usbhsh_is_running() becomes invalid
	 * according to this process.
	 * see
	 *	usbhsh_is_running()
	 *	usbhsh_urb_enqueue()
	 */
	hpriv->mod.irq_attch = usbhsh_irq_attch;
	usbhs_irq_callback_update(priv, &hpriv->mod);

	/*
	 * usbhsh_queue_force_pop_all() should be called
	 * after usbhsh_is_running() becomes invalid.
	 */
	usbhsh_queue_force_pop_all(priv);

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
	struct usbhs_pipe *pipe;
	struct renesas_usbhs_driver_pipe_config *pipe_configs =
					usbhs_get_dparam(priv, pipe_configs);
	int pipe_size = usbhs_get_dparam(priv, pipe_size);
	int old_type, dir_in, i;

	/* init all pipe */
	old_type = USB_ENDPOINT_XFER_CONTROL;
	for (i = 0; i < pipe_size; i++) {

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
		dir_in = (pipe_configs[i].type == old_type);
		old_type = pipe_configs[i].type;

		if (USB_ENDPOINT_XFER_CONTROL == pipe_configs[i].type) {
			pipe = usbhs_dcp_malloc(priv);
			usbhsh_hpriv_to_dcp(hpriv) = pipe;
		} else {
			pipe = usbhs_pipe_malloc(priv,
						 pipe_configs[i].type,
						 dir_in);
		}

		pipe->mod_private = NULL;
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
	device_wakeup_enable(hcd->self.controller);

	/*
	 * pipe initialize and enable DCP
	 */
	usbhs_fifo_init(priv);
	usbhs_pipe_init(priv,
			usbhsh_dma_map_ctrl);
	usbhsh_pipe_init_for_host(priv);

	/*
	 * system config enble
	 * - HI speed
	 * - host
	 * - usb module
	 */
	usbhs_sys_host_ctrl(priv, 1);

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
	usbhs_sys_host_ctrl(priv, 0);

	dev_dbg(dev, "quit host\n");

	return 0;
}

int usbhs_mod_host_probe(struct usbhs_priv *priv)
{
	struct usbhsh_hpriv *hpriv;
	struct usb_hcd *hcd;
	struct usbhsh_device *udev;
	struct device *dev = usbhs_priv_to_dev(priv);
	int i;

	/* initialize hcd */
	hcd = usb_create_hcd(&usbhsh_driver, dev, usbhsh_hcd_name);
	if (!hcd) {
		dev_err(dev, "Failed to create hcd\n");
		return -ENOMEM;
	}
	hcd->has_tt = 1; /* for low/full speed */

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
	usbhsh_port_stat_init(hpriv);

	/* init all device */
	usbhsh_for_each_udev_with_dev0(udev, hpriv, i) {
		udev->usbv	= NULL;
		INIT_LIST_HEAD(&udev->ep_list_head);
	}

	dev_info(dev, "host probed\n");

	return 0;
}

int usbhs_mod_host_remove(struct usbhs_priv *priv)
{
	struct usbhsh_hpriv *hpriv = usbhsh_priv_to_hpriv(priv);
	struct usb_hcd *hcd = usbhsh_hpriv_to_hcd(hpriv);

	usb_put_hcd(hcd);

	return 0;
}
