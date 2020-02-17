/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Shobhit Kumar <shobhit.kumar@intel.com>
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/slab.h>

#include <asm/intel-mid.h>
#include <asm/unaligned.h>

#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/i915_drm.h>

#include <video/mipi_display.h>

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_dsi.h"
#include "intel_sideband.h"

#define MIPI_TRANSFER_MODE_SHIFT	0
#define MIPI_VIRTUAL_CHANNEL_SHIFT	1
#define MIPI_PORT_SHIFT			3

/* base offsets for gpio pads */
#define VLV_GPIO_NC_0_HV_DDI0_HPD	0x4130
#define VLV_GPIO_NC_1_HV_DDI0_DDC_SDA	0x4120
#define VLV_GPIO_NC_2_HV_DDI0_DDC_SCL	0x4110
#define VLV_GPIO_NC_3_PANEL0_VDDEN	0x4140
#define VLV_GPIO_NC_4_PANEL0_BKLTEN	0x4150
#define VLV_GPIO_NC_5_PANEL0_BKLTCTL	0x4160
#define VLV_GPIO_NC_6_HV_DDI1_HPD	0x4180
#define VLV_GPIO_NC_7_HV_DDI1_DDC_SDA	0x4190
#define VLV_GPIO_NC_8_HV_DDI1_DDC_SCL	0x4170
#define VLV_GPIO_NC_9_PANEL1_VDDEN	0x4100
#define VLV_GPIO_NC_10_PANEL1_BKLTEN	0x40E0
#define VLV_GPIO_NC_11_PANEL1_BKLTCTL	0x40F0

#define VLV_GPIO_PCONF0(base_offset)	(base_offset)
#define VLV_GPIO_PAD_VAL(base_offset)	((base_offset) + 8)

struct gpio_map {
	u16 base_offset;
	bool init;
};

static struct gpio_map vlv_gpio_table[] = {
	{ VLV_GPIO_NC_0_HV_DDI0_HPD },
	{ VLV_GPIO_NC_1_HV_DDI0_DDC_SDA },
	{ VLV_GPIO_NC_2_HV_DDI0_DDC_SCL },
	{ VLV_GPIO_NC_3_PANEL0_VDDEN },
	{ VLV_GPIO_NC_4_PANEL0_BKLTEN },
	{ VLV_GPIO_NC_5_PANEL0_BKLTCTL },
	{ VLV_GPIO_NC_6_HV_DDI1_HPD },
	{ VLV_GPIO_NC_7_HV_DDI1_DDC_SDA },
	{ VLV_GPIO_NC_8_HV_DDI1_DDC_SCL },
	{ VLV_GPIO_NC_9_PANEL1_VDDEN },
	{ VLV_GPIO_NC_10_PANEL1_BKLTEN },
	{ VLV_GPIO_NC_11_PANEL1_BKLTCTL },
};

struct i2c_adapter_lookup {
	u16 slave_addr;
	struct intel_dsi *intel_dsi;
	acpi_handle dev_handle;
};

#define CHV_GPIO_IDX_START_N		0
#define CHV_GPIO_IDX_START_E		73
#define CHV_GPIO_IDX_START_SW		100
#define CHV_GPIO_IDX_START_SE		198

#define CHV_VBT_MAX_PINS_PER_FMLY	15

#define CHV_GPIO_PAD_CFG0(f, i)		(0x4400 + (f) * 0x400 + (i) * 8)
#define  CHV_GPIO_GPIOEN		(1 << 15)
#define  CHV_GPIO_GPIOCFG_GPIO		(0 << 8)
#define  CHV_GPIO_GPIOCFG_GPO		(1 << 8)
#define  CHV_GPIO_GPIOCFG_GPI		(2 << 8)
#define  CHV_GPIO_GPIOCFG_HIZ		(3 << 8)
#define  CHV_GPIO_GPIOTXSTATE(state)	((!!(state)) << 1)

#define CHV_GPIO_PAD_CFG1(f, i)		(0x4400 + (f) * 0x400 + (i) * 8 + 4)
#define  CHV_GPIO_CFGLOCK		(1 << 31)

/* ICL DSI Display GPIO Pins */
#define  ICL_GPIO_DDSP_HPD_A		0
#define  ICL_GPIO_L_VDDEN_1		1
#define  ICL_GPIO_L_BKLTEN_1		2
#define  ICL_GPIO_DDPA_CTRLCLK_1	3
#define  ICL_GPIO_DDPA_CTRLDATA_1	4
#define  ICL_GPIO_DDSP_HPD_B		5
#define  ICL_GPIO_L_VDDEN_2		6
#define  ICL_GPIO_L_BKLTEN_2		7
#define  ICL_GPIO_DDPA_CTRLCLK_2	8
#define  ICL_GPIO_DDPA_CTRLDATA_2	9

