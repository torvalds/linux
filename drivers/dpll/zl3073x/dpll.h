/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_DPLL_H
#define _ZL3073X_DPLL_H

#include <linux/dpll.h>
#include <linux/list.h>
#include <linux/ptp_clock_kernel.h>

#include "core.h"

/**
 * struct zl3073x_dpll - ZL3073x DPLL sub-device structure
 * @list: this DPLL list entry
 * @dev: pointer to multi-function parent device
 * @id: DPLL index
 * @check_count: periodic check counter
 * @phase_monitor: is phase offset monitor enabled
 * @ops: DPLL device operations for this instance
 * @dpll_dev: pointer to registered DPLL device
 * @tracker: tracking object for the acquired reference
 * @lock: per-DPLL mutex serializing all operations
 * @type: DPLL type (PPS or EEC)
 * @lock_status: last saved DPLL lock status
 * @pins: list of pins
 * @ptp_info: PTP clock info
 * @ptp_clock: registered PTP clock (or NULL)
 */
struct zl3073x_dpll {
	struct list_head		list;
	struct zl3073x_dev		*dev;
	u8				id;
	u8				check_count;
	bool				phase_monitor;
	struct dpll_device_ops		ops;
	struct dpll_device		*dpll_dev;
	dpll_tracker			tracker;
	struct mutex			lock;
	enum dpll_type			type;
	enum dpll_lock_status		lock_status;
	struct list_head		pins;
	struct ptp_clock_info		ptp_info;
	struct ptp_clock		*ptp_clock;
};

struct zl3073x_dpll *zl3073x_dpll_alloc(struct zl3073x_dev *zldev, u8 ch);
void zl3073x_dpll_free(struct zl3073x_dpll *zldpll);

int zl3073x_dpll_register(struct zl3073x_dpll *zldpll);
void zl3073x_dpll_unregister(struct zl3073x_dpll *zldpll);

int zl3073x_dpll_init_fine_phase_adjust(struct zl3073x_dev *zldev);
void zl3073x_dpll_changes_check(struct zl3073x_dpll *zldpll);

#endif /* _ZL3073X_DPLL_H */
