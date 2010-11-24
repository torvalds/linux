/*
 * Machine specific setup for xen
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pm.h>
#include <linux/memblock.h>

#include <asm/elf.h>
#include <asm/vdso.h>
#include <asm/e820.h>
#include <asm/setup.h>
#include <asm/acpi.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/interface/callback.h>
#include <xen/interface/memory.h>
#include <xen/interface/physdev.h>
#include <xen/interface/memory.h>
#include <xen/features.h>

#include "xen-ops.h"
#include "vdso.h"

/* These are code, but not functions.  Defined in entry.S */
extern const char xen_hypervisor_callback[];
extern const char xen_failsafe_callback[];
extern void xen_sysenter_target(void);
extern void xen_syscall_target(void);
extern void xen_syscall32_target(void);

/* Amount of extra memory space we add to the e820 ranges */
phys_addr_t xen_extra_mem_start, xen_extra_mem_size;

/* 
 * The maximum amount of extra memory compared to the base size.  The
 * main scaling factor is the size of struct page.  At extreme ratios
 * of base:extra, all the base memory can be filled with page
 * structures for the extra memory, leaving no space for anything
 * else.
 * 
 * 10x seems like a reasonable balance between scaling flexibility and
 * leaving a practically usable system.
 */
#define EXTRA_MEM_RATIO		(10)

static __init void xen_add_extra_mem(unsigned long pages)
{
	u64 size = (u64)pages * PAGE_SIZE;
	u64 extra_start = xen_extra_mem_start + xen_extra_mem_size;

	if (!pages)
		return;

	e820_add_region(extra_start, size, E820_RAM);
	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);

	memblock_x86_reserve_range(extra_start, extra_start + size, "XEN EXTRA");

	xen_extra_mem_size += size;

	xen_max_p2m_pfn = PFN_DOWN(extra_start + size);
}

static unsigned long __init xen_release_chunk(phys_addr_t start_addr,
					      phys_addr_t end_addr)
{
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};
	unsigned long start, end;
	unsigned long len = 0;
	unsigned long pfn;
	int ret;

	start = PFN_UP(start_addr);
	end = PFN_DOWN(end_addr);

	if (end <= start)
		return 0;

	printk(KERN_INFO "xen_release_chunk: looking at area pfn %lx-%lx: ",
	       start, end);
	for(pfn = start; pfn < end; pfn++) {
		unsigned long mfn = pfn_to_mfn(pfn);

		/* Make sure pfn exists to start with */
		if (mfn == INVALID_P2M_ENTRY || mfn_to_pfn(mfn) != pfn)
			continue;

		set_xen_guest_handle(reservation.extent_start, &mfn);
		reservation.nr_extents = 1;

		ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					   &reservation);
		WARN(ret != 1, "Failed to release memory %lx-%lx err=%d\n",
		     start, end, ret);
		if (ret == 1) {
			set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
			len++;
		}
	}
	printk(KERN_CONT "%ld pages freed\n", len);

	return len;
}

static unsigned long __init xen_return_unused_memory(unsigned long max_pfn,
						     const struct e820map *e820)
{
	phys_addr_t max_addr = PFN_PHYS(max_pfn);
	phys_addr_t last_end = ISA_END_ADDRESS;
	unsigned long released = 0;
	int i;

	/* Free any unused memory above the low 1Mbyte. */
	for (i = 0; i < e820->nr_map && last_end < max_addr; i++) {
		phys_addr_t end = e820->map[i].addr;
		end = min(max_addr, end);

		if (last_end < end)
			released += xen_release_chunk(last_end, end);
		last_end = max(last_end, e820->map[i].addr + e820->map[i].size);
	}

	if (last_end < max_addr)
		released += xen_release_chunk(last_end, max_addr);

	printk(KERN_INFO "released %ld pages of unused memory\n", released);
	return released;
}

/**
 * machine_specific_memory_setup - Hook for machine specific memory setup.
 **/
