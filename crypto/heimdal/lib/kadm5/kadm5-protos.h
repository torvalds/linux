/* This is a generated file */
#ifndef __kadm5_protos_h__
#define __kadm5_protos_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

kadm5_ret_t
kadm5_ad_init_with_password (
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_ad_init_with_password_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

krb5_error_code
kadm5_add_passwd_quality_verifier (
	krb5_context /*context*/,
	const char */*check_library*/);

const char *
kadm5_check_password_quality (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	krb5_data */*pwd_data*/);

kadm5_ret_t
kadm5_chpass_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	const char */*password*/);

kadm5_ret_t
kadm5_chpass_principal_with_key (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	int /*n_key_data*/,
	krb5_key_data */*key_data*/);

kadm5_ret_t
kadm5_create_principal (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/,
	const char */*password*/);

kadm5_ret_t
kadm5_delete_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/);

kadm5_ret_t
kadm5_destroy (void */*server_handle*/);

kadm5_ret_t
kadm5_flush (void */*server_handle*/);

void
kadm5_free_key_data (
	void */*server_handle*/,
	int16_t */*n_key_data*/,
	krb5_key_data */*key_data*/);

void
kadm5_free_name_list (
	void */*server_handle*/,
	char **/*names*/,
	int */*count*/);

void
kadm5_free_principal_ent (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/);

kadm5_ret_t
kadm5_get_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	kadm5_principal_ent_t /*out*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_get_principals (
	void */*server_handle*/,
	const char */*expression*/,
	char ***/*princs*/,
	int */*count*/);

kadm5_ret_t
kadm5_get_privs (
	void */*server_handle*/,
	uint32_t */*privs*/);

kadm5_ret_t
kadm5_init_with_creds (
	const char */*client_name*/,
	krb5_ccache /*ccache*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_init_with_creds_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	krb5_ccache /*ccache*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_init_with_password (
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_init_with_password_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*password*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_init_with_skey (
	const char */*client_name*/,
	const char */*keytab*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_init_with_skey_ctx (
	krb5_context /*context*/,
	const char */*client_name*/,
	const char */*keytab*/,
	const char */*service_name*/,
	kadm5_config_params */*realm_params*/,
	unsigned long /*struct_version*/,
	unsigned long /*api_version*/,
	void **/*server_handle*/);

kadm5_ret_t
kadm5_modify_principal (
	void */*server_handle*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_randkey_principal (
	void */*server_handle*/,
	krb5_principal /*princ*/,
	krb5_keyblock **/*new_keys*/,
	int */*n_keys*/);

kadm5_ret_t
kadm5_rename_principal (
	void */*server_handle*/,
	krb5_principal /*source*/,
	krb5_principal /*target*/);

kadm5_ret_t
kadm5_ret_key_data (
	krb5_storage */*sp*/,
	krb5_key_data */*key*/);

kadm5_ret_t
kadm5_ret_principal_ent (
	krb5_storage */*sp*/,
	kadm5_principal_ent_t /*princ*/);

kadm5_ret_t
kadm5_ret_principal_ent_mask (
	krb5_storage */*sp*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t */*mask*/);

kadm5_ret_t
kadm5_ret_tl_data (
	krb5_storage */*sp*/,
	krb5_tl_data */*tl*/);

void
kadm5_setup_passwd_quality_check (
	krb5_context /*context*/,
	const char */*check_library*/,
	const char */*check_function*/);

kadm5_ret_t
kadm5_store_key_data (
	krb5_storage */*sp*/,
	krb5_key_data */*key*/);

kadm5_ret_t
kadm5_store_principal_ent (
	krb5_storage */*sp*/,
	kadm5_principal_ent_t /*princ*/);

kadm5_ret_t
kadm5_store_principal_ent_mask (
	krb5_storage */*sp*/,
	kadm5_principal_ent_t /*princ*/,
	uint32_t /*mask*/);

kadm5_ret_t
kadm5_store_tl_data (
	krb5_storage */*sp*/,
	krb5_tl_data */*tl*/);

#ifdef __cplusplus
}
#endif

#endif /* __kadm5_protos_h__ */
