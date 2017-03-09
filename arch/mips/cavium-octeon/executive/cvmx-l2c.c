/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2017 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/*
 * Implementation of the Level 2 Cache (L2C) control,
 * measurement, and debugging facilities.
 */

#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-l2c.h>
#include <asm/octeon/cvmx-spinlock.h>

/*
 * This spinlock is used internally to ensure that only one core is
 * performing certain L2 operations at a time.
 *
 * NOTE: This only protects calls from within a single application -
 * if multiple applications or operating systems are running, then it
 * is up to the user program to coordinate between them.
 */
cvmx_spinlock_t cvmx_l2c_spinlock;

int cvmx_l2c_get_core_way_partition(uint32_t core)
{
	uint32_t field;

	/* Validate the core number */
	if (core >= cvmx_octeon_num_cores())
		return -1;

	if (OCTEON_IS_MODEL(OCTEON_CN63XX))
		return cvmx_read_csr(CVMX_L2C_WPAR_PPX(core)) & 0xffff;

	/*
	 * Use the lower two bits of the coreNumber to determine the
	 * bit offset of the UMSK[] field in the L2C_SPAR register.
	 */
	field = (core & 0x3) * 8;

	/*
	 * Return the UMSK[] field from the appropriate L2C_SPAR
	 * register based on the coreNumber.
	 */

	switch (core & 0xC) {
	case 0x0:
		return (cvmx_read_csr(CVMX_L2C_SPAR0) & (0xFF << field)) >> field;
	case 0x4:
		return (cvmx_read_csr(CVMX_L2C_SPAR1) & (0xFF << field)) >> field;
	case 0x8:
		return (cvmx_read_csr(CVMX_L2C_SPAR2) & (0xFF << field)) >> field;
	case 0xC:
		return (cvmx_read_csr(CVMX_L2C_SPAR3) & (0xFF << field)) >> field;
	}
	return 0;
}

int cvmx_l2c_set_core_way_partition(uint32_t core, uint32_t mask)
{
	uint32_t field;
	uint32_t valid_mask;

	valid_mask = (0x1 << cvmx_l2c_get_num_assoc()) - 1;

	mask &= valid_mask;

	/* A UMSK setting which blocks all L2C Ways is an error on some chips */
	if (mask == valid_mask && !OCTEON_IS_MODEL(OCTEON_CN63XX))
		return -1;

	/* Validate the core number */
	if (core >= cvmx_octeon_num_cores())
		return -1;

	if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		cvmx_write_csr(CVMX_L2C_WPAR_PPX(core), mask);
		return 0;
	}

	/*
	 * Use the lower two bits of core to determine the bit offset of the
	 * UMSK[] field in the L2C_SPAR register.
	 */
	field = (core & 0x3) * 8;

	/*
	 * Assign the new mask setting to the UMSK[] field in the appropriate
	 * L2C_SPAR register based on the core_num.
	 *
	 */
	switch (core & 0xC) {
	case 0x0:
		cvmx_write_csr(CVMX_L2C_SPAR0,
			       (cvmx_read_csr(CVMX_L2C_SPAR0) & ~(0xFF << field)) |
			       mask << field);
		break;
	case 0x4:
		cvmx_write_csr(CVMX_L2C_SPAR1,
			       (cvmx_read_csr(CVMX_L2C_SPAR1) & ~(0xFF << field)) |
			       mask << field);
		break;
	case 0x8:
		cvmx_write_csr(CVMX_L2C_SPAR2,
			       (cvmx_read_csr(CVMX_L2C_SPAR2) & ~(0xFF << field)) |
			       mask << field);
		break;
	case 0xC:
		cvmx_write_csr(CVMX_L2C_SPAR3,
			       (cvmx_read_csr(CVMX_L2C_SPAR3) & ~(0xFF << field)) |
			       mask << field);
		break;
	}
	return 0;
}

int cvmx_l2c_set_hw_way_partition(uint32_t mask)
{
	uint32_t valid_mask;

	valid_mask = (0x1 << cvmx_l2c_get_num_assoc()) - 1;
	mask &= valid_mask;

	/* A UMSK setting which blocks all L2C Ways is an error on some chips */
	if (mask == valid_mask	&& !OCTEON_IS_MODEL(OCTEON_CN63XX))
		return -1;

	if (OCTEON_IS_MODEL(OCTEON_CN63XX))
		cvmx_write_csr(CVMX_L2C_WPAR_IOBX(0), mask);
	else
		cvmx_write_csr(CVMX_L2C_SPAR4,
			       (cvmx_read_csr(CVMX_L2C_SPAR4) & ~0xFF) | mask);
	return 0;
}

