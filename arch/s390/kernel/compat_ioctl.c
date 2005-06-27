/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 *  S390 version
 *    Copyright (C) 2000-2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Gerhard Tonn (ton@de.ibm.com)
 *               Arnd Bergmann (arndb@de.ibm.com)
 *
 * Original implementation from 32-bit Sparc compat code which is
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com) 
 */

#include "compat_linux.h"
#define INCLUDES
#define CODE
#include "../../../fs/compat_ioctl.c"
#include <asm/dasd.h>
#include <asm/cmb.h>
#include <asm/tape390.h>

static int do_ioctl32_pointer(unsigned int fd, unsigned int cmd,
				unsigned long arg, struct file *f)
{
	return sys_ioctl(fd, cmd, (unsigned long)compat_ptr(arg));
}

static int do_ioctl32_ulong(unsigned int fd, unsigned int cmd,
				unsigned long arg, struct file *f)
{
	return sys_ioctl(fd, cmd, arg);
}

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),(ioctl_trans_handler_t)do_ioctl32_pointer)
#define ULONG_IOCTL(cmd)		HANDLE_IOCTL((cmd),(ioctl_trans_handler_t)do_ioctl32_ulong)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl_trans_handler_t)(handler), NULL },

struct ioctl_trans ioctl_start[] = {
/* architecture independent ioctls */
#include <linux/compat_ioctl.h>
#define DECLARES
#include "../../../fs/compat_ioctl.c"

/* s390 only ioctls */
COMPATIBLE_IOCTL(DASDAPIVER)
COMPATIBLE_IOCTL(BIODASDDISABLE)
COMPATIBLE_IOCTL(BIODASDENABLE)
COMPATIBLE_IOCTL(BIODASDRSRV)
COMPATIBLE_IOCTL(BIODASDRLSE)
COMPATIBLE_IOCTL(BIODASDSLCK)
COMPATIBLE_IOCTL(BIODASDINFO)
COMPATIBLE_IOCTL(BIODASDINFO2)
COMPATIBLE_IOCTL(BIODASDFMT)
COMPATIBLE_IOCTL(BIODASDPRRST)
COMPATIBLE_IOCTL(BIODASDQUIESCE)
COMPATIBLE_IOCTL(BIODASDRESUME)
COMPATIBLE_IOCTL(BIODASDPRRD)
COMPATIBLE_IOCTL(BIODASDPSRD)
COMPATIBLE_IOCTL(BIODASDGATTR)
COMPATIBLE_IOCTL(BIODASDSATTR)
COMPATIBLE_IOCTL(BIODASDCMFENABLE)
COMPATIBLE_IOCTL(BIODASDCMFDISABLE)
COMPATIBLE_IOCTL(BIODASDREADALLCMB)

COMPATIBLE_IOCTL(TAPE390_DISPLAY)

/* s390 doesn't need handlers here */
COMPATIBLE_IOCTL(TIOCGSERIAL)
COMPATIBLE_IOCTL(TIOCSSERIAL)
};

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
