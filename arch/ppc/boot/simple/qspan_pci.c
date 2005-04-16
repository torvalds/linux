/*
 * LinuxPPC arch/ppc/kernel/qspan_pci.c   Dan Malek (dmalek@jlc.net)
 *
 * QSpan Motorola bus to PCI bridge.  The config address register
 * is located 0x500 from the base of the bridge control/status registers.
 * The data register is located at 0x504.
 * This is a two step operation.  First, the address register is written,
 * then the data register is read/written as required.
 * I don't know what to do about interrupts (yet).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/mpc8xx.h>

/*
 * When reading the configuration space, if something does not respond
 * the bus times out and we get a machine check interrupt.  So, the
 * good ol' exception tables come to mind to trap it and return some
 * value.
 *
 * On an error we just return a -1, since that is what the caller wants
 * returned if nothing is present.  I copied this from __get_user_asm,
 * with the only difference of returning -1 instead of EFAULT.
 * There is an associated hack in the machine check trap code.
 *
 * The QSPAN is also a big endian device, that is it makes the PCI
 * look big endian to us.  This presents a problem for the Linux PCI
 * functions, which assume little endian.  For example, we see the
 * first 32-bit word like this:
 *	------------------------
 *	| Device ID | Vendor ID |
 *	------------------------
 * If we read/write as a double word, that's OK.  But in our world,
 * when read as a word, device ID is at location 0, not location 2 as
 * the little endian PCI would believe.  We have to switch bits in
 * the PCI addresses given to us to get the data to/from the correct
 * byte lanes.
 *
 * The QSPAN only supports 4 bits of "slot" in the dev_fn instead of 5.
 * It always forces the MS bit to zero.  Therefore, dev_fn values
 * greater than 128 are returned as "no device found" errors.
 *
 * The QSPAN can only perform long word (32-bit) configuration cycles.
 * The "offset" must have the two LS bits set to zero.  Read operations
 * require we read the entire word and then sort out what should be
 * returned.  Write operations other than long word require that we
 * read the long word, update the proper word or byte, then write the
 * entire long word back.
 *
 * PCI Bridge hack.  We assume (correctly) that bus 0 is the primary
 * PCI bus from the QSPAN.  If we are called with a bus number other
 * than zero, we create a Type 1 configuration access that a downstream
 * PCI bridge will interpret.
 */

#define __get_pci_config(x, addr, op)		\
	__asm__ __volatile__(				\
		"1:	"op" %0,0(%1)\n"		\
		"	eieio\n"			\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %0,-1\n"			\
		"	b 2b\n"				\
		".section __ex_table,\"a\"\n"		\
		"	.align 2\n"			\
		"	.long 1b,3b\n"			\
		".text"					\
		: "=r"(x) : "r"(addr))

#define QS_CONFIG_ADDR	((volatile uint *)(PCI_CSR_ADDR + 0x500))
#define QS_CONFIG_DATA	((volatile uint *)(PCI_CSR_ADDR + 0x504))

#define mk_config_addr(bus, dev, offset) \
	(((bus)<<16) | ((dev)<<8) | (offset & 0xfc))

#define mk_config_type1(bus, dev, offset) \
	mk_config_addr(bus, dev, offset) | 1;

/* Initialize the QSpan device registers after power up.
*/
void
qspan_init(void)
{
	uint	*qptr;



	qptr = (uint *)PCI_CSR_ADDR;

	/* PCI Configuration/status.  Upper bits written to clear
	 * pending interrupt or status.  Lower bits enable QSPAN as
	 * PCI master, enable memory and I/O cycles, and enable PCI
	 * parity error checking.
	 * IMPORTANT:  The last two bits of this word enable PCI
	 * master cycles into the QBus.  The QSpan is broken and can't
	 * meet the timing specs of the PQ bus for this to work.  Therefore,
	 * if you don't have external bus arbitration, you can't use
	 * this function.
	 */
#ifdef EXTERNAL_PQ_ARB
	qptr[1] = 0xf9000147;
#else
	qptr[1] = 0xf9000144;
#endif

	/* PCI Misc configuration.  Set PCI latency timer resolution
	 * of 8 cycles, set cache size to 4 x 32.
	 */
	qptr[3] = 0;

	/* Set up PCI Target address mapping.  Enable, Posted writes,
	 * 2Gbyte space (processor memory controller determines actual size).
	 */
	qptr[64] = 0x8f000080;

	/* Map processor 0x80000000 to PCI 0x00000000.
	 * Processor address bit 1 determines I/O type access (0x80000000)
	 * or memory type access (0xc0000000).
	 */
	qptr[65] = 0x80000000;

	/* Enable error logging and clear any pending error status.
	*/
	qptr[80] = 0x90000000;

	qptr[512] = 0x000c0003;

	/* Set up Qbus slave image.
	*/
	qptr[960] = 0x01000000;
	qptr[961] = 0x000000d1;
	qptr[964] = 0x00000000;
	qptr[965] = 0x000000d1;

}

