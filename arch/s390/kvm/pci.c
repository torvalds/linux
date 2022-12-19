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
#include <asm/pci_io.h>
#include <asm/sclp.h>
#include "pci.h"
#include "kvm-s390.h"

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

	aift->sbv = airq_iv_create(ZPCI_NR_DEVICES, AIRQ_IV_ALLOC, NULL);
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
	aift->gait = (struct zpci_gaite *)page_to_virt(page);

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
	aift->kzdev = kcalloc(ZPCI_NR_DEVICES, sizeof(struct kvm_zdev *),
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

/* Modify PCI: Register floating adapter interruption forwarding */
static int kvm_zpci_set_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_REG_INT);
	struct zpci_fib fib = {};
	u8 status;

	fib.fmt0.isc = zdev->kzdev->fib.fmt0.isc;
	fib.fmt0.sum = 1;       /* enable summary notifications */
	fib.fmt0.noi = airq_iv_end(zdev->aibv);
	fib.fmt0.aibv = virt_to_phys(zdev->aibv->vector);
	fib.fmt0.aibvo = 0;
	fib.fmt0.aisb = virt_to_phys(aift->sbv->vector + (zdev->aisb / 64) * 8);
	fib.fmt0.aisbo = zdev->aisb & 63;
	fib.gd = zdev->gisa;

	return zpci_mod_fc(req, &fib, &status) ? -EIO : 0;
}

/* Modify PCI: Unregister floating adapter interruption forwarding */
static int kvm_zpci_clear_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_DEREG_INT);
	struct zpci_fib fib = {};
	u8 cc, status;

	fib.gd = zdev->gisa;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc == 3 || (cc == 1 && status == 24))
		/* Function already gone or IRQs already deregistered. */
		cc = 0;

	return cc ? -EIO : 0;
}

static inline void unaccount_mem(unsigned long nr_pages)
{
	struct user_struct *user = get_uid(current_user());

	if (user)
		atomic_long_sub(nr_pages, &user->locked_vm);
	if (current->mm)
		atomic64_sub(nr_pages, &current->mm->pinned_vm);
}

static inline int account_mem(unsigned long nr_pages)
{
	struct user_struct *user = get_uid(current_user());
	unsigned long page_limit, cur_pages, new_pages;

	page_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	do {
		cur_pages = atomic_long_read(&user->locked_vm);
		new_pages = cur_pages + nr_pages;
		if (new_pages > page_limit)
			return -ENOMEM;
	} while (atomic_long_cmpxchg(&user->locked_vm, cur_pages,
					new_pages) != cur_pages);

	atomic64_add(nr_pages, &current->mm->pinned_vm);

	return 0;
}

