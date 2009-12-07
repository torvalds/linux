/* bnx2i.c: Broadcom NetXtreme II iSCSI driver.
 *
 * Copyright (c) 2006 - 2009 Broadcom Corporation
 * Copyright (c) 2007, 2008 Red Hat, Inc.  All rights reserved.
 * Copyright (c) 2007, 2008 Mike Christie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 */

#include "bnx2i.h"

static struct list_head adapter_list = LIST_HEAD_INIT(adapter_list);
static u32 adapter_count;

#define DRV_MODULE_NAME		"bnx2i"
#define DRV_MODULE_VERSION	"2.0.1e"
#define DRV_MODULE_RELDATE	"June 22, 2009"

static char version[] __devinitdata =
		"Broadcom NetXtreme II iSCSI Driver " DRV_MODULE_NAME \
		" v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";


MODULE_AUTHOR("Anil Veerabhadrappa <anilgv@broadcom.com>");
MODULE_DESCRIPTION("Broadcom NetXtreme II BCM5706/5708/5709 iSCSI Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static DEFINE_MUTEX(bnx2i_dev_lock);

unsigned int event_coal_div = 1;
module_param(event_coal_div, int, 0664);
MODULE_PARM_DESC(event_coal_div, "Event Coalescing Divide Factor");

unsigned int en_tcp_dack = 1;
module_param(en_tcp_dack, int, 0664);
MODULE_PARM_DESC(en_tcp_dack, "Enable TCP Delayed ACK");

unsigned int error_mask1 = 0x00;
module_param(error_mask1, int, 0664);
MODULE_PARM_DESC(error_mask1, "Config FW iSCSI Error Mask #1");

unsigned int error_mask2 = 0x00;
module_param(error_mask2, int, 0664);
MODULE_PARM_DESC(error_mask2, "Config FW iSCSI Error Mask #2");

unsigned int sq_size;
module_param(sq_size, int, 0664);
MODULE_PARM_DESC(sq_size, "Configure SQ size");

unsigned int rq_size = BNX2I_RQ_WQES_DEFAULT;
module_param(rq_size, int, 0664);
MODULE_PARM_DESC(rq_size, "Configure RQ size");

u64 iscsi_error_mask = 0x00;

static void bnx2i_unreg_one_device(struct bnx2i_hba *hba) ;


/**
 * bnx2i_identify_device - identifies NetXtreme II device type
 * @hba: 		Adapter structure pointer
 *
 * This function identifies the NX2 device type and sets appropriate
 *	queue mailbox register access method, 5709 requires driver to
 *	access MBOX regs using *bin* mode
 */
void bnx2i_identify_device(struct bnx2i_hba *hba)
{
	hba->cnic_dev_type = 0;
	if ((hba->pci_did == PCI_DEVICE_ID_NX2_5706) ||
	    (hba->pci_did == PCI_DEVICE_ID_NX2_5706S))
		set_bit(BNX2I_NX2_DEV_5706, &hba->cnic_dev_type);
	else if ((hba->pci_did == PCI_DEVICE_ID_NX2_5708) ||
	    (hba->pci_did == PCI_DEVICE_ID_NX2_5708S))
		set_bit(BNX2I_NX2_DEV_5708, &hba->cnic_dev_type);
	else if ((hba->pci_did == PCI_DEVICE_ID_NX2_5709) ||
	    (hba->pci_did == PCI_DEVICE_ID_NX2_5709S)) {
		set_bit(BNX2I_NX2_DEV_5709, &hba->cnic_dev_type);
		hba->mail_queue_access = BNX2I_MQ_BIN_MODE;
	} else if (hba->pci_did == PCI_DEVICE_ID_NX2_57710 ||
		   hba->pci_did == PCI_DEVICE_ID_NX2_57711)
		set_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type);
}


/**
 * get_adapter_list_head - returns head of adapter list
 */
struct bnx2i_hba *get_adapter_list_head(void)
{
	struct bnx2i_hba *hba = NULL;
	struct bnx2i_hba *tmp_hba;

	if (!adapter_count)
		goto hba_not_found;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry(tmp_hba, &adapter_list, link) {
		if (tmp_hba->cnic && tmp_hba->cnic->cm_select_dev) {
			hba = tmp_hba;
			break;
		}
	}
	mutex_unlock(&bnx2i_dev_lock);
hba_not_found:
	return hba;
}


/**
 * bnx2i_find_hba_for_cnic - maps cnic device instance to bnx2i adapter instance
 * @cnic:	pointer to cnic device instance
 *
 */
