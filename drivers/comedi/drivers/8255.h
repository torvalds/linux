/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * module/8255.h
 * Header file for 8255
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 */

#ifndef _8255_H
#define _8255_H

#define I8255_SIZE		0x04

#define I8255_DATA_A_REG	0x00
#define I8255_DATA_B_REG	0x01
#define I8255_DATA_C_REG	0x02
#define I8255_CTRL_REG		0x03
#define I8255_CTRL_C_LO_IO	BIT(0)
#define I8255_CTRL_B_IO		BIT(1)
#define I8255_CTRL_B_MODE	BIT(2)
#define I8255_CTRL_C_HI_IO	BIT(3)
#define I8255_CTRL_A_IO		BIT(4)
#define I8255_CTRL_A_MODE(x)	((x) << 5)
#define I8255_CTRL_CW		BIT(7)

struct comedi_device;
struct comedi_subdevice;

int subdev_8255_init(struct comedi_device *dev, struct comedi_subdevice *s,
		     int (*io)(struct comedi_device *dev, int dir, int port,
			       int data, unsigned long regbase),
		     unsigned long regbase);

int subdev_8255_mm_init(struct comedi_device *dev, struct comedi_subdevice *s,
			int (*io)(struct comedi_device *dev, int dir, int port,
				  int data, unsigned long regbase),
			unsigned long regbase);

unsigned long subdev_8255_regbase(struct comedi_subdevice *s);

#endif
