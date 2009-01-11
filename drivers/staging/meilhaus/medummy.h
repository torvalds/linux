/*
 * Copyright (C) 2005 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * Source File : medummy.h
 * Author      : GG (Guenter Gebhardt)  <g.gebhardt@meilhaus.de>
 */

#ifndef _MEDUMMY_H_
#define _MEDUMMY_H_

#include "metypes.h"
#include "medefines.h"
#include "medevice.h"

#ifdef __KERNEL__

#define MEDUMMY_MAGIC_NUMBER	0xDDDD

typedef struct medummy_device {
	me_device_t base;			/**< The Meilhaus device base class. */
//      int magic;                                      /**< The magic number of the structure */
	unsigned short vendor_id;	/**< Vendor ID */
	unsigned short device_id;	/**< Device ID */
	unsigned int serial_no;		/**< Serial number of the device */
	int bus_type;				/**< Bus type */
	int bus_no;					/**< Bus number */
	int dev_no;					/**< Device number */
	int func_no;				/**< Function number */
} medummy_device_t;

me_device_t *medummy_constructor(unsigned short vendor_id,
				 unsigned short device_id,
				 unsigned int serial_no,
				 int bus_type,
				 int bus_no,
				 int dev_no,
				 int func_no) __attribute__ ((weak));

#endif
#endif
