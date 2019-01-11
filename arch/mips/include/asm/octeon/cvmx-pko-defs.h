/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Networks
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

#ifndef __CVMX_PKO_DEFS_H__
#define __CVMX_PKO_DEFS_H__

#define CVMX_PKO_MEM_COUNT0 (CVMX_ADD_IO_SEG(0x0001180050001080ull))
#define CVMX_PKO_MEM_COUNT1 (CVMX_ADD_IO_SEG(0x0001180050001088ull))
#define CVMX_PKO_MEM_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180050001100ull))
#define CVMX_PKO_MEM_DEBUG1 (CVMX_ADD_IO_SEG(0x0001180050001108ull))
#define CVMX_PKO_MEM_DEBUG10 (CVMX_ADD_IO_SEG(0x0001180050001150ull))
#define CVMX_PKO_MEM_DEBUG11 (CVMX_ADD_IO_SEG(0x0001180050001158ull))
#define CVMX_PKO_MEM_DEBUG12 (CVMX_ADD_IO_SEG(0x0001180050001160ull))
#define CVMX_PKO_MEM_DEBUG13 (CVMX_ADD_IO_SEG(0x0001180050001168ull))
#define CVMX_PKO_MEM_DEBUG14 (CVMX_ADD_IO_SEG(0x0001180050001170ull))
#define CVMX_PKO_MEM_DEBUG2 (CVMX_ADD_IO_SEG(0x0001180050001110ull))
#define CVMX_PKO_MEM_DEBUG3 (CVMX_ADD_IO_SEG(0x0001180050001118ull))
#define CVMX_PKO_MEM_DEBUG4 (CVMX_ADD_IO_SEG(0x0001180050001120ull))
#define CVMX_PKO_MEM_DEBUG5 (CVMX_ADD_IO_SEG(0x0001180050001128ull))
#define CVMX_PKO_MEM_DEBUG6 (CVMX_ADD_IO_SEG(0x0001180050001130ull))
#define CVMX_PKO_MEM_DEBUG7 (CVMX_ADD_IO_SEG(0x0001180050001138ull))
#define CVMX_PKO_MEM_DEBUG8 (CVMX_ADD_IO_SEG(0x0001180050001140ull))
#define CVMX_PKO_MEM_DEBUG9 (CVMX_ADD_IO_SEG(0x0001180050001148ull))
#define CVMX_PKO_MEM_IPORT_PTRS (CVMX_ADD_IO_SEG(0x0001180050001030ull))
#define CVMX_PKO_MEM_IPORT_QOS (CVMX_ADD_IO_SEG(0x0001180050001038ull))
#define CVMX_PKO_MEM_IQUEUE_PTRS (CVMX_ADD_IO_SEG(0x0001180050001040ull))
#define CVMX_PKO_MEM_IQUEUE_QOS (CVMX_ADD_IO_SEG(0x0001180050001048ull))
#define CVMX_PKO_MEM_PORT_PTRS (CVMX_ADD_IO_SEG(0x0001180050001010ull))
#define CVMX_PKO_MEM_PORT_QOS (CVMX_ADD_IO_SEG(0x0001180050001018ull))
#define CVMX_PKO_MEM_PORT_RATE0 (CVMX_ADD_IO_SEG(0x0001180050001020ull))
#define CVMX_PKO_MEM_PORT_RATE1 (CVMX_ADD_IO_SEG(0x0001180050001028ull))
#define CVMX_PKO_MEM_QUEUE_PTRS (CVMX_ADD_IO_SEG(0x0001180050001000ull))
#define CVMX_PKO_MEM_QUEUE_QOS (CVMX_ADD_IO_SEG(0x0001180050001008ull))
#define CVMX_PKO_MEM_THROTTLE_INT (CVMX_ADD_IO_SEG(0x0001180050001058ull))
#define CVMX_PKO_MEM_THROTTLE_PIPE (CVMX_ADD_IO_SEG(0x0001180050001050ull))
#define CVMX_PKO_REG_BIST_RESULT (CVMX_ADD_IO_SEG(0x0001180050000080ull))
#define CVMX_PKO_REG_CMD_BUF (CVMX_ADD_IO_SEG(0x0001180050000010ull))
#define CVMX_PKO_REG_CRC_CTLX(offset) (CVMX_ADD_IO_SEG(0x0001180050000028ull) + ((offset) & 1) * 8)
#define CVMX_PKO_REG_CRC_ENABLE (CVMX_ADD_IO_SEG(0x0001180050000020ull))
#define CVMX_PKO_REG_CRC_IVX(offset) (CVMX_ADD_IO_SEG(0x0001180050000038ull) + ((offset) & 1) * 8)
#define CVMX_PKO_REG_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180050000098ull))
#define CVMX_PKO_REG_DEBUG1 (CVMX_ADD_IO_SEG(0x00011800500000A0ull))
#define CVMX_PKO_REG_DEBUG2 (CVMX_ADD_IO_SEG(0x00011800500000A8ull))
#define CVMX_PKO_REG_DEBUG3 (CVMX_ADD_IO_SEG(0x00011800500000B0ull))
#define CVMX_PKO_REG_DEBUG4 (CVMX_ADD_IO_SEG(0x00011800500000B8ull))
#define CVMX_PKO_REG_ENGINE_INFLIGHT (CVMX_ADD_IO_SEG(0x0001180050000050ull))
#define CVMX_PKO_REG_ENGINE_INFLIGHT1 (CVMX_ADD_IO_SEG(0x0001180050000318ull))
#define CVMX_PKO_REG_ENGINE_STORAGEX(offset) (CVMX_ADD_IO_SEG(0x0001180050000300ull) + ((offset) & 1) * 8)
#define CVMX_PKO_REG_ENGINE_THRESH (CVMX_ADD_IO_SEG(0x0001180050000058ull))
#define CVMX_PKO_REG_ERROR (CVMX_ADD_IO_SEG(0x0001180050000088ull))
#define CVMX_PKO_REG_FLAGS (CVMX_ADD_IO_SEG(0x0001180050000000ull))
#define CVMX_PKO_REG_GMX_PORT_MODE (CVMX_ADD_IO_SEG(0x0001180050000018ull))
#define CVMX_PKO_REG_INT_MASK (CVMX_ADD_IO_SEG(0x0001180050000090ull))
#define CVMX_PKO_REG_LOOPBACK_BPID (CVMX_ADD_IO_SEG(0x0001180050000118ull))
#define CVMX_PKO_REG_LOOPBACK_PKIND (CVMX_ADD_IO_SEG(0x0001180050000068ull))
#define CVMX_PKO_REG_MIN_PKT (CVMX_ADD_IO_SEG(0x0001180050000070ull))
#define CVMX_PKO_REG_PREEMPT (CVMX_ADD_IO_SEG(0x0001180050000110ull))
#define CVMX_PKO_REG_QUEUE_MODE (CVMX_ADD_IO_SEG(0x0001180050000048ull))
#define CVMX_PKO_REG_QUEUE_PREEMPT (CVMX_ADD_IO_SEG(0x0001180050000108ull))
#define CVMX_PKO_REG_QUEUE_PTRS1 (CVMX_ADD_IO_SEG(0x0001180050000100ull))
#define CVMX_PKO_REG_READ_IDX (CVMX_ADD_IO_SEG(0x0001180050000008ull))
#define CVMX_PKO_REG_THROTTLE (CVMX_ADD_IO_SEG(0x0001180050000078ull))
#define CVMX_PKO_REG_TIMESTAMP (CVMX_ADD_IO_SEG(0x0001180050000060ull))

