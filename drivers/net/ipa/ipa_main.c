// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/bug.h>
#include <linux/io.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/remoteproc.h>
#include <linux/qcom_scm.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "ipa.h"
#include "ipa_clock.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_cmd.h"
#include "ipa_reg.h"
#include "ipa_mem.h"
#include "ipa_table.h"
#include "ipa_modem.h"
#include "ipa_uc.h"
#include "ipa_interrupt.h"
#include "gsi_trans.h"

/**
 * DOC: The IP Accelerator
 *
 * This driver supports the Qualcomm IP Accelerator (IPA), which is a
 * networking component found in many Qualcomm SoCs.  The IPA is connected
 * to the application processor (AP), but is also connected (and partially
 * controlled by) other "execution environments" (EEs), such as a modem.
 *
 * The IPA is the conduit between the AP and the modem that carries network
 * traffic.  This driver presents a network interface representing the
 * connection of the modem to external (e.g. LTE) networks.
 *
 * The IPA provides protocol checksum calculation, offloading this work
 * from the AP.  The IPA offers additional functionality, including routing,
 * filtering, and NAT support, but that more advanced functionality is not
 * currently supported.  Despite that, some resources--including routing
 * tables and filter tables--are defined in this driver because they must
 * be initialized even when the advanced hardware features are not used.
 *
 * There are two distinct layers that implement the IPA hardware, and this
 * is reflected in the organization of the driver.  The generic software
 * interface (GSI) is an integral component of the IPA, providing a
 * well-defined communication layer between the AP subsystem and the IPA
 * core.  The GSI implements a set of "channels" used for communication
 * between the AP and the IPA.
 *
 * The IPA layer uses GSI channels to implement its "endpoints".  And while
 * a GSI channel carries data between the AP and the IPA, a pair of IPA
 * endpoints is used to carry traffic between two EEs.  Specifically, the main
 * modem network interface is implemented by two pairs of endpoints:  a TX
 * endpoint on the AP coupled with an RX endpoint on the modem; and another
 * RX endpoint on the AP receiving data from a TX endpoint on the modem.
 */

/* The name of the GSI firmware file relative to /lib/firmware */
#define IPA_FWS_PATH		"ipa_fws.mdt"
#define IPA_PAS_ID		15

/**
 * ipa_suspend_handler() - Handle the suspend IPA interrupt
 * @ipa:	IPA pointer
 * @irq_id:	IPA interrupt type (unused)
 *
 * If an RX endpoint is in suspend state, and the IPA has a packet
 * destined for that endpoint, the IPA generates a SUSPEND interrupt
 * to inform the AP that it should resume the endpoint.  If we get
 * one of these interrupts we just resume everything.
 */
static void ipa_suspend_handler(struct ipa *ipa, enum ipa_irq_id irq_id)
{
	/* Just report the event, and let system resume handle the rest.
	 * More than one endpoint could signal this; if so, ignore
	 * all but the first.
	 */
	if (!test_and_set_bit(IPA_FLAG_RESUMED, ipa->flags))
		pm_wakeup_dev_event(&ipa->pdev->dev, 0, true);

	/* Acknowledge/clear the suspend interrupt on all endpoints */
	ipa_interrupt_suspend_clear_all(ipa->interrupt);
}

/**
 * ipa_setup() - Set up IPA hardware
 * @ipa:	IPA pointer
 *
 * Perform initialization that requires issuing immediate commands on
 * the command TX endpoint.  If the modem is doing GSI firmware load
 * and initialization, this function will be called when an SMP2P
 * interrupt has been signaled by the modem.  Otherwise it will be
 * called from ipa_probe() after GSI firmware has been successfully
 * loaded, authenticated, and started by Trust Zone.
 */
