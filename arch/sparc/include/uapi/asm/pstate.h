/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _SPARC64_PSTATE_H
#define _SPARC64_PSTATE_H

#include <linux/const.h>

/* The V9 PSTATE Register (with SpitFire extensions).
 *
 * -----------------------------------------------------------------------
 * | Resv | IG | MG | CLE | TLE |  MM  | RED | PEF | AM | PRIV | IE | AG |
 * -----------------------------------------------------------------------
 *  63  12  11   10    9     8    7   6   5     4     3     2     1    0
 */
/* IG on V9 conflicts with MCDE on M7. PSTATE_MCDE will only be used on
 * processors that support ADI which do not use IG, hence there is no
 * functional conflict
 */
#define PSTATE_IG   _AC(0x0000000000000800,UL) /* Interrupt Globals.	*/
#define PSTATE_MCDE _AC(0x0000000000000800,UL) /* MCD Enable		*/
#define PSTATE_MG   _AC(0x0000000000000400,UL) /* MMU Globals.		*/
#define PSTATE_CLE  _AC(0x0000000000000200,UL) /* Current Little Endian.*/
#define PSTATE_TLE  _AC(0x0000000000000100,UL) /* Trap Little Endian.	*/
#define PSTATE_MM   _AC(0x00000000000000c0,UL) /* Memory Model.		*/
#define PSTATE_TSO  _AC(0x0000000000000000,UL) /* MM: TotalStoreOrder	*/
#define PSTATE_PSO  _AC(0x0000000000000040,UL) /* MM: PartialStoreOrder	*/
#define PSTATE_RMO  _AC(0x0000000000000080,UL) /* MM: RelaxedMemoryOrder*/
#define PSTATE_RED  _AC(0x0000000000000020,UL) /* Reset Error Debug.	*/
#define PSTATE_PEF  _AC(0x0000000000000010,UL) /* Floating Point Enable.*/
#define PSTATE_AM   _AC(0x0000000000000008,UL) /* Address Mask.		*/
#define PSTATE_PRIV _AC(0x0000000000000004,UL) /* Privilege.		*/
#define PSTATE_IE   _AC(0x0000000000000002,UL) /* Interrupt Enable.	*/
#define PSTATE_AG   _AC(0x0000000000000001,UL) /* Alternate Globals.	*/

/* The V9 TSTATE Register (with SpitFire and Linux extensions).
 *
 * ---------------------------------------------------------------------
 * |  Resv |  GL  |  CCR  |  ASI  |  %pil  |  PSTATE  |  Resv  |  CWP  |
 * ---------------------------------------------------------------------
 *  63   43 42  40 39   32 31   24 23    20 19       8 7      5 4     0
 */
#define TSTATE_GL	_AC(0x0000070000000000,UL) /* Global reg level  */
#define TSTATE_CCR	_AC(0x000000ff00000000,UL) /* Condition Codes.	*/
#define TSTATE_XCC	_AC(0x000000f000000000,UL) /* Condition Codes.	*/
#define TSTATE_XNEG	_AC(0x0000008000000000,UL) /* %xcc Negative.	*/
#define TSTATE_XZERO	_AC(0x0000004000000000,UL) /* %xcc Zero.	*/
#define TSTATE_XOVFL	_AC(0x0000002000000000,UL) /* %xcc Overflow.	*/
#define TSTATE_XCARRY	_AC(0x0000001000000000,UL) /* %xcc Carry.	*/
#define TSTATE_ICC	_AC(0x0000000f00000000,UL) /* Condition Codes.	*/
#define TSTATE_INEG	_AC(0x0000000800000000,UL) /* %icc Negative.	*/
#define TSTATE_IZERO	_AC(0x0000000400000000,UL) /* %icc Zero.	*/
#define TSTATE_IOVFL	_AC(0x0000000200000000,UL) /* %icc Overflow.	*/
#define TSTATE_ICARRY	_AC(0x0000000100000000,UL) /* %icc Carry.	*/
#define TSTATE_ASI	_AC(0x00000000ff000000,UL) /* AddrSpace ID.	*/
#define TSTATE_PIL	_AC(0x0000000000f00000,UL) /* %pil (Linux traps)*/
#define TSTATE_PSTATE	_AC(0x00000000000fff00,UL) /* PSTATE.		*/
/* IG on V9 conflicts with MCDE on M7. TSTATE_MCDE will only be used on
 * processors that support ADI which do not support IG, hence there is
 * no functional conflict
 */
