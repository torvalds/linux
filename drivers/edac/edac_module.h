
/*
 * edac_module.h
 *
 * For defining functions/data for within the EDAC_CORE module only
 *
 * written by doug thompson <norsk5@xmission.h>
 */

#ifndef	__EDAC_MODULE_H__
#define	__EDAC_MODULE_H__

#include <linux/sysdev.h>

#include "edac_core.h"

/*
 * INTERNAL EDAC MODULE:
 * EDAC memory controller sysfs create/remove functions
 * and setup/teardown functions
 */
extern int edac_create_sysfs_mci_device(struct mem_ctl_info *mci);
extern void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci);
extern int edac_sysfs_memctrl_setup(void);
extern void edac_sysfs_memctrl_teardown(void);
extern void edac_check_mc_devices(void);
extern int edac_get_log_ue(void);
extern int edac_get_log_ce(void);
extern int edac_get_panic_on_ue(void);
extern int edac_mc_get_log_ue(void);
extern int edac_mc_get_log_ce(void);
extern int edac_mc_get_panic_on_ue(void);
extern int edac_get_poll_msec(void);
extern int edac_mc_get_poll_msec(void);

extern int edac_device_create_sysfs(struct edac_device_ctl_info *edac_dev);
extern void edac_device_remove_sysfs(struct edac_device_ctl_info *edac_dev);
extern struct sysdev_class *edac_get_edac_class(void);

/* edac core workqueue: single CPU mode */
extern struct workqueue_struct *edac_workqueue;
extern void edac_device_workq_setup(struct edac_device_ctl_info *edac_dev,
				    unsigned msec);
extern void edac_device_workq_teardown(struct edac_device_ctl_info *edac_dev);
extern void edac_device_reset_delay_period(struct edac_device_ctl_info
					   *edac_dev, unsigned long value);
extern void *edac_align_ptr(void *ptr, unsigned size);

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
#else				/* CONFIG_PCI */
/* pre-process these away */
#define edac_pci_do_parity_check()
#define edac_pci_clear_parity_errors()
#define edac_sysfs_pci_setup()  (0)
#define edac_sysfs_pci_teardown()
#define edac_pci_get_check_errors()
#define edac_pci_get_poll_msec()
#endif				/* CONFIG_PCI */

#endif				/* __EDAC_MODULE_H__ */
