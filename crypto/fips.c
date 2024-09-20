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
#include <linux/notifier.h>
#include <generated/utsrelease.h>

int fips_enabled;
EXPORT_SYMBOL_GPL(fips_enabled);

ATOMIC_NOTIFIER_HEAD(fips_fail_notif_chain);
EXPORT_SYMBOL_GPL(fips_fail_notif_chain);

/* Process kernel command-line parameter at boot time. fips=0 or fips=1 */
static int fips_enable(char *str)
{
	fips_enabled = !!simple_strtol(str, NULL, 0);
	printk(KERN_INFO "fips mode: %s\n",
		fips_enabled ? "enabled" : "disabled");
	return 1;
}

__setup("fips=", fips_enable);

#define FIPS_MODULE_NAME CONFIG_CRYPTO_FIPS_NAME
#ifdef CONFIG_CRYPTO_FIPS_CUSTOM_VERSION
#define FIPS_MODULE_VERSION CONFIG_CRYPTO_FIPS_VERSION
#else
#define FIPS_MODULE_VERSION UTS_RELEASE
#endif

static char fips_name[] = FIPS_MODULE_NAME;
static char fips_version[] = FIPS_MODULE_VERSION;

static struct ctl_table crypto_sysctl_table[] = {
	{
		.procname	= "fips_enabled",
		.data		= &fips_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "fips_name",
		.data		= &fips_name,
		.maxlen		= 64,
		.mode		= 0444,
		.proc_handler	= proc_dostring
	},
	{
		.procname	= "fips_version",
		.data		= &fips_version,
		.maxlen		= 64,
		.mode		= 0444,
		.proc_handler	= proc_dostring
	},
};

static struct ctl_table_header *crypto_sysctls;

static void crypto_proc_fips_init(void)
{
	crypto_sysctls = register_sysctl("crypto", crypto_sysctl_table);
}

static void crypto_proc_fips_exit(void)
{
	unregister_sysctl_table(crypto_sysctls);
}

void fips_fail_notify(void)
{
	if (fips_enabled)
		atomic_notifier_call_chain(&fips_fail_notif_chain, 0, NULL);
}
EXPORT_SYMBOL_GPL(fips_fail_notify);

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
