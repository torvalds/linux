/**
 * \file drm_auth.c
 * IOCTLs for authentication
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Tue Feb  2 08:37:54 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"

/**
 * Find the file with the given magic number.
 *
 * \param dev DRM device.
 * \param magic magic number.
 *
 * Searches in drm_device::magiclist within all files with the same hash key
 * the one with matching magic number, while holding the drm_device::struct_mutex
 * lock.
 */
static struct drm_file *drm_find_file(struct drm_device * dev, drm_magic_t magic)
{
	struct drm_file *retval = NULL;
	struct drm_magic_entry *pt;
	struct drm_hash_item *hash;

	mutex_lock(&dev->struct_mutex);
	if (!drm_ht_find_item(&dev->magiclist, (unsigned long)magic, &hash)) {
		pt = drm_hash_entry(hash, struct drm_magic_entry, hash_item);
		retval = pt->priv;
	}
	mutex_unlock(&dev->struct_mutex);
	return retval;
}

/**
 * Adds a magic number.
 *
 * \param dev DRM device.
 * \param priv file private data.
 * \param magic magic number.
 *
 * Creates a drm_magic_entry structure and appends to the linked list
 * associated the magic number hash key in drm_device::magiclist, while holding
 * the drm_device::struct_mutex lock.
 */
static int drm_add_magic(struct drm_device * dev, struct drm_file * priv,
			 drm_magic_t magic)
{
	struct drm_magic_entry *entry;

	DRM_DEBUG("%d\n", magic);

	entry = drm_alloc(sizeof(*entry), DRM_MEM_MAGIC);
	if (!entry)
		return -ENOMEM;
	memset(entry, 0, sizeof(*entry));
	entry->priv = priv;

	entry->hash_item.key = (unsigned long)magic;
	mutex_lock(&dev->struct_mutex);
	drm_ht_insert_item(&dev->magiclist, &entry->hash_item);
	list_add_tail(&entry->head, &dev->magicfree);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/**
 * Remove a magic number.
 *
 * \param dev DRM device.
 * \param magic magic number.
 *
 * Searches and unlinks the entry in drm_device::magiclist with the magic
 * number hash key, while holding the drm_device::struct_mutex lock.
 */
static int drm_remove_magic(struct drm_device * dev, drm_magic_t magic)
{
	struct drm_magic_entry *pt;
	struct drm_hash_item *hash;

	DRM_DEBUG("%d\n", magic);

	mutex_lock(&dev->struct_mutex);
	if (drm_ht_find_item(&dev->magiclist, (unsigned long)magic, &hash)) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}
	pt = drm_hash_entry(hash, struct drm_magic_entry, hash_item);
	drm_ht_remove_item(&dev->magiclist, hash);
	list_del(&pt->head);
	mutex_unlock(&dev->struct_mutex);

	drm_free(pt, sizeof(*pt), DRM_MEM_MAGIC);

	return 0;
}

/**
 * Get a unique magic number (ioctl).
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg pointer to a resulting drm_auth structure.
 * \return zero on success, or a negative number on failure.
 *
 * If there is a magic number in drm_file::magic then use it, otherwise
 * searches an unique non-zero magic number and add it associating it with \p
 * file_priv.
 */
int drm_getmagic(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	static drm_magic_t sequence = 0;
	static DEFINE_SPINLOCK(lock);
	struct drm_auth *auth = data;

	/* Find unique magic */
	if (file_priv->magic) {
		auth->magic = file_priv->magic;
	} else {
		do {
			spin_lock(&lock);
			if (!sequence)
				++sequence;	/* reserve 0 */
			auth->magic = sequence++;
			spin_unlock(&lock);
		} while (drm_find_file(dev, auth->magic));
		file_priv->magic = auth->magic;
		drm_add_magic(dev, file_priv, auth->magic);
	}

	DRM_DEBUG("%u\n", auth->magic);

	return 0;
}

/**
 * Authenticate with a magic.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg pointer to a drm_auth structure.
 * \return zero if authentication successed, or a negative number otherwise.
 *
 * Checks if \p file_priv is associated with the magic number passed in \arg.
 */
int drm_authmagic(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_auth *auth = data;
	struct drm_file *file;

	DRM_DEBUG("%u\n", auth->magic);
	if ((file = drm_find_file(dev, auth->magic))) {
		file->authenticated = 1;
		drm_remove_magic(dev, auth->magic);
		return 0;
	}
	return -EINVAL;
}
