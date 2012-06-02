/*
 * Copyright (c) 2012 Synaptics Incorporated
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
#include <linux/input.h>
#include <linux/slab.h>
#include "rmi_driver.h"
#define QUERY_BASE_INDEX 1
#define MAX_NAME_LENGTH 256

union f17_device_query {
	struct {
		u8 number_of_sticks:3;
	};
	u8 regs[1];
};

#define F17_MANUFACTURER_SYNAPTICS 0
#define F17_MANUFACTURER_NMB 1
#define F17_MANUFACTURER_ALPS 2

struct f17_stick_query {
	union {
		struct {
			u8 manufacturer:4;
			u8 resistive:1;
			u8 ballistics:1;
			u8 reserved1:2;
			u8 has_relative:1;
			u8 has_absolute:1;
			u8 has_gestures:1;
			u8 has_dribble:1;
			u8 reserved2:4;
		};
		u8 regs[2];
	} general;

	union {
		struct {
			u8 has_single_tap:1;
			u8 has_tap_and_hold:1;
			u8 has_double_tap:1;
			u8 has_early_tap:1;
			u8 has_press:1;
		};
		u8 regs[1];
	} gestures;
};

union f17_device_controls {
	struct {
		u8 reporting_mode:3;
		u8 dribble:1;
	};
	u8 regs[1];
};

struct f17_stick_controls {
	union {
		struct {
			u8 z_force_threshold;
			u8 radial_force_threshold;
		};
		u8 regs[3];
	} general;

	union {
		struct {
			u8 motion_sensitivity:4;
			u8 antijitter:1;
		};
		u8 regs[1];
	} relative;

	union {
		struct {
			u8 single_tap:1;
			u8 tap_and_hold:1;

			u8 double_tap:1;
			u8 early_tap:1;
			u8 press:1;
		};
		u8 regs[1];
	} enable;

	u8 maximum_tap_time;
	u8 minimum_press_time;
	u8 maximum_radial_force;
};


union f17_device_commands {
	struct {
		u8 rezero:1;
	};
	u8 regs[1];
};

struct f17_stick_data {
	union {
		struct {
			u8 x_force_high;
			u8 y_force_high;
			u8 y_force_low:4;
			u8 x_force_low:4;
			u8 z_force;
		};
		u8 regs[4];
	} abs;
	union {
		struct {
			s8 x_delta;
			s8 y_delta;
		};
		u8 regs[2];
	} rel;
	union {
		struct {
			u8 single_tap:1;
			u8 tap_and_hold:1;
			u8 double_tap:1;
			u8 early_tap:1;
			u8 press:1;
		};
		u8 regs[1];
	} gestures;
};


/* data specific to f17 that needs to be kept around */

struct rmi_f17_stick_data {
	struct f17_stick_query query;
	struct f17_stick_controls controls;
	struct f17_stick_data data;

	u16 abs_data_address;
	u16 rel_data_address;
	u16 gesture_data_address;
	u16 control_address;

	int index;

	char input_name[MAX_NAME_LENGTH];
	char input_phys[MAX_NAME_LENGTH];
	struct input_dev *input;
	char mouse_name[MAX_NAME_LENGTH];
	char mouse_phys[MAX_NAME_LENGTH];
	struct input_dev *mouse;
};

struct rmi_f17_device_data {
	u16 control_address;

	union f17_device_query query;
	union f17_device_commands commands;
	union f17_device_controls controls;

	struct rmi_f17_stick_data *sticks;

};

static ssize_t f17_rezero_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t f17_rezero_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count);



static int f17_alloc_memory(struct rmi_function_container *fc);

static void f17_free_memory(struct rmi_function_container *fc);

static int f17_initialize(struct rmi_function_container *fc);

static int f17_register_devices(struct rmi_function_container *fc);

static int f17_create_sysfs(struct rmi_function_container *fc);

static int f17_config(struct rmi_function_container *fc);

static int f17_reset(struct rmi_function_container *fc);

static struct device_attribute attrs[] = {
	__ATTR(rezero, RMI_RW_ATTR,
		f17_rezero_show, f17_rezero_store),
};


