/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_TARGET_H
#define ISCSI_TARGET_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct iscsit_cmd;
struct iscsit_conn;
struct iscsi_np;
struct iscsi_portal_group;
struct iscsit_session;
struct iscsi_tpg_np;
struct kref;
struct sockaddr_storage;

extern struct iscsi_tiqn *iscsit_get_tiqn_for_login(unsigned char *);
extern struct iscsi_tiqn *iscsit_get_tiqn(unsigned char *, int);
extern void iscsit_put_tiqn_for_login(struct iscsi_tiqn *);
extern struct iscsi_tiqn *iscsit_add_tiqn(unsigned char *);
extern void iscsit_del_tiqn(struct iscsi_tiqn *);
extern int iscsit_access_np(struct iscsi_np *, struct iscsi_portal_group *);
extern void iscsit_login_kref_put(struct kref *);
extern int iscsit_deaccess_np(struct iscsi_np *, struct iscsi_portal_group *,
				struct iscsi_tpg_np *);
extern bool iscsit_check_np_match(struct sockaddr_storage *,
				struct iscsi_np *, int);
extern struct iscsi_np *iscsit_add_np(struct sockaddr_storage *,
				int);
extern int iscsit_reset_np_thread(struct iscsi_np *, struct iscsi_tpg_np *,
				struct iscsi_portal_group *, bool);
extern int iscsit_del_np(struct iscsi_np *);
extern int iscsit_reject_cmd(struct iscsit_cmd *cmd, u8, unsigned char *);
extern void iscsit_set_unsolicited_dataout(struct iscsit_cmd *);
extern int iscsit_logout_closesession(struct iscsit_cmd *, struct iscsit_conn *);
extern int iscsit_logout_closeconnection(struct iscsit_cmd *, struct iscsit_conn *);
extern int iscsit_logout_removeconnforrecovery(struct iscsit_cmd *, struct iscsit_conn *);
extern int iscsit_send_async_msg(struct iscsit_conn *, u16, u8, u8);
extern int iscsit_build_r2ts_for_cmd(struct iscsit_conn *, struct iscsit_cmd *, bool recovery);
extern void iscsit_thread_get_cpumask(struct iscsit_conn *);
extern int iscsi_target_tx_thread(void *);
extern int iscsi_target_rx_thread(void *);
extern int iscsit_close_connection(struct iscsit_conn *);
extern int iscsit_close_session(struct iscsit_session *, bool can_sleep);
extern void iscsit_fail_session(struct iscsit_session *);
extern void iscsit_stop_session(struct iscsit_session *, int, int);
extern int iscsit_release_sessions_for_tpg(struct iscsi_portal_group *, int);

extern struct iscsit_global *iscsit_global;
extern const struct target_core_fabric_ops iscsi_ops;

extern struct kmem_cache *lio_dr_cache;
extern struct kmem_cache *lio_ooo_cache;
extern struct kmem_cache *lio_qr_cache;
extern struct kmem_cache *lio_r2t_cache;

extern struct ida sess_ida;
extern struct mutex auth_id_lock;

#endif   /*** ISCSI_TARGET_H ***/
