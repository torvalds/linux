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
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include "common.h"

/*
 *		struct
 */
struct usbhsg_request {
	struct usb_request	req;
	struct usbhs_pkt	pkt;
};

#define EP_NAME_SIZE 8
struct usbhsg_gpriv;
struct usbhsg_uep {
	struct usb_ep		 ep;
	struct usbhs_pipe	*pipe;

	char ep_name[EP_NAME_SIZE];

	struct usbhsg_gpriv *gpriv;
};

struct usbhsg_gpriv {
	struct usb_gadget	 gadget;
	struct usbhs_mod	 mod;
	struct list_head	 link;

	struct usbhsg_uep	*uep;
	int			 uep_size;

	struct usb_gadget_driver	*driver;

	u32	status;
#define USBHSG_STATUS_STARTED		(1 << 0)
#define USBHSG_STATUS_REGISTERD		(1 << 1)
#define USBHSG_STATUS_WEDGE		(1 << 2)
};

struct usbhsg_recip_handle {
	char *name;
	int (*device)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
		      struct usb_ctrlrequest *ctrl);
	int (*interface)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
			 struct usb_ctrlrequest *ctrl);
	int (*endpoint)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
			struct usb_ctrlrequest *ctrl);
};

/*
 *		macro
 */
#define usbhsg_priv_to_gpriv(priv)			\
	container_of(					\
		usbhs_mod_get(priv, USBHS_GADGET),	\
		struct usbhsg_gpriv, mod)

#define __usbhsg_for_each_uep(start, pos, g, i)	\
	for (i = start, pos = (g)->uep + i;	\
	     i < (g)->uep_size;			\
	     i++, pos = (g)->uep + i)

#define usbhsg_for_each_uep(pos, gpriv, i)	\
	__usbhsg_for_each_uep(1, pos, gpriv, i)

#define usbhsg_for_each_uep_with_dcp(pos, gpriv, i)	\
	__usbhsg_for_each_uep(0, pos, gpriv, i)

#define usbhsg_gadget_to_gpriv(g)\
	container_of(g, struct usbhsg_gpriv, gadget)

#define usbhsg_req_to_ureq(r)\
	container_of(r, struct usbhsg_request, req)

#define usbhsg_ep_to_uep(e)		container_of(e, struct usbhsg_uep, ep)
#define usbhsg_gpriv_to_dev(gp)		usbhs_priv_to_dev((gp)->mod.priv)
#define usbhsg_gpriv_to_priv(gp)	((gp)->mod.priv)
#define usbhsg_gpriv_to_dcp(gp)		((gp)->uep)
#define usbhsg_gpriv_to_nth_uep(gp, i)	((gp)->uep + i)
#define usbhsg_uep_to_gpriv(u)		((u)->gpriv)
#define usbhsg_uep_to_pipe(u)		((u)->pipe)
#define usbhsg_pipe_to_uep(p)		((p)->mod_private)
#define usbhsg_is_dcp(u)		((u) == usbhsg_gpriv_to_dcp((u)->gpriv))

#define usbhsg_ureq_to_pkt(u)		(&(u)->pkt)
#define usbhsg_pkt_to_ureq(i)	\
	container_of(i, struct usbhsg_request, pkt)

#define usbhsg_is_not_connected(gp) ((gp)->gadget.speed == USB_SPEED_UNKNOWN)

/* status */
#define usbhsg_status_init(gp)   do {(gp)->status = 0; } while (0)
#define usbhsg_status_set(gp, b) (gp->status |=  b)
#define usbhsg_status_clr(gp, b) (gp->status &= ~b)
#define usbhsg_status_has(gp, b) (gp->status &   b)

/* controller */
LIST_HEAD(the_controller_link);

#define usbhsg_for_each_controller(gpriv)\
	list_for_each_entry(gpriv, &the_controller_link, link)
#define usbhsg_controller_register(gpriv)\
	list_add_tail(&(gpriv)->link, &the_controller_link)
#define usbhsg_controller_unregister(gpriv)\
	list_del_init(&(gpriv)->link)

/*
 *		queue push/pop
 */
