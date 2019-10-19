/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/asm-m68k/raw_io.h
 *
 * 10/20/00 RZ: - created from bits of io.h and ide.h to cleanup namespace
 *
 */

#ifndef _RAW_IO_H
#define _RAW_IO_H

#ifdef __KERNEL__

#include <asm/byteorder.h>

/* ++roman: The assignments to temp. vars avoid that gcc sometimes generates
 * two accesses to memory, which may be undesirable for some devices.
 */
#define in_8(addr) \
    ({ u8 __v = (*(__force volatile u8 *) (addr)); __v; })
#define in_be16(addr) \
    ({ u16 __v = (*(__force volatile u16 *) (addr)); __v; })
#define in_be32(addr) \
    ({ u32 __v = (*(__force volatile u32 *) (addr)); __v; })
#define in_le16(addr) \
    ({ u16 __v = le16_to_cpu(*(__force volatile __le16 *) (addr)); __v; })
#define in_le32(addr) \
    ({ u32 __v = le32_to_cpu(*(__force volatile __le32 *) (addr)); __v; })

#define out_8(addr,b) (void)((*(__force volatile u8 *) (addr)) = (b))
#define out_be16(addr,w) (void)((*(__force volatile u16 *) (addr)) = (w))
#define out_be32(addr,l) (void)((*(__force volatile u32 *) (addr)) = (l))
#define out_le16(addr,w) (void)((*(__force volatile __le16 *) (addr)) = cpu_to_le16(w))
#define out_le32(addr,l) (void)((*(__force volatile __le32 *) (addr)) = cpu_to_le32(l))

#define raw_inb in_8
#define raw_inw in_be16
#define raw_inl in_be32
#define __raw_readb in_8
#define __raw_readw in_be16
#define __raw_readl in_be32

#define raw_outb(val,port) out_8((port),(val))
#define raw_outw(val,port) out_be16((port),(val))
#define raw_outl(val,port) out_be32((port),(val))
#define __raw_writeb(val,addr) out_8((addr),(val))
#define __raw_writew(val,addr) out_be16((addr),(val))
#define __raw_writel(val,addr) out_be32((addr),(val))

/*
 * Atari ROM port (cartridge port) ISA adapter, used for the EtherNEC NE2000
 * network card driver.
 * The ISA adapter connects address lines A9-A13 to ISA address lines A0-A4,
 * and hardwires the rest of the ISA addresses for a base address of 0x300.
 *
 * Data lines D8-D15 are connected to ISA data lines D0-D7 for reading.
 * For writes, address lines A1-A8 are latched to ISA data lines D0-D7
 * (meaning the bit pattern on A1-A8 can be read back as byte).
 *
 * Read and write operations are distinguished by the base address used:
 * reads are from the ROM A side range, writes are through the B side range
 * addresses (A side base + 0x10000).
 *
 * Reads and writes are byte only.
 *
 * 16 bit reads and writes are necessary for the NetUSBee adapter's USB
 * chipset - 16 bit words are read straight off the ROM port while 16 bit
 * reads are split into two byte writes. The low byte is latched to the
 * NetUSBee buffer by a read from the _read_ window (with the data pattern
 * asserted as A1-A8 address pattern). The high byte is then written to the
 * write range as usual, completing the write cycle.
 */

#if defined(CONFIG_ATARI_ROM_ISA)
#define rom_in_8(addr) \
	({ u16 __v = (*(__force volatile u16 *) (addr)); __v >>= 8; __v; })
#define rom_in_be16(addr) \
	({ u16 __v = (*(__force volatile u16 *) (addr)); __v; })
#define rom_in_le16(addr) \
	({ u16 __v = le16_to_cpu(*(__force volatile u16 *) (addr)); __v; })

#define rom_out_8(addr, b)	\
	({u8 __w, __v = (b);  u32 _addr = ((u32) (addr)); \
	__w = ((*(__force volatile u8 *)  ((_addr | 0x10000) + (__v<<1)))); })
