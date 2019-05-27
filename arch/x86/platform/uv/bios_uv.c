// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BIOS run time interface routines.
 *
 *  Copyright (c) 2008-2009 Silicon Graphics, Inc.  All Rights Reserved.
 *  Copyright (c) Russ Anderson <rja@sgi.com>
 */

#include <linux/efi.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <asm/efi.h>
#include <linux/io.h>
#include <asm/uv/bios.h>
#include <asm/uv/uv_hub.h>

struct uv_systab *uv_systab;

static s64 __uv_bios_call(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3,
			u64 a4, u64 a5)
{
	struct uv_systab *tab = uv_systab;
	s64 ret;

	if (!tab || !tab->function)
		/*
		 * BIOS does not support UV systab
		 */
		return BIOS_STATUS_UNIMPLEMENTED;

	/*
	 * If EFI_OLD_MEMMAP is set, we need to fall back to using our old EFI
	 * callback method, which uses efi_call() directly, with the kernel page tables:
	 */
	if (unlikely(efi_enabled(EFI_OLD_MEMMAP)))
		ret = efi_call((void *)__va(tab->function), (u64)which, a1, a2, a3, a4, a5);
	else
		ret = efi_call_virt_pointer(tab, function, (u64)which, a1, a2, a3, a4, a5);

	return ret;
}

s64 uv_bios_call(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
	s64 ret;

	if (down_interruptible(&__efi_uv_runtime_lock))
		return BIOS_STATUS_ABORT;

	ret = __uv_bios_call(which, a1, a2, a3, a4, a5);
	up(&__efi_uv_runtime_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(uv_bios_call);

s64 uv_bios_call_irqsave(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3,
					u64 a4, u64 a5)
{
	unsigned long bios_flags;
	s64 ret;

	if (down_interruptible(&__efi_uv_runtime_lock))
		return BIOS_STATUS_ABORT;

	local_irq_save(bios_flags);
	ret = __uv_bios_call(which, a1, a2, a3, a4, a5);
	local_irq_restore(bios_flags);

	up(&__efi_uv_runtime_lock);

	return ret;
}


long sn_partition_id;
EXPORT_SYMBOL_GPL(sn_partition_id);
long sn_coherency_id;
EXPORT_SYMBOL_GPL(sn_coherency_id);
long sn_region_size;
EXPORT_SYMBOL_GPL(sn_region_size);
long system_serial_number;
EXPORT_SYMBOL_GPL(system_serial_number);
int uv_type;
EXPORT_SYMBOL_GPL(uv_type);


s64 uv_bios_get_sn_info(int fc, int *uvtype, long *partid, long *coher,
		long *region, long *ssn)
{
	s64 ret;
	u64 v0, v1;
	union partition_info_u part;

	ret = uv_bios_call_irqsave(UV_BIOS_GET_SN_INFO, fc,
				(u64)(&v0), (u64)(&v1), 0, 0);
	if (ret != BIOS_STATUS_SUCCESS)
		return ret;

	part.val = v0;
	if (uvtype)
		*uvtype = part.hub_version;
	if (partid)
		*partid = part.partition_id;
	if (coher)
		*coher = part.coherence_id;
	if (region)
		*region = part.region_size;
	if (ssn)
		*ssn = v1;
	return ret;
}
EXPORT_SYMBOL_GPL(uv_bios_get_sn_info);

int
uv_bios_mq_watchlist_alloc(unsigned long addr, unsigned int mq_size,
			   unsigned long *intr_mmr_offset)
{
	u64 watchlist;
	s64 ret;

	/*
	 * bios returns watchlist number or negative error number.
	 */
	ret = (int)uv_bios_call_irqsave(UV_BIOS_WATCHLIST_ALLOC, addr,
			mq_size, (u64)intr_mmr_offset,
			(u64)&watchlist, 0);
	if (ret < BIOS_STATUS_SUCCESS)
		return ret;

	return watchlist;
}
EXPORT_SYMBOL_GPL(uv_bios_mq_watchlist_alloc);

int
uv_bios_mq_watchlist_free(int blade, int watchlist_num)
{
	return (int)uv_bios_call_irqsave(UV_BIOS_WATCHLIST_FREE,
				blade, watchlist_num, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_mq_watchlist_free);

s64
uv_bios_change_memprotect(u64 paddr, u64 len, enum uv_memprotect perms)
{
	return uv_bios_call_irqsave(UV_BIOS_MEMPROTECT, paddr, len,
					perms, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_change_memprotect);

s64
uv_bios_reserved_page_pa(u64 buf, u64 *cookie, u64 *addr, u64 *len)
{
	return uv_bios_call_irqsave(UV_BIOS_GET_PARTITION_ADDR, (u64)cookie,
				    (u64)addr, buf, (u64)len, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_reserved_page_pa);

s64 uv_bios_freq_base(u64 clock_type, u64 *ticks_per_second)
{
	return uv_bios_call(UV_BIOS_FREQ_BASE, clock_type,
			   (u64)ticks_per_second, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_freq_base);

/*
 * uv_bios_set_legacy_vga_target - Set Legacy VGA I/O Target
 * @decode: true to enable target, false to disable target
 * @domain: PCI domain number
 * @bus: PCI bus number
 *
 * Returns:
 *    0: Success
 *    -EINVAL: Invalid domain or bus number
 *    -ENOSYS: Capability not available
 *    -EBUSY: Legacy VGA I/O cannot be retargeted at this time
 */
int uv_bios_set_legacy_vga_target(bool decode, int domain, int bus)
{
	return uv_bios_call(UV_BIOS_SET_LEGACY_VGA_TARGET,
				(u64)decode, (u64)domain, (u64)bus, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_set_legacy_vga_target);

void uv_bios_init(void)
{
	uv_systab = NULL;
	if ((efi.uv_systab == EFI_INVALID_TABLE_ADDR) ||
	    !efi.uv_systab || efi_runtime_disabled()) {
		pr_crit("UV: UVsystab: missing\n");
		return;
	}

	uv_systab = ioremap(efi.uv_systab, sizeof(struct uv_systab));
	if (!uv_systab || strncmp(uv_systab->signature, UV_SYSTAB_SIG, 4)) {
		pr_err("UV: UVsystab: bad signature!\n");
		iounmap(uv_systab);
		return;
	}

	/* Starting with UV4 the UV systab size is variable */
	if (uv_systab->revision >= UV_SYSTAB_VERSION_UV4) {
		int size = uv_systab->size;

		iounmap(uv_systab);
		uv_systab = ioremap(efi.uv_systab, size);
		if (!uv_systab) {
			pr_err("UV: UVsystab: ioremap(%d) failed!\n", size);
			return;
		}
	}
	pr_info("UV: UVsystab: Revision:%x\n", uv_systab->revision);
}
