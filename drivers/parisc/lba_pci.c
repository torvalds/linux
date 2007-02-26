/*
**
**  PCI Lower Bus Adapter (LBA) manager
**
**	(c) Copyright 1999,2000 Grant Grundler
**	(c) Copyright 1999,2000 Hewlett-Packard Company
**
**	This program is free software; you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**      the Free Software Foundation; either version 2 of the License, or
**      (at your option) any later version.
**
**
** This module primarily provides access to PCI bus (config/IOport
** spaces) on platforms with an SBA/LBA chipset. A/B/C/J/L/N-class
** with 4 digit model numbers - eg C3000 (and A400...sigh).
**
** LBA driver isn't as simple as the Dino driver because:
**   (a) this chip has substantial bug fixes between revisions
**       (Only one Dino bug has a software workaround :^(  )
**   (b) has more options which we don't (yet) support (DMA hints, OLARD)
**   (c) IRQ support lives in the I/O SAPIC driver (not with PCI driver)
**   (d) play nicely with both PAT and "Legacy" PA-RISC firmware (PDC).
**       (dino only deals with "Legacy" PDC)
**
** LBA driver passes the I/O SAPIC HPA to the I/O SAPIC driver.
** (I/O SAPIC is integratd in the LBA chip).
**
** FIXME: Add support to SBA and LBA drivers for DMA hint sets
** FIXME: Add support for PCI card hot-plug (OLARD).
*/

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/init.h>		/* for __init and __devinit */
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>

#include <asm/byteorder.h>
#include <asm/pdc.h>
#include <asm/pdcpat.h>
#include <asm/page.h>
#include <asm/system.h>

#include <asm/ropes.h>
#include <asm/hardware.h>	/* for register_parisc_driver() stuff */
#include <asm/parisc-device.h>
#include <asm/io.h>		/* read/write stuff */

#undef DEBUG_LBA	/* general stuff */
#undef DEBUG_LBA_PORT	/* debug I/O Port access */
#undef DEBUG_LBA_CFG	/* debug Config Space Access (ie PCI Bus walk) */
#undef DEBUG_LBA_PAT	/* debug PCI Resource Mgt code - PDC PAT only */

#undef FBB_SUPPORT	/* Fast Back-Back xfers - NOT READY YET */


#ifdef DEBUG_LBA
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

#ifdef DEBUG_LBA_PORT
#define DBG_PORT(x...)	printk(x)
#else
#define DBG_PORT(x...)
#endif

#ifdef DEBUG_LBA_CFG
#define DBG_CFG(x...)	printk(x)
#else
#define DBG_CFG(x...)
#endif

#ifdef DEBUG_LBA_PAT
#define DBG_PAT(x...)	printk(x)
#else
#define DBG_PAT(x...)
#endif


/*
** Config accessor functions only pass in the 8-bit bus number and not
** the 8-bit "PCI Segment" number. Each LBA will be assigned a PCI bus
** number based on what firmware wrote into the scratch register.
**
** The "secondary" bus number is set to this before calling
** pci_register_ops(). If any PPB's are present, the scan will
** discover them and update the "secondary" and "subordinate"
** fields in the pci_bus structure.
**
** Changes in the configuration *may* result in a different
** bus number for each LBA depending on what firmware does.
*/

#define MODULE_NAME "LBA"

/* non-postable I/O port space, densely packed */
#define LBA_PORT_BASE	(PCI_F_EXTEND | 0xfee00000UL)
static void __iomem *astro_iop_base __read_mostly;

static u32 lba_t32;

/* lba flags */
#define LBA_FLAG_SKIP_PROBE	0x10

#define LBA_SKIP_PROBE(d) ((d)->flags & LBA_FLAG_SKIP_PROBE)


/* Looks nice and keeps the compiler happy */
#define LBA_DEV(d) ((struct lba_device *) (d))


/*
** Only allow 8 subsidiary busses per LBA
** Problem is the PCI bus numbering is globally shared.
*/
#define LBA_MAX_NUM_BUSES 8

/************************************
 * LBA register read and write support
 *
 * BE WARNED: register writes are posted.
 *  (ie follow writes which must reach HW with a read)
 */
#define READ_U8(addr)  __raw_readb(addr)
#define READ_U16(addr) __raw_readw(addr)
#define READ_U32(addr) __raw_readl(addr)
#define WRITE_U8(value, addr)  __raw_writeb(value, addr)
#define WRITE_U16(value, addr) __raw_writew(value, addr)
#define WRITE_U32(value, addr) __raw_writel(value, addr)

#define READ_REG8(addr)  readb(addr)
#define READ_REG16(addr) readw(addr)
#define READ_REG32(addr) readl(addr)
#define READ_REG64(addr) readq(addr)
#define WRITE_REG8(value, addr)  writeb(value, addr)
#define WRITE_REG16(value, addr) writew(value, addr)
#define WRITE_REG32(value, addr) writel(value, addr)


#define LBA_CFG_TOK(bus,dfn) ((u32) ((bus)<<16 | (dfn)<<8))
#define LBA_CFG_BUS(tok)  ((u8) ((tok)>>16))
#define LBA_CFG_DEV(tok)  ((u8) ((tok)>>11) & 0x1f)
#define LBA_CFG_FUNC(tok) ((u8) ((tok)>>8 ) & 0x7)


/*
** Extract LBA (Rope) number from HPA
** REVISIT: 16 ropes for Stretch/Ike?
*/
#define ROPES_PER_IOC	8
#define LBA_NUM(x)    ((((unsigned long) x) >> 13) & (ROPES_PER_IOC-1))


static void
lba_dump_res(struct resource *r, int d)
{
	int i;

	if (NULL == r)
		return;

	printk(KERN_DEBUG "(%p)", r->parent);
	for (i = d; i ; --i) printk(" ");
	printk(KERN_DEBUG "%p [%lx,%lx]/%lx\n", r,
		(long)r->start, (long)r->end, r->flags);
	lba_dump_res(r->child, d+2);
	lba_dump_res(r->sibling, d);
}


/*
** LBA rev 2.0, 2.1, 2.2, and 3.0 bus walks require a complex
** workaround for cfg cycles:
**	-- preserve  LBA state
**	-- prevent any DMA from occurring
**	-- turn on smart mode
**	-- probe with config writes before doing config reads
**	-- check ERROR_STATUS
**	-- clear ERROR_STATUS
**	-- restore LBA state
**
** The workaround is only used for device discovery.
*/

static int lba_device_present(u8 bus, u8 dfn, struct lba_device *d)
{
	u8 first_bus = d->hba.hba_bus->secondary;
	u8 last_sub_bus = d->hba.hba_bus->subordinate;

	if ((bus < first_bus) ||
	    (bus > last_sub_bus) ||
	    ((bus - first_bus) >= LBA_MAX_NUM_BUSES)) {
		return 0;
	}

	return 1;
}



#define LBA_CFG_SETUP(d, tok) {				\
    /* Save contents of error config register.  */			\
    error_config = READ_REG32(d->hba.base_addr + LBA_ERROR_CONFIG);		\
\
    /* Save contents of status control register.  */			\
    status_control = READ_REG32(d->hba.base_addr + LBA_STAT_CTL);		\
\
    /* For LBA rev 2.0, 2.1, 2.2, and 3.0, we must disable DMA		\
    ** arbitration for full bus walks.					\
    */									\
	/* Save contents of arb mask register. */			\
	arb_mask = READ_REG32(d->hba.base_addr + LBA_ARB_MASK);		\
\
	/*								\
	 * Turn off all device arbitration bits (i.e. everything	\
	 * except arbitration enable bit).				\
	 */								\
	WRITE_REG32(0x1, d->hba.base_addr + LBA_ARB_MASK);		\
