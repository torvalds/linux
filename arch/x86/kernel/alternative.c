#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/kprobes.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/slab.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/mce.h>
#include <asm/nmi.h>
#include <asm/vsyscall.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/fixmap.h>

#define MAX_PATCH_LEN (255-1)

#ifdef CONFIG_HOTPLUG_CPU
static int smp_alt_once;

static int __init bootonly(char *str)
{
	smp_alt_once = 1;
	return 1;
}
__setup("smp-alt-boot", bootonly);
#else
#define smp_alt_once 1
#endif

static int __initdata_or_module debug_alternative;

static int __init debug_alt(char *str)
{
	debug_alternative = 1;
	return 1;
}
__setup("debug-alternative", debug_alt);

static int noreplace_smp;

static int __init setup_noreplace_smp(char *str)
{
	noreplace_smp = 1;
	return 1;
}
__setup("noreplace-smp", setup_noreplace_smp);

#ifdef CONFIG_PARAVIRT
static int __initdata_or_module noreplace_paravirt = 0;

static int __init setup_noreplace_paravirt(char *str)
{
	noreplace_paravirt = 1;
	return 1;
}
__setup("noreplace-paravirt", setup_noreplace_paravirt);
#endif

#define DPRINTK(fmt, args...) if (debug_alternative) \
	printk(KERN_DEBUG fmt, args)

#if defined(GENERIC_NOP1) && !defined(CONFIG_X86_64)
/* Use inline assembly to define this because the nops are defined
   as inline assembly strings in the include files and we cannot
   get them easily into strings. */
asm("\t" __stringify(__INITRODATA_OR_MODULE) "\nintelnops: "
	GENERIC_NOP1 GENERIC_NOP2 GENERIC_NOP3 GENERIC_NOP4 GENERIC_NOP5 GENERIC_NOP6
	GENERIC_NOP7 GENERIC_NOP8
    "\t.previous");
extern const unsigned char intelnops[];
static const unsigned char *const __initconst_or_module
intel_nops[ASM_NOP_MAX+1] = {
	NULL,
	intelnops,
	intelnops + 1,
	intelnops + 1 + 2,
	intelnops + 1 + 2 + 3,
	intelnops + 1 + 2 + 3 + 4,
	intelnops + 1 + 2 + 3 + 4 + 5,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
};
#endif

#ifdef K8_NOP1
asm("\t" __stringify(__INITRODATA_OR_MODULE) "\nk8nops: "
	K8_NOP1 K8_NOP2 K8_NOP3 K8_NOP4 K8_NOP5 K8_NOP6
	K8_NOP7 K8_NOP8
    "\t.previous");
extern const unsigned char k8nops[];
static const unsigned char *const __initconst_or_module
k8_nops[ASM_NOP_MAX+1] = {
	NULL,
	k8nops,
	k8nops + 1,
	k8nops + 1 + 2,
	k8nops + 1 + 2 + 3,
	k8nops + 1 + 2 + 3 + 4,
	k8nops + 1 + 2 + 3 + 4 + 5,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
};
#endif

#if defined(K7_NOP1) && !defined(CONFIG_X86_64)
asm("\t" __stringify(__INITRODATA_OR_MODULE) "\nk7nops: "
	K7_NOP1 K7_NOP2 K7_NOP3 K7_NOP4 K7_NOP5 K7_NOP6
	K7_NOP7 K7_NOP8
    "\t.previous");
extern const unsigned char k7nops[];
static const unsigned char *const __initconst_or_module
k7_nops[ASM_NOP_MAX+1] = {
	NULL,
	k7nops,
	k7nops + 1,
	k7nops + 1 + 2,
	k7nops + 1 + 2 + 3,
	k7nops + 1 + 2 + 3 + 4,
	k7nops + 1 + 2 + 3 + 4 + 5,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
};
#endif

