/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CE4100_H_
#define _ASM_CE4100_H_

int ce4100_pci_init(void);

#ifdef CONFIG_SERIAL_8250
void __init sdv_serial_fixup(void);
#else
static inline void sdv_serial_fixup(void) {};
#endif

#endif