static void usbhsg_queue_pop(struct usbhsg_uep *uep,
			     struct usbhsg_request *ureq,
			     int status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);

	dev_dbg(dev, "pipe %d : queue pop\n", usbhs_pipe_number(pipe));

	ureq->req.status = status;
	ureq->req.complete(&uep->ep, &ureq->req);
}

static void usbhsg_queue_done(struct usbhs_priv *priv, struct usbhs_pkt *pkt)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhsg_uep *uep = usbhsg_pipe_to_uep(pipe);
	struct usbhsg_request *ureq = usbhsg_pkt_to_ureq(pkt);

	ureq->req.actual = pkt->actual;

	usbhsg_queue_pop(uep, ureq, 0);
}

static void usbhsg_queue_push(struct usbhsg_uep *uep,
			      struct usbhsg_request *ureq)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhs_pkt *pkt = usbhsg_ureq_to_pkt(ureq);
	struct usb_request *req = &ureq->req;

	req->actual = 0;
	req->status = -EINPROGRESS;
	usbhs_pkt_push(pipe, pkt, usbhsg_queue_done,
		       req->buf, req->length, req->zero);
	usbhs_pkt_start(pipe);

	dev_dbg(dev, "pipe %d : queue push (%d)\n",
		usbhs_pipe_number(pipe),
		req->length);
}

/*
 *		dma map/unmap
 */
static int usbhsg_dma_map(struct device *dev,
			  struct usbhs_pkt *pkt,
			  enum dma_data_direction dir)
{
	struct usbhsg_request *ureq = usbhsg_pkt_to_ureq(pkt);
	struct usb_request *req = &ureq->req;

	if (pkt->dma != DMA_ADDR_INVALID) {
		dev_err(dev, "dma is already mapped\n");
		return -EIO;
	}

	if (req->dma == DMA_ADDR_INVALID) {
		pkt->dma = dma_map_single(dev, pkt->buf, pkt->length, dir);
	} else {
		dma_sync_single_for_device(dev, req->dma, req->length, dir);
		pkt->dma = req->dma;
	}

	if (dma_mapping_error(dev, pkt->dma)) {
		dev_err(dev, "dma mapping error %x\n", pkt->dma);
		return -EIO;
	}

	return 0;
}

static int usbhsg_dma_unmap(struct device *dev,
			    struct usbhs_pkt *pkt,
			    enum dma_data_direction dir)
{
	struct usbhsg_request *ureq = usbhsg_pkt_to_ureq(pkt);
	struct usb_request *req = &ureq->req;

	if (pkt->dma == DMA_ADDR_INVALID) {
		dev_err(dev, "dma is not mapped\n");
		return -EIO;
	}

	if (req->dma == DMA_ADDR_INVALID)
		dma_unmap_single(dev, pkt->dma, pkt->length, dir);
	else
		dma_sync_single_for_cpu(dev, req->dma, req->length, dir);

	pkt->dma = DMA_ADDR_INVALID;

	return 0;
}

static int usbhsg_dma_map_ctrl(struct usbhs_pkt *pkt, int map)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhsg_uep *uep = usbhsg_pipe_to_uep(pipe);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	enum dma_data_direction dir;

	dir = usbhs_pipe_is_dir_in(pipe) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	if (map)
		return usbhsg_dma_map(dev, pkt, dir);
	else
		return usbhsg_dma_unmap(dev, pkt, dir);
}

/*
 *		USB_TYPE_STANDARD / clear feature functions
 */
static int usbhsg_recip_handler_std_control_done(struct usbhs_priv *priv,
						 struct usbhsg_uep *uep,
						 struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(dcp);

	usbhs_dcp_control_transfer_done(pipe);

	return 0;
}

static int usbhsg_recip_handler_std_clear_endpoint(struct usbhs_priv *priv,
						   struct usbhsg_uep *uep,
						   struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);

	if (!usbhsg_status_has(gpriv, USBHSG_STATUS_WEDGE)) {
		usbhs_pipe_disable(pipe);
		usbhs_pipe_sequence_data0(pipe);
		usbhs_pipe_enable(pipe);
	}

	usbhsg_recip_handler_std_control_done(priv, uep, ctrl);

	return 0;
}

struct usbhsg_recip_handle req_clear_feature = {
	.name		= "clear feature",
	.device		= usbhsg_recip_handler_std_control_done,
	.interface	= usbhsg_recip_handler_std_control_done,
	.endpoint	= usbhsg_recip_handler_std_clear_endpoint,
};

