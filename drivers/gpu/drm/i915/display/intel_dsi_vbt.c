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
#include <linux/string_helpers.h>

#include <linux/unaligned.h>

#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>

#include <video/mipi_display.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dsi.h"
#include "intel_dsi_vbt.h"
#include "intel_gmbus_regs.h"
#include "intel_pps_regs.h"
#include "vlv_dsi.h"
#include "vlv_dsi_regs.h"
#include "vlv_sideband.h"

#define MIPI_TRANSFER_MODE_SHIFT	0
#define MIPI_VIRTUAL_CHANNEL_SHIFT	1
#define MIPI_PORT_SHIFT			3

struct i2c_adapter_lookup {
	u16 target_addr;
	struct intel_dsi *intel_dsi;
	acpi_handle dev_handle;
};

#define CHV_GPIO_IDX_START_N		0
#define CHV_GPIO_IDX_START_E		73
#define CHV_GPIO_IDX_START_SW		100
#define CHV_GPIO_IDX_START_SE		198

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

static enum port intel_dsi_seq_port_to_port(struct intel_dsi *intel_dsi,
					    u8 seq_port)
{
	/*
	 * If single link DSI is being used on any port, the VBT sequence block
	 * send packet apparently always has 0 for the port. Just use the port
	 * we have configured, and ignore the sequence block port.
	 */
	if (hweight8(intel_dsi->ports) == 1)
		return ffs(intel_dsi->ports) - 1;

	if (seq_port) {
		if (intel_dsi->ports & BIT(PORT_B))
			return PORT_B;
		if (intel_dsi->ports & BIT(PORT_C))
			return PORT_C;
	}

	return PORT_A;
}

static const u8 *mipi_exec_send_packet(struct intel_dsi *intel_dsi,
				       const u8 *data)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);
	struct mipi_dsi_device *dsi_device;
	u8 type, flags, seq_port;
	u16 len;
	enum port port;

	drm_dbg_kms(&dev_priv->drm, "\n");

	flags = *data++;
	type = *data++;

	len = *((u16 *) data);
	data += 2;

	seq_port = (flags >> MIPI_PORT_SHIFT) & 3;

	port = intel_dsi_seq_port_to_port(intel_dsi, seq_port);

	if (drm_WARN_ON(&dev_priv->drm, !intel_dsi->dsi_hosts[port]))
		goto out;

	dsi_device = intel_dsi->dsi_hosts[port]->device;
	if (!dsi_device) {
		drm_dbg_kms(&dev_priv->drm, "no dsi device for port %c\n",
			    port_name(port));
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
		drm_dbg(&dev_priv->drm,
			"Generic Read not yet implemented or used\n");
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
		drm_dbg(&dev_priv->drm,
			"DCS Read not yet implemented or used\n");
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		mipi_dsi_dcs_write_buffer(dsi_device, data, len);
		break;
	}

	if (DISPLAY_VER(dev_priv) < 11)
		vlv_dsi_wait_for_fifo_empty(intel_dsi, port);

out:
	data += len;

	return data;
}

static const u8 *mipi_exec_delay(struct intel_dsi *intel_dsi, const u8 *data)
{
	struct drm_i915_private *i915 = to_i915(intel_dsi->base.base.dev);
	u32 delay = *((const u32 *) data);

	drm_dbg_kms(&i915->drm, "%d usecs\n", delay);

	usleep_range(delay, delay + 10);
	data += 4;

	return data;
}

static void soc_gpio_set_value(struct intel_connector *connector, u8 gpio_index,
			       const char *con_id, u8 idx, bool value)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	/* XXX: this table is a quick ugly hack. */
	static struct gpio_desc *soc_gpio_table[U8_MAX + 1];
	struct gpio_desc *gpio_desc = soc_gpio_table[gpio_index];

	if (gpio_desc) {
		gpiod_set_value(gpio_desc, value);
	} else {
		gpio_desc = devm_gpiod_get_index(dev_priv->drm.dev, con_id, idx,
						 value ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW);
		if (IS_ERR(gpio_desc)) {
			drm_err(&dev_priv->drm,
				"GPIO index %u request failed (%pe)\n",
				gpio_index, gpio_desc);
			return;
		}

		soc_gpio_table[gpio_index] = gpio_desc;
	}
}

