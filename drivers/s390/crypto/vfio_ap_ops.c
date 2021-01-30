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
#include <asm/kvm.h>
#include <asm/zcrypt.h>

#include "vfio_ap_private.h"

#define VFIO_AP_MDEV_TYPE_HWVIRT "passthrough"
#define VFIO_AP_MDEV_NAME_HWVIRT "VFIO AP Passthrough Device"

static int vfio_ap_mdev_reset_queues(struct mdev_device *mdev);
static struct vfio_ap_queue *vfio_ap_find_queue(int apqn);

static int match_apqn(struct device *dev, const void *data)
{
	struct vfio_ap_queue *q = dev_get_drvdata(dev);

	return (q->apqn == *(int *)(data)) ? 1 : 0;
}

/**
 * vfio_ap_get_queue: Retrieve a queue with a specific APQN from a list
 * @matrix_mdev: the associated mediated matrix
 * @apqn: The queue APQN
 *
 * Retrieve a queue with a specific APQN from the list of the
 * devices of the vfio_ap_drv.
 * Verify that the APID and the APQI are set in the matrix.
 *
 * Returns the pointer to the associated vfio_ap_queue
 */
static struct vfio_ap_queue *vfio_ap_get_queue(
					struct ap_matrix_mdev *matrix_mdev,
					int apqn)
{
	struct vfio_ap_queue *q;

	if (!test_bit_inv(AP_QID_CARD(apqn), matrix_mdev->matrix.apm))
		return NULL;
	if (!test_bit_inv(AP_QID_QUEUE(apqn), matrix_mdev->matrix.aqm))
		return NULL;

	q = vfio_ap_find_queue(apqn);
	if (q)
		q->matrix_mdev = matrix_mdev;

	return q;
}

/**
 * vfio_ap_wait_for_irqclear
 * @apqn: The AP Queue number
 *
 * Checks the IRQ bit for the status of this APQN using ap_tapq.
 * Returns if the ap_tapq function succeeded and the bit is clear.
 * Returns if ap_tapq function failed with invalid, deconfigured or
 * checkstopped AP.
 * Otherwise retries up to 5 times after waiting 20ms.
 *
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
 * vfio_ap_free_aqic_resources
 * @q: The vfio_ap_queue
 *
 * Unregisters the ISC in the GIB when the saved ISC not invalid.
 * Unpin the guest's page holding the NIB when it exist.
 * Reset the saved_pfn and saved_isc to invalid values.
 *
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
	if (q->saved_pfn && !WARN_ON(!q->matrix_mdev)) {
		vfio_unpin_pages(mdev_dev(q->matrix_mdev->mdev),
				 &q->saved_pfn, 1);
		q->saved_pfn = 0;
	}
}

/**
 * vfio_ap_irq_disable
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
 */
static struct ap_queue_status vfio_ap_irq_disable(struct vfio_ap_queue *q)
{
	struct ap_qirq_ctrl aqic_gisa = {};
	struct ap_queue_status status;
	int retries = 5;

