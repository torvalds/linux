/*
 * EFI capsule support.
 *
 * Copyright 2013 Intel Corporation; author Matt Fleming
 *
 * This file is part of the Linux kernel, and is made available under
 * the terms of the GNU General Public License version 2.
 */

#define pr_fmt(fmt) "efi: " fmt

#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/highmem.h>
#include <linux/efi.h>
#include <linux/vmalloc.h>
#include <asm/io.h>

typedef struct {
	u64 length;
	u64 data;
} efi_capsule_block_desc_t;

static bool capsule_pending;
static bool stop_capsules;
static int efi_reset_type = -1;

/*
 * capsule_mutex serialises access to both capsule_pending and
 * efi_reset_type and stop_capsules.
 */
static DEFINE_MUTEX(capsule_mutex);

/**
 * efi_capsule_pending - has a capsule been passed to the firmware?
 * @reset_type: store the type of EFI reset if capsule is pending
 *
 * To ensure that the registered capsule is processed correctly by the
 * firmware we need to perform a specific type of reset. If a capsule is
 * pending return the reset type in @reset_type.
 *
 * This function will race with callers of efi_capsule_update(), for
 * example, calling this function while somebody else is in
 * efi_capsule_update() but hasn't reached efi_capsue_update_locked()
 * will miss the updates to capsule_pending and efi_reset_type after
 * efi_capsule_update_locked() completes.
 *
 * A non-racy use is from platform reboot code because we use
 * system_state to ensure no capsules can be sent to the firmware once
 * we're at SYSTEM_RESTART. See efi_capsule_update_locked().
 */
bool efi_capsule_pending(int *reset_type)
{
	if (!capsule_pending)
		return false;

	if (reset_type)
		*reset_type = efi_reset_type;

	return true;
}

/*
 * Whitelist of EFI capsule flags that we support.
 *
 * We do not handle EFI_CAPSULE_INITIATE_RESET because that would
 * require us to prepare the kernel for reboot. Refuse to load any
 * capsules with that flag and any other flags that we do not know how
 * to handle.
 */
#define EFI_CAPSULE_SUPPORTED_FLAG_MASK			\
	(EFI_CAPSULE_PERSIST_ACROSS_RESET | EFI_CAPSULE_POPULATE_SYSTEM_TABLE)

/**
 * efi_capsule_supported - does the firmware support the capsule?
 * @guid: vendor guid of capsule
 * @flags: capsule flags
 * @size: size of capsule data
 * @reset: the reset type required for this capsule
 *
 * Check whether a capsule with @flags is supported by the firmware
 * and that @size doesn't exceed the maximum size for a capsule.
 *
 * No attempt is made to check @reset against the reset type required
 * by any pending capsules because of the races involved.
 */
int efi_capsule_supported(efi_guid_t guid, u32 flags, size_t size, int *reset)
{
	efi_capsule_header_t capsule;
	efi_capsule_header_t *cap_list[] = { &capsule };
	efi_status_t status;
	u64 max_size;

	if (flags & ~EFI_CAPSULE_SUPPORTED_FLAG_MASK)
		return -EINVAL;

	capsule.headersize = capsule.imagesize = sizeof(capsule);
	memcpy(&capsule.guid, &guid, sizeof(efi_guid_t));
	capsule.flags = flags;

	status = efi.query_capsule_caps(cap_list, 1, &max_size, reset);
	if (status != EFI_SUCCESS)
		return efi_status_to_err(status);

	if (size > max_size)
		return -ENOSPC;

	return 0;
}
EXPORT_SYMBOL_GPL(efi_capsule_supported);

/*
 * Every scatter gather list (block descriptor) page must end with a
 * continuation pointer. The last continuation pointer of the last
 * page must be zero to mark the end of the chain.
 */
#define SGLIST_PER_PAGE	((PAGE_SIZE / sizeof(efi_capsule_block_desc_t)) - 1)

/*
 * How many scatter gather list (block descriptor) pages do we need
 * to map @count pages?
 */
static inline unsigned int sg_pages_num(unsigned int count)
{
	return DIV_ROUND_UP(count, SGLIST_PER_PAGE);
}

/**
 * efi_capsule_update_locked - pass a single capsule to the firmware
 * @capsule: capsule to send to the firmware
 * @sg_pages: array of scatter gather (block descriptor) pages
 * @reset: the reset type required for @capsule
 *
 * Since this function must be called under capsule_mutex check
 * whether efi_reset_type will conflict with @reset, and atomically
 * set it and capsule_pending if a capsule was successfully sent to
 * the firmware.
 *
 * We also check to see if the system is about to restart, and if so,
 * abort. This avoids races between efi_capsule_update() and
 * efi_capsule_pending().
 */
static int
efi_capsule_update_locked(efi_capsule_header_t *capsule,
			  struct page **sg_pages, int reset)
{
	efi_physical_addr_t sglist_phys;
	efi_status_t status;

	lockdep_assert_held(&capsule_mutex);

