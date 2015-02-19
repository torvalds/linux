#include <linux/init.h>
#include <linux/types.h>
#include <linux/audit.h>
#include <asm/unistd.h>

static unsigned dir_class[] = {
#include <asm-generic/audit_dir_write.h>
~0U
};

static unsigned read_class[] = {
#include <asm-generic/audit_read.h>
~0U
};

static unsigned write_class[] = {
#include <asm-generic/audit_write.h>
~0U
};

static unsigned chattr_class[] = {
#include <asm-generic/audit_change_attr.h>
~0U
};

static unsigned signal_class[] = {
#include <asm-generic/audit_signal.h>
~0U
};

int audit_classify_arch(int arch)
{
#ifdef CONFIG_COMPAT
	if (arch == AUDIT_ARCH_PARISC)
		return 1;
#endif
	return 0;
}

int audit_classify_syscall(int abi, unsigned syscall)
{
#ifdef CONFIG_COMPAT
	extern int parisc32_classify_syscall(unsigned);
	if (abi == AUDIT_ARCH_PARISC)
		return parisc32_classify_syscall(syscall);
#endif
	switch (syscall) {
	case __NR_open:
		return 2;
	case __NR_openat:
		return 3;
	case __NR_execve:
		return 5;
	default:
		return 0;
	}
}

static int __init audit_classes_init(void)
{
#ifdef CONFIG_COMPAT
	extern __u32 parisc32_dir_class[];
	extern __u32 parisc32_write_class[];
	extern __u32 parisc32_read_class[];
	extern __u32 parisc32_chattr_class[];
	extern __u32 parisc32_signal_class[];
	audit_register_class(AUDIT_CLASS_WRITE_32, parisc32_write_class);
	audit_register_class(AUDIT_CLASS_READ_32, parisc32_read_class);
	audit_register_class(AUDIT_CLASS_DIR_WRITE_32, parisc32_dir_class);
	audit_register_class(AUDIT_CLASS_CHATTR_32, parisc32_chattr_class);
	audit_register_class(AUDIT_CLASS_SIGNAL_32, parisc32_signal_class);
#endif
	audit_register_class(AUDIT_CLASS_WRITE, write_class);
	audit_register_class(AUDIT_CLASS_READ, read_class);
	audit_register_class(AUDIT_CLASS_DIR_WRITE, dir_class);
	audit_register_class(AUDIT_CLASS_CHATTR, chattr_class);
	audit_register_class(AUDIT_CLASS_SIGNAL, signal_class);
	return 0;
}

__initcall(audit_classes_init);
