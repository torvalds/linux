/* Stand alone funtions for QSpan Tundra support.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/mpc8xx.h>

extern void puthex(unsigned long val);
extern void puts(const char *);

/* To map PCI devices, you first write 0xffffffff into the device
 * base address registers.  When the register is read back, the
 * number of most significant '1' bits describes the amount of address
 * space needed for mapping.  If the most significant bit is not set,
 * either the device does not use that address register, or it has
 * a fixed address that we can't change.  After the address is assigned,
 * the command register has to be written to enable the card.
 */
typedef struct {
	u_char	pci_bus;
	u_char	pci_devfn;
	ushort	pci_command;
	uint	pci_addrs[6];
} pci_map_t;

/* We should probably dynamically allocate these structures.
*/
#define MAX_PCI_DEVS	32
int	pci_dev_cnt;
pci_map_t	pci_map[MAX_PCI_DEVS];

void pci_conf_write(int bus, int device, int func, int reg, uint writeval);
void pci_conf_read(int bus, int device, int func, int reg, void *readval);
void probe_addresses(int bus, int devfn);
void map_pci_addrs(void);

extern int
qs_pci_read_config_byte(unsigned char bus, unsigned char dev_fn,
			unsigned char offset, unsigned char *val);
extern int
qs_pci_read_config_word(unsigned char bus, unsigned char dev_fn,
			unsigned char offset, unsigned short *val);
extern int
qs_pci_read_config_dword(unsigned char bus, unsigned char dev_fn,
			 unsigned char offset, unsigned int *val);
extern int
qs_pci_write_config_byte(unsigned char bus, unsigned char dev_fn,
			 unsigned char offset, unsigned char val);
extern int
qs_pci_write_config_word(unsigned char bus, unsigned char dev_fn,
			 unsigned char offset, unsigned short val);
extern int
qs_pci_write_config_dword(unsigned char bus, unsigned char dev_fn,
			  unsigned char offset, unsigned int val);


/* This is a really stripped version of PCI bus scan.  All we are
 * looking for are devices that exist.
 */
void
pci_scanner(int addr_probe)
{
	unsigned int devfn, l, class, bus_number;
	unsigned char hdr_type, is_multi;

	is_multi = 0;
	bus_number = 0;
	for (devfn = 0; devfn < 0xff; ++devfn) {
		/* The device numbers are comprised of upper 5 bits of
		 * device number and lower 3 bits of multi-function number.
		 */
		if ((devfn & 7) && !is_multi) {
			/* Don't scan multifunction addresses if this is
			 * not a multifunction device.
			 */
			continue;
		}

		/* Read the header to determine card type.
		*/
		qs_pci_read_config_byte(bus_number, devfn, PCI_HEADER_TYPE,
								&hdr_type);

		/* If this is a base device number, check the header to
		 * determine if it is mulifunction.
		 */
		if ((devfn & 7) == 0)
			is_multi = hdr_type & 0x80;

		/* Check to see if the board is really in the slot.
		*/
		qs_pci_read_config_dword(bus_number, devfn, PCI_VENDOR_ID, &l);
		/* some broken boards return 0 if a slot is empty: */
		if (l == 0xffffffff || l == 0x00000000 || l == 0x0000ffff ||
							l == 0xffff0000) {
			/* Nothing there.
			*/
			is_multi = 0;
			continue;
		}

		/* If we are not performing an address probe,
		 * just simply print out some information.
		 */
		if (!addr_probe) {
			qs_pci_read_config_dword(bus_number, devfn,
						PCI_CLASS_REVISION, &class);

			class >>= 8;	    /* upper 3 bytes */

#if 0
			printf("Found (%3d:%d): vendor 0x%04x, device 0x%04x, class 0x%06x\n",
				(devfn >> 3), (devfn & 7),
				(l & 0xffff),  (l >> 16) & 0xffff, class);
#else
			puts("Found ("); puthex(devfn >> 3);
			puts(":"); puthex(devfn & 7);
			puts("): vendor "); puthex(l & 0xffff);
			puts(", device "); puthex((l >> 16) & 0xffff);
			puts(", class "); puthex(class); puts("\n");
#endif
		}
		else {
			/* If this is a "normal" device, build address list.
			*/
			if ((hdr_type & 0x7f) == PCI_HEADER_TYPE_NORMAL)
				probe_addresses(bus_number, devfn);
		}
	}

	/* Now map the boards.
	*/
	if (addr_probe)
		map_pci_addrs();
}

/* Probe addresses for the specified device.  This is a destructive
 * operation because it writes the registers.
 */