int cvmx_l2c_get_hw_way_partition(void)
{
	if (OCTEON_IS_MODEL(OCTEON_CN63XX))
		return cvmx_read_csr(CVMX_L2C_WPAR_IOBX(0)) & 0xffff;
	else
		return cvmx_read_csr(CVMX_L2C_SPAR4) & (0xFF);
}

void cvmx_l2c_config_perf(uint32_t counter, enum cvmx_l2c_event event,
			  uint32_t clear_on_read)
{
	if (OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
		union cvmx_l2c_pfctl pfctl;

		pfctl.u64 = cvmx_read_csr(CVMX_L2C_PFCTL);

		switch (counter) {
		case 0:
			pfctl.s.cnt0sel = event;
			pfctl.s.cnt0ena = 1;
			pfctl.s.cnt0rdclr = clear_on_read;
			break;
		case 1:
			pfctl.s.cnt1sel = event;
			pfctl.s.cnt1ena = 1;
			pfctl.s.cnt1rdclr = clear_on_read;
			break;
		case 2:
			pfctl.s.cnt2sel = event;
			pfctl.s.cnt2ena = 1;
			pfctl.s.cnt2rdclr = clear_on_read;
			break;
		case 3:
		default:
			pfctl.s.cnt3sel = event;
			pfctl.s.cnt3ena = 1;
			pfctl.s.cnt3rdclr = clear_on_read;
			break;
		}

		cvmx_write_csr(CVMX_L2C_PFCTL, pfctl.u64);
	} else {
		union cvmx_l2c_tadx_prf l2c_tadx_prf;
		int tad;

		cvmx_dprintf("L2C performance counter events are different for this chip, mapping 'event' to cvmx_l2c_tad_event_t\n");
		if (clear_on_read)
			cvmx_dprintf("L2C counters don't support clear on read for this chip\n");

		l2c_tadx_prf.u64 = cvmx_read_csr(CVMX_L2C_TADX_PRF(0));

		switch (counter) {
		case 0:
			l2c_tadx_prf.s.cnt0sel = event;
			break;
		case 1:
			l2c_tadx_prf.s.cnt1sel = event;
			break;
		case 2:
			l2c_tadx_prf.s.cnt2sel = event;
			break;
		default:
		case 3:
			l2c_tadx_prf.s.cnt3sel = event;
			break;
		}
		for (tad = 0; tad < CVMX_L2C_TADS; tad++)
			cvmx_write_csr(CVMX_L2C_TADX_PRF(tad),
				       l2c_tadx_prf.u64);
	}
}

uint64_t cvmx_l2c_read_perf(uint32_t counter)
{
	switch (counter) {
	case 0:
		if (OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN3XXX))
			return cvmx_read_csr(CVMX_L2C_PFC0);
		else {
			uint64_t counter = 0;
			int tad;

			for (tad = 0; tad < CVMX_L2C_TADS; tad++)
				counter += cvmx_read_csr(CVMX_L2C_TADX_PFC0(tad));
			return counter;
		}
	case 1:
		if (OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN3XXX))
			return cvmx_read_csr(CVMX_L2C_PFC1);
		else {
			uint64_t counter = 0;
			int tad;

			for (tad = 0; tad < CVMX_L2C_TADS; tad++)
				counter += cvmx_read_csr(CVMX_L2C_TADX_PFC1(tad));
			return counter;
		}
	case 2:
		if (OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN3XXX))
			return cvmx_read_csr(CVMX_L2C_PFC2);
		else {
			uint64_t counter = 0;
			int tad;

			for (tad = 0; tad < CVMX_L2C_TADS; tad++)
				counter += cvmx_read_csr(CVMX_L2C_TADX_PFC2(tad));
			return counter;
		}
	case 3:
	default:
		if (OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN3XXX))
			return cvmx_read_csr(CVMX_L2C_PFC3);
		else {
			uint64_t counter = 0;
			int tad;

			for (tad = 0; tad < CVMX_L2C_TADS; tad++)
				counter += cvmx_read_csr(CVMX_L2C_TADX_PFC3(tad));
			return counter;
		}
	}
}

/**
 * @INTERNAL
 * Helper function use to fault in cache lines for L2 cache locking
 *
 * @addr:   Address of base of memory region to read into L2 cache
 * @len:    Length (in bytes) of region to fault in
 */
