/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Private data and functions for adjunct processor VFIO matrix driver.
 *
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *	      Halil Pasic <pasic@linux.ibm.com>
 *	      Pierre Morel <pmorel@linux.ibm.com>
 *
 * Copyright IBM Corp. 2018
 */

#ifndef _VFIO_AP_PRIVATE_H_
#define _VFIO_AP_PRIVATE_H_

#include <linux/types.h>
#include <linux/mdev.h>
#include <linux/delay.h>
#include <linux/eventfd.h>
#include <linux/mutex.h>
#include <linux/kvm_host.h>
#include <linux/vfio.h>
#include <linux/hashtable.h>

#include "ap_bus.h"

#define VFIO_AP_MODULE_NAME "vfio_ap"
#define VFIO_AP_DRV_NAME "vfio_ap"

/**
 * struct ap_matrix_dev - Contains the data for the matrix device.
 *
 * @device:	generic device structure associated with the AP matrix device
 * @info:	the struct containing the output from the PQAP(QCI) instruction
 * @mdev_list:	the list of mediated matrix devices created
 * @mdevs_lock: mutex for locking the AP matrix device. This lock will be
 *		taken every time we fiddle with state managed by the vfio_ap
 *		driver, be it using @mdev_list or writing the state of a
 *		single ap_matrix_mdev device. It's quite coarse but we don't
 *		expect much contention.
 * @vfio_ap_drv: the vfio_ap device driver
 * @guests_lock: mutex for controlling access to a guest that is using AP
 *		 devices passed through by the vfio_ap device driver. This lock
 *		 will be taken when the AP devices are plugged into or unplugged
 *		 from a guest, and when an ap_matrix_mdev device is added to or
 *		 removed from @mdev_list or the list is iterated.
 */
struct ap_matrix_dev {
	struct device device;
	struct ap_config_info info;
	struct list_head mdev_list;
	struct mutex mdevs_lock; /* serializes access to each ap_matrix_mdev */
	struct ap_driver  *vfio_ap_drv;
	struct mutex guests_lock; /* serializes access to each KVM guest */
	struct mdev_parent parent;
	struct mdev_type mdev_type;
	struct mdev_type *mdev_types[1];
};

extern struct ap_matrix_dev *matrix_dev;

/**
 * struct ap_matrix - matrix of adapters, domains and control domains
 *
 * @apm_max: max adapter number in @apm
 * @apm: identifies the AP adapters in the matrix
 * @aqm_max: max domain number in @aqm
 * @aqm: identifies the AP queues (domains) in the matrix
 * @adm_max: max domain number in @adm
 * @adm: identifies the AP control domains in the matrix
 *
 * The AP matrix is comprised of three bit masks identifying the adapters,
 * queues (domains) and control domains that belong to an AP matrix. The bits in
 * each mask, from left to right, correspond to IDs 0 to 255. When a bit is set
 * the corresponding ID belongs to the matrix.
 */
struct ap_matrix {
	unsigned long apm_max;
	DECLARE_BITMAP(apm, 256);
	unsigned long aqm_max;
	DECLARE_BITMAP(aqm, 256);
	unsigned long adm_max;
	DECLARE_BITMAP(adm, 256);
};

/**
 * struct ap_queue_table - a table of queue objects.
 *
 * @queues: a hashtable of queues (struct vfio_ap_queue).
 */
struct ap_queue_table {
	DECLARE_HASHTABLE(queues, 8);
};

/**
 * struct ap_matrix_mdev - Contains the data associated with a matrix mediated
 *			   device.
 * @vdev:	the vfio device
 * @node:	allows the ap_matrix_mdev struct to be added to a list
 * @matrix:	the adapters, usage domains and control domains assigned to the
 *		mediated matrix device.
 * @shadow_apcb:    the shadow copy of the APCB field of the KVM guest's CRYCB
 * @kvm:	the struct holding guest's state
 * @pqap_hook:	the function pointer to the interception handler for the
 *		PQAP(AQIC) instruction.
 * @mdev:	the mediated device
 * @qtable:	table of queues (struct vfio_ap_queue) assigned to the mdev
 * @req_trigger eventfd ctx for signaling userspace to return a device
 * @apm_add:	bitmap of APIDs added to the host's AP configuration
 * @aqm_add:	bitmap of APQIs added to the host's AP configuration
 * @adm_add:	bitmap of control domain numbers added to the host's AP
 *		configuration
 */
struct ap_matrix_mdev {
	struct vfio_device vdev;
	struct list_head node;
	struct ap_matrix matrix;
	struct ap_matrix shadow_apcb;
	struct kvm *kvm;
	crypto_hook pqap_hook;
	struct mdev_device *mdev;
	struct ap_queue_table qtable;
	struct eventfd_ctx *req_trigger;
	DECLARE_BITMAP(apm_add, AP_DEVICES);
	DECLARE_BITMAP(aqm_add, AP_DOMAINS);
	DECLARE_BITMAP(adm_add, AP_DOMAINS);
};

/**
 * struct vfio_ap_queue - contains the data associated with a queue bound to the
 *			  vfio_ap device driver
 * @matrix_mdev: the matrix mediated device
 * @saved_iova: the notification indicator byte (nib) address
 * @apqn: the APQN of the AP queue device
 * @saved_isc: the guest ISC registered with the GIB interface
 * @mdev_qnode: allows the vfio_ap_queue struct to be added to a hashtable
 * @reset_status: the status from the last reset of the queue
 * @reset_work: work to wait for queue reset to complete
 */
struct vfio_ap_queue {
	struct ap_matrix_mdev *matrix_mdev;
	dma_addr_t saved_iova;
	int	apqn;
#define VFIO_AP_ISC_INVALID 0xff
	unsigned char saved_isc;
	struct hlist_node mdev_qnode;
	struct ap_queue_status reset_status;
	struct work_struct reset_work;
};

int vfio_ap_mdev_register(void);
void vfio_ap_mdev_unregister(void);

int vfio_ap_mdev_probe_queue(struct ap_device *queue);
void vfio_ap_mdev_remove_queue(struct ap_device *queue);

int vfio_ap_mdev_resource_in_use(unsigned long *apm, unsigned long *aqm);

void vfio_ap_on_cfg_changed(struct ap_config_info *new_config_info,
			    struct ap_config_info *old_config_info);
void vfio_ap_on_scan_complete(struct ap_config_info *new_config_info,
			      struct ap_config_info *old_config_info);

#endif /* _VFIO_AP_PRIVATE_H_ */
