/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef MSS_EGRESS_REGS_HEADER
#define MSS_EGRESS_REGS_HEADER

#define MSS_EGRESS_CTL_REGISTER_ADDR 0x00005002
#define MSS_EGRESS_SA_EXPIRED_STATUS_REGISTER_ADDR 0x00005060
#define MSS_EGRESS_SA_THRESHOLD_EXPIRED_STATUS_REGISTER_ADDR 0x00005062
#define MSS_EGRESS_LUT_ADDR_CTL_REGISTER_ADDR 0x00005080
#define MSS_EGRESS_LUT_CTL_REGISTER_ADDR 0x00005081
#define MSS_EGRESS_LUT_DATA_CTL_REGISTER_ADDR 0x000050A0

struct mss_egress_ctl_register {
	union {
		struct {
			unsigned int soft_reset : 1;
			unsigned int drop_kay_packet : 1;
			unsigned int drop_egprc_lut_miss : 1;
			unsigned int gcm_start : 1;
			unsigned int gcm_test_mode : 1;
			unsigned int unmatched_use_sc_0 : 1;
			unsigned int drop_invalid_sa_sc_packets : 1;
			unsigned int reserved0 : 1;
			/* Should always be set to 0. */
			unsigned int external_classification_enable : 1;
			unsigned int icv_lsb_8bytes_enable : 1;
			unsigned int high_prio : 1;
			unsigned int clear_counter : 1;
			unsigned int clear_global_time : 1;
			unsigned int ethertype_explicit_sectag_lsb : 3;
		} bits_0;
		unsigned short word_0;
	};
	union {
		struct {
			unsigned int ethertype_explicit_sectag_msb : 13;
			unsigned int reserved0 : 3;
		} bits_1;
		unsigned short word_1;
	};
};

struct mss_egress_lut_addr_ctl_register {
	union {
		struct {
			unsigned int lut_addr : 9;
			unsigned int reserved0 : 3;
			/* 0x0 : Egress MAC Control FIlter (CTLF) LUT
			 * 0x1 : Egress Classification LUT
			 * 0x2 : Egress SC/SA LUT
			 * 0x3 : Egress SMIB
			 */
			unsigned int lut_select : 4;
		} bits_0;
		unsigned short word_0;
	};
};

struct mss_egress_lut_ctl_register {
	union {
		struct {
			unsigned int reserved0 : 14;
			unsigned int lut_read : 1;
			unsigned int lut_write : 1;
		} bits_0;
		unsigned short word_0;
	};
};

#endif /* MSS_EGRESS_REGS_HEADER */
