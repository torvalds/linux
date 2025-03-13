// SPDX-License-Identifier: GPL-2.0

#undef pr_fmt
#define pr_fmt(fmt)     "tdx: " fmt

#include <linux/array_size.h>
#include <linux/printk.h>
#include <asm/tdx.h>

#define DEF_TDX_ATTR_NAME(_name) [TDX_ATTR_##_name##_BIT] = __stringify(_name)

static __initdata const char *tdx_attributes[] = {
	DEF_TDX_ATTR_NAME(DEBUG),
	DEF_TDX_ATTR_NAME(HGS_PLUS_PROF),
	DEF_TDX_ATTR_NAME(PERF_PROF),
	DEF_TDX_ATTR_NAME(PMT_PROF),
	DEF_TDX_ATTR_NAME(ICSSD),
	DEF_TDX_ATTR_NAME(LASS),
	DEF_TDX_ATTR_NAME(SEPT_VE_DISABLE),
	DEF_TDX_ATTR_NAME(MIGRTABLE),
	DEF_TDX_ATTR_NAME(PKS),
	DEF_TDX_ATTR_NAME(KL),
	DEF_TDX_ATTR_NAME(TPA),
	DEF_TDX_ATTR_NAME(PERFMON),
};

#define DEF_TD_CTLS_NAME(_name) [TD_CTLS_##_name##_BIT] = __stringify(_name)

static __initdata const char *tdcs_td_ctls[] = {
	DEF_TD_CTLS_NAME(PENDING_VE_DISABLE),
	DEF_TD_CTLS_NAME(ENUM_TOPOLOGY),
	DEF_TD_CTLS_NAME(VIRT_CPUID2),
	DEF_TD_CTLS_NAME(REDUCE_VE),
	DEF_TD_CTLS_NAME(LOCK),
};

void __init tdx_dump_attributes(u64 td_attr)
{
	pr_info("Attributes:");

	for (int i = 0; i < ARRAY_SIZE(tdx_attributes); i++) {
		if (!tdx_attributes[i])
			continue;
		if (td_attr & BIT(i))
			pr_cont(" %s", tdx_attributes[i]);
		td_attr &= ~BIT(i);
	}

	if (td_attr)
		pr_cont(" unknown:%#llx", td_attr);
	pr_cont("\n");

}

void __init tdx_dump_td_ctls(u64 td_ctls)
{
	pr_info("TD_CTLS:");

	for (int i = 0; i < ARRAY_SIZE(tdcs_td_ctls); i++) {
		if (!tdcs_td_ctls[i])
			continue;
		if (td_ctls & BIT(i))
			pr_cont(" %s", tdcs_td_ctls[i]);
		td_ctls &= ~BIT(i);
	}
	if (td_ctls)
		pr_cont(" unknown:%#llx", td_ctls);
	pr_cont("\n");
}
