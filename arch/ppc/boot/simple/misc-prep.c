/*
 * arch/ppc/boot/simple/misc-prep.c
 *
 * Maintainer: Tom Rini <trini@kernel.crashing.org>
 *
 * In the past: Gary Thomas, Cort Dougan <cort@cs.nmt.edu>
 */

#include <linux/config.h>
#include <linux/pci_ids.h>
#include <linux/types.h>
#include <asm/residual.h>
#include <asm/string.h>
#include <asm/byteorder.h>
#include "mpc10x.h"
#include "of1275.h"
#include "nonstdio.h"

extern int keyb_present;	/* keyboard controller is present by default */
RESIDUAL hold_resid_buf;
RESIDUAL *hold_residual = &hold_resid_buf;
static void *OFW_interface;	/* Pointer to OF, if available. */

#ifdef CONFIG_VGA_CONSOLE
char *vidmem = (char *)0xC00B8000;
int lines = 25, cols = 80;
int orig_x, orig_y = 24;
#endif /* CONFIG_VGA_CONSOLE */

extern int CRT_tstc(void);
extern int vga_init(unsigned char *ISA_mem);
extern void gunzip(void *, int, unsigned char *, int *);
extern unsigned long serial_init(int chan, void *ignored);
extern void serial_fixups(void);
extern struct bi_record *decompress_kernel(unsigned long load_addr,
		int num_words, unsigned long cksum);
extern void disable_6xx_mmu(void);
extern unsigned long mpc10x_get_mem_size(void);

static void
writel(unsigned int val, unsigned int address)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	*(unsigned int *)address = cpu_to_le32(val);
}

#define PCI_CFG_ADDR(dev,off)	((0x80<<24) | (dev<<8) | (off&0xfc))
#define PCI_CFG_DATA(off)	(MPC10X_MAPA_CNFG_DATA+(off&3))

static void
pci_read_config_32(unsigned char devfn,
		unsigned char offset,
		unsigned int *val)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	*(unsigned int *)PCI_CFG_ADDR(devfn,offset) =
		cpu_to_le32(MPC10X_MAPA_CNFG_ADDR);
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	*val = le32_to_cpu(*(unsigned int *)PCI_CFG_DATA(offset));
	return;
}

#ifdef CONFIG_VGA_CONSOLE
void
scroll(void)
{
	int i;

	memcpy ( vidmem, vidmem + cols * 2, ( lines - 1 ) * cols * 2 );
	for ( i = ( lines - 1 ) * cols * 2; i < lines * cols * 2; i += 2 )
		vidmem[i] = ' ';
}
#endif /* CONFIG_VGA_CONSOLE */

unsigned long
load_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		  RESIDUAL *residual, void *OFW)
{
	int start_multi = 0;
	unsigned int pci_viddid, pci_did, tulip_pci_base, tulip_base;

	/* If we have Open Firmware, initialise it immediately */
	if (OFW) {
		OFW_interface = OFW;
		ofinit(OFW_interface);
	}

	board_isa_init();
#if defined(CONFIG_VGA_CONSOLE)
	vga_init((unsigned char *)0xC0000000);
#endif /* CONFIG_VGA_CONSOLE */

	if (residual) {
		/* Is this Motorola PPCBug? */
		if ((1 & residual->VitalProductData.FirmwareSupports) &&
		    (1 == residual->VitalProductData.FirmwareSupplier)) {
			unsigned char base_mod;
			unsigned char board_type = inb(0x801) & 0xF0;

			/*
			 * Reset the onboard 21x4x Ethernet
			 * Motorola Ethernet is at IDSEL 14 (devfn 0x70)
			 */
			pci_read_config_32(0x70, 0x00, &pci_viddid);
			pci_did = (pci_viddid & 0xffff0000) >> 16;
			/* Be sure we've really found a 21x4x chip */
			if (((pci_viddid & 0xffff) == PCI_VENDOR_ID_DEC) &&
				((pci_did == PCI_DEVICE_ID_DEC_TULIP_FAST) ||
				(pci_did == PCI_DEVICE_ID_DEC_TULIP) ||
				(pci_did == PCI_DEVICE_ID_DEC_TULIP_PLUS) ||
				(pci_did == PCI_DEVICE_ID_DEC_21142))) {
				pci_read_config_32(0x70,
						0x10,
						&tulip_pci_base);
				/* Get the physical base address */
				tulip_base =
					(tulip_pci_base & ~0x03UL) + 0x80000000;
				/* Strobe the 21x4x reset bit in CSR0 */
				writel(0x1, tulip_base);
			}

			/* If this is genesis 2 board then check for no
			 * keyboard controller and more than one processor.
			 */
			if (board_type == 0xe0) {
				base_mod = inb(0x803);
				/* if a MVME2300/2400 or a Sitka then no keyboard */
				if((base_mod == 0xFA) || (base_mod == 0xF9) ||
				   (base_mod == 0xE1)) {
					keyb_present = 0;	/* no keyboard */
				}
			}
			/* If this is a multiprocessor system then
			 * park the other processor so that the
			 * kernel knows where to find them.
			 */
			if (residual->MaxNumCpus > 1)
				start_multi = 1;
		}
		memcpy(hold_residual,residual,sizeof(RESIDUAL));
        }

	/* Call decompress_kernel */
	decompress_kernel(load_addr, num_words, cksum);

	if (start_multi) {
		residual->VitalProductData.SmpIar = (unsigned long)0xc0;
		residual->Cpus[1].CpuState = CPU_GOOD;
		hold_residual->VitalProductData.Reserved5 = 0xdeadbeef;
	}

	/* Now go and clear out the BATs and ensure that our MSR is
	 * correct .*/
	disable_6xx_mmu();

	/* Make r3 be a pointer to the residual data. */
	return (unsigned long)hold_residual;
}

unsigned long
get_mem_size(void)
{
	unsigned int pci_viddid, pci_did;

	/* First, figure out what kind of host bridge we are on.  If it's
	 * an MPC10x, we can ask it directly how much memory it has.
	 * Otherwise, see if the residual data has anything.  This isn't
	 * the best way, but it can be the only way.  If there's nothing,
	 * assume 32MB. -- Tom.
	 */
	/* See what our host bridge is. */
	pci_read_config_32(0x00, 0x00, &pci_viddid);
	pci_did = (pci_viddid & 0xffff0000) >> 16;
	/* See if we are on an MPC10x. */
	if (((pci_viddid & 0xffff) == PCI_VENDOR_ID_MOTOROLA)
			&& ((pci_did == PCI_DEVICE_ID_MOTOROLA_MPC105)
				|| (pci_did == PCI_DEVICE_ID_MOTOROLA_MPC106)
				|| (pci_did == PCI_DEVICE_ID_MOTOROLA_MPC107)))
		return mpc10x_get_mem_size();
	/* If it's not, see if we have anything in the residual data. */
	else if (hold_residual && hold_residual->TotalMemory)
		return hold_residual->TotalMemory;
	else if (OFW_interface) {
		/*
		 * This is a 'best guess' check.  We want to make sure
		 * we don't try this on a PReP box without OF
		 *     -- Cort
		 */
		while (OFW_interface)
		{
			phandle dev_handle;
			int mem_info[2];

			/* get handle to memory description */
			if (!(dev_handle = finddevice("/memory@0")))
				break;

			/* get the info */
			if (getprop(dev_handle, "reg", mem_info,
						sizeof(mem_info)) != 8)
				break;

			return mem_info[1];
		}
	}

	/* Fall back to hard-coding 32MB. */
	return 32*1024*1024;
}
