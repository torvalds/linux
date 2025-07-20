// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Rivos Inc.
 */

#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/sprintf.h>

#include <asm/bugs.h>
#include <asm/vendor_extensions/thead.h>

static enum mitigation_state ghostwrite_state;

void ghostwrite_set_vulnerable(void)
{
	ghostwrite_state = VULNERABLE;
}

/*
 * Vendor extension alternatives will use the value set at the time of boot
 * alternative patching, thus this must be called before boot alternatives are
 * patched (and after extension probing) to be effective.
 *
 * Returns true if mitgated, false otherwise.
 */
bool ghostwrite_enable_mitigation(void)
{
	if (IS_ENABLED(CONFIG_RISCV_ISA_XTHEADVECTOR) &&
	    ghostwrite_state == VULNERABLE && !cpu_mitigations_off()) {
		disable_xtheadvector();
		ghostwrite_state = MITIGATED;
		return true;
	}

	return false;
}

enum mitigation_state ghostwrite_get_state(void)
{
	return ghostwrite_state;
}

ssize_t cpu_show_ghostwrite(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (IS_ENABLED(CONFIG_RISCV_ISA_XTHEADVECTOR)) {
		switch (ghostwrite_state) {
		case UNAFFECTED:
			return sprintf(buf, "Not affected\n");
		case MITIGATED:
			return sprintf(buf, "Mitigation: xtheadvector disabled\n");
		case VULNERABLE:
			fallthrough;
		default:
			return sprintf(buf, "Vulnerable\n");
		}
	} else {
		return sprintf(buf, "Not affected\n");
	}
}
