// SPDX-License-Identifier: GPL-2.0
#include <linux/elf.h>
#include <asm/kexec.h>

int arch_kexec_do_relocs(int r_type, void *loc, unsigned long val,
			 unsigned long addr)
{
	switch (r_type) {
	case R_390_NONE:
		break;
	case R_390_8:		/* Direct 8 bit.   */
		*(u8 *)loc = val;
		break;
	case R_390_12:		/* Direct 12 bit.  */
		*(u16 *)loc &= 0xf000;
		*(u16 *)loc |= val & 0xfff;
		break;
	case R_390_16:		/* Direct 16 bit.  */
		*(u16 *)loc = val;
		break;
	case R_390_20:		/* Direct 20 bit.  */
		*(u32 *)loc &= 0xf00000ff;
		*(u32 *)loc |= (val & 0xfff) << 16;	/* DL */
		*(u32 *)loc |= (val & 0xff000) >> 4;	/* DH */
		break;
	case R_390_32:		/* Direct 32 bit.  */
		*(u32 *)loc = val;
		break;
	case R_390_64:		/* Direct 64 bit.  */
	case R_390_GLOB_DAT:
	case R_390_JMP_SLOT:
		*(u64 *)loc = val;
		break;
	case R_390_PC16:	/* PC relative 16 bit.	*/
		*(u16 *)loc = (val - addr);
		break;
	case R_390_PC16DBL:	/* PC relative 16 bit shifted by 1.  */
		*(u16 *)loc = (val - addr) >> 1;
		break;
	case R_390_PC32DBL:	/* PC relative 32 bit shifted by 1.  */
		*(u32 *)loc = (val - addr) >> 1;
		break;
	case R_390_PC32:	/* PC relative 32 bit.	*/
		*(u32 *)loc = (val - addr);
		break;
	case R_390_PC64:	/* PC relative 64 bit.	*/
		*(u64 *)loc = (val - addr);
		break;
	case R_390_RELATIVE:
		*(unsigned long *) loc = val;
		break;
	default:
		return 1;
	}
	return 0;
}
