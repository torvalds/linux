// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2019 SUSE
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#define pr_fmt(fmt)	"SEV: " fmt

#include <linux/sched/debug.h>	/* For show_regs() */
#include <linux/percpu-defs.h>
#include <linux/cc_platform.h>
#include <linux/printk.h>
#include <linux/mm_types.h>
#include <linux/set_memory.h>
#include <linux/memblock.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <linux/efi.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/cpu_entry_area.h>
#include <asm/stacktrace.h>
#include <asm/sev.h>
#include <asm/insn-eval.h>
#include <asm/fpu/xcr.h>
#include <asm/processor.h>
#include <asm/realmode.h>
#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/svm.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/apic.h>
#include <asm/cpuid.h>
#include <asm/cmdline.h>

#define DR7_RESET_VALUE        0x400

/* AP INIT values as documented in the APM2  section "Processor Initialization State" */
#define AP_INIT_CS_LIMIT		0xffff
#define AP_INIT_DS_LIMIT		0xffff
#define AP_INIT_LDTR_LIMIT		0xffff
#define AP_INIT_GDTR_LIMIT		0xffff
#define AP_INIT_IDTR_LIMIT		0xffff
#define AP_INIT_TR_LIMIT		0xffff
#define AP_INIT_RFLAGS_DEFAULT		0x2
#define AP_INIT_DR6_DEFAULT		0xffff0ff0
#define AP_INIT_GPAT_DEFAULT		0x0007040600070406ULL
#define AP_INIT_XCR0_DEFAULT		0x1
#define AP_INIT_X87_FTW_DEFAULT		0x5555
#define AP_INIT_X87_FCW_DEFAULT		0x0040
#define AP_INIT_CR0_DEFAULT		0x60000010
#define AP_INIT_MXCSR_DEFAULT		0x1f80

/* For early boot hypervisor communication in SEV-ES enabled guests */
static struct ghcb boot_ghcb_page __bss_decrypted __aligned(PAGE_SIZE);

/*
 * Needs to be in the .data section because we need it NULL before bss is
 * cleared
 */
static struct ghcb *boot_ghcb __section(".data");

/* Bitmap of SEV features supported by the hypervisor */
static u64 sev_hv_features __ro_after_init;

/* #VC handler runtime per-CPU data */
struct sev_es_runtime_data {
	struct ghcb ghcb_page;

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

static DEFINE_PER_CPU(struct sev_es_save_area *, sev_vmsa);

struct sev_config {
	__u64 debug		: 1,
	      __reserved	: 63;
};

static struct sev_config sev_cfg __read_mostly;

static __always_inline bool on_vc_stack(struct pt_regs *regs)
{
	unsigned long sp = regs->sp;

	/* User-mode RSP is not trusted */
	if (user_mode(regs))
		return false;

	/* SYSCALL gap still has user-mode RSP */
	if (ip_within_syscall_gap(regs))
		return false;

	return ((sp >= __this_cpu_ist_bottom_va(VC)) && (sp < __this_cpu_ist_top_va(VC)));
}

/*
 * This function handles the case when an NMI is raised in the #VC
 * exception handler entry code, before the #VC handler has switched off
 * its IST stack. In this case, the IST entry for #VC must be adjusted,
 * so that any nested #VC exception will not overwrite the stack
 * contents of the interrupted #VC handler.
 *
 * The IST entry is adjusted unconditionally so that it can be also be
 * unconditionally adjusted back in __sev_es_ist_exit(). Otherwise a
 * nested sev_es_ist_exit() call may adjust back the IST entry too
 * early.
 *
 * The __sev_es_ist_enter() and __sev_es_ist_exit() functions always run
 * on the NMI IST stack, as they are only called from NMI handling code
 * right now.
 */
void noinstr __sev_es_ist_enter(struct pt_regs *regs)
{
	unsigned long old_ist, new_ist;

	/* Read old IST entry */
	new_ist = old_ist = __this_cpu_read(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC]);

	/*
	 * If NMI happened while on the #VC IST stack, set the new IST
	 * value below regs->sp, so that the interrupted stack frame is
	 * not overwritten by subsequent #VC exceptions.
	 */
	if (on_vc_stack(regs))
		new_ist = regs->sp;

