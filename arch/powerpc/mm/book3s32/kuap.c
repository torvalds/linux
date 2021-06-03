// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>

void __init setup_kuap(bool disabled)
{
	pr_info("Activating Kernel Userspace Access Protection\n");

	if (disabled)
		pr_warn("KUAP cannot be disabled yet on 6xx when compiled in\n");
}
