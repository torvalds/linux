/*
 * File Name:
 *   defxx.c
 *
 * Copyright Information:
 *   Copyright Digital Equipment Corporation 1996.
 *
 *   This software may be used and distributed according to the terms of
 *   the GNU General Public License, incorporated herein by reference.
 *
 * Abstract:
 *   A Linux device driver supporting the Digital Equipment Corporation
 *   FDDI TURBOchannel, EISA and PCI controller families.  Supported
 *   adapters include:
 *
 *		DEC FDDIcontroller/TURBOchannel (DEFTA)
 *		DEC FDDIcontroller/EISA         (DEFEA)
 *		DEC FDDIcontroller/PCI          (DEFPA)
 *
 * The original author:
 *   LVS	Lawrence V. Stefani <lstefani@yahoo.com>
 *
 * Maintainers:
 *   macro	Maciej W. Rozycki <macro@linux-mips.org>
 *
 * Credits:
 *   I'd like to thank Patricia Cross for helping me get started with
 *   Linux, David Davies for a lot of help upgrading and configuring
 *   my development system and for answering many OS and driver
 *   development questions, and Alan Cox for recommendations and
 *   integration help on getting FDDI support into Linux.  LVS
 *
 * Driver Architecture:
 *   The driver architecture is largely based on previous driver work
 *   for other operating systems.  The upper edge interface and
 *   functions were largely taken from existing Linux device drivers
 *   such as David Davies' DE4X5.C driver and Donald Becker's TULIP.C
 *   driver.
 *
 *   Adapter Probe -
 *		The driver scans for supported EISA adapters by reading the
 *		SLOT ID register for each EISA slot and making a match
 *		against the expected value.
 *
 *   Bus-Specific Initialization -
 *		This driver currently supports both EISA and PCI controller
 *		families.  While the custom DMA chip and FDDI logic is similar
 *		or identical, the bus logic is very different.  After
 *		initialization, the	only bus-specific differences is in how the
 *		driver enables and disables interrupts.  Other than that, the
 *		run-time critical code behaves the same on both families.
 *		It's important to note that both adapter families are configured
 *		to I/O map, rather than memory map, the adapter registers.
 *
 *   Driver Open/Close -
 *		In the driver open routine, the driver ISR (interrupt service
 *		routine) is registered and the adapter is brought to an
 *		operational state.  In the driver close routine, the opposite
 *		occurs; the driver ISR is deregistered and the adapter is
 *		brought to a safe, but closed state.  Users may use consecutive
 *		commands to bring the adapter up and down as in the following
 *		example:
 *					ifconfig fddi0 up
 *					ifconfig fddi0 down
 *					ifconfig fddi0 up
 *
 *   Driver Shutdown -
 *		Apparently, there is no shutdown or halt routine support under
 *		Linux.  This routine would be called during "reboot" or
 *		"shutdown" to allow the driver to place the adapter in a safe
 *		state before a warm reboot occurs.  To be really safe, the user
 *		should close the adapter before shutdown (eg. ifconfig fddi0 down)
 *		to ensure that the adapter DMA engine is taken off-line.  However,
 *		the current driver code anticipates this problem and always issues
 *		a soft reset of the adapter	at the beginning of driver initialization.
 *		A future driver enhancement in this area may occur in 2.1.X where
 *		Alan indicated that a shutdown handler may be implemented.
 *
 *   Interrupt Service Routine -
 *		The driver supports shared interrupts, so the ISR is registered for
 *		each board with the appropriate flag and the pointer to that board's
 *		device structure.  This provides the context during interrupt
 *		processing to support shared interrupts and multiple boards.
 *
 *		Interrupt enabling/disabling can occur at many levels.  At the host
 *		end, you can disable system interrupts, or disable interrupts at the
 *		PIC (on Intel systems).  Across the bus, both EISA and PCI adapters
 *		have a bus-logic chip interrupt enable/disable as well as a DMA
 *		controller interrupt enable/disable.
 *
 *		The driver currently enables and disables adapter interrupts at the
 *		bus-logic chip and assumes that Linux will take care of clearing or
 *		acknowledging any host-based interrupt chips.
 *
 *   Control Functions -
 *		Control functions are those used to support functions such as adding
 *		or deleting multicast addresses, enabling or disabling packet
 *		reception filters, or other custom/proprietary commands.  Presently,
 *		the driver supports the "get statistics", "set multicast list", and
 *		"set mac address" functions defined by Linux.  A list of possible
 *		enhancements include:
 *
 *				- Custom ioctl interface for executing port interface commands
 *				- Custom ioctl interface for adding unicast addresses to
 *				  adapter CAM (to support bridge functions).
 *				- Custom ioctl interface for supporting firmware upgrades.
 *
 *   Hardware (port interface) Support Routines -
 *		The driver function names that start with "dfx_hw_" represent
 *		low-level port interface routines that are called frequently.  They
 *		include issuing a DMA or port control command to the adapter,
 *		resetting the adapter, or reading the adapter state.  Since the
 *		driver initialization and run-time code must make calls into the
 *		port interface, these routines were written to be as generic and
 *		usable as possible.
 *
 *   Receive Path -
 *		The adapter DMA engine supports a 256 entry receive descriptor block
 *		of which up to 255 entries can be used at any given time.  The
 *		architecture is a standard producer, consumer, completion model in
 *		which the driver "produces" receive buffers to the adapter, the
 *		adapter "consumes" the receive buffers by DMAing incoming packet data,
 *		and the driver "completes" the receive buffers by servicing the
 *		incoming packet, then "produces" a new buffer and starts the cycle
 *		again.  Receive buffers can be fragmented in up to 16 fragments
 *		(descriptor	entries).  For simplicity, this driver posts
 *		single-fragment receive buffers of 4608 bytes, then allocates a
 *		sk_buff, copies the data, then reposts the buffer.  To reduce CPU
 *		utilization, a better approach would be to pass up the receive
 *		buffer (no extra copy) then allocate and post a replacement buffer.
 *		This is a performance enhancement that should be looked into at
 *		some point.
 *
 *   Transmit Path -
 *		Like the receive path, the adapter DMA engine supports a 256 entry
 *		transmit descriptor block of which up to 255 entries can be used at
 *		any	given time.  Transmit buffers can be fragmented	in up to 255
 *		fragments (descriptor entries).  This driver always posts one
 *		fragment per transmit packet request.
 *
 *		The fragment contains the entire packet from FC to end of data.
 *		Before posting the buffer to the adapter, the driver sets a three-byte
 *		packet request header (PRH) which is required by the Motorola MAC chip
 *		used on the adapters.  The PRH tells the MAC the type of token to
 *		receive/send, whether or not to generate and append the CRC, whether
 *		synchronous or asynchronous framing is used, etc.  Since the PRH
 *		definition is not necessarily consistent across all FDDI chipsets,
 *		the driver, rather than the common FDDI packet handler routines,
 *		sets these bytes.
 *
 *		To reduce the amount of descriptor fetches needed per transmit request,
 *		the driver takes advantage of the fact that there are at least three
 *		bytes available before the skb->data field on the outgoing transmit
 *		request.  This is guaranteed by having fddi_setup() in net_init.c set
 *		dev->hard_header_len to 24 bytes.  21 bytes accounts for the largest
 *		header in an 802.2 SNAP frame.  The other 3 bytes are the extra "pad"
 *		bytes which we'll use to store the PRH.
 *
 *		There's a subtle advantage to adding these pad bytes to the
 *		hard_header_len, it ensures that the data portion of the packet for
 *		an 802.2 SNAP frame is longword aligned.  Other FDDI driver
 *		implementations may not need the extra padding and can start copying
 *		or DMAing directly from the FC byte which starts at skb->data.  Should
 *		another driver implementation need ADDITIONAL padding, the net_init.c
 *		module should be updated and dev->hard_header_len should be increased.
 *		NOTE: To maintain the alignment on the data portion of the packet,
 *		dev->hard_header_len should always be evenly divisible by 4 and at
 *		least 24 bytes in size.
 *
 * Modification History:
 *		Date		Name	Description
 *		16-Aug-96	LVS		Created.
 *		20-Aug-96	LVS		Updated dfx_probe so that version information
 *							string is only displayed if 1 or more cards are
 *							found.  Changed dfx_rcv_queue_process to copy
 *							3 NULL bytes before FC to ensure that data is
 *							longword aligned in receive buffer.
 *		09-Sep-96	LVS		Updated dfx_ctl_set_multicast_list to enable
 *							LLC group promiscuous mode if multicast list
 *							is too large.  LLC individual/group promiscuous
 *							mode is now disabled if IFF_PROMISC flag not set.
 *							dfx_xmt_queue_pkt no longer checks for NULL skb
 *							on Alan Cox recommendation.  Added node address
 *							override support.
 *		12-Sep-96	LVS		Reset current address to factory address during
 *							device open.  Updated transmit path to post a
 *							single fragment which includes PRH->end of data.
 *		Mar 2000	AC		Did various cleanups for 2.3.x
 *		Jun 2000	jgarzik		PCI and resource alloc cleanups
 *		Jul 2000	tjeerd		Much cleanup and some bug fixes
 *		Sep 2000	tjeerd		Fix leak on unload, cosmetic code cleanup
 *		Feb 2001			Skb allocation fixes
 *		Feb 2001	davej		PCI enable cleanups.
 *		04 Aug 2003	macro		Converted to the DMA API.
 *		14 Aug 2004	macro		Fix device names reported.
 *		14 Jun 2005	macro		Use irqreturn_t.
 *		23 Oct 2006	macro		Big-endian host support.
 *		14 Dec 2006	macro		TURBOchannel support.
 *		01 Jul 2014	macro		Fixes for DMA on 64-bit hosts.
 */

/* Include files */
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/eisa.h>
#include <linux/errno.h>
#include <linux/fddidevice.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tc.h>

#include <asm/byteorder.h>
#include <asm/io.h>

#include "defxx.h"

/* Version information string should be updated prior to each new release!  */
#define DRV_NAME "defxx"
#define DRV_VERSION "v1.11"
#define DRV_RELDATE "2014/07/01"

static const char version[] =
	DRV_NAME ": " DRV_VERSION " " DRV_RELDATE
	"  Lawrence V. Stefani and others\n";

#define DYNAMIC_BUFFERS 1

#define SKBUFF_RX_COPYBREAK 200
/*
 * NEW_SKB_SIZE = PI_RCV_DATA_K_SIZE_MAX+128 to allow 128 byte
 * alignment for compatibility with old EISA boards.
 */
#define NEW_SKB_SIZE (PI_RCV_DATA_K_SIZE_MAX+128)

#ifdef CONFIG_EISA
#define DFX_BUS_EISA(dev) (dev->bus == &eisa_bus_type)
#else
#define DFX_BUS_EISA(dev) 0
#endif

#ifdef CONFIG_TC
#define DFX_BUS_TC(dev) (dev->bus == &tc_bus_type)
#else
#define DFX_BUS_TC(dev) 0
#endif

#ifdef CONFIG_DEFXX_MMIO
#define DFX_MMIO 1
#else
#define DFX_MMIO 0
#endif

/* Define module-wide (static) routines */

static void		dfx_bus_init(struct net_device *dev);
static void		dfx_bus_uninit(struct net_device *dev);
static void		dfx_bus_config_check(DFX_board_t *bp);

static int		dfx_driver_init(struct net_device *dev,
					const char *print_name,
					resource_size_t bar_start);
static int		dfx_adap_init(DFX_board_t *bp, int get_buffers);

static int		dfx_open(struct net_device *dev);
static int		dfx_close(struct net_device *dev);

static void		dfx_int_pr_halt_id(DFX_board_t *bp);
static void		dfx_int_type_0_process(DFX_board_t *bp);
static void		dfx_int_common(struct net_device *dev);
static irqreturn_t	dfx_interrupt(int irq, void *dev_id);

static struct		net_device_stats *dfx_ctl_get_stats(struct net_device *dev);
static void		dfx_ctl_set_multicast_list(struct net_device *dev);
static int		dfx_ctl_set_mac_address(struct net_device *dev, void *addr);
static int		dfx_ctl_update_cam(DFX_board_t *bp);
static int		dfx_ctl_update_filters(DFX_board_t *bp);

static int		dfx_hw_dma_cmd_req(DFX_board_t *bp);
static int		dfx_hw_port_ctrl_req(DFX_board_t *bp, PI_UINT32	command, PI_UINT32 data_a, PI_UINT32 data_b, PI_UINT32 *host_data);
static void		dfx_hw_adap_reset(DFX_board_t *bp, PI_UINT32 type);
static int		dfx_hw_adap_state_rd(DFX_board_t *bp);
static int		dfx_hw_dma_uninit(DFX_board_t *bp, PI_UINT32 type);

static int		dfx_rcv_init(DFX_board_t *bp, int get_buffers);
static void		dfx_rcv_queue_process(DFX_board_t *bp);
#ifdef DYNAMIC_BUFFERS
static void		dfx_rcv_flush(DFX_board_t *bp);
#else
static inline void	dfx_rcv_flush(DFX_board_t *bp) {}
#endif

static netdev_tx_t dfx_xmt_queue_pkt(struct sk_buff *skb,
				     struct net_device *dev);
static int		dfx_xmt_done(DFX_board_t *bp);
static void		dfx_xmt_flush(DFX_board_t *bp);

/* Define module-wide (static) variables */

static struct pci_driver dfx_pci_driver;
static struct eisa_driver dfx_eisa_driver;
static struct tc_driver dfx_tc_driver;


/*
 * =======================
 * = dfx_port_write_long =
 * = dfx_port_read_long  =
 * =======================
 *
 * Overview:
 *   Routines for reading and writing values from/to adapter
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp		- pointer to board information
 *   offset	- register offset from base I/O address
 *   data	- for dfx_port_write_long, this is a value to write;
 *		  for dfx_port_read_long, this is a pointer to store
 *		  the read value
 *
 * Functional Description:
 *   These routines perform the correct operation to read or write
 *   the adapter register.
 *
 *   EISA port block base addresses are based on the slot number in which the
 *   controller is installed.  For example, if the EISA controller is installed
 *   in slot 4, the port block base address is 0x4000.  If the controller is
 *   installed in slot 2, the port block base address is 0x2000, and so on.
 *   This port block can be used to access PDQ, ESIC, and DEFEA on-board
 *   registers using the register offsets defined in DEFXX.H.
 *
 *   PCI port block base addresses are assigned by the PCI BIOS or system
 *   firmware.  There is one 128 byte port block which can be accessed.  It
 *   allows for I/O mapping of both PDQ and PFI registers using the register
 *   offsets defined in DEFXX.H.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   bp->base is a valid base I/O address for this adapter.
 *   offset is a valid register offset for this adapter.
 *
 * Side Effects:
 *   Rather than produce macros for these functions, these routines
 *   are defined using "inline" to ensure that the compiler will
 *   generate inline code and not waste a procedure call and return.
 *   This provides all the benefits of macros, but with the
 *   advantage of strict data type checking.
 */

static inline void dfx_writel(DFX_board_t *bp, int offset, u32 data)
{
	writel(data, bp->base.mem + offset);
	mb();
}

static inline void dfx_outl(DFX_board_t *bp, int offset, u32 data)
{
	outl(data, bp->base.port + offset);
}

static void dfx_port_write_long(DFX_board_t *bp, int offset, u32 data)
{
	struct device __maybe_unused *bdev = bp->bus_dev;
	int dfx_bus_tc = DFX_BUS_TC(bdev);
	int dfx_use_mmio = DFX_MMIO || dfx_bus_tc;

	if (dfx_use_mmio)
		dfx_writel(bp, offset, data);
	else
		dfx_outl(bp, offset, data);
}


static inline void dfx_readl(DFX_board_t *bp, int offset, u32 *data)
{
	mb();
	*data = readl(bp->base.mem + offset);
}

static inline void dfx_inl(DFX_board_t *bp, int offset, u32 *data)
{
	*data = inl(bp->base.port + offset);
}

static void dfx_port_read_long(DFX_board_t *bp, int offset, u32 *data)
{
	struct device __maybe_unused *bdev = bp->bus_dev;
	int dfx_bus_tc = DFX_BUS_TC(bdev);
	int dfx_use_mmio = DFX_MMIO || dfx_bus_tc;

	if (dfx_use_mmio)
		dfx_readl(bp, offset, data);
	else
		dfx_inl(bp, offset, data);
}


/*
 * ================
 * = dfx_get_bars =
 * ================
 *
 * Overview:
 *   Retrieves the address ranges used to access control and status
 *   registers.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bdev	- pointer to device information
 *   bar_start	- pointer to store the start addresses
 *   bar_len	- pointer to store the lengths of the areas
 *
 * Assumptions:
 *   I am sure there are some.
 *
 * Side Effects:
 *   None
 */
