/*
 * Copyright IBM Corp. 2004, 2005
 * Interface implementation for communication with the z/VM control program
 * Version 1.0
 * Author(s): Christian Borntraeger <cborntra@de.ibm.com>
 *
 *
 * z/VMs CP offers the possibility to issue commands via the diagnose code 8
 * this driver implements a character device that issues these commands and
 * returns the answer of CP.
 *
 * The idea of this driver is based on cpint from Neale Ferguson
 */

#ifndef _UAPI_ASM_VMCP_H
#define _UAPI_ASM_VMCP_H

#include <linux/ioctl.h>

#define VMCP_GETCODE	_IOR(0x10, 1, int)
#define VMCP_SETBUF	_IOW(0x10, 2, int)
#define VMCP_GETSIZE	_IOR(0x10, 3, int)

#endif /* _UAPI_ASM_VMCP_H */
