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

#ifndef __CC_FIPS_H__
#define __CC_FIPS_H__

#ifdef CONFIG_CRYPTO_FIPS

enum cc_fips_status {
	CC_FIPS_SYNC_MODULE_OK = 0x0,
	CC_FIPS_SYNC_MODULE_ERROR = 0x1,
	CC_FIPS_SYNC_REE_STATUS = 0x4,
	CC_FIPS_SYNC_TEE_STATUS = 0x8,
	CC_FIPS_SYNC_STATUS_RESERVE32B = S32_MAX
};

int cc_fips_init(struct cc_drvdata *p_drvdata);
void cc_fips_fini(struct cc_drvdata *drvdata);
void fips_handler(struct cc_drvdata *drvdata);
void cc_set_ree_fips_status(struct cc_drvdata *drvdata, bool ok);

#else  /* CONFIG_CRYPTO_FIPS */

static inline int cc_fips_init(struct cc_drvdata *p_drvdata)
{
	return 0;
}

static inline void cc_fips_fini(struct cc_drvdata *drvdata) {}
static inline void cc_set_ree_fips_status(struct cc_drvdata *drvdata,
					  bool ok) {}
static inline void fips_handler(struct cc_drvdata *drvdata) {}

#endif /* CONFIG_CRYPTO_FIPS */

#endif  /*__CC_FIPS_H__*/