#ifdef P6_NOP1
asm("\t" __stringify(__INITRODATA_OR_MODULE) "\np6nops: "
	P6_NOP1 P6_NOP2 P6_NOP3 P6_NOP4 P6_NOP5 P6_NOP6
	P6_NOP7 P6_NOP8
    "\t.previous");
extern const unsigned char p6nops[];
static const unsigned char *const __initconst_or_module
p6_nops[ASM_NOP_MAX+1] = {
	NULL,
	p6nops,
	p6nops + 1,
	p6nops + 1 + 2,
	p6nops + 1 + 2 + 3,
	p6nops + 1 + 2 + 3 + 4,
	p6nops + 1 + 2 + 3 + 4 + 5,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
};
#endif

#ifdef CONFIG_X86_64

extern char __vsyscall_0;
static const unsigned char *const *__init_or_module find_nop_table(void)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_has(X86_FEATURE_NOPL))
		return p6_nops;
	else
		return k8_nops;
}

#else /* CONFIG_X86_64 */

static const unsigned char *const *__init_or_module find_nop_table(void)
{
	if (boot_cpu_has(X86_FEATURE_K8))
		return k8_nops;
	else if (boot_cpu_has(X86_FEATURE_K7))
		return k7_nops;
	else if (boot_cpu_has(X86_FEATURE_NOPL))
		return p6_nops;
	else
		return intel_nops;
}

#endif /* CONFIG_X86_64 */

/* Use this to add nops to a buffer, then text_poke the whole buffer. */
static void __init_or_module add_nops(void *insns, unsigned int len)
{
	const unsigned char *const *noptable = find_nop_table();

	while (len > 0) {
		unsigned int noplen = len;
		if (noplen > ASM_NOP_MAX)
			noplen = ASM_NOP_MAX;
		memcpy(insns, noptable[noplen], noplen);
		insns += noplen;
		len -= noplen;
	}
}

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
extern s32 __smp_locks[], __smp_locks_end[];
void *text_poke_early(void *addr, const void *opcode, size_t len);

/* Replace instructions with better alternatives for this CPU type.
   This runs before SMP is initialized to avoid SMP problems with
   self modifying code. This implies that assymetric systems where
   APs have less capabilities than the boot processor are not handled.
   Tough. Make sure you disable such features by hand. */

void __init_or_module apply_alternatives(struct alt_instr *start,
					 struct alt_instr *end)
{
	struct alt_instr *a;
	u8 insnbuf[MAX_PATCH_LEN];

	DPRINTK("%s: alt table %p -> %p\n", __func__, start, end);
	for (a = start; a < end; a++) {
		u8 *instr = a->instr;
		BUG_ON(a->replacementlen > a->instrlen);
		BUG_ON(a->instrlen > sizeof(insnbuf));
		BUG_ON(a->cpuid >= NCAPINTS*32);
		if (!boot_cpu_has(a->cpuid))
			continue;
#ifdef CONFIG_X86_64
		/* vsyscall code is not mapped yet. resolve it manually. */
		if (instr >= (u8 *)VSYSCALL_START && instr < (u8*)VSYSCALL_END) {
			instr = __va(instr - (u8*)VSYSCALL_START + (u8*)__pa_symbol(&__vsyscall_0));
			DPRINTK("%s: vsyscall fixup: %p => %p\n",
				__func__, a->instr, instr);
		}
#endif
		memcpy(insnbuf, a->replacement, a->replacementlen);
		if (*insnbuf == 0xe8 && a->replacementlen == 5)
		    *(s32 *)(insnbuf + 1) += a->replacement - a->instr;
		add_nops(insnbuf + a->replacementlen,
			 a->instrlen - a->replacementlen);
		text_poke_early(instr, insnbuf, a->instrlen);
	}
}

#ifdef CONFIG_SMP

