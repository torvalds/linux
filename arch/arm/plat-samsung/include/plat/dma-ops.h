/* arch/arm/plat-samsung/include/plat/dma-ops.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung DMA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SAMSUNG_DMA_OPS_H_
#define __SAMSUNG_DMA_OPS_H_ __FILE__

#include <linux/dmaengine.h>
#include <mach/dma.h>

struct samsung_dma_req {
	enum dma_transaction_type cap;
	struct property *dt_dmach_prop;
	struct s3c2410_dma_client *client;
};

struct samsung_dma_prep {
	enum dma_transaction_type cap;
	enum dma_transfer_direction direction;
	dma_addr_t buf;
	unsigned long period;
	unsigned long len;
	void (*fp)(void *data);
	void *fp_param;
};

struct samsung_dma_config {
	enum dma_transfer_direction direction;
	enum dma_slave_buswidth width;
	dma_addr_t fifo;
};

struct samsung_dma_ops {
	unsigned (*request)(enum dma_ch ch, struct samsung_dma_req *param,
				struct device *dev, char *ch_name);
	int (*release)(unsigned ch, void *param);
	int (*config)(unsigned ch, struct samsung_dma_config *param);
	int (*prepare)(unsigned ch, struct samsung_dma_prep *param);
	int (*trigger)(unsigned ch);
	int (*started)(unsigned ch);
	int (*flush)(unsigned ch);
	int (*stop)(unsigned ch);
};

extern void *samsung_dmadev_get_ops(void);
extern void *s3c_dma_get_ops(void);

static inline void *__samsung_dma_get_ops(void)
{
	if (samsung_dma_is_dmadev())
		return samsung_dmadev_get_ops();
	else
		return s3c_dma_get_ops();
}

/*
 * samsung_dma_get_ops
 * get the set of samsung dma operations
 */
#define samsung_dma_get_ops() __samsung_dma_get_ops()

#endif /* __SAMSUNG_DMA_OPS_H_ */
