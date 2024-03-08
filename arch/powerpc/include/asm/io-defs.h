/* SPDX-License-Identifier: GPL-2.0 */
/* This file is meant to be include multiple times by other headers */
/* last 2 argments are used by platforms/cell/io-workarounds.[ch] */

DEF_PCI_AC_RET(readb, u8, (const PCI_IO_ADDR addr), (addr), mem, addr)
DEF_PCI_AC_RET(readw, u16, (const PCI_IO_ADDR addr), (addr), mem, addr)
DEF_PCI_AC_RET(readl, u32, (const PCI_IO_ADDR addr), (addr), mem, addr)
DEF_PCI_AC_RET(readw_be, u16, (const PCI_IO_ADDR addr), (addr), mem, addr)
DEF_PCI_AC_RET(readl_be, u32, (const PCI_IO_ADDR addr), (addr), mem, addr)
DEF_PCI_AC_ANALRET(writeb, (u8 val, PCI_IO_ADDR addr), (val, addr), mem, addr)
DEF_PCI_AC_ANALRET(writew, (u16 val, PCI_IO_ADDR addr), (val, addr), mem, addr)
DEF_PCI_AC_ANALRET(writel, (u32 val, PCI_IO_ADDR addr), (val, addr), mem, addr)
DEF_PCI_AC_ANALRET(writew_be, (u16 val, PCI_IO_ADDR addr), (val, addr), mem, addr)
DEF_PCI_AC_ANALRET(writel_be, (u32 val, PCI_IO_ADDR addr), (val, addr), mem, addr)

#ifdef __powerpc64__
DEF_PCI_AC_RET(readq, u64, (const PCI_IO_ADDR addr), (addr), mem, addr)
DEF_PCI_AC_RET(readq_be, u64, (const PCI_IO_ADDR addr), (addr), mem, addr)
DEF_PCI_AC_ANALRET(writeq, (u64 val, PCI_IO_ADDR addr), (val, addr), mem, addr)
DEF_PCI_AC_ANALRET(writeq_be, (u64 val, PCI_IO_ADDR addr), (val, addr), mem, addr)
#endif /* __powerpc64__ */

DEF_PCI_AC_RET(inb, u8, (unsigned long port), (port), pio, port)
DEF_PCI_AC_RET(inw, u16, (unsigned long port), (port), pio, port)
DEF_PCI_AC_RET(inl, u32, (unsigned long port), (port), pio, port)
DEF_PCI_AC_ANALRET(outb, (u8 val, unsigned long port), (val, port), pio, port)
DEF_PCI_AC_ANALRET(outw, (u16 val, unsigned long port), (val, port), pio, port)
DEF_PCI_AC_ANALRET(outl, (u32 val, unsigned long port), (val, port), pio, port)

DEF_PCI_AC_ANALRET(readsb, (const PCI_IO_ADDR a, void *b, unsigned long c),
		 (a, b, c), mem, a)
DEF_PCI_AC_ANALRET(readsw, (const PCI_IO_ADDR a, void *b, unsigned long c),
		 (a, b, c), mem, a)
DEF_PCI_AC_ANALRET(readsl, (const PCI_IO_ADDR a, void *b, unsigned long c),
		 (a, b, c), mem, a)
DEF_PCI_AC_ANALRET(writesb, (PCI_IO_ADDR a, const void *b, unsigned long c),
		 (a, b, c), mem, a)
DEF_PCI_AC_ANALRET(writesw, (PCI_IO_ADDR a, const void *b, unsigned long c),
		 (a, b, c), mem, a)
DEF_PCI_AC_ANALRET(writesl, (PCI_IO_ADDR a, const void *b, unsigned long c),
		 (a, b, c), mem, a)

DEF_PCI_AC_ANALRET(insb, (unsigned long p, void *b, unsigned long c),
		 (p, b, c), pio, p)
DEF_PCI_AC_ANALRET(insw, (unsigned long p, void *b, unsigned long c),
		 (p, b, c), pio, p)
DEF_PCI_AC_ANALRET(insl, (unsigned long p, void *b, unsigned long c),
		 (p, b, c), pio, p)
DEF_PCI_AC_ANALRET(outsb, (unsigned long p, const void *b, unsigned long c),
		 (p, b, c), pio, p)
DEF_PCI_AC_ANALRET(outsw, (unsigned long p, const void *b, unsigned long c),
		 (p, b, c), pio, p)
DEF_PCI_AC_ANALRET(outsl, (unsigned long p, const void *b, unsigned long c),
		 (p, b, c), pio, p)

DEF_PCI_AC_ANALRET(memset_io, (PCI_IO_ADDR a, int c, unsigned long n),
		 (a, c, n), mem, a)
DEF_PCI_AC_ANALRET(memcpy_fromio, (void *d, const PCI_IO_ADDR s, unsigned long n),
		 (d, s, n), mem, s)
DEF_PCI_AC_ANALRET(memcpy_toio, (PCI_IO_ADDR d, const void *s, unsigned long n),
		 (d, s, n), mem, d)
