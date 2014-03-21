#ifndef __CAMSYS_MIPICSI_PHY_H__
#define __CAMSYS_MIPICSI_PHY_H__

#include "camsys_internal.h"

#if defined(CONFIG_ARCH_ROCKCHIP)

#define write_grf_reg(addr, val)           __raw_writel(val, addr+RK_GRF_VIRT)
#define read_grf_reg(addr)                 __raw_readl(addr+RK_GRF_VIRT)
#define mask_grf_reg(addr, msk, val)       write_grf_reg(addr,(val)|((~(msk))&read_grf_reg(addr)))
#else
#define write_grf_reg(addr, val)  
#define read_grf_reg(addr)                 0
#define mask_grf_reg(addr, msk, val)	
#endif


typedef struct camsys_mipiphy_clk_s {
    struct clk       *pd_mipi_csi;
    struct clk       *pclk_mipiphy_csi;
    bool             in_on;
    spinlock_t       lock;
} camsys_mipiphy_clk_t;


int camsys_mipiphy_probe_cb(struct platform_device *pdev, camsys_dev_t *camsys_dev);

#endif
