/* busctl-regs.h: FR400-series CPU bus controller registers
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_BUSCTL_REGS_H
#define _ASM_BUSCTL_REGS_H

/* bus controller registers */
#define __get_LGCR()	({ *(volatile unsigned long *)(0xfe000010); })
#define __get_LMAICR()	({ *(volatile unsigned long *)(0xfe000030); })
#define __get_LEMBR()	({ *(volatile unsigned long *)(0xfe000040); })
#define __get_LEMAM()	({ *(volatile unsigned long *)(0xfe000048); })
#define __get_LCR(R)	({ *(volatile unsigned long *)(0xfe000100 + 8*(R)); })
#define __get_LSBR(R)	({ *(volatile unsigned long *)(0xfe000c00 + 8*(R)); })
#define __get_LSAM(R)	({ *(volatile unsigned long *)(0xfe000d00 + 8*(R)); })

#define __set_LGCR(V)	do { *(volatile unsigned long *)(0xfe000010) = (V); } while(0)
#define __set_LMAICR(V)	do { *(volatile unsigned long *)(0xfe000030) = (V); } while(0)
#define __set_LEMBR(V)	do { *(volatile unsigned long *)(0xfe000040) = (V); } while(0)
#define __set_LEMAM(V)	do { *(volatile unsigned long *)(0xfe000048) = (V); } while(0)
#define __set_LCR(R,V)	do { *(volatile unsigned long *)(0xfe000100 + 8*(R)) = (V); } while(0)
#define __set_LSBR(R,V)	do { *(volatile unsigned long *)(0xfe000c00 + 8*(R)) = (V); } while(0)
#define __set_LSAM(R,V)	do { *(volatile unsigned long *)(0xfe000d00 + 8*(R)) = (V); } while(0)

/* FR401 SDRAM controller registers */
#define __get_DBR(R)	({ *(volatile unsigned long *)(0xfe000e00 + 8*(R)); })
#define __get_DAM(R)	({ *(volatile unsigned long *)(0xfe000f00 + 8*(R)); })

/* FR551 SDRAM controller registers */
#define __get_DARS(R)	({ *(volatile unsigned long *)(0xfeff0100 + 8*(R)); })
#define __get_DAMK(R)	({ *(volatile unsigned long *)(0xfeff0110 + 8*(R)); })


#endif /* _ASM_BUSCTL_REGS_H */
