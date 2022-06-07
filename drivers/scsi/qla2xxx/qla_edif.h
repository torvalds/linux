/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell Fibre Channel HBA Driver
 * Copyright (c)  2021    Marvell
 */
#ifndef __QLA_EDIF_H
#define __QLA_EDIF_H

struct qla_scsi_host;
#define EDIF_APP_ID 0x73730001

#define EDIF_MAX_INDEX	2048
struct edif_sa_ctl {
	struct list_head next;
	uint16_t	del_index;
	uint16_t	index;
	uint16_t	slot;
	uint16_t	flags;
#define	EDIF_SA_CTL_FLG_REPL		BIT_0
#define	EDIF_SA_CTL_FLG_DEL		BIT_1
#define EDIF_SA_CTL_FLG_CLEANUP_DEL BIT_4
	// Invalidate Index bit and mirrors QLA_SA_UPDATE_FLAGS_DELETE
	unsigned long   state;
#define EDIF_SA_CTL_USED	1	/* Active Sa update  */
#define EDIF_SA_CTL_PEND	2	/* Waiting for slot */
#define EDIF_SA_CTL_REPL	3	/* Active Replace and Delete */
#define EDIF_SA_CTL_DEL		4	/* Delete Pending */
	struct fc_port	*fcport;
	struct bsg_job *bsg_job;
	struct qla_sa_update_frame sa_frame;
};

enum enode_flags_t {
	ENODE_ACTIVE = 0x1,
};

struct pur_core {
	enum enode_flags_t	enode_flags;
	spinlock_t		pur_lock;
	struct  list_head	head;
};

enum db_flags_t {
	EDB_ACTIVE = BIT_0,
};

#define DBELL_ACTIVE(_v) (_v->e_dbell.db_flags & EDB_ACTIVE)
#define DBELL_INACTIVE(_v) (!(_v->e_dbell.db_flags & EDB_ACTIVE))

struct edif_dbell {
	enum db_flags_t		db_flags;
	spinlock_t		db_lock;
	struct  list_head	head;
	struct bsg_job *dbell_bsg_job;
	unsigned long bsg_expire;
};

#define SA_UPDATE_IOCB_TYPE            0x71    /* Security Association Update IOCB entry */
struct sa_update_28xx {
	uint8_t entry_type;             /* Entry type. */
	uint8_t entry_count;            /* Entry count. */
	uint8_t sys_define;             /* System Defined. */
	uint8_t entry_status;           /* Entry Status. */

	uint32_t handle;                /* IOCB System handle. */

	union {
		__le16 nport_handle;  /* in: N_PORT handle. */
		__le16 comp_sts;              /* out: completion status */
#define CS_PORT_EDIF_UNAVAIL	0x28
#define CS_PORT_EDIF_LOGOUT	0x29
#define CS_PORT_EDIF_SUPP_NOT_RDY 0x64
#define CS_PORT_EDIF_INV_REQ      0x66
	} u;
	uint8_t vp_index;
	uint8_t reserved_1;
	uint8_t port_id[3];
	uint8_t flags;
#define SA_FLAG_INVALIDATE BIT_0
#define SA_FLAG_TX	   BIT_1 // 1=tx, 0=rx

	uint8_t sa_key[32];     /* 256 bit key */
	__le32 salt;
	__le32 spi;
	uint8_t sa_control;
#define SA_CNTL_ENC_FCSP        (1 << 3)
#define SA_CNTL_ENC_OPD         (2 << 3)
#define SA_CNTL_ENC_MSK         (3 << 3)  // mask bits 4,3
#define SA_CNTL_AES_GMAC	(1 << 2)
#define SA_CNTL_KEY256          (2 << 0)
#define SA_CNTL_KEY128          0

	uint8_t reserved_2;
	__le16 sa_index;   // reserve: bit 11-15
	__le16 old_sa_info;
	__le16 new_sa_info;
};

#define        NUM_ENTRIES     256
#define        PUR_GET         1

struct dinfo {
	int		nodecnt;
	int		lstate;
};

struct pur_ninfo {
	port_id_t       pur_sid;
	port_id_t	pur_did;
	uint8_t		vp_idx;
	short           pur_bytes_rcvd;
	unsigned short  pur_nphdl;
	unsigned int    pur_rx_xchg_address;
};

struct purexevent {
	struct  pur_ninfo	pur_info;
	unsigned char		*msgp;
	u32			msgp_len;
};

#define	N_UNDEF		0
#define	N_PUREX		1
struct enode {
	struct list_head	list;
	struct dinfo		dinfo;
	uint32_t		ntype;
	union {
		struct purexevent	purexinfo;
	} u;
};

#define RX_ELS_SIZE (roundup(sizeof(struct enode) + ELS_MAX_PAYLOAD, SMP_CACHE_BYTES))

#define EDIF_SESSION_DOWN(_s) \
	(qla_ini_mode_enabled(_s->vha) && (_s->disc_state == DSC_DELETE_PEND || \
	 _s->disc_state == DSC_DELETED || \
	 !_s->edif.app_sess_online))

#define EDIF_NEGOTIATION_PENDING(_fcport) \
	(DBELL_ACTIVE(_fcport->vha) && \
	 (_fcport->disc_state == DSC_LOGIN_AUTH_PEND))

#endif	/* __QLA_EDIF_H */