static void soc_opaque_gpio_set_value(struct intel_connector *connector,
				      u8 gpio_index, const char *chip,
				      const char *con_id, u8 idx, bool value)
{
	struct gpiod_lookup_table *lookup;

	lookup = kzalloc(struct_size(lookup, table, 2), GFP_KERNEL);
	if (!lookup)
		return;

	lookup->dev_id = "0000:00:02.0";
	lookup->table[0] =
		GPIO_LOOKUP_IDX(chip, idx, con_id, idx, GPIO_ACTIVE_HIGH);

	gpiod_add_lookup_table(lookup);

	soc_gpio_set_value(connector, gpio_index, con_id, idx, value);

	gpiod_remove_lookup_table(lookup);
	kfree(lookup);
}

static void vlv_gpio_set_value(struct intel_connector *connector,
			       u8 gpio_source, u8 gpio_index, bool value)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);

	/* XXX: this assumes vlv_gpio_table only has NC GPIOs. */
	if (connector->panel.vbt.dsi.seq_version < 3) {
		if (gpio_source == 1) {
			drm_dbg_kms(&dev_priv->drm, "SC gpio not supported\n");
			return;
		}
		if (gpio_source > 1) {
			drm_dbg_kms(&dev_priv->drm,
				    "unknown gpio source %u\n", gpio_source);
			return;
		}
	}

	soc_opaque_gpio_set_value(connector, gpio_index,
				  "INT33FC:01", "Panel N", gpio_index, value);
}

static void chv_gpio_set_value(struct intel_connector *connector,
			       u8 gpio_source, u8 gpio_index, bool value)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);

	if (connector->panel.vbt.dsi.seq_version >= 3) {
		if (gpio_index >= CHV_GPIO_IDX_START_SE) {
			/* XXX: it's unclear whether 255->57 is part of SE. */
			soc_opaque_gpio_set_value(connector, gpio_index, "INT33FF:03", "Panel SE",
						  gpio_index - CHV_GPIO_IDX_START_SE, value);
		} else if (gpio_index >= CHV_GPIO_IDX_START_SW) {
			soc_opaque_gpio_set_value(connector, gpio_index, "INT33FF:00", "Panel SW",
						  gpio_index - CHV_GPIO_IDX_START_SW, value);
		} else if (gpio_index >= CHV_GPIO_IDX_START_E) {
			soc_opaque_gpio_set_value(connector, gpio_index, "INT33FF:02", "Panel E",
						  gpio_index - CHV_GPIO_IDX_START_E, value);
		} else {
			soc_opaque_gpio_set_value(connector, gpio_index, "INT33FF:01", "Panel N",
						  gpio_index - CHV_GPIO_IDX_START_N, value);
		}
	} else {
		/* XXX: The spec is unclear about CHV GPIO on seq v2 */
		if (gpio_source != 0) {
			drm_dbg_kms(&dev_priv->drm,
				    "unknown gpio source %u\n", gpio_source);
			return;
		}

		if (gpio_index >= CHV_GPIO_IDX_START_E) {
			drm_dbg_kms(&dev_priv->drm,
				    "invalid gpio index %u for GPIO N\n",
				    gpio_index);
			return;
		}

		soc_opaque_gpio_set_value(connector, gpio_index, "INT33FF:01", "Panel N",
					  gpio_index - CHV_GPIO_IDX_START_N, value);
	}
}

static void bxt_gpio_set_value(struct intel_connector *connector,
			       u8 gpio_index, bool value)
{
	soc_gpio_set_value(connector, gpio_index, NULL, gpio_index, value);
}

enum {
	MIPI_RESET_1 = 0,
	MIPI_AVDD_EN_1,
	MIPI_BKLT_EN_1,
	MIPI_AVEE_EN_1,
	MIPI_VIO_EN_1,
	MIPI_RESET_2,
	MIPI_AVDD_EN_2,
	MIPI_BKLT_EN_2,
	MIPI_AVEE_EN_2,
	MIPI_VIO_EN_2,
};

