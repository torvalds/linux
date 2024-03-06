/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Wave5 series multi-standard codec IP - low level access functions
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#ifndef _VDI_H_
#define _VDI_H_

#include "wave5-vpuconfig.h"
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/device.h>

/************************************************************************/
/* COMMON REGISTERS */
/************************************************************************/
#define VPU_PRODUCT_CODE_REGISTER 0x1044

/* system register write */
#define vpu_write_reg(VPU_INST, ADDR, DATA) wave5_vdi_write_register(VPU_INST, ADDR, DATA)
/* system register read */
#define vpu_read_reg(CORE, ADDR) wave5_vdi_read_register(CORE, ADDR)

struct vpu_buf {
	size_t size;
	dma_addr_t daddr;
	void *vaddr;
};

int wave5_vdi_init(struct device *dev);
int wave5_vdi_release(struct device *dev);	//this function may be called only at system off.

#endif //#ifndef _VDI_H_
