#include <asm/kvm_pgtable.h>

#define HCALL_HANDLED 0
#define HCALL_UNHANDLED -1

int __pkvm_register_host_smc_handler(bool (*cb)(struct kvm_cpu_context *));
int __pkvm_register_default_trap_handler(bool (*cb)(struct kvm_cpu_context *));
int __pkvm_register_illegal_abt_notifier(void (*cb)(struct kvm_cpu_context *));

enum pkvm_psci_notification;
int __pkvm_register_psci_notifier(void (*cb)(enum pkvm_psci_notification, struct kvm_cpu_context *));

#ifdef CONFIG_MODULES
int __pkvm_init_module(void *module_init);
int __pkvm_register_hcall(unsigned long hfn_hyp_va);
int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt);
void pkvm_modules_lock(void);
void pkvm_modules_unlock(void);
bool pkvm_modules_enabled(void);
int __pkvm_close_module_registration(void);
#else
static inline int __pkvm_init_module(void *module_init) { return -EOPNOTSUPP; }
static inline int
__pkvm_register_hcall(unsigned long hfn_hyp_va) { return -EOPNOTSUPP; }
static inline int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt)
{
	return HCALL_UNHANDLED;
}
static inline void pkvm_modules_lock(void) { }
static inline void pkvm_modules_unlock(void) { }
static inline bool pkvm_modules_enabled(void) { return false; }
static inline int __pkvm_close_module_registration(void) { return -EOPNOTSUPP; }
#endif
