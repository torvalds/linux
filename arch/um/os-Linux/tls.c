#include <errno.h>
#include <sys/ptrace.h>
#include "sysdep/tls.h"

/* TLS support - we basically rely on the host's one.*/

#ifndef PTRACE_GET_THREAD_AREA
#define PTRACE_GET_THREAD_AREA 25
#endif

#ifndef PTRACE_SET_THREAD_AREA
#define PTRACE_SET_THREAD_AREA 26
#endif

int os_set_thread_area(user_desc_t *info, int pid)
{
	int ret;

	ret = ptrace(PTRACE_SET_THREAD_AREA, pid, info->entry_number,
		     (unsigned long) info);
	if (ret < 0)
		ret = -errno;
	return ret;
}

int os_get_thread_area(user_desc_t *info, int pid)
{
	int ret;

	ret = ptrace(PTRACE_GET_THREAD_AREA, pid, info->entry_number,
		     (unsigned long) info);
	if (ret < 0)
		ret = -errno;
	return ret;
}