\
    /*									\
     * Set the smart mode bit so that master aborts don't cause		\
     * LBA to go into PCI fatal mode (required).			\
     */									\
    WRITE_REG32(error_config | LBA_SMART_MODE, d->hba.base_addr + LBA_ERROR_CONFIG);	\
}


#define LBA_CFG_PROBE(d, tok) {				\
    /*									\
     * Setup Vendor ID write and read back the address register		\
     * to make sure that LBA is the bus master.				\
     */									\
    WRITE_REG32(tok | PCI_VENDOR_ID, (d)->hba.base_addr + LBA_PCI_CFG_ADDR);\
    /*									\
     * Read address register to ensure that LBA is the bus master,	\
     * which implies that DMA traffic has stopped when DMA arb is off.	\
     */									\
    lba_t32 = READ_REG32((d)->hba.base_addr + LBA_PCI_CFG_ADDR);	\
    /*									\
     * Generate a cfg write cycle (will have no affect on		\
     * Vendor ID register since read-only).				\
     */									\
    WRITE_REG32(~0, (d)->hba.base_addr + LBA_PCI_CFG_DATA);		\
    /*									\
     * Make sure write has completed before proceeding further,		\
     * i.e. before setting clear enable.				\
     */									\
    lba_t32 = READ_REG32((d)->hba.base_addr + LBA_PCI_CFG_ADDR);	\
}


/*
 * HPREVISIT:
 *   -- Can't tell if config cycle got the error.
 *
 *		OV bit is broken until rev 4.0, so can't use OV bit and
 *		LBA_ERROR_LOG_ADDR to tell if error belongs to config cycle.
 *
 *		As of rev 4.0, no longer need the error check.
 *
 *   -- Even if we could tell, we still want to return -1
 *	for **ANY** error (not just master abort).
 *
 *   -- Only clear non-fatal errors (we don't want to bring
 *	LBA out of pci-fatal mode).
 *
 *		Actually, there is still a race in which
 *		we could be clearing a fatal error.  We will
 *		live with this during our initial bus walk
 *		until rev 4.0 (no driver activity during
 *		initial bus walk).  The initial bus walk
 *		has race conditions concerning the use of
 *		smart mode as well.
 */

#define LBA_MASTER_ABORT_ERROR 0xc
#define LBA_FATAL_ERROR 0x10

#define LBA_CFG_MASTER_ABORT_CHECK(d, base, tok, error) {		\
    u32 error_status = 0;						\
    /*									\
     * Set clear enable (CE) bit. Unset by HW when new			\
     * errors are logged -- LBA HW ERS section 14.3.3).		\
     */									\
    WRITE_REG32(status_control | CLEAR_ERRLOG_ENABLE, base + LBA_STAT_CTL); \
    error_status = READ_REG32(base + LBA_ERROR_STATUS);		\
    if ((error_status & 0x1f) != 0) {					\
	/*								\
	 * Fail the config read request.				\
	 */								\
	error = 1;							\
	if ((error_status & LBA_FATAL_ERROR) == 0) {			\
	    /*								\
	     * Clear error status (if fatal bit not set) by setting	\
	     * clear error log bit (CL).				\
	     */								\
	    WRITE_REG32(status_control | CLEAR_ERRLOG, base + LBA_STAT_CTL); \
	}								\
    }									\
}

#define LBA_CFG_TR4_ADDR_SETUP(d, addr)					\
	WRITE_REG32(((addr) & ~3), (d)->hba.base_addr + LBA_PCI_CFG_ADDR);

#define LBA_CFG_ADDR_SETUP(d, addr) {					\
    WRITE_REG32(((addr) & ~3), (d)->hba.base_addr + LBA_PCI_CFG_ADDR);	\
    /*									\
     * Read address register to ensure that LBA is the bus master,	\
     * which implies that DMA traffic has stopped when DMA arb is off.	\
     */									\
    lba_t32 = READ_REG32((d)->hba.base_addr + LBA_PCI_CFG_ADDR);	\
}


#define LBA_CFG_RESTORE(d, base) {					\
    /*									\
     * Restore status control register (turn off clear enable).		\
     */									\
    WRITE_REG32(status_control, base + LBA_STAT_CTL);			\
    /*									\
     * Restore error config register (turn off smart mode).		\
     */									\
    WRITE_REG32(error_config, base + LBA_ERROR_CONFIG);			\
	/*								\
	 * Restore arb mask register (reenables DMA arbitration).	\
	 */								\
	WRITE_REG32(arb_mask, base + LBA_ARB_MASK);			\
}



static unsigned int
lba_rd_cfg(struct lba_device *d, u32 tok, u8 reg, u32 size)
{
	u32 data = ~0U;
	int error = 0;
	u32 arb_mask = 0;	/* used by LBA_CFG_SETUP/RESTORE */
	u32 error_config = 0;	/* used by LBA_CFG_SETUP/RESTORE */
	u32 status_control = 0;	/* used by LBA_CFG_SETUP/RESTORE */

	LBA_CFG_SETUP(d, tok);
	LBA_CFG_PROBE(d, tok);
	LBA_CFG_MASTER_ABORT_CHECK(d, d->hba.base_addr, tok, error);
	if (!error) {
		void __iomem *data_reg = d->hba.base_addr + LBA_PCI_CFG_DATA;

		LBA_CFG_ADDR_SETUP(d, tok | reg);
		switch (size) {
		case 1: data = (u32) READ_REG8(data_reg + (reg & 3)); break;
		case 2: data = (u32) READ_REG16(data_reg+ (reg & 2)); break;
		case 4: data = READ_REG32(data_reg); break;
		}
	}
	LBA_CFG_RESTORE(d, d->hba.base_addr);
	return(data);
}


static int elroy_cfg_read(struct pci_bus *bus, unsigned int devfn, int pos, int size, u32 *data)
{
	struct lba_device *d = LBA_DEV(parisc_walk_tree(bus->bridge));
	u32 local_bus = (bus->parent == NULL) ? 0 : bus->secondary;
	u32 tok = LBA_CFG_TOK(local_bus, devfn);
	void __iomem *data_reg = d->hba.base_addr + LBA_PCI_CFG_DATA;

	if ((pos > 255) || (devfn > 255))
		return -EINVAL;

/* FIXME: B2K/C3600 workaround is always use old method... */
	/* if (!LBA_SKIP_PROBE(d)) */ {
		/* original - Generate config cycle on broken elroy
		  with risk we will miss PCI bus errors. */
		*data = lba_rd_cfg(d, tok, pos, size);
		DBG_CFG("%s(%x+%2x) -> 0x%x (a)\n", __FUNCTION__, tok, pos, *data);
		return 0;
	}

	if (LBA_SKIP_PROBE(d) && !lba_device_present(bus->secondary, devfn, d)) {
		DBG_CFG("%s(%x+%2x) -> -1 (b)\n", __FUNCTION__, tok, pos);
		/* either don't want to look or know device isn't present. */
		*data = ~0U;
		return(0);
	}

	/* Basic Algorithm
	** Should only get here on fully working LBA rev.
	** This is how simple the code should have been.
	*/
	LBA_CFG_ADDR_SETUP(d, tok | pos);
	switch(size) {
	case 1: *data = READ_REG8 (data_reg + (pos & 3)); break;
	case 2: *data = READ_REG16(data_reg + (pos & 2)); break;
	case 4: *data = READ_REG32(data_reg); break;
	}
	DBG_CFG("%s(%x+%2x) -> 0x%x (c)\n", __FUNCTION__, tok, pos, *data);
	return 0;
}


