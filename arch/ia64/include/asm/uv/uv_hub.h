/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV architectural definitions
 *
 * Copyright (C) 2008 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef __ASM_IA64_UV_HUB_H__
#define __ASM_IA64_UV_HUB_H__

#include <linux/numa.h>
#include <linux/percpu.h>
#include <asm/types.h>
#include <asm/percpu.h>


/*
 * Addressing Terminology
 *
 *	M       - The low M bits of a physical address represent the offset
 *		  into the blade local memory. RAM memory on a blade is physically
 *		  contiguous (although various IO spaces may punch holes in
 *		  it)..
 *
 * 	N	- Number of bits in the node portion of a socket physical
 * 		  address.
 *
 * 	NASID   - network ID of a router, Mbrick or Cbrick. Nasid values of
 * 	 	  routers always have low bit of 1, C/MBricks have low bit
 * 		  equal to 0. Most addressing macros that target UV hub chips
 * 		  right shift the NASID by 1 to exclude the always-zero bit.
 * 		  NASIDs contain up to 15 bits.
 *
 *	GNODE   - NASID right shifted by 1 bit. Most mmrs contain gnodes instead
 *		  of nasids.
 *
 * 	PNODE   - the low N bits of the GNODE. The PNODE is the most useful variant
 * 		  of the nasid for socket usage.
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
#define UV_MAX_SSI_BLADES	1

/*
 * The largest possible NASID of a C or M brick (+ 2)
 */
#define UV_MAX_NASID_VALUE	(UV_MAX_NUMALINK_NODES * 2)

/*
 * The following defines attributes of the HUB chip. These attributes are
 * frequently referenced and are kept in the per-cpu data areas of each cpu.
 * They are kept together in a struct to minimize cache misses.
 */
struct uv_hub_info_s {
	unsigned long	global_mmr_base;
	unsigned long	gpa_mask;
	unsigned long	gnode_upper;
	unsigned long	lowmem_remap_top;
	unsigned long	lowmem_remap_base;
	unsigned short	pnode;
	unsigned short	pnode_mask;
	unsigned short	coherency_domain_number;
	unsigned short	numa_blade_id;
	unsigned char	blade_processor_id;
	unsigned char	m_val;
	unsigned char	n_val;
};
DECLARE_PER_CPU(struct uv_hub_info_s, __uv_hub_info);
#define uv_hub_info 		this_cpu_ptr(&__uv_hub_info)
#define uv_cpu_hub_info(cpu)	(&per_cpu(__uv_hub_info, cpu))

/*
 * Local & Global MMR space macros.
 * 	Note: macros are intended to be used ONLY by inline functions
 * 	in this file - not by other kernel code.
 * 		n -  NASID (full 15-bit global nasid)
 * 		g -  GNODE (full 15-bit global nasid, right shifted 1)
 * 		p -  PNODE (local part of nsids, right shifted 1)
 */
#define UV_NASID_TO_PNODE(n)		(((n) >> 1) & uv_hub_info->pnode_mask)
#define UV_PNODE_TO_NASID(p)		(((p) << 1) | uv_hub_info->gnode_upper)

#define UV_LOCAL_MMR_BASE		0xf4000000UL
#define UV_GLOBAL_MMR32_BASE		0xf8000000UL
#define UV_GLOBAL_MMR64_BASE		(uv_hub_info->global_mmr_base)

#define UV_GLOBAL_MMR32_PNODE_SHIFT	15
#define UV_GLOBAL_MMR64_PNODE_SHIFT	26

#define UV_GLOBAL_MMR32_PNODE_BITS(p)	((p) << (UV_GLOBAL_MMR32_PNODE_SHIFT))

#define UV_GLOBAL_MMR64_PNODE_BITS(p)					\
	((unsigned long)(p) << UV_GLOBAL_MMR64_PNODE_SHIFT)

/*
 * Macros for converting between kernel virtual addresses, socket local physical
 * addresses, and UV global physical addresses.
 * 	Note: use the standard __pa() & __va() macros for converting
 * 	      between socket virtual and socket physical addresses.
 */