	/*
	 * Reserve additional 8 bytes and store old IST value so this
	 * adjustment can be unrolled in __sev_es_ist_exit().
	 */
	new_ist -= sizeof(old_ist);
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

/*
 * Nothing shall interrupt this code path while holding the per-CPU
 * GHCB. The backup GHCB is only for NMIs interrupting this path.
 *
 * Callers must disable local interrupts around it.
 */
static noinstr struct ghcb *__sev_get_ghcb(struct ghcb_state *state)
{
	struct sev_es_runtime_data *data;
	struct ghcb *ghcb;

	WARN_ON(!irqs_disabled());

	data = this_cpu_read(runtime_data);
	ghcb = &data->ghcb_page;

	if (unlikely(data->ghcb_active)) {
		/* GHCB is already in use - save its contents */

		if (unlikely(data->backup_ghcb_active)) {
			/*
			 * Backup-GHCB is also already in use. There is no way
			 * to continue here so just kill the machine. To make
			 * panic() work, mark GHCBs inactive so that messages
			 * can be printed out.
			 */
			data->ghcb_active        = false;
			data->backup_ghcb_active = false;

			instrumentation_begin();
			panic("Unable to handle #VC exception! GHCB and Backup GHCB are already in use");
			instrumentation_end();
		}

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

static enum es_result __vc_decode_user_insn(struct es_em_ctxt *ctxt)
{
	char buffer[MAX_INSN_SIZE];
	int insn_bytes;

	insn_bytes = insn_fetch_from_user_inatomic(ctxt->regs, buffer);
	if (insn_bytes == 0) {
		/* Nothing could be copied */
		ctxt->fi.vector     = X86_TRAP_PF;
		ctxt->fi.error_code = X86_PF_INSTR | X86_PF_USER;
		ctxt->fi.cr2        = ctxt->regs->ip;
		return ES_EXCEPTION;
	} else if (insn_bytes == -EINVAL) {
		/* Effective RIP could not be calculated */
		ctxt->fi.vector     = X86_TRAP_GP;
		ctxt->fi.error_code = 0;
		ctxt->fi.cr2        = 0;
		return ES_EXCEPTION;
	}

	if (!insn_decode_from_regs(&ctxt->insn, ctxt->regs, buffer, insn_bytes))
		return ES_DECODE_FAILED;

	if (ctxt->insn.immediate.got)
		return ES_OK;
	else
		return ES_DECODE_FAILED;
}

static enum es_result __vc_decode_kern_insn(struct es_em_ctxt *ctxt)
{
	char buffer[MAX_INSN_SIZE];
	int res, ret;

	res = vc_fetch_insn_kernel(ctxt, buffer);
	if (res) {
		ctxt->fi.vector     = X86_TRAP_PF;
		ctxt->fi.error_code = X86_PF_INSTR;
		ctxt->fi.cr2        = ctxt->regs->ip;
		return ES_EXCEPTION;
	}

	ret = insn_decode(&ctxt->insn, buffer, MAX_INSN_SIZE, INSN_MODE_64);
	if (ret < 0)
		return ES_DECODE_FAILED;
	else
		return ES_OK;
}

static enum es_result vc_decode_insn(struct es_em_ctxt *ctxt)
{
	if (user_mode(ctxt->regs))
		return __vc_decode_user_insn(ctxt);
	else
		return __vc_decode_kern_insn(ctxt);
}

static enum es_result vc_write_mem(struct es_em_ctxt *ctxt,
				   char *dst, char *buf, size_t size)
{
	unsigned long error_code = X86_PF_PROT | X86_PF_WRITE;

	/*
	 * This function uses __put_user() independent of whether kernel or user
	 * memory is accessed. This works fine because __put_user() does no
	 * sanity checks of the pointer being accessed. All that it does is
	 * to report when the access failed.
	 *
	 * Also, this function runs in atomic context, so __put_user() is not
	 * allowed to sleep. The page-fault handler detects that it is running
	 * in atomic context and will not try to take mmap_sem and handle the
	 * fault, so additional pagefault_enable()/disable() calls are not
	 * needed.
	 *
	 * The access can't be done via copy_to_user() here because
	 * vc_write_mem() must not use string instructions to access unsafe
	 * memory. The reason is that MOVS is emulated by the #VC handler by
	 * splitting the move up into a read and a write and taking a nested #VC
	 * exception on whatever of them is the MMIO access. Using string
	 * instructions here would cause infinite nesting.
	 */
	switch (size) {
	case 1: {
		u8 d1;
		u8 __user *target = (u8 __user *)dst;

		memcpy(&d1, buf, 1);
		if (__put_user(d1, target))
			goto fault;
		break;
	}
	case 2: {
		u16 d2;
		u16 __user *target = (u16 __user *)dst;

		memcpy(&d2, buf, 2);
		if (__put_user(d2, target))
			goto fault;
		break;
	}
	case 4: {
		u32 d4;
		u32 __user *target = (u32 __user *)dst;

		memcpy(&d4, buf, 4);
		if (__put_user(d4, target))
			goto fault;
		break;
	}
	case 8: {
		u64 d8;
		u64 __user *target = (u64 __user *)dst;

		memcpy(&d8, buf, 8);
		if (__put_user(d8, target))
			goto fault;
		break;
	}
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

	/*
	 * This function uses __get_user() independent of whether kernel or user
	 * memory is accessed. This works fine because __get_user() does no
	 * sanity checks of the pointer being accessed. All that it does is
	 * to report when the access failed.
	 *
	 * Also, this function runs in atomic context, so __get_user() is not
	 * allowed to sleep. The page-fault handler detects that it is running
	 * in atomic context and will not try to take mmap_sem and handle the
	 * fault, so additional pagefault_enable()/disable() calls are not
	 * needed.
	 *
	 * The access can't be done via copy_from_user() here because
	 * vc_read_mem() must not use string instructions to access unsafe
	 * memory. The reason is that MOVS is emulated by the #VC handler by
	 * splitting the move up into a read and a write and taking a nested #VC
	 * exception on whatever of them is the MMIO access. Using string
	 * instructions here would cause infinite nesting.
	 */
	switch (size) {
	case 1: {
		u8 d1;
		u8 __user *s = (u8 __user *)src;

		if (__get_user(d1, s))
			goto fault;
		memcpy(buf, &d1, 1);
		break;
	}
	case 2: {
		u16 d2;
		u16 __user *s = (u16 __user *)src;

		if (__get_user(d2, s))
			goto fault;
		memcpy(buf, &d2, 2);
		break;
	}
	case 4: {
		u32 d4;
		u32 __user *s = (u32 __user *)src;

		if (__get_user(d4, s))
			goto fault;
		memcpy(buf, &d4, 4);
		break;
	}
	case 8: {
		u64 d8;
		u64 __user *s = (u64 __user *)src;
		if (__get_user(d8, s))
			goto fault;
		memcpy(buf, &d8, 8);
		break;
	}
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
#include "sev-shared.c"

static noinstr void __sev_put_ghcb(struct ghcb_state *state)
{
	struct sev_es_runtime_data *data;
	struct ghcb *ghcb;

	WARN_ON(!irqs_disabled());

	data = this_cpu_read(runtime_data);
	ghcb = &data->ghcb_page;

	if (state->ghcb) {
		/* Restore GHCB from Backup */
		*ghcb = *state->ghcb;
		data->backup_ghcb_active = false;
		state->ghcb = NULL;
	} else {
		/*
		 * Invalidate the GHCB so a VMGEXIT instruction issued
		 * from userspace won't appear to be valid.
		 */
		vc_ghcb_invalidate(ghcb);
		data->ghcb_active = false;
	}
}

void noinstr __sev_es_nmi_complete(void)
{
	struct ghcb_state state;
	struct ghcb *ghcb;

	ghcb = __sev_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_NMI_COMPLETE);
	ghcb_set_sw_exit_info_1(ghcb, 0);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa_nodebug(ghcb));
	VMGEXIT();

	__sev_put_ghcb(&state);
}

static u64 __init get_secrets_page(void)
{
	u64 pa_data = boot_params.cc_blob_address;
	struct cc_blob_sev_info info;
	void *map;

	/*
	 * The CC blob contains the address of the secrets page, check if the
	 * blob is present.
	 */
	if (!pa_data)
		return 0;

	map = early_memremap(pa_data, sizeof(info));
	if (!map) {
		pr_err("Unable to locate SNP secrets page: failed to map the Confidential Computing blob.\n");
		return 0;
	}
	memcpy(&info, map, sizeof(info));
	early_memunmap(map, sizeof(info));

	/* smoke-test the secrets page passed */
	if (!info.secrets_phys || info.secrets_len != PAGE_SIZE)
		return 0;

	return info.secrets_phys;
}

static u64 __init get_snp_jump_table_addr(void)
{
	struct snp_secrets_page_layout *layout;
	void __iomem *mem;
	u64 pa, addr;

	pa = get_secrets_page();
	if (!pa)
		return 0;

	mem = ioremap_encrypted(pa, PAGE_SIZE);
	if (!mem) {
		pr_err("Unable to locate AP jump table address: failed to map the SNP secrets page.\n");
		return 0;
	}

	layout = (__force struct snp_secrets_page_layout *)mem;

	addr = layout->os_area.ap_jump_table_pa;
	iounmap(mem);

	return addr;
}

static u64 __init get_jump_table_addr(void)
{
	struct ghcb_state state;
	unsigned long flags;
	struct ghcb *ghcb;
	u64 ret = 0;

	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return get_snp_jump_table_addr();

	local_irq_save(flags);

	ghcb = __sev_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_AP_JUMP_TABLE);
	ghcb_set_sw_exit_info_1(ghcb, SVM_VMGEXIT_GET_AP_JUMP_TABLE);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	if (ghcb_sw_exit_info_1_is_valid(ghcb) &&
	    ghcb_sw_exit_info_2_is_valid(ghcb))
		ret = ghcb->save.sw_exit_info_2;

	__sev_put_ghcb(&state);

	local_irq_restore(flags);

	return ret;
}

static void pvalidate_pages(unsigned long vaddr, unsigned int npages, bool validate)
{
	unsigned long vaddr_end;
	int rc;

	vaddr = vaddr & PAGE_MASK;
	vaddr_end = vaddr + (npages << PAGE_SHIFT);

	while (vaddr < vaddr_end) {
		rc = pvalidate(vaddr, RMP_PG_SIZE_4K, validate);
		if (WARN(rc, "Failed to validate address 0x%lx ret %d", vaddr, rc))
			sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PVALIDATE);

		vaddr = vaddr + PAGE_SIZE;
	}
}

static void __init early_set_pages_state(unsigned long paddr, unsigned int npages, enum psc_op op)
{
	unsigned long paddr_end;
	u64 val;

	paddr = paddr & PAGE_MASK;
	paddr_end = paddr + (npages << PAGE_SHIFT);

	while (paddr < paddr_end) {
		/*
		 * Use the MSR protocol because this function can be called before
		 * the GHCB is established.
		 */
		sev_es_wr_ghcb_msr(GHCB_MSR_PSC_REQ_GFN(paddr >> PAGE_SHIFT, op));
		VMGEXIT();

		val = sev_es_rd_ghcb_msr();

		if (WARN(GHCB_RESP_CODE(val) != GHCB_MSR_PSC_RESP,
			 "Wrong PSC response code: 0x%x\n",
			 (unsigned int)GHCB_RESP_CODE(val)))
			goto e_term;

		if (WARN(GHCB_MSR_PSC_RESP_VAL(val),
			 "Failed to change page state to '%s' paddr 0x%lx error 0x%llx\n",
			 op == SNP_PAGE_STATE_PRIVATE ? "private" : "shared",
			 paddr, GHCB_MSR_PSC_RESP_VAL(val)))
			goto e_term;

		paddr = paddr + PAGE_SIZE;
	}

	return;

e_term:
	sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PSC);
}

void __init early_snp_set_memory_private(unsigned long vaddr, unsigned long paddr,
					 unsigned int npages)
{
	/*
	 * This can be invoked in early boot while running identity mapped, so
	 * use an open coded check for SNP instead of using cc_platform_has().
	 * This eliminates worries about jump tables or checking boot_cpu_data
	 * in the cc_platform_has() function.
	 */
	if (!(sev_status & MSR_AMD64_SEV_SNP_ENABLED))
		return;

	 /*
	  * Ask the hypervisor to mark the memory pages as private in the RMP
	  * table.
	  */
	early_set_pages_state(paddr, npages, SNP_PAGE_STATE_PRIVATE);

	/* Validate the memory pages after they've been added in the RMP table. */
	pvalidate_pages(vaddr, npages, true);
}

void __init early_snp_set_memory_shared(unsigned long vaddr, unsigned long paddr,
					unsigned int npages)
{
	/*
	 * This can be invoked in early boot while running identity mapped, so
	 * use an open coded check for SNP instead of using cc_platform_has().
	 * This eliminates worries about jump tables or checking boot_cpu_data
	 * in the cc_platform_has() function.
	 */
	if (!(sev_status & MSR_AMD64_SEV_SNP_ENABLED))
		return;

	/* Invalidate the memory pages before they are marked shared in the RMP table. */
	pvalidate_pages(vaddr, npages, false);

	 /* Ask hypervisor to mark the memory pages shared in the RMP table. */
	early_set_pages_state(paddr, npages, SNP_PAGE_STATE_SHARED);
}

void __init snp_prep_memory(unsigned long paddr, unsigned int sz, enum psc_op op)
{
	unsigned long vaddr, npages;

	vaddr = (unsigned long)__va(paddr);
	npages = PAGE_ALIGN(sz) >> PAGE_SHIFT;

	if (op == SNP_PAGE_STATE_PRIVATE)
		early_snp_set_memory_private(vaddr, paddr, npages);
	else if (op == SNP_PAGE_STATE_SHARED)
		early_snp_set_memory_shared(vaddr, paddr, npages);
	else
		WARN(1, "invalid memory op %d\n", op);
}

static int vmgexit_psc(struct snp_psc_desc *desc)
{
	int cur_entry, end_entry, ret = 0;
	struct snp_psc_desc *data;
	struct ghcb_state state;
	struct es_em_ctxt ctxt;
	unsigned long flags;
	struct ghcb *ghcb;

	/*
	 * __sev_get_ghcb() needs to run with IRQs disabled because it is using
	 * a per-CPU GHCB.
	 */
	local_irq_save(flags);

	ghcb = __sev_get_ghcb(&state);
	if (!ghcb) {
		ret = 1;
		goto out_unlock;
	}

	/* Copy the input desc into GHCB shared buffer */
	data = (struct snp_psc_desc *)ghcb->shared_buffer;
	memcpy(ghcb->shared_buffer, desc, min_t(int, GHCB_SHARED_BUF_SIZE, sizeof(*desc)));

	/*
	 * As per the GHCB specification, the hypervisor can resume the guest
	 * before processing all the entries. Check whether all the entries
	 * are processed. If not, then keep retrying. Note, the hypervisor
	 * will update the data memory directly to indicate the status, so
	 * reference the data->hdr everywhere.
	 *
	 * The strategy here is to wait for the hypervisor to change the page
	 * state in the RMP table before guest accesses the memory pages. If the
	 * page state change was not successful, then later memory access will
	 * result in a crash.
	 */
	cur_entry = data->hdr.cur_entry;
	end_entry = data->hdr.end_entry;

	while (data->hdr.cur_entry <= data->hdr.end_entry) {
		ghcb_set_sw_scratch(ghcb, (u64)__pa(data));

		/* This will advance the shared buffer data points to. */
		ret = sev_es_ghcb_hv_call(ghcb, &ctxt, SVM_VMGEXIT_PSC, 0, 0);

		/*
		 * Page State Change VMGEXIT can pass error code through
		 * exit_info_2.
		 */
		if (WARN(ret || ghcb->save.sw_exit_info_2,
			 "SNP: PSC failed ret=%d exit_info_2=%llx\n",
			 ret, ghcb->save.sw_exit_info_2)) {
			ret = 1;
			goto out;
		}

		/* Verify that reserved bit is not set */
		if (WARN(data->hdr.reserved, "Reserved bit is set in the PSC header\n")) {
			ret = 1;
			goto out;
		}

		/*
		 * Sanity check that entry processing is not going backwards.
		 * This will happen only if hypervisor is tricking us.
		 */
		if (WARN(data->hdr.end_entry > end_entry || cur_entry > data->hdr.cur_entry,
"SNP: PSC processing going backward, end_entry %d (got %d) cur_entry %d (got %d)\n",
			 end_entry, data->hdr.end_entry, cur_entry, data->hdr.cur_entry)) {
			ret = 1;
			goto out;
		}
	}

out:
	__sev_put_ghcb(&state);

out_unlock:
	local_irq_restore(flags);

	return ret;
}

static void __set_pages_state(struct snp_psc_desc *data, unsigned long vaddr,
			      unsigned long vaddr_end, int op)
{
	struct psc_hdr *hdr;
	struct psc_entry *e;
	unsigned long pfn;
	int i;

	hdr = &data->hdr;
	e = data->entries;

	memset(data, 0, sizeof(*data));
	i = 0;

	while (vaddr < vaddr_end) {
		if (is_vmalloc_addr((void *)vaddr))
			pfn = vmalloc_to_pfn((void *)vaddr);
		else
			pfn = __pa(vaddr) >> PAGE_SHIFT;

		e->gfn = pfn;
		e->operation = op;
		hdr->end_entry = i;

		/*
		 * Current SNP implementation doesn't keep track of the RMP page
		 * size so use 4K for simplicity.
		 */
		e->pagesize = RMP_PG_SIZE_4K;

		vaddr = vaddr + PAGE_SIZE;
		e++;
		i++;
	}

	if (vmgexit_psc(data))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_PSC);
}

