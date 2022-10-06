#ifdef CONFIG_MODULES
int __pkvm_init_module(void *module_init);
#else
static inline int __pkvm_init_module(void *module_init); { return -EOPNOTSUPP; }
#endif
