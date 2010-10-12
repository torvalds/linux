/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: upc.h
 *
 * Purpose: Macros to access device
 *
 * Author: Tevin Chen
 *
 * Date: Mar 17, 1997
 *
 */

#ifndef __UPC_H__
#define __UPC_H__

#include "device.h"
#include "ttype.h"

/*---------------------  Export Definitions -------------------------*/


//
//  For IO mapped
//

#ifdef IO_MAP

#define VNSvInPortB(dwIOAddress, pbyData) {                     \
	*(pbyData) = inb(dwIOAddress);                              \
}


#define VNSvInPortW(dwIOAddress, pwData) {                      \
	    *(pwData) = inw(dwIOAddress);                           \
}

#define VNSvInPortD(dwIOAddress, pdwData) {                     \
	    *(pdwData) = inl(dwIOAddress);                          \
}


#define VNSvOutPortB(dwIOAddress, byData) {                     \
        outb(byData, dwIOAddress);                              \
}


#define VNSvOutPortW(dwIOAddress, wData) {                      \
        outw(wData, dwIOAddress);                               \
}

#define VNSvOutPortD(dwIOAddress, dwData) {                     \
        outl(dwData, dwIOAddress);                              \
}

#else

//
//  For memory mapped IO
//


#define VNSvInPortB(dwIOAddress, pbyData) {                     \
	volatile unsigned char * pbyAddr = ((unsigned char *)(dwIOAddress));            \
	*(pbyData) = readb(pbyAddr);                           \
}


#define VNSvInPortW(dwIOAddress, pwData) {                      \
	volatile unsigned short *pwAddr = ((unsigned short *)(dwIOAddress));             \
	*(pwData) = readw(pwAddr);                             \
}

#define VNSvInPortD(dwIOAddress, pdwData) {                     \
	volatile unsigned long *pdwAddr = ((unsigned long *)(dwIOAddress));          \
	*(pdwData) = readl(pdwAddr);                           \
}


#define VNSvOutPortB(dwIOAddress, byData) {                     \
    volatile unsigned char * pbyAddr = ((unsigned char *)(dwIOAddress));            \
    writeb((unsigned char)byData, pbyAddr);							\
}


#define VNSvOutPortW(dwIOAddress, wData) {                      \
    volatile unsigned short *pwAddr = ((unsigned short *)(dwIOAddress));             \
    writew((unsigned short)wData, pwAddr);							\
}

#define VNSvOutPortD(dwIOAddress, dwData) {                     \
    volatile unsigned long *pdwAddr = ((unsigned long *)(dwIOAddress));          \
    writel((unsigned long)dwData, pdwAddr);					    \
}

#endif


//
// ALWAYS IO-Mapped IO when in 16-bit/32-bit environment
//
#define PCBvInPortB(dwIOAddress, pbyData) {     \
	    *(pbyData) = inb(dwIOAddress);          \
}

#define PCBvInPortW(dwIOAddress, pwData) {      \
	    *(pwData) = inw(dwIOAddress);           \
}

#define PCBvInPortD(dwIOAddress, pdwData) {     \
	    *(pdwData) = inl(dwIOAddress);          \
}

#define PCBvOutPortB(dwIOAddress, byData) {     \
        outb(byData, dwIOAddress);              \
}

#define PCBvOutPortW(dwIOAddress, wData) {      \
        outw(wData, dwIOAddress);               \
}

#define PCBvOutPortD(dwIOAddress, dwData) {     \
        outl(dwData, dwIOAddress);              \
}


#define PCAvDelayByIO(uDelayUnit) {             \
    unsigned char byData;                       \
    unsigned long ii;                           \
                                                \
    if (uDelayUnit <= 50) {                     \
        udelay(uDelayUnit);                     \
    }                                           \
    else {                                      \
        for (ii = 0; ii < (uDelayUnit); ii++)   \
		     byData = inb(0x61);				\
    }                                           \
}


/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/




#endif // __UPC_H__

