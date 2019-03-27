/* This is a generated file */
#ifndef __hdb_protos_h__
#define __hdb_protos_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

krb5_error_code
hdb_add_master_key (
	krb5_context /*context*/,
	krb5_keyblock */*key*/,
	hdb_master_key */*inout*/);

krb5_error_code
hdb_check_db_format (
	krb5_context /*context*/,
	HDB */*db*/);

krb5_error_code
hdb_clear_extension (
	krb5_context /*context*/,
	hdb_entry */*entry*/,
	int /*type*/);

krb5_error_code
hdb_clear_master_key (
	krb5_context /*context*/,
	HDB */*db*/);

krb5_error_code
hdb_create (
	krb5_context /*context*/,
	HDB **/*db*/,
	const char */*filename*/);

krb5_error_code
hdb_db_create (
	krb5_context /*context*/,
	HDB **/*db*/,
	const char */*filename*/);

const char *
hdb_db_dir (krb5_context /*context*/);

const char *
hdb_dbinfo_get_acl_file (
	krb5_context /*context*/,
	struct hdb_dbinfo */*dbp*/);

const krb5_config_binding *
hdb_dbinfo_get_binding (
	krb5_context /*context*/,
	struct hdb_dbinfo */*dbp*/);

const char *
hdb_dbinfo_get_dbname (
	krb5_context /*context*/,
	struct hdb_dbinfo */*dbp*/);

const char *
hdb_dbinfo_get_label (
	krb5_context /*context*/,
	struct hdb_dbinfo */*dbp*/);

const char *
hdb_dbinfo_get_log_file (
	krb5_context /*context*/,
	struct hdb_dbinfo */*dbp*/);

const char *
hdb_dbinfo_get_mkey_file (
	krb5_context /*context*/,
	struct hdb_dbinfo */*dbp*/);

struct hdb_dbinfo *
hdb_dbinfo_get_next (
	struct hdb_dbinfo */*dbp*/,
	struct hdb_dbinfo */*dbprevp*/);

const char *
hdb_dbinfo_get_realm (
	krb5_context /*context*/,
	struct hdb_dbinfo */*dbp*/);

const char *
hdb_default_db (krb5_context /*context*/);

krb5_error_code
hdb_enctype2key (
	krb5_context /*context*/,
	hdb_entry */*e*/,
	krb5_enctype /*enctype*/,
	Key **/*key*/);

krb5_error_code
hdb_entry2string (
	krb5_context /*context*/,
	hdb_entry */*ent*/,
	char **/*str*/);

int
hdb_entry2value (
	krb5_context /*context*/,
	const hdb_entry */*ent*/,
	krb5_data */*value*/);

int
hdb_entry_alias2value (
	krb5_context /*context*/,
	const hdb_entry_alias */*alias*/,
	krb5_data */*value*/);

krb5_error_code
hdb_entry_check_mandatory (
	krb5_context /*context*/,
	const hdb_entry */*ent*/);

int
hdb_entry_clear_password (
	krb5_context /*context*/,
	hdb_entry */*entry*/);

krb5_error_code
hdb_entry_get_ConstrainedDelegACL (
	const hdb_entry */*entry*/,
	const HDB_Ext_Constrained_delegation_acl **/*a*/);

krb5_error_code
hdb_entry_get_aliases (
	const hdb_entry */*entry*/,
	const HDB_Ext_Aliases **/*a*/);

int
hdb_entry_get_password (
	krb5_context /*context*/,
	HDB */*db*/,
	const hdb_entry */*entry*/,
	char **/*p*/);

krb5_error_code
hdb_entry_get_pkinit_acl (
	const hdb_entry */*entry*/,
	const HDB_Ext_PKINIT_acl **/*a*/);

krb5_error_code
hdb_entry_get_pkinit_cert (
	const hdb_entry */*entry*/,
	const HDB_Ext_PKINIT_cert **/*a*/);

krb5_error_code
hdb_entry_get_pkinit_hash (
	const hdb_entry */*entry*/,
	const HDB_Ext_PKINIT_hash **/*a*/);

krb5_error_code
hdb_entry_get_pw_change_time (
	const hdb_entry */*entry*/,
	time_t */*t*/);

int
hdb_entry_set_password (
	krb5_context /*context*/,
	HDB */*db*/,
	hdb_entry */*entry*/,
	const char */*p*/);

krb5_error_code
hdb_entry_set_pw_change_time (
	krb5_context /*context*/,
	hdb_entry */*entry*/,
	time_t /*t*/);

HDB_extension *
hdb_find_extension (
	const hdb_entry */*entry*/,
	int /*type*/);

krb5_error_code
hdb_foreach (
	krb5_context /*context*/,
	HDB */*db*/,
	unsigned /*flags*/,
	hdb_foreach_func_t /*func*/,
	void */*data*/);

void
hdb_free_dbinfo (
	krb5_context /*context*/,
	struct hdb_dbinfo **/*dbp*/);

void
hdb_free_entry (
	krb5_context /*context*/,
	hdb_entry_ex */*ent*/);

void
hdb_free_key (Key */*key*/);

