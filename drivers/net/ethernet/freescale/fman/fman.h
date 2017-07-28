/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FM_H
#define __FM_H

#include <linux/io.h>

/* FM Frame descriptor macros  */
/* Frame queue Context Override */
#define FM_FD_CMD_FCO                   0x80000000
#define FM_FD_CMD_RPD                   0x40000000  /* Read Prepended Data */
#define FM_FD_CMD_DTC                   0x10000000  /* Do L4 Checksum */

/* TX-Port: Unsupported Format */
#define FM_FD_ERR_UNSUPPORTED_FORMAT    0x04000000
/* TX Port: Length Error */
#define FM_FD_ERR_LENGTH                0x02000000
#define FM_FD_ERR_DMA                   0x01000000  /* DMA Data error */

/* IPR frame (not error) */
#define FM_FD_IPR                       0x00000001
/* IPR non-consistent-sp */
#define FM_FD_ERR_IPR_NCSP              (0x00100000 | FM_FD_IPR)
/* IPR error */
#define FM_FD_ERR_IPR                   (0x00200000 | FM_FD_IPR)
/* IPR timeout */
#define FM_FD_ERR_IPR_TO                (0x00300000 | FM_FD_IPR)
/* TX Port: Length Error */
#define FM_FD_ERR_IPRE                  (FM_FD_ERR_IPR & ~FM_FD_IPR)

/* Rx FIFO overflow, FCS error, code error, running disparity error
 * (SGMII and TBI modes), FIFO parity error. PHY Sequence error,
 * PHY error control character detected.
 */
#define FM_FD_ERR_PHYSICAL              0x00080000
/* Frame too long OR Frame size exceeds max_length_frame  */
#define FM_FD_ERR_SIZE                  0x00040000
/* classification discard */
#define FM_FD_ERR_CLS_DISCARD           0x00020000
/* Extract Out of Frame */
#define FM_FD_ERR_EXTRACTION            0x00008000
/* No Scheme Selected */
#define FM_FD_ERR_NO_SCHEME             0x00004000
/* Keysize Overflow */
#define FM_FD_ERR_KEYSIZE_OVERFLOW      0x00002000
/* Frame color is red */
#define FM_FD_ERR_COLOR_RED             0x00000800
/* Frame color is yellow */
#define FM_FD_ERR_COLOR_YELLOW          0x00000400
/* Parser Time out Exceed */
#define FM_FD_ERR_PRS_TIMEOUT           0x00000080
/* Invalid Soft Parser instruction */
#define FM_FD_ERR_PRS_ILL_INSTRUCT      0x00000040
/* Header error was identified during parsing */
#define FM_FD_ERR_PRS_HDR_ERR           0x00000020
/* Frame parsed beyind 256 first bytes */
#define FM_FD_ERR_BLOCK_LIMIT_EXCEEDED  0x00000008

/* non Frame-Manager error */
#define FM_FD_RX_STATUS_ERR_NON_FM      0x00400000

/* FMan driver defines */
#define FMAN_BMI_FIFO_UNITS		0x100
#define OFFSET_UNITS			16

/* BMan defines */
#define BM_MAX_NUM_OF_POOLS		64 /* Buffers pools */
#define FMAN_PORT_MAX_EXT_POOLS_NUM	8  /* External BM pools per Rx port */

struct fman; /* FMan data */

/* Enum for defining port types */
enum fman_port_type {
	FMAN_PORT_TYPE_TX = 0,	/* TX Port */
	FMAN_PORT_TYPE_RX,	/* RX Port */
};

struct fman_rev_info {
	u8 major;			/* Major revision */
	u8 minor;			/* Minor revision */
};

enum fman_exceptions {
	FMAN_EX_DMA_BUS_ERROR = 0,	/* DMA bus error. */
	FMAN_EX_DMA_READ_ECC,		/* Read Buffer ECC error */
	FMAN_EX_DMA_SYSTEM_WRITE_ECC,	/* Write Buffer ECC err on sys side */
	FMAN_EX_DMA_FM_WRITE_ECC,	/* Write Buffer ECC error on FM side */
	FMAN_EX_DMA_SINGLE_PORT_ECC,	/* Single Port ECC error on FM side */
	FMAN_EX_FPM_STALL_ON_TASKS,	/* Stall of tasks on FPM */
	FMAN_EX_FPM_SINGLE_ECC,		/* Single ECC on FPM. */
	FMAN_EX_FPM_DOUBLE_ECC,		/* Double ECC error on FPM ram access */
	FMAN_EX_QMI_SINGLE_ECC,	/* Single ECC on QMI. */
	FMAN_EX_QMI_DOUBLE_ECC,	/* Double bit ECC occurred on QMI */
	FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID,/* DeQ from unknown port id */
	FMAN_EX_BMI_LIST_RAM_ECC,	/* Linked List RAM ECC error */
	FMAN_EX_BMI_STORAGE_PROFILE_ECC,/* storage profile */
	FMAN_EX_BMI_STATISTICS_RAM_ECC,/* Statistics RAM ECC Err Enable */
	FMAN_EX_BMI_DISPATCH_RAM_ECC,	/* Dispatch RAM ECC Error Enable */
	FMAN_EX_IRAM_ECC,		/* Double bit ECC occurred on IRAM */
	FMAN_EX_MURAM_ECC		/* Double bit ECC occurred on MURAM */
};

