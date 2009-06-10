/*
 * BIOS run time interface routines.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *  Copyright (c) Russ Anderson
 */

#include <linux/efi.h>
#include <asm/efi.h>
#include <linux/io.h>
#include <asm/uv/bios.h>
#include <asm/uv/uv_hub.h>

static struct uv_systab uv_systab;

s64 uv_bios_call(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
	struct uv_systab *tab = &uv_systab;

	if (!tab->function)
		/*
		 * BIOS does not support UV systab
		 */
		return BIOS_STATUS_UNIMPLEMENTED;

	return efi_call6((void *)__va(tab->function),
					(u64)which, a1, a2, a3, a4, a5);
}

s64 uv_bios_call_irqsave(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3,
					u64 a4, u64 a5)
{
	unsigned long bios_flags;
	s64 ret;

	local_irq_save(bios_flags);
	ret = uv_bios_call(which, a1, a2, a3, a4, a5);
	local_irq_restore(bios_flags);

	return ret;
}

s64 uv_bios_call_reentrant(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3,
					u64 a4, u64 a5)
{
	s64 ret;

	preempt_disable();
	ret = uv_bios_call(which, a1, a2, a3, a4, a5);
	preempt_enable();

	return ret;
}


long sn_partition_id;
EXPORT_SYMBOL_GPL(sn_partition_id);
long sn_coherency_id;
EXPORT_SYMBOL_GPL(sn_coherency_id);
long sn_region_size;
EXPORT_SYMBOL_GPL(sn_region_size);
int uv_type;


s64 uv_bios_get_sn_info(int fc, int *uvtype, long *partid, long *coher,
		long *region)
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
	return ret;
}

int
uv_bios_mq_watchlist_alloc(int blade, unsigned long addr, unsigned int mq_size,
			   unsigned long *intr_mmr_offset)
{
	union uv_watchlist_u size_blade;
	u64 watchlist;
	s64 ret;

	size_blade.size = mq_size;
	size_blade.blade = blade;

	/*
	 * bios returns watchlist number or negative error number.
	 */
	ret = (int)uv_bios_call_irqsave(UV_BIOS_WATCHLIST_ALLOC, addr,
			size_blade.val, (u64)intr_mmr_offset,
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
	s64 ret;

	ret = uv_bios_call_irqsave(UV_BIOS_GET_PARTITION_ADDR, (u64)cookie,
					(u64)addr, buf, (u64)len, 0);
	return ret;
}
EXPORT_SYMBOL_GPL(uv_bios_reserved_page_pa);

s64 uv_bios_freq_base(u64 clock_type, u64 *ticks_per_second)
{
	return uv_bios_call(UV_BIOS_FREQ_BASE, clock_type,
			   (u64)ticks_per_second, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_freq_base);


#ifdef CONFIG_EFI
void uv_bios_init(void)
{
	struct uv_systab *tab;

	if ((efi.uv_systab == EFI_INVALID_TABLE_ADDR) ||
	    (efi.uv_systab == (unsigned long)NULL)) {
		printk(KERN_CRIT "No EFI UV System Table.\n");
		uv_systab.function = (unsigned long)NULL;
		return;
	}

	tab = (struct uv_systab *)ioremap(efi.uv_systab,
					sizeof(struct uv_systab));
	if (strncmp(tab->signature, "UVST", 4) != 0)
		printk(KERN_ERR "bad signature in UV system table!");

	/*
	 * Copy table to permanent spot for later use.
	 */
	memcpy(&uv_systab, tab, sizeof(struct uv_systab));
	iounmap(tab);

	printk(KERN_INFO "EFI UV System Table Revision %d\n",
					uv_systab.revision);
}
#else	/* !CONFIG_EFI */

void uv_bios_init(void) { }
#endif

