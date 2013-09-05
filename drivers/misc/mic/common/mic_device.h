/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC driver.
 *
 */
#ifndef __MIC_COMMON_DEVICE_H_
#define __MIC_COMMON_DEVICE_H_

/**
 * struct mic_mw - MIC memory window
 *
 * @pa: Base physical address.
 * @va: Base ioremap'd virtual address.
 * @len: Size of the memory window.
 */
struct mic_mw {
	phys_addr_t pa;
	void __iomem *va;
	resource_size_t len;
};

#endif