static int kvm_s390_pci_aif_enable(struct zpci_dev *zdev, struct zpci_fib *fib,
				   bool assist)
{
	struct page *pages[1], *aibv_page, *aisb_page = NULL;
	unsigned int msi_vecs, idx;
	struct zpci_gaite *gaite;
	unsigned long hva, bit;
	struct kvm *kvm;
	phys_addr_t gaddr;
	int rc = 0, gisc, npages, pcount = 0;

	/*
	 * Interrupt forwarding is only applicable if the device is already
	 * enabled for interpretation
	 */
	if (zdev->gisa == 0)
		return -EINVAL;

	kvm = zdev->kzdev->kvm;
	msi_vecs = min_t(unsigned int, fib->fmt0.noi, zdev->max_msi);

	/* Get the associated forwarding ISC - if invalid, return the error */
	gisc = kvm_s390_gisc_register(kvm, fib->fmt0.isc);
	if (gisc < 0)
		return gisc;

	/* Replace AIBV address */
	idx = srcu_read_lock(&kvm->srcu);
	hva = gfn_to_hva(kvm, gpa_to_gfn((gpa_t)fib->fmt0.aibv));
	npages = pin_user_pages_fast(hva, 1, FOLL_WRITE | FOLL_LONGTERM, pages);
	srcu_read_unlock(&kvm->srcu, idx);
	if (npages < 1) {
		rc = -EIO;
		goto out;
	}
	aibv_page = pages[0];
	pcount++;
	gaddr = page_to_phys(aibv_page) + (fib->fmt0.aibv & ~PAGE_MASK);
	fib->fmt0.aibv = gaddr;

	/* Pin the guest AISB if one was specified */
	if (fib->fmt0.sum == 1) {
		idx = srcu_read_lock(&kvm->srcu);
		hva = gfn_to_hva(kvm, gpa_to_gfn((gpa_t)fib->fmt0.aisb));
		npages = pin_user_pages_fast(hva, 1, FOLL_WRITE | FOLL_LONGTERM,
					     pages);
		srcu_read_unlock(&kvm->srcu, idx);
		if (npages < 1) {
			rc = -EIO;
			goto unpin1;
		}
		aisb_page = pages[0];
		pcount++;
	}

	/* Account for pinned pages, roll back on failure */
	if (account_mem(pcount))
		goto unpin2;

	/* AISB must be allocated before we can fill in GAITE */
	mutex_lock(&aift->aift_lock);
	bit = airq_iv_alloc_bit(aift->sbv);
	if (bit == -1UL)
		goto unlock;
	zdev->aisb = bit; /* store the summary bit number */
	zdev->aibv = airq_iv_create(msi_vecs, AIRQ_IV_DATA |
				    AIRQ_IV_BITLOCK |
				    AIRQ_IV_GUESTVEC,
				    phys_to_virt(fib->fmt0.aibv));

	spin_lock_irq(&aift->gait_lock);
	gaite = (struct zpci_gaite *)aift->gait + (zdev->aisb *
						   sizeof(struct zpci_gaite));

	/* If assist not requested, host will get all alerts */
	if (assist)
		gaite->gisa = (u32)virt_to_phys(&kvm->arch.sie_page2->gisa);
	else
		gaite->gisa = 0;

	gaite->gisc = fib->fmt0.isc;
	gaite->count++;
	gaite->aisbo = fib->fmt0.aisbo;
	gaite->aisb = virt_to_phys(page_address(aisb_page) + (fib->fmt0.aisb &
							      ~PAGE_MASK));
	aift->kzdev[zdev->aisb] = zdev->kzdev;
	spin_unlock_irq(&aift->gait_lock);

	/* Update guest FIB for re-issue */
	fib->fmt0.aisbo = zdev->aisb & 63;
	fib->fmt0.aisb = virt_to_phys(aift->sbv->vector + (zdev->aisb / 64) * 8);
	fib->fmt0.isc = gisc;

	/* Save some guest fib values in the host for later use */
	zdev->kzdev->fib.fmt0.isc = fib->fmt0.isc;
	zdev->kzdev->fib.fmt0.aibv = fib->fmt0.aibv;
	mutex_unlock(&aift->aift_lock);

	/* Issue the clp to setup the irq now */
	rc = kvm_zpci_set_airq(zdev);
	return rc;

unlock:
	mutex_unlock(&aift->aift_lock);
unpin2:
	if (fib->fmt0.sum == 1)
		unpin_user_page(aisb_page);
unpin1:
	unpin_user_page(aibv_page);
out:
	return rc;
}

