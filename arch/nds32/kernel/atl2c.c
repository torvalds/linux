// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/compiler.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <asm/l2_cache.h>

void __iomem *atl2c_base;
static const struct of_device_id atl2c_ids[] __initconst = {
	{.compatible = "andestech,atl2c",},
	{}
};

static int __init atl2c_of_init(void)
{
	struct device_node *np;
	struct resource res;
	unsigned long tmp = 0;
	unsigned long l2set, l2way, l2clsz;

	if (!(__nds32__mfsr(NDS32_SR_MSC_CFG) & MSC_CFG_mskL2C))
		return -ENODEV;

	np = of_find_matching_node(NULL, atl2c_ids);
	if (!np)
		return -ENODEV;

	if (of_address_to_resource(np, 0, &res))
		return -ENODEV;

	atl2c_base = ioremap(res.start, resource_size(&res));
	if (!atl2c_base)
		return -ENOMEM;

	l2set =
	    64 << ((L2C_R_REG(L2_CA_CONF_OFF) & L2_CA_CONF_mskL2SET) >>
		   L2_CA_CONF_offL2SET);
	l2way =
	    1 +
	    ((L2C_R_REG(L2_CA_CONF_OFF) & L2_CA_CONF_mskL2WAY) >>
	     L2_CA_CONF_offL2WAY);
	l2clsz =
	    4 << ((L2C_R_REG(L2_CA_CONF_OFF) & L2_CA_CONF_mskL2CLSZ) >>
		  L2_CA_CONF_offL2CLSZ);
	pr_info("L2:%luKB/%luS/%luW/%luB\n",
		l2set * l2way * l2clsz / 1024, l2set, l2way, l2clsz);

	tmp = L2C_R_REG(L2CC_PROT_OFF);
	tmp &= ~L2CC_PROT_mskMRWEN;
	L2C_W_REG(L2CC_PROT_OFF, tmp);

	tmp = L2C_R_REG(L2CC_SETUP_OFF);
	tmp &= ~L2CC_SETUP_mskPART;
	L2C_W_REG(L2CC_SETUP_OFF, tmp);

	tmp = L2C_R_REG(L2CC_CTRL_OFF);
	tmp |= L2CC_CTRL_mskEN;
	L2C_W_REG(L2CC_CTRL_OFF, tmp);

	return 0;
}

subsys_initcall(atl2c_of_init);
