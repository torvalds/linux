// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI AM33XX SRAM EMIF Driver
 *
 * Copyright (C) 2016-2017 Texas Instruments Inc.
 *	Dave Gerlach
 */

#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sram.h>
#include <linux/ti-emif-sram.h>

#include "emif.h"

#define TI_EMIF_SRAM_SYMBOL_OFFSET(sym) ((unsigned long)(sym) - \
					 (unsigned long)&ti_emif_sram)

#define EMIF_POWER_MGMT_WAIT_SELF_REFRESH_8192_CYCLES		0x00a0

struct ti_emif_data {
	phys_addr_t ti_emif_sram_phys;
	phys_addr_t ti_emif_sram_data_phys;
	unsigned long ti_emif_sram_virt;
	unsigned long ti_emif_sram_data_virt;
	struct gen_pool *sram_pool_code;
	struct gen_pool	*sram_pool_data;
	struct ti_emif_pm_data pm_data;
	struct ti_emif_pm_functions pm_functions;
};

static struct ti_emif_data *emif_instance;

static u32 sram_suspend_address(struct ti_emif_data *emif_data,
				unsigned long addr)
{
	return (emif_data->ti_emif_sram_virt +
		TI_EMIF_SRAM_SYMBOL_OFFSET(addr));
}

static phys_addr_t sram_resume_address(struct ti_emif_data *emif_data,
				       unsigned long addr)
{
	return ((unsigned long)emif_data->ti_emif_sram_phys +
		TI_EMIF_SRAM_SYMBOL_OFFSET(addr));
}

static void ti_emif_free_sram(struct ti_emif_data *emif_data)
{
	gen_pool_free(emif_data->sram_pool_code, emif_data->ti_emif_sram_virt,
		      ti_emif_sram_sz);
	gen_pool_free(emif_data->sram_pool_data,
		      emif_data->ti_emif_sram_data_virt,
		      sizeof(struct emif_regs_amx3));
}

static int ti_emif_alloc_sram(struct device *dev,
			      struct ti_emif_data *emif_data)
{
	struct device_node *np = dev->of_node;
	int ret;

	emif_data->sram_pool_code = of_gen_pool_get(np, "sram", 0);
	if (!emif_data->sram_pool_code) {
		dev_err(dev, "Unable to get sram pool for ocmcram code\n");
		return -ENODEV;
	}

	emif_data->ti_emif_sram_virt =
			gen_pool_alloc(emif_data->sram_pool_code,
				       ti_emif_sram_sz);
	if (!emif_data->ti_emif_sram_virt) {
		dev_err(dev, "Unable to allocate code memory from ocmcram\n");
		return -ENOMEM;
	}

	/* Save physical address to calculate resume offset during pm init */
	emif_data->ti_emif_sram_phys =
			gen_pool_virt_to_phys(emif_data->sram_pool_code,
					      emif_data->ti_emif_sram_virt);

	/* Get sram pool for data section and allocate space */
	emif_data->sram_pool_data = of_gen_pool_get(np, "sram", 1);
	if (!emif_data->sram_pool_data) {
		dev_err(dev, "Unable to get sram pool for ocmcram data\n");
		ret = -ENODEV;
		goto err_free_sram_code;
	}

	emif_data->ti_emif_sram_data_virt =
				gen_pool_alloc(emif_data->sram_pool_data,
					       sizeof(struct emif_regs_amx3));
	if (!emif_data->ti_emif_sram_data_virt) {
		dev_err(dev, "Unable to allocate data memory from ocmcram\n");
		ret = -ENOMEM;
		goto err_free_sram_code;
	}

	/* Save physical address to calculate resume offset during pm init */
	emif_data->ti_emif_sram_data_phys =
		gen_pool_virt_to_phys(emif_data->sram_pool_data,
				      emif_data->ti_emif_sram_data_virt);
	/*
	 * These functions are called during suspend path while MMU is
	 * still on so add virtual base to offset for absolute address
	 */
	emif_data->pm_functions.save_context =
		sram_suspend_address(emif_data,
				     (unsigned long)ti_emif_save_context);
	emif_data->pm_functions.enter_sr =
		sram_suspend_address(emif_data,
				     (unsigned long)ti_emif_enter_sr);
	emif_data->pm_functions.abort_sr =
		sram_suspend_address(emif_data,
				     (unsigned long)ti_emif_abort_sr);

	/*
	 * These are called during resume path when MMU is not enabled
	 * so physical address is used instead
	 */
	emif_data->pm_functions.restore_context =
		sram_resume_address(emif_data,
				    (unsigned long)ti_emif_restore_context);
	emif_data->pm_functions.exit_sr =
		sram_resume_address(emif_data,
				    (unsigned long)ti_emif_exit_sr);
	emif_data->pm_functions.run_hw_leveling =
		sram_resume_address(emif_data,
				    (unsigned long)ti_emif_run_hw_leveling);

	emif_data->pm_data.regs_virt =
		(struct emif_regs_amx3 *)emif_data->ti_emif_sram_data_virt;
	emif_data->pm_data.regs_phys = emif_data->ti_emif_sram_data_phys;

	return 0;

err_free_sram_code:
	gen_pool_free(emif_data->sram_pool_code, emif_data->ti_emif_sram_virt,
		      ti_emif_sram_sz);
	return ret;
}