	do {
		status = ap_aqic(q->apqn, aqic_gisa, NULL);
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
	q->matrix_mdev = NULL;
	return status;
}

/**
 * vfio_ap_setirq: Enable Interruption for a APQN
 *
 * @dev: the device associated with the ap_queue
 * @q:	 the vfio_ap_queue holding AQIC parameters
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
 */
static struct ap_queue_status vfio_ap_irq_enable(struct vfio_ap_queue *q,
						 int isc,
						 unsigned long nib)
{
	struct ap_qirq_ctrl aqic_gisa = {};
	struct ap_queue_status status = {};
	struct kvm_s390_gisa *gisa;
	struct kvm *kvm;
	unsigned long h_nib, g_pfn, h_pfn;
	int ret;

	g_pfn = nib >> PAGE_SHIFT;
	ret = vfio_pin_pages(mdev_dev(q->matrix_mdev->mdev), &g_pfn, 1,
			     IOMMU_READ | IOMMU_WRITE, &h_pfn);
	switch (ret) {
	case 1:
		break;
	default:
		status.response_code = AP_RESPONSE_INVALID_ADDRESS;
		return status;
	}

	kvm = q->matrix_mdev->kvm;
	gisa = kvm->arch.gisa_int.origin;

	h_nib = (h_pfn << PAGE_SHIFT) | (nib & ~PAGE_MASK);
	aqic_gisa.gisc = isc;
	aqic_gisa.isc = kvm_s390_gisc_register(kvm, isc);
	aqic_gisa.ir = 1;
	aqic_gisa.gisa = (uint64_t)gisa >> 4;

	status = ap_aqic(q->apqn, aqic_gisa, (void *)h_nib);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		/* See if we did clear older IRQ configuration */
		vfio_ap_free_aqic_resources(q);
		q->saved_pfn = g_pfn;
		q->saved_isc = isc;
		break;
	case AP_RESPONSE_OTHERWISE_CHANGED:
		/* We could not modify IRQ setings: clear new configuration */
		vfio_unpin_pages(mdev_dev(q->matrix_mdev->mdev), &g_pfn, 1);
		kvm_s390_gisc_unregister(kvm, isc);
		break;
	default:
		pr_warn("%s: apqn %04x: response: %02x\n", __func__, q->apqn,
			status.response_code);
		vfio_ap_irq_disable(q);
		break;
	}

	return status;
}

/**
 * handle_pqap: PQAP instruction callback
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
 * Return 0 if we could handle the request inside KVM.
 * otherwise, returns -EOPNOTSUPP to let QEMU handle the fault.
 */
static int handle_pqap(struct kvm_vcpu *vcpu)
{
	uint64_t status;
	uint16_t apqn;
	struct vfio_ap_queue *q;
	struct ap_queue_status qstatus = {
			       .response_code = AP_RESPONSE_Q_NOT_AVAIL, };
	struct ap_matrix_mdev *matrix_mdev;

	/* If we do not use the AIV facility just go to userland */
	if (!(vcpu->arch.sie_block->eca & ECA_AIV))
		return -EOPNOTSUPP;

	apqn = vcpu->run->s.regs.gprs[0] & 0xffff;
	mutex_lock(&matrix_dev->lock);

	if (!vcpu->kvm->arch.crypto.pqap_hook)
		goto out_unlock;
	matrix_mdev = container_of(vcpu->kvm->arch.crypto.pqap_hook,
				   struct ap_matrix_mdev, pqap_hook);

	q = vfio_ap_get_queue(matrix_mdev, apqn);
	if (!q)
		goto out_unlock;

	status = vcpu->run->s.regs.gprs[1];

	/* If IR bit(16) is set we enable the interrupt */
	if ((status >> (63 - 16)) & 0x01)
		qstatus = vfio_ap_irq_enable(q, status & 0x07,
					     vcpu->run->s.regs.gprs[2]);
	else
		qstatus = vfio_ap_irq_disable(q);

out_unlock:
	memcpy(&vcpu->run->s.regs.gprs[1], &qstatus, sizeof(qstatus));
	vcpu->run->s.regs.gprs[1] >>= 32;
	mutex_unlock(&matrix_dev->lock);
	return 0;
}

static void vfio_ap_matrix_init(struct ap_config_info *info,
				struct ap_matrix *matrix)
{
	matrix->apm_max = info->apxa ? info->Na : 63;
	matrix->aqm_max = info->apxa ? info->Nd : 15;
	matrix->adm_max = info->apxa ? info->Nd : 15;
}

static int vfio_ap_mdev_create(struct kobject *kobj, struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev;

	if ((atomic_dec_if_positive(&matrix_dev->available_instances) < 0))
		return -EPERM;

	matrix_mdev = kzalloc(sizeof(*matrix_mdev), GFP_KERNEL);
	if (!matrix_mdev) {
		atomic_inc(&matrix_dev->available_instances);
		return -ENOMEM;
	}

	matrix_mdev->mdev = mdev;
	vfio_ap_matrix_init(&matrix_dev->info, &matrix_mdev->matrix);
	mdev_set_drvdata(mdev, matrix_mdev);
	matrix_mdev->pqap_hook.hook = handle_pqap;
	matrix_mdev->pqap_hook.owner = THIS_MODULE;
	mutex_lock(&matrix_dev->lock);
	list_add(&matrix_mdev->node, &matrix_dev->mdev_list);
	mutex_unlock(&matrix_dev->lock);

	return 0;
}