static void fault_in(uint64_t addr, int len)
{
	char *ptr;

	/*
	 * Adjust addr and length so we get all cache lines even for
	 * small ranges spanning two cache lines.
	 */
	len += addr & CVMX_CACHE_LINE_MASK;
	addr &= ~CVMX_CACHE_LINE_MASK;
	ptr = cvmx_phys_to_ptr(addr);
	/*
	 * Invalidate L1 cache to make sure all loads result in data
	 * being in L2.
	 */
	CVMX_DCACHE_INVALIDATE;
	while (len > 0) {
		READ_ONCE(*ptr);
		len -= CVMX_CACHE_LINE_SIZE;
		ptr += CVMX_CACHE_LINE_SIZE;
	}
}

int cvmx_l2c_lock_line(uint64_t addr)
{
	if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		int shift = CVMX_L2C_TAG_ADDR_ALIAS_SHIFT;
		uint64_t assoc = cvmx_l2c_get_num_assoc();
		uint64_t tag = addr >> shift;
		uint64_t index = CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS, cvmx_l2c_address_to_index(addr) << CVMX_L2C_IDX_ADDR_SHIFT);
		uint64_t way;
		union cvmx_l2c_tadx_tag l2c_tadx_tag;

		CVMX_CACHE_LCKL2(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS, addr), 0);

		/* Make sure we were able to lock the line */
		for (way = 0; way < assoc; way++) {
			CVMX_CACHE_LTGL2I(index | (way << shift), 0);
			/* make sure CVMX_L2C_TADX_TAG is updated */
			CVMX_SYNC;
			l2c_tadx_tag.u64 = cvmx_read_csr(CVMX_L2C_TADX_TAG(0));
			if (l2c_tadx_tag.s.valid && l2c_tadx_tag.s.tag == tag)
				break;
		}

		/* Check if a valid line is found */
		if (way >= assoc) {
			/* cvmx_dprintf("ERROR: cvmx_l2c_lock_line: line not found for locking at 0x%llx address\n", (unsigned long long)addr); */
			return -1;
		}

		/* Check if lock bit is not set */
		if (!l2c_tadx_tag.s.lock) {
			/* cvmx_dprintf("ERROR: cvmx_l2c_lock_line: Not able to lock at 0x%llx address\n", (unsigned long long)addr); */
			return -1;
		}
		return way;
	} else {
		int retval = 0;
		union cvmx_l2c_dbg l2cdbg;
		union cvmx_l2c_lckbase lckbase;
		union cvmx_l2c_lckoff lckoff;
		union cvmx_l2t_err l2t_err;

		cvmx_spinlock_lock(&cvmx_l2c_spinlock);

		l2cdbg.u64 = 0;
		lckbase.u64 = 0;
		lckoff.u64 = 0;

		/* Clear l2t error bits if set */
		l2t_err.u64 = cvmx_read_csr(CVMX_L2T_ERR);
		l2t_err.s.lckerr = 1;
		l2t_err.s.lckerr2 = 1;
		cvmx_write_csr(CVMX_L2T_ERR, l2t_err.u64);

		addr &= ~CVMX_CACHE_LINE_MASK;

		/* Set this core as debug core */
		l2cdbg.s.ppnum = cvmx_get_core_num();
		CVMX_SYNC;
		cvmx_write_csr(CVMX_L2C_DBG, l2cdbg.u64);
		cvmx_read_csr(CVMX_L2C_DBG);

		lckoff.s.lck_offset = 0; /* Only lock 1 line at a time */
		cvmx_write_csr(CVMX_L2C_LCKOFF, lckoff.u64);
		cvmx_read_csr(CVMX_L2C_LCKOFF);

		if (((union cvmx_l2c_cfg)(cvmx_read_csr(CVMX_L2C_CFG))).s.idxalias) {
			int alias_shift = CVMX_L2C_IDX_ADDR_SHIFT + 2 * CVMX_L2_SET_BITS - 1;
			uint64_t addr_tmp = addr ^ (addr & ((1 << alias_shift) - 1)) >> CVMX_L2_SET_BITS;

			lckbase.s.lck_base = addr_tmp >> 7;

		} else {
			lckbase.s.lck_base = addr >> 7;
		}

		lckbase.s.lck_ena = 1;
		cvmx_write_csr(CVMX_L2C_LCKBASE, lckbase.u64);
		/* Make sure it gets there */
		cvmx_read_csr(CVMX_L2C_LCKBASE);

		fault_in(addr, CVMX_CACHE_LINE_SIZE);

		lckbase.s.lck_ena = 0;
		cvmx_write_csr(CVMX_L2C_LCKBASE, lckbase.u64);
		/* Make sure it gets there */
		cvmx_read_csr(CVMX_L2C_LCKBASE);

		/* Stop being debug core */
		cvmx_write_csr(CVMX_L2C_DBG, 0);
		cvmx_read_csr(CVMX_L2C_DBG);

		l2t_err.u64 = cvmx_read_csr(CVMX_L2T_ERR);
		if (l2t_err.s.lckerr || l2t_err.s.lckerr2)
			retval = 1;  /* We were unable to lock the line */

		cvmx_spinlock_unlock(&cvmx_l2c_spinlock);
		return retval;
	}
}

