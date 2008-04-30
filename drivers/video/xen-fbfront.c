/*
 * Xen para-virtual frame buffer device
 *
 * Copyright (C) 2005-2006 Anthony Liguori <aliguori@us.ibm.com>
 * Copyright (C) 2006-2008 Red Hat, Inc., Markus Armbruster <armbru@redhat.com>
 *
 *  Based on linux/drivers/video/q40fb.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

/*
 * TODO:
 *
 * Switch to grant tables when they become capable of dealing with the
 * frame buffer.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/xen/hypervisor.h>
#include <xen/events.h>
#include <xen/page.h>
#include <xen/interface/io/fbif.h>
#include <xen/interface/io/protocols.h>
#include <xen/xenbus.h>

struct xenfb_info {
	unsigned char		*fb;
	struct fb_info		*fb_info;
	int			x1, y1, x2, y2;	/* dirty rectangle,
						   protected by dirty_lock */
	spinlock_t		dirty_lock;
	int			nr_pages;
	int			irq;
	struct xenfb_page	*page;
	unsigned long 		*mfns;
	int			update_wanted; /* XENFB_TYPE_UPDATE wanted */

	struct xenbus_device	*xbdev;
};

static u32 xenfb_mem_len = XENFB_WIDTH * XENFB_HEIGHT * XENFB_DEPTH / 8;

static int xenfb_remove(struct xenbus_device *);
static void xenfb_init_shared_page(struct xenfb_info *);
static int xenfb_connect_backend(struct xenbus_device *, struct xenfb_info *);
static void xenfb_disconnect_backend(struct xenfb_info *);

static void xenfb_do_update(struct xenfb_info *info,
			    int x, int y, int w, int h)
{
	union xenfb_out_event event;
	u32 prod;

	event.type = XENFB_TYPE_UPDATE;
	event.update.x = x;
	event.update.y = y;
	event.update.width = w;
	event.update.height = h;

	prod = info->page->out_prod;
	/* caller ensures !xenfb_queue_full() */
	mb();			/* ensure ring space available */
	XENFB_OUT_RING_REF(info->page, prod) = event;
	wmb();			/* ensure ring contents visible */
	info->page->out_prod = prod + 1;

	notify_remote_via_irq(info->irq);
}

static int xenfb_queue_full(struct xenfb_info *info)
{
	u32 cons, prod;

	prod = info->page->out_prod;
	cons = info->page->out_cons;
	return prod - cons == XENFB_OUT_RING_LEN;
}

static void xenfb_refresh(struct xenfb_info *info,
			  int x1, int y1, int w, int h)
{
	unsigned long flags;
	int y2 = y1 + h - 1;
	int x2 = x1 + w - 1;

	if (!info->update_wanted)
		return;

	spin_lock_irqsave(&info->dirty_lock, flags);

	/* Combine with dirty rectangle: */
	if (info->y1 < y1)
		y1 = info->y1;
	if (info->y2 > y2)
		y2 = info->y2;
	if (info->x1 < x1)
		x1 = info->x1;
	if (info->x2 > x2)
		x2 = info->x2;

	if (xenfb_queue_full(info)) {
		/* Can't send right now, stash it in the dirty rectangle */
		info->x1 = x1;
		info->x2 = x2;
		info->y1 = y1;
		info->y2 = y2;
		spin_unlock_irqrestore(&info->dirty_lock, flags);
		return;
	}

	/* Clear dirty rectangle: */
	info->x1 = info->y1 = INT_MAX;
	info->x2 = info->y2 = 0;

	spin_unlock_irqrestore(&info->dirty_lock, flags);

	if (x1 <= x2 && y1 <= y2)
		xenfb_do_update(info, x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}

static void xenfb_deferred_io(struct fb_info *fb_info,
			      struct list_head *pagelist)
{
	struct xenfb_info *info = fb_info->par;
	struct page *page;
	unsigned long beg, end;
	int y1, y2, miny, maxy;

	miny = INT_MAX;
	maxy = 0;
	list_for_each_entry(page, pagelist, lru) {
		beg = page->index << PAGE_SHIFT;
		end = beg + PAGE_SIZE - 1;
		y1 = beg / fb_info->fix.line_length;
		y2 = end / fb_info->fix.line_length;
		if (y2 >= fb_info->var.yres)
			y2 = fb_info->var.yres - 1;
		if (miny > y1)
			miny = y1;
		if (maxy < y2)
			maxy = y2;
	}
	xenfb_refresh(info, 0, miny, fb_info->var.xres, maxy - miny + 1);
}

static struct fb_deferred_io xenfb_defio = {
	.delay		= HZ / 20,
	.deferred_io	= xenfb_deferred_io,
};

static int xenfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	u32 v;

	if (regno > info->cmap.len)
		return 1;

#define CNVT_TOHW(val, width) ((((val)<<(width))+0x7FFF-(val))>>16)
	red = CNVT_TOHW(red, info->var.red.length);
	green = CNVT_TOHW(green, info->var.green.length);
	blue = CNVT_TOHW(blue, info->var.blue.length);
	transp = CNVT_TOHW(transp, info->var.transp.length);
#undef CNVT_TOHW

	v = (red << info->var.red.offset) |
	    (green << info->var.green.offset) |
	    (blue << info->var.blue.offset);

	switch (info->var.bits_per_pixel) {
	case 16:
	case 24:
	case 32:
		((u32 *)info->pseudo_palette)[regno] = v;
		break;
	}

	return 0;
}

