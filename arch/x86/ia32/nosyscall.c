#include <linux/kernel.h>
#include <linux/errno.h>

long compat_ni_syscall(void)
{
	return -ENOSYS;
}
