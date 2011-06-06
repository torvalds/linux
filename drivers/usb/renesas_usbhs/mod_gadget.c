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
	struct usbhs_pkt_handle *handler;
};

struct usbhsg_gpriv {
	struct usb_gadget	 gadget;
	struct usbhs_mod	 mod;

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
	for (i = start, pos = (g)->uep;		\
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
#define usbhsg_gpriv_to_lock(gp)	usbhs_priv_to_lock((gp)->mod.priv)
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

/*
 *		usbhsg_trylock
 *
 * This driver don't use spin_try_lock
 * to avoid warning of CONFIG_DEBUG_SPINLOCK
 */
static spinlock_t *usbhsg_trylock(struct usbhsg_gpriv *gpriv,
				  unsigned long *flags)
{
	spinlock_t *lock = usbhsg_gpriv_to_lock(gpriv);

	/* check spin lock status
	 * to avoid deadlock/nest */
	if (spin_is_locked(lock))
		return NULL;

	spin_lock_irqsave(lock, *flags);

	return lock;
}

static void usbhsg_unlock(spinlock_t *lock, unsigned long *flags)
{
	if (!lock)
		return;

	spin_unlock_irqrestore(lock, *flags);
}

/*
 *		list push/pop
 */
static void usbhsg_queue_push(struct usbhsg_uep *uep,
			      struct usbhsg_request *ureq)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhs_pkt *pkt = usbhsg_ureq_to_pkt(ureq);
	struct usb_request *req = &ureq->req;

	/*
	 *********  assume under spin lock  *********
	 */
	usbhs_pkt_push(pipe, pkt, uep->handler,
		       req->buf, req->length, req->zero);
	req->actual = 0;
	req->status = -EINPROGRESS;

	dev_dbg(dev, "pipe %d : queue push (%d)\n",
		usbhs_pipe_number(pipe),
		req->length);
}

static int usbhsg_queue_start(struct usbhsg_uep *uep)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhs_pkt *pkt;
	spinlock_t *lock;
	unsigned long flags;
	int ret = 0;

	/*
	 * CAUTION [*queue handler*]
	 *
	 * This function will be called for start/restart queue operation.
	 * OTOH the most much worry for USB driver is spinlock nest.
	 * Specially it are
	 *   - usb_ep_ops  :: queue
	 *   - usb_request :: complete
	 *
	 * But the caller of this function need not care about spinlock.
	 * This function is using usbhsg_trylock for it.
	 * if "is_locked" is 1, this mean this function lock it.
	 * but if it is 0, this mean it is already under spin lock.
	 * see also
	 *   CAUTION [*endpoint queue*]
	 *   CAUTION [*request complete*]
	 */

	/******************  spin try lock *******************/
	lock = usbhsg_trylock(gpriv, &flags);

	pkt = usbhs_pkt_get(pipe);
	if (pkt)
		ret = usbhs_pkt_start(pkt);

	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ******************/

	return ret;
}

static void usbhsg_queue_pop(struct usbhsg_uep *uep,
			     struct usbhsg_request *ureq,
			     int status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usbhs_pkt *pkt = usbhsg_ureq_to_pkt(ureq);

	/*
	 *********  assume under spin lock  *********
	 */

	/*
	 * CAUTION [*request complete*]
	 *
	 * There is a possibility not to be called in correct order
	 * if "complete" is called without spinlock.
	 *
	 * So, this function assume it is under spinlock,
	 * and call usb_request :: complete.
	 *
	 * But this "complete" will push next usb_request.
	 * It mean "usb_ep_ops :: queue" which is using spinlock is called
	 * under spinlock.
	 *
	 * To avoid dead-lock, this driver is using usbhsg_trylock.
	 *   CAUTION [*endpoint queue*]
	 *   CAUTION [*queue handler*]
	 */

	dev_dbg(dev, "pipe %d : queue pop\n", usbhs_pipe_number(pipe));

	usbhs_pkt_pop(pkt);

	ureq->req.status = status;
	ureq->req.complete(&uep->ep, &ureq->req);

	/* more request ? */
	if (0 == status)
		usbhsg_queue_start(uep);
}

