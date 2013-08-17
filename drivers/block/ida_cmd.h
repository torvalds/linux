/*
 *    Disk Array driver for Compaq SMART2 Controllers
 *    Copyright 1998 Compaq Computer Corporation
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *
 */
#ifndef ARRAYCMD_H
#define ARRAYCMD_H

#include <asm/types.h>
#if 0
#include <linux/blkdev.h>
#endif

/* for the Smart Array 42XX cards */
#define S42XX_REQUEST_PORT_OFFSET	0x40
#define S42XX_REPLY_INTR_MASK_OFFSET	0x34
#define S42XX_REPLY_PORT_OFFSET		0x44
#define S42XX_INTR_STATUS		0x30

#define S42XX_INTR_OFF		0x08
#define S42XX_INTR_PENDING	0x08

#define COMMAND_FIFO		0x04
#define COMMAND_COMPLETE_FIFO	0x08
#define INTR_MASK		0x0C
#define INTR_STATUS		0x10
#define INTR_PENDING		0x14

#define FIFO_NOT_EMPTY		0x01
#define FIFO_NOT_FULL		0x02

#define BIG_PROBLEM		0x40
#define LOG_NOT_CONF		2

#pragma pack(1)
typedef struct {
	__u32	size;
	__u32	addr;
} sg_t;

#define RCODE_NONFATAL	0x02
#define RCODE_FATAL	0x04
#define RCODE_INVREQ	0x10
typedef struct {
	__u16	next;
	__u8	cmd;
	__u8	rcode;
	__u32	blk;
	__u16	blk_cnt;
	__u8	sg_cnt;
	__u8	reserved;
} rhdr_t;

#define SG_MAX			32
typedef struct {
	rhdr_t	hdr;
	sg_t	sg[SG_MAX];
	__u32	bp;
} rblk_t;

typedef struct {
	__u8	unit;
	__u8	prio;
	__u16	size;
} chdr_t;

#define CMD_RWREQ	0x00
#define CMD_IOCTL_PEND	0x01
#define CMD_IOCTL_DONE	0x02

typedef struct cmdlist {
	chdr_t	hdr;
	rblk_t	req;
	__u32	size;
	int	retry_cnt;
	__u32	busaddr;
	int	ctlr;
	struct cmdlist *prev;
	struct cmdlist *next;
	struct request *rq;
	int type;
} cmdlist_t;
	
#define ID_CTLR		0x11
typedef struct {
	__u8	nr_drvs;
	__u32	cfg_sig;
	__u8	firm_rev[4];
	__u8	rom_rev[4];
	__u8	hw_rev;
	__u32	bb_rev;
	__u32	drv_present_map;
	__u32	ext_drv_map;
	__u32	board_id;
	__u8	cfg_error;
	__u32	non_disk_bits;
	__u8	bad_ram_addr;
	__u8	cpu_rev;
	__u8	pdpi_rev;
	__u8	epic_rev;
	__u8	wcxc_rev;
	__u8	marketing_rev;
	__u8	ctlr_flags;
	__u8	host_flags;
	__u8	expand_dis;
	__u8	scsi_chips;
	__u32	max_req_blocks;
	__u32	ctlr_clock;
	__u8	drvs_per_bus;
	__u16	big_drv_present_map[8];
	__u16	big_ext_drv_map[8];
	__u16	big_non_disk_map[8];
	__u16	task_flags;
	__u8	icl_bus;
	__u8	red_modes;
	__u8	cur_red_mode;
	__u8	red_ctlr_stat;
	__u8	red_fail_reason;
	__u8	reserved[403];
} id_ctlr_t;

typedef struct {
	__u16	cyl;
	__u8	heads;
	__u8	xsig;
	__u8	psectors;
	__u16	wpre;
	__u8	maxecc;
	__u8	drv_ctrl;
	__u16	pcyls;
	__u8	pheads;
	__u16	landz;
	__u8	sect_per_track;
	__u8	cksum;
} drv_param_t;

#define ID_LOG_DRV	0x10
typedef struct {
	__u16	blk_size;
	__u32	nr_blks;
	drv_param_t drv;
	__u8	fault_tol;
	__u8	reserved;
	__u8	bios_disable;
} id_log_drv_t;

#define ID_LOG_DRV_EXT	0x18
typedef struct {
	__u32	log_drv_id;
	__u8	log_drv_label[64];
	__u8	reserved[418];
} id_log_drv_ext_t;

#define SENSE_LOG_DRV_STAT	0x12
typedef struct {
	__u8	status;
	__u32	fail_map;
	__u16	read_err[32];
	__u16	write_err[32];
	__u8	drv_err_data[256];
	__u8	drq_timeout[32];
	__u32	blks_to_recover;
	__u8	drv_recovering;
	__u16	remap_cnt[32];
	__u32	replace_drv_map;
	__u32	act_spare_map;
	__u8	spare_stat;
	__u8	spare_repl_map[32];
	__u32	repl_ok_map;
	__u8	media_exch;
	__u8	cache_fail;
	__u8	expn_fail;
	__u8	unit_flags;
	__u16	big_fail_map[8];
	__u16	big_remap_map[128];
	__u16	big_repl_map[8];
	__u16	big_act_spare_map[8];
	__u8	big_spar_repl_map[128];
	__u16	big_repl_ok_map[8];
	__u8	big_drv_rebuild;
	__u8	reserved[36];
} sense_log_drv_stat_t;

