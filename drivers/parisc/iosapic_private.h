/* 
 *    Private structs/constants for PARISC IOSAPIC support
 *
 *    Copyright (C) 2000 Hewlett Packard (Grant Grundler)
 *    Copyright (C) 2000,2003 Grant Grundler (grundler at parisc-linux.org)
 *    Copyright (C) 2002 Matthew Wilcox (willy at parisc-linux.org)
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
** This file is private to iosapic driver.
** If stuff needs to be used by another driver, move it to a common file.
**
** WARNING: fields most data structures here are ordered to make sure
**          they pack nicely for 64-bit compilation. (ie sizeof(long) == 8)
*/


/*
** Interrupt Routing Stuff
** -----------------------
** The interrupt routing table consists of entries derived from
** MP Specification Draft 1.5. There is one interrupt routing 
** table per cell.  N- and L-class consist of a single cell.
*/
struct irt_entry {

	/* Entry Type 139 identifies an I/O SAPIC interrupt entry */
	u8 entry_type;

	/* Entry Length 16 indicates entry is 16 bytes long */
	u8 entry_length;

	/* 
	** Interrupt Type of 0 indicates a vectored interrupt, 
	** all other values are reserved 
	*/
	u8 interrupt_type;

	/* 
	** PO and EL
	** Polarity of SAPIC I/O input signals: 
	**    00 = Reserved 
	**    01 = Active high 
	**    10 = Reserved 
	**    11 = Active low 
	** Trigger mode of SAPIC I/O input signals: 
	**    00 = Reserved 
	**    01 = Edge-triggered 
	**    10 = Reserved 
	**    11 = Level-triggered
	*/
	u8 polarity_trigger;

	/* 
	** IRQ and DEVNO
	** irq identifies PCI interrupt signal where
	**    0x0 corresponds to INT_A#, 
	**    0x1 corresponds to INT_B#, 
	**    0x2 corresponds to INT_C# 
	**    0x3 corresponds to INT_D# 
	** PCI device number where interrupt originates 
	*/
	u8 src_bus_irq_devno;

	/* Source Bus ID identifies the bus where interrupt signal comes from */
	u8 src_bus_id;

	/* 
	** Segment ID is unique across a protection domain and
	** identifies a segment of PCI buses (reserved in 
	** MP Specification Draft 1.5) 
	*/
	u8 src_seg_id;

	/* 
	** Destination I/O SAPIC INTIN# identifies the INTIN n pin 
	** to which the signal is connected 
	*/
	u8 dest_iosapic_intin;

	/* 
	** Destination I/O SAPIC Address identifies the I/O SAPIC 
	** to which the signal is connected 
	*/
	u64 dest_iosapic_addr;
};

#define IRT_IOSAPIC_TYPE   139
#define IRT_IOSAPIC_LENGTH 16

#define IRT_VECTORED_INTR  0

#define IRT_PO_MASK        0x3
#define IRT_ACTIVE_HI      1
#define IRT_ACTIVE_LO      3

#define IRT_EL_MASK        0x3
#define IRT_EL_SHIFT       2
#define IRT_EDGE_TRIG      1
#define IRT_LEVEL_TRIG     3

#define IRT_IRQ_MASK       0x3
#define IRT_DEV_MASK       0x1f
#define IRT_DEV_SHIFT      2

#define IRT_IRQ_DEVNO_MASK	((IRT_DEV_MASK << IRT_DEV_SHIFT) | IRT_IRQ_MASK)

#ifdef SUPPORT_MULTI_CELL
struct iosapic_irt {
        struct iosapic_irt *irt_next;  /* next routing table */
        struct irt_entry *irt_base;             /* intr routing table address */
        size_t  irte_count;            /* number of entries in the table */
        size_t  irte_size;             /* size (bytes) of each entry */
};
#endif

struct vector_info {
	struct iosapic_info *iosapic;	/* I/O SAPIC this vector is on */
	struct irt_entry *irte;		/* IRT entry */
	u32	*eoi_addr;		/* precalculate EOI reg address */
	u32	eoi_data;		/* IA64: ?       PA: swapped txn_data */
	int	txn_irq;		/* virtual IRQ number for processor */
	ulong	txn_addr;		/* IA64: id_eid  PA: partial HPA */
	u32	txn_data;		/* CPU interrupt bit */
	u8	status;			/* status/flags */
	u8	irqline;		/* INTINn(IRQ) */
};


struct iosapic_info {
	struct iosapic_info *	isi_next;	/* list of I/O SAPIC */
	void __iomem *		addr;		/* remapped address */
	unsigned long		isi_hpa;	/* physical base address */
	struct vector_info *	isi_vector;	/* IRdT (IRQ line) array */
	int			isi_num_vectors; /* size of IRdT array */
	int			isi_status;	/* status/flags */
	unsigned int		isi_version;	/* DEBUG: data fr version reg */
};



#ifdef __IA64__
/*
** PA risc does NOT have any local sapics. IA64 does.
** PIB (Processor Interrupt Block) is handled by Astro or Dew (Stretch CEC).
**
** PA: Get id_eid from IRT and hardcode PIB to 0xfeeNNNN0
**     Emulate the data on PAT platforms.
*/
struct local_sapic_info {
	struct local_sapic_info *lsi_next;      /* point to next CPU info */
	int                     *lsi_cpu_id;    /* point to logical CPU id */
	unsigned long           *lsi_id_eid;    /* point to IA-64 CPU id */
	int                     *lsi_status;    /* point to CPU status   */
	void                    *lsi_private;   /* point to special info */
};

/*
** "root" data structure which ties everything together.
** Should always be able to start with sapic_root and locate
** the desired information.
*/
struct sapic_info {
	struct sapic_info	*si_next;	/* info is per cell */
	int                     si_cellid;      /* cell id */
	unsigned int            si_status;       /* status  */
	char                    *si_pib_base;   /* intr blk base address */
	local_sapic_info_t      *si_local_info;
	io_sapic_info_t         *si_io_info;
	extint_info_t           *si_extint_info;/* External Intr info      */
};
#endif