static void usbhsg_queue_done(struct usbhs_pkt *pkt)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhsg_uep *uep = usbhsg_pipe_to_uep(pipe);
	struct usbhsg_request *ureq = usbhsg_pkt_to_ureq(pkt);

	ureq->req.actual = pkt->actual;

	usbhsg_queue_pop(uep, ureq, 0);
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
		usbhs_pipe_clear_sequence(pipe);
		usbhs_pipe_enable(pipe);
	}

	usbhsg_recip_handler_std_control_done(priv, uep, ctrl);

	usbhsg_queue_start(uep);

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
	int recip = ctrl->bRequestType & USB_RECIP_MASK;
	int nth = le16_to_cpu(ctrl->wIndex) & USB_ENDPOINT_NUMBER_MASK;
	int ret;
	int (*func)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
		    struct usb_ctrlrequest *ctrl);
	char *msg;

	uep = usbhsg_gpriv_to_nth_uep(gpriv, nth);
	if (!usbhsg_uep_to_pipe(uep)) {
		dev_err(dev, "wrong recip request\n");
		return -EINVAL;
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
		dev_dbg(dev, "%s (pipe %d :%s)\n", handler->name, nth, msg);
		ret = func(priv, uep, ctrl);
	}

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

	gpriv->gadget.speed = usbhs_status_get_usb_speed(irq_state);

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
		dcp->handler = &usbhs_fifo_push_handler;
		break;
	case WRITE_DATA_STAGE:
		dcp->handler = &usbhs_fifo_pop_handler;
		break;
	case NODATA_STATUS_STAGE:
		dcp->handler = &usbhs_ctrl_stage_end_handler;
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
static int usbhsg_dcp_enable(struct usbhsg_uep *uep)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	struct usbhs_pipe *pipe;

	/*
	 *********  assume under spin lock  *********
	 */

	pipe = usbhs_dcp_malloc(priv);
	if (!pipe)
		return -EIO;

	uep->pipe		= pipe;
	uep->pipe->mod_private	= uep;

	return 0;
}

#define usbhsg_dcp_disable usbhsg_pipe_disable
static int usbhsg_pipe_disable(struct usbhsg_uep *uep)
{
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhs_pkt *pkt;

	/*
	 *********  assume under spin lock  *********
	 */

	usbhs_pipe_disable(pipe);

	while (1) {
		pkt = usbhs_pkt_get(pipe);
		if (!pkt)
			break;

		usbhs_pkt_pop(pkt);
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
	spinlock_t *lock;
	unsigned long flags;
	int ret = -EIO;

	/*
	 * if it already have pipe,
	 * nothing to do
	 */
	if (uep->pipe)
		return 0;

	/********************  spin lock ********************/
	lock = usbhsg_trylock(gpriv, &flags);

	pipe = usbhs_pipe_malloc(priv, desc);
	if (pipe) {
		uep->pipe		= pipe;
		pipe->mod_private	= uep;

		if (usb_endpoint_dir_in(desc))
			uep->handler = &usbhs_fifo_push_handler;
		else
			uep->handler = &usbhs_fifo_pop_handler;

		ret = 0;
	}

	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ******************/

	return ret;
}

static int usbhsg_ep_disable(struct usb_ep *ep)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	spinlock_t *lock;
	unsigned long flags;
	int ret;

	/********************  spin lock ********************/
	lock = usbhsg_trylock(gpriv, &flags);

	ret = usbhsg_pipe_disable(uep);

	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ******************/

	return ret;
}

static struct usb_request *usbhsg_ep_alloc_request(struct usb_ep *ep,
						   gfp_t gfp_flags)
{
	struct usbhsg_request *ureq;

	ureq = kzalloc(sizeof *ureq, gfp_flags);
	if (!ureq)
		return NULL;

	usbhs_pkt_init(usbhsg_ureq_to_pkt(ureq));

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
	spinlock_t *lock;
	unsigned long flags;
	int ret = 0;

	/*
	 * CAUTION [*endpoint queue*]
	 *
	 * This function will be called from usb_request :: complete
	 * or usb driver timing.
	 * If this function is called from usb_request :: complete,
	 * it is already under spinlock on this driver.
	 * but it is called frm usb driver, this function should call spinlock.
	 *
	 * This function is using usbshg_trylock to solve this issue.
	 * if "is_locked" is 1, this mean this function lock it.
	 * but if it is 0, this mean it is already under spin lock.
	 * see also
	 *   CAUTION [*queue handler*]
	 *   CAUTION [*request complete*]
	 */

	/********************  spin lock ********************/
	lock = usbhsg_trylock(gpriv, &flags);

	/* param check */
	if (usbhsg_is_not_connected(gpriv)	||
	    unlikely(!gpriv->driver)		||
	    unlikely(!pipe))
		ret = -ESHUTDOWN;
	else
		usbhsg_queue_push(uep, ureq);

	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ******************/

	usbhsg_queue_start(uep);

	return ret;
}

