/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PRU-ICSS Subsystem user interfaces
 *
 * Copyright (C) 2015-2023 Texas Instruments Incorporated - http://www.ti.com
 *	MD Danish Anwar <danishanwar@ti.com>
 */

#ifndef _SOC_TI_PRUSS_H_
#define _SOC_TI_PRUSS_H_

#include <linux/bits.h>
#include <linux/regmap.h>

/*
 * PRU_ICSS_CFG registers
 * SYSCFG, ISRP, ISP, IESP, IECP, SCRP applicable on AMxxxx devices only
 */
#define PRUSS_CFG_REVID         0x00
#define PRUSS_CFG_SYSCFG        0x04
#define PRUSS_CFG_GPCFG(x)      (0x08 + (x) * 4)
#define PRUSS_CFG_CGR           0x10
#define PRUSS_CFG_ISRP          0x14
#define PRUSS_CFG_ISP           0x18
#define PRUSS_CFG_IESP          0x1C
#define PRUSS_CFG_IECP          0x20
#define PRUSS_CFG_SCRP          0x24
#define PRUSS_CFG_PMAO          0x28
#define PRUSS_CFG_MII_RT        0x2C
#define PRUSS_CFG_IEPCLK        0x30
#define PRUSS_CFG_SPP           0x34
#define PRUSS_CFG_PIN_MX        0x40

/* PRUSS_GPCFG register bits */
#define PRUSS_GPCFG_PRU_GPI_MODE_MASK           GENMASK(1, 0)
#define PRUSS_GPCFG_PRU_GPI_MODE_SHIFT          0

#define PRUSS_GPCFG_PRU_MUX_SEL_SHIFT           26
#define PRUSS_GPCFG_PRU_MUX_SEL_MASK            GENMASK(29, 26)

/* PRUSS_MII_RT register bits */
#define PRUSS_MII_RT_EVENT_EN                   BIT(0)

/* PRUSS_SPP register bits */
#define PRUSS_SPP_XFER_SHIFT_EN                 BIT(1)
#define PRUSS_SPP_PRU1_PAD_HP_EN                BIT(0)
#define PRUSS_SPP_RTU_XFR_SHIFT_EN              BIT(3)

/**
 * pruss_cfg_read() - read a PRUSS CFG sub-module register
 * @pruss: the pruss instance handle
 * @reg: register offset within the CFG sub-module
 * @val: pointer to return the value in
 *
 * Reads a given register within the PRUSS CFG sub-module and
 * returns it through the passed-in @val pointer
 *
 * Return: 0 on success, or an error code otherwise
 */
static int pruss_cfg_read(struct pruss *pruss, unsigned int reg, unsigned int *val)
{
	if (IS_ERR_OR_NULL(pruss))
		return -EINVAL;

	return regmap_read(pruss->cfg_regmap, reg, val);
}

/**
 * pruss_cfg_update() - configure a PRUSS CFG sub-module register
 * @pruss: the pruss instance handle
 * @reg: register offset within the CFG sub-module
 * @mask: bit mask to use for programming the @val
 * @val: value to write
 *
 * Programs a given register within the PRUSS CFG sub-module
 *
 * Return: 0 on success, or an error code otherwise
 */
static int pruss_cfg_update(struct pruss *pruss, unsigned int reg,
			    unsigned int mask, unsigned int val)
{
	if (IS_ERR_OR_NULL(pruss))
		return -EINVAL;

	return regmap_update_bits(pruss->cfg_regmap, reg, mask, val);
}

#endif  /* _SOC_TI_PRUSS_H_ */