static inline enum port intel_dsi_seq_port_to_port(u8 port)
{
	return port ? PORT_C : PORT_A;
}

static const u8 *mipi_exec_send_packet(struct intel_dsi *intel_dsi,
				       const u8 *data)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);
	struct mipi_dsi_device *dsi_device;
	u8 type, flags, seq_port;
	u16 len;
	enum port port;

	DRM_DEBUG_KMS("\n");

	flags = *data++;
	type = *data++;

	len = *((u16 *) data);
	data += 2;

	seq_port = (flags >> MIPI_PORT_SHIFT) & 3;

	/* For DSI single link on Port A & C, the seq_port value which is
	 * parsed from Sequence Block#53 of VBT has been set to 0
	 * Now, read/write of packets for the DSI single link on Port A and
	 * Port C will based on the DVO port from VBT block 2.
	 */
	if (intel_dsi->ports == (1 << PORT_C))
		port = PORT_C;
	else
		port = intel_dsi_seq_port_to_port(seq_port);

	dsi_device = intel_dsi->dsi_hosts[port]->device;
	if (!dsi_device) {
		DRM_DEBUG_KMS("no dsi device for port %c\n", port_name(port));
		goto out;
	}

	if ((flags >> MIPI_TRANSFER_MODE_SHIFT) & 1)
		dsi_device->mode_flags &= ~MIPI_DSI_MODE_LPM;
	else
		dsi_device->mode_flags |= MIPI_DSI_MODE_LPM;

	dsi_device->channel = (flags >> MIPI_VIRTUAL_CHANNEL_SHIFT) & 3;

	switch (type) {
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		mipi_dsi_generic_write(dsi_device, NULL, 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		mipi_dsi_generic_write(dsi_device, data, 1);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		mipi_dsi_generic_write(dsi_device, data, 2);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		DRM_DEBUG_DRIVER("Generic Read not yet implemented or used\n");
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		mipi_dsi_generic_write(dsi_device, data, len);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE:
		mipi_dsi_dcs_write_buffer(dsi_device, data, 1);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		mipi_dsi_dcs_write_buffer(dsi_device, data, 2);
		break;
	case MIPI_DSI_DCS_READ:
		DRM_DEBUG_DRIVER("DCS Read not yet implemented or used\n");
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		mipi_dsi_dcs_write_buffer(dsi_device, data, len);
		break;
	}

	if (INTEL_GEN(dev_priv) < 11)
		vlv_dsi_wait_for_fifo_empty(intel_dsi, port);

out:
	data += len;

	return data;
}

static const u8 *mipi_exec_delay(struct intel_dsi *intel_dsi, const u8 *data)
{
	u32 delay = *((const u32 *) data);

	DRM_DEBUG_KMS("\n");

	usleep_range(delay, delay + 10);
	data += 4;

	return data;
}

static void vlv_exec_gpio(struct drm_i915_private *dev_priv,
			  u8 gpio_source, u8 gpio_index, bool value)
{
	struct gpio_map *map;
	u16 pconf0, padval;
	u32 tmp;
	u8 port;

	if (gpio_index >= ARRAY_SIZE(vlv_gpio_table)) {
		DRM_DEBUG_KMS("unknown gpio index %u\n", gpio_index);
		return;
	}

	map = &vlv_gpio_table[gpio_index];

	if (dev_priv->vbt.dsi.seq_version >= 3) {
		/* XXX: this assumes vlv_gpio_table only has NC GPIOs. */
		port = IOSF_PORT_GPIO_NC;
	} else {
		if (gpio_source == 0) {
			port = IOSF_PORT_GPIO_NC;
		} else if (gpio_source == 1) {
			DRM_DEBUG_KMS("SC gpio not supported\n");
			return;
		} else {
			DRM_DEBUG_KMS("unknown gpio source %u\n", gpio_source);
			return;
		}
	}

	pconf0 = VLV_GPIO_PCONF0(map->base_offset);
	padval = VLV_GPIO_PAD_VAL(map->base_offset);

	vlv_iosf_sb_get(dev_priv, BIT(VLV_IOSF_SB_GPIO));
	if (!map->init) {
		/* FIXME: remove constant below */
		vlv_iosf_sb_write(dev_priv, port, pconf0, 0x2000CC00);
		map->init = true;
	}

	tmp = 0x4 | value;
	vlv_iosf_sb_write(dev_priv, port, padval, tmp);
	vlv_iosf_sb_put(dev_priv, BIT(VLV_IOSF_SB_GPIO));
}

