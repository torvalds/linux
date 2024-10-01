// SPDX-License-Identifier: GPL-2.0
/*
 * Functions for registration of I/O interruption subclasses on s390.
 *
 * Copyright IBM Corp. 2008
 * Authors: Sebastian Ott <sebott@linux.vnet.ibm.com>
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/isc.h>

static unsigned int isc_refs[MAX_ISC + 1];
static DEFINE_SPINLOCK(isc_ref_lock);


/**
 * isc_register - register an I/O interruption subclass.
 * @isc: I/O interruption subclass to register
 *
 * The number of users for @isc is increased. If this is the first user to
 * register @isc, the corresponding I/O interruption subclass mask is enabled.
 *
 * Context:
 *   This function must not be called in interrupt context.
 */
void isc_register(unsigned int isc)
{
	if (isc > MAX_ISC) {
		WARN_ON(1);
		return;
	}

	spin_lock(&isc_ref_lock);
	if (isc_refs[isc] == 0)
		ctl_set_bit(6, 31 - isc);
	isc_refs[isc]++;
	spin_unlock(&isc_ref_lock);
}
EXPORT_SYMBOL_GPL(isc_register);

/**
 * isc_unregister - unregister an I/O interruption subclass.
 * @isc: I/O interruption subclass to unregister
 *
 * The number of users for @isc is decreased. If this is the last user to
 * unregister @isc, the corresponding I/O interruption subclass mask is
 * disabled.
 * Note: This function must not be called if isc_register() hasn't been called
 * before by the driver for @isc.
 *
 * Context:
 *   This function must not be called in interrupt context.
 */
void isc_unregister(unsigned int isc)
{
	spin_lock(&isc_ref_lock);
	/* check for misuse */
	if (isc > MAX_ISC || isc_refs[isc] == 0) {
		WARN_ON(1);
		goto out_unlock;
	}
	if (isc_refs[isc] == 1)
		ctl_clear_bit(6, 31 - isc);
	isc_refs[isc]--;
out_unlock:
	spin_unlock(&isc_ref_lock);
}
EXPORT_SYMBOL_GPL(isc_unregister);
