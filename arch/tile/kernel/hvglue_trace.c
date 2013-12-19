/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/*
 * Pull in the hypervisor header so we declare all the ABI functions
 * with the underscore versions, then undef the names so that we can
 * provide our own wrapper versions.
 */
#define hv_init _hv_init
#define hv_install_context _hv_install_context
#define hv_sysconf _hv_sysconf
#define hv_get_rtc _hv_get_rtc
#define hv_set_rtc _hv_set_rtc
#define hv_flush_asid _hv_flush_asid
#define hv_flush_page _hv_flush_page
#define hv_flush_pages _hv_flush_pages
#define hv_restart _hv_restart
#define hv_halt _hv_halt
#define hv_power_off _hv_power_off
#define hv_inquire_physical _hv_inquire_physical
#define hv_inquire_memory_controller _hv_inquire_memory_controller
#define hv_inquire_virtual _hv_inquire_virtual
#define hv_inquire_asid _hv_inquire_asid
#define hv_nanosleep _hv_nanosleep
#define hv_console_read_if_ready _hv_console_read_if_ready
#define hv_console_write _hv_console_write
#define hv_downcall_dispatch _hv_downcall_dispatch
#define hv_inquire_topology _hv_inquire_topology
#define hv_fs_findfile _hv_fs_findfile
#define hv_fs_fstat _hv_fs_fstat
#define hv_fs_pread _hv_fs_pread
#define hv_physaddr_read64 _hv_physaddr_read64
#define hv_physaddr_write64 _hv_physaddr_write64
#define hv_get_command_line _hv_get_command_line
#define hv_set_caching _hv_set_caching
#define hv_bzero_page _hv_bzero_page
#define hv_register_message_state _hv_register_message_state
#define hv_send_message _hv_send_message
#define hv_receive_message _hv_receive_message
#define hv_inquire_context _hv_inquire_context
#define hv_start_all_tiles _hv_start_all_tiles
#define hv_dev_open _hv_dev_open
#define hv_dev_close _hv_dev_close
#define hv_dev_pread _hv_dev_pread
#define hv_dev_pwrite _hv_dev_pwrite
#define hv_dev_poll _hv_dev_poll
#define hv_dev_poll_cancel _hv_dev_poll_cancel
#define hv_dev_preada _hv_dev_preada
#define hv_dev_pwritea _hv_dev_pwritea
#define hv_flush_remote _hv_flush_remote
#define hv_console_putc _hv_console_putc
#define hv_inquire_tiles _hv_inquire_tiles
#define hv_confstr _hv_confstr
#define hv_reexec _hv_reexec
#define hv_set_command_line _hv_set_command_line
#define hv_clear_intr _hv_clear_intr
#define hv_enable_intr _hv_enable_intr
#define hv_disable_intr _hv_disable_intr
#define hv_raise_intr _hv_raise_intr
#define hv_trigger_ipi _hv_trigger_ipi
#define hv_store_mapping _hv_store_mapping
#define hv_inquire_realpa _hv_inquire_realpa
#define hv_flush_all _hv_flush_all
#define hv_get_ipi_pte _hv_get_ipi_pte
#define hv_set_pte_super_shift _hv_set_pte_super_shift
#define hv_console_set_ipi _hv_console_set_ipi
#include <hv/hypervisor.h>
#undef hv_init
#undef hv_install_context
#undef hv_sysconf
#undef hv_get_rtc
#undef hv_set_rtc
#undef hv_flush_asid
#undef hv_flush_page
#undef hv_flush_pages
#undef hv_restart
#undef hv_halt
#undef hv_power_off
#undef hv_inquire_physical
#undef hv_inquire_memory_controller
#undef hv_inquire_virtual
#undef hv_inquire_asid
#undef hv_nanosleep
#undef hv_console_read_if_ready
#undef hv_console_write
#undef hv_downcall_dispatch
#undef hv_inquire_topology
#undef hv_fs_findfile
#undef hv_fs_fstat
#undef hv_fs_pread
#undef hv_physaddr_read64
#undef hv_physaddr_write64
#undef hv_get_command_line
#undef hv_set_caching
#undef hv_bzero_page
#undef hv_register_message_state
#undef hv_send_message
#undef hv_receive_message
#undef hv_inquire_context
#undef hv_start_all_tiles
#undef hv_dev_open
#undef hv_dev_close
#undef hv_dev_pread
#undef hv_dev_pwrite
#undef hv_dev_poll
#undef hv_dev_poll_cancel
#undef hv_dev_preada
#undef hv_dev_pwritea
#undef hv_flush_remote
#undef hv_console_putc
#undef hv_inquire_tiles
#undef hv_confstr
#undef hv_reexec
#undef hv_set_command_line
#undef hv_clear_intr
#undef hv_enable_intr
#undef hv_disable_intr
#undef hv_raise_intr
#undef hv_trigger_ipi
#undef hv_store_mapping
#undef hv_inquire_realpa
#undef hv_flush_all
#undef hv_get_ipi_pte
#undef hv_set_pte_super_shift
#undef hv_console_set_ipi