union cvmx_pko_mem_count0 {
	uint64_t u64;
	struct cvmx_pko_mem_count0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t count:32;
#else
		uint64_t count:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_pko_mem_count1 {
	uint64_t u64;
	struct cvmx_pko_mem_count1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t count:48;
#else
		uint64_t count:48;
		uint64_t reserved_48_63:16;
#endif
	} s;
};

union cvmx_pko_mem_debug0 {
	uint64_t u64;
	struct cvmx_pko_mem_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t fau:28;
		uint64_t cmd:14;
		uint64_t segs:6;
		uint64_t size:16;
#else
		uint64_t size:16;
		uint64_t segs:6;
		uint64_t cmd:14;
		uint64_t fau:28;
#endif
	} s;
};

union cvmx_pko_mem_debug1 {
	uint64_t u64;
	struct cvmx_pko_mem_debug1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t i:1;
		uint64_t back:4;
		uint64_t pool:3;
		uint64_t size:16;
		uint64_t ptr:40;
#else
		uint64_t ptr:40;
		uint64_t size:16;
		uint64_t pool:3;
		uint64_t back:4;
		uint64_t i:1;
#endif
	} s;
};

union cvmx_pko_mem_debug10 {
	uint64_t u64;
	struct cvmx_pko_mem_debug10_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug10_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t fau:28;
		uint64_t cmd:14;
		uint64_t segs:6;
		uint64_t size:16;
#else
		uint64_t size:16;
		uint64_t segs:6;
		uint64_t cmd:14;
		uint64_t fau:28;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug10_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ptrs1:17;
		uint64_t reserved_17_31:15;
		uint64_t ptrs2:17;
#else
		uint64_t ptrs2:17;
		uint64_t reserved_17_31:15;
		uint64_t ptrs1:17;
		uint64_t reserved_49_63:15;
#endif
	} cn50xx;
};

union cvmx_pko_mem_debug11 {
	uint64_t u64;
	struct cvmx_pko_mem_debug11_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t i:1;
		uint64_t back:4;
		uint64_t pool:3;
		uint64_t size:16;
		uint64_t reserved_0_39:40;
#else
		uint64_t reserved_0_39:40;
		uint64_t size:16;
		uint64_t pool:3;
		uint64_t back:4;
		uint64_t i:1;
#endif
	} s;
	struct cvmx_pko_mem_debug11_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t i:1;
		uint64_t back:4;
		uint64_t pool:3;
		uint64_t size:16;
		uint64_t ptr:40;
#else
		uint64_t ptr:40;
		uint64_t size:16;
		uint64_t pool:3;
		uint64_t back:4;
		uint64_t i:1;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug11_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t maj:1;
		uint64_t uid:3;
		uint64_t sop:1;
		uint64_t len:1;
		uint64_t chk:1;
		uint64_t cnt:13;
		uint64_t mod:3;
#else
		uint64_t mod:3;
		uint64_t cnt:13;
		uint64_t chk:1;
		uint64_t len:1;
		uint64_t sop:1;
		uint64_t uid:3;
		uint64_t maj:1;
		uint64_t reserved_23_63:41;
#endif
	} cn50xx;
};

union cvmx_pko_mem_debug12 {
	uint64_t u64;
	struct cvmx_pko_mem_debug12_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug12_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug12_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t fau:28;
		uint64_t cmd:14;
		uint64_t segs:6;
		uint64_t size:16;
#else
		uint64_t size:16;
		uint64_t segs:6;
		uint64_t cmd:14;
		uint64_t fau:28;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug12_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t state:64;
#else
		uint64_t state:64;
#endif
	} cn68xx;
};

union cvmx_pko_mem_debug13 {
	uint64_t u64;
	struct cvmx_pko_mem_debug13_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug13_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_51_63:13;
		uint64_t widx:17;
		uint64_t ridx2:17;
		uint64_t widx2:17;
#else
		uint64_t widx2:17;
		uint64_t ridx2:17;
		uint64_t widx:17;
		uint64_t reserved_51_63:13;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug13_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t i:1;
		uint64_t back:4;
		uint64_t pool:3;
		uint64_t size:16;
		uint64_t ptr:40;
#else
		uint64_t ptr:40;
		uint64_t size:16;
		uint64_t pool:3;
		uint64_t back:4;
		uint64_t i:1;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug13_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t state:64;
#else
		uint64_t state:64;
#endif
	} cn68xx;
};

union cvmx_pko_mem_debug14 {
	uint64_t u64;
	struct cvmx_pko_mem_debug14_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug14_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t ridx:17;
#else
		uint64_t ridx:17;
		uint64_t reserved_17_63:47;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug14_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} cn52xx;
};

union cvmx_pko_mem_debug2 {
	uint64_t u64;
	struct cvmx_pko_mem_debug2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t i:1;
		uint64_t back:4;
		uint64_t pool:3;
		uint64_t size:16;
		uint64_t ptr:40;
#else
		uint64_t ptr:40;
		uint64_t size:16;
		uint64_t pool:3;
		uint64_t back:4;
		uint64_t i:1;
#endif
	} s;
};

union cvmx_pko_mem_debug3 {
	uint64_t u64;
	struct cvmx_pko_mem_debug3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug3_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t i:1;
		uint64_t back:4;
		uint64_t pool:3;
		uint64_t size:16;
		uint64_t ptr:40;
#else
		uint64_t ptr:40;
		uint64_t size:16;
		uint64_t pool:3;
		uint64_t back:4;
		uint64_t i:1;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug3_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} cn50xx;
};

union cvmx_pko_mem_debug4 {
	uint64_t u64;
	struct cvmx_pko_mem_debug4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug4_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t data:64;
#else
		uint64_t data:64;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug4_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t cmnd_segs:3;
		uint64_t cmnd_siz:16;
		uint64_t cmnd_off:6;
		uint64_t uid:3;
		uint64_t dread_sop:1;
		uint64_t init_dwrite:1;
		uint64_t chk_once:1;
		uint64_t chk_mode:1;
		uint64_t active:1;
		uint64_t static_p:1;
		uint64_t qos:3;
		uint64_t qcb_ridx:5;
		uint64_t qid_off_max:4;
		uint64_t qid_off:4;
		uint64_t qid_base:8;
		uint64_t wait:1;
		uint64_t minor:2;
		uint64_t major:3;
#else
		uint64_t major:3;
		uint64_t minor:2;
		uint64_t wait:1;
		uint64_t qid_base:8;
		uint64_t qid_off:4;
		uint64_t qid_off_max:4;
		uint64_t qcb_ridx:5;
		uint64_t qos:3;
		uint64_t static_p:1;
		uint64_t active:1;
		uint64_t chk_mode:1;
		uint64_t chk_once:1;
		uint64_t init_dwrite:1;
		uint64_t dread_sop:1;
		uint64_t uid:3;
		uint64_t cmnd_off:6;
		uint64_t cmnd_siz:16;
		uint64_t cmnd_segs:3;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug4_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t curr_siz:8;
		uint64_t curr_off:16;
		uint64_t cmnd_segs:6;
		uint64_t cmnd_siz:16;
		uint64_t cmnd_off:6;
		uint64_t uid:2;
		uint64_t dread_sop:1;
		uint64_t init_dwrite:1;
		uint64_t chk_once:1;
		uint64_t chk_mode:1;
		uint64_t wait:1;
		uint64_t minor:2;
		uint64_t major:3;
#else
		uint64_t major:3;
		uint64_t minor:2;
		uint64_t wait:1;
		uint64_t chk_mode:1;
		uint64_t chk_once:1;
		uint64_t init_dwrite:1;
		uint64_t dread_sop:1;
		uint64_t uid:2;
		uint64_t cmnd_off:6;
		uint64_t cmnd_siz:16;
		uint64_t cmnd_segs:6;
		uint64_t curr_off:16;
		uint64_t curr_siz:8;
#endif
	} cn52xx;
};

