/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV architectural definitions
 *
 * Copyright (C) 2007-2014 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_X86_UV_UV_HUB_H
#define _ASM_X86_UV_UV_HUB_H

#ifdef CONFIG_X86_64
#include <linux/numa.h>
#include <linux/percpu.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/topology.h>
#include <asm/types.h>
#include <asm/percpu.h>
#include <asm/uv/uv.h>
#include <asm/uv/uv_mmrs.h>
#include <asm/uv/bios.h>
#include <asm/irq_vectors.h>
#include <asm/io_apic.h>


/*
 * Addressing Terminology
 *
 *	M       - The low M bits of a physical address represent the offset
 *		  into the blade local memory. RAM memory on a blade is physically
 *		  contiguous (although various IO spaces may punch holes in
 *		  it)..
 *
 *	N	- Number of bits in the node portion of a socket physical
 *		  address.
 *
 *	NASID   - network ID of a router, Mbrick or Cbrick. Nasid values of
 *		  routers always have low bit of 1, C/MBricks have low bit
 *		  equal to 0. Most addressing macros that target UV hub chips
 *		  right shift the NASID by 1 to exclude the always-zero bit.
 *		  NASIDs contain up to 15 bits.
 *
 *	GNODE   - NASID right shifted by 1 bit. Most mmrs contain gnodes instead
 *		  of nasids.
 *
 *	PNODE   - the low N bits of the GNODE. The PNODE is the most useful variant
 *		  of the nasid for socket usage.
 *
 *	GPA	- (global physical address) a socket physical address converted
 *		  so that it can be used by the GRU as a global address. Socket
 *		  physical addresses 1) need additional NASID (node) bits added
 *		  to the high end of the address, and 2) unaliased if the
 *		  partition does not have a physical address 0. In addition, on
 *		  UV2 rev 1, GPAs need the gnode left shifted to bits 39 or 40.
 *
 *
 *  NumaLink Global Physical Address Format:
 *  +--------------------------------+---------------------+
 *  |00..000|      GNODE             |      NodeOffset     |
 *  +--------------------------------+---------------------+
 *          |<-------53 - M bits --->|<--------M bits ----->
 *
 *	M - number of node offset bits (35 .. 40)
 *
 *
 *  Memory/UV-HUB Processor Socket Address Format:
 *  +----------------+---------------+---------------------+
 *  |00..000000000000|   PNODE       |      NodeOffset     |
 *  +----------------+---------------+---------------------+
 *                   <--- N bits --->|<--------M bits ----->
 *
 *	M - number of node offset bits (35 .. 40)
 *	N - number of PNODE bits (0 .. 10)
 *
 *		Note: M + N cannot currently exceed 44 (x86_64) or 46 (IA64).
 *		The actual values are configuration dependent and are set at
 *		boot time. M & N values are set by the hardware/BIOS at boot.
 *
 *
 * APICID format
 *	NOTE!!!!!! This is the current format of the APICID. However, code
 *	should assume that this will change in the future. Use functions
 *	in this file for all APICID bit manipulations and conversion.
 *
 *		1111110000000000
 *		5432109876543210
 *		pppppppppplc0cch	Nehalem-EX (12 bits in hdw reg)
 *		ppppppppplcc0cch	Westmere-EX (12 bits in hdw reg)
 *		pppppppppppcccch	SandyBridge (15 bits in hdw reg)
 *		sssssssssss
 *
 *			p  = pnode bits
 *			l =  socket number on board
 *			c  = core
 *			h  = hyperthread
 *			s  = bits that are in the SOCKET_ID CSR
 *
 *	Note: Processor may support fewer bits in the APICID register. The ACPI
 *	      tables hold all 16 bits. Software needs to be aware of this.
 *
 *	      Unless otherwise specified, all references to APICID refer to
 *	      the FULL value contained in ACPI tables, not the subset in the
 *	      processor APICID register.
 */

