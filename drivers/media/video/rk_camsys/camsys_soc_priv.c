
#include "camsys_soc_priv.h"



static camsys_soc_priv_t* camsys_soc_p;

#include "camsys_soc_rk3288.c"

static int camsys_rk3288_cfg (camsys_soc_cfg_t cfg_cmd, void* cfg_para)
{
    unsigned int *para_int;
    
    switch (cfg_cmd)
    {
        case Clk_DriverStrength_Cfg:
        {
            para_int = (unsigned int*)cfg_para;
            __raw_writel((((*para_int)&0x03)<<3)|(0x03<<3), RK_GRF_VIRT+0x01d4);
            break;
        }

        case Cif_IoDomain_Cfg:
        {
            para_int = (unsigned int*)cfg_para;
            if (*para_int < 28000000) {
                __raw_writel(((1<<1)|(1<<(1+16))),RK_GRF_VIRT+0x0380);    // 1.8v IO
            } else {
                __raw_writel(((0<<1)|(1<<(1+16))),RK_GRF_VIRT+0x0380);    // 3.3v IO
            }
            break;
        }

        case Mipi_Phy_Cfg:
        {
            camsys_rk3288_mipihpy_cfg((camsys_mipiphy_soc_para_t*)cfg_para);
            break;
        }

        case Isp_SoftRst:         /* ddl@rock-chips.com: v0.d.0 */
        {
            unsigned int reset;
            reset = (unsigned int)cfg_para;

            if (reset == 1)
                cru_writel(0x40004000,0x1d0);
            else 
                cru_writel(0x40000000,0x1d0);
            camsys_trace(1, "Isp_SoftRst: %d",reset);
            break;
        }

        default:
        {
            camsys_warn("cfg_cmd: 0x%x isn't support for %s",cfg_cmd,camsys_soc_p->name);
            break;
        }

    }

    return 0;


}

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

    if (soc_is_rk3288()) {
        strlcpy(camsys_soc_p->name,"camsys_rk3288",31);
        camsys_soc_p->soc_cfg = camsys_rk3288_cfg;
    } else {
        camsys_err("camsys isn't support soc: 0x%lx!",rockchip_soc_id);
        goto fail;
    }
    
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
