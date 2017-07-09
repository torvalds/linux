/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**************************************************************
 * This file defines the driver FIPS APIs                     *
 **************************************************************/

#include <linux/module.h>
#include "ssi_fips.h"

extern int ssi_fips_ext_get_state(enum cc_fips_state_t *p_state);
extern int ssi_fips_ext_get_error(enum cc_fips_error *p_err);

/*
 * This function returns the REE FIPS state.
 * It should be called by kernel module.
 */
int ssi_fips_get_state(enum cc_fips_state_t *p_state)
{
	int rc = 0;

	if (!p_state)
		return -EINVAL;

	rc = ssi_fips_ext_get_state(p_state);

	return rc;
}
EXPORT_SYMBOL(ssi_fips_get_state);

/*
 * This function returns the REE FIPS error.
 * It should be called by kernel module.
 */
int ssi_fips_get_error(enum cc_fips_error *p_err)
{
	int rc = 0;

	if (!p_err)
		return -EINVAL;

	rc = ssi_fips_ext_get_error(p_err);

	return rc;
}
EXPORT_SYMBOL(ssi_fips_get_error);
