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
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <mach/board.h>
#ifdef CONFIG_ADC_RK28
#include "plat/rk28_adc.h"
#endif
#ifdef CONFIG_ADC_RK29
#include "plat/rk29_adc.h"
#endif
#ifdef CONFIG_ADC_RK30
#include "plat/rk30_adc.h"
#endif

#define ADC_READ_TMO    100 // ms

#define adc_writel                 writel_relaxed
#define adc_readl                  readl_relaxed

#if 0
#define adc_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#else
#define adc_dbg(dev, format, arg...)
#endif

enum read_type{
        ADC_SYNC_READ = 0,
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
	void (*dump)(struct adc_host *);
};
struct adc_host {
        struct list_head entry;
        struct list_head req_head;
        struct list_head callback_head;
        unsigned int is_suspended;
        enum host_chn_mask mask;
        struct device *dev;
        unsigned int chn;
        spinlock_t lock;
        struct mutex m_lock;
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

