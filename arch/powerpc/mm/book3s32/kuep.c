// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/kup.h>

void __init setup_kuep(bool disabled)
{
	pr_info("Activating Kernel Userspace Execution Prevention\n");

	if (disabled)
		pr_warn("KUEP cannot be disabled yet on 6xx when compiled in\n");
}
