/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_HWCAP2_H
#define _ASM_X86_HWCAP2_H

#include <linux/const.h>

/* MONITOR/MWAIT enabled in Ring 3 */
#define HWCAP2_RING3MWAIT		_BITUL(0)

/* Kernel allows FSGSBASE instructions available in Ring 3 */
#define HWCAP2_FSGSBASE			_BITUL(1)

#endif
