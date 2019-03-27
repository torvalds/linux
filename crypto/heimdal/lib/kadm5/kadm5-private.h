/* This is a generated file */
#ifndef __kadm5_private_h__
#define __kadm5_private_h__

#include <stdarg.h>

kadm5_ret_t
_kadm5_acl_check_permission (
	kadm5_server_context */*context*/,
	unsigned /*op*/,
	krb5_const_principal /*princ*/);

kadm5_ret_t
_kadm5_acl_init (kadm5_server_context */*context*/);

kadm5_ret_t
_kadm5_bump_pw_expire (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/);

krb5_error_code
_kadm5_c_get_cred_cache (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*server_name*/,
	const char */*password*/,
	krb5_prompter_fct /*prompter*/,
	const char */*keytab*/,
	krb5_ccache /*ccache*/,
	krb5_ccache */*ret_cache*/);

kadm5_ret_t
_kadm5_c_init_context (
	kadm5_client_context **/*ctx*/,
	kadm5_config_params */*params*/,
	krb5_context /*context*/);

kadm5_ret_t
_kadm5_client_recv (
	kadm5_client_context */*context*/,
	krb5_data */*reply*/);

kadm5_ret_t
_kadm5_client_send (
	kadm5_client_context */*context*/,
	krb5_storage */*sp*/);

kadm5_ret_t
_kadm5_connect (void */*handle*/);

kadm5_ret_t
_kadm5_error_code (kadm5_ret_t /*code*/);

int
_kadm5_exists_keys (
	Key */*keys1*/,
	int /*len1*/,
	Key */*keys2*/,
	int /*len2*/);

void
_kadm5_free_keys (
	krb5_context /*context*/,
	int /*len*/,
	Key */*keys*/);

void
_kadm5_init_keys (
	Key */*keys*/,
	int /*len*/);

kadm5_ret_t
_kadm5_marshal_params (
	krb5_context /*context*/,
	kadm5_config_params */*params*/,
	krb5_data */*out*/);

kadm5_ret_t
_kadm5_privs_to_string (
	uint32_t /*privs*/,
	char */*string*/,
	size_t /*len*/);

HDB *
_kadm5_s_get_db (void */*server_handle*/);

kadm5_ret_t
_kadm5_s_init_context (
	kadm5_server_context **/*ctx*/,
	kadm5_config_params */*params*/,
	krb5_context /*context*/);

kadm5_ret_t
_kadm5_set_keys (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/,
	const char */*password*/);

kadm5_ret_t
_kadm5_set_keys2 (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/,
	int16_t /*n_key_data*/,
	krb5_key_data */*key_data*/);

kadm5_ret_t
_kadm5_set_keys3 (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/,
	int /*n_keys*/,
	krb5_keyblock */*keyblocks*/);

kadm5_ret_t
_kadm5_set_keys_randomly (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/,
	krb5_keyblock **/*new_keys*/,
	int */*n_keys*/);

kadm5_ret_t
_kadm5_set_modifier (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/);

kadm5_ret_t
_kadm5_setup_entry (
	kadm5_server_context */*context*/,
	hdb_entry_ex */*ent*/,
	uint32_t /*mask*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*princ_mask*/,
	kadm5_principal_ent_t /*def*/,
	uint32_t /*def_mask*/);

kadm5_ret_t
_kadm5_string_to_privs (
	const char */*s*/,
	uint32_t* /*privs*/);

kadm5_ret_t
_kadm5_unmarshal_params (
	krb5_context /*context*/,
	krb5_data */*in*/,
	kadm5_config_params */*params*/);

kadm5_ret_t
kadm5_c_chpass_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	const char */*password*/);

kadm5_ret_t
kadm5_c_chpass_principal_with_key (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	int /*n_key_data*/,
	krb5_key_data */*key_data*/);

kadm5_ret_t
kadm5_c_create_principal (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/,
	const char */*password*/);

kadm5_ret_t
kadm5_c_delete_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/);

kadm5_ret_t
kadm5_c_destroy (void */*server_handle*/);

kadm5_ret_t
kadm5_c_flush (void */*server_handle*/);

kadm5_ret_t
kadm5_c_get_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	kadm5_principal_ent_t /*out*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_c_get_principals (
	void */*server_handle*/,
	const char */*expression*/,
	char ***/*princs*/,
	int */*count*/);

kadm5_ret_t
kadm5_c_get_privs (
	void */*server_handle*/,
	uint32_t */*privs*/);