/*
 *		USB_TYPE handler
 */
static int usbhsg_recip_run_handle(struct usbhs_priv *priv,
				   struct usbhsg_recip_handle *handler,
				   struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usbhsg_uep *uep;
	struct usbhs_pipe *pipe;
	int recip = ctrl->bRequestType & USB_RECIP_MASK;
	int nth = le16_to_cpu(ctrl->wIndex) & USB_ENDPOINT_NUMBER_MASK;
	int ret;
	int (*func)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
		    struct usb_ctrlrequest *ctrl);
	char *msg;

	uep = usbhsg_gpriv_to_nth_uep(gpriv, nth);
	pipe = usbhsg_uep_to_pipe(uep);
	if (!pipe) {
		dev_err(dev, "wrong recip request\n");
		ret = -EINVAL;
		goto usbhsg_recip_run_handle_end;
	}

	switch (recip) {
	case USB_RECIP_DEVICE:
		msg	= "DEVICE";
		func	= handler->device;
		break;
	case USB_RECIP_INTERFACE:
		msg	= "INTERFACE";
		func	= handler->interface;
		break;
	case USB_RECIP_ENDPOINT:
		msg	= "ENDPOINT";
		func	= handler->endpoint;
		break;
	default:
		dev_warn(dev, "unsupported RECIP(%d)\n", recip);
		func = NULL;
		ret = -EINVAL;
	}

	if (func) {
		unsigned long flags;

		dev_dbg(dev, "%s (pipe %d :%s)\n", handler->name, nth, msg);

		/********************  spin lock ********************/
		usbhs_lock(priv, flags);
		ret = func(priv, uep, ctrl);
		usbhs_unlock(priv, flags);
		/********************  spin unlock ******************/
	}

usbhsg_recip_run_handle_end:
	usbhs_pkt_start(pipe);

	return ret;
}

/*
 *		irq functions
 *
 * it will be called from usbhs_interrupt
 */
static int usbhsg_irq_dev_state(struct usbhs_priv *priv,
				struct usbhs_irq_state *irq_state)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);

	gpriv->gadget.speed = usbhs_bus_get_speed(priv);

	dev_dbg(dev, "state = %x : speed : %d\n",
		usbhs_status_get_device_state(irq_state),
		gpriv->gadget.speed);

	return 0;
}

static int usbhsg_irq_ctrl_stage(struct usbhs_priv *priv,
				 struct usbhs_irq_state *irq_state)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(dcp);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usb_ctrlrequest ctrl;
	struct usbhsg_recip_handle *recip_handler = NULL;
	int stage = usbhs_status_get_ctrl_stage(irq_state);
	int ret = 0;

	dev_dbg(dev, "stage = %d\n", stage);

	/*
	 * see Manual
	 *
	 *  "Operation"
	 *  - "Interrupt Function"
	 *    - "Control Transfer Stage Transition Interrupt"
	 *      - Fig. "Control Transfer Stage Transitions"
	 */

	switch (stage) {
	case READ_DATA_STAGE:
		pipe->handler = &usbhs_fifo_pio_push_handler;
		break;
	case WRITE_DATA_STAGE:
		pipe->handler = &usbhs_fifo_pio_pop_handler;
		break;
	case NODATA_STATUS_STAGE:
		pipe->handler = &usbhs_ctrl_stage_end_handler;
		break;
	default:
		return ret;
	}

	/*
	 * get usb request
	 */
	usbhs_usbreq_get_val(priv, &ctrl);

	switch (ctrl.bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		switch (ctrl.bRequest) {
		case USB_REQ_CLEAR_FEATURE:
			recip_handler = &req_clear_feature;
			break;
		}
	}

	/*
	 * setup stage / run recip
	 */
	if (recip_handler)
		ret = usbhsg_recip_run_handle(priv, recip_handler, &ctrl);
	else
		ret = gpriv->driver->setup(&gpriv->gadget, &ctrl);

	if (ret < 0)
		usbhs_pipe_stall(pipe);

	return ret;
}

/*
 *
 *		usb_dcp_ops
 *
 */
