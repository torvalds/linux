#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bios_ebda.h>

void __init x86_early_init_platform_quirks(void)
{
	x86_platform.legacy.rtc = 1;
	x86_platform.legacy.ebda_search = 0;

	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_PC:
		x86_platform.legacy.ebda_search = 1;
		break;
	case X86_SUBARCH_XEN:
	case X86_SUBARCH_LGUEST:
	case X86_SUBARCH_INTEL_MID:
		x86_platform.legacy.rtc = 0;
		break;
	}

	if (x86_platform.set_legacy_features)
		x86_platform.set_legacy_features();
}
