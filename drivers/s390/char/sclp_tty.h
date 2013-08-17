/*
 *  drivers/s390/char/sclp_tty.h
 *    interface to the SCLP-read/write driver
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __SCLP_TTY_H__
#define __SCLP_TTY_H__

#include <linux/tty_driver.h>

extern struct tty_driver *sclp_tty_driver;

#endif	/* __SCLP_TTY_H__ */
