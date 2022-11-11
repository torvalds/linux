// SPDX-License-Identifier: GPL-2.0-only
/*
 * USB Super Speed (Plus) redriver core module
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "redriver-core: " fmt

#include <linux/module.h>
#include <linux/usb/redriver.h>

static LIST_HEAD(usb_redriver_list);
static DEFINE_SPINLOCK(usb_rediver_lock);

/**
 * usb_add_redriver() - register a redriver from a specific chip driver.
 * @redriver: redriver allocated by specific chip driver.
 *
 * Return:
 *  -EINVAL - if of node not exist
 *  0 - if exist, add redriver to global list.
 */
int usb_add_redriver(struct usb_redriver *redriver)
{
	struct usb_redriver *iter;

	if (!redriver->of_node ||  redriver->bounded)
		return -EINVAL;

	spin_lock(&usb_rediver_lock);

	list_for_each_entry(iter, &usb_redriver_list, list) {
		if (iter == redriver) {
			spin_unlock(&usb_rediver_lock);
			return -EEXIST;
		}
	}

	pr_debug("add redriver %s\n", of_node_full_name(redriver->of_node));
	list_add_tail(&redriver->list, &usb_redriver_list);
	spin_unlock(&usb_rediver_lock);

	return 0;
}
EXPORT_SYMBOL(usb_add_redriver);

/**
 * usb_remove_redriver() - remove a redriver from a specific chip driver.
 * @redriver: redriver allocated by specific chip driver.
 *
 * remove redriver from global list.
 * if redriver rmmod, it is better change to default state inside it's driver,
 * no unbind operation here.
 *
 * Return:
 *  -EINVAL - redriver still used by uppper layer.
 *  0 - redriver removed.
 */
int usb_remove_redriver(struct usb_redriver *redriver)
{
	spin_lock(&usb_rediver_lock);

	if (redriver->bounded) {
		spin_unlock(&usb_rediver_lock);
		return -EINVAL;
	}

	pr_debug("remove redriver %s\n", of_node_full_name(redriver->of_node));
	list_del(&redriver->list);

	spin_unlock(&usb_rediver_lock);

	return 0;
}
EXPORT_SYMBOL(usb_remove_redriver);

/**
 * usb_get_redriver_by_phandle() - find redriver to be used.
 * @np: device node of device which use the redriver
 * @phandle_name: phandle name which refer to the redriver
 * @index: phandle index which refer to the redriver
 *
 * Return:
 *  NULL - if no phandle or redriver device tree status is disabled.
 *  ERR_PTR(-EPROBE_DEFER) - if redriver is not registered
 *  if redriver registered, return pointer of it.
 */
struct usb_redriver *usb_get_redriver_by_phandle(const struct device_node *np,
		const char *phandle_name, int index)
{
	struct usb_redriver *redriver;
	struct device_node *node;
	bool found = false;

	node = of_parse_phandle(np, phandle_name, index);
	if (!of_device_is_available(node)) {
		of_node_put(node);
		return NULL;
	}

	spin_lock(&usb_rediver_lock);
	list_for_each_entry(redriver, &usb_redriver_list, list) {
		if (redriver->of_node == node) {
			found = true;
			break;
		}
	}

	if (!found) {
		of_node_put(node);
		spin_unlock(&usb_rediver_lock);
		return ERR_PTR(-EPROBE_DEFER);
	}

	pr_debug("get redriver %s\n", of_node_full_name(redriver->of_node));
	redriver->bounded = true;

	spin_unlock(&usb_rediver_lock);

	return redriver;
}
EXPORT_SYMBOL(usb_get_redriver_by_phandle);

/**
 * usb_put_redriver() - redriver will not be used.
 * @redriver: redriver allocated by specific chip driver.
 *
 * when user module exit, unbind redriver.
 */
void usb_put_redriver(struct usb_redriver *redriver)
{
	if (!redriver)
		return;

	spin_lock(&usb_rediver_lock);
	of_node_put(redriver->of_node);
	pr_debug("put redriver %s\n", of_node_full_name(redriver->of_node));
	redriver->bounded = false;
	spin_unlock(&usb_rediver_lock);

	if (redriver->unbind)
		redriver->unbind(redriver);
}
EXPORT_SYMBOL(usb_put_redriver);

/* note: following exported symbol can be inlined in header file,
 * export here to avoid unexpected CFI(Clang Control Flow Integrity) issue.
 */
void usb_redriver_release_lanes(struct usb_redriver *ur, int ort, int num)
{
	if (ur && ur->release_usb_lanes)
		ur->release_usb_lanes(ur, ort, num);
}
EXPORT_SYMBOL(usb_redriver_release_lanes);

void usb_redriver_notify_connect(struct usb_redriver *ur, int ort)
{
	if (ur && ur->notify_connect)
		ur->notify_connect(ur, ort);
}
EXPORT_SYMBOL(usb_redriver_notify_connect);

void usb_redriver_notify_disconnect(struct usb_redriver *ur)
{
	if (ur && ur->notify_disconnect)
		ur->notify_disconnect(ur);
}
EXPORT_SYMBOL(usb_redriver_notify_disconnect);

void usb_redriver_gadget_pullup_enter(struct usb_redriver *ur,
					int is_on)
{
	if (ur && ur->gadget_pullup_enter)
		ur->gadget_pullup_enter(ur, is_on);
}
EXPORT_SYMBOL(usb_redriver_gadget_pullup_enter);

void usb_redriver_gadget_pullup_exit(struct usb_redriver *ur,
		int is_on)
{
	if (ur && ur->gadget_pullup_exit)
		ur->gadget_pullup_exit(ur, is_on);
}
EXPORT_SYMBOL(usb_redriver_gadget_pullup_exit);

void usb_redriver_host_powercycle(struct usb_redriver *ur)
{
	if (ur && ur->host_powercycle)
		ur->host_powercycle(ur);
}
EXPORT_SYMBOL(usb_redriver_host_powercycle);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB Super Speed (Plus) redriver core module");