static int vfio_ap_mdev_remove(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	if (matrix_mdev->kvm)
		return -EBUSY;

	mutex_lock(&matrix_dev->lock);
	vfio_ap_mdev_reset_queues(mdev);
	list_del(&matrix_mdev->node);
	mutex_unlock(&matrix_dev->lock);

	kfree(matrix_mdev);
	mdev_set_drvdata(mdev, NULL);
	atomic_inc(&matrix_dev->available_instances);

	return 0;
}

static ssize_t name_show(struct kobject *kobj, struct device *dev, char *buf)
{
	return sprintf(buf, "%s\n", VFIO_AP_MDEV_NAME_HWVIRT);
}

static MDEV_TYPE_ATTR_RO(name);

static ssize_t available_instances_show(struct kobject *kobj,
					struct device *dev, char *buf)
{
	return sprintf(buf, "%d\n",
		       atomic_read(&matrix_dev->available_instances));
}

static MDEV_TYPE_ATTR_RO(available_instances);

static ssize_t device_api_show(struct kobject *kobj, struct device *dev,
			       char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_AP_STRING);
}

static MDEV_TYPE_ATTR_RO(device_api);

static struct attribute *vfio_ap_mdev_type_attrs[] = {
	&mdev_type_attr_name.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_available_instances.attr,
	NULL,
};

static struct attribute_group vfio_ap_mdev_hwvirt_type_group = {
	.name = VFIO_AP_MDEV_TYPE_HWVIRT,
	.attrs = vfio_ap_mdev_type_attrs,
};

static struct attribute_group *vfio_ap_mdev_type_groups[] = {
	&vfio_ap_mdev_hwvirt_type_group,
	NULL,
};

struct vfio_ap_queue_reserved {
	unsigned long *apid;
	unsigned long *apqi;
	bool reserved;
};

/**
 * vfio_ap_has_queue
 *
 * @dev: an AP queue device
 * @data: a struct vfio_ap_queue_reserved reference
 *
 * Flags whether the AP queue device (@dev) has a queue ID containing the APQN,
 * apid or apqi specified in @data:
 *
 * - If @data contains both an apid and apqi value, then @data will be flagged
 *   as reserved if the APID and APQI fields for the AP queue device matches
 *
 * - If @data contains only an apid value, @data will be flagged as
 *   reserved if the APID field in the AP queue device matches
 *
 * - If @data contains only an apqi value, @data will be flagged as
 *   reserved if the APQI field in the AP queue device matches
 *
 * Returns 0 to indicate the input to function succeeded. Returns -EINVAL if
 * @data does not contain either an apid or apqi.
 */
static int vfio_ap_has_queue(struct device *dev, void *data)
{
	struct vfio_ap_queue_reserved *qres = data;
	struct ap_queue *ap_queue = to_ap_queue(dev);
	ap_qid_t qid;
	unsigned long id;

	if (qres->apid && qres->apqi) {
		qid = AP_MKQID(*qres->apid, *qres->apqi);
		if (qid == ap_queue->qid)
			qres->reserved = true;
	} else if (qres->apid && !qres->apqi) {
		id = AP_QID_CARD(ap_queue->qid);
		if (id == *qres->apid)
			qres->reserved = true;
	} else if (!qres->apid && qres->apqi) {
		id = AP_QID_QUEUE(ap_queue->qid);
		if (id == *qres->apqi)
			qres->reserved = true;
	} else {
		return -EINVAL;
	}

	return 0;
}

