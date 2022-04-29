// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2022 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"
#include <linux/bsg-lib.h>

/**
 * mpi3mr_bsg_request - bsg request entry point
 * @job: BSG job reference
 *
 * This is driver's entry point for bsg requests
 *
 * Return: 0 on success and proper error codes on failure
 */
static int mpi3mr_bsg_request(struct bsg_job *job)
{
	return 0;
}

/**
 * mpi3mr_bsg_exit - de-registration from bsg layer
 *
 * This will be called during driver unload and all
 * bsg resources allocated during load will be freed.
 *
 * Return:Nothing
 */
void mpi3mr_bsg_exit(struct mpi3mr_ioc *mrioc)
{
	if (!mrioc->bsg_queue)
		return;

	bsg_remove_queue(mrioc->bsg_queue);
	mrioc->bsg_queue = NULL;

	device_del(mrioc->bsg_dev);
	put_device(mrioc->bsg_dev);
	kfree(mrioc->bsg_dev);
}

/**
 * mpi3mr_bsg_node_release -release bsg device node
 * @dev: bsg device node
 *
 * decrements bsg dev reference count
 *
 * Return:Nothing
 */
static void mpi3mr_bsg_node_release(struct device *dev)
{
	put_device(dev);
}

/**
 * mpi3mr_bsg_init -  registration with bsg layer
 *
 * This will be called during driver load and it will
 * register driver with bsg layer
 *
 * Return:Nothing
 */
void mpi3mr_bsg_init(struct mpi3mr_ioc *mrioc)
{
	mrioc->bsg_dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!mrioc->bsg_dev) {
		ioc_err(mrioc, "bsg device mem allocation failed\n");
		return;
	}

	device_initialize(mrioc->bsg_dev);
	dev_set_name(mrioc->bsg_dev, "mpi3mrctl%u", mrioc->id);

	if (device_add(mrioc->bsg_dev)) {
		ioc_err(mrioc, "%s: bsg device add failed\n",
		    dev_name(mrioc->bsg_dev));
		goto err_device_add;
	}

	mrioc->bsg_dev->release = mpi3mr_bsg_node_release;

	mrioc->bsg_queue = bsg_setup_queue(mrioc->bsg_dev, dev_name(mrioc->bsg_dev),
			mpi3mr_bsg_request, NULL, 0);
	if (!mrioc->bsg_queue) {
		ioc_err(mrioc, "%s: bsg registration failed\n",
		    dev_name(mrioc->bsg_dev));
		goto err_setup_queue;
	}

	blk_queue_max_segments(mrioc->bsg_queue, MPI3MR_MAX_APP_XFER_SEGMENTS);
	blk_queue_max_hw_sectors(mrioc->bsg_queue, MPI3MR_MAX_APP_XFER_SECTORS);

	return;

err_setup_queue:
	device_del(mrioc->bsg_dev);
	put_device(mrioc->bsg_dev);
err_device_add:
	kfree(mrioc->bsg_dev);
}
