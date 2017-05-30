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
 * This file defines the driver FIPS functions that should be
 * implemented by the driver user. Current implementation is sample code only.
 ***************************************************************/

#include <linux/module.h>
#include "ssi_fips_local.h"
#include "ssi_driver.h"


static bool tee_error;
module_param(tee_error, bool, 0644);
MODULE_PARM_DESC(tee_error, "Simulate TEE library failure flag: 0 - no error (default), 1 - TEE error occured ");

static ssi_fips_state_t fips_state = CC_FIPS_STATE_NOT_SUPPORTED;
static ssi_fips_error_t fips_error = CC_REE_FIPS_ERROR_OK;

/*
 * This function returns the FIPS REE state.
 * The function should be implemented by the driver user, depends on where
 * the state value is stored.
 * The reference code uses global variable.
 */
int ssi_fips_ext_get_state(ssi_fips_state_t *p_state)
{
        int rc = 0;

	if (p_state == NULL) {
		return -EINVAL;
	}

	*p_state = fips_state;

	return rc;
}

/*
 * This function returns the FIPS REE error.
 * The function should be implemented by the driver user, depends on where
 * the error value is stored.
 * The reference code uses global variable.
 */
int ssi_fips_ext_get_error(ssi_fips_error_t *p_err)
{
        int rc = 0;

	if (p_err == NULL) {
		return -EINVAL;
	}

	*p_err = fips_error;

	return rc;
}

/*
 * This function sets the FIPS REE state.
 * The function should be implemented by the driver user, depends on where
 * the state value is stored.
 * The reference code uses global variable.
 */
int ssi_fips_ext_set_state(ssi_fips_state_t state)
{
	fips_state = state;
	return 0;
}

/*
 * This function sets the FIPS REE error.
 * The function should be implemented by the driver user, depends on where
 * the error value is stored.
 * The reference code uses global variable.
 */
int ssi_fips_ext_set_error(ssi_fips_error_t err)
{
	fips_error = err;
	return 0;
}


