/*
 * Definitions for the SH-2 DMAC.
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH2_DMA_H
#define __ASM_CPU_SH2_DMA_H

#define SH_MAX_DMA_CHANNELS	2

#define SAR	((unsigned long[]){ 0xffffff80, 0xffffff90 })
#define DAR	((unsigned long[]){ 0xffffff84, 0xffffff94 })
#define DMATCR	((unsigned long[]){ 0xffffff88, 0xffffff98 })
#define CHCR	((unsigned long[]){ 0xfffffffc, 0xffffff9c })

#define DMAOR	0xffffffb0

#endif /* __ASM_CPU_SH2_DMA_H */

