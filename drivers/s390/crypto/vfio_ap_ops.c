// SPDX-License-Identifier: GPL-2.0+
/*
 * Adjunct processor matrix VFIO device driver callbacks.
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *	      Halil Pasic <pasic@linux.ibm.com>
 *	      Pierre Morel <pmorel@linux.ibm.com>
 */
#include <linux/string.h>
#include <linux/vfio.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/bitops.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/uuid.h>
#include <asm/kvm.h>
#include <asm/zcrypt.h>

#include "vfio_ap_private.h"
#include "vfio_ap_debug.h"

#define VFIO_AP_MDEV_TYPE_HWVIRT "passthrough"
#define VFIO_AP_MDEV_NAME_HWVIRT "VFIO AP Passthrough Device"

#define AP_QUEUE_ASSIGNED "assigned"
#define AP_QUEUE_UNASSIGNED "unassigned"
#define AP_QUEUE_IN_USE "in use"

static int vfio_ap_mdev_reset_queues(struct ap_queue_table *qtable);
static struct vfio_ap_queue *vfio_ap_find_queue(int apqn);
static const struct vfio_device_ops vfio_ap_matrix_dev_ops;
static int vfio_ap_mdev_reset_queue(struct vfio_ap_queue *q, unsigned int retry);

/**
 * get_update_locks_for_kvm: Acquire the locks required to dynamically update a
 *			     KVM guest's APCB in the proper order.
 *
 * @kvm: a pointer to a struct kvm object containing the KVM guest's APCB.
 *
 * The proper locking order is:
 * 1. matrix_dev->guests_lock: required to use the KVM pointer to update a KVM
 *			       guest's APCB.
 * 2. kvm->lock:	       required to update a guest's APCB
 * 3. matrix_dev->mdevs_lock:  required to access data stored in a matrix_mdev
 *
 * Note: If @kvm is NULL, the KVM lock will not be taken.
 */
static inline void get_update_locks_for_kvm(struct kvm *kvm)
{
	mutex_lock(&matrix_dev->guests_lock);
	if (kvm)
		mutex_lock(&kvm->lock);
	mutex_lock(&matrix_dev->mdevs_lock);
}

/**
 * release_update_locks_for_kvm: Release the locks used to dynamically update a
 *				 KVM guest's APCB in the proper order.
 *
 * @kvm: a pointer to a struct kvm object containing the KVM guest's APCB.
 *
 * The proper unlocking order is:
 * 1. matrix_dev->mdevs_lock
 * 2. kvm->lock
 * 3. matrix_dev->guests_lock
 *
 * Note: If @kvm is NULL, the KVM lock will not be released.
 */
static inline void release_update_locks_for_kvm(struct kvm *kvm)
{
	mutex_unlock(&matrix_dev->mdevs_lock);
	if (kvm)
		mutex_unlock(&kvm->lock);
	mutex_unlock(&matrix_dev->guests_lock);
}

/**
 * get_update_locks_for_mdev: Acquire the locks required to dynamically update a
 *			      KVM guest's APCB in the proper order.
 *
 * @matrix_mdev: a pointer to a struct ap_matrix_mdev object containing the AP
 *		 configuration data to use to update a KVM guest's APCB.
 *
 * The proper locking order is:
 * 1. matrix_dev->guests_lock: required to use the KVM pointer to update a KVM
 *			       guest's APCB.
 * 2. matrix_mdev->kvm->lock:  required to update a guest's APCB
 * 3. matrix_dev->mdevs_lock:  required to access data stored in a matrix_mdev
 *
 * Note: If @matrix_mdev is NULL or is not attached to a KVM guest, the KVM
 *	 lock will not be taken.
 */
static inline void get_update_locks_for_mdev(struct ap_matrix_mdev *matrix_mdev)
{
	mutex_lock(&matrix_dev->guests_lock);
	if (matrix_mdev && matrix_mdev->kvm)
		mutex_lock(&matrix_mdev->kvm->lock);
	mutex_lock(&matrix_dev->mdevs_lock);
}

/**
 * release_update_locks_for_mdev: Release the locks used to dynamically update a
 *				  KVM guest's APCB in the proper order.
 *
 * @matrix_mdev: a pointer to a struct ap_matrix_mdev object containing the AP
 *		 configuration data to use to update a KVM guest's APCB.
 *
 * The proper unlocking order is:
 * 1. matrix_dev->mdevs_lock
 * 2. matrix_mdev->kvm->lock
 * 3. matrix_dev->guests_lock
 *
 * Note: If @matrix_mdev is NULL or is not attached to a KVM guest, the KVM
 *	 lock will not be released.
 */
static inline void release_update_locks_for_mdev(struct ap_matrix_mdev *matrix_mdev)
{
	mutex_unlock(&matrix_dev->mdevs_lock);
	if (matrix_mdev && matrix_mdev->kvm)
		mutex_unlock(&matrix_mdev->kvm->lock);
	mutex_unlock(&matrix_dev->guests_lock);
}

/**
 * get_update_locks_by_apqn: Find the mdev to which an APQN is assigned and
 *			     acquire the locks required to update the APCB of
 *			     the KVM guest to which the mdev is attached.
 *
 * @apqn: the APQN of a queue device.
 *
 * The proper locking order is:
 * 1. matrix_dev->guests_lock: required to use the KVM pointer to update a KVM
 *			       guest's APCB.
 * 2. matrix_mdev->kvm->lock:  required to update a guest's APCB
 * 3. matrix_dev->mdevs_lock:  required to access data stored in a matrix_mdev
 *
 * Note: If @apqn is not assigned to a matrix_mdev, the matrix_mdev->kvm->lock
 *	 will not be taken.
 *
 * Return: the ap_matrix_mdev object to which @apqn is assigned or NULL if @apqn
 *	   is not assigned to an ap_matrix_mdev.
 */
static struct ap_matrix_mdev *get_update_locks_by_apqn(int apqn)
{
	struct ap_matrix_mdev *matrix_mdev;

	mutex_lock(&matrix_dev->guests_lock);

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		if (test_bit_inv(AP_QID_CARD(apqn), matrix_mdev->matrix.apm) &&
		    test_bit_inv(AP_QID_QUEUE(apqn), matrix_mdev->matrix.aqm)) {
			if (matrix_mdev->kvm)
				mutex_lock(&matrix_mdev->kvm->lock);

			mutex_lock(&matrix_dev->mdevs_lock);

			return matrix_mdev;
		}
	}

	mutex_lock(&matrix_dev->mdevs_lock);

	return NULL;
}

/**
 * get_update_locks_for_queue: get the locks required to update the APCB of the
 *			       KVM guest to which the matrix mdev linked to a
 *			       vfio_ap_queue object is attached.
 *
 * @q: a pointer to a vfio_ap_queue object.
 *
 * The proper locking order is:
 * 1. q->matrix_dev->guests_lock: required to use the KVM pointer to update a
 *				  KVM guest's APCB.
 * 2. q->matrix_mdev->kvm->lock:  required to update a guest's APCB
 * 3. matrix_dev->mdevs_lock:	  required to access data stored in matrix_mdev
 *
 * Note: if @queue is not linked to an ap_matrix_mdev object, the KVM lock
 *	  will not be taken.
 */
static inline void get_update_locks_for_queue(struct vfio_ap_queue *q)
{
	mutex_lock(&matrix_dev->guests_lock);
	if (q->matrix_mdev && q->matrix_mdev->kvm)
		mutex_lock(&q->matrix_mdev->kvm->lock);
	mutex_lock(&matrix_dev->mdevs_lock);
}

/**
 * vfio_ap_mdev_get_queue - retrieve a queue with a specific APQN from a
 *			    hash table of queues assigned to a matrix mdev
 * @matrix_mdev: the matrix mdev
 * @apqn: The APQN of a queue device
 *
 * Return: the pointer to the vfio_ap_queue struct representing the queue or
 *	   NULL if the queue is not assigned to @matrix_mdev
 */
static struct vfio_ap_queue *vfio_ap_mdev_get_queue(
					struct ap_matrix_mdev *matrix_mdev,
					int apqn)
{
	struct vfio_ap_queue *q;

	hash_for_each_possible(matrix_mdev->qtable.queues, q, mdev_qnode,
			       apqn) {
		if (q && q->apqn == apqn)
			return q;
	}

	return NULL;
}

/**
 * vfio_ap_wait_for_irqclear - clears the IR bit or gives up after 5 tries
 * @apqn: The AP Queue number
 *
 * Checks the IRQ bit for the status of this APQN using ap_tapq.
 * Returns if the ap_tapq function succeeded and the bit is clear.
 * Returns if ap_tapq function failed with invalid, deconfigured or
 * checkstopped AP.
 * Otherwise retries up to 5 times after waiting 20ms.
 */
static void vfio_ap_wait_for_irqclear(int apqn)
{
	struct ap_queue_status status;
	int retry = 5;

	do {
		status = ap_tapq(apqn, NULL);
		switch (status.response_code) {
		case AP_RESPONSE_NORMAL:
		case AP_RESPONSE_RESET_IN_PROGRESS:
			if (!status.irq_enabled)
				return;
			fallthrough;
		case AP_RESPONSE_BUSY:
			msleep(20);
			break;
		case AP_RESPONSE_Q_NOT_AVAIL:
		case AP_RESPONSE_DECONFIGURED:
		case AP_RESPONSE_CHECKSTOPPED:
		default:
			WARN_ONCE(1, "%s: tapq rc %02x: %04x\n", __func__,
				  status.response_code, apqn);
			return;
		}
	} while (--retry);

	WARN_ONCE(1, "%s: tapq rc %02x: %04x could not clear IR bit\n",
		  __func__, status.response_code, apqn);
}

