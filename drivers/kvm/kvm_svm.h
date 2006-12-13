#ifndef __KVM_SVM_H
#define __KVM_SVM_H

#include <linux/types.h>
#include <linux/list.h>
#include <asm/msr.h>

#include "svm.h"
#include "kvm.h"

static const u32 host_save_msrs[] = {
#ifdef CONFIG_X86_64
	MSR_STAR, MSR_LSTAR, MSR_CSTAR, MSR_SYSCALL_MASK, MSR_KERNEL_GS_BASE,
	MSR_FS_BASE, MSR_GS_BASE,
#endif
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_IA32_DEBUGCTLMSR, /*MSR_IA32_LASTBRANCHFROMIP,
	MSR_IA32_LASTBRANCHTOIP, MSR_IA32_LASTINTFROMIP,MSR_IA32_LASTINTTOIP,*/
};

#define NR_HOST_SAVE_MSRS (sizeof(host_save_msrs) / sizeof(*host_save_msrs))
#define NUM_DB_REGS 4

struct vcpu_svm {
	struct vmcb *vmcb;
	unsigned long vmcb_pa;
	struct svm_cpu_data *svm_data;
	uint64_t asid_generation;

	unsigned long cr0;
	unsigned long cr4;
	unsigned long db_regs[NUM_DB_REGS];

	u64 next_rip;

	u64 host_msrs[NR_HOST_SAVE_MSRS];
	unsigned long host_cr2;
	unsigned long host_db_regs[NUM_DB_REGS];
	unsigned long host_dr6;
	unsigned long host_dr7;
};

#endif

