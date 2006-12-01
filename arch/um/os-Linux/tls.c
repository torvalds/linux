#include <errno.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <asm/ldt.h>
#include "sysdep/tls.h"
#include "uml-config.h"

/* TLS support - we basically rely on the host's one.*/

/* In TT mode, this should be called only by the tracing thread, and makes sense
 * only for PTRACE_SET_THREAD_AREA. In SKAS mode, it's used normally.
 *
 */

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

#ifdef UML_CONFIG_MODE_SKAS

int os_get_thread_area(user_desc_t *info, int pid)
{
	int ret;

	ret = ptrace(PTRACE_GET_THREAD_AREA, pid, info->entry_number,
		     (unsigned long) info);
	if (ret < 0)
		ret = -errno;
	return ret;
}

#endif

#ifdef UML_CONFIG_MODE_TT
#include "linux/unistd.h"

int do_set_thread_area_tt(user_desc_t *info)
{
	int ret;

	ret = syscall(__NR_set_thread_area,info);
	if (ret < 0) {
		ret = -errno;
	}
	return ret;
}

int do_get_thread_area_tt(user_desc_t *info)
{
	int ret;

	ret = syscall(__NR_get_thread_area,info);
	if (ret < 0) {
		ret = -errno;
	}
	return ret;
}

#endif /* UML_CONFIG_MODE_TT */
