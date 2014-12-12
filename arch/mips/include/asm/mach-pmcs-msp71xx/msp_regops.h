/*
 * SMP/VPE-safe functions to access "registers" (see note).
 *
 * NOTES:
* - These macros use ll/sc instructions, so it is your responsibility to
 * ensure these are available on your platform before including this file.
 * - The MIPS32 spec states that ll/sc results are undefined for uncached
 * accesses. This means they can't be used on HW registers accessed
 * through kseg1. Code which requires these macros for this purpose must
 * front-end the registers with cached memory "registers" and have a single
 * thread update the actual HW registers.
 * - A maximum of 2k of code can be inserted between ll and sc. Every
 * memory accesses between the instructions will increase the chance of
 * sc failing and having to loop.
 * - When using custom_read_reg32/custom_write_reg32 only perform the
 * necessary logical operations on the register value in between these
 * two calls. All other logic should be performed before the first call.
  * - There is a bug on the R10000 chips which has a workaround. If you
 * are affected by this bug, make sure to define the symbol 'R10000_LLSC_WAR'
 * to be non-zero.  If you are using this header from within linux, you may
 * include <asm/war.h> before including this file to have this defined
 * appropriately for you.
 *
 * Copyright 2005-2007 PMC-Sierra, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 *  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF USE,
 *  DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc., 675
 *  Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_REGOPS_H__
#define __ASM_REGOPS_H__

#include <linux/types.h>

#include <asm/compiler.h>
#include <asm/war.h>

#ifndef R10000_LLSC_WAR
#define R10000_LLSC_WAR 0
#endif

#if R10000_LLSC_WAR == 1
#define __beqz	"beqzl	"
#else
#define __beqz	"beqz	"
#endif

#ifndef _LINUX_TYPES_H
typedef unsigned int u32;
#endif

/*
 * Sets all the masked bits to the corresponding value bits
 */
static inline void set_value_reg32(volatile u32 *const addr,
					u32 const mask,
					u32 const value)
{
	u32 temp;

	__asm__ __volatile__(
	"	.set	push				\n"
	"	.set	arch=r4000			\n"
	"1:	ll	%0, %1	# set_value_reg32	\n"
	"	and	%0, %2				\n"
	"	or	%0, %3				\n"
	"	sc	%0, %1				\n"
	"	"__beqz"%0, 1b				\n"
	"	nop					\n"
	"	.set	pop				\n"
	: "=&r" (temp), "=" GCC_OFF12_ASM() (*addr)
	: "ir" (~mask), "ir" (value), GCC_OFF12_ASM() (*addr));
}

/*
 * Sets all the masked bits to '1'
 */
static inline void set_reg32(volatile u32 *const addr,
				u32 const mask)
{
	u32 temp;

	__asm__ __volatile__(
	"	.set	push				\n"
	"	.set	arch=r4000			\n"
	"1:	ll	%0, %1		# set_reg32	\n"
	"	or	%0, %2				\n"
	"	sc	%0, %1				\n"
	"	"__beqz"%0, 1b				\n"
	"	nop					\n"
	"	.set	pop				\n"
	: "=&r" (temp), "=" GCC_OFF12_ASM() (*addr)
	: "ir" (mask), GCC_OFF12_ASM() (*addr));
}

/*
 * Sets all the masked bits to '0'
 */
static inline void clear_reg32(volatile u32 *const addr,
				u32 const mask)
{
	u32 temp;

	__asm__ __volatile__(
	"	.set	push				\n"
	"	.set	arch=r4000			\n"
	"1:	ll	%0, %1		# clear_reg32	\n"
	"	and	%0, %2				\n"
	"	sc	%0, %1				\n"
	"	"__beqz"%0, 1b				\n"
	"	nop					\n"
	"	.set	pop				\n"
	: "=&r" (temp), "=" GCC_OFF12_ASM() (*addr)
	: "ir" (~mask), GCC_OFF12_ASM() (*addr));
}

/*
 * Toggles all masked bits from '0' to '1' and '1' to '0'
 */
static inline void toggle_reg32(volatile u32 *const addr,
				u32 const mask)
{
	u32 temp;

	__asm__ __volatile__(
	"	.set	push				\n"
	"	.set	arch=r4000			\n"
	"1:	ll	%0, %1		# toggle_reg32	\n"
	"	xor	%0, %2				\n"
	"	sc	%0, %1				\n"
	"	"__beqz"%0, 1b				\n"
	"	nop					\n"
	"	.set	pop				\n"
	: "=&r" (temp), "=" GCC_OFF12_ASM() (*addr)
	: "ir" (mask), GCC_OFF12_ASM() (*addr));
}

/*
 * Read all masked bits others are returned as '0'
 */
static inline u32 read_reg32(volatile u32 *const addr,
				u32 const mask)
{
	u32 temp;

	__asm__ __volatile__(
	"	.set	push				\n"
	"	.set	noreorder			\n"
	"	lw	%0, %1		# read		\n"
	"	and	%0, %2		# mask		\n"
	"	.set	pop				\n"
	: "=&r" (temp)
	: "m" (*addr), "ir" (mask));

	return temp;
}

/*
 * blocking_read_reg32 - Read address with blocking load
 *
 * Uncached writes need to be read back to ensure they reach RAM.
 * The returned value must be 'used' to prevent from becoming a
 * non-blocking load.
 */
static inline u32 blocking_read_reg32(volatile u32 *const addr)
{
	u32 temp;

	__asm__ __volatile__(
	"	.set	push				\n"
	"	.set	noreorder			\n"
	"	lw	%0, %1		# read		\n"
	"	move	%0, %0		# block		\n"
	"	.set	pop				\n"
	: "=&r" (temp)
	: "m" (*addr));

	return temp;
}

/*
 * For special strange cases only:
 *
 * If you need custom processing within a ll/sc loop, use the following macros
 * VERY CAREFULLY:
 *
 *   u32 tmp;				<-- Define a variable to hold the data
 *
 *   custom_read_reg32(address, tmp);	<-- Reads the address and put the value
 *						in the 'tmp' variable given
 *
 *	From here on out, you are (basically) atomic, so don't do anything too
 *	fancy!
 *	Also, this code may loop if the end of this block fails to write
 *	everything back safely due do the other CPU, so do NOT do anything
 *	with side-effects!
 *
 *   custom_write_reg32(address, tmp);	<-- Writes back 'tmp' safely.
 */
#define custom_read_reg32(address, tmp)				\
	__asm__ __volatile__(					\
	"	.set	push				\n"	\
	"	.set	arch=r4000			\n"	\
	"1:	ll	%0, %1	#custom_read_reg32	\n"	\
	"	.set	pop				\n"	\
	: "=r" (tmp), "=" GCC_OFF12_ASM() (*address)		\
	: GCC_OFF12_ASM() (*address))

#define custom_write_reg32(address, tmp)			\
	__asm__ __volatile__(					\
	"	.set	push				\n"	\
	"	.set	arch=r4000			\n"	\
	"	sc	%0, %1	#custom_write_reg32	\n"	\
	"	"__beqz"%0, 1b				\n"	\
	"	nop					\n"	\
	"	.set	pop				\n"	\
	: "=&r" (tmp), "=" GCC_OFF12_ASM() (*address)		\
	: "0" (tmp), GCC_OFF12_ASM() (*address))

#endif	/* __ASM_REGOPS_H__ */
