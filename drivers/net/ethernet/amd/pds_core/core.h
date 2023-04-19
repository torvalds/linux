/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _PDSC_H_
#define _PDSC_H_

#include <linux/debugfs.h>
#include <net/devlink.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_intr.h>

#define PDSC_DRV_DESCRIPTION	"AMD/Pensando Core Driver"

#define PDSC_WATCHDOG_SECS	5
#define PDSC_TEARDOWN_RECOVERY	false
#define PDSC_TEARDOWN_REMOVING	true
#define PDSC_SETUP_RECOVERY	false
#define PDSC_SETUP_INIT		true

struct pdsc_dev_bar {
	void __iomem *vaddr;
	phys_addr_t bus_addr;
	unsigned long len;
	int res_index;
};

struct pdsc_devinfo {
	u8 asic_type;
	u8 asic_rev;
	char fw_version[PDS_CORE_DEVINFO_FWVERS_BUFLEN + 1];
	char serial_num[PDS_CORE_DEVINFO_SERIAL_BUFLEN + 1];
};

#define PDSC_INTR_NAME_MAX_SZ		32

struct pdsc_intr_info {
	char name[PDSC_INTR_NAME_MAX_SZ];
	unsigned int index;
	unsigned int vector;
	void *data;
};

/* No state flags set means we are in a steady running state */
enum pdsc_state_flags {
	PDSC_S_FW_DEAD,		    /* stopped, wait on startup or recovery */
	PDSC_S_INITING_DRIVER,	    /* initial startup from probe */
	PDSC_S_STOPPING_DRIVER,	    /* driver remove */

	/* leave this as last */
	PDSC_S_STATE_SIZE
};

struct pdsc {
	struct pci_dev *pdev;
	struct dentry *dentry;
	struct device *dev;
	struct pdsc_dev_bar bars[PDS_CORE_BARS_MAX];
	int hw_index;
	int uid;

	unsigned long state;
	u8 fw_status;
	u8 fw_generation;
	unsigned long last_fw_time;
	u32 last_hb;
	struct timer_list wdtimer;
	unsigned int wdtimer_period;
	struct work_struct health_work;
	struct devlink_health_reporter *fw_reporter;
	u32 fw_recoveries;

	struct pdsc_devinfo dev_info;
	struct pds_core_dev_identity dev_ident;
	unsigned int nintrs;
	struct pdsc_intr_info *intr_info;	/* array of nintrs elements */

	struct workqueue_struct *wq;

	unsigned int devcmd_timeout;
	struct mutex devcmd_lock;	/* lock for dev_cmd operations */
	struct mutex config_lock;	/* lock for configuration operations */
	struct pds_core_dev_info_regs __iomem *info_regs;
	struct pds_core_dev_cmd_regs __iomem *cmd_regs;
	struct pds_core_intr __iomem *intr_ctrl;
	u64 __iomem *intr_status;
	u64 __iomem *db_pages;
	dma_addr_t phy_db_pages;
	u64 __iomem *kern_dbpage;
};

int pdsc_fw_reporter_diagnose(struct devlink_health_reporter *reporter,
			      struct devlink_fmsg *fmsg,
			      struct netlink_ext_ack *extack);

void pdsc_debugfs_create(void);
void pdsc_debugfs_destroy(void);
void pdsc_debugfs_add_dev(struct pdsc *pdsc);
void pdsc_debugfs_del_dev(struct pdsc *pdsc);
void pdsc_debugfs_add_ident(struct pdsc *pdsc);
void pdsc_debugfs_add_irqs(struct pdsc *pdsc);

int pdsc_err_to_errno(enum pds_core_status_code code);
bool pdsc_is_fw_running(struct pdsc *pdsc);
bool pdsc_is_fw_good(struct pdsc *pdsc);
int pdsc_devcmd(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
		union pds_core_dev_comp *comp, int max_seconds);
int pdsc_devcmd_locked(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
		       union pds_core_dev_comp *comp, int max_seconds);
int pdsc_devcmd_init(struct pdsc *pdsc);
int pdsc_devcmd_reset(struct pdsc *pdsc);
int pdsc_dev_reinit(struct pdsc *pdsc);
int pdsc_dev_init(struct pdsc *pdsc);

int pdsc_setup(struct pdsc *pdsc, bool init);
void pdsc_teardown(struct pdsc *pdsc, bool removing);
void pdsc_health_thread(struct work_struct *work);

#endif /* _PDSC_H_ */
