/*
 * io.h
 *
 *  Created on: 2012-4-25
 *      Author: Benn Huang (benn@allwinnertech.com)
 */

#ifndef IO_H_
#define IO_H_

#define readb(addr)		(*((volatile unsigned char  *)(addr)))
#define readw(addr)		(*((volatile unsigned short *)(addr)))
#define readl(addr)		(*((volatile unsigned long  *)(addr)))
#define writeb(v, addr)	(*((volatile unsigned char  *)(addr)) = (unsigned char)(v))
#define writew(v, addr)	(*((volatile unsigned short *)(addr)) = (unsigned short)(v))
#define writel(v, addr)	(*((volatile unsigned long  *)(addr)) = (unsigned long)(v))

#endif /* IO_H_ */
