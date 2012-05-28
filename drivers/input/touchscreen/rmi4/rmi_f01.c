/*
 * Copyright (c) 2011-2012 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/slab.h>
#include "rmi_driver.h"

/* control register bits */
#define RMI_SLEEP_MODE_NORMAL (0x00)
#define RMI_SLEEP_MODE_SENSOR_SLEEP (0x01)
#define RMI_SLEEP_MODE_RESERVED0 (0x02)
#define RMI_SLEEP_MODE_RESERVED1 (0x03)

#define RMI_IS_VALID_SLEEPMODE(mode) \
	(mode >= RMI_SLEEP_MODE_NORMAL && mode <= RMI_SLEEP_MODE_RESERVED1)

union f01_device_commands {
	struct {
		u8 reset:1;
		u8 reserved:1;
	};
	u8 reg;
};

struct f01_device_control_0 {
	union {
		struct {
			u8 sleep_mode:2;
			u8 nosleep:1;
			u8 reserved:2;
			u8 charger_input:1;
			u8 report_rate:1;
			u8 configured:1;
		};
		u8 reg;
	};
};

struct f01_device_control {
	struct f01_device_control_0 ctrl0;
	u8 *interrupt_enable;
	u8 doze_interval;
	u8 wakeup_threshold;
	u8 doze_holdoff;
};

union f01_basic_queries {
	struct {
		u8 manufacturer_id:8;

		u8 custom_map:1;
		u8 non_compliant:1;
		u8 has_lts:1;
		u8 has_sensor_id:1;
		u8 has_charger_input:1;
		u8 has_adjustable_doze:1;
		u8 has_adjustable_doze_holdoff:1;
		u8 has_product_properties_2:1;

		u8 productinfo_1:7;
		u8 q2_bit_7:1;
		u8 productinfo_2:7;
		u8 q3_bit_7:1;

		u8 year:5;
		u8 month:4;
		u8 day:5;
		u8 cp1:1;
		u8 cp2:1;
		u8 wafer_id1_lsb:8;
		u8 wafer_id1_msb:8;
		u8 wafer_id2_lsb:8;
		u8 wafer_id2_msb:8;
		u8 wafer_id3_lsb:8;
	};
	u8 regs[11];
};

union f01_query_42 {
	struct {
		u8 has_ds4_queries:1;
		u8 has_multi_phy:1;
		u8 has_guest:1;
		u8 reserved:5;
	};
	u8 regs[1];
};

union f01_ds4_queries {
	struct {
		u8 length:4;
		u8 reserved_1:4;

		u8 has_package_id_query:1;
		u8 has_packrat_query:1;
		u8 has_reset_query:1;
		u8 has_maskrev_query:1;
		u8 reserved_2:4;

		u8 has_i2c_control:1;
		u8 has_spi_control:1;
		u8 has_attn_control:1;
		u8 reserved_3:5;

		u8 reset_enabled:1;
		u8 reset_polarity:1;
		u8 pullup_enabled:1;
		u8 reserved_4:1;
		u8 reset_pin_number:4;
	};
	u8 regs[4];
};

struct f01_data {
	struct f01_device_control device_control;
	union f01_basic_queries basic_queries;
	union f01_device_status device_status;
	u8 product_id[RMI_PRODUCT_ID_LENGTH+1];

	u8 interrupt_enable_addr;
	u8 doze_interval_addr;
	u8 wakeup_threshold_addr;
	u8 doze_holdoff_addr;

	int irq_count;
	int num_of_irq_regs;

#ifdef	CONFIG_PM
	bool suspended;
	bool old_nosleep;
#endif
};


static ssize_t rmi_fn_01_productinfo_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf);

static ssize_t rmi_fn_01_productid_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);

static ssize_t rmi_fn_01_manufacturer_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf);

static ssize_t rmi_fn_01_datecode_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);

static ssize_t rmi_fn_01_reportrate_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);

static ssize_t rmi_fn_01_reportrate_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);

static ssize_t rmi_fn_01_interrupt_enable_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);

static ssize_t rmi_fn_01_interrupt_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);

static ssize_t rmi_fn_01_doze_interval_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);

static ssize_t rmi_fn_01_doze_interval_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);

static ssize_t rmi_fn_01_wakeup_threshold_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);

static ssize_t rmi_fn_01_wakeup_threshold_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);

static ssize_t rmi_fn_01_doze_holdoff_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);

static ssize_t rmi_fn_01_doze_holdoff_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count);

static ssize_t rmi_fn_01_reset_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count);

