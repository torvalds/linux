/*
 * arch/ppc/boot/spruce/misc.c
 *
 * Misc. bootloader code for IBM Spruce reference platform
 *
 * Authors: Johnnie Peters <jpeters@mvista.com>
 *	    Matt Porter <mporter@mvista.com>
 *
 * Derived from arch/ppc/boot/prep/misc.c
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/pci.h>

#include <asm/bootinfo.h>

extern unsigned long decompress_kernel(unsigned long load_addr, int num_words,
				       unsigned long cksum);

/* Define some important locations of the Spruce. */
#define SPRUCE_PCI_CONFIG_ADDR	0xfec00000
#define SPRUCE_PCI_CONFIG_DATA	0xfec00004

/* PCI configuration space access routines. */
unsigned int *pci_config_address = (unsigned int *)SPRUCE_PCI_CONFIG_ADDR;
unsigned char *pci_config_data   = (unsigned char *)SPRUCE_PCI_CONFIG_DATA;

void cpc700_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char *val)
{
	out_le32(pci_config_address,
		 (((bus & 0xff)<<16) | (dev_fn<<8) | (offset&0xfc) | 0x80000000));

	*val= (in_le32((unsigned *)pci_config_data) >> (8 * (offset & 3))) & 0xff;
}

void cpc700_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char val)
{
	out_le32(pci_config_address,
		 (((bus & 0xff)<<16) | (dev_fn<<8) | (offset&0xfc) | 0x80000000));

	out_8(pci_config_data + (offset&3), val);
}

void cpc700_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short *val)
{
	out_le32(pci_config_address,
		 (((bus & 0xff)<<16) | (dev_fn<<8) | (offset&0xfc) | 0x80000000));

	*val= in_le16((unsigned short *)(pci_config_data + (offset&3)));
}

void cpc700_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short val)
{
	out_le32(pci_config_address,
		 (((bus & 0xff)<<16) | (dev_fn<<8) | (offset&0xfc) | 0x80000000));

	out_le16((unsigned short *)(pci_config_data + (offset&3)), val);
}

void cpc700_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned int *val)
{
	out_le32(pci_config_address,
		 (((bus & 0xff)<<16) | (dev_fn<<8) | (offset&0xfc) | 0x80000000));

	*val= in_le32((unsigned *)pci_config_data);
}

void cpc700_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned int val)
{
	out_le32(pci_config_address,
		 (((bus & 0xff)<<16) | (dev_fn<<8) | (offset&0xfc) | 0x80000000));

	out_le32((unsigned *)pci_config_data, val);
}

#define PCNET32_WIO_RDP		0x10
#define PCNET32_WIO_RAP		0x12
#define PCNET32_WIO_RESET	0x14

#define PCNET32_DWIO_RDP	0x10
#define PCNET32_DWIO_RAP	0x14
#define PCNET32_DWIO_RESET	0x18

/* Processor interface config register access */
#define PIFCFGADDR 0xff500000
#define PIFCFGDATA 0xff500004

#define PLBMIFOPT 0x18 /* PLB Master Interface Options */

#define MEM_MBEN	0x24
#define MEM_TYPE	0x28
#define MEM_B1SA	0x3c
#define MEM_B1EA	0x5c
#define MEM_B2SA	0x40
#define MEM_B2EA	0x60

unsigned long
get_mem_size(void)
{
	int loop;
	unsigned long mem_size = 0;
	unsigned long mem_mben;
	unsigned long mem_type;
	unsigned long mem_start;
	unsigned long mem_end;
	volatile int *mem_addr = (int *)0xff500008;
	volatile int *mem_data = (int *)0xff50000c;

	/* Get the size of memory from the memory controller. */
	*mem_addr = MEM_MBEN;
	asm("sync");
	mem_mben = *mem_data;
	asm("sync");
	for(loop = 0; loop < 1000; loop++);

	*mem_addr = MEM_TYPE;
	asm("sync");
	mem_type = *mem_data;
	asm("sync");
	for(loop = 0; loop < 1000; loop++);

	*mem_addr = MEM_TYPE;
	/* Confirm bank 1 has DRAM memory */
	if ((mem_mben & 0x40000000) &&
				((mem_type & 0x30000000) == 0x10000000)) {
		*mem_addr = MEM_B1SA;
		asm("sync");
		mem_start = *mem_data;
		asm("sync");
		for(loop = 0; loop < 1000; loop++);

		*mem_addr = MEM_B1EA;
		asm("sync");
		mem_end = *mem_data;
		asm("sync");
		for(loop = 0; loop < 1000; loop++);

		mem_size = mem_end - mem_start + 0x100000;
	}

	/* Confirm bank 2 has DRAM memory */
	if ((mem_mben & 0x20000000) &&
				((mem_type & 0xc000000) == 0x4000000)) {
		*mem_addr = MEM_B2SA;
		asm("sync");
		mem_start = *mem_data;
		asm("sync");
		for(loop = 0; loop < 1000; loop++);

		*mem_addr = MEM_B2EA;
		asm("sync");
		mem_end = *mem_data;
		asm("sync");
		for(loop = 0; loop < 1000; loop++);

		mem_size += mem_end - mem_start + 0x100000;
	}
	return mem_size;
}