static void icl_native_gpio_set_value(struct drm_i915_private *dev_priv,
				      int gpio, bool value)
{
	struct intel_display *display = &dev_priv->display;
	int index;

	if (drm_WARN_ON(&dev_priv->drm, DISPLAY_VER(dev_priv) == 11 && gpio >= MIPI_RESET_2))
		return;

	switch (gpio) {
	case MIPI_RESET_1:
	case MIPI_RESET_2:
		index = gpio == MIPI_RESET_1 ? HPD_PORT_A : HPD_PORT_B;

		/*
		 * Disable HPD to set the pin to output, and set output
		 * value. The HPD pin should not be enabled for DSI anyway,
		 * assuming the board design and VBT are sane, and the pin isn't
		 * used by a non-DSI encoder.
		 *
		 * The locking protects against concurrent SHOTPLUG_CTL_DDI
		 * modifications in irq setup and handling.
		 */
		spin_lock_irq(&dev_priv->irq_lock);
		intel_de_rmw(dev_priv, SHOTPLUG_CTL_DDI,
			     SHOTPLUG_CTL_DDI_HPD_ENABLE(index) |
			     SHOTPLUG_CTL_DDI_HPD_OUTPUT_DATA(index),
			     value ? SHOTPLUG_CTL_DDI_HPD_OUTPUT_DATA(index) : 0);
		spin_unlock_irq(&dev_priv->irq_lock);
		break;
	case MIPI_AVDD_EN_1:
	case MIPI_AVDD_EN_2:
		index = gpio == MIPI_AVDD_EN_1 ? 0 : 1;

		intel_de_rmw(dev_priv, PP_CONTROL(dev_priv, index), PANEL_POWER_ON,
			     value ? PANEL_POWER_ON : 0);
		break;
	case MIPI_BKLT_EN_1:
	case MIPI_BKLT_EN_2:
		index = gpio == MIPI_BKLT_EN_1 ? 0 : 1;

		intel_de_rmw(dev_priv, PP_CONTROL(dev_priv, index), EDP_BLC_ENABLE,
			     value ? EDP_BLC_ENABLE : 0);
		break;
	case MIPI_AVEE_EN_1:
	case MIPI_AVEE_EN_2:
		index = gpio == MIPI_AVEE_EN_1 ? 1 : 2;

		intel_de_rmw(display, GPIO(display, index),
			     GPIO_CLOCK_VAL_OUT,
			     GPIO_CLOCK_DIR_MASK | GPIO_CLOCK_DIR_OUT |
			     GPIO_CLOCK_VAL_MASK | (value ? GPIO_CLOCK_VAL_OUT : 0));
		break;
	case MIPI_VIO_EN_1:
	case MIPI_VIO_EN_2:
		index = gpio == MIPI_VIO_EN_1 ? 1 : 2;

		intel_de_rmw(display, GPIO(display, index),
			     GPIO_DATA_VAL_OUT,
			     GPIO_DATA_DIR_MASK | GPIO_DATA_DIR_OUT |
			     GPIO_DATA_VAL_MASK | (value ? GPIO_DATA_VAL_OUT : 0));
		break;
	default:
		MISSING_CASE(gpio);
	}
}

static const u8 *mipi_exec_gpio(struct intel_dsi *intel_dsi, const u8 *data)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_connector *connector = intel_dsi->attached_connector;
	u8 gpio_source = 0, gpio_index = 0, gpio_number;
	bool value;
	int size;
	bool native = DISPLAY_VER(i915) >= 11;

	if (connector->panel.vbt.dsi.seq_version >= 3) {
		size = 3;

		gpio_index = data[0];
		gpio_number = data[1];
		value = data[2] & BIT(0);

		if (connector->panel.vbt.dsi.seq_version >= 4 && data[2] & BIT(1))
			native = false;
	} else {
		size = 2;

		gpio_number = data[0];
		value = data[1] & BIT(0);

		if (connector->panel.vbt.dsi.seq_version == 2)
			gpio_source = (data[1] >> 1) & 3;
	}

	drm_dbg_kms(&i915->drm, "GPIO index %u, number %u, source %u, native %s, set to %s\n",
		    gpio_index, gpio_number, gpio_source, str_yes_no(native), str_on_off(value));

	if (native)
		icl_native_gpio_set_value(i915, gpio_number, value);
	else if (DISPLAY_VER(i915) >= 9)
		bxt_gpio_set_value(connector, gpio_index, value);
	else if (IS_VALLEYVIEW(i915))
		vlv_gpio_set_value(connector, gpio_source, gpio_number, value);
	else if (IS_CHERRYVIEW(i915))
		chv_gpio_set_value(connector, gpio_source, gpio_number, value);

	return data + size;
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

	if (lookup->target_addr != sb->slave_address)
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
				  const u16 target_addr)
{
	struct drm_device *drm_dev = intel_dsi->base.base.dev;
	struct acpi_device *adev = ACPI_COMPANION(drm_dev->dev);
	struct i2c_adapter_lookup lookup = {
		.target_addr = target_addr,
		.intel_dsi = intel_dsi,
		.dev_handle = acpi_device_handle(adev),
	};
	LIST_HEAD(resource_list);

	acpi_dev_get_resources(adev, &resource_list, i2c_adapter_lookup, &lookup);
	acpi_dev_free_resource_list(&resource_list);
}
#else
static inline void i2c_acpi_find_adapter(struct intel_dsi *intel_dsi,
					 const u16 target_addr)
{
}
#endif

