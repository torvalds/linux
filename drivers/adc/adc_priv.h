/* include/linux/adc.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __ASM_ADC_PRIV_H
#define __ASM_ADC_PRIV_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <mach/board.h>

#define ADC_READ_TMO    100 // ms

#define adc_writel                 writel_relaxed
#define adc_readl                  readl_relaxed

enum read_type{
        ADC_SYNC_READ,
        ADC_ASYNC_READ,
};

struct adc_request {
        struct list_head entry;
        struct adc_client *client;
};
struct adc_ops {
	void (*start)(struct adc_host *);
	void (*stop)(struct adc_host *);
	int (*read)(struct adc_host *);
};
struct adc_host {
        struct list_head entry;
        struct list_head request_head;
        unsigned int is_suspended;
        enum host_chn_mask mask;
        struct device *dev;
        struct adc_client *cur;
        spinlock_t lock;
        unsigned int client_count;
	const struct adc_ops *ops;
        unsigned long priv[0];
};

static inline void *adc_priv(struct adc_host *adc)
{
	return adc->priv;
}
	
struct adc_host *adc_alloc_host(struct device *dev, int extra, enum host_chn_mask mask);
void adc_free_host(struct adc_host *adc);
void adc_core_irq_handle(struct adc_host *adc);

#endif

