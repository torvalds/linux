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
 *	0.3	09/04/2006 (Anssi Hannula <anssi.hannula@gmail.com>)
 *		- updated ff support for the changes in kernel interface
 *		- added MODULE_VERSION
 *	0.2	16/10/2004 (Micah Dowty <micah@navi.cx>)
 *		- added force feedback support
 *              - added UI_SET_PHYS
 *	0.1	20/06/2002
 *		- first public version
 */
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uinput.h>
#include "../input-compat.h"

static int uinput_dev_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct uinput_device	*udev = input_get_drvdata(dev);

	udev->buff[udev->head].type = type;
	udev->buff[udev->head].code = code;
	udev->buff[udev->head].value = value;
	do_gettimeofday(&udev->buff[udev->head].time);
	udev->head = (udev->head + 1) % UINPUT_BUFFER_SIZE;

	wake_up_interruptible(&udev->waitq);

	return 0;
}

/* Atomically allocate an ID for the given request. Returns 0 on success. */
static int uinput_request_alloc_id(struct uinput_device *udev, struct uinput_request *request)
{
	int id;
	int err = -1;

	spin_lock(&udev->requests_lock);

	for (id = 0; id < UINPUT_NUM_REQUESTS; id++) {
		if (!udev->requests[id]) {
			request->id = id;
			udev->requests[id] = request;
			err = 0;
			break;
		}
	}

	spin_unlock(&udev->requests_lock);
	return err;
}

static struct uinput_request *uinput_request_find(struct uinput_device *udev, int id)
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
	wake_up(&udev->requests_waitq);

	complete(&request->done);
}

static int uinput_request_submit(struct uinput_device *udev, struct uinput_request *request)
{
	int retval;

	retval = uinput_request_reserve_slot(udev, request);
	if (retval)
		return retval;

	retval = mutex_lock_interruptible(&udev->mutex);
	if (retval)
		return retval;

	if (udev->state != UIST_CREATED) {
		retval = -ENODEV;
		goto out;
	}

	/* Tell our userspace app about this new request by queueing an input event */
	uinput_dev_event(udev->dev, EV_UINPUT, request->code, request->id);

 out:
	mutex_unlock(&udev->mutex);
	return retval;
}

/*
 * Fail all ouitstanding requests so handlers don't wait for the userspace
 * to finish processing them.
 */
static void uinput_flush_requests(struct uinput_device *udev)
{
	struct uinput_request *request;
	int i;

	spin_lock(&udev->requests_lock);

	for (i = 0; i < UINPUT_NUM_REQUESTS; i++) {
		request = udev->requests[i];
		if (request) {
			request->retval = -ENODEV;
			uinput_request_done(udev, request);
		}
	}

	spin_unlock(&udev->requests_lock);
}

static void uinput_dev_set_gain(struct input_dev *dev, u16 gain)
{
	uinput_dev_event(dev, EV_FF, FF_GAIN, gain);
}

static void uinput_dev_set_autocenter(struct input_dev *dev, u16 magnitude)
{
	uinput_dev_event(dev, EV_FF, FF_AUTOCENTER, magnitude);
}

static int uinput_dev_playback(struct input_dev *dev, int effect_id, int value)
{
	return uinput_dev_event(dev, EV_FF, effect_id, value);
}

static int uinput_dev_upload_effect(struct input_dev *dev, struct ff_effect *effect, struct ff_effect *old)
{
	struct uinput_device *udev = input_get_drvdata(dev);
	struct uinput_request request;
	int retval;

	/*
	 * uinput driver does not currently support periodic effects with
	 * custom waveform since it does not have a way to pass buffer of
	 * samples (custom_data) to userspace. If ever there is a device
	 * supporting custom waveforms we would need to define an additional
	 * ioctl (UI_UPLOAD_SAMPLES) but for now we just bail out.
	 */
	if (effect->type == FF_PERIODIC &&
			effect->u.periodic.waveform == FF_CUSTOM)
		return -EINVAL;

	request.id = -1;
	init_completion(&request.done);
	request.code = UI_FF_UPLOAD;
	request.u.upload.effect = effect;
	request.u.upload.old = old;

	retval = uinput_request_submit(udev, &request);
	if (!retval) {
		wait_for_completion(&request.done);
		retval = request.retval;
	}

	return retval;
}

