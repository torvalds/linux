// SPDX-License-Identifier: MIT
/*
 * vgaarb.c: Implements VGA arbitration. For details refer to
 * Documentation/gpu/vgaarbiter.rst
 *
 * (C) Copyright 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 * (C) Copyright 2007 Paulo R. Zanoni <przanoni@gmail.com>
 * (C) Copyright 2007, 2009 Tiago Vignatti <vignatti@freedesktop.org>
 */

#define pr_fmt(fmt) "vgaarb: " fmt

#define vgaarb_dbg(dev, fmt, arg...)	dev_dbg(dev, "vgaarb: " fmt, ##arg)
#define vgaarb_info(dev, fmt, arg...)	dev_info(dev, "vgaarb: " fmt, ##arg)
#define vgaarb_err(dev, fmt, arg...)	dev_err(dev, "vgaarb: " fmt, ##arg)

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/screen_info.h>
#include <linux/vt.h>
#include <linux/console.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>
#include <linux/vgaarb.h>

static void vga_arbiter_notify_clients(void);

/*
 * We keep a list of all VGA devices in the system to speed
 * up the various operations of the arbiter
 */
struct vga_device {
	struct list_head list;
	struct pci_dev *pdev;
	unsigned int decodes;		/* what it decodes */
	unsigned int owns;		/* what it owns */
	unsigned int locks;		/* what it locks */
	unsigned int io_lock_cnt;	/* legacy IO lock count */
	unsigned int mem_lock_cnt;	/* legacy MEM lock count */
	unsigned int io_norm_cnt;	/* normal IO count */
	unsigned int mem_norm_cnt;	/* normal MEM count */
	bool bridge_has_one_vga;
	bool is_firmware_default;	/* device selected by firmware */
	unsigned int (*set_decode)(struct pci_dev *pdev, bool decode);
};

static LIST_HEAD(vga_list);
static int vga_count, vga_decode_count;
static bool vga_arbiter_used;
static DEFINE_SPINLOCK(vga_lock);
static DECLARE_WAIT_QUEUE_HEAD(vga_wait_queue);

static const char *vga_iostate_to_str(unsigned int iostate)
{
	/* Ignore VGA_RSRC_IO and VGA_RSRC_MEM */
	iostate &= VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
	switch (iostate) {
	case VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM:
		return "io+mem";
	case VGA_RSRC_LEGACY_IO:
		return "io";
	case VGA_RSRC_LEGACY_MEM:
		return "mem";
	}
	return "none";
}

static int vga_str_to_iostate(char *buf, int str_size, unsigned int *io_state)
{
	/*
	 * In theory, we could hand out locks on IO and MEM separately to
	 * userspace, but this can cause deadlocks.
	 */
	if (strncmp(buf, "none", 4) == 0) {
		*io_state = VGA_RSRC_NONE;
		return 1;
	}

	/* XXX We're not checking the str_size! */
	if (strncmp(buf, "io+mem", 6) == 0)
		goto both;
	else if (strncmp(buf, "io", 2) == 0)
		goto both;
	else if (strncmp(buf, "mem", 3) == 0)
		goto both;
	return 0;
both:
	*io_state = VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
	return 1;
}

/* This is only used as a cookie, it should not be dereferenced */
static struct pci_dev *vga_default;

/* Find somebody in our list */
static struct vga_device *vgadev_find(struct pci_dev *pdev)
{
	struct vga_device *vgadev;

	list_for_each_entry(vgadev, &vga_list, list)
		if (pdev == vgadev->pdev)
			return vgadev;
	return NULL;
}

/**
 * vga_default_device - return the default VGA device, for vgacon
 *
 * This can be defined by the platform. The default implementation is
 * rather dumb and will probably only work properly on single VGA card
 * setups and/or x86 platforms.
 *
 * If your VGA default device is not PCI, you'll have to return NULL here.
 * In this case, I assume it will not conflict with any PCI card. If this
 * is not true, I'll have to define two arch hooks for enabling/disabling
 * the VGA default device if that is possible. This may be a problem with
 * real _ISA_ VGA cards, in addition to a PCI one. I don't know at this
 * point how to deal with that card. Can their IOs be disabled at all? If
 * not, then I suppose it's a matter of having the proper arch hook telling
 * us about it, so we basically never allow anybody to succeed a vga_get().
 */
struct pci_dev *vga_default_device(void)
{
	return vga_default;
}
EXPORT_SYMBOL_GPL(vga_default_device);

void vga_set_default_device(struct pci_dev *pdev)
{
	if (vga_default == pdev)
		return;

	pci_dev_put(vga_default);
	vga_default = pci_dev_get(pdev);
}

/**
 * vga_remove_vgacon - deactivate VGA console
 *
 * Unbind and unregister vgacon in case pdev is the default VGA device.
 * Can be called by GPU drivers on initialization to make sure VGA register
 * access done by vgacon will not disturb the device.
 *
 * @pdev: PCI device.
 */
#if !defined(CONFIG_VGA_CONSOLE)
int vga_remove_vgacon(struct pci_dev *pdev)
{
	return 0;
}
#elif !defined(CONFIG_DUMMY_CONSOLE)
int vga_remove_vgacon(struct pci_dev *pdev)
{
	return -ENODEV;
}
#else
int vga_remove_vgacon(struct pci_dev *pdev)
{
	int ret = 0;

	if (pdev != vga_default)
		return 0;
	vgaarb_info(&pdev->dev, "deactivate vga console\n");

	console_lock();
	if (con_is_bound(&vga_con))
		ret = do_take_over_console(&dummy_con, 0,
					   MAX_NR_CONSOLES - 1, 1);
	if (ret == 0) {
		ret = do_unregister_con_driver(&vga_con);

		/* Ignore "already unregistered". */
		if (ret == -ENODEV)
			ret = 0;
	}
	console_unlock();

	return ret;
}
#endif
EXPORT_SYMBOL(vga_remove_vgacon);

/*
 * If we don't ever use VGA arbitration, we should avoid turning off
 * anything anywhere due to old X servers getting confused about the boot
 * device not being VGA.
 */
static void vga_check_first_use(void)
{
	/*
	 * Inform all GPUs in the system that VGA arbitration has occurred
	 * so they can disable resources if possible.
	 */
	if (!vga_arbiter_used) {
		vga_arbiter_used = true;
		vga_arbiter_notify_clients();
	}
}

