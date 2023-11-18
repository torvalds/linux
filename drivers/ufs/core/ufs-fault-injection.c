// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/fault-inject.h>
#include <linux/module.h>
#include <ufs/ufshcd.h>
#include "ufs-fault-injection.h"

static int ufs_fault_get(char *buffer, const struct kernel_param *kp);
static int ufs_fault_set(const char *val, const struct kernel_param *kp);

static const struct kernel_param_ops ufs_fault_ops = {
	.get = ufs_fault_get,
	.set = ufs_fault_set,
};

enum { FAULT_INJ_STR_SIZE = 80 };

/*
 * For more details about fault injection, please refer to
 * Documentation/fault-injection/fault-injection.rst.
 */
static char g_trigger_eh_str[FAULT_INJ_STR_SIZE];
module_param_cb(trigger_eh, &ufs_fault_ops, g_trigger_eh_str, 0644);
MODULE_PARM_DESC(trigger_eh,
	"Fault injection. trigger_eh=<interval>,<probability>,<space>,<times>");
static DECLARE_FAULT_ATTR(ufs_trigger_eh_attr);

static char g_timeout_str[FAULT_INJ_STR_SIZE];
module_param_cb(timeout, &ufs_fault_ops, g_timeout_str, 0644);
MODULE_PARM_DESC(timeout,
	"Fault injection. timeout=<interval>,<probability>,<space>,<times>");
static DECLARE_FAULT_ATTR(ufs_timeout_attr);

static int ufs_fault_get(char *buffer, const struct kernel_param *kp)
{
	const char *fault_str = kp->arg;

	return sysfs_emit(buffer, "%s\n", fault_str);
}

static int ufs_fault_set(const char *val, const struct kernel_param *kp)
{
	struct fault_attr *attr = NULL;

	if (kp->arg == g_trigger_eh_str)
		attr = &ufs_trigger_eh_attr;
	else if (kp->arg == g_timeout_str)
		attr = &ufs_timeout_attr;

	if (WARN_ON_ONCE(!attr))
		return -EINVAL;

	if (!setup_fault_attr(attr, (char *)val))
		return -EINVAL;

	strscpy(kp->arg, val, FAULT_INJ_STR_SIZE);

	return 0;
}

void ufs_fault_inject_hba_init(struct ufs_hba *hba)
{
	hba->trigger_eh_attr = ufs_trigger_eh_attr;
	hba->timeout_attr = ufs_timeout_attr;
#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS
	fault_create_debugfs_attr("trigger_eh_inject", hba->debugfs_root, &hba->trigger_eh_attr);
	fault_create_debugfs_attr("timeout_inject", hba->debugfs_root, &hba->timeout_attr);
#endif
}

bool ufs_trigger_eh(struct ufs_hba *hba)
{
	return should_fail(&hba->trigger_eh_attr, 1);
}

bool ufs_fail_completion(struct ufs_hba *hba)
{
	return should_fail(&hba->timeout_attr, 1);
}
