/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
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

/**
 *
 * Interface to the hardware Input Packet Data unit.
 */

#ifndef __CVMX_IPD_H__
#define __CVMX_IPD_H__

#include <asm/octeon/octeon-feature.h>

#include <asm/octeon/cvmx-ipd-defs.h>
#include <asm/octeon/cvmx-pip-defs.h>

enum cvmx_ipd_mode {
   CVMX_IPD_OPC_MODE_STT = 0LL,	  /* All blocks DRAM, not cached in L2 */
   CVMX_IPD_OPC_MODE_STF = 1LL,	  /* All blocks into  L2 */
   CVMX_IPD_OPC_MODE_STF1_STT = 2LL,   /* 1st block L2, rest DRAM */
   CVMX_IPD_OPC_MODE_STF2_STT = 3LL    /* 1st, 2nd blocks L2, rest DRAM */
};

#ifndef CVMX_ENABLE_LEN_M8_FIX
#define CVMX_ENABLE_LEN_M8_FIX 0
#endif

/* CSR typedefs have been moved to cvmx-csr-*.h */
typedef union cvmx_ipd_1st_mbuff_skip cvmx_ipd_mbuff_first_skip_t;
typedef union cvmx_ipd_1st_next_ptr_back cvmx_ipd_first_next_ptr_back_t;

typedef cvmx_ipd_mbuff_first_skip_t cvmx_ipd_mbuff_not_first_skip_t;
typedef cvmx_ipd_first_next_ptr_back_t cvmx_ipd_second_next_ptr_back_t;

/**
 * Configure IPD
 *
 * @mbuff_size: Packets buffer size in 8 byte words
 * @first_mbuff_skip:
 *		     Number of 8 byte words to skip in the first buffer
 * @not_first_mbuff_skip:
 *		     Number of 8 byte words to skip in each following buffer
 * @first_back: Must be same as first_mbuff_skip / 128
 * @second_back:
 *		     Must be same as not_first_mbuff_skip / 128
 * @wqe_fpa_pool:
 *		     FPA pool to get work entries from
 * @cache_mode:
 * @back_pres_enable_flag:
 *		     Enable or disable port back pressure
 */
static inline void cvmx_ipd_config(uint64_t mbuff_size,
				   uint64_t first_mbuff_skip,
				   uint64_t not_first_mbuff_skip,
				   uint64_t first_back,
				   uint64_t second_back,
				   uint64_t wqe_fpa_pool,
				   enum cvmx_ipd_mode cache_mode,
				   uint64_t back_pres_enable_flag)
{
	cvmx_ipd_mbuff_first_skip_t first_skip;
	cvmx_ipd_mbuff_not_first_skip_t not_first_skip;
	union cvmx_ipd_packet_mbuff_size size;
	cvmx_ipd_first_next_ptr_back_t first_back_struct;
	cvmx_ipd_second_next_ptr_back_t second_back_struct;
	union cvmx_ipd_wqe_fpa_queue wqe_pool;
	union cvmx_ipd_ctl_status ipd_ctl_reg;

	first_skip.u64 = 0;
	first_skip.s.skip_sz = first_mbuff_skip;
	cvmx_write_csr(CVMX_IPD_1ST_MBUFF_SKIP, first_skip.u64);

	not_first_skip.u64 = 0;
	not_first_skip.s.skip_sz = not_first_mbuff_skip;
	cvmx_write_csr(CVMX_IPD_NOT_1ST_MBUFF_SKIP, not_first_skip.u64);

	size.u64 = 0;
	size.s.mb_size = mbuff_size;
	cvmx_write_csr(CVMX_IPD_PACKET_MBUFF_SIZE, size.u64);

	first_back_struct.u64 = 0;
	first_back_struct.s.back = first_back;
	cvmx_write_csr(CVMX_IPD_1st_NEXT_PTR_BACK, first_back_struct.u64);

	second_back_struct.u64 = 0;
	second_back_struct.s.back = second_back;
	cvmx_write_csr(CVMX_IPD_2nd_NEXT_PTR_BACK, second_back_struct.u64);

	wqe_pool.u64 = 0;
	wqe_pool.s.wqe_pool = wqe_fpa_pool;
	cvmx_write_csr(CVMX_IPD_WQE_FPA_QUEUE, wqe_pool.u64);

	ipd_ctl_reg.u64 = cvmx_read_csr(CVMX_IPD_CTL_STATUS);
	ipd_ctl_reg.s.opc_mode = cache_mode;
	ipd_ctl_reg.s.pbp_en = back_pres_enable_flag;
	cvmx_write_csr(CVMX_IPD_CTL_STATUS, ipd_ctl_reg.u64);

	/* Note: the example RED code that used to be here has been moved to
	   cvmx_helper_setup_red */
}

/**
 * Enable IPD
 */
