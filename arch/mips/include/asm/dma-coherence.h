/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 *
 */
#ifndef __ASM_DMA_COHERENCE_H
#define __ASM_DMA_COHERENCE_H

#ifdef CONFIG_DMA_MAYBE_COHERENT
extern int coherentio;
extern int hw_coherentio;
#else
#ifdef CONFIG_DMA_COHERENT
#define coherentio	1
#else
#define coherentio	0
#endif
#define hw_coherentio	0
#endif /* CONFIG_DMA_MAYBE_COHERENT */

#endif
