#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <asm/alternative.h>
#include <asm/sections.h>

static int no_replacement    = 0;
static int smp_alt_once      = 0;
static int debug_alternative = 0;

static int __init noreplacement_setup(char *s)
{
	no_replacement = 1;
	return 1;
}
static int __init bootonly(char *str)
{
	smp_alt_once = 1;
	return 1;
}
static int __init debug_alt(char *str)
{
	debug_alternative = 1;
	return 1;
}

__setup("noreplacement", noreplacement_setup);
__setup("smp-alt-boot", bootonly);
__setup("debug-alternative", debug_alt);

#define DPRINTK(fmt, args...) if (debug_alternative) \
	printk(KERN_DEBUG fmt, args)

#ifdef GENERIC_NOP1
/* Use inline assembly to define this because the nops are defined
   as inline assembly strings in the include files and we cannot
   get them easily into strings. */
asm("\t.data\nintelnops: "
	GENERIC_NOP1 GENERIC_NOP2 GENERIC_NOP3 GENERIC_NOP4 GENERIC_NOP5 GENERIC_NOP6
	GENERIC_NOP7 GENERIC_NOP8);
extern unsigned char intelnops[];
static unsigned char *intel_nops[ASM_NOP_MAX+1] = {
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
asm("\t.data\nk8nops: "
	K8_NOP1 K8_NOP2 K8_NOP3 K8_NOP4 K8_NOP5 K8_NOP6
	K8_NOP7 K8_NOP8);
extern unsigned char k8nops[];
static unsigned char *k8_nops[ASM_NOP_MAX+1] = {
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

#ifdef K7_NOP1
asm("\t.data\nk7nops: "
	K7_NOP1 K7_NOP2 K7_NOP3 K7_NOP4 K7_NOP5 K7_NOP6
	K7_NOP7 K7_NOP8);
extern unsigned char k7nops[];
static unsigned char *k7_nops[ASM_NOP_MAX+1] = {
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

#ifdef CONFIG_X86_64

extern char __vsyscall_0;
static inline unsigned char** find_nop_table(void)
{
	return k8_nops;
}

#else /* CONFIG_X86_64 */

static struct nop {
	int cpuid;
	unsigned char **noptable;
} noptypes[] = {
	{ X86_FEATURE_K8, k8_nops },
	{ X86_FEATURE_K7, k7_nops },
	{ -1, NULL }
};

static unsigned char** find_nop_table(void)
{
	unsigned char **noptable = intel_nops;
	int i;

	for (i = 0; noptypes[i].cpuid >= 0; i++) {
		if (boot_cpu_has(noptypes[i].cpuid)) {
			noptable = noptypes[i].noptable;
			break;
		}
	}
	return noptable;
}

#endif /* CONFIG_X86_64 */

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
extern struct alt_instr __smp_alt_instructions[], __smp_alt_instructions_end[];
extern u8 *__smp_locks[], *__smp_locks_end[];

extern u8 __smp_alt_begin[], __smp_alt_end[];

/* Replace instructions with better alternatives for this CPU type.
   This runs before SMP is initialized to avoid SMP problems with
   self modifying code. This implies that assymetric systems where
   APs have less capabilities than the boot processor are not handled.
   Tough. Make sure you disable such features by hand. */

void apply_alternatives(struct alt_instr *start, struct alt_instr *end)
{
	unsigned char **noptable = find_nop_table();
	struct alt_instr *a;
	u8 *instr;
	int diff, i, k;

	DPRINTK("%s: alt table %p -> %p\n", __FUNCTION__, start, end);
	for (a = start; a < end; a++) {
		BUG_ON(a->replacementlen > a->instrlen);
		if (!boot_cpu_has(a->cpuid))
			continue;
		instr = a->instr;
#ifdef CONFIG_X86_64
		/* vsyscall code is not mapped yet. resolve it manually. */
		if (instr >= (u8 *)VSYSCALL_START && instr < (u8*)VSYSCALL_END) {
			instr = __va(instr - (u8*)VSYSCALL_START + (u8*)__pa_symbol(&__vsyscall_0));
			DPRINTK("%s: vsyscall fixup: %p => %p\n",
				__FUNCTION__, a->instr, instr);
		}
#endif
		memcpy(instr, a->replacement, a->replacementlen);
		diff = a->instrlen - a->replacementlen;
		/* Pad the rest with nops */
		for (i = a->replacementlen; diff > 0; diff -= k, i += k) {
			k = diff;
			if (k > ASM_NOP_MAX)
				k = ASM_NOP_MAX;
			memcpy(a->instr + i, noptable[k], k);
		}
	}
}

#ifdef CONFIG_SMP

static void alternatives_smp_save(struct alt_instr *start, struct alt_instr *end)
{
	struct alt_instr *a;

	DPRINTK("%s: alt table %p-%p\n", __FUNCTION__, start, end);
	for (a = start; a < end; a++) {
		memcpy(a->replacement + a->replacementlen,
		       a->instr,
		       a->instrlen);
	}
}

static void alternatives_smp_apply(struct alt_instr *start, struct alt_instr *end)
{
	struct alt_instr *a;

	for (a = start; a < end; a++) {
		memcpy(a->instr,
		       a->replacement + a->replacementlen,
		       a->instrlen);
	}
}

static void alternatives_smp_lock(u8 **start, u8 **end, u8 *text, u8 *text_end)
{
	u8 **ptr;

	for (ptr = start; ptr < end; ptr++) {
		if (*ptr < text)
			continue;
		if (*ptr > text_end)
			continue;
		**ptr = 0xf0; /* lock prefix */
	};
}

static void alternatives_smp_unlock(u8 **start, u8 **end, u8 *text, u8 *text_end)
{
	unsigned char **noptable = find_nop_table();
	u8 **ptr;

	for (ptr = start; ptr < end; ptr++) {
		if (*ptr < text)
			continue;
		if (*ptr > text_end)
			continue;
		**ptr = noptable[1][0];
	};
}

struct smp_alt_module {
	/* what is this ??? */
	struct module	*mod;
	char		*name;

	/* ptrs to lock prefixes */
	u8		**locks;
	u8		**locks_end;

	/* .text segment, needed to avoid patching init code ;) */
	u8		*text;
	u8		*text_end;

	struct list_head next;
};
static LIST_HEAD(smp_alt_modules);
static DEFINE_SPINLOCK(smp_alt);

void alternatives_smp_module_add(struct module *mod, char *name,
				 void *locks, void *locks_end,
				 void *text,  void *text_end)
{
	struct smp_alt_module *smp;
	unsigned long flags;

	if (no_replacement)
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
		__FUNCTION__, smp->locks, smp->locks_end,
		smp->text, smp->text_end, smp->name);

	spin_lock_irqsave(&smp_alt, flags);
	list_add_tail(&smp->next, &smp_alt_modules);
	if (boot_cpu_has(X86_FEATURE_UP))
		alternatives_smp_unlock(smp->locks, smp->locks_end,
					smp->text, smp->text_end);
	spin_unlock_irqrestore(&smp_alt, flags);
}

void alternatives_smp_module_del(struct module *mod)
{
	struct smp_alt_module *item;
	unsigned long flags;

	if (no_replacement || smp_alt_once)
		return;

	spin_lock_irqsave(&smp_alt, flags);
	list_for_each_entry(item, &smp_alt_modules, next) {
		if (mod != item->mod)
			continue;
		list_del(&item->next);
		spin_unlock_irqrestore(&smp_alt, flags);
		DPRINTK("%s: %s\n", __FUNCTION__, item->name);
		kfree(item);
		return;
	}
	spin_unlock_irqrestore(&smp_alt, flags);
}

void alternatives_smp_switch(int smp)
{
	struct smp_alt_module *mod;
	unsigned long flags;

#ifdef CONFIG_LOCKDEP
	/*
	 * A not yet fixed binutils section handling bug prevents
	 * alternatives-replacement from working reliably, so turn
	 * it off:
	 */
	printk("lockdep: not fixing up alternatives.\n");
	return;
#endif

	if (no_replacement || smp_alt_once)
		return;
	BUG_ON(!smp && (num_online_cpus() > 1));

	spin_lock_irqsave(&smp_alt, flags);
	if (smp) {
		printk(KERN_INFO "SMP alternatives: switching to SMP code\n");
		clear_bit(X86_FEATURE_UP, boot_cpu_data.x86_capability);
		clear_bit(X86_FEATURE_UP, cpu_data[0].x86_capability);
		alternatives_smp_apply(__smp_alt_instructions,
				       __smp_alt_instructions_end);
		list_for_each_entry(mod, &smp_alt_modules, next)
			alternatives_smp_lock(mod->locks, mod->locks_end,
					      mod->text, mod->text_end);
	} else {
		printk(KERN_INFO "SMP alternatives: switching to UP code\n");
		set_bit(X86_FEATURE_UP, boot_cpu_data.x86_capability);
		set_bit(X86_FEATURE_UP, cpu_data[0].x86_capability);
		apply_alternatives(__smp_alt_instructions,
				   __smp_alt_instructions_end);
		list_for_each_entry(mod, &smp_alt_modules, next)
			alternatives_smp_unlock(mod->locks, mod->locks_end,
						mod->text, mod->text_end);
	}
	spin_unlock_irqrestore(&smp_alt, flags);
}

#endif

void __init alternative_instructions(void)
{
	unsigned long flags;
	if (no_replacement) {
		printk(KERN_INFO "(SMP-)alternatives turned off\n");
		free_init_pages("SMP alternatives",
				(unsigned long)__smp_alt_begin,
				(unsigned long)__smp_alt_end);
		return;
	}

	local_irq_save(flags);
	apply_alternatives(__alt_instructions, __alt_instructions_end);

	/* switch to patch-once-at-boottime-only mode and free the
	 * tables in case we know the number of CPUs will never ever
	 * change */
#ifdef CONFIG_HOTPLUG_CPU
	if (num_possible_cpus() < 2)
		smp_alt_once = 1;
#else
	smp_alt_once = 1;
#endif

#ifdef CONFIG_SMP
	if (smp_alt_once) {
		if (1 == num_possible_cpus()) {
			printk(KERN_INFO "SMP alternatives: switching to UP code\n");
			set_bit(X86_FEATURE_UP, boot_cpu_data.x86_capability);
			set_bit(X86_FEATURE_UP, cpu_data[0].x86_capability);
			apply_alternatives(__smp_alt_instructions,
					   __smp_alt_instructions_end);
			alternatives_smp_unlock(__smp_locks, __smp_locks_end,
						_text, _etext);
		}
		free_init_pages("SMP alternatives",
				(unsigned long)__smp_alt_begin,
				(unsigned long)__smp_alt_end);
	} else {
		alternatives_smp_save(__smp_alt_instructions,
				      __smp_alt_instructions_end);
		alternatives_smp_module_add(NULL, "core kernel",
					    __smp_locks, __smp_locks_end,
					    _text, _etext);
		alternatives_smp_switch(0);
	}
#endif
	local_irq_restore(flags);
}
