/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_SIGCONTEXT_H
#define __ASM_SIGCONTEXT_H

#include <uapi/asm/sigcontext.h>

/*
 * Auxiliary context saved in the sigcontext.__reserved array. Not exported to
 * user space as it will change with the addition of new context. User space
 * should check the magic/size information.
 */
struct aux_context {
	struct fpsimd_context fpsimd;
	/* additional context to be added before "end" */
	struct _aarch64_ctx end;
};
#endif