static ssize_t rmi_fn_01_sleepmode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);

static ssize_t rmi_fn_01_sleepmode_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count);

static ssize_t rmi_fn_01_nosleep_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

static ssize_t rmi_fn_01_nosleep_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count);

static ssize_t rmi_fn_01_chargerinput_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

static ssize_t rmi_fn_01_chargerinput_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count);

static ssize_t rmi_fn_01_configured_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

static ssize_t rmi_fn_01_unconfigured_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

static ssize_t rmi_fn_01_flashprog_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

static ssize_t rmi_fn_01_statuscode_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

static int rmi_f01_alloc_memory(struct rmi_function_container *fc,
					int num_of_irq_regs);

static void rmi_f01_free_memory(struct rmi_function_container *fc);

static int rmi_f01_initialize(struct rmi_function_container *fc);

static int rmi_f01_create_sysfs(struct rmi_function_container *fc);

static int rmi_f01_config(struct rmi_function_container *fc);

static int rmi_f01_reset(struct rmi_function_container *fc);


static struct device_attribute fn_01_attrs[] = {
	__ATTR(productinfo, RMI_RO_ATTR,
	       rmi_fn_01_productinfo_show, rmi_store_error),
	__ATTR(productid, RMI_RO_ATTR,
	       rmi_fn_01_productid_show, rmi_store_error),
	__ATTR(manufacturer, RMI_RO_ATTR,
	       rmi_fn_01_manufacturer_show, rmi_store_error),
	__ATTR(datecode, RMI_RO_ATTR,
	       rmi_fn_01_datecode_show, rmi_store_error),

	/* control register access */
	__ATTR(sleepmode, RMI_RW_ATTR,
	       rmi_fn_01_sleepmode_show, rmi_fn_01_sleepmode_store),
	__ATTR(nosleep, RMI_RW_ATTR,
	       rmi_fn_01_nosleep_show, rmi_fn_01_nosleep_store),
	__ATTR(chargerinput, RMI_RW_ATTR,
	       rmi_fn_01_chargerinput_show, rmi_fn_01_chargerinput_store),
	__ATTR(reportrate, RMI_RW_ATTR,
	       rmi_fn_01_reportrate_show, rmi_fn_01_reportrate_store),
	__ATTR(interrupt_enable, RMI_RW_ATTR,
	       rmi_fn_01_interrupt_enable_show,
		rmi_fn_01_interrupt_enable_store),
	__ATTR(doze_interval, RMI_RW_ATTR,
	       rmi_fn_01_doze_interval_show,
		rmi_fn_01_doze_interval_store),
	__ATTR(wakeup_threshold, RMI_RW_ATTR,
	       rmi_fn_01_wakeup_threshold_show,
		rmi_fn_01_wakeup_threshold_store),
	__ATTR(doze_holdoff, RMI_RW_ATTR,
	       rmi_fn_01_doze_holdoff_show,
		rmi_fn_01_doze_holdoff_store),

	/* We make report rate RO, since the driver uses that to look for
	 * resets.  We don't want someone faking us out by changing that
	 * bit.
	 */
	__ATTR(configured, RMI_RO_ATTR,
	       rmi_fn_01_configured_show, rmi_store_error),

	/* Command register access. */
	__ATTR(reset, RMI_WO_ATTR,
	       rmi_show_error, rmi_fn_01_reset_store),

	/* STatus register access. */
	__ATTR(unconfigured, RMI_RO_ATTR,
	       rmi_fn_01_unconfigured_show, rmi_store_error),
	__ATTR(flashprog, RMI_RO_ATTR,
	       rmi_fn_01_flashprog_show, rmi_store_error),
	__ATTR(statuscode, RMI_RO_ATTR,
	       rmi_fn_01_statuscode_show, rmi_store_error),
};

/* Utility routine to set the value of a bit field in a register. */
int rmi_set_bit_field(struct rmi_device *rmi_dev,
		      unsigned short address,
		      unsigned char field_mask,
		      unsigned char bits)
{
	unsigned char reg_contents;
	int retval;

	retval = rmi_read(rmi_dev, address, &reg_contents);
	if (retval)
		return retval;
	reg_contents = (reg_contents & ~field_mask) | bits;
	retval = rmi_write(rmi_dev, address, reg_contents);
	if (retval == 1)
		return 0;
	else if (retval == 0)
		return -EIO;
	return retval;
}

static ssize_t rmi_fn_01_productinfo_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "0x%02x 0x%02x\n",
			data->basic_queries.productinfo_1,
			data->basic_queries.productinfo_2);
}

