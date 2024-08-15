/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __IWMMXT_H__
#define __IWMMXT_H__

.irp b, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
.set .LwR\b, \b
.set .Lr\b, \b
.endr

.set .LwCSSF, 0x2
.set .LwCASF, 0x3
.set .LwCGR0, 0x8
.set .LwCGR1, 0x9
.set .LwCGR2, 0xa
.set .LwCGR3, 0xb

.macro wldrd, reg:req, base:req, offset:req
.inst 0xedd00100 | (.L\reg << 12) | (.L\base << 16) | (\offset >> 2)
.endm

.macro wldrw, reg:req, base:req, offset:req
.inst 0xfd900100 | (.L\reg << 12) | (.L\base << 16) | (\offset >> 2)
.endm

.macro wstrd, reg:req, base:req, offset:req
.inst 0xedc00100 | (.L\reg << 12) | (.L\base << 16) | (\offset >> 2)
.endm

.macro wstrw, reg:req, base:req, offset:req
.inst 0xfd800100 | (.L\reg << 12) | (.L\base << 16) | (\offset >> 2)
.endm

#ifdef __clang__

#define wCon c1

.macro tmrc, dest:req, control:req
mrc p1, 0, \dest, \control, c0, 0
.endm

.macro tmcr, control:req, src:req
mcr p1, 0, \src, \control, c0, 0
.endm
#endif

#endif
