// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * This driver provides the core support for a single RMI4-based device.
 *
 * The RMI4 specification can be found here (URL split for line length):
 *
 * http://www.synaptics.com/sites/default/files/
 *      511-000136-01-Rev-E-RMI4-Interfacing-Guide.pdf
 */

#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/irqdomain.h>
#include <uapi/linux/input.h>
#include <linux/rmi.h>
#include "rmi_bus.h"
#include "rmi_driver.h"

#define HAS_NONSTANDARD_PDT_MASK 0x40
#define RMI4_MAX_PAGE 0xff
#define RMI4_PAGE_SIZE 0x100
#define RMI4_PAGE_MASK 0xFF00

#define RMI_DEVICE_RESET_CMD	0x01
#define DEFAULT_RESET_DELAY_MS	100

void rmi_free_function_list(struct rmi_device *rmi_dev)
{
	struct rmi_function *fn, *tmp;
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);

	rmi_dbg(RMI_DEBUG_CORE, &rmi_dev->dev, "Freeing function list\n");

	/* Doing it in the reverse order so F01 will be removed last */
	list_for_each_entry_safe_reverse(fn, tmp,
					 &data->function_list, node) {
		list_del(&fn->node);
		rmi_unregister_function(fn);
	}

	devm_kfree(&rmi_dev->dev, data->irq_memory);
	data->irq_memory = NULL;
	data->irq_status = NULL;
	data->fn_irq_bits = NULL;
	data->current_irq_mask = NULL;
	data->new_irq_mask = NULL;

	data->f01_container = NULL;
	data->f34_container = NULL;
}

static int reset_one_function(struct rmi_function *fn)
{
	struct rmi_function_handler *fh;
	int retval = 0;

	if (!fn || !fn->dev.driver)
		return 0;

	fh = to_rmi_function_handler(fn->dev.driver);
	if (fh->reset) {
		retval = fh->reset(fn);
		if (retval < 0)
			dev_err(&fn->dev, "Reset failed with code %d.\n",
				retval);
	}

	return retval;
}

static int configure_one_function(struct rmi_function *fn)
{
	struct rmi_function_handler *fh;
	int retval = 0;

	if (!fn || !fn->dev.driver)
		return 0;

	fh = to_rmi_function_handler(fn->dev.driver);
	if (fh->config) {
		retval = fh->config(fn);
		if (retval < 0)
			dev_err(&fn->dev, "Config failed with code %d.\n",
				retval);
	}

	return retval;
}

static int rmi_driver_process_reset_requests(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct rmi_function *entry;
	int retval;

	list_for_each_entry(entry, &data->function_list, node) {
		retval = reset_one_function(entry);
		if (retval < 0)
			return retval;
	}

	return 0;
}

static int rmi_driver_process_config_requests(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct rmi_function *entry;
	int retval;

	list_for_each_entry(entry, &data->function_list, node) {
		retval = configure_one_function(entry);
		if (retval < 0)
			return retval;
	}

	return 0;
}

static int rmi_process_interrupt_requests(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct device *dev = &rmi_dev->dev;
	int i;
	int error;

	if (!data)
		return 0;

	if (!data->attn_data.data) {
		error = rmi_read_block(rmi_dev,
				data->f01_container->fd.data_base_addr + 1,
				data->irq_status, data->num_of_irq_regs);
		if (error < 0) {
			dev_err(dev, "Failed to read irqs, code=%d\n", error);
			return error;
		}
	}

	mutex_lock(&data->irq_mutex);
	bitmap_and(data->irq_status, data->irq_status, data->fn_irq_bits,
	       data->irq_count);
	/*
	 * At this point, irq_status has all bits that are set in the
	 * interrupt status register and are enabled.
	 */
	mutex_unlock(&data->irq_mutex);

	for_each_set_bit(i, data->irq_status, data->irq_count)
		handle_nested_irq(irq_find_mapping(data->irqdomain, i));

	if (data->input)
		input_sync(data->input);

	return 0;
}

void rmi_set_attn_data(struct rmi_device *rmi_dev, unsigned long irq_status,
		       void *data, size_t size)
{
	struct rmi_driver_data *drvdata = dev_get_drvdata(&rmi_dev->dev);
	struct rmi4_attn_data attn_data;
	void *fifo_data;

	if (!drvdata->enabled)
		return;

	fifo_data = kmemdup(data, size, GFP_ATOMIC);
	if (!fifo_data)
		return;

	attn_data.irq_status = irq_status;
	attn_data.size = size;
	attn_data.data = fifo_data;

	kfifo_put(&drvdata->attn_fifo, attn_data);
}
EXPORT_SYMBOL_GPL(rmi_set_attn_data);