	/*
	 * If someone has already registered a capsule that requires a
	 * different reset type, we're out of luck and must abort.
	 */
	if (efi_reset_type >= 0 && efi_reset_type != reset) {
		pr_err("Conflicting capsule reset type %d (%d).\n",
		       reset, efi_reset_type);
		return -EINVAL;
	}

	/*
	 * If the system is getting ready to restart it may have
	 * called efi_capsule_pending() to make decisions (such as
	 * whether to force an EFI reboot), and we're racing against
	 * that call. Abort in that case.
	 */
	if (unlikely(stop_capsules)) {
		pr_warn("Capsule update raced with reboot, aborting.\n");
		return -EINVAL;
	}

	sglist_phys = page_to_phys(sg_pages[0]);

	status = efi.update_capsule(&capsule, 1, sglist_phys);
	if (status == EFI_SUCCESS) {
		capsule_pending = true;
		efi_reset_type = reset;
	}

	return efi_status_to_err(status);
}

/**
 * efi_capsule_update - send a capsule to the firmware
 * @capsule: capsule to send to firmware
 * @pages: an array of capsule data pages
 *
 * Build a scatter gather list with EFI capsule block descriptors to
 * map the capsule described by @capsule with its data in @pages and
 * send it to the firmware via the UpdateCapsule() runtime service.
 *
 * @capsule must be a virtual mapping of the complete capsule update in the
 * kernel address space, as the capsule can be consumed immediately.
 * A capsule_header_t that describes the entire contents of the capsule
 * must be at the start of the first data page.
 *
 * Even though this function will validate that the firmware supports
 * the capsule guid, users will likely want to check that
 * efi_capsule_supported() returns true before calling this function
 * because it makes it easier to print helpful error messages.
 *
 * If the capsule is successfully submitted to the firmware, any
 * subsequent calls to efi_capsule_pending() will return true. @pages
 * must not be released or modified if this function returns
 * successfully.
 *
 * Callers must be prepared for this function to fail, which can
 * happen if we raced with system reboot or if there is already a
 * pending capsule that has a reset type that conflicts with the one
 * required by @capsule. Do NOT use efi_capsule_pending() to detect
 * this conflict since that would be racy. Instead, submit the capsule
 * to efi_capsule_update() and check the return value.
 *
 * Return 0 on success, a converted EFI status code on failure.
 */
int efi_capsule_update(efi_capsule_header_t *capsule, struct page **pages)
{
	u32 imagesize = capsule->imagesize;
	efi_guid_t guid = capsule->guid;
	unsigned int count, sg_count;
	u32 flags = capsule->flags;
	struct page **sg_pages;
	int rv, reset_type;
	int i, j;

	rv = efi_capsule_supported(guid, flags, imagesize, &reset_type);
	if (rv)
		return rv;

	count = DIV_ROUND_UP(imagesize, PAGE_SIZE);
	sg_count = sg_pages_num(count);

	sg_pages = kzalloc(sg_count * sizeof(*sg_pages), GFP_KERNEL);
	if (!sg_pages)
		return -ENOMEM;

	for (i = 0; i < sg_count; i++) {
		sg_pages[i] = alloc_page(GFP_KERNEL);
		if (!sg_pages[i]) {
			rv = -ENOMEM;
			goto out;
		}
	}

	for (i = 0; i < sg_count; i++) {
		efi_capsule_block_desc_t *sglist;

		sglist = kmap(sg_pages[i]);

		for (j = 0; j < SGLIST_PER_PAGE && count > 0; j++) {
			u64 sz = min_t(u64, imagesize, PAGE_SIZE);

			sglist[j].length = sz;
			sglist[j].data = page_to_phys(*pages++);

			imagesize -= sz;
			count--;
		}

		/* Continuation pointer */
		sglist[j].length = 0;

		if (i + 1 == sg_count)
			sglist[j].data = 0;
		else
			sglist[j].data = page_to_phys(sg_pages[i + 1]);

		kunmap(sg_pages[i]);
	}

	mutex_lock(&capsule_mutex);
	rv = efi_capsule_update_locked(capsule, sg_pages, reset_type);
	mutex_unlock(&capsule_mutex);

out:
	for (i = 0; rv && i < sg_count; i++) {
		if (sg_pages[i])
			__free_page(sg_pages[i]);
	}

	kfree(sg_pages);
	return rv;
}
EXPORT_SYMBOL_GPL(efi_capsule_update);

static int capsule_reboot_notify(struct notifier_block *nb, unsigned long event, void *cmd)
{
	mutex_lock(&capsule_mutex);
	stop_capsules = true;
	mutex_unlock(&capsule_mutex);

	return NOTIFY_DONE;
}

static struct notifier_block capsule_reboot_nb = {
	.notifier_call = capsule_reboot_notify,
};

static int __init capsule_reboot_register(void)
{
	return register_reboot_notifier(&capsule_reboot_nb);
}
core_initcall(capsule_reboot_register);