int f17_read_control_parameters(struct rmi_device *rmi_dev,
	struct rmi_f17_device_data *f17)
{
	int retval = 0;

	// TODO: read this or delete the function

	return retval;
}


static int f17_init(struct rmi_function_container *fc)
{
	int retval;

	retval = f17_alloc_memory(fc);
	if (retval < 0)
		goto err_free_data;

	retval = f17_initialize(fc);
	if (retval < 0)
		goto err_free_data;

	retval = f17_register_devices(fc);
	if (retval < 0)
		goto err_free_data;

	retval = f17_create_sysfs(fc);
	if (retval < 0)
		goto err_free_data;

	return 0;

err_free_data:
	f17_free_memory(fc);

	return retval;
}

static int f17_alloc_memory(struct rmi_function_container *fc)
{
	struct rmi_f17_device_data *f17;
	int retval;
	int i;

	f17 = kzalloc(sizeof(struct rmi_f17_device_data), GFP_KERNEL);
	if (!f17) {
		dev_err(&fc->dev, "Failed to allocate function data.\n");
		return -ENOMEM;
	}
	fc->data = f17;

	retval = rmi_read_block(fc->rmi_dev, fc->fd.query_base_addr,
				f17->query.regs, sizeof(f17->query.regs));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read query register.\n");
		goto error_exit;
	}
	dev_info(&fc->dev, "Found F17 with %d sticks.\n",
		 f17->query.number_of_sticks + 1);

	f17->sticks = kcalloc(f17->query.number_of_sticks + 1,
			sizeof(struct rmi_f17_stick_data), GFP_KERNEL);
	if (!f17->sticks) {
		dev_err(&fc->dev, "Failed to allocate per stick data.\n");
		return -ENOMEM;
	}

	for (i = 0; i < f17->query.number_of_sticks + 1; i++) {
		// TODO: allocate any per stick stuff (or remove this loop)
	}

	return 0;

error_exit:
	kfree(f17);
	fc->data = NULL;
	return retval;
}

static void f17_free_memory(struct rmi_function_container *fc)
{
	struct rmi_f17_device_data *f17 = fc->data;
	int i;

	if (f17) {
		if (f17->sticks) {
			for (i = 0; i < f17->query.number_of_sticks + 1; i++) {
				// TODO: Free stick stuff
			}
		}
		kfree(f17->sticks);
		f17->sticks = NULL;
	}
	kfree(f17);
	fc->data = NULL;
}

static int f17_init_stick(struct rmi_device *rmi_dev,
			  struct rmi_f17_stick_data *stick,
			  u16 *next_query_reg, u16 *next_data_reg,
			  u16 *next_control_reg) {
	int retval = 0;

	retval = rmi_read_block(rmi_dev, *next_query_reg,
		stick->query.general.regs,
		sizeof(stick->query.general.regs));
	if (retval < 0) {
		dev_err(&rmi_dev->dev, "Failed to read stick general query.\n");
		return retval;
	}
	*next_query_reg += sizeof(stick->query.general.regs);

	dev_info(&rmi_dev->dev, "Stick %d found\n", stick->index);
	dev_info(&rmi_dev->dev, "    Manufacturer: %d.\n", stick->query.general.manufacturer);
	dev_info(&rmi_dev->dev, "    Resistive:    %d.\n", stick->query.general.resistive);
	dev_info(&rmi_dev->dev, "    Ballistics:   %d.\n", stick->query.general.ballistics);
	dev_info(&rmi_dev->dev, "    Manufacturer: %d.\n", stick->query.general.ballistics);
	dev_info(&rmi_dev->dev, "    Has relative: %d.\n", stick->query.general.has_relative);
	dev_info(&rmi_dev->dev, "    Has absolute: %d.\n", stick->query.general.has_absolute);
	dev_info(&rmi_dev->dev, "    Had dribble:  %d.\n", stick->query.general.has_dribble);
	dev_info(&rmi_dev->dev, "    Has gestures: %d.\n", stick->query.general.has_gestures);

	if (stick->query.general.has_gestures) {
		retval = rmi_read_block(rmi_dev, *next_query_reg,
			stick->query.gestures.regs,
			sizeof(stick->query.gestures.regs));
		if (retval < 0) {
			dev_err(&rmi_dev->dev, "Failed to read stick gestures query.\n");
			return retval;
		}
		*next_query_reg += sizeof(stick->query.gestures.regs);
		dev_info(&rmi_dev->dev, "        single tap: %d.\n", stick->query.gestures.has_single_tap);
		dev_info(&rmi_dev->dev, "        tap & hold: %d.\n", stick->query.gestures.has_tap_and_hold);
		dev_info(&rmi_dev->dev, "        double tap: %d.\n", stick->query.gestures.has_double_tap);
		dev_info(&rmi_dev->dev, "        early tap:  %d.\n", stick->query.gestures.has_early_tap);
		dev_info(&rmi_dev->dev, "        press:      %d.\n", stick->query.gestures.has_press);
	}
	if (stick->query.general.has_absolute) {
		stick->abs_data_address = *next_data_reg;
		*next_data_reg += sizeof(stick->data.abs.regs);
	}
	if (stick->query.general.has_relative) {
		stick->rel_data_address = *next_data_reg;
		*next_data_reg += sizeof(stick->data.rel.regs);
	}
	if (stick->query.general.has_gestures) {
		stick->gesture_data_address = *next_data_reg;
		*next_data_reg += sizeof(stick->data.gestures.regs);
	}

	return retval;
}