int ipa_setup(struct ipa *ipa)
{
	struct ipa_endpoint *exception_endpoint;
	struct ipa_endpoint *command_endpoint;
	struct device *dev = &ipa->pdev->dev;
	int ret;

	/* Setup for IPA v3.5.1 has some slight differences */
	ret = gsi_setup(&ipa->gsi, ipa->version == IPA_VERSION_3_5_1);
	if (ret)
		return ret;

	ipa->interrupt = ipa_interrupt_setup(ipa);
	if (IS_ERR(ipa->interrupt)) {
		ret = PTR_ERR(ipa->interrupt);
		goto err_gsi_teardown;
	}
	ipa_interrupt_add(ipa->interrupt, IPA_IRQ_TX_SUSPEND,
			  ipa_suspend_handler);

	ipa_uc_setup(ipa);

	ret = device_init_wakeup(dev, true);
	if (ret)
		goto err_uc_teardown;

	ipa_endpoint_setup(ipa);

	/* We need to use the AP command TX endpoint to perform other
	 * initialization, so we enable first.
	 */
	command_endpoint = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX];
	ret = ipa_endpoint_enable_one(command_endpoint);
	if (ret)
		goto err_endpoint_teardown;

	ret = ipa_mem_setup(ipa);
	if (ret)
		goto err_command_disable;

	ret = ipa_table_setup(ipa);
	if (ret)
		goto err_mem_teardown;

	/* Enable the exception handling endpoint, and tell the hardware
	 * to use it by default.
	 */
	exception_endpoint = ipa->name_map[IPA_ENDPOINT_AP_LAN_RX];
	ret = ipa_endpoint_enable_one(exception_endpoint);
	if (ret)
		goto err_table_teardown;

	ipa_endpoint_default_route_set(ipa, exception_endpoint->endpoint_id);

	/* We're all set.  Now prepare for communication with the modem */
	ret = ipa_modem_setup(ipa);
	if (ret)
		goto err_default_route_clear;

	ipa->setup_complete = true;

	dev_info(dev, "IPA driver setup completed successfully\n");

	return 0;

err_default_route_clear:
	ipa_endpoint_default_route_clear(ipa);
	ipa_endpoint_disable_one(exception_endpoint);
err_table_teardown:
	ipa_table_teardown(ipa);
err_mem_teardown:
	ipa_mem_teardown(ipa);
err_command_disable:
	ipa_endpoint_disable_one(command_endpoint);
err_endpoint_teardown:
	ipa_endpoint_teardown(ipa);
	(void)device_init_wakeup(dev, false);
err_uc_teardown:
	ipa_uc_teardown(ipa);
	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_TX_SUSPEND);
	ipa_interrupt_teardown(ipa->interrupt);
err_gsi_teardown:
	gsi_teardown(&ipa->gsi);

	return ret;
}

/**
 * ipa_teardown() - Inverse of ipa_setup()
 * @ipa:	IPA pointer
 */
static void ipa_teardown(struct ipa *ipa)
{
	struct ipa_endpoint *exception_endpoint;
	struct ipa_endpoint *command_endpoint;

	ipa_modem_teardown(ipa);
	ipa_endpoint_default_route_clear(ipa);
	exception_endpoint = ipa->name_map[IPA_ENDPOINT_AP_LAN_RX];
	ipa_endpoint_disable_one(exception_endpoint);
	ipa_table_teardown(ipa);
	ipa_mem_teardown(ipa);
	command_endpoint = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX];
	ipa_endpoint_disable_one(command_endpoint);
	ipa_endpoint_teardown(ipa);
	(void)device_init_wakeup(&ipa->pdev->dev, false);
	ipa_uc_teardown(ipa);
	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_TX_SUSPEND);
	ipa_interrupt_teardown(ipa->interrupt);
	gsi_teardown(&ipa->gsi);
}