static int usbhsg_pipe_disable(struct usbhsg_uep *uep)
{
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhs_pkt *pkt;

	usbhs_pipe_disable(pipe);

	while (1) {
		pkt = usbhs_pkt_pop(pipe, NULL);
		if (!pkt)
			break;
	}

	return 0;
}

static void usbhsg_uep_init(struct usbhsg_gpriv *gpriv)
{
	int i;
	struct usbhsg_uep *uep;

	usbhsg_for_each_uep_with_dcp(uep, gpriv, i)
		uep->pipe = NULL;
}

/*
 *
 *		usb_ep_ops
 *
 */
static int usbhsg_ep_enable(struct usb_ep *ep,
			 const struct usb_endpoint_descriptor *desc)
{
	struct usbhsg_uep *uep   = usbhsg_ep_to_uep(ep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	struct usbhs_pipe *pipe;
	int ret = -EIO;

	/*
	 * if it already have pipe,
	 * nothing to do
	 */
	if (uep->pipe) {
		usbhs_pipe_clear(uep->pipe);
		usbhs_pipe_sequence_data0(uep->pipe);
		return 0;
	}

	pipe = usbhs_pipe_malloc(priv,
				 usb_endpoint_type(desc),
				 usb_endpoint_dir_in(desc));
	if (pipe) {
		uep->pipe		= pipe;
		pipe->mod_private	= uep;

		/* set epnum / maxp */
		usbhs_pipe_config_update(pipe, 0,
					 usb_endpoint_num(desc),
					 usb_endpoint_maxp(desc));

		/*
		 * usbhs_fifo_dma_push/pop_handler try to
		 * use dmaengine if possible.
		 * It will use pio handler if impossible.
		 */
		if (usb_endpoint_dir_in(desc))
			pipe->handler = &usbhs_fifo_dma_push_handler;
		else
			pipe->handler = &usbhs_fifo_dma_pop_handler;

		ret = 0;
	}

	return ret;
}

static int usbhsg_ep_disable(struct usb_ep *ep)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);

	return usbhsg_pipe_disable(uep);
}

static struct usb_request *usbhsg_ep_alloc_request(struct usb_ep *ep,
						   gfp_t gfp_flags)
{
	struct usbhsg_request *ureq;

	ureq = kzalloc(sizeof *ureq, gfp_flags);
	if (!ureq)
		return NULL;

	usbhs_pkt_init(usbhsg_ureq_to_pkt(ureq));

	ureq->req.dma = DMA_ADDR_INVALID;

	return &ureq->req;
}

static void usbhsg_ep_free_request(struct usb_ep *ep,
				   struct usb_request *req)
{
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);

	WARN_ON(!list_empty(&ureq->pkt.node));
	kfree(ureq);
}

static int usbhsg_ep_queue(struct usb_ep *ep, struct usb_request *req,
			  gfp_t gfp_flags)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);

	/* param check */
	if (usbhsg_is_not_connected(gpriv)	||
	    unlikely(!gpriv->driver)		||
	    unlikely(!pipe))
		return -ESHUTDOWN;

	usbhsg_queue_push(uep, ureq);

	return 0;
}

static int usbhsg_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);

	usbhs_pkt_pop(pipe, usbhsg_ureq_to_pkt(ureq));
	usbhsg_queue_pop(uep, ureq, -ECONNRESET);

	return 0;
}

static int __usbhsg_ep_set_halt_wedge(struct usb_ep *ep, int halt, int wedge)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	unsigned long flags;

	usbhsg_pipe_disable(uep);

	dev_dbg(dev, "set halt %d (pipe %d)\n",
		halt, usbhs_pipe_number(pipe));

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	if (halt)
		usbhs_pipe_stall(pipe);
	else
		usbhs_pipe_disable(pipe);

	if (halt && wedge)
		usbhsg_status_set(gpriv, USBHSG_STATUS_WEDGE);
	else
		usbhsg_status_clr(gpriv, USBHSG_STATUS_WEDGE);

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	return 0;
}

static int usbhsg_ep_set_halt(struct usb_ep *ep, int value)
{
	return __usbhsg_ep_set_halt_wedge(ep, value, 0);
}

static int usbhsg_ep_set_wedge(struct usb_ep *ep)
{
	return __usbhsg_ep_set_halt_wedge(ep, 1, 1);
}