/*
 * Maximum number of bricks in all partitions and in all coherency domains.
 * This is the total number of bricks accessible in the numalink fabric. It
 * includes all C & M bricks. Routers are NOT included.
 *
 * This value is also the value of the maximum number of non-router NASIDs
 * in the numalink fabric.
 *
 * NOTE: a brick may contain 1 or 2 OS nodes. Don't get these confused.
 */
#define UV_MAX_NUMALINK_BLADES	16384

/*
 * Maximum number of C/Mbricks within a software SSI (hardware may support
 * more).
 */
#define UV_MAX_SSI_BLADES	256

/*
 * The largest possible NASID of a C or M brick (+ 2)
 */
#define UV_MAX_NASID_VALUE	(UV_MAX_NUMALINK_BLADES * 2)

/* System Controller Interface Reg info */
struct uv_scir_s {
	struct timer_list timer;
	unsigned long	offset;
	unsigned long	last;
	unsigned long	idle_on;
	unsigned long	idle_off;
	unsigned char	state;
	unsigned char	enabled;
};

/* GAM (globally addressed memory) range table */
struct uv_gam_range_s {
	u32	limit;		/* PA bits 56:26 (GAM_RANGE_SHFT) */
	u16	nasid;		/* node's global physical address */
	s8	base;		/* entry index of node's base addr */
	u8	reserved;
};

/*
 * The following defines attributes of the HUB chip. These attributes are
 * frequently referenced and are kept in a common per hub struct.
 * After setup, the struct is read only, so it should be readily
 * available in the L3 cache on the cpu socket for the node.
 */
struct uv_hub_info_s {
	unsigned long		global_mmr_base;
	unsigned long		global_mmr_shift;
	unsigned long		gpa_mask;
	unsigned short		*socket_to_node;
	unsigned short		*socket_to_pnode;
	unsigned short		*pnode_to_socket;
	struct uv_gam_range_s	*gr_table;
	unsigned short		min_socket;
	unsigned short		min_pnode;
	unsigned char		m_val;
	unsigned char		n_val;
	unsigned char		gr_table_len;
	unsigned char		hub_revision;
	unsigned char		apic_pnode_shift;
	unsigned char		gpa_shift;
	unsigned char		m_shift;
	unsigned char		n_lshift;
	unsigned int		gnode_extra;
	unsigned long		gnode_upper;
	unsigned long		lowmem_remap_top;
	unsigned long		lowmem_remap_base;
	unsigned long		global_gru_base;
	unsigned long		global_gru_shift;
	unsigned short		pnode;
	unsigned short		pnode_mask;
	unsigned short		coherency_domain_number;
	unsigned short		numa_blade_id;
	unsigned short		nr_possible_cpus;
	unsigned short		nr_online_cpus;
	short			memory_nid;
};

/* CPU specific info with a pointer to the hub common info struct */
struct uv_cpu_info_s {
	void			*p_uv_hub_info;
	unsigned char		blade_cpu_id;
	struct uv_scir_s	scir;
};
DECLARE_PER_CPU(struct uv_cpu_info_s, __uv_cpu_info);

#define uv_cpu_info		this_cpu_ptr(&__uv_cpu_info)
#define uv_cpu_info_per(cpu)	(&per_cpu(__uv_cpu_info, cpu))

#define	uv_scir_info		(&uv_cpu_info->scir)
#define	uv_cpu_scir_info(cpu)	(&uv_cpu_info_per(cpu)->scir)

/* Node specific hub common info struct */
extern void **__uv_hub_info_list;
static inline struct uv_hub_info_s *uv_hub_info_list(int node)
{
	return (struct uv_hub_info_s *)__uv_hub_info_list[node];
}

static inline struct uv_hub_info_s *_uv_hub_info(void)
{
	return (struct uv_hub_info_s *)uv_cpu_info->p_uv_hub_info;
}
#define	uv_hub_info	_uv_hub_info()

static inline struct uv_hub_info_s *uv_cpu_hub_info(int cpu)
{
	return (struct uv_hub_info_s *)uv_cpu_info_per(cpu)->p_uv_hub_info;
}