/**
 * vfio_ap_free_aqic_resources - free vfio_ap_queue resources
 * @q: The vfio_ap_queue
 *
 * Unregisters the ISC in the GIB when the saved ISC not invalid.
 * Unpins the guest's page holding the NIB when it exists.
 * Resets the saved_iova and saved_isc to invalid values.
 */
static void vfio_ap_free_aqic_resources(struct vfio_ap_queue *q)
{
	if (!q)
		return;
	if (q->saved_isc != VFIO_AP_ISC_INVALID &&
	    !WARN_ON(!(q->matrix_mdev && q->matrix_mdev->kvm))) {
		kvm_s390_gisc_unregister(q->matrix_mdev->kvm, q->saved_isc);
		q->saved_isc = VFIO_AP_ISC_INVALID;
	}
	if (q->saved_iova && !WARN_ON(!q->matrix_mdev)) {
		vfio_unpin_pages(&q->matrix_mdev->vdev, q->saved_iova, 1);
		q->saved_iova = 0;
	}
}

/**
 * vfio_ap_irq_disable - disables and clears an ap_queue interrupt
 * @q: The vfio_ap_queue
 *
 * Uses ap_aqic to disable the interruption and in case of success, reset
 * in progress or IRQ disable command already proceeded: calls
 * vfio_ap_wait_for_irqclear() to check for the IRQ bit to be clear
 * and calls vfio_ap_free_aqic_resources() to free the resources associated
 * with the AP interrupt handling.
 *
 * In the case the AP is busy, or a reset is in progress,
 * retries after 20ms, up to 5 times.
 *
 * Returns if ap_aqic function failed with invalid, deconfigured or
 * checkstopped AP.
 *
 * Return: &struct ap_queue_status
 */
static struct ap_queue_status vfio_ap_irq_disable(struct vfio_ap_queue *q)
{
	struct ap_qirq_ctrl aqic_gisa = {};
	struct ap_queue_status status;
	int retries = 5;

	do {
		status = ap_aqic(q->apqn, aqic_gisa, 0);
		switch (status.response_code) {
		case AP_RESPONSE_OTHERWISE_CHANGED:
		case AP_RESPONSE_NORMAL:
			vfio_ap_wait_for_irqclear(q->apqn);
			goto end_free;
		case AP_RESPONSE_RESET_IN_PROGRESS:
		case AP_RESPONSE_BUSY:
			msleep(20);
			break;
		case AP_RESPONSE_Q_NOT_AVAIL:
		case AP_RESPONSE_DECONFIGURED:
		case AP_RESPONSE_CHECKSTOPPED:
		case AP_RESPONSE_INVALID_ADDRESS:
		default:
			/* All cases in default means AP not operational */
			WARN_ONCE(1, "%s: ap_aqic status %d\n", __func__,
				  status.response_code);
			goto end_free;
		}
	} while (retries--);

	WARN_ONCE(1, "%s: ap_aqic status %d\n", __func__,
		  status.response_code);
end_free:
	vfio_ap_free_aqic_resources(q);
	return status;
}

/**
 * vfio_ap_validate_nib - validate a notification indicator byte (nib) address.
 *
 * @vcpu: the object representing the vcpu executing the PQAP(AQIC) instruction.
 * @nib: the location for storing the nib address.
 *
 * When the PQAP(AQIC) instruction is executed, general register 2 contains the
 * address of the notification indicator byte (nib) used for IRQ notification.
 * This function parses and validates the nib from gr2.
 *
 * Return: returns zero if the nib address is a valid; otherwise, returns
 *	   -EINVAL.
 */
static int vfio_ap_validate_nib(struct kvm_vcpu *vcpu, dma_addr_t *nib)
{
	*nib = vcpu->run->s.regs.gprs[2];

	if (!*nib)
		return -EINVAL;
	if (kvm_is_error_hva(gfn_to_hva(vcpu->kvm, *nib >> PAGE_SHIFT)))
		return -EINVAL;

	return 0;
}

/**
 * vfio_ap_irq_enable - Enable Interruption for a APQN
 *
 * @q:	 the vfio_ap_queue holding AQIC parameters
 * @isc: the guest ISC to register with the GIB interface
 * @vcpu: the vcpu object containing the registers specifying the parameters
 *	  passed to the PQAP(AQIC) instruction.
 *
 * Pin the NIB saved in *q
 * Register the guest ISC to GIB interface and retrieve the
 * host ISC to issue the host side PQAP/AQIC
 *
 * Response.status may be set to AP_RESPONSE_INVALID_ADDRESS in case the
 * vfio_pin_pages failed.
 *
 * Otherwise return the ap_queue_status returned by the ap_aqic(),
 * all retry handling will be done by the guest.
 *
 * Return: &struct ap_queue_status
 */
static struct ap_queue_status vfio_ap_irq_enable(struct vfio_ap_queue *q,
						 int isc,
						 struct kvm_vcpu *vcpu)
{
	struct ap_qirq_ctrl aqic_gisa = {};
	struct ap_queue_status status = {};
	struct kvm_s390_gisa *gisa;
	struct page *h_page;
	int nisc;
	struct kvm *kvm;
	phys_addr_t h_nib;
	dma_addr_t nib;
	int ret;

	/* Verify that the notification indicator byte address is valid */
	if (vfio_ap_validate_nib(vcpu, &nib)) {
		VFIO_AP_DBF_WARN("%s: invalid NIB address: nib=%pad, apqn=%#04x\n",
				 __func__, &nib, q->apqn);

		status.response_code = AP_RESPONSE_INVALID_ADDRESS;
		return status;
	}

	ret = vfio_pin_pages(&q->matrix_mdev->vdev, nib, 1,
			     IOMMU_READ | IOMMU_WRITE, &h_page);
	switch (ret) {
	case 1:
		break;
	default:
		VFIO_AP_DBF_WARN("%s: vfio_pin_pages failed: rc=%d,"
				 "nib=%pad, apqn=%#04x\n",
				 __func__, ret, &nib, q->apqn);

		status.response_code = AP_RESPONSE_INVALID_ADDRESS;
		return status;
	}

	kvm = q->matrix_mdev->kvm;
	gisa = kvm->arch.gisa_int.origin;

	h_nib = page_to_phys(h_page) | (nib & ~PAGE_MASK);
	aqic_gisa.gisc = isc;

	nisc = kvm_s390_gisc_register(kvm, isc);
	if (nisc < 0) {
		VFIO_AP_DBF_WARN("%s: gisc registration failed: nisc=%d, isc=%d, apqn=%#04x\n",
				 __func__, nisc, isc, q->apqn);

		vfio_unpin_pages(&q->matrix_mdev->vdev, nib, 1);
		status.response_code = AP_RESPONSE_INVALID_GISA;
		return status;
	}

	aqic_gisa.isc = nisc;
	aqic_gisa.ir = 1;
	aqic_gisa.gisa = (uint64_t)gisa >> 4;

	status = ap_aqic(q->apqn, aqic_gisa, h_nib);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		/* See if we did clear older IRQ configuration */
		vfio_ap_free_aqic_resources(q);
		q->saved_iova = nib;
		q->saved_isc = isc;
		break;
	case AP_RESPONSE_OTHERWISE_CHANGED:
		/* We could not modify IRQ setings: clear new configuration */
		vfio_unpin_pages(&q->matrix_mdev->vdev, nib, 1);
		kvm_s390_gisc_unregister(kvm, isc);
		break;
	default:
		pr_warn("%s: apqn %04x: response: %02x\n", __func__, q->apqn,
			status.response_code);
		vfio_ap_irq_disable(q);
		break;
	}

	if (status.response_code != AP_RESPONSE_NORMAL) {
		VFIO_AP_DBF_WARN("%s: PQAP(AQIC) failed with status=%#02x: "
				 "zone=%#x, ir=%#x, gisc=%#x, f=%#x,"
				 "gisa=%#x, isc=%#x, apqn=%#04x\n",
				 __func__, status.response_code,
				 aqic_gisa.zone, aqic_gisa.ir, aqic_gisa.gisc,
				 aqic_gisa.gf, aqic_gisa.gisa, aqic_gisa.isc,
				 q->apqn);
	}

	return status;
}

/**
 * vfio_ap_le_guid_to_be_uuid - convert a little endian guid array into an array
 *				of big endian elements that can be passed by
 *				value to an s390dbf sprintf event function to
 *				format a UUID string.
 *
 * @guid: the object containing the little endian guid
 * @uuid: a six-element array of long values that can be passed by value as
 *	  arguments for a formatting string specifying a UUID.
 *
 * The S390 Debug Feature (s390dbf) allows the use of "%s" in the sprintf
 * event functions if the memory for the passed string is available as long as
 * the debug feature exists. Since a mediated device can be removed at any
 * time, it's name can not be used because %s passes the reference to the string
 * in memory and the reference will go stale once the device is removed .
 *
 * The s390dbf string formatting function allows a maximum of 9 arguments for a
 * message to be displayed in the 'sprintf' view. In order to use the bytes
 * comprising the mediated device's UUID to display the mediated device name,
 * they will have to be converted into an array whose elements can be passed by
 * value to sprintf. For example:
 *
 * guid array: { 83, 78, 17, 62, bb, f1, f0, 47, 91, 4d, 32, a2, 2e, 3a, 88, 04 }
 * mdev name: 62177883-f1bb-47f0-914d-32a22e3a8804
 * array returned: { 62177883, f1bb, 47f0, 914d, 32a2, 2e3a8804 }
 * formatting string: "%08lx-%04lx-%04lx-%04lx-%02lx%04lx"
 */