#define TSTATE_IG	_AC(0x0000000000080000,UL) /* Interrupt Globals.*/
#define TSTATE_MCDE	_AC(0x0000000000080000,UL) /* MCD enable.       */
#define TSTATE_MG	_AC(0x0000000000040000,UL) /* MMU Globals.	*/
#define TSTATE_CLE	_AC(0x0000000000020000,UL) /* CurrLittleEndian.	*/
#define TSTATE_TLE	_AC(0x0000000000010000,UL) /* TrapLittleEndian.	*/
#define TSTATE_MM	_AC(0x000000000000c000,UL) /* Memory Model.	*/
#define TSTATE_TSO	_AC(0x0000000000000000,UL) /* MM: TSO		*/
#define TSTATE_PSO	_AC(0x0000000000004000,UL) /* MM: PSO		*/
#define TSTATE_RMO	_AC(0x0000000000008000,UL) /* MM: RMO		*/
#define TSTATE_RED	_AC(0x0000000000002000,UL) /* Reset Error Debug.*/
#define TSTATE_PEF	_AC(0x0000000000001000,UL) /* FPU Enable.	*/
#define TSTATE_AM	_AC(0x0000000000000800,UL) /* Address Mask.	*/
#define TSTATE_PRIV	_AC(0x0000000000000400,UL) /* Privilege.	*/
#define TSTATE_IE	_AC(0x0000000000000200,UL) /* Interrupt Enable.	*/
#define TSTATE_AG	_AC(0x0000000000000100,UL) /* Alternate Globals.*/
#define TSTATE_SYSCALL	_AC(0x0000000000000020,UL) /* in syscall trap   */
#define TSTATE_CWP	_AC(0x000000000000001f,UL) /* Curr Win-Pointer.	*/

/* Floating-Point Registers State Register.
 *
 * --------------------------------
 * |  Resv  |  FEF  |  DU  |  DL  |
 * --------------------------------
 *  63     3    2       1      0
 */
#define FPRS_FEF	_AC(0x0000000000000004,UL) /* FPU Enable.	*/
#define FPRS_DU		_AC(0x0000000000000002,UL) /* Dirty Upper.	*/
#define FPRS_DL		_AC(0x0000000000000001,UL) /* Dirty Lower.	*/

/* Version Register.
 *
 * ------------------------------------------------------
 * | MANUF | IMPL | MASK | Resv | MAXTL | Resv | MAXWIN |
 * ------------------------------------------------------
 *  63   48 47  32 31  24 23  16 15    8 7    5 4      0
 */
#define VERS_MANUF	_AC(0xffff000000000000,UL) /* Manufacturer.	*/
#define VERS_IMPL	_AC(0x0000ffff00000000,UL) /* Implementation.	*/
#define VERS_MASK	_AC(0x00000000ff000000,UL) /* Mask Set Revision.*/
#define VERS_MAXTL	_AC(0x000000000000ff00,UL) /* Max Trap Level.	*/
#define VERS_MAXWIN	_AC(0x000000000000001f,UL) /* Max RegWindow Idx.*/

/* Compatibility Feature Register (%asr26), SPARC-T4 and later  */
#define CFR_AES		_AC(0x0000000000000001,UL) /* Supports AES opcodes     */
#define CFR_DES		_AC(0x0000000000000002,UL) /* Supports DES opcodes     */
#define CFR_KASUMI	_AC(0x0000000000000004,UL) /* Supports KASUMI opcodes  */
#define CFR_CAMELLIA	_AC(0x0000000000000008,UL) /* Supports CAMELLIA opcodes*/
#define CFR_MD5		_AC(0x0000000000000010,UL) /* Supports MD5 opcodes     */
#define CFR_SHA1	_AC(0x0000000000000020,UL) /* Supports SHA1 opcodes    */
#define CFR_SHA256	_AC(0x0000000000000040,UL) /* Supports SHA256 opcodes  */
#define CFR_SHA512	_AC(0x0000000000000080,UL) /* Supports SHA512 opcodes  */
#define CFR_MPMUL	_AC(0x0000000000000100,UL) /* Supports MPMUL opcodes   */
#define CFR_MONTMUL	_AC(0x0000000000000200,UL) /* Supports MONTMUL opcodes */
#define CFR_MONTSQR	_AC(0x0000000000000400,UL) /* Supports MONTSQR opcodes */
#define CFR_CRC32C	_AC(0x0000000000000800,UL) /* Supports CRC32C opcodes  */

#endif /* !(_SPARC64_PSTATE_H) */