static void dfx_get_bars(struct device *bdev,
			 resource_size_t *bar_start, resource_size_t *bar_len)
{
	int dfx_bus_pci = dev_is_pci(bdev);
	int dfx_bus_eisa = DFX_BUS_EISA(bdev);
	int dfx_bus_tc = DFX_BUS_TC(bdev);
	int dfx_use_mmio = DFX_MMIO || dfx_bus_tc;

	if (dfx_bus_pci) {
		int num = dfx_use_mmio ? 0 : 1;

		bar_start[0] = pci_resource_start(to_pci_dev(bdev), num);
		bar_len[0] = pci_resource_len(to_pci_dev(bdev), num);
		bar_start[2] = bar_start[1] = 0;
		bar_len[2] = bar_len[1] = 0;
	}
	if (dfx_bus_eisa) {
		unsigned long base_addr = to_eisa_device(bdev)->base_addr;
		resource_size_t bar_lo;
		resource_size_t bar_hi;

		if (dfx_use_mmio) {
			bar_lo = inb(base_addr + PI_ESIC_K_MEM_ADD_LO_CMP_2);
			bar_lo <<= 8;
			bar_lo |= inb(base_addr + PI_ESIC_K_MEM_ADD_LO_CMP_1);
			bar_lo <<= 8;
			bar_lo |= inb(base_addr + PI_ESIC_K_MEM_ADD_LO_CMP_0);
			bar_lo <<= 8;
			bar_start[0] = bar_lo;
			bar_hi = inb(base_addr + PI_ESIC_K_MEM_ADD_HI_CMP_2);
			bar_hi <<= 8;
			bar_hi |= inb(base_addr + PI_ESIC_K_MEM_ADD_HI_CMP_1);
			bar_hi <<= 8;
			bar_hi |= inb(base_addr + PI_ESIC_K_MEM_ADD_HI_CMP_0);
			bar_hi <<= 8;
			bar_len[0] = ((bar_hi - bar_lo) | PI_MEM_ADD_MASK_M) +
				     1;
		} else {
			bar_start[0] = base_addr;
			bar_len[0] = PI_ESIC_K_CSR_IO_LEN;
		}
		bar_start[1] = base_addr + PI_DEFEA_K_BURST_HOLDOFF;
		bar_len[1] = PI_ESIC_K_BURST_HOLDOFF_LEN;
		bar_start[2] = base_addr + PI_ESIC_K_ESIC_CSR;
		bar_len[2] = PI_ESIC_K_ESIC_CSR_LEN;
	}
	if (dfx_bus_tc) {
		bar_start[0] = to_tc_dev(bdev)->resource.start +
			       PI_TC_K_CSR_OFFSET;
		bar_len[0] = PI_TC_K_CSR_LEN;
		bar_start[2] = bar_start[1] = 0;
		bar_len[2] = bar_len[1] = 0;
	}
}

static const struct net_device_ops dfx_netdev_ops = {
	.ndo_open		= dfx_open,
	.ndo_stop		= dfx_close,
	.ndo_start_xmit		= dfx_xmt_queue_pkt,
	.ndo_get_stats		= dfx_ctl_get_stats,
	.ndo_set_rx_mode	= dfx_ctl_set_multicast_list,
	.ndo_set_mac_address	= dfx_ctl_set_mac_address,
};

static void dfx_register_res_alloc_err(const char *print_name, bool mmio,
				       bool eisa)
{
	pr_err("%s: Cannot use %s, no address set, aborting\n",
	       print_name, mmio ? "MMIO" : "I/O");
	pr_err("%s: Recompile driver with \"CONFIG_DEFXX_MMIO=%c\"\n",
	       print_name, mmio ? 'n' : 'y');
	if (eisa && mmio)
		pr_err("%s: Or run ECU and set adapter's MMIO location\n",
		       print_name);
}

static void dfx_register_res_err(const char *print_name, bool mmio,
				 unsigned long start, unsigned long len)
{
	pr_err("%s: Cannot reserve %s resource 0x%lx @ 0x%lx, aborting\n",
	       print_name, mmio ? "MMIO" : "I/O", len, start);
}

/*
 * ================
 * = dfx_register =
 * ================
 *
 * Overview:
 *   Initializes a supported FDDI controller
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bdev - pointer to device information
 *
 * Functional Description:
 *
 * Return Codes:
 *   0		 - This device (fddi0, fddi1, etc) configured successfully
 *   -EBUSY      - Failed to get resources, or dfx_driver_init failed.
 *
 * Assumptions:
 *   It compiles so it should work :-( (PCI cards do :-)
 *
 * Side Effects:
 *   Device structures for FDDI adapters (fddi0, fddi1, etc) are
 *   initialized and the board resources are read and stored in
 *   the device structure.
 */
static int dfx_register(struct device *bdev)
{
	static int version_disp;
	int dfx_bus_pci = dev_is_pci(bdev);
	int dfx_bus_eisa = DFX_BUS_EISA(bdev);
	int dfx_bus_tc = DFX_BUS_TC(bdev);
	int dfx_use_mmio = DFX_MMIO || dfx_bus_tc;
	const char *print_name = dev_name(bdev);
	struct net_device *dev;
	DFX_board_t	  *bp;			/* board pointer */
	resource_size_t bar_start[3] = {0};	/* pointers to ports */
	resource_size_t bar_len[3] = {0};	/* resource length */
	int alloc_size;				/* total buffer size used */
	struct resource *region;
	int err = 0;

	if (!version_disp) {	/* display version info if adapter is found */
		version_disp = 1;	/* set display flag to TRUE so that */
		printk(version);	/* we only display this string ONCE */
	}

	dev = alloc_fddidev(sizeof(*bp));
	if (!dev) {
		printk(KERN_ERR "%s: Unable to allocate fddidev, aborting\n",
		       print_name);
		return -ENOMEM;
	}

	/* Enable PCI device. */
	if (dfx_bus_pci) {
		err = pci_enable_device(to_pci_dev(bdev));
		if (err) {
			pr_err("%s: Cannot enable PCI device, aborting\n",
			       print_name);
			goto err_out;
		}
	}

	SET_NETDEV_DEV(dev, bdev);

	bp = netdev_priv(dev);
	bp->bus_dev = bdev;
	dev_set_drvdata(bdev, dev);

	dfx_get_bars(bdev, bar_start, bar_len);
	if (bar_len[0] == 0 ||
	    (dfx_bus_eisa && dfx_use_mmio && bar_start[0] == 0)) {
		dfx_register_res_alloc_err(print_name, dfx_use_mmio,
					   dfx_bus_eisa);
		err = -ENXIO;
		goto err_out_disable;
	}

	if (dfx_use_mmio)
		region = request_mem_region(bar_start[0], bar_len[0],
					    print_name);
	else
		region = request_region(bar_start[0], bar_len[0], print_name);
	if (!region) {
		dfx_register_res_err(print_name, dfx_use_mmio,
				     bar_start[0], bar_len[0]);
		err = -EBUSY;
		goto err_out_disable;
	}
	if (bar_start[1] != 0) {
		region = request_region(bar_start[1], bar_len[1], print_name);
		if (!region) {
			dfx_register_res_err(print_name, 0,
					     bar_start[1], bar_len[1]);
			err = -EBUSY;
			goto err_out_csr_region;
		}
	}
	if (bar_start[2] != 0) {
		region = request_region(bar_start[2], bar_len[2], print_name);
		if (!region) {
			dfx_register_res_err(print_name, 0,
					     bar_start[2], bar_len[2]);
			err = -EBUSY;
			goto err_out_bh_region;
		}
	}

	/* Set up I/O base address. */
	if (dfx_use_mmio) {
		bp->base.mem = ioremap(bar_start[0], bar_len[0]);
		if (!bp->base.mem) {
			printk(KERN_ERR "%s: Cannot map MMIO\n", print_name);
			err = -ENOMEM;
			goto err_out_esic_region;
		}
	} else {
		bp->base.port = bar_start[0];
		dev->base_addr = bar_start[0];
	}

	/* Initialize new device structure */
	dev->netdev_ops			= &dfx_netdev_ops;

	if (dfx_bus_pci)
		pci_set_master(to_pci_dev(bdev));

	if (dfx_driver_init(dev, print_name, bar_start[0]) != DFX_K_SUCCESS) {
		err = -ENODEV;
		goto err_out_unmap;
	}

	err = register_netdev(dev);
	if (err)
		goto err_out_kfree;

	printk("%s: registered as %s\n", print_name, dev->name);
	return 0;

err_out_kfree:
	alloc_size = sizeof(PI_DESCR_BLOCK) +
		     PI_CMD_REQ_K_SIZE_MAX + PI_CMD_RSP_K_SIZE_MAX +
#ifndef DYNAMIC_BUFFERS
		     (bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX) +
#endif
		     sizeof(PI_CONSUMER_BLOCK) +
		     (PI_ALIGN_K_DESC_BLK - 1);
	if (bp->kmalloced)
		dma_free_coherent(bdev, alloc_size,
				  bp->kmalloced, bp->kmalloced_dma);

err_out_unmap:
	if (dfx_use_mmio)
		iounmap(bp->base.mem);

err_out_esic_region:
	if (bar_start[2] != 0)
		release_region(bar_start[2], bar_len[2]);

err_out_bh_region:
	if (bar_start[1] != 0)
		release_region(bar_start[1], bar_len[1]);

err_out_csr_region:
	if (dfx_use_mmio)
		release_mem_region(bar_start[0], bar_len[0]);
	else
		release_region(bar_start[0], bar_len[0]);

err_out_disable:
	if (dfx_bus_pci)
		pci_disable_device(to_pci_dev(bdev));

err_out:
	free_netdev(dev);
	return err;
}


/*
 * ================
 * = dfx_bus_init =
 * ================
 *
 * Overview:
 *   Initializes the bus-specific controller logic.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   Determine and save adapter IRQ in device table,
 *   then perform bus-specific logic initialization.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   bp->base has already been set with the proper
 *	 base I/O address for this device.
 *
 * Side Effects:
 *   Interrupts are enabled at the adapter bus-specific logic.
 *   Note:  Interrupts at the DMA engine (PDQ chip) are not
 *   enabled yet.
 */

static void dfx_bus_init(struct net_device *dev)
{
	DFX_board_t *bp = netdev_priv(dev);
	struct device *bdev = bp->bus_dev;
	int dfx_bus_pci = dev_is_pci(bdev);
	int dfx_bus_eisa = DFX_BUS_EISA(bdev);
	int dfx_bus_tc = DFX_BUS_TC(bdev);
	int dfx_use_mmio = DFX_MMIO || dfx_bus_tc;
	u8 val;

	DBG_printk("In dfx_bus_init...\n");

	/* Initialize a pointer back to the net_device struct */
	bp->dev = dev;

	/* Initialize adapter based on bus type */

	if (dfx_bus_tc)
		dev->irq = to_tc_dev(bdev)->interrupt;
	if (dfx_bus_eisa) {
		unsigned long base_addr = to_eisa_device(bdev)->base_addr;

		/* Disable the board before fiddling with the decoders.  */
		outb(0, base_addr + PI_ESIC_K_SLOT_CNTRL);

		/* Get the interrupt level from the ESIC chip.  */
		val = inb(base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);
		val &= PI_CONFIG_STAT_0_M_IRQ;
		val >>= PI_CONFIG_STAT_0_V_IRQ;

		switch (val) {
		case PI_CONFIG_STAT_0_IRQ_K_9:
			dev->irq = 9;
			break;

		case PI_CONFIG_STAT_0_IRQ_K_10:
			dev->irq = 10;
			break;

		case PI_CONFIG_STAT_0_IRQ_K_11:
			dev->irq = 11;
			break;

		case PI_CONFIG_STAT_0_IRQ_K_15:
			dev->irq = 15;
			break;
		}

		/*
		 * Enable memory decoding (MEMCS1) and/or port decoding
		 * (IOCS1/IOCS0) as appropriate in Function Control
		 * Register.  MEMCS1 or IOCS0 is used for PDQ registers,
		 * taking 16 32-bit words, while IOCS1 is used for the
		 * Burst Holdoff register, taking a single 32-bit word
		 * only.  We use the slot-specific I/O range as per the
		 * ESIC spec, that is set bits 15:12 in the mask registers
		 * to mask them out.
		 */

		/* Set the decode range of the board.  */
		val = 0;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_CMP_0_1);
		val = PI_DEFEA_K_CSR_IO;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_CMP_0_0);

		val = PI_IO_CMP_M_SLOT;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_MASK_0_1);
		val = (PI_ESIC_K_CSR_IO_LEN - 1) & ~3;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_MASK_0_0);

		val = 0;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_CMP_1_1);
		val = PI_DEFEA_K_BURST_HOLDOFF;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_CMP_1_0);

		val = PI_IO_CMP_M_SLOT;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_MASK_1_1);
		val = (PI_ESIC_K_BURST_HOLDOFF_LEN - 1) & ~3;
		outb(val, base_addr + PI_ESIC_K_IO_ADD_MASK_1_0);

		/* Enable the decoders.  */
		val = PI_FUNCTION_CNTRL_M_IOCS1;
		if (dfx_use_mmio)
			val |= PI_FUNCTION_CNTRL_M_MEMCS1;
		else
			val |= PI_FUNCTION_CNTRL_M_IOCS0;
		outb(val, base_addr + PI_ESIC_K_FUNCTION_CNTRL);

		/*
		 * Enable access to the rest of the module
		 * (including PDQ and packet memory).
		 */
		val = PI_SLOT_CNTRL_M_ENB;
		outb(val, base_addr + PI_ESIC_K_SLOT_CNTRL);

		/*
		 * Map PDQ registers into memory or port space.  This is
		 * done with a bit in the Burst Holdoff register.
		 */
		val = inb(base_addr + PI_DEFEA_K_BURST_HOLDOFF);
		if (dfx_use_mmio)
			val |= PI_BURST_HOLDOFF_M_MEM_MAP;
		else
			val &= ~PI_BURST_HOLDOFF_M_MEM_MAP;
		outb(val, base_addr + PI_DEFEA_K_BURST_HOLDOFF);

		/* Enable interrupts at EISA bus interface chip (ESIC) */
		val = inb(base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);
		val |= PI_CONFIG_STAT_0_M_INT_ENB;
		outb(val, base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);
	}
	if (dfx_bus_pci) {
		struct pci_dev *pdev = to_pci_dev(bdev);

		/* Get the interrupt level from the PCI Configuration Table */

		dev->irq = pdev->irq;

		/* Check Latency Timer and set if less than minimal */

		pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &val);
		if (val < PFI_K_LAT_TIMER_MIN) {
			val = PFI_K_LAT_TIMER_DEF;
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER, val);
		}

		/* Enable interrupts at PCI bus interface chip (PFI) */
		val = PFI_MODE_M_PDQ_INT_ENB | PFI_MODE_M_DMA_ENB;
		dfx_port_write_long(bp, PFI_K_REG_MODE_CTRL, val);
	}
}

/*
 * ==================
 * = dfx_bus_uninit =
 * ==================
 *
 * Overview:
 *   Uninitializes the bus-specific controller logic.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   Perform bus-specific logic uninitialization.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   bp->base has already been set with the proper
 *	 base I/O address for this device.
 *
 * Side Effects:
 *   Interrupts are disabled at the adapter bus-specific logic.
 */

static void dfx_bus_uninit(struct net_device *dev)
{
	DFX_board_t *bp = netdev_priv(dev);
	struct device *bdev = bp->bus_dev;
	int dfx_bus_pci = dev_is_pci(bdev);
	int dfx_bus_eisa = DFX_BUS_EISA(bdev);
	u8 val;

	DBG_printk("In dfx_bus_uninit...\n");

	/* Uninitialize adapter based on bus type */

	if (dfx_bus_eisa) {
		unsigned long base_addr = to_eisa_device(bdev)->base_addr;

		/* Disable interrupts at EISA bus interface chip (ESIC) */
		val = inb(base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);
		val &= ~PI_CONFIG_STAT_0_M_INT_ENB;
		outb(val, base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);

		/* Disable the board.  */
		outb(0, base_addr + PI_ESIC_K_SLOT_CNTRL);

		/* Disable memory and port decoders.  */
		outb(0, base_addr + PI_ESIC_K_FUNCTION_CNTRL);
	}
	if (dfx_bus_pci) {
		/* Disable interrupts at PCI bus interface chip (PFI) */
		dfx_port_write_long(bp, PFI_K_REG_MODE_CTRL, 0);
	}
}


/*
 * ========================
 * = dfx_bus_config_check =
 * ========================
 *
 * Overview:
 *   Checks the configuration (burst size, full-duplex, etc.)  If any parameters
 *   are illegal, then this routine will set new defaults.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   For Revision 1 FDDI EISA, Revision 2 or later FDDI EISA with rev E or later
 *   PDQ, and all FDDI PCI controllers, all values are legal.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   dfx_adap_init has NOT been called yet so burst size and other items have
 *   not been set.
 *
 * Side Effects:
 *   None
 */

