
#include "camsys_soc_priv.h"


static camsys_soc_priv_t* camsys_soc_p;

extern int camsys_rk3288_cfg (camsys_soc_cfg_t cfg_cmd, void* cfg_para);
extern int camsys_rk3368_cfg (camsys_soc_cfg_t cfg_cmd, void* cfg_para);

camsys_soc_priv_t* camsys_soc_get(void)
{
    if (camsys_soc_p != NULL) {
        return camsys_soc_p;
    } else {
        return NULL;
    }
}

int camsys_soc_init(void)
{    
    camsys_soc_p = kzalloc(sizeof(camsys_soc_priv_t),GFP_KERNEL);
    if (camsys_soc_p == NULL) {
        camsys_err("malloc camsys_soc_priv_t failed!");
        goto fail;
    }

#ifdef CONFIG_ARM64
	strlcpy(camsys_soc_p->name,"camsys_rk3368",31);
	camsys_soc_p->soc_cfg = camsys_rk3368_cfg;
	camsys_err("camsys_soc_init exit!");
#else
	if (cpu_is_rk3288()) {
	        strlcpy(camsys_soc_p->name,"camsys_rk3288",31);
        camsys_soc_p->soc_cfg = camsys_rk3288_cfg;	
	} else {
		camsys_err("camsys isn't support soc: 0x%lx!",rockchip_soc_id);
		goto fail;
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