static int uinput_dev_erase_effect(struct input_dev *dev, int effect_id)
{
	struct uinput_device *udev = input_get_drvdata(dev);
	struct uinput_request request;
	int retval;

	if (!test_bit(EV_FF, dev->evbit))
		return -ENOSYS;

	request.id = -1;
	init_completion(&request.done);
	request.code = UI_FF_ERASE;
	request.u.effect_id = effect_id;

	retval = uinput_request_submit(udev, &request);
	if (!retval) {
		wait_for_completion(&request.done);
		retval = request.retval;
	}

	return retval;
}

static void uinput_destroy_device(struct uinput_device *udev)
{
	const char *name, *phys;
	struct input_dev *dev = udev->dev;
	enum uinput_state old_state = udev->state;

	udev->state = UIST_NEW_DEVICE;

	if (dev) {
		name = dev->name;
		phys = dev->phys;
		if (old_state == UIST_CREATED) {
			uinput_flush_requests(udev);
			input_unregister_device(dev);
		} else {
			input_free_device(dev);
		}
		kfree(name);
		kfree(phys);
		udev->dev = NULL;
	}
}

static int uinput_create_device(struct uinput_device *udev)
{
	struct input_dev *dev = udev->dev;
	int error;

	if (udev->state != UIST_SETUP_COMPLETE) {
		printk(KERN_DEBUG "%s: write device info first\n", UINPUT_NAME);
		return -EINVAL;
	}

	if (udev->ff_effects_max) {
		error = input_ff_create(dev, udev->ff_effects_max);
		if (error)
			goto fail1;

		dev->ff->upload = uinput_dev_upload_effect;
		dev->ff->erase = uinput_dev_erase_effect;
		dev->ff->playback = uinput_dev_playback;
		dev->ff->set_gain = uinput_dev_set_gain;
		dev->ff->set_autocenter = uinput_dev_set_autocenter;
	}

	error = input_register_device(udev->dev);
	if (error)
		goto fail2;

	udev->state = UIST_CREATED;

	return 0;

 fail2:	input_ff_destroy(dev);
 fail1: uinput_destroy_device(udev);
	return error;
}

static int uinput_open(struct inode *inode, struct file *file)
{
	struct uinput_device *newdev;

	newdev = kzalloc(sizeof(struct uinput_device), GFP_KERNEL);
	if (!newdev)
		return -ENOMEM;

	mutex_init(&newdev->mutex);
	spin_lock_init(&newdev->requests_lock);
	init_waitqueue_head(&newdev->requests_waitq);
	init_waitqueue_head(&newdev->waitq);
	newdev->state = UIST_NEW_DEVICE;

	file->private_data = newdev;
	nonseekable_open(inode, file);

	return 0;
}

static int uinput_validate_absbits(struct input_dev *dev)
{
	unsigned int cnt;
	int retval = 0;

	for (cnt = 0; cnt < ABS_CNT; cnt++) {
		if (!test_bit(cnt, dev->absbit))
			continue;

		if (input_abs_get_max(dev, cnt) <= input_abs_get_min(dev, cnt)) {
			printk(KERN_DEBUG
				"%s: invalid abs[%02x] min:%d max:%d\n",
				UINPUT_NAME, cnt,
				input_abs_get_min(dev, cnt),
				input_abs_get_max(dev, cnt));
			retval = -EINVAL;
			break;
		}

		if (input_abs_get_flat(dev, cnt) >
		    input_abs_get_max(dev, cnt) - input_abs_get_min(dev, cnt)) {
			printk(KERN_DEBUG
				"%s: abs_flat #%02x out of range: %d "
				"(min:%d/max:%d)\n",
				UINPUT_NAME, cnt,
				input_abs_get_flat(dev, cnt),
				input_abs_get_min(dev, cnt),
				input_abs_get_max(dev, cnt));
			retval = -EINVAL;
			break;
		}
	}
	return retval;
}

