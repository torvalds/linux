/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_RESCTRL_INTERNAL_H
#define _ASM_X86_RESCTRL_INTERNAL_H

#include <linux/resctrl.h>

#define L3_QOS_CDP_ENABLE		0x01ULL

#define L2_QOS_CDP_ENABLE		0x01ULL

#define MBM_CNTR_WIDTH_BASE		24

#define MBA_IS_LINEAR			0x4

#define MBM_CNTR_WIDTH_OFFSET_AMD	20

#define RMID_VAL_ERROR			BIT_ULL(63)

#define RMID_VAL_UNAVAIL		BIT_ULL(62)

/*
 * With the above fields in use 62 bits remain in MSR_IA32_QM_CTR for
 * data to be returned. The counter width is discovered from the hardware
 * as an offset from MBM_CNTR_WIDTH_BASE.
 */
#define MBM_CNTR_WIDTH_OFFSET_MAX (62 - MBM_CNTR_WIDTH_BASE)

/**
 * struct arch_mbm_state - values used to compute resctrl_arch_rmid_read()s
 *			   return value.
 * @chunks:	Total data moved (multiply by rdt_group.mon_scale to get bytes)
 * @prev_msr:	Value of IA32_QM_CTR last time it was read for the RMID used to
 *		find this struct.
 */
struct arch_mbm_state {
	u64	chunks;
	u64	prev_msr;
};

/* Setting bit 0 in L3_QOS_EXT_CFG enables the ABMC feature. */
#define ABMC_ENABLE_BIT			0

/*
 * Qos Event Identifiers.
 */
#define ABMC_EXTENDED_EVT_ID		BIT(31)
#define ABMC_EVT_ID			BIT(0)

/**
 * struct rdt_hw_ctrl_domain - Arch private attributes of a set of CPUs that share
 *			       a resource for a control function
 * @d_resctrl:	Properties exposed to the resctrl file system
 * @ctrl_val:	array of cache or mem ctrl values (indexed by CLOSID)
 *
 * Members of this structure are accessed via helpers that provide abstraction.
 */
struct rdt_hw_ctrl_domain {
	struct rdt_ctrl_domain		d_resctrl;
	u32				*ctrl_val;
};

/**
 * struct rdt_hw_mon_domain - Arch private attributes of a set of CPUs that share
 *			      a resource for a monitor function
 * @d_resctrl:	Properties exposed to the resctrl file system
 * @arch_mbm_states:	Per-event pointer to the MBM event's saved state.
 *			An MBM event's state is an array of struct arch_mbm_state
 *			indexed by RMID on x86.
 *
 * Members of this structure are accessed via helpers that provide abstraction.
 */
struct rdt_hw_mon_domain {
	struct rdt_mon_domain		d_resctrl;
	struct arch_mbm_state		*arch_mbm_states[QOS_NUM_L3_MBM_EVENTS];
};

static inline struct rdt_hw_ctrl_domain *resctrl_to_arch_ctrl_dom(struct rdt_ctrl_domain *r)
{
	return container_of(r, struct rdt_hw_ctrl_domain, d_resctrl);
}

static inline struct rdt_hw_mon_domain *resctrl_to_arch_mon_dom(struct rdt_mon_domain *r)
{
	return container_of(r, struct rdt_hw_mon_domain, d_resctrl);
}

/**
 * struct msr_param - set a range of MSRs from a domain
 * @res:       The resource to use
 * @dom:       The domain to update
 * @low:       Beginning index from base MSR
 * @high:      End index
 */
struct msr_param {
	struct rdt_resource	*res;
	struct rdt_ctrl_domain	*dom;
	u32			low;
	u32			high;
};