static struct vga_device *__vga_tryget(struct vga_device *vgadev,
				       unsigned int rsrc)
{
	struct device *dev = &vgadev->pdev->dev;
	unsigned int wants, legacy_wants, match;
	struct vga_device *conflict;
	unsigned int pci_bits;
	u32 flags = 0;

	/*
	 * Account for "normal" resources to lock. If we decode the legacy,
	 * counterpart, we need to request it as well
	 */
	if ((rsrc & VGA_RSRC_NORMAL_IO) &&
	    (vgadev->decodes & VGA_RSRC_LEGACY_IO))
		rsrc |= VGA_RSRC_LEGACY_IO;
	if ((rsrc & VGA_RSRC_NORMAL_MEM) &&
	    (vgadev->decodes & VGA_RSRC_LEGACY_MEM))
		rsrc |= VGA_RSRC_LEGACY_MEM;

	vgaarb_dbg(dev, "%s: %d\n", __func__, rsrc);
	vgaarb_dbg(dev, "%s: owns: %d\n", __func__, vgadev->owns);

	/* Check what resources we need to acquire */
	wants = rsrc & ~vgadev->owns;

	/* We already own everything, just mark locked & bye bye */
	if (wants == 0)
		goto lock_them;

	/*
	 * We don't need to request a legacy resource, we just enable
	 * appropriate decoding and go.
	 */
	legacy_wants = wants & VGA_RSRC_LEGACY_MASK;
	if (legacy_wants == 0)
		goto enable_them;

	/* Ok, we don't, let's find out who we need to kick off */
	list_for_each_entry(conflict, &vga_list, list) {
		unsigned int lwants = legacy_wants;
		unsigned int change_bridge = 0;

		/* Don't conflict with myself */
		if (vgadev == conflict)
			continue;

		/*
		 * We have a possible conflict. Before we go further, we must
		 * check if we sit on the same bus as the conflicting device.
		 * If we don't, then we must tie both IO and MEM resources
		 * together since there is only a single bit controlling
		 * VGA forwarding on P2P bridges.
		 */
		if (vgadev->pdev->bus != conflict->pdev->bus) {
			change_bridge = 1;
			lwants = VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
		}

		/*
		 * Check if the guy has a lock on the resource. If he does,
		 * return the conflicting entry.
		 */
		if (conflict->locks & lwants)
			return conflict;

		/*
		 * Ok, now check if it owns the resource we want.  We can
		 * lock resources that are not decoded; therefore a device
		 * can own resources it doesn't decode.
		 */
		match = lwants & conflict->owns;
		if (!match)
			continue;

		/*
		 * Looks like he doesn't have a lock, we can steal them
		 * from him.
		 */

		flags = 0;
		pci_bits = 0;

		/*
		 * If we can't control legacy resources via the bridge, we
		 * also need to disable normal decoding.
		 */
		if (!conflict->bridge_has_one_vga) {
			if ((match & conflict->decodes) & VGA_RSRC_LEGACY_MEM)
				pci_bits |= PCI_COMMAND_MEMORY;
			if ((match & conflict->decodes) & VGA_RSRC_LEGACY_IO)
				pci_bits |= PCI_COMMAND_IO;

			if (pci_bits)
				flags |= PCI_VGA_STATE_CHANGE_DECODES;
		}

		if (change_bridge)
			flags |= PCI_VGA_STATE_CHANGE_BRIDGE;

		pci_set_vga_state(conflict->pdev, false, pci_bits, flags);
		conflict->owns &= ~match;

		/* If we disabled normal decoding, reflect it in owns */
		if (pci_bits & PCI_COMMAND_MEMORY)
			conflict->owns &= ~VGA_RSRC_NORMAL_MEM;
		if (pci_bits & PCI_COMMAND_IO)
			conflict->owns &= ~VGA_RSRC_NORMAL_IO;
	}

enable_them:
	/*
	 * Ok, we got it, everybody conflicting has been disabled, let's
	 * enable us.  Mark any bits in "owns" regardless of whether we
	 * decoded them.  We can lock resources we don't decode, therefore
	 * we must track them via "owns".
	 */
	flags = 0;
	pci_bits = 0;

	if (!vgadev->bridge_has_one_vga) {
		flags |= PCI_VGA_STATE_CHANGE_DECODES;
		if (wants & (VGA_RSRC_LEGACY_MEM|VGA_RSRC_NORMAL_MEM))
			pci_bits |= PCI_COMMAND_MEMORY;
		if (wants & (VGA_RSRC_LEGACY_IO|VGA_RSRC_NORMAL_IO))
			pci_bits |= PCI_COMMAND_IO;
	}
	if (wants & VGA_RSRC_LEGACY_MASK)
		flags |= PCI_VGA_STATE_CHANGE_BRIDGE;

	pci_set_vga_state(vgadev->pdev, true, pci_bits, flags);

	vgadev->owns |= wants;
lock_them:
	vgadev->locks |= (rsrc & VGA_RSRC_LEGACY_MASK);
	if (rsrc & VGA_RSRC_LEGACY_IO)
		vgadev->io_lock_cnt++;
	if (rsrc & VGA_RSRC_LEGACY_MEM)
		vgadev->mem_lock_cnt++;
	if (rsrc & VGA_RSRC_NORMAL_IO)
		vgadev->io_norm_cnt++;
	if (rsrc & VGA_RSRC_NORMAL_MEM)
		vgadev->mem_norm_cnt++;

	return NULL;
}

