/*
 * All the USB notify logic
 *
 * (C) Copyright 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * notifier functions originally based on those in kernel/sys.c
 * but fixed up to not be so broken.
 *
 */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/usb.h>
#include "usb.h"


static struct notifier_block *usb_notifier_list;
static DECLARE_MUTEX(usb_notifier_lock);

static void usb_notifier_chain_register(struct notifier_block **list,
					struct notifier_block *n)
{
	down(&usb_notifier_lock);
	while (*list) {
		if (n->priority > (*list)->priority)
			break;
		list = &((*list)->next);
	}
	n->next = *list;
	*list = n;
	up(&usb_notifier_lock);
}

static void usb_notifier_chain_unregister(struct notifier_block **nl,
				   struct notifier_block *n)
{
	down(&usb_notifier_lock);
	while ((*nl)!=NULL) {
		if ((*nl)==n) {
			*nl = n->next;
			goto exit;
		}
		nl=&((*nl)->next);
	}
exit:
	up(&usb_notifier_lock);
}

static int usb_notifier_call_chain(struct notifier_block **n,
				   unsigned long val, void *v)
{
	int ret=NOTIFY_DONE;
	struct notifier_block *nb = *n;

	down(&usb_notifier_lock);
	while (nb) {
		ret = nb->notifier_call(nb,val,v);
		if (ret&NOTIFY_STOP_MASK) {
			goto exit;
		}
		nb = nb->next;
	}
exit:
	up(&usb_notifier_lock);
	return ret;
}

/**
 * usb_register_notify - register a notifier callback whenever a usb change happens
 * @nb: pointer to the notifier block for the callback events.
 *
 * These changes are either USB devices or busses being added or removed.
 */
void usb_register_notify(struct notifier_block *nb)
{
	usb_notifier_chain_register(&usb_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(usb_register_notify);

/**
 * usb_unregister_notify - unregister a notifier callback
 * @nb: pointer to the notifier block for the callback events.
 *
 * usb_register_notifier() must have been previously called for this function
 * to work properly.
 */
void usb_unregister_notify(struct notifier_block *nb)
{
	usb_notifier_chain_unregister(&usb_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(usb_unregister_notify);


void usb_notify_add_device(struct usb_device *udev)
{
	usb_notifier_call_chain(&usb_notifier_list, USB_DEVICE_ADD, udev);
}

void usb_notify_remove_device(struct usb_device *udev)
{
	usb_notifier_call_chain(&usb_notifier_list, USB_DEVICE_REMOVE, udev);
}

void usb_notify_add_bus(struct usb_bus *ubus)
{
	usb_notifier_call_chain(&usb_notifier_list, USB_BUS_ADD, ubus);
}

void usb_notify_remove_bus(struct usb_bus *ubus)
{
	usb_notifier_call_chain(&usb_notifier_list, USB_BUS_REMOVE, ubus);
}