static irqreturn_t rmi_irq_fn(int irq, void *dev_id)
{
	struct rmi_device *rmi_dev = dev_id;
	struct rmi_driver_data *drvdata = dev_get_drvdata(&rmi_dev->dev);
	struct rmi4_attn_data attn_data = {0};
	int ret, count;

	count = kfifo_get(&drvdata->attn_fifo, &attn_data);
	if (count) {
		*(drvdata->irq_status) = attn_data.irq_status;
		drvdata->attn_data = attn_data;
	}

	ret = rmi_process_interrupt_requests(rmi_dev);
	if (ret)
		rmi_dbg(RMI_DEBUG_CORE, &rmi_dev->dev,
			"Failed to process interrupt request: %d\n", ret);

	if (count) {
		kfree(attn_data.data);
		drvdata->attn_data.data = NULL;
	}

	if (!kfifo_is_empty(&drvdata->attn_fifo))
		return rmi_irq_fn(irq, dev_id);

	return IRQ_HANDLED;
}

static int rmi_irq_init(struct rmi_device *rmi_dev)
{
	struct rmi_device_platform_data *pdata = rmi_get_platform_data(rmi_dev);
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	int irq_flags = irq_get_trigger_type(pdata->irq);
	int ret;

	if (!irq_flags)
		irq_flags = IRQF_TRIGGER_LOW;

	ret = devm_request_threaded_irq(&rmi_dev->dev, pdata->irq, NULL,
					rmi_irq_fn, irq_flags | IRQF_ONESHOT,
					dev_driver_string(rmi_dev->xport->dev),
					rmi_dev);
	if (ret < 0) {
		dev_err(&rmi_dev->dev, "Failed to register interrupt %d\n",
			pdata->irq);

		return ret;
	}

	data->enabled = true;

	return 0;
}

struct rmi_function *rmi_find_function(struct rmi_device *rmi_dev, u8 number)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct rmi_function *entry;

	list_for_each_entry(entry, &data->function_list, node) {
		if (entry->fd.function_number == number)
			return entry;
	}

	return NULL;
}

static int suspend_one_function(struct rmi_function *fn)
{
	struct rmi_function_handler *fh;
	int retval = 0;

	if (!fn || !fn->dev.driver)
		return 0;

	fh = to_rmi_function_handler(fn->dev.driver);
	if (fh->suspend) {
		retval = fh->suspend(fn);
		if (retval < 0)
			dev_err(&fn->dev, "Suspend failed with code %d.\n",
				retval);
	}

	return retval;
}

static int rmi_suspend_functions(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct rmi_function *entry;
	int retval;

	list_for_each_entry(entry, &data->function_list, node) {
		retval = suspend_one_function(entry);
		if (retval < 0)
			return retval;
	}

	return 0;
}

static int resume_one_function(struct rmi_function *fn)
{
	struct rmi_function_handler *fh;
	int retval = 0;

	if (!fn || !fn->dev.driver)
		return 0;

	fh = to_rmi_function_handler(fn->dev.driver);
	if (fh->resume) {
		retval = fh->resume(fn);
		if (retval < 0)
			dev_err(&fn->dev, "Resume failed with code %d.\n",
				retval);
	}

	return retval;
}

static int rmi_resume_functions(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct rmi_function *entry;
	int retval;

	list_for_each_entry(entry, &data->function_list, node) {
		retval = resume_one_function(entry);
		if (retval < 0)
			return retval;
	}

	return 0;
}

int rmi_enable_sensor(struct rmi_device *rmi_dev)
{
	int retval = 0;

	retval = rmi_driver_process_config_requests(rmi_dev);
	if (retval < 0)
		return retval;

	return rmi_process_interrupt_requests(rmi_dev);
}

/**
 * rmi_driver_set_input_params - set input device id and other data.
 *
 * @rmi_dev: Pointer to an RMI device
 * @input: Pointer to input device
 *
 */
static int rmi_driver_set_input_params(struct rmi_device *rmi_dev,
				struct input_dev *input)
{
	input->name = SYNAPTICS_INPUT_DEVICE_NAME;
	input->id.vendor  = SYNAPTICS_VENDOR_ID;
	input->id.bustype = BUS_RMI;
	return 0;
}

static void rmi_driver_set_input_name(struct rmi_device *rmi_dev,
				struct input_dev *input)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	const char *device_name = rmi_f01_get_product_ID(data->f01_container);
	char *name;

	name = devm_kasprintf(&rmi_dev->dev, GFP_KERNEL,
			      "Synaptics %s", device_name);
	if (!name)
		return;

	input->name = name;
}

