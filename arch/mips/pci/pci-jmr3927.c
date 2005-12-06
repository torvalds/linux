/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
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
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/jmr3927/jmr3927.h>
#include <asm/debug.h>

struct resource pci_io_resource = {
	"IO MEM",
	0x1000,			/* reserve regacy I/O space */
	0x1000 + JMR3927_PCIIO_SIZE - 1,
	IORESOURCE_IO
};

struct resource pci_mem_resource = {
	"PCI MEM",
	JMR3927_PCIMEM,
	JMR3927_PCIMEM + JMR3927_PCIMEM_SIZE - 1,
	IORESOURCE_MEM
};

extern struct pci_ops jmr3927_pci_ops;

struct pci_controller jmr3927_controller = {
	.pci_ops	= &jmr3927_pci_ops,
	.io_resource	= &pci_io_resource,
	.mem_resource	= &pci_mem_resource,
	.mem_offset	= JMR3927_PCIMEM
};
