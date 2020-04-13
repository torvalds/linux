/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  n_tracesink.h - Kernel driver API to route trace data in kernel space.
 *
 *  Copyright (C) Intel 2011
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The PTI (Parallel Trace Interface) driver directs trace data routed from
 * various parts in the system out through the Intel Penwell PTI port and
 * out of the mobile device for analysis with a debugging tool
 * (Lauterbach, Fido). This is part of a solution for the MIPI P1149.7,
 * compact JTAG, standard.
 *
 * This header file is used by n_tracerouter to be able to send the
 * data of it's tty port to the tty port this module sits.  This
 * mechanism can also be used independent of the PTI module.
 *
 */

#ifndef N_TRACESINK_H_
#define N_TRACESINK_H_

void n_tracesink_datadrain(u8 *buf, int count);

#endif
