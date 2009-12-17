#ifdef CONFIG_SYSFS
extern void register_wlags_sysfs(struct net_device *);
extern void unregister_wlags_sysfs(struct net_device *);
#else
static void register_wlags_sysfs(struct net_device *) { return; };
static void unregister_wlags_sysfs(struct net_device *) { return; };
#endif
