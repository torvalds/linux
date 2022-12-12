/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 */

#ifndef _CORESIGHT_PRIV_H
#define _CORESIGHT_PRIV_H

#include <linux/amba/bus.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/coresight.h>
#include <linux/pm_runtime.h>

/*
 * Coresight management registers (0xf00-0xfcc)
 * 0xfa0 - 0xfa4: Management	registers in PFTv1.0
 *		  Trace		registers in PFTv1.1
 */
#define CORESIGHT_ITCTRL	0xf00
#define CORESIGHT_CLAIMSET	0xfa0
#define CORESIGHT_CLAIMCLR	0xfa4
#define CORESIGHT_LAR		0xfb0
#define CORESIGHT_LSR		0xfb4
#define CORESIGHT_DEVARCH	0xfbc
#define CORESIGHT_AUTHSTATUS	0xfb8
#define CORESIGHT_DEVID		0xfc8
#define CORESIGHT_DEVTYPE	0xfcc


/*
 * Coresight device CLAIM protocol.
 * See PSCI - ARM DEN 0022D, Section: 6.8.1 Debug and Trace save and restore.
 */
#define CORESIGHT_CLAIM_SELF_HOSTED	BIT(1)

#define TIMEOUT_US		100
#define BMVAL(val, lsb, msb)	((val & GENMASK(msb, lsb)) >> lsb)

#define ETM_MODE_EXCL_KERN	BIT(30)
#define ETM_MODE_EXCL_USER	BIT(31)
struct cs_pair_attribute {
	struct device_attribute attr;
	u32 lo_off;
	u32 hi_off;
};

struct cs_off_attribute {
	struct device_attribute attr;
	u32 off;
};

extern ssize_t coresight_simple_show32(struct device *_dev,
				     struct device_attribute *attr, char *buf);
extern ssize_t coresight_simple_show_pair(struct device *_dev,
				     struct device_attribute *attr, char *buf);

#define coresight_simple_reg32(name, offset)				\
	(&((struct cs_off_attribute[]) {				\
	   {								\
		__ATTR(name, 0444, coresight_simple_show32, NULL),	\
		offset							\
	   }								\
	})[0].attr.attr)

#define coresight_simple_reg64(name, lo_off, hi_off)			\
	(&((struct cs_pair_attribute[]) {				\
	   {								\
		__ATTR(name, 0444, coresight_simple_show_pair, NULL),	\
		lo_off, hi_off						\
	   }								\
	})[0].attr.attr)

extern const u32 coresight_barrier_pkt[4];
#define CORESIGHT_BARRIER_PKT_SIZE (sizeof(coresight_barrier_pkt))

enum etm_addr_type {
	ETM_ADDR_TYPE_NONE,
	ETM_ADDR_TYPE_SINGLE,
	ETM_ADDR_TYPE_RANGE,
	ETM_ADDR_TYPE_START,
	ETM_ADDR_TYPE_STOP,
};

enum cs_mode {
	CS_MODE_DISABLED,
	CS_MODE_SYSFS,
	CS_MODE_PERF,
};

/**
 * struct cs_buffer - keep track of a recording session' specifics
 * @cur:	index of the current buffer
 * @nr_pages:	max number of pages granted to us
 * @pid:	PID this cs_buffer belongs to
 * @offset:	offset within the current buffer
 * @data_size:	how much we collected in this run
 * @snapshot:	is this run in snapshot mode
 * @data_pages:	a handle the ring buffer
 */
struct cs_buffers {
	unsigned int		cur;
	unsigned int		nr_pages;
	pid_t			pid;
	unsigned long		offset;
	local_t			data_size;
	bool			snapshot;
	void			**data_pages;
};

static inline void coresight_insert_barrier_packet(void *buf)
{
	if (buf)
		memcpy(buf, coresight_barrier_pkt, CORESIGHT_BARRIER_PKT_SIZE);
}

