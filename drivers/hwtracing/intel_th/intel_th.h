/*
 * Intel(R) Trace Hub data structures
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __INTEL_TH_H__
#define __INTEL_TH_H__

/* intel_th_device device types */
enum {
	/* Devices that generate trace data */
	INTEL_TH_SOURCE = 0,
	/* Output ports (MSC, PTI) */
	INTEL_TH_OUTPUT,
	/* Switch, the Global Trace Hub (GTH) */
	INTEL_TH_SWITCH,
};

/**
 * struct intel_th_output - descriptor INTEL_TH_OUTPUT type devices
 * @port:	output port number, assigned by the switch
 * @type:	GTH_{MSU,CTP,PTI}
 * @scratchpad:	scratchpad bits to flag when this output is enabled
 * @multiblock:	true for multiblock output configuration
 * @active:	true when this output is enabled
 *
 * Output port descriptor, used by switch driver to tell which output
 * port this output device corresponds to. Filled in at output device's
 * probe time by switch::assign(). Passed from output device driver to
 * switch related code to enable/disable its port.
 */
struct intel_th_output {
	int		port;
	unsigned int	type;
	unsigned int	scratchpad;
	bool		multiblock;
	bool		active;
};

/**
 * struct intel_th_device - device on the intel_th bus
 * @dev:		device
 * @resource:		array of resources available to this device
 * @num_resources:	number of resources in @resource array
 * @type:		INTEL_TH_{SOURCE,OUTPUT,SWITCH}
 * @id:			device instance or -1
 * @output:		output descriptor for INTEL_TH_OUTPUT devices
 * @name:		device name to match the driver
 */
struct intel_th_device {
	struct device	dev;
	struct resource	*resource;
	unsigned int	num_resources;
	unsigned int	type;
	int		id;

	/* INTEL_TH_OUTPUT specific */
	struct intel_th_output	output;

	char		name[];
};

#define to_intel_th_device(_d)				\
	container_of((_d), struct intel_th_device, dev)

/**
 * intel_th_device_get_resource() - obtain @num'th resource of type @type
 * @thdev:	the device to search the resource for
 * @type:	resource type
 * @num:	number of the resource
 */
static inline struct resource *
intel_th_device_get_resource(struct intel_th_device *thdev, unsigned int type,
			     unsigned int num)
{
	int i;

	for (i = 0; i < thdev->num_resources; i++)
		if (resource_type(&thdev->resource[i]) == type && !num--)
			return &thdev->resource[i];

	return NULL;
}

/**
 * intel_th_output_assigned() - if an output device is assigned to a switch port
 * @thdev:	the output device
 *
 * Return:	true if the device is INTEL_TH_OUTPUT *and* is assigned a port
 */
static inline bool
intel_th_output_assigned(struct intel_th_device *thdev)
{
	return thdev->type == INTEL_TH_OUTPUT &&
		thdev->output.port >= 0;
}

/**
 * struct intel_th_driver - driver for an intel_th_device device
 * @driver:	generic driver
 * @probe:	probe method
 * @remove:	remove method
 * @assign:	match a given output type device against available outputs
 * @unassign:	deassociate an output type device from an output port
 * @enable:	enable tracing for a given output device
 * @disable:	disable tracing for a given output device
 * @fops:	file operations for device nodes
 *
 * Callbacks @probe and @remove are required for all device types.
 * Switch device driver needs to fill in @assign, @enable and @disable
 * callbacks.
 */
struct intel_th_driver {
	struct device_driver	driver;
	int			(*probe)(struct intel_th_device *thdev);
	void			(*remove)(struct intel_th_device *thdev);
	/* switch (GTH) ops */
	int			(*assign)(struct intel_th_device *thdev,
					  struct intel_th_device *othdev);
	void			(*unassign)(struct intel_th_device *thdev,
					    struct intel_th_device *othdev);
	void			(*enable)(struct intel_th_device *thdev,
					  struct intel_th_output *output);
	void			(*disable)(struct intel_th_device *thdev,
					   struct intel_th_output *output);
	/* output ops */
	void			(*irq)(struct intel_th_device *thdev);
	int			(*activate)(struct intel_th_device *thdev);
	void			(*deactivate)(struct intel_th_device *thdev);
	/* file_operations for those who want a device node */
	const struct file_operations *fops;

	/* source ops */
	int			(*set_output)(struct intel_th_device *thdev,
					      unsigned int master);
};

#define to_intel_th_driver(_d)					\
	container_of((_d), struct intel_th_driver, driver)

static inline struct intel_th_device *
to_intel_th_hub(struct intel_th_device *thdev)
{
	struct device *parent = thdev->dev.parent;

	if (!parent)
		return NULL;

	return to_intel_th_device(parent);
}