static void dfx_bus_config_check(DFX_board_t *bp)
{
	struct device __maybe_unused *bdev = bp->bus_dev;
	int dfx_bus_eisa = DFX_BUS_EISA(bdev);
	int	status;				/* return code from adapter port control call */
	u32	host_data;			/* LW data returned from port control call */

	DBG_printk("In dfx_bus_config_check...\n");

	/* Configuration check only valid for EISA adapter */

	if (dfx_bus_eisa) {
		/*
		 * First check if revision 2 EISA controller.  Rev. 1 cards used
		 * PDQ revision B, so no workaround needed in this case.  Rev. 3
		 * cards used PDQ revision E, so no workaround needed in this
		 * case, either.  Only Rev. 2 cards used either Rev. D or E
		 * chips, so we must verify the chip revision on Rev. 2 cards.
		 */
		if (to_eisa_device(bdev)->id.driver_data == DEFEA_PROD_ID_2) {
			/*
			 * Revision 2 FDDI EISA controller found,
			 * so let's check PDQ revision of adapter.
			 */
			status = dfx_hw_port_ctrl_req(bp,
											PI_PCTRL_M_SUB_CMD,
											PI_SUB_CMD_K_PDQ_REV_GET,
											0,
											&host_data);
			if ((status != DFX_K_SUCCESS) || (host_data == 2))
				{
				/*
				 * Either we couldn't determine the PDQ revision, or
				 * we determined that it is at revision D.  In either case,
				 * we need to implement the workaround.
				 */

				/* Ensure that the burst size is set to 8 longwords or less */

				switch (bp->burst_size)
					{
					case PI_PDATA_B_DMA_BURST_SIZE_32:
					case PI_PDATA_B_DMA_BURST_SIZE_16:
						bp->burst_size = PI_PDATA_B_DMA_BURST_SIZE_8;
						break;

					default:
						break;
					}

				/* Ensure that full-duplex mode is not enabled */

				bp->full_duplex_enb = PI_SNMP_K_FALSE;
				}
			}
		}
	}


/*
 * ===================
 * = dfx_driver_init =
 * ===================
 *
 * Overview:
 *   Initializes remaining adapter board structure information
 *   and makes sure adapter is in a safe state prior to dfx_open().
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   dev - pointer to device information
 *   print_name - printable device name
 *
 * Functional Description:
 *   This function allocates additional resources such as the host memory
 *   blocks needed by the adapter (eg. descriptor and consumer blocks).
 *	 Remaining bus initialization steps are also completed.  The adapter
 *   is also reset so that it is in the DMA_UNAVAILABLE state.  The OS
 *   must call dfx_open() to open the adapter and bring it on-line.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	- initialization succeeded
 *   DFX_K_FAILURE	- initialization failed - could not allocate memory
 *						or read adapter MAC address
 *
 * Assumptions:
 *   Memory allocated from pci_alloc_consistent() call is physically
 *   contiguous, locked memory.
 *
 * Side Effects:
 *   Adapter is reset and should be in DMA_UNAVAILABLE state before
 *   returning from this routine.
 */

static int dfx_driver_init(struct net_device *dev, const char *print_name,
			   resource_size_t bar_start)
{
	DFX_board_t *bp = netdev_priv(dev);
	struct device *bdev = bp->bus_dev;
	int dfx_bus_pci = dev_is_pci(bdev);
	int dfx_bus_eisa = DFX_BUS_EISA(bdev);
	int dfx_bus_tc = DFX_BUS_TC(bdev);
	int dfx_use_mmio = DFX_MMIO || dfx_bus_tc;
	int alloc_size;			/* total buffer size needed */
	char *top_v, *curr_v;		/* virtual addrs into memory block */
	dma_addr_t top_p, curr_p;	/* physical addrs into memory block */
	u32 data;			/* host data register value */
	__le32 le32;
	char *board_name = NULL;

	DBG_printk("In dfx_driver_init...\n");

	/* Initialize bus-specific hardware registers */

	dfx_bus_init(dev);

	/*
	 * Initialize default values for configurable parameters
	 *
	 * Note: All of these parameters are ones that a user may
	 *       want to customize.  It'd be nice to break these
	 *		 out into Space.c or someplace else that's more
	 *		 accessible/understandable than this file.
	 */

	bp->full_duplex_enb		= PI_SNMP_K_FALSE;
	bp->req_ttrt			= 8 * 12500;		/* 8ms in 80 nanosec units */
	bp->burst_size			= PI_PDATA_B_DMA_BURST_SIZE_DEF;
	bp->rcv_bufs_to_post	= RCV_BUFS_DEF;

	/*
	 * Ensure that HW configuration is OK
	 *
	 * Note: Depending on the hardware revision, we may need to modify
	 *       some of the configurable parameters to workaround hardware
	 *       limitations.  We'll perform this configuration check AFTER
	 *       setting the parameters to their default values.
	 */

	dfx_bus_config_check(bp);

	/* Disable PDQ interrupts first */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);

	/* Place adapter in DMA_UNAVAILABLE state by resetting adapter */

	(void) dfx_hw_dma_uninit(bp, PI_PDATA_A_RESET_M_SKIP_ST);

	/*  Read the factory MAC address from the adapter then save it */

	if (dfx_hw_port_ctrl_req(bp, PI_PCTRL_M_MLA, PI_PDATA_A_MLA_K_LO, 0,
				 &data) != DFX_K_SUCCESS) {
		printk("%s: Could not read adapter factory MAC address!\n",
		       print_name);
		return DFX_K_FAILURE;
	}
	le32 = cpu_to_le32(data);
	memcpy(&bp->factory_mac_addr[0], &le32, sizeof(u32));

	if (dfx_hw_port_ctrl_req(bp, PI_PCTRL_M_MLA, PI_PDATA_A_MLA_K_HI, 0,
				 &data) != DFX_K_SUCCESS) {
		printk("%s: Could not read adapter factory MAC address!\n",
		       print_name);
		return DFX_K_FAILURE;
	}
	le32 = cpu_to_le32(data);
	memcpy(&bp->factory_mac_addr[4], &le32, sizeof(u16));

	/*
	 * Set current address to factory address
	 *
	 * Note: Node address override support is handled through
	 *       dfx_ctl_set_mac_address.
	 */

	memcpy(dev->dev_addr, bp->factory_mac_addr, FDDI_K_ALEN);
	if (dfx_bus_tc)
		board_name = "DEFTA";
	if (dfx_bus_eisa)
		board_name = "DEFEA";
	if (dfx_bus_pci)
		board_name = "DEFPA";
	pr_info("%s: %s at %s addr = 0x%llx, IRQ = %d, Hardware addr = %pMF\n",
		print_name, board_name, dfx_use_mmio ? "MMIO" : "I/O",
		(long long)bar_start, dev->irq, dev->dev_addr);

	/*
	 * Get memory for descriptor block, consumer block, and other buffers
	 * that need to be DMA read or written to by the adapter.
	 */

	alloc_size = sizeof(PI_DESCR_BLOCK) +
					PI_CMD_REQ_K_SIZE_MAX +
					PI_CMD_RSP_K_SIZE_MAX +
#ifndef DYNAMIC_BUFFERS
					(bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX) +
#endif
					sizeof(PI_CONSUMER_BLOCK) +
					(PI_ALIGN_K_DESC_BLK - 1);
	bp->kmalloced = top_v = dma_alloc_coherent(bp->bus_dev, alloc_size,
						   &bp->kmalloced_dma,
						   GFP_ATOMIC);
	if (top_v == NULL)
		return DFX_K_FAILURE;

	top_p = bp->kmalloced_dma;	/* get physical address of buffer */

	/*
	 *  To guarantee the 8K alignment required for the descriptor block, 8K - 1
	 *  plus the amount of memory needed was allocated.  The physical address
	 *	is now 8K aligned.  By carving up the memory in a specific order,
	 *  we'll guarantee the alignment requirements for all other structures.
	 *
	 *  Note: If the assumptions change regarding the non-paged, non-cached,
	 *		  physically contiguous nature of the memory block or the address
	 *		  alignments, then we'll need to implement a different algorithm
	 *		  for allocating the needed memory.
	 */

	curr_p = ALIGN(top_p, PI_ALIGN_K_DESC_BLK);
	curr_v = top_v + (curr_p - top_p);

	/* Reserve space for descriptor block */

	bp->descr_block_virt = (PI_DESCR_BLOCK *) curr_v;
	bp->descr_block_phys = curr_p;
	curr_v += sizeof(PI_DESCR_BLOCK);
	curr_p += sizeof(PI_DESCR_BLOCK);

	/* Reserve space for command request buffer */

	bp->cmd_req_virt = (PI_DMA_CMD_REQ *) curr_v;
	bp->cmd_req_phys = curr_p;
	curr_v += PI_CMD_REQ_K_SIZE_MAX;
	curr_p += PI_CMD_REQ_K_SIZE_MAX;

	/* Reserve space for command response buffer */

	bp->cmd_rsp_virt = (PI_DMA_CMD_RSP *) curr_v;
	bp->cmd_rsp_phys = curr_p;
	curr_v += PI_CMD_RSP_K_SIZE_MAX;
	curr_p += PI_CMD_RSP_K_SIZE_MAX;

	/* Reserve space for the LLC host receive queue buffers */

	bp->rcv_block_virt = curr_v;
	bp->rcv_block_phys = curr_p;

#ifndef DYNAMIC_BUFFERS
	curr_v += (bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX);
	curr_p += (bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX);
#endif

	/* Reserve space for the consumer block */

	bp->cons_block_virt = (PI_CONSUMER_BLOCK *) curr_v;
	bp->cons_block_phys = curr_p;

	/* Display virtual and physical addresses if debug driver */

	DBG_printk("%s: Descriptor block virt = %p, phys = %pad\n",
		   print_name, bp->descr_block_virt, &bp->descr_block_phys);
	DBG_printk("%s: Command Request buffer virt = %p, phys = %pad\n",
		   print_name, bp->cmd_req_virt, &bp->cmd_req_phys);
	DBG_printk("%s: Command Response buffer virt = %p, phys = %pad\n",
		   print_name, bp->cmd_rsp_virt, &bp->cmd_rsp_phys);
	DBG_printk("%s: Receive buffer block virt = %p, phys = %pad\n",
		   print_name, bp->rcv_block_virt, &bp->rcv_block_phys);
	DBG_printk("%s: Consumer block virt = %p, phys = %pad\n",
		   print_name, bp->cons_block_virt, &bp->cons_block_phys);

	return DFX_K_SUCCESS;
}


/*
 * =================
 * = dfx_adap_init =
 * =================
 *
 * Overview:
 *   Brings the adapter to the link avail/link unavailable state.
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bp - pointer to board information
 *   get_buffers - non-zero if buffers to be allocated
 *
 * Functional Description:
 *   Issues the low-level firmware/hardware calls necessary to bring
 *   the adapter up, or to properly reset and restore adapter during
 *   run-time.
 *
 * Return Codes:
 *   DFX_K_SUCCESS - Adapter brought up successfully
 *   DFX_K_FAILURE - Adapter initialization failed
 *
 * Assumptions:
 *   bp->reset_type should be set to a valid reset type value before
 *   calling this routine.
 *
 * Side Effects:
 *   Adapter should be in LINK_AVAILABLE or LINK_UNAVAILABLE state
 *   upon a successful return of this routine.
 */

static int dfx_adap_init(DFX_board_t *bp, int get_buffers)
	{
	DBG_printk("In dfx_adap_init...\n");

	/* Disable PDQ interrupts first */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);

	/* Place adapter in DMA_UNAVAILABLE state by resetting adapter */

	if (dfx_hw_dma_uninit(bp, bp->reset_type) != DFX_K_SUCCESS)
		{
		printk("%s: Could not uninitialize/reset adapter!\n", bp->dev->name);
		return DFX_K_FAILURE;
		}

	/*
	 * When the PDQ is reset, some false Type 0 interrupts may be pending,
	 * so we'll acknowledge all Type 0 interrupts now before continuing.
	 */

	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_0_STATUS, PI_HOST_INT_K_ACK_ALL_TYPE_0);

	/*
	 * Clear Type 1 and Type 2 registers before going to DMA_AVAILABLE state
	 *
	 * Note: We only need to clear host copies of these registers.  The PDQ reset
	 *       takes care of the on-board register values.
	 */

	bp->cmd_req_reg.lword	= 0;
	bp->cmd_rsp_reg.lword	= 0;
	bp->rcv_xmt_reg.lword	= 0;

	/* Clear consumer block before going to DMA_AVAILABLE state */

	memset(bp->cons_block_virt, 0, sizeof(PI_CONSUMER_BLOCK));

	/* Initialize the DMA Burst Size */

	if (dfx_hw_port_ctrl_req(bp,
							PI_PCTRL_M_SUB_CMD,
							PI_SUB_CMD_K_BURST_SIZE_SET,
							bp->burst_size,
							NULL) != DFX_K_SUCCESS)
		{
		printk("%s: Could not set adapter burst size!\n", bp->dev->name);
		return DFX_K_FAILURE;
		}

	/*
	 * Set base address of Consumer Block
	 *
	 * Assumption: 32-bit physical address of consumer block is 64 byte
	 *			   aligned.  That is, bits 0-5 of the address must be zero.
	 */

	if (dfx_hw_port_ctrl_req(bp,
							PI_PCTRL_M_CONS_BLOCK,
							bp->cons_block_phys,
							0,
							NULL) != DFX_K_SUCCESS)
		{
		printk("%s: Could not set consumer block address!\n", bp->dev->name);
		return DFX_K_FAILURE;
		}

	/*
	 * Set the base address of Descriptor Block and bring adapter
	 * to DMA_AVAILABLE state.
	 *
	 * Note: We also set the literal and data swapping requirements
	 *       in this command.
	 *
	 * Assumption: 32-bit physical address of descriptor block
	 *       is 8Kbyte aligned.
	 */
	if (dfx_hw_port_ctrl_req(bp, PI_PCTRL_M_INIT,
				 (u32)(bp->descr_block_phys |
				       PI_PDATA_A_INIT_M_BSWAP_INIT),
				 0, NULL) != DFX_K_SUCCESS) {
		printk("%s: Could not set descriptor block address!\n",
		       bp->dev->name);
		return DFX_K_FAILURE;
	}

	/* Set transmit flush timeout value */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_CHARS_SET;
	bp->cmd_req_virt->char_set.item[0].item_code	= PI_ITEM_K_FLUSH_TIME;
	bp->cmd_req_virt->char_set.item[0].value		= 3;	/* 3 seconds */
	bp->cmd_req_virt->char_set.item[0].item_index	= 0;
	bp->cmd_req_virt->char_set.item[1].item_code	= PI_ITEM_K_EOL;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		{
		printk("%s: DMA command request failed!\n", bp->dev->name);
		return DFX_K_FAILURE;
		}

	/* Set the initial values for eFDXEnable and MACTReq MIB objects */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_SNMP_SET;
	bp->cmd_req_virt->snmp_set.item[0].item_code	= PI_ITEM_K_FDX_ENB_DIS;
	bp->cmd_req_virt->snmp_set.item[0].value		= bp->full_duplex_enb;
	bp->cmd_req_virt->snmp_set.item[0].item_index	= 0;
	bp->cmd_req_virt->snmp_set.item[1].item_code	= PI_ITEM_K_MAC_T_REQ;
	bp->cmd_req_virt->snmp_set.item[1].value		= bp->req_ttrt;
	bp->cmd_req_virt->snmp_set.item[1].item_index	= 0;
	bp->cmd_req_virt->snmp_set.item[2].item_code	= PI_ITEM_K_EOL;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		{
		printk("%s: DMA command request failed!\n", bp->dev->name);
		return DFX_K_FAILURE;
		}

	/* Initialize adapter CAM */

	if (dfx_ctl_update_cam(bp) != DFX_K_SUCCESS)
		{
		printk("%s: Adapter CAM update failed!\n", bp->dev->name);
		return DFX_K_FAILURE;
		}

	/* Initialize adapter filters */

	if (dfx_ctl_update_filters(bp) != DFX_K_SUCCESS)
		{
		printk("%s: Adapter filters update failed!\n", bp->dev->name);
		return DFX_K_FAILURE;
		}

	/*
	 * Remove any existing dynamic buffers (i.e. if the adapter is being
	 * reinitialized)
	 */

	if (get_buffers)
		dfx_rcv_flush(bp);

	/* Initialize receive descriptor block and produce buffers */

	if (dfx_rcv_init(bp, get_buffers))
	        {
		printk("%s: Receive buffer allocation failed\n", bp->dev->name);
		if (get_buffers)
			dfx_rcv_flush(bp);
		return DFX_K_FAILURE;
		}

	/* Issue START command and bring adapter to LINK_(UN)AVAILABLE state */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_START;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		{
		printk("%s: Start command failed\n", bp->dev->name);
		if (get_buffers)
			dfx_rcv_flush(bp);
		return DFX_K_FAILURE;
		}

	/* Initialization succeeded, reenable PDQ interrupts */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_ENABLE_DEF_INTS);
	return DFX_K_SUCCESS;
	}