union cvmx_pko_mem_debug5 {
	uint64_t u64;
	struct cvmx_pko_mem_debug5_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug5_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dwri_mod:1;
		uint64_t dwri_sop:1;
		uint64_t dwri_len:1;
		uint64_t dwri_cnt:13;
		uint64_t cmnd_siz:16;
		uint64_t uid:1;
		uint64_t xfer_wor:1;
		uint64_t xfer_dwr:1;
		uint64_t cbuf_fre:1;
		uint64_t reserved_27_27:1;
		uint64_t chk_mode:1;
		uint64_t active:1;
		uint64_t qos:3;
		uint64_t qcb_ridx:5;
		uint64_t qid_off:3;
		uint64_t qid_base:7;
		uint64_t wait:1;
		uint64_t minor:2;
		uint64_t major:4;
#else
		uint64_t major:4;
		uint64_t minor:2;
		uint64_t wait:1;
		uint64_t qid_base:7;
		uint64_t qid_off:3;
		uint64_t qcb_ridx:5;
		uint64_t qos:3;
		uint64_t active:1;
		uint64_t chk_mode:1;
		uint64_t reserved_27_27:1;
		uint64_t cbuf_fre:1;
		uint64_t xfer_dwr:1;
		uint64_t xfer_wor:1;
		uint64_t uid:1;
		uint64_t cmnd_siz:16;
		uint64_t dwri_cnt:13;
		uint64_t dwri_len:1;
		uint64_t dwri_sop:1;
		uint64_t dwri_mod:1;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug5_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t curr_ptr:29;
		uint64_t curr_siz:16;
		uint64_t curr_off:16;
		uint64_t cmnd_segs:3;
#else
		uint64_t cmnd_segs:3;
		uint64_t curr_off:16;
		uint64_t curr_siz:16;
		uint64_t curr_ptr:29;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug5_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t nxt_inflt:6;
		uint64_t curr_ptr:40;
		uint64_t curr_siz:8;
#else
		uint64_t curr_siz:8;
		uint64_t curr_ptr:40;
		uint64_t nxt_inflt:6;
		uint64_t reserved_54_63:10;
#endif
	} cn52xx;
	struct cvmx_pko_mem_debug5_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t ptp:1;
		uint64_t major_3:1;
		uint64_t nxt_inflt:6;
		uint64_t curr_ptr:40;
		uint64_t curr_siz:8;
#else
		uint64_t curr_siz:8;
		uint64_t curr_ptr:40;
		uint64_t nxt_inflt:6;
		uint64_t major_3:1;
		uint64_t ptp:1;
		uint64_t reserved_56_63:8;
#endif
	} cn61xx;
	struct cvmx_pko_mem_debug5_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_57_63:7;
		uint64_t uid_2:1;
		uint64_t ptp:1;
		uint64_t major_3:1;
		uint64_t nxt_inflt:6;
		uint64_t curr_ptr:40;
		uint64_t curr_siz:8;
#else
		uint64_t curr_siz:8;
		uint64_t curr_ptr:40;
		uint64_t nxt_inflt:6;
		uint64_t major_3:1;
		uint64_t ptp:1;
		uint64_t uid_2:1;
		uint64_t reserved_57_63:7;
#endif
	} cn68xx;
};

union cvmx_pko_mem_debug6 {
	uint64_t u64;
	struct cvmx_pko_mem_debug6_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t qid_offres:4;
		uint64_t qid_offths:4;
		uint64_t preempter:1;
		uint64_t preemptee:1;
		uint64_t preempted:1;
		uint64_t active:1;
		uint64_t statc:1;
		uint64_t qos:3;
		uint64_t qcb_ridx:5;
		uint64_t qid_offmax:4;
		uint64_t reserved_0_11:12;
#else
		uint64_t reserved_0_11:12;
		uint64_t qid_offmax:4;
		uint64_t qcb_ridx:5;
		uint64_t qos:3;
		uint64_t statc:1;
		uint64_t active:1;
		uint64_t preempted:1;
		uint64_t preemptee:1;
		uint64_t preempter:1;
		uint64_t qid_offths:4;
		uint64_t qid_offres:4;
		uint64_t reserved_37_63:27;
#endif
	} s;
	struct cvmx_pko_mem_debug6_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t qid_offm:3;
		uint64_t static_p:1;
		uint64_t work_min:3;
		uint64_t dwri_chk:1;
		uint64_t dwri_uid:1;
		uint64_t dwri_mod:2;
#else
		uint64_t dwri_mod:2;
		uint64_t dwri_uid:1;
		uint64_t dwri_chk:1;
		uint64_t work_min:3;
		uint64_t static_p:1;
		uint64_t qid_offm:3;
		uint64_t reserved_11_63:53;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug6_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t curr_ptr:11;
#else
		uint64_t curr_ptr:11;
		uint64_t reserved_11_63:53;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug6_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t qid_offres:4;
		uint64_t qid_offths:4;
		uint64_t preempter:1;
		uint64_t preemptee:1;
		uint64_t preempted:1;
		uint64_t active:1;
		uint64_t statc:1;
		uint64_t qos:3;
		uint64_t qcb_ridx:5;
		uint64_t qid_offmax:4;
		uint64_t qid_off:4;
		uint64_t qid_base:8;
#else
		uint64_t qid_base:8;
		uint64_t qid_off:4;
		uint64_t qid_offmax:4;
		uint64_t qcb_ridx:5;
		uint64_t qos:3;
		uint64_t statc:1;
		uint64_t active:1;
		uint64_t preempted:1;
		uint64_t preemptee:1;
		uint64_t preempter:1;
		uint64_t qid_offths:4;
		uint64_t qid_offres:4;
		uint64_t reserved_37_63:27;
#endif
	} cn52xx;
};

union cvmx_pko_mem_debug7 {
	uint64_t u64;
	struct cvmx_pko_mem_debug7_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_mem_debug7_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_58_63:6;
		uint64_t dwb:9;
		uint64_t start:33;
		uint64_t size:16;
#else
		uint64_t size:16;
		uint64_t start:33;
		uint64_t dwb:9;
		uint64_t reserved_58_63:6;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug7_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t qos:5;
		uint64_t tail:1;
		uint64_t buf_siz:13;
		uint64_t buf_ptr:33;
		uint64_t qcb_widx:6;
		uint64_t qcb_ridx:6;
#else
		uint64_t qcb_ridx:6;
		uint64_t qcb_widx:6;
		uint64_t buf_ptr:33;
		uint64_t buf_siz:13;
		uint64_t tail:1;
		uint64_t qos:5;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug7_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t qos:3;
		uint64_t tail:1;
		uint64_t buf_siz:13;
		uint64_t buf_ptr:33;
		uint64_t qcb_widx:7;
		uint64_t qcb_ridx:7;
#else
		uint64_t qcb_ridx:7;
		uint64_t qcb_widx:7;
		uint64_t buf_ptr:33;
		uint64_t buf_siz:13;
		uint64_t tail:1;
		uint64_t qos:3;
#endif
	} cn68xx;
};