static void set_pages_state(unsigned long vaddr, unsigned int npages, int op)
{
	unsigned long vaddr_end, next_vaddr;
	struct snp_psc_desc *desc;

	desc = kmalloc(sizeof(*desc), GFP_KERNEL_ACCOUNT);
	if (!desc)
		panic("SNP: failed to allocate memory for PSC descriptor\n");

	vaddr = vaddr & PAGE_MASK;
	vaddr_end = vaddr + (npages << PAGE_SHIFT);

	while (vaddr < vaddr_end) {
		/* Calculate the last vaddr that fits in one struct snp_psc_desc. */
		next_vaddr = min_t(unsigned long, vaddr_end,
				   (VMGEXIT_PSC_MAX_ENTRY * PAGE_SIZE) + vaddr);

		__set_pages_state(desc, vaddr, next_vaddr, op);

		vaddr = next_vaddr;
	}

	kfree(desc);
}

void snp_set_memory_shared(unsigned long vaddr, unsigned int npages)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	pvalidate_pages(vaddr, npages, false);

	set_pages_state(vaddr, npages, SNP_PAGE_STATE_SHARED);
}

void snp_set_memory_private(unsigned long vaddr, unsigned int npages)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	set_pages_state(vaddr, npages, SNP_PAGE_STATE_PRIVATE);

	pvalidate_pages(vaddr, npages, true);
}