static int rmi_driver_set_irq_bits(struct rmi_device *rmi_dev,
				   unsigned long *mask)
{
	int error = 0;
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct device *dev = &rmi_dev->dev;

	mutex_lock(&data->irq_mutex);
	bitmap_or(data->new_irq_mask,
		  data->current_irq_mask, mask, data->irq_count);

	error = rmi_write_block(rmi_dev,
			data->f01_container->fd.control_base_addr + 1,
			data->new_irq_mask, data->num_of_irq_regs);
	if (error < 0) {
		dev_err(dev, "%s: Failed to change enabled interrupts!",
							__func__);
		goto error_unlock;
	}
	bitmap_copy(data->current_irq_mask, data->new_irq_mask,
		    data->num_of_irq_regs);

	bitmap_or(data->fn_irq_bits, data->fn_irq_bits, mask, data->irq_count);

error_unlock:
	mutex_unlock(&data->irq_mutex);
	return error;
}

static int rmi_driver_clear_irq_bits(struct rmi_device *rmi_dev,
				     unsigned long *mask)
{
	int error = 0;
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct device *dev = &rmi_dev->dev;

	mutex_lock(&data->irq_mutex);
	bitmap_andnot(data->fn_irq_bits,
		      data->fn_irq_bits, mask, data->irq_count);
	bitmap_andnot(data->new_irq_mask,
		  data->current_irq_mask, mask, data->irq_count);

	error = rmi_write_block(rmi_dev,
			data->f01_container->fd.control_base_addr + 1,
			data->new_irq_mask, data->num_of_irq_regs);
	if (error < 0) {
		dev_err(dev, "%s: Failed to change enabled interrupts!",
							__func__);
		goto error_unlock;
	}
	bitmap_copy(data->current_irq_mask, data->new_irq_mask,
		    data->num_of_irq_regs);

error_unlock:
	mutex_unlock(&data->irq_mutex);
	return error;
}

static int rmi_driver_reset_handler(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	int error;

	/*
	 * Can get called before the driver is fully ready to deal with
	 * this situation.
	 */
	if (!data || !data->f01_container) {
		dev_warn(&rmi_dev->dev,
			 "Not ready to handle reset yet!\n");
		return 0;
	}

	error = rmi_read_block(rmi_dev,
			       data->f01_container->fd.control_base_addr + 1,
			       data->current_irq_mask, data->num_of_irq_regs);
	if (error < 0) {
		dev_err(&rmi_dev->dev, "%s: Failed to read current IRQ mask.\n",
			__func__);
		return error;
	}

	error = rmi_driver_process_reset_requests(rmi_dev);
	if (error < 0)
		return error;

	error = rmi_driver_process_config_requests(rmi_dev);
	if (error < 0)
		return error;

	return 0;
}

static int rmi_read_pdt_entry(struct rmi_device *rmi_dev,
			      struct pdt_entry *entry, u16 pdt_address)
{
	u8 buf[RMI_PDT_ENTRY_SIZE];
	int error;

	error = rmi_read_block(rmi_dev, pdt_address, buf, RMI_PDT_ENTRY_SIZE);
	if (error) {
		dev_err(&rmi_dev->dev, "Read PDT entry at %#06x failed, code: %d.\n",
				pdt_address, error);
		return error;
	}

	entry->page_start = pdt_address & RMI4_PAGE_MASK;
	entry->query_base_addr = buf[0];
	entry->command_base_addr = buf[1];
	entry->control_base_addr = buf[2];
	entry->data_base_addr = buf[3];
	entry->interrupt_source_count = buf[4] & RMI_PDT_INT_SOURCE_COUNT_MASK;
	entry->function_version = (buf[4] & RMI_PDT_FUNCTION_VERSION_MASK) >> 5;
	entry->function_number = buf[5];

	return 0;
}

static void rmi_driver_copy_pdt_to_fd(const struct pdt_entry *pdt,
				      struct rmi_function_descriptor *fd)
{
	fd->query_base_addr = pdt->query_base_addr + pdt->page_start;
	fd->command_base_addr = pdt->command_base_addr + pdt->page_start;
	fd->control_base_addr = pdt->control_base_addr + pdt->page_start;
	fd->data_base_addr = pdt->data_base_addr + pdt->page_start;
	fd->function_number = pdt->function_number;
	fd->interrupt_source_count = pdt->interrupt_source_count;
	fd->function_version = pdt->function_version;
}

#define RMI_SCAN_CONTINUE	0
#define RMI_SCAN_DONE		1

