// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2019 SUSE
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#define pr_fmt(fmt)	"SEV-ES: " fmt

#include <linux/sched/debug.h>	/* For show_regs() */
#include <linux/percpu-defs.h>
#include <linux/mem_encrypt.h>
#include <linux/lockdep.h>
#include <linux/printk.h>
#include <linux/mm_types.h>
#include <linux/set_memory.h>
#include <linux/memblock.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/cpu_entry_area.h>
#include <asm/stacktrace.h>
#include <asm/sev-es.h>
#include <asm/insn-eval.h>
#include <asm/fpu/internal.h>
#include <asm/processor.h>
#include <asm/realmode.h>
#include <asm/traps.h>
#include <asm/svm.h>
#include <asm/smp.h>
#include <asm/cpu.h>

#define DR7_RESET_VALUE        0x400

/* For early boot hypervisor communication in SEV-ES enabled guests */
static struct ghcb boot_ghcb_page __bss_decrypted __aligned(PAGE_SIZE);

/*
 * Needs to be in the .data section because we need it NULL before bss is
 * cleared
 */
static struct ghcb __initdata *boot_ghcb;

/* #VC handler runtime per-CPU data */
struct sev_es_runtime_data {
	struct ghcb ghcb_page;

	/* Physical storage for the per-CPU IST stack of the #VC handler */
	char ist_stack[EXCEPTION_STKSZ] __aligned(PAGE_SIZE);

	/*
	 * Physical storage for the per-CPU fall-back stack of the #VC handler.
	 * The fall-back stack is used when it is not safe to switch back to the
	 * interrupted stack in the #VC entry code.
	 */
	char fallback_stack[EXCEPTION_STKSZ] __aligned(PAGE_SIZE);

	/*
	 * Reserve one page per CPU as backup storage for the unencrypted GHCB.
	 * It is needed when an NMI happens while the #VC handler uses the real
	 * GHCB, and the NMI handler itself is causing another #VC exception. In
	 * that case the GHCB content of the first handler needs to be backed up
	 * and restored.
	 */
	struct ghcb backup_ghcb;

	/*
	 * Mark the per-cpu GHCBs as in-use to detect nested #VC exceptions.
	 * There is no need for it to be atomic, because nothing is written to
	 * the GHCB between the read and the write of ghcb_active. So it is safe
	 * to use it when a nested #VC exception happens before the write.
	 *
	 * This is necessary for example in the #VC->NMI->#VC case when the NMI
	 * happens while the first #VC handler uses the GHCB. When the NMI code
	 * raises a second #VC handler it might overwrite the contents of the
	 * GHCB written by the first handler. To avoid this the content of the
	 * GHCB is saved and restored when the GHCB is detected to be in use
	 * already.
	 */
	bool ghcb_active;
	bool backup_ghcb_active;

	/*
	 * Cached DR7 value - write it on DR7 writes and return it on reads.
	 * That value will never make it to the real hardware DR7 as debugging
	 * is currently unsupported in SEV-ES guests.
	 */
	unsigned long dr7;
};

struct ghcb_state {
	struct ghcb *ghcb;
};

static DEFINE_PER_CPU(struct sev_es_runtime_data*, runtime_data);
DEFINE_STATIC_KEY_FALSE(sev_es_enable_key);

/* Needed in vc_early_forward_exception */
void do_early_exception(struct pt_regs *regs, int trapnr);

static void __init setup_vc_stacks(int cpu)
{
	struct sev_es_runtime_data *data;
	struct cpu_entry_area *cea;
	unsigned long vaddr;
	phys_addr_t pa;

	data = per_cpu(runtime_data, cpu);
	cea  = get_cpu_entry_area(cpu);

	/* Map #VC IST stack */
	vaddr = CEA_ESTACK_BOT(&cea->estacks, VC);
	pa    = __pa(data->ist_stack);
	cea_set_pte((void *)vaddr, pa, PAGE_KERNEL);

	/* Map VC fall-back stack */
	vaddr = CEA_ESTACK_BOT(&cea->estacks, VC2);
	pa    = __pa(data->fallback_stack);
	cea_set_pte((void *)vaddr, pa, PAGE_KERNEL);
}

static __always_inline bool on_vc_stack(unsigned long sp)
{
	return ((sp >= __this_cpu_ist_bottom_va(VC)) && (sp < __this_cpu_ist_top_va(VC)));
}

/*
 * This function handles the case when an NMI is raised in the #VC exception
 * handler entry code. In this case, the IST entry for #VC must be adjusted, so
 * that any subsequent #VC exception will not overwrite the stack contents of the
 * interrupted #VC handler.
 *
 * The IST entry is adjusted unconditionally so that it can be also be
 * unconditionally adjusted back in sev_es_ist_exit(). Otherwise a nested
 * sev_es_ist_exit() call may adjust back the IST entry too early.
 */
void noinstr __sev_es_ist_enter(struct pt_regs *regs)
{
	unsigned long old_ist, new_ist;

	/* Read old IST entry */
	old_ist = __this_cpu_read(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC]);

	/* Make room on the IST stack */
	if (on_vc_stack(regs->sp))
		new_ist = ALIGN_DOWN(regs->sp, 8) - sizeof(old_ist);
	else
		new_ist = old_ist - sizeof(old_ist);

	/* Store old IST entry */
	*(unsigned long *)new_ist = old_ist;

	/* Set new IST entry */
	this_cpu_write(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC], new_ist);
}