static ssize_t rmi_fn_01_productid_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%s\n", data->product_id);
}

static ssize_t rmi_fn_01_manufacturer_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "0x%02x\n",
			data->basic_queries.manufacturer_id);
}

static ssize_t rmi_fn_01_datecode_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "20%02u-%02u-%02u\n",
			data->basic_queries.year,
			data->basic_queries.month,
			data->basic_queries.day);
}

static ssize_t rmi_fn_01_reset_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct rmi_function_container *fc = NULL;
	unsigned int reset;
	int retval = 0;
	/* Command register always reads as 0, so we can just use a local. */
	union f01_device_commands commands = {};

	fc = to_rmi_function_container(dev);

	if (sscanf(buf, "%u", &reset) != 1)
		return -EINVAL;
	if (reset < 0 || reset > 1)
		return -EINVAL;

	/* Per spec, 0 has no effect, so we skip it entirely. */
	if (reset) {
		commands.reset = 1;
		retval = rmi_write_block(fc->rmi_dev, fc->fd.command_base_addr,
				&commands.reg, sizeof(commands.reg));
		if (retval < 0) {
			dev_err(dev, "%s: failed to issue reset command, "
				"error = %d.", __func__, retval);
			return retval;
		}
	}

	return count;
}

static ssize_t rmi_fn_01_sleepmode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE,
			"%d\n", data->device_control.ctrl0.sleep_mode);
}

static ssize_t rmi_fn_01_sleepmode_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t count)
{
	struct f01_data *data = NULL;
	unsigned long new_value;
	int retval;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	retval = strict_strtoul(buf, 10, &new_value);
	if (retval < 0 || !RMI_IS_VALID_SLEEPMODE(new_value)) {
		dev_err(dev, "%s: Invalid sleep mode %s.", __func__, buf);
		return -EINVAL;
	}

	dev_dbg(dev, "Setting sleep mode to %ld.", new_value);
	data->device_control.ctrl0.sleep_mode = new_value;
	retval = rmi_write_block(fc->rmi_dev, fc->fd.control_base_addr,
			&data->device_control.ctrl0.reg,
			sizeof(data->device_control.ctrl0.reg));
	if (retval >= 0)
		retval = count;
	else
		dev_err(dev, "Failed to write sleep mode, code %d.\n", retval);
	return retval;
}

static ssize_t rmi_fn_01_nosleep_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
		data->device_control.ctrl0.nosleep);
}

static ssize_t rmi_fn_01_nosleep_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	struct f01_data *data = NULL;
	unsigned long new_value;
	int retval;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	retval = strict_strtoul(buf, 10, &new_value);
	if (retval < 0 || new_value < 0 || new_value > 1) {
		dev_err(dev, "%s: Invalid nosleep bit %s.", __func__, buf);
		return -EINVAL;
	}

	data->device_control.ctrl0.nosleep = new_value;
	retval = rmi_write_block(fc->rmi_dev, fc->fd.control_base_addr,
			&data->device_control.ctrl0.reg,
			sizeof(data->device_control.ctrl0.reg));
	if (retval >= 0)
		retval = count;
	else
		dev_err(dev, "Failed to write nosleep bit.\n");
	return retval;
}

static ssize_t rmi_fn_01_chargerinput_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_control.ctrl0.charger_input);
}

static ssize_t rmi_fn_01_chargerinput_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	struct f01_data *data = NULL;
	unsigned long new_value;
	int retval;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	retval = strict_strtoul(buf, 10, &new_value);
	if (retval < 0 || new_value < 0 || new_value > 1) {
		dev_err(dev, "%s: Invalid chargerinput bit %s.", __func__, buf);
		return -EINVAL;
	}

	data->device_control.ctrl0.charger_input = new_value;
	retval = rmi_write_block(fc->rmi_dev, fc->fd.control_base_addr,
			&data->device_control.ctrl0.reg,
			sizeof(data->device_control.ctrl0.reg));
	if (retval >= 0)
		retval = count;
	else
		dev_err(dev, "Failed to write chargerinput bit.\n");
	return retval;
}

static ssize_t rmi_fn_01_reportrate_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_control.ctrl0.report_rate);
}