/*
 * HUB revision ranges for each UV HUB architecture.
 * This is a software convention - NOT the hardware revision numbers in
 * the hub chip.
 */
#define UV1_HUB_REVISION_BASE		1
#define UV2_HUB_REVISION_BASE		3
#define UV3_HUB_REVISION_BASE		5
#define UV4_HUB_REVISION_BASE		7
#define UV4A_HUB_REVISION_BASE		8	/* UV4 (fixed) rev 2 */

static inline int is_uv1_hub(void)
{
	return is_uv_hubbed(uv(1));
}

static inline int is_uv2_hub(void)
{
	return is_uv_hubbed(uv(2));
}

static inline int is_uv3_hub(void)
{
	return is_uv_hubbed(uv(3));
}

/* First test "is UV4A", then "is UV4" */
static inline int is_uv4a_hub(void)
{
	if (is_uv_hubbed(uv(4)))
		return (uv_hub_info->hub_revision == UV4A_HUB_REVISION_BASE);
	return 0;
}

static inline int is_uv4_hub(void)
{
	return is_uv_hubbed(uv(4));
}

static inline int is_uvx_hub(void)
{
	return (is_uv_hubbed(-2) >= uv(2));
}

static inline int is_uv_hub(void)
{
	return is_uv1_hub() || is_uvx_hub();
}

union uvh_apicid {
    unsigned long       v;
    struct uvh_apicid_s {
        unsigned long   local_apic_mask  : 24;
        unsigned long   local_apic_shift :  5;
        unsigned long   unused1          :  3;
        unsigned long   pnode_mask       : 24;
        unsigned long   pnode_shift      :  5;
        unsigned long   unused2          :  3;
    } s;
};

/*
 * Local & Global MMR space macros.
 *	Note: macros are intended to be used ONLY by inline functions
 *	in this file - not by other kernel code.
 *		n -  NASID (full 15-bit global nasid)
 *		g -  GNODE (full 15-bit global nasid, right shifted 1)
 *		p -  PNODE (local part of nsids, right shifted 1)
 */
#define UV_NASID_TO_PNODE(n)		(((n) >> 1) & uv_hub_info->pnode_mask)
#define UV_PNODE_TO_GNODE(p)		((p) |uv_hub_info->gnode_extra)
#define UV_PNODE_TO_NASID(p)		(UV_PNODE_TO_GNODE(p) << 1)

#define UV1_LOCAL_MMR_BASE		0xf4000000UL
#define UV1_GLOBAL_MMR32_BASE		0xf8000000UL
#define UV1_LOCAL_MMR_SIZE		(64UL * 1024 * 1024)
#define UV1_GLOBAL_MMR32_SIZE		(64UL * 1024 * 1024)

#define UV2_LOCAL_MMR_BASE		0xfa000000UL
#define UV2_GLOBAL_MMR32_BASE		0xfc000000UL
#define UV2_LOCAL_MMR_SIZE		(32UL * 1024 * 1024)
#define UV2_GLOBAL_MMR32_SIZE		(32UL * 1024 * 1024)

#define UV3_LOCAL_MMR_BASE		0xfa000000UL
#define UV3_GLOBAL_MMR32_BASE		0xfc000000UL
#define UV3_LOCAL_MMR_SIZE		(32UL * 1024 * 1024)
#define UV3_GLOBAL_MMR32_SIZE		(32UL * 1024 * 1024)

#define UV4_LOCAL_MMR_BASE		0xfa000000UL
#define UV4_GLOBAL_MMR32_BASE		0xfc000000UL
#define UV4_LOCAL_MMR_SIZE		(32UL * 1024 * 1024)
#define UV4_GLOBAL_MMR32_SIZE		(16UL * 1024 * 1024)

#define UV_LOCAL_MMR_BASE		(				\
					is_uv1_hub() ? UV1_LOCAL_MMR_BASE : \
					is_uv2_hub() ? UV2_LOCAL_MMR_BASE : \
					is_uv3_hub() ? UV3_LOCAL_MMR_BASE : \
					/*is_uv4_hub*/ UV4_LOCAL_MMR_BASE)