static int kvm_s390_pci_aif_disable(struct zpci_dev *zdev, bool force)
{
	struct kvm_zdev *kzdev = zdev->kzdev;
	struct zpci_gaite *gaite;
	struct page *vpage = NULL, *spage = NULL;
	int rc, pcount = 0;
	u8 isc;

	if (zdev->gisa == 0)
		return -EINVAL;

	mutex_lock(&aift->aift_lock);

	/*
	 * If the clear fails due to an error, leave now unless we know this
	 * device is about to go away (force) -- In that case clear the GAITE
	 * regardless.
	 */
	rc = kvm_zpci_clear_airq(zdev);
	if (rc && !force)
		goto out;

	if (zdev->kzdev->fib.fmt0.aibv == 0)
		goto out;
	spin_lock_irq(&aift->gait_lock);
	gaite = (struct zpci_gaite *)aift->gait + (zdev->aisb *
						   sizeof(struct zpci_gaite));
	isc = gaite->gisc;
	gaite->count--;
	if (gaite->count == 0) {
		/* Release guest AIBV and AISB */
		vpage = phys_to_page(kzdev->fib.fmt0.aibv);
		if (gaite->aisb != 0)
			spage = phys_to_page(gaite->aisb);
		/* Clear the GAIT entry */
		gaite->aisb = 0;
		gaite->gisc = 0;
		gaite->aisbo = 0;
		gaite->gisa = 0;
		aift->kzdev[zdev->aisb] = NULL;
		/* Clear zdev info */
		airq_iv_free_bit(aift->sbv, zdev->aisb);
		airq_iv_release(zdev->aibv);
		zdev->aisb = 0;
		zdev->aibv = NULL;
	}
	spin_unlock_irq(&aift->gait_lock);
	kvm_s390_gisc_unregister(kzdev->kvm, isc);
	kzdev->fib.fmt0.isc = 0;
	kzdev->fib.fmt0.aibv = 0;

	if (vpage) {
		unpin_user_page(vpage);
		pcount++;
	}
	if (spage) {
		unpin_user_page(spage);
		pcount++;
	}
	if (pcount > 0)
		unaccount_mem(pcount);
out:
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


/*
 * Register device with the specified KVM. If interpetation facilities are
 * available, enable them and let userspace indicate whether or not they will
 * be used (specify SHM bit to disable).
 */
static int kvm_s390_pci_register_kvm(void *opaque, struct kvm *kvm)
{
	struct zpci_dev *zdev = opaque;
	u8 status;
	int rc;

	if (!zdev)
		return -EINVAL;

	mutex_lock(&zdev->kzdev_lock);

	if (zdev->kzdev || zdev->gisa != 0 || !kvm) {
		mutex_unlock(&zdev->kzdev_lock);
		return -EINVAL;
	}

	kvm_get_kvm(kvm);

	mutex_lock(&kvm->lock);

	rc = kvm_s390_pci_dev_open(zdev);
	if (rc)
		goto err;

	/*
	 * If interpretation facilities aren't available, add the device to
	 * the kzdev list but don't enable for interpretation.
	 */
	if (!kvm_s390_pci_interp_allowed())
		goto out;

	/*
	 * If this is the first request to use an interpreted device, make the
	 * necessary vcpu changes
	 */
	if (!kvm->arch.use_zpci_interp)
		kvm_s390_vcpu_pci_enable_interp(kvm);

	if (zdev_enabled(zdev)) {
		rc = zpci_disable_device(zdev);
		if (rc)
			goto err;
	}

	/*
	 * Store information about the identity of the kvm guest allowed to
	 * access this device via interpretation to be used by host CLP
	 */
	zdev->gisa = (u32)virt_to_phys(&kvm->arch.sie_page2->gisa);

	rc = zpci_enable_device(zdev);
	if (rc)
		goto clear_gisa;

	/* Re-register the IOMMU that was already created */
	rc = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
				virt_to_phys(zdev->dma_table), &status);
	if (rc)
		goto clear_gisa;

out:
	zdev->kzdev->kvm = kvm;

	spin_lock(&kvm->arch.kzdev_list_lock);
	list_add_tail(&zdev->kzdev->entry, &kvm->arch.kzdev_list);
	spin_unlock(&kvm->arch.kzdev_list_lock);

	mutex_unlock(&kvm->lock);
	mutex_unlock(&zdev->kzdev_lock);
	return 0;

clear_gisa:
	zdev->gisa = 0;
err:
	if (zdev->kzdev)
		kvm_s390_pci_dev_release(zdev);
	mutex_unlock(&kvm->lock);
	mutex_unlock(&zdev->kzdev_lock);
	kvm_put_kvm(kvm);
	return rc;
}

static void kvm_s390_pci_unregister_kvm(void *opaque)
{
	struct zpci_dev *zdev = opaque;
	struct kvm *kvm;
	u8 status;

	if (!zdev)
		return;

	mutex_lock(&zdev->kzdev_lock);

	if (WARN_ON(!zdev->kzdev)) {
		mutex_unlock(&zdev->kzdev_lock);
		return;
	}

	kvm = zdev->kzdev->kvm;
	mutex_lock(&kvm->lock);

	/*
	 * A 0 gisa means interpretation was never enabled, just remove the
	 * device from the list.
	 */
	if (zdev->gisa == 0)
		goto out;

	/* Forwarding must be turned off before interpretation */
	if (zdev->kzdev->fib.fmt0.aibv != 0)
		kvm_s390_pci_aif_disable(zdev, true);

	/* Remove the host CLP guest designation */
	zdev->gisa = 0;

	if (zdev_enabled(zdev)) {
		if (zpci_disable_device(zdev))
			goto out;
	}

	if (zpci_enable_device(zdev))
		goto out;

	/* Re-register the IOMMU that was already created */
	zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
			   virt_to_phys(zdev->dma_table), &status);

out:
	spin_lock(&kvm->arch.kzdev_list_lock);
	list_del(&zdev->kzdev->entry);
	spin_unlock(&kvm->arch.kzdev_list_lock);
	kvm_s390_pci_dev_release(zdev);

	mutex_unlock(&kvm->lock);
	mutex_unlock(&zdev->kzdev_lock);

	kvm_put_kvm(kvm);
}

