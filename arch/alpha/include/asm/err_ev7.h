/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_ERR_EV7_H
#define __ALPHA_ERR_EV7_H 1

/*
 * Data for el packet class PAL (14), type LOGOUT_FRAME (1)
 */
struct ev7_pal_logout_subpacket {
	u32 mchk_code;
	u32 subpacket_count;
	u64 whami;
	u64 rbox_whami;
	u64 rbox_int;
	u64 exc_addr;
	union el_timestamp timestamp;
	u64 halt_code;
	u64 reserved;
};

/*
 * Data for el packet class PAL (14), type EV7_PROCESSOR (4)
 */
struct ev7_pal_processor_subpacket {
	u64 i_stat;
	u64 dc_stat;
	u64 c_addr;
	u64 c_syndrome_1;
	u64 c_syndrome_0;
	u64 c_stat;
	u64 c_sts;
	u64 mm_stat;
	u64 exc_addr;
	u64 ier_cm;
	u64 isum;
	u64 pal_base;
	u64 i_ctl;
	u64 process_context;
	u64 cbox_ctl;
	u64 cbox_stp_ctl;
	u64 cbox_acc_ctl;
	u64 cbox_lcl_set;
	u64 cbox_gbl_set;
	u64 bbox_ctl;
	u64 bbox_err_sts;
	u64 bbox_err_idx;
	u64 cbox_ddp_err_sts;
	u64 bbox_dat_rmp;
	u64 reserved[2];
};

/*
 * Data for el packet class PAL (14), type EV7_ZBOX (5)
 */
struct ev7_pal_zbox_subpacket {
	u32 zbox0_dram_err_status_1;
	u32 zbox0_dram_err_status_2;
	u32 zbox0_dram_err_status_3;
	u32 zbox0_dram_err_ctl;
	u32 zbox0_dram_err_adr;
	u32 zbox0_dift_timeout;
	u32 zbox0_dram_mapper_ctl;
	u32 zbox0_frc_err_adr;
	u32 zbox0_dift_err_status;
	u32 reserved1;
	u32 zbox1_dram_err_status_1;
	u32 zbox1_dram_err_status_2;
	u32 zbox1_dram_err_status_3;
	u32 zbox1_dram_err_ctl;
	u32 zbox1_dram_err_adr;
	u32 zbox1_dift_timeout;
	u32 zbox1_dram_mapper_ctl;
	u32 zbox1_frc_err_adr;
	u32 zbox1_dift_err_status;
	u32 reserved2;
	u64 cbox_ctl;
	u64 cbox_stp_ctl;
	u64 zbox0_error_pa;
	u64 zbox1_error_pa;
	u64 zbox0_ored_syndrome;
	u64 zbox1_ored_syndrome;
	u64 reserved3[2];
};

/*
 * Data for el packet class PAL (14), type EV7_RBOX (6)
 */
struct ev7_pal_rbox_subpacket {
	u64 rbox_cfg;
	u64 rbox_n_cfg;
	u64 rbox_s_cfg;
	u64 rbox_e_cfg;
	u64 rbox_w_cfg;
	u64 rbox_n_err;
	u64 rbox_s_err;
	u64 rbox_e_err;
	u64 rbox_w_err;
	u64 rbox_io_cfg;
	u64 rbox_io_err;
	u64 rbox_l_err;
	u64 rbox_whoami;
	u64 rbox_imask;
	u64 rbox_intq;
	u64 rbox_int;
	u64 reserved[2];
};

/*
 * Data for el packet class PAL (14), type EV7_IO (7)
 */
struct ev7_pal_io_one_port {
	u64 pox_err_sum;
	u64 pox_tlb_err;
	u64 pox_spl_cmplt;
	u64 pox_trans_sum;
	u64 pox_first_err;
	u64 pox_mult_err;
	u64 pox_dm_source;
	u64 pox_dm_dest;
	u64 pox_dm_size;
	u64 pox_dm_ctrl;
	u64 reserved;
};

struct ev7_pal_io_subpacket {
	u64 io_asic_rev;
	u64 io_sys_rev;
	u64 io7_uph;
	u64 hpi_ctl;
	u64 crd_ctl;
	u64 hei_ctl;
	u64 po7_error_sum;
	u64 po7_uncrr_sym;
	u64 po7_crrct_sym;
	u64 po7_ugbge_sym;
	u64 po7_err_pkt0;
	u64 po7_err_pkt1;
	u64 reserved[2];
	struct ev7_pal_io_one_port ports[4];
};

/*
 * Environmental subpacket. Data used for el packets:
 * 	   class PAL (14), type AMBIENT_TEMPERATURE (10)
 * 	   class PAL (14), type AIRMOVER_FAN (11)
 * 	   class PAL (14), type VOLTAGE (12)
 * 	   class PAL (14), type INTRUSION (13)
 *	   class PAL (14), type POWER_SUPPLY (14)
 *	   class PAL (14), type LAN (15)
 *	   class PAL (14), type HOT_PLUG (16)
 */
struct ev7_pal_environmental_subpacket {
	u16 cabinet;
	u16 drawer;
	u16 reserved1[2];
	u8 module_type;
	u8 unit_id;		/* unit reporting condition */
	u8 reserved2;
	u8 condition;		/* condition reported       */
};

/*
 * Convert environmental type to index
 */
static inline int ev7_lf_env_index(int type)
{
	BUG_ON((type < EL_TYPE__PAL__ENV__AMBIENT_TEMPERATURE) 
	       || (type > EL_TYPE__PAL__ENV__HOT_PLUG));

	return type - EL_TYPE__PAL__ENV__AMBIENT_TEMPERATURE;
}

/*
 * Data for generic el packet class PAL.
 */
struct ev7_pal_subpacket {
	union {
		struct ev7_pal_logout_subpacket logout;	     /* Type     1 */
		struct ev7_pal_processor_subpacket ev7;	     /* Type     4 */
		struct ev7_pal_zbox_subpacket zbox;	     /* Type     5 */
		struct ev7_pal_rbox_subpacket rbox;	     /* Type     6 */
		struct ev7_pal_io_subpacket io;		     /* Type     7 */
		struct ev7_pal_environmental_subpacket env;  /* Type 10-16 */
		u64 as_quad[1];				     /* Raw u64    */
	} by_type;
};

/*
 * Struct to contain collected logout from subpackets.
 */
struct ev7_lf_subpackets {
	struct ev7_pal_logout_subpacket *logout;		/* Type  1 */
	struct ev7_pal_processor_subpacket *ev7;		/* Type  4 */
	struct ev7_pal_zbox_subpacket *zbox;			/* Type  5 */
	struct ev7_pal_rbox_subpacket *rbox;			/* Type  6 */
	struct ev7_pal_io_subpacket *io;			/* Type  7 */
	struct ev7_pal_environmental_subpacket *env[7];	     /* Type 10-16 */

	unsigned int io_pid;
};

#endif /* __ALPHA_ERR_EV7_H */