static struct usb_ep_ops usbhsg_ep_ops = {
	.enable		= usbhsg_ep_enable,
	.disable	= usbhsg_ep_disable,

	.alloc_request	= usbhsg_ep_alloc_request,
	.free_request	= usbhsg_ep_free_request,

	.queue		= usbhsg_ep_queue,
	.dequeue	= usbhsg_ep_dequeue,

	.set_halt	= usbhsg_ep_set_halt,
	.set_wedge	= usbhsg_ep_set_wedge,
};

/*
 *		usb module start/end
 */
static int usbhsg_try_start(struct usbhs_priv *priv, u32 status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;
	int ret = 0;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	usbhsg_status_set(gpriv, status);
	if (!(usbhsg_status_has(gpriv, USBHSG_STATUS_STARTED) &&
	      usbhsg_status_has(gpriv, USBHSG_STATUS_REGISTERD)))
		ret = -1; /* not ready */

	usbhs_unlock(priv, flags);
	/********************  spin unlock ********************/

	if (ret < 0)
		return 0; /* not ready is not error */

	/*
	 * enable interrupt and systems if ready
	 */
	dev_dbg(dev, "start gadget\n");

	/*
	 * pipe initialize and enable DCP
	 */
	usbhs_pipe_init(priv,
			usbhsg_dma_map_ctrl);
	usbhs_fifo_init(priv);
	usbhsg_uep_init(gpriv);

	/* dcp init */
	dcp->pipe		= usbhs_dcp_malloc(priv);
	dcp->pipe->mod_private	= dcp;
	usbhs_pipe_config_update(dcp->pipe, 0, 0, 64);

	/*
	 * system config enble
	 * - HI speed
	 * - function
	 * - usb module
	 */
	usbhs_sys_hispeed_ctrl(priv, 1);
	usbhs_sys_function_ctrl(priv, 1);
	usbhs_sys_usb_ctrl(priv, 1);

	/*
	 * enable irq callback
	 */
	mod->irq_dev_state	= usbhsg_irq_dev_state;
	mod->irq_ctrl_stage	= usbhsg_irq_ctrl_stage;
	usbhs_irq_callback_update(priv, mod);

	return 0;
}

static int usbhsg_try_stop(struct usbhs_priv *priv, u32 status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;
	int ret = 0;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	usbhsg_status_clr(gpriv, status);
	if (!usbhsg_status_has(gpriv, USBHSG_STATUS_STARTED) &&
	    !usbhsg_status_has(gpriv, USBHSG_STATUS_REGISTERD))
		ret = -1; /* already done */

	usbhs_unlock(priv, flags);
	/********************  spin unlock ********************/

	if (ret < 0)
		return 0; /* already done is not error */

	/*
	 * disable interrupt and systems if 1st try
	 */
	usbhs_fifo_quit(priv);

	/* disable all irq */
	mod->irq_dev_state	= NULL;
	mod->irq_ctrl_stage	= NULL;
	usbhs_irq_callback_update(priv, mod);

	gpriv->gadget.speed = USB_SPEED_UNKNOWN;

	/* disable sys */
	usbhs_sys_hispeed_ctrl(priv, 0);
	usbhs_sys_function_ctrl(priv, 0);
	usbhs_sys_usb_ctrl(priv, 0);

	usbhsg_pipe_disable(dcp);

	dev_dbg(dev, "stop gadget\n");

	return 0;
}

/*
 *
 *		linux usb function
 *
 */
static int usbhsg_gadget_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);

	if (!driver		||
	    !driver->setup	||
	    driver->speed < USB_SPEED_FULL)
		return -EINVAL;

	/* first hook up the driver ... */
	gpriv->driver = driver;
	gpriv->gadget.dev.driver = &driver->driver;

	return usbhsg_try_start(priv, USBHSG_STATUS_REGISTERD);
}

static int usbhsg_gadget_stop(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);

	if (!driver		||
	    !driver->unbind)
		return -EINVAL;

	usbhsg_try_stop(priv, USBHSG_STATUS_REGISTERD);
	gpriv->gadget.dev.driver = NULL;
	gpriv->driver = NULL;

	return 0;
}

/*
 *		usb gadget ops
 */