#define UV_GLOBAL_MMR32_BASE		(				\
					is_uv1_hub() ? UV1_GLOBAL_MMR32_BASE : \
					is_uv2_hub() ? UV2_GLOBAL_MMR32_BASE : \
					is_uv3_hub() ? UV3_GLOBAL_MMR32_BASE : \
					/*is_uv4_hub*/ UV4_GLOBAL_MMR32_BASE)

#define UV_LOCAL_MMR_SIZE		(				\
					is_uv1_hub() ? UV1_LOCAL_MMR_SIZE : \
					is_uv2_hub() ? UV2_LOCAL_MMR_SIZE : \
					is_uv3_hub() ? UV3_LOCAL_MMR_SIZE : \
					/*is_uv4_hub*/ UV4_LOCAL_MMR_SIZE)

#define UV_GLOBAL_MMR32_SIZE		(				\
					is_uv1_hub() ? UV1_GLOBAL_MMR32_SIZE : \
					is_uv2_hub() ? UV2_GLOBAL_MMR32_SIZE : \
					is_uv3_hub() ? UV3_GLOBAL_MMR32_SIZE : \
					/*is_uv4_hub*/ UV4_GLOBAL_MMR32_SIZE)

#define UV_GLOBAL_MMR64_BASE		(uv_hub_info->global_mmr_base)

#define UV_GLOBAL_GRU_MMR_BASE		0x4000000

#define UV_GLOBAL_MMR32_PNODE_SHIFT	15
#define _UV_GLOBAL_MMR64_PNODE_SHIFT	26
#define UV_GLOBAL_MMR64_PNODE_SHIFT	(uv_hub_info->global_mmr_shift)

#define UV_GLOBAL_MMR32_PNODE_BITS(p)	((p) << (UV_GLOBAL_MMR32_PNODE_SHIFT))

#define UV_GLOBAL_MMR64_PNODE_BITS(p)					\
	(((unsigned long)(p)) << UV_GLOBAL_MMR64_PNODE_SHIFT)

#define UVH_APICID		0x002D0E00L
#define UV_APIC_PNODE_SHIFT	6

#define UV_APICID_HIBIT_MASK	0xffff0000

/* Local Bus from cpu's perspective */
#define LOCAL_BUS_BASE		0x1c00000
#define LOCAL_BUS_SIZE		(4 * 1024 * 1024)

/*
 * System Controller Interface Reg
 *
 * Note there are NO leds on a UV system.  This register is only
 * used by the system controller to monitor system-wide operation.
 * There are 64 regs per node.  With Nahelem cpus (2 cores per node,
 * 8 cpus per core, 2 threads per cpu) there are 32 cpu threads on
 * a node.
 *
 * The window is located at top of ACPI MMR space
 */
#define SCIR_WINDOW_COUNT	64
#define SCIR_LOCAL_MMR_BASE	(LOCAL_BUS_BASE + \
				 LOCAL_BUS_SIZE - \
				 SCIR_WINDOW_COUNT)

#define SCIR_CPU_HEARTBEAT	0x01	/* timer interrupt */
#define SCIR_CPU_ACTIVITY	0x02	/* not idle */
#define SCIR_CPU_HB_INTERVAL	(HZ)	/* once per second */

/* Loop through all installed blades */
#define for_each_possible_blade(bid)		\
	for ((bid) = 0; (bid) < uv_num_possible_blades(); (bid)++)

/*
 * Macros for converting between kernel virtual addresses, socket local physical
 * addresses, and UV global physical addresses.
 *	Note: use the standard __pa() & __va() macros for converting
 *	      between socket virtual and socket physical addresses.
 */

/* global bits offset - number of local address bits in gpa for this UV arch */
static inline unsigned int uv_gpa_shift(void)
{
	return uv_hub_info->gpa_shift;
}
#define	_uv_gpa_shift

