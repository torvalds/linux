/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CAMSYS_CIF_H__
#define __CAMSYS_CIF_H__

#include "camsys_internal.h"

#define CAMSYS_CIF_IRQNAME                   "CifIrq"


#define CIF_BASE                                0x00
#define CIF_CTRL                                (CIF_BASE)
#define CIF_INITSTA                             (CIF_BASE+0x08)
#define CIF_FRAME_STATUS                        (CIF_BASE+0x60)
#define CIF_LAST_LINE                           (CIF_BASE+0x68)
#define CIF_LAST_PIX                            (CIF_BASE+0x6c)
#define CRU_PCLK_REG30                           0xbc


#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188) \
	|| defined(CONFIG_ARCH_ROCKCHIP)
/*GRF_IO_CON3                        0x100*/
#define CIF_DRIVER_STRENGTH_2MA            (0x00 << 12)
#define CIF_DRIVER_STRENGTH_4MA            (0x01 << 12)
#define CIF_DRIVER_STRENGTH_8MA            (0x02 << 12)
#define CIF_DRIVER_STRENGTH_12MA           (0x03 << 12)
#define CIF_DRIVER_STRENGTH_MASK           (0x03 << 28)

/*GRF_IO_CON4                        0x104*/
#define CIF_CLKOUT_AMP_3V3                 (0x00 << 10)
#define CIF_CLKOUT_AMP_1V8                 (0x01 << 10)
#define CIF_CLKOUT_AMP_MASK                (0x01 << 26)

#define write_grf_reg(addr, val)           \
	__raw_writel(val, addr+RK_GRF_VIRT)
#define read_grf_reg(addr)                 \
	__raw_readl(addr+RK_GRF_VIRT)
#define mask_grf_reg(addr, msk, val)       \
	write_grf_reg(addr, (val)|((~(msk))&read_grf_reg(addr)))
#else
#define write_grf_reg(addr, val)
#define read_grf_reg(addr)                 0
#define mask_grf_reg(addr, msk, val)
#endif


typedef struct camsys_cif_clk_s {
	struct clk *pd_cif;
	struct clk *aclk_cif;
	struct clk *hclk_cif;
	struct clk *cif_clk_in;
	bool in_on;

	struct clk *cif_clk_out;
	unsigned int out_on;

	spinlock_t lock;
} camsys_cif_clk_t;


int camsys_cif_probe_cb(
struct platform_device *pdev, camsys_dev_t *camsys_dev);

#endif

