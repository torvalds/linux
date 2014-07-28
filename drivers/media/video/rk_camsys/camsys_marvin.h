#ifndef __CAMSYS_MARVIN_H__
#define __CAMSYS_MARVIN_H__

#include "camsys_internal.h"

#define CAMSYS_MARVIN_IRQNAME                   "MarvinIrq"



#define MRV_ISP_BASE                            0x400
#define MRV_ISP_RIS                             (MRV_ISP_BASE+0x1c0)
#define MRV_ISP_MIS                             (MRV_ISP_BASE+0x1c4)
#define MRV_ISP_ICR                             (MRV_ISP_BASE+0x1c8)

#define MRV_MIPI_BASE                           0x1C00
#define MRV_MIPI_MIS                            (MRV_MIPI_BASE+0x10)
#define MRV_MIPI_ICR                            (MRV_MIPI_BASE+0x14)

#define MRV_MI_BASE                             (0x1400)

#define MRV_MI_MP_Y_OFFS_CNT_START                   (MRV_MI_BASE+0x14)
#define MRV_MI_INIT                   (MRV_MI_BASE+0x4)
#define MRV_MI_MP_Y_BASE_AD                   (MRV_MI_BASE+0x8)
#define MRV_MI_Y_BASE_AD_SHD                   (MRV_MI_BASE+0x78)
#define MRV_MI_Y_OFFS_CNT_SHD                   (MRV_MI_BASE+0x80)
#define MRV_MI_IMIS                              (MRV_MI_BASE+0xf8)
#define MRV_MI_RIS                              (MRV_MI_BASE+0xfc)
#define MRV_MI_MIS                              (MRV_MI_BASE+0x100)
#define MRV_MI_ICR                              (MRV_MI_BASE+0x104)

#define MRV_FLASH_CONFIG                        (0x664)

typedef enum IO_USE_TYPE_e{
    USE_AS_GPIO,
    USE_AS_ISP_INTERNAL,
}IO_USE_TYPE_t;

typedef struct camsys_mrv_clk_s {
    struct clk      *pd_isp;
    struct clk      *hclk_isp;
    struct clk      *aclk_isp;
    struct clk      *isp;
    struct clk      *isp_jpe;
    struct clk      *pclkin_isp;
    struct clk      *clk_mipi_24m;
    bool             in_on;

    struct clk      *cif_clk_out;
    struct clk      *cif_clk_pll;
    unsigned int     out_on;

    struct mutex     lock;
} camsys_mrv_clk_t;

int camsys_mrv_probe_cb(struct platform_device *pdev, camsys_dev_t *camsys_dev);

#endif


