/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Secure Processor driver
 *
 * Copyright (C) 2017-2019 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#ifndef __SP_DEV_H__
#define __SP_DEV_H__

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/dmapool.h>
#include <linux/hw_random.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>

#define SP_MAX_NAME_LEN		32

#define CACHE_NONE			0x00
#define CACHE_WB_NO_ALLOC		0xb7

/* Structure to hold CCP device data */
struct ccp_device;
struct ccp_vdata {
	const unsigned int version;
	const unsigned int dma_chan_attr;
	void (*setup)(struct ccp_device *);
	const struct ccp_actions *perform;
	const unsigned int offset;
	const unsigned int rsamax;
};

struct sev_vdata {
	const unsigned int cmdresp_reg;
	const unsigned int cmdbuff_addr_lo_reg;
	const unsigned int cmdbuff_addr_hi_reg;
};

struct tee_vdata {
	const unsigned int cmdresp_reg;
	const unsigned int cmdbuff_addr_lo_reg;
	const unsigned int cmdbuff_addr_hi_reg;
	const unsigned int ring_wptr_reg;
	const unsigned int ring_rptr_reg;
};

struct psp_vdata {
	const struct sev_vdata *sev;
	const struct tee_vdata *tee;
	const unsigned int feature_reg;
	const unsigned int inten_reg;
	const unsigned int intsts_reg;
};

/* Structure to hold SP device data */
struct sp_dev_vdata {
	const unsigned int bar;

	const struct ccp_vdata *ccp_vdata;
	const struct psp_vdata *psp_vdata;
};

struct sp_device {
	struct list_head entry;

	struct device *dev;

	struct sp_dev_vdata *dev_vdata;
	unsigned int ord;
	char name[SP_MAX_NAME_LEN];

	/* Bus specific device information */
	void *dev_specific;

	/* I/O area used for device communication. */
	void __iomem *io_map;

	/* DMA caching attribute support */
	unsigned int axcache;

	/* get and set master device */
	struct sp_device*(*get_psp_master_device)(void);
	void (*set_psp_master_device)(struct sp_device *);
	void (*clear_psp_master_device)(struct sp_device *);

	bool irq_registered;
	bool use_tasklet;

	unsigned int ccp_irq;
	irq_handler_t ccp_irq_handler;
	void *ccp_irq_data;

	unsigned int psp_irq;
	irq_handler_t psp_irq_handler;
	void *psp_irq_data;

	void *ccp_data;
	void *psp_data;
};

int sp_pci_init(void);
void sp_pci_exit(void);

int sp_platform_init(void);
void sp_platform_exit(void);

struct sp_device *sp_alloc_struct(struct device *dev);

int sp_init(struct sp_device *sp);
void sp_destroy(struct sp_device *sp);
struct sp_device *sp_get_master(void);

int sp_suspend(struct sp_device *sp, pm_message_t state);
int sp_resume(struct sp_device *sp);
int sp_request_ccp_irq(struct sp_device *sp, irq_handler_t handler,
		       const char *name, void *data);
void sp_free_ccp_irq(struct sp_device *sp, void *data);
int sp_request_psp_irq(struct sp_device *sp, irq_handler_t handler,
		       const char *name, void *data);
void sp_free_psp_irq(struct sp_device *sp, void *data);
struct sp_device *sp_get_psp_master_device(void);

#ifdef CONFIG_CRYPTO_DEV_SP_CCP

int ccp_dev_init(struct sp_device *sp);
void ccp_dev_destroy(struct sp_device *sp);

int ccp_dev_suspend(struct sp_device *sp, pm_message_t state);
int ccp_dev_resume(struct sp_device *sp);

#else	/* !CONFIG_CRYPTO_DEV_SP_CCP */

static inline int ccp_dev_init(struct sp_device *sp)
{
	return 0;
}
static inline void ccp_dev_destroy(struct sp_device *sp) { }

static inline int ccp_dev_suspend(struct sp_device *sp, pm_message_t state)
{
	return 0;
}
static inline int ccp_dev_resume(struct sp_device *sp)
{
	return 0;
}
#endif	/* CONFIG_CRYPTO_DEV_SP_CCP */

#ifdef CONFIG_CRYPTO_DEV_SP_PSP

int psp_dev_init(struct sp_device *sp);
void psp_pci_init(void);
void psp_dev_destroy(struct sp_device *sp);
void psp_pci_exit(void);

#else /* !CONFIG_CRYPTO_DEV_SP_PSP */

static inline int psp_dev_init(struct sp_device *sp) { return 0; }
static inline void psp_pci_init(void) { }
static inline void psp_dev_destroy(struct sp_device *sp) { }
static inline void psp_pci_exit(void) { }

#endif /* CONFIG_CRYPTO_DEV_SP_PSP */

#endif