#define START_RECOVER		0x13

#define ID_PHYS_DRV		0x15
typedef struct {
	__u8	scsi_bus;
	__u8	scsi_id;
	__u16	blk_size;
	__u32	nr_blks;
	__u32	rsvd_blks;
	__u8	drv_model[40];
	__u8	drv_sn[40];
	__u8	drv_fw[8];
	__u8	scsi_iq_bits;
	__u8	compaq_drv_stmp;
	__u8	last_fail;
	__u8	phys_drv_flags;
	__u8	phys_drv_flags1;
	__u8	scsi_lun;
	__u8	phys_drv_flags2;
	__u8	reserved;
	__u32	spi_speed_rules;
	__u8	phys_connector[2];
	__u8	phys_box_on_bus;
	__u8	phys_bay_in_box;
} id_phys_drv_t;

#define BLINK_DRV_LEDS		0x16
typedef struct {
	__u32	blink_duration;
	__u32	reserved;
	__u8	blink[256];
	__u8	reserved1[248];
} blink_drv_leds_t;

#define SENSE_BLINK_LEDS	0x17
typedef struct {
	__u32	blink_duration;
	__u32	btime_elap;
	__u8	blink[256];
	__u8	reserved1[248];
} sense_blink_leds_t;

#define IDA_READ		0x20
#define IDA_WRITE		0x30
#define IDA_WRITE_MEDIA		0x31
#define RESET_TO_DIAG		0x40
#define DIAG_PASS_THRU		0x41

#define SENSE_CONFIG		0x50
#define SET_CONFIG		0x51
typedef struct {
	__u32	cfg_sig;
	__u16	compat_port;
	__u8	data_dist_mode;
	__u8	surf_an_ctrl;
	__u16	ctlr_phys_drv;
	__u16	log_unit_phys_drv;
	__u16	fault_tol_mode;
	__u8	phys_drv_param[16];
	drv_param_t drv;
	__u32	drv_asgn_map;
	__u16	dist_factor;
	__u32	spare_asgn_map;
	__u8	reserved[6];
	__u16	os;
	__u8	ctlr_order;
	__u8	extra_info;
	__u32	data_offs;
	__u8	parity_backedout_write_drvs;
	__u8	parity_dist_mode;
	__u8	parity_shift_fact;
	__u8	bios_disable_flag;
	__u32	blks_on_vol;
	__u32	blks_per_drv;
	__u8	scratch[16];
	__u16	big_drv_map[8];
	__u16	big_spare_map[8];
	__u8	ss_source_vol;
	__u8	mix_drv_cap_range;
	struct {
		__u16	big_drv_map[8];
		__u32	blks_per_drv;
		__u16	fault_tol_mode;
		__u16	dist_factor;
	} MDC_range[4];
	__u8	reserved1[248];
} config_t;

#define BYPASS_VOL_STATE	0x52
#define SS_CREATE_VOL		0x53
#define CHANGE_CONFIG		0x54
#define SENSE_ORIG_CONF		0x55
#define REORDER_LOG_DRV		0x56
typedef struct {
	__u8	old_units[32];
} reorder_log_drv_t;

#define LABEL_LOG_DRV		0x57
typedef struct {
	__u8	log_drv_label[64];
} label_log_drv_t;

#define SS_TO_VOL		0x58
	
#define SET_SURF_DELAY		0x60
typedef struct {
	__u16	delay;
	__u8	reserved[510];
} surf_delay_t;

#define SET_OVERHEAT_DELAY	0x61
typedef struct {
	__u16	delay;
} overhead_delay_t;
 
#define SET_MP_DELAY
typedef struct {
	__u16	delay;
	__u8	reserved[510];
} mp_delay_t;

#define PASSTHRU_A	0x91
typedef struct {
	__u8	target;
	__u8	bus;
	__u8	lun;
	__u32	timeout;
	__u32	flags;
	__u8	status;
	__u8	error;
	__u8	cdb_len;
	__u8	sense_error;
	__u8	sense_key;
	__u32	sense_info;
	__u8	sense_code;
	__u8	sense_qual;
	__u32	residual;
	__u8	reserved[4];
	__u8	cdb[12];	
} scsi_param_t;

#define RESUME_BACKGROUND_ACTIVITY	0x99
#define SENSE_CONTROLLER_PERFORMANCE	0xa8
#define FLUSH_CACHE			0xc2
#define COLLECT_BUFFER			0xd2
#define READ_FLASH_ROM			0xf6
#define WRITE_FLASH_ROM			0xf7
#pragma pack()	

#endif /* ARRAYCMD_H */