void noinstr __sev_es_ist_exit(void)
{
	unsigned long ist;

	/* Read IST entry */
	ist = __this_cpu_read(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC]);

	if (WARN_ON(ist == __this_cpu_ist_top_va(VC)))
		return;

	/* Read back old IST entry and write it to the TSS */
	this_cpu_write(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC], *(unsigned long *)ist);
}

static __always_inline struct ghcb *sev_es_get_ghcb(struct ghcb_state *state)
{
	struct sev_es_runtime_data *data;
	struct ghcb *ghcb;

	data = this_cpu_read(runtime_data);
	ghcb = &data->ghcb_page;

	if (unlikely(data->ghcb_active)) {
		/* GHCB is already in use - save its contents */

		if (unlikely(data->backup_ghcb_active))
			return NULL;

		/* Mark backup_ghcb active before writing to it */
		data->backup_ghcb_active = true;

		state->ghcb = &data->backup_ghcb;

		/* Backup GHCB content */
		*state->ghcb = *ghcb;
	} else {
		state->ghcb = NULL;
		data->ghcb_active = true;
	}

	return ghcb;
}

static __always_inline void sev_es_put_ghcb(struct ghcb_state *state)
{
	struct sev_es_runtime_data *data;
	struct ghcb *ghcb;

	data = this_cpu_read(runtime_data);
	ghcb = &data->ghcb_page;

	if (state->ghcb) {
		/* Restore GHCB from Backup */
		*ghcb = *state->ghcb;
		data->backup_ghcb_active = false;
		state->ghcb = NULL;
	} else {
		data->ghcb_active = false;
	}
}

/* Needed in vc_early_forward_exception */
void do_early_exception(struct pt_regs *regs, int trapnr);

static inline u64 sev_es_rd_ghcb_msr(void)
{
	return __rdmsr(MSR_AMD64_SEV_ES_GHCB);
}

static __always_inline void sev_es_wr_ghcb_msr(u64 val)
{
	u32 low, high;

	low  = (u32)(val);
	high = (u32)(val >> 32);

	native_wrmsr(MSR_AMD64_SEV_ES_GHCB, low, high);
}

static int vc_fetch_insn_kernel(struct es_em_ctxt *ctxt,
				unsigned char *buffer)
{
	return copy_from_kernel_nofault(buffer, (unsigned char *)ctxt->regs->ip, MAX_INSN_SIZE);
}

static enum es_result vc_decode_insn(struct es_em_ctxt *ctxt)
{
	char buffer[MAX_INSN_SIZE];
	enum es_result ret;
	int res;

	if (user_mode(ctxt->regs)) {
		res = insn_fetch_from_user(ctxt->regs, buffer);
		if (!res) {
			ctxt->fi.vector     = X86_TRAP_PF;
			ctxt->fi.error_code = X86_PF_INSTR | X86_PF_USER;
			ctxt->fi.cr2        = ctxt->regs->ip;
			return ES_EXCEPTION;
		}

		if (!insn_decode(&ctxt->insn, ctxt->regs, buffer, res))
			return ES_DECODE_FAILED;
	} else {
		res = vc_fetch_insn_kernel(ctxt, buffer);
		if (res) {
			ctxt->fi.vector     = X86_TRAP_PF;
			ctxt->fi.error_code = X86_PF_INSTR;
			ctxt->fi.cr2        = ctxt->regs->ip;
			return ES_EXCEPTION;
		}

		insn_init(&ctxt->insn, buffer, MAX_INSN_SIZE - res, 1);
		insn_get_length(&ctxt->insn);
	}

	ret = ctxt->insn.immediate.got ? ES_OK : ES_DECODE_FAILED;

	return ret;
}

static enum es_result vc_write_mem(struct es_em_ctxt *ctxt,
				   char *dst, char *buf, size_t size)
{
	unsigned long error_code = X86_PF_PROT | X86_PF_WRITE;
	char __user *target = (char __user *)dst;
	u64 d8;
	u32 d4;
	u16 d2;
	u8  d1;

	/* If instruction ran in kernel mode and the I/O buffer is in kernel space */
	if (!user_mode(ctxt->regs) && !access_ok(target, size)) {
		memcpy(dst, buf, size);
		return ES_OK;
	}

	switch (size) {
	case 1:
		memcpy(&d1, buf, 1);
		if (put_user(d1, target))
			goto fault;
		break;
	case 2:
		memcpy(&d2, buf, 2);
		if (put_user(d2, target))
			goto fault;
		break;
	case 4:
		memcpy(&d4, buf, 4);
		if (put_user(d4, target))
			goto fault;
		break;
	case 8:
		memcpy(&d8, buf, 8);
		if (put_user(d8, target))
			goto fault;
		break;
	default:
		WARN_ONCE(1, "%s: Invalid size: %zu\n", __func__, size);
		return ES_UNSUPPORTED;
	}

	return ES_OK;

fault:
	if (user_mode(ctxt->regs))
		error_code |= X86_PF_USER;

	ctxt->fi.vector = X86_TRAP_PF;
	ctxt->fi.error_code = error_code;
	ctxt->fi.cr2 = (unsigned long)dst;

	return ES_EXCEPTION;
}

