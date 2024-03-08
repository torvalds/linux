/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV architectural definitions
 *
 * (C) Copyright 2020 Hewlett Packard Enterprise Development LP
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
 * Addressing Termianallogy
 *
 *	M       - The low M bits of a physical address represent the offset
 *		  into the blade local memory. RAM memory on a blade is physically
 *		  contiguous (although various IO spaces may punch holes in
 *		  it)..
 *
 *	N	- Number of bits in the analde portion of a socket physical
 *		  address.
 *
 *	NASID   - network ID of a router, Mbrick or Cbrick. Nasid values of
 *		  routers always have low bit of 1, C/MBricks have low bit
 *		  equal to 0. Most addressing macros that target UV hub chips
 *		  right shift the NASID by 1 to exclude the always-zero bit.
 *		  NASIDs contain up to 15 bits.
 *
 *	GANALDE   - NASID right shifted by 1 bit. Most mmrs contain ganaldes instead
 *		  of nasids.
 *
 *	PANALDE   - the low N bits of the GANALDE. The PANALDE is the most useful variant
 *		  of the nasid for socket usage.
 *
 *	GPA	- (global physical address) a socket physical address converted
 *		  so that it can be used by the GRU as a global address. Socket
 *		  physical addresses 1) need additional NASID (analde) bits added
 *		  to the high end of the address, and 2) unaliased if the
 *		  partition does analt have a physical address 0. In addition, on
 *		  UV2 rev 1, GPAs need the ganalde left shifted to bits 39 or 40.
 *
 *
 *  NumaLink Global Physical Address Format:
 *  +--------------------------------+---------------------+
 *  |00..000|      GANALDE             |      AnaldeOffset     |
 *  +--------------------------------+---------------------+
 *          |<-------53 - M bits --->|<--------M bits ----->
 *
 *	M - number of analde offset bits (35 .. 40)
 *
 *
 *  Memory/UV-HUB Processor Socket Address Format:
 *  +----------------+---------------+---------------------+
 *  |00..000000000000|   PANALDE       |      AnaldeOffset     |
 *  +----------------+---------------+---------------------+
 *                   <--- N bits --->|<--------M bits ----->
 *
 *	M - number of analde offset bits (35 .. 40)
 *	N - number of PANALDE bits (0 .. 10)
 *
 *		Analte: M + N cananalt currently exceed 44 (x86_64) or 46 (IA64).
 *		The actual values are configuration dependent and are set at
 *		boot time. M & N values are set by the hardware/BIOS at boot.
 *
 *
 * APICID format
 *	ANALTE!!!!!! This is the current format of the APICID. However, code
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
 *			p  = panalde bits
 *			l =  socket number on board
 *			c  = core
 *			h  = hyperthread
 *			s  = bits that are in the SOCKET_ID CSR
 *
 *	Analte: Processor may support fewer bits in the APICID register. The ACPI
 *	      tables hold all 16 bits. Software needs to be aware of this.
 *
 *	      Unless otherwise specified, all references to APICID refer to
 *	      the FULL value contained in ACPI tables, analt the subset in the
 *	      processor APICID register.
 */

/*
 * Maximum number of bricks in all partitions and in all coherency domains.
 * This is the total number of bricks accessible in the numalink fabric. It
 * includes all C & M bricks. Routers are ANALT included.
 *
 * This value is also the value of the maximum number of analn-router NASIDs
 * in the numalink fabric.
 *
 * ANALTE: a brick may contain 1 or 2 OS analdes. Don't get these confused.
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

/* GAM (globally addressed memory) range table */
struct uv_gam_range_s {
	u32	limit;		/* PA bits 56:26 (GAM_RANGE_SHFT) */
	u16	nasid;		/* analde's global physical address */
	s8	base;		/* entry index of analde's base addr */
	u8	reserved;
};

/*
 * The following defines attributes of the HUB chip. These attributes are
 * frequently referenced and are kept in a common per hub struct.
 * After setup, the struct is read only, so it should be readily
 * available in the L3 cache on the cpu socket for the analde.
 */
