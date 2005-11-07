/*
 *  User level driver support for input subsystem
 *
 * Heavily based on evdev.c by Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Aristeu Sergio Rozanski Filho <aris@cathedrallabs.org>
 *
 * Changes/Revisions:
 *	0.2	16/10/2004 (Micah Dowty <micah@navi.cx>)
 *		- added force feedback support
 *              - added UI_SET_PHYS
 *	0.1	20/06/2002
 *		- first public version
 */
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uinput.h>

static int uinput_dev_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct uinput_device	*udev;

	udev = dev->private;

	udev->buff[udev->head].type = type;
	udev->buff[udev->head].code = code;
	udev->buff[udev->head].value = value;
	do_gettimeofday(&udev->buff[udev->head].time);
	udev->head = (udev->head + 1) % UINPUT_BUFFER_SIZE;

	wake_up_interruptible(&udev->waitq);

	return 0;
}

static int uinput_request_alloc_id(struct uinput_device *udev, struct uinput_request *request)
{
	/* Atomically allocate an ID for the given request. Returns 0 on success. */
	int id;
	int err = -1;

	spin_lock(&udev->requests_lock);

	for (id = 0; id < UINPUT_NUM_REQUESTS; id++)
		if (!udev->requests[id]) {
			request->id = id;
			udev->requests[id] = request;
			err = 0;
			break;
		}

	spin_unlock(&udev->requests_lock);
	return err;
}

static struct uinput_request* uinput_request_find(struct uinput_device *udev, int id)
{
	/* Find an input request, by ID. Returns NULL if the ID isn't valid. */
	if (id >= UINPUT_NUM_REQUESTS || id < 0)
		return NULL;
	return udev->requests[id];
}

static inline int uinput_request_reserve_slot(struct uinput_device *udev, struct uinput_request *request)
{
	/* Allocate slot. If none are available right away, wait. */
	return wait_event_interruptible(udev->requests_waitq,
					!uinput_request_alloc_id(udev, request));
}

static void uinput_request_done(struct uinput_device *udev, struct uinput_request *request)
{
	/* Mark slot as available */
	udev->requests[request->id] = NULL;
	wake_up_interruptible(&udev->requests_waitq);

	complete(&request->done);
}

static int uinput_request_submit(struct input_dev *dev, struct uinput_request *request)
{
	int retval;

	/* Tell our userspace app about this new request by queueing an input event */
	uinput_dev_event(dev, EV_UINPUT, request->code, request->id);

	/* Wait for the request to complete */
	retval = wait_for_completion_interruptible(&request->done);
	if (!retval)
		retval = request->retval;

	return retval;
}

static int uinput_dev_upload_effect(struct input_dev *dev, struct ff_effect *effect)
{
	struct uinput_request request;
	int retval;

	if (!test_bit(EV_FF, dev->evbit))
		return -ENOSYS;

	request.id = -1;
	init_completion(&request.done);
	request.code = UI_FF_UPLOAD;
	request.u.effect = effect;

	retval = uinput_request_reserve_slot(dev->private, &request);
	if (!retval)
		retval = uinput_request_submit(dev, &request);

	return retval;
}

static int uinput_dev_erase_effect(struct input_dev *dev, int effect_id)
{
	struct uinput_request request;
	int retval;

	if (!test_bit(EV_FF, dev->evbit))
		return -ENOSYS;

	request.id = -1;
	init_completion(&request.done);
	request.code = UI_FF_ERASE;
	request.u.effect_id = effect_id;

	retval = uinput_request_reserve_slot(dev->private, &request);
	if (!retval)
		retval = uinput_request_submit(dev, &request);

	return retval;
}

static int uinput_create_device(struct uinput_device *udev)
{
	if (!udev->dev->name) {
		printk(KERN_DEBUG "%s: write device info first\n", UINPUT_NAME);
		return -EINVAL;
	}

	udev->dev->event = uinput_dev_event;
	udev->dev->upload_effect = uinput_dev_upload_effect;
	udev->dev->erase_effect = uinput_dev_erase_effect;
	udev->dev->private = udev;

	init_waitqueue_head(&udev->waitq);

	input_register_device(udev->dev);

	set_bit(UIST_CREATED, &udev->state);

	return 0;
}