static ssize_t rmi_fn_01_reportrate_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	struct f01_data *data = NULL;
	unsigned long new_value;
	int retval;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	retval = strict_strtoul(buf, 10, &new_value);
	if (retval < 0 || new_value < 0 || new_value > 1) {
		dev_err(dev, "%s: Invalid reportrate bit %s.", __func__, buf);
		return -EINVAL;
	}

	data->device_control.ctrl0.report_rate = new_value;
	retval = rmi_write_block(fc->rmi_dev, fc->fd.control_base_addr,
			&data->device_control.ctrl0.reg,
			sizeof(data->device_control.ctrl0.reg));
	if (retval >= 0)
		retval = count;
	else
		dev_err(dev, "Failed to write reportrate bit.\n");
	return retval;
}

static ssize_t rmi_fn_01_interrupt_enable_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f01_data *data;
	int i, len, total_len = 0;
	char *current_buf = buf;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	/* loop through each irq value and copy its
	 * string representation into buf */
	for (i = 0; i < data->irq_count; i++) {
		int irq_reg;
		int irq_shift;
		int interrupt_enable;

		irq_reg = i / 8;
		irq_shift = i % 8;
		interrupt_enable =
		    ((data->device_control.interrupt_enable[irq_reg]
			>> irq_shift) & 0x01);

		/* get next irq value and write it to buf */
		len = snprintf(current_buf, PAGE_SIZE - total_len,
			"%u ", interrupt_enable);
		/* bump up ptr to next location in buf if the
		 * snprintf was valid.  Otherwise issue an error
		 * and return. */
		if (len > 0) {
			current_buf += len;
			total_len += len;
		} else {
			dev_err(dev, "%s: Failed to build interrupt_enable"
				" buffer, code = %d.\n", __func__, len);
			return snprintf(buf, PAGE_SIZE, "unknown\n");
		}
	}
	len = snprintf(current_buf, PAGE_SIZE - total_len, "\n");
	if (len > 0)
		total_len += len;
	else
		dev_warn(dev, "%s: Failed to append carriage return.\n",
			 __func__);
	return total_len;

}

static ssize_t rmi_fn_01_interrupt_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f01_data *data;
	int i;
	int irq_count = 0;
	int retval = count;
	int irq_reg = 0;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	for (i = 0; i < data->irq_count && *buf != 0;
	     i++, buf += 2) {
		int irq_shift;
		int interrupt_enable;
		int result;

		irq_reg = i / 8;
		irq_shift = i % 8;

		/* get next interrupt mapping value and store and bump up to
		 * point to next item in buf */
		result = sscanf(buf, "%u", &interrupt_enable);
		if ((result != 1) ||
			(interrupt_enable != 0 && interrupt_enable != 1)) {
			dev_err(dev,
				"%s: Error - interrupt enable[%d]"
				" is not a valid value 0x%x.\n",
				__func__, i, interrupt_enable);
			return -EINVAL;
		}
		if (interrupt_enable == 0) {
			data->device_control.interrupt_enable[irq_reg] &=
				(1 << irq_shift) ^ 0xFF;
		} else
			data->device_control.interrupt_enable[irq_reg] |=
				(1 << irq_shift);
		irq_count++;
	}

	/* Make sure the irq count matches */
	if (irq_count != data->irq_count) {
		dev_err(dev,
			"%s: Error - interrupt enable count of %d"
			" doesn't match device count of %d.\n",
			 __func__, irq_count, data->irq_count);
		return -EINVAL;
	}

	/* write back to the control register */
	retval = rmi_write_block(fc->rmi_dev, data->interrupt_enable_addr,
			data->device_control.interrupt_enable,
			sizeof(u8)*(data->num_of_irq_regs));
	if (retval < 0) {
		dev_err(dev, "%s : Could not write interrupt_enable_store"
			" to 0x%x\n", __func__, data->interrupt_enable_addr);
		return retval;
	}

	return count;

}

static ssize_t rmi_fn_01_doze_interval_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_control.doze_interval);

}

static ssize_t rmi_fn_01_doze_interval_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct f01_data *data = NULL;
	unsigned long new_value;
	int retval;
	int ctrl_base_addr;

	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	retval = strict_strtoul(buf, 10, &new_value);
	if (retval < 0 || new_value < 0 || new_value > 255) {
		dev_err(dev, "%s: Invalid doze interval %s.", __func__, buf);
		return -EINVAL;
	}

	data->device_control.doze_interval = new_value;
	ctrl_base_addr = fc->fd.control_base_addr + sizeof(u8) +
			(sizeof(u8)*(data->num_of_irq_regs));
	dev_info(dev, "doze_interval store address %x, value %d",
		ctrl_base_addr, data->device_control.doze_interval);

	retval = rmi_write_block(fc->rmi_dev, data->doze_interval_addr,
			&data->device_control.doze_interval,
			sizeof(u8));
	if (retval >= 0)
		retval = count;
	else
		dev_err(dev, "Failed to write doze interval.\n");
	return retval;

}