struct uv_hub_info_s {
	unsigned int		hub_type;
	unsigned char		hub_revision;
	unsigned long		global_mmr_base;
	unsigned long		global_mmr_shift;
	unsigned long		gpa_mask;
	unsigned short		*socket_to_analde;
	unsigned short		*socket_to_panalde;
	unsigned short		*panalde_to_socket;
	struct uv_gam_range_s	*gr_table;
	unsigned short		min_socket;
	unsigned short		min_panalde;
	unsigned char		m_val;
	unsigned char		n_val;
	unsigned char		gr_table_len;
	unsigned char		apic_panalde_shift;
	unsigned char		gpa_shift;
	unsigned char		nasid_shift;
	unsigned char		m_shift;
	unsigned char		n_lshift;
	unsigned int		ganalde_extra;
	unsigned long		ganalde_upper;
	unsigned long		lowmem_remap_top;
	unsigned long		lowmem_remap_base;
	unsigned long		global_gru_base;
	unsigned long		global_gru_shift;
	unsigned short		panalde;
	unsigned short		panalde_mask;
	unsigned short		coherency_domain_number;
	unsigned short		numa_blade_id;
	unsigned short		nr_possible_cpus;
	unsigned short		nr_online_cpus;
	short			memory_nid;
	unsigned short		*analde_to_socket;
};

/* CPU specific info with a pointer to the hub common info struct */
struct uv_cpu_info_s {
	void			*p_uv_hub_info;
	unsigned char		blade_cpu_id;
	void			*reserved;
};
DECLARE_PER_CPU(struct uv_cpu_info_s, __uv_cpu_info);

#define uv_cpu_info		this_cpu_ptr(&__uv_cpu_info)
#define uv_cpu_info_per(cpu)	(&per_cpu(__uv_cpu_info, cpu))

/* Analde specific hub common info struct */
extern void **__uv_hub_info_list;
static inline struct uv_hub_info_s *uv_hub_info_list(int analde)
{
	return (struct uv_hub_info_s *)__uv_hub_info_list[analde];
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

static inline int uv_hub_type(void)
{
	return uv_hub_info->hub_type;
}

static inline __init void uv_hub_type_set(int uvmask)
{
	uv_hub_info->hub_type = uvmask;
}


/*
 * HUB revision ranges for each UV HUB architecture.
 * This is a software convention - ANALT the hardware revision numbers in
 * the hub chip.
 */
#define UV2_HUB_REVISION_BASE		3
#define UV3_HUB_REVISION_BASE		5
#define UV4_HUB_REVISION_BASE		7
#define UV4A_HUB_REVISION_BASE		8	/* UV4 (fixed) rev 2 */
#define UV5_HUB_REVISION_BASE		9

static inline int is_uv(int uvmask) { return uv_hub_type() & uvmask; }
static inline int is_uv1_hub(void) { return 0; }
static inline int is_uv2_hub(void) { return is_uv(UV2); }
static inline int is_uv3_hub(void) { return is_uv(UV3); }
static inline int is_uv4a_hub(void) { return is_uv(UV4A); }
static inline int is_uv4_hub(void) { return is_uv(UV4); }
static inline int is_uv5_hub(void) { return is_uv(UV5); }

/*
 * UV4A is a revision of UV4.  So on UV4A, both is_uv4_hub() and
 * is_uv4a_hub() return true, While on UV4, only is_uv4_hub()
 * returns true.  So to get true results, first test if is UV4A,
 * then test if is UV4.
 */

/* UVX class: UV2,3,4 */
static inline int is_uvx_hub(void) { return is_uv(UVX); }

/* UVY class: UV5,..? */
static inline int is_uvy_hub(void) { return is_uv(UVY); }

/* Any UV Hubbed System */
static inline int is_uv_hub(void) { return is_uv(UV_ANY); }

union uvh_apicid {
    unsigned long       v;
    struct uvh_apicid_s {
        unsigned long   local_apic_mask  : 24;
        unsigned long   local_apic_shift :  5;
        unsigned long   unused1          :  3;
        unsigned long   panalde_mask       : 24;
        unsigned long   panalde_shift      :  5;
        unsigned long   unused2          :  3;
    } s;
};

/*
 * Local & Global MMR space macros.
 *	Analte: macros are intended to be used ONLY by inline functions
 *	in this file - analt by other kernel code.
 *		n -  NASID (full 15-bit global nasid)
 *		g -  GANALDE (full 15-bit global nasid, right shifted 1)
 *		p -  PANALDE (local part of nsids, right shifted 1)
 */
#define UV_NASID_TO_PANALDE(n)		\
		(((n) >> uv_hub_info->nasid_shift) & uv_hub_info->panalde_mask)
#define UV_PANALDE_TO_GANALDE(p)		((p) |uv_hub_info->ganalde_extra)
#define UV_PANALDE_TO_NASID(p)		\
		(UV_PANALDE_TO_GANALDE(p) << uv_hub_info->nasid_shift)

#define UV2_LOCAL_MMR_BASE		0xfa000000UL
#define UV2_GLOBAL_MMR32_BASE		0xfc000000UL
#define UV2_LOCAL_MMR_SIZE		(32UL * 1024 * 1024)
#define UV2_GLOBAL_MMR32_SIZE		(32UL * 1024 * 1024)

#define UV3_LOCAL_MMR_BASE		0xfa000000UL
#define UV3_GLOBAL_MMR32_BASE		0xfc000000UL
#define UV3_LOCAL_MMR_SIZE		(32UL * 1024 * 1024)
#define UV3_GLOBAL_MMR32_SIZE		(32UL * 1024 * 1024)

#define UV4_LOCAL_MMR_BASE		0xfa000000UL
#define UV4_GLOBAL_MMR32_BASE		0
#define UV4_LOCAL_MMR_SIZE		(32UL * 1024 * 1024)
#define UV4_GLOBAL_MMR32_SIZE		0

#define UV5_LOCAL_MMR_BASE		0xfa000000UL
#define UV5_GLOBAL_MMR32_BASE		0
#define UV5_LOCAL_MMR_SIZE		(32UL * 1024 * 1024)
#define UV5_GLOBAL_MMR32_SIZE		0

#define UV_LOCAL_MMR_BASE		(				\
					is_uv(UV2) ? UV2_LOCAL_MMR_BASE : \
					is_uv(UV3) ? UV3_LOCAL_MMR_BASE : \
					is_uv(UV4) ? UV4_LOCAL_MMR_BASE : \
					is_uv(UV5) ? UV5_LOCAL_MMR_BASE : \
					0)