/*
 * ============
 * = dfx_open =
 * ============
 *
 * Overview:
 *   Opens the adapter
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This function brings the adapter to an operational state.
 *
 * Return Codes:
 *   0		 - Adapter was successfully opened
 *   -EAGAIN - Could not register IRQ or adapter initialization failed
 *
 * Assumptions:
 *   This routine should only be called for a device that was
 *   initialized successfully.
 *
 * Side Effects:
 *   Adapter should be in LINK_AVAILABLE or LINK_UNAVAILABLE state
 *   if the open is successful.
 */

static int dfx_open(struct net_device *dev)
{
	DFX_board_t *bp = netdev_priv(dev);
	int ret;

	DBG_printk("In dfx_open...\n");

	/* Register IRQ - support shared interrupts by passing device ptr */

	ret = request_irq(dev->irq, dfx_interrupt, IRQF_SHARED, dev->name,
			  dev);
	if (ret) {
		printk(KERN_ERR "%s: Requested IRQ %d is busy\n", dev->name, dev->irq);
		return ret;
	}

	/*
	 * Set current address to factory MAC address
	 *
	 * Note: We've already done this step in dfx_driver_init.
	 *       However, it's possible that a user has set a node
	 *		 address override, then closed and reopened the
	 *		 adapter.  Unless we reset the device address field
	 *		 now, we'll continue to use the existing modified
	 *		 address.
	 */

	memcpy(dev->dev_addr, bp->factory_mac_addr, FDDI_K_ALEN);

	/* Clear local unicast/multicast address tables and counts */

	memset(bp->uc_table, 0, sizeof(bp->uc_table));
	memset(bp->mc_table, 0, sizeof(bp->mc_table));
	bp->uc_count = 0;
	bp->mc_count = 0;

	/* Disable promiscuous filter settings */

	bp->ind_group_prom	= PI_FSTATE_K_BLOCK;
	bp->group_prom		= PI_FSTATE_K_BLOCK;

	spin_lock_init(&bp->lock);

	/* Reset and initialize adapter */

	bp->reset_type = PI_PDATA_A_RESET_M_SKIP_ST;	/* skip self-test */
	if (dfx_adap_init(bp, 1) != DFX_K_SUCCESS)
	{
		printk(KERN_ERR "%s: Adapter open failed!\n", dev->name);
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}

	/* Set device structure info */
	netif_start_queue(dev);
	return 0;
}


/*
 * =============
 * = dfx_close =
 * =============
 *
 * Overview:
 *   Closes the device/module.
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This routine closes the adapter and brings it to a safe state.
 *   The interrupt service routine is deregistered with the OS.
 *   The adapter can be opened again with another call to dfx_open().
 *
 * Return Codes:
 *   Always return 0.
 *
 * Assumptions:
 *   No further requests for this adapter are made after this routine is
 *   called.  dfx_open() can be called to reset and reinitialize the
 *   adapter.
 *
 * Side Effects:
 *   Adapter should be in DMA_UNAVAILABLE state upon completion of this
 *   routine.
 */

static int dfx_close(struct net_device *dev)
{
	DFX_board_t *bp = netdev_priv(dev);

	DBG_printk("In dfx_close...\n");

	/* Disable PDQ interrupts first */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);

	/* Place adapter in DMA_UNAVAILABLE state by resetting adapter */

	(void) dfx_hw_dma_uninit(bp, PI_PDATA_A_RESET_M_SKIP_ST);

	/*
	 * Flush any pending transmit buffers
	 *
	 * Note: It's important that we flush the transmit buffers
	 *		 BEFORE we clear our copy of the Type 2 register.
	 *		 Otherwise, we'll have no idea how many buffers
	 *		 we need to free.
	 */

	dfx_xmt_flush(bp);

	/*
	 * Clear Type 1 and Type 2 registers after adapter reset
	 *
	 * Note: Even though we're closing the adapter, it's
	 *       possible that an interrupt will occur after
	 *		 dfx_close is called.  Without some assurance to
	 *		 the contrary we want to make sure that we don't
	 *		 process receive and transmit LLC frames and update
	 *		 the Type 2 register with bad information.
	 */

	bp->cmd_req_reg.lword	= 0;
	bp->cmd_rsp_reg.lword	= 0;
	bp->rcv_xmt_reg.lword	= 0;

	/* Clear consumer block for the same reason given above */

	memset(bp->cons_block_virt, 0, sizeof(PI_CONSUMER_BLOCK));

	/* Release all dynamically allocate skb in the receive ring. */

	dfx_rcv_flush(bp);

	/* Clear device structure flags */

	netif_stop_queue(dev);

	/* Deregister (free) IRQ */

	free_irq(dev->irq, dev);

	return 0;
}


/*
 * ======================
 * = dfx_int_pr_halt_id =
 * ======================
 *
 * Overview:
 *   Displays halt id's in string form.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Determine current halt id and display appropriate string.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

static void dfx_int_pr_halt_id(DFX_board_t	*bp)
	{
	PI_UINT32	port_status;			/* PDQ port status register value */
	PI_UINT32	halt_id;				/* PDQ port status halt ID */

	/* Read the latest port status */

	dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_STATUS, &port_status);

	/* Display halt state transition information */

	halt_id = (port_status & PI_PSTATUS_M_HALT_ID) >> PI_PSTATUS_V_HALT_ID;
	switch (halt_id)
		{
		case PI_HALT_ID_K_SELFTEST_TIMEOUT:
			printk("%s: Halt ID: Selftest Timeout\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_PARITY_ERROR:
			printk("%s: Halt ID: Host Bus Parity Error\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_HOST_DIR_HALT:
			printk("%s: Halt ID: Host-Directed Halt\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_SW_FAULT:
			printk("%s: Halt ID: Adapter Software Fault\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_HW_FAULT:
			printk("%s: Halt ID: Adapter Hardware Fault\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_PC_TRACE:
			printk("%s: Halt ID: FDDI Network PC Trace Path Test\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_DMA_ERROR:
			printk("%s: Halt ID: Adapter DMA Error\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_IMAGE_CRC_ERROR:
			printk("%s: Halt ID: Firmware Image CRC Error\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_BUS_EXCEPTION:
			printk("%s: Halt ID: 68000 Bus Exception\n", bp->dev->name);
			break;

		default:
			printk("%s: Halt ID: Unknown (code = %X)\n", bp->dev->name, halt_id);
			break;
		}
	}


/*
 * ==========================
 * = dfx_int_type_0_process =
 * ==========================
 *
 * Overview:
 *   Processes Type 0 interrupts.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Processes all enabled Type 0 interrupts.  If the reason for the interrupt
 *   is a serious fault on the adapter, then an error message is displayed
 *   and the adapter is reset.
 *
 *   One tricky potential timing window is the rapid succession of "link avail"
 *   "link unavail" state change interrupts.  The acknowledgement of the Type 0
 *   interrupt must be done before reading the state from the Port Status
 *   register.  This is true because a state change could occur after reading
 *   the data, but before acknowledging the interrupt.  If this state change
 *   does happen, it would be lost because the driver is using the old state,
 *   and it will never know about the new state because it subsequently
 *   acknowledges the state change interrupt.
 *
 *          INCORRECT                                      CORRECT
 *      read type 0 int reasons                   read type 0 int reasons
 *      read adapter state                        ack type 0 interrupts
 *      ack type 0 interrupts                     read adapter state
 *      ... process interrupt ...                 ... process interrupt ...
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   An adapter reset may occur if the adapter has any Type 0 error interrupts
 *   or if the port status indicates that the adapter is halted.  The driver
 *   is responsible for reinitializing the adapter with the current CAM
 *   contents and adapter filter settings.
 */

static void dfx_int_type_0_process(DFX_board_t	*bp)

	{
	PI_UINT32	type_0_status;		/* Host Interrupt Type 0 register */
	PI_UINT32	state;				/* current adap state (from port status) */

	/*
	 * Read host interrupt Type 0 register to determine which Type 0
	 * interrupts are pending.  Immediately write it back out to clear
	 * those interrupts.
	 */

	dfx_port_read_long(bp, PI_PDQ_K_REG_TYPE_0_STATUS, &type_0_status);
	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_0_STATUS, type_0_status);

	/* Check for Type 0 error interrupts */

	if (type_0_status & (PI_TYPE_0_STAT_M_NXM |
							PI_TYPE_0_STAT_M_PM_PAR_ERR |
							PI_TYPE_0_STAT_M_BUS_PAR_ERR))
		{
		/* Check for Non-Existent Memory error */

		if (type_0_status & PI_TYPE_0_STAT_M_NXM)
			printk("%s: Non-Existent Memory Access Error\n", bp->dev->name);

		/* Check for Packet Memory Parity error */

		if (type_0_status & PI_TYPE_0_STAT_M_PM_PAR_ERR)
			printk("%s: Packet Memory Parity Error\n", bp->dev->name);

		/* Check for Host Bus Parity error */

		if (type_0_status & PI_TYPE_0_STAT_M_BUS_PAR_ERR)
			printk("%s: Host Bus Parity Error\n", bp->dev->name);

		/* Reset adapter and bring it back on-line */

		bp->link_available = PI_K_FALSE;	/* link is no longer available */
		bp->reset_type = 0;					/* rerun on-board diagnostics */
		printk("%s: Resetting adapter...\n", bp->dev->name);
		if (dfx_adap_init(bp, 0) != DFX_K_SUCCESS)
			{
			printk("%s: Adapter reset failed!  Disabling adapter interrupts.\n", bp->dev->name);
			dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);
			return;
			}
		printk("%s: Adapter reset successful!\n", bp->dev->name);
		return;
		}

	/* Check for transmit flush interrupt */

	if (type_0_status & PI_TYPE_0_STAT_M_XMT_FLUSH)
		{
		/* Flush any pending xmt's and acknowledge the flush interrupt */

		bp->link_available = PI_K_FALSE;		/* link is no longer available */
		dfx_xmt_flush(bp);						/* flush any outstanding packets */
		(void) dfx_hw_port_ctrl_req(bp,
									PI_PCTRL_M_XMT_DATA_FLUSH_DONE,
									0,
									0,
									NULL);
		}

	/* Check for adapter state change */

	if (type_0_status & PI_TYPE_0_STAT_M_STATE_CHANGE)
		{
		/* Get latest adapter state */

		state = dfx_hw_adap_state_rd(bp);	/* get adapter state */
		if (state == PI_STATE_K_HALTED)
			{
			/*
			 * Adapter has transitioned to HALTED state, try to reset
			 * adapter to bring it back on-line.  If reset fails,
			 * leave the adapter in the broken state.
			 */

			printk("%s: Controller has transitioned to HALTED state!\n", bp->dev->name);
			dfx_int_pr_halt_id(bp);			/* display halt id as string */

			/* Reset adapter and bring it back on-line */

			bp->link_available = PI_K_FALSE;	/* link is no longer available */
			bp->reset_type = 0;					/* rerun on-board diagnostics */
			printk("%s: Resetting adapter...\n", bp->dev->name);
			if (dfx_adap_init(bp, 0) != DFX_K_SUCCESS)
				{
				printk("%s: Adapter reset failed!  Disabling adapter interrupts.\n", bp->dev->name);
				dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);
				return;
				}
			printk("%s: Adapter reset successful!\n", bp->dev->name);
			}
		else if (state == PI_STATE_K_LINK_AVAIL)
			{
			bp->link_available = PI_K_TRUE;		/* set link available flag */
			}
		}
	}


/*
 * ==================
 * = dfx_int_common =
 * ==================
 *
 * Overview:
 *   Interrupt service routine (ISR)
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   This is the ISR which processes incoming adapter interrupts.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   This routine assumes PDQ interrupts have not been disabled.
 *   When interrupts are disabled at the PDQ, the Port Status register
 *   is automatically cleared.  This routine uses the Port Status
 *   register value to determine whether a Type 0 interrupt occurred,
 *   so it's important that adapter interrupts are not normally
 *   enabled/disabled at the PDQ.
 *
 *   It's vital that this routine is NOT reentered for the
 *   same board and that the OS is not in another section of
 *   code (eg. dfx_xmt_queue_pkt) for the same board on a
 *   different thread.
 *
 * Side Effects:
 *   Pending interrupts are serviced.  Depending on the type of
 *   interrupt, acknowledging and clearing the interrupt at the
 *   PDQ involves writing a register to clear the interrupt bit
 *   or updating completion indices.
 */

static void dfx_int_common(struct net_device *dev)
{
	DFX_board_t *bp = netdev_priv(dev);
	PI_UINT32	port_status;		/* Port Status register */

	/* Process xmt interrupts - frequent case, so always call this routine */

	if(dfx_xmt_done(bp))				/* free consumed xmt packets */
		netif_wake_queue(dev);

	/* Process rcv interrupts - frequent case, so always call this routine */

	dfx_rcv_queue_process(bp);		/* service received LLC frames */

	/*
	 * Transmit and receive producer and completion indices are updated on the
	 * adapter by writing to the Type 2 Producer register.  Since the frequent
	 * case is that we'll be processing either LLC transmit or receive buffers,
	 * we'll optimize I/O writes by doing a single register write here.
	 */

	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_2_PROD, bp->rcv_xmt_reg.lword);

	/* Read PDQ Port Status register to find out which interrupts need processing */

	dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_STATUS, &port_status);

	/* Process Type 0 interrupts (if any) - infrequent, so only call when needed */

	if (port_status & PI_PSTATUS_M_TYPE_0_PENDING)
		dfx_int_type_0_process(bp);	/* process Type 0 interrupts */
	}


/*
 * =================
 * = dfx_interrupt =
 * =================
 *
 * Overview:
 *   Interrupt processing routine
 *
 * Returns:
 *   Whether a valid interrupt was seen.
 *
 * Arguments:
 *   irq	- interrupt vector
 *   dev_id	- pointer to device information
 *
 * Functional Description:
 *   This routine calls the interrupt processing routine for this adapter.  It
 *   disables and reenables adapter interrupts, as appropriate.  We can support
 *   shared interrupts since the incoming dev_id pointer provides our device
 *   structure context.
 *
 * Return Codes:
 *   IRQ_HANDLED - an IRQ was handled.
 *   IRQ_NONE    - no IRQ was handled.
 *
 * Assumptions:
 *   The interrupt acknowledgement at the hardware level (eg. ACKing the PIC
 *   on Intel-based systems) is done by the operating system outside this
 *   routine.
 *
 *	 System interrupts are enabled through this call.
 *
 * Side Effects:
 *   Interrupts are disabled, then reenabled at the adapter.
 */

static irqreturn_t dfx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	DFX_board_t *bp = netdev_priv(dev);
	struct device *bdev = bp->bus_dev;
	int dfx_bus_pci = dev_is_pci(bdev);
	int dfx_bus_eisa = DFX_BUS_EISA(bdev);
	int dfx_bus_tc = DFX_BUS_TC(bdev);

	/* Service adapter interrupts */

	if (dfx_bus_pci) {
		u32 status;

		dfx_port_read_long(bp, PFI_K_REG_STATUS, &status);
		if (!(status & PFI_STATUS_M_PDQ_INT))
			return IRQ_NONE;

		spin_lock(&bp->lock);

		/* Disable PDQ-PFI interrupts at PFI */
		dfx_port_write_long(bp, PFI_K_REG_MODE_CTRL,
				    PFI_MODE_M_DMA_ENB);

		/* Call interrupt service routine for this adapter */
		dfx_int_common(dev);

		/* Clear PDQ interrupt status bit and reenable interrupts */
		dfx_port_write_long(bp, PFI_K_REG_STATUS,
				    PFI_STATUS_M_PDQ_INT);
		dfx_port_write_long(bp, PFI_K_REG_MODE_CTRL,
				    (PFI_MODE_M_PDQ_INT_ENB |
				     PFI_MODE_M_DMA_ENB));

		spin_unlock(&bp->lock);
	}
	if (dfx_bus_eisa) {
		unsigned long base_addr = to_eisa_device(bdev)->base_addr;
		u8 status;

		status = inb(base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);
		if (!(status & PI_CONFIG_STAT_0_M_PEND))
			return IRQ_NONE;

		spin_lock(&bp->lock);

		/* Disable interrupts at the ESIC */
		status &= ~PI_CONFIG_STAT_0_M_INT_ENB;
		outb(status, base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);

		/* Call interrupt service routine for this adapter */
		dfx_int_common(dev);

		/* Reenable interrupts at the ESIC */
		status = inb(base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);
		status |= PI_CONFIG_STAT_0_M_INT_ENB;
		outb(status, base_addr + PI_ESIC_K_IO_CONFIG_STAT_0);

		spin_unlock(&bp->lock);
	}
	if (dfx_bus_tc) {
		u32 status;

		dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_STATUS, &status);
		if (!(status & (PI_PSTATUS_M_RCV_DATA_PENDING |
				PI_PSTATUS_M_XMT_DATA_PENDING |
				PI_PSTATUS_M_SMT_HOST_PENDING |
				PI_PSTATUS_M_UNSOL_PENDING |
				PI_PSTATUS_M_CMD_RSP_PENDING |
				PI_PSTATUS_M_CMD_REQ_PENDING |
				PI_PSTATUS_M_TYPE_0_PENDING)))
			return IRQ_NONE;

		spin_lock(&bp->lock);

		/* Call interrupt service routine for this adapter */
		dfx_int_common(dev);

		spin_unlock(&bp->lock);
	}

	return IRQ_HANDLED;
}