int cvmx_l2c_lock_mem_region(uint64_t start, uint64_t len)
{
	int retval = 0;

	/* Round start/end to cache line boundaries */
	len += start & CVMX_CACHE_LINE_MASK;
	start &= ~CVMX_CACHE_LINE_MASK;
	len = (len + CVMX_CACHE_LINE_MASK) & ~CVMX_CACHE_LINE_MASK;

	while (len) {
		retval += cvmx_l2c_lock_line(start);
		start += CVMX_CACHE_LINE_SIZE;
		len -= CVMX_CACHE_LINE_SIZE;
	}
	return retval;
}

void cvmx_l2c_flush(void)
{
	uint64_t assoc, set;
	uint64_t n_assoc, n_set;

	n_set = cvmx_l2c_get_num_sets();
	n_assoc = cvmx_l2c_get_num_assoc();

	if (OCTEON_IS_MODEL(OCTEON_CN6XXX)) {
		uint64_t address;
		/* These may look like constants, but they aren't... */
		int assoc_shift = CVMX_L2C_TAG_ADDR_ALIAS_SHIFT;
		int set_shift = CVMX_L2C_IDX_ADDR_SHIFT;

		for (set = 0; set < n_set; set++) {
			for (assoc = 0; assoc < n_assoc; assoc++) {
				address = CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						       (assoc << assoc_shift) | (set << set_shift));
				CVMX_CACHE_WBIL2I(address, 0);
			}
		}
	} else {
		for (set = 0; set < n_set; set++)
			for (assoc = 0; assoc < n_assoc; assoc++)
				cvmx_l2c_flush_line(assoc, set);
	}
}


int cvmx_l2c_unlock_line(uint64_t address)
{

	if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		int assoc;
		union cvmx_l2c_tag tag;
		uint32_t tag_addr;
		uint32_t index = cvmx_l2c_address_to_index(address);

		tag_addr = ((address >> CVMX_L2C_TAG_ADDR_ALIAS_SHIFT) & ((1 << CVMX_L2C_TAG_ADDR_ALIAS_SHIFT) - 1));

		/*
		 * For 63XX, we can flush a line by using the physical
		 * address directly, so finding the cache line used by
		 * the address is only required to provide the proper
		 * return value for the function.
		 */
		for (assoc = 0; assoc < CVMX_L2_ASSOC; assoc++) {
			tag = cvmx_l2c_get_tag(assoc, index);

			if (tag.s.V && (tag.s.addr == tag_addr)) {
				CVMX_CACHE_WBIL2(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS, address), 0);
				return tag.s.L;
			}
		}
	} else {
		int assoc;
		union cvmx_l2c_tag tag;
		uint32_t tag_addr;

		uint32_t index = cvmx_l2c_address_to_index(address);

		/* Compute portion of address that is stored in tag */
		tag_addr = ((address >> CVMX_L2C_TAG_ADDR_ALIAS_SHIFT) & ((1 << CVMX_L2C_TAG_ADDR_ALIAS_SHIFT) - 1));
		for (assoc = 0; assoc < CVMX_L2_ASSOC; assoc++) {
			tag = cvmx_l2c_get_tag(assoc, index);

			if (tag.s.V && (tag.s.addr == tag_addr)) {
				cvmx_l2c_flush_line(assoc, index);
				return tag.s.L;
			}
		}
	}
	return 0;
}