/**
 * vfio_ap_verify_queue_reserved
 *
 * @matrix_dev: a mediated matrix device
 * @apid: an AP adapter ID
 * @apqi: an AP queue index
 *
 * Verifies that the AP queue with @apid/@apqi is reserved by the VFIO AP device
 * driver according to the following rules:
 *
 * - If both @apid and @apqi are not NULL, then there must be an AP queue
 *   device bound to the vfio_ap driver with the APQN identified by @apid and
 *   @apqi
 *
 * - If only @apid is not NULL, then there must be an AP queue device bound
 *   to the vfio_ap driver with an APQN containing @apid
 *
 * - If only @apqi is not NULL, then there must be an AP queue device bound
 *   to the vfio_ap driver with an APQN containing @apqi
 *
 * Returns 0 if the AP queue is reserved; otherwise, returns -EADDRNOTAVAIL.
 */
static int vfio_ap_verify_queue_reserved(unsigned long *apid,
					 unsigned long *apqi)
{
	int ret;
	struct vfio_ap_queue_reserved qres;

	qres.apid = apid;
	qres.apqi = apqi;
	qres.reserved = false;

	ret = driver_for_each_device(&matrix_dev->vfio_ap_drv->driver, NULL,
				     &qres, vfio_ap_has_queue);
	if (ret)
		return ret;

	if (qres.reserved)
		return 0;

	return -EADDRNOTAVAIL;
}

static int
vfio_ap_mdev_verify_queues_reserved_for_apid(struct ap_matrix_mdev *matrix_mdev,
					     unsigned long apid)
{
	int ret;
	unsigned long apqi;
	unsigned long nbits = matrix_mdev->matrix.aqm_max + 1;

	if (find_first_bit_inv(matrix_mdev->matrix.aqm, nbits) >= nbits)
		return vfio_ap_verify_queue_reserved(&apid, NULL);

	for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, nbits) {
		ret = vfio_ap_verify_queue_reserved(&apid, &apqi);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * vfio_ap_mdev_verify_no_sharing
 *
 * Verifies that the APQNs derived from the cross product of the AP adapter IDs
 * and AP queue indexes comprising the AP matrix are not configured for another
 * mediated device. AP queue sharing is not allowed.
 *
 * @matrix_mdev: the mediated matrix device
 *
 * Returns 0 if the APQNs are not shared, otherwise; returns -EADDRINUSE.
 */
static int vfio_ap_mdev_verify_no_sharing(struct ap_matrix_mdev *matrix_mdev)
{
	struct ap_matrix_mdev *lstdev;
	DECLARE_BITMAP(apm, AP_DEVICES);
	DECLARE_BITMAP(aqm, AP_DOMAINS);

	list_for_each_entry(lstdev, &matrix_dev->mdev_list, node) {
		if (matrix_mdev == lstdev)
			continue;

		memset(apm, 0, sizeof(apm));
		memset(aqm, 0, sizeof(aqm));

		/*
		 * We work on full longs, as we can only exclude the leftover
		 * bits in non-inverse order. The leftover is all zeros.
		 */
		if (!bitmap_and(apm, matrix_mdev->matrix.apm,
				lstdev->matrix.apm, AP_DEVICES))
			continue;

		if (!bitmap_and(aqm, matrix_mdev->matrix.aqm,
				lstdev->matrix.aqm, AP_DOMAINS))
			continue;

		return -EADDRINUSE;
	}

	return 0;
}

/**
 * assign_adapter_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_adapter attribute
 * @buf:	a buffer containing the AP adapter number (APID) to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APID from @buf and sets the corresponding bit in the mediated
 * matrix device's APM.
 *
 * Returns the number of bytes processed if the APID is valid; otherwise,
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
 */
static ssize_t assign_adapter_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	unsigned long apid;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	/* If the guest is running, disallow assignment of adapter */
	if (matrix_mdev->kvm)
		return -EBUSY;

	ret = kstrtoul(buf, 0, &apid);
	if (ret)
		return ret;

	if (apid > matrix_mdev->matrix.apm_max)
		return -ENODEV;

	/*
	 * Set the bit in the AP mask (APM) corresponding to the AP adapter
	 * number (APID). The bits in the mask, from most significant to least
	 * significant bit, correspond to APIDs 0-255.
	 */
	mutex_lock(&matrix_dev->lock);

	ret = vfio_ap_mdev_verify_queues_reserved_for_apid(matrix_mdev, apid);
	if (ret)
		goto done;

	set_bit_inv(apid, matrix_mdev->matrix.apm);

	ret = vfio_ap_mdev_verify_no_sharing(matrix_mdev);
	if (ret)
		goto share_err;

	ret = count;
	goto done;

share_err:
	clear_bit_inv(apid, matrix_mdev->matrix.apm);
done:
	mutex_unlock(&matrix_dev->lock);

	return ret;
}
static DEVICE_ATTR_WO(assign_adapter);

