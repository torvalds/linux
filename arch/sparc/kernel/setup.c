// SPDX-License-Identifier: GPL-2.0

#include <asm/setup.h>
#include <linux/sysctl.h>
#include <linux/panic.h>
#include <linux/printk.h>

static const struct ctl_table sparc_sysctl_table[] = {
	{
		.procname	= "reboot-cmd",
		.data		= reboot_command,
		.maxlen		= 256,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "stop-a",
		.data		= &stop_a_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "scons-poweroff",
		.data		= &scons_pwroff,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_SPARC64
	{
		.procname	= "tsb-ratio",
		.data		= &sysctl_tsb_ratio,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
};

void arch_do_panic(void)
{
	/* Make sure the user can actually press Stop-A (L1-A) */
	stop_a_enabled = 1;
	pr_emerg("Press Stop-A (L1-A) from sun keyboard or send break\n"
		 "twice on console to return to the boot prom\n");
}

static int __init init_sparc_sysctls(void)
{
	register_sysctl_init("kernel", sparc_sysctl_table);
	return 0;
}

arch_initcall(init_sparc_sysctls);