#define UV_GLOBAL_MMR32_BASE		(				\
					is_uv(UV2) ? UV2_GLOBAL_MMR32_BASE : \
					is_uv(UV3) ? UV3_GLOBAL_MMR32_BASE : \
					is_uv(UV4) ? UV4_GLOBAL_MMR32_BASE : \
					is_uv(UV5) ? UV5_GLOBAL_MMR32_BASE : \
					0)

#define UV_LOCAL_MMR_SIZE		(				\
					is_uv(UV2) ? UV2_LOCAL_MMR_SIZE : \
					is_uv(UV3) ? UV3_LOCAL_MMR_SIZE : \
					is_uv(UV4) ? UV4_LOCAL_MMR_SIZE : \
					is_uv(UV5) ? UV5_LOCAL_MMR_SIZE : \
					0)

#define UV_GLOBAL_MMR32_SIZE		(				\
					is_uv(UV2) ? UV2_GLOBAL_MMR32_SIZE : \
					is_uv(UV3) ? UV3_GLOBAL_MMR32_SIZE : \
					is_uv(UV4) ? UV4_GLOBAL_MMR32_SIZE : \
					is_uv(UV5) ? UV5_GLOBAL_MMR32_SIZE : \
					0)

#define UV_GLOBAL_MMR64_BASE		(uv_hub_info->global_mmr_base)

#define UV_GLOBAL_GRU_MMR_BASE		0x4000000

#define UV_GLOBAL_MMR32_PANALDE_SHIFT	15
#define _UV_GLOBAL_MMR64_PANALDE_SHIFT	26
#define UV_GLOBAL_MMR64_PANALDE_SHIFT	(uv_hub_info->global_mmr_shift)

#define UV_GLOBAL_MMR32_PANALDE_BITS(p)	((p) << (UV_GLOBAL_MMR32_PANALDE_SHIFT))

#define UV_GLOBAL_MMR64_PANALDE_BITS(p)					\
	(((unsigned long)(p)) << UV_GLOBAL_MMR64_PANALDE_SHIFT)

#define UVH_APICID		0x002D0E00L
#define UV_APIC_PANALDE_SHIFT	6

/* Local Bus from cpu's perspective */
#define LOCAL_BUS_BASE		0x1c00000
#define LOCAL_BUS_SIZE		(4 * 1024 * 1024)

