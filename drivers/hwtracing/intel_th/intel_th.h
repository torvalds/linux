/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel(R) Trace Hub data structures
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#ifndef __INTEL_TH_H__
#define __INTEL_TH_H__

#include <linux/irqreturn.h>

/* intel_th_device device types */
enum {
	/* Devices that generate trace data */
	INTEL_TH_SOURCE = 0,
	/* Output ports (MSC, PTI) */
	INTEL_TH_OUTPUT,
	/* Switch, the Global Trace Hub (GTH) */
	INTEL_TH_SWITCH,
};

struct intel_th_device;

/**
 * struct intel_th_output - descriptor INTEL_TH_OUTPUT type devices
 * @port:	output port number, assigned by the switch
 * @type:	GTH_{MSU,CTP,PTI}
 * @scratchpad:	scratchpad bits to flag when this output is enabled
 * @multiblock:	true for multiblock output configuration
 * @active:	true when this output is enabled
 * @wait_empty:	wait for device pipeline to be empty
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
 * struct intel_th_drvdata - describes hardware capabilities and quirks
 * @tscu_enable:	device needs SW to enable time stamping unit
 * @multi_is_broken:	device has multiblock mode is broken
 * @has_mintctl:	device has interrupt control (MINTCTL) register
 * @host_mode_only:	device can only operate in 'host debugger' mode
 */
struct intel_th_drvdata {
	unsigned int	tscu_enable        : 1,
			multi_is_broken    : 1,
			has_mintctl        : 1,
			host_mode_only     : 1;
};

#define INTEL_TH_CAP(_th, _cap) ((_th)->drvdata ? (_th)->drvdata->_cap : 0)

/**
 * struct intel_th_device - device on the intel_th bus
 * @dev:		device
 * @drvdata:		hardware capabilities/quirks
 * @resource:		array of resources available to this device
 * @num_resources:	number of resources in @resource array
 * @type:		INTEL_TH_{SOURCE,OUTPUT,SWITCH}
 * @id:			device instance or -1
 * @host_mode:		Intel TH is controlled by an external debug host
 * @output:		output descriptor for INTEL_TH_OUTPUT devices
 * @name:		device name to match the driver
 */
struct intel_th_device {
	struct device		dev;
	struct intel_th_drvdata *drvdata;
	struct resource		*resource;
	unsigned int		num_resources;
	unsigned int		type;
	int			id;

	/* INTEL_TH_SWITCH specific */
	bool			host_mode;

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

/*
 * GTH, output ports configuration
 */
enum {
	GTH_NONE = 0,
	GTH_MSU,	/* memory/usb */
	GTH_CTP,	/* Common Trace Port */
	GTH_LPP,	/* Low Power Path */
	GTH_PTI,	/* MIPI-PTI */
};

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
		(thdev->output.port >= 0 ||
		 thdev->output.type == GTH_NONE);
}

/**
 * struct intel_th_driver - driver for an intel_th_device device
 * @driver:	generic driver
 * @probe:	probe method
 * @remove:	remove method
 * @assign:	match a given output type device against available outputs
 * @unassign:	deassociate an output type device from an output port
 * @prepare:	prepare output port for tracing
 * @enable:	enable tracing for a given output device
 * @disable:	disable tracing for a given output device
 * @irq:	interrupt callback
 * @activate:	enable tracing on the output's side
 * @deactivate:	disable tracing on the output's side
 * @fops:	file operations for device nodes
 * @attr_group:	attributes provided by the driver
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
	void			(*prepare)(struct intel_th_device *thdev,
					   struct intel_th_output *output);
	void			(*enable)(struct intel_th_device *thdev,
					  struct intel_th_output *output);
	void			(*trig_switch)(struct intel_th_device *thdev,
					       struct intel_th_output *output);
	void			(*disable)(struct intel_th_device *thdev,
					   struct intel_th_output *output);
	/* output ops */
	irqreturn_t		(*irq)(struct intel_th_device *thdev);
	void			(*wait_empty)(struct intel_th_device *thdev);
	int			(*activate)(struct intel_th_device *thdev);
	void			(*deactivate)(struct intel_th_device *thdev);
	/* file_operations for those who want a device node */
	const struct file_operations *fops;
	/* optional attributes */
	struct attribute_group	*attr_group;

	/* source ops */
	int			(*set_output)(struct intel_th_device *thdev,
					      unsigned int master);
};

#define to_intel_th_driver(_d)					\
	container_of((_d), struct intel_th_driver, driver)

#define to_intel_th_driver_or_null(_d)		\
	((_d) ? to_intel_th_driver(_d) : NULL)