int cvmx_l2c_unlock_mem_region(uint64_t start, uint64_t len)
{
	int num_unlocked = 0;
	/* Round start/end to cache line boundaries */
	len += start & CVMX_CACHE_LINE_MASK;
	start &= ~CVMX_CACHE_LINE_MASK;
	len = (len + CVMX_CACHE_LINE_MASK) & ~CVMX_CACHE_LINE_MASK;
	while (len > 0) {
		num_unlocked += cvmx_l2c_unlock_line(start);
		start += CVMX_CACHE_LINE_SIZE;
		len -= CVMX_CACHE_LINE_SIZE;
	}

	return num_unlocked;
}

/*
 * Internal l2c tag types.  These are converted to a generic structure
 * that can be used on all chips.
 */
union __cvmx_l2c_tag {
	uint64_t u64;
	struct cvmx_l2c_tag_cn50xx {
		__BITFIELD_FIELD(uint64_t reserved:40,
		__BITFIELD_FIELD(uint64_t V:1,		/* Line valid */
		__BITFIELD_FIELD(uint64_t D:1,		/* Line dirty */
		__BITFIELD_FIELD(uint64_t L:1,		/* Line locked */
		__BITFIELD_FIELD(uint64_t U:1,		/* Use, LRU eviction */
		__BITFIELD_FIELD(uint64_t addr:20,	/* Phys addr (33..14) */
		;))))))
	} cn50xx;
	struct cvmx_l2c_tag_cn30xx {
		__BITFIELD_FIELD(uint64_t reserved:41,
		__BITFIELD_FIELD(uint64_t V:1,		/* Line valid */
		__BITFIELD_FIELD(uint64_t D:1,		/* Line dirty */
		__BITFIELD_FIELD(uint64_t L:1,		/* Line locked */
		__BITFIELD_FIELD(uint64_t U:1,		/* Use, LRU eviction */
		__BITFIELD_FIELD(uint64_t addr:19,	/* Phys addr (33..15) */
		;))))))
	} cn30xx;
	struct cvmx_l2c_tag_cn31xx {
		__BITFIELD_FIELD(uint64_t reserved:42,
		__BITFIELD_FIELD(uint64_t V:1,		/* Line valid */
		__BITFIELD_FIELD(uint64_t D:1,		/* Line dirty */
		__BITFIELD_FIELD(uint64_t L:1,		/* Line locked */
		__BITFIELD_FIELD(uint64_t U:1,		/* Use, LRU eviction */
		__BITFIELD_FIELD(uint64_t addr:18,	/* Phys addr (33..16) */
		;))))))
	} cn31xx;
	struct cvmx_l2c_tag_cn38xx {
		__BITFIELD_FIELD(uint64_t reserved:43,
		__BITFIELD_FIELD(uint64_t V:1,		/* Line valid */
		__BITFIELD_FIELD(uint64_t D:1,		/* Line dirty */
		__BITFIELD_FIELD(uint64_t L:1,		/* Line locked */
		__BITFIELD_FIELD(uint64_t U:1,		/* Use, LRU eviction */
		__BITFIELD_FIELD(uint64_t addr:17,	/* Phys addr (33..17) */
		;))))))
	} cn38xx;
	struct cvmx_l2c_tag_cn58xx {
		__BITFIELD_FIELD(uint64_t reserved:44,
		__BITFIELD_FIELD(uint64_t V:1,		/* Line valid */
		__BITFIELD_FIELD(uint64_t D:1,		/* Line dirty */
		__BITFIELD_FIELD(uint64_t L:1,		/* Line locked */
		__BITFIELD_FIELD(uint64_t U:1,		/* Use, LRU eviction */
		__BITFIELD_FIELD(uint64_t addr:16,	/* Phys addr (33..18) */
		;))))))
	} cn58xx;
	struct cvmx_l2c_tag_cn58xx cn56xx;	/* 2048 sets */
	struct cvmx_l2c_tag_cn31xx cn52xx;	/* 512 sets */
};


/**
 * @INTERNAL
 * Function to read a L2C tag.  This code make the current core
 * the 'debug core' for the L2.  This code must only be executed by
 * 1 core at a time.
 *
 * @assoc:  Association (way) of the tag to dump
 * @index:  Index of the cacheline
 *
 * Returns The Octeon model specific tag structure.  This is
 *	   translated by a wrapper function to a generic form that is
 *	   easier for applications to use.
 */
static union __cvmx_l2c_tag __read_l2_tag(uint64_t assoc, uint64_t index)
{

