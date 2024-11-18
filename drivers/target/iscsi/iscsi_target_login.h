/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_TARGET_LOGIN_H
#define ISCSI_TARGET_LOGIN_H

#include <linux/types.h>

struct iscsit_conn;
struct iscsi_login;
struct iscsi_np;
struct sockaddr_storage;

extern int iscsi_login_setup_crypto(struct iscsit_conn *);
extern int iscsi_check_for_session_reinstatement(struct iscsit_conn *);
extern int iscsi_login_post_auth_non_zero_tsih(struct iscsit_conn *, u16, u32);
extern int iscsit_setup_np(struct iscsi_np *,
				struct sockaddr_storage *);
extern int iscsi_target_setup_login_socket(struct iscsi_np *,
				struct sockaddr_storage *);
extern int iscsit_accept_np(struct iscsi_np *, struct iscsit_conn *);
extern int iscsit_get_login_rx(struct iscsit_conn *, struct iscsi_login *);
extern int iscsit_put_login_tx(struct iscsit_conn *, struct iscsi_login *, u32);
extern void iscsit_free_conn(struct iscsit_conn *);
extern int iscsit_start_kthreads(struct iscsit_conn *);
extern void iscsi_post_login_handler(struct iscsi_np *, struct iscsit_conn *, u8);
extern void iscsi_target_login_sess_out(struct iscsit_conn *, bool, bool);
extern int iscsi_target_login_thread(void *);

#endif   /*** ISCSI_TARGET_LOGIN_H ***/
