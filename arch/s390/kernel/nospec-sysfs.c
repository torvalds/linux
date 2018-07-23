// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/cpu.h>
#include <asm/facility.h>
#include <asm/nospec-branch.h>

ssize_t cpu_show_spectre_v1(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Mitigation: __user pointer sanitization\n");
}

ssize_t cpu_show_spectre_v2(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	if (IS_ENABLED(CC_USING_EXPOLINE) && !nospec_disable)
		return sprintf(buf, "Mitigation: execute trampolines\n");
	if (__test_facility(82, S390_lowcore.alt_stfle_fac_list))
		return sprintf(buf, "Mitigation: limited branch prediction\n");
	return sprintf(buf, "Vulnerable\n");
}
