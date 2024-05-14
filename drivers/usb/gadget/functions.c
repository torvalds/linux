// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/err.h>

#include <linux/usb/composite.h>

static LIST_HEAD(func_list);
static DEFINE_MUTEX(func_lock);

static struct usb_function_instance *try_get_usb_function_instance(const char *name)
{
	struct usb_function_driver *fd;
	struct usb_function_instance *fi;

	fi = ERR_PTR(-ENOENT);
	mutex_lock(&func_lock);
	list_for_each_entry(fd, &func_list, list) {

		if (strcmp(name, fd->name))
			continue;

		if (!try_module_get(fd->mod)) {
			fi = ERR_PTR(-EBUSY);
			break;
		}
		fi = fd->alloc_inst();
		if (IS_ERR(fi))
			module_put(fd->mod);
		else
			fi->fd = fd;
		break;
	}
	mutex_unlock(&func_lock);
	return fi;
}

struct usb_function_instance *usb_get_function_instance(const char *name)
{
	struct usb_function_instance *fi;
	int ret;

	fi = try_get_usb_function_instance(name);
	if (!IS_ERR(fi))
		return fi;
	ret = PTR_ERR(fi);
	if (ret != -ENOENT)
		return fi;
	ret = request_module("usbfunc:%s", name);
	if (ret < 0)
		return ERR_PTR(ret);
	return try_get_usb_function_instance(name);
}
EXPORT_SYMBOL_GPL(usb_get_function_instance);

struct usb_function *usb_get_function(struct usb_function_instance *fi)
{
	struct usb_function *f;

	f = fi->fd->alloc_func(fi);
	if (IS_ERR(f))
		return f;
	f->fi = fi;
	return f;
}
EXPORT_SYMBOL_GPL(usb_get_function);

void usb_put_function_instance(struct usb_function_instance *fi)
{
	struct module *mod;

	if (!fi)
		return;

	mod = fi->fd->mod;
	fi->free_func_inst(fi);
	module_put(mod);
}
EXPORT_SYMBOL_GPL(usb_put_function_instance);

void usb_put_function(struct usb_function *f)
{
	if (!f)
		return;

	f->free_func(f);
}
EXPORT_SYMBOL_GPL(usb_put_function);

int usb_function_register(struct usb_function_driver *newf)
{
	struct usb_function_driver *fd;
	int ret;

	ret = -EEXIST;

	mutex_lock(&func_lock);
	list_for_each_entry(fd, &func_list, list) {
		if (!strcmp(fd->name, newf->name))
			goto out;
	}
	ret = 0;
	list_add_tail(&newf->list, &func_list);
out:
	mutex_unlock(&func_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_function_register);

void usb_function_unregister(struct usb_function_driver *fd)
{
	mutex_lock(&func_lock);
	list_del(&fd->list);
	mutex_unlock(&func_lock);
}
EXPORT_SYMBOL_GPL(usb_function_unregister);
