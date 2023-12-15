/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_NVMEM_INTERNALS_H
#define _LINUX_NVMEM_INTERNALS_H

#include <linux/device.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>

struct nvmem_device {
	struct module		*owner;
	struct device		dev;
	struct list_head	node;
	int			stride;
	int			word_size;
	int			id;
	struct kref		refcnt;
	size_t			size;
	bool			read_only;
	bool			root_only;
	int			flags;
	enum nvmem_type		type;
	struct bin_attribute	eeprom;
	struct device		*base_dev;
	struct list_head	cells;
	void (*fixup_dt_cell_info)(struct nvmem_device *nvmem,
				   struct nvmem_cell_info *cell);
	const struct nvmem_keepout *keepout;
	unsigned int		nkeepout;
	nvmem_reg_read_t	reg_read;
	nvmem_reg_write_t	reg_write;
	struct gpio_desc	*wp_gpio;
	struct nvmem_layout	*layout;
	void *priv;
};

#endif  /* ifndef _LINUX_NVMEM_INTERNALS_H */