/*
 * =====================
 * = dfx_ctl_get_stats =
 * =====================
 *
 * Overview:
 *   Get statistics for FDDI adapter
 *
 * Returns:
 *   Pointer to FDDI statistics structure
 *
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   Gets current MIB objects from adapter, then
 *   returns FDDI statistics structure as defined
 *   in if_fddi.h.
 *
 *   Note: Since the FDDI statistics structure is
 *   still new and the device structure doesn't
 *   have an FDDI-specific get statistics handler,
 *   we'll return the FDDI statistics structure as
 *   a pointer to an Ethernet statistics structure.
 *   That way, at least the first part of the statistics
 *   structure can be decoded properly, and it allows
 *   "smart" applications to perform a second cast to
 *   decode the FDDI-specific statistics.
 *
 *   We'll have to pay attention to this routine as the
 *   device structure becomes more mature and LAN media
 *   independent.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

static struct net_device_stats *dfx_ctl_get_stats(struct net_device *dev)
	{
	DFX_board_t *bp = netdev_priv(dev);

	/* Fill the bp->stats structure with driver-maintained counters */

	bp->stats.gen.rx_packets = bp->rcv_total_frames;
	bp->stats.gen.tx_packets = bp->xmt_total_frames;
	bp->stats.gen.rx_bytes   = bp->rcv_total_bytes;
	bp->stats.gen.tx_bytes   = bp->xmt_total_bytes;
	bp->stats.gen.rx_errors  = bp->rcv_crc_errors +
				   bp->rcv_frame_status_errors +
				   bp->rcv_length_errors;
	bp->stats.gen.tx_errors  = bp->xmt_length_errors;
	bp->stats.gen.rx_dropped = bp->rcv_discards;
	bp->stats.gen.tx_dropped = bp->xmt_discards;
	bp->stats.gen.multicast  = bp->rcv_multicast_frames;
	bp->stats.gen.collisions = 0;		/* always zero (0) for FDDI */

	/* Get FDDI SMT MIB objects */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_SMT_MIB_GET;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return (struct net_device_stats *)&bp->stats;

	/* Fill the bp->stats structure with the SMT MIB object values */

	memcpy(bp->stats.smt_station_id, &bp->cmd_rsp_virt->smt_mib_get.smt_station_id, sizeof(bp->cmd_rsp_virt->smt_mib_get.smt_station_id));
	bp->stats.smt_op_version_id					= bp->cmd_rsp_virt->smt_mib_get.smt_op_version_id;
	bp->stats.smt_hi_version_id					= bp->cmd_rsp_virt->smt_mib_get.smt_hi_version_id;
	bp->stats.smt_lo_version_id					= bp->cmd_rsp_virt->smt_mib_get.smt_lo_version_id;
	memcpy(bp->stats.smt_user_data, &bp->cmd_rsp_virt->smt_mib_get.smt_user_data, sizeof(bp->cmd_rsp_virt->smt_mib_get.smt_user_data));
	bp->stats.smt_mib_version_id				= bp->cmd_rsp_virt->smt_mib_get.smt_mib_version_id;
	bp->stats.smt_mac_cts						= bp->cmd_rsp_virt->smt_mib_get.smt_mac_ct;
	bp->stats.smt_non_master_cts				= bp->cmd_rsp_virt->smt_mib_get.smt_non_master_ct;
	bp->stats.smt_master_cts					= bp->cmd_rsp_virt->smt_mib_get.smt_master_ct;
	bp->stats.smt_available_paths				= bp->cmd_rsp_virt->smt_mib_get.smt_available_paths;
	bp->stats.smt_config_capabilities			= bp->cmd_rsp_virt->smt_mib_get.smt_config_capabilities;
	bp->stats.smt_config_policy					= bp->cmd_rsp_virt->smt_mib_get.smt_config_policy;
	bp->stats.smt_connection_policy				= bp->cmd_rsp_virt->smt_mib_get.smt_connection_policy;
	bp->stats.smt_t_notify						= bp->cmd_rsp_virt->smt_mib_get.smt_t_notify;
	bp->stats.smt_stat_rpt_policy				= bp->cmd_rsp_virt->smt_mib_get.smt_stat_rpt_policy;
	bp->stats.smt_trace_max_expiration			= bp->cmd_rsp_virt->smt_mib_get.smt_trace_max_expiration;
	bp->stats.smt_bypass_present				= bp->cmd_rsp_virt->smt_mib_get.smt_bypass_present;
	bp->stats.smt_ecm_state						= bp->cmd_rsp_virt->smt_mib_get.smt_ecm_state;
	bp->stats.smt_cf_state						= bp->cmd_rsp_virt->smt_mib_get.smt_cf_state;
	bp->stats.smt_remote_disconnect_flag		= bp->cmd_rsp_virt->smt_mib_get.smt_remote_disconnect_flag;
	bp->stats.smt_station_status				= bp->cmd_rsp_virt->smt_mib_get.smt_station_status;
	bp->stats.smt_peer_wrap_flag				= bp->cmd_rsp_virt->smt_mib_get.smt_peer_wrap_flag;
	bp->stats.smt_time_stamp					= bp->cmd_rsp_virt->smt_mib_get.smt_msg_time_stamp.ls;
	bp->stats.smt_transition_time_stamp			= bp->cmd_rsp_virt->smt_mib_get.smt_transition_time_stamp.ls;
	bp->stats.mac_frame_status_functions		= bp->cmd_rsp_virt->smt_mib_get.mac_frame_status_functions;
	bp->stats.mac_t_max_capability				= bp->cmd_rsp_virt->smt_mib_get.mac_t_max_capability;
	bp->stats.mac_tvx_capability				= bp->cmd_rsp_virt->smt_mib_get.mac_tvx_capability;
	bp->stats.mac_available_paths				= bp->cmd_rsp_virt->smt_mib_get.mac_available_paths;
	bp->stats.mac_current_path					= bp->cmd_rsp_virt->smt_mib_get.mac_current_path;
	memcpy(bp->stats.mac_upstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_upstream_nbr, FDDI_K_ALEN);
	memcpy(bp->stats.mac_downstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_downstream_nbr, FDDI_K_ALEN);
	memcpy(bp->stats.mac_old_upstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_old_upstream_nbr, FDDI_K_ALEN);
	memcpy(bp->stats.mac_old_downstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_old_downstream_nbr, FDDI_K_ALEN);
	bp->stats.mac_dup_address_test				= bp->cmd_rsp_virt->smt_mib_get.mac_dup_address_test;
	bp->stats.mac_requested_paths				= bp->cmd_rsp_virt->smt_mib_get.mac_requested_paths;
	bp->stats.mac_downstream_port_type			= bp->cmd_rsp_virt->smt_mib_get.mac_downstream_port_type;
	memcpy(bp->stats.mac_smt_address, &bp->cmd_rsp_virt->smt_mib_get.mac_smt_address, FDDI_K_ALEN);
	bp->stats.mac_t_req							= bp->cmd_rsp_virt->smt_mib_get.mac_t_req;
	bp->stats.mac_t_neg							= bp->cmd_rsp_virt->smt_mib_get.mac_t_neg;
	bp->stats.mac_t_max							= bp->cmd_rsp_virt->smt_mib_get.mac_t_max;
	bp->stats.mac_tvx_value						= bp->cmd_rsp_virt->smt_mib_get.mac_tvx_value;
	bp->stats.mac_frame_error_threshold			= bp->cmd_rsp_virt->smt_mib_get.mac_frame_error_threshold;
	bp->stats.mac_frame_error_ratio				= bp->cmd_rsp_virt->smt_mib_get.mac_frame_error_ratio;
	bp->stats.mac_rmt_state						= bp->cmd_rsp_virt->smt_mib_get.mac_rmt_state;
	bp->stats.mac_da_flag						= bp->cmd_rsp_virt->smt_mib_get.mac_da_flag;
	bp->stats.mac_una_da_flag					= bp->cmd_rsp_virt->smt_mib_get.mac_unda_flag;
	bp->stats.mac_frame_error_flag				= bp->cmd_rsp_virt->smt_mib_get.mac_frame_error_flag;
	bp->stats.mac_ma_unitdata_available			= bp->cmd_rsp_virt->smt_mib_get.mac_ma_unitdata_available;
	bp->stats.mac_hardware_present				= bp->cmd_rsp_virt->smt_mib_get.mac_hardware_present;
	bp->stats.mac_ma_unitdata_enable			= bp->cmd_rsp_virt->smt_mib_get.mac_ma_unitdata_enable;
	bp->stats.path_tvx_lower_bound				= bp->cmd_rsp_virt->smt_mib_get.path_tvx_lower_bound;
	bp->stats.path_t_max_lower_bound			= bp->cmd_rsp_virt->smt_mib_get.path_t_max_lower_bound;
	bp->stats.path_max_t_req					= bp->cmd_rsp_virt->smt_mib_get.path_max_t_req;
	memcpy(bp->stats.path_configuration, &bp->cmd_rsp_virt->smt_mib_get.path_configuration, sizeof(bp->cmd_rsp_virt->smt_mib_get.path_configuration));
	bp->stats.port_my_type[0]					= bp->cmd_rsp_virt->smt_mib_get.port_my_type[0];
	bp->stats.port_my_type[1]					= bp->cmd_rsp_virt->smt_mib_get.port_my_type[1];
	bp->stats.port_neighbor_type[0]				= bp->cmd_rsp_virt->smt_mib_get.port_neighbor_type[0];
	bp->stats.port_neighbor_type[1]				= bp->cmd_rsp_virt->smt_mib_get.port_neighbor_type[1];
	bp->stats.port_connection_policies[0]		= bp->cmd_rsp_virt->smt_mib_get.port_connection_policies[0];
	bp->stats.port_connection_policies[1]		= bp->cmd_rsp_virt->smt_mib_get.port_connection_policies[1];
	bp->stats.port_mac_indicated[0]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_indicated[0];
	bp->stats.port_mac_indicated[1]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_indicated[1];
	bp->stats.port_current_path[0]				= bp->cmd_rsp_virt->smt_mib_get.port_current_path[0];
	bp->stats.port_current_path[1]				= bp->cmd_rsp_virt->smt_mib_get.port_current_path[1];
	memcpy(&bp->stats.port_requested_paths[0*3], &bp->cmd_rsp_virt->smt_mib_get.port_requested_paths[0], 3);
	memcpy(&bp->stats.port_requested_paths[1*3], &bp->cmd_rsp_virt->smt_mib_get.port_requested_paths[1], 3);
	bp->stats.port_mac_placement[0]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_placement[0];
	bp->stats.port_mac_placement[1]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_placement[1];
	bp->stats.port_available_paths[0]			= bp->cmd_rsp_virt->smt_mib_get.port_available_paths[0];
	bp->stats.port_available_paths[1]			= bp->cmd_rsp_virt->smt_mib_get.port_available_paths[1];
	bp->stats.port_pmd_class[0]					= bp->cmd_rsp_virt->smt_mib_get.port_pmd_class[0];
	bp->stats.port_pmd_class[1]					= bp->cmd_rsp_virt->smt_mib_get.port_pmd_class[1];
	bp->stats.port_connection_capabilities[0]	= bp->cmd_rsp_virt->smt_mib_get.port_connection_capabilities[0];
	bp->stats.port_connection_capabilities[1]	= bp->cmd_rsp_virt->smt_mib_get.port_connection_capabilities[1];
	bp->stats.port_bs_flag[0]					= bp->cmd_rsp_virt->smt_mib_get.port_bs_flag[0];
	bp->stats.port_bs_flag[1]					= bp->cmd_rsp_virt->smt_mib_get.port_bs_flag[1];
	bp->stats.port_ler_estimate[0]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_estimate[0];
	bp->stats.port_ler_estimate[1]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_estimate[1];
	bp->stats.port_ler_cutoff[0]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_cutoff[0];
	bp->stats.port_ler_cutoff[1]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_cutoff[1];
	bp->stats.port_ler_alarm[0]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_alarm[0];
	bp->stats.port_ler_alarm[1]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_alarm[1];
	bp->stats.port_connect_state[0]				= bp->cmd_rsp_virt->smt_mib_get.port_connect_state[0];
	bp->stats.port_connect_state[1]				= bp->cmd_rsp_virt->smt_mib_get.port_connect_state[1];
	bp->stats.port_pcm_state[0]					= bp->cmd_rsp_virt->smt_mib_get.port_pcm_state[0];
	bp->stats.port_pcm_state[1]					= bp->cmd_rsp_virt->smt_mib_get.port_pcm_state[1];
	bp->stats.port_pc_withhold[0]				= bp->cmd_rsp_virt->smt_mib_get.port_pc_withhold[0];
	bp->stats.port_pc_withhold[1]				= bp->cmd_rsp_virt->smt_mib_get.port_pc_withhold[1];
	bp->stats.port_ler_flag[0]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_flag[0];
	bp->stats.port_ler_flag[1]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_flag[1];
	bp->stats.port_hardware_present[0]			= bp->cmd_rsp_virt->smt_mib_get.port_hardware_present[0];
	bp->stats.port_hardware_present[1]			= bp->cmd_rsp_virt->smt_mib_get.port_hardware_present[1];

	/* Get FDDI counters */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_CNTRS_GET;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return (struct net_device_stats *)&bp->stats;

	/* Fill the bp->stats structure with the FDDI counter values */

	bp->stats.mac_frame_cts				= bp->cmd_rsp_virt->cntrs_get.cntrs.frame_cnt.ls;
	bp->stats.mac_copied_cts			= bp->cmd_rsp_virt->cntrs_get.cntrs.copied_cnt.ls;
	bp->stats.mac_transmit_cts			= bp->cmd_rsp_virt->cntrs_get.cntrs.transmit_cnt.ls;
	bp->stats.mac_error_cts				= bp->cmd_rsp_virt->cntrs_get.cntrs.error_cnt.ls;
	bp->stats.mac_lost_cts				= bp->cmd_rsp_virt->cntrs_get.cntrs.lost_cnt.ls;
	bp->stats.port_lct_fail_cts[0]		= bp->cmd_rsp_virt->cntrs_get.cntrs.lct_rejects[0].ls;
	bp->stats.port_lct_fail_cts[1]		= bp->cmd_rsp_virt->cntrs_get.cntrs.lct_rejects[1].ls;
	bp->stats.port_lem_reject_cts[0]	= bp->cmd_rsp_virt->cntrs_get.cntrs.lem_rejects[0].ls;
	bp->stats.port_lem_reject_cts[1]	= bp->cmd_rsp_virt->cntrs_get.cntrs.lem_rejects[1].ls;
	bp->stats.port_lem_cts[0]			= bp->cmd_rsp_virt->cntrs_get.cntrs.link_errors[0].ls;
	bp->stats.port_lem_cts[1]			= bp->cmd_rsp_virt->cntrs_get.cntrs.link_errors[1].ls;

	return (struct net_device_stats *)&bp->stats;
	}


/*
 * ==============================
 * = dfx_ctl_set_multicast_list =
 * ==============================
 *
 * Overview:
 *   Enable/Disable LLC frame promiscuous mode reception
 *   on the adapter and/or update multicast address table.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This routine follows a fairly simple algorithm for setting the
 *   adapter filters and CAM:
 *
 *		if IFF_PROMISC flag is set
 *			enable LLC individual/group promiscuous mode
 *		else
 *			disable LLC individual/group promiscuous mode
 *			if number of incoming multicast addresses >
 *					(CAM max size - number of unicast addresses in CAM)
 *				enable LLC group promiscuous mode
 *				set driver-maintained multicast address count to zero
 *			else
 *				disable LLC group promiscuous mode
 *				set driver-maintained multicast address count to incoming count
 *			update adapter CAM
 *		update adapter filters
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   Multicast addresses are presented in canonical (LSB) format.
 *
 * Side Effects:
 *   On-board adapter CAM and filters are updated.
 */

