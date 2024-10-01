/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef MSS_INGRESS_REGS_HEADER
#define MSS_INGRESS_REGS_HEADER

#define MSS_INGRESS_CTL_REGISTER_ADDR 0x0000800E
#define MSS_INGRESS_LUT_ADDR_CTL_REGISTER_ADDR 0x00008080
#define MSS_INGRESS_LUT_CTL_REGISTER_ADDR 0x00008081
#define MSS_INGRESS_LUT_DATA_CTL_REGISTER_ADDR 0x000080A0

struct mss_ingress_ctl_register {
	union {
		struct {
			unsigned int soft_reset : 1;
			unsigned int operation_point_to_point : 1;
			unsigned int create_sci : 1;
			/* Unused  */
			unsigned int mask_short_length_error : 1;
			unsigned int drop_kay_packet : 1;
			unsigned int drop_igprc_miss : 1;
			/* Unused  */
			unsigned int check_icv : 1;
			unsigned int clear_global_time : 1;
			unsigned int clear_count : 1;
			unsigned int high_prio : 1;
			unsigned int remove_sectag : 1;
			unsigned int global_validate_frames : 2;
			unsigned int icv_lsb_8bytes_enabled : 1;
			unsigned int reserved0 : 2;
		} bits_0;
		unsigned short word_0;
	};
	union {
		struct {
			unsigned int reserved0 : 16;
		} bits_1;
		unsigned short word_1;
	};
};

struct mss_ingress_lut_addr_ctl_register {
	union {
		struct {
			unsigned int lut_addr : 9;
			unsigned int reserved0 : 3;
			/* 0x0 : Ingress Pre-Security MAC Control FIlter
			 *       (IGPRCTLF) LUT
			 * 0x1 : Ingress Pre-Security Classification LUT (IGPRC)
			 * 0x2 : Ingress Packet Format (IGPFMT) SAKey LUT
			 * 0x3 : Ingress Packet Format (IGPFMT) SC/SA LUT
			 * 0x4 : Ingress Post-Security Classification LUT
			 *       (IGPOC)
			 * 0x5 : Ingress Post-Security MAC Control Filter
			 *       (IGPOCTLF) LUT
			 * 0x6 : Ingress MIB (IGMIB)
			 */
			unsigned int lut_select : 4;
		} bits_0;
		unsigned short word_0;
	};
};

struct mss_ingress_lut_ctl_register {
	union {
		struct {
			unsigned int reserved0 : 14;
			unsigned int lut_read : 1;
			unsigned int lut_write : 1;
		} bits_0;
		unsigned short word_0;
	};
};

#endif /* MSS_INGRESS_REGS_HEADER */