static int uinput_destroy_device(struct uinput_device *udev)
{
	if (!test_bit(UIST_CREATED, &udev->state)) {
		printk(KERN_WARNING "%s: create the device first\n", UINPUT_NAME);
		return -EINVAL;
	}

	input_unregister_device(udev->dev);

	clear_bit(UIST_CREATED, &udev->state);

	return 0;
}

static int uinput_open(struct inode *inode, struct file *file)
{
	struct uinput_device	*newdev;
	struct input_dev	*newinput;

	newdev = kmalloc(sizeof(struct uinput_device), GFP_KERNEL);
	if (!newdev)
		goto error;
	memset(newdev, 0, sizeof(struct uinput_device));
	spin_lock_init(&newdev->requests_lock);
	init_waitqueue_head(&newdev->requests_waitq);

	newinput = kmalloc(sizeof(struct input_dev), GFP_KERNEL);
	if (!newinput)
		goto cleanup;
	memset(newinput, 0, sizeof(struct input_dev));

	newdev->dev = newinput;

	file->private_data = newdev;

	return 0;
cleanup:
	kfree(newdev);
error:
	return -ENOMEM;
}

static int uinput_validate_absbits(struct input_dev *dev)
{
	unsigned int cnt;
	int retval = 0;

	for (cnt = 0; cnt < ABS_MAX + 1; cnt++) {
		if (!test_bit(cnt, dev->absbit))
			continue;

		if ((dev->absmax[cnt] <= dev->absmin[cnt])) {
			printk(KERN_DEBUG
				"%s: invalid abs[%02x] min:%d max:%d\n",
				UINPUT_NAME, cnt,
				dev->absmin[cnt], dev->absmax[cnt]);
			retval = -EINVAL;
			break;
		}

		if (dev->absflat[cnt] > (dev->absmax[cnt] - dev->absmin[cnt])) {
			printk(KERN_DEBUG
				"%s: absflat[%02x] out of range: %d "
				"(min:%d/max:%d)\n",
				UINPUT_NAME, cnt, dev->absflat[cnt],
				dev->absmin[cnt], dev->absmax[cnt]);
			retval = -EINVAL;
			break;
		}
	}
	return retval;
}

static int uinput_alloc_device(struct file *file, const char __user *buffer, size_t count)
{
	struct uinput_user_dev	*user_dev;
	struct input_dev	*dev;
	struct uinput_device	*udev;
	char			*name;
	int			size;
	int			retval;

	retval = count;

	udev = file->private_data;
	dev = udev->dev;

	user_dev = kmalloc(sizeof(struct uinput_user_dev), GFP_KERNEL);
	if (!user_dev) {
		retval = -ENOMEM;
		goto exit;
	}

	if (copy_from_user(user_dev, buffer, sizeof(struct uinput_user_dev))) {
		retval = -EFAULT;
		goto exit;
	}

	kfree(dev->name);

	size = strnlen(user_dev->name, UINPUT_MAX_NAME_SIZE) + 1;
	dev->name = name = kmalloc(size, GFP_KERNEL);
	if (!name) {
		retval = -ENOMEM;
		goto exit;
	}
	strlcpy(name, user_dev->name, size);

	dev->id.bustype	= user_dev->id.bustype;
	dev->id.vendor	= user_dev->id.vendor;
	dev->id.product	= user_dev->id.product;
	dev->id.version	= user_dev->id.version;
	dev->ff_effects_max = user_dev->ff_effects_max;

	size = sizeof(int) * (ABS_MAX + 1);
	memcpy(dev->absmax, user_dev->absmax, size);
	memcpy(dev->absmin, user_dev->absmin, size);
	memcpy(dev->absfuzz, user_dev->absfuzz, size);
	memcpy(dev->absflat, user_dev->absflat, size);

	/* check if absmin/absmax/absfuzz/absflat are filled as
	 * told in Documentation/input/input-programming.txt */
	if (test_bit(EV_ABS, dev->evbit)) {
		int err = uinput_validate_absbits(dev);
		if (err < 0) {
			retval = err;
			kfree(dev->name);
		}
	}

exit:
	kfree(user_dev);
	return retval;
}

