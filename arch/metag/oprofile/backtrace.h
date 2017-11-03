/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _METAG_OPROFILE_BACKTRACE_H
#define _METAG_OPROFILE_BACKTRACE_H

void metag_backtrace(struct pt_regs * const regs, unsigned int depth);

#endif
