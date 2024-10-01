// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kbuild.h>
#include <nvhe/memory.h>
#include <nvhe/pkvm.h>
#include <nvhe/trace.h>

int main(void)
{
	DEFINE(STRUCT_HYP_PAGE_SIZE,	sizeof(struct hyp_page));
	DEFINE(PKVM_HYP_VM_SIZE,	sizeof(struct pkvm_hyp_vm));
	DEFINE(PKVM_HYP_VCPU_SIZE,	sizeof(struct pkvm_hyp_vcpu));
#ifdef CONFIG_TRACING
	DEFINE(STRUCT_HYP_BUFFER_PAGE_SIZE,	sizeof(struct hyp_buffer_page));
#endif
	return 0;
}
