/*
 * Copyright 2005-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef _HDMICEC_H_
#define _HDMICEC_H_
#include <linux/ioctl.h>

/*
 * Ioctl definitions
 */

/* Use 'k' as magic number */
#define HDMICEC_IOC_MAGIC  'H'
/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": G and S atomically
 * H means "sHift": T and Q atomically
 */
#define HDMICEC_IOC_SETLOGICALADDRESS  \
				_IOW(HDMICEC_IOC_MAGIC, 1, unsigned char)
#define HDMICEC_IOC_STARTDEVICE	_IO(HDMICEC_IOC_MAGIC,  2)
#define HDMICEC_IOC_STOPDEVICE	_IO(HDMICEC_IOC_MAGIC,  3)
#define HDMICEC_IOC_GETPHYADDRESS	\
				_IOR(HDMICEC_IOC_MAGIC, 4, unsigned char[4])

#endif				/* !_HDMICEC_H_ */