unsigned long
load_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		void *ign1, void *ign2)
{
	int csr0;
	int csr_id;
	int pci_devfn;
	int found_multi = 0;
	unsigned short vendor;
	unsigned short device;
	unsigned short command;
	unsigned char header_type;
	unsigned int bar0;
	volatile int *pif_addr = (int *)0xff500000;
	volatile int *pif_data = (int *)0xff500004;

	/*
	 * Gah, these firmware guys need to learn that hardware
	 * byte swapping is evil! Disable all hardware byte
	 * swapping so it doesn't hurt anyone.
	 */
	*pif_addr = PLBMIFOPT;
	asm("sync");
	*pif_data = 0x00000000;
	asm("sync");

	/* Search out and turn off the PcNet ethernet boot device. */
	for (pci_devfn = 1; pci_devfn < 0xff; pci_devfn++) {
		if (PCI_FUNC(pci_devfn) && !found_multi)
			continue;

		cpc700_pcibios_read_config_byte(0, pci_devfn,
				PCI_HEADER_TYPE, &header_type);

		if (!PCI_FUNC(pci_devfn))
			found_multi = header_type & 0x80;

		cpc700_pcibios_read_config_word(0, pci_devfn, PCI_VENDOR_ID,
				&vendor);

		if (vendor != 0xffff) {
			cpc700_pcibios_read_config_word(0, pci_devfn,
						PCI_DEVICE_ID, &device);

			/* If this PCI device is the Lance PCNet board then turn it off */
			if ((vendor == PCI_VENDOR_ID_AMD) &&
					(device == PCI_DEVICE_ID_AMD_LANCE)) {

				/* Turn on I/O Space on the board. */
				cpc700_pcibios_read_config_word(0, pci_devfn,
						PCI_COMMAND, &command);
				command |= 0x1;
				cpc700_pcibios_write_config_word(0, pci_devfn,
						PCI_COMMAND, command);

				/* Get the I/O space address */
				cpc700_pcibios_read_config_dword(0, pci_devfn,
						PCI_BASE_ADDRESS_0, &bar0);
				bar0 &= 0xfffffffe;

				/* Reset the PCNet Board */
				inl (bar0+PCNET32_DWIO_RESET);
				inw (bar0+PCNET32_WIO_RESET);

				/* First do a work oriented read of csr0.  If the value is
				 * 4 then this is the correct mode to access the board.
				 * If not try a double word ortiented read.
				 */
				outw(0, bar0 + PCNET32_WIO_RAP);
				csr0 = inw(bar0 + PCNET32_WIO_RDP);

				if (csr0 == 4) {
					/* Check the Chip id register */
					outw(88, bar0 + PCNET32_WIO_RAP);
					csr_id = inw(bar0 + PCNET32_WIO_RDP);

					if (csr_id) {
						/* This is the valid mode - set the stop bit */
						outw(0, bar0 + PCNET32_WIO_RAP);
						outw(csr0, bar0 + PCNET32_WIO_RDP);
					}
				} else {
					outl(0, bar0 + PCNET32_DWIO_RAP);
					csr0 = inl(bar0 + PCNET32_DWIO_RDP);
					if (csr0 == 4) {
						/* Check the Chip id register */
						outl(88, bar0 + PCNET32_WIO_RAP);
						csr_id = inl(bar0 + PCNET32_WIO_RDP);

						if (csr_id) {
							/* This is the valid mode  - set the stop bit*/
							outl(0, bar0 + PCNET32_WIO_RAP);
							outl(csr0, bar0 + PCNET32_WIO_RDP);
						}
					}
				}
			}
		}
	}

	return decompress_kernel(load_addr, num_words, cksum);
}