	uint64_t debug_tag_addr = CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS, (index << 7) + 96);
	uint64_t core = cvmx_get_core_num();
	union __cvmx_l2c_tag tag_val;
	uint64_t dbg_addr = CVMX_L2C_DBG;
	unsigned long flags;
	union cvmx_l2c_dbg debug_val;

	debug_val.u64 = 0;
	/*
	 * For low core count parts, the core number is always small
	 * enough to stay in the correct field and not set any
	 * reserved bits.
	 */
	debug_val.s.ppnum = core;
	debug_val.s.l2t = 1;
	debug_val.s.set = assoc;

	local_irq_save(flags);
	/*
	 * Make sure core is quiet (no prefetches, etc.) before
	 * entering debug mode.
	 */
	CVMX_SYNC;
	/* Flush L1 to make sure debug load misses L1 */
	CVMX_DCACHE_INVALIDATE;

	/*
	 * The following must be done in assembly as when in debug
	 * mode all data loads from L2 return special debug data, not
	 * normal memory contents.  Also, interrupts must be disabled,
	 * since if an interrupt occurs while in debug mode the ISR
	 * will get debug data from all its memory * reads instead of
	 * the contents of memory.
	 */

	asm volatile (
		".set push\n\t"
		".set mips64\n\t"
		".set noreorder\n\t"
		"sd    %[dbg_val], 0(%[dbg_addr])\n\t"	 /* Enter debug mode, wait for store */
		"ld    $0, 0(%[dbg_addr])\n\t"
		"ld    %[tag_val], 0(%[tag_addr])\n\t"	 /* Read L2C tag data */
		"sd    $0, 0(%[dbg_addr])\n\t"		/* Exit debug mode, wait for store */
		"ld    $0, 0(%[dbg_addr])\n\t"
		"cache 9, 0($0)\n\t"		 /* Invalidate dcache to discard debug data */
		".set pop"
		: [tag_val] "=r" (tag_val)
		: [dbg_addr] "r" (dbg_addr), [dbg_val] "r" (debug_val), [tag_addr] "r" (debug_tag_addr)
		: "memory");

	local_irq_restore(flags);

	return tag_val;
}


union cvmx_l2c_tag cvmx_l2c_get_tag(uint32_t association, uint32_t index)
{
	union cvmx_l2c_tag tag;

	tag.u64 = 0;
	if ((int)association >= cvmx_l2c_get_num_assoc()) {
		cvmx_dprintf("ERROR: cvmx_l2c_get_tag association out of range\n");
		return tag;
	}
	if ((int)index >= cvmx_l2c_get_num_sets()) {
		cvmx_dprintf("ERROR: cvmx_l2c_get_tag index out of range (arg: %d, max: %d)\n",
			     (int)index, cvmx_l2c_get_num_sets());
		return tag;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		union cvmx_l2c_tadx_tag l2c_tadx_tag;
		uint64_t address = CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						(association << CVMX_L2C_TAG_ADDR_ALIAS_SHIFT) |
						(index << CVMX_L2C_IDX_ADDR_SHIFT));
		/*
		 * Use L2 cache Index load tag cache instruction, as
		 * hardware loads the virtual tag for the L2 cache
		 * block with the contents of L2C_TAD0_TAG
		 * register.
		 */
		CVMX_CACHE_LTGL2I(address, 0);
		CVMX_SYNC;   /* make sure CVMX_L2C_TADX_TAG is updated */
		l2c_tadx_tag.u64 = cvmx_read_csr(CVMX_L2C_TADX_TAG(0));

