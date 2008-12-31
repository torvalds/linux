/*
 * Trivial I/O routine definitions, intentionally meant to be included
 * multiple times. Ugly I/O routine concatenation helpers taken from
 * alpha. Must be included _before_ io.h to avoid preprocessor-induced
 * routine mismatch.
 */
#define IO_CONCAT(a,b)	_IO_CONCAT(a,b)
#define _IO_CONCAT(a,b)	a ## _ ## b

#ifndef __IO_PREFIX
#error "Don't include this header without a valid system prefix"
#endif

u8 IO_CONCAT(__IO_PREFIX,inb)(unsigned long);
u16 IO_CONCAT(__IO_PREFIX,inw)(unsigned long);
u32 IO_CONCAT(__IO_PREFIX,inl)(unsigned long);

void IO_CONCAT(__IO_PREFIX,outb)(u8, unsigned long);
void IO_CONCAT(__IO_PREFIX,outw)(u16, unsigned long);
void IO_CONCAT(__IO_PREFIX,outl)(u32, unsigned long);

u8 IO_CONCAT(__IO_PREFIX,inb_p)(unsigned long);
u16 IO_CONCAT(__IO_PREFIX,inw_p)(unsigned long);
u32 IO_CONCAT(__IO_PREFIX,inl_p)(unsigned long);
void IO_CONCAT(__IO_PREFIX,outb_p)(u8, unsigned long);
void IO_CONCAT(__IO_PREFIX,outw_p)(u16, unsigned long);
void IO_CONCAT(__IO_PREFIX,outl_p)(u32, unsigned long);

void IO_CONCAT(__IO_PREFIX,insb)(unsigned long, void *dst, unsigned long count);
void IO_CONCAT(__IO_PREFIX,insw)(unsigned long, void *dst, unsigned long count);
void IO_CONCAT(__IO_PREFIX,insl)(unsigned long, void *dst, unsigned long count);
void IO_CONCAT(__IO_PREFIX,outsb)(unsigned long, const void *src, unsigned long count);
void IO_CONCAT(__IO_PREFIX,outsw)(unsigned long, const void *src, unsigned long count);
void IO_CONCAT(__IO_PREFIX,outsl)(unsigned long, const void *src, unsigned long count);

void *IO_CONCAT(__IO_PREFIX,ioremap)(unsigned long offset, unsigned long size);
void IO_CONCAT(__IO_PREFIX,iounmap)(void *addr);

void __iomem *IO_CONCAT(__IO_PREFIX,ioport_map)(unsigned long addr, unsigned int size);
void IO_CONCAT(__IO_PREFIX,ioport_unmap)(void __iomem *addr);

#undef __IO_PREFIX
