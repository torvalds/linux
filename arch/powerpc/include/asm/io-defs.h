/* SPDX-License-Identifier: GPL-2.0 */
/* This file is meant to be include multiple times by other headers */

DEF_PCI_AC_RET(inb, u8, (unsigned long port), (port))
DEF_PCI_AC_RET(inw, u16, (unsigned long port), (port))
DEF_PCI_AC_RET(inl, u32, (unsigned long port), (port))
DEF_PCI_AC_NORET(outb, (u8 val, unsigned long port), (val, port))
DEF_PCI_AC_NORET(outw, (u16 val, unsigned long port), (val, port))
DEF_PCI_AC_NORET(outl, (u32 val, unsigned long port), (val, port))
DEF_PCI_AC_NORET(insb, (unsigned long p, void *b, unsigned long c), (p, b, c))
DEF_PCI_AC_NORET(insw, (unsigned long p, void *b, unsigned long c), (p, b, c))
DEF_PCI_AC_NORET(insl, (unsigned long p, void *b, unsigned long c), (p, b, c))
DEF_PCI_AC_NORET(outsb, (unsigned long p, const void *b, unsigned long c), (p, b, c))
DEF_PCI_AC_NORET(outsw, (unsigned long p, const void *b, unsigned long c), (p, b, c))
DEF_PCI_AC_NORET(outsl, (unsigned long p, const void *b, unsigned long c), (p, b, c))
