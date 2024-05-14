/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BMIPS_SPACES_H
#define __ASM_BMIPS_SPACES_H

/* Avoid collisions with system base register (SBR) region on BMIPS3300 */
#define FIXADDR_TOP		((unsigned long)(long)(int)0xff000000)

#endif /* __ASM_BMIPS_SPACES_H */