/* Find node that has the address range that contains global address  */
static inline struct uv_gam_range_s *uv_gam_range(unsigned long pa)
{
	struct uv_gam_range_s *gr = uv_hub_info->gr_table;
	unsigned long pal = (pa & uv_hub_info->gpa_mask) >> UV_GAM_RANGE_SHFT;
	int i, num = uv_hub_info->gr_table_len;

	if (gr) {
		for (i = 0; i < num; i++, gr++) {
			if (pal < gr->limit)
				return gr;
		}
	}
	pr_crit("UV: GAM Range for 0x%lx not found at %p!\n", pa, gr);
	BUG();
}

/* Return base address of node that contains global address  */
static inline unsigned long uv_gam_range_base(unsigned long pa)
{
	struct uv_gam_range_s *gr = uv_gam_range(pa);
	int base = gr->base;

	if (base < 0)
		return 0UL;

	return uv_hub_info->gr_table[base].limit;
}

/* socket phys RAM --> UV global NASID (UV4+) */
static inline unsigned long uv_soc_phys_ram_to_nasid(unsigned long paddr)
{
	return uv_gam_range(paddr)->nasid;
}
#define	_uv_soc_phys_ram_to_nasid

/* socket virtual --> UV global NASID (UV4+) */
static inline unsigned long uv_gpa_nasid(void *v)
{
	return uv_soc_phys_ram_to_nasid(__pa(v));
}

/* socket phys RAM --> UV global physical address */
static inline unsigned long uv_soc_phys_ram_to_gpa(unsigned long paddr)
{
	unsigned int m_val = uv_hub_info->m_val;

	if (paddr < uv_hub_info->lowmem_remap_top)
		paddr |= uv_hub_info->lowmem_remap_base;

	if (m_val) {
		paddr |= uv_hub_info->gnode_upper;
		paddr = ((paddr << uv_hub_info->m_shift)
						>> uv_hub_info->m_shift) |
			((paddr >> uv_hub_info->m_val)
						<< uv_hub_info->n_lshift);
	} else {
		paddr |= uv_soc_phys_ram_to_nasid(paddr)
						<< uv_hub_info->gpa_shift;
	}
	return paddr;
}

/* socket virtual --> UV global physical address */
static inline unsigned long uv_gpa(void *v)
{
	return uv_soc_phys_ram_to_gpa(__pa(v));
}

/* Top two bits indicate the requested address is in MMR space.  */
static inline int
uv_gpa_in_mmr_space(unsigned long gpa)
{
	return (gpa >> 62) == 0x3UL;
}

/* UV global physical address --> socket phys RAM */
static inline unsigned long uv_gpa_to_soc_phys_ram(unsigned long gpa)
{
	unsigned long paddr;
	unsigned long remap_base = uv_hub_info->lowmem_remap_base;
	unsigned long remap_top =  uv_hub_info->lowmem_remap_top;
	unsigned int m_val = uv_hub_info->m_val;

	if (m_val)
		gpa = ((gpa << uv_hub_info->m_shift) >> uv_hub_info->m_shift) |
			((gpa >> uv_hub_info->n_lshift) << uv_hub_info->m_val);

	paddr = gpa & uv_hub_info->gpa_mask;
	if (paddr >= remap_base && paddr < remap_base + remap_top)
		paddr -= remap_base;
	return paddr;
}

/* gpa -> gnode */
static inline unsigned long uv_gpa_to_gnode(unsigned long gpa)
{
	unsigned int n_lshift = uv_hub_info->n_lshift;

	if (n_lshift)
		return gpa >> n_lshift;

	return uv_gam_range(gpa)->nasid >> 1;
}

/* gpa -> pnode */
static inline int uv_gpa_to_pnode(unsigned long gpa)
{
	return uv_gpa_to_gnode(gpa) & uv_hub_info->pnode_mask;
}