static void vfio_ap_le_guid_to_be_uuid(guid_t *guid, unsigned long *uuid)
{
	/*
	 * The input guid is ordered in little endian, so it needs to be
	 * reordered for displaying a UUID as a string. This specifies the
	 * guid indices in proper order.
	 */
	uuid[0] = le32_to_cpup((__le32 *)guid);
	uuid[1] = le16_to_cpup((__le16 *)&guid->b[4]);
	uuid[2] = le16_to_cpup((__le16 *)&guid->b[6]);
	uuid[3] = *((__u16 *)&guid->b[8]);
	uuid[4] = *((__u16 *)&guid->b[10]);
	uuid[5] = *((__u32 *)&guid->b[12]);
}

/**
 * handle_pqap - PQAP instruction callback
 *
 * @vcpu: The vcpu on which we received the PQAP instruction
 *
 * Get the general register contents to initialize internal variables.
 * REG[0]: APQN
 * REG[1]: IR and ISC
 * REG[2]: NIB
 *
 * Response.status may be set to following Response Code:
 * - AP_RESPONSE_Q_NOT_AVAIL: if the queue is not available
 * - AP_RESPONSE_DECONFIGURED: if the queue is not configured
 * - AP_RESPONSE_NORMAL (0) : in case of successs
 *   Check vfio_ap_setirq() and vfio_ap_clrirq() for other possible RC.
 * We take the matrix_dev lock to ensure serialization on queues and
 * mediated device access.
 *
 * Return: 0 if we could handle the request inside KVM.
 * Otherwise, returns -EOPNOTSUPP to let QEMU handle the fault.
 */
static int handle_pqap(struct kvm_vcpu *vcpu)
{
	uint64_t status;
	uint16_t apqn;
	unsigned long uuid[6];
	struct vfio_ap_queue *q;
	struct ap_queue_status qstatus = {
			       .response_code = AP_RESPONSE_Q_NOT_AVAIL, };
	struct ap_matrix_mdev *matrix_mdev;

	apqn = vcpu->run->s.regs.gprs[0] & 0xffff;

	/* If we do not use the AIV facility just go to userland */
	if (!(vcpu->arch.sie_block->eca & ECA_AIV)) {
		VFIO_AP_DBF_WARN("%s: AIV facility not installed: apqn=0x%04x, eca=0x%04x\n",
				 __func__, apqn, vcpu->arch.sie_block->eca);

		return -EOPNOTSUPP;
	}

	mutex_lock(&matrix_dev->mdevs_lock);

	if (!vcpu->kvm->arch.crypto.pqap_hook) {
		VFIO_AP_DBF_WARN("%s: PQAP(AQIC) hook not registered with the vfio_ap driver: apqn=0x%04x\n",
				 __func__, apqn);

		goto out_unlock;
	}

	matrix_mdev = container_of(vcpu->kvm->arch.crypto.pqap_hook,
				   struct ap_matrix_mdev, pqap_hook);

	/* If the there is no guest using the mdev, there is nothing to do */
	if (!matrix_mdev->kvm) {
		vfio_ap_le_guid_to_be_uuid(&matrix_mdev->mdev->uuid, uuid);
		VFIO_AP_DBF_WARN("%s: mdev %08lx-%04lx-%04lx-%04lx-%04lx%08lx not in use: apqn=0x%04x\n",
				 __func__, uuid[0],  uuid[1], uuid[2],
				 uuid[3], uuid[4], uuid[5], apqn);
		goto out_unlock;
	}

	q = vfio_ap_mdev_get_queue(matrix_mdev, apqn);
	if (!q) {
		VFIO_AP_DBF_WARN("%s: Queue %02x.%04x not bound to the vfio_ap driver\n",
				 __func__, AP_QID_CARD(apqn),
				 AP_QID_QUEUE(apqn));
		goto out_unlock;
	}

	status = vcpu->run->s.regs.gprs[1];

	/* If IR bit(16) is set we enable the interrupt */
	if ((status >> (63 - 16)) & 0x01)
		qstatus = vfio_ap_irq_enable(q, status & 0x07, vcpu);
	else
		qstatus = vfio_ap_irq_disable(q);

out_unlock:
	memcpy(&vcpu->run->s.regs.gprs[1], &qstatus, sizeof(qstatus));
	vcpu->run->s.regs.gprs[1] >>= 32;
	mutex_unlock(&matrix_dev->mdevs_lock);
	return 0;
}

static void vfio_ap_matrix_init(struct ap_config_info *info,
				struct ap_matrix *matrix)
{
	matrix->apm_max = info->apxa ? info->Na : 63;
	matrix->aqm_max = info->apxa ? info->Nd : 15;
	matrix->adm_max = info->apxa ? info->Nd : 15;
}

static void vfio_ap_mdev_update_guest_apcb(struct ap_matrix_mdev *matrix_mdev)
{
	if (matrix_mdev->kvm)
		kvm_arch_crypto_set_masks(matrix_mdev->kvm,
					  matrix_mdev->shadow_apcb.apm,
					  matrix_mdev->shadow_apcb.aqm,
					  matrix_mdev->shadow_apcb.adm);
}

static bool vfio_ap_mdev_filter_cdoms(struct ap_matrix_mdev *matrix_mdev)
{
	DECLARE_BITMAP(prev_shadow_adm, AP_DOMAINS);

	bitmap_copy(prev_shadow_adm, matrix_mdev->shadow_apcb.adm, AP_DOMAINS);
	bitmap_and(matrix_mdev->shadow_apcb.adm, matrix_mdev->matrix.adm,
		   (unsigned long *)matrix_dev->info.adm, AP_DOMAINS);

	return !bitmap_equal(prev_shadow_adm, matrix_mdev->shadow_apcb.adm,
			     AP_DOMAINS);
}

/*
 * vfio_ap_mdev_filter_matrix - filter the APQNs assigned to the matrix mdev
 *				to ensure no queue devices are passed through to
 *				the guest that are not bound to the vfio_ap
 *				device driver.
 *
 * @matrix_mdev: the matrix mdev whose matrix is to be filtered.
 *
 * Note: If an APQN referencing a queue device that is not bound to the vfio_ap
 *	 driver, its APID will be filtered from the guest's APCB. The matrix
 *	 structure precludes filtering an individual APQN, so its APID will be
 *	 filtered.
 *
 * Return: a boolean value indicating whether the KVM guest's APCB was changed
 *	   by the filtering or not.
 */
static bool vfio_ap_mdev_filter_matrix(struct ap_matrix_mdev *matrix_mdev)
{
	unsigned long apid, apqi, apqn;
	DECLARE_BITMAP(prev_shadow_apm, AP_DEVICES);
	DECLARE_BITMAP(prev_shadow_aqm, AP_DOMAINS);
	struct vfio_ap_queue *q;

	bitmap_copy(prev_shadow_apm, matrix_mdev->shadow_apcb.apm, AP_DEVICES);
	bitmap_copy(prev_shadow_aqm, matrix_mdev->shadow_apcb.aqm, AP_DOMAINS);
	vfio_ap_matrix_init(&matrix_dev->info, &matrix_mdev->shadow_apcb);

	/*
	 * Copy the adapters, domains and control domains to the shadow_apcb
	 * from the matrix mdev, but only those that are assigned to the host's
	 * AP configuration.
	 */
	bitmap_and(matrix_mdev->shadow_apcb.apm, matrix_mdev->matrix.apm,
		   (unsigned long *)matrix_dev->info.apm, AP_DEVICES);
	bitmap_and(matrix_mdev->shadow_apcb.aqm, matrix_mdev->matrix.aqm,
		   (unsigned long *)matrix_dev->info.aqm, AP_DOMAINS);

	for_each_set_bit_inv(apid, matrix_mdev->shadow_apcb.apm, AP_DEVICES) {
		for_each_set_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm,
				     AP_DOMAINS) {
			/*
			 * If the APQN is not bound to the vfio_ap device
			 * driver, then we can't assign it to the guest's
			 * AP configuration. The AP architecture won't
			 * allow filtering of a single APQN, so let's filter
			 * the APID since an adapter represents a physical
			 * hardware device.
			 */
			apqn = AP_MKQID(apid, apqi);
			q = vfio_ap_mdev_get_queue(matrix_mdev, apqn);
			if (!q || q->reset_rc) {
				clear_bit_inv(apid,
					      matrix_mdev->shadow_apcb.apm);
				break;
			}
		}
	}

	return !bitmap_equal(prev_shadow_apm, matrix_mdev->shadow_apcb.apm,
			     AP_DEVICES) ||
	       !bitmap_equal(prev_shadow_aqm, matrix_mdev->shadow_apcb.aqm,
			     AP_DOMAINS);
}

static int vfio_ap_mdev_init_dev(struct vfio_device *vdev)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);

	matrix_mdev->mdev = to_mdev_device(vdev->dev);
	vfio_ap_matrix_init(&matrix_dev->info, &matrix_mdev->matrix);
	matrix_mdev->pqap_hook = handle_pqap;
	vfio_ap_matrix_init(&matrix_dev->info, &matrix_mdev->shadow_apcb);
	hash_init(matrix_mdev->qtable.queues);

	return 0;
}

static int vfio_ap_mdev_probe(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev;
	int ret;

	matrix_mdev = vfio_alloc_device(ap_matrix_mdev, vdev, &mdev->dev,
					&vfio_ap_matrix_dev_ops);
	if (IS_ERR(matrix_mdev))
		return PTR_ERR(matrix_mdev);

	ret = vfio_register_emulated_iommu_dev(&matrix_mdev->vdev);
	if (ret)
		goto err_put_vdev;
	dev_set_drvdata(&mdev->dev, matrix_mdev);
	mutex_lock(&matrix_dev->mdevs_lock);
	list_add(&matrix_mdev->node, &matrix_dev->mdev_list);
	mutex_unlock(&matrix_dev->mdevs_lock);
	return 0;

err_put_vdev:
	vfio_put_device(&matrix_mdev->vdev);
	return ret;
}