/* Configure QMB Core Master Port selection */
static void ipa_hardware_config_comp(struct ipa *ipa)
{
	u32 val;

	/* Nothing to configure for IPA v3.5.1 */
	if (ipa->version == IPA_VERSION_3_5_1)
		return;

	val = ioread32(ipa->reg_virt + IPA_REG_COMP_CFG_OFFSET);

	if (ipa->version == IPA_VERSION_4_0) {
		val &= ~IPA_QMB_SELECT_CONS_EN_FMASK;
		val &= ~IPA_QMB_SELECT_PROD_EN_FMASK;
		val &= ~IPA_QMB_SELECT_GLOBAL_EN_FMASK;
	} else  {
		val |= GSI_MULTI_AXI_MASTERS_DIS_FMASK;
	}

	val |= GSI_MULTI_INORDER_RD_DIS_FMASK;
	val |= GSI_MULTI_INORDER_WR_DIS_FMASK;

	iowrite32(val, ipa->reg_virt + IPA_REG_COMP_CFG_OFFSET);
}

/* Configure DDR and PCIe max read/write QSB values */
static void ipa_hardware_config_qsb(struct ipa *ipa)
{
	u32 val;

	/* QMB_0 represents DDR; QMB_1 represents PCIe (not present in 4.2) */
	val = u32_encode_bits(8, GEN_QMB_0_MAX_WRITES_FMASK);
	if (ipa->version == IPA_VERSION_4_2)
		val |= u32_encode_bits(0, GEN_QMB_1_MAX_WRITES_FMASK);
	else
		val |= u32_encode_bits(4, GEN_QMB_1_MAX_WRITES_FMASK);
	iowrite32(val, ipa->reg_virt + IPA_REG_QSB_MAX_WRITES_OFFSET);

	if (ipa->version == IPA_VERSION_3_5_1) {
		val = u32_encode_bits(8, GEN_QMB_0_MAX_READS_FMASK);
		val |= u32_encode_bits(12, GEN_QMB_1_MAX_READS_FMASK);
	} else {
		val = u32_encode_bits(12, GEN_QMB_0_MAX_READS_FMASK);
		if (ipa->version == IPA_VERSION_4_2)
			val |= u32_encode_bits(0, GEN_QMB_1_MAX_READS_FMASK);
		else
			val |= u32_encode_bits(12, GEN_QMB_1_MAX_READS_FMASK);
		/* GEN_QMB_0_MAX_READS_BEATS is 0 */
		/* GEN_QMB_1_MAX_READS_BEATS is 0 */
	}
	iowrite32(val, ipa->reg_virt + IPA_REG_QSB_MAX_READS_OFFSET);
}

static void ipa_idle_indication_cfg(struct ipa *ipa,
				    u32 enter_idle_debounce_thresh,
				    bool const_non_idle_enable)
{
	u32 offset;
	u32 val;

	val = u32_encode_bits(enter_idle_debounce_thresh,
			      ENTER_IDLE_DEBOUNCE_THRESH_FMASK);
	if (const_non_idle_enable)
		val |= CONST_NON_IDLE_ENABLE_FMASK;

	offset = ipa_reg_idle_indication_cfg_offset(ipa->version);
	iowrite32(val, ipa->reg_virt + offset);
}

/**
 * ipa_hardware_dcd_config() - Enable dynamic clock division on IPA
 * @ipa:	IPA pointer
 *
 * Configures when the IPA signals it is idle to the global clock
 * controller, which can respond by scalling down the clock to
 * save power.
 */
static void ipa_hardware_dcd_config(struct ipa *ipa)
{
	/* Recommended values for IPA 3.5 according to IPA HPG */
	ipa_idle_indication_cfg(ipa, 256, false);
}

static void ipa_hardware_dcd_deconfig(struct ipa *ipa)
{
	/* Power-on reset values */
	ipa_idle_indication_cfg(ipa, 0, true);
}

/**
 * ipa_hardware_config() - Primitive hardware initialization
 * @ipa:	IPA pointer
 */
