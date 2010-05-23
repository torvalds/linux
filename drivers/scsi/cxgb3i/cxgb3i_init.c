/* cxgb3i_init.c: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#include "cxgb3i.h"

#define DRV_MODULE_NAME         "cxgb3i"
#define DRV_MODULE_VERSION	"1.0.2"
#define DRV_MODULE_RELDATE	"Mar. 2009"

static char version[] =
	"Chelsio S3xx iSCSI Driver " DRV_MODULE_NAME
	" v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Karen Xie <kxie@chelsio.com>");
MODULE_DESCRIPTION("Chelsio S3xx iSCSI Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static void open_s3_dev(struct t3cdev *);
static void close_s3_dev(struct t3cdev *);
static void s3_event_handler(struct t3cdev *tdev, u32 event, u32 port);

static cxgb3_cpl_handler_func cxgb3i_cpl_handlers[NUM_CPL_CMDS];
static struct cxgb3_client t3c_client = {
	.name = "iscsi_cxgb3",
	.handlers = cxgb3i_cpl_handlers,
	.add = open_s3_dev,
	.remove = close_s3_dev,
	.event_handler = s3_event_handler,
};

/**
 * open_s3_dev - register with cxgb3 LLD
 * @t3dev:	cxgb3 adapter instance
 */
static void open_s3_dev(struct t3cdev *t3dev)
{
	static int vers_printed;

	if (!vers_printed) {
		printk(KERN_INFO "%s", version);
		vers_printed = 1;
	}

	cxgb3i_ddp_init(t3dev);
	cxgb3i_sdev_add(t3dev, &t3c_client);
	cxgb3i_adapter_open(t3dev);
}

/**
 * close_s3_dev - de-register with cxgb3 LLD
 * @t3dev:	cxgb3 adapter instance
 */
static void close_s3_dev(struct t3cdev *t3dev)
{
	cxgb3i_adapter_close(t3dev);
	cxgb3i_sdev_remove(t3dev);
	cxgb3i_ddp_cleanup(t3dev);
}

static void s3_event_handler(struct t3cdev *tdev, u32 event, u32 port)
{
	struct cxgb3i_adapter *snic = cxgb3i_adapter_find_by_tdev(tdev);

	cxgb3i_log_info("snic 0x%p, tdev 0x%p, event 0x%x, port 0x%x.\n",
			snic, tdev, event, port);
	if (!snic)
		return;

	switch (event) {
	case OFFLOAD_STATUS_DOWN:
		snic->flags |= CXGB3I_ADAPTER_FLAG_RESET;
		break;
	case OFFLOAD_STATUS_UP:
		snic->flags &= ~CXGB3I_ADAPTER_FLAG_RESET;
		break;
	}
}

/**
 * cxgb3i_init_module - module init entry point
 *
 * initialize any driver wide global data structures and register itself
 *	with the cxgb3 module
 */
static int __init cxgb3i_init_module(void)
{
	int err;

	err = cxgb3i_sdev_init(cxgb3i_cpl_handlers);
	if (err < 0)
		return err;

	err = cxgb3i_iscsi_init();
	if (err < 0)
		return err;

	err = cxgb3i_pdu_init();
	if (err < 0) {
		cxgb3i_iscsi_cleanup();
		return err;
	}

	cxgb3_register_client(&t3c_client);

	return 0;
}

/**
 * cxgb3i_exit_module - module cleanup/exit entry point
 *
 * go through the driver hba list and for each hba, release any resource held.
 *	and unregisters iscsi transport and the cxgb3 module
 */
static void __exit cxgb3i_exit_module(void)
{
	cxgb3_unregister_client(&t3c_client);
	cxgb3i_pdu_cleanup();
	cxgb3i_iscsi_cleanup();
	cxgb3i_sdev_cleanup();
}

module_init(cxgb3i_init_module);
module_exit(cxgb3i_exit_module);
