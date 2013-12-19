/* Hypervisor call vector addresses; see <hv/hypervisor.h> */
.macro gensym sym, val, size
.org \val
.global _\sym
.type _\sym,function
_\sym:
.size _\sym,\size
#ifndef CONFIG_TILE_HVGLUE_TRACE
.globl \sym
.set \sym,_\sym
#endif
.endm

.section .hvglue,"x",@nobits
.align 8
gensym hv_init, 0x20, 32
gensym hv_install_context, 0x40, 32
gensym hv_sysconf, 0x60, 32
gensym hv_get_rtc, 0x80, 32
gensym hv_set_rtc, 0xa0, 32
gensym hv_flush_asid, 0xc0, 32
gensym hv_flush_page, 0xe0, 32
gensym hv_flush_pages, 0x100, 32
gensym hv_restart, 0x120, 32
gensym hv_halt, 0x140, 32
gensym hv_power_off, 0x160, 32
gensym hv_inquire_physical, 0x180, 32
gensym hv_inquire_memory_controller, 0x1a0, 32
gensym hv_inquire_virtual, 0x1c0, 32
gensym hv_inquire_asid, 0x1e0, 32
gensym hv_nanosleep, 0x200, 32
gensym hv_console_read_if_ready, 0x220, 32
gensym hv_console_write, 0x240, 32
gensym hv_downcall_dispatch, 0x260, 32
gensym hv_inquire_topology, 0x280, 32
gensym hv_fs_findfile, 0x2a0, 32
gensym hv_fs_fstat, 0x2c0, 32
gensym hv_fs_pread, 0x2e0, 32
gensym hv_physaddr_read64, 0x300, 32
gensym hv_physaddr_write64, 0x320, 32
gensym hv_get_command_line, 0x340, 32
gensym hv_set_caching, 0x360, 32
gensym hv_bzero_page, 0x380, 32
gensym hv_register_message_state, 0x3a0, 32
gensym hv_send_message, 0x3c0, 32
gensym hv_receive_message, 0x3e0, 32
gensym hv_inquire_context, 0x400, 32
gensym hv_start_all_tiles, 0x420, 32
gensym hv_dev_open, 0x440, 32
gensym hv_dev_close, 0x460, 32
gensym hv_dev_pread, 0x480, 32
gensym hv_dev_pwrite, 0x4a0, 32
gensym hv_dev_poll, 0x4c0, 32
gensym hv_dev_poll_cancel, 0x4e0, 32
gensym hv_dev_preada, 0x500, 32
gensym hv_dev_pwritea, 0x520, 32
gensym hv_flush_remote, 0x540, 32
gensym hv_console_putc, 0x560, 32
gensym hv_inquire_tiles, 0x580, 32
gensym hv_confstr, 0x5a0, 32
gensym hv_reexec, 0x5c0, 32
gensym hv_set_command_line, 0x5e0, 32
gensym hv_clear_intr, 0x600, 32
gensym hv_enable_intr, 0x620, 32
gensym hv_disable_intr, 0x640, 32
gensym hv_raise_intr, 0x660, 32
gensym hv_trigger_ipi, 0x680, 32
gensym hv_store_mapping, 0x6a0, 32
gensym hv_inquire_realpa, 0x6c0, 32
gensym hv_flush_all, 0x6e0, 32
gensym hv_get_ipi_pte, 0x700, 32
gensym hv_set_pte_super_shift, 0x720, 32
gensym hv_console_set_ipi, 0x7e0, 32
gensym hv_glue_internals, 0x800, 30720
