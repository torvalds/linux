/* $Id: max1617.h,v 1.1 2001/04/02 09:59:08 davem Exp $ */
#ifndef _MAX1617_H
#define _MAX1617_H

#define MAX1617_AMB_TEMP	0x00 /* Ambient temp in C	*/
#define MAX1617_CPU_TEMP	0x01 /* Processor die temp in C	*/
#define MAX1617_STATUS		0x02 /* Chip status bits	*/

/* Read-only versions of changable registers. */
#define MAX1617_RD_CFG_BYTE	0x03 /* Config register		*/
#define MAX1617_RD_CVRATE_BYTE	0x04 /* Temp conversion rate	*/
#define MAX1617_RD_AMB_HIGHLIM	0x05 /* Ambient high limit	*/
#define MAX1617_RD_AMB_LOWLIM	0x06 /* Ambient low limit	*/
#define MAX1617_RD_CPU_HIGHLIM	0x07 /* Processor high limit	*/
#define MAX1617_RD_CPU_LOWLIM	0x08 /* Processor low limit	*/

/* Write-only versions of the same. */
#define MAX1617_WR_CFG_BYTE	0x09
#define MAX1617_WR_CVRATE_BYTE	0x0a
#define MAX1617_WR_AMB_HIGHLIM	0x0b
#define MAX1617_WR_AMB_LOWLIM	0x0c
#define MAX1617_WR_CPU_HIGHLIM	0x0d
#define MAX1617_WR_CPU_LOWLIM	0x0e

#define MAX1617_ONESHOT		0x0f

#endif /* _MAX1617_H */