static void vfio_ap_mdev_link_queue(struct ap_matrix_mdev *matrix_mdev,
				    struct vfio_ap_queue *q)
{
	if (q) {
		q->matrix_mdev = matrix_mdev;
		hash_add(matrix_mdev->qtable.queues, &q->mdev_qnode, q->apqn);
	}
}

static void vfio_ap_mdev_link_apqn(struct ap_matrix_mdev *matrix_mdev, int apqn)
{
	struct vfio_ap_queue *q;

	q = vfio_ap_find_queue(apqn);
	vfio_ap_mdev_link_queue(matrix_mdev, q);
}

static void vfio_ap_unlink_queue_fr_mdev(struct vfio_ap_queue *q)
{
	hash_del(&q->mdev_qnode);
}

static void vfio_ap_unlink_mdev_fr_queue(struct vfio_ap_queue *q)
{
	q->matrix_mdev = NULL;
}

static void vfio_ap_mdev_unlink_fr_queues(struct ap_matrix_mdev *matrix_mdev)
{
	struct vfio_ap_queue *q;
	unsigned long apid, apqi;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES) {
		for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm,
				     AP_DOMAINS) {
			q = vfio_ap_mdev_get_queue(matrix_mdev,
						   AP_MKQID(apid, apqi));
			if (q)
				q->matrix_mdev = NULL;
		}
	}
}

static void vfio_ap_mdev_release_dev(struct vfio_device *vdev)
{
	vfio_free_device(vdev);
}

static void vfio_ap_mdev_remove(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(&mdev->dev);

	vfio_unregister_group_dev(&matrix_mdev->vdev);

	mutex_lock(&matrix_dev->guests_lock);
	mutex_lock(&matrix_dev->mdevs_lock);
	vfio_ap_mdev_reset_queues(&matrix_mdev->qtable);
	vfio_ap_mdev_unlink_fr_queues(matrix_mdev);
	list_del(&matrix_mdev->node);
	mutex_unlock(&matrix_dev->mdevs_lock);
	mutex_unlock(&matrix_dev->guests_lock);
	vfio_put_device(&matrix_mdev->vdev);
}

#define MDEV_SHARING_ERR "Userspace may not re-assign queue %02lx.%04lx " \
			 "already assigned to %s"

static void vfio_ap_mdev_log_sharing_err(struct ap_matrix_mdev *matrix_mdev,
					 unsigned long *apm,
					 unsigned long *aqm)
{
	unsigned long apid, apqi;
	const struct device *dev = mdev_dev(matrix_mdev->mdev);
	const char *mdev_name = dev_name(dev);

	for_each_set_bit_inv(apid, apm, AP_DEVICES)
		for_each_set_bit_inv(apqi, aqm, AP_DOMAINS)
			dev_warn(dev, MDEV_SHARING_ERR, apid, apqi, mdev_name);
}

/**
 * vfio_ap_mdev_verify_no_sharing - verify APQNs are not shared by matrix mdevs
 *
 * @mdev_apm: mask indicating the APIDs of the APQNs to be verified
 * @mdev_aqm: mask indicating the APQIs of the APQNs to be verified
 *
 * Verifies that each APQN derived from the Cartesian product of a bitmap of
 * AP adapter IDs and AP queue indexes is not configured for any matrix
 * mediated device. AP queue sharing is not allowed.
 *
 * Return: 0 if the APQNs are not shared; otherwise return -EADDRINUSE.
 */
static int vfio_ap_mdev_verify_no_sharing(unsigned long *mdev_apm,
					  unsigned long *mdev_aqm)
{
	struct ap_matrix_mdev *matrix_mdev;
	DECLARE_BITMAP(apm, AP_DEVICES);
	DECLARE_BITMAP(aqm, AP_DOMAINS);

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		/*
		 * If the input apm and aqm are fields of the matrix_mdev
		 * object, then move on to the next matrix_mdev.
		 */
		if (mdev_apm == matrix_mdev->matrix.apm &&
		    mdev_aqm == matrix_mdev->matrix.aqm)
			continue;

		memset(apm, 0, sizeof(apm));
		memset(aqm, 0, sizeof(aqm));

		/*
		 * We work on full longs, as we can only exclude the leftover
		 * bits in non-inverse order. The leftover is all zeros.
		 */
		if (!bitmap_and(apm, mdev_apm, matrix_mdev->matrix.apm,
				AP_DEVICES))
			continue;

		if (!bitmap_and(aqm, mdev_aqm, matrix_mdev->matrix.aqm,
				AP_DOMAINS))
			continue;

		vfio_ap_mdev_log_sharing_err(matrix_mdev, apm, aqm);

		return -EADDRINUSE;
	}

	return 0;
}

/**
 * vfio_ap_mdev_validate_masks - verify that the APQNs assigned to the mdev are
 *				 not reserved for the default zcrypt driver and
 *				 are not assigned to another mdev.
 *
 * @matrix_mdev: the mdev to which the APQNs being validated are assigned.
 *
 * Return: One of the following values:
 * o the error returned from the ap_apqn_in_matrix_owned_by_def_drv() function,
 *   most likely -EBUSY indicating the ap_perms_mutex lock is already held.
 * o EADDRNOTAVAIL if an APQN assigned to @matrix_mdev is reserved for the
 *		   zcrypt default driver.
 * o EADDRINUSE if an APQN assigned to @matrix_mdev is assigned to another mdev
 * o A zero indicating validation succeeded.
 */
static int vfio_ap_mdev_validate_masks(struct ap_matrix_mdev *matrix_mdev)
{
	if (ap_apqn_in_matrix_owned_by_def_drv(matrix_mdev->matrix.apm,
					       matrix_mdev->matrix.aqm))
		return -EADDRNOTAVAIL;

	return vfio_ap_mdev_verify_no_sharing(matrix_mdev->matrix.apm,
					      matrix_mdev->matrix.aqm);
}

static void vfio_ap_mdev_link_adapter(struct ap_matrix_mdev *matrix_mdev,
				      unsigned long apid)
{
	unsigned long apqi;

	for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, AP_DOMAINS)
		vfio_ap_mdev_link_apqn(matrix_mdev,
				       AP_MKQID(apid, apqi));
}

/**
 * assign_adapter_store - parses the APID from @buf and sets the
 * corresponding bit in the mediated matrix device's APM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_adapter attribute
 * @buf:	a buffer containing the AP adapter number (APID) to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APID is valid; otherwise,
 * returns one of the following errors:
 *
 *	1. -EINVAL
 *	   The APID is not a valid number
 *
 *	2. -ENODEV
 *	   The APID exceeds the maximum value configured for the system
 *
 *	3. -EADDRNOTAVAIL
 *	   An APQN derived from the cross product of the APID being assigned
 *	   and the APQIs previously assigned is not bound to the vfio_ap device
 *	   driver; or, if no APQIs have yet been assigned, the APID is not
 *	   contained in an APQN bound to the vfio_ap device driver.
 *
 *	4. -EADDRINUSE
 *	   An APQN derived from the cross product of the APID being assigned
 *	   and the APQIs previously assigned is being used by another mediated
 *	   matrix device
 *
 *	5. -EAGAIN
 *	   A lock required to validate the mdev's AP configuration could not
 *	   be obtained.
 */
static ssize_t assign_adapter_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	unsigned long apid;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	mutex_lock(&ap_perms_mutex);
	get_update_locks_for_mdev(matrix_mdev);

	ret = kstrtoul(buf, 0, &apid);
	if (ret)
		goto done;

	if (apid > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	if (test_bit_inv(apid, matrix_mdev->matrix.apm)) {
		ret = count;
		goto done;
	}

	set_bit_inv(apid, matrix_mdev->matrix.apm);

	ret = vfio_ap_mdev_validate_masks(matrix_mdev);
	if (ret) {
		clear_bit_inv(apid, matrix_mdev->matrix.apm);
		goto done;
	}

	vfio_ap_mdev_link_adapter(matrix_mdev, apid);

	if (vfio_ap_mdev_filter_matrix(matrix_mdev))
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);

	ret = count;
done:
	release_update_locks_for_mdev(matrix_mdev);
	mutex_unlock(&ap_perms_mutex);

	return ret;
}
static DEVICE_ATTR_WO(assign_adapter);

static struct vfio_ap_queue
*vfio_ap_unlink_apqn_fr_mdev(struct ap_matrix_mdev *matrix_mdev,
			     unsigned long apid, unsigned long apqi)
{
	struct vfio_ap_queue *q = NULL;

	q = vfio_ap_mdev_get_queue(matrix_mdev, AP_MKQID(apid, apqi));
	/* If the queue is assigned to the matrix mdev, unlink it. */
	if (q)
		vfio_ap_unlink_queue_fr_mdev(q);

	return q;
}

/**
 * vfio_ap_mdev_unlink_adapter - unlink all queues associated with unassigned
 *				 adapter from the matrix mdev to which the
 *				 adapter was assigned.
 * @matrix_mdev: the matrix mediated device to which the adapter was assigned.
 * @apid: the APID of the unassigned adapter.
 * @qtable: table for storing queues associated with unassigned adapter.
 */
static void vfio_ap_mdev_unlink_adapter(struct ap_matrix_mdev *matrix_mdev,
					unsigned long apid,
					struct ap_queue_table *qtable)
{
	unsigned long apqi;
	struct vfio_ap_queue *q;

	for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, AP_DOMAINS) {
		q = vfio_ap_unlink_apqn_fr_mdev(matrix_mdev, apid, apqi);

		if (q && qtable) {
			if (test_bit_inv(apid, matrix_mdev->shadow_apcb.apm) &&
			    test_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm))
				hash_add(qtable->queues, &q->mdev_qnode,
					 q->apqn);
		}
	}
}