static void alternatives_smp_lock(const s32 *start, const s32 *end,
				  u8 *text, u8 *text_end)
{
	const s32 *poff;

	mutex_lock(&text_mutex);
	for (poff = start; poff < end; poff++) {
		u8 *ptr = (u8 *)poff + *poff;

		if (!*poff || ptr < text || ptr >= text_end)
			continue;
		/* turn DS segment override prefix into lock prefix */
		if (*ptr == 0x3e)
			text_poke(ptr, ((unsigned char []){0xf0}), 1);
	};
	mutex_unlock(&text_mutex);
}

static void alternatives_smp_unlock(const s32 *start, const s32 *end,
				    u8 *text, u8 *text_end)
{
	const s32 *poff;

	if (noreplace_smp)
		return;

	mutex_lock(&text_mutex);
	for (poff = start; poff < end; poff++) {
		u8 *ptr = (u8 *)poff + *poff;

		if (!*poff || ptr < text || ptr >= text_end)
			continue;
		/* turn lock prefix into DS segment override prefix */
		if (*ptr == 0xf0)
			text_poke(ptr, ((unsigned char []){0x3E}), 1);
	};
	mutex_unlock(&text_mutex);
}

struct smp_alt_module {
	/* what is this ??? */
	struct module	*mod;
	char		*name;

	/* ptrs to lock prefixes */
	const s32	*locks;
	const s32	*locks_end;

	/* .text segment, needed to avoid patching init code ;) */
	u8		*text;
	u8		*text_end;

	struct list_head next;
};
static LIST_HEAD(smp_alt_modules);
static DEFINE_MUTEX(smp_alt);
static int smp_mode = 1;	/* protected by smp_alt */

void __init_or_module alternatives_smp_module_add(struct module *mod,
						  char *name,
						  void *locks, void *locks_end,
						  void *text,  void *text_end)
{
	struct smp_alt_module *smp;

	if (noreplace_smp)
		return;

	if (smp_alt_once) {
		if (boot_cpu_has(X86_FEATURE_UP))
			alternatives_smp_unlock(locks, locks_end,
						text, text_end);
		return;
	}

	smp = kzalloc(sizeof(*smp), GFP_KERNEL);
	if (NULL == smp)
		return; /* we'll run the (safe but slow) SMP code then ... */

	smp->mod	= mod;
	smp->name	= name;
	smp->locks	= locks;
	smp->locks_end	= locks_end;
	smp->text	= text;
	smp->text_end	= text_end;
	DPRINTK("%s: locks %p -> %p, text %p -> %p, name %s\n",
		__func__, smp->locks, smp->locks_end,
		smp->text, smp->text_end, smp->name);

	mutex_lock(&smp_alt);
	list_add_tail(&smp->next, &smp_alt_modules);
	if (boot_cpu_has(X86_FEATURE_UP))
		alternatives_smp_unlock(smp->locks, smp->locks_end,
					smp->text, smp->text_end);
	mutex_unlock(&smp_alt);
}

void __init_or_module alternatives_smp_module_del(struct module *mod)
{
	struct smp_alt_module *item;

	if (smp_alt_once || noreplace_smp)
		return;

	mutex_lock(&smp_alt);
	list_for_each_entry(item, &smp_alt_modules, next) {
		if (mod != item->mod)
			continue;
		list_del(&item->next);
		mutex_unlock(&smp_alt);
		DPRINTK("%s: %s\n", __func__, item->name);
		kfree(item);
		return;
	}
	mutex_unlock(&smp_alt);
}