static void
lba_wr_cfg(struct lba_device *d, u32 tok, u8 reg, u32 data, u32 size)
{
	int error = 0;
	u32 arb_mask = 0;
	u32 error_config = 0;
	u32 status_control = 0;
	void __iomem *data_reg = d->hba.base_addr + LBA_PCI_CFG_DATA;

	LBA_CFG_SETUP(d, tok);
	LBA_CFG_ADDR_SETUP(d, tok | reg);
	switch (size) {
	case 1: WRITE_REG8 (data, data_reg + (reg & 3)); break;
	case 2: WRITE_REG16(data, data_reg + (reg & 2)); break;
	case 4: WRITE_REG32(data, data_reg);             break;
	}
	LBA_CFG_MASTER_ABORT_CHECK(d, d->hba.base_addr, tok, error);
	LBA_CFG_RESTORE(d, d->hba.base_addr);
}


/*
 * LBA 4.0 config write code implements non-postable semantics
 * by doing a read of CONFIG ADDR after the write.
 */

static int elroy_cfg_write(struct pci_bus *bus, unsigned int devfn, int pos, int size, u32 data)
{
	struct lba_device *d = LBA_DEV(parisc_walk_tree(bus->bridge));
	u32 local_bus = (bus->parent == NULL) ? 0 : bus->secondary;
	u32 tok = LBA_CFG_TOK(local_bus,devfn);

	if ((pos > 255) || (devfn > 255))
		return -EINVAL;

	if (!LBA_SKIP_PROBE(d)) {
		/* Original Workaround */
		lba_wr_cfg(d, tok, pos, (u32) data, size);
		DBG_CFG("%s(%x+%2x) = 0x%x (a)\n", __FUNCTION__, tok, pos,data);
		return 0;
	}

	if (LBA_SKIP_PROBE(d) && (!lba_device_present(bus->secondary, devfn, d))) {
		DBG_CFG("%s(%x+%2x) = 0x%x (b)\n", __FUNCTION__, tok, pos,data);
		return 1; /* New Workaround */
	}

	DBG_CFG("%s(%x+%2x) = 0x%x (c)\n", __FUNCTION__, tok, pos, data);

	/* Basic Algorithm */
	LBA_CFG_ADDR_SETUP(d, tok | pos);
	switch(size) {
	case 1: WRITE_REG8 (data, d->hba.base_addr + LBA_PCI_CFG_DATA + (pos & 3));
		   break;
	case 2: WRITE_REG16(data, d->hba.base_addr + LBA_PCI_CFG_DATA + (pos & 2));
		   break;
	case 4: WRITE_REG32(data, d->hba.base_addr + LBA_PCI_CFG_DATA);
		   break;
	}
	/* flush posted write */
	lba_t32 = READ_REG32(d->hba.base_addr + LBA_PCI_CFG_ADDR);
	return 0;
}


static struct pci_ops elroy_cfg_ops = {
	.read =		elroy_cfg_read,
	.write =	elroy_cfg_write,
};

/*
 * The mercury_cfg_ops are slightly misnamed; they're also used for Elroy
 * TR4.0 as no additional bugs were found in this areea between Elroy and
 * Mercury
 */

static int mercury_cfg_read(struct pci_bus *bus, unsigned int devfn, int pos, int size, u32 *data)
{
	struct lba_device *d = LBA_DEV(parisc_walk_tree(bus->bridge));
	u32 local_bus = (bus->parent == NULL) ? 0 : bus->secondary;
	u32 tok = LBA_CFG_TOK(local_bus, devfn);
	void __iomem *data_reg = d->hba.base_addr + LBA_PCI_CFG_DATA;

	if ((pos > 255) || (devfn > 255))
		return -EINVAL;

	LBA_CFG_TR4_ADDR_SETUP(d, tok | pos);
	switch(size) {
	case 1:
		*data = READ_REG8(data_reg + (pos & 3));
		break;
	case 2:
		*data = READ_REG16(data_reg + (pos & 2));
		break;
	case 4:
		*data = READ_REG32(data_reg);             break;
		break;
	}

	DBG_CFG("mercury_cfg_read(%x+%2x) -> 0x%x\n", tok, pos, *data);
	return 0;
}

/*
 * LBA 4.0 config write code implements non-postable semantics
 * by doing a read of CONFIG ADDR after the write.
 */

static int mercury_cfg_write(struct pci_bus *bus, unsigned int devfn, int pos, int size, u32 data)
{
	struct lba_device *d = LBA_DEV(parisc_walk_tree(bus->bridge));
	void __iomem *data_reg = d->hba.base_addr + LBA_PCI_CFG_DATA;
	u32 local_bus = (bus->parent == NULL) ? 0 : bus->secondary;
	u32 tok = LBA_CFG_TOK(local_bus,devfn);

	if ((pos > 255) || (devfn > 255))
		return -EINVAL;

	DBG_CFG("%s(%x+%2x) <- 0x%x (c)\n", __FUNCTION__, tok, pos, data);

	LBA_CFG_TR4_ADDR_SETUP(d, tok | pos);
	switch(size) {
	case 1:
		WRITE_REG8 (data, data_reg + (pos & 3));
		break;
	case 2:
		WRITE_REG16(data, data_reg + (pos & 2));
		break;
	case 4:
		WRITE_REG32(data, data_reg);
		break;
	}

	/* flush posted write */
	lba_t32 = READ_U32(d->hba.base_addr + LBA_PCI_CFG_ADDR);
	return 0;
}

static struct pci_ops mercury_cfg_ops = {
	.read =		mercury_cfg_read,
	.write =	mercury_cfg_write,
};


static void
lba_bios_init(void)
{
	DBG(MODULE_NAME ": lba_bios_init\n");
}


#ifdef CONFIG_64BIT

/*
** Determine if a device is already configured.
** If so, reserve it resources.
**
** Read PCI cfg command register and see if I/O or MMIO is enabled.
** PAT has to enable the devices it's using.
**
** Note: resources are fixed up before we try to claim them.
*/
static void
lba_claim_dev_resources(struct pci_dev *dev)
{
	u16 cmd;
	int i, srch_flags;

	(void) pci_read_config_word(dev, PCI_COMMAND, &cmd);

	srch_flags  = (cmd & PCI_COMMAND_IO) ? IORESOURCE_IO : 0;
	if (cmd & PCI_COMMAND_MEMORY)
		srch_flags |= IORESOURCE_MEM;

	if (!srch_flags)
		return;

	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		if (dev->resource[i].flags & srch_flags) {
			pci_claim_resource(dev, i);
			DBG("   claimed %s %d [%lx,%lx]/%lx\n",
				pci_name(dev), i,
				dev->resource[i].start,
				dev->resource[i].end,
				dev->resource[i].flags
				);
		}
	}
}


/*
 * truncate_pat_collision:  Deal with overlaps or outright collisions
 *			between PAT PDC reported ranges.
 *
 *   Broken PA8800 firmware will report lmmio range that
 *   overlaps with CPU HPA. Just truncate the lmmio range.
 *
 *   BEWARE: conflicts with this lmmio range may be an
 *   elmmio range which is pointing down another rope.
 *
 *  FIXME: only deals with one collision per range...theoretically we
 *  could have several. Supporting more than one collision will get messy.
 */
static unsigned long
truncate_pat_collision(struct resource *root, struct resource *new)
{
	unsigned long start = new->start;
	unsigned long end = new->end;
	struct resource *tmp = root->child;

	if (end <= start || start < root->start || !tmp)
		return 0;

	/* find first overlap */
	while (tmp && tmp->end < start)
		tmp = tmp->sibling;

	/* no entries overlap */
	if (!tmp)  return 0;

	/* found one that starts behind the new one
	** Don't need to do anything.
	*/
	if (tmp->start >= end) return 0;

	if (tmp->start <= start) {
		/* "front" of new one overlaps */
		new->start = tmp->end + 1;

		if (tmp->end >= end) {
			/* AACCKK! totally overlaps! drop this range. */
			return 1;
		}
	} 

	if (tmp->end < end ) {
		/* "end" of new one overlaps */
		new->end = tmp->start - 1;
	}

	printk(KERN_WARNING "LBA: Truncating lmmio_space [%lx/%lx] "
					"to [%lx,%lx]\n",
			start, end,
			(long)new->start, (long)new->end );

	return 0;	/* truncation successful */
}

