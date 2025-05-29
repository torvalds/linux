// SPDX-License-Identifier: GPL-2.0-only
#include <linux/efi.h>

#include <asm/boot.h>
#include <asm/desc.h>
#include <asm/efi.h>

#include "efistub.h"
#include "x86-stub.h"

bool efi_no5lvl;

static void (*la57_toggle)(void *cr3);

static const struct desc_struct gdt[] = {
	[GDT_ENTRY_KERNEL32_CS] = GDT_ENTRY_INIT(DESC_CODE32, 0, 0xfffff),
	[GDT_ENTRY_KERNEL_CS]   = GDT_ENTRY_INIT(DESC_CODE64, 0, 0xfffff),
};

/*
 * Enabling (or disabling) 5 level paging is tricky, because it can only be
 * done from 32-bit mode with paging disabled. This means not only that the
 * code itself must be running from 32-bit addressable physical memory, but
 * also that the root page table must be 32-bit addressable, as programming
 * a 64-bit value into CR3 when running in 32-bit mode is not supported.
 */
efi_status_t efi_setup_5level_paging(void)
{
	u8 tmpl_size = (u8 *)&trampoline_ljmp_imm_offset - (u8 *)&trampoline_32bit_src;
	efi_status_t status;
	u8 *la57_code;

	if (!efi_is_64bit())
		return EFI_SUCCESS;

	/* check for 5 level paging support */
	if (native_cpuid_eax(0) < 7 ||
	    !(native_cpuid_ecx(7) & (1 << (X86_FEATURE_LA57 & 31))))
		return EFI_SUCCESS;

	/* allocate some 32-bit addressable memory for code and a page table */
	status = efi_allocate_pages(2 * PAGE_SIZE, (unsigned long *)&la57_code,
				    U32_MAX);
	if (status != EFI_SUCCESS)
		return status;

	la57_toggle = memcpy(la57_code, trampoline_32bit_src, tmpl_size);
	memset(la57_code + tmpl_size, 0x90, PAGE_SIZE - tmpl_size);

	/*
	 * To avoid the need to allocate a 32-bit addressable stack, the
	 * trampoline uses a LJMP instruction to switch back to long mode.
	 * LJMP takes an absolute destination address, which needs to be
	 * fixed up at runtime.
	 */
	*(u32 *)&la57_code[trampoline_ljmp_imm_offset] += (unsigned long)la57_code;

	efi_adjust_memory_range_protection((unsigned long)la57_toggle, PAGE_SIZE);

	return EFI_SUCCESS;
}

void efi_5level_switch(void)
{
	bool want_la57 = !efi_no5lvl;
	bool have_la57 = native_read_cr4() & X86_CR4_LA57;
	bool need_toggle = want_la57 ^ have_la57;
	u64 *pgt = (void *)la57_toggle + PAGE_SIZE;
	u64 *cr3 = (u64 *)__native_read_cr3();
	u64 *new_cr3;

	if (!la57_toggle || !need_toggle)
		return;

	if (!have_la57) {
		/*
		 * 5 level paging will be enabled, so a root level page needs
		 * to be allocated from the 32-bit addressable physical region,
		 * with its first entry referring to the existing hierarchy.
		 */
		new_cr3 = memset(pgt, 0, PAGE_SIZE);
		new_cr3[0] = (u64)cr3 | _PAGE_TABLE_NOENC;
	} else {
		/* take the new root table pointer from the current entry #0 */
		new_cr3 = (u64 *)(cr3[0] & PAGE_MASK);

		/* copy the new root table if it is not 32-bit addressable */
		if ((u64)new_cr3 > U32_MAX)
			new_cr3 = memcpy(pgt, new_cr3, PAGE_SIZE);
	}

	native_load_gdt(&(struct desc_ptr){ sizeof(gdt) - 1, (u64)gdt });

	la57_toggle(new_cr3);
}