/* Parse results memory layout */
struct fman_prs_result {
	u8 lpid;		/* Logical port id */
	u8 shimr;		/* Shim header result  */
	__be16 l2r;		/* Layer 2 result */
	__be16 l3r;		/* Layer 3 result */
	u8 l4r;		/* Layer 4 result */
	u8 cplan;		/* Classification plan id */
	__be16 nxthdr;		/* Next Header  */
	__be16 cksum;		/* Running-sum */
	/* Flags&fragment-offset field of the last IP-header */
	__be16 flags_frag_off;
	/* Routing type field of a IPV6 routing extension header */
	u8 route_type;
	/* Routing Extension Header Present; last bit is IP valid */
	u8 rhp_ip_valid;
	u8 shim_off[2];		/* Shim offset */
	u8 ip_pid_off;		/* IP PID (last IP-proto) offset */
	u8 eth_off;		/* ETH offset */
	u8 llc_snap_off;	/* LLC_SNAP offset */
	u8 vlan_off[2];		/* VLAN offset */
	u8 etype_off;		/* ETYPE offset */
	u8 pppoe_off;		/* PPP offset */
	u8 mpls_off[2];		/* MPLS offset */
	u8 ip_off[2];		/* IP offset */
	u8 gre_off;		/* GRE offset */
	u8 l4_off;		/* Layer 4 offset */
	u8 nxthdr_off;		/* Parser end point */
};

/* A structure for defining buffer prefix area content. */
struct fman_buffer_prefix_content {
	/* Number of bytes to be left at the beginning of the external
	 * buffer; Note that the private-area will start from the base
	 * of the buffer address.
	 */
	u16 priv_data_size;
	/* true to pass the parse result to/from the FM;
	 * User may use FM_PORT_GetBufferPrsResult() in
	 * order to get the parser-result from a buffer.
	 */
	bool pass_prs_result;
	/* true to pass the timeStamp to/from the FM User */
	bool pass_time_stamp;
	/* true to pass the KG hash result to/from the FM User may
	 * use FM_PORT_GetBufferHashResult() in order to get the
	 * parser-result from a buffer.
	 */
	bool pass_hash_result;
	/* Add all other Internal-Context information: AD,
	 * hash-result, key, etc.
	 */
	u16 data_align;
};

/* A structure of information about each of the external
 * buffer pools used by a port or storage-profile.
 */
struct fman_ext_pool_params {
	u8 id;		    /* External buffer pool id */
	u16 size;		    /* External buffer pool buffer size */
};

/* A structure for informing the driver about the external
 * buffer pools allocated in the BM and used by a port or a
 * storage-profile.
 */
struct fman_ext_pools {
	u8 num_of_pools_used; /* Number of pools use by this port */
	struct fman_ext_pool_params ext_buf_pool[FMAN_PORT_MAX_EXT_POOLS_NUM];
					/* Parameters for each port */
};

/* A structure for defining BM pool depletion criteria */
struct fman_buf_pool_depletion {
	/* select mode in which pause frames will be sent after a
	 * number of pools (all together!) are depleted
	 */
	bool pools_grp_mode_enable;
	/* the number of depleted pools that will invoke pause
	 * frames transmission.
	 */
	u8 num_of_pools;
	/* For each pool, true if it should be considered for
	 * depletion (Note - this pool must be used by this port!).
	 */
	bool pools_to_consider[BM_MAX_NUM_OF_POOLS];
	/* select mode in which pause frames will be sent
	 * after a single-pool is depleted;
	 */
	bool single_pool_mode_enable;
	/* For each pool, true if it should be considered
	 * for depletion (Note - this pool must be used by this port!)
	 */
	bool pools_to_consider_for_single_mode[BM_MAX_NUM_OF_POOLS];
};