/* gpa -> node offset */
static inline unsigned long uv_gpa_to_offset(unsigned long gpa)
{
	unsigned int m_shift = uv_hub_info->m_shift;

	if (m_shift)
		return (gpa << m_shift) >> m_shift;

	return (gpa & uv_hub_info->gpa_mask) - uv_gam_range_base(gpa);
}

/* Convert socket to node */
static inline int _uv_socket_to_node(int socket, unsigned short *s2nid)
{
	return s2nid ? s2nid[socket - uv_hub_info->min_socket] : socket;
}

static inline int uv_socket_to_node(int socket)
{
	return _uv_socket_to_node(socket, uv_hub_info->socket_to_node);
}

/* pnode, offset --> socket virtual */
static inline void *uv_pnode_offset_to_vaddr(int pnode, unsigned long offset)
{
	unsigned int m_val = uv_hub_info->m_val;
	unsigned long base;
	unsigned short sockid, node, *p2s;

	if (m_val)
		return __va(((unsigned long)pnode << m_val) | offset);

	p2s = uv_hub_info->pnode_to_socket;
	sockid = p2s ? p2s[pnode - uv_hub_info->min_pnode] : pnode;
	node = uv_socket_to_node(sockid);

	/* limit address of previous socket is our base, except node 0 is 0 */
	if (!node)
		return __va((unsigned long)offset);

	base = (unsigned long)(uv_hub_info->gr_table[node - 1].limit);
	return __va(base << UV_GAM_RANGE_SHFT | offset);
}

/* Extract/Convert a PNODE from an APICID (full apicid, not processor subset) */
static inline int uv_apicid_to_pnode(int apicid)
{
	int pnode = apicid >> uv_hub_info->apic_pnode_shift;
	unsigned short *s2pn = uv_hub_info->socket_to_pnode;

	return s2pn ? s2pn[pnode - uv_hub_info->min_socket] : pnode;
}

/* Convert an apicid to the socket number on the blade */
static inline int uv_apicid_to_socket(int apicid)
{
	if (is_uv1_hub())
		return (apicid >> (uv_hub_info->apic_pnode_shift - 1)) & 1;
	else
		return 0;
}

/*
 * Access global MMRs using the low memory MMR32 space. This region supports
 * faster MMR access but not all MMRs are accessible in this space.
 */
static inline unsigned long *uv_global_mmr32_address(int pnode, unsigned long offset)
{
	return __va(UV_GLOBAL_MMR32_BASE |
		       UV_GLOBAL_MMR32_PNODE_BITS(pnode) | offset);
}

static inline void uv_write_global_mmr32(int pnode, unsigned long offset, unsigned long val)
{
	writeq(val, uv_global_mmr32_address(pnode, offset));
}

static inline unsigned long uv_read_global_mmr32(int pnode, unsigned long offset)
{
	return readq(uv_global_mmr32_address(pnode, offset));
}

/*
 * Access Global MMR space using the MMR space located at the top of physical
 * memory.
 */
static inline volatile void __iomem *uv_global_mmr64_address(int pnode, unsigned long offset)
{
	return __va(UV_GLOBAL_MMR64_BASE |
		    UV_GLOBAL_MMR64_PNODE_BITS(pnode) | offset);
}

static inline void uv_write_global_mmr64(int pnode, unsigned long offset, unsigned long val)
{
	writeq(val, uv_global_mmr64_address(pnode, offset));
}

static inline unsigned long uv_read_global_mmr64(int pnode, unsigned long offset)
{
	return readq(uv_global_mmr64_address(pnode, offset));
}

static inline void uv_write_global_mmr8(int pnode, unsigned long offset, unsigned char val)
{
	writeb(val, uv_global_mmr64_address(pnode, offset));
}

static inline unsigned char uv_read_global_mmr8(int pnode, unsigned long offset)
{
	return readb(uv_global_mmr64_address(pnode, offset));
}

/*
 * Access hub local MMRs. Faster than using global space but only local MMRs
 * are accessible.
 */
static inline unsigned long *uv_local_mmr_address(unsigned long offset)
{
	return __va(UV_LOCAL_MMR_BASE | offset);
}