		tag.s.V	    = l2c_tadx_tag.s.valid;
		tag.s.D	    = l2c_tadx_tag.s.dirty;
		tag.s.L	    = l2c_tadx_tag.s.lock;
		tag.s.U	    = l2c_tadx_tag.s.use;
		tag.s.addr  = l2c_tadx_tag.s.tag;
	} else {
		union __cvmx_l2c_tag tmp_tag;
		/* __read_l2_tag is intended for internal use only */
		tmp_tag = __read_l2_tag(association, index);

		/*
		 * Convert all tag structure types to generic version,
		 * as it can represent all models.
		 */
		if (OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)) {
			tag.s.V	   = tmp_tag.cn58xx.V;
			tag.s.D	   = tmp_tag.cn58xx.D;
			tag.s.L	   = tmp_tag.cn58xx.L;
			tag.s.U	   = tmp_tag.cn58xx.U;
			tag.s.addr = tmp_tag.cn58xx.addr;
		} else if (OCTEON_IS_MODEL(OCTEON_CN38XX)) {
			tag.s.V	   = tmp_tag.cn38xx.V;
			tag.s.D	   = tmp_tag.cn38xx.D;
			tag.s.L	   = tmp_tag.cn38xx.L;
			tag.s.U	   = tmp_tag.cn38xx.U;
			tag.s.addr = tmp_tag.cn38xx.addr;
		} else if (OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN52XX)) {
			tag.s.V	   = tmp_tag.cn31xx.V;
			tag.s.D	   = tmp_tag.cn31xx.D;
			tag.s.L	   = tmp_tag.cn31xx.L;
			tag.s.U	   = tmp_tag.cn31xx.U;
			tag.s.addr = tmp_tag.cn31xx.addr;
		} else if (OCTEON_IS_MODEL(OCTEON_CN30XX)) {
			tag.s.V	   = tmp_tag.cn30xx.V;
			tag.s.D	   = tmp_tag.cn30xx.D;
			tag.s.L	   = tmp_tag.cn30xx.L;
			tag.s.U	   = tmp_tag.cn30xx.U;
			tag.s.addr = tmp_tag.cn30xx.addr;
		} else if (OCTEON_IS_MODEL(OCTEON_CN50XX)) {
			tag.s.V	   = tmp_tag.cn50xx.V;
			tag.s.D	   = tmp_tag.cn50xx.D;
			tag.s.L	   = tmp_tag.cn50xx.L;
			tag.s.U	   = tmp_tag.cn50xx.U;
			tag.s.addr = tmp_tag.cn50xx.addr;
		} else {
			cvmx_dprintf("Unsupported OCTEON Model in %s\n", __func__);
		}
	}
	return tag;
}

uint32_t cvmx_l2c_address_to_index(uint64_t addr)
{
	uint64_t idx = addr >> CVMX_L2C_IDX_ADDR_SHIFT;
	int indxalias = 0;

	if (OCTEON_IS_MODEL(OCTEON_CN6XXX)) {
		union cvmx_l2c_ctl l2c_ctl;

		l2c_ctl.u64 = cvmx_read_csr(CVMX_L2C_CTL);
		indxalias = !l2c_ctl.s.disidxalias;
	} else {
		union cvmx_l2c_cfg l2c_cfg;

		l2c_cfg.u64 = cvmx_read_csr(CVMX_L2C_CFG);
		indxalias = l2c_cfg.s.idxalias;
	}

	if (indxalias) {
		if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
			uint32_t a_14_12 = (idx / (CVMX_L2C_MEMBANK_SELECT_SIZE/(1<<CVMX_L2C_IDX_ADDR_SHIFT))) & 0x7;

			idx ^= idx / cvmx_l2c_get_num_sets();
			idx ^= a_14_12;
		} else {
			idx ^= ((addr & CVMX_L2C_ALIAS_MASK) >> CVMX_L2C_TAG_ADDR_ALIAS_SHIFT);
		}
	}
	idx &= CVMX_L2C_IDX_MASK;
	return idx;
}

int cvmx_l2c_get_cache_size_bytes(void)
{
	return cvmx_l2c_get_num_sets() * cvmx_l2c_get_num_assoc() *
		CVMX_CACHE_LINE_SIZE;
}

/**
 * Return log base 2 of the number of sets in the L2 cache
 * Returns
 */
int cvmx_l2c_get_set_bits(void)
{
	int l2_set_bits;

	if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX))
		l2_set_bits = 11;	/* 2048 sets */
	else if (OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN63XX))
		l2_set_bits = 10;	/* 1024 sets */
	else if (OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN52XX))
		l2_set_bits = 9;	/* 512 sets */
	else if (OCTEON_IS_MODEL(OCTEON_CN30XX))
		l2_set_bits = 8;	/* 256 sets */
	else if (OCTEON_IS_MODEL(OCTEON_CN50XX))
		l2_set_bits = 7;	/* 128 sets */
	else {
		cvmx_dprintf("Unsupported OCTEON Model in %s\n", __func__);
		l2_set_bits = 11;	/* 2048 sets */
	}
	return l2_set_bits;
}

/* Return the number of sets in the L2 Cache */
int cvmx_l2c_get_num_sets(void)
{
	return 1 << cvmx_l2c_get_set_bits();
}