static int ti_emif_push_sram(struct device *dev, struct ti_emif_data *emif_data)
{
	void *copy_addr;
	u32 data_addr;

	copy_addr = sram_exec_copy(emif_data->sram_pool_code,
				   (void *)emif_data->ti_emif_sram_virt,
				   &ti_emif_sram, ti_emif_sram_sz);
	if (!copy_addr) {
		dev_err(dev, "Cannot copy emif code to sram\n");
		return -ENODEV;
	}

	data_addr = sram_suspend_address(emif_data,
					 (unsigned long)&ti_emif_pm_sram_data);
	copy_addr = sram_exec_copy(emif_data->sram_pool_code,
				   (void *)data_addr,
				   &emif_data->pm_data,
				   sizeof(emif_data->pm_data));
	if (!copy_addr) {
		dev_err(dev, "Cannot copy emif data to code sram\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * Due to Usage Note 3.1.2 "DDR3: JEDEC Compliance for Maximum
 * Self-Refresh Command Limit" found in AM335x Silicon Errata
 * (Document SPRZ360F Revised November 2013) we must configure
 * the self refresh delay timer to 0xA (8192 cycles) to avoid
 * generating too many refresh command from the EMIF.
 */
static void ti_emif_configure_sr_delay(struct ti_emif_data *emif_data)
{
	writel(EMIF_POWER_MGMT_WAIT_SELF_REFRESH_8192_CYCLES,
	       (emif_data->pm_data.ti_emif_base_addr_virt +
		EMIF_POWER_MANAGEMENT_CONTROL));

	writel(EMIF_POWER_MGMT_WAIT_SELF_REFRESH_8192_CYCLES,
	       (emif_data->pm_data.ti_emif_base_addr_virt +
		EMIF_POWER_MANAGEMENT_CTRL_SHDW));
}

/**
 * ti_emif_copy_pm_function_table - copy mapping of pm funcs in sram
 * @sram_pool: pointer to struct gen_pool where dst resides
 * @dst: void * to address that table should be copied
 *
 * Returns 0 if success other error code if table is not available
 */
int ti_emif_copy_pm_function_table(struct gen_pool *sram_pool, void *dst)
{
	void *copy_addr;

	if (!emif_instance)
		return -ENODEV;

	copy_addr = sram_exec_copy(sram_pool, dst,
				   &emif_instance->pm_functions,
				   sizeof(emif_instance->pm_functions));
	if (!copy_addr)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_GPL(ti_emif_copy_pm_function_table);

/**
 * ti_emif_get_mem_type - return type for memory type in use
 *
 * Returns memory type value read from EMIF or error code if fails
 */
int ti_emif_get_mem_type(void)
{
	unsigned long temp;

	if (!emif_instance)
		return -ENODEV;

	temp = readl(emif_instance->pm_data.ti_emif_base_addr_virt +
		     EMIF_SDRAM_CONFIG);

	temp = (temp & SDRAM_TYPE_MASK) >> SDRAM_TYPE_SHIFT;
	return temp;
}
EXPORT_SYMBOL_GPL(ti_emif_get_mem_type);

static const struct of_device_id ti_emif_of_match[] = {
	{ .compatible = "ti,emif-am3352", .data =
					(void *)EMIF_SRAM_AM33_REG_LAYOUT, },
	{ .compatible = "ti,emif-am4372", .data =
					(void *)EMIF_SRAM_AM43_REG_LAYOUT, },
	{},
};
MODULE_DEVICE_TABLE(of, ti_emif_of_match);

#ifdef CONFIG_PM_SLEEP
static int ti_emif_resume(struct device *dev)
{
	unsigned long tmp =
			__raw_readl((void *)emif_instance->ti_emif_sram_virt);

	/*
	 * Check to see if what we are copying is already present in the
	 * first byte at the destination, only copy if it is not which
	 * indicates we have lost context and sram no longer contains
	 * the PM code
	 */
	if (tmp != ti_emif_sram)
		ti_emif_push_sram(dev, emif_instance);

	return 0;
}

static int ti_emif_suspend(struct device *dev)
{
	/*
	 * The contents will be present in DDR hence no need to
	 * explicitly save
	 */
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static int ti_emif_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct ti_emif_data *emif_data;

	emif_data = devm_kzalloc(dev, sizeof(*emif_data), GFP_KERNEL);
	if (!emif_data)
		return -ENOMEM;

	match = of_match_device(ti_emif_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	emif_data->pm_data.ti_emif_sram_config = (unsigned long)match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	emif_data->pm_data.ti_emif_base_addr_virt = devm_ioremap_resource(dev,
									  res);
	if (IS_ERR(emif_data->pm_data.ti_emif_base_addr_virt)) {
		ret = PTR_ERR(emif_data->pm_data.ti_emif_base_addr_virt);
		return ret;
	}

	emif_data->pm_data.ti_emif_base_addr_phys = res->start;

	ti_emif_configure_sr_delay(emif_data);

	ret = ti_emif_alloc_sram(dev, emif_data);
	if (ret)
		return ret;

	ret = ti_emif_push_sram(dev, emif_data);
	if (ret)
		goto fail_free_sram;

	emif_instance = emif_data;

	return 0;

fail_free_sram:
	ti_emif_free_sram(emif_data);

	return ret;
}

static int ti_emif_remove(struct platform_device *pdev)
{
	struct ti_emif_data *emif_data = emif_instance;

	emif_instance = NULL;

	ti_emif_free_sram(emif_data);

	return 0;
}

static const struct dev_pm_ops ti_emif_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ti_emif_suspend, ti_emif_resume)
};

static struct platform_driver ti_emif_driver = {
	.probe = ti_emif_probe,
	.remove = ti_emif_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(ti_emif_of_match),
		.pm = &ti_emif_pm_ops,
	},
};
module_platform_driver(ti_emif_driver);

MODULE_AUTHOR("Dave Gerlach <d-gerlach@ti.com>");
MODULE_DESCRIPTION("Texas Instruments SRAM EMIF driver");
MODULE_LICENSE("GPL v2");
