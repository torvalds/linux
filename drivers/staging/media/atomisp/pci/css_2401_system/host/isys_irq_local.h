/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __ISYS_IRQ_LOCAL_H__
#define __ISYS_IRQ_LOCAL_H__

#include <type_support.h>

typedef struct isys_irqc_state_s isys_irqc_state_t;

struct isys_irqc_state_s {
	hrt_data edge;
	hrt_data mask;
	hrt_data status;
	hrt_data enable;
	hrt_data level_no;
	/*hrt_data clear;	*/	/* write-only register */
};


#endif	/* __ISYS_IRQ_LOCAL_H__ */
