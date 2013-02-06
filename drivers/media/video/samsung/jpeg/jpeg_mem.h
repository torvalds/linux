/* linux/drivers/media/video/samsung/jpeg/jpeg_mem.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Definition for Operation of Jpeg encoder/docoder with memory
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __JPEG_MEM_H__
#define __JPEG_MEM_H__

// JYSHIN for google demo 20101014
#define MAX_JPEG_WIDTH		3264
#define MAX_JPEG_HEIGHT		2448

//#define MAX_JPEG_WIDTH	3072
//#define MAX_JPEG_HEIGHT	2048
#ifdef CONFIG_UMP_VCM_ALLOC
#include <plat/s5p-vcm.h>
#include "ump_kernel_interface.h"
#include "ump_kernel_interface_ref_drv.h"
#endif

#define MAX_JPEG_RES	(MAX_JPEG_WIDTH * MAX_JPEG_HEIGHT)

/* jpeg stream buf */
#define JPEG_S_BUF_SIZE	((MAX_JPEG_RES / PAGE_SIZE + 1) * PAGE_SIZE)
/* jpeg frame buf */
#define JPEG_F_BUF_SIZE	(((MAX_JPEG_RES * 3) / PAGE_SIZE + 1) * PAGE_SIZE)

#define JPEG_MEM_SIZE		(JPEG_S_BUF_SIZE + JPEG_F_BUF_SIZE)
#define JPEG_MAIN_START		0x00

#define SYSMMU_JPEG_ON

/* for reserved memory */
struct jpeg_mem {
	/* buffer base */
	unsigned int	base;
	/* for jpeg stream data */
	unsigned int	stream_data_addr;
	unsigned int	stream_data_size;
	/* for raw data */
	unsigned int	frame_data_addr;
	unsigned int	frame_data_size;
};

int jpeg_init_mem(struct device *dev, unsigned int *base);
int jpeg_mem_free(void);
unsigned long jpeg_get_stream_buf(unsigned long arg);
unsigned long jpeg_get_frame_buf(unsigned long arg);
void jpeg_set_stream_buf(unsigned int *str_buf, unsigned int base);
void jpeg_set_frame_buf(unsigned int *fra_buf, unsigned int base);

#if defined(CONFIG_S5P_SYSMMU_JPEG) && defined(CONFIG_S5P_VMEM)
extern unsigned int *s5p_vmalloc(size_t size);
extern void *s5p_getaddress(unsigned int cookie);
extern void s5p_vfree(unsigned int cookie);
#endif

#endif /* __JPEG_MEM_H__ */