static int snp_set_vmsa(void *va, bool vmsa)
{
	u64 attrs;

	/*
	 * Running at VMPL0 allows the kernel to change the VMSA bit for a page
	 * using the RMPADJUST instruction. However, for the instruction to
	 * succeed it must target the permissions of a lesser privileged
	 * (higher numbered) VMPL level, so use VMPL1 (refer to the RMPADJUST
	 * instruction in the AMD64 APM Volume 3).
	 */
	attrs = 1;
	if (vmsa)
		attrs |= RMPADJUST_VMSA_PAGE_BIT;

	return rmpadjust((unsigned long)va, RMP_PG_SIZE_4K, attrs);
}

#define __ATTR_BASE		(SVM_SELECTOR_P_MASK | SVM_SELECTOR_S_MASK)
#define INIT_CS_ATTRIBS		(__ATTR_BASE | SVM_SELECTOR_READ_MASK | SVM_SELECTOR_CODE_MASK)
#define INIT_DS_ATTRIBS		(__ATTR_BASE | SVM_SELECTOR_WRITE_MASK)

#define INIT_LDTR_ATTRIBS	(SVM_SELECTOR_P_MASK | 2)
#define INIT_TR_ATTRIBS		(SVM_SELECTOR_P_MASK | 3)

static void *snp_alloc_vmsa_page(void)
{
	struct page *p;

	/*
	 * Allocate VMSA page to work around the SNP erratum where the CPU will
	 * incorrectly signal an RMP violation #PF if a large page (2MB or 1GB)
	 * collides with the RMP entry of VMSA page. The recommended workaround
	 * is to not use a large page.
	 *
	 * Allocate an 8k page which is also 8k-aligned.
	 */
	p = alloc_pages(GFP_KERNEL_ACCOUNT | __GFP_ZERO, 1);
	if (!p)
		return NULL;

	split_page(p, 1);

	/* Free the first 4k. This page may be 2M/1G aligned and cannot be used. */
	__free_page(p);

	return page_address(p + 1);
}