static int f17_initialize(struct rmi_function_container *fc)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_f17_device_data *f17 = fc->data;
	int i;
	int retval;
	u16 next_query_reg = fc->fd.query_base_addr;
	u16 next_data_reg = fc->fd.data_base_addr;
	u16 next_control_reg = fc->fd.control_base_addr;

	dev_info(&fc->dev, "Intializing F17 values.");

	retval = rmi_read_block(fc->rmi_dev, fc->fd.query_base_addr,
				f17->query.regs, sizeof(f17->query.regs));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read query register.\n");
		return retval;
	}
	dev_info(&fc->dev, "Found F17 with %d sticks.\n",
		 f17->query.number_of_sticks + 1);
	next_query_reg += sizeof(f17->query.regs);

	retval = rmi_read_block(rmi_dev, fc->fd.command_base_addr,
		f17->commands.regs, sizeof(f17->commands.regs));
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to read command register.\n");
		return retval;
	}

	f17->control_address = fc->fd.control_base_addr;
	retval = f17_read_control_parameters(rmi_dev, f17);
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to initialize F17 control params.\n");
		return retval;
	}

	for (i = 0; i < f17->query.number_of_sticks + 1; i++) {
		f17->sticks[i].index = i;
		retval = f17_init_stick(rmi_dev, &f17->sticks[i],
					&next_query_reg, &next_data_reg,
					&next_control_reg);
		if (!retval) {
			dev_err(&fc->dev, "Failed to init stick %d.\n", i);
			return retval;
		}
	}

	return retval;
}