static enum es_result vc_read_mem(struct es_em_ctxt *ctxt,
				  char *src, char *buf, size_t size)
{
	unsigned long error_code = X86_PF_PROT;
	char __user *s = (char __user *)src;
	u64 d8;
	u32 d4;
	u16 d2;
	u8  d1;

	/* If instruction ran in kernel mode and the I/O buffer is in kernel space */
	if (!user_mode(ctxt->regs) && !access_ok(s, size)) {
		memcpy(buf, src, size);
		return ES_OK;
	}

	switch (size) {
	case 1:
		if (get_user(d1, s))
			goto fault;
		memcpy(buf, &d1, 1);
		break;
	case 2:
		if (get_user(d2, s))
			goto fault;
		memcpy(buf, &d2, 2);
		break;
	case 4:
		if (get_user(d4, s))
			goto fault;
		memcpy(buf, &d4, 4);
		break;
	case 8:
		if (get_user(d8, s))
			goto fault;
		memcpy(buf, &d8, 8);
		break;
	default:
		WARN_ONCE(1, "%s: Invalid size: %zu\n", __func__, size);
		return ES_UNSUPPORTED;
	}

	return ES_OK;

fault:
	if (user_mode(ctxt->regs))
		error_code |= X86_PF_USER;

	ctxt->fi.vector = X86_TRAP_PF;
	ctxt->fi.error_code = error_code;
	ctxt->fi.cr2 = (unsigned long)src;

	return ES_EXCEPTION;
}

static enum es_result vc_slow_virt_to_phys(struct ghcb *ghcb, struct es_em_ctxt *ctxt,
					   unsigned long vaddr, phys_addr_t *paddr)
{
	unsigned long va = (unsigned long)vaddr;
	unsigned int level;
	phys_addr_t pa;
	pgd_t *pgd;
	pte_t *pte;

	pgd = __va(read_cr3_pa());
	pgd = &pgd[pgd_index(va)];
	pte = lookup_address_in_pgd(pgd, va, &level);
	if (!pte) {
		ctxt->fi.vector     = X86_TRAP_PF;
		ctxt->fi.cr2        = vaddr;
		ctxt->fi.error_code = 0;

		if (user_mode(ctxt->regs))
			ctxt->fi.error_code |= X86_PF_USER;

		return ES_EXCEPTION;
	}

	if (WARN_ON_ONCE(pte_val(*pte) & _PAGE_ENC))
		/* Emulated MMIO to/from encrypted memory not supported */
		return ES_UNSUPPORTED;

	pa = (phys_addr_t)pte_pfn(*pte) << PAGE_SHIFT;
	pa |= va & ~page_level_mask(level);

	*paddr = pa;

	return ES_OK;
}

/* Include code shared with pre-decompression boot stage */
#include "sev-es-shared.c"

void noinstr __sev_es_nmi_complete(void)
{
	struct ghcb_state state;
	struct ghcb *ghcb;

	ghcb = sev_es_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_NMI_COMPLETE);
	ghcb_set_sw_exit_info_1(ghcb, 0);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa_nodebug(ghcb));
	VMGEXIT();

	sev_es_put_ghcb(&state);
}

static u64 get_jump_table_addr(void)
{
	struct ghcb_state state;
	unsigned long flags;
	struct ghcb *ghcb;
	u64 ret = 0;

	local_irq_save(flags);

	ghcb = sev_es_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_AP_JUMP_TABLE);
	ghcb_set_sw_exit_info_1(ghcb, SVM_VMGEXIT_GET_AP_JUMP_TABLE);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	if (ghcb_sw_exit_info_1_is_valid(ghcb) &&
	    ghcb_sw_exit_info_2_is_valid(ghcb))
		ret = ghcb->save.sw_exit_info_2;

	sev_es_put_ghcb(&state);

	local_irq_restore(flags);

	return ret;
}

int sev_es_setup_ap_jump_table(struct real_mode_header *rmh)
{
	u16 startup_cs, startup_ip;
	phys_addr_t jump_table_pa;
	u64 jump_table_addr;
	u16 __iomem *jump_table;

	jump_table_addr = get_jump_table_addr();

	/* On UP guests there is no jump table so this is not a failure */
	if (!jump_table_addr)
		return 0;

	/* Check if AP Jump Table is page-aligned */
	if (jump_table_addr & ~PAGE_MASK)
		return -EINVAL;

	jump_table_pa = jump_table_addr & PAGE_MASK;

	startup_cs = (u16)(rmh->trampoline_start >> 4);
	startup_ip = (u16)(rmh->sev_es_trampoline_start -
			   rmh->trampoline_start);

	jump_table = ioremap_encrypted(jump_table_pa, PAGE_SIZE);
	if (!jump_table)
		return -EIO;

	writew(startup_ip, &jump_table[0]);
	writew(startup_cs, &jump_table[1]);

	iounmap(jump_table);

	return 0;
}

/*
 * This is needed by the OVMF UEFI firmware which will use whatever it finds in
 * the GHCB MSR as its GHCB to talk to the hypervisor. So make sure the per-cpu
 * runtime GHCBs used by the kernel are also mapped in the EFI page-table.
 */
int __init sev_es_efi_map_ghcbs(pgd_t *pgd)
{
	struct sev_es_runtime_data *data;
	unsigned long address, pflags;
	int cpu;
	u64 pfn;

	if (!sev_es_active())
		return 0;

	pflags = _PAGE_NX | _PAGE_RW;

	for_each_possible_cpu(cpu) {
		data = per_cpu(runtime_data, cpu);

		address = __pa(&data->ghcb_page);
		pfn = address >> PAGE_SHIFT;

		if (kernel_map_pages_in_pgd(pgd, pfn, address, 1, pflags))
			return 1;
	}

	return 0;
}

