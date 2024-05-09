/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASMPARISC_SHMPARAM_H
#define _ASMPARISC_SHMPARAM_H

/*
 * PA-RISC uses virtually indexed & physically tagged (VIPT) caches
 * which has strict requirements when two pages to the same physical
 * address are accessed through different mappings. Read the section
 * "Address Aliasing" in the arch docs for more detail:
 * PA-RISC 1.1 (page 3-6):
 * https://parisc.wiki.kernel.org/images-parisc/6/68/Pa11_acd.pdf
 * PA-RISC 2.0 (page F-5):
 * https://parisc.wiki.kernel.org/images-parisc/7/73/Parisc2.0.pdf
 *
 * For Linux we allow kernel and userspace to map pages on page size
 * granularity (SHMLBA) but have to ensure that, if two pages are
 * mapped to the same physical address, the virtual and physical
 * addresses modulo SHM_COLOUR are identical.
 */
#define SHMLBA	   PAGE_SIZE	/* attach addr a multiple of this */
#define SHM_COLOUR 0x00400000	/* shared mappings colouring */

#endif /* _ASMPARISC_SHMPARAM_H */
