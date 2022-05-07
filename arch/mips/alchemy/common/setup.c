/*
 * Copyright 2000, 2007-2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com
 *
 * Updates to 2.6, Pete Popov, Embedded Alley Solutions, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/dma-coherence.h>
#include <asm/mipsregs.h>

#include <au1000.h>

extern void __init board_setup(void);
extern void __init alchemy_set_lpj(void);

void __init plat_mem_setup(void)
{
	alchemy_set_lpj();

	if (au1xxx_cpu_needs_config_od())
		/* Various early Au1xx0 errata corrected by this */
		set_c0_config(1 << 19); /* Set Config[OD] */
	else
		/* Clear to obtain best system bus performance */
		clear_c0_config(1 << 19); /* Clear Config[OD] */

	hw_coherentio = 0;
	coherentio = IO_COHERENCE_ENABLED;
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1100:
		coherentio = IO_COHERENCE_DISABLED;
		break;
	case ALCHEMY_CPU_AU1200:
		/* Au1200 AB USB does not support coherent memory */
		if (0 == (read_c0_prid() & PRID_REV_MASK))
			coherentio = IO_COHERENCE_DISABLED;
		break;
	}

	board_setup();	/* board specific setup */

	/* IO/MEM resources. */
	set_io_port_base(0);
	ioport_resource.start = IOPORT_RESOURCE_START;
	ioport_resource.end = IOPORT_RESOURCE_END;
	iomem_resource.start = IOMEM_RESOURCE_START;
	iomem_resource.end = IOMEM_RESOURCE_END;
}

#ifdef CONFIG_MIPS_FIXUP_BIGPHYS_ADDR
/* This routine should be valid for all Au1x based boards */
phys_addr_t fixup_bigphys_addr(phys_addr_t phys_addr, phys_addr_t size)
{
	unsigned long start = ALCHEMY_PCI_MEMWIN_START;
	unsigned long end = ALCHEMY_PCI_MEMWIN_END;

	/* Don't fixup 36-bit addresses */
	if ((phys_addr >> 32) != 0)
		return phys_addr;

	/* Check for PCI memory window */
	if (phys_addr >= start && (phys_addr + size - 1) <= end)
		return (phys_addr_t)(AU1500_PCI_MEM_PHYS_ADDR + phys_addr);

	/* default nop */
	return phys_addr;
}

int io_remap_pfn_range(struct vm_area_struct *vma, unsigned long vaddr,
		unsigned long pfn, unsigned long size, pgprot_t prot)
{
	phys_addr_t phys_addr = fixup_bigphys_addr(pfn << PAGE_SHIFT, size);

	return remap_pfn_range(vma, vaddr, phys_addr >> PAGE_SHIFT, size, prot);
}
EXPORT_SYMBOL(io_remap_pfn_range);
#endif /* CONFIG_MIPS_FIXUP_BIGPHYS_ADDR */
