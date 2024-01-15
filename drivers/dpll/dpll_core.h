/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (c) 2023 Meta Platforms, Inc. and affiliates
 *  Copyright (c) 2023 Intel and affiliates
 */

#ifndef __DPLL_CORE_H__
#define __DPLL_CORE_H__

#include <linux/dpll.h>
#include <linux/list.h>
#include <linux/refcount.h>
#include "dpll_nl.h"

#define DPLL_REGISTERED		XA_MARK_1

/**
 * struct dpll_device - stores DPLL device internal data
 * @id:			unique id number for device given by dpll subsystem
 * @device_idx:		id given by dev driver
 * @clock_id:		unique identifier (clock_id) of a dpll
 * @module:		module of creator
 * @type:		type of a dpll
 * @pin_refs:		stores pins registered within a dpll
 * @refcount:		refcount
 * @registration_list:	list of registered ops and priv data of dpll owners
 **/
struct dpll_device {
	u32 id;
	u32 device_idx;
	u64 clock_id;
	struct module *module;
	enum dpll_type type;
	struct xarray pin_refs;
	refcount_t refcount;
	struct list_head registration_list;
};

/**
 * struct dpll_pin - structure for a dpll pin
 * @id:			unique id number for pin given by dpll subsystem
 * @pin_idx:		index of a pin given by dev driver
 * @clock_id:		clock_id of creator
 * @module:		module of creator
 * @dpll_refs:		hold referencees to dplls pin was registered with
 * @parent_refs:	hold references to parent pins pin was registered with
 * @prop:		pointer to pin properties given by registerer
 * @rclk_dev_name:	holds name of device when pin can recover clock from it
 * @refcount:		refcount
 **/
struct dpll_pin {
	u32 id;
	u32 pin_idx;
	u64 clock_id;
	struct module *module;
	struct xarray dpll_refs;
	struct xarray parent_refs;
	const struct dpll_pin_properties *prop;
	refcount_t refcount;
};

/**
 * struct dpll_pin_ref - structure for referencing either dpll or pins
 * @dpll:		pointer to a dpll
 * @pin:		pointer to a pin
 * @registration_list:	list of ops and priv data registered with the ref
 * @refcount:		refcount
 **/
struct dpll_pin_ref {
	union {
		struct dpll_device *dpll;
		struct dpll_pin *pin;
	};
	struct list_head registration_list;
	refcount_t refcount;
};

void *dpll_priv(struct dpll_device *dpll);
void *dpll_pin_on_dpll_priv(struct dpll_device *dpll, struct dpll_pin *pin);
void *dpll_pin_on_pin_priv(struct dpll_pin *parent, struct dpll_pin *pin);

const struct dpll_device_ops *dpll_device_ops(struct dpll_device *dpll);
struct dpll_device *dpll_device_get_by_id(int id);
const struct dpll_pin_ops *dpll_pin_ops(struct dpll_pin_ref *ref);
struct dpll_pin_ref *dpll_xa_ref_dpll_first(struct xarray *xa_refs);
extern struct xarray dpll_device_xa;
extern struct xarray dpll_pin_xa;
extern struct mutex dpll_lock;
#endif
