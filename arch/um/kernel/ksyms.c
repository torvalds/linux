/* 
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/module.h"
#include "linux/syscalls.h"
#include "asm/a.out.h"
#include "asm/tlbflush.h"
#include "asm/uaccess.h"
#include "as-layout.h"
#include "kern_util.h"
#include "mem_user.h"
#include "os.h"

EXPORT_SYMBOL(uml_physmem);
EXPORT_SYMBOL(set_signals);
EXPORT_SYMBOL(get_signals);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(sys_waitpid);
EXPORT_SYMBOL(flush_tlb_range);
EXPORT_SYMBOL(arch_validate);

EXPORT_SYMBOL(high_physmem);
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(handle_page_fault);
EXPORT_SYMBOL(find_iomem);

EXPORT_SYMBOL(strnlen_user);
EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(copy_to_user);
EXPORT_SYMBOL(copy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(uml_strdup);

EXPORT_SYMBOL(os_stat_fd);
EXPORT_SYMBOL(os_stat_file);
EXPORT_SYMBOL(os_access);
EXPORT_SYMBOL(os_set_exec_close);
EXPORT_SYMBOL(os_getpid);
EXPORT_SYMBOL(os_open_file);
EXPORT_SYMBOL(os_read_file);
EXPORT_SYMBOL(os_write_file);
EXPORT_SYMBOL(os_seek_file);
EXPORT_SYMBOL(os_lock_file);
EXPORT_SYMBOL(os_ioctl_generic);
EXPORT_SYMBOL(os_pipe);
EXPORT_SYMBOL(os_file_type);
EXPORT_SYMBOL(os_file_mode);
EXPORT_SYMBOL(os_file_size);
EXPORT_SYMBOL(os_flush_stdout);
EXPORT_SYMBOL(os_close_file);
EXPORT_SYMBOL(os_set_fd_async);
EXPORT_SYMBOL(os_set_fd_block);
EXPORT_SYMBOL(helper_wait);
EXPORT_SYMBOL(os_shutdown_socket);
EXPORT_SYMBOL(os_create_unix_socket);
EXPORT_SYMBOL(os_connect_socket);
EXPORT_SYMBOL(os_accept_connection);
EXPORT_SYMBOL(os_rcv_fd);
EXPORT_SYMBOL(run_helper);
EXPORT_SYMBOL(start_thread);

#ifdef CONFIG_SMP

/* required for SMP */

extern void __write_lock_failed(rwlock_t *rw);
EXPORT_SYMBOL(__write_lock_failed);

extern void __read_lock_failed(rwlock_t *rw);
EXPORT_SYMBOL(__read_lock_failed);

#endif