/* Return the number of associations in the L2 Cache */
int cvmx_l2c_get_num_assoc(void)
{
	int l2_assoc;

	if (OCTEON_IS_MODEL(OCTEON_CN56XX) ||
	    OCTEON_IS_MODEL(OCTEON_CN52XX) ||
	    OCTEON_IS_MODEL(OCTEON_CN58XX) ||
	    OCTEON_IS_MODEL(OCTEON_CN50XX) ||
	    OCTEON_IS_MODEL(OCTEON_CN38XX))
		l2_assoc = 8;
	else if (OCTEON_IS_MODEL(OCTEON_CN63XX))
		l2_assoc = 16;
	else if (OCTEON_IS_MODEL(OCTEON_CN31XX) ||
		 OCTEON_IS_MODEL(OCTEON_CN30XX))
		l2_assoc = 4;
	else {
		cvmx_dprintf("Unsupported OCTEON Model in %s\n", __func__);
		l2_assoc = 8;
	}

	/* Check to see if part of the cache is disabled */
	if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		union cvmx_mio_fus_dat3 mio_fus_dat3;

		mio_fus_dat3.u64 = cvmx_read_csr(CVMX_MIO_FUS_DAT3);
		/*
		 * cvmx_mio_fus_dat3.s.l2c_crip fuses map as follows
		 * <2> will be not used for 63xx
		 * <1> disables 1/2 ways
		 * <0> disables 1/4 ways
		 * They are cumulative, so for 63xx:
		 * <1> <0>
		 * 0 0 16-way 2MB cache
		 * 0 1 12-way 1.5MB cache
		 * 1 0 8-way 1MB cache
		 * 1 1 4-way 512KB cache
		 */

		if (mio_fus_dat3.s.l2c_crip == 3)
			l2_assoc = 4;
		else if (mio_fus_dat3.s.l2c_crip == 2)
			l2_assoc = 8;
		else if (mio_fus_dat3.s.l2c_crip == 1)
			l2_assoc = 12;
	} else {
		uint64_t l2d_fus3;

		l2d_fus3 = cvmx_read_csr(CVMX_L2D_FUS3);
		/*
		 * Using shifts here, as bit position names are
		 * different for each model but they all mean the
		 * same.
		 */
		if ((l2d_fus3 >> 35) & 0x1)
			l2_assoc = l2_assoc >> 2;
		else if ((l2d_fus3 >> 34) & 0x1)
			l2_assoc = l2_assoc >> 1;
	}
	return l2_assoc;
}

/**
 * Flush a line from the L2 cache
 * This should only be called from one core at a time, as this routine
 * sets the core to the 'debug' core in order to flush the line.
 *
 * @assoc:  Association (or way) to flush
 * @index:  Index to flush
 */
void cvmx_l2c_flush_line(uint32_t assoc, uint32_t index)
{
	/* Check the range of the index. */
	if (index > (uint32_t)cvmx_l2c_get_num_sets()) {
		cvmx_dprintf("ERROR: cvmx_l2c_flush_line index out of range.\n");
		return;
	}

	/* Check the range of association. */
	if (assoc > (uint32_t)cvmx_l2c_get_num_assoc()) {
		cvmx_dprintf("ERROR: cvmx_l2c_flush_line association out of range.\n");
		return;
	}

	if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		uint64_t address;
		/* Create the address based on index and association.
		 * Bits<20:17> select the way of the cache block involved in
		 *	       the operation
		 * Bits<16:7> of the effect address select the index
		 */
		address = CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
				(assoc << CVMX_L2C_TAG_ADDR_ALIAS_SHIFT) |
				(index << CVMX_L2C_IDX_ADDR_SHIFT));
		CVMX_CACHE_WBIL2I(address, 0);
	} else {
		union cvmx_l2c_dbg l2cdbg;

		l2cdbg.u64 = 0;
		if (!OCTEON_IS_MODEL(OCTEON_CN30XX))
			l2cdbg.s.ppnum = cvmx_get_core_num();
		l2cdbg.s.finv = 1;

		l2cdbg.s.set = assoc;
		cvmx_spinlock_lock(&cvmx_l2c_spinlock);
		/*
		 * Enter debug mode, and make sure all other writes
		 * complete before we enter debug mode
		 */
		CVMX_SYNC;
		cvmx_write_csr(CVMX_L2C_DBG, l2cdbg.u64);
		cvmx_read_csr(CVMX_L2C_DBG);

		CVMX_PREPARE_FOR_STORE(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						    index * CVMX_CACHE_LINE_SIZE),
				       0);
		/* Exit debug mode */
		CVMX_SYNC;
		cvmx_write_csr(CVMX_L2C_DBG, 0);
		cvmx_read_csr(CVMX_L2C_DBG);
		cvmx_spinlock_unlock(&cvmx_l2c_spinlock);
	}
}