void
hdb_free_keys (
	krb5_context /*context*/,
	int /*len*/,
	Key */*keys*/);

void
hdb_free_master_key (
	krb5_context /*context*/,
	hdb_master_key /*mkey*/);

krb5_error_code
hdb_generate_key_set (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	Key **/*ret_key_set*/,
	size_t */*nkeyset*/,
	int /*no_salt*/);

krb5_error_code
hdb_generate_key_set_password (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	const char */*password*/,
	Key **/*keys*/,
	size_t */*num_keys*/);

int
hdb_get_dbinfo (
	krb5_context /*context*/,
	struct hdb_dbinfo **/*dbp*/);

krb5_error_code
hdb_init_db (
	krb5_context /*context*/,
	HDB */*db*/);

int
hdb_key2principal (
	krb5_context /*context*/,
	krb5_data */*key*/,
	krb5_principal /*p*/);

krb5_error_code
hdb_keytab_create (
	krb5_context /*context*/,
	HDB ** /*db*/,
	const char */*arg*/);

krb5_error_code
hdb_ldap_create (
	krb5_context /*context*/,
	HDB ** /*db*/,
	const char */*arg*/);

krb5_error_code
hdb_ldapi_create (
	krb5_context /*context*/,
	HDB ** /*db*/,
	const char */*arg*/);

krb5_error_code
hdb_list_builtin (
	krb5_context /*context*/,
	char **/*list*/);

krb5_error_code
hdb_lock (
	int /*fd*/,
	int /*operation*/);

krb5_error_code
hdb_mdb_create (
	krb5_context /*context*/,
	HDB **/*db*/,
	const char */*filename*/);

krb5_error_code
hdb_ndbm_create (
	krb5_context /*context*/,
	HDB **/*db*/,
	const char */*filename*/);

krb5_error_code
hdb_next_enctype2key (
	krb5_context /*context*/,
	const hdb_entry */*e*/,
	krb5_enctype /*enctype*/,
	Key **/*key*/);

int
hdb_principal2key (
	krb5_context /*context*/,
	krb5_const_principal /*p*/,
	krb5_data */*key*/);

krb5_error_code
hdb_print_entry (
	krb5_context /*context*/,
	HDB */*db*/,
	hdb_entry_ex */*entry*/,
	void */*data*/);

krb5_error_code
hdb_process_master_key (
	krb5_context /*context*/,
	int /*kvno*/,
	krb5_keyblock */*key*/,
	krb5_enctype /*etype*/,
	hdb_master_key */*mkey*/);

krb5_error_code
hdb_read_master_key (
	krb5_context /*context*/,
	const char */*filename*/,
	hdb_master_key */*mkey*/);

krb5_error_code
hdb_replace_extension (
	krb5_context /*context*/,
	hdb_entry */*entry*/,
	const HDB_extension */*ext*/);

krb5_error_code
hdb_seal_key (
	krb5_context /*context*/,
	HDB */*db*/,
	Key */*k*/);

krb5_error_code
hdb_seal_key_mkey (
	krb5_context /*context*/,
	Key */*k*/,
	hdb_master_key /*mkey*/);

krb5_error_code
hdb_seal_keys (
	krb5_context /*context*/,
	HDB */*db*/,
	hdb_entry */*ent*/);

krb5_error_code
hdb_seal_keys_mkey (
	krb5_context /*context*/,
	hdb_entry */*ent*/,
	hdb_master_key /*mkey*/);

krb5_error_code
hdb_set_master_key (
	krb5_context /*context*/,
	HDB */*db*/,
	krb5_keyblock */*key*/);

krb5_error_code
hdb_set_master_keyfile (
	krb5_context /*context*/,
	HDB */*db*/,
	const char */*keyfile*/);

krb5_error_code
hdb_sqlite_create (
	krb5_context /*context*/,
	HDB **/*db*/,
	const char */*argument*/);

krb5_error_code
hdb_unlock (int /*fd*/);

krb5_error_code
hdb_unseal_key (
	krb5_context /*context*/,
	HDB */*db*/,
	Key */*k*/);

krb5_error_code
hdb_unseal_key_mkey (
	krb5_context /*context*/,
	Key */*k*/,
	hdb_master_key /*mkey*/);

krb5_error_code
hdb_unseal_keys (
	krb5_context /*context*/,
	HDB */*db*/,
	hdb_entry */*ent*/);

krb5_error_code
hdb_unseal_keys_mkey (
	krb5_context /*context*/,
	hdb_entry */*ent*/,
	hdb_master_key /*mkey*/);

int
hdb_value2entry (
	krb5_context /*context*/,
	krb5_data */*value*/,
	hdb_entry */*ent*/);

int
hdb_value2entry_alias (
	krb5_context /*context*/,
	krb5_data */*value*/,
	hdb_entry_alias */*ent*/);

krb5_error_code
hdb_write_master_key (
	krb5_context /*context*/,
	const char */*filename*/,
	hdb_master_key /*mkey*/);

#ifdef __cplusplus
}
#endif

#endif /* __hdb_protos_h__ */