static void dfx_ctl_set_multicast_list(struct net_device *dev)
{
	DFX_board_t *bp = netdev_priv(dev);
	int					i;			/* used as index in for loop */
	struct netdev_hw_addr *ha;

	/* Enable LLC frame promiscuous mode, if necessary */

	if (dev->flags & IFF_PROMISC)
		bp->ind_group_prom = PI_FSTATE_K_PASS;		/* Enable LLC ind/group prom mode */

	/* Else, update multicast address table */

	else
		{
		bp->ind_group_prom = PI_FSTATE_K_BLOCK;		/* Disable LLC ind/group prom mode */
		/*
		 * Check whether incoming multicast address count exceeds table size
		 *
		 * Note: The adapters utilize an on-board 64 entry CAM for
		 *       supporting perfect filtering of multicast packets
		 *		 and bridge functions when adding unicast addresses.
		 *		 There is no hash function available.  To support
		 *		 additional multicast addresses, the all multicast
		 *		 filter (LLC group promiscuous mode) must be enabled.
		 *
		 *		 The firmware reserves two CAM entries for SMT-related
		 *		 multicast addresses, which leaves 62 entries available.
		 *		 The following code ensures that we're not being asked
		 *		 to add more than 62 addresses to the CAM.  If we are,
		 *		 the driver will enable the all multicast filter.
		 *		 Should the number of multicast addresses drop below
		 *		 the high water mark, the filter will be disabled and
		 *		 perfect filtering will be used.
		 */

		if (netdev_mc_count(dev) > (PI_CMD_ADDR_FILTER_K_SIZE - bp->uc_count))
			{
			bp->group_prom	= PI_FSTATE_K_PASS;		/* Enable LLC group prom mode */
			bp->mc_count	= 0;					/* Don't add mc addrs to CAM */
			}
		else
			{
			bp->group_prom	= PI_FSTATE_K_BLOCK;	/* Disable LLC group prom mode */
			bp->mc_count	= netdev_mc_count(dev);		/* Add mc addrs to CAM */
			}

		/* Copy addresses to multicast address table, then update adapter CAM */

		i = 0;
		netdev_for_each_mc_addr(ha, dev)
			memcpy(&bp->mc_table[i++ * FDDI_K_ALEN],
			       ha->addr, FDDI_K_ALEN);

		if (dfx_ctl_update_cam(bp) != DFX_K_SUCCESS)
			{
			DBG_printk("%s: Could not update multicast address table!\n", dev->name);
			}
		else
			{
			DBG_printk("%s: Multicast address table updated!  Added %d addresses.\n", dev->name, bp->mc_count);
			}
		}

	/* Update adapter filters */

	if (dfx_ctl_update_filters(bp) != DFX_K_SUCCESS)
		{
		DBG_printk("%s: Could not update adapter filters!\n", dev->name);
		}
	else
		{
		DBG_printk("%s: Adapter filters updated!\n", dev->name);
		}
	}


/*
 * ===========================
 * = dfx_ctl_set_mac_address =
 * ===========================
 *
 * Overview:
 *   Add node address override (unicast address) to adapter
 *   CAM and update dev_addr field in device table.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   dev  - pointer to device information
 *   addr - pointer to sockaddr structure containing unicast address to add
 *
 * Functional Description:
 *   The adapter supports node address overrides by adding one or more
 *   unicast addresses to the adapter CAM.  This is similar to adding
 *   multicast addresses.  In this routine we'll update the driver and
 *   device structures with the new address, then update the adapter CAM
 *   to ensure that the adapter will copy and strip frames destined and
 *   sourced by that address.
 *
 * Return Codes:
 *   Always returns zero.
 *
 * Assumptions:
 *   The address pointed to by addr->sa_data is a valid unicast
 *   address and is presented in canonical (LSB) format.
 *
 * Side Effects:
 *   On-board adapter CAM is updated.  On-board adapter filters
 *   may be updated.
 */

static int dfx_ctl_set_mac_address(struct net_device *dev, void *addr)
	{
	struct sockaddr	*p_sockaddr = (struct sockaddr *)addr;
	DFX_board_t *bp = netdev_priv(dev);

	/* Copy unicast address to driver-maintained structs and update count */

	memcpy(dev->dev_addr, p_sockaddr->sa_data, FDDI_K_ALEN);	/* update device struct */
	memcpy(&bp->uc_table[0], p_sockaddr->sa_data, FDDI_K_ALEN);	/* update driver struct */
	bp->uc_count = 1;

	/*
	 * Verify we're not exceeding the CAM size by adding unicast address
	 *
	 * Note: It's possible that before entering this routine we've
	 *       already filled the CAM with 62 multicast addresses.
	 *		 Since we need to place the node address override into
	 *		 the CAM, we have to check to see that we're not
	 *		 exceeding the CAM size.  If we are, we have to enable
	 *		 the LLC group (multicast) promiscuous mode filter as
	 *		 in dfx_ctl_set_multicast_list.
	 */

	if ((bp->uc_count + bp->mc_count) > PI_CMD_ADDR_FILTER_K_SIZE)
		{
		bp->group_prom	= PI_FSTATE_K_PASS;		/* Enable LLC group prom mode */
		bp->mc_count	= 0;					/* Don't add mc addrs to CAM */

		/* Update adapter filters */

		if (dfx_ctl_update_filters(bp) != DFX_K_SUCCESS)
			{
			DBG_printk("%s: Could not update adapter filters!\n", dev->name);
			}
		else
			{
			DBG_printk("%s: Adapter filters updated!\n", dev->name);
			}
		}

	/* Update adapter CAM with new unicast address */

	if (dfx_ctl_update_cam(bp) != DFX_K_SUCCESS)
		{
		DBG_printk("%s: Could not set new MAC address!\n", dev->name);
		}
	else
		{
		DBG_printk("%s: Adapter CAM updated with new MAC address\n", dev->name);
		}
	return 0;			/* always return zero */
	}


/*
 * ======================
 * = dfx_ctl_update_cam =
 * ======================
 *
 * Overview:
 *   Procedure to update adapter CAM (Content Addressable Memory)
 *   with desired unicast and multicast address entries.
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Updates adapter CAM with current contents of board structure
 *   unicast and multicast address tables.  Since there are only 62
 *   free entries in CAM, this routine ensures that the command
 *   request buffer is not overrun.
 *
 * Return Codes:
 *   DFX_K_SUCCESS - Request succeeded
 *   DFX_K_FAILURE - Request failed
 *
 * Assumptions:
 *   All addresses being added (unicast and multicast) are in canonical
 *   order.
 *
 * Side Effects:
 *   On-board adapter CAM is updated.
 */

static int dfx_ctl_update_cam(DFX_board_t *bp)
	{
	int			i;				/* used as index */
	PI_LAN_ADDR	*p_addr;		/* pointer to CAM entry */

	/*
	 * Fill in command request information
	 *
	 * Note: Even though both the unicast and multicast address
	 *       table entries are stored as contiguous 6 byte entries,
	 *		 the firmware address filter set command expects each
	 *		 entry to be two longwords (8 bytes total).  We must be
	 *		 careful to only copy the six bytes of each unicast and
	 *		 multicast table entry into each command entry.  This
	 *		 is also why we must first clear the entire command
	 *		 request buffer.
	 */

	memset(bp->cmd_req_virt, 0, PI_CMD_REQ_K_SIZE_MAX);	/* first clear buffer */
	bp->cmd_req_virt->cmd_type = PI_CMD_K_ADDR_FILTER_SET;
	p_addr = &bp->cmd_req_virt->addr_filter_set.entry[0];

	/* Now add unicast addresses to command request buffer, if any */

	for (i=0; i < (int)bp->uc_count; i++)
		{
		if (i < PI_CMD_ADDR_FILTER_K_SIZE)
			{
			memcpy(p_addr, &bp->uc_table[i*FDDI_K_ALEN], FDDI_K_ALEN);
			p_addr++;			/* point to next command entry */
			}
		}

	/* Now add multicast addresses to command request buffer, if any */

	for (i=0; i < (int)bp->mc_count; i++)
		{
		if ((i + bp->uc_count) < PI_CMD_ADDR_FILTER_K_SIZE)
			{
			memcpy(p_addr, &bp->mc_table[i*FDDI_K_ALEN], FDDI_K_ALEN);
			p_addr++;			/* point to next command entry */
			}
		}

	/* Issue command to update adapter CAM, then return */

	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return DFX_K_FAILURE;
	return DFX_K_SUCCESS;
	}


/*
 * ==========================
 * = dfx_ctl_update_filters =
 * ==========================
 *
 * Overview:
 *   Procedure to update adapter filters with desired
 *   filter settings.
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Enables or disables filter using current filter settings.
 *
 * Return Codes:
 *   DFX_K_SUCCESS - Request succeeded.
 *   DFX_K_FAILURE - Request failed.
 *
 * Assumptions:
 *   We must always pass up packets destined to the broadcast
 *   address (FF-FF-FF-FF-FF-FF), so we'll always keep the
 *   broadcast filter enabled.
 *
 * Side Effects:
 *   On-board adapter filters are updated.
 */

static int dfx_ctl_update_filters(DFX_board_t *bp)
	{
	int	i = 0;					/* used as index */

	/* Fill in command request information */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_FILTERS_SET;

	/* Initialize Broadcast filter - * ALWAYS ENABLED * */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_BROADCAST;
	bp->cmd_req_virt->filter_set.item[i++].value	= PI_FSTATE_K_PASS;

	/* Initialize LLC Individual/Group Promiscuous filter */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_IND_GROUP_PROM;
	bp->cmd_req_virt->filter_set.item[i++].value	= bp->ind_group_prom;

	/* Initialize LLC Group Promiscuous filter */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_GROUP_PROM;
	bp->cmd_req_virt->filter_set.item[i++].value	= bp->group_prom;

	/* Terminate the item code list */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_EOL;

	/* Issue command to update adapter filters, then return */

	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return DFX_K_FAILURE;
	return DFX_K_SUCCESS;
	}


/*
 * ======================
 * = dfx_hw_dma_cmd_req =
 * ======================
 *
 * Overview:
 *   Sends PDQ DMA command to adapter firmware
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   The command request and response buffers are posted to the adapter in the manner
 *   described in the PDQ Port Specification:
 *
 *		1. Command Response Buffer is posted to adapter.
 *		2. Command Request Buffer is posted to adapter.
 *		3. Command Request consumer index is polled until it indicates that request
 *         buffer has been DMA'd to adapter.
 *		4. Command Response consumer index is polled until it indicates that response
 *         buffer has been DMA'd from adapter.
 *
 *   This ordering ensures that a response buffer is already available for the firmware
 *   to use once it's done processing the request buffer.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	  - DMA command succeeded
 * 	 DFX_K_OUTSTATE   - Adapter is NOT in proper state
 *   DFX_K_HW_TIMEOUT - DMA command timed out
 *
 * Assumptions:
 *   Command request buffer has already been filled with desired DMA command.
 *
 * Side Effects:
 *   None
 */

static int dfx_hw_dma_cmd_req(DFX_board_t *bp)
	{
	int status;			/* adapter status */
	int timeout_cnt;	/* used in for loops */

	/* Make sure the adapter is in a state that we can issue the DMA command in */

	status = dfx_hw_adap_state_rd(bp);
	if ((status == PI_STATE_K_RESET)		||
		(status == PI_STATE_K_HALTED)		||
		(status == PI_STATE_K_DMA_UNAVAIL)	||
		(status == PI_STATE_K_UPGRADE))
		return DFX_K_OUTSTATE;

	/* Put response buffer on the command response queue */

	bp->descr_block_virt->cmd_rsp[bp->cmd_rsp_reg.index.prod].long_0 = (u32) (PI_RCV_DESCR_M_SOP |
			((PI_CMD_RSP_K_SIZE_MAX / PI_ALIGN_K_CMD_RSP_BUFF) << PI_RCV_DESCR_V_SEG_LEN));
	bp->descr_block_virt->cmd_rsp[bp->cmd_rsp_reg.index.prod].long_1 = bp->cmd_rsp_phys;

	/* Bump (and wrap) the producer index and write out to register */

	bp->cmd_rsp_reg.index.prod += 1;
	bp->cmd_rsp_reg.index.prod &= PI_CMD_RSP_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_RSP_PROD, bp->cmd_rsp_reg.lword);

	/* Put request buffer on the command request queue */

	bp->descr_block_virt->cmd_req[bp->cmd_req_reg.index.prod].long_0 = (u32) (PI_XMT_DESCR_M_SOP |
			PI_XMT_DESCR_M_EOP | (PI_CMD_REQ_K_SIZE_MAX << PI_XMT_DESCR_V_SEG_LEN));
	bp->descr_block_virt->cmd_req[bp->cmd_req_reg.index.prod].long_1 = bp->cmd_req_phys;

	/* Bump (and wrap) the producer index and write out to register */

	bp->cmd_req_reg.index.prod += 1;
	bp->cmd_req_reg.index.prod &= PI_CMD_REQ_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_REQ_PROD, bp->cmd_req_reg.lword);

	/*
	 * Here we wait for the command request consumer index to be equal
	 * to the producer, indicating that the adapter has DMAed the request.
	 */

	for (timeout_cnt = 20000; timeout_cnt > 0; timeout_cnt--)
		{
		if (bp->cmd_req_reg.index.prod == (u8)(bp->cons_block_virt->cmd_req))
			break;
		udelay(100);			/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0)
		return DFX_K_HW_TIMEOUT;

	/* Bump (and wrap) the completion index and write out to register */

	bp->cmd_req_reg.index.comp += 1;
	bp->cmd_req_reg.index.comp &= PI_CMD_REQ_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_REQ_PROD, bp->cmd_req_reg.lword);

	/*
	 * Here we wait for the command response consumer index to be equal
	 * to the producer, indicating that the adapter has DMAed the response.
	 */

	for (timeout_cnt = 20000; timeout_cnt > 0; timeout_cnt--)
		{
		if (bp->cmd_rsp_reg.index.prod == (u8)(bp->cons_block_virt->cmd_rsp))
			break;
		udelay(100);			/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0)
		return DFX_K_HW_TIMEOUT;

	/* Bump (and wrap) the completion index and write out to register */

	bp->cmd_rsp_reg.index.comp += 1;
	bp->cmd_rsp_reg.index.comp &= PI_CMD_RSP_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_RSP_PROD, bp->cmd_rsp_reg.lword);
	return DFX_K_SUCCESS;
	}


/*
 * ========================
 * = dfx_hw_port_ctrl_req =
 * ========================
 *
 * Overview:
 *   Sends PDQ port control command to adapter firmware
 *
 * Returns:
 *   Host data register value in host_data if ptr is not NULL
 *
 * Arguments:
 *   bp			- pointer to board information
 *	 command	- port control command
 *	 data_a		- port data A register value
 *	 data_b		- port data B register value
 *	 host_data	- ptr to host data register value
 *
 * Functional Description:
 *   Send generic port control command to adapter by writing
 *   to various PDQ port registers, then polling for completion.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	  - port control command succeeded
 *   DFX_K_HW_TIMEOUT - port control command timed out
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

static int dfx_hw_port_ctrl_req(
	DFX_board_t	*bp,
	PI_UINT32	command,
	PI_UINT32	data_a,
	PI_UINT32	data_b,
	PI_UINT32	*host_data
	)

	{
	PI_UINT32	port_cmd;		/* Port Control command register value */
	int			timeout_cnt;	/* used in for loops */

	/* Set Command Error bit in command longword */

	port_cmd = (PI_UINT32) (command | PI_PCTRL_M_CMD_ERROR);

	/* Issue port command to the adapter */

	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_DATA_A, data_a);
	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_DATA_B, data_b);
	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_CTRL, port_cmd);

	/* Now wait for command to complete */

	if (command == PI_PCTRL_M_BLAST_FLASH)
		timeout_cnt = 600000;	/* set command timeout count to 60 seconds */
	else
		timeout_cnt = 20000;	/* set command timeout count to 2 seconds */

	for (; timeout_cnt > 0; timeout_cnt--)
		{
		dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_CTRL, &port_cmd);
		if (!(port_cmd & PI_PCTRL_M_CMD_ERROR))
			break;
		udelay(100);			/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0)
		return DFX_K_HW_TIMEOUT;

	/*
	 * If the address of host_data is non-zero, assume caller has supplied a
	 * non NULL pointer, and return the contents of the HOST_DATA register in
	 * it.
	 */

	if (host_data != NULL)
		dfx_port_read_long(bp, PI_PDQ_K_REG_HOST_DATA, host_data);
	return DFX_K_SUCCESS;
	}