static int uinput_allocate_device(struct uinput_device *udev)
{
	udev->dev = input_allocate_device();
	if (!udev->dev)
		return -ENOMEM;

	udev->dev->event = uinput_dev_event;
	input_set_drvdata(udev->dev, udev);

	return 0;
}

static int uinput_setup_device(struct uinput_device *udev, const char __user *buffer, size_t count)
{
	struct uinput_user_dev	*user_dev;
	struct input_dev	*dev;
	char			*name;
	int			i, size;
	int			retval;

	if (count != sizeof(struct uinput_user_dev))
		return -EINVAL;

	if (!udev->dev) {
		retval = uinput_allocate_device(udev);
		if (retval)
			return retval;
	}

	dev = udev->dev;

	user_dev = kmalloc(sizeof(struct uinput_user_dev), GFP_KERNEL);
	if (!user_dev)
		return -ENOMEM;

	if (copy_from_user(user_dev, buffer, sizeof(struct uinput_user_dev))) {
		retval = -EFAULT;
		goto exit;
	}

	udev->ff_effects_max = user_dev->ff_effects_max;

	size = strnlen(user_dev->name, UINPUT_MAX_NAME_SIZE) + 1;
	if (!size) {
		retval = -EINVAL;
		goto exit;
	}

	kfree(dev->name);
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

	for (i = 0; i < ABS_CNT; i++) {
		input_abs_set_max(dev, i, user_dev->absmax[i]);
		input_abs_set_min(dev, i, user_dev->absmin[i]);
		input_abs_set_fuzz(dev, i, user_dev->absfuzz[i]);
		input_abs_set_flat(dev, i, user_dev->absflat[i]);
	}

	/* check if absmin/absmax/absfuzz/absflat are filled as
	 * told in Documentation/input/input-programming.txt */
	if (test_bit(EV_ABS, dev->evbit)) {
		retval = uinput_validate_absbits(dev);
		if (retval < 0)
			goto exit;
		if (test_bit(ABS_MT_SLOT, dev->absbit)) {
			int nslot = input_abs_get_max(dev, ABS_MT_SLOT) + 1;
			input_mt_create_slots(dev, nslot);
			input_set_events_per_packet(dev, 6 * nslot);
		} else if (test_bit(ABS_MT_POSITION_X, dev->absbit)) {
			input_set_events_per_packet(dev, 60);
		}
	}

	udev->state = UIST_SETUP_COMPLETE;
	retval = count;

 exit:
	kfree(user_dev);
	return retval;
}

static inline ssize_t uinput_inject_event(struct uinput_device *udev, const char __user *buffer, size_t count)
{
	struct input_event ev;

	if (count < input_event_size())
		return -EINVAL;

	if (input_event_from_user(buffer, &ev))
		return -EFAULT;

	input_event(udev->dev, ev.type, ev.code, ev.value);

	return input_event_size();
}

static ssize_t uinput_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct uinput_device *udev = file->private_data;
	int retval;

	retval = mutex_lock_interruptible(&udev->mutex);
	if (retval)
		return retval;

	retval = udev->state == UIST_CREATED ?
			uinput_inject_event(udev, buffer, count) :
			uinput_setup_device(udev, buffer, count);

	mutex_unlock(&udev->mutex);

	return retval;
}

