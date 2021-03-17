/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 */
#ifndef _ASM_C6X_DSCR_H
#define _ASM_C6X_DSCR_H

enum dscr_devstate_t {
	DSCR_DEVSTATE_ENABLED,
	DSCR_DEVSTATE_DISABLED,
};

/*
 * Set the device state of the device with the given ID.
 *
 * Individual drivers should use this to enable or disable the
 * hardware device. The devid used to identify the device being
 * controlled should be a property in the device's tree node.
 */
extern void dscr_set_devstate(int devid, enum dscr_devstate_t state);

/*
 * Assert or de-assert an RMII reset.
 */
extern void dscr_rmii_reset(int id, int assert);

extern void dscr_probe(void);

#endif /* _ASM_C6X_DSCR_H */