static ssize_t uinput_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct uinput_device *udev = file->private_data;

	if (test_bit(UIST_CREATED, &udev->state)) {
		struct input_event	ev;

		if (copy_from_user(&ev, buffer, sizeof(struct input_event)))
			return -EFAULT;
		input_event(udev->dev, ev.type, ev.code, ev.value);
	} else
		count = uinput_alloc_device(file, buffer, count);

	return count;
}

static ssize_t uinput_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct uinput_device *udev = file->private_data;
	int retval = 0;

	if (!test_bit(UIST_CREATED, &udev->state))
		return -ENODEV;

	if (udev->head == udev->tail && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(udev->waitq,
			udev->head != udev->tail || !test_bit(UIST_CREATED, &udev->state));
	if (retval)
		return retval;

	if (!test_bit(UIST_CREATED, &udev->state))
		return -ENODEV;

	while ((udev->head != udev->tail) &&
	    (retval + sizeof(struct input_event) <= count)) {
		if (copy_to_user(buffer + retval, &udev->buff[udev->tail], sizeof(struct input_event)))
			return -EFAULT;
		udev->tail = (udev->tail + 1) % UINPUT_BUFFER_SIZE;
		retval += sizeof(struct input_event);
	}

	return retval;
}

static unsigned int uinput_poll(struct file *file, poll_table *wait)
{
	struct uinput_device *udev = file->private_data;

	poll_wait(file, &udev->waitq, wait);

	if (udev->head != udev->tail)
		return POLLIN | POLLRDNORM;

	return 0;
}

static int uinput_burn_device(struct uinput_device *udev)
{
	if (test_bit(UIST_CREATED, &udev->state))
		uinput_destroy_device(udev);

	kfree(udev->dev->name);
	kfree(udev->dev->phys);
	kfree(udev->dev);
	kfree(udev);

	return 0;
}

static int uinput_close(struct inode *inode, struct file *file)
{
	uinput_burn_device(file->private_data);
	return 0;
}