static void __vga_put(struct vga_device *vgadev, unsigned int rsrc)
{
	struct device *dev = &vgadev->pdev->dev;
	unsigned int old_locks = vgadev->locks;

	vgaarb_dbg(dev, "%s\n", __func__);

	/*
	 * Update our counters and account for equivalent legacy resources
	 * if we decode them.
	 */
	if ((rsrc & VGA_RSRC_NORMAL_IO) && vgadev->io_norm_cnt > 0) {
		vgadev->io_norm_cnt--;
		if (vgadev->decodes & VGA_RSRC_LEGACY_IO)
			rsrc |= VGA_RSRC_LEGACY_IO;
	}
	if ((rsrc & VGA_RSRC_NORMAL_MEM) && vgadev->mem_norm_cnt > 0) {
		vgadev->mem_norm_cnt--;
		if (vgadev->decodes & VGA_RSRC_LEGACY_MEM)
			rsrc |= VGA_RSRC_LEGACY_MEM;
	}
	if ((rsrc & VGA_RSRC_LEGACY_IO) && vgadev->io_lock_cnt > 0)
		vgadev->io_lock_cnt--;
	if ((rsrc & VGA_RSRC_LEGACY_MEM) && vgadev->mem_lock_cnt > 0)
		vgadev->mem_lock_cnt--;

	/*
	 * Just clear lock bits, we do lazy operations so we don't really
	 * have to bother about anything else at this point.
	 */
	if (vgadev->io_lock_cnt == 0)
		vgadev->locks &= ~VGA_RSRC_LEGACY_IO;
	if (vgadev->mem_lock_cnt == 0)
		vgadev->locks &= ~VGA_RSRC_LEGACY_MEM;

	/*
	 * Kick the wait queue in case somebody was waiting if we actually
	 * released something.
	 */
	if (old_locks != vgadev->locks)
		wake_up_all(&vga_wait_queue);
}

/**
 * vga_get - acquire & lock VGA resources
 * @pdev: PCI device of the VGA card or NULL for the system default
 * @rsrc: bit mask of resources to acquire and lock
 * @interruptible: blocking should be interruptible by signals ?
 *
 * Acquire VGA resources for the given card and mark those resources
 * locked. If the resources requested are "normal" (and not legacy)
 * resources, the arbiter will first check whether the card is doing legacy
 * decoding for that type of resource. If yes, the lock is "converted" into
 * a legacy resource lock.
 *
 * The arbiter will first look for all VGA cards that might conflict and disable
 * their IOs and/or Memory access, including VGA forwarding on P2P bridges if
 * necessary, so that the requested resources can be used. Then, the card is
 * marked as locking these resources and the IO and/or Memory accesses are
 * enabled on the card (including VGA forwarding on parent P2P bridges if any).
 *
 * This function will block if some conflicting card is already locking one of
 * the required resources (or any resource on a different bus segment, since P2P
 * bridges don't differentiate VGA memory and IO afaik). You can indicate
 * whether this blocking should be interruptible by a signal (for userland
 * interface) or not.
 *
 * Must not be called at interrupt time or in atomic context.  If the card
 * already owns the resources, the function succeeds.  Nested calls are
 * supported (a per-resource counter is maintained)
 *
 * On success, release the VGA resource again with vga_put().
 *
 * Returns:
 *
 * 0 on success, negative error code on failure.
 */
int vga_get(struct pci_dev *pdev, unsigned int rsrc, int interruptible)
{
	struct vga_device *vgadev, *conflict;
	unsigned long flags;
	wait_queue_entry_t wait;
	int rc = 0;

	vga_check_first_use();
	/* The caller should check for this, but let's be sure */
	if (pdev == NULL)
		pdev = vga_default_device();
	if (pdev == NULL)
		return 0;

	for (;;) {
		spin_lock_irqsave(&vga_lock, flags);
		vgadev = vgadev_find(pdev);
		if (vgadev == NULL) {
			spin_unlock_irqrestore(&vga_lock, flags);
			rc = -ENODEV;
			break;
		}
		conflict = __vga_tryget(vgadev, rsrc);
		spin_unlock_irqrestore(&vga_lock, flags);
		if (conflict == NULL)
			break;

		/*
		 * We have a conflict; we wait until somebody kicks the
		 * work queue. Currently we have one work queue that we
		 * kick each time some resources are released, but it would
		 * be fairly easy to have a per-device one so that we only
		 * need to attach to the conflicting device.
		 */
		init_waitqueue_entry(&wait, current);
		add_wait_queue(&vga_wait_queue, &wait);
		set_current_state(interruptible ?
				  TASK_INTERRUPTIBLE :
				  TASK_UNINTERRUPTIBLE);
		if (interruptible && signal_pending(current)) {
			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&vga_wait_queue, &wait);
			rc = -ERESTARTSYS;
			break;
		}
		schedule();
		remove_wait_queue(&vga_wait_queue, &wait);
	}
	return rc;
}
EXPORT_SYMBOL(vga_get);

/**
 * vga_tryget - try to acquire & lock legacy VGA resources
 * @pdev: PCI device of VGA card or NULL for system default
 * @rsrc: bit mask of resources to acquire and lock
 *
 * Perform the same operation as vga_get(), but return an error (-EBUSY)
 * instead of blocking if the resources are already locked by another card.
 * Can be called in any context.
 *
 * On success, release the VGA resource again with vga_put().
 *
 * Returns:
 *
 * 0 on success, negative error code on failure.
 */
static int vga_tryget(struct pci_dev *pdev, unsigned int rsrc)
{
	struct vga_device *vgadev;
	unsigned long flags;
	int rc = 0;

	vga_check_first_use();

	/* The caller should check for this, but let's be sure */
	if (pdev == NULL)
		pdev = vga_default_device();
	if (pdev == NULL)
		return 0;
	spin_lock_irqsave(&vga_lock, flags);
	vgadev = vgadev_find(pdev);
	if (vgadev == NULL) {
		rc = -ENODEV;
		goto bail;
	}
	if (__vga_tryget(vgadev, rsrc))
		rc = -EBUSY;
bail:
	spin_unlock_irqrestore(&vga_lock, flags);
	return rc;
}

/**
 * vga_put - release lock on legacy VGA resources
 * @pdev: PCI device of VGA card or NULL for system default
 * @rsrc: bit mask of resource to release
 *
 * Release resources previously locked by vga_get() or vga_tryget().  The
 * resources aren't disabled right away, so that a subsequent vga_get() on
 * the same card will succeed immediately.  Resources have a counter, so
 * locks are only released if the counter reaches 0.
 */
void vga_put(struct pci_dev *pdev, unsigned int rsrc)
{
	struct vga_device *vgadev;
	unsigned long flags;

	/* The caller should check for this, but let's be sure */
	if (pdev == NULL)
		pdev = vga_default_device();
	if (pdev == NULL)
		return;
	spin_lock_irqsave(&vga_lock, flags);
	vgadev = vgadev_find(pdev);
	if (vgadev == NULL)
		goto bail;
	__vga_put(vgadev, rsrc);
bail:
	spin_unlock_irqrestore(&vga_lock, flags);
}
EXPORT_SYMBOL(vga_put);

