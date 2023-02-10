// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/of_platform.h>
#include <coresight-priv.h>
#include <coresight-etm4x.h>
#include "sources/coresight_mali_sources.h"

#define CS_ETM_BASE_ADDR 0xE0041000
#define CS_MALI_TRACE_ID 0x00000010

#ifndef TRCVICTLR_SSSTATUS
#define TRCVICTLR_SSSTATUS BIT(9)
#endif

#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
static char *type_name = "mali-source-etm";
#endif

#define NELEMS(s) (sizeof(s) / sizeof((s)[0]))

enum cs_etm_dynamic_regs {
	CS_ETM_TRCCONFIGR,
	CS_ETM_TRCTRACEIDR,
	CS_ETM_TRCVDARCCTLR,
	CS_ETM_TRCSTALLCTLR,
	CS_ETM_TRCVIIECTLR,
	CS_ETM_NR_DYN_REGS
};

struct cs_etm_state {
	int enabled;
	u32 regs[CS_ETM_NR_DYN_REGS];
};

static struct cs_etm_state etm_state = { 0 };

static struct kbase_debug_coresight_csf_address_range etm_range[] = {
	{ CS_ETM_BASE_ADDR, CS_ETM_BASE_ADDR + CORESIGHT_DEVTYPE },
};

struct kbase_debug_coresight_csf_op etm_enable_ops[] = {
	// Unlock ETM configuration
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + CORESIGHT_LAR, CS_MALI_UNLOCK_COMPONENT),
	// Power up request
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCPDCR, TRCPDCR_PU),
	// Disable Tracing
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCPRGCTLR, 0x00000000),
	// Check the tracing unit is inactive before programming
	POLL_OP(CS_ETM_BASE_ADDR + TRCSTATR, BIT(TRCSTATR_IDLE_BIT), BIT(TRCSTATR_IDLE_BIT)),
	// Set trace configuration to enable global timestamping, and data value tracing
	WRITE_PTR_OP(CS_ETM_BASE_ADDR + TRCCONFIGR, &etm_state.regs[CS_ETM_TRCCONFIGR]),
	// Set event control 0 register
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCEVENTCTL0R, 0x00000000),
	// Set event control 1 register
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCEVENTCTL1R, 0x00000000),
	// Set trace ID
	WRITE_PTR_OP(CS_ETM_BASE_ADDR + TRCTRACEIDR, &etm_state.regs[CS_ETM_TRCTRACEIDR]),
	// Configure stall control register
	WRITE_PTR_OP(CS_ETM_BASE_ADDR + TRCSTALLCTLR, &etm_state.regs[CS_ETM_TRCSTALLCTLR]),
	// Synchronization period register - sync every 2^11 bytes
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCSYNCPR, 0x0000000C),
	// Set global timestamp control register to select resource 0
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCTSCTLR, 0x00000000),
	// Set viewData include/exclude address range comparators to 0
	WRITE_PTR_OP(CS_ETM_BASE_ADDR + TRCVDARCCTLR, &etm_state.regs[CS_ETM_TRCVDARCCTLR]),
	// Set viewData main control to select resource 0
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCVDCTLR, 0x00000001),
	//Set viewData comparators to 0
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCVDSACCTLR, 0x00000000),
	// Set stop/start logic to started state, select resource 1
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCVICTLR, TRCVICTLR_SSSTATUS | BIT(0)),
	// Set viewInst start and stop control
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCVISSCTLR, 0x00000000),
	// Set viewInst include and exclude control to math all addresses in range
	WRITE_PTR_OP(CS_ETM_BASE_ADDR + TRCVIIECTLR, &etm_state.regs[CS_ETM_TRCVIIECTLR]),
	// enable trace
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCPRGCTLR, 0x1),
	// Wait that the unit is busy
	POLL_OP(CS_ETM_BASE_ADDR + TRCSTATR, BIT(TRCSTATR_IDLE_BIT), 0),
	// Lock the ETM configuration
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + CORESIGHT_LAR, 0x00000000),
	// Set enabled bit on at the end of sequence
	BIT_OR_OP(&etm_state.enabled, 0x1),
};

struct kbase_debug_coresight_csf_op etm_disable_ops[] = {
	// Unlock ETM configuration
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + CORESIGHT_LAR, CS_MALI_UNLOCK_COMPONENT),
	// Disable trace unit
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + TRCPRGCTLR, 0x00000000),
	// Poll until idle
	POLL_OP(CS_ETM_BASE_ADDR + TRCSTATR, BIT(TRCSTATR_IDLE_BIT), BIT(TRCSTATR_IDLE_BIT)),
	// Lock ETM configuration
	WRITE_IMM_OP(CS_ETM_BASE_ADDR + CORESIGHT_LAR, 0x00000000),
	// Set enabled bit off at the end of sequence
	BIT_AND_OP(&etm_state.enabled, 0x0),
};

static void set_default_regs(void)
{
	// Turn on instruction tracing
	etm_state.regs[CS_ETM_TRCCONFIGR] = 0x00000800;
	// Set ID
	etm_state.regs[CS_ETM_TRCTRACEIDR] = CS_MALI_TRACE_ID;
	// Set data comparators to none
	etm_state.regs[CS_ETM_TRCVDARCCTLR] = 0x00000000;
	// Set instructions address filter to none
	etm_state.regs[CS_ETM_TRCVIIECTLR] = 0x00000000;
	// Set stall configuration to a basic setting
	etm_state.regs[CS_ETM_TRCSTALLCTLR] = 0x00000000;
}