struct intel_th *
intel_th_alloc(struct device *dev, struct resource *devres,
	       unsigned int ndevres, int irq);
void intel_th_free(struct intel_th *th);

int intel_th_driver_register(struct intel_th_driver *thdrv);
void intel_th_driver_unregister(struct intel_th_driver *thdrv);

int intel_th_trace_enable(struct intel_th_device *thdev);
int intel_th_trace_disable(struct intel_th_device *thdev);
int intel_th_set_output(struct intel_th_device *thdev,
			unsigned int master);

enum {
	TH_MMIO_CONFIG = 0,
	TH_MMIO_SW = 2,
	TH_MMIO_END,
};

#define TH_SUBDEVICE_MAX	6
#define TH_POSSIBLE_OUTPUTS	8
#define TH_CONFIGURABLE_MASTERS 256
#define TH_MSC_MAX		2

/**
 * struct intel_th - Intel TH controller
 * @dev:	driver core's device
 * @thdev:	subdevices
 * @hub:	"switch" subdevice (GTH)
 * @id:		this Intel TH controller's device ID in the system
 * @major:	device node major for output devices
 */
struct intel_th {
	struct device		*dev;

	struct intel_th_device	*thdev[TH_SUBDEVICE_MAX];
	struct intel_th_device	*hub;

	int			id;
	int			major;
#ifdef CONFIG_INTEL_TH_DEBUG
	struct dentry		*dbg;
#endif
};

/*
 * Register windows
 */
enum {
	/* Global Trace Hub (GTH) */
	REG_GTH_OFFSET		= 0x0000,
	REG_GTH_LENGTH		= 0x2000,

	/* Software Trace Hub (STH) [0x4000..0x4fff] */
	REG_STH_OFFSET		= 0x4000,
	REG_STH_LENGTH		= 0x2000,

	/* Memory Storage Unit (MSU) [0xa0000..0xa1fff] */
	REG_MSU_OFFSET		= 0xa0000,
	REG_MSU_LENGTH		= 0x02000,

	/* Internal MSU trace buffer [0x80000..0x9ffff] */
	BUF_MSU_OFFSET		= 0x80000,
	BUF_MSU_LENGTH		= 0x20000,

	/* PTI output == same window as GTH */
	REG_PTI_OFFSET		= REG_GTH_OFFSET,
	REG_PTI_LENGTH		= REG_GTH_LENGTH,

	/* DCI Handler (DCIH) == some window as MSU */
	REG_DCIH_OFFSET		= REG_MSU_OFFSET,
	REG_DCIH_LENGTH		= REG_MSU_LENGTH,
};

/*
 * GTH, output ports configuration
 */
enum {
	GTH_NONE = 0,
	GTH_MSU,	/* memory/usb */
	GTH_CTP,	/* Common Trace Port */
	GTH_PTI = 4,	/* MIPI-PTI */
};

/*
 * Scratchpad bits: tell firmware and external debuggers
 * what we are up to.
 */
enum {
	/* Memory is the primary destination */
	SCRPD_MEM_IS_PRIM_DEST		= BIT(0),
	/* XHCI DbC is the primary destination */
	SCRPD_DBC_IS_PRIM_DEST		= BIT(1),
	/* PTI is the primary destination */
	SCRPD_PTI_IS_PRIM_DEST		= BIT(2),
	/* BSSB is the primary destination */
	SCRPD_BSSB_IS_PRIM_DEST		= BIT(3),
	/* PTI is the alternate destination */
	SCRPD_PTI_IS_ALT_DEST		= BIT(4),
	/* BSSB is the alternate destination */
	SCRPD_BSSB_IS_ALT_DEST		= BIT(5),
	/* DeepSx exit occurred */
	SCRPD_DEEPSX_EXIT		= BIT(6),
	/* S4 exit occurred */
	SCRPD_S4_EXIT			= BIT(7),
	/* S5 exit occurred */
	SCRPD_S5_EXIT			= BIT(8),
	/* MSU controller 0/1 is enabled */
	SCRPD_MSC0_IS_ENABLED		= BIT(9),
	SCRPD_MSC1_IS_ENABLED		= BIT(10),
	/* Sx exit occurred */
	SCRPD_SX_EXIT			= BIT(11),
	/* Trigger Unit is enabled */
	SCRPD_TRIGGER_IS_ENABLED	= BIT(12),
	SCRPD_ODLA_IS_ENABLED		= BIT(13),
	SCRPD_SOCHAP_IS_ENABLED		= BIT(14),
	SCRPD_STH_IS_ENABLED		= BIT(15),
	SCRPD_DCIH_IS_ENABLED		= BIT(16),
	SCRPD_VER_IS_ENABLED		= BIT(17),
	/* External debugger is using Intel TH */
	SCRPD_DEBUGGER_IN_USE		= BIT(24),
};

#endif
