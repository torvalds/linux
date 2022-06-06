// SPDX-License-Identifier: GPL-2.0
/*
 * s390 kvm PCI passthrough support
 *
 * Copyright IBM Corp. 2022
 *
 *    Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 */

#include <linux/kvm_host.h>
#include <linux/pci.h>
#include <asm/pci.h>
#include <asm/pci_insn.h>
#include "pci.h"

struct zpci_aift *aift;

static inline int __set_irq_noiib(u16 ctl, u8 isc)
{
	union zpci_sic_iib iib = {{0}};

	return zpci_set_irq_ctrl(ctl, isc, &iib);
}

void kvm_s390_pci_aen_exit(void)
{
	unsigned long flags;
	struct kvm_zdev **gait_kzdev;

	lockdep_assert_held(&aift->aift_lock);

	/*
	 * Contents of the aipb remain registered for the life of the host
	 * kernel, the information preserved in zpci_aipb and zpci_aif_sbv
	 * in case we insert the KVM module again later.  Clear the AIFT
	 * information and free anything not registered with underlying
	 * firmware.
	 */
	spin_lock_irqsave(&aift->gait_lock, flags);
	gait_kzdev = aift->kzdev;
	aift->gait = NULL;
	aift->sbv = NULL;
	aift->kzdev = NULL;
	spin_unlock_irqrestore(&aift->gait_lock, flags);

	kfree(gait_kzdev);
}

static int zpci_setup_aipb(u8 nisc)
{
	struct page *page;
	int size, rc;

	zpci_aipb = kzalloc(sizeof(union zpci_sic_iib), GFP_KERNEL);
	if (!zpci_aipb)
		return -ENOMEM;

	aift->sbv = airq_iv_create(ZPCI_NR_DEVICES, AIRQ_IV_ALLOC, 0);
	if (!aift->sbv) {
		rc = -ENOMEM;
		goto free_aipb;
	}
	zpci_aif_sbv = aift->sbv;
	size = get_order(PAGE_ALIGN(ZPCI_NR_DEVICES *
						sizeof(struct zpci_gaite)));
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, size);
	if (!page) {
		rc = -ENOMEM;
		goto free_sbv;
	}
	aift->gait = (struct zpci_gaite *)page_to_phys(page);

	zpci_aipb->aipb.faisb = virt_to_phys(aift->sbv->vector);
	zpci_aipb->aipb.gait = virt_to_phys(aift->gait);
	zpci_aipb->aipb.afi = nisc;
	zpci_aipb->aipb.faal = ZPCI_NR_DEVICES;

	/* Setup Adapter Event Notification Interpretation */
	if (zpci_set_irq_ctrl(SIC_SET_AENI_CONTROLS, 0, zpci_aipb)) {
		rc = -EIO;
		goto free_gait;
	}

	return 0;

free_gait:
	free_pages((unsigned long)aift->gait, size);
free_sbv:
	airq_iv_release(aift->sbv);
	zpci_aif_sbv = NULL;
free_aipb:
	kfree(zpci_aipb);
	zpci_aipb = NULL;

	return rc;
}

static int zpci_reset_aipb(u8 nisc)
{
	/*
	 * AEN registration can only happen once per system boot.  If
	 * an aipb already exists then AEN was already registered and
	 * we can re-use the aipb contents.  This can only happen if
	 * the KVM module was removed and re-inserted.  However, we must
	 * ensure that the same forwarding ISC is used as this is assigned
	 * during KVM module load.
	 */
	if (zpci_aipb->aipb.afi != nisc)
		return -EINVAL;

	aift->sbv = zpci_aif_sbv;
	aift->gait = (struct zpci_gaite *)zpci_aipb->aipb.gait;

	return 0;
}

int kvm_s390_pci_aen_init(u8 nisc)
{
	int rc = 0;

	/* If already enabled for AEN, bail out now */
	if (aift->gait || aift->sbv)
		return -EPERM;

	mutex_lock(&aift->aift_lock);
	aift->kzdev = kcalloc(ZPCI_NR_DEVICES, sizeof(struct kvm_zdev),
			      GFP_KERNEL);
	if (!aift->kzdev) {
		rc = -ENOMEM;
		goto unlock;
	}

	if (!zpci_aipb)
		rc = zpci_setup_aipb(nisc);
	else
		rc = zpci_reset_aipb(nisc);
	if (rc)
		goto free_zdev;

	/* Enable floating IRQs */
	if (__set_irq_noiib(SIC_IRQ_MODE_SINGLE, nisc)) {
		rc = -EIO;
		kvm_s390_pci_aen_exit();
	}

	goto unlock;

free_zdev:
	kfree(aift->kzdev);
unlock:
	mutex_unlock(&aift->aift_lock);
	return rc;
}

static int kvm_s390_pci_dev_open(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

	kzdev = kzalloc(sizeof(struct kvm_zdev), GFP_KERNEL);
	if (!kzdev)
		return -ENOMEM;

	kzdev->zdev = zdev;
	zdev->kzdev = kzdev;

	return 0;
}

static void kvm_s390_pci_dev_release(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

	kzdev = zdev->kzdev;
	WARN_ON(kzdev->zdev != zdev);
	zdev->kzdev = NULL;
	kfree(kzdev);
}

int kvm_s390_pci_init(void)
{
	aift = kzalloc(sizeof(struct zpci_aift), GFP_KERNEL);
	if (!aift)
		return -ENOMEM;

	spin_lock_init(&aift->gait_lock);
	mutex_init(&aift->aift_lock);

	return 0;
}

void kvm_s390_pci_exit(void)
{
	mutex_destroy(&aift->aift_lock);

	kfree(aift);
}
