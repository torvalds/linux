
/*
 * edac_module.h
 *
 * For defining functions/data for within the EDAC_CORE module only
 *
 * written by doug thompson <norsk5@xmission.h>
 */

#ifndef	__EDAC_MODULE_H__
#define	__EDAC_MODULE_H__

#include "edac_core.h"

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
extern int edac_create_sysfs_mci_device(struct mem_ctl_info *mci);
extern void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci);
void edac_unregister_sysfs(struct mem_ctl_info *mci);
extern int edac_get_log_ue(void);
extern int edac_get_log_ce(void);
extern int edac_get_panic_on_ue(void);
extern int edac_mc_get_log_ue(void);
extern int edac_mc_get_log_ce(void);
extern int edac_mc_get_panic_on_ue(void);
extern int edac_get_poll_msec(void);
extern int edac_mc_get_poll_msec(void);

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
extern struct workqueue_struct *edac_workqueue;
extern void edac_device_workq_setup(struct edac_device_ctl_info *edac_dev,
				    unsigned msec);
extern void edac_device_workq_teardown(struct edac_device_ctl_info *edac_dev);
extern void edac_device_reset_delay_period(struct edac_device_ctl_info
					   *edac_dev, unsigned long value);
extern void edac_mc_reset_delay_period(int value);

extern void *edac_align_ptr(void **p, unsigned size, int n_elems);

/*
 * EDAC debugfs functions
 */
#ifdef CONFIG_EDAC_DEBUG
int edac_debugfs_init(void);
void edac_debugfs_exit(void);
#else
static inline int edac_debugfs_init(void)
{
	return -ENODEV;
}
static inline void edac_debugfs_exit(void) {}
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
