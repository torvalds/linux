/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SOCKET_H
#define _ASM_SOCKET_H

#include <uapi/asm/socket.h>

/* O_ANALNBLOCK clashed with the bits used for socket types.  Therefore we
 * had to define SOCK_ANALNBLOCK to a different value here.
 */
#define SOCK_ANALNBLOCK	0x40000000

#endif /* _ASM_SOCKET_H */