static void vfio_ap_mdev_hot_unplug_adapter(struct ap_matrix_mdev *matrix_mdev,
					    unsigned long apid)
{
	int loop_cursor;
	struct vfio_ap_queue *q;
	struct ap_queue_table *qtable = kzalloc(sizeof(*qtable), GFP_KERNEL);

	hash_init(qtable->queues);
	vfio_ap_mdev_unlink_adapter(matrix_mdev, apid, qtable);

	if (test_bit_inv(apid, matrix_mdev->shadow_apcb.apm)) {
		clear_bit_inv(apid, matrix_mdev->shadow_apcb.apm);
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);
	}

	vfio_ap_mdev_reset_queues(qtable);

	hash_for_each(qtable->queues, loop_cursor, q, mdev_qnode) {
		vfio_ap_unlink_mdev_fr_queue(q);
		hash_del(&q->mdev_qnode);
	}

	kfree(qtable);
}

/**
 * unassign_adapter_store - parses the APID from @buf and clears the
 * corresponding bit in the mediated matrix device's APM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_adapter attribute
 * @buf:	a buffer containing the adapter number (APID) to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APID is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the APID is not a number
 *	-ENODEV if the APID it exceeds the maximum value configured for the
 *		system
 */
static ssize_t unassign_adapter_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned long apid;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	get_update_locks_for_mdev(matrix_mdev);

	ret = kstrtoul(buf, 0, &apid);
	if (ret)
		goto done;

	if (apid > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	if (!test_bit_inv(apid, matrix_mdev->matrix.apm)) {
		ret = count;
		goto done;
	}

	clear_bit_inv((unsigned long)apid, matrix_mdev->matrix.apm);
	vfio_ap_mdev_hot_unplug_adapter(matrix_mdev, apid);
	ret = count;
done:
	release_update_locks_for_mdev(matrix_mdev);
	return ret;
}
static DEVICE_ATTR_WO(unassign_adapter);

static void vfio_ap_mdev_link_domain(struct ap_matrix_mdev *matrix_mdev,
				     unsigned long apqi)
{
	unsigned long apid;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES)
		vfio_ap_mdev_link_apqn(matrix_mdev,
				       AP_MKQID(apid, apqi));
}

/**
 * assign_domain_store - parses the APQI from @buf and sets the
 * corresponding bit in the mediated matrix device's AQM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APQI is valid; otherwise returns
 * one of the following errors:
 *
 *	1. -EINVAL
 *	   The APQI is not a valid number
 *
 *	2. -ENODEV
 *	   The APQI exceeds the maximum value configured for the system
 *
 *	3. -EADDRNOTAVAIL
 *	   An APQN derived from the cross product of the APQI being assigned
 *	   and the APIDs previously assigned is not bound to the vfio_ap device
 *	   driver; or, if no APIDs have yet been assigned, the APQI is not
 *	   contained in an APQN bound to the vfio_ap device driver.
 *
 *	4. -EADDRINUSE
 *	   An APQN derived from the cross product of the APQI being assigned
 *	   and the APIDs previously assigned is being used by another mediated
 *	   matrix device
 *
 *	5. -EAGAIN
 *	   The lock required to validate the mdev's AP configuration could not
 *	   be obtained.
 */
static ssize_t assign_domain_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;
	unsigned long apqi;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	mutex_lock(&ap_perms_mutex);
	get_update_locks_for_mdev(matrix_mdev);

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		goto done;

	if (apqi > matrix_mdev->matrix.aqm_max) {
		ret = -ENODEV;
		goto done;
	}

	if (test_bit_inv(apqi, matrix_mdev->matrix.aqm)) {
		ret = count;
		goto done;
	}

	set_bit_inv(apqi, matrix_mdev->matrix.aqm);

	ret = vfio_ap_mdev_validate_masks(matrix_mdev);
	if (ret) {
		clear_bit_inv(apqi, matrix_mdev->matrix.aqm);
		goto done;
	}

	vfio_ap_mdev_link_domain(matrix_mdev, apqi);

	if (vfio_ap_mdev_filter_matrix(matrix_mdev))
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);

	ret = count;
done:
	release_update_locks_for_mdev(matrix_mdev);
	mutex_unlock(&ap_perms_mutex);

	return ret;
}
static DEVICE_ATTR_WO(assign_domain);

static void vfio_ap_mdev_unlink_domain(struct ap_matrix_mdev *matrix_mdev,
				       unsigned long apqi,
				       struct ap_queue_table *qtable)
{
	unsigned long apid;
	struct vfio_ap_queue *q;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES) {
		q = vfio_ap_unlink_apqn_fr_mdev(matrix_mdev, apid, apqi);

		if (q && qtable) {
			if (test_bit_inv(apid, matrix_mdev->shadow_apcb.apm) &&
			    test_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm))
				hash_add(qtable->queues, &q->mdev_qnode,
					 q->apqn);
		}
	}
}

static void vfio_ap_mdev_hot_unplug_domain(struct ap_matrix_mdev *matrix_mdev,
					   unsigned long apqi)
{
	int loop_cursor;
	struct vfio_ap_queue *q;
	struct ap_queue_table *qtable = kzalloc(sizeof(*qtable), GFP_KERNEL);

	hash_init(qtable->queues);
	vfio_ap_mdev_unlink_domain(matrix_mdev, apqi, qtable);

	if (test_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm)) {
		clear_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm);
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);
	}

	vfio_ap_mdev_reset_queues(qtable);

	hash_for_each(qtable->queues, loop_cursor, q, mdev_qnode) {
		vfio_ap_unlink_mdev_fr_queue(q);
		hash_del(&q->mdev_qnode);
	}

	kfree(qtable);
}

/**
 * unassign_domain_store - parses the APQI from @buf and clears the
 * corresponding bit in the mediated matrix device's AQM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be unassigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APQI is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the APQI is not a number
 *	-ENODEV if the APQI exceeds the maximum value configured for the system
 */
static ssize_t unassign_domain_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int ret;
	unsigned long apqi;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	get_update_locks_for_mdev(matrix_mdev);

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		goto done;

	if (apqi > matrix_mdev->matrix.aqm_max) {
		ret = -ENODEV;
		goto done;
	}

	if (!test_bit_inv(apqi, matrix_mdev->matrix.aqm)) {
		ret = count;
		goto done;
	}

	clear_bit_inv((unsigned long)apqi, matrix_mdev->matrix.aqm);
	vfio_ap_mdev_hot_unplug_domain(matrix_mdev, apqi);
	ret = count;

done:
	release_update_locks_for_mdev(matrix_mdev);
	return ret;
}
static DEVICE_ATTR_WO(unassign_domain);

/**
 * assign_control_domain_store - parses the domain ID from @buf and sets
 * the corresponding bit in the mediated matrix device's ADM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be assigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the domain ID is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the ID is not a number
 *	-ENODEV if the ID exceeds the maximum value configured for the system
 */
static ssize_t assign_control_domain_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int ret;
	unsigned long id;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	get_update_locks_for_mdev(matrix_mdev);

	ret = kstrtoul(buf, 0, &id);
	if (ret)
		goto done;

	if (id > matrix_mdev->matrix.adm_max) {
		ret = -ENODEV;
		goto done;
	}

	if (test_bit_inv(id, matrix_mdev->matrix.adm)) {
		ret = count;
		goto done;
	}

	/* Set the bit in the ADM (bitmask) corresponding to the AP control
	 * domain number (id). The bits in the mask, from most significant to
	 * least significant, correspond to IDs 0 up to the one less than the
	 * number of control domains that can be assigned.
	 */
	set_bit_inv(id, matrix_mdev->matrix.adm);
	if (vfio_ap_mdev_filter_cdoms(matrix_mdev))
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);

	ret = count;
done:
	release_update_locks_for_mdev(matrix_mdev);
	return ret;
}
static DEVICE_ATTR_WO(assign_control_domain);

/**
 * unassign_control_domain_store - parses the domain ID from @buf and
 * clears the corresponding bit in the mediated matrix device's ADM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the domain ID is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the ID is not a number
 *	-ENODEV if the ID exceeds the maximum value configured for the system
 */
static ssize_t unassign_control_domain_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	int ret;
	unsigned long domid;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	get_update_locks_for_mdev(matrix_mdev);

	ret = kstrtoul(buf, 0, &domid);
	if (ret)
		goto done;

	if (domid > matrix_mdev->matrix.adm_max) {
		ret = -ENODEV;
		goto done;
	}

	if (!test_bit_inv(domid, matrix_mdev->matrix.adm)) {
		ret = count;
		goto done;
	}

	clear_bit_inv(domid, matrix_mdev->matrix.adm);

	if (test_bit_inv(domid, matrix_mdev->shadow_apcb.adm)) {
		clear_bit_inv(domid, matrix_mdev->shadow_apcb.adm);
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);
	}

	ret = count;
done:
	release_update_locks_for_mdev(matrix_mdev);
	return ret;
}
static DEVICE_ATTR_WO(unassign_control_domain);

static ssize_t control_domains_show(struct device *dev,
				    struct device_attribute *dev_attr,
				    char *buf)
{
	unsigned long id;
	int nchars = 0;
	int n;
	char *bufpos = buf;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);
	unsigned long max_domid = matrix_mdev->matrix.adm_max;

	mutex_lock(&matrix_dev->mdevs_lock);
	for_each_set_bit_inv(id, matrix_mdev->matrix.adm, max_domid + 1) {
		n = sprintf(bufpos, "%04lx\n", id);
		bufpos += n;
		nchars += n;
	}
	mutex_unlock(&matrix_dev->mdevs_lock);

	return nchars;
}
static DEVICE_ATTR_RO(control_domains);