/**
 * unassign_adapter_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_adapter attribute
 * @buf:	a buffer containing the adapter number (APID) to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APID from @buf and clears the corresponding bit in the mediated
 * matrix device's APM.
 *
 * Returns the number of bytes processed if the APID is valid; otherwise,
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
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	/* If the guest is running, disallow un-assignment of adapter */
	if (matrix_mdev->kvm)
		return -EBUSY;

	ret = kstrtoul(buf, 0, &apid);
	if (ret)
		return ret;

	if (apid > matrix_mdev->matrix.apm_max)
		return -ENODEV;

	mutex_lock(&matrix_dev->lock);
	clear_bit_inv((unsigned long)apid, matrix_mdev->matrix.apm);
	mutex_unlock(&matrix_dev->lock);

	return count;
}
static DEVICE_ATTR_WO(unassign_adapter);

static int
vfio_ap_mdev_verify_queues_reserved_for_apqi(struct ap_matrix_mdev *matrix_mdev,
					     unsigned long apqi)
{
	int ret;
	unsigned long apid;
	unsigned long nbits = matrix_mdev->matrix.apm_max + 1;

	if (find_first_bit_inv(matrix_mdev->matrix.apm, nbits) >= nbits)
		return vfio_ap_verify_queue_reserved(NULL, &apqi);

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, nbits) {
		ret = vfio_ap_verify_queue_reserved(&apid, &apqi);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * assign_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APQI from @buf and sets the corresponding bit in the mediated
 * matrix device's AQM.
 *
 * Returns the number of bytes processed if the APQI is valid; otherwise returns
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
 */
static ssize_t assign_domain_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;
	unsigned long apqi;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long max_apqi = matrix_mdev->matrix.aqm_max;

	/* If the guest is running, disallow assignment of domain */
	if (matrix_mdev->kvm)
		return -EBUSY;

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		return ret;
	if (apqi > max_apqi)
		return -ENODEV;

	mutex_lock(&matrix_dev->lock);

	ret = vfio_ap_mdev_verify_queues_reserved_for_apqi(matrix_mdev, apqi);
	if (ret)
		goto done;

	set_bit_inv(apqi, matrix_mdev->matrix.aqm);

	ret = vfio_ap_mdev_verify_no_sharing(matrix_mdev);
	if (ret)
		goto share_err;

	ret = count;
	goto done;

share_err:
	clear_bit_inv(apqi, matrix_mdev->matrix.aqm);
done:
	mutex_unlock(&matrix_dev->lock);

	return ret;
}
static DEVICE_ATTR_WO(assign_domain);


/**
 * unassign_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be unassigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APQI from @buf and clears the corresponding bit in the
 * mediated matrix device's AQM.
 *
 * Returns the number of bytes processed if the APQI is valid; otherwise,
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
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	/* If the guest is running, disallow un-assignment of domain */
	if (matrix_mdev->kvm)
		return -EBUSY;

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		return ret;

	if (apqi > matrix_mdev->matrix.aqm_max)
		return -ENODEV;

	mutex_lock(&matrix_dev->lock);
	clear_bit_inv((unsigned long)apqi, matrix_mdev->matrix.aqm);
	mutex_unlock(&matrix_dev->lock);

	return count;
}
static DEVICE_ATTR_WO(unassign_domain);

