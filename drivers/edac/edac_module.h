/* SPDX-License-Identifier: GPL-2.0 */

/*
 * edac_module.h
 *
 * For defining functions/data for within the EDAC_CORE module only
 *
 * written by doug thompson <norsk5@xmission.h>
 */

#ifndef	__EDAC_MODULE_H__
#define	__EDAC_MODULE_H__

#include "edac_mc.h"
#include "edac_pci.h"
#include "edac_device.h"

/*
 * INTERNAL EDAC MODULE:
 * EDAC memory controller sysfs create/remove functions
 * and setup/teardown functions
 *
 * edac_mc objects
 */
	/* on edac_mc_sysfs.c */
int edac_mc_sysfs_init(void);
void edac_mc_sysfs_exit(void);
extern int edac_create_sysfs_mci_device(struct mem_ctl_info *mci,
					const struct attribute_group **groups);
extern void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci);
extern int edac_mc_get_log_ue(void);
extern int edac_mc_get_log_ce(void);
extern int edac_mc_get_panic_on_ue(void);
extern unsigned int edac_mc_get_poll_msec(void);

unsigned edac_dimm_info_location(struct dimm_info *dimm, char *buf,
				 unsigned len);

	/* on edac_device.c */
extern int edac_device_register_sysfs_main_kobj(
				struct edac_device_ctl_info *edac_dev);
extern void edac_device_unregister_sysfs_main_kobj(
				struct edac_device_ctl_info *edac_dev);
extern int edac_device_create_sysfs(struct edac_device_ctl_info *edac_dev);
extern void edac_device_remove_sysfs(struct edac_device_ctl_info *edac_dev);

/* edac core workqueue: single CPU mode */
int edac_workqueue_setup(void);
void edac_workqueue_teardown(void);
bool edac_queue_work(struct delayed_work *work, unsigned long delay);
bool edac_stop_work(struct delayed_work *work);
bool edac_mod_work(struct delayed_work *work, unsigned long delay);

extern void edac_device_reset_delay_period(struct edac_device_ctl_info
					   *edac_dev, unsigned long value);
extern void edac_mc_reset_delay_period(unsigned long value);

/*
 * EDAC debugfs functions
 */

#define edac_debugfs_remove_recursive debugfs_remove_recursive
#define edac_debugfs_remove debugfs_remove
#ifdef CONFIG_EDAC_DEBUG
void edac_debugfs_init(void);
void edac_debugfs_exit(void);
void edac_create_debugfs_nodes(struct mem_ctl_info *mci);
struct dentry *edac_debugfs_create_dir(const char *dirname);
struct dentry *
edac_debugfs_create_dir_at(const char *dirname, struct dentry *parent);
struct dentry *
edac_debugfs_create_file(const char *name, umode_t mode, struct dentry *parent,
			 void *data, const struct file_operations *fops);
void edac_debugfs_create_x8(const char *name, umode_t mode,
			    struct dentry *parent, u8 *value);
void edac_debugfs_create_x16(const char *name, umode_t mode,
			     struct dentry *parent, u16 *value);
void edac_debugfs_create_x32(const char *name, umode_t mode,
			     struct dentry *parent, u32 *value);
#else
static inline void edac_debugfs_init(void)					{ }
static inline void edac_debugfs_exit(void)					{ }
static inline void edac_create_debugfs_nodes(struct mem_ctl_info *mci)		{ }
static inline struct dentry *edac_debugfs_create_dir(const char *dirname)	{ return NULL; }
static inline struct dentry *
edac_debugfs_create_dir_at(const char *dirname, struct dentry *parent)		{ return NULL; }
static inline struct dentry *
edac_debugfs_create_file(const char *name, umode_t mode, struct dentry *parent,
			 void *data, const struct file_operations *fops)	{ return NULL; }
static inline void edac_debugfs_create_x8(const char *name, umode_t mode,
					  struct dentry *parent, u8 *value)	{ }
static inline void edac_debugfs_create_x16(const char *name, umode_t mode,
					   struct dentry *parent, u16 *value)	{ }
static inline void edac_debugfs_create_x32(const char *name, umode_t mode,
		       struct dentry *parent, u32 *value)			{ }
#endif

/*
 * EDAC PCI functions
 */
#ifdef	CONFIG_PCI
extern void edac_pci_do_parity_check(void);
extern void edac_pci_clear_parity_errors(void);
extern int edac_sysfs_pci_setup(void);
extern void edac_sysfs_pci_teardown(void);
extern int edac_pci_get_check_errors(void);
extern int edac_pci_get_poll_msec(void);
extern void edac_pci_remove_sysfs(struct edac_pci_ctl_info *pci);
extern void edac_pci_handle_pe(struct edac_pci_ctl_info *pci, const char *msg);
extern void edac_pci_handle_npe(struct edac_pci_ctl_info *pci,
				const char *msg);
#else				/* CONFIG_PCI */
/* pre-process these away */
#define edac_pci_do_parity_check()
#define edac_pci_clear_parity_errors()
#define edac_sysfs_pci_setup()  (0)
#define edac_sysfs_pci_teardown()
#define edac_pci_get_check_errors()
#define edac_pci_get_poll_msec()
#define edac_pci_handle_pe()
#define edac_pci_handle_npe()
#endif				/* CONFIG_PCI */

#endif				/* __EDAC_MODULE_H__ */
