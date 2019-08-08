/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines for the MSP interrupt handlers.
 *
 * Copyright (C) 2005, PMC-Sierra, Inc.	 All rights reserved.
 * Author: Andrew Hughes, Andrew_Hughes@pmc-sierra.com
 *
 * ########################################################################
 *
 * ########################################################################
 */

#ifndef _MSP_INT_H
#define _MSP_INT_H

/*
 * The PMC-Sierra MSP product line has at least two different interrupt
 * controllers, the SLP register based scheme and the CIC interrupt
 * controller block mechanism.	This file distinguishes between them
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
