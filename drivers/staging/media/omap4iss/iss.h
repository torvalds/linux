/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * TI OMAP4 ISS V4L2 Driver
 *
 * Copyright (C) 2012 Texas Instruments.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 */

#ifndef _OMAP4_ISS_H_
#define _OMAP4_ISS_H_

#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>

#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include <linux/platform_data/media/omap4iss.h>

#include "iss_regs.h"
#include "iss_csiphy.h"
#include "iss_csi2.h"
#include "iss_ipipeif.h"
#include "iss_ipipe.h"
#include "iss_resizer.h"

struct regmap;

#define to_iss_device(ptr_module)				\
	container_of(ptr_module, struct iss_device, ptr_module)
#define to_device(ptr_module)						\
	(to_iss_device(ptr_module)->dev)

enum iss_mem_resources {
	OMAP4_ISS_MEM_TOP,
	OMAP4_ISS_MEM_CSI2_A_REGS1,
	OMAP4_ISS_MEM_CAMERARX_CORE1,
	OMAP4_ISS_MEM_CSI2_B_REGS1,
	OMAP4_ISS_MEM_CAMERARX_CORE2,
	OMAP4_ISS_MEM_BTE,
	OMAP4_ISS_MEM_ISP_SYS1,
	OMAP4_ISS_MEM_ISP_RESIZER,
	OMAP4_ISS_MEM_ISP_IPIPE,
	OMAP4_ISS_MEM_ISP_ISIF,
	OMAP4_ISS_MEM_ISP_IPIPEIF,
	OMAP4_ISS_MEM_LAST,
};

enum iss_subclk_resource {
	OMAP4_ISS_SUBCLK_SIMCOP		= (1 << 0),
	OMAP4_ISS_SUBCLK_ISP		= (1 << 1),
	OMAP4_ISS_SUBCLK_CSI2_A		= (1 << 2),
	OMAP4_ISS_SUBCLK_CSI2_B		= (1 << 3),
	OMAP4_ISS_SUBCLK_CCP2		= (1 << 4),
};

enum iss_isp_subclk_resource {
	OMAP4_ISS_ISP_SUBCLK_BL		= (1 << 0),
	OMAP4_ISS_ISP_SUBCLK_ISIF	= (1 << 1),
	OMAP4_ISS_ISP_SUBCLK_H3A	= (1 << 2),
	OMAP4_ISS_ISP_SUBCLK_RSZ	= (1 << 3),
	OMAP4_ISS_ISP_SUBCLK_IPIPE	= (1 << 4),
	OMAP4_ISS_ISP_SUBCLK_IPIPEIF	= (1 << 5),
};

/*
 * struct iss_reg - Structure for ISS register values.
 * @reg: 32-bit Register address.
 * @val: 32-bit Register value.
 */
struct iss_reg {
	enum iss_mem_resources mmio_range;
	u32 reg;
	u32 val;
};

/*
 * struct iss_device - ISS device structure.
 * @syscon: Regmap for the syscon register space
 * @crashed: Crashed entities
 */
struct iss_device {
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct device *dev;
	u32 revision;

	/* platform HW resources */
	struct iss_platform_data *pdata;
	unsigned int irq_num;

	struct resource *res[OMAP4_ISS_MEM_LAST];
	void __iomem *regs[OMAP4_ISS_MEM_LAST];
	struct regmap *syscon;

	u64 raw_dmamask;

	struct mutex iss_mutex;	/* For handling ref_count field */
	struct media_entity_enum crashed;
	int has_context;
	int ref_count;

	struct clk *iss_fck;
	struct clk *iss_ctrlclk;

	/* ISS modules */
	struct iss_csi2_device csi2a;
	struct iss_csi2_device csi2b;
	struct iss_csiphy csiphy1;
	struct iss_csiphy csiphy2;
	struct iss_ipipeif_device ipipeif;
	struct iss_ipipe_device ipipe;
	struct iss_resizer_device resizer;

	unsigned int subclk_resources;
	unsigned int isp_subclk_resources;
};

int omap4iss_get_external_info(struct iss_pipeline *pipe,
			       struct media_link *link);

int omap4iss_module_sync_idle(struct media_entity *me, wait_queue_head_t *wait,
			      atomic_t *stopping);