static ssize_t rmi_fn_01_wakeup_threshold_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_control.wakeup_threshold);
}

static ssize_t rmi_fn_01_wakeup_threshold_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct f01_data *data = NULL;
	unsigned long new_value;
	int retval;

	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	retval = strict_strtoul(buf, 10, &new_value);
	if (retval < 0 || new_value < 0 || new_value > 255) {
		dev_err(dev, "%s: Invalid wakeup threshold %s.", __func__, buf);
		return -EINVAL;
	}

	data->device_control.doze_interval = new_value;
	retval = rmi_write_block(fc->rmi_dev, data->wakeup_threshold_addr,
			&data->device_control.wakeup_threshold,
			sizeof(u8));
	if (retval >= 0)
		retval = count;
	else
		dev_err(dev, "Failed to write wakeup threshold.\n");
	return retval;

}

static ssize_t rmi_fn_01_doze_holdoff_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_control.doze_holdoff);

}


static ssize_t rmi_fn_01_doze_holdoff_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct f01_data *data = NULL;
	unsigned long new_value;
	int retval;

	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	retval = strict_strtoul(buf, 10, &new_value);
	if (retval < 0 || new_value < 0 || new_value > 255) {
		dev_err(dev, "%s: Invalid doze holdoff %s.", __func__, buf);
		return -EINVAL;
	}

	data->device_control.doze_interval = new_value;
	retval = rmi_write_block(fc->rmi_dev, data->doze_holdoff_addr,
			&data->device_control.doze_holdoff,
			sizeof(u8));
	if (retval >= 0)
		retval = count;
	else
		dev_err(dev, "Failed to write doze holdoff.\n");
	return retval;

}

static ssize_t rmi_fn_01_configured_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_control.ctrl0.configured);
}

static ssize_t rmi_fn_01_unconfigured_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_status.unconfigured);
}

static ssize_t rmi_fn_01_flashprog_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->device_status.flash_prog);
}

static ssize_t rmi_fn_01_statuscode_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct f01_data *data = NULL;
	struct rmi_function_container *fc = to_rmi_function_container(dev);

	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "0x%02x\n",
			data->device_status.status_code);
}

/* why is this not done in init? */
int rmi_driver_f01_init(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *driver_data = rmi_get_driverdata(rmi_dev);
	struct rmi_function_container *fc = driver_data->f01_container;
	int error;

	error = rmi_f01_alloc_memory(fc, driver_data->num_of_irq_regs);
	if (error < 0)
		goto error_exit;

	error = rmi_f01_initialize(fc);
	if (error < 0)
		goto error_exit;

	error = rmi_f01_create_sysfs(fc);
	if (error < 0)
		goto error_exit;

	return 0;

 error_exit:
	rmi_f01_free_memory(fc);

	return error;
}

static int rmi_f01_alloc_memory(struct rmi_function_container *fc,
	int num_of_irq_regs)
{
	struct f01_data *f01;

	f01 = kzalloc(sizeof(struct f01_data), GFP_KERNEL);
	if (!f01) {
		dev_err(&fc->dev, "Failed to allocate fn_01_data.\n");
		return -ENOMEM;
	}

	f01->device_control.interrupt_enable =
		kzalloc(sizeof(u8)*(num_of_irq_regs), GFP_KERNEL);
	if (!f01->device_control.interrupt_enable) {
		kfree(f01);
		return -ENOMEM;
	}
	fc->data = f01;

	return 0;
}

static void rmi_f01_free_memory(struct rmi_function_container *fc)
{
	struct f01_data *f01 = fc->data;
	kfree(f01->device_control.interrupt_enable);
	kfree(fc->data);
	fc->data = NULL;
}