static void chv_exec_gpio(struct drm_i915_private *dev_priv,
			  u8 gpio_source, u8 gpio_index, bool value)
{
	u16 cfg0, cfg1;
	u16 family_num;
	u8 port;

	if (dev_priv->vbt.dsi.seq_version >= 3) {
		if (gpio_index >= CHV_GPIO_IDX_START_SE) {
			/* XXX: it's unclear whether 255->57 is part of SE. */
			gpio_index -= CHV_GPIO_IDX_START_SE;
			port = CHV_IOSF_PORT_GPIO_SE;
		} else if (gpio_index >= CHV_GPIO_IDX_START_SW) {
			gpio_index -= CHV_GPIO_IDX_START_SW;
			port = CHV_IOSF_PORT_GPIO_SW;
		} else if (gpio_index >= CHV_GPIO_IDX_START_E) {
			gpio_index -= CHV_GPIO_IDX_START_E;
			port = CHV_IOSF_PORT_GPIO_E;
		} else {
			port = CHV_IOSF_PORT_GPIO_N;
		}
	} else {
		/* XXX: The spec is unclear about CHV GPIO on seq v2 */
		if (gpio_source != 0) {
			DRM_DEBUG_KMS("unknown gpio source %u\n", gpio_source);
			return;
		}

		if (gpio_index >= CHV_GPIO_IDX_START_E) {
			DRM_DEBUG_KMS("invalid gpio index %u for GPIO N\n",
				      gpio_index);
			return;
		}

		port = CHV_IOSF_PORT_GPIO_N;
	}

	family_num = gpio_index / CHV_VBT_MAX_PINS_PER_FMLY;
	gpio_index = gpio_index % CHV_VBT_MAX_PINS_PER_FMLY;

	cfg0 = CHV_GPIO_PAD_CFG0(family_num, gpio_index);
	cfg1 = CHV_GPIO_PAD_CFG1(family_num, gpio_index);

	vlv_iosf_sb_get(dev_priv, BIT(VLV_IOSF_SB_GPIO));
	vlv_iosf_sb_write(dev_priv, port, cfg1, 0);
	vlv_iosf_sb_write(dev_priv, port, cfg0,
			  CHV_GPIO_GPIOEN | CHV_GPIO_GPIOCFG_GPO |
			  CHV_GPIO_GPIOTXSTATE(value));
	vlv_iosf_sb_put(dev_priv, BIT(VLV_IOSF_SB_GPIO));
}

static void bxt_exec_gpio(struct drm_i915_private *dev_priv,
			  u8 gpio_source, u8 gpio_index, bool value)
{
	/* XXX: this table is a quick ugly hack. */
	static struct gpio_desc *bxt_gpio_table[U8_MAX + 1];
	struct gpio_desc *gpio_desc = bxt_gpio_table[gpio_index];

	if (!gpio_desc) {
		gpio_desc = devm_gpiod_get_index(dev_priv->drm.dev,
						 NULL, gpio_index,
						 value ? GPIOD_OUT_LOW :
						 GPIOD_OUT_HIGH);

		if (IS_ERR_OR_NULL(gpio_desc)) {
			DRM_ERROR("GPIO index %u request failed (%ld)\n",
				  gpio_index, PTR_ERR(gpio_desc));
			return;
		}

		bxt_gpio_table[gpio_index] = gpio_desc;
	}

	gpiod_set_value(gpio_desc, value);
}

static void icl_exec_gpio(struct drm_i915_private *dev_priv,
			  u8 gpio_source, u8 gpio_index, bool value)
{
	DRM_DEBUG_KMS("Skipping ICL GPIO element execution\n");
}

static const u8 *mipi_exec_gpio(struct intel_dsi *intel_dsi, const u8 *data)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u8 gpio_source, gpio_index = 0, gpio_number;
	bool value;

	DRM_DEBUG_KMS("\n");

	if (dev_priv->vbt.dsi.seq_version >= 3)
		gpio_index = *data++;

	gpio_number = *data++;

	/* gpio source in sequence v2 only */
	if (dev_priv->vbt.dsi.seq_version == 2)
		gpio_source = (*data >> 1) & 3;
	else
		gpio_source = 0;

	/* pull up/down */
	value = *data++ & 1;

	if (INTEL_GEN(dev_priv) >= 11)
		icl_exec_gpio(dev_priv, gpio_source, gpio_index, value);
	else if (IS_VALLEYVIEW(dev_priv))
		vlv_exec_gpio(dev_priv, gpio_source, gpio_number, value);
	else if (IS_CHERRYVIEW(dev_priv))
		chv_exec_gpio(dev_priv, gpio_source, gpio_number, value);
	else
		bxt_exec_gpio(dev_priv, gpio_source, gpio_index, value);

	return data;
}

