/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2010 Benjamin Herrenschmidt, IBM Corp
 *                <benh@kernel.crashing.org>
 *     and        David Gibson, IBM Corporation.
 */

#ifndef _ASM_POWERPC_SCOM_H
#define _ASM_POWERPC_SCOM_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#ifdef CONFIG_PPC_SCOM

/*
 * The SCOM bus is a sideband bus used for accessing various internal
 * registers of the processor or the chipset. The implementation details
 * differ between processors and platforms, and the access method as
 * well.
 *
 * This API allows to "map" ranges of SCOM register numbers associated
 * with a given SCOM controller. The later must be represented by a
 * device node, though some implementations might support NULL if there
 * is no possible ambiguity
 *
 * Then, scom_read/scom_write can be used to accesses registers inside
 * that range. The argument passed is a register number relative to
 * the beginning of the range mapped.
 */

typedef void *scom_map_t;

/* Value for an invalid SCOM map */
#define SCOM_MAP_INVALID	(NULL)

/* The scom_controller data structure is what the platform passes
 * to the core code in scom_init, it provides the actual implementation
 * of all the SCOM functions
 */
struct scom_controller {
	scom_map_t (*map)(struct device_node *ctrl_dev, u64 reg, u64 count);
	void (*unmap)(scom_map_t map);

	int (*read)(scom_map_t map, u64 reg, u64 *value);
	int (*write)(scom_map_t map, u64 reg, u64 value);
};

extern const struct scom_controller *scom_controller;

/**
 * scom_init - Initialize the SCOM backend, called by the platform
 * @controller: The platform SCOM controller
 */
static inline void scom_init(const struct scom_controller *controller)
{
	scom_controller = controller;
}

/**
 * scom_map_ok - Test is a SCOM mapping is successful
 * @map: The result of scom_map to test
 */
static inline int scom_map_ok(scom_map_t map)
{
	return map != SCOM_MAP_INVALID;
}

/**
 * scom_map - Map a block of SCOM registers
 * @ctrl_dev: Device node of the SCOM controller
 *            some implementations allow NULL here
 * @reg: first SCOM register to map
 * @count: Number of SCOM registers to map
 */

static inline scom_map_t scom_map(struct device_node *ctrl_dev,
				  u64 reg, u64 count)
{
	return scom_controller->map(ctrl_dev, reg, count);
}

/**
 * scom_find_parent - Find the SCOM controller for a device
 * @dev: OF node of the device
 *
 * This is not meant for general usage, but in combination with
 * scom_map() allows to map registers not represented by the
 * device own scom-reg property. Useful for applying HW workarounds
 * on things not properly represented in the device-tree for example.
 */
struct device_node *scom_find_parent(struct device_node *dev);


/**
 * scom_map_device - Map a device's block of SCOM registers
 * @dev: OF node of the device
 * @index: Register bank index (index in "scom-reg" property)
 *
 * This function will use the device-tree binding for SCOM which
 * is to follow "scom-parent" properties until it finds a node with
 * a "scom-controller" property to find the controller. It will then
 * use the "scom-reg" property which is made of reg/count pairs,
 * each of them having a size defined by the controller's #scom-cells
 * property
 */
extern scom_map_t scom_map_device(struct device_node *dev, int index);


/**
 * scom_unmap - Unmap a block of SCOM registers
 * @map: Result of scom_map is to be unmapped
 */
static inline void scom_unmap(scom_map_t map)
{
	if (scom_map_ok(map))
		scom_controller->unmap(map);
}

/**
 * scom_read - Read a SCOM register
 * @map: Result of scom_map
 * @reg: Register index within that map
 * @value: Updated with the value read
 *
 * Returns 0 (success) or a negative error code
 */
static inline int scom_read(scom_map_t map, u64 reg, u64 *value)
{
	int rc;

	rc = scom_controller->read(map, reg, value);
	if (rc)
		*value = 0xfffffffffffffffful;
	return rc;
}

/**
 * scom_write - Write to a SCOM register
 * @map: Result of scom_map
 * @reg: Register index within that map
 * @value: Value to write
 *
 * Returns 0 (success) or a negative error code
 */
static inline int scom_write(scom_map_t map, u64 reg, u64 value)
{
	return scom_controller->write(map, reg, value);
}


#endif /* CONFIG_PPC_SCOM */
#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_SCOM_H */
