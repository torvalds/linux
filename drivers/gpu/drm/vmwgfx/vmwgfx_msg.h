/*
 * Copyright (C) 2016, VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * Based on code from vmware.c and vmmouse.c.
 * Author:
 *   Sinclair Yeh <syeh@vmware.com>
 */
#ifndef _VMWGFX_MSG_H
#define _VMWGFX_MSG_H


/**
 * Hypervisor-specific bi-directional communication channel.  Should never
 * execute on bare metal hardware.  The caller must make sure to check for
 * supported hypervisor before using these macros.
 *
 * The last two parameters are both input and output and must be initialized.
 *
 * @cmd: [IN] Message Cmd
 * @in_ebx: [IN] Message Len, through EBX
 * @in_si: [IN] Input argument through SI, set to 0 if not used
 * @in_di: [IN] Input argument through DI, set ot 0 if not used
 * @port_num: [IN] port number + [channel id]
 * @magic: [IN] hypervisor magic value
 * @eax: [OUT] value of EAX register
 * @ebx: [OUT] e.g. status from an HB message status command
 * @ecx: [OUT] e.g. status from a non-HB message status command
 * @edx: [OUT] e.g. channel id
 * @si:  [OUT]
 * @di:  [OUT]
 */
#define VMW_PORT(cmd, in_ebx, in_si, in_di,	\
		 port_num, magic,		\
		 eax, ebx, ecx, edx, si, di)	\
({						\
	asm volatile ("inl %%dx, %%eax;" :	\
		"=a"(eax),			\
		"=b"(ebx),			\
		"=c"(ecx),			\
		"=d"(edx),			\
		"=S"(si),			\
		"=D"(di) :			\
		"a"(magic),			\
		"b"(in_ebx),			\
		"c"(cmd),			\
		"d"(port_num),			\
		"S"(in_si),			\
		"D"(in_di) :			\
		"memory");			\
})


/**
 * Hypervisor-specific bi-directional communication channel.  Should never
 * execute on bare metal hardware.  The caller must make sure to check for
 * supported hypervisor before using these macros.
 *
 * The last 3 parameters are both input and output and must be initialized.
 *
 * @cmd: [IN] Message Cmd
 * @in_ecx: [IN] Message Len, through ECX
 * @in_si: [IN] Input argument through SI, set to 0 if not used
 * @in_di: [IN] Input argument through DI, set to 0 if not used
 * @port_num: [IN] port number + [channel id]
 * @magic: [IN] hypervisor magic value
 * @bp:  [IN]
 * @eax: [OUT] value of EAX register
 * @ebx: [OUT] e.g. status from an HB message status command
 * @ecx: [OUT] e.g. status from a non-HB message status command
 * @edx: [OUT] e.g. channel id
 * @si:  [OUT]
 * @di:  [OUT]
 */
#ifdef __x86_64__

#define VMW_PORT_HB_OUT(cmd, in_ecx, in_si, in_di,	\
			port_num, magic, bp,		\
			eax, ebx, ecx, edx, si, di)	\
({							\
	asm volatile ("push %%rbp;"			\
		"mov %12, %%rbp;"			\
		"rep outsb;"				\
		"pop %%rbp;" :				\
		"=a"(eax),				\
		"=b"(ebx),				\
		"=c"(ecx),				\
		"=d"(edx),				\
		"=S"(si),				\
		"=D"(di) :				\
		"a"(magic),				\
		"b"(cmd),				\
		"c"(in_ecx),				\
		"d"(port_num),				\
		"S"(in_si),				\
		"D"(in_di),				\
		"r"(bp) :				\
		"memory", "cc");			\
})


#define VMW_PORT_HB_IN(cmd, in_ecx, in_si, in_di,	\
		       port_num, magic, bp,		\
		       eax, ebx, ecx, edx, si, di)	\
({							\
	asm volatile ("push %%rbp;"			\
		"mov %12, %%rbp;"			\
		"rep insb;"				\
		"pop %%rbp" :				\
		"=a"(eax),				\
		"=b"(ebx),				\
		"=c"(ecx),				\
		"=d"(edx),				\
		"=S"(si),				\
		"=D"(di) :				\
		"a"(magic),				\
		"b"(cmd),				\
		"c"(in_ecx),				\
		"d"(port_num),				\
		"S"(in_si),				\
		"D"(in_di),				\
		"r"(bp) :				\
		"memory", "cc");			\
})

#else

/* In the 32-bit version of this macro, we use "m" because there is no
 * more register left for bp
 */
#define VMW_PORT_HB_OUT(cmd, in_ecx, in_si, in_di,	\
			port_num, magic, bp,		\
			eax, ebx, ecx, edx, si, di)	\
({							\
	asm volatile ("push %%ebp;"			\
		"mov %12, %%ebp;"			\
		"rep outsb;"				\
		"pop %%ebp;" :				\
		"=a"(eax),				\
		"=b"(ebx),				\
		"=c"(ecx),				\
		"=d"(edx),				\
		"=S"(si),				\
		"=D"(di) :				\
		"a"(magic),				\
		"b"(cmd),				\
		"c"(in_ecx),				\
		"d"(port_num),				\
		"S"(in_si),				\
		"D"(in_di),				\
		"m"(bp) :				\
		"memory", "cc");			\
})


#define VMW_PORT_HB_IN(cmd, in_ecx, in_si, in_di,	\
		       port_num, magic, bp,		\
		       eax, ebx, ecx, edx, si, di)	\
({							\
	asm volatile ("push %%ebp;"			\
		"mov %12, %%ebp;"			\
		"rep insb;"				\
		"pop %%ebp" :				\
		"=a"(eax),				\
		"=b"(ebx),				\
		"=c"(ecx),				\
		"=d"(edx),				\
		"=S"(si),				\
		"=D"(di) :				\
		"a"(magic),				\
		"b"(cmd),				\
		"c"(in_ecx),				\
		"d"(port_num),				\
		"S"(in_si),				\
		"D"(in_di),				\
		"m"(bp) :				\
		"memory", "cc");			\
})
#endif /* #if __x86_64__ */

#endif