static inline unsigned long uv_read_local_mmr(unsigned long offset)
{
	return readq(uv_local_mmr_address(offset));
}

static inline void uv_write_local_mmr(unsigned long offset, unsigned long val)
{
	writeq(val, uv_local_mmr_address(offset));
}

static inline unsigned char uv_read_local_mmr8(unsigned long offset)
{
	return readb(uv_local_mmr_address(offset));
}

static inline void uv_write_local_mmr8(unsigned long offset, unsigned char val)
{
	writeb(val, uv_local_mmr_address(offset));
}

/* Blade-local cpu number of current cpu. Numbered 0 .. <# cpus on the blade> */
static inline int uv_blade_processor_id(void)
{
	return uv_cpu_info->blade_cpu_id;
}

/* Blade-local cpu number of cpu N. Numbered 0 .. <# cpus on the blade> */
static inline int uv_cpu_blade_processor_id(int cpu)
{
	return uv_cpu_info_per(cpu)->blade_cpu_id;
}

/* Blade number to Node number (UV1..UV4 is 1:1) */
static inline int uv_blade_to_node(int blade)
{
	return blade;
}

/* Blade number of current cpu. Numnbered 0 .. <#blades -1> */
static inline int uv_numa_blade_id(void)
{
	return uv_hub_info->numa_blade_id;
}

/*
 * Convert linux node number to the UV blade number.
 * .. Currently for UV1 thru UV4 the node and the blade are identical.
 * .. If this changes then you MUST check references to this function!
 */
static inline int uv_node_to_blade_id(int nid)
{
	return nid;
}

/* Convert a cpu number to the the UV blade number */
static inline int uv_cpu_to_blade_id(int cpu)
{
	return uv_node_to_blade_id(cpu_to_node(cpu));
}

/* Convert a blade id to the PNODE of the blade */
static inline int uv_blade_to_pnode(int bid)
{
	return uv_hub_info_list(uv_blade_to_node(bid))->pnode;
}

/* Nid of memory node on blade. -1 if no blade-local memory */
static inline int uv_blade_to_memory_nid(int bid)
{
	return uv_hub_info_list(uv_blade_to_node(bid))->memory_nid;
}

/* Determine the number of possible cpus on a blade */
static inline int uv_blade_nr_possible_cpus(int bid)
{
	return uv_hub_info_list(uv_blade_to_node(bid))->nr_possible_cpus;
}

/* Determine the number of online cpus on a blade */
static inline int uv_blade_nr_online_cpus(int bid)
{
	return uv_hub_info_list(uv_blade_to_node(bid))->nr_online_cpus;
}

/* Convert a cpu id to the PNODE of the blade containing the cpu */
static inline int uv_cpu_to_pnode(int cpu)
{
	return uv_cpu_hub_info(cpu)->pnode;
}

/* Convert a linux node number to the PNODE of the blade */
static inline int uv_node_to_pnode(int nid)
{
	return uv_hub_info_list(nid)->pnode;
}

/* Maximum possible number of blades */
extern short uv_possible_blades;
static inline int uv_num_possible_blades(void)
{
	return uv_possible_blades;
}

/* Per Hub NMI support */
extern void uv_nmi_setup(void);
extern void uv_nmi_setup_hubless(void);

/* BIOS/Kernel flags exchange MMR */
#define UVH_BIOS_KERNEL_MMR		UVH_SCRATCH5
#define UVH_BIOS_KERNEL_MMR_ALIAS	UVH_SCRATCH5_ALIAS
#define UVH_BIOS_KERNEL_MMR_ALIAS_2	UVH_SCRATCH5_ALIAS_2

/* TSC sync valid, set by BIOS */
#define UVH_TSC_SYNC_MMR	UVH_BIOS_KERNEL_MMR
#define UVH_TSC_SYNC_SHIFT	10
#define UVH_TSC_SYNC_SHIFT_UV2K	16	/* UV2/3k have different bits */
#define UVH_TSC_SYNC_MASK	3	/* 0011 */
#define UVH_TSC_SYNC_VALID	3	/* 0011 */
#define UVH_TSC_SYNC_INVALID	2	/* 0010 */