static enum es_result vc_handle_msr(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	struct pt_regs *regs = ctxt->regs;
	enum es_result ret;
	u64 exit_info_1;

	/* Is it a WRMSR? */
	exit_info_1 = (ctxt->insn.opcode.bytes[1] == 0x30) ? 1 : 0;

	ghcb_set_rcx(ghcb, regs->cx);
	if (exit_info_1) {
		ghcb_set_rax(ghcb, regs->ax);
		ghcb_set_rdx(ghcb, regs->dx);
	}

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_MSR, exit_info_1, 0);

	if ((ret == ES_OK) && (!exit_info_1)) {
		regs->ax = ghcb->save.rax;
		regs->dx = ghcb->save.rdx;
	}

	return ret;
}

/*
 * This function runs on the first #VC exception after the kernel
 * switched to virtual addresses.
 */
static bool __init sev_es_setup_ghcb(void)
{
	/* First make sure the hypervisor talks a supported protocol. */
	if (!sev_es_negotiate_protocol())
		return false;

	/*
	 * Clear the boot_ghcb. The first exception comes in before the bss
	 * section is cleared.
	 */
	memset(&boot_ghcb_page, 0, PAGE_SIZE);

	/* Alright - Make the boot-ghcb public */
	boot_ghcb = &boot_ghcb_page;

	return true;
}

#ifdef CONFIG_HOTPLUG_CPU
static void sev_es_ap_hlt_loop(void)
{
	struct ghcb_state state;
	struct ghcb *ghcb;

	ghcb = sev_es_get_ghcb(&state);

	while (true) {
		vc_ghcb_invalidate(ghcb);
		ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_AP_HLT_LOOP);
		ghcb_set_sw_exit_info_1(ghcb, 0);
		ghcb_set_sw_exit_info_2(ghcb, 0);

		sev_es_wr_ghcb_msr(__pa(ghcb));
		VMGEXIT();

		/* Wakeup signal? */
		if (ghcb_sw_exit_info_2_is_valid(ghcb) &&
		    ghcb->save.sw_exit_info_2)
			break;
	}

	sev_es_put_ghcb(&state);
}

/*
 * Play_dead handler when running under SEV-ES. This is needed because
 * the hypervisor can't deliver an SIPI request to restart the AP.
 * Instead the kernel has to issue a VMGEXIT to halt the VCPU until the
 * hypervisor wakes it up again.
 */
static void sev_es_play_dead(void)
{
	play_dead_common();

	/* IRQs now disabled */

	sev_es_ap_hlt_loop();

	/*
	 * If we get here, the VCPU was woken up again. Jump to CPU
	 * startup code to get it back online.
	 */
	start_cpu0();
}
#else  /* CONFIG_HOTPLUG_CPU */
#define sev_es_play_dead	native_play_dead
#endif /* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_SMP
static void __init sev_es_setup_play_dead(void)
{
	smp_ops.play_dead = sev_es_play_dead;
}
#else
static inline void sev_es_setup_play_dead(void) { }
#endif

static void __init alloc_runtime_data(int cpu)
{
	struct sev_es_runtime_data *data;

	data = memblock_alloc(sizeof(*data), PAGE_SIZE);
	if (!data)
		panic("Can't allocate SEV-ES runtime data");

	per_cpu(runtime_data, cpu) = data;
}

static void __init init_ghcb(int cpu)
{
	struct sev_es_runtime_data *data;
	int err;

	data = per_cpu(runtime_data, cpu);

	err = early_set_memory_decrypted((unsigned long)&data->ghcb_page,
					 sizeof(data->ghcb_page));
	if (err)
		panic("Can't map GHCBs unencrypted");

	memset(&data->ghcb_page, 0, sizeof(data->ghcb_page));

	data->ghcb_active = false;
	data->backup_ghcb_active = false;
}

void __init sev_es_init_vc_handling(void)
{
	int cpu;

	BUILD_BUG_ON(offsetof(struct sev_es_runtime_data, ghcb_page) % PAGE_SIZE);

	if (!sev_es_active())
		return;

	if (!sev_es_check_cpu_features())
		panic("SEV-ES CPU Features missing");

	/* Enable SEV-ES special handling */
	static_branch_enable(&sev_es_enable_key);

	/* Initialize per-cpu GHCB pages */
	for_each_possible_cpu(cpu) {
		alloc_runtime_data(cpu);
		init_ghcb(cpu);
		setup_vc_stacks(cpu);
	}

	sev_es_setup_play_dead();

	/* Secondary CPUs use the runtime #VC handler */
	initial_vc_handler = (unsigned long)safe_stack_exc_vmm_communication;
}

static void __init vc_early_forward_exception(struct es_em_ctxt *ctxt)
{
	int trapnr = ctxt->fi.vector;

	if (trapnr == X86_TRAP_PF)
		native_write_cr2(ctxt->fi.cr2);

	ctxt->regs->orig_ax = ctxt->fi.error_code;
	do_early_exception(ctxt->regs, trapnr);
}

static long *vc_insn_get_reg(struct es_em_ctxt *ctxt)
{
	long *reg_array;
	int offset;

	reg_array = (long *)ctxt->regs;
	offset    = insn_get_modrm_reg_off(&ctxt->insn, ctxt->regs);

	if (offset < 0)
		return NULL;

	offset /= sizeof(long);

	return reg_array + offset;
}

