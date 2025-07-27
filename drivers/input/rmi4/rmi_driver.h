/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef _RMI_DRIVER_H
#define _RMI_DRIVER_H

#include <linux/ctype.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/input.h>
#include "rmi_bus.h"

#define SYNAPTICS_INPUT_DEVICE_NAME "Synaptics RMI4 Touch Sensor"
#define SYNAPTICS_VENDOR_ID 0x06cb

#define GROUP(_attrs) { \
	.attrs = _attrs,  \
}

#define PDT_PROPERTIES_LOCATION 0x00EF
#define BSR_LOCATION 0x00FE

#define RMI_PDT_PROPS_HAS_BSR 0x02

#define NAME_BUFFER_SIZE 256

#define RMI_PDT_ENTRY_SIZE 6
#define RMI_PDT_FUNCTION_VERSION_MASK   0x60
#define RMI_PDT_INT_SOURCE_COUNT_MASK   0x07

#define PDT_START_SCAN_LOCATION 0x00e9
#define PDT_END_SCAN_LOCATION	0x0005
#define RMI4_END_OF_PDT(id) ((id) == 0x00 || (id) == 0xff)

struct pdt_entry {
	u16 page_start;
	u8 query_base_addr;
	u8 command_base_addr;
	u8 control_base_addr;
	u8 data_base_addr;
	u8 interrupt_source_count;
	u8 function_version;
	u8 function_number;
};

#define RMI_REG_DESC_PRESENSE_BITS	(32 * BITS_PER_BYTE)
#define RMI_REG_DESC_SUBPACKET_BITS	(37 * BITS_PER_BYTE)

/* describes a single packet register */
struct rmi_register_desc_item {
	u16 reg;
	unsigned long reg_size;
	u8 num_subpackets;
	unsigned long subpacket_map[BITS_TO_LONGS(
				RMI_REG_DESC_SUBPACKET_BITS)];
};

/*
 * describes the packet registers for a particular type
 * (ie query, control, data)
 */
struct rmi_register_descriptor {
	unsigned long struct_size;
	unsigned long presense_map[BITS_TO_LONGS(RMI_REG_DESC_PRESENSE_BITS)];
	u8 num_registers;
	struct rmi_register_desc_item *registers;
};

int rmi_read_register_desc(struct rmi_device *d, u16 addr,
				struct rmi_register_descriptor *rdesc);
const struct rmi_register_desc_item *rmi_get_register_desc_item(
				struct rmi_register_descriptor *rdesc, u16 reg);

/*
 * Calculate the total size of all of the registers described in the
 * descriptor.
 */
size_t rmi_register_desc_calc_size(struct rmi_register_descriptor *rdesc);
int rmi_register_desc_calc_reg_offset(
			struct rmi_register_descriptor *rdesc, u16 reg);
bool rmi_register_desc_has_subpacket(const struct rmi_register_desc_item *item,
			u8 subpacket);

bool rmi_is_physical_driver(const struct device_driver *);
int rmi_register_physical_driver(void);
void rmi_unregister_physical_driver(void);
void rmi_free_function_list(struct rmi_device *rmi_dev);
struct rmi_function *rmi_find_function(struct rmi_device *rmi_dev, u8 number);
int rmi_enable_sensor(struct rmi_device *rmi_dev);
int rmi_scan_pdt(struct rmi_device *rmi_dev, void *ctx,
		 int (*callback)(struct rmi_device *rmi_dev, void *ctx,
		 const struct pdt_entry *entry));
int rmi_probe_interrupts(struct rmi_driver_data *data);
void rmi_enable_irq(struct rmi_device *rmi_dev, bool clear_wake);
void rmi_disable_irq(struct rmi_device *rmi_dev, bool enable_wake);
int rmi_init_functions(struct rmi_driver_data *data);
int rmi_initial_reset(struct rmi_device *rmi_dev, void *ctx,
		      const struct pdt_entry *pdt);

const char *rmi_f01_get_product_ID(struct rmi_function *fn);

#ifdef CONFIG_RMI4_F03
int rmi_f03_overwrite_button(struct rmi_function *fn, unsigned int button,
			     int value);
void rmi_f03_commit_buttons(struct rmi_function *fn);
#else
static inline int rmi_f03_overwrite_button(struct rmi_function *fn,
					   unsigned int button, int value)
{
	return 0;
}
static inline void rmi_f03_commit_buttons(struct rmi_function *fn) {}
#endif

#ifdef CONFIG_RMI4_F34
int rmi_f34_create_sysfs(struct rmi_device *rmi_dev);
void rmi_f34_remove_sysfs(struct rmi_device *rmi_dev);
#else
static inline int rmi_f34_create_sysfs(struct rmi_device *rmi_dev)
{
	return 0;
}

static inline void rmi_f34_remove_sysfs(struct rmi_device *rmi_dev)
{
}
#endif /* CONFIG_RMI_F34 */

extern struct rmi_function_handler rmi_f01_handler;
extern struct rmi_function_handler rmi_f03_handler;
extern struct rmi_function_handler rmi_f11_handler;
extern struct rmi_function_handler rmi_f12_handler;
extern struct rmi_function_handler rmi_f1a_handler;
extern struct rmi_function_handler rmi_f21_handler;
extern struct rmi_function_handler rmi_f30_handler;
extern struct rmi_function_handler rmi_f34_handler;
extern struct rmi_function_handler rmi_f3a_handler;
extern struct rmi_function_handler rmi_f54_handler;
extern struct rmi_function_handler rmi_f55_handler;
#endif
