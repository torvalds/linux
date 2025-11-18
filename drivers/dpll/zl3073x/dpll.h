/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_DPLL_H
#define _ZL3073X_DPLL_H

#include <linux/dpll.h>
#include <linux/list.h>

#include "core.h"

/**
 * struct zl3073x_dpll - ZL3073x DPLL sub-device structure
 * @list: this DPLL list entry
 * @dev: pointer to multi-function parent device
 * @id: DPLL index
 * @refsel_mode: reference selection mode
 * @forced_ref: selected reference in forced reference lock mode
 * @check_count: periodic check counter
 * @phase_monitor: is phase offset monitor enabled
 * @dpll_dev: pointer to registered DPLL device
 * @lock_status: last saved DPLL lock status
 * @pins: list of pins
 * @change_work: device change notification work
 */
struct zl3073x_dpll {
	struct list_head		list;
	struct zl3073x_dev		*dev;
	u8				id;
	u8				refsel_mode;
	u8				forced_ref;
	u8				check_count;
	bool				phase_monitor;
	struct dpll_device		*dpll_dev;
	enum dpll_lock_status		lock_status;
	struct list_head		pins;
	struct work_struct		change_work;
};

struct zl3073x_dpll *zl3073x_dpll_alloc(struct zl3073x_dev *zldev, u8 ch);
void zl3073x_dpll_free(struct zl3073x_dpll *zldpll);

int zl3073x_dpll_register(struct zl3073x_dpll *zldpll);
void zl3073x_dpll_unregister(struct zl3073x_dpll *zldpll);

int zl3073x_dpll_init_fine_phase_adjust(struct zl3073x_dev *zldev);
void zl3073x_dpll_changes_check(struct zl3073x_dpll *zldpll);

#endif /* _ZL3073X_DPLL_H */