#define rom_out_be16(addr, w)	\
	({u16 __w, __v = (w); u32 _addr = ((u32) (addr)); \
	__w = ((*(__force volatile u16 *) ((_addr & 0xFFFF0000UL) + ((__v & 0xFF)<<1)))); \
	__w = ((*(__force volatile u16 *) ((_addr | 0x10000) + ((__v >> 8)<<1)))); })
#define rom_out_le16(addr, w)	\
	({u16 __w, __v = (w); u32 _addr = ((u32) (addr)); \
	__w = ((*(__force volatile u16 *) ((_addr & 0xFFFF0000UL) + ((__v >> 8)<<1)))); \
	__w = ((*(__force volatile u16 *) ((_addr | 0x10000) + ((__v & 0xFF)<<1)))); })

#define raw_rom_inb rom_in_8
#define raw_rom_inw rom_in_be16

#define raw_rom_outb(val, port) rom_out_8((port), (val))
#define raw_rom_outw(val, port) rom_out_be16((port), (val))
#endif /* CONFIG_ATARI_ROM_ISA */

static inline void raw_insb(volatile u8 __iomem *port, u8 *buf, unsigned int len)
{
	unsigned int i;

        for (i = 0; i < len; i++)
		*buf++ = in_8(port);
}

static inline void raw_outsb(volatile u8 __iomem *port, const u8 *buf,
			     unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: moveb %0@+,%2@; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"moveb %0@+,%2@; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}

static inline void raw_insw(volatile u16 __iomem *port, u16 *buf, unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movew %2@,%0@+; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}

static inline void raw_outsw(volatile u16 __iomem *port, const u16 *buf,
			     unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movew %0@+,%2@; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}

static inline void raw_insl(volatile u32 __iomem *port, u32 *buf, unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movel %2@,%0@+; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}

static inline void raw_outsl(volatile u32 __iomem *port, const u32 *buf,
			     unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movel %0@+,%2@; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}


static inline void raw_insw_swapw(volatile u16 __iomem *port, u16 *buf,
				  unsigned int nr)
{
    if ((nr) % 8)
	__asm__ __volatile__
	       ("\tmovel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"dbra %/d6,1b"
		:
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
    else
	__asm__ __volatile__
	       ("movel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"lsrl  #3,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"dbra %/d6,1b"
                :
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
}

static inline void raw_outsw_swapw(volatile u16 __iomem *port, const u16 *buf,
				   unsigned int nr)
{
    if ((nr) % 8)
	__asm__ __volatile__
	       ("movel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"dbra %/d6,1b"
                :
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
    else
	__asm__ __volatile__
	       ("movel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"lsrl  #3,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"dbra %/d6,1b"
                :
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
}


#if defined(CONFIG_ATARI_ROM_ISA)
static inline void raw_rom_insb(volatile u8 __iomem *port, u8 *buf, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++)
		*buf++ = rom_in_8(port);
}

static inline void raw_rom_outsb(volatile u8 __iomem *port, const u8 *buf,
			     unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++)
		rom_out_8(port, *buf++);
}

static inline void raw_rom_insw(volatile u16 __iomem *port, u16 *buf,
				   unsigned int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++)
		*buf++ = rom_in_be16(port);
}

static inline void raw_rom_outsw(volatile u16 __iomem *port, const u16 *buf,
				   unsigned int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++)
		rom_out_be16(port, *buf++);
}

static inline void raw_rom_insw_swapw(volatile u16 __iomem *port, u16 *buf,
				   unsigned int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++)
		*buf++ = rom_in_le16(port);
}

static inline void raw_rom_outsw_swapw(volatile u16 __iomem *port, const u16 *buf,
				   unsigned int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++)
		rom_out_le16(port, *buf++);
}
#endif /* CONFIG_ATARI_ROM_ISA */

#endif /* __KERNEL__ */

#endif /* _RAW_IO_H */
