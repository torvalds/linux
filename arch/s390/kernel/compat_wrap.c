#include <linux/syscalls.h>
#include <linux/compat.h>

#define COMPAT_SYSCALL_WRAP1(name, ...) \
	COMPAT_SYSCALL_WRAPx(1, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP2(name, ...) \
	COMPAT_SYSCALL_WRAPx(2, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP3(name, ...) \
	COMPAT_SYSCALL_WRAPx(3, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP4(name, ...) \
	COMPAT_SYSCALL_WRAPx(4, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP5(name, ...) \
	COMPAT_SYSCALL_WRAPx(5, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP6(name, ...) \
	COMPAT_SYSCALL_WRAPx(6, _##name, __VA_ARGS__)

#define COMPAT_SYSCALL_WRAPx(x, name, ...)					\
	asmlinkage long compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));	\
	asmlinkage long compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))	\
	{									\
		return sys##name(__MAP(x,__SC_DELOUSE,__VA_ARGS__));		\
	}

COMPAT_SYSCALL_WRAP1(exit, int, error_code);
COMPAT_SYSCALL_WRAP1(close, unsigned int, fd);
COMPAT_SYSCALL_WRAP2(creat, const char __user *, pathname, umode_t, mode);
COMPAT_SYSCALL_WRAP2(link, const char __user *, oldname, const char __user *, newname);
COMPAT_SYSCALL_WRAP1(unlink, const char __user *, pathname);
COMPAT_SYSCALL_WRAP1(chdir, const char __user *, filename);
COMPAT_SYSCALL_WRAP3(mknod, const char __user *, filename, umode_t, mode, unsigned, dev);
COMPAT_SYSCALL_WRAP2(chmod, const char __user *, filename, umode_t, mode);
COMPAT_SYSCALL_WRAP1(oldumount, char __user *, name);
COMPAT_SYSCALL_WRAP1(alarm, unsigned int, seconds);
COMPAT_SYSCALL_WRAP2(access, const char __user *, filename, int, mode);
COMPAT_SYSCALL_WRAP1(nice, int, increment);