static long *vc_insn_get_rm(struct es_em_ctxt *ctxt)
{
	long *reg_array;
	int offset;

	reg_array = (long *)ctxt->regs;
	offset    = insn_get_modrm_rm_off(&ctxt->insn, ctxt->regs);

	if (offset < 0)
		return NULL;

	offset /= sizeof(long);

	return reg_array + offset;
}
static enum es_result vc_do_mmio(struct ghcb *ghcb, struct es_em_ctxt *ctxt,
				 unsigned int bytes, bool read)
{
	u64 exit_code, exit_info_1, exit_info_2;
	unsigned long ghcb_pa = __pa(ghcb);
	enum es_result res;
	phys_addr_t paddr;
	void __user *ref;

	ref = insn_get_addr_ref(&ctxt->insn, ctxt->regs);
	if (ref == (void __user *)-1L)
		return ES_UNSUPPORTED;

	exit_code = read ? SVM_VMGEXIT_MMIO_READ : SVM_VMGEXIT_MMIO_WRITE;

	res = vc_slow_virt_to_phys(ghcb, ctxt, (unsigned long)ref, &paddr);
	if (res != ES_OK) {
		if (res == ES_EXCEPTION && !read)
			ctxt->fi.error_code |= X86_PF_WRITE;

		return res;
	}

	exit_info_1 = paddr;
	/* Can never be greater than 8 */
	exit_info_2 = bytes;

	ghcb_set_sw_scratch(ghcb, ghcb_pa + offsetof(struct ghcb, shared_buffer));

	return sev_es_ghcb_hv_call(ghcb, ctxt, exit_code, exit_info_1, exit_info_2);
}

static enum es_result vc_handle_mmio_twobyte_ops(struct ghcb *ghcb,
						 struct es_em_ctxt *ctxt)
{
	struct insn *insn = &ctxt->insn;
	unsigned int bytes = 0;
	enum es_result ret;
	int sign_byte;
	long *reg_data;

	switch (insn->opcode.bytes[1]) {
		/* MMIO Read w/ zero-extension */
	case 0xb6:
		bytes = 1;
		fallthrough;
	case 0xb7:
		if (!bytes)
			bytes = 2;

		ret = vc_do_mmio(ghcb, ctxt, bytes, true);
		if (ret)
			break;

		/* Zero extend based on operand size */
		reg_data = vc_insn_get_reg(ctxt);
		if (!reg_data)
			return ES_DECODE_FAILED;

		memset(reg_data, 0, insn->opnd_bytes);

		memcpy(reg_data, ghcb->shared_buffer, bytes);
		break;

		/* MMIO Read w/ sign-extension */
	case 0xbe:
		bytes = 1;
		fallthrough;
	case 0xbf:
		if (!bytes)
			bytes = 2;

		ret = vc_do_mmio(ghcb, ctxt, bytes, true);
		if (ret)
			break;

		/* Sign extend based on operand size */
		reg_data = vc_insn_get_reg(ctxt);
		if (!reg_data)
			return ES_DECODE_FAILED;

		if (bytes == 1) {
			u8 *val = (u8 *)ghcb->shared_buffer;

			sign_byte = (*val & 0x80) ? 0xff : 0x00;
		} else {
			u16 *val = (u16 *)ghcb->shared_buffer;

			sign_byte = (*val & 0x8000) ? 0xff : 0x00;
		}
		memset(reg_data, sign_byte, insn->opnd_bytes);

		memcpy(reg_data, ghcb->shared_buffer, bytes);
		break;

	default:
		ret = ES_UNSUPPORTED;
	}

	return ret;
}

/*
 * The MOVS instruction has two memory operands, which raises the
 * problem that it is not known whether the access to the source or the
 * destination caused the #VC exception (and hence whether an MMIO read
 * or write operation needs to be emulated).
 *
 * Instead of playing games with walking page-tables and trying to guess
 * whether the source or destination is an MMIO range, split the move
 * into two operations, a read and a write with only one memory operand.
 * This will cause a nested #VC exception on the MMIO address which can
 * then be handled.
 *
 * This implementation has the benefit that it also supports MOVS where
 * source _and_ destination are MMIO regions.
 *
 * It will slow MOVS on MMIO down a lot, but in SEV-ES guests it is a
 * rare operation. If it turns out to be a performance problem the split
 * operations can be moved to memcpy_fromio() and memcpy_toio().
 */
static enum es_result vc_handle_mmio_movs(struct es_em_ctxt *ctxt,
					  unsigned int bytes)
{
	unsigned long ds_base, es_base;
	unsigned char *src, *dst;
	unsigned char buffer[8];
	enum es_result ret;
	bool rep;
	int off;

	ds_base = insn_get_seg_base(ctxt->regs, INAT_SEG_REG_DS);
	es_base = insn_get_seg_base(ctxt->regs, INAT_SEG_REG_ES);

	if (ds_base == -1L || es_base == -1L) {
		ctxt->fi.vector = X86_TRAP_GP;
		ctxt->fi.error_code = 0;
		return ES_EXCEPTION;
	}

	src = ds_base + (unsigned char *)ctxt->regs->si;
	dst = es_base + (unsigned char *)ctxt->regs->di;

	ret = vc_read_mem(ctxt, src, buffer, bytes);
	if (ret != ES_OK)
		return ret;

	ret = vc_write_mem(ctxt, dst, buffer, bytes);
	if (ret != ES_OK)
		return ret;

	if (ctxt->regs->flags & X86_EFLAGS_DF)
		off = -bytes;
	else
		off =  bytes;

	ctxt->regs->si += off;
	ctxt->regs->di += off;

	rep = insn_has_rep_prefix(&ctxt->insn);
	if (rep)
		ctxt->regs->cx -= 1;

	if (!rep || ctxt->regs->cx == 0)
		return ES_OK;
	else
		return ES_RETRY;
}