static int rmi_scan_pdt_page(struct rmi_device *rmi_dev,
			     int page,
			     int *empty_pages,
			     void *ctx,
			     int (*callback)(struct rmi_device *rmi_dev,
					     void *ctx,
					     const struct pdt_entry *entry))
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct pdt_entry pdt_entry;
	u16 page_start = RMI4_PAGE_SIZE * page;
	u16 pdt_start = page_start + PDT_START_SCAN_LOCATION;
	u16 pdt_end = page_start + PDT_END_SCAN_LOCATION;
	u16 addr;
	int error;
	int retval;

	for (addr = pdt_start; addr >= pdt_end; addr -= RMI_PDT_ENTRY_SIZE) {
		error = rmi_read_pdt_entry(rmi_dev, &pdt_entry, addr);
		if (error)
			return error;

		if (RMI4_END_OF_PDT(pdt_entry.function_number))
			break;

		retval = callback(rmi_dev, ctx, &pdt_entry);
		if (retval != RMI_SCAN_CONTINUE)
			return retval;
	}

	/*
	 * Count number of empty PDT pages. If a gap of two pages
	 * or more is found, stop scanning.
	 */
	if (addr == pdt_start)
		++*empty_pages;
	else
		*empty_pages = 0;

	return (data->bootloader_mode || *empty_pages >= 2) ?
					RMI_SCAN_DONE : RMI_SCAN_CONTINUE;
}

int rmi_scan_pdt(struct rmi_device *rmi_dev, void *ctx,
		 int (*callback)(struct rmi_device *rmi_dev,
		 void *ctx, const struct pdt_entry *entry))
{
	int page;
	int empty_pages = 0;
	int retval = RMI_SCAN_DONE;

	for (page = 0; page <= RMI4_MAX_PAGE; page++) {
		retval = rmi_scan_pdt_page(rmi_dev, page, &empty_pages,
					   ctx, callback);
		if (retval != RMI_SCAN_CONTINUE)
			break;
	}

	return retval < 0 ? retval : 0;
}

int rmi_read_register_desc(struct rmi_device *d, u16 addr,
				struct rmi_register_descriptor *rdesc)
{
	int ret;
	u8 size_presence_reg;
	u8 buf[35];
	int presense_offset = 1;
	u8 *struct_buf;
	int reg;
	int offset = 0;
	int map_offset = 0;
	int i;
	int b;

	/*
	 * The first register of the register descriptor is the size of
	 * the register descriptor's presense register.
	 */
	ret = rmi_read(d, addr, &size_presence_reg);
	if (ret)
		return ret;
	++addr;

	if (size_presence_reg < 0 || size_presence_reg > 35)
		return -EIO;

	memset(buf, 0, sizeof(buf));

	/*
	 * The presence register contains the size of the register structure
	 * and a bitmap which identified which packet registers are present
	 * for this particular register type (ie query, control, or data).
	 */
	ret = rmi_read_block(d, addr, buf, size_presence_reg);
	if (ret)
		return ret;
	++addr;

	if (buf[0] == 0) {
		presense_offset = 3;
		rdesc->struct_size = buf[1] | (buf[2] << 8);
	} else {
		rdesc->struct_size = buf[0];
	}

	for (i = presense_offset; i < size_presence_reg; i++) {
		for (b = 0; b < 8; b++) {
			if (buf[i] & (0x1 << b))
				bitmap_set(rdesc->presense_map, map_offset, 1);
			++map_offset;
		}
	}

	rdesc->num_registers = bitmap_weight(rdesc->presense_map,
						RMI_REG_DESC_PRESENSE_BITS);

	rdesc->registers = devm_kcalloc(&d->dev,
					rdesc->num_registers,
					sizeof(struct rmi_register_desc_item),
					GFP_KERNEL);
	if (!rdesc->registers)
		return -ENOMEM;

	/*
	 * Allocate a temporary buffer to hold the register structure.
	 * I'm not using devm_kzalloc here since it will not be retained
	 * after exiting this function
	 */
	struct_buf = kzalloc(rdesc->struct_size, GFP_KERNEL);
	if (!struct_buf)
		return -ENOMEM;

	/*
	 * The register structure contains information about every packet
	 * register of this type. This includes the size of the packet
	 * register and a bitmap of all subpackets contained in the packet
	 * register.
	 */
	ret = rmi_read_block(d, addr, struct_buf, rdesc->struct_size);
	if (ret)
		goto free_struct_buff;

	reg = find_first_bit(rdesc->presense_map, RMI_REG_DESC_PRESENSE_BITS);
	for (i = 0; i < rdesc->num_registers; i++) {
		struct rmi_register_desc_item *item = &rdesc->registers[i];
		int reg_size = struct_buf[offset];

		++offset;
		if (reg_size == 0) {
			reg_size = struct_buf[offset] |
					(struct_buf[offset + 1] << 8);
			offset += 2;
		}

		if (reg_size == 0) {
			reg_size = struct_buf[offset] |
					(struct_buf[offset + 1] << 8) |
					(struct_buf[offset + 2] << 16) |
					(struct_buf[offset + 3] << 24);
			offset += 4;
		}

		item->reg = reg;
		item->reg_size = reg_size;

		map_offset = 0;

		do {
			for (b = 0; b < 7; b++) {
				if (struct_buf[offset] & (0x1 << b))
					bitmap_set(item->subpacket_map,
						map_offset, 1);
				++map_offset;
			}
		} while (struct_buf[offset++] & 0x80);

		item->num_subpackets = bitmap_weight(item->subpacket_map,
						RMI_REG_DESC_SUBPACKET_BITS);

		rmi_dbg(RMI_DEBUG_CORE, &d->dev,
			"%s: reg: %d reg size: %ld subpackets: %d\n", __func__,
			item->reg, item->reg_size, item->num_subpackets);

		reg = find_next_bit(rdesc->presense_map,
				RMI_REG_DESC_PRESENSE_BITS, reg + 1);
	}

free_struct_buff:
	kfree(struct_buf);
	return ret;
}