static int f17_register_stick(struct rmi_function_container *fc,
			      struct rmi_f17_stick_data *stick) {
	struct rmi_device *rmi_dev = fc->rmi_dev;
	int retval = 0;

	if (stick->query.general.has_absolute) {
		struct input_dev *input_dev;
		input_dev = input_allocate_device();
		if (!input_dev) {
			dev_err(&rmi_dev->dev, "Failed to allocate stick device %d.\n",
				stick->index);
			return -ENOMEM;
		}

		snprintf(stick->input_name, sizeof(stick->input_name),
			"RMI F%02x Stick %d", 0x17, stick->index);
		snprintf(stick->input_phys, sizeof(stick->input_phys),
			 "sensor00fn%02x/stick%d", 0x17, stick->index);
		input_dev->name = stick->input_name;
		input_dev->phys = stick->input_phys;
		input_dev->dev.parent = &fc->dev;
		input_set_drvdata(input_dev, stick);

		retval = input_register_device(input_dev);
		if (retval < 0) {
			dev_err(&rmi_dev->dev, "Failed to register stick device %d.\n",
				stick->index);
			goto error_free_device;
		}
		stick->input = input_dev;
	}

	if (stick->query.general.has_relative) {
		struct input_dev *input_dev_mouse;
		/*create input device for mouse events  */
		input_dev_mouse = input_allocate_device();
		if (!input_dev_mouse) {
			retval = -ENOMEM;
			goto error_free_device;
		}

		snprintf(stick->mouse_name, sizeof(stick->mouse_name),
			"RMI F%02x Mouse %d", 0x17, stick->index);
		snprintf(stick->mouse_phys, sizeof(stick->mouse_name),
			 "sensor00fn%02x/mouse%d", 0x17, stick->index);
		input_dev_mouse->name = stick->mouse_name;
		input_dev_mouse->phys = stick->mouse_phys;
		input_dev_mouse->dev.parent = &fc->dev;

		input_dev_mouse->id.vendor  = 0x18d1;
		input_dev_mouse->id.product = 0x0210;
		input_dev_mouse->id.version = 0x0100;

		set_bit(EV_REL, input_dev_mouse->evbit);
		set_bit(REL_X, input_dev_mouse->relbit);
		set_bit(REL_Y, input_dev_mouse->relbit);

		set_bit(BTN_MOUSE, input_dev_mouse->evbit);
		/* Register device's buttons and keys */
		set_bit(EV_KEY, input_dev_mouse->evbit);
		set_bit(BTN_LEFT, input_dev_mouse->keybit);
		set_bit(BTN_MIDDLE, input_dev_mouse->keybit);
		set_bit(BTN_RIGHT, input_dev_mouse->keybit);

		retval = input_register_device(input_dev_mouse);
		if (retval < 0)
			goto error_free_device;
		stick->mouse = input_dev_mouse;
	}

	return 0;

error_free_device:
	if (stick->input) {
		input_free_device(stick->input);
		stick->input = NULL;
	}
	if (stick->mouse) {
		input_free_device(stick->mouse);
		stick->mouse = NULL;
	}
	return retval;
}

static int f17_register_devices(struct rmi_function_container *fc)
{
	struct rmi_f17_device_data *f17 = fc->data;
	int i;
	int retval = 0;

	for (i = 0; i < f17->query.number_of_sticks + 1 && !retval; i++) {
		retval = f17_register_stick(fc, &f17->sticks[i]);
	}

	return retval;
}

static int f17_create_sysfs(struct rmi_function_container *fc)
{
	int attr_count = 0;
	int rc;

	dev_dbg(&fc->dev, "Creating sysfs files.\n");
	/* Set up sysfs device attributes. */
	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		if (sysfs_create_file
		    (&fc->dev.kobj, &attrs[attr_count].attr) < 0) {
			dev_err(&fc->dev,
				"Failed to create sysfs file for %s.",
				attrs[attr_count].attr.name);
			rc = -ENODEV;
			goto err_remove_sysfs;
		}
	}

	return 0;

err_remove_sysfs:
	for (attr_count--; attr_count >= 0; attr_count--)
		sysfs_remove_file(&fc->dev.kobj, &attrs[attr_count].attr);
	return rc;

}

static int f17_config(struct rmi_function_container *fc)
{
	struct rmi_f17_device_data *f17 = fc->data;
	int retval;
	int i;

	retval = rmi_write_block(fc->rmi_dev, f17->control_address,
		f17->controls.regs, sizeof(f17->controls.regs));
	if (retval < 0) {
		dev_err(&fc->dev, "Could not write stick controls to 0x%04x\n",
				f17->control_address);
		return retval;
	}

#if 0
	if (f17->query.has_relative) {
		retval = rmi_write_block(fc->rmi_dev,
			f17->relative_control_address,
			f17->controls.relative.regs,
			sizeof(f17->controls.relative.regs));
		if (retval < 0) {
			dev_err(&fc->dev, "Could not write stick controls to 0x%04x\n",
					f17->control_address);
			return retval;
		}
	}
#endif

	for (i = 0; i < f17->query.number_of_sticks + 1; i++) {
		// TODO: Configure each styk
	}

	return retval;
}

static int f17_reset(struct rmi_function_container *fc)
{
	/* we do nothing here */
	return 0;
}

