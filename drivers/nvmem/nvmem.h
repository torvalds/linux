/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _DRIVERS_NVMEM_H
#define _DRIVERS_NVMEM_H

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>

struct nvmem_device {
	const char		*name;
	struct module		*owner;
	struct device		dev;
	int			stride;
	int			word_size;
	int			id;
	struct kref		refcnt;
	size_t			size;
	bool			read_only;
	int			flags;
	struct bin_attribute	eeprom;
	struct device		*base_dev;
	struct list_head	cells;
	nvmem_reg_read_t	reg_read;
	nvmem_reg_write_t	reg_write;
	void *priv;
};

#define to_nvmem_device(d) container_of(d, struct nvmem_device, dev)
#define FLAG_COMPAT		BIT(0)

#ifdef CONFIG_NVMEM_SYSFS
const struct attribute_group **nvmem_sysfs_get_groups(
					struct nvmem_device *nvmem,
					const struct nvmem_config *config);
int nvmem_sysfs_setup_compat(struct nvmem_device *nvmem,
			      const struct nvmem_config *config);
void nvmem_sysfs_remove_compat(struct nvmem_device *nvmem,
			      const struct nvmem_config *config);
#else
static inline const struct attribute_group **nvmem_sysfs_get_groups(
					struct nvmem_device *nvmem,
					const struct nvmem_config *config)
{
	return NULL;
}

static inline int nvmem_sysfs_setup_compat(struct nvmem_device *nvmem,
				      const struct nvmem_config *config)
{
	return -ENOSYS;
}
static inline void nvmem_sysfs_remove_compat(struct nvmem_device *nvmem,
			      const struct nvmem_config *config)
{
}
#endif /* CONFIG_NVMEM_SYSFS */

#endif /* _DRIVERS_NVMEM_H */