char * __init xen_memory_setup(void)
{
	static struct e820entry map[E820MAX] __initdata;

	unsigned long max_pfn = xen_start_info->nr_pages;
	unsigned long long mem_end;
	int rc;
	struct xen_memory_map memmap;
	unsigned long extra_pages = 0;
	unsigned long extra_limit;
	int i;
	int op;

	max_pfn = min(MAX_DOMAIN_PAGES, max_pfn);
	mem_end = PFN_PHYS(max_pfn);

	memmap.nr_entries = E820MAX;
	set_xen_guest_handle(memmap.buffer, map);

	op = xen_initial_domain() ?
		XENMEM_machine_memory_map :
		XENMEM_memory_map;
	rc = HYPERVISOR_memory_op(op, &memmap);
	if (rc == -ENOSYS) {
		BUG_ON(xen_initial_domain());
		memmap.nr_entries = 1;
		map[0].addr = 0ULL;
		map[0].size = mem_end;
		/* 8MB slack (to balance backend allocations). */
		map[0].size += 8ULL << 20;
		map[0].type = E820_RAM;
		rc = 0;
	}
	BUG_ON(rc);

	e820.nr_map = 0;
	xen_extra_mem_start = mem_end;
	for (i = 0; i < memmap.nr_entries; i++) {
		unsigned long long end = map[i].addr + map[i].size;

		if (map[i].type == E820_RAM) {
			if (map[i].addr < mem_end && end > mem_end) {
				/* Truncate region to max_mem. */
				u64 delta = end - mem_end;

				map[i].size -= delta;
				extra_pages += PFN_DOWN(delta);

				end = mem_end;
			}
		}

		if (end > xen_extra_mem_start)
			xen_extra_mem_start = end;

		/* If region is non-RAM or below mem_end, add what remains */
		if ((map[i].type != E820_RAM || map[i].addr < mem_end) &&
		    map[i].size > 0)
			e820_add_region(map[i].addr, map[i].size, map[i].type);
	}

	/*
	 * In domU, the ISA region is normal, usable memory, but we
	 * reserve ISA memory anyway because too many things poke
	 * about in there.
	 *
	 * In Dom0, the host E820 information can leave gaps in the
	 * ISA range, which would cause us to release those pages.  To
	 * avoid this, we unconditionally reserve them here.
	 */
	e820_add_region(ISA_START_ADDRESS, ISA_END_ADDRESS - ISA_START_ADDRESS,
			E820_RESERVED);

	/*
	 * Reserve Xen bits:
	 *  - mfn_list
	 *  - xen_start_info
	 * See comment above "struct start_info" in <xen/interface/xen.h>
	 */
	memblock_x86_reserve_range(__pa(xen_start_info->mfn_list),
		      __pa(xen_start_info->pt_base),
			"XEN START INFO");

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);

	extra_pages += xen_return_unused_memory(xen_start_info->nr_pages, &e820);

	/*
	 * Clamp the amount of extra memory to a EXTRA_MEM_RATIO
	 * factor the base size.  On non-highmem systems, the base
	 * size is the full initial memory allocation; on highmem it
	 * is limited to the max size of lowmem, so that it doesn't
	 * get completely filled.
	 *
	 * In principle there could be a problem in lowmem systems if
	 * the initial memory is also very large with respect to
	 * lowmem, but we won't try to deal with that here.
	 */
	extra_limit = min(EXTRA_MEM_RATIO * min(max_pfn, PFN_DOWN(MAXMEM)),
			  max_pfn + extra_pages);

	if (extra_limit >= max_pfn)
		extra_pages = extra_limit - max_pfn;
	else
		extra_pages = 0;

	xen_add_extra_mem(extra_pages);

	return "Xen";
}

static void xen_idle(void)
{
	local_irq_disable();

	if (need_resched())
		local_irq_enable();
	else {
		current_thread_info()->status &= ~TS_POLLING;
		smp_mb__after_clear_bit();
		safe_halt();
		current_thread_info()->status |= TS_POLLING;
	}
}

/*
 * Set the bit indicating "nosegneg" library variants should be used.
 * We only need to bother in pure 32-bit mode; compat 32-bit processes
 * can have un-truncated segments, so wrapping around is allowed.
 */
static void __init fiddle_vdso(void)
{
#ifdef CONFIG_X86_32
	u32 *mask;
	mask = VDSO32_SYMBOL(&vdso32_int80_start, NOTE_MASK);
	*mask |= 1 << VDSO_NOTE_NONEGSEG_BIT;
	mask = VDSO32_SYMBOL(&vdso32_sysenter_start, NOTE_MASK);
	*mask |= 1 << VDSO_NOTE_NONEGSEG_BIT;
#endif
}

static __cpuinit int register_callback(unsigned type, const void *func)
{
	struct callback_register callback = {
		.type = type,
		.address = XEN_CALLBACK(__KERNEL_CS, func),
		.flags = CALLBACKF_mask_events,
	};

	return HYPERVISOR_callback_op(CALLBACKOP_register, &callback);
}

void __cpuinit xen_enable_sysenter(void)
{
	int ret;
	unsigned sysenter_feature;

#ifdef CONFIG_X86_32
	sysenter_feature = X86_FEATURE_SEP;
#else
	sysenter_feature = X86_FEATURE_SYSENTER32;
#endif

	if (!boot_cpu_has(sysenter_feature))
		return;

	ret = register_callback(CALLBACKTYPE_sysenter, xen_sysenter_target);
	if(ret != 0)
		setup_clear_cpu_cap(sysenter_feature);
}

void __cpuinit xen_enable_syscall(void)
{
#ifdef CONFIG_X86_64
	int ret;

	ret = register_callback(CALLBACKTYPE_syscall, xen_syscall_target);
	if (ret != 0) {
		printk(KERN_ERR "Failed to set syscall callback: %d\n", ret);
		/* Pretty fatal; 64-bit userspace has no other
		   mechanism for syscalls. */
	}

	if (boot_cpu_has(X86_FEATURE_SYSCALL32)) {
		ret = register_callback(CALLBACKTYPE_syscall32,
					xen_syscall32_target);
		if (ret != 0)
			setup_clear_cpu_cap(X86_FEATURE_SYSCALL32);
	}
#endif /* CONFIG_X86_64 */
}

void __init xen_arch_setup(void)
{
	xen_panic_handler_init();

	HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_4gb_segments);
	HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_writable_pagetables);

	if (!xen_feature(XENFEAT_auto_translated_physmap))
		HYPERVISOR_vm_assist(VMASST_CMD_enable,
				     VMASST_TYPE_pae_extended_cr3);

	if (register_callback(CALLBACKTYPE_event, xen_hypervisor_callback) ||
	    register_callback(CALLBACKTYPE_failsafe, xen_failsafe_callback))
		BUG();

	xen_enable_sysenter();
	xen_enable_syscall();

#ifdef CONFIG_ACPI
	if (!(xen_start_info->flags & SIF_INITDOMAIN)) {
		printk(KERN_INFO "ACPI in unprivileged domain disabled\n");
		disable_acpi();
	}
#endif

	memcpy(boot_command_line, xen_start_info->cmd_line,
	       MAX_GUEST_CMDLINE > COMMAND_LINE_SIZE ?
	       COMMAND_LINE_SIZE : MAX_GUEST_CMDLINE);

	pm_idle = xen_idle;

	fiddle_vdso();
}
