/* 
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/module.h>
#include "os.h"

EXPORT_SYMBOL(set_signals);
EXPORT_SYMBOL(get_signals);

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
EXPORT_SYMBOL(os_major);
EXPORT_SYMBOL(os_minor);
EXPORT_SYMBOL(os_makedev);

EXPORT_SYMBOL(add_sigio_fd);
EXPORT_SYMBOL(ignore_sigio_fd);
EXPORT_SYMBOL(sigio_broken);
