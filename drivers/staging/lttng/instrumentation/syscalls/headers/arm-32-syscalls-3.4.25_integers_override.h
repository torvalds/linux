

#define OVERRIDE_TABLE_32_sys_arm_fadvise64_64
#define OVERRIDE_TABLE_32_sys_sync_file_range2

#ifndef CREATE_SYSCALL_TABLE

SC_TRACE_EVENT(sys_arm_fadvise64_64,
	TP_PROTO(int fd, int advice, loff_t offset, loff_t len),
	TP_ARGS(fd, advice, offset, len),
	TP_STRUCT__entry(
		__field_hex(int, fd)
		__field_hex(int, advice)
		__field_hex(loff_t, offset)
		__field_hex(loff_t, len)),
	TP_fast_assign(
		tp_assign(fd, fd)
		tp_assign(advice, advice)
		tp_assign(offset, offset)
		tp_assign(len, len)),
	TP_printk()
)

SC_TRACE_EVENT(sys_sync_file_range2,
	TP_PROTO(int fd, loff_t offset, loff_t nbytes, unsigned int flags),
	TP_ARGS(fd, offset, nbytes, flags),
	TP_STRUCT__entry(
		__field_hex(int, fd)
		__field_hex(loff_t, offset)
		__field_hex(loff_t, nbytes)
		__field_hex(unsigned int, flags)),
	TP_fast_assign(
		tp_assign(fd, fd)
		tp_assign(offset, offset)
		tp_assign(nbytes, nbytes)
		tp_assign(flags, flags)),
	TP_printk()
)

#else	/* CREATE_SYSCALL_TABLE */

#define OVVERRIDE_TABLE_32_sys_mmap
TRACE_SYSCALL_TABLE(sys_mmap, sys_mmap, 90, 6)

#define OVERRIDE_TABLE_32_sys_arm_fadvise64_64
TRACE_SYSCALL_TABLE(sys_arm_fadvise64_64, sys_arm_fadvise64_64, 270, 4)
#define OVERRIDE_TABLE_32_sys_sync_file_range2
TRACE_SYSCALL_TABLE(sys_sync_file_range2, sys_sync_file_range2, 341, 4)

#endif /* CREATE_SYSCALL_TABLE */


