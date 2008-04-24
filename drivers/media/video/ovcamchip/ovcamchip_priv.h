/* OmniVision* camera chip driver private definitions for core code and
 * chip-specific code
 *
 * Copyright (c) 1999-2004 Mark McClelland
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. NO WARRANTY OF ANY KIND is expressed or implied.
 *
 * * OmniVision is a trademark of OmniVision Technologies, Inc. This driver
 * is not sponsored or developed by them.
 */

#ifndef __LINUX_OVCAMCHIP_PRIV_H
#define __LINUX_OVCAMCHIP_PRIV_H

#include <linux/i2c.h>
#include <media/ovcamchip.h>

#ifdef DEBUG
extern int ovcamchip_debug;
#endif

#define PDEBUG(level, fmt, args...) \
	if (ovcamchip_debug >= (level))	pr_debug("[%s:%d] " fmt "\n", \
		__func__, __LINE__ , ## args)

#define DDEBUG(level, dev, fmt, args...) \
	if (ovcamchip_debug >= (level))	dev_dbg(dev, "[%s:%d] " fmt "\n", \
		__func__, __LINE__ , ## args)

/* Number of times to retry chip detection. Increase this if you are getting
 * "Failed to init camera chip" */
#define I2C_DETECT_RETRIES	10

struct ovcamchip_regvals {
	unsigned char reg;
	unsigned char val;
};

struct ovcamchip_ops {
	int (*init)(struct i2c_client *);
	int (*free)(struct i2c_client *);
	int (*command)(struct i2c_client *, unsigned int, void *);
};

struct ovcamchip {
	struct ovcamchip_ops *sops;
	void *spriv;               /* Private data for OV7x10.c etc... */
	int subtype;               /* = SEN_OV7610 etc... */
	int mono;                  /* Monochrome chip? (invalid until init) */
	int initialized;           /* OVCAMCHIP_CMD_INITIALIZE was successful */
};

/* --------------------------------- */
/*              I2C I/O              */
/* --------------------------------- */

static inline int ov_read(struct i2c_client *c, unsigned char reg,
			  unsigned char *value)
{
	int rc;

	rc = i2c_smbus_read_byte_data(c, reg);
	*value = (unsigned char) rc;
	return rc;
}

static inline int ov_write(struct i2c_client *c, unsigned char reg,
			   unsigned char value )
{
	return i2c_smbus_write_byte_data(c, reg, value);
}

/* --------------------------------- */
/*        FUNCTION PROTOTYPES        */
/* --------------------------------- */

/* Functions in ovcamchip_core.c */

extern int ov_write_regvals(struct i2c_client *c,
			    struct ovcamchip_regvals *rvals);

extern int ov_write_mask(struct i2c_client *c, unsigned char reg,
			 unsigned char value, unsigned char mask);

#endif
