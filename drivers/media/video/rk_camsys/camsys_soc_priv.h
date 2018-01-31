/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RKCAMSYS_SOC_PRIV_H__
#define __RKCAMSYS_SOC_PRIV_H__

#include "camsys_internal.h"

typedef struct camsys_mipiphy_soc_para_s {
	camsys_dev_t        *camsys_dev;
	camsys_mipiphy_t    *phy;
} camsys_mipiphy_soc_para_t;

typedef enum camsys_soc_cfg_e {
	Clk_DriverStrength_Cfg = 0,
	Cif_IoDomain_Cfg,
	Mipi_Phy_Cfg,

	Isp_SoftRst,
} camsys_soc_cfg_t;

typedef struct camsys_soc_priv_s {
	char name[32];

	int (*soc_cfg)
		(camsys_dev_t *camsys_dev,
		camsys_soc_cfg_t cfg_cmd,
		void *cfg_para
		);

} camsys_soc_priv_t;

extern camsys_soc_priv_t *camsys_soc_get(void);
extern int camsys_soc_init(unsigned int);
extern int camsys_soc_deinit(void);

extern unsigned long rk_grf_base;
extern unsigned long rk_cru_base;
extern unsigned long rk_isp_base;
extern unsigned int CHIP_TYPE;

#endif