void alternatives_smp_switch(int smp)
{
	struct smp_alt_module *mod;

#ifdef CONFIG_LOCKDEP
	/*
	 * Older binutils section handling bug prevented
	 * alternatives-replacement from working reliably.
	 *
	 * If this still occurs then you should see a hang
	 * or crash shortly after this line:
	 */
	printk("lockdep: fixing up alternatives.\n");
#endif

	if (noreplace_smp || smp_alt_once)
		return;
	BUG_ON(!smp && (num_online_cpus() > 1));

	mutex_lock(&smp_alt);

	/*
	 * Avoid unnecessary switches because it forces JIT based VMs to
	 * throw away all cached translations, which can be quite costly.
	 */
	if (smp == smp_mode) {
		/* nothing */
	} else if (smp) {
		printk(KERN_INFO "SMP alternatives: switching to SMP code\n");
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_UP);
		clear_cpu_cap(&cpu_data(0), X86_FEATURE_UP);
		list_for_each_entry(mod, &smp_alt_modules, next)
			alternatives_smp_lock(mod->locks, mod->locks_end,
					      mod->text, mod->text_end);
	} else {
		printk(KERN_INFO "SMP alternatives: switching to UP code\n");
		set_cpu_cap(&boot_cpu_data, X86_FEATURE_UP);
		set_cpu_cap(&cpu_data(0), X86_FEATURE_UP);
		list_for_each_entry(mod, &smp_alt_modules, next)
			alternatives_smp_unlock(mod->locks, mod->locks_end,
						mod->text, mod->text_end);
	}
	smp_mode = smp;
	mutex_unlock(&smp_alt);
}

/* Return 1 if the address range is reserved for smp-alternatives */
int alternatives_text_reserved(void *start, void *end)
{
	struct smp_alt_module *mod;
	const s32 *poff;
	u8 *text_start = start;
	u8 *text_end = end;

	list_for_each_entry(mod, &smp_alt_modules, next) {
		if (mod->text > text_end || mod->text_end < text_start)
			continue;
		for (poff = mod->locks; poff < mod->locks_end; poff++) {
			const u8 *ptr = (const u8 *)poff + *poff;

			if (text_start <= ptr && text_end > ptr)
				return 1;
		}
	}

	return 0;
}
#endif

#ifdef CONFIG_PARAVIRT
void __init_or_module apply_paravirt(struct paravirt_patch_site *start,
				     struct paravirt_patch_site *end)
{
	struct paravirt_patch_site *p;
	char insnbuf[MAX_PATCH_LEN];

	if (noreplace_paravirt)
		return;

	for (p = start; p < end; p++) {
		unsigned int used;

		BUG_ON(p->len > MAX_PATCH_LEN);
		/* prep the buffer with the original instructions */
		memcpy(insnbuf, p->instr, p->len);
		used = pv_init_ops.patch(p->instrtype, p->clobbers, insnbuf,
					 (unsigned long)p->instr, p->len);

		BUG_ON(used > p->len);

		/* Pad the rest with nops */
		add_nops(insnbuf + used, p->len - used);
		text_poke_early(p->instr, insnbuf, p->len);
	}
}
extern struct paravirt_patch_site __start_parainstructions[],
	__stop_parainstructions[];
#endif	/* CONFIG_PARAVIRT */

void __init alternative_instructions(void)
{
	/* The patching is not fully atomic, so try to avoid local interruptions
	   that might execute the to be patched code.
	   Other CPUs are not running. */
	stop_nmi();

	/*
	 * Don't stop machine check exceptions while patching.
	 * MCEs only happen when something got corrupted and in this
	 * case we must do something about the corruption.
	 * Ignoring it is worse than a unlikely patching race.
	 * Also machine checks tend to be broadcast and if one CPU
	 * goes into machine check the others follow quickly, so we don't
	 * expect a machine check to cause undue problems during to code
	 * patching.
	 */

	apply_alternatives(__alt_instructions, __alt_instructions_end);

	/* switch to patch-once-at-boottime-only mode and free the
	 * tables in case we know the number of CPUs will never ever
	 * change */
#ifdef CONFIG_HOTPLUG_CPU
	if (num_possible_cpus() < 2)
		smp_alt_once = 1;
#endif

#ifdef CONFIG_SMP
	if (smp_alt_once) {
		if (1 == num_possible_cpus()) {
			printk(KERN_INFO "SMP alternatives: switching to UP code\n");
			set_cpu_cap(&boot_cpu_data, X86_FEATURE_UP);
			set_cpu_cap(&cpu_data(0), X86_FEATURE_UP);

			alternatives_smp_unlock(__smp_locks, __smp_locks_end,
						_text, _etext);
		}
	} else {
		alternatives_smp_module_add(NULL, "core kernel",
					    __smp_locks, __smp_locks_end,
					    _text, _etext);

		/* Only switch to UP mode if we don't immediately boot others */
		if (num_present_cpus() == 1 || setup_max_cpus <= 1)
			alternatives_smp_switch(0);
	}
#endif
 	apply_paravirt(__parainstructions, __parainstructions_end);

	if (smp_alt_once)
		free_init_pages("SMP alternatives",
				(unsigned long)__smp_locks,
				(unsigned long)__smp_locks_end);

	restart_nmi();
}

