/* SPDX-License-Identifier: MIT */
#ifndef __GK20A_DEVFREQ_H__
#define __GK20A_DEVFREQ_H__

#include <linux/devfreq.h>

struct gk20a_devfreq;

#if defined(CONFIG_PM_DEVFREQ)
int gk20a_devfreq_init(struct nvkm_clk *base, struct gk20a_devfreq **devfreq);

int gk20a_devfreq_resume(struct device *dev);
int gk20a_devfreq_suspend(struct device *dev);
#else
static inline int gk20a_devfreq_init(struct nvkm_clk *base, struct gk20a_devfreq **devfreq)
{
	return 0;
}

static inline int gk20a_devfreq_resume(struct device dev) { return 0; }
static inline int gk20a_devfreq_suspend(struct device *dev) { return 0; }
#endif /* CONFIG_PM_DEVFREQ */

#endif /* __GK20A_DEVFREQ_H__ */