static const u8 *mipi_exec_i2c(struct intel_dsi *intel_dsi, const u8 *data)
{
	struct drm_i915_private *i915 = to_i915(intel_dsi->base.base.dev);
	struct i2c_adapter *adapter;
	struct i2c_msg msg;
	int ret;
	u8 vbt_i2c_bus_num = *(data + 2);
	u16 target_addr = *(u16 *)(data + 3);
	u8 reg_offset = *(data + 5);
	u8 payload_size = *(data + 6);
	u8 *payload_data;

	drm_dbg_kms(&i915->drm, "bus %d target-addr 0x%02x reg 0x%02x data %*ph\n",
		    vbt_i2c_bus_num, target_addr, reg_offset, payload_size, data + 7);

	if (intel_dsi->i2c_bus_num < 0) {
		intel_dsi->i2c_bus_num = vbt_i2c_bus_num;
		i2c_acpi_find_adapter(intel_dsi, target_addr);
	}

	adapter = i2c_get_adapter(intel_dsi->i2c_bus_num);
	if (!adapter) {
		drm_err(&i915->drm, "Cannot find a valid i2c bus for xfer\n");
		goto err_bus;
	}

	payload_data = kzalloc(payload_size + 1, GFP_KERNEL);
	if (!payload_data)
		goto err_alloc;

	payload_data[0] = reg_offset;
	memcpy(&payload_data[1], (data + 7), payload_size);

	msg.addr = target_addr;
	msg.flags = 0;
	msg.len = payload_size + 1;
	msg.buf = payload_data;

	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0)
		drm_err(&i915->drm,
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
	struct drm_i915_private *i915 = to_i915(intel_dsi->base.base.dev);

	drm_dbg_kms(&i915->drm, "Skipping SPI element execution\n");

	return data + *(data + 5) + 6;
}

static const u8 *mipi_exec_pmic(struct intel_dsi *intel_dsi, const u8 *data)
{
	struct drm_i915_private *i915 = to_i915(intel_dsi->base.base.dev);
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
		drm_err(&i915->drm, "%s failed, error: %d\n", __func__, ret);
#else
	drm_err(&i915->drm,
		"Your hardware requires CONFIG_PMIC_OPREGION and it is not set\n");
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
	[MIPI_SEQ_END] = "MIPI_SEQ_END",
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
	if (seq_id < ARRAY_SIZE(seq_name))
		return seq_name[seq_id];

	return "(unknown)";
}