static int rmi_f01_initialize(struct rmi_function_container *fc)
{
	u8 temp;
	int retval;
	int ctrl_base_addr;
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_driver_data *driver_data = rmi_get_driverdata(rmi_dev);
	struct f01_data *data = fc->data;
	struct rmi_device_platform_data *pdata = to_rmi_platform_data(rmi_dev);

	/* Set the configured bit and (optionally) other important stuff
	 * in the device control register. */
	ctrl_base_addr = fc->fd.control_base_addr;
	retval = rmi_read_block(rmi_dev, fc->fd.control_base_addr,
			&data->device_control.ctrl0.reg,
			sizeof(data->device_control.ctrl0.reg));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read F01 control.\n");
		return retval;
	}
	switch (pdata->power_management.nosleep) {
	case RMI_F01_NOSLEEP_DEFAULT:
		break;
	case RMI_F01_NOSLEEP_OFF:
		data->device_control.ctrl0.nosleep = 0;
		break;
	case RMI_F01_NOSLEEP_ON:
		data->device_control.ctrl0.nosleep = 1;
		break;
	}
	/* Sleep mode might be set as a hangover from a system crash or
	 * reboot without power cycle.  If so, clear it so the sensor
	 * is certain to function.
	 */
	if (data->device_control.ctrl0.sleep_mode != RMI_SLEEP_MODE_NORMAL) {
		dev_warn(&fc->dev,
			 "WARNING: Non-zero sleep mode found. Clearing...\n");
		data->device_control.ctrl0.sleep_mode = RMI_SLEEP_MODE_NORMAL;
	}

	data->device_control.ctrl0.configured = 1;
	retval = rmi_write_block(rmi_dev, fc->fd.control_base_addr,
			&data->device_control.ctrl0.reg,
			sizeof(data->device_control.ctrl0.reg));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to write F01 control.\n");
		return retval;
	}

	data->irq_count = driver_data->irq_count;
	data->num_of_irq_regs = driver_data->num_of_irq_regs;
	ctrl_base_addr += sizeof(struct f01_device_control_0);

	data->interrupt_enable_addr = ctrl_base_addr;
	retval = rmi_read_block(rmi_dev, ctrl_base_addr,
			data->device_control.interrupt_enable,
			sizeof(u8) * (driver_data->num_of_irq_regs));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read F01 control interrupt enable register.\n");
		goto error_exit;
	}
	ctrl_base_addr += (sizeof(u8) * (driver_data->num_of_irq_regs));

	/* dummy read in order to clear irqs */
	retval = rmi_read(rmi_dev, fc->fd.data_base_addr + 1, &temp);
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read Interrupt Status.\n");
		return retval;
	}

	retval = rmi_read_block(rmi_dev, fc->fd.query_base_addr,
				data->basic_queries.regs,
				sizeof(data->basic_queries.regs));

	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read device query registers.\n");
		return retval;
	}

	retval = rmi_read_block(rmi_dev,
		fc->fd.query_base_addr + sizeof(data->basic_queries.regs),
		data->product_id, RMI_PRODUCT_ID_LENGTH);
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read product ID.\n");
		return retval;
	}
	data->product_id[RMI_PRODUCT_ID_LENGTH] = '\0';
	dev_info(&fc->dev, "found RMI device, manufacturer: %s, product: %s\n",
		 data->basic_queries.manufacturer_id == 1 ?
							"synaptics" : "unknown",
		 data->product_id);

	/* read control register */
	if (data->basic_queries.has_adjustable_doze) {
		data->doze_interval_addr = ctrl_base_addr;
		ctrl_base_addr++;

		if (pdata->power_management.doze_interval) {
			data->device_control.doze_interval =
				pdata->power_management.doze_interval;
			retval = rmi_write(rmi_dev, data->doze_interval_addr,
					data->device_control.doze_interval);
			if (retval < 0) {
				dev_err(&fc->dev, "Failed to configure F01 doze interval register.\n");
				goto error_exit;
			}
		} else {
			retval = rmi_read(rmi_dev, data->doze_interval_addr,
					&data->device_control.doze_interval);
			if (retval < 0) {
				dev_err(&fc->dev, "Failed to read F01 doze interval register.\n");
				goto error_exit;
			}
		}

		data->wakeup_threshold_addr = ctrl_base_addr;
		ctrl_base_addr++;

		if (pdata->power_management.wakeup_threshold) {
			data->device_control.wakeup_threshold =
				pdata->power_management.wakeup_threshold;
			retval = rmi_write(rmi_dev, data->wakeup_threshold_addr,
					data->device_control.wakeup_threshold);
			if (retval < 0) {
				dev_err(&fc->dev, "Failed to configure F01 wakeup threshold register.\n");
				goto error_exit;
			}
		} else {
			retval = rmi_read(rmi_dev, data->wakeup_threshold_addr,
					&data->device_control.wakeup_threshold);
			if (retval < 0) {
				dev_err(&fc->dev, "Failed to read F01 wakeup threshold register.\n");
				goto error_exit;
			}
		}
	}

	if (data->basic_queries.has_adjustable_doze_holdoff) {
		data->doze_holdoff_addr = ctrl_base_addr;
		ctrl_base_addr++;

		if (pdata->power_management.doze_holdoff) {
			data->device_control.doze_holdoff =
				pdata->power_management.doze_holdoff;
			retval = rmi_write(rmi_dev, data->doze_holdoff_addr,
					data->device_control.doze_holdoff);
			if (retval < 0) {
				dev_err(&fc->dev, "Failed to configure F01 "
					"doze holdoff register.\n");
				goto error_exit;
			}
		} else {
			retval = rmi_read(rmi_dev, data->doze_holdoff_addr,
					&data->device_control.doze_holdoff);
			if (retval < 0) {
				dev_err(&fc->dev, "Failed to read F01 doze"
					" holdoff register.\n");
				goto error_exit;
			}
		}
	}

	retval = rmi_read_block(rmi_dev, fc->fd.data_base_addr,
			&data->device_status.reg,
			sizeof(data->device_status.reg));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read device status.\n");
		goto error_exit;
	}

	if (data->device_status.unconfigured) {
		dev_err(&fc->dev,
			"Device reset during configuration process, status: "
			"%#02x!\n", data->device_status.status_code);
		retval = -EINVAL;
		goto error_exit;
	}

	return retval;

 error_exit:
	kfree(data);
	return retval;
}

