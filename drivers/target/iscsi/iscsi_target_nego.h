/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_TARGET_NEGO_H
#define ISCSI_TARGET_NEGO_H

#define DECIMAL         0
#define HEX             1
#define BASE64          2

struct iscsit_conn;
struct iscsi_login;
struct iscsi_np;

extern void convert_null_to_semi(char *, int);
extern int extract_param(const char *, const char *, unsigned int, char *,
		unsigned char *);
extern int iscsi_target_check_login_request(struct iscsit_conn *,
		struct iscsi_login *);
extern int iscsi_target_locate_portal(struct iscsi_np *, struct iscsit_conn *,
		struct iscsi_login *);
extern int iscsi_target_start_negotiation(
		struct iscsi_login *, struct iscsit_conn *);
extern void iscsi_target_nego_release(struct iscsit_conn *);
extern bool iscsi_conn_auth_required(struct iscsit_conn *conn);
#endif /* ISCSI_TARGET_NEGO_H */