/* Enum for inter-module interrupts registration */
enum fman_event_modules {
	FMAN_MOD_MAC = 0,		/* MAC event */
	FMAN_MOD_FMAN_CTRL,	/* FMAN Controller */
	FMAN_MOD_DUMMY_LAST
};

/* Enum for interrupts types */
enum fman_intr_type {
	FMAN_INTR_TYPE_ERR,
	FMAN_INTR_TYPE_NORMAL
};

/* Enum for inter-module interrupts registration */
enum fman_inter_module_event {
	FMAN_EV_ERR_MAC0 = 0,	/* MAC 0 error event */
	FMAN_EV_ERR_MAC1,		/* MAC 1 error event */
	FMAN_EV_ERR_MAC2,		/* MAC 2 error event */
	FMAN_EV_ERR_MAC3,		/* MAC 3 error event */
	FMAN_EV_ERR_MAC4,		/* MAC 4 error event */
	FMAN_EV_ERR_MAC5,		/* MAC 5 error event */
	FMAN_EV_ERR_MAC6,		/* MAC 6 error event */
	FMAN_EV_ERR_MAC7,		/* MAC 7 error event */
	FMAN_EV_ERR_MAC8,		/* MAC 8 error event */
	FMAN_EV_ERR_MAC9,		/* MAC 9 error event */
	FMAN_EV_MAC0,		/* MAC 0 event (Magic packet detection) */
	FMAN_EV_MAC1,		/* MAC 1 event (Magic packet detection) */
	FMAN_EV_MAC2,		/* MAC 2 (Magic packet detection) */
	FMAN_EV_MAC3,		/* MAC 3 (Magic packet detection) */
	FMAN_EV_MAC4,		/* MAC 4 (Magic packet detection) */
	FMAN_EV_MAC5,		/* MAC 5 (Magic packet detection) */
	FMAN_EV_MAC6,		/* MAC 6 (Magic packet detection) */
	FMAN_EV_MAC7,		/* MAC 7 (Magic packet detection) */
	FMAN_EV_MAC8,		/* MAC 8 event (Magic packet detection) */
	FMAN_EV_MAC9,		/* MAC 9 event (Magic packet detection) */
	FMAN_EV_FMAN_CTRL_0,	/* Fman controller event 0 */
	FMAN_EV_FMAN_CTRL_1,	/* Fman controller event 1 */
	FMAN_EV_FMAN_CTRL_2,	/* Fman controller event 2 */
	FMAN_EV_FMAN_CTRL_3,	/* Fman controller event 3 */
	FMAN_EV_CNT
};

struct fman_intr_src {
	void (*isr_cb)(void *src_arg);
	void *src_handle;
};

/* Structure for port-FM communication during fman_port_init. */
struct fman_port_init_params {
	u8 port_id;			/* port Id */
	enum fman_port_type port_type;	/* Port type */
	u16 port_speed;			/* Port speed */
	u16 liodn_offset;		/* Port's requested resource */
	u8 num_of_tasks;		/* Port's requested resource */
	u8 num_of_extra_tasks;		/* Port's requested resource */
	u8 num_of_open_dmas;		/* Port's requested resource */
	u8 num_of_extra_open_dmas;	/* Port's requested resource */
	u32 size_of_fifo;		/* Port's requested resource */
	u32 extra_size_of_fifo;		/* Port's requested resource */
	u8 deq_pipeline_depth;		/* Port's requested resource */
	u16 max_frame_length;		/* Port's max frame length. */
	u16 liodn_base;
	/* LIODN base for this port, to be used together with LIODN offset. */
};

void fman_get_revision(struct fman *fman, struct fman_rev_info *rev_info);

void fman_register_intr(struct fman *fman, enum fman_event_modules mod,
			u8 mod_id, enum fman_intr_type intr_type,
			void (*f_isr)(void *h_src_arg), void *h_src_arg);

void fman_unregister_intr(struct fman *fman, enum fman_event_modules mod,
			  u8 mod_id, enum fman_intr_type intr_type);

int fman_set_port_params(struct fman *fman,
			 struct fman_port_init_params *port_params);

int fman_reset_mac(struct fman *fman, u8 mac_id);

u16 fman_get_clock_freq(struct fman *fman);

u32 fman_get_bmi_max_fifo_size(struct fman *fman);

int fman_set_mac_max_frame(struct fman *fman, u8 mac_id, u16 mfl);

u32 fman_get_qman_channel_id(struct fman *fman, u32 port_id);

struct resource *fman_get_mem_region(struct fman *fman);

u16 fman_get_max_frm(void);

int fman_get_rx_extra_headroom(void);

struct fman *fman_bind(struct device *dev);

#endif /* __FM_H */
