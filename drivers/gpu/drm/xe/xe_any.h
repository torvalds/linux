/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_ANY_H_
#define _XE_ANY_H_

#include "xe_device.h"

#define __xe_any_to_self_assoc(type, any) \
	const type * : (any), \
	type * : (any)

/**
 * xe_any_if_type() - Get the pointer only if it is @type pointer.
 * @any: any pointer
 * @type: data type to look for
 *
 * Return: the @type pointer or NULL.
 */
#define xe_any_if_type(any, type)						\
	_Generic((any),								\
		 __xe_any_to_self_assoc(type, (any)),				\
		 default : NULL)

/**
 * xe_any_if_gt() - Get the pointer only if it is &xe_gt.
 * @any: any pointer
 *
 * Return: the @xe_gt pointer or NULL.
 */
#define xe_any_if_gt(any)	xe_any_if_type((any), struct xe_gt)

/**
 * xe_any_if_tile() - Get the pointer only if it is &xe_tile.
 * @any: any pointer
 *
 * Return: the @xe_tile pointer or NULL.
 */
#define xe_any_if_tile(any)	xe_any_if_type((any), struct xe_tile)

/**
 * xe_any_if_xe() - Get the pointer only if it is &xe_device.
 * @any: any pointer
 *
 * Return: the @xe_device pointer or NULL.
 */
#define xe_any_if_xe(any)	xe_any_if_type((any), struct xe_device)

/**
 * xe_any_if_pdev() - Get the pointer only if it is &pci_dev.
 * @any: any pointer
 *
 * Return: the @pci_dev pointer or NULL.
 */
#define xe_any_if_pdev(any)	xe_any_if_type((any), struct pci_dev)

#define __xe_any_to_other_assoc(const, from, other, p) \
	const struct from * : __##from##_to_##other((const struct from *)(p))

#define __xe_tile_to_xe_device(p)	tile_to_xe(p)
#define __xe_gt_to_xe_device(p)		gt_to_xe(p)
#define __pci_dev_to_xe_device(p)	pdev_to_xe_device(p)
#define __device_to_xe_device(p)	kdev_to_xe_device(p)
#define __drm_device_to_xe_device(p)	to_xe_device(p)
#define __pci_dev_to_device(p)		(&(p)->dev)

/**
 * xe_any_to_xe() - Obtain the &xe_device pointer.
 * @any: the &pci_dev or the &xe_device or &xe_tile or &xe_gt pointer
 *
 * Return: the @xe_device pointer or backpointer.
 */
#define xe_any_to_xe(any)							\
	_Generic((any),								\
		 __xe_any_to_self_assoc(struct xe_device, (any)),		\
		 __xe_any_to_other_assoc(/* */, xe_tile, xe_device, (any)),	\
		 __xe_any_to_other_assoc(const, xe_tile, xe_device, (any)),	\
		 __xe_any_to_other_assoc(/* */, xe_gt, xe_device, (any)),	\
		 __xe_any_to_other_assoc(const, xe_gt, xe_device, (any)),	\
		 __xe_any_to_other_assoc(, drm_device, xe_device, (any)),	\
		 __xe_any_to_other_assoc(, pci_dev, xe_device, (any)),		\
		 __xe_any_to_other_assoc(, device, xe_device, (any)))

/**
 * xe_any_to_drm() - Obtain the &drm_device pointer.
 * @any: the &pci_dev or the &xe_device or &xe_tile or &xe_gt pointer
 *
 * Return: the @drm_device pointer or backpointer.
 */
#define xe_any_to_drm(any)							\
	_Generic((any),								\
		 __xe_any_to_self_assoc(struct drm_device, (any)),		\
		 default : &xe_any_to_xe(any)->drm)

/**
 * xe_any_to_dev() - Obtain the &device pointer.
 * @any: the &pci_dev or the &xe_device or &xe_tile or &xe_gt pointer
 *
 * Return: the @device pointer or backpointer.
 */
#define xe_any_to_dev(any)							\
	_Generic((any),								\
		 __xe_any_to_self_assoc(struct device, (any)),			\
		 __xe_any_to_other_assoc(, pci_dev, device, (any)),		\
		 default : xe_any_to_drm(any)->dev)

/**
 * xe_any_to_pdev() - Obtain the &pci_dev pointer.
 * @any: the &pci_dev or the &xe_device or &xe_tile or &xe_gt pointer
 *
 * Return: the @pci_dev pointer or backpointer.
 */
#define xe_any_to_pdev(any)							\
	_Generic((any),								\
		 __xe_any_to_self_assoc(struct pci_dev, (any)),			\
		 default : to_pci_dev(xe_any_to_dev(any)))

#define __xe_tile_to_id(p)		((p)->id)
#define __xe_gt_to_id(p)		((p)->info.id)

/**
 * xe_any_id() - Get the identifier of the underlying object.
 * @any: the &pci_dev or the &xe_device or &xe_tile or &xe_gt pointer
 *
 * Return: the identifier of the object, or 0 if not applicable/available.
 */
#define xe_any_id(any)								\
	_Generic((any),								\
		 __xe_any_to_other_assoc(/* */, xe_tile, id, (any)),		\
		 __xe_any_to_other_assoc(const, xe_tile, id, (any)),		\
		 __xe_any_to_other_assoc(/* */, xe_gt, id, (any)),		\
		 __xe_any_to_other_assoc(const, xe_gt, id, (any)),		\
		 default : 0)

#endif
