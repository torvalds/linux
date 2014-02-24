
#ifdef CONFIG_ZORRO_NAMES
extern void zorro_name_device(struct zorro_dev *z);
#else
static inline void zorro_name_device(struct zorro_dev *dev) { }
#endif

extern int zorro_create_sysfs_dev_files(struct zorro_dev *z);