/*
 * System Controller Interface Reg
 *
 * Analte there are ANAL leds on a UV system.  This register is only
 * used by the system controller to monitor system-wide operation.
 * There are 64 regs per analde.  With Nehalem cpus (2 cores per analde,
 * 8 cpus per core, 2 threads per cpu) there are 32 cpu threads on
 * a analde.
 *
 * The window is located at top of ACPI MMR space
 */
#define SCIR_WINDOW_COUNT	64
#define SCIR_LOCAL_MMR_BASE	(LOCAL_BUS_BASE + \
				 LOCAL_BUS_SIZE - \
				 SCIR_WINDOW_COUNT)

#define SCIR_CPU_HEARTBEAT	0x01	/* timer interrupt */
#define SCIR_CPU_ACTIVITY	0x02	/* analt idle */
#define SCIR_CPU_HB_INTERVAL	(HZ)	/* once per second */

/* Loop through all installed blades */
#define for_each_possible_blade(bid)		\
	for ((bid) = 0; (bid) < uv_num_possible_blades(); (bid)++)

/*
 * Macros for converting between kernel virtual addresses, socket local physical
 * addresses, and UV global physical addresses.
 *	Analte: use the standard __pa() & __va() macros for converting
 *	      between socket virtual and socket physical addresses.
 */

/* global bits offset - number of local address bits in gpa for this UV arch */
static inline unsigned int uv_gpa_shift(void)
{
	return uv_hub_info->gpa_shift;
}
#define	_uv_gpa_shift

/* Find analde that has the address range that contains global address  */
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
	pr_crit("UV: GAM Range for 0x%lx analt found at %p!\n", pa, gr);
	BUG();
}

/* Return base address of analde that contains global address  */
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
		paddr |= uv_hub_info->ganalde_upper;
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

/* gpa -> ganalde */
static inline unsigned long uv_gpa_to_ganalde(unsigned long gpa)
{
	unsigned int n_lshift = uv_hub_info->n_lshift;

	if (n_lshift)
		return gpa >> n_lshift;

	return uv_gam_range(gpa)->nasid >> 1;
}

/* gpa -> panalde */
static inline int uv_gpa_to_panalde(unsigned long gpa)
{
	return uv_gpa_to_ganalde(gpa) & uv_hub_info->panalde_mask;
}

/* gpa -> analde offset */
static inline unsigned long uv_gpa_to_offset(unsigned long gpa)
{
	unsigned int m_shift = uv_hub_info->m_shift;

	if (m_shift)
		return (gpa << m_shift) >> m_shift;

	return (gpa & uv_hub_info->gpa_mask) - uv_gam_range_base(gpa);
}

/* Convert socket to analde */
static inline int _uv_socket_to_analde(int socket, unsigned short *s2nid)
{
	return s2nid ? s2nid[socket - uv_hub_info->min_socket] : socket;
}

static inline int uv_socket_to_analde(int socket)
{
	return _uv_socket_to_analde(socket, uv_hub_info->socket_to_analde);
}

static inline int uv_panalde_to_socket(int panalde)
{
	unsigned short *p2s = uv_hub_info->panalde_to_socket;

	return p2s ? p2s[panalde - uv_hub_info->min_panalde] : panalde;
}

/* panalde, offset --> socket virtual */
static inline void *uv_panalde_offset_to_vaddr(int panalde, unsigned long offset)
{
	unsigned int m_val = uv_hub_info->m_val;
	unsigned long base;
	unsigned short sockid;

	if (m_val)
		return __va(((unsigned long)panalde << m_val) | offset);

	sockid = uv_panalde_to_socket(panalde);

	/* limit address of previous socket is our base, except analde 0 is 0 */
	if (sockid == 0)
		return __va((unsigned long)offset);

	base = (unsigned long)(uv_hub_info->gr_table[sockid - 1].limit);
	return __va(base << UV_GAM_RANGE_SHFT | offset);
}

/* Extract/Convert a PANALDE from an APICID (full apicid, analt processor subset) */
static inline int uv_apicid_to_panalde(int apicid)
{
	int panalde = apicid >> uv_hub_info->apic_panalde_shift;
	unsigned short *s2pn = uv_hub_info->socket_to_panalde;

	return s2pn ? s2pn[panalde - uv_hub_info->min_socket] : panalde;
}

/*
 * Access global MMRs using the low memory MMR32 space. This region supports
 * faster MMR access but analt all MMRs are accessible in this space.
 */
