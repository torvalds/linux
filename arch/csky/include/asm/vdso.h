/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_VDSO_H
#define __ASM_CSKY_VDSO_H

#include <abi/vdso.h>

struct csky_vdso {
	unsigned short rt_signal_retcode[4];
};

#endif /* __ASM_CSKY_VDSO_H */