static void xenfb_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	struct xenfb_info *info = p->par;

	sys_fillrect(p, rect);
	xenfb_refresh(info, rect->dx, rect->dy, rect->width, rect->height);
}

static void xenfb_imageblit(struct fb_info *p, const struct fb_image *image)
{
	struct xenfb_info *info = p->par;

	sys_imageblit(p, image);
	xenfb_refresh(info, image->dx, image->dy, image->width, image->height);
}

static void xenfb_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	struct xenfb_info *info = p->par;

	sys_copyarea(p, area);
	xenfb_refresh(info, area->dx, area->dy, area->width, area->height);
}

static ssize_t xenfb_write(struct fb_info *p, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct xenfb_info *info = p->par;
	ssize_t res;

	res = fb_sys_write(p, buf, count, ppos);
	xenfb_refresh(info, 0, 0, info->page->width, info->page->height);
	return res;
}

static struct fb_ops xenfb_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_read	= fb_sys_read,
	.fb_write	= xenfb_write,
	.fb_setcolreg	= xenfb_setcolreg,
	.fb_fillrect	= xenfb_fillrect,
	.fb_copyarea	= xenfb_copyarea,
	.fb_imageblit	= xenfb_imageblit,
};

static irqreturn_t xenfb_event_handler(int rq, void *dev_id)
{
	/*
	 * No in events recognized, simply ignore them all.
	 * If you need to recognize some, see xen-kbdfront's
	 * input_handler() for how to do that.
	 */
	struct xenfb_info *info = dev_id;
	struct xenfb_page *page = info->page;

	if (page->in_cons != page->in_prod) {
		info->page->in_cons = info->page->in_prod;
		notify_remote_via_irq(info->irq);
	}

	/* Flush dirty rectangle: */
	xenfb_refresh(info, INT_MAX, INT_MAX, -INT_MAX, -INT_MAX);

	return IRQ_HANDLED;
}