struct bnx2i_hba *bnx2i_find_hba_for_cnic(struct cnic_dev *cnic)
{
	struct bnx2i_hba *hba, *temp;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry_safe(hba, temp, &adapter_list, link) {
		if (hba->cnic == cnic) {
			mutex_unlock(&bnx2i_dev_lock);
			return hba;
		}
	}
	mutex_unlock(&bnx2i_dev_lock);
	return NULL;
}


/**
 * bnx2i_start - cnic callback to initialize & start adapter instance
 * @handle:	transparent handle pointing to adapter structure
 *
 * This function maps adapter structure to pcidev structure and initiates
 *	firmware handshake to enable/initialize on chip iscsi components
 * 	This bnx2i - cnic interface api callback is issued after following
 *	2 conditions are met -
 *	  a) underlying network interface is up (marked by event 'NETDEV_UP'
 *		from netdev
 *	  b) bnx2i adapter instance is registered
 */
void bnx2i_start(void *handle)
{
#define BNX2I_INIT_POLL_TIME	(1000 / HZ)
	struct bnx2i_hba *hba = handle;
	int i = HZ;

	bnx2i_send_fw_iscsi_init_msg(hba);
	while (!test_bit(ADAPTER_STATE_UP, &hba->adapter_state) && i--)
		msleep(BNX2I_INIT_POLL_TIME);
}


/**
 * bnx2i_stop - cnic callback to shutdown adapter instance
 * @handle:	transparent handle pointing to adapter structure
 *
 * driver checks if adapter is already in shutdown mode, if not start
 *	the shutdown process
 */
void bnx2i_stop(void *handle)
{
	struct bnx2i_hba *hba = handle;

	/* check if cleanup happened in GOING_DOWN context */
	clear_bit(ADAPTER_STATE_UP, &hba->adapter_state);
	if (!test_and_clear_bit(ADAPTER_STATE_GOING_DOWN,
				&hba->adapter_state))
		iscsi_host_for_each_session(hba->shost,
					    bnx2i_drop_session);
}

/**
 * bnx2i_register_device - register bnx2i adapter instance with the cnic driver
 * @hba:	Adapter instance to register
 *
 * registers bnx2i adapter instance with the cnic driver while holding the
 *	adapter structure lock
 */
void bnx2i_register_device(struct bnx2i_hba *hba)
{
	int rc;

	if (test_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state) ||
	    test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic)) {
		return;
	}

	rc = hba->cnic->register_device(hba->cnic, CNIC_ULP_ISCSI, hba);

	if (!rc)
		set_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
}


/**
 * bnx2i_reg_dev_all - registers all adapter instances with the cnic driver
 *
 * registers all bnx2i adapter instances with the cnic driver while holding
 *	the global resource lock
 */
void bnx2i_reg_dev_all(void)
{
	struct bnx2i_hba *hba, *temp;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry_safe(hba, temp, &adapter_list, link)
		bnx2i_register_device(hba);
	mutex_unlock(&bnx2i_dev_lock);
}


/**
 * bnx2i_unreg_one_device - unregister adapter instance with the cnic driver
 * @hba:	Adapter instance to unregister
 *
 * registers bnx2i adapter instance with the cnic driver while holding
 *	the adapter structure lock
 */
static void bnx2i_unreg_one_device(struct bnx2i_hba *hba)
{
	if (hba->ofld_conns_active ||
	    !test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic) ||
	    test_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state))
		return;

	hba->cnic->unregister_device(hba->cnic, CNIC_ULP_ISCSI);

	/* ep_disconnect could come before NETDEV_DOWN, driver won't
	 * see NETDEV_DOWN as it already unregistered itself.
	 */
	hba->adapter_state = 0;
	clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
}

/**
 * bnx2i_unreg_dev_all - unregisters all bnx2i instances with the cnic driver
 *
 * unregisters all bnx2i adapter instances with the cnic driver while holding
 *	the global resource lock
 */
void bnx2i_unreg_dev_all(void)
{
	struct bnx2i_hba *hba, *temp;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry_safe(hba, temp, &adapter_list, link)
		bnx2i_unreg_one_device(hba);
	mutex_unlock(&bnx2i_dev_lock);
}


/**
 * bnx2i_init_one - initialize an adapter instance and allocate memory resources
 * @hba:	bnx2i adapter instance
 * @cnic:	cnic device handle
 *
 * Global resource lock is held during critical sections below. This routine is
 *	called from either cnic_register_driver() or device hot plug context and
 *	and does majority of device specific initialization
 */
