#ifndef ISCSI_TARGET_NEGO_H
#define ISCSI_TARGET_NEGO_H

#define DECIMAL         0
#define HEX             1

extern void convert_null_to_semi(char *, int);
extern int extract_param(const char *, const char *, unsigned int, char *,
		unsigned char *);
extern int iscsi_target_check_login_request(struct iscsi_conn *,
		struct iscsi_login *);
extern int iscsi_target_get_initial_payload(struct iscsi_conn *,
		struct iscsi_login *);
extern int iscsi_target_locate_portal(struct iscsi_np *, struct iscsi_conn *,
		struct iscsi_login *);
extern int iscsi_target_start_negotiation(
		struct iscsi_login *, struct iscsi_conn *);
extern void iscsi_target_nego_release(struct iscsi_conn *);

#endif /* ISCSI_TARGET_NEGO_H */