static bool vga_is_firmware_default(struct pci_dev *pdev)
{
#if defined(CONFIG_X86) || defined(CONFIG_IA64)
	u64 base = screen_info.lfb_base;
	u64 size = screen_info.lfb_size;
	struct resource *r;
	u64 limit;

	/* Select the device owning the boot framebuffer if there is one */

	if (screen_info.capabilities & VIDEO_CAPABILITY_64BIT_BASE)
		base |= (u64)screen_info.ext_lfb_base << 32;

	limit = base + size;

	/* Does firmware framebuffer belong to us? */
	pci_dev_for_each_resource(pdev, r) {
		if (resource_type(r) != IORESOURCE_MEM)
			continue;

		if (!r->start || !r->end)
			continue;

		if (base < r->start || limit >= r->end)
			continue;

		return true;
	}
#endif
	return false;
}

static bool vga_arb_integrated_gpu(struct device *dev)
{
#if defined(CONFIG_ACPI)
	struct acpi_device *adev = ACPI_COMPANION(dev);

	return adev && !strcmp(acpi_device_hid(adev), ACPI_VIDEO_HID);
#else
	return false;
#endif
}

/*
 * Return true if vgadev is a better default VGA device than the best one
 * we've seen so far.
 */
static bool vga_is_boot_device(struct vga_device *vgadev)
{
	struct vga_device *boot_vga = vgadev_find(vga_default_device());
	struct pci_dev *pdev = vgadev->pdev;
	u16 cmd, boot_cmd;

	/*
	 * We select the default VGA device in this order:
	 *   Firmware framebuffer (see vga_arb_select_default_device())
	 *   Legacy VGA device (owns VGA_RSRC_LEGACY_MASK)
	 *   Non-legacy integrated device (see vga_arb_select_default_device())
	 *   Non-legacy discrete device (see vga_arb_select_default_device())
	 *   Other device (see vga_arb_select_default_device())
	 */

	/*
	 * We always prefer a firmware default device, so if we've already
	 * found one, there's no need to consider vgadev.
	 */
	if (boot_vga && boot_vga->is_firmware_default)
		return false;

	if (vga_is_firmware_default(pdev)) {
		vgadev->is_firmware_default = true;
		return true;
	}

	/*
	 * A legacy VGA device has MEM and IO enabled and any bridges
	 * leading to it have PCI_BRIDGE_CTL_VGA enabled so the legacy
	 * resources ([mem 0xa0000-0xbffff], [io 0x3b0-0x3bb], etc) are
	 * routed to it.
	 *
	 * We use the first one we find, so if we've already found one,
	 * vgadev is no better.
	 */
	if (boot_vga &&
	    (boot_vga->owns & VGA_RSRC_LEGACY_MASK) == VGA_RSRC_LEGACY_MASK)
		return false;

	if ((vgadev->owns & VGA_RSRC_LEGACY_MASK) == VGA_RSRC_LEGACY_MASK)
		return true;

	/*
	 * If we haven't found a legacy VGA device, accept a non-legacy
	 * device.  It may have either IO or MEM enabled, and bridges may
	 * not have PCI_BRIDGE_CTL_VGA enabled, so it may not be able to
	 * use legacy VGA resources.  Prefer an integrated GPU over others.
	 */
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if (cmd & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) {

		/*
		 * An integrated GPU overrides a previous non-legacy
		 * device.  We expect only a single integrated GPU, but if
		 * there are more, we use the *last* because that was the
		 * previous behavior.
		 */
		if (vga_arb_integrated_gpu(&pdev->dev))
			return true;

		/*
		 * We prefer the first non-legacy discrete device we find.
		 * If we already found one, vgadev is no better.
		 */
		if (boot_vga) {
			pci_read_config_word(boot_vga->pdev, PCI_COMMAND,
					     &boot_cmd);
			if (boot_cmd & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY))
				return false;
		}
		return true;
	}

	/*
	 * Vgadev has neither IO nor MEM enabled.  If we haven't found any
	 * other VGA devices, it is the best candidate so far.
	 */
	if (!boot_vga)
		return true;

	return false;
}

/*
 * Rules for using a bridge to control a VGA descendant decoding: if a bridge
 * has only one VGA descendant then it can be used to control the VGA routing
 * for that device. It should always use the bridge closest to the device to
 * control it. If a bridge has a direct VGA descendant, but also have a sub-
 * bridge VGA descendant then we cannot use that bridge to control the direct
 * VGA descendant. So for every device we register, we need to iterate all
 * its parent bridges so we can invalidate any devices using them properly.
 */
static void vga_arbiter_check_bridge_sharing(struct vga_device *vgadev)
{
	struct vga_device *same_bridge_vgadev;
	struct pci_bus *new_bus, *bus;
	struct pci_dev *new_bridge, *bridge;

	vgadev->bridge_has_one_vga = true;

	if (list_empty(&vga_list)) {
		vgaarb_info(&vgadev->pdev->dev, "bridge control possible\n");
		return;
	}

	/* Iterate the new device's bridge hierarchy */
	new_bus = vgadev->pdev->bus;
	while (new_bus) {
		new_bridge = new_bus->self;

		/* Go through list of devices already registered */
		list_for_each_entry(same_bridge_vgadev, &vga_list, list) {
			bus = same_bridge_vgadev->pdev->bus;
			bridge = bus->self;

			/* See if it shares a bridge with this device */
			if (new_bridge == bridge) {
				/*
				 * If its direct parent bridge is the same
				 * as any bridge of this device then it can't
				 * be used for that device.
				 */
				same_bridge_vgadev->bridge_has_one_vga = false;
			}

			/*
			 * Now iterate the previous device's bridge hierarchy.
			 * If the new device's parent bridge is in the other
			 * device's hierarchy, we can't use it to control this
			 * device.
			 */
			while (bus) {
				bridge = bus->self;

				if (bridge && bridge == vgadev->pdev->bus->self)
					vgadev->bridge_has_one_vga = false;

				bus = bus->parent;
			}
		}
		new_bus = new_bus->parent;
	}

	if (vgadev->bridge_has_one_vga)
		vgaarb_info(&vgadev->pdev->dev, "bridge control possible\n");
	else
		vgaarb_info(&vgadev->pdev->dev, "no bridge control possible\n");
}

/*
 * Currently, we assume that the "initial" setup of the system is not sane,
 * that is, we come up with conflicting devices and let the arbiter's
 * client decide if devices decodes legacy things or not.
 */