static ssize_t vfio_ap_mdev_matrix_show(struct ap_matrix *matrix, char *buf)
{
	char *bufpos = buf;
	unsigned long apid;
	unsigned long apqi;
	unsigned long apid1;
	unsigned long apqi1;
	unsigned long napm_bits = matrix->apm_max + 1;
	unsigned long naqm_bits = matrix->aqm_max + 1;
	int nchars = 0;
	int n;

	apid1 = find_first_bit_inv(matrix->apm, napm_bits);
	apqi1 = find_first_bit_inv(matrix->aqm, naqm_bits);

	if ((apid1 < napm_bits) && (apqi1 < naqm_bits)) {
		for_each_set_bit_inv(apid, matrix->apm, napm_bits) {
			for_each_set_bit_inv(apqi, matrix->aqm,
					     naqm_bits) {
				n = sprintf(bufpos, "%02lx.%04lx\n", apid,
					    apqi);
				bufpos += n;
				nchars += n;
			}
		}
	} else if (apid1 < napm_bits) {
		for_each_set_bit_inv(apid, matrix->apm, napm_bits) {
			n = sprintf(bufpos, "%02lx.\n", apid);
			bufpos += n;
			nchars += n;
		}
	} else if (apqi1 < naqm_bits) {
		for_each_set_bit_inv(apqi, matrix->aqm, naqm_bits) {
			n = sprintf(bufpos, ".%04lx\n", apqi);
			bufpos += n;
			nchars += n;
		}
	}

	return nchars;
}

static ssize_t matrix_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ssize_t nchars;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	mutex_lock(&matrix_dev->mdevs_lock);
	nchars = vfio_ap_mdev_matrix_show(&matrix_mdev->matrix, buf);
	mutex_unlock(&matrix_dev->mdevs_lock);

	return nchars;
}
static DEVICE_ATTR_RO(matrix);

static ssize_t guest_matrix_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	ssize_t nchars;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	mutex_lock(&matrix_dev->mdevs_lock);
	nchars = vfio_ap_mdev_matrix_show(&matrix_mdev->shadow_apcb, buf);
	mutex_unlock(&matrix_dev->mdevs_lock);

	return nchars;
}
static DEVICE_ATTR_RO(guest_matrix);

static struct attribute *vfio_ap_mdev_attrs[] = {
	&dev_attr_assign_adapter.attr,
	&dev_attr_unassign_adapter.attr,
	&dev_attr_assign_domain.attr,
	&dev_attr_unassign_domain.attr,
	&dev_attr_assign_control_domain.attr,
	&dev_attr_unassign_control_domain.attr,
	&dev_attr_control_domains.attr,
	&dev_attr_matrix.attr,
	&dev_attr_guest_matrix.attr,
	NULL,
};

static struct attribute_group vfio_ap_mdev_attr_group = {
	.attrs = vfio_ap_mdev_attrs
};

static const struct attribute_group *vfio_ap_mdev_attr_groups[] = {
	&vfio_ap_mdev_attr_group,
	NULL
};

/**
 * vfio_ap_mdev_set_kvm - sets all data for @matrix_mdev that are needed
 * to manage AP resources for the guest whose state is represented by @kvm
 *
 * @matrix_mdev: a mediated matrix device
 * @kvm: reference to KVM instance
 *
 * Return: 0 if no other mediated matrix device has a reference to @kvm;
 * otherwise, returns an -EPERM.
 */
static int vfio_ap_mdev_set_kvm(struct ap_matrix_mdev *matrix_mdev,
				struct kvm *kvm)
{
	struct ap_matrix_mdev *m;

	if (kvm->arch.crypto.crycbd) {
		down_write(&kvm->arch.crypto.pqap_hook_rwsem);
		kvm->arch.crypto.pqap_hook = &matrix_mdev->pqap_hook;
		up_write(&kvm->arch.crypto.pqap_hook_rwsem);

		get_update_locks_for_kvm(kvm);

		list_for_each_entry(m, &matrix_dev->mdev_list, node) {
			if (m != matrix_mdev && m->kvm == kvm) {
				release_update_locks_for_kvm(kvm);
				return -EPERM;
			}
		}

		kvm_get_kvm(kvm);
		matrix_mdev->kvm = kvm;
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);

		release_update_locks_for_kvm(kvm);
	}

	return 0;
}

static void vfio_ap_mdev_dma_unmap(struct vfio_device *vdev, u64 iova,
				   u64 length)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);

	vfio_unpin_pages(&matrix_mdev->vdev, iova, 1);
}

/**
 * vfio_ap_mdev_unset_kvm - performs clean-up of resources no longer needed
 * by @matrix_mdev.
 *
 * @matrix_mdev: a matrix mediated device
 */
static void vfio_ap_mdev_unset_kvm(struct ap_matrix_mdev *matrix_mdev)
{
	struct kvm *kvm = matrix_mdev->kvm;

	if (kvm && kvm->arch.crypto.crycbd) {
		down_write(&kvm->arch.crypto.pqap_hook_rwsem);
		kvm->arch.crypto.pqap_hook = NULL;
		up_write(&kvm->arch.crypto.pqap_hook_rwsem);

		get_update_locks_for_kvm(kvm);

		kvm_arch_crypto_clear_masks(kvm);
		vfio_ap_mdev_reset_queues(&matrix_mdev->qtable);
		kvm_put_kvm(kvm);
		matrix_mdev->kvm = NULL;

		release_update_locks_for_kvm(kvm);
	}
}

static struct vfio_ap_queue *vfio_ap_find_queue(int apqn)
{
	struct ap_queue *queue;
	struct vfio_ap_queue *q = NULL;

	queue = ap_get_qdev(apqn);
	if (!queue)
		return NULL;

	if (queue->ap_dev.device.driver == &matrix_dev->vfio_ap_drv->driver)
		q = dev_get_drvdata(&queue->ap_dev.device);

	put_device(&queue->ap_dev.device);

	return q;
}

static int vfio_ap_mdev_reset_queue(struct vfio_ap_queue *q,
				    unsigned int retry)
{
	struct ap_queue_status status;
	int ret;
	int retry2 = 2;

	if (!q)
		return 0;
retry_zapq:
	status = ap_zapq(q->apqn);
	q->reset_rc = status.response_code;
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		ret = 0;
		break;
	case AP_RESPONSE_RESET_IN_PROGRESS:
		if (retry--) {
			msleep(20);
			goto retry_zapq;
		}
		ret = -EBUSY;
		break;
	case AP_RESPONSE_Q_NOT_AVAIL:
	case AP_RESPONSE_DECONFIGURED:
	case AP_RESPONSE_CHECKSTOPPED:
		WARN_ONCE(status.irq_enabled,
			  "PQAP/ZAPQ for %02x.%04x failed with rc=%u while IRQ enabled",
			  AP_QID_CARD(q->apqn), AP_QID_QUEUE(q->apqn),
			  status.response_code);
		ret = -EBUSY;
		goto free_resources;
	default:
		/* things are really broken, give up */
		WARN(true,
		     "PQAP/ZAPQ for %02x.%04x failed with invalid rc=%u\n",
		     AP_QID_CARD(q->apqn), AP_QID_QUEUE(q->apqn),
		     status.response_code);
		return -EIO;
	}

	/* wait for the reset to take effect */
	while (retry2--) {
		if (status.queue_empty && !status.irq_enabled)
			break;
		msleep(20);
		status = ap_tapq(q->apqn, NULL);
	}
	WARN_ONCE(retry2 <= 0, "unable to verify reset of queue %02x.%04x",
		  AP_QID_CARD(q->apqn), AP_QID_QUEUE(q->apqn));

free_resources:
	vfio_ap_free_aqic_resources(q);

	return ret;
}

static int vfio_ap_mdev_reset_queues(struct ap_queue_table *qtable)
{
	int ret, loop_cursor, rc = 0;
	struct vfio_ap_queue *q;

	hash_for_each(qtable->queues, loop_cursor, q, mdev_qnode) {
		ret = vfio_ap_mdev_reset_queue(q, 1);
		/*
		 * Regardless whether a queue turns out to be busy, or
		 * is not operational, we need to continue resetting
		 * the remaining queues.
		 */
		if (ret)
			rc = ret;
	}

	return rc;
}

static int vfio_ap_mdev_open_device(struct vfio_device *vdev)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);

	if (!vdev->kvm)
		return -EINVAL;

	return vfio_ap_mdev_set_kvm(matrix_mdev, vdev->kvm);
}

static void vfio_ap_mdev_close_device(struct vfio_device *vdev)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);

	vfio_ap_mdev_unset_kvm(matrix_mdev);
}

static int vfio_ap_mdev_get_device_info(unsigned long arg)
{
	unsigned long minsz;
	struct vfio_device_info info;

	minsz = offsetofend(struct vfio_device_info, num_irqs);

	if (copy_from_user(&info, (void __user *)arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	info.flags = VFIO_DEVICE_FLAGS_AP | VFIO_DEVICE_FLAGS_RESET;
	info.num_regions = 0;
	info.num_irqs = 0;

	return copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;
}

static ssize_t vfio_ap_mdev_ioctl(struct vfio_device *vdev,
				    unsigned int cmd, unsigned long arg)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);
	int ret;

	mutex_lock(&matrix_dev->mdevs_lock);
	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
		ret = vfio_ap_mdev_get_device_info(arg);
		break;
	case VFIO_DEVICE_RESET:
		ret = vfio_ap_mdev_reset_queues(&matrix_mdev->qtable);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&matrix_dev->mdevs_lock);

	return ret;
}