/* Functions to support PCI bios-like features to read/write configuration
 * space.  If the function fails for any reason, a -1 (0xffffffff) value
 * must be returned.
 */
#define DEVICE_NOT_FOUND	(-1)
#define SUCCESSFUL		0

int qs_pci_read_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char *val)
{
	uint	temp;
	u_char	*cp;

	if ((bus > 7) || (dev_fn > 127)) {
		*val = 0xff;
		return DEVICE_NOT_FOUND;
	}

	if (bus == 0)
		*QS_CONFIG_ADDR = mk_config_addr(bus, dev_fn, offset);
	else
		*QS_CONFIG_ADDR = mk_config_type1(bus, dev_fn, offset);
	__get_pci_config(temp, QS_CONFIG_DATA, "lwz");

	offset ^= 0x03;
	cp = ((u_char *)&temp) + (offset & 0x03);
	*val = *cp;
	return SUCCESSFUL;
}

int qs_pci_read_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short *val)
{
	uint	temp;
	ushort	*sp;

	if ((bus > 7) || (dev_fn > 127)) {
		*val = 0xffff;
		return DEVICE_NOT_FOUND;
	}

	if (bus == 0)
		*QS_CONFIG_ADDR = mk_config_addr(bus, dev_fn, offset);
	else
		*QS_CONFIG_ADDR = mk_config_type1(bus, dev_fn, offset);
	__get_pci_config(temp, QS_CONFIG_DATA, "lwz");
	offset ^= 0x02;

	sp = ((ushort *)&temp) + ((offset >> 1) & 1);
	*val = *sp;
	return SUCCESSFUL;
}

int qs_pci_read_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int *val)
{
	if ((bus > 7) || (dev_fn > 127)) {
		*val = 0xffffffff;
		return DEVICE_NOT_FOUND;
	}
	if (bus == 0)
		*QS_CONFIG_ADDR = mk_config_addr(bus, dev_fn, offset);
	else
		*QS_CONFIG_ADDR = mk_config_type1(bus, dev_fn, offset);
	__get_pci_config(*val, QS_CONFIG_DATA, "lwz");
	return SUCCESSFUL;
}

int qs_pci_write_config_byte(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned char val)
{
	uint	temp;
	u_char	*cp;

	if ((bus > 7) || (dev_fn > 127))
		return DEVICE_NOT_FOUND;

	qs_pci_read_config_dword(bus, dev_fn, offset, &temp);

	offset ^= 0x03;
	cp = ((u_char *)&temp) + (offset & 0x03);
	*cp = val;

	if (bus == 0)
		*QS_CONFIG_ADDR = mk_config_addr(bus, dev_fn, offset);
	else
		*QS_CONFIG_ADDR = mk_config_type1(bus, dev_fn, offset);
	*QS_CONFIG_DATA = temp;

	return SUCCESSFUL;
}

int qs_pci_write_config_word(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned short val)
{
	uint	temp;
	ushort	*sp;

	if ((bus > 7) || (dev_fn > 127))
		return DEVICE_NOT_FOUND;

	qs_pci_read_config_dword(bus, dev_fn, offset, &temp);

	offset ^= 0x02;
	sp = ((ushort *)&temp) + ((offset >> 1) & 1);
	*sp = val;

	if (bus == 0)
		*QS_CONFIG_ADDR = mk_config_addr(bus, dev_fn, offset);
	else
		*QS_CONFIG_ADDR = mk_config_type1(bus, dev_fn, offset);
	*QS_CONFIG_DATA = temp;

	return SUCCESSFUL;
}

int qs_pci_write_config_dword(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned int val)
{
	if ((bus > 7) || (dev_fn > 127))
		return DEVICE_NOT_FOUND;

	if (bus == 0)
		*QS_CONFIG_ADDR = mk_config_addr(bus, dev_fn, offset);
	else
		*QS_CONFIG_ADDR = mk_config_type1(bus, dev_fn, offset);
	*(unsigned int *)QS_CONFIG_DATA = val;

	return SUCCESSFUL;
}