#ifdef CONFIG_ACPI
static int i2c_adapter_lookup(struct acpi_resource *ares, void *data)
{
	struct i2c_adapter_lookup *lookup = data;
	struct intel_dsi *intel_dsi = lookup->intel_dsi;
	struct acpi_resource_i2c_serialbus *sb;
	struct i2c_adapter *adapter;
	acpi_handle adapter_handle;
	acpi_status status;

	if (!i2c_acpi_get_i2c_resource(ares, &sb))
		return 1;

	if (lookup->slave_addr != sb->slave_address)
		return 1;

	status = acpi_get_handle(lookup->dev_handle,
				 sb->resource_source.string_ptr,
				 &adapter_handle);
	if (ACPI_FAILURE(status))
		return 1;

	adapter = i2c_acpi_find_adapter_by_handle(adapter_handle);
	if (adapter)
		intel_dsi->i2c_bus_num = adapter->nr;

	return 1;
}

static void i2c_acpi_find_adapter(struct intel_dsi *intel_dsi,
				  const u16 slave_addr)
{
	struct drm_device *drm_dev = intel_dsi->base.base.dev;
	struct device *dev = &drm_dev->pdev->dev;
	struct acpi_device *acpi_dev;
	struct list_head resource_list;
	struct i2c_adapter_lookup lookup;

	acpi_dev = ACPI_COMPANION(dev);
	if (acpi_dev) {
		memset(&lookup, 0, sizeof(lookup));
		lookup.slave_addr = slave_addr;
		lookup.intel_dsi = intel_dsi;
		lookup.dev_handle = acpi_device_handle(acpi_dev);

		INIT_LIST_HEAD(&resource_list);
		acpi_dev_get_resources(acpi_dev, &resource_list,
				       i2c_adapter_lookup,
				       &lookup);
		acpi_dev_free_resource_list(&resource_list);
	}
}
#else
static inline void i2c_acpi_find_adapter(struct intel_dsi *intel_dsi,
					 const u16 slave_addr)
{
}
#endif

static const u8 *mipi_exec_i2c(struct intel_dsi *intel_dsi, const u8 *data)
{
	struct drm_device *drm_dev = intel_dsi->base.base.dev;
	struct device *dev = &drm_dev->pdev->dev;
	struct i2c_adapter *adapter;
	struct i2c_msg msg;
	int ret;
	u8 vbt_i2c_bus_num = *(data + 2);
	u16 slave_addr = *(u16 *)(data + 3);
	u8 reg_offset = *(data + 5);
	u8 payload_size = *(data + 6);
	u8 *payload_data;

	if (intel_dsi->i2c_bus_num < 0) {
		intel_dsi->i2c_bus_num = vbt_i2c_bus_num;
		i2c_acpi_find_adapter(intel_dsi, slave_addr);
	}

	adapter = i2c_get_adapter(intel_dsi->i2c_bus_num);
	if (!adapter) {
		DRM_DEV_ERROR(dev, "Cannot find a valid i2c bus for xfer\n");
		goto err_bus;
	}

	payload_data = kzalloc(payload_size + 1, GFP_KERNEL);
	if (!payload_data)
		goto err_alloc;

	payload_data[0] = reg_offset;
	memcpy(&payload_data[1], (data + 7), payload_size);

	msg.addr = slave_addr;
	msg.flags = 0;
	msg.len = payload_size + 1;
	msg.buf = payload_data;

	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0)
		DRM_DEV_ERROR(dev,
			      "Failed to xfer payload of size (%u) to reg (%u)\n",
			      payload_size, reg_offset);

	kfree(payload_data);
err_alloc:
	i2c_put_adapter(adapter);
err_bus:
	return data + payload_size + 7;
}

static const u8 *mipi_exec_spi(struct intel_dsi *intel_dsi, const u8 *data)
{
	DRM_DEBUG_KMS("Skipping SPI element execution\n");

	return data + *(data + 5) + 6;
}