union cvmx_pko_mem_debug8 {
	uint64_t u64;
	struct cvmx_pko_mem_debug8_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t tail:1;
		uint64_t buf_siz:13;
		uint64_t reserved_0_44:45;
#else
		uint64_t reserved_0_44:45;
		uint64_t buf_siz:13;
		uint64_t tail:1;
		uint64_t reserved_59_63:5;
#endif
	} s;
	struct cvmx_pko_mem_debug8_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t qos:5;
		uint64_t tail:1;
		uint64_t buf_siz:13;
		uint64_t buf_ptr:33;
		uint64_t qcb_widx:6;
		uint64_t qcb_ridx:6;
#else
		uint64_t qcb_ridx:6;
		uint64_t qcb_widx:6;
		uint64_t buf_ptr:33;
		uint64_t buf_siz:13;
		uint64_t tail:1;
		uint64_t qos:5;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug8_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t doorbell:20;
		uint64_t reserved_6_7:2;
		uint64_t static_p:1;
		uint64_t s_tail:1;
		uint64_t static_q:1;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t static_q:1;
		uint64_t s_tail:1;
		uint64_t static_p:1;
		uint64_t reserved_6_7:2;
		uint64_t doorbell:20;
		uint64_t reserved_28_63:36;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug8_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t preempter:1;
		uint64_t doorbell:20;
		uint64_t reserved_7_7:1;
		uint64_t preemptee:1;
		uint64_t static_p:1;
		uint64_t s_tail:1;
		uint64_t static_q:1;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t static_q:1;
		uint64_t s_tail:1;
		uint64_t static_p:1;
		uint64_t preemptee:1;
		uint64_t reserved_7_7:1;
		uint64_t doorbell:20;
		uint64_t preempter:1;
		uint64_t reserved_29_63:35;
#endif
	} cn52xx;
	struct cvmx_pko_mem_debug8_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_42_63:22;
		uint64_t qid_qqos:8;
		uint64_t reserved_33_33:1;
		uint64_t qid_idx:4;
		uint64_t preempter:1;
		uint64_t doorbell:20;
		uint64_t reserved_7_7:1;
		uint64_t preemptee:1;
		uint64_t static_p:1;
		uint64_t s_tail:1;
		uint64_t static_q:1;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t static_q:1;
		uint64_t s_tail:1;
		uint64_t static_p:1;
		uint64_t preemptee:1;
		uint64_t reserved_7_7:1;
		uint64_t doorbell:20;
		uint64_t preempter:1;
		uint64_t qid_idx:4;
		uint64_t reserved_33_33:1;
		uint64_t qid_qqos:8;
		uint64_t reserved_42_63:22;
#endif
	} cn61xx;
	struct cvmx_pko_mem_debug8_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t preempter:1;
		uint64_t doorbell:20;
		uint64_t reserved_9_15:7;
		uint64_t preemptee:1;
		uint64_t static_p:1;
		uint64_t s_tail:1;
		uint64_t static_q:1;
		uint64_t qos:5;
#else
		uint64_t qos:5;
		uint64_t static_q:1;
		uint64_t s_tail:1;
		uint64_t static_p:1;
		uint64_t preemptee:1;
		uint64_t reserved_9_15:7;
		uint64_t doorbell:20;
		uint64_t preempter:1;
		uint64_t reserved_37_63:27;
#endif
	} cn68xx;
};

union cvmx_pko_mem_debug9 {
	uint64_t u64;
	struct cvmx_pko_mem_debug9_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ptrs0:17;
		uint64_t reserved_0_31:32;
#else
		uint64_t reserved_0_31:32;
		uint64_t ptrs0:17;
		uint64_t reserved_49_63:15;
#endif
	} s;
	struct cvmx_pko_mem_debug9_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t doorbell:20;
		uint64_t reserved_5_7:3;
		uint64_t s_tail:1;
		uint64_t static_q:1;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t static_q:1;
		uint64_t s_tail:1;
		uint64_t reserved_5_7:3;
		uint64_t doorbell:20;
		uint64_t reserved_28_63:36;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug9_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t doorbell:20;
		uint64_t reserved_6_7:2;
		uint64_t static_p:1;
		uint64_t s_tail:1;
		uint64_t static_q:1;
		uint64_t qos:3;
#else
		uint64_t qos:3;
		uint64_t static_q:1;
		uint64_t s_tail:1;
		uint64_t static_p:1;
		uint64_t reserved_6_7:2;
		uint64_t doorbell:20;
		uint64_t reserved_28_63:36;
#endif
	} cn38xx;
	struct cvmx_pko_mem_debug9_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ptrs0:17;
		uint64_t reserved_17_31:15;
		uint64_t ptrs3:17;
#else
		uint64_t ptrs3:17;
		uint64_t reserved_17_31:15;
		uint64_t ptrs0:17;
		uint64_t reserved_49_63:15;
#endif
	} cn50xx;
};

union cvmx_pko_mem_iport_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_iport_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_63_63:1;
		uint64_t crc:1;
		uint64_t static_p:1;
		uint64_t qos_mask:8;
		uint64_t min_pkt:3;
		uint64_t reserved_31_49:19;
		uint64_t pipe:7;
		uint64_t reserved_21_23:3;
		uint64_t intr:5;
		uint64_t reserved_13_15:3;
		uint64_t eid:5;
		uint64_t reserved_7_7:1;
		uint64_t ipid:7;
#else
		uint64_t ipid:7;
		uint64_t reserved_7_7:1;
		uint64_t eid:5;
		uint64_t reserved_13_15:3;
		uint64_t intr:5;
		uint64_t reserved_21_23:3;
		uint64_t pipe:7;
		uint64_t reserved_31_49:19;
		uint64_t min_pkt:3;
		uint64_t qos_mask:8;
		uint64_t static_p:1;
		uint64_t crc:1;
		uint64_t reserved_63_63:1;
#endif
	} s;
};

union cvmx_pko_mem_iport_qos {
	uint64_t u64;
	struct cvmx_pko_mem_iport_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t qos_mask:8;
		uint64_t reserved_13_52:40;
		uint64_t eid:5;
		uint64_t reserved_7_7:1;
		uint64_t ipid:7;
#else
		uint64_t ipid:7;
		uint64_t reserved_7_7:1;
		uint64_t eid:5;
		uint64_t reserved_13_52:40;
		uint64_t qos_mask:8;
		uint64_t reserved_61_63:3;
#endif
	} s;
};

union cvmx_pko_mem_iqueue_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_iqueue_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t s_tail:1;
		uint64_t static_p:1;
		uint64_t static_q:1;
		uint64_t qos_mask:8;
		uint64_t buf_ptr:31;
		uint64_t tail:1;
		uint64_t index:5;
		uint64_t reserved_15_15:1;
		uint64_t ipid:7;
		uint64_t qid:8;
