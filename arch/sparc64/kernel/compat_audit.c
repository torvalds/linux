#include <asm-sparc/unistd.h>

unsigned sparc32_dir_class[] = {
#include <asm-generic/audit_dir_write.h>
~0U
};

unsigned sparc32_chattr_class[] = {
#include <asm-generic/audit_change_attr.h>
~0U
};

unsigned sparc32_write_class[] = {
#include <asm-generic/audit_write.h>
~0U
};

unsigned sparc32_read_class[] = {
#include <asm-generic/audit_read.h>
~0U
};

unsigned sparc32_signal_class[] = {
#include <asm-generic/audit_signal.h>
~0U
};

int sparc32_classify_syscall(unsigned syscall)
{
	switch(syscall) {
	case __NR_open:
		return 2;
	case __NR_openat:
		return 3;
	case __NR_socketcall:
		return 4;
	case __NR_execve:
		return 5;
	default:
		return 1;
	}
}