static const u8 *mipi_exec_pmic(struct intel_dsi *intel_dsi, const u8 *data)
{
#ifdef CONFIG_PMIC_OPREGION
	u32 value, mask, reg_address;
	u16 i2c_address;
	int ret;

	/* byte 0 aka PMIC Flag is reserved */
	i2c_address	= get_unaligned_le16(data + 1);
	reg_address	= get_unaligned_le32(data + 3);
	value		= get_unaligned_le32(data + 7);
	mask		= get_unaligned_le32(data + 11);

	ret = intel_soc_pmic_exec_mipi_pmic_seq_element(i2c_address,
							reg_address,
							value, mask);
	if (ret)
		DRM_ERROR("%s failed, error: %d\n", __func__, ret);
#else
	DRM_ERROR("Your hardware requires CONFIG_PMIC_OPREGION and it is not set\n");
#endif

	return data + 15;
}

typedef const u8 * (*fn_mipi_elem_exec)(struct intel_dsi *intel_dsi,
					const u8 *data);
static const fn_mipi_elem_exec exec_elem[] = {
	[MIPI_SEQ_ELEM_SEND_PKT] = mipi_exec_send_packet,
	[MIPI_SEQ_ELEM_DELAY] = mipi_exec_delay,
	[MIPI_SEQ_ELEM_GPIO] = mipi_exec_gpio,
	[MIPI_SEQ_ELEM_I2C] = mipi_exec_i2c,
	[MIPI_SEQ_ELEM_SPI] = mipi_exec_spi,
	[MIPI_SEQ_ELEM_PMIC] = mipi_exec_pmic,
};

/*
 * MIPI Sequence from VBT #53 parsing logic
 * We have already separated each seqence during bios parsing
 * Following is generic execution function for any sequence
 */

static const char * const seq_name[] = {
	[MIPI_SEQ_DEASSERT_RESET] = "MIPI_SEQ_DEASSERT_RESET",
	[MIPI_SEQ_INIT_OTP] = "MIPI_SEQ_INIT_OTP",
	[MIPI_SEQ_DISPLAY_ON] = "MIPI_SEQ_DISPLAY_ON",
	[MIPI_SEQ_DISPLAY_OFF]  = "MIPI_SEQ_DISPLAY_OFF",
	[MIPI_SEQ_ASSERT_RESET] = "MIPI_SEQ_ASSERT_RESET",
	[MIPI_SEQ_BACKLIGHT_ON] = "MIPI_SEQ_BACKLIGHT_ON",
	[MIPI_SEQ_BACKLIGHT_OFF] = "MIPI_SEQ_BACKLIGHT_OFF",
	[MIPI_SEQ_TEAR_ON] = "MIPI_SEQ_TEAR_ON",
	[MIPI_SEQ_TEAR_OFF] = "MIPI_SEQ_TEAR_OFF",
	[MIPI_SEQ_POWER_ON] = "MIPI_SEQ_POWER_ON",
	[MIPI_SEQ_POWER_OFF] = "MIPI_SEQ_POWER_OFF",
};

static const char *sequence_name(enum mipi_seq seq_id)
{
	if (seq_id < ARRAY_SIZE(seq_name) && seq_name[seq_id])
		return seq_name[seq_id];
	else
		return "(unknown)";
}

static void intel_dsi_vbt_exec(struct intel_dsi *intel_dsi,
			       enum mipi_seq seq_id)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);
	const u8 *data;
	fn_mipi_elem_exec mipi_elem_exec;

	if (WARN_ON(seq_id >= ARRAY_SIZE(dev_priv->vbt.dsi.sequence)))
		return;

	data = dev_priv->vbt.dsi.sequence[seq_id];
	if (!data)
		return;

	WARN_ON(*data != seq_id);

	DRM_DEBUG_KMS("Starting MIPI sequence %d - %s\n",
		      seq_id, sequence_name(seq_id));

	/* Skip Sequence Byte. */
	data++;

	/* Skip Size of Sequence. */
	if (dev_priv->vbt.dsi.seq_version >= 3)
		data += 4;

	while (1) {
		u8 operation_byte = *data++;
		u8 operation_size = 0;

		if (operation_byte == MIPI_SEQ_ELEM_END)
			break;

		if (operation_byte < ARRAY_SIZE(exec_elem))
			mipi_elem_exec = exec_elem[operation_byte];
		else
			mipi_elem_exec = NULL;

		/* Size of Operation. */
		if (dev_priv->vbt.dsi.seq_version >= 3)
			operation_size = *data++;

		if (mipi_elem_exec) {
			const u8 *next = data + operation_size;

			data = mipi_elem_exec(intel_dsi, data);

			/* Consistency check if we have size. */
			if (operation_size && data != next) {
				DRM_ERROR("Inconsistent operation size\n");
				return;
			}
		} else if (operation_size) {
			/* We have size, skip. */
			DRM_DEBUG_KMS("Unsupported MIPI operation byte %u\n",
				      operation_byte);
			data += operation_size;
		} else {
			/* No size, can't skip without parsing. */
			DRM_ERROR("Unsupported MIPI operation byte %u\n",
				  operation_byte);
			return;
		}
	}
}