/**
 * struct rdt_hw_resource - arch private attributes of a resctrl resource
 * @r_resctrl:		Attributes of the resource used directly by resctrl.
 * @num_closid:		Maximum number of closid this hardware can support,
 *			regardless of CDP. This is exposed via
 *			resctrl_arch_get_num_closid() to avoid confusion
 *			with struct resctrl_schema's property of the same name,
 *			which has been corrected for features like CDP.
 * @msr_base:		Base MSR address for CBMs
 * @msr_update:		Function pointer to update QOS MSRs
 * @mon_scale:		cqm counter * mon_scale = occupancy in bytes
 * @mbm_width:		Monitor width, to detect and correct for overflow.
 * @cdp_enabled:	CDP state of this resource
 * @mbm_cntr_assign_enabled:	ABMC feature is enabled
 *
 * Members of this structure are either private to the architecture
 * e.g. mbm_width, or accessed via helpers that provide abstraction. e.g.
 * msr_update and msr_base.
 */
struct rdt_hw_resource {
	struct rdt_resource	r_resctrl;
	u32			num_closid;
	unsigned int		msr_base;
	void			(*msr_update)(struct msr_param *m);
	unsigned int		mon_scale;
	unsigned int		mbm_width;
	bool			cdp_enabled;
	bool			mbm_cntr_assign_enabled;
};

static inline struct rdt_hw_resource *resctrl_to_arch_res(struct rdt_resource *r)
{
	return container_of(r, struct rdt_hw_resource, r_resctrl);
}

extern struct rdt_hw_resource rdt_resources_all[];

void arch_mon_domain_online(struct rdt_resource *r, struct rdt_mon_domain *d);

/* CPUID.(EAX=10H, ECX=ResID=1).EAX */
union cpuid_0x10_1_eax {
	struct {
		unsigned int cbm_len:5;
	} split;
	unsigned int full;
};

/* CPUID.(EAX=10H, ECX=ResID=3).EAX */
union cpuid_0x10_3_eax {
	struct {
		unsigned int max_delay:12;
	} split;
	unsigned int full;
};

/* CPUID.(EAX=10H, ECX=ResID).ECX */
union cpuid_0x10_x_ecx {
	struct {
		unsigned int reserved:3;
		unsigned int noncont:1;
	} split;
	unsigned int full;
};

/* CPUID.(EAX=10H, ECX=ResID).EDX */
union cpuid_0x10_x_edx {
	struct {
		unsigned int cos_max:16;
	} split;
	unsigned int full;
};

/*
 * ABMC counters are configured by writing to MSR_IA32_L3_QOS_ABMC_CFG.
 *
 * @bw_type		: Event configuration that represents the memory
 *			  transactions being tracked by the @cntr_id.
 * @bw_src		: Bandwidth source (RMID or CLOSID).
 * @reserved1		: Reserved.
 * @is_clos		: @bw_src field is a CLOSID (not an RMID).
 * @cntr_id		: Counter identifier.
 * @reserved		: Reserved.
 * @cntr_en		: Counting enable bit.
 * @cfg_en		: Configuration enable bit.
 *
 * Configuration and counting:
 * Counter can be configured across multiple writes to MSR. Configuration
 * is applied only when @cfg_en = 1. Counter @cntr_id is reset when the
 * configuration is applied.
 * @cfg_en = 1, @cntr_en = 0 : Apply @cntr_id configuration but do not
 *                             count events.
 * @cfg_en = 1, @cntr_en = 1 : Apply @cntr_id configuration and start
 *                             counting events.
 */
union l3_qos_abmc_cfg {
	struct {
		unsigned long bw_type  :32,
			      bw_src   :12,
			      reserved1: 3,
			      is_clos  : 1,
			      cntr_id  : 5,
			      reserved : 9,
			      cntr_en  : 1,
			      cfg_en   : 1;
	} split;
	unsigned long full;
};

void rdt_ctrl_update(void *arg);

int rdt_get_mon_l3_config(struct rdt_resource *r);

bool rdt_cpu_has(int flag);

void __init intel_rdt_mbm_apply_quirk(void);

void rdt_domain_reconfigure_cdp(struct rdt_resource *r);
void resctrl_arch_mbm_cntr_assign_set_one(struct rdt_resource *r);

#endif /* _ASM_X86_RESCTRL_INTERNAL_H */