void kvm_s390_pci_init_list(struct kvm *kvm)
{
	spin_lock_init(&kvm->arch.kzdev_list_lock);
	INIT_LIST_HEAD(&kvm->arch.kzdev_list);
}

void kvm_s390_pci_clear_list(struct kvm *kvm)
{
	/*
	 * This list should already be empty, either via vfio device closures
	 * or kvm fd cleanup.
	 */
	spin_lock(&kvm->arch.kzdev_list_lock);
	WARN_ON_ONCE(!list_empty(&kvm->arch.kzdev_list));
	spin_unlock(&kvm->arch.kzdev_list_lock);
}

static struct zpci_dev *get_zdev_from_kvm_by_fh(struct kvm *kvm, u32 fh)
{
	struct zpci_dev *zdev = NULL;
	struct kvm_zdev *kzdev;

	spin_lock(&kvm->arch.kzdev_list_lock);
	list_for_each_entry(kzdev, &kvm->arch.kzdev_list, entry) {
		if (kzdev->zdev->fh == fh) {
			zdev = kzdev->zdev;
			break;
		}
	}
	spin_unlock(&kvm->arch.kzdev_list_lock);

	return zdev;
}

static int kvm_s390_pci_zpci_reg_aen(struct zpci_dev *zdev,
				     struct kvm_s390_zpci_op *args)
{
	struct zpci_fib fib = {};
	bool hostflag;

	fib.fmt0.aibv = args->u.reg_aen.ibv;
	fib.fmt0.isc = args->u.reg_aen.isc;
	fib.fmt0.noi = args->u.reg_aen.noi;
	if (args->u.reg_aen.sb != 0) {
		fib.fmt0.aisb = args->u.reg_aen.sb;
		fib.fmt0.aisbo = args->u.reg_aen.sbo;
		fib.fmt0.sum = 1;
	} else {
		fib.fmt0.aisb = 0;
		fib.fmt0.aisbo = 0;
		fib.fmt0.sum = 0;
	}

	hostflag = !(args->u.reg_aen.flags & KVM_S390_ZPCIOP_REGAEN_HOST);
	return kvm_s390_pci_aif_enable(zdev, &fib, hostflag);
}

int kvm_s390_pci_zpci_op(struct kvm *kvm, struct kvm_s390_zpci_op *args)
{
	struct kvm_zdev *kzdev;
	struct zpci_dev *zdev;
	int r;

	zdev = get_zdev_from_kvm_by_fh(kvm, args->fh);
	if (!zdev)
		return -ENODEV;

	mutex_lock(&zdev->kzdev_lock);
	mutex_lock(&kvm->lock);

	kzdev = zdev->kzdev;
	if (!kzdev) {
		r = -ENODEV;
		goto out;
	}
	if (kzdev->kvm != kvm) {
		r = -EPERM;
		goto out;
	}

	switch (args->op) {
	case KVM_S390_ZPCIOP_REG_AEN:
		/* Fail on unknown flags */
		if (args->u.reg_aen.flags & ~KVM_S390_ZPCIOP_REGAEN_HOST) {
			r = -EINVAL;
			break;
		}
		r = kvm_s390_pci_zpci_reg_aen(zdev, args);
		break;
	case KVM_S390_ZPCIOP_DEREG_AEN:
		r = kvm_s390_pci_aif_disable(zdev, false);
		break;
	default:
		r = -EINVAL;
	}

out:
	mutex_unlock(&kvm->lock);
	mutex_unlock(&zdev->kzdev_lock);
	return r;
}

int kvm_s390_pci_init(void)
{
	zpci_kvm_hook.kvm_register = kvm_s390_pci_register_kvm;
	zpci_kvm_hook.kvm_unregister = kvm_s390_pci_unregister_kvm;

	if (!kvm_s390_pci_interp_allowed())
		return 0;

	aift = kzalloc(sizeof(struct zpci_aift), GFP_KERNEL);
	if (!aift)
		return -ENOMEM;

	spin_lock_init(&aift->gait_lock);
	mutex_init(&aift->aift_lock);

	return 0;
}

void kvm_s390_pci_exit(void)
{
	zpci_kvm_hook.kvm_register = NULL;
	zpci_kvm_hook.kvm_unregister = NULL;

	if (!kvm_s390_pci_interp_allowed())
		return;

	mutex_destroy(&aift->aift_lock);

	kfree(aift);
}