void intel_dsi_vbt_exec_sequence(struct intel_dsi *intel_dsi,
				 enum mipi_seq seq_id)
{
	if (seq_id == MIPI_SEQ_POWER_ON && intel_dsi->gpio_panel)
		gpiod_set_value_cansleep(intel_dsi->gpio_panel, 1);
	if (seq_id == MIPI_SEQ_BACKLIGHT_ON && intel_dsi->gpio_backlight)
		gpiod_set_value_cansleep(intel_dsi->gpio_backlight, 1);

	intel_dsi_vbt_exec(intel_dsi, seq_id);

	if (seq_id == MIPI_SEQ_POWER_OFF && intel_dsi->gpio_panel)
		gpiod_set_value_cansleep(intel_dsi->gpio_panel, 0);
	if (seq_id == MIPI_SEQ_BACKLIGHT_OFF && intel_dsi->gpio_backlight)
		gpiod_set_value_cansleep(intel_dsi->gpio_backlight, 0);
}

void intel_dsi_msleep(struct intel_dsi *intel_dsi, int msec)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);

	/* For v3 VBTs in vid-mode the delays are part of the VBT sequences */
	if (is_vid_mode(intel_dsi) && dev_priv->vbt.dsi.seq_version >= 3)
		return;

	msleep(msec);
}

void intel_dsi_log_params(struct intel_dsi *intel_dsi)
{
	DRM_DEBUG_KMS("Pclk %d\n", intel_dsi->pclk);
	DRM_DEBUG_KMS("Pixel overlap %d\n", intel_dsi->pixel_overlap);
	DRM_DEBUG_KMS("Lane count %d\n", intel_dsi->lane_count);
	DRM_DEBUG_KMS("DPHY param reg 0x%x\n", intel_dsi->dphy_reg);
	DRM_DEBUG_KMS("Video mode format %s\n",
		      intel_dsi->video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE ?
		      "non-burst with sync pulse" :
		      intel_dsi->video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS ?
		      "non-burst with sync events" :
		      intel_dsi->video_mode_format == VIDEO_MODE_BURST ?
		      "burst" : "<unknown>");
	DRM_DEBUG_KMS("Burst mode ratio %d\n", intel_dsi->burst_mode_ratio);
	DRM_DEBUG_KMS("Reset timer %d\n", intel_dsi->rst_timer_val);
	DRM_DEBUG_KMS("Eot %s\n", enableddisabled(intel_dsi->eotp_pkt));
	DRM_DEBUG_KMS("Clockstop %s\n", enableddisabled(!intel_dsi->clock_stop));
	DRM_DEBUG_KMS("Mode %s\n", intel_dsi->operation_mode ? "command" : "video");
	if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
		DRM_DEBUG_KMS("Dual link: DSI_DUAL_LINK_FRONT_BACK\n");
	else if (intel_dsi->dual_link == DSI_DUAL_LINK_PIXEL_ALT)
		DRM_DEBUG_KMS("Dual link: DSI_DUAL_LINK_PIXEL_ALT\n");
	else
		DRM_DEBUG_KMS("Dual link: NONE\n");
	DRM_DEBUG_KMS("Pixel Format %d\n", intel_dsi->pixel_format);
	DRM_DEBUG_KMS("TLPX %d\n", intel_dsi->escape_clk_div);
	DRM_DEBUG_KMS("LP RX Timeout 0x%x\n", intel_dsi->lp_rx_timeout);
	DRM_DEBUG_KMS("Turnaround Timeout 0x%x\n", intel_dsi->turn_arnd_val);
	DRM_DEBUG_KMS("Init Count 0x%x\n", intel_dsi->init_count);
	DRM_DEBUG_KMS("HS to LP Count 0x%x\n", intel_dsi->hs_to_lp_count);
	DRM_DEBUG_KMS("LP Byte Clock %d\n", intel_dsi->lp_byte_clk);
	DRM_DEBUG_KMS("DBI BW Timer 0x%x\n", intel_dsi->bw_timer);
	DRM_DEBUG_KMS("LP to HS Clock Count 0x%x\n", intel_dsi->clk_lp_to_hs_count);
	DRM_DEBUG_KMS("HS to LP Clock Count 0x%x\n", intel_dsi->clk_hs_to_lp_count);
	DRM_DEBUG_KMS("BTA %s\n",
			enableddisabled(!(intel_dsi->video_frmt_cfg_bits & DISABLE_VIDEO_BTA)));
}

