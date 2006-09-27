/*
 *	I/O routines for Titan
 */

#include <linux/pci.h>
#include <asm/machvec.h>
#include <asm/addrspace.h>
#include <asm/titan.h>
#include <asm/io.h>
#include "../../drivers/pci/pci-sh7751.h"

#define PCIIOBR         (volatile long *)PCI_REG(SH7751_PCIIOBR)
#define PCIMBR          (volatile long *)PCI_REG(SH7751_PCIMBR)
#define PCI_IO_AREA     SH7751_PCI_IO_BASE
#define PCI_MEM_AREA    SH7751_PCI_CONFIG_BASE

#define PCI_IOMAP(adr)  (PCI_IO_AREA + (adr & ~SH7751_PCIIOBR_MASK))

#if defined(CONFIG_PCI)
#define CHECK_SH7751_PCIIO(port) \
  ((port >= PCIBIOS_MIN_IO) && (port < (PCIBIOS_MIN_IO + SH7751_PCI_IO_SIZE)))
#define CHECK_SH7751_PCIMEMIO(port) \
  ((port >= PCIBIOS_MIN_MEM) && (port < (PCIBIOS_MIN_MEM + SH7751_PCI_MEM_SIZE)))
#else
#define CHECK_SH7751_PCIIO(port) (0)
#endif

static inline void delay(void)
{
        ctrl_inw(0xa0000000);
}

static inline volatile u16 *port2adr(unsigned int port)
{
        maybebadio((unsigned long)port);
        return (volatile u16*)port;
}

u8 titan_inb(unsigned long port)
{
        if (PXSEG(port))
                return ctrl_inb(port);
        else if (CHECK_SH7751_PCIIO(port))
                return ctrl_inb(PCI_IOMAP(port));
        return ctrl_inw(port2adr(port)) & 0xff;
}

u8 titan_inb_p(unsigned long port)
{
        u8 v;

        if (PXSEG(port))
                v = ctrl_inb(port);
        else if (CHECK_SH7751_PCIIO(port))
                v = ctrl_inb(PCI_IOMAP(port));
        else
                v = ctrl_inw(port2adr(port)) & 0xff;
        delay();
        return v;
}

u16 titan_inw(unsigned long port)
{
        if (PXSEG(port))
                return ctrl_inw(port);
        else if (CHECK_SH7751_PCIIO(port))
                return ctrl_inw(PCI_IOMAP(port));
        else if (port >= 0x2000)
                return ctrl_inw(port2adr(port));
        else
                maybebadio(port);
        return 0;
}

u32 titan_inl(unsigned long port)
{
        if (PXSEG(port))
                return ctrl_inl(port);
        else if (CHECK_SH7751_PCIIO(port))
                return ctrl_inl(PCI_IOMAP(port));
        else if (port >= 0x2000)
                return ctrl_inw(port2adr(port));
        else
                maybebadio(port);
        return 0;
}

void titan_outb(u8 value, unsigned long port)
{
        if (PXSEG(port))
                ctrl_outb(value, port);
        else if (CHECK_SH7751_PCIIO(port))
                ctrl_outb(value, PCI_IOMAP(port));
        else
                ctrl_outw(value, port2adr(port));
}

void titan_outb_p(u8 value, unsigned long port)
{
        if (PXSEG(port))
                ctrl_outb(value, port);
        else if (CHECK_SH7751_PCIIO(port))
                ctrl_outb(value, PCI_IOMAP(port));
        else
                ctrl_outw(value, port2adr(port));
        delay();
}

void titan_outw(u16 value, unsigned long port)
{
        if (PXSEG(port))
                ctrl_outw(value, port);
        else if (CHECK_SH7751_PCIIO(port))
                ctrl_outw(value, PCI_IOMAP(port));
        else if (port >= 0x2000)
                ctrl_outw(value, port2adr(port));
        else
                maybebadio(port);
}

void titan_outl(u32 value, unsigned long port)
{
        if (PXSEG(port))
                ctrl_outl(value, port);
        else if (CHECK_SH7751_PCIIO(port))
                ctrl_outl(value, PCI_IOMAP(port));
        else
                maybebadio(port);
}

void titan_insl(unsigned long port, void *dst, unsigned long count)
{
        maybebadio(port);
}

void titan_outsl(unsigned long port, const void *src, unsigned long count)
{
        maybebadio(port);
}

void *titan_ioremap(unsigned long offset, unsigned long size) {
	if (CHECK_SH7751_PCIIO(offset) || CHECK_SH7751_PCIMEMIO(offset))
		return (void *)offset;
}

void __iomem *titan_ioport_map(unsigned long port, unsigned int size)
{
	if (PXSEG(port))
		return (void __iomem *)port;
	else if (CHECK_SH7751_PCIIO(port))
		return (void __iomem *)PCI_IOMAP(port);

	return (void __iomem *)port2adr(port);
}

EXPORT_SYMBOL(titan_ioremap);
