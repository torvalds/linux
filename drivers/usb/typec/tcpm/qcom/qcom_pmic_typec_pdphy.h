/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */
#ifndef __QCOM_PMIC_PDPHY_H__
#define __QCOM_PMIC_PDPHY_H__

#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Resources */
#define PMIC_PDPHY_MAX_IRQS		0x08

struct pmic_typec_pdphy_irq_params {
	int				virq;
	char				*irq_name;
};

struct pmic_typec_pdphy_resources {
	unsigned int				nr_irqs;
	const struct pmic_typec_pdphy_irq_params	irq_params[PMIC_PDPHY_MAX_IRQS];
};

/* API */
struct pmic_typec_pdphy;

extern const struct pmic_typec_pdphy_resources pm8150b_pdphy_res;
int qcom_pmic_typec_pdphy_probe(struct platform_device *pdev,
				struct pmic_typec *tcpm,
				const struct pmic_typec_pdphy_resources *res,
				struct regmap *regmap,
				u32 base);
int qcom_pmic_typec_pdphy_stub_probe(struct platform_device *pdev,
				     struct pmic_typec *tcpm);

#endif /* __QCOM_PMIC_TYPEC_PDPHY_H__ */
