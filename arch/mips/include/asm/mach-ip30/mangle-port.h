/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2003, 2004 Ralf Baechle
 */
#ifndef __ASM_MACH_IP30_MANGLE_PORT_H
#define __ASM_MACH_IP30_MANGLE_PORT_H

#define __swizzle_addr_b(port)	((port)^3)
#define __swizzle_addr_w(port)	((port)^2)
#define __swizzle_addr_l(port)	(port)
#define __swizzle_addr_q(port)	(port)

#define ioswabb(a, x)		(x)
#define __mem_ioswabb(a, x)	(x)
#define ioswabw(a, x)		(x)
#define __mem_ioswabw(a, x)	((__force u16)cpu_to_le16(x))
#define ioswabl(a, x)		(x)
#define __mem_ioswabl(a, x)	((__force u32)cpu_to_le32(x))
#define ioswabq(a, x)		(x)
#define __mem_ioswabq(a, x)	((__force u64)cpu_to_le64(x))

#endif /* __ASM_MACH_IP30_MANGLE_PORT_H */
