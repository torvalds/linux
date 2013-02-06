/*
 * Linux 2.6.32 and later Kernel module for VMware MVP PVTCP Server
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief PVTCP socket destructor shim.
 *
 * The module reference accounting code for socket destruction in the core
 * Linux kernel does not know about PVTCP sockets, so it does not properly
 * increment/decrement the reference count on pvtcpkm when calling through a
 * function pointer into our destructor. If a module unload is requested on
 * pvtcpkm while a socket is being destroyed, it is possible for the destructor
 * to be preempted after decrementing the module reference count but before
 * returning to the core kernel. If the module code is unmapped before the
 * function return, it is possible that we will attempt to execute unmapped
 * code, resulting in a host crash.
 *
 * This shim proxies socket destruction requests through to the PVTCP socket
 * destructor, then jumps directly to module_put to drop the reference count.
 * module_put will return directly to the caller, eliminating the race.
 */

.text
.p2align 4

.global asmDestructorShim

/**
 *  @brief Socket destructor callback. Calls into pvtcpkm to destroy a socket
 *  and then decrements the refcount.
 *  @param r0 pointer to struct sock
 */

asmDestructorShim:
   push   {lr}
   ldr     r1, targetAddr             @ Destroy socket
   blx     r1
   pop    {lr}
   cmp     r0, #0
   bxne    lr                         @ We shouldn't module_put, just return.
   ldr     r0, owner
   ldr     r1, modulePutAddr          @ Jump to module_put. module_put
   bx      r1                         @ returns directly to caller

owner:
   .word __this_module

targetAddr:
   .word DestructCB

modulePutAddr:
   .word module_put