/**
 * text_poke_early - Update instructions on a live kernel at boot time
 * @addr: address to modify
 * @opcode: source of the copy
 * @len: length to copy
 *
 * When you use this code to patch more than one byte of an instruction
 * you need to make sure that other CPUs cannot execute this code in parallel.
 * Also no thread must be currently preempted in the middle of these
 * instructions. And on the local CPU you need to be protected again NMI or MCE
 * handlers seeing an inconsistent instruction while you patch.
 */
void *__init_or_module text_poke_early(void *addr, const void *opcode,
					      size_t len)
{
	unsigned long flags;
	local_irq_save(flags);
	memcpy(addr, opcode, len);
	sync_core();
	local_irq_restore(flags);
	/* Could also do a CLFLUSH here to speed up CPU recovery; but
	   that causes hangs on some VIA CPUs. */
	return addr;
}

/**
 * text_poke - Update instructions on a live kernel
 * @addr: address to modify
 * @opcode: source of the copy
 * @len: length to copy
 *
 * Only atomic text poke/set should be allowed when not doing early patching.
 * It means the size must be writable atomically and the address must be aligned
 * in a way that permits an atomic write. It also makes sure we fit on a single
 * page.
 *
 * Note: Must be called under text_mutex.
 */
void *__kprobes text_poke(void *addr, const void *opcode, size_t len)
{
	unsigned long flags;
	char *vaddr;
	struct page *pages[2];
	int i;

	if (!core_kernel_text((unsigned long)addr)) {
		pages[0] = vmalloc_to_page(addr);
		pages[1] = vmalloc_to_page(addr + PAGE_SIZE);
	} else {
		pages[0] = virt_to_page(addr);
		WARN_ON(!PageReserved(pages[0]));
		pages[1] = virt_to_page(addr + PAGE_SIZE);
	}
	BUG_ON(!pages[0]);
	local_irq_save(flags);
	set_fixmap(FIX_TEXT_POKE0, page_to_phys(pages[0]));
	if (pages[1])
		set_fixmap(FIX_TEXT_POKE1, page_to_phys(pages[1]));
	vaddr = (char *)fix_to_virt(FIX_TEXT_POKE0);
	memcpy(&vaddr[(unsigned long)addr & ~PAGE_MASK], opcode, len);
	clear_fixmap(FIX_TEXT_POKE0);
	if (pages[1])
		clear_fixmap(FIX_TEXT_POKE1);
	local_flush_tlb();
	sync_core();
	/* Could also do a CLFLUSH here to speed up CPU recovery; but
	   that causes hangs on some VIA CPUs. */
	for (i = 0; i < len; i++)
		BUG_ON(((char *)addr)[i] != ((char *)opcode)[i]);
	local_irq_restore(flags);
	return addr;
}

/*
 * Cross-modifying kernel text with stop_machine().
 * This code originally comes from immediate value.
 */
static atomic_t stop_machine_first;
static int wrote_text;

struct text_poke_params {
	void *addr;
	const void *opcode;
	size_t len;
};