bool intel_dsi_vbt_init(struct intel_dsi *intel_dsi, u16 panel_id)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct mipi_config *mipi_config = dev_priv->vbt.dsi.config;
	struct mipi_pps_data *pps = dev_priv->vbt.dsi.pps;
	struct drm_display_mode *mode = dev_priv->vbt.lfp_lvds_vbt_mode;
	u16 burst_mode_ratio;
	enum port port;

	DRM_DEBUG_KMS("\n");

	intel_dsi->eotp_pkt = mipi_config->eot_pkt_disabled ? 0 : 1;
	intel_dsi->clock_stop = mipi_config->enable_clk_stop ? 1 : 0;
	intel_dsi->lane_count = mipi_config->lane_cnt + 1;
	intel_dsi->pixel_format =
			pixel_format_from_register_bits(
				mipi_config->videomode_color_format << 7);

	intel_dsi->dual_link = mipi_config->dual_link;
	intel_dsi->pixel_overlap = mipi_config->pixel_overlap;
	intel_dsi->operation_mode = mipi_config->is_cmd_mode;
	intel_dsi->video_mode_format = mipi_config->video_transfer_mode;
	intel_dsi->escape_clk_div = mipi_config->byte_clk_sel;
	intel_dsi->lp_rx_timeout = mipi_config->lp_rx_timeout;
	intel_dsi->hs_tx_timeout = mipi_config->hs_tx_timeout;
	intel_dsi->turn_arnd_val = mipi_config->turn_around_timeout;
	intel_dsi->rst_timer_val = mipi_config->device_reset_timer;
	intel_dsi->init_count = mipi_config->master_init_timer;
	intel_dsi->bw_timer = mipi_config->dbi_bw_timer;
	intel_dsi->video_frmt_cfg_bits =
		mipi_config->bta_enabled ? DISABLE_VIDEO_BTA : 0;
	intel_dsi->bgr_enabled = mipi_config->rgb_flip;

	/* Starting point, adjusted depending on dual link and burst mode */
	intel_dsi->pclk = mode->clock;

	/* In dual link mode each port needs half of pixel clock */
	if (intel_dsi->dual_link) {
		intel_dsi->pclk /= 2;

		/* we can enable pixel_overlap if needed by panel. In this
		 * case we need to increase the pixelclock for extra pixels
		 */
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK) {
			intel_dsi->pclk += DIV_ROUND_UP(mode->vtotal * intel_dsi->pixel_overlap * 60, 1000);
		}
	}

	/* Burst Mode Ratio
	 * Target ddr frequency from VBT / non burst ddr freq
	 * multiply by 100 to preserve remainder
	 */
	if (intel_dsi->video_mode_format == VIDEO_MODE_BURST) {
		if (mipi_config->target_burst_mode_freq) {
			u32 bitrate = intel_dsi_bitrate(intel_dsi);

			/*
			 * Sometimes the VBT contains a slightly lower clock,
			 * then the bitrate we have calculated, in this case
			 * just replace it with the calculated bitrate.
			 */
			if (mipi_config->target_burst_mode_freq < bitrate &&
			    intel_fuzzy_clock_check(
					mipi_config->target_burst_mode_freq,
					bitrate))
				mipi_config->target_burst_mode_freq = bitrate;

			if (mipi_config->target_burst_mode_freq < bitrate) {
				DRM_ERROR("Burst mode freq is less than computed\n");
				return false;
			}

			burst_mode_ratio = DIV_ROUND_UP(
				mipi_config->target_burst_mode_freq * 100,
				bitrate);

			intel_dsi->pclk = DIV_ROUND_UP(intel_dsi->pclk * burst_mode_ratio, 100);
		} else {
			DRM_ERROR("Burst mode target is not set\n");
			return false;
		}
	} else
		burst_mode_ratio = 100;

	intel_dsi->burst_mode_ratio = burst_mode_ratio;

	/* delays in VBT are in unit of 100us, so need to convert
	 * here in ms
	 * Delay (100us) * 100 /1000 = Delay / 10 (ms) */
	intel_dsi->backlight_off_delay = pps->bl_disable_delay / 10;
	intel_dsi->backlight_on_delay = pps->bl_enable_delay / 10;
	intel_dsi->panel_on_delay = pps->panel_on_delay / 10;
	intel_dsi->panel_off_delay = pps->panel_off_delay / 10;
	intel_dsi->panel_pwr_cycle_delay = pps->panel_power_cycle_delay / 10;

	intel_dsi->i2c_bus_num = -1;

	/* a regular driver would get the device in probe */
	for_each_dsi_port(port, intel_dsi->ports) {
		mipi_dsi_attach(intel_dsi->dsi_hosts[port]->device);
	}

	return true;
}