/* socket phys RAM --> UV global physical address */
static inline unsigned long uv_soc_phys_ram_to_gpa(unsigned long paddr)
{
	if (paddr < uv_hub_info->lowmem_remap_top)
		paddr += uv_hub_info->lowmem_remap_base;
	return paddr | uv_hub_info->gnode_upper;
}


/* socket virtual --> UV global physical address */
static inline unsigned long uv_gpa(void *v)
{
	return __pa(v) | uv_hub_info->gnode_upper;
}

/* socket virtual --> UV global physical address */
static inline void *uv_vgpa(void *v)
{
	return (void *)uv_gpa(v);
}

/* UV global physical address --> socket virtual */
static inline void *uv_va(unsigned long gpa)
{
	return __va(gpa & uv_hub_info->gpa_mask);
}

/* pnode, offset --> socket virtual */
static inline void *uv_pnode_offset_to_vaddr(int pnode, unsigned long offset)
{
	return __va(((unsigned long)pnode << uv_hub_info->m_val) | offset);
}


/*
 * Access global MMRs using the low memory MMR32 space. This region supports
 * faster MMR access but not all MMRs are accessible in this space.
 */
static inline unsigned long *uv_global_mmr32_address(int pnode,
				unsigned long offset)
{
	return __va(UV_GLOBAL_MMR32_BASE |
		       UV_GLOBAL_MMR32_PNODE_BITS(pnode) | offset);
}

static inline void uv_write_global_mmr32(int pnode, unsigned long offset,
				 unsigned long val)
{
	*uv_global_mmr32_address(pnode, offset) = val;
}

static inline unsigned long uv_read_global_mmr32(int pnode,
						 unsigned long offset)
{
	return *uv_global_mmr32_address(pnode, offset);
}

/*
 * Access Global MMR space using the MMR space located at the top of physical
 * memory.
 */
static inline unsigned long *uv_global_mmr64_address(int pnode,
				unsigned long offset)
{
	return __va(UV_GLOBAL_MMR64_BASE |
		    UV_GLOBAL_MMR64_PNODE_BITS(pnode) | offset);
}

static inline void uv_write_global_mmr64(int pnode, unsigned long offset,
				unsigned long val)
{
	*uv_global_mmr64_address(pnode, offset) = val;
}

static inline unsigned long uv_read_global_mmr64(int pnode,
						 unsigned long offset)
{
	return *uv_global_mmr64_address(pnode, offset);
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
	return *uv_local_mmr_address(offset);
}

static inline void uv_write_local_mmr(unsigned long offset, unsigned long val)
{
	*uv_local_mmr_address(offset) = val;
}

/*
 * Structures and definitions for converting between cpu, node, pnode, and blade
 * numbers.
 */

/* Blade-local cpu number of current cpu. Numbered 0 .. <# cpus on the blade> */
static inline int uv_blade_processor_id(void)
{
	return smp_processor_id();
}

/* Blade number of current cpu. Numnbered 0 .. <#blades -1> */
static inline int uv_numa_blade_id(void)
{
	return 0;
}

/* Convert a cpu number to the the UV blade number */
static inline int uv_cpu_to_blade_id(int cpu)
{
	return 0;
}

/* Convert linux node number to the UV blade number */
static inline int uv_node_to_blade_id(int nid)
{
	return 0;
}

/* Convert a blade id to the PNODE of the blade */
static inline int uv_blade_to_pnode(int bid)
{
	return 0;
}

/* Determine the number of possible cpus on a blade */
static inline int uv_blade_nr_possible_cpus(int bid)
{
	return num_possible_cpus();
}

/* Determine the number of online cpus on a blade */
static inline int uv_blade_nr_online_cpus(int bid)
{
	return num_online_cpus();
}

/* Convert a cpu id to the PNODE of the blade containing the cpu */
static inline int uv_cpu_to_pnode(int cpu)
{
	return 0;
}

/* Convert a linux node number to the PNODE of the blade */
static inline int uv_node_to_pnode(int nid)
{
	return 0;
}

/* Maximum possible number of blades */
static inline int uv_num_possible_blades(void)
{
	return 1;
}

static inline void uv_hub_send_ipi(int pnode, int apicid, int vector)
{
	/* not currently needed on ia64 */
}


#endif /* __ASM_IA64_UV_HUB__ */