/*
 * Subdevice tree structure is as follows:
 * + struct intel_th device (pci; dev_{get,set}_drvdata()
 *   + struct intel_th_device INTEL_TH_SWITCH (GTH)
 *     + struct intel_th_device INTEL_TH_OUTPUT (MSU, PTI)
 *   + struct intel_th_device INTEL_TH_SOURCE (STH)
 *
 * In other words, INTEL_TH_OUTPUT devices are children of INTEL_TH_SWITCH;
 * INTEL_TH_SWITCH and INTEL_TH_SOURCE are children of the intel_th device.
 */
static inline struct intel_th_device *
to_intel_th_parent(struct intel_th_device *thdev)
{
	struct device *parent = thdev->dev.parent;

	if (!parent)
		return NULL;

	return to_intel_th_device(parent);
}

static inline struct intel_th *to_intel_th(struct intel_th_device *thdev)
{
	if (thdev->type == INTEL_TH_OUTPUT)
		thdev = to_intel_th_parent(thdev);

	if (WARN_ON_ONCE(!thdev || thdev->type == INTEL_TH_OUTPUT))
		return NULL;

	return dev_get_drvdata(thdev->dev.parent);
}

struct intel_th *
intel_th_alloc(struct device *dev, struct intel_th_drvdata *drvdata,
	       struct resource *devres, unsigned int ndevres);
void intel_th_free(struct intel_th *th);

int intel_th_driver_register(struct intel_th_driver *thdrv);
void intel_th_driver_unregister(struct intel_th_driver *thdrv);

int intel_th_trace_enable(struct intel_th_device *thdev);
int intel_th_trace_switch(struct intel_th_device *thdev);
int intel_th_trace_disable(struct intel_th_device *thdev);
int intel_th_set_output(struct intel_th_device *thdev,
			unsigned int master);
int intel_th_output_enable(struct intel_th *th, unsigned int otype);

enum th_mmio_idx {
	TH_MMIO_CONFIG = 0,
	TH_MMIO_SW = 1,
	TH_MMIO_RTIT = 2,
	TH_MMIO_END,
};

#define TH_POSSIBLE_OUTPUTS	8
/* Total number of possible subdevices: outputs + GTH + STH */
#define TH_SUBDEVICE_MAX	(TH_POSSIBLE_OUTPUTS + 2)
#define TH_CONFIGURABLE_MASTERS 256
#define TH_MSC_MAX		2

/* Maximum IRQ vectors */
#define TH_NVEC_MAX		8

/**
 * struct intel_th - Intel TH controller
 * @dev:	driver core's device
 * @thdev:	subdevices
 * @hub:	"switch" subdevice (GTH)
 * @resource:	resources of the entire controller
 * @num_thdevs:	number of devices in the @thdev array
 * @num_resources:	number of resources in the @resource array
 * @irq:	irq number
 * @num_irqs:	number of IRQs is use
 * @id:		this Intel TH controller's device ID in the system
 * @major:	device node major for output devices
 */
struct intel_th {
	struct device		*dev;

	struct intel_th_device	*thdev[TH_SUBDEVICE_MAX];
	struct intel_th_device	*hub;
	struct intel_th_drvdata	*drvdata;

	struct resource		resource[TH_MMIO_END];
	int			(*activate)(struct intel_th *);
	void			(*deactivate)(struct intel_th *);
	unsigned int		num_thdevs;
	unsigned int		num_resources;
	int			irq;
	int			num_irqs;

	int			id;
	int			major;
#ifdef CONFIG_MODULES
	struct work_struct	request_module_work;
#endif /* CONFIG_MODULES */
#ifdef CONFIG_INTEL_TH_DEBUG
	struct dentry		*dbg;
#endif
};

static inline struct intel_th_device *
to_intel_th_hub(struct intel_th_device *thdev)
{
	if (thdev->type == INTEL_TH_SWITCH)
		return thdev;
	else if (thdev->type == INTEL_TH_OUTPUT)
		return to_intel_th_parent(thdev);

	return to_intel_th(thdev)->hub;
}

/*
 * Register windows
 */
enum {
	/* Global Trace Hub (GTH) */
	REG_GTH_OFFSET		= 0x0000,
	REG_GTH_LENGTH		= 0x2000,

	/* Timestamp counter unit (TSCU) */
	REG_TSCU_OFFSET		= 0x2000,
	REG_TSCU_LENGTH		= 0x1000,

	REG_CTS_OFFSET		= 0x3000,
	REG_CTS_LENGTH		= 0x1000,

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
