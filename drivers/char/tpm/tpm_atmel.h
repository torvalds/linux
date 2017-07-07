/*
 * Copyright (C) 2005 IBM Corporation
 *
 * Authors:
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * These difference are required on power because the device must be
 * discovered through the device tree and iomap must be used to get
 * around the need for holes in the io_page_mask.  This does not happen
 * automatically because the tpm is not a normal pci device and lives
 * under the root node.
 *
 */

struct tpm_atmel_priv {
	int region_size;
	int have_region;
	unsigned long base;
	void __iomem *iobase;
};

#ifdef CONFIG_PPC64

#include <asm/prom.h>

#define atmel_getb(priv, offset) readb(priv->iobase + offset)
#define atmel_putb(val, priv, offset) writeb(val, priv->iobase + offset)
#define atmel_request_region request_mem_region
#define atmel_release_region release_mem_region

static inline void atmel_put_base_addr(void __iomem *iobase)
{
	iounmap(iobase);
}

static void __iomem * atmel_get_base_addr(unsigned long *base, int *region_size)
{
	struct device_node *dn;
	unsigned long address, size;
	const unsigned int *reg;
	int reglen;
	int naddrc;
	int nsizec;

	dn = of_find_node_by_name(NULL, "tpm");

	if (!dn)
		return NULL;

	if (!of_device_is_compatible(dn, "AT97SC3201")) {
		of_node_put(dn);
		return NULL;
	}

	reg = of_get_property(dn, "reg", &reglen);
	naddrc = of_n_addr_cells(dn);
	nsizec = of_n_size_cells(dn);

	of_node_put(dn);


	if (naddrc == 2)
		address = ((unsigned long) reg[0] << 32) | reg[1];
	else
		address = reg[0];

	if (nsizec == 2)
		size =
		    ((unsigned long) reg[naddrc] << 32) | reg[naddrc + 1];
	else
		size = reg[naddrc];

	*base = address;
	*region_size = size;
	return ioremap(*base, *region_size);
}
#else
#define atmel_getb(chip, offset) inb(atmel_get_priv(chip)->base + offset)
#define atmel_putb(val, chip, offset) \
	outb(val, atmel_get_priv(chip)->base + offset)
#define atmel_request_region request_region
#define atmel_release_region release_region
/* Atmel definitions */
enum tpm_atmel_addr {
	TPM_ATMEL_BASE_ADDR_LO = 0x08,
	TPM_ATMEL_BASE_ADDR_HI = 0x09
};

static inline int tpm_read_index(int base, int index)
{
	outb(index, base);
	return inb(base+1) & 0xFF;
}

/* Verify this is a 1.1 Atmel TPM */
static int atmel_verify_tpm11(void)
{

	/* verify that it is an Atmel part */
	if (tpm_read_index(TPM_ADDR, 4) != 'A' ||
	    tpm_read_index(TPM_ADDR, 5) != 'T' ||
	    tpm_read_index(TPM_ADDR, 6) != 'M' ||
	    tpm_read_index(TPM_ADDR, 7) != 'L')
		return 1;

	/* query chip for its version number */
	if (tpm_read_index(TPM_ADDR, 0x00) != 1 ||
	    tpm_read_index(TPM_ADDR, 0x01) != 1)
		return 1;

	/* This is an atmel supported part */
	return 0;
}

static inline void atmel_put_base_addr(void __iomem *iobase)
{
}

/* Determine where to talk to device */
static void __iomem * atmel_get_base_addr(unsigned long *base, int *region_size)
{
	int lo, hi;

	if (atmel_verify_tpm11() != 0)
		return NULL;

	lo = tpm_read_index(TPM_ADDR, TPM_ATMEL_BASE_ADDR_LO);
	hi = tpm_read_index(TPM_ADDR, TPM_ATMEL_BASE_ADDR_HI);

	*base = (hi << 8) | lo;
	*region_size = 2;

	return ioport_map(*base, *region_size);
}
#endif
