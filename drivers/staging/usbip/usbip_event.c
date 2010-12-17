/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include "usbip_common.h"
#include <linux/kthread.h>

static int event_handler(struct usbip_device *ud)
{
	usbip_dbg_eh("enter\n");

	/*
	 * Events are handled by only this thread.
	 */
	while (usbip_event_happened(ud)) {
		usbip_dbg_eh("pending event %lx\n", ud->event);

		/*
		 * NOTE: shutdown must come first.
		 * Shutdown the device.
		 */
		if (ud->event & USBIP_EH_SHUTDOWN) {
			ud->eh_ops.shutdown(ud);

			ud->event &= ~USBIP_EH_SHUTDOWN;
		}

		/* Reset the device. */
		if (ud->event & USBIP_EH_RESET) {
			ud->eh_ops.reset(ud);

			ud->event &= ~USBIP_EH_RESET;
		}

		/* Mark the device as unusable. */
		if (ud->event & USBIP_EH_UNUSABLE) {
			ud->eh_ops.unusable(ud);

			ud->event &= ~USBIP_EH_UNUSABLE;
		}

		/* Stop the error handler. */
		if (ud->event & USBIP_EH_BYE)
			return -1;
	}

	return 0;
}

static void event_handler_loop(struct usbip_task *ut)
{
	struct usbip_device *ud = container_of(ut, struct usbip_device, eh);

	while (1) {
		if (signal_pending(current)) {
			usbip_dbg_eh("signal catched!\n");
			break;
		}

		if (event_handler(ud) < 0)
			break;

		wait_event_interruptible(ud->eh_waitq,
					usbip_event_happened(ud));
		usbip_dbg_eh("wakeup\n");
	}
}

int usbip_start_eh(struct usbip_device *ud)
{
	struct usbip_task *eh = &ud->eh;
	struct task_struct *th;

	init_waitqueue_head(&ud->eh_waitq);
	ud->event = 0;

	usbip_task_init(eh, "usbip_eh", event_handler_loop);

	th = kthread_run(usbip_thread, (void *)eh, "usbip");
	if (IS_ERR(th)) {
		printk(KERN_WARNING
			"Unable to start control thread\n");
		return PTR_ERR(th);
	}

	wait_for_completion(&eh->thread_done);
	return 0;
}
EXPORT_SYMBOL_GPL(usbip_start_eh);

void usbip_stop_eh(struct usbip_device *ud)
{
	struct usbip_task *eh = &ud->eh;

	if (eh->thread == current)
		return; /* do not wait for myself */

	wait_for_completion(&eh->thread_done);
	usbip_dbg_eh("usbip_eh has finished\n");
}
EXPORT_SYMBOL_GPL(usbip_stop_eh);

void usbip_event_add(struct usbip_device *ud, unsigned long event)
{
	spin_lock(&ud->lock);

	ud->event |= event;

	wake_up(&ud->eh_waitq);

	spin_unlock(&ud->lock);
}
EXPORT_SYMBOL_GPL(usbip_event_add);

int usbip_event_happened(struct usbip_device *ud)
{
	int happened = 0;

	spin_lock(&ud->lock);

	if (ud->event != 0)
		happened = 1;

	spin_unlock(&ud->lock);

	return happened;
}
EXPORT_SYMBOL_GPL(usbip_event_happened);