static void snp_cleanup_vmsa(struct sev_es_save_area *vmsa)
{
	int err;

	err = snp_set_vmsa(vmsa, false);
	if (err)
		pr_err("clear VMSA page failed (%u), leaking page\n", err);
	else
		free_page((unsigned long)vmsa);
}

static int wakeup_cpu_via_vmgexit(int apic_id, unsigned long start_ip)
{
	struct sev_es_save_area *cur_vmsa, *vmsa;
	struct ghcb_state state;
	unsigned long flags;
	struct ghcb *ghcb;
	u8 sipi_vector;
	int cpu, ret;
	u64 cr4;

	/*
	 * The hypervisor SNP feature support check has happened earlier, just check
	 * the AP_CREATION one here.
	 */
	if (!(sev_hv_features & GHCB_HV_FT_SNP_AP_CREATION))
		return -EOPNOTSUPP;

	/*
	 * Verify the desired start IP against the known trampoline start IP
	 * to catch any future new trampolines that may be introduced that
	 * would require a new protected guest entry point.
	 */
	if (WARN_ONCE(start_ip != real_mode_header->trampoline_start,
		      "Unsupported SNP start_ip: %lx\n", start_ip))
		return -EINVAL;

	/* Override start_ip with known protected guest start IP */
	start_ip = real_mode_header->sev_es_trampoline_start;

	/* Find the logical CPU for the APIC ID */
	for_each_present_cpu(cpu) {
		if (arch_match_cpu_phys_id(cpu, apic_id))
			break;
	}
	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	cur_vmsa = per_cpu(sev_vmsa, cpu);

	/*
	 * A new VMSA is created each time because there is no guarantee that
	 * the current VMSA is the kernels or that the vCPU is not running. If
	 * an attempt was done to use the current VMSA with a running vCPU, a
	 * #VMEXIT of that vCPU would wipe out all of the settings being done
	 * here.
	 */
	vmsa = (struct sev_es_save_area *)snp_alloc_vmsa_page();
	if (!vmsa)
		return -ENOMEM;

	/* CR4 should maintain the MCE value */
	cr4 = native_read_cr4() & X86_CR4_MCE;

	/* Set the CS value based on the start_ip converted to a SIPI vector */
	sipi_vector		= (start_ip >> 12);
	vmsa->cs.base		= sipi_vector << 12;
	vmsa->cs.limit		= AP_INIT_CS_LIMIT;
	vmsa->cs.attrib		= INIT_CS_ATTRIBS;
	vmsa->cs.selector	= sipi_vector << 8;

	/* Set the RIP value based on start_ip */
	vmsa->rip		= start_ip & 0xfff;

	/* Set AP INIT defaults as documented in the APM */
	vmsa->ds.limit		= AP_INIT_DS_LIMIT;
	vmsa->ds.attrib		= INIT_DS_ATTRIBS;
	vmsa->es		= vmsa->ds;
	vmsa->fs		= vmsa->ds;
	vmsa->gs		= vmsa->ds;
	vmsa->ss		= vmsa->ds;

	vmsa->gdtr.limit	= AP_INIT_GDTR_LIMIT;
	vmsa->ldtr.limit	= AP_INIT_LDTR_LIMIT;
	vmsa->ldtr.attrib	= INIT_LDTR_ATTRIBS;
	vmsa->idtr.limit	= AP_INIT_IDTR_LIMIT;
	vmsa->tr.limit		= AP_INIT_TR_LIMIT;
	vmsa->tr.attrib		= INIT_TR_ATTRIBS;

	vmsa->cr4		= cr4;
	vmsa->cr0		= AP_INIT_CR0_DEFAULT;
	vmsa->dr7		= DR7_RESET_VALUE;
	vmsa->dr6		= AP_INIT_DR6_DEFAULT;
	vmsa->rflags		= AP_INIT_RFLAGS_DEFAULT;
	vmsa->g_pat		= AP_INIT_GPAT_DEFAULT;
	vmsa->xcr0		= AP_INIT_XCR0_DEFAULT;
	vmsa->mxcsr		= AP_INIT_MXCSR_DEFAULT;
	vmsa->x87_ftw		= AP_INIT_X87_FTW_DEFAULT;
	vmsa->x87_fcw		= AP_INIT_X87_FCW_DEFAULT;

	/* SVME must be set. */
	vmsa->efer		= EFER_SVME;

	/*
	 * Set the SNP-specific fields for this VMSA:
	 *   VMPL level
	 *   SEV_FEATURES (matches the SEV STATUS MSR right shifted 2 bits)
	 */
	vmsa->vmpl		= 0;
	vmsa->sev_features	= sev_status >> 2;

	/* Switch the page over to a VMSA page now that it is initialized */
	ret = snp_set_vmsa(vmsa, true);
	if (ret) {
		pr_err("set VMSA page failed (%u)\n", ret);
		free_page((unsigned long)vmsa);

		return -EINVAL;
	}

	/* Issue VMGEXIT AP Creation NAE event */
	local_irq_save(flags);

	ghcb = __sev_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	ghcb_set_rax(ghcb, vmsa->sev_features);
	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_AP_CREATION);
	ghcb_set_sw_exit_info_1(ghcb, ((u64)apic_id << 32) | SVM_VMGEXIT_AP_CREATE);
	ghcb_set_sw_exit_info_2(ghcb, __pa(vmsa));

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	if (!ghcb_sw_exit_info_1_is_valid(ghcb) ||
	    lower_32_bits(ghcb->save.sw_exit_info_1)) {
		pr_err("SNP AP Creation error\n");
		ret = -EINVAL;
	}

	__sev_put_ghcb(&state);

	local_irq_restore(flags);

	/* Perform cleanup if there was an error */
	if (ret) {
		snp_cleanup_vmsa(vmsa);
		vmsa = NULL;
	}

	/* Free up any previous VMSA page */
	if (cur_vmsa)
		snp_cleanup_vmsa(cur_vmsa);

	/* Record the current VMSA page */
	per_cpu(sev_vmsa, cpu) = vmsa;

	return ret;
}