#else
#define lba_claim_dev_resources(dev) do { } while (0)
#define truncate_pat_collision(r,n)  (0)
#endif

/*
** The algorithm is generic code.
** But it needs to access local data structures to get the IRQ base.
** Could make this a "pci_fixup_irq(bus, region)" but not sure
** it's worth it.
**
** Called by do_pci_scan_bus() immediately after each PCI bus is walked.
** Resources aren't allocated until recursive buswalk below HBA is completed.
*/
static void
lba_fixup_bus(struct pci_bus *bus)
{
	struct list_head *ln;
#ifdef FBB_SUPPORT
	u16 status;
#endif
	struct lba_device *ldev = LBA_DEV(parisc_walk_tree(bus->bridge));
	int lba_portbase = HBA_PORT_BASE(ldev->hba.hba_num);

	DBG("lba_fixup_bus(0x%p) bus %d platform_data 0x%p\n",
		bus, bus->secondary, bus->bridge->platform_data);

	/*
	** Properly Setup MMIO resources for this bus.
	** pci_alloc_primary_bus() mangles this.
	*/
	if (bus->self) {
		/* PCI-PCI Bridge */
		pci_read_bridge_bases(bus);
	} else {
		/* Host-PCI Bridge */
		int err, i;

		DBG("lba_fixup_bus() %s [%lx/%lx]/%lx\n",
			ldev->hba.io_space.name,
			ldev->hba.io_space.start, ldev->hba.io_space.end,
			ldev->hba.io_space.flags);
		DBG("lba_fixup_bus() %s [%lx/%lx]/%lx\n",
			ldev->hba.lmmio_space.name,
			ldev->hba.lmmio_space.start, ldev->hba.lmmio_space.end,
			ldev->hba.lmmio_space.flags);

		err = request_resource(&ioport_resource, &(ldev->hba.io_space));
		if (err < 0) {
			lba_dump_res(&ioport_resource, 2);
			BUG();
		}
		/* advertize Host bridge resources to PCI bus */
		bus->resource[0] = &(ldev->hba.io_space);
		i = 1;

		if (ldev->hba.elmmio_space.start) {
			err = request_resource(&iomem_resource,
					&(ldev->hba.elmmio_space));
			if (err < 0) {

				printk("FAILED: lba_fixup_bus() request for "
						"elmmio_space [%lx/%lx]\n",
						(long)ldev->hba.elmmio_space.start,
						(long)ldev->hba.elmmio_space.end);

				/* lba_dump_res(&iomem_resource, 2); */
				/* BUG(); */
			} else
				bus->resource[i++] = &(ldev->hba.elmmio_space);
		}


		/*   Overlaps with elmmio can (and should) fail here.
		 *   We will prune (or ignore) the distributed range.
		 *
		 *   FIXME: SBA code should register all elmmio ranges first.
		 *      that would take care of elmmio ranges routed
		 *	to a different rope (already discovered) from
		 *	getting registered *after* LBA code has already
		 *	registered it's distributed lmmio range.
		 */
		if (truncate_pat_collision(&iomem_resource,
				       	&(ldev->hba.lmmio_space))) {

			printk(KERN_WARNING "LBA: lmmio_space [%lx/%lx] duplicate!\n",
					(long)ldev->hba.lmmio_space.start,
					(long)ldev->hba.lmmio_space.end);
		} else {
			err = request_resource(&iomem_resource, &(ldev->hba.lmmio_space));
			if (err < 0) {
				printk(KERN_ERR "FAILED: lba_fixup_bus() request for "
					"lmmio_space [%lx/%lx]\n",
					(long)ldev->hba.lmmio_space.start,
					(long)ldev->hba.lmmio_space.end);
			} else
				bus->resource[i++] = &(ldev->hba.lmmio_space);
		}

#ifdef CONFIG_64BIT
		/* GMMIO is  distributed range. Every LBA/Rope gets part it. */
		if (ldev->hba.gmmio_space.flags) {
			err = request_resource(&iomem_resource, &(ldev->hba.gmmio_space));
			if (err < 0) {
				printk("FAILED: lba_fixup_bus() request for "
					"gmmio_space [%lx/%lx]\n",
					(long)ldev->hba.gmmio_space.start,
					(long)ldev->hba.gmmio_space.end);
				lba_dump_res(&iomem_resource, 2);
				BUG();
			}
			bus->resource[i++] = &(ldev->hba.gmmio_space);
		}
#endif

	}

	list_for_each(ln, &bus->devices) {
		int i;
		struct pci_dev *dev = pci_dev_b(ln);

		DBG("lba_fixup_bus() %s\n", pci_name(dev));

		/* Virtualize Device/Bridge Resources. */
		for (i = 0; i < PCI_BRIDGE_RESOURCES; i++) {
			struct resource *res = &dev->resource[i];

			/* If resource not allocated - skip it */
			if (!res->start)
				continue;

			if (res->flags & IORESOURCE_IO) {
				DBG("lba_fixup_bus() I/O Ports [%lx/%lx] -> ",
					res->start, res->end);
				res->start |= lba_portbase;
				res->end   |= lba_portbase;
				DBG("[%lx/%lx]\n", res->start, res->end);
			} else if (res->flags & IORESOURCE_MEM) {
				/*
				** Convert PCI (IO_VIEW) addresses to
				** processor (PA_VIEW) addresses
				 */
				DBG("lba_fixup_bus() MMIO [%lx/%lx] -> ",
					res->start, res->end);
				res->start = PCI_HOST_ADDR(HBA_DATA(ldev), res->start);
				res->end   = PCI_HOST_ADDR(HBA_DATA(ldev), res->end);
				DBG("[%lx/%lx]\n", res->start, res->end);
			} else {
				DBG("lba_fixup_bus() WTF? 0x%lx [%lx/%lx] XXX",
					res->flags, res->start, res->end);
			}
		}

#ifdef FBB_SUPPORT
		/*
		** If one device does not support FBB transfers,
		** No one on the bus can be allowed to use them.
		*/
		(void) pci_read_config_word(dev, PCI_STATUS, &status);
		bus->bridge_ctl &= ~(status & PCI_STATUS_FAST_BACK);
#endif

		if (is_pdc_pat()) {
			/* Claim resources for PDC's devices */
			lba_claim_dev_resources(dev);
		}

                /*
		** P2PB's have no IRQs. ignore them.
		*/
		if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI)
			continue;

		/* Adjust INTERRUPT_LINE for this dev */
		iosapic_fixup_irq(ldev->iosapic_obj, dev);
	}

#ifdef FBB_SUPPORT
/* FIXME/REVISIT - finish figuring out to set FBB on both
** pci_setup_bridge() clobbers PCI_BRIDGE_CONTROL.
** Can't fixup here anyway....garr...
*/
	if (fbb_enable) {
		if (bus->self) {
			u8 control;
			/* enable on PPB */
			(void) pci_read_config_byte(bus->self, PCI_BRIDGE_CONTROL, &control);
			(void) pci_write_config_byte(bus->self, PCI_BRIDGE_CONTROL, control | PCI_STATUS_FAST_BACK);

		} else {
			/* enable on LBA */
		}
		fbb_enable = PCI_COMMAND_FAST_BACK;
	}

	/* Lastly enable FBB/PERR/SERR on all devices too */
	list_for_each(ln, &bus->devices) {
		(void) pci_read_config_word(dev, PCI_COMMAND, &status);
		status |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR | fbb_enable;
		(void) pci_write_config_word(dev, PCI_COMMAND, status);
	}
