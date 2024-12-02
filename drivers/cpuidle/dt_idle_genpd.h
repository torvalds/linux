/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DT_IDLE_GENPD
#define __DT_IDLE_GENPD

struct device_node;
struct generic_pm_domain;

#ifdef CONFIG_DT_IDLE_GENPD

void dt_idle_pd_free(struct generic_pm_domain *pd);

struct generic_pm_domain *dt_idle_pd_alloc(struct device_node *np,
			int (*parse_state)(struct device_node *, u32 *));

int dt_idle_pd_init_topology(struct device_node *np);

int dt_idle_pd_remove_topology(struct device_node *np);

struct device *dt_idle_attach_cpu(int cpu, const char *name);

void dt_idle_detach_cpu(struct device *dev);

#else

static inline void dt_idle_pd_free(struct generic_pm_domain *pd)
{
}

static inline struct generic_pm_domain *dt_idle_pd_alloc(
			struct device_node *np,
			int (*parse_state)(struct device_node *, u32 *))
{
	return NULL;
}

static inline int dt_idle_pd_init_topology(struct device_node *np)
{
	return 0;
}

static inline int dt_idle_pd_remove_topology(struct device_node *np)
{
	return 0;
}

static inline struct device *dt_idle_attach_cpu(int cpu, const char *name)
{
	return NULL;
}

static inline void dt_idle_detach_cpu(struct device *dev)
{
}

#endif

#endif
