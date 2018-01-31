/* SPDX-License-Identifier: GPL-2.0 */

#include "camsys_soc_priv.h"


static camsys_soc_priv_t *camsys_soc_p;

#ifdef CONFIG_ARM64
extern int camsys_rk3368_cfg(
	camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para);
extern int camsys_rk3366_cfg(
	camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para);
extern int camsys_rk3399_cfg(
	camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para);
extern int camsys_rk3326_cfg(
	camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para);
#else
extern int camsys_rk3288_cfg(
	camsys_dev_t *camsys_dev, camsys_soc_cfg_t cfg_cmd, void *cfg_para);
#endif
camsys_soc_priv_t *camsys_soc_get(void)
{
	if (camsys_soc_p != NULL) {
		return camsys_soc_p;
	} else {
		return NULL;
	}
}

int camsys_soc_init(unsigned int chip_type)
{
	camsys_soc_p = kzalloc(sizeof(camsys_soc_priv_t), GFP_KERNEL);
	if (camsys_soc_p == NULL) {
		camsys_err("malloc camsys_soc_priv_t failed!");
		goto fail;
	}

#ifdef CONFIG_ARM64
	if (chip_type == 3368) {
		strlcpy(camsys_soc_p->name, "camsys_rk3368", 31);
		camsys_soc_p->soc_cfg = camsys_rk3368_cfg;
		camsys_trace(2, "rk3368 exit!");
	} else if (chip_type == 3366) {
		strlcpy(camsys_soc_p->name, "camsys_rk3366", 31);
		camsys_soc_p->soc_cfg = camsys_rk3366_cfg;
		camsys_trace(2, "rk3366 exit!");
	} else if (chip_type == 3399) {
		strlcpy(camsys_soc_p->name, "camsys_rk3399", 31);
		camsys_soc_p->soc_cfg = camsys_rk3399_cfg;
		camsys_trace(2, "rk3399 exit!");
	} else if (chip_type == 3326) {
		strlcpy(camsys_soc_p->name, "camsys_rk3326", 31);
		camsys_soc_p->soc_cfg = camsys_rk3326_cfg;
		camsys_trace(2, "rk3326 exit!");
	}
#else
	if (chip_type == 3288) {
		strlcpy(camsys_soc_p->name, "camsys_rk3288", 31);
		camsys_soc_p->soc_cfg = camsys_rk3288_cfg;
		camsys_trace(2, "rk3288 exit!");
	}
#endif

	return 0;
fail:
	if (camsys_soc_p != NULL) {
		kfree(camsys_soc_p);
		camsys_soc_p = NULL;
	}
	return -1;
}

int camsys_soc_deinit(void)
{
	if (camsys_soc_p != NULL) {
		kfree(camsys_soc_p);
		camsys_soc_p = NULL;
	}
	return 0;
}
