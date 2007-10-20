/*
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 *
 * Multipath hardware handler registration.
 */

#include "dm.h"
#include "dm-hw-handler.h"

#include <linux/slab.h>

struct hwh_internal {
	struct hw_handler_type hwht;

	struct list_head list;
	long use;
};

#define hwht_to_hwhi(__hwht) container_of((__hwht), struct hwh_internal, hwht)

static LIST_HEAD(_hw_handlers);
static DECLARE_RWSEM(_hwh_lock);

static struct hwh_internal *__find_hw_handler_type(const char *name)
{
	struct hwh_internal *hwhi;

	list_for_each_entry(hwhi, &_hw_handlers, list) {
		if (!strcmp(name, hwhi->hwht.name))
			return hwhi;
	}

	return NULL;
}

static struct hwh_internal *get_hw_handler(const char *name)
{
	struct hwh_internal *hwhi;

	down_read(&_hwh_lock);
	hwhi = __find_hw_handler_type(name);
	if (hwhi) {
		if ((hwhi->use == 0) && !try_module_get(hwhi->hwht.module))
			hwhi = NULL;
		else
			hwhi->use++;
	}
	up_read(&_hwh_lock);

	return hwhi;
}

struct hw_handler_type *dm_get_hw_handler(const char *name)
{
	struct hwh_internal *hwhi;

	if (!name)
		return NULL;

	hwhi = get_hw_handler(name);
	if (!hwhi) {
		request_module("dm-%s", name);
		hwhi = get_hw_handler(name);
	}

	return hwhi ? &hwhi->hwht : NULL;
}

void dm_put_hw_handler(struct hw_handler_type *hwht)
{
	struct hwh_internal *hwhi;

	if (!hwht)
		return;

	down_read(&_hwh_lock);
	hwhi = __find_hw_handler_type(hwht->name);
	if (!hwhi)
		goto out;

	if (--hwhi->use == 0)
		module_put(hwhi->hwht.module);

	BUG_ON(hwhi->use < 0);

      out:
	up_read(&_hwh_lock);
}

static struct hwh_internal *_alloc_hw_handler(struct hw_handler_type *hwht)
{
	struct hwh_internal *hwhi = kzalloc(sizeof(*hwhi), GFP_KERNEL);

	if (hwhi)
		hwhi->hwht = *hwht;

	return hwhi;
}

int dm_register_hw_handler(struct hw_handler_type *hwht)
{
	int r = 0;
	struct hwh_internal *hwhi = _alloc_hw_handler(hwht);

	if (!hwhi)
		return -ENOMEM;

	down_write(&_hwh_lock);

	if (__find_hw_handler_type(hwht->name)) {
		kfree(hwhi);
		r = -EEXIST;
	} else
		list_add(&hwhi->list, &_hw_handlers);

	up_write(&_hwh_lock);

	return r;
}

int dm_unregister_hw_handler(struct hw_handler_type *hwht)
{
	struct hwh_internal *hwhi;

	down_write(&_hwh_lock);

	hwhi = __find_hw_handler_type(hwht->name);
	if (!hwhi) {
		up_write(&_hwh_lock);
		return -EINVAL;
	}

	if (hwhi->use) {
		up_write(&_hwh_lock);
		return -ETXTBSY;
	}

	list_del(&hwhi->list);

	up_write(&_hwh_lock);

	kfree(hwhi);

	return 0;
}

unsigned dm_scsi_err_handler(struct hw_handler *hwh, struct bio *bio)
{
#if 0
	int sense_key, asc, ascq;

	if (bio->bi_error & BIO_SENSE) {
		/* FIXME: This is just an initial guess. */
		/* key / asc / ascq */
		sense_key = (bio->bi_error >> 16) & 0xff;
		asc = (bio->bi_error >> 8) & 0xff;
		ascq = bio->bi_error & 0xff;

		switch (sense_key) {
			/* This block as a whole comes from the device.
			 * So no point retrying on another path. */
		case 0x03:	/* Medium error */
		case 0x05:	/* Illegal request */
		case 0x07:	/* Data protect */
		case 0x08:	/* Blank check */
		case 0x0a:	/* copy aborted */
		case 0x0c:	/* obsolete - no clue ;-) */
		case 0x0d:	/* volume overflow */
		case 0x0e:	/* data miscompare */
		case 0x0f:	/* reserved - no idea either. */
			return MP_ERROR_IO;

			/* For these errors it's unclear whether they
			 * come from the device or the controller.
			 * So just lets try a different path, and if
			 * it eventually succeeds, user-space will clear
			 * the paths again... */
		case 0x02:	/* Not ready */
		case 0x04:	/* Hardware error */
		case 0x09:	/* vendor specific */
		case 0x0b:	/* Aborted command */
			return MP_FAIL_PATH;

		case 0x06:	/* Unit attention - might want to decode */
			if (asc == 0x04 && ascq == 0x01)
				/* "Unit in the process of
				 * becoming ready" */
				return 0;
			return MP_FAIL_PATH;

			/* FIXME: For Unit Not Ready we may want
			 * to have a generic pg activation
			 * feature (START_UNIT). */

			/* Should these two ever end up in the
			 * error path? I don't think so. */
		case 0x00:	/* No sense */
		case 0x01:	/* Recovered error */
			return 0;
		}
	}
#endif

	/* We got no idea how to decode the other kinds of errors ->
	 * assume generic error condition. */
	return MP_FAIL_PATH;
}

EXPORT_SYMBOL_GPL(dm_register_hw_handler);
EXPORT_SYMBOL_GPL(dm_unregister_hw_handler);
EXPORT_SYMBOL_GPL(dm_scsi_err_handler);
