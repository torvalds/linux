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

static int event_handler(struct usbip_device *ud)
{
	dbg_eh("enter\n");

	/*
	 * Events are handled by only this thread.
	 */
	while (usbip_event_happend(ud)) {
		dbg_eh("pending event %lx\n", ud->event);

		/*
		 * NOTE: shutdown must come first.
		 * Shutdown the device.
		 */
		if (ud->event & USBIP_EH_SHUTDOWN) {
			ud->eh_ops.shutdown(ud);

			ud->event &= ~USBIP_EH_SHUTDOWN;

			break;
		}

		/* Stop the error handler. */
		if (ud->event & USBIP_EH_BYE)
			return -1;

		/* Reset the device. */
		if (ud->event & USBIP_EH_RESET) {
			ud->eh_ops.reset(ud);

			ud->event &= ~USBIP_EH_RESET;

			break;
		}

		/* Mark the device as unusable. */
		if (ud->event & USBIP_EH_UNUSABLE) {
			ud->eh_ops.unusable(ud);

			ud->event &= ~USBIP_EH_UNUSABLE;

			break;
		}

		/* NOTREACHED */
		printk(KERN_ERR "%s: unknown event\n", __func__);
		return -1;
	}

	return 0;
}

static void event_handler_loop(struct usbip_task *ut)
{
	struct usbip_device *ud = container_of(ut, struct usbip_device, eh);

	while (1) {
		if (signal_pending(current)) {
			dbg_eh("signal catched!\n");
			break;
		}

		if (event_handler(ud) < 0)
			break;

		wait_event_interruptible(ud->eh_waitq, usbip_event_happend(ud));
		dbg_eh("wakeup\n");
	}
}

void usbip_start_eh(struct usbip_device *ud)
{
	struct usbip_task *eh = &ud->eh;

	init_waitqueue_head(&ud->eh_waitq);
	ud->event = 0;

	usbip_task_init(eh, "usbip_eh", event_handler_loop);

	kernel_thread(usbip_thread, (void *)eh, 0);

	wait_for_completion(&eh->thread_done);
}
EXPORT_SYMBOL_GPL(usbip_start_eh);

void usbip_stop_eh(struct usbip_device *ud)
{
	struct usbip_task *eh = &ud->eh;

	wait_for_completion(&eh->thread_done);
	dbg_eh("usbip_eh has finished\n");
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

int usbip_event_happend(struct usbip_device *ud)
{
	int happend = 0;

	spin_lock(&ud->lock);

	if (ud->event != 0)
		happend = 1;

	spin_unlock(&ud->lock);

	return happend;
}
EXPORT_SYMBOL_GPL(usbip_event_happend);