const struct rmi_register_desc_item *rmi_get_register_desc_item(
				struct rmi_register_descriptor *rdesc, u16 reg)
{
	const struct rmi_register_desc_item *item;
	int i;

	for (i = 0; i < rdesc->num_registers; i++) {
		item = &rdesc->registers[i];
		if (item->reg == reg)
			return item;
	}

	return NULL;
}

size_t rmi_register_desc_calc_size(struct rmi_register_descriptor *rdesc)
{
	const struct rmi_register_desc_item *item;
	int i;
	size_t size = 0;

	for (i = 0; i < rdesc->num_registers; i++) {
		item = &rdesc->registers[i];
		size += item->reg_size;
	}
	return size;
}

/* Compute the register offset relative to the base address */
int rmi_register_desc_calc_reg_offset(
		struct rmi_register_descriptor *rdesc, u16 reg)
{
	const struct rmi_register_desc_item *item;
	int offset = 0;
	int i;

	for (i = 0; i < rdesc->num_registers; i++) {
		item = &rdesc->registers[i];
		if (item->reg == reg)
			return offset;
		++offset;
	}
	return -1;
}

bool rmi_register_desc_has_subpacket(const struct rmi_register_desc_item *item,
	u8 subpacket)
{
	return find_next_bit(item->subpacket_map, RMI_REG_DESC_PRESENSE_BITS,
				subpacket) == subpacket;
}

static int rmi_check_bootloader_mode(struct rmi_device *rmi_dev,
				     const struct pdt_entry *pdt)
{
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	int ret;
	u8 status;

	if (pdt->function_number == 0x34 && pdt->function_version > 1) {
		ret = rmi_read(rmi_dev, pdt->data_base_addr, &status);
		if (ret) {
			dev_err(&rmi_dev->dev,
				"Failed to read F34 status: %d.\n", ret);
			return ret;
		}

		if (status & BIT(7))
			data->bootloader_mode = true;
	} else if (pdt->function_number == 0x01) {
		ret = rmi_read(rmi_dev, pdt->data_base_addr, &status);
		if (ret) {
			dev_err(&rmi_dev->dev,
				"Failed to read F01 status: %d.\n", ret);
			return ret;
		}

		if (status & BIT(6))
			data->bootloader_mode = true;
	}

	return 0;
}

static int rmi_count_irqs(struct rmi_device *rmi_dev,
			 void *ctx, const struct pdt_entry *pdt)
{
	int *irq_count = ctx;
	int ret;

	*irq_count += pdt->interrupt_source_count;

	ret = rmi_check_bootloader_mode(rmi_dev, pdt);
	if (ret < 0)
		return ret;

	return RMI_SCAN_CONTINUE;
}

int rmi_initial_reset(struct rmi_device *rmi_dev, void *ctx,
		      const struct pdt_entry *pdt)
{
	int error;

	if (pdt->function_number == 0x01) {
		u16 cmd_addr = pdt->page_start + pdt->command_base_addr;
		u8 cmd_buf = RMI_DEVICE_RESET_CMD;
		const struct rmi_device_platform_data *pdata =
				rmi_get_platform_data(rmi_dev);

		if (rmi_dev->xport->ops->reset) {
			error = rmi_dev->xport->ops->reset(rmi_dev->xport,
								cmd_addr);
			if (error)
				return error;

			return RMI_SCAN_DONE;
		}

		rmi_dbg(RMI_DEBUG_CORE, &rmi_dev->dev, "Sending reset\n");
		error = rmi_write_block(rmi_dev, cmd_addr, &cmd_buf, 1);
		if (error) {
			dev_err(&rmi_dev->dev,
				"Initial reset failed. Code = %d.\n", error);
			return error;
		}

		mdelay(pdata->reset_delay_ms ?: DEFAULT_RESET_DELAY_MS);

		return RMI_SCAN_DONE;
	}

	/* F01 should always be on page 0. If we don't find it there, fail. */
	return pdt->page_start == 0 ? RMI_SCAN_CONTINUE : -ENODEV;
}

