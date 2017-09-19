#include <errno.h>
#include <linux/unistd.h>

#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <sysdep/tls.h>

#ifndef PTRACE_GET_THREAD_AREA
#define PTRACE_GET_THREAD_AREA 25
#endif

#ifndef PTRACE_SET_THREAD_AREA
#define PTRACE_SET_THREAD_AREA 26
#endif

/* Checks whether host supports TLS, and sets *tls_min according to the value
 * valid on the host.
 * i386 host have it == 6; x86_64 host have it == 12, for i386 emulation. */
void check_host_supports_tls(int *supports_tls, int *tls_min)
{
	/* Values for x86 and x86_64.*/
	int val[] = {GDT_ENTRY_TLS_MIN_I386, GDT_ENTRY_TLS_MIN_X86_64};
	int i;

	for (i = 0; i < ARRAY_SIZE(val); i++) {
		user_desc_t info;
		info.entry_number = val[i];

		if (syscall(__NR_get_thread_area, &info) == 0) {
			*tls_min = val[i];
			*supports_tls = 1;
			return;
		} else {
			if (errno == EINVAL)
				continue;
			else if (errno == ENOSYS)
				*supports_tls = 0;
			return;
		}
	}

	*supports_tls = 0;
}

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