static enum es_result vc_handle_mmio(struct ghcb *ghcb,
				     struct es_em_ctxt *ctxt)
{
	struct insn *insn = &ctxt->insn;
	unsigned int bytes = 0;
	enum es_result ret;
	long *reg_data;

	switch (insn->opcode.bytes[0]) {
	/* MMIO Write */
	case 0x88:
		bytes = 1;
		fallthrough;
	case 0x89:
		if (!bytes)
			bytes = insn->opnd_bytes;

		reg_data = vc_insn_get_reg(ctxt);
		if (!reg_data)
			return ES_DECODE_FAILED;

		memcpy(ghcb->shared_buffer, reg_data, bytes);

		ret = vc_do_mmio(ghcb, ctxt, bytes, false);
		break;

	case 0xc6:
		bytes = 1;
		fallthrough;
	case 0xc7:
		if (!bytes)
			bytes = insn->opnd_bytes;

		memcpy(ghcb->shared_buffer, insn->immediate1.bytes, bytes);

		ret = vc_do_mmio(ghcb, ctxt, bytes, false);
		break;

		/* MMIO Read */
	case 0x8a:
		bytes = 1;
		fallthrough;
	case 0x8b:
		if (!bytes)
			bytes = insn->opnd_bytes;

		ret = vc_do_mmio(ghcb, ctxt, bytes, true);
		if (ret)
			break;

		reg_data = vc_insn_get_reg(ctxt);
		if (!reg_data)
			return ES_DECODE_FAILED;

		/* Zero-extend for 32-bit operation */
		if (bytes == 4)
			*reg_data = 0;

		memcpy(reg_data, ghcb->shared_buffer, bytes);
		break;

		/* MOVS instruction */
	case 0xa4:
		bytes = 1;
		fallthrough;
	case 0xa5:
		if (!bytes)
			bytes = insn->opnd_bytes;

		ret = vc_handle_mmio_movs(ctxt, bytes);
		break;
		/* Two-Byte Opcodes */
	case 0x0f:
		ret = vc_handle_mmio_twobyte_ops(ghcb, ctxt);
		break;
	default:
		ret = ES_UNSUPPORTED;
	}

	return ret;
}

static enum es_result vc_handle_dr7_write(struct ghcb *ghcb,
					  struct es_em_ctxt *ctxt)
{
	struct sev_es_runtime_data *data = this_cpu_read(runtime_data);
	long val, *reg = vc_insn_get_rm(ctxt);
	enum es_result ret;

	if (!reg)
		return ES_DECODE_FAILED;

	val = *reg;

	/* Upper 32 bits must be written as zeroes */
	if (val >> 32) {
		ctxt->fi.vector = X86_TRAP_GP;
		ctxt->fi.error_code = 0;
		return ES_EXCEPTION;
	}

	/* Clear out other reserved bits and set bit 10 */
	val = (val & 0xffff23ffL) | BIT(10);

	/* Early non-zero writes to DR7 are not supported */
	if (!data && (val & ~DR7_RESET_VALUE))
		return ES_UNSUPPORTED;

	/* Using a value of 0 for ExitInfo1 means RAX holds the value */
	ghcb_set_rax(ghcb, val);
	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_WRITE_DR7, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (data)
		data->dr7 = val;

	return ES_OK;
}

static enum es_result vc_handle_dr7_read(struct ghcb *ghcb,
					 struct es_em_ctxt *ctxt)
{
	struct sev_es_runtime_data *data = this_cpu_read(runtime_data);
	long *reg = vc_insn_get_rm(ctxt);

	if (!reg)
		return ES_DECODE_FAILED;

	if (data)
		*reg = data->dr7;
	else
		*reg = DR7_RESET_VALUE;

	return ES_OK;
}

static enum es_result vc_handle_wbinvd(struct ghcb *ghcb,
				       struct es_em_ctxt *ctxt)
{
	return sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_WBINVD, 0, 0);
}

static enum es_result vc_handle_rdpmc(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	enum es_result ret;

	ghcb_set_rcx(ghcb, ctxt->regs->cx);

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_RDPMC, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!(ghcb_rax_is_valid(ghcb) && ghcb_rdx_is_valid(ghcb)))
		return ES_VMM_ERROR;

	ctxt->regs->ax = ghcb->save.rax;
	ctxt->regs->dx = ghcb->save.rdx;

	return ES_OK;
}

static enum es_result vc_handle_monitor(struct ghcb *ghcb,
					struct es_em_ctxt *ctxt)
{
	/*
	 * Treat it as a NOP and do not leak a physical address to the
	 * hypervisor.
	 */
	return ES_OK;
}

static enum es_result vc_handle_mwait(struct ghcb *ghcb,
				      struct es_em_ctxt *ctxt)
{
	/* Treat the same as MONITOR/MONITORX */
	return ES_OK;
}

static enum es_result vc_handle_vmmcall(struct ghcb *ghcb,
					struct es_em_ctxt *ctxt)
{
	enum es_result ret;