static void ipa_hardware_config(struct ipa *ipa)
{
	u32 granularity;
	u32 val;

	/* Fill in backward-compatibility register, based on version */
	val = ipa_reg_bcr_val(ipa->version);
	iowrite32(val, ipa->reg_virt + IPA_REG_BCR_OFFSET);

	if (ipa->version != IPA_VERSION_3_5_1) {
		/* Enable open global clocks (hardware workaround) */
		val = GLOBAL_FMASK;
		val |= GLOBAL_2X_CLK_FMASK;
		iowrite32(val, ipa->reg_virt + IPA_REG_CLKON_CFG_OFFSET);

		/* Disable PA mask to allow HOLB drop (hardware workaround) */
		val = ioread32(ipa->reg_virt + IPA_REG_TX_CFG_OFFSET);
		val &= ~PA_MASK_EN;
		iowrite32(val, ipa->reg_virt + IPA_REG_TX_CFG_OFFSET);
	}

	ipa_hardware_config_comp(ipa);

	/* Configure system bus limits */
	ipa_hardware_config_qsb(ipa);

	/* Configure aggregation granularity */
	val = ioread32(ipa->reg_virt + IPA_REG_COUNTER_CFG_OFFSET);
	granularity = ipa_aggr_granularity_val(IPA_AGGR_GRANULARITY);
	val = u32_encode_bits(granularity, AGGR_GRANULARITY);
	iowrite32(val, ipa->reg_virt + IPA_REG_COUNTER_CFG_OFFSET);

	/* Disable hashed IPv4 and IPv6 routing and filtering for IPA v4.2 */
	if (ipa->version == IPA_VERSION_4_2)
		iowrite32(0, ipa->reg_virt + IPA_REG_FILT_ROUT_HASH_EN_OFFSET);

	/* Enable dynamic clock division */
	ipa_hardware_dcd_config(ipa);
}

/**
 * ipa_hardware_deconfig() - Inverse of ipa_hardware_config()
 * @ipa:	IPA pointer
 *
 * This restores the power-on reset values (even if they aren't different)
 */
static void ipa_hardware_deconfig(struct ipa *ipa)
{
	/* Mostly we just leave things as we set them. */
	ipa_hardware_dcd_deconfig(ipa);
}

#ifdef IPA_VALIDATION

/* # IPA resources used based on version (see IPA_RESOURCE_GROUP_COUNT) */
static int ipa_resource_group_count(struct ipa *ipa)
{
	switch (ipa->version) {
	case IPA_VERSION_3_5_1:
		return 3;

	case IPA_VERSION_4_0:
	case IPA_VERSION_4_1:
		return 4;

	case IPA_VERSION_4_2:
		return 1;

	default:
		return 0;
	}
}

static bool ipa_resource_limits_valid(struct ipa *ipa,
				      const struct ipa_resource_data *data)
{
	u32 group_count = ipa_resource_group_count(ipa);
	u32 i;
	u32 j;

	if (!group_count)
		return false;

	/* Return an error if a non-zero resource group limit is specified
	 * for a resource not supported by hardware.
	 */
	for (i = 0; i < data->resource_src_count; i++) {
		const struct ipa_resource_src *resource;

		resource = &data->resource_src[i];
		for (j = group_count; j < IPA_RESOURCE_GROUP_COUNT; j++)
			if (resource->limits[j].min || resource->limits[j].max)
				return false;
	}

	for (i = 0; i < data->resource_dst_count; i++) {
		const struct ipa_resource_dst *resource;

		resource = &data->resource_dst[i];
		for (j = group_count; j < IPA_RESOURCE_GROUP_COUNT; j++)
			if (resource->limits[j].min || resource->limits[j].max)
				return false;
	}

	return true;
}

#else /* !IPA_VALIDATION */

static bool ipa_resource_limits_valid(struct ipa *ipa,
				      const struct ipa_resource_data *data)
{
	return true;
}

#endif /* !IPA_VALIDATION */

