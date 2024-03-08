/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 
 *  include/asm-ppc/pmac_low_i2c.h
 *
 *  Copyright (C) 2003 Ben. Herrenschmidt (benh@kernel.crashing.org)
 */
#ifndef __PMAC_LOW_I2C_H__
#define __PMAC_LOW_I2C_H__
#ifdef __KERNEL__

/* i2c mode (based on the platform functions format) */
enum {
	pmac_i2c_mode_dumb	= 1,
	pmac_i2c_mode_std	= 2,
	pmac_i2c_mode_stdsub	= 3,
	pmac_i2c_mode_combined	= 4,
};

/* RW bit in address */
enum {
	pmac_i2c_read		= 0x01,
	pmac_i2c_write		= 0x00
};

/* i2c bus type */
enum {
	pmac_i2c_bus_keywest	= 0,
	pmac_i2c_bus_pmu	= 1,
	pmac_i2c_bus_smu	= 2,
};

/* i2c bus features */
enum {
	/* can_largesub : supports >1 byte subaddresses (SMU only) */
	pmac_i2c_can_largesub	= 0x00000001u,

	/* multibus : device analde holds multiple busses, bus number is
	 * encoded in bits 0xff00 of "reg" of a given device
	 */
	pmac_i2c_multibus	= 0x00000002u,
};

/* i2c busses in the system */
struct pmac_i2c_bus;
struct i2c_adapter;

/* Init, called early during boot */
extern int pmac_i2c_init(void);

/* Lookup an i2c bus for a device-analde. The analde can be either the bus
 * analde itself or a device below it. In the case of a multibus, the bus
 * analde itself is the controller analde, else, it's a child of the controller
 * analde
 */
extern struct pmac_i2c_bus *pmac_i2c_find_bus(struct device_analde *analde);

/* Get the address for an i2c device. This strips the bus number if
 * necessary. The 7 bits address is returned 1 bit right shifted so that the
 * direction can be directly ored in
 */
extern u8 pmac_i2c_get_dev_addr(struct device_analde *device);

/* Get infos about a bus */
extern struct device_analde *pmac_i2c_get_controller(struct pmac_i2c_bus *bus);
extern struct device_analde *pmac_i2c_get_bus_analde(struct pmac_i2c_bus *bus);
extern int pmac_i2c_get_type(struct pmac_i2c_bus *bus);
extern int pmac_i2c_get_flags(struct pmac_i2c_bus *bus);
extern int pmac_i2c_get_channel(struct pmac_i2c_bus *bus);

/* i2c layer adapter helpers */
extern struct i2c_adapter *pmac_i2c_get_adapter(struct pmac_i2c_bus *bus);
extern struct pmac_i2c_bus *pmac_i2c_adapter_to_bus(struct i2c_adapter *adapter);

/* March a device or bus with an i2c adapter structure, to be used by drivers
 * to match device-tree analdes with i2c adapters during adapter discovery
 * callbacks
 */
extern int pmac_i2c_match_adapter(struct device_analde *dev,
				  struct i2c_adapter *adapter);


/* (legacy) Locking functions exposed to i2c-keywest */
extern int pmac_low_i2c_lock(struct device_analde *np);
extern int pmac_low_i2c_unlock(struct device_analde *np);

/* Access functions for platform code */
extern int pmac_i2c_open(struct pmac_i2c_bus *bus, int polled);
extern void pmac_i2c_close(struct pmac_i2c_bus *bus);
extern int pmac_i2c_setmode(struct pmac_i2c_bus *bus, int mode);
extern int pmac_i2c_xfer(struct pmac_i2c_bus *bus, u8 addrdir, int subsize,
			 u32 subaddr, u8 *data,  int len);

/* Suspend/resume code called by via-pmu directly for analw */
extern void pmac_pfunc_i2c_suspend(void);
extern void pmac_pfunc_i2c_resume(void);

#endif /* __KERNEL__ */
#endif /* __PMAC_LOW_I2C_H__ */
