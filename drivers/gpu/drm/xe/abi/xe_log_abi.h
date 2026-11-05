/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _ABI_XE_LOG_ABI_H_
#define _ABI_XE_LOG_ABI_H_

#include <linux/bits.h>
#include <linux/bitfield.h>

#include "abi/xe_sigid_abi.h"

/**
 * enum xe_log_component_bits - bits for components structure definitions
 *
 * Component identifiers are structured based on::
 *
 *     COMPONENT = CLASS(8b).TYPE(8b)
 *
 * and the structure looks like this::
 *
 *     ├── SYSTEM(0)
 *     │   └── ...
 *     ├── DRIVER(1)
 *     │   └── ...
 *     ├── FEATURE(2)
 *     │   └── ...
 *     ├── FIRMWARE(4)
 *     │   └── ...
 *     └── HARDWARE(8)
 *         └── ...
 *
 * Examples::
 *
 *     COMPONENT(0.type) = SYSTEM.type = system component
 *     COMPONENT(1.type) = DRIVER.type = driver core component
 *     COMPONENT(3.type) = DRIVER_FEATURE.type = driver feature
 *     COMPONENT(5.type) = DRIVER_FIRMWARE.type = firmware driver component
 *     COMPONENT(9.type) = DRIVER_HARDWARE.type = hardware driver component
 *
 */
enum xe_log_component_bits {
	/* private: */
	XE_LOG_COMPONENT_CLASS_MASK = GENMASK_U16(7, 0),
	XE_LOG_COMPONENT_TYPE_MASK = GENMASK_U16(15, 8),
	/* private: component classes */
	XE_LOG_COMPONENT_CLASS_SYSTEM = 0u,
	XE_LOG_COMPONENT_CLASS_DRIVER = 1u,
	XE_LOG_COMPONENT_CLASS_FEATURE = 2u,
	XE_LOG_COMPONENT_CLASS_FIRMWARE = 4u,
	XE_LOG_COMPONENT_CLASS_HARDWARE = 8u,
	XE_LOG_COMPONENT_CLASS_DRIVER_FEATURE = XE_LOG_COMPONENT_CLASS_DRIVER |
						XE_LOG_COMPONENT_CLASS_FEATURE,
	XE_LOG_COMPONENT_CLASS_DRIVER_FIRMWARE = XE_LOG_COMPONENT_CLASS_DRIVER |
						 XE_LOG_COMPONENT_CLASS_FIRMWARE,
	XE_LOG_COMPONENT_CLASS_DRIVER_HARDWARE = XE_LOG_COMPONENT_CLASS_DRIVER |
						 XE_LOG_COMPONENT_CLASS_HARDWARE,
	/* private: reserved identifiers */
	XE_LOG_COMPONENT_NONE = 0u,
};

