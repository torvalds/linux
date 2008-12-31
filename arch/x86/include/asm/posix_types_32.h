#ifndef _ASM_X86_POSIX_TYPES_32_H
#define _ASM_X86_POSIX_TYPES_32_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned long	__kernel_ino_t;
typedef unsigned short	__kernel_mode_t;
typedef unsigned short	__kernel_nlink_t;
typedef long		__kernel_off_t;
typedef int		__kernel_pid_t;
typedef unsigned short	__kernel_ipc_pid_t;
typedef unsigned short	__kernel_uid_t;
typedef unsigned short	__kernel_gid_t;
typedef unsigned int	__kernel_size_t;
typedef int		__kernel_ssize_t;
typedef int		__kernel_ptrdiff_t;
typedef long		__kernel_time_t;
typedef long		__kernel_suseconds_t;
typedef long		__kernel_clock_t;
typedef int		__kernel_timer_t;
typedef int		__kernel_clockid_t;
typedef int		__kernel_daddr_t;
typedef char *		__kernel_caddr_t;
typedef unsigned short	__kernel_uid16_t;
typedef unsigned short	__kernel_gid16_t;
typedef unsigned int	__kernel_uid32_t;
typedef unsigned int	__kernel_gid32_t;

typedef unsigned short	__kernel_old_uid_t;
typedef unsigned short	__kernel_old_gid_t;
typedef unsigned short	__kernel_old_dev_t;

#ifdef __GNUC__
typedef long long	__kernel_loff_t;
#endif

typedef struct {
	int	val[2];
} __kernel_fsid_t;

#if defined(__KERNEL__)

#undef	__FD_SET
#define __FD_SET(fd,fdsetp)					\
	asm volatile("btsl %1,%0":				\
		     "+m" (*(__kernel_fd_set *)(fdsetp))	\
		     : "r" ((int)(fd)))

#undef	__FD_CLR
#define __FD_CLR(fd,fdsetp)					\
	asm volatile("btrl %1,%0":				\
		     "+m" (*(__kernel_fd_set *)(fdsetp))	\
		     : "r" ((int) (fd)))

#undef	__FD_ISSET
#define __FD_ISSET(fd,fdsetp)					\
	(__extension__						\
	 ({							\
	 unsigned char __result;				\
	 asm volatile("btl %1,%2 ; setb %0"			\
		      : "=q" (__result)				\
		      : "r" ((int)(fd)),			\
			"m" (*(__kernel_fd_set *)(fdsetp)));	\
	 __result;						\
}))

#undef	__FD_ZERO
#define __FD_ZERO(fdsetp)					\
do {								\
	int __d0, __d1;						\
	asm volatile("cld ; rep ; stosl"			\
		     : "=m" (*(__kernel_fd_set *)(fdsetp)),	\
		       "=&c" (__d0), "=&D" (__d1)		\
		     : "a" (0), "1" (__FDSET_LONGS),		\
		       "2" ((__kernel_fd_set *)(fdsetp))	\
		     : "memory");				\
} while (0)

#endif /* defined(__KERNEL__) */

#endif /* _ASM_X86_POSIX_TYPES_32_H */