static int bnx2i_init_one(struct bnx2i_hba *hba, struct cnic_dev *cnic)
{
	int rc;

	mutex_lock(&bnx2i_dev_lock);
	rc = cnic->register_device(cnic, CNIC_ULP_ISCSI, hba);
	if (!rc) {
		hba->age++;
		set_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
		list_add_tail(&hba->link, &adapter_list);
		adapter_count++;
	} else if (rc == -EBUSY) 	/* duplicate registration */
		printk(KERN_ALERT "bnx2i, duplicate registration"
				  "hba=%p, cnic=%p\n", hba, cnic);
	else if (rc == -EAGAIN)
		printk(KERN_ERR "bnx2i, driver not registered\n");
	else if (rc == -EINVAL)
		printk(KERN_ERR "bnx2i, invalid type %d\n", CNIC_ULP_ISCSI);
	else
		printk(KERN_ERR "bnx2i dev reg, unknown error, %d\n", rc);

	mutex_unlock(&bnx2i_dev_lock);

	return rc;
}


/**
 * bnx2i_ulp_init - initialize an adapter instance
 * @dev:	cnic device handle
 *
 * Called from cnic_register_driver() context to initialize all enumerated
 *	cnic devices. This routine allocate adapter structure and other
 *	device specific resources.
 */
void bnx2i_ulp_init(struct cnic_dev *dev)
{
	struct bnx2i_hba *hba;

	/* Allocate a HBA structure for this device */
	hba = bnx2i_alloc_hba(dev);
	if (!hba) {
		printk(KERN_ERR "bnx2i init: hba initialization failed\n");
		return;
	}

	/* Get PCI related information and update hba struct members */
	clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
	if (bnx2i_init_one(hba, dev)) {
		printk(KERN_ERR "bnx2i - hba %p init failed\n", hba);
		bnx2i_free_hba(hba);
	} else
		hba->cnic = dev;
}


/**
 * bnx2i_ulp_exit - shuts down adapter instance and frees all resources
 * @dev:	cnic device handle
 *
 */
void bnx2i_ulp_exit(struct cnic_dev *dev)
{
	struct bnx2i_hba *hba;

	hba = bnx2i_find_hba_for_cnic(dev);
	if (!hba) {
		printk(KERN_INFO "bnx2i_ulp_exit: hba not "
				 "found, dev 0x%p\n", dev);
		return;
	}
	mutex_lock(&bnx2i_dev_lock);
	list_del_init(&hba->link);
	adapter_count--;

	if (test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic)) {
		hba->cnic->unregister_device(hba->cnic, CNIC_ULP_ISCSI);
		clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
	}
	mutex_unlock(&bnx2i_dev_lock);

	bnx2i_free_hba(hba);
}


/**
 * bnx2i_mod_init - module init entry point
 *
 * initialize any driver wide global data structures such as endpoint pool,
 *	tcp port manager/queue, sysfs. finally driver will register itself
 *	with the cnic module
 */
static int __init bnx2i_mod_init(void)
{
	int err;

	printk(KERN_INFO "%s", version);

	if (!is_power_of_2(sq_size))
		sq_size = roundup_pow_of_two(sq_size);

	mutex_init(&bnx2i_dev_lock);

	bnx2i_scsi_xport_template =
			iscsi_register_transport(&bnx2i_iscsi_transport);
	if (!bnx2i_scsi_xport_template) {
		printk(KERN_ERR "Could not register bnx2i transport.\n");
		err = -ENOMEM;
		goto out;
	}

	err = cnic_register_driver(CNIC_ULP_ISCSI, &bnx2i_cnic_cb);
	if (err) {
		printk(KERN_ERR "Could not register bnx2i cnic driver.\n");
		goto unreg_xport;
	}

	return 0;

unreg_xport:
	iscsi_unregister_transport(&bnx2i_iscsi_transport);
out:
	return err;
}


/**
 * bnx2i_mod_exit - module cleanup/exit entry point
 *
 * Global resource lock and host adapter lock is held during critical sections
 *	in this function. Driver will browse through the adapter list, cleans-up
 *	each instance, unregisters iscsi transport name and finally driver will
 *	unregister itself with the cnic module
 */
static void __exit bnx2i_mod_exit(void)
{
	struct bnx2i_hba *hba;

	mutex_lock(&bnx2i_dev_lock);
	while (!list_empty(&adapter_list)) {
		hba = list_entry(adapter_list.next, struct bnx2i_hba, link);
		list_del(&hba->link);
		adapter_count--;

		if (test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic)) {
			hba->cnic->unregister_device(hba->cnic, CNIC_ULP_ISCSI);
			clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
		}

		bnx2i_free_hba(hba);
	}
	mutex_unlock(&bnx2i_dev_lock);

	iscsi_unregister_transport(&bnx2i_iscsi_transport);
	cnic_unregister_driver(CNIC_ULP_ISCSI);
}

module_init(bnx2i_mod_init);
module_exit(bnx2i_mod_exit);