static inline void CS_LOCK(void __iomem *addr)
{
	do {
		/* Wait for things to settle */
		mb();
		writel_relaxed(0x0, addr + CORESIGHT_LAR);
	} while (0);
}

static inline void CS_UNLOCK(void __iomem *addr)
{
	do {
		writel_relaxed(CORESIGHT_UNLOCK, addr + CORESIGHT_LAR);
		/* Make sure everyone has seen this */
		mb();
	} while (0);
}

void coresight_disable_path(struct list_head *path);
int coresight_enable_path(struct list_head *path, u32 mode, void *sink_data);
struct coresight_device *coresight_get_sink(struct list_head *path);
struct coresight_device *
coresight_get_enabled_sink(struct coresight_device *source);
struct coresight_device *coresight_get_sink_by_id(u32 id);
struct coresight_device *
coresight_find_default_sink(struct coresight_device *csdev);
struct list_head *coresight_build_path(struct coresight_device *csdev,
				       struct coresight_device *sink);
void coresight_release_path(struct list_head *path);
int coresight_add_sysfs_link(struct coresight_sysfs_link *info);
void coresight_remove_sysfs_link(struct coresight_sysfs_link *info);
int coresight_create_conns_sysfs_group(struct coresight_device *csdev);
void coresight_remove_conns_sysfs_group(struct coresight_device *csdev);
int coresight_make_links(struct coresight_device *orig,
			 struct coresight_connection *conn,
			 struct coresight_device *target);
void coresight_remove_links(struct coresight_device *orig,
			    struct coresight_connection *conn);

#if IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM3X)
extern int etm_readl_cp14(u32 off, unsigned int *val);
extern int etm_writel_cp14(u32 off, u32 val);
#else
static inline int etm_readl_cp14(u32 off, unsigned int *val) { return 0; }
static inline int etm_writel_cp14(u32 off, u32 val) { return 0; }
#endif

struct cti_assoc_op {
	void (*add)(struct coresight_device *csdev);
	void (*remove)(struct coresight_device *csdev);
};

extern void coresight_set_cti_ops(const struct cti_assoc_op *cti_op);
extern void coresight_remove_cti_ops(void);

/*
 * Macros and inline functions to handle CoreSight UCI data and driver
 * private data in AMBA ID table entries, and extract data values.
 */

/* coresight AMBA ID, no UCI, no driver data: id table entry */
#define CS_AMBA_ID(pid)			\
	{				\
		.id	= pid,		\
		.mask	= 0x000fffff,	\
	}

/* coresight AMBA ID, UCI with driver data only: id table entry. */
#define CS_AMBA_ID_DATA(pid, dval)				\
	{							\
		.id	= pid,					\
		.mask	= 0x000fffff,				\
		.data	=  (void *)&(struct amba_cs_uci_id)	\
			{				\
				.data = (void *)dval,	\
			}				\
	}

/* coresight AMBA ID, full UCI structure: id table entry. */
#define CS_AMBA_UCI_ID(pid, uci_ptr)		\
	{					\
		.id	= pid,			\
		.mask	= 0x000fffff,		\
		.data	= (void *)uci_ptr	\
	}

/* extract the data value from a UCI structure given amba_id pointer. */
static inline void *coresight_get_uci_data(const struct amba_id *id)
{
	struct amba_cs_uci_id *uci_id = id->data;

	if (!uci_id)
		return NULL;

	return uci_id->data;
}

void coresight_release_platform_data(struct coresight_device *csdev,
				     struct coresight_platform_data *pdata);
struct coresight_device *
coresight_find_csdev_by_fwnode(struct fwnode_handle *r_fwnode);
void coresight_set_assoc_ectdev_mutex(struct coresight_device *csdev,
				      struct coresight_device *ect_csdev);

void coresight_set_percpu_sink(int cpu, struct coresight_device *csdev);
struct coresight_device *coresight_get_percpu_sink(int cpu);

#endif