static void
ipa_resource_config_common(struct ipa *ipa, u32 offset,
			   const struct ipa_resource_limits *xlimits,
			   const struct ipa_resource_limits *ylimits)
{
	u32 val;

	val = u32_encode_bits(xlimits->min, X_MIN_LIM_FMASK);
	val |= u32_encode_bits(xlimits->max, X_MAX_LIM_FMASK);
	val |= u32_encode_bits(ylimits->min, Y_MIN_LIM_FMASK);
	val |= u32_encode_bits(ylimits->max, Y_MAX_LIM_FMASK);

	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_resource_config_src_01(struct ipa *ipa,
				       const struct ipa_resource_src *resource)
{
	u32 offset = IPA_REG_SRC_RSRC_GRP_01_RSRC_TYPE_N_OFFSET(resource->type);

	ipa_resource_config_common(ipa, offset,
				   &resource->limits[0], &resource->limits[1]);
}

static void ipa_resource_config_src_23(struct ipa *ipa,
				       const struct ipa_resource_src *resource)
{
	u32 offset = IPA_REG_SRC_RSRC_GRP_23_RSRC_TYPE_N_OFFSET(resource->type);

	ipa_resource_config_common(ipa, offset,
				   &resource->limits[2], &resource->limits[3]);
}

static void ipa_resource_config_dst_01(struct ipa *ipa,
				       const struct ipa_resource_dst *resource)
{
	u32 offset = IPA_REG_DST_RSRC_GRP_01_RSRC_TYPE_N_OFFSET(resource->type);

	ipa_resource_config_common(ipa, offset,
				   &resource->limits[0], &resource->limits[1]);
}

static void ipa_resource_config_dst_23(struct ipa *ipa,
				       const struct ipa_resource_dst *resource)
{
	u32 offset = IPA_REG_DST_RSRC_GRP_23_RSRC_TYPE_N_OFFSET(resource->type);

	ipa_resource_config_common(ipa, offset,
				   &resource->limits[2], &resource->limits[3]);
}

static int
ipa_resource_config(struct ipa *ipa, const struct ipa_resource_data *data)
{
	u32 i;

	if (!ipa_resource_limits_valid(ipa, data))
		return -EINVAL;

	for (i = 0; i < data->resource_src_count; i++) {
		ipa_resource_config_src_01(ipa, &data->resource_src[i]);
		ipa_resource_config_src_23(ipa, &data->resource_src[i]);
	}

	for (i = 0; i < data->resource_dst_count; i++) {
		ipa_resource_config_dst_01(ipa, &data->resource_dst[i]);
		ipa_resource_config_dst_23(ipa, &data->resource_dst[i]);
	}

	return 0;
}

static void ipa_resource_deconfig(struct ipa *ipa)
{
	/* Nothing to do */
}

/**
 * ipa_config() - Configure IPA hardware
 * @ipa:	IPA pointer
 * @data:	IPA configuration data
 *
 * Perform initialization requiring IPA clock to be enabled.
 */
static int ipa_config(struct ipa *ipa, const struct ipa_data *data)
{
	int ret;

	/* Get a clock reference to allow initialization.  This reference
	 * is held after initialization completes, and won't get dropped
	 * unless/until a system suspend request arrives.
	 */
	ipa_clock_get(ipa);

	ipa_hardware_config(ipa);

	ret = ipa_endpoint_config(ipa);
	if (ret)
		goto err_hardware_deconfig;

	ret = ipa_mem_config(ipa);
	if (ret)
		goto err_endpoint_deconfig;

	ipa_table_config(ipa);

	/* Assign resource limitation to each group */
	ret = ipa_resource_config(ipa, data->resource_data);
	if (ret)
		goto err_table_deconfig;

	ret = ipa_modem_config(ipa);
	if (ret)
		goto err_resource_deconfig;

	return 0;

err_resource_deconfig:
	ipa_resource_deconfig(ipa);
err_table_deconfig:
	ipa_table_deconfig(ipa);
	ipa_mem_deconfig(ipa);
err_endpoint_deconfig:
	ipa_endpoint_deconfig(ipa);
err_hardware_deconfig:
	ipa_hardware_deconfig(ipa);
	ipa_clock_put(ipa);

	return ret;
}

/**
 * ipa_deconfig() - Inverse of ipa_config()
 * @ipa:	IPA pointer
 */
static void ipa_deconfig(struct ipa *ipa)
{
	ipa_modem_deconfig(ipa);
	ipa_resource_deconfig(ipa);
	ipa_table_deconfig(ipa);
	ipa_mem_deconfig(ipa);
	ipa_endpoint_deconfig(ipa);
	ipa_hardware_deconfig(ipa);
	ipa_clock_put(ipa);
}

static int ipa_firmware_load(struct device *dev)
{
	const struct firmware *fw;
	struct device_node *node;
	struct resource res;
	phys_addr_t phys;
	ssize_t size;
	void *virt;
	int ret;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(dev, "DT error getting \"memory-region\" property\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &res);
	of_node_put(node);
	if (ret) {
		dev_err(dev, "error %d getting \"memory-region\" resource\n",
			ret);
		return ret;
	}

	ret = request_firmware(&fw, IPA_FWS_PATH, dev);
	if (ret) {
		dev_err(dev, "error %d requesting \"%s\"\n", ret, IPA_FWS_PATH);
		return ret;
	}

	phys = res.start;
	size = (size_t)resource_size(&res);
	virt = memremap(phys, size, MEMREMAP_WC);
	if (!virt) {
		dev_err(dev, "unable to remap firmware memory\n");
		ret = -ENOMEM;
		goto out_release_firmware;
	}

	ret = qcom_mdt_load(dev, fw, IPA_FWS_PATH, IPA_PAS_ID,
			    virt, phys, size, NULL);
	if (ret)
		dev_err(dev, "error %d loading \"%s\"\n", ret, IPA_FWS_PATH);
	else if ((ret = qcom_scm_pas_auth_and_reset(IPA_PAS_ID)))
		dev_err(dev, "error %d authenticating \"%s\"\n", ret,
			IPA_FWS_PATH);

	memunmap(virt);
out_release_firmware:
	release_firmware(fw);

	return ret;
}

static const struct of_device_id ipa_match[] = {
	{
		.compatible	= "qcom,sdm845-ipa",
		.data		= &ipa_data_sdm845,
	},
	{
		.compatible	= "qcom,sc7180-ipa",
		.data		= &ipa_data_sc7180,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ipa_match);

static phandle of_property_read_phandle(const struct device_node *np,
					const char *name)
{
        struct property *prop;
        int len = 0;

        prop = of_find_property(np, name, &len);
        if (!prop || len != sizeof(__be32))
                return 0;

        return be32_to_cpup(prop->value);
}

/* Check things that can be validated at build time.  This just
 * groups these things BUILD_BUG_ON() calls don't clutter the rest
 * of the code.
 * */
static void ipa_validate_build(void)
{
#ifdef IPA_VALIDATE
	/* We assume we're working on 64-bit hardware */
	BUILD_BUG_ON(!IS_ENABLED(CONFIG_64BIT));

	/* Code assumes the EE ID for the AP is 0 (zeroed structure field) */
	BUILD_BUG_ON(GSI_EE_AP != 0);

	/* There's no point if we have no channels or event rings */
	BUILD_BUG_ON(!GSI_CHANNEL_COUNT_MAX);
	BUILD_BUG_ON(!GSI_EVT_RING_COUNT_MAX);

	/* GSI hardware design limits */
	BUILD_BUG_ON(GSI_CHANNEL_COUNT_MAX > 32);
	BUILD_BUG_ON(GSI_EVT_RING_COUNT_MAX > 31);

	/* The number of TREs in a transaction is limited by the channel's
	 * TLV FIFO size.  A transaction structure uses 8-bit fields
	 * to represents the number of TREs it has allocated and used.
	 */
	BUILD_BUG_ON(GSI_TLV_MAX > U8_MAX);

	/* Exceeding 128 bytes makes the transaction pool *much* larger */
	BUILD_BUG_ON(sizeof(struct gsi_trans) > 128);

	/* This is used as a divisor */
	BUILD_BUG_ON(!IPA_AGGR_GRANULARITY);

	/* Aggregation granularity value can't be 0, and must fit */
	BUILD_BUG_ON(!ipa_aggr_granularity_val(IPA_AGGR_GRANULARITY));
	BUILD_BUG_ON(ipa_aggr_granularity_val(IPA_AGGR_GRANULARITY) >
			field_max(AGGR_GRANULARITY));
#endif /* IPA_VALIDATE */
}

/**
 * ipa_probe() - IPA platform driver probe function
 * @pdev:	Platform device pointer
 *
 * Return:	0 if successful, or a negative error code (possibly
 *		EPROBE_DEFER)
 *
 * This is the main entry point for the IPA driver.  Initialization proceeds
 * in several stages:
 *   - The "init" stage involves activities that can be initialized without
 *     access to the IPA hardware.
 *   - The "config" stage requires the IPA clock to be active so IPA registers
 *     can be accessed, but does not require the use of IPA immediate commands.
 *   - The "setup" stage uses IPA immediate commands, and so requires the GSI
 *     layer to be initialized.
 *
 * A Boolean Device Tree "modem-init" property determines whether GSI
 * initialization will be performed by the AP (Trust Zone) or the modem.
 * If the AP does GSI initialization, the setup phase is entered after
 * this has completed successfully.  Otherwise the modem initializes
 * the GSI layer and signals it has finished by sending an SMP2P interrupt
 * to the AP; this triggers the start if IPA setup.
 */
static int ipa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct ipa_data *data;
	struct ipa_clock *clock;
	struct rproc *rproc;
	bool modem_alloc;
	bool modem_init;
	struct ipa *ipa;
	bool prefetch;
	phandle ph;
	int ret;

	ipa_validate_build();

	/* If we need Trust Zone, make sure it's available */
	modem_init = of_property_read_bool(dev->of_node, "modem-init");
	if (!modem_init)
		if (!qcom_scm_is_available())
			return -EPROBE_DEFER;

	/* We rely on remoteproc to tell us about modem state changes */
	ph = of_property_read_phandle(dev->of_node, "modem-remoteproc");
	if (!ph) {
		dev_err(dev, "DT missing \"modem-remoteproc\" property\n");
		return -EINVAL;
	}

	rproc = rproc_get_by_phandle(ph);
	if (!rproc)
		return -EPROBE_DEFER;

	/* The clock and interconnects might not be ready when we're
	 * probed, so might return -EPROBE_DEFER.
	 */
	clock = ipa_clock_init(dev);
	if (IS_ERR(clock)) {
		ret = PTR_ERR(clock);
		goto err_rproc_put;
	}

	/* No more EPROBE_DEFER.  Get our configuration data */
	data = of_device_get_match_data(dev);
	if (!data) {
		/* This is really IPA_VALIDATE (should never happen) */
		dev_err(dev, "matched hardware not supported\n");
		ret = -ENOTSUPP;
		goto err_clock_exit;
	}

	/* Allocate and initialize the IPA structure */
	ipa = kzalloc(sizeof(*ipa), GFP_KERNEL);
	if (!ipa) {
		ret = -ENOMEM;
		goto err_clock_exit;
	}

	ipa->pdev = pdev;
	dev_set_drvdata(dev, ipa);
	ipa->modem_rproc = rproc;
	ipa->clock = clock;
	ipa->version = data->version;

	ret = ipa_reg_init(ipa);
	if (ret)
		goto err_kfree_ipa;

	ret = ipa_mem_init(ipa, data->mem_data);
	if (ret)
		goto err_reg_exit;

	/* GSI v2.0+ (IPA v4.0+) uses prefetch for the command channel */
	prefetch = ipa->version != IPA_VERSION_3_5_1;
	/* IPA v4.2 requires the AP to allocate channels for the modem */
	modem_alloc = ipa->version == IPA_VERSION_4_2;

	ret = gsi_init(&ipa->gsi, pdev, prefetch, data->endpoint_count,
		       data->endpoint_data, modem_alloc);
	if (ret)
		goto err_mem_exit;

	/* Result is a non-zero mask endpoints that support filtering */
	ipa->filter_map = ipa_endpoint_init(ipa, data->endpoint_count,
					    data->endpoint_data);
	if (!ipa->filter_map) {
		ret = -EINVAL;
		goto err_gsi_exit;
	}

	ret = ipa_table_init(ipa);
	if (ret)
		goto err_endpoint_exit;

	ret = ipa_modem_init(ipa, modem_init);
	if (ret)
		goto err_table_exit;

	ret = ipa_config(ipa, data);
	if (ret)
		goto err_modem_exit;

	dev_info(dev, "IPA driver initialized");

	/* If the modem is doing early initialization, it will trigger a
	 * call to ipa_setup() call when it has finished.  In that case
	 * we're done here.
	 */
	if (modem_init)
		return 0;

	/* Otherwise we need to load the firmware and have Trust Zone validate
	 * and install it.  If that succeeds we can proceed with setup.
	 */
	ret = ipa_firmware_load(dev);
	if (ret)
		goto err_deconfig;

	ret = ipa_setup(ipa);
	if (ret)
		goto err_deconfig;

	return 0;

err_deconfig:
	ipa_deconfig(ipa);
err_modem_exit:
	ipa_modem_exit(ipa);
err_table_exit:
	ipa_table_exit(ipa);
err_endpoint_exit:
	ipa_endpoint_exit(ipa);
err_gsi_exit:
	gsi_exit(&ipa->gsi);
err_mem_exit:
	ipa_mem_exit(ipa);
err_reg_exit:
	ipa_reg_exit(ipa);
err_kfree_ipa:
	kfree(ipa);
err_clock_exit:
	ipa_clock_exit(clock);
err_rproc_put:
	rproc_put(rproc);

	return ret;
}

static int ipa_remove(struct platform_device *pdev)
{
	struct ipa *ipa = dev_get_drvdata(&pdev->dev);
	struct rproc *rproc = ipa->modem_rproc;
	struct ipa_clock *clock = ipa->clock;
	int ret;

	if (ipa->setup_complete) {
		ret = ipa_modem_stop(ipa);
		if (ret)
			return ret;

		ipa_teardown(ipa);
	}

	ipa_deconfig(ipa);
	ipa_modem_exit(ipa);
	ipa_table_exit(ipa);
	ipa_endpoint_exit(ipa);
	gsi_exit(&ipa->gsi);
	ipa_mem_exit(ipa);
	ipa_reg_exit(ipa);
	kfree(ipa);
	ipa_clock_exit(clock);
	rproc_put(rproc);

	return 0;
}

/**
 * ipa_suspend() - Power management system suspend callback
 * @dev:	IPA device structure
 *
 * Return:	Always returns zero
 *
 * Called by the PM framework when a system suspend operation is invoked.
 * Suspends endpoints and releases the clock reference held to keep
 * the IPA clock running until this point.
 */
static int ipa_suspend(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	/* When a suspended RX endpoint has a packet ready to receive, we
	 * get an IPA SUSPEND interrupt.  We trigger a system resume in
	 * that case, but only on the first such interrupt since suspend.
	 */
	__clear_bit(IPA_FLAG_RESUMED, ipa->flags);

	ipa_endpoint_suspend(ipa);

	ipa_clock_put(ipa);

	return 0;
}

/**
 * ipa_resume() - Power management system resume callback
 * @dev:	IPA device structure
 *
 * Return:	Always returns 0
 *
 * Called by the PM framework when a system resume operation is invoked.
 * Takes an IPA clock reference to keep the clock running until suspend,
 * and resumes endpoints.
 */
static int ipa_resume(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	/* This clock reference will keep the IPA out of suspend
	 * until we get a power management suspend request.
	 */
	ipa_clock_get(ipa);

	ipa_endpoint_resume(ipa);

	return 0;
}

static const struct dev_pm_ops ipa_pm_ops = {
	.suspend	= ipa_suspend,
	.resume		= ipa_resume,
};

static struct platform_driver ipa_driver = {
	.probe	= ipa_probe,
	.remove	= ipa_remove,
	.driver	= {
		.name		= "ipa",
		.pm		= &ipa_pm_ops,
		.of_match_table	= ipa_match,
	},
};

module_platform_driver(ipa_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm IP Accelerator device driver");
