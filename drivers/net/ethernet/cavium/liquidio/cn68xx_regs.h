/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/

/*! \file cn68xx_regs.h
 *  \brief Host Driver: Register Address and Register Mask values for
 *  Octeon CN68XX devices. The register map for CN66XX is the same
 *  for most registers. This file has the other registers that are
 *  68XX-specific.
 */

#ifndef __CN68XX_REGS_H__
#define __CN68XX_REGS_H__
#include "cn66xx_regs.h"

/*###################### REQUEST QUEUE #########################*/

#define    CN68XX_SLI_IQ_PORT0_PKIND             0x0800

#define    CN68XX_SLI_IQ_PORT_PKIND(iq)           \
	(CN68XX_SLI_IQ_PORT0_PKIND + ((iq) * CN6XXX_IQ_OFFSET))

/*############################ OUTPUT QUEUE #########################*/

/* Starting pipe number and number of pipes used by the SLI packet output. */
#define    CN68XX_SLI_TX_PIPE                    0x1230

/*######################## INTERRUPTS #########################*/

/*------------------ Interrupt Masks ----------------*/
#define    CN68XX_INTR_PIPE_ERR                  BIT_ULL(61)

#endif
