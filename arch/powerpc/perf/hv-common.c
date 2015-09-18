#include <asm/io.h>
#include <asm/hvcall.h>

#include "hv-gpci.h"
#include "hv-common.h"

unsigned long hv_perf_caps_get(struct hv_perf_caps *caps)
{
	unsigned long r;
	struct p {
		struct hv_get_perf_counter_info_params params;
		struct hv_gpci_system_performance_capabilities caps;
	} __packed __aligned(sizeof(uint64_t));

	struct p arg = {
		.params = {
			.counter_request = cpu_to_be32(
				HV_GPCI_system_performance_capabilities),
			.starting_index = cpu_to_be32(-1),
			.counter_info_version_in = 0,
		}
	};

	r = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO,
			       virt_to_phys(&arg), sizeof(arg));

	if (r)
		return r;

	pr_devel("capability_mask: 0x%x\n", arg.caps.capability_mask);

	caps->version = arg.params.counter_info_version_out;
	caps->collect_privileged = !!arg.caps.perf_collect_privileged;
	caps->ga = !!(arg.caps.capability_mask & HV_GPCI_CM_GA);
	caps->expanded = !!(arg.caps.capability_mask & HV_GPCI_CM_EXPANDED);
	caps->lab = !!(arg.caps.capability_mask & HV_GPCI_CM_LAB);

	return r;
}