	ghcb_set_rax(ghcb, ctxt->regs->ax);
	ghcb_set_cpl(ghcb, user_mode(ctxt->regs) ? 3 : 0);

	if (x86_platform.hyper.sev_es_hcall_prepare)
		x86_platform.hyper.sev_es_hcall_prepare(ghcb, ctxt->regs);

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_VMMCALL, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!ghcb_rax_is_valid(ghcb))
		return ES_VMM_ERROR;

	ctxt->regs->ax = ghcb->save.rax;

	/*
	 * Call sev_es_hcall_finish() after regs->ax is already set.
	 * This allows the hypervisor handler to overwrite it again if
	 * necessary.
	 */
	if (x86_platform.hyper.sev_es_hcall_finish &&
	    !x86_platform.hyper.sev_es_hcall_finish(ghcb, ctxt->regs))
		return ES_VMM_ERROR;

	return ES_OK;
}

static enum es_result vc_handle_trap_ac(struct ghcb *ghcb,
					struct es_em_ctxt *ctxt)
{
	/*
	 * Calling ecx_alignment_check() directly does not work, because it
	 * enables IRQs and the GHCB is active. Forward the exception and call
	 * it later from vc_forward_exception().
	 */
	ctxt->fi.vector = X86_TRAP_AC;
	ctxt->fi.error_code = 0;
	return ES_EXCEPTION;
}

static __always_inline void vc_handle_trap_db(struct pt_regs *regs)
{
	if (user_mode(regs))
		noist_exc_debug(regs);
	else
		exc_debug(regs);
}

static enum es_result vc_handle_exitcode(struct es_em_ctxt *ctxt,
					 struct ghcb *ghcb,
					 unsigned long exit_code)
{
	enum es_result result;

	switch (exit_code) {
	case SVM_EXIT_READ_DR7:
		result = vc_handle_dr7_read(ghcb, ctxt);
		break;
	case SVM_EXIT_WRITE_DR7:
		result = vc_handle_dr7_write(ghcb, ctxt);
		break;
	case SVM_EXIT_EXCP_BASE + X86_TRAP_AC:
		result = vc_handle_trap_ac(ghcb, ctxt);
		break;
	case SVM_EXIT_RDTSC:
	case SVM_EXIT_RDTSCP:
		result = vc_handle_rdtsc(ghcb, ctxt, exit_code);
		break;
	case SVM_EXIT_RDPMC:
		result = vc_handle_rdpmc(ghcb, ctxt);
		break;
	case SVM_EXIT_INVD:
		pr_err_ratelimited("#VC exception for INVD??? Seriously???\n");
		result = ES_UNSUPPORTED;
		break;
	case SVM_EXIT_CPUID:
		result = vc_handle_cpuid(ghcb, ctxt);
		break;
	case SVM_EXIT_IOIO:
		result = vc_handle_ioio(ghcb, ctxt);
		break;
	case SVM_EXIT_MSR:
		result = vc_handle_msr(ghcb, ctxt);
		break;
	case SVM_EXIT_VMMCALL:
		result = vc_handle_vmmcall(ghcb, ctxt);
		break;
	case SVM_EXIT_WBINVD:
		result = vc_handle_wbinvd(ghcb, ctxt);
		break;
	case SVM_EXIT_MONITOR:
		result = vc_handle_monitor(ghcb, ctxt);
		break;
	case SVM_EXIT_MWAIT:
		result = vc_handle_mwait(ghcb, ctxt);
		break;
	case SVM_EXIT_NPF:
		result = vc_handle_mmio(ghcb, ctxt);
		break;
	default:
		/*
		 * Unexpected #VC exception
		 */
		result = ES_UNSUPPORTED;
	}

	return result;
}

static __always_inline void vc_forward_exception(struct es_em_ctxt *ctxt)
{
	long error_code = ctxt->fi.error_code;
	int trapnr = ctxt->fi.vector;

	ctxt->regs->orig_ax = ctxt->fi.error_code;

	switch (trapnr) {
	case X86_TRAP_GP:
		exc_general_protection(ctxt->regs, error_code);
		break;
	case X86_TRAP_UD:
		exc_invalid_op(ctxt->regs);
		break;
	case X86_TRAP_AC:
		exc_alignment_check(ctxt->regs, error_code);
		break;
	default:
		pr_emerg("Unsupported exception in #VC instruction emulation - can't continue\n");
		BUG();
	}
}

static __always_inline bool on_vc_fallback_stack(struct pt_regs *regs)
{
	unsigned long sp = (unsigned long)regs;

	return (sp >= __this_cpu_ist_bottom_va(VC2) && sp < __this_cpu_ist_top_va(VC2));
}

/*
 * Main #VC exception handler. It is called when the entry code was able to
 * switch off the IST to a safe kernel stack.
 *
 * With the current implementation it is always possible to switch to a safe
 * stack because #VC exceptions only happen at known places, like intercepted
 * instructions or accesses to MMIO areas/IO ports. They can also happen with
 * code instrumentation when the hypervisor intercepts #DB, but the critical
 * paths are forbidden to be instrumented, so #DB exceptions currently also
 * only happen in safe places.
 */