static int __devinit xenfb_probe(struct xenbus_device *dev,
				 const struct xenbus_device_id *id)
{
	struct xenfb_info *info;
	struct fb_info *fb_info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating info structure");
		return -ENOMEM;
	}
	dev->dev.driver_data = info;
	info->xbdev = dev;
	info->irq = -1;
	info->x1 = info->y1 = INT_MAX;
	spin_lock_init(&info->dirty_lock);

	info->fb = vmalloc(xenfb_mem_len);
	if (info->fb == NULL)
		goto error_nomem;
	memset(info->fb, 0, xenfb_mem_len);

	info->nr_pages = (xenfb_mem_len + PAGE_SIZE - 1) >> PAGE_SHIFT;

	info->mfns = vmalloc(sizeof(unsigned long) * info->nr_pages);
	if (!info->mfns)
		goto error_nomem;

	/* set up shared page */
	info->page = (void *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!info->page)
		goto error_nomem;

	xenfb_init_shared_page(info);

	/* abusing framebuffer_alloc() to allocate pseudo_palette */
	fb_info = framebuffer_alloc(sizeof(u32) * 256, NULL);
	if (fb_info == NULL)
		goto error_nomem;

	/* complete the abuse: */
	fb_info->pseudo_palette = fb_info->par;
	fb_info->par = info;

	fb_info->screen_base = info->fb;

	fb_info->fbops = &xenfb_fb_ops;
	fb_info->var.xres_virtual = fb_info->var.xres = info->page->width;
	fb_info->var.yres_virtual = fb_info->var.yres = info->page->height;
	fb_info->var.bits_per_pixel = info->page->depth;

	fb_info->var.red = (struct fb_bitfield){16, 8, 0};
	fb_info->var.green = (struct fb_bitfield){8, 8, 0};
	fb_info->var.blue = (struct fb_bitfield){0, 8, 0};

	fb_info->var.activate = FB_ACTIVATE_NOW;
	fb_info->var.height = -1;
	fb_info->var.width = -1;
	fb_info->var.vmode = FB_VMODE_NONINTERLACED;

	fb_info->fix.visual = FB_VISUAL_TRUECOLOR;
	fb_info->fix.line_length = info->page->line_length;
	fb_info->fix.smem_start = 0;
	fb_info->fix.smem_len = xenfb_mem_len;
	strcpy(fb_info->fix.id, "xen");
	fb_info->fix.type = FB_TYPE_PACKED_PIXELS;
	fb_info->fix.accel = FB_ACCEL_NONE;

	fb_info->flags = FBINFO_FLAG_DEFAULT;

	ret = fb_alloc_cmap(&fb_info->cmap, 256, 0);
	if (ret < 0) {
		framebuffer_release(fb_info);
		xenbus_dev_fatal(dev, ret, "fb_alloc_cmap");
		goto error;
	}

	fb_info->fbdefio = &xenfb_defio;
	fb_deferred_io_init(fb_info);

	ret = register_framebuffer(fb_info);
	if (ret) {
		fb_deferred_io_cleanup(fb_info);
		fb_dealloc_cmap(&fb_info->cmap);
		framebuffer_release(fb_info);
		xenbus_dev_fatal(dev, ret, "register_framebuffer");
		goto error;
	}
	info->fb_info = fb_info;

	ret = xenfb_connect_backend(dev, info);
	if (ret < 0)
		goto error;

	return 0;

 error_nomem:
	ret = -ENOMEM;
	xenbus_dev_fatal(dev, ret, "allocating device memory");
 error:
	xenfb_remove(dev);
	return ret;
}

static int xenfb_resume(struct xenbus_device *dev)
{
	struct xenfb_info *info = dev->dev.driver_data;

	xenfb_disconnect_backend(info);
	xenfb_init_shared_page(info);
	return xenfb_connect_backend(dev, info);
}

static int xenfb_remove(struct xenbus_device *dev)
{
	struct xenfb_info *info = dev->dev.driver_data;

	xenfb_disconnect_backend(info);
	if (info->fb_info) {
		fb_deferred_io_cleanup(info->fb_info);
		unregister_framebuffer(info->fb_info);
		fb_dealloc_cmap(&info->fb_info->cmap);
		framebuffer_release(info->fb_info);
	}
	free_page((unsigned long)info->page);
	vfree(info->mfns);
	vfree(info->fb);
	kfree(info);

	return 0;
}

static unsigned long vmalloc_to_mfn(void *address)
{
	return pfn_to_mfn(vmalloc_to_pfn(address));
}

