/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_ASM_PCI_DEBUG_H
#define _S390_ASM_PCI_DEBUG_H

#include <asm/debug.h>

extern debug_info_t *pci_debug_msg_id;
extern debug_info_t *pci_debug_err_id;

#define zpci_dbg(imp, fmt, args...)				\
	debug_sprintf_event(pci_debug_msg_id, imp, fmt, ##args)

#define zpci_err(text...)							\
	do {									\
		char debug_buffer[16];						\
		snprintf(debug_buffer, 16, text);				\
		debug_text_event(pci_debug_err_id, 0, debug_buffer);		\
	} while (0)

static inline void zpci_err_hex(void *addr, int len)
{
	while (len > 0) {
		debug_event(pci_debug_err_id, 0, (void *) addr, len);
		len -= pci_debug_err_id->buf_size;
		addr += pci_debug_err_id->buf_size;
	}
}

#endif
