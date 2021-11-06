/*
 * Broadcom Dongle Host Driver (DHD), common DHD core.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id:  $
 */

/*
 * NOTE:-
 * This file is a exact copy of trunk/components/opensource/pom/pom.h.
 * Any changes needed to this file should reflect in trunk too.
 * Otherwise POM(power on off manager) and dhd will go out of sync
 */

/*
 * Donot change the order of func ids below.
 * Order of invoking the power off and power on handlers is importent during
 * power toggle through API: pom_toggle_reg_on.
 * WLAN FW should be loaded first after REG-ON. Otherwise SR doesnot work.
 *
 */
enum pom_func_id {
	WLAN_FUNC_ID = 0,
	BT_FUNC_ID = 1,
	MAX_COEX_FUNC = 2
};

enum pom_toggle_reason {
	BY_WLAN_DUE_TO_WLAN = 0,
	BY_WLAN_DUE_TO_BT = 1,
	BY_BT_DUE_TO_BT = 2,
	BY_BT_DUE_TO_WLAN = 3,
	BY_USER_PROCESS = 4,
	BY_UNKNOWN_REASON = 5
};

/* Common structure to be used to register and de-register from BT/WLAN */
typedef struct pom_func_handler {
	unsigned char func_id;
	void *handler;
	int (*power_off)(void *handler, unsigned char reason);
	int (*power_on)(void *handler, unsigned char reason);
} pom_func_handler_t;

/* Register call back during attach of each function */
extern int pom_func_register(struct pom_func_handler *func);

/* De-Register call back during detach of each function */
extern int pom_func_deregister(struct pom_func_handler *func);

/* Toggle Reg ON, called to recover from bad state */
extern int pom_toggle_reg_on(unsigned char func_id, unsigned char reason);
