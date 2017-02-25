#define pr_fmt(fmt) "SMP alternatives: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/mce.h>
#include <asm/nmi.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/fixmap.h>

int __read_mostly alternatives_patched;

EXPORT_SYMBOL_GPL(alternatives_patched);

#define MAX_PATCH_LEN (255-1)

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

#define DPRINTK(fmt, args...)						\
do {									\
	if (debug_alternative)						\
		printk(KERN_DEBUG "%s: " fmt "\n", __func__, ##args);	\
} while (0)

#define DUMP_BYTES(buf, len, fmt, args...)				\
do {									\
	if (unlikely(debug_alternative)) {				\
		int j;							\
									\
		if (!(len))						\
			break;						\
									\
		printk(KERN_DEBUG fmt, ##args);				\
		for (j = 0; j < (len) - 1; j++)				\
			printk(KERN_CONT "%02hhx ", buf[j]);		\
		printk(KERN_CONT "%02hhx\n", buf[j]);			\
	}								\
} while (0)

/*
 * Each GENERIC_NOPX is of X bytes, and defined as an array of bytes
 * that correspond to that nop. Getting from one nop to the next, we
 * add to the array the offset that is equal to the sum of all sizes of
 * nops preceding the one we are after.
 *
 * Note: The GENERIC_NOP5_ATOMIC is at the end, as it breaks the
 * nice symmetry of sizes of the previous nops.
 */
#if defined(GENERIC_NOP1) && !defined(CONFIG_X86_64)
static const unsigned char intelnops[] =
{
	GENERIC_NOP1,
	GENERIC_NOP2,
	GENERIC_NOP3,
	GENERIC_NOP4,
	GENERIC_NOP5,
	GENERIC_NOP6,
	GENERIC_NOP7,
	GENERIC_NOP8,
	GENERIC_NOP5_ATOMIC
};
static const unsigned char * const intel_nops[ASM_NOP_MAX+2] =
{
	NULL,
	intelnops,
	intelnops + 1,
	intelnops + 1 + 2,
	intelnops + 1 + 2 + 3,
	intelnops + 1 + 2 + 3 + 4,
	intelnops + 1 + 2 + 3 + 4 + 5,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

#ifdef K8_NOP1
static const unsigned char k8nops[] =
{
	K8_NOP1,
	K8_NOP2,
	K8_NOP3,
	K8_NOP4,
	K8_NOP5,
	K8_NOP6,
	K8_NOP7,
	K8_NOP8,
	K8_NOP5_ATOMIC
};
static const unsigned char * const k8_nops[ASM_NOP_MAX+2] =
{
	NULL,
	k8nops,
	k8nops + 1,
	k8nops + 1 + 2,
	k8nops + 1 + 2 + 3,
	k8nops + 1 + 2 + 3 + 4,
	k8nops + 1 + 2 + 3 + 4 + 5,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

#if defined(K7_NOP1) && !defined(CONFIG_X86_64)
static const unsigned char k7nops[] =
{
	K7_NOP1,
	K7_NOP2,
	K7_NOP3,
	K7_NOP4,
	K7_NOP5,
	K7_NOP6,
	K7_NOP7,
	K7_NOP8,
	K7_NOP5_ATOMIC
};
static const unsigned char * const k7_nops[ASM_NOP_MAX+2] =
{
	NULL,
	k7nops,
	k7nops + 1,
	k7nops + 1 + 2,
	k7nops + 1 + 2 + 3,
	k7nops + 1 + 2 + 3 + 4,
	k7nops + 1 + 2 + 3 + 4 + 5,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

#ifdef P6_NOP1
static const unsigned char p6nops[] =
{
	P6_NOP1,
	P6_NOP2,
	P6_NOP3,
	P6_NOP4,
	P6_NOP5,
	P6_NOP6,
	P6_NOP7,
	P6_NOP8,
	P6_NOP5_ATOMIC
};
static const unsigned char * const p6_nops[ASM_NOP_MAX+2] =
{
	NULL,
	p6nops,
	p6nops + 1,
	p6nops + 1 + 2,
	p6nops + 1 + 2 + 3,
	p6nops + 1 + 2 + 3 + 4,
	p6nops + 1 + 2 + 3 + 4 + 5,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

/* Initialize these to a safe default */
#ifdef CONFIG_X86_64
const unsigned char * const *ideal_nops = p6_nops;
#else
const unsigned char * const *ideal_nops = intel_nops;
#endif

void __init arch_init_ideal_nops(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		/*
		 * Due to a decoder implementation quirk, some
		 * specific Intel CPUs actually perform better with
		 * the "k8_nops" than with the SDM-recommended NOPs.
		 */
		if (boot_cpu_data.x86 == 6 &&
		    boot_cpu_data.x86_model >= 0x0f &&
		    boot_cpu_data.x86_model != 0x1c &&
		    boot_cpu_data.x86_model != 0x26 &&
		    boot_cpu_data.x86_model != 0x27 &&
		    boot_cpu_data.x86_model < 0x30) {
			ideal_nops = k8_nops;
		} else if (boot_cpu_has(X86_FEATURE_NOPL)) {
			   ideal_nops = p6_nops;
		} else {
#ifdef CONFIG_X86_64
			ideal_nops = k8_nops;
#else
			ideal_nops = intel_nops;
#endif
		}
		break;

	case X86_VENDOR_AMD:
		if (boot_cpu_data.x86 > 0xf) {
			ideal_nops = p6_nops;
			return;
		}

		/* fall through */

	default:
#ifdef CONFIG_X86_64
		ideal_nops = k8_nops;
#else
		if (boot_cpu_has(X86_FEATURE_K8))
			ideal_nops = k8_nops;
		else if (boot_cpu_has(X86_FEATURE_K7))
			ideal_nops = k7_nops;
		else
			ideal_nops = intel_nops;
#endif
	}
}

/* Use this to add nops to a buffer, then text_poke the whole buffer. */
static void __init_or_module add_nops(void *insns, unsigned int len)
{
	while (len > 0) {
		unsigned int noplen = len;
		if (noplen > ASM_NOP_MAX)
			noplen = ASM_NOP_MAX;
		memcpy(insns, ideal_nops[noplen], noplen);
		insns += noplen;
		len -= noplen;
	}
}

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
extern s32 __smp_locks[], __smp_locks_end[];
void *text_poke_early(void *addr, const void *opcode, size_t len);

/*
 * Are we looking at a near JMP with a 1 or 4-byte displacement.
 */
static inline bool is_jmp(const u8 opcode)
{
	return opcode == 0xeb || opcode == 0xe9;
}

static void __init_or_module
recompute_jump(struct alt_instr *a, u8 *orig_insn, u8 *repl_insn, u8 *insnbuf)
{
	u8 *next_rip, *tgt_rip;
	s32 n_dspl, o_dspl;
	int repl_len;

	if (a->replacementlen != 5)
		return;

	o_dspl = *(s32 *)(insnbuf + 1);

	/* next_rip of the replacement JMP */
	next_rip = repl_insn + a->replacementlen;
	/* target rip of the replacement JMP */
	tgt_rip  = next_rip + o_dspl;
	n_dspl = tgt_rip - orig_insn;

	DPRINTK("target RIP: %p, new_displ: 0x%x", tgt_rip, n_dspl);

	if (tgt_rip - orig_insn >= 0) {
		if (n_dspl - 2 <= 127)
			goto two_byte_jmp;
		else
			goto five_byte_jmp;
	/* negative offset */
	} else {
		if (((n_dspl - 2) & 0xff) == (n_dspl - 2))
			goto two_byte_jmp;
		else
			goto five_byte_jmp;
	}

two_byte_jmp:
	n_dspl -= 2;

	insnbuf[0] = 0xeb;
	insnbuf[1] = (s8)n_dspl;
	add_nops(insnbuf + 2, 3);

	repl_len = 2;
	goto done;

five_byte_jmp:
	n_dspl -= 5;

	insnbuf[0] = 0xe9;
	*(s32 *)&insnbuf[1] = n_dspl;

	repl_len = 5;

done:

	DPRINTK("final displ: 0x%08x, JMP 0x%lx",
		n_dspl, (unsigned long)orig_insn + n_dspl + repl_len);
}

/*
 * "noinline" to cause control flow change and thus invalidate I$ and
 * cause refetch after modification.
 */
static void __init_or_module noinline optimize_nops(struct alt_instr *a, u8 *instr)
{
	unsigned long flags;

	if (instr[0] != 0x90)
		return;

	local_irq_save(flags);
	add_nops(instr + (a->instrlen - a->padlen), a->padlen);
	local_irq_restore(flags);

	DUMP_BYTES(instr, a->instrlen, "%p: [%d:%d) optimized NOPs: ",
		   instr, a->instrlen - a->padlen, a->padlen);
}

/*
 * Replace instructions with better alternatives for this CPU type. This runs
 * before SMP is initialized to avoid SMP problems with self modifying code.
 * This implies that asymmetric systems where APs have less capabilities than
 * the boot processor are not handled. Tough. Make sure you disable such
 * features by hand.
 *
 * Marked "noinline" to cause control flow change and thus insn cache
 * to refetch changed I$ lines.
 */
void __init_or_module noinline apply_alternatives(struct alt_instr *start,
						  struct alt_instr *end)
{
	struct alt_instr *a;
	u8 *instr, *replacement;
	u8 insnbuf[MAX_PATCH_LEN];

	DPRINTK("alt table %p -> %p", start, end);
	/*
	 * The scan order should be from start to end. A later scanned
	 * alternative code can overwrite previously scanned alternative code.
	 * Some kernel functions (e.g. memcpy, memset, etc) use this order to
	 * patch code.
	 *
	 * So be careful if you want to change the scan order to any other
	 * order.
	 */
	for (a = start; a < end; a++) {
		int insnbuf_sz = 0;

		instr = (u8 *)&a->instr_offset + a->instr_offset;
		replacement = (u8 *)&a->repl_offset + a->repl_offset;
		BUG_ON(a->instrlen > sizeof(insnbuf));
		BUG_ON(a->cpuid >= (NCAPINTS + NBUGINTS) * 32);
		if (!boot_cpu_has(a->cpuid)) {
			if (a->padlen > 1)
				optimize_nops(a, instr);

			continue;
		}

		DPRINTK("feat: %d*32+%d, old: (%p, len: %d), repl: (%p, len: %d), pad: %d",
			a->cpuid >> 5,
			a->cpuid & 0x1f,
			instr, a->instrlen,
			replacement, a->replacementlen, a->padlen);

		DUMP_BYTES(instr, a->instrlen, "%p: old_insn: ", instr);
		DUMP_BYTES(replacement, a->replacementlen, "%p: rpl_insn: ", replacement);

		memcpy(insnbuf, replacement, a->replacementlen);
		insnbuf_sz = a->replacementlen;

		/* 0xe8 is a relative jump; fix the offset. */
		if (*insnbuf == 0xe8 && a->replacementlen == 5) {
			*(s32 *)(insnbuf + 1) += replacement - instr;
			DPRINTK("Fix CALL offset: 0x%x, CALL 0x%lx",
				*(s32 *)(insnbuf + 1),
				(unsigned long)instr + *(s32 *)(insnbuf + 1) + 5);
		}

		if (a->replacementlen && is_jmp(replacement[0]))
			recompute_jump(a, instr, replacement, insnbuf);

		if (a->instrlen > a->replacementlen) {
			add_nops(insnbuf + a->replacementlen,
				 a->instrlen - a->replacementlen);
			insnbuf_sz += a->instrlen - a->replacementlen;
		}
		DUMP_BYTES(insnbuf, insnbuf_sz, "%p: final_insn: ", instr);

		text_poke_early(instr, insnbuf, insnbuf_sz);
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
	}
	mutex_unlock(&text_mutex);
}

static void alternatives_smp_unlock(const s32 *start, const s32 *end,
				    u8 *text, u8 *text_end)
{
	const s32 *poff;

	mutex_lock(&text_mutex);
	for (poff = start; poff < end; poff++) {
		u8 *ptr = (u8 *)poff + *poff;

		if (!*poff || ptr < text || ptr >= text_end)
			continue;
		/* turn lock prefix into DS segment override prefix */
		if (*ptr == 0xf0)
			text_poke(ptr, ((unsigned char []){0x3E}), 1);
	}
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
static bool uniproc_patched = false;	/* protected by smp_alt */

void __init_or_module alternatives_smp_module_add(struct module *mod,
						  char *name,
						  void *locks, void *locks_end,
						  void *text,  void *text_end)
{
	struct smp_alt_module *smp;

	mutex_lock(&smp_alt);
	if (!uniproc_patched)
		goto unlock;

	if (num_possible_cpus() == 1)
		/* Don't bother remembering, we'll never have to undo it. */
		goto smp_unlock;

	smp = kzalloc(sizeof(*smp), GFP_KERNEL);
	if (NULL == smp)
		/* we'll run the (safe but slow) SMP code then ... */
		goto unlock;

	smp->mod	= mod;
	smp->name	= name;
	smp->locks	= locks;
	smp->locks_end	= locks_end;
	smp->text	= text;
	smp->text_end	= text_end;
	DPRINTK("locks %p -> %p, text %p -> %p, name %s\n",
		smp->locks, smp->locks_end,
		smp->text, smp->text_end, smp->name);

	list_add_tail(&smp->next, &smp_alt_modules);
smp_unlock:
	alternatives_smp_unlock(locks, locks_end, text, text_end);
unlock:
	mutex_unlock(&smp_alt);
}

void __init_or_module alternatives_smp_module_del(struct module *mod)
{
	struct smp_alt_module *item;

	mutex_lock(&smp_alt);
	list_for_each_entry(item, &smp_alt_modules, next) {
		if (mod != item->mod)
			continue;
		list_del(&item->next);
		kfree(item);
		break;
	}
	mutex_unlock(&smp_alt);
}

void alternatives_enable_smp(void)
{
	struct smp_alt_module *mod;

	/* Why bother if there are no other CPUs? */
	BUG_ON(num_possible_cpus() == 1);

	mutex_lock(&smp_alt);

	if (uniproc_patched) {
		pr_info("switching to SMP code\n");
		BUG_ON(num_online_cpus() != 1);
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_UP);
		clear_cpu_cap(&cpu_data(0), X86_FEATURE_UP);
		list_for_each_entry(mod, &smp_alt_modules, next)
			alternatives_smp_lock(mod->locks, mod->locks_end,
					      mod->text, mod->text_end);
		uniproc_patched = false;
	}
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
#endif /* CONFIG_SMP */

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

#ifdef CONFIG_SMP
	/* Patch to UP if other cpus not imminent. */
	if (!noreplace_smp && (num_present_cpus() == 1 || setup_max_cpus <= 1)) {
		uniproc_patched = true;
		alternatives_smp_module_add(NULL, "core kernel",
					    __smp_locks, __smp_locks_end,
					    _text, _etext);
	}

	if (!uniproc_patched || num_possible_cpus() == 1)
		free_init_pages("SMP alternatives",
				(unsigned long)__smp_locks,
				(unsigned long)__smp_locks_end);
#endif

	apply_paravirt(__parainstructions, __parainstructions_end);

	restart_nmi();
	alternatives_patched = 1;
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
void *text_poke(void *addr, const void *opcode, size_t len)
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

static void do_sync_core(void *info)
{
	sync_core();
}

static bool bp_patching_in_progress;
static void *bp_int3_handler, *bp_int3_addr;

int poke_int3_handler(struct pt_regs *regs)
{
	/* bp_patching_in_progress */
	smp_rmb();

	if (likely(!bp_patching_in_progress))
		return 0;

	if (user_mode(regs) || regs->ip != (unsigned long)bp_int3_addr)
		return 0;

	/* set up the specified breakpoint handler */
	regs->ip = (unsigned long) bp_int3_handler;

	return 1;

}

/**
 * text_poke_bp() -- update instructions on live kernel on SMP
 * @addr:	address to patch
 * @opcode:	opcode of new instruction
 * @len:	length to copy
 * @handler:	address to jump to when the temporary breakpoint is hit
 *
 * Modify multi-byte instruction by using int3 breakpoint on SMP.
 * We completely avoid stop_machine() here, and achieve the
 * synchronization using int3 breakpoint.
 *
 * The way it is done:
 *	- add a int3 trap to the address that will be patched
 *	- sync cores
 *	- update all but the first byte of the patched range
 *	- sync cores
 *	- replace the first byte (int3) by the first byte of
 *	  replacing opcode
 *	- sync cores
 *
 * Note: must be called under text_mutex.
 */
void *text_poke_bp(void *addr, const void *opcode, size_t len, void *handler)
{
	unsigned char int3 = 0xcc;

	bp_int3_handler = handler;
	bp_int3_addr = (u8 *)addr + sizeof(int3);
	bp_patching_in_progress = true;
	/*
	 * Corresponding read barrier in int3 notifier for
	 * making sure the in_progress flags is correctly ordered wrt.
	 * patching
	 */
	smp_wmb();

	text_poke(addr, &int3, sizeof(int3));

	on_each_cpu(do_sync_core, NULL, 1);

	if (len - sizeof(int3) > 0) {
		/* patch all but the first byte */
		text_poke((char *)addr + sizeof(int3),
			  (const char *) opcode + sizeof(int3),
			  len - sizeof(int3));
		/*
		 * According to Intel, this core syncing is very likely
		 * not necessary and we'd be safe even without it. But
		 * better safe than sorry (plus there's not only Intel).
		 */
		on_each_cpu(do_sync_core, NULL, 1);
	}

	/* patch the first byte */
	text_poke(addr, opcode, sizeof(int3));

	on_each_cpu(do_sync_core, NULL, 1);

	bp_patching_in_progress = false;
	smp_wmb();

	return addr;
}

