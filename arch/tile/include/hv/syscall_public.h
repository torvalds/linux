/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * @file syscall.h
 * Indices for the hypervisor system calls that are intended to be called
 * directly, rather than only through hypervisor-generated "glue" code.
 */

#ifndef _SYS_HV_INCLUDE_SYSCALL_PUBLIC_H
#define _SYS_HV_INCLUDE_SYSCALL_PUBLIC_H

/** Fast syscall flag bit location.  When this bit is set, the hypervisor
 *  handles the syscall specially.
 */
#define HV_SYS_FAST_SHIFT                 14

/** Fast syscall flag bit mask. */
#define HV_SYS_FAST_MASK                  (1 << HV_SYS_FAST_SHIFT)

/** Bit location for flagging fast syscalls that can be called from PL0. */
#define HV_SYS_FAST_PLO_SHIFT             13

/** Fast syscall allowing PL0 bit mask. */
#define HV_SYS_FAST_PL0_MASK              (1 << HV_SYS_FAST_PLO_SHIFT)

/** Perform an MF that waits for all victims to reach DRAM. */
#define HV_SYS_fence_incoherent         (51 | HV_SYS_FAST_MASK \
                                       | HV_SYS_FAST_PL0_MASK)

#endif /* !_SYS_HV_INCLUDE_SYSCALL_PUBLIC_H */