static bool vga_arbiter_add_pci_device(struct pci_dev *pdev)
{
	struct vga_device *vgadev;
	unsigned long flags;
	struct pci_bus *bus;
	struct pci_dev *bridge;
	u16 cmd;

	/* Allocate structure */
	vgadev = kzalloc(sizeof(struct vga_device), GFP_KERNEL);
	if (vgadev == NULL) {
		vgaarb_err(&pdev->dev, "failed to allocate VGA arbiter data\n");
		/*
		 * What to do on allocation failure? For now, let's just do
		 * nothing, I'm not sure there is anything saner to be done.
		 */
		return false;
	}

	/* Take lock & check for duplicates */
	spin_lock_irqsave(&vga_lock, flags);
	if (vgadev_find(pdev) != NULL) {
		BUG_ON(1);
		goto fail;
	}
	vgadev->pdev = pdev;

	/* By default, assume we decode everything */
	vgadev->decodes = VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
			  VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;

	/* By default, mark it as decoding */
	vga_decode_count++;

	/*
	 * Mark that we "own" resources based on our enables, we will
	 * clear that below if the bridge isn't forwarding.
	 */
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if (cmd & PCI_COMMAND_IO)
		vgadev->owns |= VGA_RSRC_LEGACY_IO;
	if (cmd & PCI_COMMAND_MEMORY)
		vgadev->owns |= VGA_RSRC_LEGACY_MEM;

	/* Check if VGA cycles can get down to us */
	bus = pdev->bus;
	while (bus) {
		bridge = bus->self;
		if (bridge) {
			u16 l;

			pci_read_config_word(bridge, PCI_BRIDGE_CONTROL, &l);
			if (!(l & PCI_BRIDGE_CTL_VGA)) {
				vgadev->owns = 0;
				break;
			}
		}
		bus = bus->parent;
	}

	if (vga_is_boot_device(vgadev)) {
		vgaarb_info(&pdev->dev, "setting as boot VGA device%s\n",
			    vga_default_device() ?
			    " (overriding previous)" : "");
		vga_set_default_device(pdev);
	}

	vga_arbiter_check_bridge_sharing(vgadev);

	/* Add to the list */
	list_add_tail(&vgadev->list, &vga_list);
	vga_count++;
	vgaarb_info(&pdev->dev, "VGA device added: decodes=%s,owns=%s,locks=%s\n",
		vga_iostate_to_str(vgadev->decodes),
		vga_iostate_to_str(vgadev->owns),
		vga_iostate_to_str(vgadev->locks));

	spin_unlock_irqrestore(&vga_lock, flags);
	return true;
fail:
	spin_unlock_irqrestore(&vga_lock, flags);
	kfree(vgadev);
	return false;
}

static bool vga_arbiter_del_pci_device(struct pci_dev *pdev)
{
	struct vga_device *vgadev;
	unsigned long flags;
	bool ret = true;

	spin_lock_irqsave(&vga_lock, flags);
	vgadev = vgadev_find(pdev);
	if (vgadev == NULL) {
		ret = false;
		goto bail;
	}

	if (vga_default == pdev)
		vga_set_default_device(NULL);

	if (vgadev->decodes & (VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM))
		vga_decode_count--;

	/* Remove entry from list */
	list_del(&vgadev->list);
	vga_count--;

	/* Wake up all possible waiters */
	wake_up_all(&vga_wait_queue);
bail:
	spin_unlock_irqrestore(&vga_lock, flags);
	kfree(vgadev);
	return ret;
}

/* Called with the lock */
static void vga_update_device_decodes(struct vga_device *vgadev,
				      unsigned int new_decodes)
{
	struct device *dev = &vgadev->pdev->dev;
	unsigned int old_decodes = vgadev->decodes;
	unsigned int decodes_removed = ~new_decodes & old_decodes;
	unsigned int decodes_unlocked = vgadev->locks & decodes_removed;

	vgadev->decodes = new_decodes;

	vgaarb_info(dev, "VGA decodes changed: olddecodes=%s,decodes=%s:owns=%s\n",
		    vga_iostate_to_str(old_decodes),
		    vga_iostate_to_str(vgadev->decodes),
		    vga_iostate_to_str(vgadev->owns));

	/* If we removed locked decodes, lock count goes to zero, and release */
	if (decodes_unlocked) {
		if (decodes_unlocked & VGA_RSRC_LEGACY_IO)
			vgadev->io_lock_cnt = 0;
		if (decodes_unlocked & VGA_RSRC_LEGACY_MEM)
			vgadev->mem_lock_cnt = 0;
		__vga_put(vgadev, decodes_unlocked);
	}

	/* Change decodes counter */
	if (old_decodes & VGA_RSRC_LEGACY_MASK &&
	    !(new_decodes & VGA_RSRC_LEGACY_MASK))
		vga_decode_count--;
	if (!(old_decodes & VGA_RSRC_LEGACY_MASK) &&
	    new_decodes & VGA_RSRC_LEGACY_MASK)
		vga_decode_count++;
	vgaarb_dbg(dev, "decoding count now is: %d\n", vga_decode_count);
}

static void __vga_set_legacy_decoding(struct pci_dev *pdev,
				      unsigned int decodes,
				      bool userspace)
{
	struct vga_device *vgadev;
	unsigned long flags;

	decodes &= VGA_RSRC_LEGACY_MASK;

	spin_lock_irqsave(&vga_lock, flags);
	vgadev = vgadev_find(pdev);
	if (vgadev == NULL)
		goto bail;

	/* Don't let userspace futz with kernel driver decodes */
	if (userspace && vgadev->set_decode)
		goto bail;

	/* Update the device decodes + counter */
	vga_update_device_decodes(vgadev, decodes);

	/*
	 * XXX If somebody is going from "doesn't decode" to "decodes"
	 * state here, additional care must be taken as we may have pending
	 * ownership of non-legacy region.
	 */
bail:
	spin_unlock_irqrestore(&vga_lock, flags);
}

/**
 * vga_set_legacy_decoding
 * @pdev: PCI device of the VGA card
 * @decodes: bit mask of what legacy regions the card decodes
 *
 * Indicate to the arbiter if the card decodes legacy VGA IOs, legacy VGA
 * Memory, both, or none. All cards default to both, the card driver (fbdev for
 * example) should tell the arbiter if it has disabled legacy decoding, so the
 * card can be left out of the arbitration process (and can be safe to take
 * interrupts at any time.
 */