static int rmi_create_function(struct rmi_device *rmi_dev,
			       void *ctx, const struct pdt_entry *pdt)
{
	struct device *dev = &rmi_dev->dev;
	struct rmi_driver_data *data = dev_get_drvdata(dev);
	int *current_irq_count = ctx;
	struct rmi_function *fn;
	int i;
	int error;

	rmi_dbg(RMI_DEBUG_CORE, dev, "Initializing F%02X.\n",
			pdt->function_number);

	fn = kzalloc(sizeof(struct rmi_function) +
			BITS_TO_LONGS(data->irq_count) * sizeof(unsigned long),
		     GFP_KERNEL);
	if (!fn) {
		dev_err(dev, "Failed to allocate memory for F%02X\n",
			pdt->function_number);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&fn->node);
	rmi_driver_copy_pdt_to_fd(pdt, &fn->fd);

	fn->rmi_dev = rmi_dev;

	fn->num_of_irqs = pdt->interrupt_source_count;
	fn->irq_pos = *current_irq_count;
	*current_irq_count += fn->num_of_irqs;

	for (i = 0; i < fn->num_of_irqs; i++)
		set_bit(fn->irq_pos + i, fn->irq_mask);

	error = rmi_register_function(fn);
	if (error)
		return error;

	if (pdt->function_number == 0x01)
		data->f01_container = fn;
	else if (pdt->function_number == 0x34)
		data->f34_container = fn;

	list_add_tail(&fn->node, &data->function_list);

	return RMI_SCAN_CONTINUE;
}

void rmi_enable_irq(struct rmi_device *rmi_dev, bool clear_wake)
{
	struct rmi_device_platform_data *pdata = rmi_get_platform_data(rmi_dev);
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	int irq = pdata->irq;
	int irq_flags;
	int retval;

	mutex_lock(&data->enabled_mutex);

	if (data->enabled)
		goto out;

	enable_irq(irq);
	data->enabled = true;
	if (clear_wake && device_may_wakeup(rmi_dev->xport->dev)) {
		retval = disable_irq_wake(irq);
		if (retval)
			dev_warn(&rmi_dev->dev,
				 "Failed to disable irq for wake: %d\n",
				 retval);
	}

	/*
	 * Call rmi_process_interrupt_requests() after enabling irq,
	 * otherwise we may lose interrupt on edge-triggered systems.
	 */
	irq_flags = irq_get_trigger_type(pdata->irq);
	if (irq_flags & IRQ_TYPE_EDGE_BOTH)
		rmi_process_interrupt_requests(rmi_dev);

out:
	mutex_unlock(&data->enabled_mutex);
}

void rmi_disable_irq(struct rmi_device *rmi_dev, bool enable_wake)
{
	struct rmi_device_platform_data *pdata = rmi_get_platform_data(rmi_dev);
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);
	struct rmi4_attn_data attn_data = {0};
	int irq = pdata->irq;
	int retval, count;

	mutex_lock(&data->enabled_mutex);

	if (!data->enabled)
		goto out;

	data->enabled = false;
	disable_irq(irq);
	if (enable_wake && device_may_wakeup(rmi_dev->xport->dev)) {
		retval = enable_irq_wake(irq);
		if (retval)
			dev_warn(&rmi_dev->dev,
				 "Failed to enable irq for wake: %d\n",
				 retval);
	}

	/* make sure the fifo is clean */
	while (!kfifo_is_empty(&data->attn_fifo)) {
		count = kfifo_get(&data->attn_fifo, &attn_data);
		if (count)
			kfree(attn_data.data);
	}

out:
	mutex_unlock(&data->enabled_mutex);
}

int rmi_driver_suspend(struct rmi_device *rmi_dev, bool enable_wake)
{
	int retval;

	retval = rmi_suspend_functions(rmi_dev);
	if (retval)
		dev_warn(&rmi_dev->dev, "Failed to suspend functions: %d\n",
			retval);

	rmi_disable_irq(rmi_dev, enable_wake);
	return retval;
}
EXPORT_SYMBOL_GPL(rmi_driver_suspend);

int rmi_driver_resume(struct rmi_device *rmi_dev, bool clear_wake)
{
	int retval;

	rmi_enable_irq(rmi_dev, clear_wake);

	retval = rmi_resume_functions(rmi_dev);
	if (retval)
		dev_warn(&rmi_dev->dev, "Failed to suspend functions: %d\n",
			retval);

	return retval;
}
EXPORT_SYMBOL_GPL(rmi_driver_resume);

static int rmi_driver_remove(struct device *dev)
{
	struct rmi_device *rmi_dev = to_rmi_device(dev);
	struct rmi_driver_data *data = dev_get_drvdata(&rmi_dev->dev);

	rmi_disable_irq(rmi_dev, false);

	irq_domain_remove(data->irqdomain);
	data->irqdomain = NULL;

	rmi_f34_remove_sysfs(rmi_dev);
	rmi_free_function_list(rmi_dev);

	return 0;
}

