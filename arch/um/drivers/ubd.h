/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Copyright (C) 2001 RidgeRun, Inc (glonnon@ridgerun.com)
 */

#ifndef __UM_UBD_USER_H
#define __UM_UBD_USER_H

#include <os.h>

int start_io_thread(struct os_helper_thread **td_out, int *fd_out);
void *io_thread(void *arg);
extern int kernel_fd;

extern int ubd_read_poll(int timeout);
extern int ubd_write_poll(int timeout);

#define UBD_REQ_BUFFER_SIZE 64

#endif