static int uinput_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int			retval = 0;
	struct uinput_device	*udev;
	void __user             *p = (void __user *)arg;
	struct uinput_ff_upload ff_up;
	struct uinput_ff_erase  ff_erase;
	struct uinput_request   *req;
	int                     length;
	char			*phys;

	udev = file->private_data;

	/* device attributes can not be changed after the device is created */
	switch (cmd) {
		case UI_SET_EVBIT:
		case UI_SET_KEYBIT:
		case UI_SET_RELBIT:
		case UI_SET_ABSBIT:
		case UI_SET_MSCBIT:
		case UI_SET_LEDBIT:
		case UI_SET_SNDBIT:
		case UI_SET_FFBIT:
		case UI_SET_PHYS:
			if (test_bit(UIST_CREATED, &udev->state))
				return -EINVAL;
	}

	switch (cmd) {
		case UI_DEV_CREATE:
			retval = uinput_create_device(udev);
			break;

		case UI_DEV_DESTROY:
			retval = uinput_destroy_device(udev);
			break;

		case UI_SET_EVBIT:
			if (arg > EV_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->evbit);
			break;

		case UI_SET_KEYBIT:
			if (arg > KEY_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->keybit);
			break;

		case UI_SET_RELBIT:
			if (arg > REL_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->relbit);
			break;

		case UI_SET_ABSBIT:
			if (arg > ABS_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->absbit);
			break;

		case UI_SET_MSCBIT:
			if (arg > MSC_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->mscbit);
			break;

		case UI_SET_LEDBIT:
			if (arg > LED_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->ledbit);
			break;

		case UI_SET_SNDBIT:
			if (arg > SND_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->sndbit);
			break;

		case UI_SET_FFBIT:
			if (arg > FF_MAX) {
				retval = -EINVAL;
				break;
			}
			set_bit(arg, udev->dev->ffbit);
			break;

		case UI_SET_PHYS:
			length = strnlen_user(p, 1024);
			if (length <= 0) {
				retval = -EFAULT;
				break;
			}
			kfree(udev->dev->phys);
			udev->dev->phys = phys = kmalloc(length, GFP_KERNEL);
			if (!phys) {
				retval = -ENOMEM;
				break;
			}
			if (copy_from_user(phys, p, length)) {
				udev->dev->phys = NULL;
				kfree(phys);
				retval = -EFAULT;
				break;
			}
			phys[length - 1] = '\0';
			break;

		case UI_BEGIN_FF_UPLOAD:
			if (copy_from_user(&ff_up, p, sizeof(ff_up))) {
				retval = -EFAULT;
				break;
			}
			req = uinput_request_find(udev, ff_up.request_id);
			if (!(req && req->code == UI_FF_UPLOAD && req->u.effect)) {
				retval = -EINVAL;
				break;
			}
			ff_up.retval = 0;
			memcpy(&ff_up.effect, req->u.effect, sizeof(struct ff_effect));
			if (copy_to_user(p, &ff_up, sizeof(ff_up))) {
				retval = -EFAULT;
				break;
			}
			break;

		case UI_BEGIN_FF_ERASE:
			if (copy_from_user(&ff_erase, p, sizeof(ff_erase))) {
				retval = -EFAULT;
				break;
			}
			req = uinput_request_find(udev, ff_erase.request_id);
			if (!(req && req->code == UI_FF_ERASE)) {
				retval = -EINVAL;
				break;
			}
			ff_erase.retval = 0;
			ff_erase.effect_id = req->u.effect_id;
			if (copy_to_user(p, &ff_erase, sizeof(ff_erase))) {
				retval = -EFAULT;
				break;
			}
			break;

		case UI_END_FF_UPLOAD:
			if (copy_from_user(&ff_up, p, sizeof(ff_up))) {
				retval = -EFAULT;
				break;
			}
			req = uinput_request_find(udev, ff_up.request_id);
			if (!(req && req->code == UI_FF_UPLOAD && req->u.effect)) {
				retval = -EINVAL;
				break;
			}
			req->retval = ff_up.retval;
			memcpy(req->u.effect, &ff_up.effect, sizeof(struct ff_effect));
			uinput_request_done(udev, req);
			break;

		case UI_END_FF_ERASE:
			if (copy_from_user(&ff_erase, p, sizeof(ff_erase))) {
				retval = -EFAULT;
				break;
			}
			req = uinput_request_find(udev, ff_erase.request_id);
			if (!(req && req->code == UI_FF_ERASE)) {
				retval = -EINVAL;
				break;
			}
			req->retval = ff_erase.retval;
			uinput_request_done(udev, req);
			break;

		default:
			retval = -EINVAL;
	}
	return retval;
}

static struct file_operations uinput_fops = {
	.owner =	THIS_MODULE,
	.open =		uinput_open,
	.release =	uinput_close,
	.read =		uinput_read,
	.write =	uinput_write,
	.poll =		uinput_poll,
	.ioctl =	uinput_ioctl,
};

static struct miscdevice uinput_misc = {
	.fops =		&uinput_fops,
	.minor =	UINPUT_MINOR,
	.name =		UINPUT_NAME,
};

static int __init uinput_init(void)
{
	return misc_register(&uinput_misc);
}

static void __exit uinput_exit(void)
{
	misc_deregister(&uinput_misc);
}

MODULE_AUTHOR("Aristeu Sergio Rozanski Filho");
MODULE_DESCRIPTION("User level driver support for input subsystem");
MODULE_LICENSE("GPL");

module_init(uinput_init);
module_exit(uinput_exit);