/*
 * =====================
 * = dfx_hw_adap_reset =
 * =====================
 *
 * Overview:
 *   Resets adapter
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp   - pointer to board information
 *   type - type of reset to perform
 *
 * Functional Description:
 *   Issue soft reset to adapter by writing to PDQ Port Reset
 *   register.  Use incoming reset type to tell adapter what
 *   kind of reset operation to perform.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   This routine merely issues a soft reset to the adapter.
 *   It is expected that after this routine returns, the caller
 *   will appropriately poll the Port Status register for the
 *   adapter to enter the proper state.
 *
 * Side Effects:
 *   Internal adapter registers are cleared.
 */

static void dfx_hw_adap_reset(
	DFX_board_t	*bp,
	PI_UINT32	type
	)

	{
	/* Set Reset type and assert reset */

	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_DATA_A, type);	/* tell adapter type of reset */
	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_RESET, PI_RESET_M_ASSERT_RESET);

	/* Wait for at least 1 Microsecond according to the spec. We wait 20 just to be safe */

	udelay(20);

	/* Deassert reset */

	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_RESET, 0);
	}


/*
 * ========================
 * = dfx_hw_adap_state_rd =
 * ========================
 *
 * Overview:
 *   Returns current adapter state
 *
 * Returns:
 *   Adapter state per PDQ Port Specification
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Reads PDQ Port Status register and returns adapter state.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

static int dfx_hw_adap_state_rd(DFX_board_t *bp)
	{
	PI_UINT32 port_status;		/* Port Status register value */

	dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_STATUS, &port_status);
	return (port_status & PI_PSTATUS_M_STATE) >> PI_PSTATUS_V_STATE;
	}


/*
 * =====================
 * = dfx_hw_dma_uninit =
 * =====================
 *
 * Overview:
 *   Brings adapter to DMA_UNAVAILABLE state
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bp   - pointer to board information
 *   type - type of reset to perform
 *
 * Functional Description:
 *   Bring adapter to DMA_UNAVAILABLE state by performing the following:
 *		1. Set reset type bit in Port Data A Register then reset adapter.
 *		2. Check that adapter is in DMA_UNAVAILABLE state.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	  - adapter is in DMA_UNAVAILABLE state
 *   DFX_K_HW_TIMEOUT - adapter did not reset properly
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   Internal adapter registers are cleared.
 */

static int dfx_hw_dma_uninit(DFX_board_t *bp, PI_UINT32 type)
	{
	int timeout_cnt;	/* used in for loops */

	/* Set reset type bit and reset adapter */

	dfx_hw_adap_reset(bp, type);

	/* Now wait for adapter to enter DMA_UNAVAILABLE state */

	for (timeout_cnt = 100000; timeout_cnt > 0; timeout_cnt--)
		{
		if (dfx_hw_adap_state_rd(bp) == PI_STATE_K_DMA_UNAVAIL)
			break;
		udelay(100);					/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0)
		return DFX_K_HW_TIMEOUT;
	return DFX_K_SUCCESS;
	}

/*
 *	Align an sk_buff to a boundary power of 2
 *
 */
#ifdef DYNAMIC_BUFFERS
static void my_skb_align(struct sk_buff *skb, int n)
{
	unsigned long x = (unsigned long)skb->data;
	unsigned long v;

	v = ALIGN(x, n);	/* Where we want to be */

	skb_reserve(skb, v - x);
}
#endif

/*
 * ================
 * = dfx_rcv_init =
 * ================
 *
 * Overview:
 *   Produces buffers to adapter LLC Host receive descriptor block
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *   get_buffers - non-zero if buffers to be allocated
 *
 * Functional Description:
 *   This routine can be called during dfx_adap_init() or during an adapter
 *	 reset.  It initializes the descriptor block and produces all allocated
 *   LLC Host queue receive buffers.
 *
 * Return Codes:
 *   Return 0 on success or -ENOMEM if buffer allocation failed (when using
 *   dynamic buffer allocation). If the buffer allocation failed, the
 *   already allocated buffers will not be released and the caller should do
 *   this.
 *
 * Assumptions:
 *   The PDQ has been reset and the adapter and driver maintained Type 2
 *   register indices are cleared.
 *
 * Side Effects:
 *   Receive buffers are posted to the adapter LLC queue and the adapter
 *   is notified.
 */

static int dfx_rcv_init(DFX_board_t *bp, int get_buffers)
	{
	int	i, j;					/* used in for loop */

	/*
	 *  Since each receive buffer is a single fragment of same length, initialize
	 *  first longword in each receive descriptor for entire LLC Host descriptor
	 *  block.  Also initialize second longword in each receive descriptor with
	 *  physical address of receive buffer.  We'll always allocate receive
	 *  buffers in powers of 2 so that we can easily fill the 256 entry descriptor
	 *  block and produce new receive buffers by simply updating the receive
	 *  producer index.
	 *
	 * 	Assumptions:
	 *		To support all shipping versions of PDQ, the receive buffer size
	 *		must be mod 128 in length and the physical address must be 128 byte
	 *		aligned.  In other words, bits 0-6 of the length and address must
	 *		be zero for the following descriptor field entries to be correct on
	 *		all PDQ-based boards.  We guaranteed both requirements during
	 *		driver initialization when we allocated memory for the receive buffers.
	 */

	if (get_buffers) {
#ifdef DYNAMIC_BUFFERS
	for (i = 0; i < (int)(bp->rcv_bufs_to_post); i++)
		for (j = 0; (i + j) < (int)PI_RCV_DATA_K_NUM_ENTRIES; j += bp->rcv_bufs_to_post)
		{
			struct sk_buff *newskb;
			dma_addr_t dma_addr;

			newskb = __netdev_alloc_skb(bp->dev, NEW_SKB_SIZE,
						    GFP_NOIO);
			if (!newskb)
				return -ENOMEM;
			/*
			 * align to 128 bytes for compatibility with
			 * the old EISA boards.
			 */

			my_skb_align(newskb, 128);
			dma_addr = dma_map_single(bp->bus_dev,
						  newskb->data,
						  PI_RCV_DATA_K_SIZE_MAX,
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(bp->bus_dev, dma_addr)) {
				dev_kfree_skb(newskb);
				return -ENOMEM;
			}
			bp->descr_block_virt->rcv_data[i + j].long_0 =
				(u32)(PI_RCV_DESCR_M_SOP |
				      ((PI_RCV_DATA_K_SIZE_MAX /
					PI_ALIGN_K_RCV_DATA_BUFF) <<
				       PI_RCV_DESCR_V_SEG_LEN));
			bp->descr_block_virt->rcv_data[i + j].long_1 =
				(u32)dma_addr;

			/*
			 * p_rcv_buff_va is only used inside the
			 * kernel so we put the skb pointer here.
			 */
			bp->p_rcv_buff_va[i+j] = (char *) newskb;
		}
#else
	for (i=0; i < (int)(bp->rcv_bufs_to_post); i++)
		for (j=0; (i + j) < (int)PI_RCV_DATA_K_NUM_ENTRIES; j += bp->rcv_bufs_to_post)
			{
			bp->descr_block_virt->rcv_data[i+j].long_0 = (u32) (PI_RCV_DESCR_M_SOP |
				((PI_RCV_DATA_K_SIZE_MAX / PI_ALIGN_K_RCV_DATA_BUFF) << PI_RCV_DESCR_V_SEG_LEN));
			bp->descr_block_virt->rcv_data[i+j].long_1 = (u32) (bp->rcv_block_phys + (i * PI_RCV_DATA_K_SIZE_MAX));
			bp->p_rcv_buff_va[i+j] = (bp->rcv_block_virt + (i * PI_RCV_DATA_K_SIZE_MAX));
			}
#endif
	}

	/* Update receive producer and Type 2 register */

	bp->rcv_xmt_reg.index.rcv_prod = bp->rcv_bufs_to_post;
	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_2_PROD, bp->rcv_xmt_reg.lword);
	return 0;
	}


/*
 * =========================
 * = dfx_rcv_queue_process =
 * =========================
 *
 * Overview:
 *   Process received LLC frames.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Received LLC frames are processed until there are no more consumed frames.
 *   Once all frames are processed, the receive buffers are returned to the
 *   adapter.  Note that this algorithm fixes the length of time that can be spent
 *   in this routine, because there are a fixed number of receive buffers to
 *   process and buffers are not produced until this routine exits and returns
 *   to the ISR.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

static void dfx_rcv_queue_process(
	DFX_board_t *bp
	)

	{
	PI_TYPE_2_CONSUMER	*p_type_2_cons;		/* ptr to rcv/xmt consumer block register */
	char				*p_buff;			/* ptr to start of packet receive buffer (FMC descriptor) */
	u32					descr, pkt_len;		/* FMC descriptor field and packet length */
	struct sk_buff		*skb = NULL;			/* pointer to a sk_buff to hold incoming packet data */

	/* Service all consumed LLC receive frames */

	p_type_2_cons = (PI_TYPE_2_CONSUMER *)(&bp->cons_block_virt->xmt_rcv_data);
	while (bp->rcv_xmt_reg.index.rcv_comp != p_type_2_cons->index.rcv_cons)
		{
		/* Process any errors */
		dma_addr_t dma_addr;
		int entry;

		entry = bp->rcv_xmt_reg.index.rcv_comp;
#ifdef DYNAMIC_BUFFERS
		p_buff = (char *) (((struct sk_buff *)bp->p_rcv_buff_va[entry])->data);
#else
		p_buff = bp->p_rcv_buff_va[entry];
#endif
		dma_addr = bp->descr_block_virt->rcv_data[entry].long_1;
		dma_sync_single_for_cpu(bp->bus_dev,
					dma_addr + RCV_BUFF_K_DESCR,
					sizeof(u32),
					DMA_FROM_DEVICE);
		memcpy(&descr, p_buff + RCV_BUFF_K_DESCR, sizeof(u32));

		if (descr & PI_FMC_DESCR_M_RCC_FLUSH)
			{
			if (descr & PI_FMC_DESCR_M_RCC_CRC)
				bp->rcv_crc_errors++;
			else
				bp->rcv_frame_status_errors++;
			}
		else
		{
			int rx_in_place = 0;

			/* The frame was received without errors - verify packet length */

			pkt_len = (u32)((descr & PI_FMC_DESCR_M_LEN) >> PI_FMC_DESCR_V_LEN);
			pkt_len -= 4;				/* subtract 4 byte CRC */
			if (!IN_RANGE(pkt_len, FDDI_K_LLC_ZLEN, FDDI_K_LLC_LEN))
				bp->rcv_length_errors++;
			else{
#ifdef DYNAMIC_BUFFERS
				struct sk_buff *newskb = NULL;

				if (pkt_len > SKBUFF_RX_COPYBREAK) {
					dma_addr_t new_dma_addr;

					newskb = netdev_alloc_skb(bp->dev,
								  NEW_SKB_SIZE);
					if (newskb){
						my_skb_align(newskb, 128);
						new_dma_addr = dma_map_single(
								bp->bus_dev,
								newskb->data,
								PI_RCV_DATA_K_SIZE_MAX,
								DMA_FROM_DEVICE);
						if (dma_mapping_error(
								bp->bus_dev,
								new_dma_addr)) {
							dev_kfree_skb(newskb);
							newskb = NULL;
						}
					}
					if (newskb) {
						rx_in_place = 1;

						skb = (struct sk_buff *)bp->p_rcv_buff_va[entry];
						dma_unmap_single(bp->bus_dev,
							dma_addr,
							PI_RCV_DATA_K_SIZE_MAX,
							DMA_FROM_DEVICE);
						skb_reserve(skb, RCV_BUFF_K_PADDING);
						bp->p_rcv_buff_va[entry] = (char *)newskb;
						bp->descr_block_virt->rcv_data[entry].long_1 = (u32)new_dma_addr;
					}
				}
				if (!newskb)
#endif
					/* Alloc new buffer to pass up,
					 * add room for PRH. */
					skb = netdev_alloc_skb(bp->dev,
							       pkt_len + 3);
				if (skb == NULL)
					{
					printk("%s: Could not allocate receive buffer.  Dropping packet.\n", bp->dev->name);
					bp->rcv_discards++;
					break;
					}
				else {
					if (!rx_in_place) {
						/* Receive buffer allocated, pass receive packet up */
						dma_sync_single_for_cpu(
							bp->bus_dev,
							dma_addr +
							RCV_BUFF_K_PADDING,
							pkt_len + 3,
							DMA_FROM_DEVICE);

						skb_copy_to_linear_data(skb,
							       p_buff + RCV_BUFF_K_PADDING,
							       pkt_len + 3);
					}

					skb_reserve(skb,3);		/* adjust data field so that it points to FC byte */
					skb_put(skb, pkt_len);		/* pass up packet length, NOT including CRC */
					skb->protocol = fddi_type_trans(skb, bp->dev);
					bp->rcv_total_bytes += skb->len;
					netif_rx(skb);

					/* Update the rcv counters */
					bp->rcv_total_frames++;
					if (*(p_buff + RCV_BUFF_K_DA) & 0x01)
						bp->rcv_multicast_frames++;
				}
			}
			}

		/*
		 * Advance the producer (for recycling) and advance the completion
		 * (for servicing received frames).  Note that it is okay to
		 * advance the producer without checking that it passes the
		 * completion index because they are both advanced at the same
		 * rate.
		 */

		bp->rcv_xmt_reg.index.rcv_prod += 1;
		bp->rcv_xmt_reg.index.rcv_comp += 1;
		}
	}


/*
 * =====================
 * = dfx_xmt_queue_pkt =
 * =====================
 *
 * Overview:
 *   Queues packets for transmission
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   skb - pointer to sk_buff to queue for transmission
 *   dev - pointer to device information
 *
 * Functional Description:
 *   Here we assume that an incoming skb transmit request
 *   is contained in a single physically contiguous buffer
 *   in which the virtual address of the start of packet
 *   (skb->data) can be converted to a physical address
 *   by using pci_map_single().
 *
 *   Since the adapter architecture requires a three byte
 *   packet request header to prepend the start of packet,
 *   we'll write the three byte field immediately prior to
 *   the FC byte.  This assumption is valid because we've
 *   ensured that dev->hard_header_len includes three pad
 *   bytes.  By posting a single fragment to the adapter,
 *   we'll reduce the number of descriptor fetches and
 *   bus traffic needed to send the request.
 *
 *   Also, we can't free the skb until after it's been DMA'd
 *   out by the adapter, so we'll queue it in the driver and
 *   return it in dfx_xmt_done.
 *
 * Return Codes:
 *   0 - driver queued packet, link is unavailable, or skbuff was bad
 *	 1 - caller should requeue the sk_buff for later transmission
 *
 * Assumptions:
 *	 First and foremost, we assume the incoming skb pointer
 *   is NOT NULL and is pointing to a valid sk_buff structure.
 *
 *   The outgoing packet is complete, starting with the
 *   frame control byte including the last byte of data,
 *   but NOT including the 4 byte CRC.  We'll let the
 *   adapter hardware generate and append the CRC.
 *
 *   The entire packet is stored in one physically
 *   contiguous buffer which is not cached and whose
 *   32-bit physical address can be determined.
 *
 *   It's vital that this routine is NOT reentered for the
 *   same board and that the OS is not in another section of
 *   code (eg. dfx_int_common) for the same board on a
 *   different thread.
 *
 * Side Effects:
 *   None
 */