static ssize_t uinput_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct uinput_device *udev = file->private_data;
	int retval = 0;

	if (udev->state != UIST_CREATED)
		return -ENODEV;

	if (udev->head == udev->tail && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(udev->waitq,
			udev->head != udev->tail || udev->state != UIST_CREATED);
	if (retval)
		return retval;

	retval = mutex_lock_interruptible(&udev->mutex);
	if (retval)
		return retval;

	if (udev->state != UIST_CREATED) {
		retval = -ENODEV;
		goto out;
	}

	while (udev->head != udev->tail && retval + input_event_size() <= count) {
		if (input_event_to_user(buffer + retval, &udev->buff[udev->tail])) {
			retval = -EFAULT;
			goto out;
		}
		udev->tail = (udev->tail + 1) % UINPUT_BUFFER_SIZE;
		retval += input_event_size();
	}

 out:
	mutex_unlock(&udev->mutex);

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

static int uinput_release(struct inode *inode, struct file *file)
{
	struct uinput_device *udev = file->private_data;

	uinput_destroy_device(udev);
	kfree(udev);

	return 0;
}

#ifdef CONFIG_COMPAT
struct uinput_ff_upload_compat {
	int			request_id;
	int			retval;
	struct ff_effect_compat	effect;
	struct ff_effect_compat	old;
};

static int uinput_ff_upload_to_user(char __user *buffer,
				    const struct uinput_ff_upload *ff_up)
{
	if (INPUT_COMPAT_TEST) {
		struct uinput_ff_upload_compat ff_up_compat;

		ff_up_compat.request_id = ff_up->request_id;
		ff_up_compat.retval = ff_up->retval;
		/*
		 * It so happens that the pointer that gives us the trouble
		 * is the last field in the structure. Since we don't support
		 * custom waveforms in uinput anyway we can just copy the whole
		 * thing (to the compat size) and ignore the pointer.
		 */
		memcpy(&ff_up_compat.effect, &ff_up->effect,
			sizeof(struct ff_effect_compat));
		memcpy(&ff_up_compat.old, &ff_up->old,
			sizeof(struct ff_effect_compat));

		if (copy_to_user(buffer, &ff_up_compat,
				 sizeof(struct uinput_ff_upload_compat)))
			return -EFAULT;
	} else {
		if (copy_to_user(buffer, ff_up,
				 sizeof(struct uinput_ff_upload)))
			return -EFAULT;
	}

	return 0;
}

static int uinput_ff_upload_from_user(const char __user *buffer,
				      struct uinput_ff_upload *ff_up)
{
	if (INPUT_COMPAT_TEST) {
		struct uinput_ff_upload_compat ff_up_compat;

		if (copy_from_user(&ff_up_compat, buffer,
				   sizeof(struct uinput_ff_upload_compat)))
			return -EFAULT;

		ff_up->request_id = ff_up_compat.request_id;
		ff_up->retval = ff_up_compat.retval;
		memcpy(&ff_up->effect, &ff_up_compat.effect,
			sizeof(struct ff_effect_compat));
		memcpy(&ff_up->old, &ff_up_compat.old,
			sizeof(struct ff_effect_compat));

	} else {
		if (copy_from_user(ff_up, buffer,
				   sizeof(struct uinput_ff_upload)))
			return -EFAULT;
	}

	return 0;
}

#else

static int uinput_ff_upload_to_user(char __user *buffer,
				    const struct uinput_ff_upload *ff_up)
{
	if (copy_to_user(buffer, ff_up, sizeof(struct uinput_ff_upload)))
		return -EFAULT;

	return 0;
}

static int uinput_ff_upload_from_user(const char __user *buffer,
				      struct uinput_ff_upload *ff_up)
{
	if (copy_from_user(ff_up, buffer, sizeof(struct uinput_ff_upload)))
		return -EFAULT;

	return 0;
}

#endif

#define uinput_set_bit(_arg, _bit, _max)		\
({							\
	int __ret = 0;					\
	if (udev->state == UIST_CREATED)		\
		__ret =  -EINVAL;			\
	else if ((_arg) > (_max))			\
		__ret = -EINVAL;			\
	else set_bit((_arg), udev->dev->_bit);		\
	__ret;						\
})

static long uinput_ioctl_handler(struct file *file, unsigned int cmd,
				 unsigned long arg, void __user *p)
{
	int			retval;
	struct uinput_device	*udev = file->private_data;
	struct uinput_ff_upload ff_up;
	struct uinput_ff_erase  ff_erase;
	struct uinput_request   *req;
	int                     length;
	char			*phys;

	retval = mutex_lock_interruptible(&udev->mutex);
	if (retval)
		return retval;

	if (!udev->dev) {
		retval = uinput_allocate_device(udev);
		if (retval)
			goto out;
	}

	switch (cmd) {
		case UI_DEV_CREATE:
			retval = uinput_create_device(udev);
			break;

		case UI_DEV_DESTROY:
			uinput_destroy_device(udev);
			break;

		case UI_SET_EVBIT:
			retval = uinput_set_bit(arg, evbit, EV_MAX);
			break;

		case UI_SET_KEYBIT:
			retval = uinput_set_bit(arg, keybit, KEY_MAX);
			break;

		case UI_SET_RELBIT:
			retval = uinput_set_bit(arg, relbit, REL_MAX);
			break;

		case UI_SET_ABSBIT:
			retval = uinput_set_bit(arg, absbit, ABS_MAX);
			break;

		case UI_SET_MSCBIT:
			retval = uinput_set_bit(arg, mscbit, MSC_MAX);
			break;

		case UI_SET_LEDBIT:
			retval = uinput_set_bit(arg, ledbit, LED_MAX);
			break;

		case UI_SET_SNDBIT:
			retval = uinput_set_bit(arg, sndbit, SND_MAX);
			break;

		case UI_SET_FFBIT:
			retval = uinput_set_bit(arg, ffbit, FF_MAX);
			break;

		case UI_SET_SWBIT:
			retval = uinput_set_bit(arg, swbit, SW_MAX);
			break;

		case UI_SET_PHYS:
			if (udev->state == UIST_CREATED) {
				retval = -EINVAL;
				goto out;
			}
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
			retval = uinput_ff_upload_from_user(p, &ff_up);
			if (retval)
				break;

			req = uinput_request_find(udev, ff_up.request_id);
			if (!req || req->code != UI_FF_UPLOAD || !req->u.upload.effect) {
				retval = -EINVAL;
				break;
			}

			ff_up.retval = 0;
			ff_up.effect = *req->u.upload.effect;
			if (req->u.upload.old)
				ff_up.old = *req->u.upload.old;
			else
				memset(&ff_up.old, 0, sizeof(struct ff_effect));

			retval = uinput_ff_upload_to_user(p, &ff_up);
			break;

		case UI_BEGIN_FF_ERASE:
			if (copy_from_user(&ff_erase, p, sizeof(ff_erase))) {
				retval = -EFAULT;
				break;
			}

			req = uinput_request_find(udev, ff_erase.request_id);
			if (!req || req->code != UI_FF_ERASE) {
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
			retval = uinput_ff_upload_from_user(p, &ff_up);
			if (retval)
				break;

			req = uinput_request_find(udev, ff_up.request_id);
			if (!req || req->code != UI_FF_UPLOAD ||
			    !req->u.upload.effect) {
				retval = -EINVAL;
				break;
			}

			req->retval = ff_up.retval;
			uinput_request_done(udev, req);
			break;

		case UI_END_FF_ERASE:
			if (copy_from_user(&ff_erase, p, sizeof(ff_erase))) {
				retval = -EFAULT;
				break;
			}

			req = uinput_request_find(udev, ff_erase.request_id);
			if (!req || req->code != UI_FF_ERASE) {
				retval = -EINVAL;
				break;
			}

			req->retval = ff_erase.retval;
			uinput_request_done(udev, req);
			break;

		default:
			retval = -EINVAL;
	}

 out:
	mutex_unlock(&udev->mutex);
	return retval;
}

static long uinput_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return uinput_ioctl_handler(file, cmd, arg, (void __user *)arg);
}

#ifdef CONFIG_COMPAT
static long uinput_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return uinput_ioctl_handler(file, cmd, arg, compat_ptr(arg));
}
#endif

static const struct file_operations uinput_fops = {
	.owner		= THIS_MODULE,
	.open		= uinput_open,
	.release	= uinput_release,
	.read		= uinput_read,
	.write		= uinput_write,
	.poll		= uinput_poll,
	.unlocked_ioctl	= uinput_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= uinput_compat_ioctl,
#endif
};

static struct miscdevice uinput_misc = {
	.fops		= &uinput_fops,
	.minor		= UINPUT_MINOR,
	.name		= UINPUT_NAME,
};
MODULE_ALIAS_MISCDEV(UINPUT_MINOR);
MODULE_ALIAS("devname:" UINPUT_NAME);

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
MODULE_VERSION("0.3");

module_init(uinput_init);
module_exit(uinput_exit);