#endif
}


struct pci_bios_ops lba_bios_ops = {
	.init =		lba_bios_init,
	.fixup_bus =	lba_fixup_bus,
};




/*******************************************************
**
** LBA Sprockets "I/O Port" Space Accessor Functions
**
** This set of accessor functions is intended for use with
** "legacy firmware" (ie Sprockets on Allegro/Forte boxes).
**
** Many PCI devices don't require use of I/O port space (eg Tulip,
** NCR720) since they export the same registers to both MMIO and
** I/O port space. In general I/O port space is slower than
** MMIO since drivers are designed so PIO writes can be posted.
**
********************************************************/

#define LBA_PORT_IN(size, mask) \
static u##size lba_astro_in##size (struct pci_hba_data *d, u16 addr) \
{ \
	u##size t; \
	t = READ_REG##size(astro_iop_base + addr); \
	DBG_PORT(" 0x%x\n", t); \
	return (t); \
}

LBA_PORT_IN( 8, 3)
LBA_PORT_IN(16, 2)
LBA_PORT_IN(32, 0)



/*
** BUG X4107:  Ordering broken - DMA RD return can bypass PIO WR
**
** Fixed in Elroy 2.2. The READ_U32(..., LBA_FUNC_ID) below is
** guarantee non-postable completion semantics - not avoid X4107.
** The READ_U32 only guarantees the write data gets to elroy but
** out to the PCI bus. We can't read stuff from I/O port space
** since we don't know what has side-effects. Attempting to read
** from configuration space would be suicidal given the number of
** bugs in that elroy functionality.
**
**      Description:
**          DMA read results can improperly pass PIO writes (X4107).  The
**          result of this bug is that if a processor modifies a location in
**          memory after having issued PIO writes, the PIO writes are not
**          guaranteed to be completed before a PCI device is allowed to see
**          the modified data in a DMA read.
**
**          Note that IKE bug X3719 in TR1 IKEs will result in the same
**          symptom.
**
**      Workaround:
**          The workaround for this bug is to always follow a PIO write with
**          a PIO read to the same bus before starting DMA on that PCI bus.
**
*/
#define LBA_PORT_OUT(size, mask) \
static void lba_astro_out##size (struct pci_hba_data *d, u16 addr, u##size val) \
{ \
	DBG_PORT("%s(0x%p, 0x%x, 0x%x)\n", __FUNCTION__, d, addr, val); \
	WRITE_REG##size(val, astro_iop_base + addr); \
	if (LBA_DEV(d)->hw_rev < 3) \
		lba_t32 = READ_U32(d->base_addr + LBA_FUNC_ID); \
}

LBA_PORT_OUT( 8, 3)
LBA_PORT_OUT(16, 2)
LBA_PORT_OUT(32, 0)


static struct pci_port_ops lba_astro_port_ops = {
	.inb =	lba_astro_in8,
	.inw =	lba_astro_in16,
	.inl =	lba_astro_in32,
	.outb =	lba_astro_out8,
	.outw =	lba_astro_out16,
	.outl =	lba_astro_out32
};


#ifdef CONFIG_64BIT
#define PIOP_TO_GMMIO(lba, addr) \
	((lba)->iop_base + (((addr)&0xFFFC)<<10) + ((addr)&3))

/*******************************************************
**
** LBA PAT "I/O Port" Space Accessor Functions
**
** This set of accessor functions is intended for use with
** "PAT PDC" firmware (ie Prelude/Rhapsody/Piranha boxes).
**
** This uses the PIOP space located in the first 64MB of GMMIO.
** Each rope gets a full 64*KB* (ie 4 bytes per page) this way.
** bits 1:0 stay the same.  bits 15:2 become 25:12.
** Then add the base and we can generate an I/O Port cycle.
********************************************************/
#undef LBA_PORT_IN
#define LBA_PORT_IN(size, mask) \
static u##size lba_pat_in##size (struct pci_hba_data *l, u16 addr) \
{ \
	u##size t; \
	DBG_PORT("%s(0x%p, 0x%x) ->", __FUNCTION__, l, addr); \
	t = READ_REG##size(PIOP_TO_GMMIO(LBA_DEV(l), addr)); \
	DBG_PORT(" 0x%x\n", t); \
	return (t); \
}

LBA_PORT_IN( 8, 3)
LBA_PORT_IN(16, 2)
LBA_PORT_IN(32, 0)


#undef LBA_PORT_OUT
#define LBA_PORT_OUT(size, mask) \
static void lba_pat_out##size (struct pci_hba_data *l, u16 addr, u##size val) \
{ \
	void __iomem *where = PIOP_TO_GMMIO(LBA_DEV(l), addr); \
	DBG_PORT("%s(0x%p, 0x%x, 0x%x)\n", __FUNCTION__, l, addr, val); \
	WRITE_REG##size(val, where); \
	/* flush the I/O down to the elroy at least */ \
	lba_t32 = READ_U32(l->base_addr + LBA_FUNC_ID); \
}

LBA_PORT_OUT( 8, 3)
LBA_PORT_OUT(16, 2)
LBA_PORT_OUT(32, 0)


static struct pci_port_ops lba_pat_port_ops = {
	.inb =	lba_pat_in8,
	.inw =	lba_pat_in16,
	.inl =	lba_pat_in32,
	.outb =	lba_pat_out8,
	.outw =	lba_pat_out16,
	.outl =	lba_pat_out32
};