void
probe_addresses(bus, devfn)
{
	int	i;
	uint	pciaddr;
	ushort	pcicmd;
	pci_map_t	*pm;

	if (pci_dev_cnt >= MAX_PCI_DEVS) {
		puts("Too many PCI devices\n");
		return;
	}

	pm = &pci_map[pci_dev_cnt++];

	pm->pci_bus = bus;
	pm->pci_devfn = devfn;

	for (i=0; i<6; i++) {
		qs_pci_write_config_dword(bus, devfn, PCI_BASE_ADDRESS_0 + (i * 4), -1);
		qs_pci_read_config_dword(bus, devfn, PCI_BASE_ADDRESS_0 + (i * 4),
								&pciaddr);
		pm->pci_addrs[i] = pciaddr;
		qs_pci_read_config_word(bus, devfn, PCI_COMMAND, &pcicmd);
		pm->pci_command = pcicmd;
	}
}

/* Map the cards into the PCI space.  The PCI has separate memory
 * and I/O spaces.  In addition, some memory devices require mapping
 * below 1M.  The least significant 4 bits of the address register
 * provide information.  If this is an I/O device, only the LS bit
 * is used to indicate that, so I/O devices can be mapped to a two byte
 * boundard.  Memory addresses can be mapped to a 32 byte boundary.
 * The QSpan implementations usually have a 1Gbyte space for each
 * memory and I/O spaces.
 *
 * This isn't a terribly fancy algorithm.  I just map the spaces from
 * the top starting with the largest address space.  When finished,
 * the registers are written and the card enabled.
 *
 * While the Tundra can map a large address space on most boards, we
 * need to be careful because it may overlap other devices (like IMMR).
 */
#define MEMORY_SPACE_SIZE	0x20000000
#define IO_SPACE_SIZE		0x20000000

void
map_pci_addrs()
{
	uint	pci_mem_top, pci_mem_low;
	uint	pci_io_top;
	uint	addr_mask, reg_addr, space;
	int	i, j;
	pci_map_t *pm;

	pci_mem_top = MEMORY_SPACE_SIZE;
	pci_io_top = IO_SPACE_SIZE;
	pci_mem_low = (1 * 1024 * 1024);	/* Below one meg addresses */

	/* We can't map anything more than the maximum space, but test
	 * for it anyway to catch devices out of range.
	 */
	addr_mask = 0x80000000;

	do {
		space = (~addr_mask) + 1;	/* Size of the space */
		for (i=0; i<pci_dev_cnt; i++) {
			pm = &pci_map[i];
			for (j=0; j<6; j++) {
				/* If the MS bit is not set, this has either
				 * already been mapped, or is not used.
				 */
				reg_addr = pm->pci_addrs[j];
				if ((reg_addr & 0x80000000) == 0)
					continue;
				if (reg_addr & PCI_BASE_ADDRESS_SPACE_IO) {
					if ((reg_addr & PCI_BASE_ADDRESS_IO_MASK) != addr_mask)
						continue;
					if (pci_io_top < space) {
						puts("Out of PCI I/O space\n");
					}
					else {
						pci_io_top -= space;
						pm->pci_addrs[j] = pci_io_top;
						pm->pci_command |= PCI_COMMAND_IO;
					}
				}
				else {
					if ((reg_addr & PCI_BASE_ADDRESS_MEM_MASK) != addr_mask)
						continue;

					/* Memory space.  Test if below 1M.
					*/
					if (reg_addr & PCI_BASE_ADDRESS_MEM_TYPE_1M) {
						if (pci_mem_low < space) {
							puts("Out of PCI 1M space\n");
						}
						else {
							pci_mem_low -= space;
							pm->pci_addrs[j] = pci_mem_low;
						}
					}
					else {
						if (pci_mem_top < space) {
							puts("Out of PCI Mem space\n");
						}
						else {
							pci_mem_top -= space;
							pm->pci_addrs[j] = pci_mem_top;
						}
					}
					pm->pci_command |= PCI_COMMAND_MEMORY;
				}
			}
		}
		addr_mask >>= 1;
		addr_mask |= 0x80000000;
	} while (addr_mask != 0xfffffffe);
	
	/* Now, run the list one more time and map everything.
	*/
	for (i=0; i<pci_dev_cnt; i++) {
		pm = &pci_map[i];
		for (j=0; j<6; j++) {
			qs_pci_write_config_dword(pm->pci_bus, pm->pci_devfn,
				PCI_BASE_ADDRESS_0 + (j * 4), pm->pci_addrs[j]);
		}

		/* Enable memory or address mapping.
		*/
		qs_pci_write_config_word(pm->pci_bus, pm->pci_devfn, PCI_COMMAND,
			pm->pci_command);
	}
}