/*
 * Provide macros based on <linux/syscalls.h> to provide a wrapper
 * function that invokes the same function with an underscore prefix.
 * We can't use the existing __SC_xxx macros because we need to
 * support up to nine arguments rather than up to six, and also this
 * way the file stands alone from possible changes in the
 * implementation of <linux/syscalls.h>.
 */
#define HV_WRAP0(type, name)					\
	type name(void);					\
	type name(void)						\
	{							\
		return _##name();				\
	}
#define __HV_DECL1(t1, a1)	t1 a1
#define __HV_DECL2(t2, a2, ...) t2 a2, __HV_DECL1(__VA_ARGS__)
#define __HV_DECL3(t3, a3, ...) t3 a3, __HV_DECL2(__VA_ARGS__)
#define __HV_DECL4(t4, a4, ...) t4 a4, __HV_DECL3(__VA_ARGS__)
#define __HV_DECL5(t5, a5, ...) t5 a5, __HV_DECL4(__VA_ARGS__)
#define __HV_DECL6(t6, a6, ...) t6 a6, __HV_DECL5(__VA_ARGS__)
#define __HV_DECL7(t7, a7, ...) t7 a7, __HV_DECL6(__VA_ARGS__)
#define __HV_DECL8(t8, a8, ...) t8 a8, __HV_DECL7(__VA_ARGS__)
#define __HV_DECL9(t9, a9, ...) t9 a9, __HV_DECL8(__VA_ARGS__)
#define __HV_PASS1(t1, a1)	a1
#define __HV_PASS2(t2, a2, ...) a2, __HV_PASS1(__VA_ARGS__)
#define __HV_PASS3(t3, a3, ...) a3, __HV_PASS2(__VA_ARGS__)
#define __HV_PASS4(t4, a4, ...) a4, __HV_PASS3(__VA_ARGS__)
#define __HV_PASS5(t5, a5, ...) a5, __HV_PASS4(__VA_ARGS__)
#define __HV_PASS6(t6, a6, ...) a6, __HV_PASS5(__VA_ARGS__)
#define __HV_PASS7(t7, a7, ...) a7, __HV_PASS6(__VA_ARGS__)
#define __HV_PASS8(t8, a8, ...) a8, __HV_PASS7(__VA_ARGS__)
#define __HV_PASS9(t9, a9, ...) a9, __HV_PASS8(__VA_ARGS__)
#define HV_WRAPx(x, type, name, ...)				\
	type name(__HV_DECL##x(__VA_ARGS__));			\
	type name(__HV_DECL##x(__VA_ARGS__))			\
	{							\
		return _##name(__HV_PASS##x(__VA_ARGS__));	\
	}
#define HV_WRAP1(type, name, ...) HV_WRAPx(1, type, name, __VA_ARGS__)
#define HV_WRAP2(type, name, ...) HV_WRAPx(2, type, name, __VA_ARGS__)
#define HV_WRAP3(type, name, ...) HV_WRAPx(3, type, name, __VA_ARGS__)
#define HV_WRAP4(type, name, ...) HV_WRAPx(4, type, name, __VA_ARGS__)
#define HV_WRAP5(type, name, ...) HV_WRAPx(5, type, name, __VA_ARGS__)
#define HV_WRAP6(type, name, ...) HV_WRAPx(6, type, name, __VA_ARGS__)
#define HV_WRAP7(type, name, ...) HV_WRAPx(7, type, name, __VA_ARGS__)
#define HV_WRAP8(type, name, ...) HV_WRAPx(8, type, name, __VA_ARGS__)
#define HV_WRAP9(type, name, ...) HV_WRAPx(9, type, name, __VA_ARGS__)

/* List all the hypervisor API functions. */
HV_WRAP4(void, hv_init, HV_VersionNumber, interface_version_number,
	 int, chip_num, int, chip_rev_num, int, client_pl)
HV_WRAP1(long, hv_sysconf, HV_SysconfQuery, query)
HV_WRAP3(int, hv_confstr, HV_ConfstrQuery, query, HV_VirtAddr, buf, int, len)
#if CHIP_HAS_IPI()
HV_WRAP3(int, hv_get_ipi_pte, HV_Coord, tile, int, pl, HV_PTE*, pte)
HV_WRAP3(int, hv_console_set_ipi, int, ipi, int, event, HV_Coord, coord);
#else
HV_WRAP1(void, hv_enable_intr, HV_IntrMask, enab_mask)
HV_WRAP1(void, hv_disable_intr, HV_IntrMask, disab_mask)
HV_WRAP1(void, hv_clear_intr, HV_IntrMask, clear_mask)
HV_WRAP1(void, hv_raise_intr, HV_IntrMask, raise_mask)
HV_WRAP2(HV_Errno, hv_trigger_ipi, HV_Coord, tile, int, interrupt)
#endif /* !CHIP_HAS_IPI() */
HV_WRAP3(int, hv_store_mapping, HV_VirtAddr, va, unsigned int, len,
	 HV_PhysAddr, pa)
HV_WRAP2(HV_PhysAddr, hv_inquire_realpa, HV_PhysAddr, cpa, unsigned int, len)
HV_WRAP0(HV_RTCTime, hv_get_rtc)
HV_WRAP1(void, hv_set_rtc, HV_RTCTime, time)
HV_WRAP4(int, hv_install_context, HV_PhysAddr, page_table, HV_PTE, access,
	 HV_ASID, asid, __hv32, flags)
HV_WRAP2(int, hv_set_pte_super_shift, int, level, int, log2_count)
HV_WRAP0(HV_Context, hv_inquire_context)
HV_WRAP1(int, hv_flush_asid, HV_ASID, asid)
HV_WRAP2(int, hv_flush_page, HV_VirtAddr, address, HV_PageSize, page_size)
HV_WRAP3(int, hv_flush_pages, HV_VirtAddr, start, HV_PageSize, page_size,
	 unsigned long, size)
HV_WRAP1(int, hv_flush_all, int, preserve_global)
HV_WRAP2(void, hv_restart, HV_VirtAddr, cmd, HV_VirtAddr, args)
HV_WRAP0(void, hv_halt)
HV_WRAP0(void, hv_power_off)
HV_WRAP1(int, hv_reexec, HV_PhysAddr, entry)
HV_WRAP0(HV_Topology, hv_inquire_topology)
HV_WRAP3(HV_Errno, hv_inquire_tiles, HV_InqTileSet, set, HV_VirtAddr, cpumask,
	 int, length)
HV_WRAP1(HV_PhysAddrRange, hv_inquire_physical, int, idx)
HV_WRAP2(HV_MemoryControllerInfo, hv_inquire_memory_controller, HV_Coord, coord,
	 int, controller)
HV_WRAP1(HV_VirtAddrRange, hv_inquire_virtual, int, idx)
HV_WRAP1(HV_ASIDRange, hv_inquire_asid, int, idx)
HV_WRAP1(void, hv_nanosleep, int, nanosecs)
HV_WRAP0(int, hv_console_read_if_ready)
HV_WRAP1(void, hv_console_putc, int, byte)
HV_WRAP2(int, hv_console_write, HV_VirtAddr, bytes, int, len)
HV_WRAP0(void, hv_downcall_dispatch)
HV_WRAP1(int, hv_fs_findfile, HV_VirtAddr, filename)
HV_WRAP1(HV_FS_StatInfo, hv_fs_fstat, int, inode)
HV_WRAP4(int, hv_fs_pread, int, inode, HV_VirtAddr, buf,
	 int, length, int, offset)
HV_WRAP2(unsigned long long, hv_physaddr_read64, HV_PhysAddr, addr,
	 HV_PTE, access)
HV_WRAP3(void, hv_physaddr_write64, HV_PhysAddr, addr, HV_PTE, access,
	 unsigned long long, val)
HV_WRAP2(int, hv_get_command_line, HV_VirtAddr, buf, int, length)
HV_WRAP2(HV_Errno, hv_set_command_line, HV_VirtAddr, buf, int, length)
HV_WRAP1(void, hv_set_caching, unsigned long, bitmask)
HV_WRAP2(void, hv_bzero_page, HV_VirtAddr, va, unsigned int, size)
HV_WRAP1(HV_Errno, hv_register_message_state, HV_MsgState*, msgstate)
HV_WRAP4(int, hv_send_message, HV_Recipient *, recips, int, nrecip,
	 HV_VirtAddr, buf, int, buflen)
HV_WRAP3(HV_RcvMsgInfo, hv_receive_message, HV_MsgState, msgstate,
	 HV_VirtAddr, buf, int, buflen)
HV_WRAP0(void, hv_start_all_tiles)
HV_WRAP2(int, hv_dev_open, HV_VirtAddr, name, __hv32, flags)
HV_WRAP1(int, hv_dev_close, int, devhdl)
HV_WRAP5(int, hv_dev_pread, int, devhdl, __hv32, flags, HV_VirtAddr, va,
	 __hv32, len, __hv64, offset)
HV_WRAP5(int, hv_dev_pwrite, int, devhdl, __hv32, flags, HV_VirtAddr, va,
	 __hv32, len, __hv64, offset)
HV_WRAP3(int, hv_dev_poll, int, devhdl, __hv32, events, HV_IntArg, intarg)
HV_WRAP1(int, hv_dev_poll_cancel, int, devhdl)
HV_WRAP6(int, hv_dev_preada, int, devhdl, __hv32, flags, __hv32, sgl_len,
	 HV_SGL *, sglp, __hv64, offset, HV_IntArg, intarg)
HV_WRAP6(int, hv_dev_pwritea, int, devhdl, __hv32, flags, __hv32, sgl_len,
	 HV_SGL *, sglp, __hv64, offset, HV_IntArg, intarg)
HV_WRAP9(int, hv_flush_remote, HV_PhysAddr, cache_pa,
	 unsigned long, cache_control, unsigned long*, cache_cpumask,
	 HV_VirtAddr, tlb_va, unsigned long, tlb_length,
	 unsigned long, tlb_pgsize, unsigned long*, tlb_cpumask,
	 HV_Remote_ASID*, asids, int, asidcount)