static int usbhsg_get_frame(struct usb_gadget *gadget)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);

	return usbhs_frame_get_num(priv);
}

static struct usb_gadget_ops usbhsg_gadget_ops = {
	.get_frame		= usbhsg_get_frame,
	.udc_start		= usbhsg_gadget_start,
	.udc_stop		= usbhsg_gadget_stop,
};

static int usbhsg_start(struct usbhs_priv *priv)
{
	return usbhsg_try_start(priv, USBHSG_STATUS_STARTED);
}

static int usbhsg_stop(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);

	/* cable disconnect */
	if (gpriv->driver &&
	    gpriv->driver->disconnect)
		gpriv->driver->disconnect(&gpriv->gadget);

	return usbhsg_try_stop(priv, USBHSG_STATUS_STARTED);
}

int usbhs_mod_gadget_probe(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv;
	struct usbhsg_uep *uep;
	struct device *dev = usbhs_priv_to_dev(priv);
	int pipe_size = usbhs_get_dparam(priv, pipe_size);
	int i;
	int ret;

	gpriv = kzalloc(sizeof(struct usbhsg_gpriv), GFP_KERNEL);
	if (!gpriv) {
		dev_err(dev, "Could not allocate gadget priv\n");
		return -ENOMEM;
	}

	uep = kzalloc(sizeof(struct usbhsg_uep) * pipe_size, GFP_KERNEL);
	if (!uep) {
		dev_err(dev, "Could not allocate ep\n");
		ret = -ENOMEM;
		goto usbhs_mod_gadget_probe_err_gpriv;
	}

	/*
	 * CAUTION
	 *
	 * There is no guarantee that it is possible to access usb module here.
	 * Don't accesses to it.
	 * The accesse will be enable after "usbhsg_start"
	 */

	/*
	 * register itself
	 */
	usbhs_mod_register(priv, &gpriv->mod, USBHS_GADGET);

	/* init gpriv */
	gpriv->mod.name		= "gadget";
	gpriv->mod.start	= usbhsg_start;
	gpriv->mod.stop		= usbhsg_stop;
	gpriv->uep		= uep;
	gpriv->uep_size		= pipe_size;
	usbhsg_status_init(gpriv);

	/*
	 * init gadget
	 */
	dev_set_name(&gpriv->gadget.dev, "gadget");
	gpriv->gadget.dev.parent	= dev;
	gpriv->gadget.name		= "renesas_usbhs_udc";
	gpriv->gadget.ops		= &usbhsg_gadget_ops;
	gpriv->gadget.is_dualspeed	= 1;
	ret = device_register(&gpriv->gadget.dev);
	if (ret < 0)
		goto err_add_udc;

	INIT_LIST_HEAD(&gpriv->gadget.ep_list);

	/*
	 * init usb_ep
	 */
	usbhsg_for_each_uep_with_dcp(uep, gpriv, i) {
		uep->gpriv	= gpriv;
		snprintf(uep->ep_name, EP_NAME_SIZE, "ep%d", i);

		uep->ep.name		= uep->ep_name;
		uep->ep.ops		= &usbhsg_ep_ops;
		INIT_LIST_HEAD(&uep->ep.ep_list);

		/* init DCP */
		if (usbhsg_is_dcp(uep)) {
			gpriv->gadget.ep0 = &uep->ep;
			uep->ep.maxpacket = 64;
		}
		/* init normal pipe */
		else {
			uep->ep.maxpacket = 512;
			list_add_tail(&uep->ep.ep_list, &gpriv->gadget.ep_list);
		}
	}

	usbhsg_controller_register(gpriv);

	ret = usb_add_gadget_udc(dev, &gpriv->gadget);
	if (ret)
		goto err_register;


	dev_info(dev, "gadget probed\n");

	return 0;

err_register:
	device_unregister(&gpriv->gadget.dev);
err_add_udc:
	kfree(gpriv->uep);

usbhs_mod_gadget_probe_err_gpriv:
	kfree(gpriv);

	return ret;
}

void usbhs_mod_gadget_remove(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);

	usb_del_gadget_udc(&gpriv->gadget);

	device_unregister(&gpriv->gadget.dev);

	usbhsg_controller_unregister(gpriv);

	kfree(gpriv->uep);
	kfree(gpriv);
}