static inline void cvmx_ipd_enable(void)
{
	union cvmx_ipd_ctl_status ipd_reg;
	ipd_reg.u64 = cvmx_read_csr(CVMX_IPD_CTL_STATUS);
	if (ipd_reg.s.ipd_en) {
		cvmx_dprintf
		    ("Warning: Enabling IPD when IPD already enabled.\n");
	}
	ipd_reg.s.ipd_en = 1;
#if  CVMX_ENABLE_LEN_M8_FIX
	if (!OCTEON_IS_MODEL(OCTEON_CN38XX_PASS2))
		ipd_reg.s.len_m8 = TRUE;
#endif
	cvmx_write_csr(CVMX_IPD_CTL_STATUS, ipd_reg.u64);
}

/**
 * Disable IPD
 */
static inline void cvmx_ipd_disable(void)
{
	union cvmx_ipd_ctl_status ipd_reg;
	ipd_reg.u64 = cvmx_read_csr(CVMX_IPD_CTL_STATUS);
	ipd_reg.s.ipd_en = 0;
	cvmx_write_csr(CVMX_IPD_CTL_STATUS, ipd_reg.u64);
}

/**
 * Supportive function for cvmx_fpa_shutdown_pool.
 */
static inline void cvmx_ipd_free_ptr(void)
{
	/* Only CN38XXp{1,2} cannot read pointer out of the IPD */
	if (!OCTEON_IS_MODEL(OCTEON_CN38XX_PASS1)
	    && !OCTEON_IS_MODEL(OCTEON_CN38XX_PASS2)) {
		int no_wptr = 0;
		union cvmx_ipd_ptr_count ipd_ptr_count;
		ipd_ptr_count.u64 = cvmx_read_csr(CVMX_IPD_PTR_COUNT);

		/* Handle Work Queue Entry in cn56xx and cn52xx */
		if (octeon_has_feature(OCTEON_FEATURE_NO_WPTR)) {
			union cvmx_ipd_ctl_status ipd_ctl_status;
			ipd_ctl_status.u64 = cvmx_read_csr(CVMX_IPD_CTL_STATUS);
			if (ipd_ctl_status.s.no_wptr)
				no_wptr = 1;
		}

		/* Free the prefetched WQE */
		if (ipd_ptr_count.s.wqev_cnt) {
			union cvmx_ipd_wqe_ptr_valid ipd_wqe_ptr_valid;
			ipd_wqe_ptr_valid.u64 =
			    cvmx_read_csr(CVMX_IPD_WQE_PTR_VALID);
			if (no_wptr)
				cvmx_fpa_free(cvmx_phys_to_ptr
					      ((uint64_t) ipd_wqe_ptr_valid.s.
					       ptr << 7), CVMX_FPA_PACKET_POOL,
					      0);
			else
				cvmx_fpa_free(cvmx_phys_to_ptr
					      ((uint64_t) ipd_wqe_ptr_valid.s.
					       ptr << 7), CVMX_FPA_WQE_POOL, 0);
		}

		/* Free all WQE in the fifo */
		if (ipd_ptr_count.s.wqe_pcnt) {
			int i;
			union cvmx_ipd_pwp_ptr_fifo_ctl ipd_pwp_ptr_fifo_ctl;
			ipd_pwp_ptr_fifo_ctl.u64 =
			    cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);
			for (i = 0; i < ipd_ptr_count.s.wqe_pcnt; i++) {
				ipd_pwp_ptr_fifo_ctl.s.cena = 0;
				ipd_pwp_ptr_fifo_ctl.s.raddr =
				    ipd_pwp_ptr_fifo_ctl.s.max_cnts +
				    (ipd_pwp_ptr_fifo_ctl.s.wraddr +
				     i) % ipd_pwp_ptr_fifo_ctl.s.max_cnts;
				cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL,
					       ipd_pwp_ptr_fifo_ctl.u64);
				ipd_pwp_ptr_fifo_ctl.u64 =
				    cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);
				if (no_wptr)
					cvmx_fpa_free(cvmx_phys_to_ptr
						      ((uint64_t)
						       ipd_pwp_ptr_fifo_ctl.s.
						       ptr << 7),
						      CVMX_FPA_PACKET_POOL, 0);
				else
					cvmx_fpa_free(cvmx_phys_to_ptr
						      ((uint64_t)
						       ipd_pwp_ptr_fifo_ctl.s.
						       ptr << 7),
						      CVMX_FPA_WQE_POOL, 0);
			}
			ipd_pwp_ptr_fifo_ctl.s.cena = 1;
			cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL,
				       ipd_pwp_ptr_fifo_ctl.u64);
		}

		/* Free the prefetched packet */
		if (ipd_ptr_count.s.pktv_cnt) {
			union cvmx_ipd_pkt_ptr_valid ipd_pkt_ptr_valid;
			ipd_pkt_ptr_valid.u64 =
			    cvmx_read_csr(CVMX_IPD_PKT_PTR_VALID);
			cvmx_fpa_free(cvmx_phys_to_ptr
				      (ipd_pkt_ptr_valid.s.ptr << 7),
				      CVMX_FPA_PACKET_POOL, 0);
		}

		/* Free the per port prefetched packets */
		if (1) {
			int i;
			union cvmx_ipd_prc_port_ptr_fifo_ctl
			    ipd_prc_port_ptr_fifo_ctl;
			ipd_prc_port_ptr_fifo_ctl.u64 =
			    cvmx_read_csr(CVMX_IPD_PRC_PORT_PTR_FIFO_CTL);

			for (i = 0; i < ipd_prc_port_ptr_fifo_ctl.s.max_pkt;
			     i++) {
				ipd_prc_port_ptr_fifo_ctl.s.cena = 0;
				ipd_prc_port_ptr_fifo_ctl.s.raddr =
				    i % ipd_prc_port_ptr_fifo_ctl.s.max_pkt;
				cvmx_write_csr(CVMX_IPD_PRC_PORT_PTR_FIFO_CTL,
					       ipd_prc_port_ptr_fifo_ctl.u64);
				ipd_prc_port_ptr_fifo_ctl.u64 =
				    cvmx_read_csr
				    (CVMX_IPD_PRC_PORT_PTR_FIFO_CTL);
				cvmx_fpa_free(cvmx_phys_to_ptr
					      ((uint64_t)
					       ipd_prc_port_ptr_fifo_ctl.s.
					       ptr << 7), CVMX_FPA_PACKET_POOL,
					      0);
			}
			ipd_prc_port_ptr_fifo_ctl.s.cena = 1;
			cvmx_write_csr(CVMX_IPD_PRC_PORT_PTR_FIFO_CTL,
				       ipd_prc_port_ptr_fifo_ctl.u64);
		}

		/* Free all packets in the holding fifo */
		if (ipd_ptr_count.s.pfif_cnt) {
			int i;
			union cvmx_ipd_prc_hold_ptr_fifo_ctl
			    ipd_prc_hold_ptr_fifo_ctl;

			ipd_prc_hold_ptr_fifo_ctl.u64 =
			    cvmx_read_csr(CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL);

			for (i = 0; i < ipd_ptr_count.s.pfif_cnt; i++) {
				ipd_prc_hold_ptr_fifo_ctl.s.cena = 0;
				ipd_prc_hold_ptr_fifo_ctl.s.raddr =
				    (ipd_prc_hold_ptr_fifo_ctl.s.praddr +
				     i) % ipd_prc_hold_ptr_fifo_ctl.s.max_pkt;
				cvmx_write_csr(CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL,
					       ipd_prc_hold_ptr_fifo_ctl.u64);
				ipd_prc_hold_ptr_fifo_ctl.u64 =
				    cvmx_read_csr
				    (CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL);
				cvmx_fpa_free(cvmx_phys_to_ptr
					      ((uint64_t)
					       ipd_prc_hold_ptr_fifo_ctl.s.
					       ptr << 7), CVMX_FPA_PACKET_POOL,
					      0);
			}
			ipd_prc_hold_ptr_fifo_ctl.s.cena = 1;
			cvmx_write_csr(CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL,
				       ipd_prc_hold_ptr_fifo_ctl.u64);
		}

		/* Free all packets in the fifo */
		if (ipd_ptr_count.s.pkt_pcnt) {
			int i;
			union cvmx_ipd_pwp_ptr_fifo_ctl ipd_pwp_ptr_fifo_ctl;
			ipd_pwp_ptr_fifo_ctl.u64 =
			    cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);

			for (i = 0; i < ipd_ptr_count.s.pkt_pcnt; i++) {
				ipd_pwp_ptr_fifo_ctl.s.cena = 0;
				ipd_pwp_ptr_fifo_ctl.s.raddr =
				    (ipd_pwp_ptr_fifo_ctl.s.praddr +
				     i) % ipd_pwp_ptr_fifo_ctl.s.max_cnts;
				cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL,
					       ipd_pwp_ptr_fifo_ctl.u64);
				ipd_pwp_ptr_fifo_ctl.u64 =
				    cvmx_read_csr(CVMX_IPD_PWP_PTR_FIFO_CTL);
				cvmx_fpa_free(cvmx_phys_to_ptr
					      ((uint64_t) ipd_pwp_ptr_fifo_ctl.
					       s.ptr << 7),
					      CVMX_FPA_PACKET_POOL, 0);
			}
			ipd_pwp_ptr_fifo_ctl.s.cena = 1;
			cvmx_write_csr(CVMX_IPD_PWP_PTR_FIFO_CTL,
				       ipd_pwp_ptr_fifo_ctl.u64);
		}

		/* Reset the IPD to get all buffers out of it */
		{
			union cvmx_ipd_ctl_status ipd_ctl_status;
			ipd_ctl_status.u64 = cvmx_read_csr(CVMX_IPD_CTL_STATUS);
			ipd_ctl_status.s.reset = 1;
			cvmx_write_csr(CVMX_IPD_CTL_STATUS, ipd_ctl_status.u64);
		}

		/* Reset the PIP */
		{
			union cvmx_pip_sft_rst pip_sft_rst;
			pip_sft_rst.u64 = cvmx_read_csr(CVMX_PIP_SFT_RST);
			pip_sft_rst.s.rst = 1;
			cvmx_write_csr(CVMX_PIP_SFT_RST, pip_sft_rst.u64);
		}
	}
}

#endif /*  __CVMX_IPD_H__ */
