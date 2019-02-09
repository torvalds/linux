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

enum coherent_io_user_state {
	IO_COHERENCE_DEFAULT,
	IO_COHERENCE_ENABLED,
	IO_COHERENCE_DISABLED,
};

#if defined(CONFIG_DMA_PERDEV_COHERENT)
/* Don't provide (hw_)coherentio to avoid misuse */
#elif defined(CONFIG_DMA_MAYBE_COHERENT)
extern enum coherent_io_user_state coherentio;
extern int hw_coherentio;
#else
#ifdef CONFIG_DMA_NONCOHERENT
#define coherentio	IO_COHERENCE_DISABLED
#else
#define coherentio	IO_COHERENCE_ENABLED
#endif
#define hw_coherentio	0
#endif /* CONFIG_DMA_MAYBE_COHERENT */

#endif
