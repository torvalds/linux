#ifndef ISCSI_TARGET_UTIL_H
#define ISCSI_TARGET_UTIL_H

#define MARKER_SIZE	8

extern int iscsit_add_r2t_to_list(struct iscsi_cmd *, u32, u32, int, u32);
extern struct iscsi_r2t *iscsit_get_r2t_for_eos(struct iscsi_cmd *, u32, u32);
extern struct iscsi_r2t *iscsit_get_r2t_from_list(struct iscsi_cmd *);
extern void iscsit_free_r2t(struct iscsi_r2t *, struct iscsi_cmd *);
extern void iscsit_free_r2ts_from_list(struct iscsi_cmd *);
extern struct iscsi_cmd *iscsit_alloc_cmd(struct iscsi_conn *, gfp_t);
extern struct iscsi_cmd *iscsit_allocate_cmd(struct iscsi_conn *, int);
extern struct iscsi_seq *iscsit_get_seq_holder_for_datain(struct iscsi_cmd *, u32);
extern struct iscsi_seq *iscsit_get_seq_holder_for_r2t(struct iscsi_cmd *);
extern struct iscsi_r2t *iscsit_get_holder_for_r2tsn(struct iscsi_cmd *, u32);
extern int iscsit_sequence_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			       unsigned char * ,__be32 cmdsn);
extern int iscsit_check_unsolicited_dataout(struct iscsi_cmd *, unsigned char *);
extern struct iscsi_cmd *iscsit_find_cmd_from_itt(struct iscsi_conn *, itt_t);
extern struct iscsi_cmd *iscsit_find_cmd_from_itt_or_dump(struct iscsi_conn *,
			itt_t, u32);
extern struct iscsi_cmd *iscsit_find_cmd_from_ttt(struct iscsi_conn *, u32);
extern int iscsit_find_cmd_for_recovery(struct iscsi_session *, struct iscsi_cmd **,
			struct iscsi_conn_recovery **, itt_t);
extern void iscsit_add_cmd_to_immediate_queue(struct iscsi_cmd *, struct iscsi_conn *, u8);
extern struct iscsi_queue_req *iscsit_get_cmd_from_immediate_queue(struct iscsi_conn *);
extern void iscsit_add_cmd_to_response_queue(struct iscsi_cmd *, struct iscsi_conn *, u8);
extern struct iscsi_queue_req *iscsit_get_cmd_from_response_queue(struct iscsi_conn *);
extern void iscsit_remove_cmd_from_tx_queues(struct iscsi_cmd *, struct iscsi_conn *);
extern bool iscsit_conn_all_queues_empty(struct iscsi_conn *);
extern void iscsit_free_queue_reqs_for_conn(struct iscsi_conn *);
extern void iscsit_release_cmd(struct iscsi_cmd *);
extern void __iscsit_free_cmd(struct iscsi_cmd *, bool, bool);
extern void iscsit_free_cmd(struct iscsi_cmd *, bool);
extern int iscsit_check_session_usage_count(struct iscsi_session *);
extern void iscsit_dec_session_usage_count(struct iscsi_session *);
extern void iscsit_inc_session_usage_count(struct iscsi_session *);
extern int iscsit_set_sync_and_steering_values(struct iscsi_conn *);
extern struct iscsi_conn *iscsit_get_conn_from_cid(struct iscsi_session *, u16);
extern struct iscsi_conn *iscsit_get_conn_from_cid_rcfr(struct iscsi_session *, u16);
extern void iscsit_check_conn_usage_count(struct iscsi_conn *);
extern void iscsit_dec_conn_usage_count(struct iscsi_conn *);
extern void iscsit_inc_conn_usage_count(struct iscsi_conn *);
extern void iscsit_mod_nopin_response_timer(struct iscsi_conn *);
extern void iscsit_start_nopin_response_timer(struct iscsi_conn *);
extern void iscsit_stop_nopin_response_timer(struct iscsi_conn *);
extern void __iscsit_start_nopin_timer(struct iscsi_conn *);
extern void iscsit_start_nopin_timer(struct iscsi_conn *);
extern void iscsit_stop_nopin_timer(struct iscsi_conn *);
extern int iscsit_send_tx_data(struct iscsi_cmd *, struct iscsi_conn *, int);
extern int iscsit_fe_sendpage_sg(struct iscsi_cmd *, struct iscsi_conn *);
extern int iscsit_tx_login_rsp(struct iscsi_conn *, u8, u8);
extern void iscsit_print_session_params(struct iscsi_session *);
extern int iscsit_print_dev_to_proc(char *, char **, off_t, int);
extern int iscsit_print_sessions_to_proc(char *, char **, off_t, int);
extern int iscsit_print_tpg_to_proc(char *, char **, off_t, int);
extern int rx_data(struct iscsi_conn *, struct kvec *, int, int);
extern int tx_data(struct iscsi_conn *, struct kvec *, int, int);
extern void iscsit_collect_login_stats(struct iscsi_conn *, u8, u8);
extern struct iscsi_tiqn *iscsit_snmp_get_tiqn(struct iscsi_conn *);

#endif /*** ISCSI_TARGET_UTIL_H ***/
