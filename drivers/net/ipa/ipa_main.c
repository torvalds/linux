// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */

#include <linux/bug.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "ipa.h"
#include "ipa_cmd.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_interrupt.h"
#include "ipa_mem.h"
#include "ipa_modem.h"
#include "ipa_power.h"
#include "ipa_reg.h"
#include "ipa_resource.h"
#include "ipa_smp2p.h"
#include "ipa_sysfs.h"
#include "ipa_table.h"
#include "ipa_uc.h"
#include "ipa_version.h"

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
#define IPA_FW_PATH_DEFAULT	"ipa_fws.mdt"
#define IPA_PAS_ID		15

/* Shift of 19.2 MHz timestamp to achieve lower resolution timestamps */
/* IPA v5.5+ does not specify Qtime timestamp config for DPL */
#define DPL_TIMESTAMP_SHIFT	14	/* ~1.172 kHz, ~853 usec per tick */
#define TAG_TIMESTAMP_SHIFT	14
#define NAT_TIMESTAMP_SHIFT	24	/* ~1.144 Hz, ~874 msec per tick */

/* Divider for 19.2 MHz crystal oscillator clock to get common timer clock */
#define IPA_XO_CLOCK_DIVIDER	192	/* 1 is subtracted where used */

/**
 * enum ipa_firmware_loader: How GSI firmware gets loaded
 *
 * @IPA_LOADER_DEFER:		System not ready; try again later
 * @IPA_LOADER_SELF:		AP loads GSI firmware
 * @IPA_LOADER_MODEM:		Modem loads GSI firmware, signals when done
 * @IPA_LOADER_SKIP:		Neither AP nor modem need to load GSI firmware
 * @IPA_LOADER_INVALID:	GSI firmware loader specification is invalid
 */
enum ipa_firmware_loader {
	IPA_LOADER_DEFER,
	IPA_LOADER_SELF,
	IPA_LOADER_MODEM,
	IPA_LOADER_SKIP,
	IPA_LOADER_INVALID,
};

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
	struct device *dev = ipa->dev;
	int ret;

	ret = gsi_setup(&ipa->gsi);
	if (ret)
		return ret;

	ipa_endpoint_setup(ipa);

	/* We need to use the AP command TX endpoint to perform other
	 * initialization, so we enable first.
	 */
	command_endpoint = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX];
	ret = ipa_endpoint_enable_one(command_endpoint);
	if (ret)
		goto err_endpoint_teardown;

	ret = ipa_mem_setup(ipa);	/* No matching teardown required */
	if (ret)
		goto err_command_disable;

	ret = ipa_table_setup(ipa);	/* No matching teardown required */
	if (ret)
		goto err_command_disable;

	/* Enable the exception handling endpoint, and tell the hardware
	 * to use it by default.
	 */
	exception_endpoint = ipa->name_map[IPA_ENDPOINT_AP_LAN_RX];
	ret = ipa_endpoint_enable_one(exception_endpoint);
	if (ret)
		goto err_command_disable;

	ipa_endpoint_default_route_set(ipa, exception_endpoint->endpoint_id);

	/* We're all set.  Now prepare for communication with the modem */
	ret = ipa_qmi_setup(ipa);
	if (ret)
		goto err_default_route_clear;

	ipa->setup_complete = true;

	dev_info(dev, "IPA driver setup completed successfully\n");

	return 0;

err_default_route_clear:
	ipa_endpoint_default_route_clear(ipa);
	ipa_endpoint_disable_one(exception_endpoint);
err_command_disable:
	ipa_endpoint_disable_one(command_endpoint);
err_endpoint_teardown:
	ipa_endpoint_teardown(ipa);
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

	/* We're going to tear everything down, as if setup never completed */
	ipa->setup_complete = false;

	ipa_qmi_teardown(ipa);
	ipa_endpoint_default_route_clear(ipa);
	exception_endpoint = ipa->name_map[IPA_ENDPOINT_AP_LAN_RX];
	ipa_endpoint_disable_one(exception_endpoint);
	command_endpoint = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX];
	ipa_endpoint_disable_one(command_endpoint);
	ipa_endpoint_teardown(ipa);
	gsi_teardown(&ipa->gsi);
}

