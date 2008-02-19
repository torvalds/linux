/*
 *	I/O routines for Titan
 */
#include <linux/pci.h>
#include <asm/machvec.h>
#include <asm/addrspace.h>
#include <mach/titan.h>
#include <asm/io.h>

static inline unsigned int port2adr(unsigned int port)
{
        maybebadio((unsigned long)port);
        return port;
}

u8 titan_inb(unsigned long port)
{
        if (PXSEG(port))
                return ctrl_inb(port);
        else if (is_pci_ioaddr(port))
                return ctrl_inb(pci_ioaddr(port));
        return ctrl_inw(port2adr(port)) & 0xff;
}

u8 titan_inb_p(unsigned long port)
{
        u8 v;

        if (PXSEG(port))
                v = ctrl_inb(port);
        else if (is_pci_ioaddr(port))
                v = ctrl_inb(pci_ioaddr(port));
        else
                v = ctrl_inw(port2adr(port)) & 0xff;
        ctrl_delay();
        return v;
}

u16 titan_inw(unsigned long port)
{
        if (PXSEG(port))
                return ctrl_inw(port);
        else if (is_pci_ioaddr(port))
                return ctrl_inw(pci_ioaddr(port));
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
        else if (is_pci_ioaddr(port))
                return ctrl_inl(pci_ioaddr(port));
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
        else if (is_pci_ioaddr(port))
                ctrl_outb(value, pci_ioaddr(port));
        else
                ctrl_outw(value, port2adr(port));
}

void titan_outb_p(u8 value, unsigned long port)
{
        if (PXSEG(port))
                ctrl_outb(value, port);
        else if (is_pci_ioaddr(port))
                ctrl_outb(value, pci_ioaddr(port));
        else
                ctrl_outw(value, port2adr(port));
        ctrl_delay();
}

void titan_outw(u16 value, unsigned long port)
{
        if (PXSEG(port))
                ctrl_outw(value, port);
        else if (is_pci_ioaddr(port))
                ctrl_outw(value, pci_ioaddr(port));
        else if (port >= 0x2000)
                ctrl_outw(value, port2adr(port));
        else
                maybebadio(port);
}

void titan_outl(u32 value, unsigned long port)
{
        if (PXSEG(port))
                ctrl_outl(value, port);
        else if (is_pci_ioaddr(port))
                ctrl_outl(value, pci_ioaddr(port));
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

void __iomem *titan_ioport_map(unsigned long port, unsigned int size)
{
	if (PXSEG(port))
		return (void __iomem *)port;
	else if (is_pci_ioaddr(port))
		return (void __iomem *)pci_ioaddr(port);

	return (void __iomem *)port2adr(port);
}
