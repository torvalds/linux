#ifndef __BACKPORT_LINUX_MODULE_H
#define __BACKPORT_LINUX_MODULE_H
#include_next <linux/module.h>
#include <linux/rcupdate.h>

/*
 * The define overwriting module_init is based on the original module_init
 * which looks like this:
 * #define module_init(initfn)					\
 *	static inline initcall_t __inittest(void)		\
 *	{ return initfn; }					\
 *	int init_module(void) __attribute__((alias(#initfn)));
 *
 * To the call to the initfn we added the symbol dependency on compat
 * to make sure that compat.ko gets loaded for any compat modules.
 */
extern void backport_dependency_symbol(void);

#ifdef BACKPORTS_GIT_TRACKED
#define BACKPORT_MOD_VERSIONS MODULE_VERSION(BACKPORTS_GIT_TRACKED);
#else
#define BACKPORT_MOD_VERSIONS						\
	MODULE_VERSION("backported from " CPTCFG_KERNEL_NAME	\
		       " (" CPTCFG_KERNEL_VERSION ")"		\
		       " using backports " CPTCFG_VERSION);
#endif

#ifdef MODULE
#undef module_init
#define module_init(initfn)						\
	static int __init __init_backport(void)				\
	{								\
		backport_dependency_symbol();				\
		return initfn();					\
	}								\
	int init_module(void) __attribute__((alias("__init_backport")));\
	BACKPORT_MOD_VERSIONS

/*
 * The define overwriting module_exit is based on the original module_exit
 * which looks like this:
 * #define module_exit(exitfn)                                    \
 *         static inline exitcall_t __exittest(void)               \
 *         { return exitfn; }                                      \
 *         void cleanup_module(void) __attribute__((alias(#exitfn)));
 *
 * We replaced the call to the actual function exitfn() with a call to our
 * function which calls the original exitfn() and then rcu_barrier()
 *
 * As a module will not be unloaded that ofter it should not have a big
 * performance impact when rcu_barrier() is called on every module exit,
 * also when no kfree_rcu() backport is used in that module.
 */
#undef module_exit
#define module_exit(exitfn)						\
	static void __exit __exit_compat(void)				\
	{								\
		exitfn();						\
		rcu_barrier();						\
	}								\
	void cleanup_module(void) __attribute__((alias("__exit_compat")));
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#undef param_check_bool
#define param_check_bool(name, p) __param_check(name, p, bool)
#endif

#endif /* __BACKPORT_LINUX_MODULE_H */