static void intel_dsi_vbt_exec(struct intel_dsi *intel_dsi,
			       enum mipi_seq seq_id)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);
	struct intel_connector *connector = intel_dsi->attached_connector;
	const u8 *data;
	fn_mipi_elem_exec mipi_elem_exec;

	if (drm_WARN_ON(&dev_priv->drm,
			seq_id >= ARRAY_SIZE(connector->panel.vbt.dsi.sequence)))
		return;

	data = connector->panel.vbt.dsi.sequence[seq_id];
	if (!data)
		return;

	drm_WARN_ON(&dev_priv->drm, *data != seq_id);

	drm_dbg_kms(&dev_priv->drm, "Starting MIPI sequence %d - %s\n",
		    seq_id, sequence_name(seq_id));

	/* Skip Sequence Byte. */
	data++;

	/* Skip Size of Sequence. */
	if (connector->panel.vbt.dsi.seq_version >= 3)
		data += 4;

	while (*data != MIPI_SEQ_ELEM_END) {
		u8 operation_byte = *data++;
		u8 operation_size = 0;

		if (operation_byte < ARRAY_SIZE(exec_elem))
			mipi_elem_exec = exec_elem[operation_byte];
		else
			mipi_elem_exec = NULL;

		/* Size of Operation. */
		if (connector->panel.vbt.dsi.seq_version >= 3)
			operation_size = *data++;

		if (mipi_elem_exec) {
			const u8 *next = data + operation_size;

			data = mipi_elem_exec(intel_dsi, data);

			/* Consistency check if we have size. */
			if (operation_size && data != next) {
				drm_err(&dev_priv->drm,
					"Inconsistent operation size\n");
				return;
			}
		} else if (operation_size) {
			/* We have size, skip. */
			drm_dbg_kms(&dev_priv->drm,
				    "Unsupported MIPI operation byte %u\n",
				    operation_byte);
			data += operation_size;
		} else {
			/* No size, can't skip without parsing. */
			drm_err(&dev_priv->drm,
				"Unsupported MIPI operation byte %u\n",
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

void intel_dsi_log_params(struct intel_dsi *intel_dsi)
{
	struct drm_i915_private *i915 = to_i915(intel_dsi->base.base.dev);

	drm_dbg_kms(&i915->drm, "Pclk %d\n", intel_dsi->pclk);
	drm_dbg_kms(&i915->drm, "Pixel overlap %d\n",
		    intel_dsi->pixel_overlap);
	drm_dbg_kms(&i915->drm, "Lane count %d\n", intel_dsi->lane_count);
	drm_dbg_kms(&i915->drm, "DPHY param reg 0x%x\n", intel_dsi->dphy_reg);
	drm_dbg_kms(&i915->drm, "Video mode format %s\n",
		    intel_dsi->video_mode == NON_BURST_SYNC_PULSE ?
		    "non-burst with sync pulse" :
		    intel_dsi->video_mode == NON_BURST_SYNC_EVENTS ?
		    "non-burst with sync events" :
		    intel_dsi->video_mode == BURST_MODE ?
		    "burst" : "<unknown>");
	drm_dbg_kms(&i915->drm, "Burst mode ratio %d\n",
		    intel_dsi->burst_mode_ratio);
	drm_dbg_kms(&i915->drm, "Reset timer %d\n", intel_dsi->rst_timer_val);
	drm_dbg_kms(&i915->drm, "Eot %s\n",
		    str_enabled_disabled(intel_dsi->eotp_pkt));
	drm_dbg_kms(&i915->drm, "Clockstop %s\n",
		    str_enabled_disabled(!intel_dsi->clock_stop));
	drm_dbg_kms(&i915->drm, "Mode %s\n",
		    intel_dsi->operation_mode ? "command" : "video");
	if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
		drm_dbg_kms(&i915->drm,
			    "Dual link: DSI_DUAL_LINK_FRONT_BACK\n");
	else if (intel_dsi->dual_link == DSI_DUAL_LINK_PIXEL_ALT)
		drm_dbg_kms(&i915->drm,
			    "Dual link: DSI_DUAL_LINK_PIXEL_ALT\n");
	else
		drm_dbg_kms(&i915->drm, "Dual link: NONE\n");
	drm_dbg_kms(&i915->drm, "Pixel Format %d\n", intel_dsi->pixel_format);
	drm_dbg_kms(&i915->drm, "TLPX %d\n", intel_dsi->escape_clk_div);
	drm_dbg_kms(&i915->drm, "LP RX Timeout 0x%x\n",
		    intel_dsi->lp_rx_timeout);
	drm_dbg_kms(&i915->drm, "Turnaround Timeout 0x%x\n",
		    intel_dsi->turn_arnd_val);
	drm_dbg_kms(&i915->drm, "Init Count 0x%x\n", intel_dsi->init_count);
	drm_dbg_kms(&i915->drm, "HS to LP Count 0x%x\n",
		    intel_dsi->hs_to_lp_count);
	drm_dbg_kms(&i915->drm, "LP Byte Clock %d\n", intel_dsi->lp_byte_clk);
	drm_dbg_kms(&i915->drm, "DBI BW Timer 0x%x\n", intel_dsi->bw_timer);
	drm_dbg_kms(&i915->drm, "LP to HS Clock Count 0x%x\n",
		    intel_dsi->clk_lp_to_hs_count);
	drm_dbg_kms(&i915->drm, "HS to LP Clock Count 0x%x\n",
		    intel_dsi->clk_hs_to_lp_count);
	drm_dbg_kms(&i915->drm, "BTA %s\n",
		    str_enabled_disabled(!(intel_dsi->video_frmt_cfg_bits & DISABLE_VIDEO_BTA)));
}

bool intel_dsi_vbt_init(struct intel_dsi *intel_dsi, u16 panel_id)
{
	struct drm_device *dev = intel_dsi->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_connector *connector = intel_dsi->attached_connector;
	struct mipi_config *mipi_config = connector->panel.vbt.dsi.config;
	struct mipi_pps_data *pps = connector->panel.vbt.dsi.pps;
	struct drm_display_mode *mode = connector->panel.vbt.lfp_vbt_mode;
	u16 burst_mode_ratio;
	enum port port;

	drm_dbg_kms(&dev_priv->drm, "\n");

	intel_dsi->eotp_pkt = mipi_config->eot_pkt_disabled ? 0 : 1;
	intel_dsi->clock_stop = mipi_config->enable_clk_stop ? 1 : 0;
	intel_dsi->lane_count = mipi_config->lane_cnt + 1;
	intel_dsi->pixel_format =
			pixel_format_from_register_bits(
				mipi_config->videomode_color_format << 7);

	intel_dsi->dual_link = mipi_config->dual_link;
	intel_dsi->pixel_overlap = mipi_config->pixel_overlap;
	intel_dsi->operation_mode = mipi_config->is_cmd_mode;
	intel_dsi->video_mode = mipi_config->video_transfer_mode;
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
	if (intel_dsi->video_mode == BURST_MODE) {
		u32 bitrate;

		if (mipi_config->target_burst_mode_freq == 0) {
			drm_err(&dev_priv->drm, "Burst mode target is not set\n");
			return false;
		}

		bitrate = intel_dsi_bitrate(intel_dsi);

		/*
		 * Sometimes the VBT contains a slightly lower clock, then
		 * the bitrate we have calculated, in this case just replace it
		 * with the calculated bitrate.
		 */
		if (mipi_config->target_burst_mode_freq < bitrate &&
		    intel_fuzzy_clock_check(mipi_config->target_burst_mode_freq,
					    bitrate))
			mipi_config->target_burst_mode_freq = bitrate;

		if (mipi_config->target_burst_mode_freq < bitrate) {
			drm_err(&dev_priv->drm, "Burst mode freq is less than computed\n");
			return false;
		}

		burst_mode_ratio =
			DIV_ROUND_UP(mipi_config->target_burst_mode_freq * 100, bitrate);

		intel_dsi->pclk = DIV_ROUND_UP(intel_dsi->pclk * burst_mode_ratio, 100);
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
	struct intel_connector *connector = intel_dsi->attached_connector;
	struct mipi_config *mipi_config = connector->panel.vbt.dsi.config;
	enum gpiod_flags flags = panel_is_on ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;
	struct gpiod_lookup_table *gpiod_lookup_table = NULL;
	bool want_backlight_gpio = false;
	bool want_panel_gpio = false;
	struct pinctrl *pinctrl;
	int ret;

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    mipi_config->pwm_blc == PPS_BLC_PMIC) {
		gpiod_lookup_table = &pmic_panel_gpio_table;
		want_panel_gpio = true;
	}

	if (IS_VALLEYVIEW(dev_priv) && mipi_config->pwm_blc == PPS_BLC_SOC) {
		gpiod_lookup_table = &soc_panel_gpio_table;
		want_panel_gpio = true;
		want_backlight_gpio = true;

		/* Ensure PWM0 pin is muxed as PWM instead of GPIO */
		ret = pinctrl_register_mappings(soc_pwm_pinctrl_map,
					     ARRAY_SIZE(soc_pwm_pinctrl_map));
		if (ret)
			drm_err(&dev_priv->drm,
				"Failed to register pwm0 pinmux mapping\n");

		pinctrl = devm_pinctrl_get_select(dev->dev, "soc_pwm0");
		if (IS_ERR(pinctrl))
			drm_err(&dev_priv->drm,
				"Failed to set pinmux to PWM\n");
	}

	if (gpiod_lookup_table)
		gpiod_add_lookup_table(gpiod_lookup_table);

	if (want_panel_gpio) {
		intel_dsi->gpio_panel = devm_gpiod_get(dev->dev, "panel", flags);
		if (IS_ERR(intel_dsi->gpio_panel)) {
			drm_err(&dev_priv->drm,
				"Failed to own gpio for panel control\n");
			intel_dsi->gpio_panel = NULL;
		}
	}

	if (want_backlight_gpio) {
		intel_dsi->gpio_backlight =
			devm_gpiod_get(dev->dev, "backlight", flags);
		if (IS_ERR(intel_dsi->gpio_backlight)) {
			drm_err(&dev_priv->drm,
				"Failed to own gpio for backlight control\n");
			intel_dsi->gpio_backlight = NULL;
		}
	}

	if (gpiod_lookup_table)
		gpiod_remove_lookup_table(gpiod_lookup_table);
}