static int __kprobes stop_machine_text_poke(void *data)
{
	struct text_poke_params *tpp = data;

	if (atomic_dec_and_test(&stop_machine_first)) {
		text_poke(tpp->addr, tpp->opcode, tpp->len);
		smp_wmb();	/* Make sure other cpus see that this has run */
		wrote_text = 1;
	} else {
		while (!wrote_text)
			cpu_relax();
		smp_mb();	/* Load wrote_text before following execution */
	}

	flush_icache_range((unsigned long)tpp->addr,
			   (unsigned long)tpp->addr + tpp->len);
	return 0;
}

/**
 * text_poke_smp - Update instructions on a live kernel on SMP
 * @addr: address to modify
 * @opcode: source of the copy
 * @len: length to copy
 *
 * Modify multi-byte instruction by using stop_machine() on SMP. This allows
 * user to poke/set multi-byte text on SMP. Only non-NMI/MCE code modifying
 * should be allowed, since stop_machine() does _not_ protect code against
 * NMI and MCE.
 *
 * Note: Must be called under get_online_cpus() and text_mutex.
 */
void *__kprobes text_poke_smp(void *addr, const void *opcode, size_t len)
{
	struct text_poke_params tpp;

	tpp.addr = addr;
	tpp.opcode = opcode;
	tpp.len = len;
	atomic_set(&stop_machine_first, 1);
	wrote_text = 0;
	/* Use __stop_machine() because the caller already got online_cpus. */
	__stop_machine(stop_machine_text_poke, (void *)&tpp, NULL);
	return addr;
}

#if defined(CONFIG_DYNAMIC_FTRACE) || defined(HAVE_JUMP_LABEL)

unsigned char ideal_nop5[IDEAL_NOP_SIZE_5];

void __init arch_init_ideal_nop5(void)
{
	extern const unsigned char ftrace_test_p6nop[];
	extern const unsigned char ftrace_test_nop5[];
	extern const unsigned char ftrace_test_jmp[];
	int faulted = 0;

	/*
	 * There is no good nop for all x86 archs.
	 * We will default to using the P6_NOP5, but first we
	 * will test to make sure that the nop will actually
	 * work on this CPU. If it faults, we will then
	 * go to a lesser efficient 5 byte nop. If that fails
	 * we then just use a jmp as our nop. This isn't the most
	 * efficient nop, but we can not use a multi part nop
	 * since we would then risk being preempted in the middle
	 * of that nop, and if we enabled tracing then, it might
	 * cause a system crash.
	 *
	 * TODO: check the cpuid to determine the best nop.
	 */
	asm volatile (
		"ftrace_test_jmp:"
		"jmp ftrace_test_p6nop\n"
		"nop\n"
		"nop\n"
		"nop\n"  /* 2 byte jmp + 3 bytes */
		"ftrace_test_p6nop:"
		P6_NOP5
		"jmp 1f\n"
		"ftrace_test_nop5:"
		".byte 0x66,0x66,0x66,0x66,0x90\n"
		"1:"
		".section .fixup, \"ax\"\n"
		"2:	movl $1, %0\n"
		"	jmp ftrace_test_nop5\n"
		"3:	movl $2, %0\n"
		"	jmp 1b\n"
		".previous\n"
		_ASM_EXTABLE(ftrace_test_p6nop, 2b)
		_ASM_EXTABLE(ftrace_test_nop5, 3b)
		: "=r"(faulted) : "0" (faulted));

	switch (faulted) {
	case 0:
		pr_info("converting mcount calls to 0f 1f 44 00 00\n");
		memcpy(ideal_nop5, ftrace_test_p6nop, IDEAL_NOP_SIZE_5);
		break;
	case 1:
		pr_info("converting mcount calls to 66 66 66 66 90\n");
		memcpy(ideal_nop5, ftrace_test_nop5, IDEAL_NOP_SIZE_5);
		break;
	case 2:
		pr_info("converting mcount calls to jmp . + 5\n");
		memcpy(ideal_nop5, ftrace_test_jmp, IDEAL_NOP_SIZE_5);
		break;
	}

}
#endif
