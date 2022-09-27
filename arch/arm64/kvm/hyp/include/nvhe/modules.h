#include <asm/kvm_pgtable.h>

#define HCALL_HANDLED 0
#define HCALL_UNHANDLED -1

#ifdef CONFIG_MODULES
int __pkvm_init_module(void *module_init);
int __pkvm_register_hcall(unsigned long hfn_hyp_va);
int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt);
#else
static inline int __pkvm_init_module(void *module_init); { return -EOPNOTSUPP; }
static inline int
__pkvm_register_hcall(unsigned long hfn_hyp_va) { return -EOPNOTSUPP; }
static inline int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt)
{
	return HCALL_UNHANDLED;
}
#endif