static void xenfb_init_shared_page(struct xenfb_info *info)
{
	int i;

	for (i = 0; i < info->nr_pages; i++)
		info->mfns[i] = vmalloc_to_mfn(info->fb + i * PAGE_SIZE);

	info->page->pd[0] = vmalloc_to_mfn(info->mfns);
	info->page->pd[1] = 0;
	info->page->width = XENFB_WIDTH;
	info->page->height = XENFB_HEIGHT;
	info->page->depth = XENFB_DEPTH;
	info->page->line_length = (info->page->depth / 8) * info->page->width;
	info->page->mem_length = xenfb_mem_len;
	info->page->in_cons = info->page->in_prod = 0;
	info->page->out_cons = info->page->out_prod = 0;
}

static int xenfb_connect_backend(struct xenbus_device *dev,
				 struct xenfb_info *info)
{
	int ret, evtchn;
	struct xenbus_transaction xbt;

	ret = xenbus_alloc_evtchn(dev, &evtchn);
	if (ret)
		return ret;
	ret = bind_evtchn_to_irqhandler(evtchn, xenfb_event_handler,
					0, dev->devicetype, info);
	if (ret < 0) {
		xenbus_free_evtchn(dev, evtchn);
		xenbus_dev_fatal(dev, ret, "bind_evtchn_to_irqhandler");
		return ret;
	}
	info->irq = ret;

 again:
	ret = xenbus_transaction_start(&xbt);
	if (ret) {
		xenbus_dev_fatal(dev, ret, "starting transaction");
		return ret;
	}
	ret = xenbus_printf(xbt, dev->nodename, "page-ref", "%lu",
			    virt_to_mfn(info->page));
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "event-channel", "%u",
			    evtchn);
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "protocol", "%s",
			    XEN_IO_PROTO_ABI_NATIVE);
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "feature-update", "1");
	if (ret)
		goto error_xenbus;
	ret = xenbus_transaction_end(xbt, 0);
	if (ret) {
		if (ret == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, ret, "completing transaction");
		return ret;
	}

	xenbus_switch_state(dev, XenbusStateInitialised);
	return 0;

 error_xenbus:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, ret, "writing xenstore");
	return ret;
}

static void xenfb_disconnect_backend(struct xenfb_info *info)
{
	if (info->irq >= 0)
		unbind_from_irqhandler(info->irq, info);
	info->irq = -1;
}

static void xenfb_backend_changed(struct xenbus_device *dev,
				  enum xenbus_state backend_state)
{
	struct xenfb_info *info = dev->dev.driver_data;
	int val;

	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateUnknown:
	case XenbusStateClosed:
		break;

	case XenbusStateInitWait:
InitWait:
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateConnected:
		/*
		 * Work around xenbus race condition: If backend goes
		 * through InitWait to Connected fast enough, we can
		 * get Connected twice here.
		 */
		if (dev->state != XenbusStateConnected)
			goto InitWait; /* no InitWait seen yet, fudge it */

		if (xenbus_scanf(XBT_NIL, info->xbdev->otherend,
				 "request-update", "%d", &val) < 0)
			val = 0;
		if (val)
			info->update_wanted = 1;
		break;

	case XenbusStateClosing:
		xenbus_frontend_closed(dev);
		break;
	}
}

static struct xenbus_device_id xenfb_ids[] = {
	{ "vfb" },
	{ "" }
};

static struct xenbus_driver xenfb = {
	.name = "vfb",
	.owner = THIS_MODULE,
	.ids = xenfb_ids,
	.probe = xenfb_probe,
	.remove = xenfb_remove,
	.resume = xenfb_resume,
	.otherend_changed = xenfb_backend_changed,
};

static int __init xenfb_init(void)
{
	if (!is_running_on_xen())
		return -ENODEV;

	/* Nothing to do if running in dom0. */
	if (is_initial_xendomain())
		return -ENODEV;

	return xenbus_register_frontend(&xenfb);
}

static void __exit xenfb_cleanup(void)
{
	xenbus_unregister_driver(&xenfb);
}

module_init(xenfb_init);
module_exit(xenfb_cleanup);

MODULE_LICENSE("GPL");