/*
 * On some BYT/CHT devs some sequences are incomplete and we need to manually
 * control some GPIOs. We need to add a GPIO lookup table before we get these.
 * If the GOP did not initialize the panel (HDMI inserted) we may need to also
 * change the pinmux for the SoC's PWM0 pin from GPIO to PWM.
 */
static struct gpiod_lookup_table pmic_panel_gpio_table = {
	/* Intel GFX is consumer */
	.dev_id = "0000:00:02.0",
	.table = {
		/* Panel EN/DISABLE */
		GPIO_LOOKUP("gpio_crystalcove", 94, "panel", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table soc_panel_gpio_table = {
	.dev_id = "0000:00:02.0",
	.table = {
		GPIO_LOOKUP("INT33FC:01", 10, "backlight", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:01", 11, "panel", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static const struct pinctrl_map soc_pwm_pinctrl_map[] = {
	PIN_MAP_MUX_GROUP("0000:00:02.0", "soc_pwm0", "INT33FC:00",
			  "pwm0_grp", "pwm"),
};

void intel_dsi_vbt_gpio_init(struct intel_dsi *intel_dsi, bool panel_is_on)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct mipi_config *mipi_config = dev_priv->vbt.dsi.config;
	enum gpiod_flags flags = panel_is_on ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;
	bool want_backlight_gpio = false;
	bool want_panel_gpio = false;
	struct pinctrl *pinctrl;
	int ret;

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    mipi_config->pwm_blc == PPS_BLC_PMIC) {
		gpiod_add_lookup_table(&pmic_panel_gpio_table);
		want_panel_gpio = true;
	}

	if (IS_VALLEYVIEW(dev_priv) && mipi_config->pwm_blc == PPS_BLC_SOC) {
		gpiod_add_lookup_table(&soc_panel_gpio_table);
		want_panel_gpio = true;
		want_backlight_gpio = true;

		/* Ensure PWM0 pin is muxed as PWM instead of GPIO */
		ret = pinctrl_register_mappings(soc_pwm_pinctrl_map,
					     ARRAY_SIZE(soc_pwm_pinctrl_map));
		if (ret)
			DRM_ERROR("Failed to register pwm0 pinmux mapping\n");

		pinctrl = devm_pinctrl_get_select(dev->dev, "soc_pwm0");
		if (IS_ERR(pinctrl))
			DRM_ERROR("Failed to set pinmux to PWM\n");
	}

	if (want_panel_gpio) {
		intel_dsi->gpio_panel = gpiod_get(dev->dev, "panel", flags);
		if (IS_ERR(intel_dsi->gpio_panel)) {
			DRM_ERROR("Failed to own gpio for panel control\n");
			intel_dsi->gpio_panel = NULL;
		}
	}

	if (want_backlight_gpio) {
		intel_dsi->gpio_backlight =
			gpiod_get(dev->dev, "backlight", flags);
		if (IS_ERR(intel_dsi->gpio_backlight)) {
			DRM_ERROR("Failed to own gpio for backlight control\n");
			intel_dsi->gpio_backlight = NULL;
		}
	}
}

void intel_dsi_vbt_gpio_cleanup(struct intel_dsi *intel_dsi)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct mipi_config *mipi_config = dev_priv->vbt.dsi.config;

	if (intel_dsi->gpio_panel) {
		gpiod_put(intel_dsi->gpio_panel);
		intel_dsi->gpio_panel = NULL;
	}

	if (intel_dsi->gpio_backlight) {
		gpiod_put(intel_dsi->gpio_backlight);
		intel_dsi->gpio_backlight = NULL;
	}

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    mipi_config->pwm_blc == PPS_BLC_PMIC)
		gpiod_remove_lookup_table(&pmic_panel_gpio_table);

	if (IS_VALLEYVIEW(dev_priv) && mipi_config->pwm_blc == PPS_BLC_SOC) {
		pinctrl_unregister_mappings(soc_pwm_pinctrl_map);
		gpiod_remove_lookup_table(&soc_panel_gpio_table);
	}
}
