/*
 * vgaarb.c: Implements the VGA arbitration. For details refer to
 * Documentation/gpu/vgaarbiter.rst
 *
 *
 * (C) Copyright 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 * (C) Copyright 2007 Paulo R. Zanoni <przanoni@gmail.com>
 * (C) Copyright 2007, 2009 Tiago Vignatti <vignatti@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS
 * IN THE SOFTWARE.
 *
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

#include <linux/uaccess.h>

#include <linux/vgaarb.h>

static void vga_arbiter_notify_clients(void);
/*
 * We keep a list of all vga devices in the system to speed
 * up the various operations of the arbiter
 */
struct vga_device {
	struct list_head list;
	struct pci_dev *pdev;
	unsigned int decodes;	/* what does it decodes */
	unsigned int owns;	/* what does it owns */
	unsigned int locks;	/* what does it locks */
	unsigned int io_lock_cnt;	/* legacy IO lock count */
	unsigned int mem_lock_cnt;	/* legacy MEM lock count */
	unsigned int io_norm_cnt;	/* normal IO count */
	unsigned int mem_norm_cnt;	/* normal MEM count */
	bool bridge_has_one_vga;
	/* allow IRQ enable/disable hook */
	void *cookie;
	void (*irq_set_state)(void *cookie, bool enable);
	unsigned int (*set_vga_decode)(void *cookie, bool decode);
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

static int vga_str_to_iostate(char *buf, int str_size, int *io_state)
{
	/* we could in theory hand out locks on IO and mem
	 * separately to userspace but it can cause deadlocks */
	if (strncmp(buf, "none", 4) == 0) {
		*io_state = VGA_RSRC_NONE;
		return 1;
	}

	/* XXX We're not chekcing the str_size! */
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

/* this is only used a cookie - it should not be dereferenced */
static struct pci_dev *vga_default;

static void vga_arb_device_card_gone(struct pci_dev *pdev);

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
 * This can be defined by the platform. The default implementation
 * is rather dumb and will probably only work properly on single
 * vga card setups and/or x86 platforms.
 *
 * If your VGA default device is not PCI, you'll have to return
 * NULL here. In this case, I assume it will not conflict with
 * any PCI card. If this is not true, I'll have to define two archs
 * hooks for enabling/disabling the VGA default device if that is
 * possible. This may be a problem with real _ISA_ VGA cards, in
 * addition to a PCI one. I don't know at this point how to deal
 * with that card. Can theirs IOs be disabled at all ? If not, then
 * I suppose it's a matter of having the proper arch hook telling
 * us about it, so we basically never allow anybody to succeed a
 * vga_get()...
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

static inline void vga_irq_set_state(struct vga_device *vgadev, bool state)
{
	if (vgadev->irq_set_state)
		vgadev->irq_set_state(vgadev->cookie, state);
}


/* If we don't ever use VGA arb we should avoid
   turning off anything anywhere due to old X servers getting
   confused about the boot device not being VGA */
static void vga_check_first_use(void)
{
	/* we should inform all GPUs in the system that
	 * VGA arb has occurred and to try and disable resources
	 * if they can */
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

	/* Account for "normal" resources to lock. If we decode the legacy,
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

	/* We don't need to request a legacy resource, we just enable
	 * appropriate decoding and go
	 */
	legacy_wants = wants & VGA_RSRC_LEGACY_MASK;
	if (legacy_wants == 0)
		goto enable_them;

	/* Ok, we don't, let's find out how we need to kick off */
	list_for_each_entry(conflict, &vga_list, list) {
		unsigned int lwants = legacy_wants;
		unsigned int change_bridge = 0;

		/* Don't conflict with myself */
		if (vgadev == conflict)
			continue;

		/* Check if the architecture allows a conflict between those
		 * 2 devices or if they are on separate domains
		 */
		if (!vga_conflicts(vgadev->pdev, conflict->pdev))
			continue;

		/* We have a possible conflict. before we go further, we must
		 * check if we sit on the same bus as the conflicting device.
		 * if we don't, then we must tie both IO and MEM resources
		 * together since there is only a single bit controlling
		 * VGA forwarding on P2P bridges
		 */
		if (vgadev->pdev->bus != conflict->pdev->bus) {
			change_bridge = 1;
			lwants = VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
		}

		/* Check if the guy has a lock on the resource. If he does,
		 * return the conflicting entry
		 */
		if (conflict->locks & lwants)
			return conflict;

		/* Ok, now check if it owns the resource we want.  We can
		 * lock resources that are not decoded, therefore a device
		 * can own resources it doesn't decode.
		 */
		match = lwants & conflict->owns;
		if (!match)
			continue;

		/* looks like he doesn't have a lock, we can steal
		 * them from him
		 */

		flags = 0;
		pci_bits = 0;

		/* If we can't control legacy resources via the bridge, we
		 * also need to disable normal decoding.
		 */
		if (!conflict->bridge_has_one_vga) {
			if ((match & conflict->decodes) & VGA_RSRC_LEGACY_MEM)
				pci_bits |= PCI_COMMAND_MEMORY;
			if ((match & conflict->decodes) & VGA_RSRC_LEGACY_IO)
				pci_bits |= PCI_COMMAND_IO;

			if (pci_bits) {
				vga_irq_set_state(conflict, false);
				flags |= PCI_VGA_STATE_CHANGE_DECODES;
			}
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
	/* ok dude, we got it, everybody conflicting has been disabled, let's
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

	if (!vgadev->bridge_has_one_vga)
		vga_irq_set_state(vgadev, true);

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

	/* Update our counters, and account for equivalent legacy resources
	 * if we decode them
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

	/* Just clear lock bits, we do lazy operations so we don't really
	 * have to bother about anything else at this point
	 */
	if (vgadev->io_lock_cnt == 0)
		vgadev->locks &= ~VGA_RSRC_LEGACY_IO;
	if (vgadev->mem_lock_cnt == 0)
		vgadev->locks &= ~VGA_RSRC_LEGACY_MEM;

	/* Kick the wait queue in case somebody was waiting if we actually
	 * released something
	 */
	if (old_locks != vgadev->locks)
		wake_up_all(&vga_wait_queue);
}

/**
 * vga_get - acquire & locks VGA resources
 * @pdev: pci device of the VGA card or NULL for the system default
 * @rsrc: bit mask of resources to acquire and lock
 * @interruptible: blocking should be interruptible by signals ?
 *
 * This function acquires VGA resources for the given card and mark those
 * resources locked. If the resource requested are "normal" (and not legacy)
 * resources, the arbiter will first check whether the card is doing legacy
 * decoding for that type of resource. If yes, the lock is "converted" into a
 * legacy resource lock.
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
	/* The one who calls us should check for this, but lets be sure... */
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


		/* We have a conflict, we wait until somebody kicks the
		 * work queue. Currently we have one work queue that we
		 * kick each time some resources are released, but it would
		 * be fairly easy to have a per device one so that we only
		 * need to attach to the conflicting device
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
 * @pdev: pci devivce of VGA card or NULL for system default
 * @rsrc: bit mask of resources to acquire and lock
 *
 * This function performs the same operation as vga_get(), but will return an
 * error (-EBUSY) instead of blocking if the resources are already locked by
 * another card. It can be called in any context
 *
 * On success, release the VGA resource again with vga_put().
 *
 * Returns:
 *
 * 0 on success, negative error code on failure.
 */
int vga_tryget(struct pci_dev *pdev, unsigned int rsrc)
{
	struct vga_device *vgadev;
	unsigned long flags;
	int rc = 0;

	vga_check_first_use();

	/* The one who calls us should check for this, but lets be sure... */
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
EXPORT_SYMBOL(vga_tryget);

/**
 * vga_put - release lock on legacy VGA resources
 * @pdev: pci device of VGA card or NULL for system default
 * @rsrc: but mask of resource to release
 *
 * This fuction releases resources previously locked by vga_get() or
 * vga_tryget(). The resources aren't disabled right away, so that a subsequence
 * vga_get() on the same card will succeed immediately. Resources have a
 * counter, so locks are only released if the counter reaches 0.
 */
void vga_put(struct pci_dev *pdev, unsigned int rsrc)
{
	struct vga_device *vgadev;
	unsigned long flags;

	/* The one who calls us should check for this, but lets be sure... */
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

	if (list_empty(&vga_list))
		return;

	/* okay iterate the new devices bridge hierarachy */
	new_bus = vgadev->pdev->bus;
	while (new_bus) {
		new_bridge = new_bus->self;

		/* go through list of devices already registered */
		list_for_each_entry(same_bridge_vgadev, &vga_list, list) {
			bus = same_bridge_vgadev->pdev->bus;
			bridge = bus->self;

			/* see if the share a bridge with this device */
			if (new_bridge == bridge) {
				/*
				 * If their direct parent bridge is the same
				 * as any bridge of this device then it can't
				 * be used for that device.
				 */
				same_bridge_vgadev->bridge_has_one_vga = false;
			}

			/*
			 * Now iterate the previous devices bridge hierarchy.
			 * If the new devices parent bridge is in the other
			 * devices hierarchy then we can't use it to control
			 * this device
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
}

/*
 * Currently, we assume that the "initial" setup of the system is
 * not sane, that is we come up with conflicting devices and let
 * the arbiter's client decides if devices decodes or not legacy
 * things.
 */
static bool vga_arbiter_add_pci_device(struct pci_dev *pdev)
{
	struct vga_device *vgadev;
	unsigned long flags;
	struct pci_bus *bus;
	struct pci_dev *bridge;
	u16 cmd;

	/* Only deal with VGA class devices */
	if ((pdev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
		return false;

	/* Allocate structure */
	vgadev = kzalloc(sizeof(struct vga_device), GFP_KERNEL);
	if (vgadev == NULL) {
		vgaarb_err(&pdev->dev, "failed to allocate VGA arbiter data\n");
		/*
		 * What to do on allocation failure ? For now, let's just do
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

	/* by default mark it as decoding */
	vga_decode_count++;
	/* Mark that we "own" resources based on our enables, we will
	 * clear that below if the bridge isn't forwarding
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

	/* Deal with VGA default device. Use first enabled one
	 * by default if arch doesn't have it's own hook
	 */
	if (vga_default == NULL &&
	    ((vgadev->owns & VGA_RSRC_LEGACY_MASK) == VGA_RSRC_LEGACY_MASK)) {
		vgaarb_info(&pdev->dev, "setting as boot VGA device\n");
		vga_set_default_device(pdev);
	}

	vga_arbiter_check_bridge_sharing(vgadev);

	/* Add to the list */
	list_add(&vgadev->list, &vga_list);
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
	/* Notify userland driver that the device is gone so it discards
	 * it's copies of the pci_dev pointer
	 */
	vga_arb_device_card_gone(pdev);

	/* Wake up all possible waiters */
	wake_up_all(&vga_wait_queue);
bail:
	spin_unlock_irqrestore(&vga_lock, flags);
	kfree(vgadev);
	return ret;
}

/* this is called with the lock */
static inline void vga_update_device_decodes(struct vga_device *vgadev,
					     int new_decodes)
{
	struct device *dev = &vgadev->pdev->dev;
	int old_decodes, decodes_removed, decodes_unlocked;

	old_decodes = vgadev->decodes;
	decodes_removed = ~new_decodes & old_decodes;
	decodes_unlocked = vgadev->locks & decodes_removed;
	vgadev->decodes = new_decodes;

	vgaarb_info(dev, "changed VGA decodes: olddecodes=%s,decodes=%s:owns=%s\n",
		vga_iostate_to_str(old_decodes),
		vga_iostate_to_str(vgadev->decodes),
		vga_iostate_to_str(vgadev->owns));

	/* if we removed locked decodes, lock count goes to zero, and release */
	if (decodes_unlocked) {
		if (decodes_unlocked & VGA_RSRC_LEGACY_IO)
			vgadev->io_lock_cnt = 0;
		if (decodes_unlocked & VGA_RSRC_LEGACY_MEM)
			vgadev->mem_lock_cnt = 0;
		__vga_put(vgadev, decodes_unlocked);
	}

	/* change decodes counter */
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

	/* don't let userspace futz with kernel driver decodes */
	if (userspace && vgadev->set_vga_decode)
		goto bail;

	/* update the device decodes + counter */
	vga_update_device_decodes(vgadev, decodes);

	/* XXX if somebody is going from "doesn't decode" to "decodes" state
	 * here, additional care must be taken as we may have pending owner
	 * ship of non-legacy region ...
	 */
bail:
	spin_unlock_irqrestore(&vga_lock, flags);
}

void vga_set_legacy_decoding(struct pci_dev *pdev, unsigned int decodes)
{
	__vga_set_legacy_decoding(pdev, decodes, false);
}
EXPORT_SYMBOL(vga_set_legacy_decoding);

/**
 * vga_client_register - register or unregister a VGA arbitration client
 * @pdev: pci device of the VGA client
 * @cookie: client cookie to be used in callbacks
 * @irq_set_state: irq state change callback
 * @set_vga_decode: vga decode change callback
 *
 * Clients have two callback mechanisms they can use.
 *
 * @irq_set_state callback: If a client can't disable its GPUs VGA
 * resources, then we need to be able to ask it to turn off its irqs when we
 * turn off its mem and io decoding.
 *
 * @set_vga_decode callback: If a client can disable its GPU VGA resource, it
 * will get a callback from this to set the encode/decode state.
 *
 * Rationale: we cannot disable VGA decode resources unconditionally some single
 * GPU laptops seem to require ACPI or BIOS access to the VGA registers to
 * control things like backlights etc.  Hopefully newer multi-GPU laptops do
 * something saner, and desktops won't have any special ACPI for this. The
 * driver will get a callback when VGA arbitration is first used by userspace
 * since some older X servers have issues.
 *
 * This function does not check whether a client for @pdev has been registered
 * already.
 *
 * To unregister just call this function with @irq_set_state and @set_vga_decode
 * both set to NULL for the same @pdev as originally used to register them.
 *
 * Returns: 0 on success, -1 on failure
 */
int vga_client_register(struct pci_dev *pdev, void *cookie,
			void (*irq_set_state)(void *cookie, bool state),
			unsigned int (*set_vga_decode)(void *cookie,
						       bool decode))
{
	int ret = -ENODEV;
	struct vga_device *vgadev;
	unsigned long flags;

	spin_lock_irqsave(&vga_lock, flags);
	vgadev = vgadev_find(pdev);
	if (!vgadev)
		goto bail;

	vgadev->irq_set_state = irq_set_state;
	vgadev->set_vga_decode = set_vga_decode;
	vgadev->cookie = cookie;
	ret = 0;

bail:
	spin_unlock_irqrestore(&vga_lock, flags);
	return ret;

}
EXPORT_SYMBOL(vga_client_register);

/*
 * Char driver implementation
 *
 * Semantics is:
 *
 *  open       : open user instance of the arbitrer. by default, it's
 *                attached to the default VGA device of the system.
 *
 *  close      : close user instance, release locks
 *
 *  read       : return a string indicating the status of the target.
 *                an IO state string is of the form {io,mem,io+mem,none},
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
 *   lock <io_state>    : acquires locks on target ("none" is invalid io_state)
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
 * supports stacking, like the kernel one. This complexifies the implementation
 * a bit, but makes the arbiter more tolerant to userspace problems and able
 * to properly cleanup in all cases when a process dies.
 * Currently, a max of 16 cards simultaneously can have locks issued from
 * userspace for a given user (file descriptor instance) of the arbiter.
 *
 * If the device is hot-unplugged, there is a hook inside the module to notify
 * they being added/removed in the system and automatically added/removed in
 * the arbiter.
 */

#define MAX_USER_CARDS         CONFIG_VGA_ARB_MAX_GPUS
#define PCI_INVALID_CARD       ((struct pci_dev *)-1UL)

/*
 * Each user has an array of these, tracking which cards have locks
 */
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
 * This function gets a string in the format: "PCI:domain:bus:dev.fn" and
 * returns the respective values. If the string is not in this format,
 * it returns 0.
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

	/* Shields against vga_arb_device_card_gone (pci_dev going
	 * away), and allows access to vga list
	 */
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
		/* Wow, it's not in the list, that shouldn't happen,
		 * let's fix us up and return invalid card
		 */
		if (pdev == priv->target)
			vga_arb_device_card_gone(pdev);
		spin_unlock_irqrestore(&vga_lock, flags);
		len = sprintf(lbuf, "invalid");
		goto done;
	}

	/* Fill the buffer with infos */
	len = snprintf(lbuf, 1024,
		       "count:%d,PCI:%s,decodes=%s,owns=%s,locks=%s(%d:%d)\n",
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
	kbuf[count] = '\0';	/* Just to make sure... */

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
		/* if target is default */
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
			/* XXX: which value to return? */
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

	/* Set the client' lists of locks */
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

static void vga_arb_device_card_gone(struct pci_dev *pdev)
{
}

/*
 * callback any registered clients to let them know we have a
 * change in VGA cards
 */
static void vga_arbiter_notify_clients(void)
{
	struct vga_device *vgadev;
	unsigned long flags;
	uint32_t new_decodes;
	bool new_state;

	if (!vga_arbiter_used)
		return;

	spin_lock_irqsave(&vga_lock, flags);
	list_for_each_entry(vgadev, &vga_list, list) {
		if (vga_count > 1)
			new_state = false;
		else
			new_state = true;
		if (vgadev->set_vga_decode) {
			new_decodes = vgadev->set_vga_decode(vgadev->cookie,
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

	/* For now we're only intereted in devices added and removed. I didn't
	 * test this thing here, so someone needs to double check for the
	 * cases of hotplugable vga cards. */
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

static void __init vga_arb_select_default_device(void)
{
	struct pci_dev *pdev;
	struct vga_device *vgadev;

#if defined(CONFIG_X86) || defined(CONFIG_IA64)
	list_for_each_entry(vgadev, &vga_list, list) {
		struct device *dev = &vgadev->pdev->dev;
		/*
		 * Override vga_arbiter_add_pci_device()'s I/O based detection
		 * as it may take the wrong device (e.g. on Apple system under
		 * EFI).
		 *
		 * Select the device owning the boot framebuffer if there is
		 * one.
		 */
		resource_size_t start, end, limit;
		unsigned long flags;
		int i;

		limit = screen_info.lfb_base + screen_info.lfb_size;

		/* Does firmware framebuffer belong to us? */
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			flags = pci_resource_flags(vgadev->pdev, i);

			if ((flags & IORESOURCE_MEM) == 0)
				continue;

			start = pci_resource_start(vgadev->pdev, i);
			end  = pci_resource_end(vgadev->pdev, i);

			if (!start || !end)
				continue;

			if (screen_info.lfb_base < start || limit >= end)
				continue;

			if (!vga_default_device())
				vgaarb_info(dev, "setting as boot device\n");
			else if (vgadev->pdev != vga_default_device())
				vgaarb_info(dev, "overriding boot device\n");
			vga_set_default_device(vgadev->pdev);
		}
	}
#endif

	if (!vga_default_device()) {
		list_for_each_entry(vgadev, &vga_list, list) {
			struct device *dev = &vgadev->pdev->dev;
			u16 cmd;

			pdev = vgadev->pdev;
			pci_read_config_word(pdev, PCI_COMMAND, &cmd);
			if (cmd & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) {
				vgaarb_info(dev, "setting as boot device (VGA legacy resources not available)\n");
				vga_set_default_device(pdev);
				break;
			}
		}
	}

	if (!vga_default_device()) {
		vgadev = list_first_entry_or_null(&vga_list,
						  struct vga_device, list);
		if (vgadev) {
			struct device *dev = &vgadev->pdev->dev;
			vgaarb_info(dev, "setting as boot device (VGA legacy resources not available)\n");
			vga_set_default_device(vgadev->pdev);
		}
	}
}

static int __init vga_arb_device_init(void)
{
	int rc;
	struct pci_dev *pdev;
	struct vga_device *vgadev;

	rc = misc_register(&vga_arb_device);
	if (rc < 0)
		pr_err("error %d registering device\n", rc);

	bus_register_notifier(&pci_bus_type, &pci_notifier);

	/* We add all PCI devices satisfying VGA class in the arbiter by
	 * default */
	pdev = NULL;
	while ((pdev =
		pci_get_subsys(PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
			       PCI_ANY_ID, pdev)) != NULL)
		vga_arbiter_add_pci_device(pdev);

	list_for_each_entry(vgadev, &vga_list, list) {
		struct device *dev = &vgadev->pdev->dev;

		if (vgadev->bridge_has_one_vga)
			vgaarb_info(dev, "bridge control possible\n");
		else
			vgaarb_info(dev, "no bridge control possible\n");
	}

	vga_arb_select_default_device();

	pr_info("loaded\n");
	return rc;
}
subsys_initcall(vga_arb_device_init);