/**
 * assign_control_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be assigned
 * @count:	the number of bytes in @buf
 *
 * Parses the domain ID from @buf and sets the corresponding bit in the mediated
 * matrix device's ADM.
 *
 * Returns the number of bytes processed if the domain ID is valid; otherwise,
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
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	/* If the guest is running, disallow assignment of control domain */
	if (matrix_mdev->kvm)
		return -EBUSY;

	ret = kstrtoul(buf, 0, &id);
	if (ret)
		return ret;

	if (id > matrix_mdev->matrix.adm_max)
		return -ENODEV;

	/* Set the bit in the ADM (bitmask) corresponding to the AP control
	 * domain number (id). The bits in the mask, from most significant to
	 * least significant, correspond to IDs 0 up to the one less than the
	 * number of control domains that can be assigned.
	 */
	mutex_lock(&matrix_dev->lock);
	set_bit_inv(id, matrix_mdev->matrix.adm);
	mutex_unlock(&matrix_dev->lock);

	return count;
}
static DEVICE_ATTR_WO(assign_control_domain);

/**
 * unassign_control_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Parses the domain ID from @buf and clears the corresponding bit in the
 * mediated matrix device's ADM.
 *
 * Returns the number of bytes processed if the domain ID is valid; otherwise,
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
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long max_domid =  matrix_mdev->matrix.adm_max;

	/* If the guest is running, disallow un-assignment of control domain */
	if (matrix_mdev->kvm)
		return -EBUSY;

	ret = kstrtoul(buf, 0, &domid);
	if (ret)
		return ret;
	if (domid > max_domid)
		return -ENODEV;

	mutex_lock(&matrix_dev->lock);
	clear_bit_inv(domid, matrix_mdev->matrix.adm);
	mutex_unlock(&matrix_dev->lock);

	return count;
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
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long max_domid = matrix_mdev->matrix.adm_max;

	mutex_lock(&matrix_dev->lock);
	for_each_set_bit_inv(id, matrix_mdev->matrix.adm, max_domid + 1) {
		n = sprintf(bufpos, "%04lx\n", id);
		bufpos += n;
		nchars += n;
	}
	mutex_unlock(&matrix_dev->lock);

	return nchars;
}
static DEVICE_ATTR_RO(control_domains);

static ssize_t matrix_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	char *bufpos = buf;
	unsigned long apid;
	unsigned long apqi;
	unsigned long apid1;
	unsigned long apqi1;
	unsigned long napm_bits = matrix_mdev->matrix.apm_max + 1;
	unsigned long naqm_bits = matrix_mdev->matrix.aqm_max + 1;
	int nchars = 0;
	int n;

	apid1 = find_first_bit_inv(matrix_mdev->matrix.apm, napm_bits);
	apqi1 = find_first_bit_inv(matrix_mdev->matrix.aqm, naqm_bits);

	mutex_lock(&matrix_dev->lock);

	if ((apid1 < napm_bits) && (apqi1 < naqm_bits)) {
		for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, napm_bits) {
			for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm,
					     naqm_bits) {
				n = sprintf(bufpos, "%02lx.%04lx\n", apid,
					    apqi);
				bufpos += n;
				nchars += n;
			}
		}
	} else if (apid1 < napm_bits) {
		for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, napm_bits) {
			n = sprintf(bufpos, "%02lx.\n", apid);
			bufpos += n;
			nchars += n;
		}
	} else if (apqi1 < naqm_bits) {
		for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, naqm_bits) {
			n = sprintf(bufpos, ".%04lx\n", apqi);
			bufpos += n;
			nchars += n;
		}
	}

	mutex_unlock(&matrix_dev->lock);

	return nchars;
}
static DEVICE_ATTR_RO(matrix);