kadm5_ret_t
kadm5_c_init_with_creds (
	const char */*client_name*/,
	krb5_ccache /*ccache*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_c_init_with_creds_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	krb5_ccache /*ccache*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_c_init_with_password (
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_c_init_with_password_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_c_init_with_skey (
	const char */*client_name*/,
	const char */*keytab*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_c_init_with_skey_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*keytab*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_c_modify_principal (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_c_randkey_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	krb5_keyblock **/*new_keys*/,
	int */*n_keys*/);

kadm5_ret_t
kadm5_c_rename_principal (
	void */*server_handle*/,
	krb5_principal /*source*/,
	krb5_principal /*target*/);

kadm5_ret_t
kadm5_log_create (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/);

kadm5_ret_t
kadm5_log_delete (
	kadm5_server_context */*context*/,
	krb5_principal /*princ*/);

kadm5_ret_t
kadm5_log_end (kadm5_server_context */*context*/);

kadm5_ret_t
kadm5_log_foreach (
	kadm5_server_context */*context*/,
	void (*/*func*/)(kadm5_server_context *server_context, uint32_t ver, time_t timestamp, enum kadm_ops op, uint32_t len, krb5_storage *, void *),
	void */*ctx*/);

kadm5_ret_t
kadm5_log_get_version (
	kadm5_server_context */*context*/,
	uint32_t */*ver*/);

kadm5_ret_t
kadm5_log_get_version_fd (
	int /*fd*/,
	uint32_t */*ver*/);

krb5_storage *
kadm5_log_goto_end (int /*fd*/);

kadm5_ret_t
kadm5_log_init (kadm5_server_context */*context*/);

kadm5_ret_t
kadm5_log_modify (
	kadm5_server_context */*context*/,
	hdb_entry */*ent*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_log_nop (kadm5_server_context */*context*/);

kadm5_ret_t
kadm5_log_previous (
	krb5_context /*context*/,
	krb5_storage */*sp*/,
	uint32_t */*ver*/,
	time_t */*timestamp*/,
	enum kadm_ops */*op*/,
	uint32_t */*len*/);

kadm5_ret_t
kadm5_log_reinit (kadm5_server_context */*context*/);

kadm5_ret_t
kadm5_log_rename (
	kadm5_server_context */*context*/,
	krb5_principal /*source*/,
	hdb_entry */*ent*/);

kadm5_ret_t
kadm5_log_replay (
	kadm5_server_context */*context*/,
	enum kadm_ops /*op*/,
	uint32_t /*ver*/,
	uint32_t /*len*/,
	krb5_storage */*sp*/);

kadm5_ret_t
kadm5_log_set_version (
	kadm5_server_context */*context*/,
	uint32_t /*vno*/);

const char *
kadm5_log_signal_socket (krb5_context /*context*/);

kadm5_ret_t
kadm5_log_signal_socket_info (
	krb5_context /*context*/,
	int /*server_end*/,
	struct addrinfo **/*ret_addrs*/);

kadm5_ret_t
kadm5_log_truncate (kadm5_server_context */*server_context*/);

kadm5_ret_t
kadm5_s_chpass_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	const char */*password*/);

kadm5_ret_t
kadm5_s_chpass_principal_cond (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	const char */*password*/);

kadm5_ret_t
kadm5_s_chpass_principal_with_key (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	int /*n_key_data*/,
	krb5_key_data */*key_data*/);

kadm5_ret_t
kadm5_s_create_principal (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/,
	const char */*password*/);

kadm5_ret_t
kadm5_s_create_principal_with_key (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_s_delete_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/);

kadm5_ret_t
kadm5_s_destroy (void */*server_handle*/);

kadm5_ret_t
kadm5_s_flush (void */*server_handle*/);

kadm5_ret_t
kadm5_s_get_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	kadm5_principal_ent_t /*out*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_s_get_principals (
	void */*server_handle*/,
	const char */*expression*/,
	char ***/*princs*/,
	int */*count*/);

kadm5_ret_t
kadm5_s_get_privs (
	void */*server_handle*/,
	uint32_t */*privs*/);

kadm5_ret_t
kadm5_s_init_with_creds (
	const char */*client_name*/,
	krb5_ccache /*ccache*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_s_init_with_creds_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	krb5_ccache /*ccache*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_s_init_with_password (
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_s_init_with_password_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_s_init_with_skey (
	const char */*client_name*/,
	const char */*keytab*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_s_init_with_skey_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*keytab*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_s_modify_principal (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_s_randkey_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	krb5_keyblock **/*new_keys*/,
	int */*n_keys*/);

kadm5_ret_t
kadm5_s_rename_principal (
	void */*server_handle*/,
	krb5_principal /*source*/,
	krb5_principal /*target*/);

#endif /* __kadm5_private_h__ */