void vga_set_legacy_decoding(struct pci_dev *pdev, unsigned int decodes)
{
	__vga_set_legacy_decoding(pdev, decodes, false);
}
EXPORT_SYMBOL(vga_set_legacy_decoding);

/**
 * vga_client_register - register or unregister a VGA arbitration client
 * @pdev: PCI device of the VGA client
 * @set_decode: VGA decode change callback
 *
 * Clients have two callback mechanisms they can use.
 *
 * @set_decode callback: If a client can disable its GPU VGA resource, it
 * will get a callback from this to set the encode/decode state.
 *
 * Rationale: we cannot disable VGA decode resources unconditionally
 * because some single GPU laptops seem to require ACPI or BIOS access to
 * the VGA registers to control things like backlights etc. Hopefully newer
 * multi-GPU laptops do something saner, and desktops won't have any
 * special ACPI for this. The driver will get a callback when VGA
 * arbitration is first used by userspace since some older X servers have
 * issues.
 *
 * Does not check whether a client for @pdev has been registered already.
 *
 * To unregister, call vga_client_unregister().
 *
 * Returns: 0 on success, -ENODEV on failure
 */
int vga_client_register(struct pci_dev *pdev,
		unsigned int (*set_decode)(struct pci_dev *pdev, bool decode))
{
	unsigned long flags;
	struct vga_device *vgadev;

	spin_lock_irqsave(&vga_lock, flags);
	vgadev = vgadev_find(pdev);
	if (vgadev)
		vgadev->set_decode = set_decode;
	spin_unlock_irqrestore(&vga_lock, flags);
	if (!vgadev)
		return -ENODEV;
	return 0;
}
EXPORT_SYMBOL(vga_client_register);

/*
 * Char driver implementation
 *
 * Semantics is:
 *
 *  open       : Open user instance of the arbiter. By default, it's
 *                attached to the default VGA device of the system.
 *
 *  close      : Close user instance, release locks
 *
 *  read       : Return a string indicating the status of the target.
 *                An IO state string is of the form {io,mem,io+mem,none},
 *                mc and ic are respectively mem and io lock counts (for
 *                debugging/diagnostic only). "decodes" indicate what the
 *                card currently decodes, "owns" indicates what is currently
 *                enabled on it, and "locks" indicates what is locked by this
 *                card. If the card is unplugged, we get "invalid" then for
 *                card_ID and an -ENODEV error is returned for any command
 *                until a new card is targeted
 *
 *   "<card_ID>,decodes=<io_state>,owns=<io_state>,locks=<io_state> (ic,mc)"
 *
 * write       : write a command to the arbiter. List of commands is:
 *
 *   target <card_ID>   : switch target to card <card_ID> (see below)
 *   lock <io_state>    : acquire locks on target ("none" is invalid io_state)
 *   trylock <io_state> : non-blocking acquire locks on target
 *   unlock <io_state>  : release locks on target
 *   unlock all         : release all locks on target held by this user
 *   decodes <io_state> : set the legacy decoding attributes for the card
 *
 * poll         : event if something change on any card (not just the target)
 *
 * card_ID is of the form "PCI:domain:bus:dev.fn". It can be set to "default"
 * to go back to the system default card (TODO: not implemented yet).
 * Currently, only PCI is supported as a prefix, but the userland API may
 * support other bus types in the future, even if the current kernel
 * implementation doesn't.
 *
 * Note about locks:
 *
 * The driver keeps track of which user has what locks on which card. It
 * supports stacking, like the kernel one. This complicates the implementation
 * a bit, but makes the arbiter more tolerant to userspace problems and able
 * to properly cleanup in all cases when a process dies.
 * Currently, a max of 16 cards simultaneously can have locks issued from
 * userspace for a given user (file descriptor instance) of the arbiter.
 *
 * If the device is hot-unplugged, there is a hook inside the module to notify
 * it being added/removed in the system and automatically added/removed in
 * the arbiter.
 */

#define MAX_USER_CARDS         CONFIG_VGA_ARB_MAX_GPUS
#define PCI_INVALID_CARD       ((struct pci_dev *)-1UL)

/* Each user has an array of these, tracking which cards have locks */
struct vga_arb_user_card {
	struct pci_dev *pdev;
	unsigned int mem_cnt;
	unsigned int io_cnt;
};

struct vga_arb_private {
	struct list_head list;
	struct pci_dev *target;
	struct vga_arb_user_card cards[MAX_USER_CARDS];
	spinlock_t lock;
};

static LIST_HEAD(vga_user_list);
static DEFINE_SPINLOCK(vga_user_lock);


/*
 * Take a string in the format: "PCI:domain:bus:dev.fn" and return the
 * respective values. If the string is not in this format, return 0.
 */
static int vga_pci_str_to_vars(char *buf, int count, unsigned int *domain,
			       unsigned int *bus, unsigned int *devfn)
{
	int n;
	unsigned int slot, func;

	n = sscanf(buf, "PCI:%x:%x:%x.%x", domain, bus, &slot, &func);
	if (n != 4)
		return 0;

	*devfn = PCI_DEVFN(slot, func);

	return 1;
}

static ssize_t vga_arb_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct vga_arb_private *priv = file->private_data;
	struct vga_device *vgadev;
	struct pci_dev *pdev;
	unsigned long flags;
	size_t len;
	int rc;
	char *lbuf;

	lbuf = kmalloc(1024, GFP_KERNEL);
	if (lbuf == NULL)
		return -ENOMEM;

	/* Protect vga_list */
	spin_lock_irqsave(&vga_lock, flags);

	/* If we are targeting the default, use it */
	pdev = priv->target;
	if (pdev == NULL || pdev == PCI_INVALID_CARD) {
		spin_unlock_irqrestore(&vga_lock, flags);
		len = sprintf(lbuf, "invalid");
		goto done;
	}

	/* Find card vgadev structure */
	vgadev = vgadev_find(pdev);
	if (vgadev == NULL) {
		/*
		 * Wow, it's not in the list, that shouldn't happen, let's
		 * fix us up and return invalid card.
		 */
		spin_unlock_irqrestore(&vga_lock, flags);
		len = sprintf(lbuf, "invalid");
		goto done;
	}

	/* Fill the buffer with info */
	len = snprintf(lbuf, 1024,
		       "count:%d,PCI:%s,decodes=%s,owns=%s,locks=%s(%u:%u)\n",
		       vga_decode_count, pci_name(pdev),
		       vga_iostate_to_str(vgadev->decodes),
		       vga_iostate_to_str(vgadev->owns),
		       vga_iostate_to_str(vgadev->locks),
		       vgadev->io_lock_cnt, vgadev->mem_lock_cnt);

	spin_unlock_irqrestore(&vga_lock, flags);
