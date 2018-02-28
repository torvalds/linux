/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_HOTPLUG_CPU
#define ARM_CPU_DISCARD(x)
#define ARM_CPU_KEEP(x)		x
#else
#define ARM_CPU_DISCARD(x)	x
#define ARM_CPU_KEEP(x)
#endif

#if (defined(CONFIG_SMP_ON_UP) && !defined(CONFIG_DEBUG_SPINLOCK)) || \
	defined(CONFIG_GENERIC_BUG) || defined(CONFIG_JUMP_LABEL)
#define ARM_EXIT_KEEP(x)	x
#define ARM_EXIT_DISCARD(x)
#else
#define ARM_EXIT_KEEP(x)
#define ARM_EXIT_DISCARD(x)	x
#endif

#define PROC_INFO							\
		. = ALIGN(4);						\
		VMLINUX_SYMBOL(__proc_info_begin) = .;			\
		*(.proc.info.init)					\
		VMLINUX_SYMBOL(__proc_info_end) = .;

#define HYPERVISOR_TEXT							\
		VMLINUX_SYMBOL(__hyp_text_start) = .;			\
		*(.hyp.text)						\
		VMLINUX_SYMBOL(__hyp_text_end) = .;

#define IDMAP_TEXT							\
		ALIGN_FUNCTION();					\
		VMLINUX_SYMBOL(__idmap_text_start) = .;			\
		*(.idmap.text)						\
		VMLINUX_SYMBOL(__idmap_text_end) = .;			\
		. = ALIGN(PAGE_SIZE);					\
		VMLINUX_SYMBOL(__hyp_idmap_text_start) = .;		\
		*(.hyp.idmap.text)					\
		VMLINUX_SYMBOL(__hyp_idmap_text_end) = .;