#else
		uint64_t qid:8;
		uint64_t ipid:7;
		uint64_t reserved_15_15:1;
		uint64_t index:5;
		uint64_t tail:1;
		uint64_t buf_ptr:31;
		uint64_t qos_mask:8;
		uint64_t static_q:1;
		uint64_t static_p:1;
		uint64_t s_tail:1;
#endif
	} s;
};

union cvmx_pko_mem_iqueue_qos {
	uint64_t u64;
	struct cvmx_pko_mem_iqueue_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t qos_mask:8;
		uint64_t reserved_15_52:38;
		uint64_t ipid:7;
		uint64_t qid:8;
#else
		uint64_t qid:8;
		uint64_t ipid:7;
		uint64_t reserved_15_52:38;
		uint64_t qos_mask:8;
		uint64_t reserved_61_63:3;
#endif
	} s;
};

union cvmx_pko_mem_port_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_port_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t static_p:1;
		uint64_t qos_mask:8;
		uint64_t reserved_16_52:37;
		uint64_t bp_port:6;
		uint64_t eid:4;
		uint64_t pid:6;
#else
		uint64_t pid:6;
		uint64_t eid:4;
		uint64_t bp_port:6;
		uint64_t reserved_16_52:37;
		uint64_t qos_mask:8;
		uint64_t static_p:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
};

union cvmx_pko_mem_port_qos {
	uint64_t u64;
	struct cvmx_pko_mem_port_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t qos_mask:8;
		uint64_t reserved_10_52:43;
		uint64_t eid:4;
		uint64_t pid:6;
#else
		uint64_t pid:6;
		uint64_t eid:4;
		uint64_t reserved_10_52:43;
		uint64_t qos_mask:8;
		uint64_t reserved_61_63:3;
#endif
	} s;
};

union cvmx_pko_mem_port_rate0 {
	uint64_t u64;
	struct cvmx_pko_mem_port_rate0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_51_63:13;
		uint64_t rate_word:19;
		uint64_t rate_pkt:24;
		uint64_t reserved_7_7:1;
		uint64_t pid:7;
#else
		uint64_t pid:7;
		uint64_t reserved_7_7:1;
		uint64_t rate_pkt:24;
		uint64_t rate_word:19;
		uint64_t reserved_51_63:13;
#endif
	} s;
	struct cvmx_pko_mem_port_rate0_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_51_63:13;
		uint64_t rate_word:19;
		uint64_t rate_pkt:24;
		uint64_t reserved_6_7:2;
		uint64_t pid:6;
#else
		uint64_t pid:6;
		uint64_t reserved_6_7:2;
		uint64_t rate_pkt:24;
		uint64_t rate_word:19;
		uint64_t reserved_51_63:13;
#endif
	} cn52xx;
};

union cvmx_pko_mem_port_rate1 {
	uint64_t u64;
	struct cvmx_pko_mem_port_rate1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rate_lim:24;
		uint64_t reserved_7_7:1;
		uint64_t pid:7;
#else
		uint64_t pid:7;
		uint64_t reserved_7_7:1;
		uint64_t rate_lim:24;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_pko_mem_port_rate1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rate_lim:24;
		uint64_t reserved_6_7:2;
		uint64_t pid:6;
#else
		uint64_t pid:6;
		uint64_t reserved_6_7:2;
		uint64_t rate_lim:24;
		uint64_t reserved_32_63:32;
#endif
	} cn52xx;
};

union cvmx_pko_mem_queue_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_queue_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t s_tail:1;
		uint64_t static_p:1;
		uint64_t static_q:1;
		uint64_t qos_mask:8;
		uint64_t buf_ptr:36;
		uint64_t tail:1;
		uint64_t index:3;
		uint64_t port:6;
		uint64_t queue:7;
#else
		uint64_t queue:7;
		uint64_t port:6;
		uint64_t index:3;
		uint64_t tail:1;
		uint64_t buf_ptr:36;
		uint64_t qos_mask:8;
		uint64_t static_q:1;
		uint64_t static_p:1;
		uint64_t s_tail:1;
#endif
	} s;
};

union cvmx_pko_mem_queue_qos {
	uint64_t u64;
	struct cvmx_pko_mem_queue_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t qos_mask:8;
		uint64_t reserved_13_52:40;
		uint64_t pid:6;
		uint64_t qid:7;
#else
		uint64_t qid:7;
		uint64_t pid:6;
		uint64_t reserved_13_52:40;
		uint64_t qos_mask:8;
		uint64_t reserved_61_63:3;
#endif
	} s;
};

union cvmx_pko_mem_throttle_int {
	uint64_t u64;
	struct cvmx_pko_mem_throttle_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t word:15;
		uint64_t reserved_14_31:18;
		uint64_t packet:6;
		uint64_t reserved_5_7:3;
		uint64_t intr:5;
#else
		uint64_t intr:5;
		uint64_t reserved_5_7:3;
		uint64_t packet:6;
		uint64_t reserved_14_31:18;
		uint64_t word:15;
		uint64_t reserved_47_63:17;
#endif
	} s;
};

union cvmx_pko_mem_throttle_pipe {
	uint64_t u64;
	struct cvmx_pko_mem_throttle_pipe_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t word:15;
		uint64_t reserved_14_31:18;
		uint64_t packet:6;
		uint64_t reserved_7_7:1;
		uint64_t pipe:7;
#else
		uint64_t pipe:7;
		uint64_t reserved_7_7:1;
		uint64_t packet:6;
		uint64_t reserved_14_31:18;
		uint64_t word:15;
		uint64_t reserved_47_63:17;
#endif
	} s;
};

union cvmx_pko_reg_bist_result {
	uint64_t u64;
	struct cvmx_pko_reg_bist_result_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_pko_reg_bist_result_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_27_63:37;
		uint64_t psb2:5;
		uint64_t count:1;
		uint64_t rif:1;
		uint64_t wif:1;
		uint64_t ncb:1;
		uint64_t out:1;
		uint64_t crc:1;
		uint64_t chk:1;
		uint64_t qsb:2;
		uint64_t qcb:2;
		uint64_t pdb:4;
		uint64_t psb:7;
#else
		uint64_t psb:7;
		uint64_t pdb:4;
		uint64_t qcb:2;
		uint64_t qsb:2;
		uint64_t chk:1;
		uint64_t crc:1;
		uint64_t out:1;
		uint64_t ncb:1;
		uint64_t wif:1;
		uint64_t rif:1;
		uint64_t count:1;
		uint64_t psb2:5;
		uint64_t reserved_27_63:37;
#endif
	} cn30xx;
	struct cvmx_pko_reg_bist_result_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t csr:1;
		uint64_t iob:1;
		uint64_t out_crc:1;
		uint64_t out_ctl:3;
		uint64_t out_sta:1;
		uint64_t out_wif:1;
		uint64_t prt_chk:3;
		uint64_t prt_nxt:1;
		uint64_t prt_psb:6;
		uint64_t ncb_inb:2;
		uint64_t prt_qcb:2;
		uint64_t prt_qsb:3;
		uint64_t dat_dat:4;
		uint64_t dat_ptr:4;
#else
		uint64_t dat_ptr:4;
		uint64_t dat_dat:4;
		uint64_t prt_qsb:3;
		uint64_t prt_qcb:2;
		uint64_t ncb_inb:2;
		uint64_t prt_psb:6;
		uint64_t prt_nxt:1;
		uint64_t prt_chk:3;
		uint64_t out_wif:1;
		uint64_t out_sta:1;
		uint64_t out_ctl:3;
		uint64_t out_crc:1;
		uint64_t iob:1;
		uint64_t csr:1;
		uint64_t reserved_33_63:31;
