#ifndef ISCSI_TARGET_H
#define ISCSI_TARGET_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct iscsi_cmd;
struct iscsi_conn;
struct iscsi_np;
struct iscsi_portal_group;
struct iscsi_session;
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
extern int iscsit_reject_cmd(struct iscsi_cmd *cmd, u8, unsigned char *);
extern void iscsit_set_unsoliticed_dataout(struct iscsi_cmd *);
extern int iscsit_logout_closesession(struct iscsi_cmd *, struct iscsi_conn *);
extern int iscsit_logout_closeconnection(struct iscsi_cmd *, struct iscsi_conn *);
extern int iscsit_logout_removeconnforrecovery(struct iscsi_cmd *, struct iscsi_conn *);
extern int iscsit_send_async_msg(struct iscsi_conn *, u16, u8, u8);
extern int iscsit_build_r2ts_for_cmd(struct iscsi_conn *, struct iscsi_cmd *, bool recovery);
extern void iscsit_thread_get_cpumask(struct iscsi_conn *);
extern int iscsi_target_tx_thread(void *);
extern int iscsi_target_rx_thread(void *);
extern int iscsit_close_connection(struct iscsi_conn *);
extern int iscsit_close_session(struct iscsi_session *);
extern void iscsit_fail_session(struct iscsi_session *);
extern int iscsit_free_session(struct iscsi_session *);
extern void iscsit_stop_session(struct iscsi_session *, int, int);
extern int iscsit_release_sessions_for_tpg(struct iscsi_portal_group *, int);

extern struct iscsit_global *iscsit_global;
extern const struct target_core_fabric_ops iscsi_ops;

extern struct kmem_cache *lio_dr_cache;
extern struct kmem_cache *lio_ooo_cache;
extern struct kmem_cache *lio_qr_cache;
extern struct kmem_cache *lio_r2t_cache;

extern struct idr sess_idr;
extern struct mutex auth_id_lock;
extern spinlock_t sess_idr_lock;


#endif   /*** ISCSI_TARGET_H ***/
