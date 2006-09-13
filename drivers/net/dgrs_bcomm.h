/*
 * The bios low-memory structure
 *
 * Some of the variables in here can be used to set parameters that
 * are stored in NVRAM and will retain their old values the next time
 * the card is brought up.  To use the values stored in NVRAM, the
 * parameter should be set to "all ones".  This tells the firmware to
 * use the NVRAM value or a suitable default.  The value that is used
 * will be stored back into this structure by the firmware.  If the
 * value of the variable is not "all ones", then that value will be
 * used and will be stored into NVRAM if it isn't already there.
 * The variables this applies to are the following:
 *	Variable	Set to:		Gets default of:
 *	bc_hashexpire	-1		300	(5 minutes)
 *	bc_spantree	-1		1	(spanning tree on)
 *	bc_ipaddr	FF:FF:FF:FF	0	(no SNMP IP address)
 *	bc_ipxnet	FF:FF:FF:FF	0	(no SNMP IPX net)
 *	bc_iptrap	FF:FF:FF:FF	0	(no SNMP IP trap address)
 *
 * Some variables MUST have their value set after the firmware
 * is loaded onto the board, but before the processor is released.
 * These are:
 *	bc_host		0 means no host "port", run as standalone switch.
 *			1 means run as a switch, with a host port. (normal)
 *			2 means run as multiple NICs, not as a switch.
 *			-1 means run in diagnostics mode.
 *	bc_nowait
 *	bc_hostarea_len
 *	bc_filter_len
 *
 */
BEGIN_STRUCT(bios_comm)
	S4(ulong, bc_intflag)	/* Count of all interrupts */
	S4(ulong, bc_lbolt)	/* Count of timer interrupts */
	S4(ulong, bc_maincnt)	/* Count of main loops */
	S4(ulong, bc_hashcnt)	/* Count of entries in hash table */
	S4A(ulong, bc_cnt, 8)	/* Misc counters, for debugging */
	S4A(ulong, bc_flag, 8)	/* Misc flags, for debugging */
	S4(ulong, bc_memsize)	/* Size of memory */
	S4(ulong, bc_dcache)	/* Size of working dcache */
	S4(ulong, bc_icache)	/* Size of working icache */
	S4(long, bc_status)	/* Firmware status */
	S1A(char, bc_file, 8)	/* File name of assertion failure */
	S4(ulong, bc_line)	/* Line # of assertion failure */
	S4(uchar *, bc_ramstart)
	S4(uchar *, bc_ramend)
	S4(uchar *, bc_heapstart) /* Start of heap (end of loaded memory) */
	S4(uchar *, bc_heapend)	/* End of heap */

	/* Configurable Parameters */
	S4(long, bc_host)	/* 1=Host Port, 0=No Host Port, -1=Test Mode */
	S4(long, bc_nowait)	/* Don't wait for 2host circ buffer to empty*/
	S4(long, bc_150ohm)	/* 0 == 100 ohm UTP, 1 == 150 ohm STP */
	S4(long, bc_squelch)	/* 0 == normal squelch, 1 == reduced squelch */
	S4(ulong, bc_hashexpire) /* Expiry time in seconds for hash table */
	S4(long, bc_spantree)	/* 1 == enable IEEE spanning tree */

	S2A(ushort, bc_eaddr, 3) /* New ether address */
	S2(ushort, bc_dummy1)	/* padding for DOS compilers */

	/* Various debugging aids */
	S4(long, bc_debug)	/* Debugging is turned on */
	S4(long, bc_spew)	/* Spew data on port 4 for bs_spew seconds */
	S4(long, bc_spewlen)	/* Length of spewed data packets */
	S4(long, bc_maxrfd)	/* If != 0, max number of RFD's to allocate */
	S4(long, bc_maxrbd)	/* If != 0, max number of RBD's to allocate */

	/* Circular buffers for messages to/from host */
	S4(ulong, bc_2host_head)
	S4(ulong, bc_2host_tail)
	S4(ulong, bc_2host_mask)
	S1A(char, bc_2host, 0x200)	/* Circ buff to host */

	S4(ulong, bc_2idt_head)
	S4(ulong, bc_2idt_tail)
	S4(ulong, bc_2idt_mask)
	S1A(char, bc_2idt, 0x200)	/* Circ buff to idt */

	/* Pointers to structures for driver access */
	S4(uchar *, bc_port)	/* pointer to Port[] structures */
	S4(long, bc_nports)	/* Number of ports */
	S4(long, bc_portlen)	/* sizeof(PORT) */
	S4(uchar *, bc_hash)	/* Pointer to hash table */
	S4(long, bc_hashlen)	/* sizeof(Table) */

	/* SNMP agent addresses */
	S1A(uchar, bc_ipaddr, 4) /* IP address for SNMP */
	S1A(uchar, bc_ipxnet, 4) /* IPX net address for SNMP */

	S4(long, bc_nohostintr) /* Do not cause periodic host interrupts */

	S4(uchar *, bc_dmaaddr) /* Physical addr of host DMA buf for diags */
	S4(ulong, bc_dmalen)	/* Length of DMA buffer 0..2048 */

	/*
	 *	Board memory allocated on startup for use by host, usually
	 *	for the purposes of creating DMA chain descriptors.  The
	 *	"len" must be set before the processor is released.  The
	 *	address of the area is returned in bc_hostarea.  The area
	 *	is guaranteed to be aligned on a 16 byte boundary.
	 */
	S4(ulong, bc_hostarea_len)	/* RW: Number of bytes to allocate */
	S4(uchar *, bc_hostarea)	/* RO: Address of allocated memory */

	/*
	 *	Variables for communicating filters into the board
	 */
	S4(ulong *, bc_filter_area)	/* RO: Space to put filter into */
	S4(ulong, bc_filter_area_len)	/* RO: Length of area, in bytes */
	S4(long, bc_filter_cmd)		/* RW: Filter command, see below */
	S4(ulong, bc_filter_len)	/* RW: Actual length of filter */
	S4(ulong, bc_filter_port)	/* RW: Port # for filter 0..6 */
	S4(ulong, bc_filter_num)	/* RW: Filter #, 0=input, 1=output */

	/* more SNMP agent addresses */
	S1A(uchar, bc_iptrap, 4) /* IP address for SNMP */

	S4A(long, bc_spare, 2)	/* spares */
END_STRUCT(bios_comm)

#define	bc	VMO(struct bios_comm, 0xa3000100)

/*
 *	bc_status values
 */
#define	BC_INIT	0
#define	BC_RUN	100

/*
 *	bc_host values
 */
#define	BC_DIAGS	-1
#define BC_SASWITCH	0
#define	BC_SWITCH	1
#define	BC_MULTINIC	2

/*
 *	Values for spew (debugging)
 */
#define	BC_SPEW_ENABLE	0x80000000

/*
 *	filter commands
 */
#define	BC_FILTER_ERR	-1
#define	BC_FILTER_OK	0
#define	BC_FILTER_SET	1
#define	BC_FILTER_CLR	2