#endif
	} cn50xx;
	struct cvmx_pko_reg_bist_result_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_35_63:29;
		uint64_t csr:1;
		uint64_t iob:1;
		uint64_t out_dat:1;
		uint64_t out_ctl:3;
		uint64_t out_sta:1;
		uint64_t out_wif:1;
		uint64_t prt_chk:3;
		uint64_t prt_nxt:1;
		uint64_t prt_psb:8;
		uint64_t ncb_inb:2;
		uint64_t prt_qcb:2;
		uint64_t prt_qsb:3;
		uint64_t prt_ctl:2;
		uint64_t dat_dat:2;
		uint64_t dat_ptr:4;
#else
		uint64_t dat_ptr:4;
		uint64_t dat_dat:2;
		uint64_t prt_ctl:2;
		uint64_t prt_qsb:3;
		uint64_t prt_qcb:2;
		uint64_t ncb_inb:2;
		uint64_t prt_psb:8;
		uint64_t prt_nxt:1;
		uint64_t prt_chk:3;
		uint64_t out_wif:1;
		uint64_t out_sta:1;
		uint64_t out_ctl:3;
		uint64_t out_dat:1;
		uint64_t iob:1;
		uint64_t csr:1;
		uint64_t reserved_35_63:29;
#endif
	} cn52xx;
	struct cvmx_pko_reg_bist_result_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t crc:1;
		uint64_t csr:1;
		uint64_t iob:1;
		uint64_t out_dat:1;
		uint64_t reserved_31_31:1;
		uint64_t out_ctl:2;
		uint64_t out_sta:1;
		uint64_t out_wif:1;
		uint64_t prt_chk:3;
		uint64_t prt_nxt:1;
		uint64_t prt_psb7:1;
		uint64_t reserved_21_21:1;
		uint64_t prt_psb:6;
		uint64_t ncb_inb:2;
		uint64_t prt_qcb:2;
		uint64_t prt_qsb:3;
		uint64_t prt_ctl:2;
		uint64_t dat_dat:2;
		uint64_t dat_ptr:4;
#else
		uint64_t dat_ptr:4;
		uint64_t dat_dat:2;
		uint64_t prt_ctl:2;
		uint64_t prt_qsb:3;
		uint64_t prt_qcb:2;
		uint64_t ncb_inb:2;
		uint64_t prt_psb:6;
		uint64_t reserved_21_21:1;
		uint64_t prt_psb7:1;
		uint64_t prt_nxt:1;
		uint64_t prt_chk:3;
		uint64_t out_wif:1;
		uint64_t out_sta:1;
		uint64_t out_ctl:2;
		uint64_t reserved_31_31:1;
		uint64_t out_dat:1;
		uint64_t iob:1;
		uint64_t csr:1;
		uint64_t crc:1;
		uint64_t reserved_36_63:28;
#endif
	} cn68xx;
	struct cvmx_pko_reg_bist_result_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_35_63:29;
		uint64_t csr:1;
		uint64_t iob:1;
		uint64_t out_dat:1;
		uint64_t reserved_31_31:1;
		uint64_t out_ctl:2;
		uint64_t out_sta:1;
		uint64_t out_wif:1;
		uint64_t prt_chk:3;
		uint64_t prt_nxt:1;
		uint64_t prt_psb7:1;
		uint64_t reserved_21_21:1;
		uint64_t prt_psb:6;
		uint64_t ncb_inb:2;
		uint64_t prt_qcb:2;
		uint64_t prt_qsb:3;
		uint64_t prt_ctl:2;
		uint64_t dat_dat:2;
		uint64_t dat_ptr:4;
#else
		uint64_t dat_ptr:4;
		uint64_t dat_dat:2;
		uint64_t prt_ctl:2;
		uint64_t prt_qsb:3;
		uint64_t prt_qcb:2;
		uint64_t ncb_inb:2;
		uint64_t prt_psb:6;
		uint64_t reserved_21_21:1;
		uint64_t prt_psb7:1;
		uint64_t prt_nxt:1;
		uint64_t prt_chk:3;
		uint64_t out_wif:1;
		uint64_t out_sta:1;
		uint64_t out_ctl:2;
		uint64_t reserved_31_31:1;
		uint64_t out_dat:1;
		uint64_t iob:1;
		uint64_t csr:1;
		uint64_t reserved_35_63:29;
#endif
	} cn68xxp1;
};

union cvmx_pko_reg_cmd_buf {
	uint64_t u64;
	struct cvmx_pko_reg_cmd_buf_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t pool:3;
		uint64_t reserved_13_19:7;
		uint64_t size:13;
#else
		uint64_t size:13;
		uint64_t reserved_13_19:7;
		uint64_t pool:3;
		uint64_t reserved_23_63:41;
#endif
	} s;
};

union cvmx_pko_reg_crc_ctlx {
	uint64_t u64;
	struct cvmx_pko_reg_crc_ctlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t invres:1;
		uint64_t refin:1;
#else
		uint64_t refin:1;
		uint64_t invres:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_pko_reg_crc_enable {
	uint64_t u64;
	struct cvmx_pko_reg_crc_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t enable:32;
#else
		uint64_t enable:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_pko_reg_crc_ivx {
	uint64_t u64;
	struct cvmx_pko_reg_crc_ivx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t iv:32;
#else
		uint64_t iv:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_pko_reg_debug0 {
	uint64_t u64;
	struct cvmx_pko_reg_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t asserts:64;
#else
		uint64_t asserts:64;
#endif
	} s;
	struct cvmx_pko_reg_debug0_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t asserts:17;
#else
		uint64_t asserts:17;
		uint64_t reserved_17_63:47;
#endif
	} cn30xx;
};

union cvmx_pko_reg_debug1 {
	uint64_t u64;
	struct cvmx_pko_reg_debug1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t asserts:64;
#else
		uint64_t asserts:64;
#endif
	} s;
};

union cvmx_pko_reg_debug2 {
	uint64_t u64;
	struct cvmx_pko_reg_debug2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t asserts:64;
#else
		uint64_t asserts:64;
#endif
	} s;
};

union cvmx_pko_reg_debug3 {
	uint64_t u64;
	struct cvmx_pko_reg_debug3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t asserts:64;
#else
		uint64_t asserts:64;
#endif
	} s;
};

union cvmx_pko_reg_debug4 {
	uint64_t u64;
	struct cvmx_pko_reg_debug4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t asserts:64;
#else
		uint64_t asserts:64;
#endif
	} s;
};

