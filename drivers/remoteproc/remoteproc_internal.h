/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Remote processor framework
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 */

#ifndef REMOTEPROC_INTERNAL_H
#define REMOTEPROC_INTERNAL_H

#include <linux/irqreturn.h>
#include <linux/firmware.h>

struct rproc;

struct rproc_debug_trace {
	struct rproc *rproc;
	struct dentry *tfile;
	struct list_head node;
	struct rproc_mem_entry trace_mem;
};

/**
 * struct rproc_vdev_data - remoteproc virtio device data
 * @rsc_offset: offset of the vdev's resource entry
 * @id: virtio device id (as in virtio_ids.h)
 * @index: vdev position versus other vdev declared in resource table
 * @rsc: pointer to the vdev resource entry. Valid only during vdev init as
 *       the resource can be cached by rproc.
 */
struct rproc_vdev_data {
	u32 rsc_offset;
	unsigned int id;
	u32 index;
	struct fw_rsc_vdev *rsc;
};

static inline bool rproc_has_feature(struct rproc *rproc, unsigned int feature)
{
	return test_bit(feature, rproc->features);
}

static inline int rproc_set_feature(struct rproc *rproc, unsigned int feature)
{
	if (feature >= RPROC_MAX_FEATURES)
		return -EINVAL;

	set_bit(feature, rproc->features);

	return 0;
}

/* from remoteproc_core.c */
void rproc_release(struct kref *kref);
int rproc_of_parse_firmware(struct device *dev, int index,
			    const char **fw_name);

/* from remoteproc_virtio.c */
irqreturn_t rproc_vq_interrupt(struct rproc *rproc, int vq_id);

/* from remoteproc_debugfs.c */
void rproc_remove_trace_file(struct dentry *tfile);
struct dentry *rproc_create_trace_file(const char *name, struct rproc *rproc,
				       struct rproc_debug_trace *trace);
void rproc_delete_debug_dir(struct rproc *rproc);
void rproc_create_debug_dir(struct rproc *rproc);
void rproc_init_debugfs(void);
void rproc_exit_debugfs(void);

/* from remoteproc_sysfs.c */
extern const struct class rproc_class;
int rproc_init_sysfs(void);
void rproc_exit_sysfs(void);

#ifdef CONFIG_REMOTEPROC_CDEV
void rproc_init_cdev(void);
void rproc_exit_cdev(void);
int rproc_char_device_add(struct rproc *rproc);
void rproc_char_device_remove(struct rproc *rproc);
#else
static inline void rproc_init_cdev(void)
{
}

static inline void rproc_exit_cdev(void)
{
}

/*
 * The character device interface is an optional feature, if it is not enabled
 * the function should not return an error.
 */
static inline int rproc_char_device_add(struct rproc *rproc)
{
	return 0;
}

static inline void  rproc_char_device_remove(struct rproc *rproc)
{
}
#endif

void rproc_free_vring(struct rproc_vring *rvring);
int rproc_alloc_vring(struct rproc_vdev *rvdev, int i);
int rproc_parse_vring(struct rproc_vdev *rvdev, struct fw_rsc_vdev *rsc, int i);

phys_addr_t rproc_va_to_pa(void *cpu_addr);
int rproc_trigger_recovery(struct rproc *rproc);

int rproc_elf_sanity_check(struct rproc *rproc, const struct firmware *fw);
u64 rproc_elf_get_boot_addr(struct rproc *rproc, const struct firmware *fw);
int rproc_elf_load_segments(struct rproc *rproc, const struct firmware *fw);
int rproc_elf_load_rsc_table(struct rproc *rproc, const struct firmware *fw);
struct resource_table *rproc_elf_find_loaded_rsc_table(struct rproc *rproc,
						       const struct firmware *fw);
struct rproc_mem_entry *
rproc_find_carveout_by_name(struct rproc *rproc, const char *name, ...);
void rproc_add_rvdev(struct rproc *rproc, struct rproc_vdev *rvdev);
void rproc_remove_rvdev(struct rproc_vdev *rvdev);

static inline int rproc_prepare_device(struct rproc *rproc)
{
	if (rproc->ops->prepare)
		return rproc->ops->prepare(rproc);

	return 0;
}

static inline int rproc_unprepare_device(struct rproc *rproc)
{
	if (rproc->ops->unprepare)
		return rproc->ops->unprepare(rproc);

	return 0;
}

static inline int rproc_attach_device(struct rproc *rproc)
{
	if (rproc->ops->attach)
		return rproc->ops->attach(rproc);

	return 0;
}

static inline
int rproc_fw_sanity_check(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->sanity_check)
		return rproc->ops->sanity_check(rproc, fw);

	return 0;
}

static inline
u64 rproc_get_boot_addr(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->get_boot_addr)
		return rproc->ops->get_boot_addr(rproc, fw);

	return 0;
}

static inline
int rproc_load_segments(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->load)
		return rproc->ops->load(rproc, fw);

	return -EINVAL;
}

static inline int rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->parse_fw)
		return rproc->ops->parse_fw(rproc, fw);

	return 0;
}

static inline
int rproc_handle_rsc(struct rproc *rproc, u32 rsc_type, void *rsc, int offset,
		     int avail)
{
	if (rproc->ops->handle_rsc)
		return rproc->ops->handle_rsc(rproc, rsc_type, rsc, offset,
					      avail);

	return RSC_IGNORED;
}

static inline
struct resource_table *rproc_find_loaded_rsc_table(struct rproc *rproc,
						   const struct firmware *fw)
{
	if (rproc->ops->find_loaded_rsc_table)
		return rproc->ops->find_loaded_rsc_table(rproc, fw);

	return NULL;
}

static inline
struct resource_table *rproc_get_loaded_rsc_table(struct rproc *rproc,
						  size_t *size)
{
	if (rproc->ops->get_loaded_rsc_table)
		return rproc->ops->get_loaded_rsc_table(rproc, size);

	return NULL;
}

static inline
bool rproc_u64_fit_in_size_t(u64 val)
{
	if (sizeof(size_t) == sizeof(u64))
		return true;

	return (val <= (size_t) -1);
}

#endif /* REMOTEPROC_INTERNAL_H */
