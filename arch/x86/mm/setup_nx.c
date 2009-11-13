#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/proto.h>

static int disable_nx __cpuinitdata;

/*
 * noexec = on|off
 *
 * Control non-executable mappings for processes.
 *
 * on      Enable
 * off     Disable
 */
static int __init noexec_setup(char *str)
{
	if (!str)
		return -EINVAL;
	if (!strncmp(str, "on", 2)) {
		disable_nx = 0;
	} else if (!strncmp(str, "off", 3)) {
		disable_nx = 1;
	}
	x86_configure_nx();
	return 0;
}
early_param("noexec", noexec_setup);

void __cpuinit x86_configure_nx(void)
{
	if (cpu_has_nx && !disable_nx)
		__supported_pte_mask |= _PAGE_NX;
	else
		__supported_pte_mask &= ~_PAGE_NX;
}
