// SPDX-License-Identifier: GPL-2.0-only

#include <asm/vendor_extensions/sifive.h>
#include <asm/vendor_extensions/sifive_hwprobe.h>
#include <asm/vendor_extensions/vendor_hwprobe.h>

#include <linux/cpumask.h>
#include <linux/types.h>

#include <uapi/asm/hwprobe.h>
#include <uapi/asm/vendor/sifive.h>

void hwprobe_isa_vendor_ext_sifive_0(struct riscv_hwprobe *pair, const struct cpumask *cpus)
{
	VENDOR_EXTENSION_SUPPORTED(pair, cpus,
				   riscv_isa_vendor_ext_list_sifive.per_hart_isa_bitmap, {
		VENDOR_EXT_KEY(XSFVQMACCDOD);
		VENDOR_EXT_KEY(XSFVQMACCQOQ);
		VENDOR_EXT_KEY(XSFVFNRCLIPXFQF);
		VENDOR_EXT_KEY(XSFVFWMACCQQQ);
	});
}