done:

	/* Copy that to user */
	if (len > count)
		len = count;
	rc = copy_to_user(buf, lbuf, len);
	kfree(lbuf);
	if (rc)
		return -EFAULT;
	return len;
}

/*
 * TODO: To avoid parsing inside kernel and to improve the speed we may
 * consider use ioctl here
 */
static ssize_t vga_arb_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct vga_arb_private *priv = file->private_data;
	struct vga_arb_user_card *uc = NULL;
	struct pci_dev *pdev;

	unsigned int io_state;

	char kbuf[64], *curr_pos;
	size_t remaining = count;

	int ret_val;
	int i;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	curr_pos = kbuf;
	kbuf[count] = '\0';

	if (strncmp(curr_pos, "lock ", 5) == 0) {
		curr_pos += 5;
		remaining -= 5;

		pr_debug("client 0x%p called 'lock'\n", priv);

		if (!vga_str_to_iostate(curr_pos, remaining, &io_state)) {
			ret_val = -EPROTO;
			goto done;
		}
		if (io_state == VGA_RSRC_NONE) {
			ret_val = -EPROTO;
			goto done;
		}

		pdev = priv->target;
		if (priv->target == NULL) {
			ret_val = -ENODEV;
			goto done;
		}

		vga_get_uninterruptible(pdev, io_state);

		/* Update the client's locks lists */
		for (i = 0; i < MAX_USER_CARDS; i++) {
			if (priv->cards[i].pdev == pdev) {
				if (io_state & VGA_RSRC_LEGACY_IO)
					priv->cards[i].io_cnt++;
				if (io_state & VGA_RSRC_LEGACY_MEM)
					priv->cards[i].mem_cnt++;
				break;
			}
		}

		ret_val = count;
		goto done;
	} else if (strncmp(curr_pos, "unlock ", 7) == 0) {
		curr_pos += 7;
		remaining -= 7;

		pr_debug("client 0x%p called 'unlock'\n", priv);

		if (strncmp(curr_pos, "all", 3) == 0)
			io_state = VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
		else {
			if (!vga_str_to_iostate
			    (curr_pos, remaining, &io_state)) {
				ret_val = -EPROTO;
				goto done;
			}
			/* TODO: Add this?
			   if (io_state == VGA_RSRC_NONE) {
			   ret_val = -EPROTO;
			   goto done;
			   }
			  */
		}

		pdev = priv->target;
		if (priv->target == NULL) {
			ret_val = -ENODEV;
			goto done;
		}
		for (i = 0; i < MAX_USER_CARDS; i++) {
			if (priv->cards[i].pdev == pdev)
				uc = &priv->cards[i];
		}

		if (!uc) {
			ret_val = -EINVAL;
			goto done;
		}

		if (io_state & VGA_RSRC_LEGACY_IO && uc->io_cnt == 0) {
			ret_val = -EINVAL;
			goto done;
		}

		if (io_state & VGA_RSRC_LEGACY_MEM && uc->mem_cnt == 0) {
			ret_val = -EINVAL;
			goto done;
		}

		vga_put(pdev, io_state);

		if (io_state & VGA_RSRC_LEGACY_IO)
			uc->io_cnt--;
		if (io_state & VGA_RSRC_LEGACY_MEM)
			uc->mem_cnt--;

		ret_val = count;
		goto done;
	} else if (strncmp(curr_pos, "trylock ", 8) == 0) {
		curr_pos += 8;
		remaining -= 8;

		pr_debug("client 0x%p called 'trylock'\n", priv);

		if (!vga_str_to_iostate(curr_pos, remaining, &io_state)) {
			ret_val = -EPROTO;
			goto done;
		}
		/* TODO: Add this?
		   if (io_state == VGA_RSRC_NONE) {
		   ret_val = -EPROTO;
		   goto done;
		   }
		 */

		pdev = priv->target;
		if (priv->target == NULL) {
			ret_val = -ENODEV;
			goto done;
		}

		if (vga_tryget(pdev, io_state)) {
			/* Update the client's locks lists... */
			for (i = 0; i < MAX_USER_CARDS; i++) {
				if (priv->cards[i].pdev == pdev) {
					if (io_state & VGA_RSRC_LEGACY_IO)
						priv->cards[i].io_cnt++;
					if (io_state & VGA_RSRC_LEGACY_MEM)
						priv->cards[i].mem_cnt++;
					break;
				}
			}
			ret_val = count;
			goto done;
		} else {
			ret_val = -EBUSY;
			goto done;
		}

	} else if (strncmp(curr_pos, "target ", 7) == 0) {
		unsigned int domain, bus, devfn;
		struct vga_device *vgadev;

		curr_pos += 7;
		remaining -= 7;
		pr_debug("client 0x%p called 'target'\n", priv);
		/* If target is default */
		if (!strncmp(curr_pos, "default", 7))
			pdev = pci_dev_get(vga_default_device());
		else {
			if (!vga_pci_str_to_vars(curr_pos, remaining,
						 &domain, &bus, &devfn)) {
				ret_val = -EPROTO;
				goto done;
			}
			pdev = pci_get_domain_bus_and_slot(domain, bus, devfn);
			if (!pdev) {
				pr_debug("invalid PCI address %04x:%02x:%02x.%x\n",
					 domain, bus, PCI_SLOT(devfn),
					 PCI_FUNC(devfn));
				ret_val = -ENODEV;
				goto done;
			}

			pr_debug("%s ==> %04x:%02x:%02x.%x pdev %p\n", curr_pos,
				domain, bus, PCI_SLOT(devfn), PCI_FUNC(devfn),
				pdev);
		}

		vgadev = vgadev_find(pdev);
		pr_debug("vgadev %p\n", vgadev);
		if (vgadev == NULL) {
			if (pdev) {
				vgaarb_dbg(&pdev->dev, "not a VGA device\n");
				pci_dev_put(pdev);
			}

			ret_val = -ENODEV;
			goto done;
		}

		priv->target = pdev;
		for (i = 0; i < MAX_USER_CARDS; i++) {
			if (priv->cards[i].pdev == pdev)
				break;
			if (priv->cards[i].pdev == NULL) {
				priv->cards[i].pdev = pdev;
				priv->cards[i].io_cnt = 0;
				priv->cards[i].mem_cnt = 0;
				break;
			}
		}
		if (i == MAX_USER_CARDS) {
			vgaarb_dbg(&pdev->dev, "maximum user cards (%d) number reached, ignoring this one!\n",
				MAX_USER_CARDS);
			pci_dev_put(pdev);
			/* XXX: Which value to return? */
			ret_val =  -ENOMEM;
			goto done;
		}

		ret_val = count;
		pci_dev_put(pdev);
		goto done;


	} else if (strncmp(curr_pos, "decodes ", 8) == 0) {
		curr_pos += 8;
		remaining -= 8;
		pr_debug("client 0x%p called 'decodes'\n", priv);

		if (!vga_str_to_iostate(curr_pos, remaining, &io_state)) {
			ret_val = -EPROTO;
			goto done;
		}
		pdev = priv->target;
		if (priv->target == NULL) {
			ret_val = -ENODEV;
			goto done;
		}

		__vga_set_legacy_decoding(pdev, io_state, true);
		ret_val = count;
		goto done;
	}
	/* If we got here, the message written is not part of the protocol! */
	return -EPROTO;

