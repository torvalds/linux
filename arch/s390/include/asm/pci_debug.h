/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_ASM_PCI_DE_H
#define _S390_ASM_PCI_DE_H

#include <asm/de.h>

extern de_info_t *pci_de_msg_id;
extern de_info_t *pci_de_err_id;

#define zpci_dbg(imp, fmt, args...)				\
	de_sprintf_event(pci_de_msg_id, imp, fmt, ##args)

#define zpci_err(text...)							\
	do {									\
		char de_buffer[16];						\
		snprintf(de_buffer, 16, text);				\
		de_text_event(pci_de_err_id, 0, de_buffer);		\
	} while (0)

static inline void zpci_err_hex(void *addr, int len)
{
	de_event(pci_de_err_id, 0, addr, len);
}

#endif