static void f17_remove(struct rmi_function_container *fc)
{
	struct rmi_f17_device_data *f17 = fc->data;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(attrs); i++)
		sysfs_remove_file(&fc->dev.kobj, &attrs[i].attr);

	for (i = 0; i < f17->query.number_of_sticks + 1; i++) {
		input_unregister_device(f17->sticks[i].input);
	}

	f17_free_memory(fc);
}

static int f17_process_stick(struct rmi_device *rmi_dev,
			     struct rmi_f17_stick_data *stick) {
	int retval = 0;

	if (stick->query.general.has_absolute) {
		retval = rmi_read_block(rmi_dev, stick->abs_data_address,
			stick->data.abs.regs, sizeof(stick->data.abs.regs));
		if (retval < 0) {
			dev_err(&rmi_dev->dev, "Failed to read abs data for stick %d.\n",
				stick->index);
			goto error_exit;
		}
	}
	if (stick->query.general.has_relative) {
		retval = rmi_read_block(rmi_dev, stick->rel_data_address,
			stick->data.rel.regs, sizeof(stick->data.rel.regs));
		if (retval < 0) {
			dev_err(&rmi_dev->dev, "Failed to read rel data for stick %d.\n",
				stick->index);
			goto error_exit;
		}
		dev_info(&rmi_dev->dev, "Reporting dX: %d, dy: %d\n", stick->data.rel.x_delta, stick->data.rel.y_delta);
		input_report_rel(stick->mouse, REL_X, stick->data.rel.x_delta);
		input_report_rel(stick->mouse, REL_Y, stick->data.rel.y_delta);
	}
	if (stick->query.general.has_gestures) {
		retval = rmi_read_block(rmi_dev, stick->gesture_data_address,
			stick->data.gestures.regs, sizeof(stick->data.gestures.regs));
		if (retval < 0) {
			dev_err(&rmi_dev->dev, "Failed to read gesture data for stick %d.\n",
				stick->index);
			goto error_exit;
		}
	}
	retval = 0;

error_exit:
	if (stick->input)
		input_sync(stick->input);
	if (stick->mouse)
		input_sync(stick->mouse);
	return retval;
}

static int f17_attention(struct rmi_function_container *fc, u8 *irq_bits)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_f17_device_data *f17 = fc->data;
	int i;
	int retval = 0;

	for (i = 0; i < f17->query.number_of_sticks + 1 && !retval; i++) {
		retval = f17_process_stick(rmi_dev, &f17->sticks[i]);
	}

	return retval;
}

static struct rmi_function_handler function_handler = {
	.func = 0x17,
	.init = f17_init,
	.config = f17_config,
	.reset = f17_reset,
	.attention = f17_attention,
	.remove = f17_remove
};

static int __init f17_module_init(void)
{
	int error;

	error = rmi_register_function_driver(&function_handler);
	if (error < 0) {
		pr_err("%s: register failed!\n", __func__);
		return error;
	}

	return 0;
}

static void f17_module_exit(void)
{
	rmi_unregister_function_driver(&function_handler);
}


static ssize_t f17_rezero_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rmi_function_container *fc;
	struct rmi_f17_device_data *f17;

	fc = to_rmi_function_container(dev);
	f17 = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n",
			f17->commands.rezero);

}

static ssize_t f17_rezero_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t count)
{
	struct rmi_function_container *fc;
	struct rmi_f17_device_data *data;
	unsigned int new_value;
	int len;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	len = sscanf(buf, "%u", &new_value);
	if (new_value != 0 && new_value != 1) {
		dev_err(dev,
			"%s: Error - rezero is not a valid value 0x%x.\n",
			__func__, new_value);
		return -EINVAL;
	}
	data->commands.rezero = new_value;
	len = rmi_write(fc->rmi_dev, fc->fd.command_base_addr,
		data->commands.rezero);

	if (len < 0) {
		dev_err(dev, "%s : Could not write rezero to 0x%x\n",
				__func__, fc->fd.command_base_addr);
		return -EINVAL;
	}
	return count;
}


module_init(f17_module_init);
module_exit(f17_module_exit);

MODULE_AUTHOR("Christopher Heiny <cheiny@synaptics.com>");
MODULE_DESCRIPTION("RMI F17 module");
MODULE_LICENSE("GPL");
MODULE_VERSION(RMI_DRIVER_VERSION);