static netdev_tx_t dfx_xmt_queue_pkt(struct sk_buff *skb,
				     struct net_device *dev)
	{
	DFX_board_t		*bp = netdev_priv(dev);
	u8			prod;				/* local transmit producer index */
	PI_XMT_DESCR		*p_xmt_descr;		/* ptr to transmit descriptor block entry */
	XMT_DRIVER_DESCR	*p_xmt_drv_descr;	/* ptr to transmit driver descriptor */
	dma_addr_t		dma_addr;
	unsigned long		flags;

	netif_stop_queue(dev);

	/*
	 * Verify that incoming transmit request is OK
	 *
	 * Note: The packet size check is consistent with other
	 *		 Linux device drivers, although the correct packet
	 *		 size should be verified before calling the
	 *		 transmit routine.
	 */

	if (!IN_RANGE(skb->len, FDDI_K_LLC_ZLEN, FDDI_K_LLC_LEN))
	{
		printk("%s: Invalid packet length - %u bytes\n",
			dev->name, skb->len);
		bp->xmt_length_errors++;		/* bump error counter */
		netif_wake_queue(dev);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;			/* return "success" */
	}
	/*
	 * See if adapter link is available, if not, free buffer
	 *
	 * Note: If the link isn't available, free buffer and return 0
	 *		 rather than tell the upper layer to requeue the packet.
	 *		 The methodology here is that by the time the link
	 *		 becomes available, the packet to be sent will be
	 *		 fairly stale.  By simply dropping the packet, the
	 *		 higher layer protocols will eventually time out
	 *		 waiting for response packets which it won't receive.
	 */

	if (bp->link_available == PI_K_FALSE)
		{
		if (dfx_hw_adap_state_rd(bp) == PI_STATE_K_LINK_AVAIL)	/* is link really available? */
			bp->link_available = PI_K_TRUE;		/* if so, set flag and continue */
		else
			{
			bp->xmt_discards++;					/* bump error counter */
			dev_kfree_skb(skb);		/* free sk_buff now */
			netif_wake_queue(dev);
			return NETDEV_TX_OK;		/* return "success" */
			}
		}

	/* Write the three PRH bytes immediately before the FC byte */

	skb_push(skb, 3);
	skb->data[0] = DFX_PRH0_BYTE;	/* these byte values are defined */
	skb->data[1] = DFX_PRH1_BYTE;	/* in the Motorola FDDI MAC chip */
	skb->data[2] = DFX_PRH2_BYTE;	/* specification */

	dma_addr = dma_map_single(bp->bus_dev, skb->data, skb->len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(bp->bus_dev, dma_addr)) {
		skb_pull(skb, 3);
		return NETDEV_TX_BUSY;
	}

	spin_lock_irqsave(&bp->lock, flags);

	/* Get the current producer and the next free xmt data descriptor */

	prod		= bp->rcv_xmt_reg.index.xmt_prod;
	p_xmt_descr = &(bp->descr_block_virt->xmt_data[prod]);

	/*
	 * Get pointer to auxiliary queue entry to contain information
	 * for this packet.
	 *
	 * Note: The current xmt producer index will become the
	 *	 current xmt completion index when we complete this
	 *	 packet later on.  So, we'll get the pointer to the
	 *	 next auxiliary queue entry now before we bump the
	 *	 producer index.
	 */

	p_xmt_drv_descr = &(bp->xmt_drv_descr_blk[prod++]);	/* also bump producer index */

	/*
	 * Write the descriptor with buffer info and bump producer
	 *
	 * Note: Since we need to start DMA from the packet request
	 *		 header, we'll add 3 bytes to the DMA buffer length,
	 *		 and we'll determine the physical address of the
	 *		 buffer from the PRH, not skb->data.
	 *
	 * Assumptions:
	 *		 1. Packet starts with the frame control (FC) byte
	 *		    at skb->data.
	 *		 2. The 4-byte CRC is not appended to the buffer or
	 *			included in the length.
	 *		 3. Packet length (skb->len) is from FC to end of
	 *			data, inclusive.
	 *		 4. The packet length does not exceed the maximum
	 *			FDDI LLC frame length of 4491 bytes.
	 *		 5. The entire packet is contained in a physically
	 *			contiguous, non-cached, locked memory space
	 *			comprised of a single buffer pointed to by
	 *			skb->data.
	 *		 6. The physical address of the start of packet
	 *			can be determined from the virtual address
	 *			by using pci_map_single() and is only 32-bits
	 *			wide.
	 */

	p_xmt_descr->long_0	= (u32) (PI_XMT_DESCR_M_SOP | PI_XMT_DESCR_M_EOP | ((skb->len) << PI_XMT_DESCR_V_SEG_LEN));
	p_xmt_descr->long_1 = (u32)dma_addr;

	/*
	 * Verify that descriptor is actually available
	 *
	 * Note: If descriptor isn't available, return 1 which tells
	 *	 the upper layer to requeue the packet for later
	 *	 transmission.
	 *
	 *       We need to ensure that the producer never reaches the
	 *	 completion, except to indicate that the queue is empty.
	 */

	if (prod == bp->rcv_xmt_reg.index.xmt_comp)
	{
		skb_pull(skb,3);
		spin_unlock_irqrestore(&bp->lock, flags);
		return NETDEV_TX_BUSY;	/* requeue packet for later */
	}

	/*
	 * Save info for this packet for xmt done indication routine
	 *
	 * Normally, we'd save the producer index in the p_xmt_drv_descr
	 * structure so that we'd have it handy when we complete this
	 * packet later (in dfx_xmt_done).  However, since the current
	 * transmit architecture guarantees a single fragment for the
	 * entire packet, we can simply bump the completion index by
	 * one (1) for each completed packet.
	 *
	 * Note: If this assumption changes and we're presented with
	 *	 an inconsistent number of transmit fragments for packet
	 *	 data, we'll need to modify this code to save the current
	 *	 transmit producer index.
	 */

	p_xmt_drv_descr->p_skb = skb;

	/* Update Type 2 register */

	bp->rcv_xmt_reg.index.xmt_prod = prod;
	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_2_PROD, bp->rcv_xmt_reg.lword);
	spin_unlock_irqrestore(&bp->lock, flags);
	netif_wake_queue(dev);
	return NETDEV_TX_OK;	/* packet queued to adapter */
	}


/*
 * ================
 * = dfx_xmt_done =
 * ================
 *
 * Overview:
 *   Processes all frames that have been transmitted.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   For all consumed transmit descriptors that have not
 *   yet been completed, we'll free the skb we were holding
 *   onto using dev_kfree_skb and bump the appropriate
 *   counters.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   The Type 2 register is not updated in this routine.  It is
 *   assumed that it will be updated in the ISR when dfx_xmt_done
 *   returns.
 *
 * Side Effects:
 *   None
 */

static int dfx_xmt_done(DFX_board_t *bp)
	{
	XMT_DRIVER_DESCR	*p_xmt_drv_descr;	/* ptr to transmit driver descriptor */
	PI_TYPE_2_CONSUMER	*p_type_2_cons;		/* ptr to rcv/xmt consumer block register */
	u8			comp;			/* local transmit completion index */
	int 			freed = 0;		/* buffers freed */

	/* Service all consumed transmit frames */

	p_type_2_cons = (PI_TYPE_2_CONSUMER *)(&bp->cons_block_virt->xmt_rcv_data);
	while (bp->rcv_xmt_reg.index.xmt_comp != p_type_2_cons->index.xmt_cons)
		{
		/* Get pointer to the transmit driver descriptor block information */

		p_xmt_drv_descr = &(bp->xmt_drv_descr_blk[bp->rcv_xmt_reg.index.xmt_comp]);

		/* Increment transmit counters */

		bp->xmt_total_frames++;
		bp->xmt_total_bytes += p_xmt_drv_descr->p_skb->len;

		/* Return skb to operating system */
		comp = bp->rcv_xmt_reg.index.xmt_comp;
		dma_unmap_single(bp->bus_dev,
				 bp->descr_block_virt->xmt_data[comp].long_1,
				 p_xmt_drv_descr->p_skb->len,
				 DMA_TO_DEVICE);
		dev_consume_skb_irq(p_xmt_drv_descr->p_skb);

		/*
		 * Move to start of next packet by updating completion index
		 *
		 * Here we assume that a transmit packet request is always
		 * serviced by posting one fragment.  We can therefore
		 * simplify the completion code by incrementing the
		 * completion index by one.  This code will need to be
		 * modified if this assumption changes.  See comments
		 * in dfx_xmt_queue_pkt for more details.
		 */

		bp->rcv_xmt_reg.index.xmt_comp += 1;
		freed++;
		}
	return freed;
	}


/*
 * =================
 * = dfx_rcv_flush =
 * =================
 *
 * Overview:
 *   Remove all skb's in the receive ring.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Free's all the dynamically allocated skb's that are
 *   currently attached to the device receive ring. This
 *   function is typically only used when the device is
 *   initialized or reinitialized.
 *
 * Return Codes:
 *   None
 *
 * Side Effects:
 *   None
 */
#ifdef DYNAMIC_BUFFERS
static void dfx_rcv_flush( DFX_board_t *bp )
	{
	int i, j;

	for (i = 0; i < (int)(bp->rcv_bufs_to_post); i++)
		for (j = 0; (i + j) < (int)PI_RCV_DATA_K_NUM_ENTRIES; j += bp->rcv_bufs_to_post)
		{
			struct sk_buff *skb;
			skb = (struct sk_buff *)bp->p_rcv_buff_va[i+j];
			if (skb) {
				dma_unmap_single(bp->bus_dev,
						 bp->descr_block_virt->rcv_data[i+j].long_1,
						 PI_RCV_DATA_K_SIZE_MAX,
						 DMA_FROM_DEVICE);
				dev_kfree_skb(skb);
			}
			bp->p_rcv_buff_va[i+j] = NULL;
		}

	}
#endif /* DYNAMIC_BUFFERS */

/*
 * =================
 * = dfx_xmt_flush =
 * =================
 *
 * Overview:
 *   Processes all frames whether they've been transmitted
 *   or not.
 *
 * Returns:
 *   None
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   For all produced transmit descriptors that have not
 *   yet been completed, we'll free the skb we were holding
 *   onto using dev_kfree_skb and bump the appropriate
 *   counters.  Of course, it's possible that some of
 *   these transmit requests actually did go out, but we
 *   won't make that distinction here.  Finally, we'll
 *   update the consumer index to match the producer.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   This routine does NOT update the Type 2 register.  It
 *   is assumed that this routine is being called during a
 *   transmit flush interrupt, or a shutdown or close routine.
 *
 * Side Effects:
 *   None
 */

static void dfx_xmt_flush( DFX_board_t *bp )
	{
	u32			prod_cons;		/* rcv/xmt consumer block longword */
	XMT_DRIVER_DESCR	*p_xmt_drv_descr;	/* ptr to transmit driver descriptor */
	u8			comp;			/* local transmit completion index */

	/* Flush all outstanding transmit frames */

	while (bp->rcv_xmt_reg.index.xmt_comp != bp->rcv_xmt_reg.index.xmt_prod)
		{
		/* Get pointer to the transmit driver descriptor block information */

		p_xmt_drv_descr = &(bp->xmt_drv_descr_blk[bp->rcv_xmt_reg.index.xmt_comp]);

		/* Return skb to operating system */
		comp = bp->rcv_xmt_reg.index.xmt_comp;
		dma_unmap_single(bp->bus_dev,
				 bp->descr_block_virt->xmt_data[comp].long_1,
				 p_xmt_drv_descr->p_skb->len,
				 DMA_TO_DEVICE);
		dev_kfree_skb(p_xmt_drv_descr->p_skb);

		/* Increment transmit error counter */

		bp->xmt_discards++;

		/*
		 * Move to start of next packet by updating completion index
		 *
		 * Here we assume that a transmit packet request is always
		 * serviced by posting one fragment.  We can therefore
		 * simplify the completion code by incrementing the
		 * completion index by one.  This code will need to be
		 * modified if this assumption changes.  See comments
		 * in dfx_xmt_queue_pkt for more details.
		 */

		bp->rcv_xmt_reg.index.xmt_comp += 1;
		}

	/* Update the transmit consumer index in the consumer block */

	prod_cons = (u32)(bp->cons_block_virt->xmt_rcv_data & ~PI_CONS_M_XMT_INDEX);
	prod_cons |= (u32)(bp->rcv_xmt_reg.index.xmt_prod << PI_CONS_V_XMT_INDEX);
	bp->cons_block_virt->xmt_rcv_data = prod_cons;
	}

/*
 * ==================
 * = dfx_unregister =
 * ==================
 *
 * Overview:
 *   Shuts down an FDDI controller
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bdev - pointer to device information
 *
 * Functional Description:
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   It compiles so it should work :-( (PCI cards do :-)
 *
 * Side Effects:
 *   Device structures for FDDI adapters (fddi0, fddi1, etc) are
 *   freed.
 */
static void dfx_unregister(struct device *bdev)
{
	struct net_device *dev = dev_get_drvdata(bdev);
	DFX_board_t *bp = netdev_priv(dev);
	int dfx_bus_pci = dev_is_pci(bdev);
	int dfx_bus_tc = DFX_BUS_TC(bdev);
	int dfx_use_mmio = DFX_MMIO || dfx_bus_tc;
	resource_size_t bar_start[3] = {0};	/* pointers to ports */
	resource_size_t bar_len[3] = {0};	/* resource lengths */
	int		alloc_size;		/* total buffer size used */

	unregister_netdev(dev);

	alloc_size = sizeof(PI_DESCR_BLOCK) +
		     PI_CMD_REQ_K_SIZE_MAX + PI_CMD_RSP_K_SIZE_MAX +
#ifndef DYNAMIC_BUFFERS
		     (bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX) +
#endif
		     sizeof(PI_CONSUMER_BLOCK) +
		     (PI_ALIGN_K_DESC_BLK - 1);
	if (bp->kmalloced)
		dma_free_coherent(bdev, alloc_size,
				  bp->kmalloced, bp->kmalloced_dma);

	dfx_bus_uninit(dev);

	dfx_get_bars(bdev, bar_start, bar_len);
	if (bar_start[2] != 0)
		release_region(bar_start[2], bar_len[2]);
	if (bar_start[1] != 0)
		release_region(bar_start[1], bar_len[1]);
	if (dfx_use_mmio) {
		iounmap(bp->base.mem);
		release_mem_region(bar_start[0], bar_len[0]);
	} else
		release_region(bar_start[0], bar_len[0]);

	if (dfx_bus_pci)
		pci_disable_device(to_pci_dev(bdev));

	free_netdev(dev);
}


static int __maybe_unused dfx_dev_register(struct device *);
static int __maybe_unused dfx_dev_unregister(struct device *);

#ifdef CONFIG_PCI
static int dfx_pci_register(struct pci_dev *, const struct pci_device_id *);
static void dfx_pci_unregister(struct pci_dev *);

static const struct pci_device_id dfx_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_FDDI) },
	{ }
};
MODULE_DEVICE_TABLE(pci, dfx_pci_table);

static struct pci_driver dfx_pci_driver = {
	.name		= "defxx",
	.id_table	= dfx_pci_table,
	.probe		= dfx_pci_register,
	.remove		= dfx_pci_unregister,
};

static int dfx_pci_register(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	return dfx_register(&pdev->dev);
}

static void dfx_pci_unregister(struct pci_dev *pdev)
{
	dfx_unregister(&pdev->dev);
}
#endif /* CONFIG_PCI */

#ifdef CONFIG_EISA
static const struct eisa_device_id dfx_eisa_table[] = {
        { "DEC3001", DEFEA_PROD_ID_1 },
        { "DEC3002", DEFEA_PROD_ID_2 },
        { "DEC3003", DEFEA_PROD_ID_3 },
        { "DEC3004", DEFEA_PROD_ID_4 },
        { }
};
MODULE_DEVICE_TABLE(eisa, dfx_eisa_table);

static struct eisa_driver dfx_eisa_driver = {
	.id_table	= dfx_eisa_table,
	.driver		= {
		.name	= "defxx",
		.bus	= &eisa_bus_type,
		.probe	= dfx_dev_register,
		.remove	= dfx_dev_unregister,
	},
};
#endif /* CONFIG_EISA */

#ifdef CONFIG_TC
static struct tc_device_id const dfx_tc_table[] = {
	{ "DEC     ", "PMAF-FA " },
	{ "DEC     ", "PMAF-FD " },
	{ "DEC     ", "PMAF-FS " },
	{ "DEC     ", "PMAF-FU " },
	{ }
};
MODULE_DEVICE_TABLE(tc, dfx_tc_table);

static struct tc_driver dfx_tc_driver = {
	.id_table	= dfx_tc_table,
	.driver		= {
		.name	= "defxx",
		.bus	= &tc_bus_type,
		.probe	= dfx_dev_register,
		.remove	= dfx_dev_unregister,
	},
};
#endif /* CONFIG_TC */

static int __maybe_unused dfx_dev_register(struct device *dev)
{
	int status;

	status = dfx_register(dev);
	if (!status)
		get_device(dev);
	return status;
}

static int __maybe_unused dfx_dev_unregister(struct device *dev)
{
	put_device(dev);
	dfx_unregister(dev);
	return 0;
}


static int dfx_init(void)
{
	int status;

	status = pci_register_driver(&dfx_pci_driver);
	if (!status)
		status = eisa_driver_register(&dfx_eisa_driver);
	if (!status)
		status = tc_register_driver(&dfx_tc_driver);
	return status;
}

static void dfx_cleanup(void)
{
	tc_unregister_driver(&dfx_tc_driver);
	eisa_driver_unregister(&dfx_eisa_driver);
	pci_unregister_driver(&dfx_pci_driver);
}

module_init(dfx_init);
module_exit(dfx_cleanup);
MODULE_AUTHOR("Lawrence V. Stefani");
MODULE_DESCRIPTION("DEC FDDIcontroller TC/EISA/PCI (DEFTA/DEFEA/DEFPA) driver "
		   DRV_VERSION " " DRV_RELDATE);
MODULE_LICENSE("GPL");
