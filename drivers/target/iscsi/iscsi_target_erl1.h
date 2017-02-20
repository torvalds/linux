#ifndef ISCSI_TARGET_ERL1_H
#define ISCSI_TARGET_ERL1_H

#include <linux/types.h>
#include <scsi/iscsi_proto.h> /* itt_t */

struct iscsi_cmd;
struct iscsi_conn;
struct iscsi_datain_req;
struct iscsi_ooo_cmdsn;
struct iscsi_pdu;
struct iscsi_session;

extern int iscsit_dump_data_payload(struct iscsi_conn *, u32, int);
extern int iscsit_create_recovery_datain_values_datasequenceinorder_yes(
			struct iscsi_cmd *, struct iscsi_datain_req *);
extern int iscsit_create_recovery_datain_values_datasequenceinorder_no(
			struct iscsi_cmd *, struct iscsi_datain_req *);
extern int iscsit_handle_recovery_datain_or_r2t(struct iscsi_conn *, unsigned char *,
			itt_t, u32, u32, u32);
extern int iscsit_handle_status_snack(struct iscsi_conn *, itt_t, u32,
			u32, u32);
extern int iscsit_handle_data_ack(struct iscsi_conn *, u32, u32, u32);
extern int iscsit_dataout_datapduinorder_no_fbit(struct iscsi_cmd *, struct iscsi_pdu *);
extern int iscsit_recover_dataout_sequence(struct iscsi_cmd *, u32, u32);
extern void iscsit_clear_ooo_cmdsns_for_conn(struct iscsi_conn *);
extern void iscsit_free_all_ooo_cmdsns(struct iscsi_session *);
extern int iscsit_execute_ooo_cmdsns(struct iscsi_session *);
extern int iscsit_execute_cmd(struct iscsi_cmd *, int);
extern int iscsit_handle_ooo_cmdsn(struct iscsi_session *, struct iscsi_cmd *, u32);
extern void iscsit_remove_ooo_cmdsn(struct iscsi_session *, struct iscsi_ooo_cmdsn *);
extern void iscsit_mod_dataout_timer(struct iscsi_cmd *);
extern void iscsit_start_dataout_timer(struct iscsi_cmd *, struct iscsi_conn *);
extern void iscsit_stop_dataout_timer(struct iscsi_cmd *);

#endif /* ISCSI_TARGET_ERL1_H */
