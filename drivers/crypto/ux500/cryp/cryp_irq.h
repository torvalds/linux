/**
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com> for ST-Ericsson.
 * Author: Jonas Linde <jonas.linde@stericsson.com> for ST-Ericsson.
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson.
 * Author: Berne Hebark <berne.herbark@stericsson.com> for ST-Ericsson.
 * Author: Niklas Hernaeus <niklas.hernaeus@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef _CRYP_IRQ_H_
#define _CRYP_IRQ_H_

#include "cryp.h"

enum cryp_irq_src_id {
	CRYP_IRQ_SRC_INPUT_FIFO = 0x1,
	CRYP_IRQ_SRC_OUTPUT_FIFO = 0x2,
	CRYP_IRQ_SRC_ALL = 0x3
};

/**
 * M0 Funtions
 */
void cryp_enable_irq_src(struct cryp_device_data *device_data, u32 irq_src);

void cryp_disable_irq_src(struct cryp_device_data *device_data, u32 irq_src);

bool cryp_pending_irq_src(struct cryp_device_data *device_data, u32 irq_src);

#endif				/* _CRYP_IRQ_H_ */
