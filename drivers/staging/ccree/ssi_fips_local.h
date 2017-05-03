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

#ifndef __SSI_FIPS_LOCAL_H__
#define __SSI_FIPS_LOCAL_H__


#ifdef CONFIG_CCX7REE_FIPS_SUPPORT

#include "ssi_fips.h"
struct ssi_drvdata;

// IG - how to make 1 file for TEE and REE
typedef enum CC_FipsSyncStatus{
	CC_FIPS_SYNC_MODULE_OK 		= 0x0,
	CC_FIPS_SYNC_MODULE_ERROR 	= 0x1,
	CC_FIPS_SYNC_REE_STATUS 	= 0x4,
	CC_FIPS_SYNC_TEE_STATUS 	= 0x8,
	CC_FIPS_SYNC_STATUS_RESERVE32B 	= INT32_MAX
}CCFipsSyncStatus_t;


#define CHECK_AND_RETURN_UPON_FIPS_ERROR() {\
        if (ssi_fips_check_fips_error() != 0) {\
                return -ENOEXEC;\
        }\
}
#define CHECK_AND_RETURN_VOID_UPON_FIPS_ERROR() {\
        if (ssi_fips_check_fips_error() != 0) {\
                return;\
        }\
}
#define SSI_FIPS_INIT(p_drvData)  (ssi_fips_init(p_drvData))
#define SSI_FIPS_FINI(p_drvData)  (ssi_fips_fini(p_drvData))

#define FIPS_LOG(...)	SSI_LOG(KERN_INFO, __VA_ARGS__)
#define FIPS_DBG(...)	//SSI_LOG(KERN_INFO, __VA_ARGS__)

/* FIPS functions */
int ssi_fips_init(struct ssi_drvdata *p_drvdata);
void ssi_fips_fini(struct ssi_drvdata *drvdata);
int ssi_fips_check_fips_error(void);
int ssi_fips_set_error(struct ssi_drvdata *p_drvdata, ssi_fips_error_t err);
void fips_handler(struct ssi_drvdata *drvdata);

#else  /* CONFIG_CC7XXREE_FIPS_SUPPORT */

#define CHECK_AND_RETURN_UPON_FIPS_ERROR()
#define CHECK_AND_RETURN_VOID_UPON_FIPS_ERROR()

static inline int ssi_fips_init(struct ssi_drvdata *p_drvdata)
{
	return 0;
}

static inline void ssi_fips_fini(struct ssi_drvdata *drvdata) {}

void fips_handler(struct ssi_drvdata *drvdata);

#endif  /* CONFIG_CC7XXREE_FIPS_SUPPORT */


#endif  /*__SSI_FIPS_LOCAL_H__*/