done:
	return ret_val;
}

static __poll_t vga_arb_fpoll(struct file *file, poll_table *wait)
{
	pr_debug("%s\n", __func__);

	poll_wait(file, &vga_wait_queue, wait);
	return EPOLLIN;
}

static int vga_arb_open(struct inode *inode, struct file *file)
{
	struct vga_arb_private *priv;
	unsigned long flags;

	pr_debug("%s\n", __func__);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;
	spin_lock_init(&priv->lock);
	file->private_data = priv;

	spin_lock_irqsave(&vga_user_lock, flags);
	list_add(&priv->list, &vga_user_list);
	spin_unlock_irqrestore(&vga_user_lock, flags);

	/* Set the client's lists of locks */
	priv->target = vga_default_device(); /* Maybe this is still null! */
	priv->cards[0].pdev = priv->target;
	priv->cards[0].io_cnt = 0;
	priv->cards[0].mem_cnt = 0;

	return 0;
}

static int vga_arb_release(struct inode *inode, struct file *file)
{
	struct vga_arb_private *priv = file->private_data;
	struct vga_arb_user_card *uc;
	unsigned long flags;
	int i;

	pr_debug("%s\n", __func__);

	spin_lock_irqsave(&vga_user_lock, flags);
	list_del(&priv->list);
	for (i = 0; i < MAX_USER_CARDS; i++) {
		uc = &priv->cards[i];
		if (uc->pdev == NULL)
			continue;
		vgaarb_dbg(&uc->pdev->dev, "uc->io_cnt == %d, uc->mem_cnt == %d\n",
			uc->io_cnt, uc->mem_cnt);
		while (uc->io_cnt--)
			vga_put(uc->pdev, VGA_RSRC_LEGACY_IO);
		while (uc->mem_cnt--)
			vga_put(uc->pdev, VGA_RSRC_LEGACY_MEM);
	}
	spin_unlock_irqrestore(&vga_user_lock, flags);

	kfree(priv);

	return 0;
}

/*
 * Callback any registered clients to let them know we have a change in VGA
 * cards.
 */
static void vga_arbiter_notify_clients(void)
{
	struct vga_device *vgadev;
	unsigned long flags;
	unsigned int new_decodes;
	bool new_state;

	if (!vga_arbiter_used)
		return;

	new_state = (vga_count > 1) ? false : true;

	spin_lock_irqsave(&vga_lock, flags);
	list_for_each_entry(vgadev, &vga_list, list) {
		if (vgadev->set_decode) {
			new_decodes = vgadev->set_decode(vgadev->pdev,
							 new_state);
			vga_update_device_decodes(vgadev, new_decodes);
		}
	}
	spin_unlock_irqrestore(&vga_lock, flags);
}

static int pci_notify(struct notifier_block *nb, unsigned long action,
		      void *data)
{
	struct device *dev = data;
	struct pci_dev *pdev = to_pci_dev(dev);
	bool notify = false;

	vgaarb_dbg(dev, "%s\n", __func__);

	/* Only deal with VGA class devices */
	if (!pci_is_vga(pdev))
		return 0;

	/*
	 * For now, we're only interested in devices added and removed.
	 * I didn't test this thing here, so someone needs to double check
	 * for the cases of hot-pluggable VGA cards.
	 */
	if (action == BUS_NOTIFY_ADD_DEVICE)
		notify = vga_arbiter_add_pci_device(pdev);
	else if (action == BUS_NOTIFY_DEL_DEVICE)
		notify = vga_arbiter_del_pci_device(pdev);

	if (notify)
		vga_arbiter_notify_clients();
	return 0;
}

static struct notifier_block pci_notifier = {
	.notifier_call = pci_notify,
};

static const struct file_operations vga_arb_device_fops = {
	.read = vga_arb_read,
	.write = vga_arb_write,
	.poll = vga_arb_fpoll,
	.open = vga_arb_open,
	.release = vga_arb_release,
	.llseek = noop_llseek,
};

static struct miscdevice vga_arb_device = {
	MISC_DYNAMIC_MINOR, "vga_arbiter", &vga_arb_device_fops
};

static int __init vga_arb_device_init(void)
{
	int rc;
	struct pci_dev *pdev;

	rc = misc_register(&vga_arb_device);
	if (rc < 0)
		pr_err("error %d registering device\n", rc);

	bus_register_notifier(&pci_bus_type, &pci_notifier);

	/* Add all VGA class PCI devices by default */
	pdev = NULL;
	while ((pdev =
		pci_get_subsys(PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
			       PCI_ANY_ID, pdev)) != NULL) {
		if (pci_is_vga(pdev))
			vga_arbiter_add_pci_device(pdev);
	}

	pr_info("loaded\n");
	return rc;
}
subsys_initcall_sync(vga_arb_device_init);