static void
ipa_hardware_config_bcr(struct ipa *ipa, const struct ipa_data *data)
{
	const struct reg *reg;
	u32 val;

	/* IPA v4.5+ has no backward compatibility register */
	if (ipa->version >= IPA_VERSION_4_5)
		return;

	reg = ipa_reg(ipa, IPA_BCR);
	val = data->backward_compat;
	iowrite32(val, ipa->reg_virt + reg_offset(reg));
}

static void ipa_hardware_config_tx(struct ipa *ipa)
{
	enum ipa_version version = ipa->version;
	const struct reg *reg;
	u32 offset;
	u32 val;

	if (version <= IPA_VERSION_4_0 || version >= IPA_VERSION_4_5)
		return;

	/* Disable PA mask to allow HOLB drop */
	reg = ipa_reg(ipa, IPA_TX_CFG);
	offset = reg_offset(reg);

	val = ioread32(ipa->reg_virt + offset);

	val &= ~reg_bit(reg, PA_MASK_EN);

	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_hardware_config_clkon(struct ipa *ipa)
{
	enum ipa_version version = ipa->version;
	const struct reg *reg;
	u32 val;

	if (version >= IPA_VERSION_4_5)
		return;

	if (version < IPA_VERSION_4_0 && version != IPA_VERSION_3_1)
		return;

	/* Implement some hardware workarounds */
	reg = ipa_reg(ipa, CLKON_CFG);
	if (version == IPA_VERSION_3_1) {
		/* Disable MISC clock gating */
		val = reg_bit(reg, CLKON_MISC);
	} else {	/* IPA v4.0+ */
		/* Enable open global clocks in the CLKON configuration */
		val = reg_bit(reg, CLKON_GLOBAL);
		val |= reg_bit(reg, GLOBAL_2X_CLK);
	}

	iowrite32(val, ipa->reg_virt + reg_offset(reg));
}

/* Configure bus access behavior for IPA components */
static void ipa_hardware_config_comp(struct ipa *ipa)
{
	const struct reg *reg;
	u32 offset;
	u32 val;

	/* Nothing to configure prior to IPA v4.0 */
	if (ipa->version < IPA_VERSION_4_0)
		return;

	reg = ipa_reg(ipa, COMP_CFG);
	offset = reg_offset(reg);

	val = ioread32(ipa->reg_virt + offset);

	if (ipa->version == IPA_VERSION_4_0) {
		val &= ~reg_bit(reg, IPA_QMB_SELECT_CONS_EN);
		val &= ~reg_bit(reg, IPA_QMB_SELECT_PROD_EN);
		val &= ~reg_bit(reg, IPA_QMB_SELECT_GLOBAL_EN);
	} else if (ipa->version < IPA_VERSION_4_5) {
		val |= reg_bit(reg, GSI_MULTI_AXI_MASTERS_DIS);
	} else {
		/* For IPA v4.5+ FULL_FLUSH_WAIT_RS_CLOSURE_EN is 0 */
	}

	val |= reg_bit(reg, GSI_MULTI_INORDER_RD_DIS);
	val |= reg_bit(reg, GSI_MULTI_INORDER_WR_DIS);

	iowrite32(val, ipa->reg_virt + offset);
}

/* Configure DDR and (possibly) PCIe max read/write QSB values */
static void
ipa_hardware_config_qsb(struct ipa *ipa, const struct ipa_data *data)
{
	const struct ipa_qsb_data *data0;
	const struct ipa_qsb_data *data1;
	const struct reg *reg;
	u32 val;

	/* QMB 0 represents DDR; QMB 1 (if present) represents PCIe */
	data0 = &data->qsb_data[IPA_QSB_MASTER_DDR];
	if (data->qsb_count > 1)
		data1 = &data->qsb_data[IPA_QSB_MASTER_PCIE];

	/* Max outstanding write accesses for QSB masters */
	reg = ipa_reg(ipa, QSB_MAX_WRITES);

	val = reg_encode(reg, GEN_QMB_0_MAX_WRITES, data0->max_writes);
	if (data->qsb_count > 1)
		val |= reg_encode(reg, GEN_QMB_1_MAX_WRITES, data1->max_writes);

	iowrite32(val, ipa->reg_virt + reg_offset(reg));

	/* Max outstanding read accesses for QSB masters */
	reg = ipa_reg(ipa, QSB_MAX_READS);

	val = reg_encode(reg, GEN_QMB_0_MAX_READS, data0->max_reads);
	if (ipa->version >= IPA_VERSION_4_0)
		val |= reg_encode(reg, GEN_QMB_0_MAX_READS_BEATS,
				  data0->max_reads_beats);
	if (data->qsb_count > 1) {
		val = reg_encode(reg, GEN_QMB_1_MAX_READS, data1->max_reads);
		if (ipa->version >= IPA_VERSION_4_0)
			val |= reg_encode(reg, GEN_QMB_1_MAX_READS_BEATS,
					  data1->max_reads_beats);
	}

	iowrite32(val, ipa->reg_virt + reg_offset(reg));
}

/* The internal inactivity timer clock is used for the aggregation timer */
#define TIMER_FREQUENCY	32000		/* 32 KHz inactivity timer clock */

/* Compute the value to use in the COUNTER_CFG register AGGR_GRANULARITY
 * field to represent the given number of microseconds.  The value is one
 * less than the number of timer ticks in the requested period.  0 is not
 * a valid granularity value (so for example @usec must be at least 16 for
 * a TIMER_FREQUENCY of 32000).
 */
static __always_inline u32 ipa_aggr_granularity_val(u32 usec)
{
	return DIV_ROUND_CLOSEST(usec * TIMER_FREQUENCY, USEC_PER_SEC) - 1;
}

/* IPA uses unified Qtime starting at IPA v4.5, implementing various
 * timestamps and timers independent of the IPA core clock rate.  The
 * Qtimer is based on a 56-bit timestamp incremented at each tick of
 * a 19.2 MHz SoC crystal oscillator (XO clock).
 *
 * For IPA timestamps (tag, NAT, data path logging) a lower resolution
 * timestamp is achieved by shifting the Qtimer timestamp value right
 * some number of bits to produce the low-order bits of the coarser
 * granularity timestamp.
 *
 * For timers, a common timer clock is derived from the XO clock using
 * a divider (we use 192, to produce a 100kHz timer clock).  From
 * this common clock, three "pulse generators" are used to produce
 * timer ticks at a configurable frequency.  IPA timers (such as
 * those used for aggregation or head-of-line block handling) now
 * define their period based on one of these pulse generators.
 */
static void ipa_qtime_config(struct ipa *ipa)
{
	const struct reg *reg;
	u32 offset;
	u32 val;

	/* Timer clock divider must be disabled when we change the rate */
	reg = ipa_reg(ipa, TIMERS_XO_CLK_DIV_CFG);
	iowrite32(0, ipa->reg_virt + reg_offset(reg));

	reg = ipa_reg(ipa, QTIME_TIMESTAMP_CFG);
	if (ipa->version < IPA_VERSION_5_5) {
		/* Set DPL time stamp resolution to use Qtime (not 1 msec) */
		val = reg_encode(reg, DPL_TIMESTAMP_LSB, DPL_TIMESTAMP_SHIFT);
		val |= reg_bit(reg, DPL_TIMESTAMP_SEL);
	}
	/* Configure tag and NAT Qtime timestamp resolution as well */
	val = reg_encode(reg, TAG_TIMESTAMP_LSB, TAG_TIMESTAMP_SHIFT);
	val = reg_encode(reg, NAT_TIMESTAMP_LSB, NAT_TIMESTAMP_SHIFT);

	iowrite32(val, ipa->reg_virt + reg_offset(reg));

	/* Set granularity of pulse generators used for other timers */
	reg = ipa_reg(ipa, TIMERS_PULSE_GRAN_CFG);
	val = reg_encode(reg, PULSE_GRAN_0, IPA_GRAN_100_US);
	val |= reg_encode(reg, PULSE_GRAN_1, IPA_GRAN_1_MS);
	if (ipa->version >= IPA_VERSION_5_0) {
		val |= reg_encode(reg, PULSE_GRAN_2, IPA_GRAN_10_MS);
		val |= reg_encode(reg, PULSE_GRAN_3, IPA_GRAN_10_MS);
	} else {
		val |= reg_encode(reg, PULSE_GRAN_2, IPA_GRAN_1_MS);
	}

	iowrite32(val, ipa->reg_virt + reg_offset(reg));

	/* Actual divider is 1 more than value supplied here */
	reg = ipa_reg(ipa, TIMERS_XO_CLK_DIV_CFG);
	offset = reg_offset(reg);

	val = reg_encode(reg, DIV_VALUE, IPA_XO_CLOCK_DIVIDER - 1);

	iowrite32(val, ipa->reg_virt + offset);

	/* Divider value is set; re-enable the common timer clock divider */
	val |= reg_bit(reg, DIV_ENABLE);

	iowrite32(val, ipa->reg_virt + offset);
}

/* Before IPA v4.5 timing is controlled by a counter register */
static void ipa_hardware_config_counter(struct ipa *ipa)
{
	u32 granularity = ipa_aggr_granularity_val(IPA_AGGR_GRANULARITY);
	const struct reg *reg;
	u32 val;

	reg = ipa_reg(ipa, COUNTER_CFG);
	/* If defined, EOT_COAL_GRANULARITY is 0 */
	val = reg_encode(reg, AGGR_GRANULARITY, granularity);
	iowrite32(val, ipa->reg_virt + reg_offset(reg));
}

static void ipa_hardware_config_timing(struct ipa *ipa)
{
	if (ipa->version < IPA_VERSION_4_5)
		ipa_hardware_config_counter(ipa);
	else
		ipa_qtime_config(ipa);
}

static void ipa_hardware_config_hashing(struct ipa *ipa)
{
	const struct reg *reg;

	/* Other than IPA v4.2, all versions enable "hashing".  Starting
	 * with IPA v5.0, the filter and router tables are implemented
	 * differently, but the default configuration enables this feature
	 * (now referred to as "cacheing"), so there's nothing to do here.
	 */
	if (ipa->version != IPA_VERSION_4_2)
		return;

	/* IPA v4.2 does not support hashed tables, so disable them */
	reg = ipa_reg(ipa, FILT_ROUT_HASH_EN);

	/* IPV6_ROUTER_HASH, IPV6_FILTER_HASH, IPV4_ROUTER_HASH,
	 * IPV4_FILTER_HASH are all zero.
	 */
	iowrite32(0, ipa->reg_virt + reg_offset(reg));
}

static void ipa_idle_indication_cfg(struct ipa *ipa,
				    u32 enter_idle_debounce_thresh,
				    bool const_non_idle_enable)
{
	const struct reg *reg;
	u32 val;

	if (ipa->version < IPA_VERSION_3_5_1)
		return;

	reg = ipa_reg(ipa, IDLE_INDICATION_CFG);
	val = reg_encode(reg, ENTER_IDLE_DEBOUNCE_THRESH,
			 enter_idle_debounce_thresh);
	if (const_non_idle_enable)
		val |= reg_bit(reg, CONST_NON_IDLE_ENABLE);

	iowrite32(val, ipa->reg_virt + reg_offset(reg));
}

/**
 * ipa_hardware_dcd_config() - Enable dynamic clock division on IPA
 * @ipa:	IPA pointer
 *
 * Configures when the IPA signals it is idle to the global clock
 * controller, which can respond by scaling down the clock to save
 * power.
 */
static void ipa_hardware_dcd_config(struct ipa *ipa)
{
	/* Recommended values for IPA 3.5 and later according to IPA HPG */
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
 * @data:	IPA configuration data
 */
static void ipa_hardware_config(struct ipa *ipa, const struct ipa_data *data)
{
	ipa_hardware_config_bcr(ipa, data);
	ipa_hardware_config_tx(ipa);
	ipa_hardware_config_clkon(ipa);
	ipa_hardware_config_comp(ipa);
	ipa_hardware_config_qsb(ipa, data);
	ipa_hardware_config_timing(ipa);
	ipa_hardware_config_hashing(ipa);
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

/**
 * ipa_config() - Configure IPA hardware
 * @ipa:	IPA pointer
 * @data:	IPA configuration data
 *
 * Perform initialization requiring IPA power to be enabled.
 */
static int ipa_config(struct ipa *ipa, const struct ipa_data *data)
{
	int ret;

	ipa_hardware_config(ipa, data);

	ret = ipa_mem_config(ipa);
	if (ret)
		goto err_hardware_deconfig;

	ret = ipa_interrupt_config(ipa);
	if (ret)
		goto err_mem_deconfig;

	ipa_uc_config(ipa);

	ret = ipa_endpoint_config(ipa);
	if (ret)
		goto err_uc_deconfig;

	ipa_table_config(ipa);		/* No deconfig required */

	/* Assign resource limitation to each group; no deconfig required */
	ret = ipa_resource_config(ipa, data->resource_data);
	if (ret)
		goto err_endpoint_deconfig;

	ret = ipa_modem_config(ipa);
	if (ret)
		goto err_endpoint_deconfig;

	return 0;

err_endpoint_deconfig:
	ipa_endpoint_deconfig(ipa);
err_uc_deconfig:
	ipa_uc_deconfig(ipa);
	ipa_interrupt_deconfig(ipa);
err_mem_deconfig:
	ipa_mem_deconfig(ipa);
err_hardware_deconfig:
	ipa_hardware_deconfig(ipa);

	return ret;
}

/**
 * ipa_deconfig() - Inverse of ipa_config()
 * @ipa:	IPA pointer
 */
static void ipa_deconfig(struct ipa *ipa)
{
	ipa_modem_deconfig(ipa);
	ipa_endpoint_deconfig(ipa);
	ipa_uc_deconfig(ipa);
	ipa_interrupt_deconfig(ipa);
	ipa_mem_deconfig(ipa);
	ipa_hardware_deconfig(ipa);
}

static int ipa_firmware_load(struct device *dev)
{
	const struct firmware *fw;
	struct device_node *node;
	struct resource res;
	phys_addr_t phys;
	const char *path;
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

	/* Use name from DTB if specified; use default for *any* error */
	ret = of_property_read_string(dev->of_node, "firmware-name", &path);
	if (ret) {
		dev_dbg(dev, "error %d getting \"firmware-name\" resource\n",
			ret);
		path = IPA_FW_PATH_DEFAULT;
	}

	ret = request_firmware(&fw, path, dev);
	if (ret) {
		dev_err(dev, "error %d requesting \"%s\"\n", ret, path);
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

	ret = qcom_mdt_load(dev, fw, path, IPA_PAS_ID, virt, phys, size, NULL);
	if (ret)
		dev_err(dev, "error %d loading \"%s\"\n", ret, path);
	else if ((ret = qcom_scm_pas_auth_and_reset(IPA_PAS_ID)))
		dev_err(dev, "error %d authenticating \"%s\"\n", ret, path);

	memunmap(virt);
out_release_firmware:
	release_firmware(fw);

	return ret;
}

static const struct of_device_id ipa_match[] = {
	{
		.compatible	= "qcom,msm8998-ipa",
		.data		= &ipa_data_v3_1,
	},
	{
		.compatible	= "qcom,sdm845-ipa",
		.data		= &ipa_data_v3_5_1,
	},
	{
		.compatible	= "qcom,sc7180-ipa",
		.data		= &ipa_data_v4_2,
	},
	{
		.compatible	= "qcom,sdx55-ipa",
		.data		= &ipa_data_v4_5,
	},
	{
		.compatible	= "qcom,sm6350-ipa",
		.data		= &ipa_data_v4_7,
	},
	{
		.compatible	= "qcom,sm8350-ipa",
		.data		= &ipa_data_v4_9,
	},
	{
		.compatible	= "qcom,sc7280-ipa",
		.data		= &ipa_data_v4_11,
	},
	{
		.compatible	= "qcom,sdx65-ipa",
		.data		= &ipa_data_v5_0,
	},
	{
		.compatible	= "qcom,sm8550-ipa",
		.data		= &ipa_data_v5_5,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ipa_match);

/* Check things that can be validated at build time.  This just
 * groups these things BUILD_BUG_ON() calls don't clutter the rest
 * of the code.
 * */
static void ipa_validate_build(void)
{
	/* At one time we assumed a 64-bit build, allowing some do_div()
	 * calls to be replaced by simple division or modulo operations.
	 * We currently only perform divide and modulo operations on u32,
	 * u16, or size_t objects, and of those only size_t has any chance
	 * of being a 64-bit value.  (It should be guaranteed 32 bits wide
	 * on a 32-bit build, but there is no harm in verifying that.)
	 */
	BUILD_BUG_ON(!IS_ENABLED(CONFIG_64BIT) && sizeof(size_t) != 4);

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

	/* This is used as a divisor */
	BUILD_BUG_ON(!IPA_AGGR_GRANULARITY);

	/* Aggregation granularity value can't be 0, and must fit */
	BUILD_BUG_ON(!ipa_aggr_granularity_val(IPA_AGGR_GRANULARITY));
}

static enum ipa_firmware_loader ipa_firmware_loader(struct device *dev)
{
	bool modem_init;
	const char *str;
	int ret;

	/* Look up the old and new properties by name */
	modem_init = of_property_read_bool(dev->of_node, "modem-init");
	ret = of_property_read_string(dev->of_node, "qcom,gsi-loader", &str);

	/* If the new property doesn't exist, it's legacy behavior */
	if (ret == -EINVAL) {
		if (modem_init)
			return IPA_LOADER_MODEM;
		goto out_self;
	}

	/* Any other error on the new property means it's poorly defined */
	if (ret)
		return IPA_LOADER_INVALID;

	/* New property value exists; if old one does too, that's invalid */
	if (modem_init)
		return IPA_LOADER_INVALID;

	/* Modem loads GSI firmware for "modem" */
	if (!strcmp(str, "modem"))
		return IPA_LOADER_MODEM;

	/* No GSI firmware load is needed for "skip" */
	if (!strcmp(str, "skip"))
		return IPA_LOADER_SKIP;

	/* Any value other than "self" is an error */
	if (strcmp(str, "self"))
		return IPA_LOADER_INVALID;
out_self:
	/* We need Trust Zone to load firmware; make sure it's available */
	if (qcom_scm_is_available())
		return IPA_LOADER_SELF;

	return IPA_LOADER_DEFER;
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
 *   - The "config" stage requires IPA power to be active so IPA registers
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
	struct ipa_interrupt *interrupt;
	enum ipa_firmware_loader loader;
	const struct ipa_data *data;
	struct ipa_power *power;
	struct ipa *ipa;
	int ret;

	ipa_validate_build();

	/* Get configuration data early; needed for power initialization */
	data = of_device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "matched hardware not supported\n");
		return -ENODEV;
	}

	if (!data->modem_route_count) {
		dev_err(dev, "modem_route_count cannot be zero\n");
		return -EINVAL;
	}

	loader = ipa_firmware_loader(dev);
	if (loader == IPA_LOADER_INVALID)
		return -EINVAL;
	if (loader == IPA_LOADER_DEFER)
		return -EPROBE_DEFER;

	/* The IPA interrupt might not be ready when we're probed, so this
	 * might return -EPROBE_DEFER.
	 */
	interrupt = ipa_interrupt_init(pdev);
	if (IS_ERR(interrupt))
		return PTR_ERR(interrupt);

	/* The clock and interconnects might not be ready when we're probed,
	 * so this might return -EPROBE_DEFER.
	 */
	power = ipa_power_init(dev, data->power_data);
	if (IS_ERR(power)) {
		ret = PTR_ERR(power);
		goto err_interrupt_exit;
	}

	/* No more EPROBE_DEFER.  Allocate and initialize the IPA structure */
	ipa = kzalloc(sizeof(*ipa), GFP_KERNEL);
	if (!ipa) {
		ret = -ENOMEM;
		goto err_power_exit;
	}

	ipa->dev = dev;
	dev_set_drvdata(dev, ipa);
	ipa->interrupt = interrupt;
	ipa->power = power;
	ipa->version = data->version;
	ipa->modem_route_count = data->modem_route_count;
	init_completion(&ipa->completion);

	ret = ipa_reg_init(ipa, pdev);
	if (ret)
		goto err_kfree_ipa;

	ret = ipa_mem_init(ipa, pdev, data->mem_data);
	if (ret)
		goto err_reg_exit;

	ret = ipa_cmd_init(ipa);
	if (ret)
		goto err_mem_exit;

	ret = gsi_init(&ipa->gsi, pdev, ipa->version, data->endpoint_count,
		       data->endpoint_data);
	if (ret)
		goto err_mem_exit;

	/* Result is a non-zero mask of endpoints that support filtering */
	ret = ipa_endpoint_init(ipa, data->endpoint_count, data->endpoint_data);
	if (ret)
		goto err_gsi_exit;

	ret = ipa_table_init(ipa);
	if (ret)
		goto err_endpoint_exit;

	ret = ipa_smp2p_init(ipa, pdev, loader == IPA_LOADER_MODEM);
	if (ret)
		goto err_table_exit;

	/* Power needs to be active for config and setup */
	ret = pm_runtime_get_sync(dev);
	if (WARN_ON(ret < 0))
		goto err_power_put;

	ret = ipa_config(ipa, data);
	if (ret)
		goto err_power_put;

	dev_info(dev, "IPA driver initialized");

	/* If the modem is loading GSI firmware, it will trigger a call to
	 * ipa_setup() when it has finished.  In that case we're done here.
	 */
	if (loader == IPA_LOADER_MODEM)
		goto done;

	if (loader == IPA_LOADER_SELF) {
		/* The AP is loading GSI firmware; do so now */
		ret = ipa_firmware_load(dev);
		if (ret)
			goto err_deconfig;
	} /* Otherwise loader == IPA_LOADER_SKIP */

	/* GSI firmware is loaded; proceed to setup */
	ret = ipa_setup(ipa);
	if (ret)
		goto err_deconfig;
done:
	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);

	return 0;

err_deconfig:
	ipa_deconfig(ipa);
err_power_put:
	pm_runtime_put_noidle(dev);
	ipa_smp2p_exit(ipa);
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
err_power_exit:
	ipa_power_exit(power);
err_interrupt_exit:
	ipa_interrupt_exit(interrupt);

	return ret;
}

static void ipa_remove(struct platform_device *pdev)
{
	struct ipa_interrupt *interrupt;
	struct ipa_power *power;
	struct device *dev;
	struct ipa *ipa;
	int ret;

	ipa = dev_get_drvdata(&pdev->dev);
	dev = ipa->dev;
	WARN_ON(dev != &pdev->dev);

	power = ipa->power;
	interrupt = ipa->interrupt;

	/* Prevent the modem from triggering a call to ipa_setup().  This
	 * also ensures a modem-initiated setup that's underway completes.
	 */
	ipa_smp2p_irq_disable_setup(ipa);

	ret = pm_runtime_get_sync(dev);
	if (WARN_ON(ret < 0))
		goto out_power_put;

	if (ipa->setup_complete) {
		ret = ipa_modem_stop(ipa);
		/* If starting or stopping is in progress, try once more */
		if (ret == -EBUSY) {
			usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);
			ret = ipa_modem_stop(ipa);
		}
		if (ret) {
			/*
			 * Not cleaning up here properly might also yield a
			 * crash later on. As the device is still unregistered
			 * in this case, this might even yield a crash later on.
			 */
			dev_err(dev, "Failed to stop modem (%pe), leaking resources\n",
				ERR_PTR(ret));
			return;
		}

		ipa_teardown(ipa);
	}

	ipa_deconfig(ipa);
out_power_put:
	pm_runtime_put_noidle(dev);
	ipa_smp2p_exit(ipa);
	ipa_table_exit(ipa);
	ipa_endpoint_exit(ipa);
	gsi_exit(&ipa->gsi);
	ipa_mem_exit(ipa);
	ipa_reg_exit(ipa);
	kfree(ipa);
	ipa_power_exit(power);
	ipa_interrupt_exit(interrupt);

	dev_info(dev, "IPA driver removed");
}

static const struct attribute_group *ipa_attribute_groups[] = {
	&ipa_attribute_group,
	&ipa_feature_attribute_group,
	&ipa_endpoint_id_attribute_group,
	&ipa_modem_attribute_group,
	NULL,
};

static struct platform_driver ipa_driver = {
	.probe		= ipa_probe,
	.remove		= ipa_remove,
	.shutdown	= ipa_remove,
	.driver	= {
		.name		= "ipa",
		.pm		= &ipa_pm_ops,
		.of_match_table	= ipa_match,
		.dev_groups	= ipa_attribute_groups,
	},
};

module_platform_driver(ipa_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm IP Accelerator device driver");
