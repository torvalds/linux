// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FIPS 200 support.
 *
 * Copyright (c) 2008 Neil Horman <nhorman@tuxdriver.com>
 */

#include <linux/export.h>
#include <linux/fips.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysctl.h>

int fips_enabled;
EXPORT_SYMBOL_GPL(fips_enabled);

/* Process kernel command-line parameter at boot time. fips=0 or fips=1 */
static int fips_enable(char *str)
{
	fips_enabled = !!simple_strtol(str, NULL, 0);
	printk(KERN_INFO "fips mode: %s\n",
		fips_enabled ? "enabled" : "disabled");
	return 1;
}

__setup("fips=", fips_enable);

static struct ctl_table crypto_sysctl_table[] = {
	{
		.procname       = "fips_enabled",
		.data           = &fips_enabled,
		.maxlen         = sizeof(int),
		.mode           = 0444,
		.proc_handler   = proc_dointvec
	},
	{}
};

static struct ctl_table crypto_dir_table[] = {
	{
		.procname       = "crypto",
		.mode           = 0555,
		.child          = crypto_sysctl_table
	},
	{}
};

static struct ctl_table_header *crypto_sysctls;

static void crypto_proc_fips_init(void)
{
	crypto_sysctls = register_sysctl_table(crypto_dir_table);
}

static void crypto_proc_fips_exit(void)
{
	unregister_sysctl_table(crypto_sysctls);
}

static int __init fips_init(void)
{
	crypto_proc_fips_init();
	return 0;
}

static void __exit fips_exit(void)
{
	crypto_proc_fips_exit();
}

subsys_initcall(fips_init);
module_exit(fips_exit);
