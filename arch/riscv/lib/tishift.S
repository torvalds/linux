/*
 * Copyright (C) 2018 Free Software Foundation, Inc.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
  .globl __lshrti3
__lshrti3:
  beqz  a2, .L1
  li    a5,64
  sub   a5,a5,a2
  addi  sp,sp,-16
  sext.w a4,a5
  blez  a5, .L2
  sext.w a2,a2
  sll   a4,a1,a4
  srl   a0,a0,a2
  srl   a1,a1,a2
  or    a0,a0,a4
  sd    a1,8(sp)
  sd    a0,0(sp)
  ld    a0,0(sp)
  ld    a1,8(sp)
  addi  sp,sp,16
  ret
.L1:
  ret
.L2:
  negw  a4,a4
  srl   a1,a1,a4
  sd    a1,0(sp)
  sd    zero,8(sp)
  ld    a0,0(sp)
  ld    a1,8(sp)
  addi  sp,sp,16
  ret