/* BMC sets a bit this MMR non-zero before sending an NMI */
#define UVH_NMI_MMR		UVH_BIOS_KERNEL_MMR
#define UVH_NMI_MMR_CLEAR	UVH_BIOS_KERNEL_MMR_ALIAS
#define UVH_NMI_MMR_SHIFT	63
#define UVH_NMI_MMR_TYPE	"SCRATCH5"

/* Newer SMM NMI handler, not present in all systems */
#define UVH_NMI_MMRX		UVH_EVENT_OCCURRED0
#define UVH_NMI_MMRX_CLEAR	UVH_EVENT_OCCURRED0_ALIAS
#define UVH_NMI_MMRX_SHIFT	UVH_EVENT_OCCURRED0_EXTIO_INT0_SHFT
#define UVH_NMI_MMRX_TYPE	"EXTIO_INT0"

/* Non-zero indicates newer SMM NMI handler present */
#define UVH_NMI_MMRX_SUPPORTED	UVH_EXTIO_INT0_BROADCAST

/* Indicates to BIOS that we want to use the newer SMM NMI handler */
#define UVH_NMI_MMRX_REQ	UVH_BIOS_KERNEL_MMR_ALIAS_2
#define UVH_NMI_MMRX_REQ_SHIFT	62

struct uv_hub_nmi_s {
	raw_spinlock_t	nmi_lock;
	atomic_t	in_nmi;		/* flag this node in UV NMI IRQ */
	atomic_t	cpu_owner;	/* last locker of this struct */
	atomic_t	read_mmr_count;	/* count of MMR reads */
	atomic_t	nmi_count;	/* count of true UV NMIs */
	unsigned long	nmi_value;	/* last value read from NMI MMR */
	bool		hub_present;	/* false means UV hubless system */
	bool		pch_owner;	/* indicates this hub owns PCH */
};

struct uv_cpu_nmi_s {
	struct uv_hub_nmi_s	*hub;
	int			state;
	int			pinging;
	int			queries;
	int			pings;
};

DECLARE_PER_CPU(struct uv_cpu_nmi_s, uv_cpu_nmi);

#define uv_hub_nmi			this_cpu_read(uv_cpu_nmi.hub)
#define uv_cpu_nmi_per(cpu)		(per_cpu(uv_cpu_nmi, cpu))
#define uv_hub_nmi_per(cpu)		(uv_cpu_nmi_per(cpu).hub)

/* uv_cpu_nmi_states */
#define	UV_NMI_STATE_OUT		0
#define	UV_NMI_STATE_IN			1
#define	UV_NMI_STATE_DUMP		2
#define	UV_NMI_STATE_DUMP_DONE		3

/* Update SCIR state */
static inline void uv_set_scir_bits(unsigned char value)
{
	if (uv_scir_info->state != value) {
		uv_scir_info->state = value;
		uv_write_local_mmr8(uv_scir_info->offset, value);
	}
}

static inline unsigned long uv_scir_offset(int apicid)
{
	return SCIR_LOCAL_MMR_BASE | (apicid & 0x3f);
}

static inline void uv_set_cpu_scir_bits(int cpu, unsigned char value)
{
	if (uv_cpu_scir_info(cpu)->state != value) {
		uv_write_global_mmr8(uv_cpu_to_pnode(cpu),
				uv_cpu_scir_info(cpu)->offset, value);
		uv_cpu_scir_info(cpu)->state = value;
	}
}

extern unsigned int uv_apicid_hibits;

/*
 * Get the minimum revision number of the hub chips within the partition.
 * (See UVx_HUB_REVISION_BASE above for specific values.)
 */
static inline int uv_get_min_hub_revision_id(void)
{
	return uv_hub_info->hub_revision;
}

#endif /* CONFIG_X86_64 */
#endif /* _ASM_X86_UV_UV_HUB_H */