#ifdef CONFIG_OF
static int rmi_driver_of_probe(struct device *dev,
				struct rmi_device_platform_data *pdata)
{
	int retval;

	retval = rmi_of_property_read_u32(dev, &pdata->reset_delay_ms,
					"syna,reset-delay-ms", 1);
	if (retval)
		return retval;

	return 0;
}
#else
static inline int rmi_driver_of_probe(struct device *dev,
					struct rmi_device_platform_data *pdata)
{
	return -ENODEV;
}
#endif

int rmi_probe_interrupts(struct rmi_driver_data *data)
{
	struct rmi_device *rmi_dev = data->rmi_dev;
	struct device *dev = &rmi_dev->dev;
	struct fwnode_handle *fwnode = rmi_dev->xport->dev->fwnode;
	int irq_count = 0;
	size_t size;
	int retval;

	/*
	 * We need to count the IRQs and allocate their storage before scanning
	 * the PDT and creating the function entries, because adding a new
	 * function can trigger events that result in the IRQ related storage
	 * being accessed.
	 */
	rmi_dbg(RMI_DEBUG_CORE, dev, "%s: Counting IRQs.\n", __func__);
	data->bootloader_mode = false;

	retval = rmi_scan_pdt(rmi_dev, &irq_count, rmi_count_irqs);
	if (retval < 0) {
		dev_err(dev, "IRQ counting failed with code %d.\n", retval);
		return retval;
	}

	if (data->bootloader_mode)
		dev_warn(dev, "Device in bootloader mode.\n");

	/* Allocate and register a linear revmap irq_domain */
	data->irqdomain = irq_domain_create_linear(fwnode, irq_count,
						   &irq_domain_simple_ops,
						   data);
	if (!data->irqdomain) {
		dev_err(&rmi_dev->dev, "Failed to create IRQ domain\n");
		return -ENOMEM;
	}

	data->irq_count = irq_count;
	data->num_of_irq_regs = (data->irq_count + 7) / 8;

	size = BITS_TO_LONGS(data->irq_count) * sizeof(unsigned long);
	data->irq_memory = devm_kcalloc(dev, size, 4, GFP_KERNEL);
	if (!data->irq_memory) {
		dev_err(dev, "Failed to allocate memory for irq masks.\n");
		return -ENOMEM;
	}

	data->irq_status	= data->irq_memory + size * 0;
	data->fn_irq_bits	= data->irq_memory + size * 1;
	data->current_irq_mask	= data->irq_memory + size * 2;
	data->new_irq_mask	= data->irq_memory + size * 3;

	return retval;
}

int rmi_init_functions(struct rmi_driver_data *data)
{
	struct rmi_device *rmi_dev = data->rmi_dev;
	struct device *dev = &rmi_dev->dev;
	int irq_count = 0;
	int retval;

	rmi_dbg(RMI_DEBUG_CORE, dev, "%s: Creating functions.\n", __func__);
	retval = rmi_scan_pdt(rmi_dev, &irq_count, rmi_create_function);
	if (retval < 0) {
		dev_err(dev, "Function creation failed with code %d.\n",
			retval);
		goto err_destroy_functions;
	}

	if (!data->f01_container) {
		dev_err(dev, "Missing F01 container!\n");
		retval = -EINVAL;
		goto err_destroy_functions;
	}

	retval = rmi_read_block(rmi_dev,
				data->f01_container->fd.control_base_addr + 1,
				data->current_irq_mask, data->num_of_irq_regs);
	if (retval < 0) {
		dev_err(dev, "%s: Failed to read current IRQ mask.\n",
			__func__);
		goto err_destroy_functions;
	}

	return 0;

err_destroy_functions:
	rmi_free_function_list(rmi_dev);
	return retval;
}