static int rmi_f01_create_sysfs(struct rmi_function_container *fc)
{
	int attr_count = 0;
	int retval = 0;
	struct f01_data *data = fc->data;

	dev_dbg(&fc->dev, "Creating sysfs files.");
	for (attr_count = 0; attr_count < ARRAY_SIZE(fn_01_attrs);
			attr_count++) {
		if (!strcmp("doze_interval", fn_01_attrs[attr_count].attr.name)
			&& !data->basic_queries.has_lts) {
			continue;
		}
		if (!strcmp("wakeup_threshold",
			fn_01_attrs[attr_count].attr.name)
			&& !data->basic_queries.has_adjustable_doze) {
			continue;
		}
		if (!strcmp("doze_holdoff", fn_01_attrs[attr_count].attr.name)
			&& !data->basic_queries.has_adjustable_doze_holdoff) {
			continue;
		}
		retval = sysfs_create_file(&fc->dev.kobj,
				      &fn_01_attrs[attr_count].attr);
		if (retval < 0) {
			dev_err(&fc->dev, "Failed to create sysfs file for %s.",
			       fn_01_attrs[attr_count].attr.name);
			goto err_remove_sysfs;
		}
	}

	return 0;

err_remove_sysfs:
	for (attr_count--; attr_count >= 0; attr_count--)
		sysfs_remove_file(&fc->dev.kobj,
				  &fn_01_attrs[attr_count].attr);

	return retval;
}

static int rmi_f01_config(struct rmi_function_container *fc)
{
	struct f01_data *data = fc->data;
	int retval;

	retval = rmi_write_block(fc->rmi_dev, fc->fd.control_base_addr,
			(u8 *)&data->device_control.ctrl0,
			sizeof(struct f01_device_control_0));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to write device_control.reg.\n");
		return retval;
	}

	retval = rmi_write_block(fc->rmi_dev, data->interrupt_enable_addr,
			data->device_control.interrupt_enable,
			sizeof(u8)*(data->num_of_irq_regs));

	if (retval < 0) {
		dev_err(&fc->dev, "Failed to write interrupt enable.\n");
		return retval;
	}
	if (data->basic_queries.has_lts) {
		retval = rmi_write_block(fc->rmi_dev, data->doze_interval_addr,
				&data->device_control.doze_interval,
				sizeof(u8));
		if (retval < 0) {
			dev_err(&fc->dev, "Failed to write doze interval.\n");
			return retval;
		}
	}

	if (data->basic_queries.has_adjustable_doze) {
		retval = rmi_write_block(
				fc->rmi_dev, data->wakeup_threshold_addr,
				&data->device_control.wakeup_threshold,
				sizeof(u8));
		if (retval < 0) {
			dev_err(&fc->dev, "Failed to write wakeup threshold.\n");
			return retval;
		}
	}

	if (data->basic_queries.has_adjustable_doze_holdoff) {
		retval = rmi_write_block(fc->rmi_dev, data->doze_holdoff_addr,
				&data->device_control.doze_holdoff,
				sizeof(u8));
		if (retval < 0) {
			dev_err(&fc->dev, "Failed to write doze holdoff.\n");
			return retval;
		}
	}
	return 0;
}

