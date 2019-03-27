/* This is a generated file */
#ifndef __hdb_private_h__
#define __hdb_private_h__

#include <stdarg.h>

krb5_error_code
_hdb_fetch_kvno (
	krb5_context /*context*/,
	HDB */*db*/,
	krb5_const_principal /*principal*/,
	unsigned /*flags*/,
	krb5_kvno /*kvno*/,
	hdb_entry_ex */*entry*/);

hdb_master_key
_hdb_find_master_key (
	uint32_t */*mkvno*/,
	hdb_master_key /*mkey*/);

krb5_error_code
_hdb_keytab2hdb_entry (
	krb5_context /*context*/,
	const krb5_keytab_entry */*ktentry*/,
	hdb_entry_ex */*entry*/);

int
_hdb_mkey_decrypt (
	krb5_context /*context*/,
	hdb_master_key /*key*/,
	krb5_key_usage /*usage*/,
	void */*ptr*/,
	size_t /*size*/,
	krb5_data */*res*/);

int
_hdb_mkey_encrypt (
	krb5_context /*context*/,
	hdb_master_key /*key*/,
	krb5_key_usage /*usage*/,
	const void */*ptr*/,
	size_t /*size*/,
	krb5_data */*res*/);

int
_hdb_mkey_version (hdb_master_key /*mkey*/);

krb5_error_code
_hdb_remove (
	krb5_context /*context*/,
	HDB */*db*/,
	krb5_const_principal /*principal*/);

krb5_error_code
_hdb_store (
	krb5_context /*context*/,
	HDB */*db*/,
	unsigned /*flags*/,
	hdb_entry_ex */*entry*/);

#endif /* __hdb_private_h__ */
