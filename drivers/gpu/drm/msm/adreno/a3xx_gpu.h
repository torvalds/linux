/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __A3XX_GPU_H__
#define __A3XX_GPU_H__

#include "adreno_gpu.h"
#include "a3xx.xml.h"

struct a3xx_gpu {
	struct adreno_gpu base;
	struct platform_device *pdev;

	/* if OCMEM is used for GMEM: */
	uint32_t ocmem_base;
	void *ocmem_hdl;
};
#define to_a3xx_gpu(x) container_of(x, struct a3xx_gpu, base)

#endif /* __A3XX_GPU_H__ */
