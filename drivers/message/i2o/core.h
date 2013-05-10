/*
 *	I2O core internal declarations
 *
 *	Copyright (C) 2005	Markus Lidel <Markus.Lidel@shadowconnect.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	Fixes/additions:
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>
 *			initial version.
 */

/* Exec-OSM */
extern struct i2o_driver i2o_exec_driver;
extern int i2o_exec_lct_get(struct i2o_controller *);

extern int __init i2o_exec_init(void);
extern void i2o_exec_exit(void);

/* driver */
extern struct bus_type i2o_bus_type;

extern int i2o_driver_dispatch(struct i2o_controller *, u32);

extern int __init i2o_driver_init(void);
extern void i2o_driver_exit(void);

/* PCI */
extern int __init i2o_pci_init(void);
extern void __exit i2o_pci_exit(void);

/* device */
extern struct device_attribute i2o_device_attrs[];

extern void i2o_device_remove(struct i2o_device *);
extern int i2o_device_parse_lct(struct i2o_controller *);

int i2o_parm_issue(struct i2o_device *i2o_dev, int cmd, void *oplist,
		   int oplen, void *reslist, int reslen);

/* IOP */
extern struct i2o_controller *i2o_iop_alloc(void);

/**
 *	i2o_iop_free - Free the i2o_controller struct
 *	@c: I2O controller to free
 */
static inline void i2o_iop_free(struct i2o_controller *c)
{
	i2o_pool_free(&c->in_msg);
	kfree(c);
}

extern int i2o_iop_add(struct i2o_controller *);
extern void i2o_iop_remove(struct i2o_controller *);

/* control registers relative to c->base */
#define I2O_IRQ_STATUS	0x30
#define I2O_IRQ_MASK	0x34
#define I2O_IN_PORT	0x40
#define I2O_OUT_PORT	0x44

/* Motorola/Freescale specific register offset */
#define I2O_MOTOROLA_PORT_OFFSET	0x10400

#define I2O_IRQ_OUTBOUND_POST	0x00000008
