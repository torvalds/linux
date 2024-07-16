// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bpf.h>
#include <linux/cpu.h>
#include <linux/device.h>

#include <asm/spectre.h>

static bool _unprivileged_ebpf_enabled(void)
{
#ifdef CONFIG_BPF_SYSCALL
	return !sysctl_unprivileged_bpf_disabled;
#else
	return false;
#endif
}

ssize_t cpu_show_spectre_v1(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "Mitigation: __user pointer sanitization\n");
}

static unsigned int spectre_v2_state;
static unsigned int spectre_v2_methods;

void spectre_v2_update_state(unsigned int state, unsigned int method)
{
	if (state > spectre_v2_state)
		spectre_v2_state = state;
	spectre_v2_methods |= method;
}

ssize_t cpu_show_spectre_v2(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	const char *method;

	if (spectre_v2_state == SPECTRE_UNAFFECTED)
		return sprintf(buf, "%s\n", "Not affected");

	if (spectre_v2_state != SPECTRE_MITIGATED)
		return sprintf(buf, "%s\n", "Vulnerable");

	if (_unprivileged_ebpf_enabled())
		return sprintf(buf, "Vulnerable: Unprivileged eBPF enabled\n");

	switch (spectre_v2_methods) {
	case SPECTRE_V2_METHOD_BPIALL:
		method = "Branch predictor hardening";
		break;

	case SPECTRE_V2_METHOD_ICIALLU:
		method = "I-cache invalidation";
		break;

	case SPECTRE_V2_METHOD_SMC:
	case SPECTRE_V2_METHOD_HVC:
		method = "Firmware call";
		break;

	case SPECTRE_V2_METHOD_LOOP8:
		method = "History overwrite";
		break;

	default:
		method = "Multiple mitigations";
		break;
	}

	return sprintf(buf, "Mitigation: %s\n", method);
}
