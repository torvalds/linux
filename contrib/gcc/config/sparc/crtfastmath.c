/*
 * Copyright (C) 2001 Free Software Foundation, Inc.
 * Contributed by David S. Miller (davem@redhat.com)
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
 *    As a special exception, if you link this library with files
 *    compiled with GCC to produce an executable, this does not cause
 *    the resulting executable to be covered by the GNU General Public License.
 *    This exception does not however invalidate any other reasons why
 *    the executable file might be covered by the GNU General Public License.
 */

#define FPRS_NS		(1 << 22)	/* Non-Standard fpu results */

static void __attribute__((constructor))
set_fast_math (void)
{
  unsigned int fsr;

  /* This works for the 64-bit case because, even if 32-bit ld/st of
     the fsr register modified the upper 32-bit, the only thing up there
     are the 3 other condition codes which are "do not care" at the time
     that this runs.  */

  __asm__("st %%fsr, %0"
	  : "=m" (fsr));

  fsr |= FPRS_NS;

  __asm__("ld %0, %%fsr"
	  : : "m" (fsr));
}
