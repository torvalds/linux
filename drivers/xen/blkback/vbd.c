/******************************************************************************
 * Routines for managing virtual block devices (VBDs).
 *
 * Copyright (c) 2003-2005, Keir Fraser & Steve Hand
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"

#define vbd_sz(_v)   ((_v)->bdev->bd_part ?				\
		      (_v)->bdev->bd_part->nr_sects : get_capacity((_v)->bdev->bd_disk))

unsigned long long vbd_size(struct vbd *vbd)
{
	return vbd_sz(vbd);
}

unsigned int vbd_info(struct vbd *vbd)
{
	return vbd->type | (vbd->readonly?VDISK_READONLY:0);
}

unsigned long vbd_secsize(struct vbd *vbd)
{
	return bdev_logical_block_size(vbd->bdev);
}

int vbd_create(struct blkif_st *blkif, blkif_vdev_t handle, unsigned major,
	       unsigned minor, int readonly, int cdrom)
{
	struct vbd *vbd;
	struct block_device *bdev;

	vbd = &blkif->vbd;
	vbd->handle   = handle;
	vbd->readonly = readonly;
	vbd->type     = 0;

	vbd->pdevice  = MKDEV(major, minor);

	bdev = blkdev_get_by_dev(vbd->pdevice, vbd->readonly ?
				 FMODE_READ : FMODE_WRITE, NULL);

	if (IS_ERR(bdev)) {
		DPRINTK("vbd_creat: device %08x could not be opened.\n",
			vbd->pdevice);
		return -ENOENT;
	}

	vbd->bdev = bdev;
	vbd->size = vbd_size(vbd);

	if (vbd->bdev->bd_disk == NULL) {
		DPRINTK("vbd_creat: device %08x doesn't exist.\n",
			vbd->pdevice);
		vbd_free(vbd);
		return -ENOENT;
	}

	if (vbd->bdev->bd_disk->flags & GENHD_FL_CD || cdrom)
		vbd->type |= VDISK_CDROM;
	if (vbd->bdev->bd_disk->flags & GENHD_FL_REMOVABLE)
		vbd->type |= VDISK_REMOVABLE;

	DPRINTK("Successful creation of handle=%04x (dom=%u)\n",
		handle, blkif->domid);
	return 0;
}

void vbd_free(struct vbd *vbd)
{
	if (vbd->bdev)
		blkdev_put(vbd->bdev, vbd->readonly ? FMODE_READ : FMODE_WRITE);
	vbd->bdev = NULL;
}

int vbd_translate(struct phys_req *req, struct blkif_st *blkif, int operation)
{
	struct vbd *vbd = &blkif->vbd;
	int rc = -EACCES;

	if ((operation != READ) && vbd->readonly)
		goto out;

	if (unlikely((req->sector_number + req->nr_sects) > vbd_sz(vbd)))
		goto out;

	req->dev  = vbd->pdevice;
	req->bdev = vbd->bdev;
	rc = 0;

 out:
	return rc;
}

void vbd_resize(struct blkif_st *blkif)
{
	struct vbd *vbd = &blkif->vbd;
	struct xenbus_transaction xbt;
	int err;
	struct xenbus_device *dev = blkback_xenbus(blkif->be);
	unsigned long long new_size = vbd_size(vbd);

	printk(KERN_INFO "VBD Resize: Domid: %d, Device: (%d, %d)\n",
		blkif->domid, MAJOR(vbd->pdevice), MINOR(vbd->pdevice));
	printk(KERN_INFO "VBD Resize: new size %Lu\n", new_size);
	vbd->size = new_size;
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		printk(KERN_WARNING "Error starting transaction");
		return;
	}
	err = xenbus_printf(xbt, dev->nodename, "sectors", "%Lu",
			    vbd_size(vbd));
	if (err) {
		printk(KERN_WARNING "Error writing new size");
		goto abort;
	}
	/*
	 * Write the current state; we will use this to synchronize
	 * the front-end. If the current state is "connected" the
	 * front-end will get the new size information online.
	 */
	err = xenbus_printf(xbt, dev->nodename, "state", "%d", dev->state);
	if (err) {
		printk(KERN_WARNING "Error writing the state");
		goto abort;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		printk(KERN_WARNING "Error ending transaction");
abort:
	xenbus_transaction_end(xbt, 1);
}