static struct attribute *vfio_ap_mdev_attrs[] = {
	&dev_attr_assign_adapter.attr,
	&dev_attr_unassign_adapter.attr,
	&dev_attr_assign_domain.attr,
	&dev_attr_unassign_domain.attr,
	&dev_attr_assign_control_domain.attr,
	&dev_attr_unassign_control_domain.attr,
	&dev_attr_control_domains.attr,
	&dev_attr_matrix.attr,
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
 * vfio_ap_mdev_set_kvm
 *
 * @matrix_mdev: a mediated matrix device
 * @kvm: reference to KVM instance
 *
 * Verifies no other mediated matrix device has @kvm and sets a reference to
 * it in @matrix_mdev->kvm.
 *
 * Return 0 if no other mediated matrix device has a reference to @kvm;
 * otherwise, returns an -EPERM.
 */
static int vfio_ap_mdev_set_kvm(struct ap_matrix_mdev *matrix_mdev,
				struct kvm *kvm)
{
	struct ap_matrix_mdev *m;

	list_for_each_entry(m, &matrix_dev->mdev_list, node) {
		if ((m != matrix_mdev) && (m->kvm == kvm))
			return -EPERM;
	}

	matrix_mdev->kvm = kvm;
	kvm_get_kvm(kvm);
	kvm->arch.crypto.pqap_hook = &matrix_mdev->pqap_hook;

	return 0;
}

/*
 * vfio_ap_mdev_iommu_notifier: IOMMU notifier callback
 *
 * @nb: The notifier block
 * @action: Action to be taken
 * @data: data associated with the request
 *
 * For an UNMAP request, unpin the guest IOVA (the NIB guest address we
 * pinned before). Other requests are ignored.
 *
 */
static int vfio_ap_mdev_iommu_notifier(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct ap_matrix_mdev *matrix_mdev;

	matrix_mdev = container_of(nb, struct ap_matrix_mdev, iommu_notifier);

	if (action == VFIO_IOMMU_NOTIFY_DMA_UNMAP) {
		struct vfio_iommu_type1_dma_unmap *unmap = data;
		unsigned long g_pfn = unmap->iova >> PAGE_SHIFT;

		vfio_unpin_pages(mdev_dev(matrix_mdev->mdev), &g_pfn, 1);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static void vfio_ap_mdev_unset_kvm(struct ap_matrix_mdev *matrix_mdev)
{
	kvm_arch_crypto_clear_masks(matrix_mdev->kvm);
	matrix_mdev->kvm->arch.crypto.pqap_hook = NULL;
	vfio_ap_mdev_reset_queues(matrix_mdev->mdev);
	kvm_put_kvm(matrix_mdev->kvm);
	matrix_mdev->kvm = NULL;
}

static int vfio_ap_mdev_group_notifier(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	int ret, notify_rc = NOTIFY_OK;
	struct ap_matrix_mdev *matrix_mdev;

	if (action != VFIO_GROUP_NOTIFY_SET_KVM)
		return NOTIFY_OK;

	matrix_mdev = container_of(nb, struct ap_matrix_mdev, group_notifier);
	mutex_lock(&matrix_dev->lock);

	if (!data) {
		if (matrix_mdev->kvm)
			vfio_ap_mdev_unset_kvm(matrix_mdev);
		goto notify_done;
	}

	ret = vfio_ap_mdev_set_kvm(matrix_mdev, data);
	if (ret) {
		notify_rc = NOTIFY_DONE;
		goto notify_done;
	}

	/* If there is no CRYCB pointer, then we can't copy the masks */
	if (!matrix_mdev->kvm->arch.crypto.crycbd) {
		notify_rc = NOTIFY_DONE;
		goto notify_done;
	}

	kvm_arch_crypto_set_masks(matrix_mdev->kvm, matrix_mdev->matrix.apm,
				  matrix_mdev->matrix.aqm,
				  matrix_mdev->matrix.adm);

notify_done:
	mutex_unlock(&matrix_dev->lock);
	return notify_rc;
}

static struct vfio_ap_queue *vfio_ap_find_queue(int apqn)
{
	struct device *dev;
	struct vfio_ap_queue *q = NULL;

	dev = driver_find_device(&matrix_dev->vfio_ap_drv->driver, NULL,
				 &apqn, match_apqn);
	if (dev) {
		q = dev_get_drvdata(dev);
		put_device(dev);
	}

	return q;
}

int vfio_ap_mdev_reset_queue(struct vfio_ap_queue *q,
			     unsigned int retry)
{
	struct ap_queue_status status;
	int ret;
	int retry2 = 2;

	if (!q)
		return 0;

retry_zapq:
	status = ap_zapq(q->apqn);
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
		WARN_ON_ONCE(status.irq_enabled);
		ret = -EBUSY;
		goto free_resources;
	default:
		/* things are really broken, give up */
		WARN(true, "PQAP/ZAPQ completed with invalid rc (%x)\n",
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
	WARN_ON_ONCE(retry2 <= 0);

free_resources:
	vfio_ap_free_aqic_resources(q);

	return ret;
}

static int vfio_ap_mdev_reset_queues(struct mdev_device *mdev)
{
	int ret;
	int rc = 0;
	unsigned long apid, apqi;
	struct vfio_ap_queue *q;
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm,
			     matrix_mdev->matrix.apm_max + 1) {
		for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm,
				     matrix_mdev->matrix.aqm_max + 1) {
			q = vfio_ap_find_queue(AP_MKQID(apid, apqi));
			ret = vfio_ap_mdev_reset_queue(q, 1);
			/*
			 * Regardless whether a queue turns out to be busy, or
			 * is not operational, we need to continue resetting
			 * the remaining queues.
			 */
			if (ret)
				rc = ret;
		}
	}

	return rc;
}

static int vfio_ap_mdev_open(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long events;
	int ret;


	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	matrix_mdev->group_notifier.notifier_call = vfio_ap_mdev_group_notifier;
	events = VFIO_GROUP_NOTIFY_SET_KVM;

	ret = vfio_register_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY,
				     &events, &matrix_mdev->group_notifier);
	if (ret) {
		module_put(THIS_MODULE);
		return ret;
	}

	matrix_mdev->iommu_notifier.notifier_call = vfio_ap_mdev_iommu_notifier;
	events = VFIO_IOMMU_NOTIFY_DMA_UNMAP;
	ret = vfio_register_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				     &events, &matrix_mdev->iommu_notifier);
	if (!ret)
		return ret;

