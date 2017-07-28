/*
 * Copyright (c) 2010 Cisco Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef __TCM_FC_H__
#define __TCM_FC_H__

#include <linux/types.h>
#include <target/target_core_base.h>

#define FT_VERSION "0.4"

#define FT_NAMELEN 32		/* length of ASCII WWPNs including pad */
#define FT_TPG_NAMELEN 32	/* max length of TPG name */
#define FT_LUN_NAMELEN 32	/* max length of LUN name */
#define TCM_FC_DEFAULT_TAGS 512	/* tags used for per-session preallocation */

struct ft_transport_id {
	__u8	format;
	__u8	__resvd1[7];
	__u8	wwpn[8];
	__u8	__resvd2[8];
} __attribute__((__packed__));

/*
 * Session (remote port).
 */
struct ft_sess {
	u32 port_id;			/* for hash lookup use only */
	u32 params;
	u16 max_frame;			/* maximum frame size */
	u64 port_name;			/* port name for transport ID */
	struct ft_tport *tport;
	struct se_session *se_sess;
	struct hlist_node hash;		/* linkage in ft_sess_hash table */
	struct rcu_head rcu;
	struct kref kref;		/* ref for hash and outstanding I/Os */
};

/*
 * Hash table of sessions per local port.
 * Hash lookup by remote port FC_ID.
 */
#define	FT_SESS_HASH_BITS	6
#define	FT_SESS_HASH_SIZE	(1 << FT_SESS_HASH_BITS)

/*
 * Per local port data.
 * This is created only after a TPG exists that allows target function
 * for the local port.  If the TPG exists, this is allocated when
 * we're notified that the local port has been created, or when
 * the first PRLI provider callback is received.
 */
struct ft_tport {
	struct fc_lport *lport;
	struct ft_tpg *tpg;		/* NULL if TPG deleted before tport */
	u32	sess_count;		/* number of sessions in hash */
	struct rcu_head rcu;
	struct hlist_head hash[FT_SESS_HASH_SIZE];	/* list of sessions */
};

/*
 * Node ID and authentication.
 */
struct ft_node_auth {
	u64	port_name;
	u64	node_name;
};

/*
 * Node ACL for FC remote port session.
 */
struct ft_node_acl {
	struct se_node_acl se_node_acl;
	struct ft_node_auth node_auth;
};

struct ft_lun {
	u32 index;
	char name[FT_LUN_NAMELEN];
};

/*
 * Target portal group (local port).
 */
struct ft_tpg {
	u32 index;
	struct ft_lport_wwn *lport_wwn;
	struct ft_tport *tport;		/* active tport or NULL */
	struct list_head lun_list;	/* head of LUNs */
	struct se_portal_group se_tpg;
	struct workqueue_struct *workqueue;
};

struct ft_lport_wwn {
	u64 wwpn;
	char name[FT_NAMELEN];
	struct list_head ft_wwn_node;
	struct ft_tpg *tpg;
	struct se_wwn se_wwn;
};

/*
 * Commands
 */
struct ft_cmd {
	struct ft_sess *sess;		/* session held for cmd */
	struct fc_seq *seq;		/* sequence in exchange mgr */
	struct se_cmd se_cmd;		/* Local TCM I/O descriptor */
	struct fc_frame *req_frame;
	u32 write_data_len;		/* data received on writes */
	struct work_struct work;
	/* Local sense buffer */
	unsigned char ft_sense_buffer[TRANSPORT_SENSE_BUFFER];
	u32 was_ddp_setup:1;		/* Set only if ddp is setup */
	u32 aborted:1;			/* Set if aborted by reset or timeout */
	struct scatterlist *sg;		/* Set only if DDP is setup */
	u32 sg_cnt;			/* No. of item in scatterlist */
};

extern struct mutex ft_lport_lock;
extern struct fc4_prov ft_prov;
extern unsigned int ft_debug_logging;

/*
 * Fabric methods.
 */

/*
 * Session ops.
 */
void ft_sess_put(struct ft_sess *);
void ft_sess_close(struct se_session *);
u32 ft_sess_get_index(struct se_session *);
u32 ft_sess_get_port_name(struct se_session *, unsigned char *, u32);

void ft_lport_add(struct fc_lport *, void *);
void ft_lport_del(struct fc_lport *, void *);
int ft_lport_notify(struct notifier_block *, unsigned long, void *);

/*
 * IO methods.
 */
int ft_check_stop_free(struct se_cmd *);
void ft_release_cmd(struct se_cmd *);
int ft_queue_status(struct se_cmd *);
int ft_queue_data_in(struct se_cmd *);
int ft_write_pending(struct se_cmd *);
int ft_write_pending_status(struct se_cmd *);
int ft_get_cmd_state(struct se_cmd *);
void ft_queue_tm_resp(struct se_cmd *);
void ft_aborted_task(struct se_cmd *);

/*
 * other internal functions.
 */
void ft_recv_req(struct ft_sess *, struct fc_frame *);
struct ft_tpg *ft_lport_find_tpg(struct fc_lport *);

void ft_recv_write_data(struct ft_cmd *, struct fc_frame *);
void ft_dump_cmd(struct ft_cmd *, const char *caller);

ssize_t ft_format_wwn(char *, size_t, u64);

/*
 * Underlying HW specific helper function
 */
void ft_invl_hw_context(struct ft_cmd *);

#endif /* __TCM_FC_H__ */
