/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DRIVERS_MTD_NAND_INGENIC_ECC_INTERNAL_H__
#define __DRIVERS_MTD_NAND_INGENIC_ECC_INTERNAL_H__

#include <linux/compiler_types.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <uapi/asm-generic/errno-base.h>

struct clk;
struct device;
struct ingenic_ecc;
struct platform_device;

/**
 * struct ingenic_ecc_params - ECC parameters
 * @size: data bytes per ECC step.
 * @bytes: ECC bytes per step.
 * @strength: number of correctable bits per ECC step.
 */
struct ingenic_ecc_params {
	int size;
	int bytes;
	int strength;
};

#if IS_ENABLED(CONFIG_MTD_NAND_INGENIC_ECC)
int ingenic_ecc_calculate(struct ingenic_ecc *ecc,
			  struct ingenic_ecc_params *params,
			  const u8 *buf, u8 *ecc_code);
int ingenic_ecc_correct(struct ingenic_ecc *ecc,
			struct ingenic_ecc_params *params, u8 *buf,
			u8 *ecc_code);

void ingenic_ecc_release(struct ingenic_ecc *ecc);
struct ingenic_ecc *of_ingenic_ecc_get(struct device_node *np);
#else /* CONFIG_MTD_NAND_INGENIC_ECC */
static inline int ingenic_ecc_calculate(struct ingenic_ecc *ecc,
			  struct ingenic_ecc_params *params,
			  const u8 *buf, u8 *ecc_code)
{
	return -ENODEV;
}

static inline int ingenic_ecc_correct(struct ingenic_ecc *ecc,
			struct ingenic_ecc_params *params, u8 *buf,
			u8 *ecc_code)
{
	return -ENODEV;
}

static inline void ingenic_ecc_release(struct ingenic_ecc *ecc)
{
}

static inline struct ingenic_ecc *of_ingenic_ecc_get(struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}
#endif /* CONFIG_MTD_NAND_INGENIC_ECC */

struct ingenic_ecc_ops {
	void (*disable)(struct ingenic_ecc *ecc);
	int (*calculate)(struct ingenic_ecc *ecc,
			 struct ingenic_ecc_params *params,
			 const u8 *buf, u8 *ecc_code);
	int (*correct)(struct ingenic_ecc *ecc,
			struct ingenic_ecc_params *params,
			u8 *buf, u8 *ecc_code);
};

struct ingenic_ecc {
	struct device *dev;
	const struct ingenic_ecc_ops *ops;
	void __iomem *base;
	struct clk *clk;
	struct mutex lock;
};

int ingenic_ecc_probe(struct platform_device *pdev);

#endif /* __DRIVERS_MTD_NAND_INGENIC_ECC_INTERNAL_H__ */