static int usbhsg_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	spinlock_t *lock;
	unsigned long flags;

	/*
	 * see
	 *   CAUTION [*queue handler*]
	 *   CAUTION [*endpoint queue*]
	 *   CAUTION [*request complete*]
	 */

	/********************  spin lock ********************/
	lock = usbhsg_trylock(gpriv, &flags);

	usbhsg_queue_pop(uep, ureq, -ECONNRESET);

	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ******************/

	return 0;
}

static int __usbhsg_ep_set_halt_wedge(struct usb_ep *ep, int halt, int wedge)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	spinlock_t *lock;
	unsigned long flags;
	int ret = -EAGAIN;

	/*
	 * see
	 *   CAUTION [*queue handler*]
	 *   CAUTION [*endpoint queue*]
	 *   CAUTION [*request complete*]
	 */

	/********************  spin lock ********************/
	lock = usbhsg_trylock(gpriv, &flags);
	if (!usbhs_pkt_get(pipe)) {

		dev_dbg(dev, "set halt %d (pipe %d)\n",
			halt, usbhs_pipe_number(pipe));

		if (halt)
			usbhs_pipe_stall(pipe);
		else
			usbhs_pipe_disable(pipe);

		if (halt && wedge)
			usbhsg_status_set(gpriv, USBHSG_STATUS_WEDGE);
		else
			usbhsg_status_clr(gpriv, USBHSG_STATUS_WEDGE);

		ret = 0;
	}

	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ******************/

	return ret;
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
	spinlock_t *lock;
	unsigned long flags;

	/********************  spin lock ********************/
	lock = usbhsg_trylock(gpriv, &flags);

	/*
	 * enable interrupt and systems if ready
	 */
	usbhsg_status_set(gpriv, status);
	if (!(usbhsg_status_has(gpriv, USBHSG_STATUS_STARTED) &&
	      usbhsg_status_has(gpriv, USBHSG_STATUS_REGISTERD)))
		goto usbhsg_try_start_unlock;

	dev_dbg(dev, "start gadget\n");

	/*
	 * pipe initialize and enable DCP
	 */
	usbhs_pipe_init(priv,
			usbhsg_queue_done);
	usbhs_fifo_init(priv);
	usbhsg_uep_init(gpriv);
	usbhsg_dcp_enable(dcp);

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

usbhsg_try_start_unlock:
	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ********************/

	return 0;
}

static int usbhsg_try_stop(struct usbhs_priv *priv, u32 status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	spinlock_t *lock;
	unsigned long flags;

	/********************  spin lock ********************/
	lock = usbhsg_trylock(gpriv, &flags);

	/*
	 * disable interrupt and systems if 1st try
	 */
	usbhsg_status_clr(gpriv, status);
	if (!usbhsg_status_has(gpriv, USBHSG_STATUS_STARTED) &&
	    !usbhsg_status_has(gpriv, USBHSG_STATUS_REGISTERD))
		goto usbhsg_try_stop_unlock;

	usbhs_fifo_quit(priv);

	/* disable all irq */
	mod->irq_dev_state	= NULL;
	mod->irq_ctrl_stage	= NULL;
	usbhs_irq_callback_update(priv, mod);

	usbhsg_dcp_disable(dcp);

	gpriv->gadget.speed = USB_SPEED_UNKNOWN;

	/* disable sys */
	usbhs_sys_hispeed_ctrl(priv, 0);
	usbhs_sys_function_ctrl(priv, 0);
	usbhs_sys_usb_ctrl(priv, 0);

	usbhsg_unlock(lock, &flags);
	/********************  spin unlock ********************/

	if (gpriv->driver &&
	    gpriv->driver->disconnect)
		gpriv->driver->disconnect(&gpriv->gadget);

	dev_dbg(dev, "stop gadget\n");

	return 0;

usbhsg_try_stop_unlock:
	usbhsg_unlock(lock, &flags);

	return 0;
}

