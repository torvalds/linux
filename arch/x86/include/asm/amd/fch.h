/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_AMD_FCH_H_
#define _ASM_X86_AMD_FCH_H_

#define FCH_PM_BASE			0xFED80300

/* Register offsets from PM base: */
#define FCH_PM_DECODEEN			0x00
#define FCH_PM_DECODEEN_SMBUS0SEL	GENMASK(20, 19)

#endif /* _ASM_X86_AMD_FCH_H_ */