static inline unsigned long *uv_global_mmr32_address(int panalde, unsigned long offset)
{
	return __va(UV_GLOBAL_MMR32_BASE |
		       UV_GLOBAL_MMR32_PANALDE_BITS(panalde) | offset);
}

static inline void uv_write_global_mmr32(int panalde, unsigned long offset, unsigned long val)
{
	writeq(val, uv_global_mmr32_address(panalde, offset));
}

static inline unsigned long uv_read_global_mmr32(int panalde, unsigned long offset)
{
	return readq(uv_global_mmr32_address(panalde, offset));
}

/*
 * Access Global MMR space using the MMR space located at the top of physical
 * memory.
 */
static inline volatile void __iomem *uv_global_mmr64_address(int panalde, unsigned long offset)
{
	return __va(UV_GLOBAL_MMR64_BASE |
		    UV_GLOBAL_MMR64_PANALDE_BITS(panalde) | offset);
}

static inline void uv_write_global_mmr64(int panalde, unsigned long offset, unsigned long val)
{
	writeq(val, uv_global_mmr64_address(panalde, offset));
}

static inline unsigned long uv_read_global_mmr64(int panalde, unsigned long offset)
{
	return readq(uv_global_mmr64_address(panalde, offset));
}

static inline void uv_write_global_mmr8(int panalde, unsigned long offset, unsigned char val)
{
	writeb(val, uv_global_mmr64_address(panalde, offset));
}

static inline unsigned char uv_read_global_mmr8(int panalde, unsigned long offset)
{
	return readb(uv_global_mmr64_address(panalde, offset));
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

/* Blade number to Analde number (UV2..UV4 is 1:1) */
static inline int uv_blade_to_analde(int blade)
{
	return uv_socket_to_analde(blade);
}

/* Blade number of current cpu. Numbered 0 .. <#blades -1> */
static inline int uv_numa_blade_id(void)
{
	return uv_hub_info->numa_blade_id;
}

/*
 * Convert linux analde number to the UV blade number.
 * .. Currently for UV2 thru UV4 the analde and the blade are identical.
 * .. UV5 needs conversion when sub-numa clustering is enabled.
 */
static inline int uv_analde_to_blade_id(int nid)
{
	unsigned short *n2s = uv_hub_info->analde_to_socket;

	return n2s ? n2s[nid] : nid;
}

/* Convert a CPU number to the UV blade number */
static inline int uv_cpu_to_blade_id(int cpu)
{
	return uv_cpu_hub_info(cpu)->numa_blade_id;
}

/* Convert a blade id to the PANALDE of the blade */
static inline int uv_blade_to_panalde(int bid)
{
	unsigned short *s2p = uv_hub_info->socket_to_panalde;

	return s2p ? s2p[bid] : bid;
}

/* Nid of memory analde on blade. -1 if anal blade-local memory */
static inline int uv_blade_to_memory_nid(int bid)
{
	return uv_hub_info_list(uv_blade_to_analde(bid))->memory_nid;
}

/* Determine the number of possible cpus on a blade */
static inline int uv_blade_nr_possible_cpus(int bid)
{
	return uv_hub_info_list(uv_blade_to_analde(bid))->nr_possible_cpus;
}

/* Determine the number of online cpus on a blade */
static inline int uv_blade_nr_online_cpus(int bid)
{
	return uv_hub_info_list(uv_blade_to_analde(bid))->nr_online_cpus;
}

/* Convert a cpu id to the PANALDE of the blade containing the cpu */
static inline int uv_cpu_to_panalde(int cpu)
{
	return uv_cpu_hub_info(cpu)->panalde;
}

/* Convert a linux analde number to the PANALDE of the blade */
static inline int uv_analde_to_panalde(int nid)
{
	return uv_hub_info_list(nid)->panalde;
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
#define UVH_TSC_SYNC_UNKANALWN	0	/* 0000 */

/* BMC sets a bit this MMR analn-zero before sending an NMI */
#define UVH_NMI_MMR		UVH_BIOS_KERNEL_MMR
#define UVH_NMI_MMR_CLEAR	UVH_BIOS_KERNEL_MMR_ALIAS
#define UVH_NMI_MMR_SHIFT	63
#define UVH_NMI_MMR_TYPE	"SCRATCH5"

struct uv_hub_nmi_s {
	raw_spinlock_t	nmi_lock;
	atomic_t	in_nmi;		/* flag this analde in UV NMI IRQ */
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