	vfio_unregister_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY,
				 &matrix_mdev->group_notifier);
	module_put(THIS_MODULE);
	return ret;
}

static void vfio_ap_mdev_release(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	mutex_lock(&matrix_dev->lock);
	if (matrix_mdev->kvm)
		vfio_ap_mdev_unset_kvm(matrix_mdev);
	mutex_unlock(&matrix_dev->lock);

	vfio_unregister_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				 &matrix_mdev->iommu_notifier);
	vfio_unregister_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY,
				 &matrix_mdev->group_notifier);
	module_put(THIS_MODULE);
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

	return copy_to_user((void __user *)arg, &info, minsz);
}

static ssize_t vfio_ap_mdev_ioctl(struct mdev_device *mdev,
				    unsigned int cmd, unsigned long arg)
{
	int ret;

	mutex_lock(&matrix_dev->lock);
	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
		ret = vfio_ap_mdev_get_device_info(arg);
		break;
	case VFIO_DEVICE_RESET:
		ret = vfio_ap_mdev_reset_queues(mdev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&matrix_dev->lock);

	return ret;
}

static const struct mdev_parent_ops vfio_ap_matrix_ops = {
	.owner			= THIS_MODULE,
	.supported_type_groups	= vfio_ap_mdev_type_groups,
	.mdev_attr_groups	= vfio_ap_mdev_attr_groups,
	.create			= vfio_ap_mdev_create,
	.remove			= vfio_ap_mdev_remove,
	.open			= vfio_ap_mdev_open,
	.release		= vfio_ap_mdev_release,
	.ioctl			= vfio_ap_mdev_ioctl,
};

int vfio_ap_mdev_register(void)
{
	atomic_set(&matrix_dev->available_instances, MAX_ZDEV_ENTRIES_EXT);

	return mdev_register_device(&matrix_dev->device, &vfio_ap_matrix_ops);
}

void vfio_ap_mdev_unregister(void)
{
	mdev_unregister_device(&matrix_dev->device);
}
