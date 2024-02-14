/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell Fibre Channel HBA Driver
 * Copyright (C)  2018-	    Marvell
 *
 */
#ifndef __QLA_EDIF_BSG_H
#define __QLA_EDIF_BSG_H

#define EDIF_VERSION1 1

/* BSG Vendor specific commands */
#define	ELS_MAX_PAYLOAD		2112
#ifndef	WWN_SIZE
#define WWN_SIZE		8
#endif
#define VND_CMD_APP_RESERVED_SIZE	28
#define VND_CMD_PAD_SIZE                3
enum auth_els_sub_cmd {
	SEND_ELS = 0,
	SEND_ELS_REPLY,
	PULL_ELS,
};

struct extra_auth_els {
	enum auth_els_sub_cmd sub_cmd;
	uint32_t        extra_rx_xchg_address;
	uint8_t         extra_control_flags;
#define BSG_CTL_FLAG_INIT       0
#define BSG_CTL_FLAG_LS_ACC     1
#define BSG_CTL_FLAG_LS_RJT     2
#define BSG_CTL_FLAG_TRM        3
	uint8_t		version;
	uint8_t		pad[2];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct qla_bsg_auth_els_request {
	struct fc_bsg_request r;
	struct extra_auth_els e;
};

struct qla_bsg_auth_els_reply {
	struct fc_bsg_reply r;
	uint32_t rx_xchg_address;
	uint8_t version;
	uint8_t pad[VND_CMD_PAD_SIZE];
	uint8_t reserved[VND_CMD_APP_RESERVED_SIZE];
};

struct app_id {
	int		app_vid;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_start_reply {
	uint32_t	host_support_edif;
	uint32_t	edif_enode_active;
	uint32_t	edif_edb_active;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_start {
	struct app_id	app_info;
	uint8_t         app_start_flags;
	uint8_t		version;
	uint8_t		pad[2];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_stop {
	struct app_id	app_info;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_plogi_reply {
	uint32_t	prli_status;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_pinfo_req {
	struct app_id app_info;
	uint8_t	 num_ports;
	struct {
#ifdef __BIG_ENDIAN
		uint8_t domain;
		uint8_t area;
		uint8_t al_pa;
#elif defined(__LITTLE_ENDIAN)
		uint8_t al_pa;
		uint8_t area;
		uint8_t domain;
#else
#error "__BIG_ENDIAN or __LITTLE_ENDIAN must be defined!"
#endif
		uint8_t rsvd_1;
	} remote_pid;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_pinfo {
	port_id_t remote_pid;
	uint8_t	remote_wwpn[WWN_SIZE];
	uint8_t	remote_type;
#define	VND_CMD_RTYPE_UNKNOWN		0
#define	VND_CMD_RTYPE_TARGET		1
#define	VND_CMD_RTYPE_INITIATOR		2
	uint8_t	remote_state;
	uint8_t	auth_state;
	uint8_t	version;
	uint8_t	pad[VND_CMD_PAD_SIZE];
	uint8_t	reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

/* AUTH States */
#define	VND_CMD_AUTH_STATE_UNDEF	0
#define	VND_CMD_AUTH_STATE_SESSION_SHUTDOWN	1
#define	VND_CMD_AUTH_STATE_NEEDED	2
#define	VND_CMD_AUTH_STATE_ELS_RCVD	3
#define	VND_CMD_AUTH_STATE_SAUPDATE_COMPL 4

struct app_pinfo_reply {
	uint8_t		port_count;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
	struct app_pinfo ports[];
} __packed;

struct app_sinfo_req {
	struct app_id	app_info;
	uint8_t		num_ports;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_sinfo {
	uint8_t	remote_wwpn[WWN_SIZE];
	int64_t	rekey_count;
	uint8_t	rekey_mode;
	int64_t	tx_bytes;
	int64_t	rx_bytes;
} __packed;

struct app_stats_reply {
	uint8_t		elem_count;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
	struct app_sinfo elem[];
} __packed;

struct qla_sa_update_frame {
	struct app_id	app_info;
	uint16_t	flags;
#define SAU_FLG_INV		0x01	/* delete key */
#define SAU_FLG_TX		0x02	/* 1=tx, 0 = rx */
#define SAU_FLG_FORCE_DELETE	0x08
#define SAU_FLG_GMAC_MODE	0x20	/*
					 * GMAC mode is cleartext for the IO
					 * (i.e. NULL encryption)
					 */
#define SAU_FLG_KEY128          0x40
#define SAU_FLG_KEY256          0x80
	uint16_t        fast_sa_index:10,
			reserved:6;
	uint32_t	salt;
	uint32_t	spi;
	uint8_t		sa_key[32];
	uint8_t		node_name[WWN_SIZE];
	uint8_t		port_name[WWN_SIZE];
	port_id_t	port_id;
	uint8_t		version;
	uint8_t		pad[VND_CMD_PAD_SIZE];
	uint8_t		reserved2[VND_CMD_APP_RESERVED_SIZE];
} __packed;

#define	QL_VND_SC_UNDEF		0
#define	QL_VND_SC_SA_UPDATE	1
#define	QL_VND_SC_APP_START	2
#define	QL_VND_SC_APP_STOP	3
#define	QL_VND_SC_AUTH_OK	4
#define	QL_VND_SC_AUTH_FAIL	5
#define	QL_VND_SC_REKEY_CONFIG	6
#define	QL_VND_SC_GET_FCINFO	7
#define	QL_VND_SC_GET_STATS	8
#define QL_VND_SC_AEN_COMPLETE  9
#define QL_VND_SC_READ_DBELL	10

/*
 * bsg caller to provide empty buffer for doorbell events.
 *
 * sg_io_v4.din_xferp  = empty buffer for door bell events
 * sg_io_v4.dout_xferp = struct edif_read_dbell *buf
 */
struct edif_read_dbell {
	struct app_id app_info;
	uint8_t version;
	uint8_t pad[VND_CMD_PAD_SIZE];
	uint8_t reserved[VND_CMD_APP_RESERVED_SIZE];
};


/* Application interface data structure for rtn data */
#define	EXT_DEF_EVENT_DATA_SIZE	64
struct edif_app_dbell {
	uint32_t	event_code;
	uint32_t	event_data_size;
	union  {
		port_id_t	port_id;
		uint8_t		event_data[EXT_DEF_EVENT_DATA_SIZE];
	};
} __packed;

struct edif_sa_update_aen {
	port_id_t port_id;
	uint32_t key_type;	/* Tx (1) or RX (2) */
	uint32_t status;	/* 0 succes,  1 failed, 2 timeout , 3 error */
	uint8_t	version;
	uint8_t	pad[VND_CMD_PAD_SIZE];
	uint8_t	reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

#define	QL_VND_SA_STAT_SUCCESS	0
#define	QL_VND_SA_STAT_FAILED	1
#define	QL_VND_SA_STAT_TIMEOUT	2
#define	QL_VND_SA_STAT_ERROR	3

#define	QL_VND_RX_SA_KEY	1
#define	QL_VND_TX_SA_KEY	2

/* App defines for plogi auth'd ok and plogi auth bad requests */
struct auth_complete_cmd {
	struct app_id app_info;
#define PL_TYPE_WWPN    1
#define PL_TYPE_DID     2
	uint32_t    type;
	union {
		uint8_t  wwpn[WWN_SIZE];
		port_id_t d_id;
	} u;
	uint8_t	version;
	uint8_t	pad[VND_CMD_PAD_SIZE];
	uint8_t	reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct aen_complete_cmd {
	struct app_id app_info;
	port_id_t   port_id;
	uint32_t    event_code;
	uint8_t     version;
	uint8_t     pad[VND_CMD_PAD_SIZE];
	uint8_t     reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

#define RX_DELAY_DELETE_TIMEOUT 20

#define FCH_EVT_VENDOR_UNIQUE_VPORT_DOWN  1

#endif	/* QLA_EDIF_BSG_H */
