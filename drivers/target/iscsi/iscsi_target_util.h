/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_TARGET_UTIL_H
#define ISCSI_TARGET_UTIL_H

#include <linux/types.h>
#include <scsi/iscsi_proto.h>        /* itt_t */

#define MARKER_SIZE	8

struct iscsit_cmd;
struct iscsit_conn;
struct iscsi_conn_recovery;
struct iscsit_session;

extern int iscsit_add_r2t_to_list(struct iscsit_cmd *, u32, u32, int, u32);
extern struct iscsi_r2t *iscsit_get_r2t_for_eos(struct iscsit_cmd *, u32, u32);
extern struct iscsi_r2t *iscsit_get_r2t_from_list(struct iscsit_cmd *);
extern void iscsit_free_r2t(struct iscsi_r2t *, struct iscsit_cmd *);
extern void iscsit_free_r2ts_from_list(struct iscsit_cmd *);
extern struct iscsit_cmd *iscsit_alloc_cmd(struct iscsit_conn *, gfp_t);
extern struct iscsit_cmd *iscsit_allocate_cmd(struct iscsit_conn *, int);
extern struct iscsi_seq *iscsit_get_seq_holder_for_datain(struct iscsit_cmd *, u32);
extern struct iscsi_seq *iscsit_get_seq_holder_for_r2t(struct iscsit_cmd *);
extern struct iscsi_r2t *iscsit_get_holder_for_r2tsn(struct iscsit_cmd *, u32);
extern int iscsit_sequence_cmd(struct iscsit_conn *conn, struct iscsit_cmd *cmd,
			       unsigned char * ,__be32 cmdsn);
extern int iscsit_check_unsolicited_dataout(struct iscsit_cmd *, unsigned char *);
extern struct iscsit_cmd *iscsit_find_cmd_from_itt_or_dump(struct iscsit_conn *,
			itt_t, u32);
extern struct iscsit_cmd *iscsit_find_cmd_from_ttt(struct iscsit_conn *, u32);
extern int iscsit_find_cmd_for_recovery(struct iscsit_session *, struct iscsit_cmd **,
			struct iscsi_conn_recovery **, itt_t);
extern void iscsit_add_cmd_to_immediate_queue(struct iscsit_cmd *, struct iscsit_conn *, u8);
extern struct iscsi_queue_req *iscsit_get_cmd_from_immediate_queue(struct iscsit_conn *);
extern int iscsit_add_cmd_to_response_queue(struct iscsit_cmd *, struct iscsit_conn *, u8);
extern struct iscsi_queue_req *iscsit_get_cmd_from_response_queue(struct iscsit_conn *);
extern void iscsit_remove_cmd_from_tx_queues(struct iscsit_cmd *, struct iscsit_conn *);
extern bool iscsit_conn_all_queues_empty(struct iscsit_conn *);
extern void iscsit_free_queue_reqs_for_conn(struct iscsit_conn *);
extern void iscsit_release_cmd(struct iscsit_cmd *);
extern void __iscsit_free_cmd(struct iscsit_cmd *, bool);
extern void iscsit_free_cmd(struct iscsit_cmd *, bool);
extern bool iscsit_check_session_usage_count(struct iscsit_session *sess, bool can_sleep);
extern void iscsit_dec_session_usage_count(struct iscsit_session *);
extern void iscsit_inc_session_usage_count(struct iscsit_session *);
extern struct iscsit_conn *iscsit_get_conn_from_cid(struct iscsit_session *, u16);
extern struct iscsit_conn *iscsit_get_conn_from_cid_rcfr(struct iscsit_session *, u16);
extern void iscsit_check_conn_usage_count(struct iscsit_conn *);
extern void iscsit_dec_conn_usage_count(struct iscsit_conn *);
extern void iscsit_inc_conn_usage_count(struct iscsit_conn *);
extern void iscsit_handle_nopin_response_timeout(struct timer_list *t);
extern void iscsit_mod_nopin_response_timer(struct iscsit_conn *);
extern void iscsit_start_nopin_response_timer(struct iscsit_conn *);
extern void iscsit_stop_nopin_response_timer(struct iscsit_conn *);
extern void iscsit_handle_nopin_timeout(struct timer_list *t);
extern void __iscsit_start_nopin_timer(struct iscsit_conn *);
extern void iscsit_start_nopin_timer(struct iscsit_conn *);
extern void iscsit_stop_nopin_timer(struct iscsit_conn *);
extern int iscsit_send_tx_data(struct iscsit_cmd *, struct iscsit_conn *, int);
extern int iscsit_fe_sendpage_sg(struct iscsit_cmd *, struct iscsit_conn *);
extern int iscsit_tx_login_rsp(struct iscsit_conn *, u8, u8);
extern void iscsit_print_session_params(struct iscsit_session *);
extern int iscsit_print_dev_to_proc(char *, char **, off_t, int);
extern int iscsit_print_sessions_to_proc(char *, char **, off_t, int);
extern int iscsit_print_tpg_to_proc(char *, char **, off_t, int);
extern int rx_data(struct iscsit_conn *, struct kvec *, int, int);
extern int tx_data(struct iscsit_conn *, struct kvec *, int, int);
extern void iscsit_collect_login_stats(struct iscsit_conn *, u8, u8);
extern struct iscsi_tiqn *iscsit_snmp_get_tiqn(struct iscsit_conn *);
extern void iscsit_fill_cxn_timeout_err_stats(struct iscsit_session *);

#endif /*** ISCSI_TARGET_UTIL_H ***/