static const struct of_device_id mali_source_ids[] = { { .compatible =
								 "arm,coresight-mali-source-etm" },
						       {} };

int coresight_mali_sources_init_drvdata(struct coresight_mali_source_drvdata *drvdata)
{
	int ret = 0;

	if (drvdata == NULL)
		return -EINVAL;

#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
	drvdata->type_name = type_name;
#endif
	etm_state.enabled = 0x0;

	drvdata->base.kbase_client = kbase_debug_coresight_csf_register(
		drvdata->base.gpu_dev, etm_range, NELEMS(etm_range));
	if (drvdata->base.kbase_client == NULL) {
		dev_err(drvdata->base.dev, "Registration with full range failed unexpectedly\n");
		return -EINVAL;
	}

	set_default_regs();
	drvdata->trcid = CS_MALI_TRACE_ID;

	drvdata->base.enable_seq.ops = etm_enable_ops;
	drvdata->base.enable_seq.nr_ops = NELEMS(etm_enable_ops);

	drvdata->base.disable_seq.ops = etm_disable_ops;
	drvdata->base.disable_seq.nr_ops = NELEMS(etm_disable_ops);

	drvdata->base.config = kbase_debug_coresight_csf_config_create(
		drvdata->base.kbase_client, &drvdata->base.enable_seq, &drvdata->base.disable_seq);
	if (!drvdata->base.config) {
		dev_err(drvdata->base.dev, "Config create failed unexpectedly\n");
		kbase_debug_coresight_csf_unregister(drvdata->base.kbase_client);
		return -EINVAL;
	}

	return ret;
}

void coresight_mali_sources_deinit_drvdata(struct coresight_mali_source_drvdata *drvdata)
{
	if (drvdata->base.config != NULL)
		kbase_debug_coresight_csf_config_free(drvdata->base.config);

	if (drvdata->base.kbase_client != NULL)
		kbase_debug_coresight_csf_unregister(drvdata->base.kbase_client);
}

static int verify_store_reg(struct device *dev, const char *buf, size_t count, int reg)
{
	struct coresight_mali_source_drvdata *drvdata = dev_get_drvdata(dev->parent);
	u32 val;
	int err;

	if (buf == NULL)
		return -EINVAL;

	if (etm_state.enabled == 1) {
		dev_err(drvdata->base.dev,
			"Config needs to be disabled before modifying registers\n");
		return -EINVAL;
	}

	err = kstrtou32(buf, 0, &val);
	if (err) {
		dev_err(drvdata->base.dev, "Invalid input value\n");
		return -EINVAL;
	}

	etm_state.regs[reg] = val;
	return count;
}

#define CS_ETM_REG_ATTR_RW(_a, _b)                                                                 \
	static ssize_t _a##_show(struct device *dev, struct device_attribute *attr,                \
				 char *const buf)                                                  \
	{                                                                                          \
		return sprintf(buf, "%#x\n", etm_state.regs[CS_ETM_##_b]);                         \
	}                                                                                          \
	static ssize_t _a##_store(struct device *dev, struct device_attribute *attr,               \
				  const char *buf, size_t count)                                   \
	{                                                                                          \
		return verify_store_reg(dev, buf, count, CS_ETM_##_b);                             \
	}                                                                                          \
	static DEVICE_ATTR_RW(_a)

CS_ETM_REG_ATTR_RW(trcconfigr, TRCCONFIGR);
CS_ETM_REG_ATTR_RW(trctraceidr, TRCTRACEIDR);
CS_ETM_REG_ATTR_RW(trcvdarcctlr, TRCVDARCCTLR);
CS_ETM_REG_ATTR_RW(trcviiectlr, TRCVIIECTLR);
CS_ETM_REG_ATTR_RW(trcstallctlr, TRCSTALLCTLR);

static ssize_t is_enabled_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	return sprintf(buf, "%d\n", etm_state.enabled);
}
static DEVICE_ATTR_RO(is_enabled);

static struct attribute *coresight_etm_attrs[] = {
	&dev_attr_is_enabled.attr,
	&dev_attr_trcconfigr.attr,
	&dev_attr_trctraceidr.attr,
	&dev_attr_trcvdarcctlr.attr,
	&dev_attr_trcviiectlr.attr,
	&dev_attr_trcstallctlr.attr,
	NULL,
};
static struct attribute_group coresight_etm_group = { .attrs = coresight_etm_attrs,
						      .name = "mgmt" };
static const struct attribute_group *coresight_etm_groups[] = {
	&coresight_etm_group,
	NULL,
};
const struct attribute_group **coresight_mali_source_groups_get(void)
{
	return coresight_etm_groups;
}

static struct platform_driver mali_sources_platform_driver = {
	.probe      = coresight_mali_sources_probe,
	.remove     = coresight_mali_sources_remove,
	.driver = {
		.name = "coresight-mali-source-etm",
		.owner = THIS_MODULE,
		.of_match_table = mali_source_ids,
		.suppress_bind_attrs    = true,
	},
};

static int __init mali_sources_init(void)
{
	return platform_driver_register(&mali_sources_platform_driver);
}

static void __exit mali_sources_exit(void)
{
	platform_driver_unregister(&mali_sources_platform_driver);
}

module_init(mali_sources_init);
module_exit(mali_sources_exit);

MODULE_AUTHOR("ARM Ltd.");
MODULE_DESCRIPTION("Arm Coresight Mali source ETM");
MODULE_LICENSE("GPL");