union cvmx_pko_reg_engine_inflight {
	uint64_t u64;
	struct cvmx_pko_reg_engine_inflight_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t engine15:4;
		uint64_t engine14:4;
		uint64_t engine13:4;
		uint64_t engine12:4;
		uint64_t engine11:4;
		uint64_t engine10:4;
		uint64_t engine9:4;
		uint64_t engine8:4;
		uint64_t engine7:4;
		uint64_t engine6:4;
		uint64_t engine5:4;
		uint64_t engine4:4;
		uint64_t engine3:4;
		uint64_t engine2:4;
		uint64_t engine1:4;
		uint64_t engine0:4;
#else
		uint64_t engine0:4;
		uint64_t engine1:4;
		uint64_t engine2:4;
		uint64_t engine3:4;
		uint64_t engine4:4;
		uint64_t engine5:4;
		uint64_t engine6:4;
		uint64_t engine7:4;
		uint64_t engine8:4;
		uint64_t engine9:4;
		uint64_t engine10:4;
		uint64_t engine11:4;
		uint64_t engine12:4;
		uint64_t engine13:4;
		uint64_t engine14:4;
		uint64_t engine15:4;
#endif
	} s;
	struct cvmx_pko_reg_engine_inflight_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t engine9:4;
		uint64_t engine8:4;
		uint64_t engine7:4;
		uint64_t engine6:4;
		uint64_t engine5:4;
		uint64_t engine4:4;
		uint64_t engine3:4;
		uint64_t engine2:4;
		uint64_t engine1:4;
		uint64_t engine0:4;
#else
		uint64_t engine0:4;
		uint64_t engine1:4;
		uint64_t engine2:4;
		uint64_t engine3:4;
		uint64_t engine4:4;
		uint64_t engine5:4;
		uint64_t engine6:4;
		uint64_t engine7:4;
		uint64_t engine8:4;
		uint64_t engine9:4;
		uint64_t reserved_40_63:24;
#endif
	} cn52xx;
	struct cvmx_pko_reg_engine_inflight_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t engine13:4;
		uint64_t engine12:4;
		uint64_t engine11:4;
		uint64_t engine10:4;
		uint64_t engine9:4;
		uint64_t engine8:4;
		uint64_t engine7:4;
		uint64_t engine6:4;
		uint64_t engine5:4;
		uint64_t engine4:4;
		uint64_t engine3:4;
		uint64_t engine2:4;
		uint64_t engine1:4;
		uint64_t engine0:4;
#else
		uint64_t engine0:4;
		uint64_t engine1:4;
		uint64_t engine2:4;
		uint64_t engine3:4;
		uint64_t engine4:4;
		uint64_t engine5:4;
		uint64_t engine6:4;
		uint64_t engine7:4;
		uint64_t engine8:4;
		uint64_t engine9:4;
		uint64_t engine10:4;
		uint64_t engine11:4;
		uint64_t engine12:4;
		uint64_t engine13:4;
		uint64_t reserved_56_63:8;
#endif
	} cn61xx;
	struct cvmx_pko_reg_engine_inflight_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t engine11:4;
		uint64_t engine10:4;
		uint64_t engine9:4;
		uint64_t engine8:4;
		uint64_t engine7:4;
		uint64_t engine6:4;
		uint64_t engine5:4;
		uint64_t engine4:4;
		uint64_t engine3:4;
		uint64_t engine2:4;
		uint64_t engine1:4;
		uint64_t engine0:4;
#else
		uint64_t engine0:4;
		uint64_t engine1:4;
		uint64_t engine2:4;
		uint64_t engine3:4;
		uint64_t engine4:4;
		uint64_t engine5:4;
		uint64_t engine6:4;
		uint64_t engine7:4;
		uint64_t engine8:4;
		uint64_t engine9:4;
		uint64_t engine10:4;
		uint64_t engine11:4;
		uint64_t reserved_48_63:16;
#endif
	} cn63xx;
};

union cvmx_pko_reg_engine_inflight1 {
	uint64_t u64;
	struct cvmx_pko_reg_engine_inflight1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t engine19:4;
		uint64_t engine18:4;
		uint64_t engine17:4;
		uint64_t engine16:4;
#else
		uint64_t engine16:4;
		uint64_t engine17:4;
		uint64_t engine18:4;
		uint64_t engine19:4;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pko_reg_engine_storagex {
	uint64_t u64;
	struct cvmx_pko_reg_engine_storagex_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t engine15:4;
		uint64_t engine14:4;
		uint64_t engine13:4;
		uint64_t engine12:4;
		uint64_t engine11:4;
		uint64_t engine10:4;
		uint64_t engine9:4;
		uint64_t engine8:4;
		uint64_t engine7:4;
		uint64_t engine6:4;
		uint64_t engine5:4;
		uint64_t engine4:4;
		uint64_t engine3:4;
		uint64_t engine2:4;
		uint64_t engine1:4;
		uint64_t engine0:4;
#else
		uint64_t engine0:4;
		uint64_t engine1:4;
		uint64_t engine2:4;
		uint64_t engine3:4;
		uint64_t engine4:4;
		uint64_t engine5:4;
		uint64_t engine6:4;
		uint64_t engine7:4;
		uint64_t engine8:4;
		uint64_t engine9:4;
		uint64_t engine10:4;
		uint64_t engine11:4;
		uint64_t engine12:4;
		uint64_t engine13:4;
		uint64_t engine14:4;
		uint64_t engine15:4;
#endif
	} s;
};

union cvmx_pko_reg_engine_thresh {
	uint64_t u64;
	struct cvmx_pko_reg_engine_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t mask:20;
#else
		uint64_t mask:20;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_pko_reg_engine_thresh_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t mask:10;
#else
		uint64_t mask:10;
		uint64_t reserved_10_63:54;
#endif
	} cn52xx;
	struct cvmx_pko_reg_engine_thresh_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t mask:14;
#else
		uint64_t mask:14;
		uint64_t reserved_14_63:50;
#endif
	} cn61xx;
	struct cvmx_pko_reg_engine_thresh_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t mask:12;
#else
		uint64_t mask:12;
		uint64_t reserved_12_63:52;
#endif
	} cn63xx;
};

union cvmx_pko_reg_error {
	uint64_t u64;
	struct cvmx_pko_reg_error_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t loopback:1;
		uint64_t currzero:1;
		uint64_t doorbell:1;
		uint64_t parity:1;
#else
		uint64_t parity:1;
		uint64_t doorbell:1;
		uint64_t currzero:1;
		uint64_t loopback:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_pko_reg_error_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t doorbell:1;
		uint64_t parity:1;
#else
		uint64_t parity:1;
		uint64_t doorbell:1;
		uint64_t reserved_2_63:62;
#endif
	} cn30xx;
	struct cvmx_pko_reg_error_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t currzero:1;
		uint64_t doorbell:1;
		uint64_t parity:1;
#else
		uint64_t parity:1;
		uint64_t doorbell:1;
		uint64_t currzero:1;
		uint64_t reserved_3_63:61;
#endif
	} cn50xx;
};

union cvmx_pko_reg_flags {
	uint64_t u64;
	struct cvmx_pko_reg_flags_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t dis_perf3:1;
		uint64_t dis_perf2:1;
		uint64_t dis_perf1:1;
		uint64_t dis_perf0:1;
		uint64_t ena_throttle:1;
		uint64_t reset:1;
		uint64_t store_be:1;
		uint64_t ena_dwb:1;
		uint64_t ena_pko:1;
#else
		uint64_t ena_pko:1;
		uint64_t ena_dwb:1;
		uint64_t store_be:1;
		uint64_t reset:1;
		uint64_t ena_throttle:1;
		uint64_t dis_perf0:1;
		uint64_t dis_perf1:1;
		uint64_t dis_perf2:1;
		uint64_t dis_perf3:1;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_pko_reg_flags_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t reset:1;
		uint64_t store_be:1;
		uint64_t ena_dwb:1;
		uint64_t ena_pko:1;
#else
		uint64_t ena_pko:1;
		uint64_t ena_dwb:1;
		uint64_t store_be:1;
		uint64_t reset:1;
		uint64_t reserved_4_63:60;
#endif
	} cn30xx;
	struct cvmx_pko_reg_flags_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t dis_perf3:1;
		uint64_t dis_perf2:1;
		uint64_t reserved_4_6:3;
		uint64_t reset:1;
		uint64_t store_be:1;
		uint64_t ena_dwb:1;
		uint64_t ena_pko:1;
