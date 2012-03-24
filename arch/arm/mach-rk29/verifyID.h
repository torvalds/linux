/*
 */

#ifndef _LINUX_VERIFYID_H
#define _LINUX_VERIFYID_H
#include <mach/rk29_iomap.h>


#define VERIFYID_GETID 0x29
#define     write_XDATA32(address, value)   (*((unsigned long volatile*)(address)) = value)
#define     read_XDATA32(address)           (*((unsigned long  volatile*)(address)))

#endif	/* _LINUX_VERIFYID_H */