#define MAKE_XE_LOG_COMPONENT(_CLASS, type) \
	(FIELD_PREP_CONST(XE_LOG_COMPONENT_CLASS_MASK, \
			  XE_LOG_COMPONENT_CLASS_##_CLASS) | \
	 FIELD_PREP_CONST(XE_LOG_COMPONENT_TYPE_MASK, (type)))

/**
 * enum xe_log_location_bits - bits for location structure definitions
 *
 * Location identifiers are structured based on::
 *
 *     LOCATION = TYPE(8b).ID(8b)
 *
 * and the structure looks like this::
 *
 *     ├── DEVICE(0)
 *     │   └── MBZ(0)
 *     ├── TILE(1)
 *     │   ├── Tile0(0)
 *     │   ├── ...
 *     │   └── TileN(n)
 *     ├── GT(1)
 *     │   ├── GT0(0)
 *     │   ├── ...
 *     │   └── GTn(n)
 *     └── ...
 *
 * Examples::
 *
 *     LOCATION(0.0) = NONE
 *     LOCATION(1.0) = DEVICE.0 = "Device"
 *     LOCATION(2.1) = TILE.1 = "Tile1"
 *     LOCATION(3.2) = GT.2 = "GT2"
 *
 */
enum xe_log_location_bits {
	/* private: */
	XE_LOG_LOCATION_TYPE_MASK = GENMASK_U16(7, 0),
	XE_LOG_LOCATION_ID_MASK = GENMASK_U16(15, 8),
	/* private: location types */
	XE_LOG_LOCATION_TYPE_DEVICE = 1u,
	XE_LOG_LOCATION_TYPE_TILE = 2u,
	XE_LOG_LOCATION_TYPE_GT = 3u,
	/* private: reserved identifiers */
	XE_LOG_LOCATION_NONE = 0u,
};

#define PREP_XE_LOG_LOCATION(type, id) \
	(FIELD_PREP(XE_LOG_LOCATION_TYPE_MASK, (type)) | \
	 FIELD_PREP(XE_LOG_LOCATION_ID_MASK, (id)))

#define MAKE_XE_LOG_LOCATION(_TYPE, id) \
	PREP_XE_LOG_LOCATION(XE_LOG_LOCATION_TYPE_##_TYPE, (id))

/**
 * DEFINE_XE_LOG_COMPONENTS() - Define log components.
 * @define: name of the inner macro to expand.
 *
 * Use this super macro to define custom code for the log components.
 * The following parameters are available for each component::
 *
 *     define(CLASS, ID, TAG, SIGID, NAME)
 *
 * where:
 *
 *     @CLASS is the component class name (without the XE_LOG_COMPONENT_CLASS_ prefix)
 *     @ID is the unique component identifier within @CLASS
 *     @TAG is unique component tag (across all components)
 *     @SIGID is the default xe_sigid for the component (without the XE_SIGID_ prefix)
 */
#define DEFINE_XE_LOG_COMPONENTS(define) \
	DEFINE_XE_LOG_SOFTWARE_COMPONENTS(define) \
	DEFINE_XE_LOG_HARDWARE_COMPONENTS(define)

#define DEFINE_XE_LOG_SOFTWARE_COMPONENTS(define) \
	/* */									\
	define(SYSTEM, 1, PCI, IO_BUS, "Linux PCI Subsystem")			\
	define(SYSTEM, 2, DRM, SW, "DRM")					\
	/* */									\
	define(DRIVER, 1, XE, SW, "Xe Driver")					\
	define(DRIVER, 2, PROBE, PROBE, "Driver Initialization")		\
	define(DRIVER, 3, WEDGED, WEDGED, "Device Malfunction")			\
	define(DRIVER, 4, RTP, SW, "Register Table Processing")			\
	define(DRIVER, 5, WA, SW, "Workarounds")				\
	define(DRIVER, 6, PAGEFAULT, MEM_FAULT, "Page Fault")			\
	define(DRIVER, 7, GUCSUBMIT, GT_TDR, "GuC Submission")			\
	/* */									\
	define(DRIVER_HARDWARE, 1, REGS, IO_BUS, "Registers")			\
	define(DRIVER_HARDWARE, 2, GGTT, IO_BUS, "Global GTT")			\
	define(DRIVER_HARDWARE, 3, GT, GT_TDR, "Graphics Technology")		\
	define(DRIVER_HARDWARE, 4, LMTT, IO_BUS, "LMEM Translation Table")	\
	define(DRIVER_HARDWARE, 5, MEMIRQ, IO_BUS, "Memory Based IRQ")		\
	/* */									\
	define(DRIVER_FEATURE, 1, PF, SW, "SR-IOV Physical Function")		\
	define(DRIVER_FEATURE, 2, VF, SW, "SR-IOV Virtual Function")		\
	define(DRIVER_FEATURE, 3, SURVIVABILITY, SURVIVABILITY, "Survivability") \
	define(DRIVER_FEATURE, 4, RAS, SW, "Reliability, Accessibility, Serviceability") \
	/* */									\
	define(DRIVER_FIRMWARE, 1, GUC, RUNTIME_FW, "GuC")			\
	define(DRIVER_FIRMWARE, 2, HUC, RUNTIME_FW, "HuC")			\
	define(DRIVER_FIRMWARE, 3, GSC, RUNTIME_FW, "GSC")			\
	define(DRIVER_FIRMWARE, 16, PCODE, DEVICE_FW, "PCode")			\
	define(DRIVER_FIRMWARE, 17, SYSCTRL, DEVICE_FW, "System Controller")	\

#define DEFINE_XE_LOG_HARDWARE_COMPONENTS(define) \
	define(HARDWARE, 1, DEVICE_MEMORY, DEVICE_MEMORY, "Device Memory")	\
	define(HARDWARE, 2, CORE_COMPUTE, CORE_COMPUTE, "Core Compute")		\
	/*     HARDWARE, 3, RESERVED */						\
	define(HARDWARE, 4, PCIE, PCIE, "PCIe Interface")			\
	define(HARDWARE, 5, FABRIC, FABRIC, "Fabric")				\
	define(HARDWARE, 6, SOC_INTERNAL, SOC_INTERNAL, "SoC Internal")		\
	/* eod */

/**
 * enum xe_log_component_tags - TAGs of all supported components
 */
enum xe_log_component_tags {
	/* private: */
#define MAKE_XE_LOG_COMPONENT_ENUM(_CLASS, _ID, _TAG, _SIG, _NAME) \
	XE_LOG_COMPONENT_##_TAG = MAKE_XE_LOG_COMPONENT(_CLASS, (_ID)), \
	XE_LOG_COMPONENT_##_CLASS##_##_ID = XE_LOG_COMPONENT_##_TAG, \
	/* eod */
	DEFINE_XE_LOG_COMPONENTS(MAKE_XE_LOG_COMPONENT_ENUM)
#undef MAKE_XE_LOG_COMPONENT_ENUM
};

/**
 * enum xe_log_component_sigids - SIGIDs of all supported components
 */
enum xe_log_component_sigids {
	/* private: */
#define MAKE_XE_LOG_COMPONENT_SIGID(_CLASS, _ID, _TAG, _SIG, _NAME) \
	XE_LOG_COMPONENT_##_TAG##_SIGID = XE_SIGID_##_SIG, \
	/* eod */
	DEFINE_XE_LOG_COMPONENTS(MAKE_XE_LOG_COMPONENT_SIGID)
#undef MAKE_XE_LOG_COMPONENT_SIGID
};

#endif