#else
		uint64_t ena_pko:1;
		uint64_t ena_dwb:1;
		uint64_t store_be:1;
		uint64_t reset:1;
		uint64_t reserved_4_6:3;
		uint64_t dis_perf2:1;
		uint64_t dis_perf3:1;
		uint64_t reserved_9_63:55;
#endif
	} cn61xx;
	struct cvmx_pko_reg_flags_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t dis_perf1:1;
		uint64_t dis_perf0:1;
		uint64_t ena_throttle:1;
		uint64_t reset:1;
		uint64_t store_be:1;
		uint64_t ena_dwb:1;
		uint64_t ena_pko:1;
#else
		uint64_t ena_pko:1;
		uint64_t ena_dwb:1;
		uint64_t store_be:1;
		uint64_t reset:1;
		uint64_t ena_throttle:1;
		uint64_t dis_perf0:1;
		uint64_t dis_perf1:1;
		uint64_t reserved_7_63:57;
#endif
	} cn68xxp1;
};

union cvmx_pko_reg_gmx_port_mode {
	uint64_t u64;
	struct cvmx_pko_reg_gmx_port_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t mode1:3;
		uint64_t mode0:3;
#else
		uint64_t mode0:3;
		uint64_t mode1:3;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_pko_reg_int_mask {
	uint64_t u64;
	struct cvmx_pko_reg_int_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t loopback:1;
		uint64_t currzero:1;
		uint64_t doorbell:1;
		uint64_t parity:1;
#else
		uint64_t parity:1;
		uint64_t doorbell:1;
		uint64_t currzero:1;
		uint64_t loopback:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_pko_reg_int_mask_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t doorbell:1;
		uint64_t parity:1;
#else
		uint64_t parity:1;
		uint64_t doorbell:1;
		uint64_t reserved_2_63:62;
#endif
	} cn30xx;
	struct cvmx_pko_reg_int_mask_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t currzero:1;
		uint64_t doorbell:1;
		uint64_t parity:1;
#else
		uint64_t parity:1;
		uint64_t doorbell:1;
		uint64_t currzero:1;
		uint64_t reserved_3_63:61;
#endif
	} cn50xx;
};

union cvmx_pko_reg_loopback_bpid {
	uint64_t u64;
	struct cvmx_pko_reg_loopback_bpid_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t bpid7:6;
		uint64_t reserved_52_52:1;
		uint64_t bpid6:6;
		uint64_t reserved_45_45:1;
		uint64_t bpid5:6;
		uint64_t reserved_38_38:1;
		uint64_t bpid4:6;
		uint64_t reserved_31_31:1;
		uint64_t bpid3:6;
		uint64_t reserved_24_24:1;
		uint64_t bpid2:6;
		uint64_t reserved_17_17:1;
		uint64_t bpid1:6;
		uint64_t reserved_10_10:1;
		uint64_t bpid0:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t bpid0:6;
		uint64_t reserved_10_10:1;
		uint64_t bpid1:6;
		uint64_t reserved_17_17:1;
		uint64_t bpid2:6;
		uint64_t reserved_24_24:1;
		uint64_t bpid3:6;
		uint64_t reserved_31_31:1;
		uint64_t bpid4:6;
		uint64_t reserved_38_38:1;
		uint64_t bpid5:6;
		uint64_t reserved_45_45:1;
		uint64_t bpid6:6;
		uint64_t reserved_52_52:1;
		uint64_t bpid7:6;
		uint64_t reserved_59_63:5;
#endif
	} s;
};

union cvmx_pko_reg_loopback_pkind {
	uint64_t u64;
	struct cvmx_pko_reg_loopback_pkind_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t pkind7:6;
		uint64_t reserved_52_52:1;
		uint64_t pkind6:6;
		uint64_t reserved_45_45:1;
		uint64_t pkind5:6;
		uint64_t reserved_38_38:1;
		uint64_t pkind4:6;
		uint64_t reserved_31_31:1;
		uint64_t pkind3:6;
		uint64_t reserved_24_24:1;
		uint64_t pkind2:6;
		uint64_t reserved_17_17:1;
		uint64_t pkind1:6;
		uint64_t reserved_10_10:1;
		uint64_t pkind0:6;
		uint64_t num_ports:4;
#else
		uint64_t num_ports:4;
		uint64_t pkind0:6;
		uint64_t reserved_10_10:1;
		uint64_t pkind1:6;
		uint64_t reserved_17_17:1;
		uint64_t pkind2:6;
		uint64_t reserved_24_24:1;
		uint64_t pkind3:6;
		uint64_t reserved_31_31:1;
		uint64_t pkind4:6;
		uint64_t reserved_38_38:1;
		uint64_t pkind5:6;
		uint64_t reserved_45_45:1;
		uint64_t pkind6:6;
		uint64_t reserved_52_52:1;
		uint64_t pkind7:6;
		uint64_t reserved_59_63:5;
#endif
	} s;
};

union cvmx_pko_reg_min_pkt {
	uint64_t u64;
	struct cvmx_pko_reg_min_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t size7:8;
		uint64_t size6:8;
		uint64_t size5:8;
		uint64_t size4:8;
		uint64_t size3:8;
		uint64_t size2:8;
		uint64_t size1:8;
		uint64_t size0:8;
#else
		uint64_t size0:8;
		uint64_t size1:8;
		uint64_t size2:8;
		uint64_t size3:8;
		uint64_t size4:8;
		uint64_t size5:8;
		uint64_t size6:8;
		uint64_t size7:8;
#endif
	} s;
};

union cvmx_pko_reg_preempt {
	uint64_t u64;
	struct cvmx_pko_reg_preempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t min_size:16;
#else
		uint64_t min_size:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pko_reg_queue_mode {
	uint64_t u64;
	struct cvmx_pko_reg_queue_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t mode:2;
#else
		uint64_t mode:2;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_pko_reg_queue_preempt {
	uint64_t u64;
	struct cvmx_pko_reg_queue_preempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t preemptee:1;
		uint64_t preempter:1;
#else
		uint64_t preempter:1;
		uint64_t preemptee:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_pko_reg_queue_ptrs1 {
	uint64_t u64;
	struct cvmx_pko_reg_queue_ptrs1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t idx3:1;
		uint64_t qid7:1;
#else
		uint64_t qid7:1;
		uint64_t idx3:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
};

union cvmx_pko_reg_read_idx {
	uint64_t u64;
	struct cvmx_pko_reg_read_idx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t inc:8;
		uint64_t index:8;
#else
		uint64_t index:8;
		uint64_t inc:8;
		uint64_t reserved_16_63:48;
#endif
	} s;
};

union cvmx_pko_reg_throttle {
	uint64_t u64;
	struct cvmx_pko_reg_throttle_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t int_mask:32;
#else
		uint64_t int_mask:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_pko_reg_timestamp {
	uint64_t u64;
	struct cvmx_pko_reg_timestamp_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t wqe_word:4;
#else
		uint64_t wqe_word:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
};

#endif
