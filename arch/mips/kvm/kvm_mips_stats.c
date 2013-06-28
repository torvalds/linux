/*
* This file is subject to the terms and conditions of the GNU General Public
* License.  See the file "COPYING" in the main directory of this archive
* for more details.
*
* KVM/MIPS: COP0 access histogram
*
* Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
* Authors: Sanjay Lal <sanjayl@kymasys.com>
*/

#include <linux/kvm_host.h>

char *kvm_mips_exit_types_str[MAX_KVM_MIPS_EXIT_TYPES] = {
	"WAIT",
	"CACHE",
	"Signal",
	"Interrupt",
	"COP0/1 Unusable",
	"TLB Mod",
	"TLB Miss (LD)",
	"TLB Miss (ST)",
	"Address Err (ST)",
	"Address Error (LD)",
	"System Call",
	"Reserved Inst",
	"Break Inst",
	"D-Cache Flushes",
};

char *kvm_cop0_str[N_MIPS_COPROC_REGS] = {
	"Index",
	"Random",
	"EntryLo0",
	"EntryLo1",
	"Context",
	"PG Mask",
	"Wired",
	"HWREna",
	"BadVAddr",
	"Count",
	"EntryHI",
	"Compare",
	"Status",
	"Cause",
	"EXC PC",
	"PRID",
	"Config",
	"LLAddr",
	"Watch Lo",
	"Watch Hi",
	"X Context",
	"Reserved",
	"Impl Dep",
	"Debug",
	"DEPC",
	"PerfCnt",
	"ErrCtl",
	"CacheErr",
	"TagLo",
	"TagHi",
	"ErrorEPC",
	"DESAVE"
};

int kvm_mips_dump_stats(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
	int i, j;

	printk("\nKVM VCPU[%d] COP0 Access Profile:\n", vcpu->vcpu_id);
	for (i = 0; i < N_MIPS_COPROC_REGS; i++) {
		for (j = 0; j < N_MIPS_COPROC_SEL; j++) {
			if (vcpu->arch.cop0->stat[i][j])
				printk("%s[%d]: %lu\n", kvm_cop0_str[i], j,
				       vcpu->arch.cop0->stat[i][j]);
		}
	}
#endif

	return 0;
}