/*
** make range information from PDC available to PCI subsystem.
** We make the PDC call here in order to get the PCI bus range
** numbers. The rest will get forwarded in pcibios_fixup_bus().
** We don't have a struct pci_bus assigned to us yet.
*/
static void
lba_pat_resources(struct parisc_device *pa_dev, struct lba_device *lba_dev)
{
	unsigned long bytecnt;
	pdc_pat_cell_mod_maddr_block_t pa_pdc_cell;	/* PA_VIEW */
	pdc_pat_cell_mod_maddr_block_t io_pdc_cell;	/* IO_VIEW */
	long io_count;
	long status;	/* PDC return status */
	long pa_count;
	int i;

	/* return cell module (IO view) */
	status = pdc_pat_cell_module(&bytecnt, pa_dev->pcell_loc, pa_dev->mod_index,
				PA_VIEW, & pa_pdc_cell);
	pa_count = pa_pdc_cell.mod[1];

	status |= pdc_pat_cell_module(&bytecnt, pa_dev->pcell_loc, pa_dev->mod_index,
				IO_VIEW, &io_pdc_cell);
	io_count = io_pdc_cell.mod[1];

	/* We've already done this once for device discovery...*/
	if (status != PDC_OK) {
		panic("pdc_pat_cell_module() call failed for LBA!\n");
	}

	if (PAT_GET_ENTITY(pa_pdc_cell.mod_info) != PAT_ENTITY_LBA) {
		panic("pdc_pat_cell_module() entity returned != PAT_ENTITY_LBA!\n");
	}

	/*
	** Inspect the resources PAT tells us about
	*/
	for (i = 0; i < pa_count; i++) {
		struct {
			unsigned long type;
			unsigned long start;
			unsigned long end;	/* aka finish */
		} *p, *io;
		struct resource *r;

		p = (void *) &(pa_pdc_cell.mod[2+i*3]);
		io = (void *) &(io_pdc_cell.mod[2+i*3]);

		/* Convert the PAT range data to PCI "struct resource" */
		switch(p->type & 0xff) {
		case PAT_PBNUM:
			lba_dev->hba.bus_num.start = p->start;
			lba_dev->hba.bus_num.end   = p->end;
			break;

		case PAT_LMMIO:
			/* used to fix up pre-initialized MEM BARs */
			if (!lba_dev->hba.lmmio_space.start) {
				sprintf(lba_dev->hba.lmmio_name,
						"PCI%02x LMMIO",
						(int)lba_dev->hba.bus_num.start);
				lba_dev->hba.lmmio_space_offset = p->start -
					io->start;
				r = &lba_dev->hba.lmmio_space;
				r->name = lba_dev->hba.lmmio_name;
			} else if (!lba_dev->hba.elmmio_space.start) {
				sprintf(lba_dev->hba.elmmio_name,
						"PCI%02x ELMMIO",
						(int)lba_dev->hba.bus_num.start);
				r = &lba_dev->hba.elmmio_space;
				r->name = lba_dev->hba.elmmio_name;
			} else {
				printk(KERN_WARNING MODULE_NAME
					" only supports 2 LMMIO resources!\n");
				break;
			}

			r->start  = p->start;
			r->end    = p->end;
			r->flags  = IORESOURCE_MEM;
			r->parent = r->sibling = r->child = NULL;
			break;

		case PAT_GMMIO:
			/* MMIO space > 4GB phys addr; for 64-bit BAR */
			sprintf(lba_dev->hba.gmmio_name, "PCI%02x GMMIO",
					(int)lba_dev->hba.bus_num.start);
			r = &lba_dev->hba.gmmio_space;
			r->name  = lba_dev->hba.gmmio_name;
			r->start  = p->start;
			r->end    = p->end;
			r->flags  = IORESOURCE_MEM;
			r->parent = r->sibling = r->child = NULL;
			break;

		case PAT_NPIOP:
			printk(KERN_WARNING MODULE_NAME
				" range[%d] : ignoring NPIOP (0x%lx)\n",
				i, p->start);
			break;

		case PAT_PIOP:
			/*
			** Postable I/O port space is per PCI host adapter.
			** base of 64MB PIOP region
			*/
			lba_dev->iop_base = ioremap_nocache(p->start, 64 * 1024 * 1024);

			sprintf(lba_dev->hba.io_name, "PCI%02x Ports",
					(int)lba_dev->hba.bus_num.start);
			r = &lba_dev->hba.io_space;
			r->name  = lba_dev->hba.io_name;
			r->start  = HBA_PORT_BASE(lba_dev->hba.hba_num);
			r->end    = r->start + HBA_PORT_SPACE_SIZE - 1;
			r->flags  = IORESOURCE_IO;
			r->parent = r->sibling = r->child = NULL;
			break;

		default:
			printk(KERN_WARNING MODULE_NAME
				" range[%d] : unknown pat range type (0x%lx)\n",
				i, p->type & 0xff);
			break;
		}
	}
}
#else
/* keep compiler from complaining about missing declarations */
#define lba_pat_port_ops lba_astro_port_ops
#define lba_pat_resources(pa_dev, lba_dev)
#endif	/* CONFIG_64BIT */


extern void sba_distributed_lmmio(struct parisc_device *, struct resource *);
extern void sba_directed_lmmio(struct parisc_device *, struct resource *);


static void
lba_legacy_resources(struct parisc_device *pa_dev, struct lba_device *lba_dev)
{
	struct resource *r;
	int lba_num;

	lba_dev->hba.lmmio_space_offset = PCI_F_EXTEND;

	/*
	** With "legacy" firmware, the lowest byte of FW_SCRATCH
	** represents bus->secondary and the second byte represents
	** bus->subsidiary (i.e. highest PPB programmed by firmware).
	** PCI bus walk *should* end up with the same result.
	** FIXME: But we don't have sanity checks in PCI or LBA.
	*/
	lba_num = READ_REG32(lba_dev->hba.base_addr + LBA_FW_SCRATCH);
	r = &(lba_dev->hba.bus_num);
	r->name = "LBA PCI Busses";
	r->start = lba_num & 0xff;
	r->end = (lba_num>>8) & 0xff;

	/* Set up local PCI Bus resources - we don't need them for
	** Legacy boxes but it's nice to see in /proc/iomem.
	*/
	r = &(lba_dev->hba.lmmio_space);
	sprintf(lba_dev->hba.lmmio_name, "PCI%02x LMMIO",
					(int)lba_dev->hba.bus_num.start);
	r->name  = lba_dev->hba.lmmio_name;

#if 1
	/* We want the CPU -> IO routing of addresses.
	 * The SBA BASE/MASK registers control CPU -> IO routing.
	 * Ask SBA what is routed to this rope/LBA.
	 */
	sba_distributed_lmmio(pa_dev, r);
#else
	/*
	 * The LBA BASE/MASK registers control IO -> System routing.
	 *
	 * The following code works but doesn't get us what we want.
	 * Well, only because firmware (v5.0) on C3000 doesn't program
	 * the LBA BASE/MASE registers to be the exact inverse of 
	 * the corresponding SBA registers. Other Astro/Pluto
	 * based platform firmware may do it right.
	 *
	 * Should someone want to mess with MSI, they may need to
	 * reprogram LBA BASE/MASK registers. Thus preserve the code
	 * below until MSI is known to work on C3000/A500/N4000/RP3440.
	 *
	 * Using the code below, /proc/iomem shows:
	 * ...
	 * f0000000-f0ffffff : PCI00 LMMIO
	 *   f05d0000-f05d0000 : lcd_data
	 *   f05d0008-f05d0008 : lcd_cmd
	 * f1000000-f1ffffff : PCI01 LMMIO
	 * f4000000-f4ffffff : PCI02 LMMIO
	 *   f4000000-f4001fff : sym53c8xx
	 *   f4002000-f4003fff : sym53c8xx
	 *   f4004000-f40043ff : sym53c8xx
	 *   f4005000-f40053ff : sym53c8xx
	 *   f4007000-f4007fff : ohci_hcd
	 *   f4008000-f40083ff : tulip
	 * f6000000-f6ffffff : PCI03 LMMIO
	 * f8000000-fbffffff : PCI00 ELMMIO
	 *   fa100000-fa4fffff : stifb mmio
	 *   fb000000-fb1fffff : stifb fb
	 *
	 * But everything listed under PCI02 actually lives under PCI00.
	 * This is clearly wrong.
	 *
	 * Asking SBA how things are routed tells the correct story:
	 * LMMIO_BASE/MASK/ROUTE f4000001 fc000000 00000000
	 * DIR0_BASE/MASK/ROUTE fa000001 fe000000 00000006
	 * DIR1_BASE/MASK/ROUTE f9000001 ff000000 00000004
	 * DIR2_BASE/MASK/ROUTE f0000000 fc000000 00000000
	 * DIR3_BASE/MASK/ROUTE f0000000 fc000000 00000000
	 *
	 * Which looks like this in /proc/iomem:
	 * f4000000-f47fffff : PCI00 LMMIO
	 *   f4000000-f4001fff : sym53c8xx
	 *   ...[deteled core devices - same as above]...
	 *   f4008000-f40083ff : tulip
	 * f4800000-f4ffffff : PCI01 LMMIO
	 * f6000000-f67fffff : PCI02 LMMIO
	 * f7000000-f77fffff : PCI03 LMMIO
	 * f9000000-f9ffffff : PCI02 ELMMIO
	 * fa000000-fbffffff : PCI03 ELMMIO
	 *   fa100000-fa4fffff : stifb mmio
	 *   fb000000-fb1fffff : stifb fb
	 *
	 * ie all Built-in core are under now correctly under PCI00.
	 * The "PCI02 ELMMIO" directed range is for:
	 *  +-[02]---03.0  3Dfx Interactive, Inc. Voodoo 2
	 *
	 * All is well now.
	 */
	r->start = READ_REG32(lba_dev->hba.base_addr + LBA_LMMIO_BASE);
	if (r->start & 1) {
		unsigned long rsize;

		r->flags = IORESOURCE_MEM;
		/* mmio_mask also clears Enable bit */
		r->start &= mmio_mask;
		r->start = PCI_HOST_ADDR(HBA_DATA(lba_dev), r->start);
		rsize = ~ READ_REG32(lba_dev->hba.base_addr + LBA_LMMIO_MASK);

		/*
		** Each rope only gets part of the distributed range.
		** Adjust "window" for this rope.
		*/
		rsize /= ROPES_PER_IOC;
		r->start += (rsize + 1) * LBA_NUM(pa_dev->hpa.start);
		r->end = r->start + rsize;
	} else {
		r->end = r->start = 0;	/* Not enabled. */
	}
#endif

	/*
	** "Directed" ranges are used when the "distributed range" isn't
	** sufficient for all devices below a given LBA.  Typically devices
	** like graphics cards or X25 may need a directed range when the
	** bus has multiple slots (ie multiple devices) or the device
	** needs more than the typical 4 or 8MB a distributed range offers.
	**
	** The main reason for ignoring it now frigging complications.
	** Directed ranges may overlap (and have precedence) over
	** distributed ranges. Or a distributed range assigned to a unused
	** rope may be used by a directed range on a different rope.
	** Support for graphics devices may require fixing this
	** since they may be assigned a directed range which overlaps
	** an existing (but unused portion of) distributed range.
	*/
	r = &(lba_dev->hba.elmmio_space);
	sprintf(lba_dev->hba.elmmio_name, "PCI%02x ELMMIO",
					(int)lba_dev->hba.bus_num.start);
	r->name  = lba_dev->hba.elmmio_name;

#if 1
	/* See comment which precedes call to sba_directed_lmmio() */
	sba_directed_lmmio(pa_dev, r);
#else
	r->start = READ_REG32(lba_dev->hba.base_addr + LBA_ELMMIO_BASE);

	if (r->start & 1) {
		unsigned long rsize;
		r->flags = IORESOURCE_MEM;
		/* mmio_mask also clears Enable bit */
		r->start &= mmio_mask;
		r->start = PCI_HOST_ADDR(HBA_DATA(lba_dev), r->start);
		rsize = READ_REG32(lba_dev->hba.base_addr + LBA_ELMMIO_MASK);
		r->end = r->start + ~rsize;
	}
#endif

	r = &(lba_dev->hba.io_space);
	sprintf(lba_dev->hba.io_name, "PCI%02x Ports",
					(int)lba_dev->hba.bus_num.start);
	r->name  = lba_dev->hba.io_name;
	r->flags = IORESOURCE_IO;
	r->start = READ_REG32(lba_dev->hba.base_addr + LBA_IOS_BASE) & ~1L;
	r->end   = r->start + (READ_REG32(lba_dev->hba.base_addr + LBA_IOS_MASK) ^ (HBA_PORT_SPACE_SIZE - 1));

	/* Virtualize the I/O Port space ranges */
	lba_num = HBA_PORT_BASE(lba_dev->hba.hba_num);
	r->start |= lba_num;
	r->end   |= lba_num;
}


