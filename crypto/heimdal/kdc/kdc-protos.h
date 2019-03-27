/* This is a generated file */
#ifndef __kdc_protos_h__
#define __kdc_protos_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

krb5_error_code
kdc_check_flags (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/,
	hdb_entry_ex */*client_ex*/,
	const char */*client_name*/,
	hdb_entry_ex */*server_ex*/,
	const char */*server_name*/,
	krb5_boolean /*is_as_req*/);

void
kdc_log (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/,
	int /*level*/,
	const char */*fmt*/,
	...);

char*
kdc_log_msg (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/,
	int /*level*/,
	const char */*fmt*/,
	...);

char*
kdc_log_msg_va (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/,
	int /*level*/,
	const char */*fmt*/,
	va_list /*ap*/);

void
kdc_openlog (
	krb5_context /*context*/,
	const char */*service*/,
	krb5_kdc_configuration */*config*/);

krb5_error_code
krb5_kdc_get_config (
	krb5_context /*context*/,
	krb5_kdc_configuration **/*config*/);

krb5_error_code
krb5_kdc_pk_initialize (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/,
	const char */*user_id*/,
	const char */*anchors*/,
	char **/*pool*/,
	char **/*revoke_list*/);

krb5_error_code
krb5_kdc_pkinit_config (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/);

int
krb5_kdc_process_krb5_request (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/,
	unsigned char */*buf*/,
	size_t /*len*/,
	krb5_data */*reply*/,
	const char */*from*/,
	struct sockaddr */*addr*/,
	int /*datagram_reply*/);

int
krb5_kdc_process_request (
	krb5_context /*context*/,
	krb5_kdc_configuration */*config*/,
	unsigned char */*buf*/,
	size_t /*len*/,
	krb5_data */*reply*/,
	krb5_boolean */*prependlength*/,
	const char */*from*/,
	struct sockaddr */*addr*/,
	int /*datagram_reply*/);

int
krb5_kdc_save_request (
	krb5_context /*context*/,
	const char */*fn*/,
	const unsigned char */*buf*/,
	size_t /*len*/,
	const krb5_data */*reply*/,
	const struct sockaddr */*sa*/);

krb5_error_code
krb5_kdc_set_dbinfo (
	krb5_context /*context*/,
	struct krb5_kdc_configuration */*c*/);

void
krb5_kdc_update_time (struct timeval */*tv*/);

krb5_error_code
krb5_kdc_windc_init (krb5_context /*context*/);

#ifdef __cplusplus
}
#endif

#endif /* __kdc_protos_h__ */
