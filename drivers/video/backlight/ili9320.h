/* drivers/video/backlight/ili9320.h
 *
 * ILI9320 LCD controller driver core.
 *
 * Copyright 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Holder for register and value pairs. */
struct ili9320_reg {
	unsigned short		address;
	unsigned short		value;
};

struct ili9320;

struct ili9320_client {
	const char	*name;
	int	(*init)(struct ili9320 *ili, struct ili9320_platdata *cfg);

};
/* Device attached via an SPI bus. */
struct  ili9320_spi {
	struct spi_device	*dev;
	struct spi_message	message;
	struct spi_transfer	xfer[2];

	unsigned char		id;
	unsigned char		buffer_addr[4];
	unsigned char		buffer_data[4];
};

/* ILI9320 device state. */
struct ili9320 {
	union {
		struct ili9320_spi	spi;	/* SPI attachged device. */
	} access;				/* Register access method. */

	struct device			*dev;
	struct lcd_device		*lcd;	/* LCD device we created. */
	struct ili9320_client		*client;
	struct ili9320_platdata		*platdata;

	int				 power; /* current power state. */
	int				 initialised;

	unsigned short			 display1;
	unsigned short			 power1;

	int (*write)(struct ili9320 *ili, unsigned int reg, unsigned int val);
};


/* ILI9320 register access routines */

extern int ili9320_write(struct ili9320 *ili,
			 unsigned int reg, unsigned int value);

extern int ili9320_write_regs(struct ili9320 *ili,
			      const struct ili9320_reg *values,
			      int nr_values);

/* Device probe */

extern int ili9320_probe_spi(struct spi_device *spi,
			     struct ili9320_client *cli);

extern int ili9320_remove(struct ili9320 *lcd);
extern void ili9320_shutdown(struct ili9320 *lcd);

/* PM */

extern int ili9320_suspend(struct ili9320 *lcd);
extern int ili9320_resume(struct ili9320 *lcd);
