/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 Intel Corporation. All rights rsvd. */

#ifndef _PERFMON_H_
#define _PERFMON_H_

#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/sbitmap.h>
#include <linux/dmaengine.h>
#include <linux/percpu-rwsem.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/uuid.h>
#include <linux/idxd.h>
#include <linux/perf_event.h>
#include "registers.h"

static inline struct idxd_pmu *event_to_pmu(struct perf_event *event)
{
	struct idxd_pmu *idxd_pmu;
	struct pmu *pmu;

	pmu = event->pmu;
	idxd_pmu = container_of(pmu, struct idxd_pmu, pmu);

	return idxd_pmu;
}

static inline struct idxd_device *event_to_idxd(struct perf_event *event)
{
	struct idxd_pmu *idxd_pmu;
	struct pmu *pmu;

	pmu = event->pmu;
	idxd_pmu = container_of(pmu, struct idxd_pmu, pmu);

	return idxd_pmu->idxd;
}

static inline struct idxd_device *pmu_to_idxd(struct pmu *pmu)
{
	struct idxd_pmu *idxd_pmu;

	idxd_pmu = container_of(pmu, struct idxd_pmu, pmu);

	return idxd_pmu->idxd;
}

enum dsa_perf_events {
	DSA_PERF_EVENT_WQ = 0,
	DSA_PERF_EVENT_ENGINE,
	DSA_PERF_EVENT_ADDR_TRANS,
	DSA_PERF_EVENT_OP,
	DSA_PERF_EVENT_COMPL,
	DSA_PERF_EVENT_MAX,
};

enum filter_enc {
	FLT_WQ = 0,
	FLT_TC,
	FLT_PG_SZ,
	FLT_XFER_SZ,
	FLT_ENG,
	FLT_MAX,
};

#define CONFIG_RESET		0x0000000000000001
#define CNTR_RESET		0x0000000000000002
#define CNTR_ENABLE		0x0000000000000001
#define INTR_OVFL		0x0000000000000002

#define COUNTER_FREEZE		0x00000000FFFFFFFF
#define COUNTER_UNFREEZE	0x0000000000000000
#define OVERFLOW_SIZE		32

#define CNTRCFG_ENABLE		BIT(0)
#define CNTRCFG_IRQ_OVERFLOW	BIT(1)
#define CNTRCFG_CATEGORY_SHIFT	8
#define CNTRCFG_EVENT_SHIFT	32

#define PERFMON_TABLE_OFFSET(_idxd)				\
({								\
	typeof(_idxd) __idxd = (_idxd);				\
	((__idxd)->reg_base + (__idxd)->perfmon_offset);	\
})
#define PERFMON_REG_OFFSET(idxd, offset)			\
	(PERFMON_TABLE_OFFSET(idxd) + (offset))

#define PERFCAP_REG(idxd)	(PERFMON_REG_OFFSET(idxd, IDXD_PERFCAP_OFFSET))
#define PERFRST_REG(idxd)	(PERFMON_REG_OFFSET(idxd, IDXD_PERFRST_OFFSET))
#define OVFSTATUS_REG(idxd)	(PERFMON_REG_OFFSET(idxd, IDXD_OVFSTATUS_OFFSET))
#define PERFFRZ_REG(idxd)	(PERFMON_REG_OFFSET(idxd, IDXD_PERFFRZ_OFFSET))

#define FLTCFG_REG(idxd, cntr, flt)				\
	(PERFMON_REG_OFFSET(idxd, IDXD_FLTCFG_OFFSET) +	((cntr) * 32) + ((flt) * 4))

#define CNTRCFG_REG(idxd, cntr)					\
	(PERFMON_REG_OFFSET(idxd, IDXD_CNTRCFG_OFFSET) + ((cntr) * 8))
#define CNTRDATA_REG(idxd, cntr)					\
	(PERFMON_REG_OFFSET(idxd, IDXD_CNTRDATA_OFFSET) + ((cntr) * 8))
#define CNTRCAP_REG(idxd, cntr)					\
	(PERFMON_REG_OFFSET(idxd, IDXD_CNTRCAP_OFFSET) + ((cntr) * 8))

#define EVNTCAP_REG(idxd, category) \
	(PERFMON_REG_OFFSET(idxd, IDXD_EVNTCAP_OFFSET) + ((category) * 8))

#define DEFINE_PERFMON_FORMAT_ATTR(_name, _format)			\
static ssize_t __perfmon_idxd_##_name##_show(struct kobject *kobj,	\
				struct kobj_attribute *attr,		\
				char *page)				\
{									\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);			\
	return sprintf(page, _format "\n");				\
}									\
static struct kobj_attribute format_attr_idxd_##_name =			\
	__ATTR(_name, 0444, __perfmon_idxd_##_name##_show, NULL)

#endif