int omap4iss_module_sync_is_stopping(wait_queue_head_t *wait,
				     atomic_t *stopping);

int omap4iss_pipeline_set_stream(struct iss_pipeline *pipe,
				 enum iss_pipeline_stream_state state);
void omap4iss_pipeline_cancel_stream(struct iss_pipeline *pipe);

void omap4iss_configure_bridge(struct iss_device *iss,
			       enum ipipeif_input_entity input);

struct iss_device *omap4iss_get(struct iss_device *iss);
void omap4iss_put(struct iss_device *iss);
int omap4iss_subclk_enable(struct iss_device *iss,
			   enum iss_subclk_resource res);
int omap4iss_subclk_disable(struct iss_device *iss,
			    enum iss_subclk_resource res);
void omap4iss_isp_subclk_enable(struct iss_device *iss,
				enum iss_isp_subclk_resource res);
void omap4iss_isp_subclk_disable(struct iss_device *iss,
				 enum iss_isp_subclk_resource res);

int omap4iss_register_entities(struct platform_device *pdev,
			       struct v4l2_device *v4l2_dev);
void omap4iss_unregister_entities(struct platform_device *pdev);

/*
 * iss_reg_read - Read the value of an OMAP4 ISS register
 * @iss: the ISS device
 * @res: memory resource in which the register is located
 * @offset: register offset in the memory resource
 *
 * Return the register value.
 */
static inline
u32 iss_reg_read(struct iss_device *iss, enum iss_mem_resources res,
		 u32 offset)
{
	return readl(iss->regs[res] + offset);
}

/*
 * iss_reg_write - Write a value to an OMAP4 ISS register
 * @iss: the ISS device
 * @res: memory resource in which the register is located
 * @offset: register offset in the memory resource
 * @value: value to be written
 */
static inline
void iss_reg_write(struct iss_device *iss, enum iss_mem_resources res,
		   u32 offset, u32 value)
{
	writel(value, iss->regs[res] + offset);
}

/*
 * iss_reg_clr - Clear bits in an OMAP4 ISS register
 * @iss: the ISS device
 * @res: memory resource in which the register is located
 * @offset: register offset in the memory resource
 * @clr: bit mask to be cleared
 */
static inline
void iss_reg_clr(struct iss_device *iss, enum iss_mem_resources res,
		 u32 offset, u32 clr)
{
	u32 v = iss_reg_read(iss, res, offset);

	iss_reg_write(iss, res, offset, v & ~clr);
}

/*
 * iss_reg_set - Set bits in an OMAP4 ISS register
 * @iss: the ISS device
 * @res: memory resource in which the register is located
 * @offset: register offset in the memory resource
 * @set: bit mask to be set
 */
static inline
void iss_reg_set(struct iss_device *iss, enum iss_mem_resources res,
		 u32 offset, u32 set)
{
	u32 v = iss_reg_read(iss, res, offset);

	iss_reg_write(iss, res, offset, v | set);
}

/*
 * iss_reg_update - Clear and set bits in an OMAP4 ISS register
 * @iss: the ISS device
 * @res: memory resource in which the register is located
 * @offset: register offset in the memory resource
 * @clr: bit mask to be cleared
 * @set: bit mask to be set
 *
 * Clear the clr mask first and then set the set mask.
 */
static inline
void iss_reg_update(struct iss_device *iss, enum iss_mem_resources res,
		    u32 offset, u32 clr, u32 set)
{
	u32 v = iss_reg_read(iss, res, offset);

	iss_reg_write(iss, res, offset, (v & ~clr) | set);
}

#define iss_poll_condition_timeout(cond, timeout, min_ival, max_ival)	\
({									\
	unsigned long __timeout = jiffies + usecs_to_jiffies(timeout);	\
	unsigned int __min_ival = (min_ival);				\
	unsigned int __max_ival = (max_ival);				\
	bool __cond;							\
	while (!(__cond = (cond))) {					\
		if (time_after(jiffies, __timeout))			\
			break;						\
		usleep_range(__min_ival, __max_ival);			\
	}								\
	!__cond;							\
})

#endif /* _OMAP4_ISS_H_ */