/*
 *
 *		linux usb function
 *
 */
struct usbhsg_gpriv *the_controller;
int usb_gadget_probe_driver(struct usb_gadget_driver *driver,
			    int (*bind)(struct usb_gadget *))
{
	struct usbhsg_gpriv *gpriv = the_controller;
	struct usbhs_priv *priv;
	struct device *dev;
	int ret;

	if (!bind		||
	    !driver		||
	    !driver->setup	||
	    driver->speed != USB_SPEED_HIGH)
		return -EINVAL;
	if (!gpriv)
		return -ENODEV;
	if (gpriv->driver)
		return -EBUSY;

	dev  = usbhsg_gpriv_to_dev(gpriv);
	priv = usbhsg_gpriv_to_priv(gpriv);

	/* first hook up the driver ... */
	gpriv->driver = driver;
	gpriv->gadget.dev.driver = &driver->driver;

	ret = device_add(&gpriv->gadget.dev);
	if (ret) {
		dev_err(dev, "device_add error %d\n", ret);
		goto add_fail;
	}

	ret = bind(&gpriv->gadget);
	if (ret) {
		dev_err(dev, "bind to driver %s error %d\n",
			driver->driver.name, ret);
		goto bind_fail;
	}

	dev_dbg(dev, "bind %s\n", driver->driver.name);

	return usbhsg_try_start(priv, USBHSG_STATUS_REGISTERD);

bind_fail:
	device_del(&gpriv->gadget.dev);
add_fail:
	gpriv->driver = NULL;
	gpriv->gadget.dev.driver = NULL;

	return ret;
}
EXPORT_SYMBOL(usb_gadget_probe_driver);

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct usbhsg_gpriv *gpriv = the_controller;
	struct usbhs_priv *priv;
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);

	if (!gpriv)
		return -ENODEV;

	if (!driver		||
	    !driver->unbind	||
	    driver != gpriv->driver)
		return -EINVAL;

	dev  = usbhsg_gpriv_to_dev(gpriv);
	priv = usbhsg_gpriv_to_priv(gpriv);

	usbhsg_try_stop(priv, USBHSG_STATUS_REGISTERD);
	device_del(&gpriv->gadget.dev);
	gpriv->driver = NULL;

	if (driver->disconnect)
		driver->disconnect(&gpriv->gadget);

	driver->unbind(&gpriv->gadget);
	dev_dbg(dev, "unbind %s\n", driver->driver.name);

	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);

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
};

static int usbhsg_start(struct usbhs_priv *priv)
{
	return usbhsg_try_start(priv, USBHSG_STATUS_STARTED);
}

static int usbhsg_stop(struct usbhs_priv *priv)
{
	return usbhsg_try_stop(priv, USBHSG_STATUS_STARTED);
}

int __devinit usbhs_mod_gadget_probe(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv;
	struct usbhsg_uep *uep;
	struct device *dev = usbhs_priv_to_dev(priv);
	int pipe_size = usbhs_get_dparam(priv, pipe_size);
	int i;

	gpriv = kzalloc(sizeof(struct usbhsg_gpriv), GFP_KERNEL);
	if (!gpriv) {
		dev_err(dev, "Could not allocate gadget priv\n");
		return -ENOMEM;
	}

	uep = kzalloc(sizeof(struct usbhsg_uep) * pipe_size, GFP_KERNEL);
	if (!uep) {
		dev_err(dev, "Could not allocate ep\n");
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
	device_initialize(&gpriv->gadget.dev);
	dev_set_name(&gpriv->gadget.dev, "gadget");
	gpriv->gadget.dev.parent	= dev;
	gpriv->gadget.name		= "renesas_usbhs_udc";
	gpriv->gadget.ops		= &usbhsg_gadget_ops;
	gpriv->gadget.is_dualspeed	= 1;

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

	the_controller = gpriv;

	dev_info(dev, "gadget probed\n");

	return 0;

usbhs_mod_gadget_probe_err_gpriv:
	kfree(gpriv);

	return -ENOMEM;
}

void __devexit usbhs_mod_gadget_remove(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);

	kfree(gpriv);
}
