/*
 * Defines for the MSP interrupt handlers.
 *
 * Copyright (C) 2005, PMC-Sierra, Inc.  All rights reserved.
 * Author: Andrew Hughes, Andrew_Hughes@pmc-sierra.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */

#ifndef _MSP_INT_H
#define _MSP_INT_H

/*
 * The PMC-Sierra MSP product line has at least two different interrupt
 * controllers, the SLP register based scheme and the CIC interrupt
 * controller block mechanism.  This file distinguishes between them
 * so that devices see a uniform interface.
 */

#if defined(CONFIG_IRQ_MSP_SLP)
	#include "msp_slp_int.h"
#elif defined(CONFIG_IRQ_MSP_CIC)
	#include "msp_cic_int.h"
#else
	#error "What sort of interrupt controller does *your* MSP have?"
#endif

#endif /* !_MSP_INT_H */
