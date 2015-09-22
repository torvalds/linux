#ifndef ISCSI_TARGET_ERL0_H
#define ISCSI_TARGET_ERL0_H

extern void iscsit_set_dataout_sequence_values(struct iscsi_cmd *);
extern int iscsit_check_pre_dataout(struct iscsi_cmd *, unsigned char *);
extern int iscsit_check_post_dataout(struct iscsi_cmd *, unsigned char *, u8);
extern void iscsit_start_time2retain_handler(struct iscsi_session *);
extern int iscsit_stop_time2retain_timer(struct iscsi_session *);
extern void iscsit_connection_reinstatement_rcfr(struct iscsi_conn *);
extern void iscsit_cause_connection_reinstatement(struct iscsi_conn *, int);
extern void iscsit_fall_back_to_erl0(struct iscsi_session *);
extern void iscsit_take_action_for_connection_exit(struct iscsi_conn *);

#endif   /*** ISCSI_TARGET_ERL0_H ***/
