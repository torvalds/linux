/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2023, Intel Corporation. All rights reserved.
 */
#ifndef _XE_HECI_GSC_H_
#define _XE_HECI_GSC_H_

#include <linux/types.h>

struct xe_device;
struct mei_aux_device;

/*
 * GSC HECI1 bit corresponds to bit15 and HECI2 to bit14.
 * The reason for this is to allow growth for more interfaces in the future.
 */
#define GSC_IRQ_INTF(_x) BIT(15 - (_x))

/*
 * CSC HECI1 bit corresponds to bit9 and HECI2 to bit10.
 */
#define CSC_IRQ_INTF(_x) BIT(9 + (_x))

/**
 * struct xe_heci_gsc - graphics security controller for xe, HECI interface
 *
 * @adev : pointer to mei auxiliary device structure
 * @irq : irq number
 *
 */
struct xe_heci_gsc {
	struct mei_aux_device *adev;
	int irq;
};

int xe_heci_gsc_init(struct xe_device *xe);
void xe_heci_gsc_irq_handler(struct xe_device *xe, u32 iir);
void xe_heci_csc_irq_handler(struct xe_device *xe, u32 iir);

#endif /* _XE_HECI_GSC_H_ */
