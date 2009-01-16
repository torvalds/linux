#ifndef __S390_VDSO_H__
#define __S390_VDSO_H__

#ifdef __KERNEL__

/* Default link addresses for the vDSOs */
#define VDSO32_LBASE	0
#define VDSO64_LBASE	0

#define VDSO_VERSION_STRING	LINUX_2.6.26

#ifndef __ASSEMBLY__

/*
 * Note about the vdso_data and vdso_per_cpu_data structures:
 *
 * NEVER USE THEM IN USERSPACE CODE DIRECTLY. The layout of the
 * structure is supposed to be known only to the function in the vdso
 * itself and may change without notice.
 */

struct vdso_data {
	__u64 tb_update_count;		/* Timebase atomicity ctr	0x00 */
	__u64 xtime_tod_stamp;		/* TOD clock for xtime		0x08 */
	__u64 xtime_clock_sec;		/* Kernel time			0x10 */
	__u64 xtime_clock_nsec;		/*				0x18 */
	__u64 wtom_clock_sec;		/* Wall to monotonic clock	0x20 */
	__u64 wtom_clock_nsec;		/*				0x28 */
	__u32 tz_minuteswest;		/* Minutes west of Greenwich	0x30 */
	__u32 tz_dsttime;		/* Type of dst correction	0x34 */
	__u32 ectg_available;
};

struct vdso_per_cpu_data {
	__u64 ectg_timer_base;
	__u64 ectg_user_time;
};

extern struct vdso_data *vdso_data;

#ifdef CONFIG_64BIT
int vdso_alloc_per_cpu(int cpu, struct _lowcore *lowcore);
void vdso_free_per_cpu(int cpu, struct _lowcore *lowcore);
#endif

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __S390_VDSO_H__ */