static struct ap_matrix_mdev *vfio_ap_mdev_for_queue(struct vfio_ap_queue *q)
{
	struct ap_matrix_mdev *matrix_mdev;
	unsigned long apid = AP_QID_CARD(q->apqn);
	unsigned long apqi = AP_QID_QUEUE(q->apqn);

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		if (test_bit_inv(apid, matrix_mdev->matrix.apm) &&
		    test_bit_inv(apqi, matrix_mdev->matrix.aqm))
			return matrix_mdev;
	}

	return NULL;
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	ssize_t nchars = 0;
	struct vfio_ap_queue *q;
	struct ap_matrix_mdev *matrix_mdev;
	struct ap_device *apdev = to_ap_dev(dev);

	mutex_lock(&matrix_dev->mdevs_lock);
	q = dev_get_drvdata(&apdev->device);
	matrix_mdev = vfio_ap_mdev_for_queue(q);

	if (matrix_mdev) {
		if (matrix_mdev->kvm)
			nchars = scnprintf(buf, PAGE_SIZE, "%s\n",
					   AP_QUEUE_IN_USE);
		else
			nchars = scnprintf(buf, PAGE_SIZE, "%s\n",
					   AP_QUEUE_ASSIGNED);
	} else {
		nchars = scnprintf(buf, PAGE_SIZE, "%s\n",
				   AP_QUEUE_UNASSIGNED);
	}

	mutex_unlock(&matrix_dev->mdevs_lock);

	return nchars;
}

static DEVICE_ATTR_RO(status);

static struct attribute *vfio_queue_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group vfio_queue_attr_group = {
	.attrs = vfio_queue_attrs,
};

static const struct vfio_device_ops vfio_ap_matrix_dev_ops = {
	.init = vfio_ap_mdev_init_dev,
	.release = vfio_ap_mdev_release_dev,
	.open_device = vfio_ap_mdev_open_device,
	.close_device = vfio_ap_mdev_close_device,
	.ioctl = vfio_ap_mdev_ioctl,
	.dma_unmap = vfio_ap_mdev_dma_unmap,
};

static struct mdev_driver vfio_ap_matrix_driver = {
	.device_api = VFIO_DEVICE_API_AP_STRING,
	.max_instances = MAX_ZDEV_ENTRIES_EXT,
	.driver = {
		.name = "vfio_ap_mdev",
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
		.dev_groups = vfio_ap_mdev_attr_groups,
	},
	.probe = vfio_ap_mdev_probe,
	.remove = vfio_ap_mdev_remove,
};

int vfio_ap_mdev_register(void)
{
	int ret;

	ret = mdev_register_driver(&vfio_ap_matrix_driver);
	if (ret)
		return ret;

	matrix_dev->mdev_type.sysfs_name = VFIO_AP_MDEV_TYPE_HWVIRT;
	matrix_dev->mdev_type.pretty_name = VFIO_AP_MDEV_NAME_HWVIRT;
	matrix_dev->mdev_types[0] = &matrix_dev->mdev_type;
	ret = mdev_register_parent(&matrix_dev->parent, &matrix_dev->device,
				   &vfio_ap_matrix_driver,
				   matrix_dev->mdev_types, 1);
	if (ret)
		goto err_driver;
	return 0;

err_driver:
	mdev_unregister_driver(&vfio_ap_matrix_driver);
	return ret;
}

void vfio_ap_mdev_unregister(void)
{
	mdev_unregister_parent(&matrix_dev->parent);
	mdev_unregister_driver(&vfio_ap_matrix_driver);
}

int vfio_ap_mdev_probe_queue(struct ap_device *apdev)
{
	int ret;
	struct vfio_ap_queue *q;
	struct ap_matrix_mdev *matrix_mdev;

	ret = sysfs_create_group(&apdev->device.kobj, &vfio_queue_attr_group);
	if (ret)
		return ret;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q) {
		ret = -ENOMEM;
		goto err_remove_group;
	}

	q->apqn = to_ap_queue(&apdev->device)->qid;
	q->saved_isc = VFIO_AP_ISC_INVALID;
	matrix_mdev = get_update_locks_by_apqn(q->apqn);

	if (matrix_mdev) {
		vfio_ap_mdev_link_queue(matrix_mdev, q);

		/*
		 * If we're in the process of handling the adding of adapters or
		 * domains to the host's AP configuration, then let the
		 * vfio_ap device driver's on_scan_complete callback filter the
		 * matrix and update the guest's AP configuration after all of
		 * the new queue devices are probed.
		 */
		if (!bitmap_empty(matrix_mdev->apm_add, AP_DEVICES) ||
		    !bitmap_empty(matrix_mdev->aqm_add, AP_DOMAINS))
			goto done;

		if (vfio_ap_mdev_filter_matrix(matrix_mdev))
			vfio_ap_mdev_update_guest_apcb(matrix_mdev);
	}

done:
	dev_set_drvdata(&apdev->device, q);
	release_update_locks_for_mdev(matrix_mdev);

	return 0;

err_remove_group:
	sysfs_remove_group(&apdev->device.kobj, &vfio_queue_attr_group);
	return ret;
}

void vfio_ap_mdev_remove_queue(struct ap_device *apdev)
{
	unsigned long apid, apqi;
	struct vfio_ap_queue *q;
	struct ap_matrix_mdev *matrix_mdev;

	sysfs_remove_group(&apdev->device.kobj, &vfio_queue_attr_group);
	q = dev_get_drvdata(&apdev->device);
	get_update_locks_for_queue(q);
	matrix_mdev = q->matrix_mdev;

	if (matrix_mdev) {
		vfio_ap_unlink_queue_fr_mdev(q);

		apid = AP_QID_CARD(q->apqn);
		apqi = AP_QID_QUEUE(q->apqn);

		/*
		 * If the queue is assigned to the guest's APCB, then remove
		 * the adapter's APID from the APCB and hot it into the guest.
		 */
		if (test_bit_inv(apid, matrix_mdev->shadow_apcb.apm) &&
		    test_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm)) {
			clear_bit_inv(apid, matrix_mdev->shadow_apcb.apm);
			vfio_ap_mdev_update_guest_apcb(matrix_mdev);
		}
	}

	vfio_ap_mdev_reset_queue(q, 1);
	dev_set_drvdata(&apdev->device, NULL);
	kfree(q);
	release_update_locks_for_mdev(matrix_mdev);
}

/**
 * vfio_ap_mdev_resource_in_use: check whether any of a set of APQNs is
 *				 assigned to a mediated device under the control
 *				 of the vfio_ap device driver.
 *
 * @apm: a bitmap specifying a set of APIDs comprising the APQNs to check.
 * @aqm: a bitmap specifying a set of APQIs comprising the APQNs to check.
 *
 * Return:
 *	* -EADDRINUSE if one or more of the APQNs specified via @apm/@aqm are
 *	  assigned to a mediated device under the control of the vfio_ap
 *	  device driver.
 *	* Otherwise, return 0.
 */
int vfio_ap_mdev_resource_in_use(unsigned long *apm, unsigned long *aqm)
{
	int ret;

	mutex_lock(&matrix_dev->guests_lock);
	mutex_lock(&matrix_dev->mdevs_lock);
	ret = vfio_ap_mdev_verify_no_sharing(apm, aqm);
	mutex_unlock(&matrix_dev->mdevs_lock);
	mutex_unlock(&matrix_dev->guests_lock);

	return ret;
}

/**
 * vfio_ap_mdev_hot_unplug_cfg - hot unplug the adapters, domains and control
 *				 domains that have been removed from the host's
 *				 AP configuration from a guest.
 *
 * @matrix_mdev: an ap_matrix_mdev object attached to a KVM guest.
 * @aprem: the adapters that have been removed from the host's AP configuration
 * @aqrem: the domains that have been removed from the host's AP configuration
 * @cdrem: the control domains that have been removed from the host's AP
 *	   configuration.
 */
static void vfio_ap_mdev_hot_unplug_cfg(struct ap_matrix_mdev *matrix_mdev,
					unsigned long *aprem,
					unsigned long *aqrem,
					unsigned long *cdrem)
{
	int do_hotplug = 0;

	if (!bitmap_empty(aprem, AP_DEVICES)) {
		do_hotplug |= bitmap_andnot(matrix_mdev->shadow_apcb.apm,
					    matrix_mdev->shadow_apcb.apm,
					    aprem, AP_DEVICES);
	}

	if (!bitmap_empty(aqrem, AP_DOMAINS)) {
		do_hotplug |= bitmap_andnot(matrix_mdev->shadow_apcb.aqm,
					    matrix_mdev->shadow_apcb.aqm,
					    aqrem, AP_DEVICES);
	}

	if (!bitmap_empty(cdrem, AP_DOMAINS))
		do_hotplug |= bitmap_andnot(matrix_mdev->shadow_apcb.adm,
					    matrix_mdev->shadow_apcb.adm,
					    cdrem, AP_DOMAINS);

	if (do_hotplug)
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);
}

/**
 * vfio_ap_mdev_cfg_remove - determines which guests are using the adapters,
 *			     domains and control domains that have been removed
 *			     from the host AP configuration and unplugs them
 *			     from those guests.
 *
 * @ap_remove:	bitmap specifying which adapters have been removed from the host
 *		config.
 * @aq_remove:	bitmap specifying which domains have been removed from the host
 *		config.
 * @cd_remove:	bitmap specifying which control domains have been removed from
 *		the host config.
 */
static void vfio_ap_mdev_cfg_remove(unsigned long *ap_remove,
				    unsigned long *aq_remove,
				    unsigned long *cd_remove)
{
	struct ap_matrix_mdev *matrix_mdev;
	DECLARE_BITMAP(aprem, AP_DEVICES);
	DECLARE_BITMAP(aqrem, AP_DOMAINS);
	DECLARE_BITMAP(cdrem, AP_DOMAINS);
	int do_remove = 0;

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		mutex_lock(&matrix_mdev->kvm->lock);
		mutex_lock(&matrix_dev->mdevs_lock);

		do_remove |= bitmap_and(aprem, ap_remove,
					  matrix_mdev->matrix.apm,
					  AP_DEVICES);
		do_remove |= bitmap_and(aqrem, aq_remove,
					  matrix_mdev->matrix.aqm,
					  AP_DOMAINS);
		do_remove |= bitmap_andnot(cdrem, cd_remove,
					     matrix_mdev->matrix.adm,
					     AP_DOMAINS);

		if (do_remove)
			vfio_ap_mdev_hot_unplug_cfg(matrix_mdev, aprem, aqrem,
						    cdrem);

		mutex_unlock(&matrix_dev->mdevs_lock);
		mutex_unlock(&matrix_mdev->kvm->lock);
	}
}

