/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 * Rewritten for Linux 2.6 by Christoph Hellwig (hch@lst.de) Jan 2004
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/page.h>

#ifdef CONFIG_NUMA
# include <mmzone.h>
#endif

#ifndef pa_to_nid
#define pa_to_nid(addr) 0
#endif

#ifndef nid_to_addrbase
#define nid_to_addrbase(nid) 0
#endif

#endif /* _ASM_MMZONE_H_ */
