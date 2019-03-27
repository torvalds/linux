/*  Macro definitions to used to support 32/64-bit code in Darwin's
 *  assembly files.
 *
 *   Copyright (C) 2004 Free Software Foundation, Inc.
 * 
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * In addition to the permissions in the GNU General Public License, the
 * Free Software Foundation gives you unlimited permission to link the
 * compiled version of this file with other programs, and to distribute
 * those programs without any restriction coming from the use of this
 * file.  (The General Public License restrictions do apply in other
 * respects; for example, they cover modification of the file, and
 * distribution when not linked into another program.)
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 * 
 *  As a special exception, if you link this library with files
 *  compiled with GCC to produce an executable, this does not cause the
 *  resulting executable to be covered by the GNU General Public License.
 *  This exception does not however invalidate any other reasons why the
 *  executable file might be covered by the GNU General Public License.
 */ 

/* These are donated from /usr/include/architecture/ppc . */

#if defined(__ppc64__)
#define MODE_CHOICE(x, y) y
#else
#define MODE_CHOICE(x, y) x
#endif

#define cmpg    MODE_CHOICE(cmpw, cmpd)
#define lg      MODE_CHOICE(lwz, ld)
#define stg     MODE_CHOICE(stw, std)
#define lgx     MODE_CHOICE(lwzx, ldx)
#define stgx    MODE_CHOICE(stwx, stdx)
#define lgu     MODE_CHOICE(lwzu, ldu)
#define stgu    MODE_CHOICE(stwu, stdu)
#define lgux    MODE_CHOICE(lwzux, ldux)
#define stgux   MODE_CHOICE(stwux, stdux)
#define lgwa    MODE_CHOICE(lwz, lwa)

#define g_long  MODE_CHOICE(long, quad)         /* usage is ".g_long" */

#define GPR_BYTES       MODE_CHOICE(4,8)        /* size of a GPR in bytes */
#define LOG2_GPR_BYTES  MODE_CHOICE(2,3)        /* log2(GPR_BYTES) */

#define SAVED_LR_OFFSET MODE_CHOICE(8,16)	/* position of saved
						   LR in frame */
