// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kbuild.h>
#include <nvhe/memory.h>
#include <nvhe/pkvm.h>

int main(void)
{
	DEFINE(STRUCT_HYP_PAGE_SIZE,	sizeof(struct hyp_page));
	DEFINE(PKVM_HYP_VM_SIZE,	sizeof(struct pkvm_hyp_vm));
	DEFINE(PKVM_HYP_VCPU_SIZE,	sizeof(struct pkvm_hyp_vcpu));
	return 0;
}