DEFINE_IDTENTRY_VC_SAFE_STACK(exc_vmm_communication)
{
	struct sev_es_runtime_data *data = this_cpu_read(runtime_data);
	struct ghcb_state state;
	struct es_em_ctxt ctxt;
	enum es_result result;
	struct ghcb *ghcb;

	lockdep_assert_irqs_disabled();

	/*
	 * Handle #DB before calling into !noinstr code to avoid recursive #DB.
	 */
	if (error_code == SVM_EXIT_EXCP_BASE + X86_TRAP_DB) {
		vc_handle_trap_db(regs);
		return;
	}

	instrumentation_begin();

	/*
	 * This is invoked through an interrupt gate, so IRQs are disabled. The
	 * code below might walk page-tables for user or kernel addresses, so
	 * keep the IRQs disabled to protect us against concurrent TLB flushes.
	 */

	ghcb = sev_es_get_ghcb(&state);
	if (!ghcb) {
		/*
		 * Mark GHCBs inactive so that panic() is able to print the
		 * message.
		 */
		data->ghcb_active        = false;
		data->backup_ghcb_active = false;

		panic("Unable to handle #VC exception! GHCB and Backup GHCB are already in use");
	}

	vc_ghcb_invalidate(ghcb);
	result = vc_init_em_ctxt(&ctxt, regs, error_code);

	if (result == ES_OK)
		result = vc_handle_exitcode(&ctxt, ghcb, error_code);

	sev_es_put_ghcb(&state);

	/* Done - now check the result */
	switch (result) {
	case ES_OK:
		vc_finish_insn(&ctxt);
		break;
	case ES_UNSUPPORTED:
		pr_err_ratelimited("Unsupported exit-code 0x%02lx in early #VC exception (IP: 0x%lx)\n",
				   error_code, regs->ip);
		goto fail;
	case ES_VMM_ERROR:
		pr_err_ratelimited("Failure in communication with VMM (exit-code 0x%02lx IP: 0x%lx)\n",
				   error_code, regs->ip);
		goto fail;
	case ES_DECODE_FAILED:
		pr_err_ratelimited("Failed to decode instruction (exit-code 0x%02lx IP: 0x%lx)\n",
				   error_code, regs->ip);
		goto fail;
	case ES_EXCEPTION:
		vc_forward_exception(&ctxt);
		break;
	case ES_RETRY:
		/* Nothing to do */
		break;
	default:
		pr_emerg("Unknown result in %s():%d\n", __func__, result);
		/*
		 * Emulating the instruction which caused the #VC exception
		 * failed - can't continue so print debug information
		 */
		BUG();
	}

out:
	instrumentation_end();

	return;

fail:
	if (user_mode(regs)) {
		/*
		 * Do not kill the machine if user-space triggered the
		 * exception. Send SIGBUS instead and let user-space deal with
		 * it.
		 */
		force_sig_fault(SIGBUS, BUS_OBJERR, (void __user *)0);
	} else {
		pr_emerg("PANIC: Unhandled #VC exception in kernel space (result=%d)\n",
			 result);

		/* Show some debug info */
		show_regs(regs);

		/* Ask hypervisor to sev_es_terminate */
		sev_es_terminate(GHCB_SEV_ES_REASON_GENERAL_REQUEST);

		/* If that fails and we get here - just panic */
		panic("Returned from Terminate-Request to Hypervisor\n");
	}

	goto out;
}

/* This handler runs on the #VC fall-back stack. It can cause further #VC exceptions */
DEFINE_IDTENTRY_VC_IST(exc_vmm_communication)
{
	instrumentation_begin();
	panic("Can't handle #VC exception from unsupported context\n");
	instrumentation_end();
}

DEFINE_IDTENTRY_VC(exc_vmm_communication)
{
	if (likely(!on_vc_fallback_stack(regs)))
		safe_stack_exc_vmm_communication(regs, error_code);
	else
		ist_exc_vmm_communication(regs, error_code);
}

bool __init handle_vc_boot_ghcb(struct pt_regs *regs)
{
	unsigned long exit_code = regs->orig_ax;
	struct es_em_ctxt ctxt;
	enum es_result result;

	/* Do initial setup or terminate the guest */
	if (unlikely(boot_ghcb == NULL && !sev_es_setup_ghcb()))
		sev_es_terminate(GHCB_SEV_ES_REASON_GENERAL_REQUEST);

	vc_ghcb_invalidate(boot_ghcb);

	result = vc_init_em_ctxt(&ctxt, regs, exit_code);
	if (result == ES_OK)
		result = vc_handle_exitcode(&ctxt, boot_ghcb, exit_code);

	/* Done - now check the result */
	switch (result) {
	case ES_OK:
		vc_finish_insn(&ctxt);
		break;
	case ES_UNSUPPORTED:
		early_printk("PANIC: Unsupported exit-code 0x%02lx in early #VC exception (IP: 0x%lx)\n",
				exit_code, regs->ip);
		goto fail;
	case ES_VMM_ERROR:
		early_printk("PANIC: Failure in communication with VMM (exit-code 0x%02lx IP: 0x%lx)\n",
				exit_code, regs->ip);
		goto fail;
	case ES_DECODE_FAILED:
		early_printk("PANIC: Failed to decode instruction (exit-code 0x%02lx IP: 0x%lx)\n",
				exit_code, regs->ip);
		goto fail;
	case ES_EXCEPTION:
		vc_early_forward_exception(&ctxt);
		break;
	case ES_RETRY:
		/* Nothing to do */
		break;
	default:
		BUG();
	}

	return true;

fail:
	show_regs(regs);

	while (true)
		halt();
}