static int rmi_f01_reset(struct rmi_function_container *fc)
{
	/*do nothing here */
	return 0;
}


#ifdef CONFIG_PM
static int rmi_f01_suspend(struct rmi_function_container *fc)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_driver_data *driver_data = rmi_get_driverdata(rmi_dev);
	struct f01_data *data = driver_data->f01_container->data;
	int retval = 0;

	dev_dbg(&fc->dev, "Suspending...\n");
	if (data->suspended)
		return 0;

	data->old_nosleep = data->device_control.ctrl0.nosleep;
	data->device_control.ctrl0.nosleep = 0;
	data->device_control.ctrl0.sleep_mode = RMI_SLEEP_MODE_SENSOR_SLEEP;
	retval = rmi_write_block(rmi_dev,
			driver_data->f01_container->fd.control_base_addr,
			(u8 *)&data->device_control.ctrl0,
			sizeof(struct f01_device_control_0));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to write sleep mode. Code: %d.\n",
			retval);
		data->device_control.ctrl0.nosleep = data->old_nosleep;
		data->device_control.ctrl0.sleep_mode = RMI_SLEEP_MODE_NORMAL;
	} else {
		data->suspended = true;
		retval = 0;
	}

	return retval;
}

static int rmi_f01_resume(struct rmi_function_container *fc)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_driver_data *driver_data = rmi_get_driverdata(rmi_dev);
	struct f01_data *data = driver_data->f01_container->data;
	int retval = 0;

	dev_dbg(&fc->dev, "Resuming...\n");
	if (!data->suspended)
		return 0;

	data->device_control.ctrl0.nosleep = data->old_nosleep;
	data->device_control.ctrl0.sleep_mode = RMI_SLEEP_MODE_NORMAL;
	retval = rmi_write_block(rmi_dev,
			driver_data->f01_container->fd.control_base_addr,
			(u8 *)&data->device_control.ctrl0,
			sizeof(struct f01_device_control_0));
	if (retval < 0)
		dev_err(&fc->dev,
			"Failed to restore normal operation. Code: %d.\n",
			retval);
	else {
		data->suspended = false;
		retval = 0;
	}

	return retval;
}
#endif /* CONFIG_PM */

static int rmi_f01_init(struct rmi_function_container *fc)
{
	return 0;
}

static void rmi_f01_remove(struct rmi_function_container *fc)
{
	int attr_count;

	for (attr_count = 0; attr_count < ARRAY_SIZE(fn_01_attrs);
			attr_count++) {
		sysfs_remove_file(&fc->dev.kobj, &fn_01_attrs[attr_count].attr);
	}

	rmi_f01_free_memory(fc);
}

static int rmi_f01_attention(struct rmi_function_container *fc, u8 *irq_bits)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct f01_data *data = fc->data;
	int retval;

	retval = rmi_read_block(rmi_dev, fc->fd.data_base_addr,
		&data->device_status.reg,
	sizeof(data->device_status.reg));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read device status, code: %d.\n",
			retval);
		return retval;
	}
	if (data->device_status.unconfigured) {
		dev_warn(&fc->dev, "Device reset detected.\n");
		retval = rmi_dev->driver->reset_handler(rmi_dev);
		if (retval < 0)
			return retval;
	}
	return 0;
}

static struct rmi_function_handler function_handler = {
	.func = 0x01,
	.init = rmi_f01_init,
	.config = rmi_f01_config,
	.reset = rmi_f01_reset,
	.attention = rmi_f01_attention,
#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
	.early_suspend = rmi_f01_suspend,
	.late_resume = rmi_f01_resume,
#endif  /* CONFIG_HAS_EARLYSUSPEND */
	.suspend = rmi_f01_suspend,
	.resume = rmi_f01_resume,
#endif  /* CONFIG_PM */
	.remove = rmi_f01_remove,
};

static int __init rmi_f01_module_init(void)
{
	int error;

	error = rmi_register_function_driver(&function_handler);
	if (error < 0) {
		pr_err("%s: register failed!\n", __func__);
		return error;
	}

	return 0;
}

static void __exit rmi_f01_module_exit(void)
{
	rmi_unregister_function_driver(&function_handler);
}

module_init(rmi_f01_module_init);
module_exit(rmi_f01_module_exit);

MODULE_AUTHOR("Christopher Heiny <cheiny@synaptics.com>");
MODULE_DESCRIPTION("RMI F01 module");
MODULE_LICENSE("GPL");
MODULE_VERSION(RMI_DRIVER_VERSION);