/**************************************************************************
**
**   LBA initialization code (HW and SW)
**
**   o identify LBA chip itself
**   o initialize LBA chip modes (HardFail)
**   o FIXME: initialize DMA hints for reasonable defaults
**   o enable configuration functions
**   o call pci_register_ops() to discover devs (fixup/fixup_bus get invoked)
**
**************************************************************************/

static int __init
lba_hw_init(struct lba_device *d)
{
	u32 stat;
	u32 bus_reset;	/* PDC_PAT_BUG */

#if 0
	printk(KERN_DEBUG "LBA %lx  STAT_CTL %Lx  ERROR_CFG %Lx  STATUS %Lx DMA_CTL %Lx\n",
		d->hba.base_addr,
		READ_REG64(d->hba.base_addr + LBA_STAT_CTL),
		READ_REG64(d->hba.base_addr + LBA_ERROR_CONFIG),
		READ_REG64(d->hba.base_addr + LBA_ERROR_STATUS),
		READ_REG64(d->hba.base_addr + LBA_DMA_CTL) );
	printk(KERN_DEBUG "	ARB mask %Lx  pri %Lx  mode %Lx  mtlt %Lx\n",
		READ_REG64(d->hba.base_addr + LBA_ARB_MASK),
		READ_REG64(d->hba.base_addr + LBA_ARB_PRI),
		READ_REG64(d->hba.base_addr + LBA_ARB_MODE),
		READ_REG64(d->hba.base_addr + LBA_ARB_MTLT) );
	printk(KERN_DEBUG "	HINT cfg 0x%Lx\n",
		READ_REG64(d->hba.base_addr + LBA_HINT_CFG));
	printk(KERN_DEBUG "	HINT reg ");
	{ int i;
	for (i=LBA_HINT_BASE; i< (14*8 + LBA_HINT_BASE); i+=8)
		printk(" %Lx", READ_REG64(d->hba.base_addr + i));
	}
	printk("\n");
#endif	/* DEBUG_LBA_PAT */

#ifdef CONFIG_64BIT
/*
 * FIXME add support for PDC_PAT_IO "Get slot status" - OLAR support
 * Only N-Class and up can really make use of Get slot status.
 * maybe L-class too but I've never played with it there.
 */
#endif

	/* PDC_PAT_BUG: exhibited in rev 40.48  on L2000 */
	bus_reset = READ_REG32(d->hba.base_addr + LBA_STAT_CTL + 4) & 1;
	if (bus_reset) {
		printk(KERN_DEBUG "NOTICE: PCI bus reset still asserted! (clearing)\n");
	}

	stat = READ_REG32(d->hba.base_addr + LBA_ERROR_CONFIG);
	if (stat & LBA_SMART_MODE) {
		printk(KERN_DEBUG "NOTICE: LBA in SMART mode! (cleared)\n");
		stat &= ~LBA_SMART_MODE;
		WRITE_REG32(stat, d->hba.base_addr + LBA_ERROR_CONFIG);
	}

	/* Set HF mode as the default (vs. -1 mode). */
        stat = READ_REG32(d->hba.base_addr + LBA_STAT_CTL);
	WRITE_REG32(stat | HF_ENABLE, d->hba.base_addr + LBA_STAT_CTL);

	/*
	** Writing a zero to STAT_CTL.rf (bit 0) will clear reset signal
	** if it's not already set. If we just cleared the PCI Bus Reset
	** signal, wait a bit for the PCI devices to recover and setup.
	*/
	if (bus_reset)
		mdelay(pci_post_reset_delay);

	if (0 == READ_REG32(d->hba.base_addr + LBA_ARB_MASK)) {
		/*
		** PDC_PAT_BUG: PDC rev 40.48 on L2000.
		** B2000/C3600/J6000 also have this problem?
		** 
		** Elroys with hot pluggable slots don't get configured
		** correctly if the slot is empty.  ARB_MASK is set to 0
		** and we can't master transactions on the bus if it's
		** not at least one. 0x3 enables elroy and first slot.
		*/
		printk(KERN_DEBUG "NOTICE: Enabling PCI Arbitration\n");
		WRITE_REG32(0x3, d->hba.base_addr + LBA_ARB_MASK);
	}

	/*
	** FIXME: Hint registers are programmed with default hint
	** values by firmware. Hints should be sane even if we
	** can't reprogram them the way drivers want.
	*/
	return 0;
}

/*
 * Unfortunately, when firmware numbers busses, it doesn't take into account
 * Cardbus bridges.  So we have to renumber the busses to suit ourselves.
 * Elroy/Mercury don't actually know what bus number they're attached to;
 * we use bus 0 to indicate the directly attached bus and any other bus
 * number will be taken care of by the PCI-PCI bridge.
 */
static unsigned int lba_next_bus = 0;