static int rmi_driver_probe(struct device *dev)
{
	struct rmi_driver *rmi_driver;
	struct rmi_driver_data *data;
	struct rmi_device_platform_data *pdata;
	struct rmi_device *rmi_dev;
	int retval;

	rmi_dbg(RMI_DEBUG_CORE, dev, "%s: Starting probe.\n",
			__func__);

	if (!rmi_is_physical_device(dev)) {
		rmi_dbg(RMI_DEBUG_CORE, dev, "Not a physical device.\n");
		return -ENODEV;
	}

	rmi_dev = to_rmi_device(dev);
	rmi_driver = to_rmi_driver(dev->driver);
	rmi_dev->driver = rmi_driver;

	pdata = rmi_get_platform_data(rmi_dev);

	if (rmi_dev->xport->dev->of_node) {
		retval = rmi_driver_of_probe(rmi_dev->xport->dev, pdata);
		if (retval)
			return retval;
	}

	data = devm_kzalloc(dev, sizeof(struct rmi_driver_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&data->function_list);
	data->rmi_dev = rmi_dev;
	dev_set_drvdata(&rmi_dev->dev, data);

	/*
	 * Right before a warm boot, the sensor might be in some unusual state,
	 * such as F54 diagnostics, or F34 bootloader mode after a firmware
	 * or configuration update.  In order to clear the sensor to a known
	 * state and/or apply any updates, we issue a initial reset to clear any
	 * previous settings and force it into normal operation.
	 *
	 * We have to do this before actually building the PDT because
	 * the reflash updates (if any) might cause various registers to move
	 * around.
	 *
	 * For a number of reasons, this initial reset may fail to return
	 * within the specified time, but we'll still be able to bring up the
	 * driver normally after that failure.  This occurs most commonly in
	 * a cold boot situation (where then firmware takes longer to come up
	 * than from a warm boot) and the reset_delay_ms in the platform data
	 * has been set too short to accommodate that.  Since the sensor will
	 * eventually come up and be usable, we don't want to just fail here
	 * and leave the customer's device unusable.  So we warn them, and
	 * continue processing.
	 */
	retval = rmi_scan_pdt(rmi_dev, NULL, rmi_initial_reset);
	if (retval < 0)
		dev_warn(dev, "RMI initial reset failed! Continuing in spite of this.\n");

	retval = rmi_read(rmi_dev, PDT_PROPERTIES_LOCATION, &data->pdt_props);
	if (retval < 0) {
		/*
		 * we'll print out a warning and continue since
		 * failure to get the PDT properties is not a cause to fail
		 */
		dev_warn(dev, "Could not read PDT properties from %#06x (code %d). Assuming 0x00.\n",
			 PDT_PROPERTIES_LOCATION, retval);
	}

	mutex_init(&data->irq_mutex);
	mutex_init(&data->enabled_mutex);

	retval = rmi_probe_interrupts(data);
	if (retval)
		goto err;

	if (rmi_dev->xport->input) {
		/*
		 * The transport driver already has an input device.
		 * In some cases it is preferable to reuse the transport
		 * devices input device instead of creating a new one here.
		 * One example is some HID touchpads report "pass-through"
		 * button events are not reported by rmi registers.
		 */
		data->input = rmi_dev->xport->input;
	} else {
		data->input = devm_input_allocate_device(dev);
		if (!data->input) {
			dev_err(dev, "%s: Failed to allocate input device.\n",
				__func__);
			retval = -ENOMEM;
			goto err;
		}
		rmi_driver_set_input_params(rmi_dev, data->input);
		data->input->phys = devm_kasprintf(dev, GFP_KERNEL,
						"%s/input0", dev_name(dev));
	}

	retval = rmi_init_functions(data);
	if (retval)
		goto err;

	retval = rmi_f34_create_sysfs(rmi_dev);
	if (retval)
		goto err;

	if (data->input) {
		rmi_driver_set_input_name(rmi_dev, data->input);
		if (!rmi_dev->xport->input) {
			retval = input_register_device(data->input);
			if (retval) {
				dev_err(dev, "%s: Failed to register input device.\n",
					__func__);
				goto err_destroy_functions;
			}
		}
	}

	retval = rmi_irq_init(rmi_dev);
	if (retval < 0)
		goto err_destroy_functions;

	if (data->f01_container->dev.driver) {
		/* Driver already bound, so enable ATTN now. */
		retval = rmi_enable_sensor(rmi_dev);
		if (retval)
			goto err_disable_irq;
	}

	return 0;

err_disable_irq:
	rmi_disable_irq(rmi_dev, false);
err_destroy_functions:
	rmi_free_function_list(rmi_dev);
err:
	return retval;
}

static struct rmi_driver rmi_physical_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "rmi4_physical",
		.bus	= &rmi_bus_type,
		.probe = rmi_driver_probe,
		.remove = rmi_driver_remove,
	},
	.reset_handler = rmi_driver_reset_handler,
	.clear_irq_bits = rmi_driver_clear_irq_bits,
	.set_irq_bits = rmi_driver_set_irq_bits,
	.set_input_params = rmi_driver_set_input_params,
};

bool rmi_is_physical_driver(struct device_driver *drv)
{
	return drv == &rmi_physical_driver.driver;
}

int __init rmi_register_physical_driver(void)
{
	int error;

	error = driver_register(&rmi_physical_driver.driver);
	if (error) {
		pr_err("%s: driver register failed, code=%d.\n", __func__,
		       error);
		return error;
	}

	return 0;
}

void __exit rmi_unregister_physical_driver(void)
{
	driver_unregister(&rmi_physical_driver.driver);
}