void snp_set_wakeup_secondary_cpu(void)
{
	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return;

	/*
	 * Always set this override if SNP is enabled. This makes it the
	 * required method to start APs under SNP. If the hypervisor does
	 * not support AP creation, then no APs will be started.
	 */
	apic->wakeup_secondary_cpu = wakeup_cpu_via_vmgexit;
}

int __init sev_es_setup_ap_jump_table(struct real_mode_header *rmh)
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

	if (!cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
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

static void snp_register_per_cpu_ghcb(void)
{
	struct sev_es_runtime_data *data;
	struct ghcb *ghcb;

	data = this_cpu_read(runtime_data);
	ghcb = &data->ghcb_page;

	snp_register_ghcb_early(__pa(ghcb));
}

void setup_ghcb(void)
{
	if (!cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		return;

	/* First make sure the hypervisor talks a supported protocol. */
	if (!sev_es_negotiate_protocol())
		sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_GEN_REQ);

	/*
	 * Check whether the runtime #VC exception handler is active. It uses
	 * the per-CPU GHCB page which is set up by sev_es_init_vc_handling().
	 *
	 * If SNP is active, register the per-CPU GHCB page so that the runtime
	 * exception handler can use it.
	 */
	if (initial_vc_handler == (unsigned long)kernel_exc_vmm_communication) {
		if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
			snp_register_per_cpu_ghcb();

		return;
	}

	/*
	 * Clear the boot_ghcb. The first exception comes in before the bss
	 * section is cleared.
	 */
	memset(&boot_ghcb_page, 0, PAGE_SIZE);

	/* Alright - Make the boot-ghcb public */
	boot_ghcb = &boot_ghcb_page;

	/* SNP guest requires that GHCB GPA must be registered. */
	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		snp_register_ghcb_early(__pa(&boot_ghcb_page));
}

#ifdef CONFIG_HOTPLUG_CPU
static void sev_es_ap_hlt_loop(void)
{
	struct ghcb_state state;
	struct ghcb *ghcb;

	ghcb = __sev_get_ghcb(&state);

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

	__sev_put_ghcb(&state);
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

	if (!cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		return;

	if (!sev_es_check_cpu_features())
		panic("SEV-ES CPU Features missing");

	/*
	 * SNP is supported in v2 of the GHCB spec which mandates support for HV
	 * features.
	 */
	if (cc_platform_has(CC_ATTR_GUEST_SEV_SNP)) {
		sev_hv_features = get_hv_features();

		if (!(sev_hv_features & GHCB_HV_FT_SNP))
			sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SNP_UNSUPPORTED);
	}

	/* Enable SEV-ES special handling */
	static_branch_enable(&sev_es_enable_key);

	/* Initialize per-cpu GHCB pages */
	for_each_possible_cpu(cpu) {
		alloc_runtime_data(cpu);
		init_ghcb(cpu);
	}

	sev_es_setup_play_dead();

	/* Secondary CPUs use the runtime #VC handler */
	initial_vc_handler = (unsigned long)kernel_exc_vmm_communication;
}

static void __init vc_early_forward_exception(struct es_em_ctxt *ctxt)
{
	int trapnr = ctxt->fi.vector;

	if (trapnr == X86_TRAP_PF)
		native_write_cr2(ctxt->fi.cr2);

	ctxt->regs->orig_ax = ctxt->fi.error_code;
	do_early_exception(ctxt->regs, trapnr);
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

static enum es_result vc_handle_mmio(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	struct insn *insn = &ctxt->insn;
	enum insn_mmio_type mmio;
	unsigned int bytes = 0;
	enum es_result ret;
	u8 sign_byte;
	long *reg_data;

	mmio = insn_decode_mmio(insn, &bytes);
	if (mmio == INSN_MMIO_DECODE_FAILED)
		return ES_DECODE_FAILED;

	if (mmio != INSN_MMIO_WRITE_IMM && mmio != INSN_MMIO_MOVS) {
		reg_data = insn_get_modrm_reg_ptr(insn, ctxt->regs);
		if (!reg_data)
			return ES_DECODE_FAILED;
	}

	switch (mmio) {
	case INSN_MMIO_WRITE:
		memcpy(ghcb->shared_buffer, reg_data, bytes);
		ret = vc_do_mmio(ghcb, ctxt, bytes, false);
		break;
	case INSN_MMIO_WRITE_IMM:
		memcpy(ghcb->shared_buffer, insn->immediate1.bytes, bytes);
		ret = vc_do_mmio(ghcb, ctxt, bytes, false);
		break;
	case INSN_MMIO_READ:
		ret = vc_do_mmio(ghcb, ctxt, bytes, true);
		if (ret)
			break;

		/* Zero-extend for 32-bit operation */
		if (bytes == 4)
			*reg_data = 0;

		memcpy(reg_data, ghcb->shared_buffer, bytes);
		break;
	case INSN_MMIO_READ_ZERO_EXTEND:
		ret = vc_do_mmio(ghcb, ctxt, bytes, true);
		if (ret)
			break;

		/* Zero extend based on operand size */
		memset(reg_data, 0, insn->opnd_bytes);
		memcpy(reg_data, ghcb->shared_buffer, bytes);
		break;
	case INSN_MMIO_READ_SIGN_EXTEND:
		ret = vc_do_mmio(ghcb, ctxt, bytes, true);
		if (ret)
			break;

		if (bytes == 1) {
			u8 *val = (u8 *)ghcb->shared_buffer;

			sign_byte = (*val & 0x80) ? 0xff : 0x00;
		} else {
			u16 *val = (u16 *)ghcb->shared_buffer;

			sign_byte = (*val & 0x8000) ? 0xff : 0x00;
		}

		/* Sign extend based on operand size */
		memset(reg_data, sign_byte, insn->opnd_bytes);
		memcpy(reg_data, ghcb->shared_buffer, bytes);
		break;
	case INSN_MMIO_MOVS:
		ret = vc_handle_mmio_movs(ctxt, bytes);
		break;
	default:
		ret = ES_UNSUPPORTED;
		break;
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
	case X86_TRAP_PF:
		write_cr2(ctxt->fi.cr2);
		exc_page_fault(ctxt->regs, error_code);
		break;
	case X86_TRAP_AC:
		exc_alignment_check(ctxt->regs, error_code);
		break;
	default:
		pr_emerg("Unsupported exception in #VC instruction emulation - can't continue\n");
		BUG();
	}
}

static __always_inline bool is_vc2_stack(unsigned long sp)
{
	return (sp >= __this_cpu_ist_bottom_va(VC2) && sp < __this_cpu_ist_top_va(VC2));
}

static __always_inline bool vc_from_invalid_context(struct pt_regs *regs)
{
	unsigned long sp, prev_sp;

	sp      = (unsigned long)regs;
	prev_sp = regs->sp;

	/*
	 * If the code was already executing on the VC2 stack when the #VC
	 * happened, let it proceed to the normal handling routine. This way the
	 * code executing on the VC2 stack can cause #VC exceptions to get handled.
	 */
	return is_vc2_stack(sp) && !is_vc2_stack(prev_sp);
}

static bool vc_raw_handle_exception(struct pt_regs *regs, unsigned long error_code)
{
	struct ghcb_state state;
	struct es_em_ctxt ctxt;
	enum es_result result;
	struct ghcb *ghcb;
	bool ret = true;

	ghcb = __sev_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	result = vc_init_em_ctxt(&ctxt, regs, error_code);

	if (result == ES_OK)
		result = vc_handle_exitcode(&ctxt, ghcb, error_code);

	__sev_put_ghcb(&state);

	/* Done - now check the result */
	switch (result) {
	case ES_OK:
		vc_finish_insn(&ctxt);
		break;
	case ES_UNSUPPORTED:
		pr_err_ratelimited("Unsupported exit-code 0x%02lx in #VC exception (IP: 0x%lx)\n",
				   error_code, regs->ip);
		ret = false;
		break;
	case ES_VMM_ERROR:
		pr_err_ratelimited("Failure in communication with VMM (exit-code 0x%02lx IP: 0x%lx)\n",
				   error_code, regs->ip);
		ret = false;
		break;
	case ES_DECODE_FAILED:
		pr_err_ratelimited("Failed to decode instruction (exit-code 0x%02lx IP: 0x%lx)\n",
				   error_code, regs->ip);
		ret = false;
		break;
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

	return ret;
}

static __always_inline bool vc_is_db(unsigned long error_code)
{
	return error_code == SVM_EXIT_EXCP_BASE + X86_TRAP_DB;
}

/*
 * Runtime #VC exception handler when raised from kernel mode. Runs in NMI mode
 * and will panic when an error happens.
 */
DEFINE_IDTENTRY_VC_KERNEL(exc_vmm_communication)
{
	irqentry_state_t irq_state;

	/*
	 * With the current implementation it is always possible to switch to a
	 * safe stack because #VC exceptions only happen at known places, like
	 * intercepted instructions or accesses to MMIO areas/IO ports. They can
	 * also happen with code instrumentation when the hypervisor intercepts
	 * #DB, but the critical paths are forbidden to be instrumented, so #DB
	 * exceptions currently also only happen in safe places.
	 *
	 * But keep this here in case the noinstr annotations are violated due
	 * to bug elsewhere.
	 */
	if (unlikely(vc_from_invalid_context(regs))) {
		instrumentation_begin();
		panic("Can't handle #VC exception from unsupported context\n");
		instrumentation_end();
	}

	/*
	 * Handle #DB before calling into !noinstr code to avoid recursive #DB.
	 */
	if (vc_is_db(error_code)) {
		exc_debug(regs);
		return;
	}

	irq_state = irqentry_nmi_enter(regs);

	instrumentation_begin();

	if (!vc_raw_handle_exception(regs, error_code)) {
		/* Show some debug info */
		show_regs(regs);

		/* Ask hypervisor to sev_es_terminate */
		sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_GEN_REQ);

		/* If that fails and we get here - just panic */
		panic("Returned from Terminate-Request to Hypervisor\n");
	}

	instrumentation_end();
	irqentry_nmi_exit(regs, irq_state);
}

/*
 * Runtime #VC exception handler when raised from user mode. Runs in IRQ mode
 * and will kill the current task with SIGBUS when an error happens.
 */
DEFINE_IDTENTRY_VC_USER(exc_vmm_communication)
{
	/*
	 * Handle #DB before calling into !noinstr code to avoid recursive #DB.
	 */
	if (vc_is_db(error_code)) {
		noist_exc_debug(regs);
		return;
	}

	irqentry_enter_from_user_mode(regs);
	instrumentation_begin();

	if (!vc_raw_handle_exception(regs, error_code)) {
		/*
		 * Do not kill the machine if user-space triggered the
		 * exception. Send SIGBUS instead and let user-space deal with
		 * it.
		 */
		force_sig_fault(SIGBUS, BUS_OBJERR, (void __user *)0);
	}

	instrumentation_end();
	irqentry_exit_to_user_mode(regs);
}

bool __init handle_vc_boot_ghcb(struct pt_regs *regs)
{
	unsigned long exit_code = regs->orig_ax;
	struct es_em_ctxt ctxt;
	enum es_result result;

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

	sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SEV_ES_GEN_REQ);
}

/*
 * Initial set up of SNP relies on information provided by the
 * Confidential Computing blob, which can be passed to the kernel
 * in the following ways, depending on how it is booted:
 *
 * - when booted via the boot/decompress kernel:
 *   - via boot_params
 *
 * - when booted directly by firmware/bootloader (e.g. CONFIG_PVH):
 *   - via a setup_data entry, as defined by the Linux Boot Protocol
 *
 * Scan for the blob in that order.
 */
static __init struct cc_blob_sev_info *find_cc_blob(struct boot_params *bp)
{
	struct cc_blob_sev_info *cc_info;

	/* Boot kernel would have passed the CC blob via boot_params. */
	if (bp->cc_blob_address) {
		cc_info = (struct cc_blob_sev_info *)(unsigned long)bp->cc_blob_address;
		goto found_cc_info;
	}

	/*
	 * If kernel was booted directly, without the use of the
	 * boot/decompression kernel, the CC blob may have been passed via
	 * setup_data instead.
	 */
	cc_info = find_cc_blob_setup_data(bp);
	if (!cc_info)
		return NULL;

found_cc_info:
	if (cc_info->magic != CC_BLOB_SEV_HDR_MAGIC)
		snp_abort();

	return cc_info;
}

bool __init snp_init(struct boot_params *bp)
{
	struct cc_blob_sev_info *cc_info;

	if (!bp)
		return false;

	cc_info = find_cc_blob(bp);
	if (!cc_info)
		return false;

	setup_cpuid_table(cc_info);

	/*
	 * The CC blob will be used later to access the secrets page. Cache
	 * it here like the boot kernel does.
	 */
	bp->cc_blob_address = (u32)(unsigned long)cc_info;

	return true;
}

void __init __noreturn snp_abort(void)
{
	sev_es_terminate(SEV_TERM_SET_GEN, GHCB_SNP_UNSUPPORTED);
}

static void dump_cpuid_table(void)
{
	const struct snp_cpuid_table *cpuid_table = snp_cpuid_get_table();
	int i = 0;

	pr_info("count=%d reserved=0x%x reserved2=0x%llx\n",
		cpuid_table->count, cpuid_table->__reserved1, cpuid_table->__reserved2);

	for (i = 0; i < SNP_CPUID_COUNT_MAX; i++) {
		const struct snp_cpuid_fn *fn = &cpuid_table->fn[i];

		pr_info("index=%3d fn=0x%08x subfn=0x%08x: eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x xcr0_in=0x%016llx xss_in=0x%016llx reserved=0x%016llx\n",
			i, fn->eax_in, fn->ecx_in, fn->eax, fn->ebx, fn->ecx,
			fn->edx, fn->xcr0_in, fn->xss_in, fn->__reserved);
	}
}

/*
 * It is useful from an auditing/testing perspective to provide an easy way
 * for the guest owner to know that the CPUID table has been initialized as
 * expected, but that initialization happens too early in boot to print any
 * sort of indicator, and there's not really any other good place to do it,
 * so do it here.
 */
static int __init report_cpuid_table(void)
{
	const struct snp_cpuid_table *cpuid_table = snp_cpuid_get_table();

	if (!cpuid_table->count)
		return 0;

	pr_info("Using SNP CPUID table, %d entries present.\n",
		cpuid_table->count);

	if (sev_cfg.debug)
		dump_cpuid_table();

	return 0;
}
arch_initcall(report_cpuid_table);

static int __init init_sev_config(char *str)
{
	char *s;

	while ((s = strsep(&str, ","))) {
		if (!strcmp(s, "debug")) {
			sev_cfg.debug = true;
			continue;
		}

		pr_info("SEV command-line option '%s' was not recognized\n", s);
	}

	return 1;
}
__setup("sev=", init_sev_config);

int snp_issue_guest_request(u64 exit_code, struct snp_req_data *input, unsigned long *fw_err)
{
	struct ghcb_state state;
	struct es_em_ctxt ctxt;
	unsigned long flags;
	struct ghcb *ghcb;
	int ret;

	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return -ENODEV;

	if (!fw_err)
		return -EINVAL;

	/*
	 * __sev_get_ghcb() needs to run with IRQs disabled because it is using
	 * a per-CPU GHCB.
	 */
	local_irq_save(flags);

	ghcb = __sev_get_ghcb(&state);
	if (!ghcb) {
		ret = -EIO;
		goto e_restore_irq;
	}

	vc_ghcb_invalidate(ghcb);

	if (exit_code == SVM_VMGEXIT_EXT_GUEST_REQUEST) {
		ghcb_set_rax(ghcb, input->data_gpa);
		ghcb_set_rbx(ghcb, input->data_npages);
	}

	ret = sev_es_ghcb_hv_call(ghcb, &ctxt, exit_code, input->req_gpa, input->resp_gpa);
	if (ret)
		goto e_put;

	if (ghcb->save.sw_exit_info_2) {
		/* Number of expected pages are returned in RBX */
		if (exit_code == SVM_VMGEXIT_EXT_GUEST_REQUEST &&
		    ghcb->save.sw_exit_info_2 == SNP_GUEST_REQ_INVALID_LEN)
			input->data_npages = ghcb_get_rbx(ghcb);

		*fw_err = ghcb->save.sw_exit_info_2;

		ret = -EIO;
	}

e_put:
	__sev_put_ghcb(&state);
e_restore_irq:
	local_irq_restore(flags);

	return ret;
}
EXPORT_SYMBOL_GPL(snp_issue_guest_request);

static struct platform_device sev_guest_device = {
	.name		= "sev-guest",
	.id		= -1,
};

static int __init snp_init_platform_device(void)
{
	struct sev_guest_platform_data data;
	u64 gpa;

	if (!cc_platform_has(CC_ATTR_GUEST_SEV_SNP))
		return -ENODEV;

	gpa = get_secrets_page();
	if (!gpa)
		return -ENODEV;

	data.secrets_gpa = gpa;
	if (platform_device_add_data(&sev_guest_device, &data, sizeof(data)))
		return -ENODEV;

	if (platform_device_register(&sev_guest_device))
		return -ENODEV;

	pr_info("SNP guest platform device initialized.\n");
	return 0;
}
device_initcall(snp_init_platform_device);
