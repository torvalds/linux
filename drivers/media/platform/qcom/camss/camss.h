/* SPDX-License-Identifier: GPL-2.0 */
/*
 * camss.h
 *
 * Qualcomm MSM Camera Subsystem - Core
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#ifndef QC_MSM_CAMSS_H
#define QC_MSM_CAMSS_H

#include <linux/device.h>
#include <linux/types.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/media-device.h>
#include <media/media-entity.h>

#include "camss-csid.h"
#include "camss-csiphy.h"
#include "camss-ispif.h"
#include "camss-vfe.h"

#define to_camss(ptr_module)	\
	container_of(ptr_module, struct camss, ptr_module)

#define to_device(ptr_module)	\
	(to_camss(ptr_module)->dev)

#define module_pointer(ptr_module, index)	\
	((const struct ptr_module##_device (*)[]) &(ptr_module[-(index)]))

#define to_camss_index(ptr_module, index)	\
	container_of(module_pointer(ptr_module, index),	\
		     struct camss, ptr_module)

#define to_device_index(ptr_module, index)	\
	(to_camss_index(ptr_module, index)->dev)

#define CAMSS_RES_MAX 17

struct resources {
	char *regulators[CAMSS_RES_MAX];
	char *clock[CAMSS_RES_MAX];
	u32 clock_rate[CAMSS_RES_MAX][CAMSS_RES_MAX];
	char *reg[CAMSS_RES_MAX];
	char *interrupt[CAMSS_RES_MAX];
};

struct resources_ispif {
	char *clock[CAMSS_RES_MAX];
	char *clock_for_reset[CAMSS_RES_MAX];
	char *reg[CAMSS_RES_MAX];
	char *interrupt;
};

struct icc_bw_tbl {
	u32 avg;
	u32 peak;
};

struct resources_icc {
	char *name;
	struct icc_bw_tbl icc_bw_tbl;
};

enum pm_domain {
	PM_DOMAIN_VFE0 = 0,
	PM_DOMAIN_VFE1 = 1,
	PM_DOMAIN_VFELITE = 2,		/* VFELITE / TOP GDSC */
};

enum camss_version {
	CAMSS_8x16,
	CAMSS_8x96,
	CAMSS_660,
	CAMSS_845,
	CAMSS_8250,
};

enum icc_count {
	ICC_DEFAULT_COUNT = 0,
	ICC_SM8250_COUNT = 4,
};

struct camss {
	enum camss_version version;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
	struct media_device media_dev;
	struct device *dev;
	int csiphy_num;
	struct csiphy_device *csiphy;
	int csid_num;
	struct csid_device *csid;
	struct ispif_device *ispif;
	int vfe_num;
	struct vfe_device *vfe;
	atomic_t ref_count;
	int genpd_num;
	struct device **genpd;
	struct device_link **genpd_link;
	struct icc_path *icc_path[ICC_SM8250_COUNT];
	struct icc_bw_tbl icc_bw_tbl[ICC_SM8250_COUNT];
};

struct camss_camera_interface {
	u8 csiphy_id;
	struct csiphy_csi2_cfg csi2;
};

struct camss_async_subdev {
	struct v4l2_async_subdev asd; /* must be first */
	struct camss_camera_interface interface;
};

struct camss_clock {
	struct clk *clk;
	const char *name;
	u32 *freq;
	u32 nfreqs;
};

void camss_add_clock_margin(u64 *rate);
int camss_enable_clocks(int nclocks, struct camss_clock *clock,
			struct device *dev);
void camss_disable_clocks(int nclocks, struct camss_clock *clock);
struct media_entity *camss_find_sensor(struct media_entity *entity);
s64 camss_get_link_freq(struct media_entity *entity, unsigned int bpp,
			unsigned int lanes);
int camss_get_pixel_clock(struct media_entity *entity, u64 *pixel_clock);
int camss_pm_domain_on(struct camss *camss, int id);
void camss_pm_domain_off(struct camss *camss, int id);
void camss_delete(struct camss *camss);

#endif /* QC_MSM_CAMSS_H */