/**
 * vfio_ap_mdev_on_cfg_remove - responds to the removal of adapters, domains and
 *				control domains from the host AP configuration
 *				by unplugging them from the guests that are
 *				using them.
 * @cur_config_info: the current host AP configuration information
 * @prev_config_info: the previous host AP configuration information
 */
static void vfio_ap_mdev_on_cfg_remove(struct ap_config_info *cur_config_info,
				       struct ap_config_info *prev_config_info)
{
	int do_remove;
	DECLARE_BITMAP(aprem, AP_DEVICES);
	DECLARE_BITMAP(aqrem, AP_DOMAINS);
	DECLARE_BITMAP(cdrem, AP_DOMAINS);

	do_remove = bitmap_andnot(aprem,
				  (unsigned long *)prev_config_info->apm,
				  (unsigned long *)cur_config_info->apm,
				  AP_DEVICES);
	do_remove |= bitmap_andnot(aqrem,
				   (unsigned long *)prev_config_info->aqm,
				   (unsigned long *)cur_config_info->aqm,
				   AP_DEVICES);
	do_remove |= bitmap_andnot(cdrem,
				   (unsigned long *)prev_config_info->adm,
				   (unsigned long *)cur_config_info->adm,
				   AP_DEVICES);

	if (do_remove)
		vfio_ap_mdev_cfg_remove(aprem, aqrem, cdrem);
}

/**
 * vfio_ap_filter_apid_by_qtype: filter APIDs from an AP mask for adapters that
 *				 are older than AP type 10 (CEX4).
 * @apm: a bitmap of the APIDs to examine
 * @aqm: a bitmap of the APQIs of the queues to query for the AP type.
 */
static void vfio_ap_filter_apid_by_qtype(unsigned long *apm, unsigned long *aqm)
{
	bool apid_cleared;
	struct ap_queue_status status;
	unsigned long apid, apqi, info;
	int qtype, qtype_mask = 0xff000000;

	for_each_set_bit_inv(apid, apm, AP_DEVICES) {
		apid_cleared = false;

		for_each_set_bit_inv(apqi, aqm, AP_DOMAINS) {
			status = ap_test_queue(AP_MKQID(apid, apqi), 1, &info);
			switch (status.response_code) {
			/*
			 * According to the architecture in each case
			 * below, the queue's info should be filled.
			 */
			case AP_RESPONSE_NORMAL:
			case AP_RESPONSE_RESET_IN_PROGRESS:
			case AP_RESPONSE_DECONFIGURED:
			case AP_RESPONSE_CHECKSTOPPED:
			case AP_RESPONSE_BUSY:
				qtype = info & qtype_mask;

				/*
				 * The vfio_ap device driver only
				 * supports CEX4 and newer adapters, so
				 * remove the APID if the adapter is
				 * older than a CEX4.
				 */
				if (qtype < AP_DEVICE_TYPE_CEX4) {
					clear_bit_inv(apid, apm);
					apid_cleared = true;
				}

				break;

			default:
				/*
				 * If we don't know the adapter type,
				 * clear its APID since it can't be
				 * determined whether the vfio_ap
				 * device driver supports it.
				 */
				clear_bit_inv(apid, apm);
				apid_cleared = true;
				break;
			}

			/*
			 * If we've already cleared the APID from the apm, there
			 * is no need to continue examining the remainin AP
			 * queues to determine the type of the adapter.
			 */
			if (apid_cleared)
				continue;
		}
	}
}

/**
 * vfio_ap_mdev_cfg_add - store bitmaps specifying the adapters, domains and
 *			  control domains that have been added to the host's
 *			  AP configuration for each matrix mdev to which they
 *			  are assigned.
 *
 * @apm_add: a bitmap specifying the adapters that have been added to the AP
 *	     configuration.
 * @aqm_add: a bitmap specifying the domains that have been added to the AP
 *	     configuration.
 * @adm_add: a bitmap specifying the control domains that have been added to the
 *	     AP configuration.
 */
static void vfio_ap_mdev_cfg_add(unsigned long *apm_add, unsigned long *aqm_add,
				 unsigned long *adm_add)
{
	struct ap_matrix_mdev *matrix_mdev;

	if (list_empty(&matrix_dev->mdev_list))
		return;

	vfio_ap_filter_apid_by_qtype(apm_add, aqm_add);

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		bitmap_and(matrix_mdev->apm_add,
			   matrix_mdev->matrix.apm, apm_add, AP_DEVICES);
		bitmap_and(matrix_mdev->aqm_add,
			   matrix_mdev->matrix.aqm, aqm_add, AP_DOMAINS);
		bitmap_and(matrix_mdev->adm_add,
			   matrix_mdev->matrix.adm, adm_add, AP_DEVICES);
	}
}

/**
 * vfio_ap_mdev_on_cfg_add - responds to the addition of adapters, domains and
 *			     control domains to the host AP configuration
 *			     by updating the bitmaps that specify what adapters,
 *			     domains and control domains have been added so they
 *			     can be hot plugged into the guest when the AP bus
 *			     scan completes (see vfio_ap_on_scan_complete
 *			     function).
 * @cur_config_info: the current AP configuration information
 * @prev_config_info: the previous AP configuration information
 */
static void vfio_ap_mdev_on_cfg_add(struct ap_config_info *cur_config_info,
				    struct ap_config_info *prev_config_info)
{
	bool do_add;
	DECLARE_BITMAP(apm_add, AP_DEVICES);
	DECLARE_BITMAP(aqm_add, AP_DOMAINS);
	DECLARE_BITMAP(adm_add, AP_DOMAINS);

	do_add = bitmap_andnot(apm_add,
			       (unsigned long *)cur_config_info->apm,
			       (unsigned long *)prev_config_info->apm,
			       AP_DEVICES);
	do_add |= bitmap_andnot(aqm_add,
				(unsigned long *)cur_config_info->aqm,
				(unsigned long *)prev_config_info->aqm,
				AP_DOMAINS);
	do_add |= bitmap_andnot(adm_add,
				(unsigned long *)cur_config_info->adm,
				(unsigned long *)prev_config_info->adm,
				AP_DOMAINS);

	if (do_add)
		vfio_ap_mdev_cfg_add(apm_add, aqm_add, adm_add);
}

/**
 * vfio_ap_on_cfg_changed - handles notification of changes to the host AP
 *			    configuration.
 *
 * @cur_cfg_info: the current host AP configuration
 * @prev_cfg_info: the previous host AP configuration
 */
void vfio_ap_on_cfg_changed(struct ap_config_info *cur_cfg_info,
			    struct ap_config_info *prev_cfg_info)
{
	if (!cur_cfg_info || !prev_cfg_info)
		return;

	mutex_lock(&matrix_dev->guests_lock);

	vfio_ap_mdev_on_cfg_remove(cur_cfg_info, prev_cfg_info);
	vfio_ap_mdev_on_cfg_add(cur_cfg_info, prev_cfg_info);
	memcpy(&matrix_dev->info, cur_cfg_info, sizeof(*cur_cfg_info));

	mutex_unlock(&matrix_dev->guests_lock);
}

static void vfio_ap_mdev_hot_plug_cfg(struct ap_matrix_mdev *matrix_mdev)
{
	bool filter_domains, filter_adapters, filter_cdoms, do_hotplug = false;

	mutex_lock(&matrix_mdev->kvm->lock);
	mutex_lock(&matrix_dev->mdevs_lock);

	filter_adapters = bitmap_intersects(matrix_mdev->matrix.apm,
					    matrix_mdev->apm_add, AP_DEVICES);
	filter_domains = bitmap_intersects(matrix_mdev->matrix.aqm,
					   matrix_mdev->aqm_add, AP_DOMAINS);
	filter_cdoms = bitmap_intersects(matrix_mdev->matrix.adm,
					 matrix_mdev->adm_add, AP_DOMAINS);

	if (filter_adapters || filter_domains)
		do_hotplug = vfio_ap_mdev_filter_matrix(matrix_mdev);

	if (filter_cdoms)
		do_hotplug |= vfio_ap_mdev_filter_cdoms(matrix_mdev);

	if (do_hotplug)
		vfio_ap_mdev_update_guest_apcb(matrix_mdev);

	mutex_unlock(&matrix_dev->mdevs_lock);
	mutex_unlock(&matrix_mdev->kvm->lock);
}

void vfio_ap_on_scan_complete(struct ap_config_info *new_config_info,
			      struct ap_config_info *old_config_info)
{
	struct ap_matrix_mdev *matrix_mdev;

	mutex_lock(&matrix_dev->guests_lock);

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		if (bitmap_empty(matrix_mdev->apm_add, AP_DEVICES) &&
		    bitmap_empty(matrix_mdev->aqm_add, AP_DOMAINS) &&
		    bitmap_empty(matrix_mdev->adm_add, AP_DOMAINS))
			continue;

		vfio_ap_mdev_hot_plug_cfg(matrix_mdev);
		bitmap_clear(matrix_mdev->apm_add, 0, AP_DEVICES);
		bitmap_clear(matrix_mdev->aqm_add, 0, AP_DOMAINS);
		bitmap_clear(matrix_mdev->adm_add, 0, AP_DOMAINS);
	}

	mutex_unlock(&matrix_dev->guests_lock);
}