/*
 * Determine if lba should claim this chip (return 0) or not (return 1).
 * If so, initialize the chip and tell other partners in crime they
 * have work to do.
 */
static int __init
lba_driver_probe(struct parisc_device *dev)
{
	struct lba_device *lba_dev;
	struct pci_bus *lba_bus;
	struct pci_ops *cfg_ops;
	u32 func_class;
	void *tmp_obj;
	char *version;
	void __iomem *addr = ioremap_nocache(dev->hpa.start, 4096);

	/* Read HW Rev First */
	func_class = READ_REG32(addr + LBA_FCLASS);

	if (IS_ELROY(dev)) {	
		func_class &= 0xf;
		switch (func_class) {
		case 0:	version = "TR1.0"; break;
		case 1:	version = "TR2.0"; break;
		case 2:	version = "TR2.1"; break;
		case 3:	version = "TR2.2"; break;
		case 4:	version = "TR3.0"; break;
		case 5:	version = "TR4.0"; break;
		default: version = "TR4+";
		}

		printk(KERN_INFO "Elroy version %s (0x%x) found at 0x%lx\n",
		       version, func_class & 0xf, (long)dev->hpa.start);

		if (func_class < 2) {
			printk(KERN_WARNING "Can't support LBA older than "
				"TR2.1 - continuing under adversity.\n");
		}

#if 0
/* Elroy TR4.0 should work with simple algorithm.
   But it doesn't.  Still missing something. *sigh*
*/
		if (func_class > 4) {
			cfg_ops = &mercury_cfg_ops;
		} else
#endif
		{
			cfg_ops = &elroy_cfg_ops;
		}

	} else if (IS_MERCURY(dev) || IS_QUICKSILVER(dev)) {
		int major, minor;

		func_class &= 0xff;
		major = func_class >> 4, minor = func_class & 0xf;

		/* We could use one printk for both Elroy and Mercury,
                 * but for the mask for func_class.
                 */ 
		printk(KERN_INFO "%s version TR%d.%d (0x%x) found at 0x%lx\n",
		       IS_MERCURY(dev) ? "Mercury" : "Quicksilver", major,
		       minor, func_class, (long)dev->hpa.start);

		cfg_ops = &mercury_cfg_ops;
	} else {
		printk(KERN_ERR "Unknown LBA found at 0x%lx\n",
			(long)dev->hpa.start);
		return -ENODEV;
	}

	/* Tell I/O SAPIC driver we have a IRQ handler/region. */
	tmp_obj = iosapic_register(dev->hpa.start + LBA_IOSAPIC_BASE);

	/* NOTE: PCI devices (e.g. 103c:1005 graphics card) which don't
	**	have an IRT entry will get NULL back from iosapic code.
	*/
	
	lba_dev = kzalloc(sizeof(struct lba_device), GFP_KERNEL);
	if (!lba_dev) {
		printk(KERN_ERR "lba_init_chip - couldn't alloc lba_device\n");
		return(1);
	}


	/* ---------- First : initialize data we already have --------- */

	lba_dev->hw_rev = func_class;
	lba_dev->hba.base_addr = addr;
	lba_dev->hba.dev = dev;
	lba_dev->iosapic_obj = tmp_obj;  /* save interrupt handle */
	lba_dev->hba.iommu = sba_get_iommu(dev);  /* get iommu data */
	parisc_set_drvdata(dev, lba_dev);

	/* ------------ Second : initialize common stuff ---------- */
	pci_bios = &lba_bios_ops;
	pcibios_register_hba(HBA_DATA(lba_dev));
	spin_lock_init(&lba_dev->lba_lock);

	if (lba_hw_init(lba_dev))
		return(1);

	/* ---------- Third : setup I/O Port and MMIO resources  --------- */

	if (is_pdc_pat()) {
		/* PDC PAT firmware uses PIOP region of GMMIO space. */
		pci_port = &lba_pat_port_ops;
		/* Go ask PDC PAT what resources this LBA has */
		lba_pat_resources(dev, lba_dev);
	} else {
		if (!astro_iop_base) {
			/* Sprockets PDC uses NPIOP region */
			astro_iop_base = ioremap_nocache(LBA_PORT_BASE, 64 * 1024);
			pci_port = &lba_astro_port_ops;
		}

		/* Poke the chip a bit for /proc output */
		lba_legacy_resources(dev, lba_dev);
	}

	if (lba_dev->hba.bus_num.start < lba_next_bus)
		lba_dev->hba.bus_num.start = lba_next_bus;

	dev->dev.platform_data = lba_dev;
	lba_bus = lba_dev->hba.hba_bus =
		pci_scan_bus_parented(&dev->dev, lba_dev->hba.bus_num.start,
				cfg_ops, NULL);
	if (lba_bus) {
		lba_next_bus = lba_bus->subordinate + 1;
		pci_bus_add_devices(lba_bus);
	}

	/* This is in lieu of calling pci_assign_unassigned_resources() */
	if (is_pdc_pat()) {
		/* assign resources to un-initialized devices */

		DBG_PAT("LBA pci_bus_size_bridges()\n");
		pci_bus_size_bridges(lba_bus);

		DBG_PAT("LBA pci_bus_assign_resources()\n");
		pci_bus_assign_resources(lba_bus);

#ifdef DEBUG_LBA_PAT
		DBG_PAT("\nLBA PIOP resource tree\n");
		lba_dump_res(&lba_dev->hba.io_space, 2);
		DBG_PAT("\nLBA LMMIO resource tree\n");
		lba_dump_res(&lba_dev->hba.lmmio_space, 2);
#endif
	}
	pci_enable_bridges(lba_bus);


	/*
	** Once PCI register ops has walked the bus, access to config
	** space is restricted. Avoids master aborts on config cycles.
	** Early LBA revs go fatal on *any* master abort.
	*/
	if (cfg_ops == &elroy_cfg_ops) {
		lba_dev->flags |= LBA_FLAG_SKIP_PROBE;
	}

	/* Whew! Finally done! Tell services we got this one covered. */
	return 0;
}

static struct parisc_device_id lba_tbl[] = {
	{ HPHW_BRIDGE, HVERSION_REV_ANY_ID, ELROY_HVERS, 0xa },
	{ HPHW_BRIDGE, HVERSION_REV_ANY_ID, MERCURY_HVERS, 0xa },
	{ HPHW_BRIDGE, HVERSION_REV_ANY_ID, QUICKSILVER_HVERS, 0xa },
	{ 0, }
};

static struct parisc_driver lba_driver = {
	.name =		MODULE_NAME,
	.id_table =	lba_tbl,
	.probe =	lba_driver_probe,
};

/*
** One time initialization to let the world know the LBA was found.
** Must be called exactly once before pci_init().
*/
void __init lba_init(void)
{
	register_parisc_driver(&lba_driver);
}

/*
** Initialize the IBASE/IMASK registers for LBA (Elroy).
** Only called from sba_iommu.c in order to route ranges (MMIO vs DMA).
** sba_iommu is responsible for locking (none needed at init time).
*/
void lba_set_iregs(struct parisc_device *lba, u32 ibase, u32 imask)
{
	void __iomem * base_addr = ioremap_nocache(lba->hpa.start, 4096);

	imask <<= 2;	/* adjust for hints - 2 more bits */

	/* Make sure we aren't trying to set bits that aren't writeable. */
	WARN_ON((ibase & 0x001fffff) != 0);
	WARN_ON((imask & 0x001fffff) != 0);
	
	DBG("%s() ibase 0x%x imask 0x%x\n", __FUNCTION__, ibase, imask);
	WRITE_REG32( imask, base_addr + LBA_IMASK);
	WRITE_REG32( ibase, base_addr + LBA_IBASE);
	iounmap(base_addr);
}

