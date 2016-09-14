#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bios_ebda.h>

void __init x86_early_init_platform_quirks(void)
{
	x86_platform.legacy.rtc = 1;
	x86_platform.legacy.reserve_bios_regions = 0;
	x86_platform.legacy.devices.pnpbios = 1;

	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_PC:
		x86_platform.legacy.reserve_bios_regions = 1;
		break;
	case X86_SUBARCH_XEN:
	case X86_SUBARCH_LGUEST:
	case X86_SUBARCH_INTEL_MID:
	case X86_SUBARCH_CE4100:
		x86_platform.legacy.devices.pnpbios = 0;
		x86_platform.legacy.rtc = 0;
		break;
	}

	if (x86_platform.set_legacy_features)
		x86_platform.set_legacy_features();
}

#if defined(CONFIG_PNPBIOS)
bool __init arch_pnpbios_disabled(void)
{
	return x86_platform.legacy.devices.pnpbios == 0;
}
#endif
