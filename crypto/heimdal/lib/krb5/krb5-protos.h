/* This is a generated file */
#ifndef __krb5_protos_h__
#define __krb5_protos_h__

#include <stdarg.h>

#if !defined(__GNUC__) && !defined(__attribute__)
#define __attribute__(x)
#endif

#ifndef KRB5_DEPRECATED_FUNCTION
#if defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1 )))
#define KRB5_DEPRECATED_FUNCTION(X) __attribute__((__deprecated__))
#else
#define KRB5_DEPRECATED_FUNCTION(X)
#endif
#endif


#ifdef __cplusplus
extern "C" {
#endif

#ifndef KRB5_LIB
#ifndef KRB5_LIB_FUNCTION
#if defined(_WIN32)
#define KRB5_LIB_FUNCTION __declspec(dllimport)
#define KRB5_LIB_CALL __stdcall
#define KRB5_LIB_VARIABLE __declspec(dllimport)
#else
#define KRB5_LIB_FUNCTION
#define KRB5_LIB_CALL
#define KRB5_LIB_VARIABLE
#endif
#endif
#endif
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb524_convert_creds_kdc (
	krb5_context /*context*/,
	krb5_creds */*in_cred*/,
	struct credentials */*v4creds*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb524_convert_creds_kdc_ccache (
	krb5_context /*context*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*in_cred*/,
	struct credentials */*v4creds*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_abort (
	krb5_context /*context*/,
	krb5_error_code /*code*/,
	const char */*fmt*/,
	...)
     __attribute__ ((noreturn, format (printf, 3, 4)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_abortx (
	krb5_context /*context*/,
	const char */*fmt*/,
	...)
     __attribute__ ((noreturn, format (printf, 2, 3)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_acl_match_file (
	krb5_context /*context*/,
	const char */*file*/,
	const char */*format*/,
	...);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_acl_match_string (
	krb5_context /*context*/,
	const char */*string*/,
	const char */*format*/,
	...);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_add_et_list (
	krb5_context /*context*/,
	void (*/*func*/)(struct et_list **));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_add_extra_addresses (
	krb5_context /*context*/,
	krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_add_ignore_addresses (
	krb5_context /*context*/,
	krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_addlog_dest (
	krb5_context /*context*/,
	krb5_log_facility */*f*/,
	const char */*orig*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_addlog_func (
	krb5_context /*context*/,
	krb5_log_facility */*fac*/,
	int /*min*/,
	int /*max*/,
	krb5_log_log_func_t /*log_func*/,
	krb5_log_close_func_t /*close_func*/,
	void */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_addr2sockaddr (
	krb5_context /*context*/,
	const krb5_address */*addr*/,
	struct sockaddr */*sa*/,
	krb5_socklen_t */*sa_size*/,
	int /*port*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_address_compare (
	krb5_context /*context*/,
	const krb5_address */*addr1*/,
	const krb5_address */*addr2*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_address_order (
	krb5_context /*context*/,
	const krb5_address */*addr1*/,
	const krb5_address */*addr2*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_address_prefixlen_boundary (
	krb5_context /*context*/,
	const krb5_address */*inaddr*/,
	unsigned long /*prefixlen*/,
	krb5_address */*low*/,
	krb5_address */*high*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_address_search (
	krb5_context /*context*/,
	const krb5_address */*addr*/,
	const krb5_addresses */*addrlist*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_allow_weak_crypto (
	krb5_context /*context*/,
	krb5_boolean /*enable*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_aname_to_localname (
	krb5_context /*context*/,
	krb5_const_principal /*aname*/,
	size_t /*lnsize*/,
	char */*lname*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_anyaddr (
	krb5_context /*context*/,
	int /*af*/,
	struct sockaddr */*sa*/,
	krb5_socklen_t */*sa_size*/,
	int /*port*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_appdefault_boolean (
	krb5_context /*context*/,
	const char */*appname*/,
	krb5_const_realm /*realm*/,
	const char */*option*/,
	krb5_boolean /*def_val*/,
	krb5_boolean */*ret_val*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_appdefault_string (
	krb5_context /*context*/,
	const char */*appname*/,
	krb5_const_realm /*realm*/,
	const char */*option*/,
	const char */*def_val*/,
	char **/*ret_val*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_appdefault_time (
	krb5_context /*context*/,
	const char */*appname*/,
	krb5_const_realm /*realm*/,
	const char */*option*/,
	time_t /*def_val*/,
	time_t */*ret_val*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_append_addresses (
	krb5_context /*context*/,
	krb5_addresses */*dest*/,
	const krb5_addresses */*source*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_addflags (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t /*addflags*/,
	int32_t */*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_free (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_genaddrs (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_socket_t /*fd*/,
	int /*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_generatelocalsubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getaddrs (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_address **/*local_addr*/,
	krb5_address **/*remote_addr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getauthenticator (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_authenticator */*authenticator*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getcksumtype (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_cksumtype */*cksumtype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getflags (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t */*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock **/*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getkeytype (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keytype */*keytype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getlocalseqnumber (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t */*seqnumber*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getlocalsubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock **/*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getrcache (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_rcache */*rcache*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getrecvsubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock **/*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getremoteseqnumber (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t */*seqnumber*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getremotesubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock **/*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getsendsubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock **/*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_init (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_removeflags (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t /*removeflags*/,
	int32_t */*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setaddrs (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_address */*local_addr*/,
	krb5_address */*remote_addr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setaddrs_from_fd (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	void */*p_fd*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setcksumtype (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_cksumtype /*cksumtype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setflags (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t /*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setkeytype (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keytype /*keytype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setlocalseqnumber (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t /*seqnumber*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setlocalsubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setrcache (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_rcache /*rcache*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setrecvsubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setremoteseqnumber (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t /*seqnumber*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setremotesubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setsendsubkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setuserkey (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_getremoteseqnumber (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	int32_t */*seqnumber*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_ap_req (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	krb5_creds */*cred*/,
	krb5_flags /*ap_options*/,
	krb5_data /*authenticator*/,
	krb5_data */*retdata*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal (
	krb5_context /*context*/,
	krb5_principal */*principal*/,
	int /*rlen*/,
	krb5_const_realm /*realm*/,
	...);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal_ext (
	krb5_context /*context*/,
	krb5_principal */*principal*/,
	int /*rlen*/,
	krb5_const_realm /*realm*/,
	...);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal_va (
	krb5_context /*context*/,
	krb5_principal */*principal*/,
	int /*rlen*/,
	krb5_const_realm /*realm*/,
	va_list /*ap*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_principal_va_ext (
	krb5_context /*context*/,
	krb5_principal */*principal*/,
	int /*rlen*/,
	krb5_const_realm /*realm*/,
	va_list /*ap*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_block_size (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	size_t */*blocksize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_checksum_length (
	krb5_context /*context*/,
	krb5_cksumtype /*cksumtype*/,
	size_t */*length*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_decrypt (
	krb5_context /*context*/,
	const krb5_keyblock /*key*/,
	krb5_keyusage /*usage*/,
	const krb5_data */*ivec*/,
	krb5_enc_data */*input*/,
	krb5_data */*output*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_encrypt (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	krb5_keyusage /*usage*/,
	const krb5_data */*ivec*/,
	const krb5_data */*input*/,
	krb5_enc_data */*output*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_encrypt_length (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	size_t /*inputlen*/,
	size_t */*length*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_enctype_compare (
	krb5_context /*context*/,
	krb5_enctype /*e1*/,
	krb5_enctype /*e2*/,
	krb5_boolean */*similar*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_get_checksum (
	krb5_context /*context*/,
	const krb5_checksum */*cksum*/,
	krb5_cksumtype */*type*/,
	krb5_data **/*data*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_is_coll_proof_cksum (krb5_cksumtype /*ctype*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_is_keyed_cksum (krb5_cksumtype /*ctype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_keylengths (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	size_t */*ilen*/,
	size_t */*keylen*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_make_checksum (
	krb5_context /*context*/,
	krb5_cksumtype /*cksumtype*/,
	const krb5_keyblock */*key*/,
	krb5_keyusage /*usage*/,
	const krb5_data */*input*/,
	krb5_checksum */*cksum*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_make_random_key (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	krb5_keyblock */*random_key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_prf (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	const krb5_data */*input*/,
	krb5_data */*output*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_prf_length (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	size_t */*length*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_random_make_octets (
	krb5_context /*context*/,
	krb5_data * /*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_set_checksum (
	krb5_context /*context*/,
	krb5_checksum */*cksum*/,
	krb5_cksumtype /*type*/,
	const krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_valid_cksumtype (krb5_cksumtype /*ctype*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_valid_enctype (krb5_enctype /*etype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_verify_checksum (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	krb5_keyusage /*usage*/,
	const krb5_data */*data*/,
	const krb5_checksum */*cksum*/,
	krb5_boolean */*valid*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_cache_end_seq_get (
	krb5_context /*context*/,
	krb5_cc_cache_cursor /*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_cache_get_first (
	krb5_context /*context*/,
	const char */*type*/,
	krb5_cc_cache_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_cache_match (
	krb5_context /*context*/,
	krb5_principal /*client*/,
	krb5_ccache */*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_cache_next (
	krb5_context /*context*/,
	krb5_cc_cache_cursor /*cursor*/,
	krb5_ccache */*id*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_cc_clear_mcred (krb5_creds */*mcred*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_close (
	krb5_context /*context*/,
	krb5_ccache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_copy_cache (
	krb5_context /*context*/,
	const krb5_ccache /*from*/,
	krb5_ccache /*to*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_copy_creds (
	krb5_context /*context*/,
	const krb5_ccache /*from*/,
	krb5_ccache /*to*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_copy_match_f (
	krb5_context /*context*/,
	const krb5_ccache /*from*/,
	krb5_ccache /*to*/,
	krb5_boolean (*/*match*/)(krb5_context, void *, const krb5_creds *),
	void */*matchctx*/,
	unsigned int */*matched*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_default (
	krb5_context /*context*/,
	krb5_ccache */*id*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_cc_default_name (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_destroy (
	krb5_context /*context*/,
	krb5_ccache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_end_seq_get (
	krb5_context /*context*/,
	const krb5_ccache /*id*/,
	krb5_cc_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_gen_new (
	krb5_context /*context*/,
	const krb5_cc_ops */*ops*/,
	krb5_ccache */*id*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_config (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_const_principal /*principal*/,
	const char */*name*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_flags (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_flags */*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_friendly_name (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	char **/*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_full_name (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	char **/*str*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_kdc_offset (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_deltat */*offset*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_lifetime (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	time_t */*t*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_cc_get_name (
	krb5_context /*context*/,
	krb5_ccache /*id*/);

KRB5_LIB_FUNCTION const krb5_cc_ops * KRB5_LIB_CALL
krb5_cc_get_ops (
	krb5_context /*context*/,
	krb5_ccache /*id*/);

KRB5_LIB_FUNCTION const krb5_cc_ops * KRB5_LIB_CALL
krb5_cc_get_prefix_ops (
	krb5_context /*context*/,
	const char */*prefix*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_principal (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_principal */*principal*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_cc_get_type (
	krb5_context /*context*/,
	krb5_ccache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_get_version (
	krb5_context /*context*/,
	const krb5_ccache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_initialize (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_principal /*primary_principal*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_last_change_time (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_timestamp */*mtime*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_move (
	krb5_context /*context*/,
	krb5_ccache /*from*/,
	krb5_ccache /*to*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_new_unique (
	krb5_context /*context*/,
	const char */*type*/,
	const char */*hint*/,
	krb5_ccache */*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_next_cred (
	krb5_context /*context*/,
	const krb5_ccache /*id*/,
	krb5_cc_cursor */*cursor*/,
	krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_register (
	krb5_context /*context*/,
	const krb5_cc_ops */*ops*/,
	krb5_boolean /*override*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_remove_cred (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_flags /*which*/,
	krb5_creds */*cred*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_resolve (
	krb5_context /*context*/,
	const char */*name*/,
	krb5_ccache */*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_retrieve_cred (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_flags /*whichfields*/,
	const krb5_creds */*mcreds*/,
	krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_set_config (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_const_principal /*principal*/,
	const char */*name*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_set_default_name (
	krb5_context /*context*/,
	const char */*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_set_flags (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_flags /*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_set_friendly_name (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	const char */*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_set_kdc_offset (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_deltat /*offset*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_start_seq_get (
	krb5_context /*context*/,
	const krb5_ccache /*id*/,
	krb5_cc_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_store_cred (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_cc_support_switch (
	krb5_context /*context*/,
	const char */*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_switch (
	krb5_context /*context*/,
	krb5_ccache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cccol_cursor_free (
	krb5_context /*context*/,
	krb5_cccol_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cccol_cursor_new (
	krb5_context /*context*/,
	krb5_cccol_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cccol_cursor_next (
	krb5_context /*context*/,
	krb5_cccol_cursor /*cursor*/,
	krb5_ccache */*cache*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cccol_last_change_time (
	krb5_context /*context*/,
	const char */*type*/,
	krb5_timestamp */*mtime*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_change_password (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	const char */*newpw*/,
	int */*result_code*/,
	krb5_data */*result_code_string*/,
	krb5_data */*result_string*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_check_transited (
	krb5_context /*context*/,
	krb5_const_realm /*client_realm*/,
	krb5_const_realm /*server_realm*/,
	krb5_realm */*realms*/,
	unsigned int /*num_realms*/,
	int */*bad_realm*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_check_transited_realms (
	krb5_context /*context*/,
	const char *const */*realms*/,
	unsigned int /*num_realms*/,
	int */*bad_realm*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_checksum_disable (
	krb5_context /*context*/,
	krb5_cksumtype /*type*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_checksum_free (
	krb5_context /*context*/,
	krb5_checksum */*cksum*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_checksum_is_collision_proof (
	krb5_context /*context*/,
	krb5_cksumtype /*type*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_checksum_is_keyed (
	krb5_context /*context*/,
	krb5_cksumtype /*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_checksumsize (
	krb5_context /*context*/,
	krb5_cksumtype /*type*/,
	size_t */*size*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cksumtype_to_enctype (
	krb5_context /*context*/,
	krb5_cksumtype /*ctype*/,
	krb5_enctype */*etype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cksumtype_valid (
	krb5_context /*context*/,
	krb5_cksumtype /*ctype*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_clear_error_message (krb5_context /*context*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_clear_error_string (krb5_context /*context*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_closelog (
	krb5_context /*context*/,
	krb5_log_facility */*fac*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_compare_creds (
	krb5_context /*context*/,
	krb5_flags /*whichfields*/,
	const krb5_creds * /*mcreds*/,
	const krb5_creds * /*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_config_file_free (
	krb5_context /*context*/,
	krb5_config_section */*s*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_config_free_strings (char **/*strings*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_config_get_bool (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	...);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_config_get_bool_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	krb5_boolean /*def_value*/,
	...);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_get_int (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	...);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_get_int_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	int /*def_value*/,
	...);

KRB5_LIB_FUNCTION const krb5_config_binding * KRB5_LIB_CALL
krb5_config_get_list (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	...);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_config_get_string (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	...);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_config_get_string_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	const char */*def_value*/,
	...);

KRB5_LIB_FUNCTION char** KRB5_LIB_CALL
krb5_config_get_strings (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	...);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_get_time (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	...);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_get_time_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	int /*def_value*/,
	...);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_config_parse_file (
	krb5_context /*context*/,
	const char */*fname*/,
	krb5_config_section **/*res*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_config_parse_file_multi (
	krb5_context /*context*/,
	const char */*fname*/,
	krb5_config_section **/*res*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_config_parse_string_multi (
	krb5_context /*context*/,
	const char */*string*/,
	krb5_config_section **/*res*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_config_vget_bool (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_config_vget_bool_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	krb5_boolean /*def_value*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_vget_int (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_vget_int_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	int /*def_value*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION const krb5_config_binding * KRB5_LIB_CALL
krb5_config_vget_list (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_config_vget_string (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_config_vget_string_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	const char */*def_value*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION char ** KRB5_LIB_CALL
krb5_config_vget_strings (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_vget_time (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_config_vget_time_default (
	krb5_context /*context*/,
	const krb5_config_section */*c*/,
	int /*def_value*/,
	va_list /*args*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_address (
	krb5_context /*context*/,
	const krb5_address */*inaddr*/,
	krb5_address */*outaddr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_addresses (
	krb5_context /*context*/,
	const krb5_addresses */*inaddr*/,
	krb5_addresses */*outaddr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_checksum (
	krb5_context /*context*/,
	const krb5_checksum */*old*/,
	krb5_checksum **/*new*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_context (
	krb5_context /*context*/,
	krb5_context */*out*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_creds (
	krb5_context /*context*/,
	const krb5_creds */*incred*/,
	krb5_creds **/*outcred*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_creds_contents (
	krb5_context /*context*/,
	const krb5_creds */*incred*/,
	krb5_creds */*c*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_data (
	krb5_context /*context*/,
	const krb5_data */*indata*/,
	krb5_data **/*outdata*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_host_realm (
	krb5_context /*context*/,
	const krb5_realm */*from*/,
	krb5_realm **/*to*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_keyblock (
	krb5_context /*context*/,
	const krb5_keyblock */*inblock*/,
	krb5_keyblock **/*to*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_keyblock_contents (
	krb5_context /*context*/,
	const krb5_keyblock */*inblock*/,
	krb5_keyblock */*to*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_principal (
	krb5_context /*context*/,
	krb5_const_principal /*inprinc*/,
	krb5_principal */*outprinc*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_ticket (
	krb5_context /*context*/,
	const krb5_ticket */*from*/,
	krb5_ticket **/*to*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_create_checksum (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	krb5_key_usage /*usage*/,
	int /*type*/,
	void */*data*/,
	size_t /*len*/,
	Checksum */*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_create_checksum_iov (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	krb5_crypto_iov */*data*/,
	unsigned int /*num_data*/,
	krb5_cksumtype */*type*/);

KRB5_LIB_FUNCTION unsigned long KRB5_LIB_CALL
krb5_creds_get_ticket_flags (krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_destroy (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_fx_cf2 (
	krb5_context /*context*/,
	const krb5_crypto /*crypto1*/,
	const krb5_crypto /*crypto2*/,
	krb5_data */*pepper1*/,
	krb5_data */*pepper2*/,
	krb5_enctype /*enctype*/,
	krb5_keyblock */*res*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_get_checksum_type (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	krb5_cksumtype */*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_getblocksize (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	size_t */*blocksize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_getconfoundersize (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	size_t */*confoundersize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_getenctype (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	krb5_enctype */*enctype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_getpadsize (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	size_t */*padsize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_init (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	krb5_enctype /*etype*/,
	krb5_crypto */*crypto*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_length (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	int /*type*/,
	size_t */*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_length_iov (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	krb5_crypto_iov */*data*/,
	unsigned int /*num_data*/);

KRB5_LIB_FUNCTION size_t KRB5_LIB_CALL
krb5_crypto_overhead (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_prf (
	krb5_context /*context*/,
	const krb5_crypto /*crypto*/,
	const krb5_data */*input*/,
	krb5_data */*output*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_crypto_prf_length (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	size_t */*length*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_data_alloc (
	krb5_data */*p*/,
	int /*len*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_data_cmp (
	const krb5_data */*data1*/,
	const krb5_data */*data2*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_data_copy (
	krb5_data */*p*/,
	const void */*data*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_data_ct_cmp (
	const krb5_data */*data1*/,
	const krb5_data */*data2*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_data_free (krb5_data */*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_data_realloc (
	krb5_data */*p*/,
	int /*len*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_data_zero (krb5_data */*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_Authenticator (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	Authenticator */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_ETYPE_INFO (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	ETYPE_INFO */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_ETYPE_INFO2 (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	ETYPE_INFO2 */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncAPRepPart (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	EncAPRepPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncASRepPart (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	EncASRepPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncKrbCredPart (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	EncKrbCredPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncTGSRepPart (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	EncTGSRepPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncTicketPart (
	krb5_context /*context*/,
	const void */*data*/,
	size_t /*length*/,
	EncTicketPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_ap_req (
	krb5_context /*context*/,
	const krb5_data */*inbuf*/,
	krb5_ap_req */*ap_req*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decrypt (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	void */*data*/,
	size_t /*len*/,
	krb5_data */*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decrypt_EncryptedData (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	const EncryptedData */*e*/,
	krb5_data */*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decrypt_iov_ivec (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	krb5_crypto_iov */*data*/,
	unsigned int /*num_data*/,
	void */*ivec*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decrypt_ivec (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	void */*data*/,
	size_t /*len*/,
	krb5_data */*result*/,
	void */*ivec*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decrypt_ticket (
	krb5_context /*context*/,
	Ticket */*ticket*/,
	krb5_keyblock */*key*/,
	EncTicketPart */*out*/,
	krb5_flags /*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_derive_key (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	krb5_enctype /*etype*/,
	const void */*constant*/,
	size_t /*constant_len*/,
	krb5_keyblock **/*derived_key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_alloc (
	krb5_context /*context*/,
	krb5_digest */*digest*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_digest_free (krb5_digest /*digest*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_get_client_binding (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	char **/*type*/,
	char **/*binding*/);

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_identifier (
	krb5_context /*context*/,
	krb5_digest /*digest*/);

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_opaque (
	krb5_context /*context*/,
	krb5_digest /*digest*/);

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_rsp (
	krb5_context /*context*/,
	krb5_digest /*digest*/);

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_digest_get_server_nonce (
	krb5_context /*context*/,
	krb5_digest /*digest*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_get_session_key (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_get_tickets (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	Ticket **/*tickets*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_init_request (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	krb5_realm /*realm*/,
	krb5_ccache /*ccache*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_probe (
	krb5_context /*context*/,
	krb5_realm /*realm*/,
	krb5_ccache /*ccache*/,
	unsigned */*flags*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_digest_rep_get_status (
	krb5_context /*context*/,
	krb5_digest /*digest*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_request (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	krb5_realm /*realm*/,
	krb5_ccache /*ccache*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_authentication_user (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	krb5_principal /*authentication_user*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_authid (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*authid*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_client_nonce (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*nonce*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_digest (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*dgst*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_hostname (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*hostname*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_identifier (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_method (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*method*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_nonceCount (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*nonce_count*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_opaque (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*opaque*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_qop (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*qop*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_realm (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*realm*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_digest_set_responseData (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*response*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_server_cb (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*type*/,
	const char */*binding*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_server_nonce (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*nonce*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_type (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_uri (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*uri*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_digest_set_username (
	krb5_context /*context*/,
	krb5_digest /*digest*/,
	const char */*username*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_domain_x500_decode (
	krb5_context /*context*/,
	krb5_data /*tr*/,
	char ***/*realms*/,
	unsigned int */*num_realms*/,
	const char */*client_realm*/,
	const char */*server_realm*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_domain_x500_encode (
	char **/*realms*/,
	unsigned int /*num_realms*/,
	krb5_data */*encoding*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_eai_to_heim_errno (
	int /*eai_errno*/,
	int /*system_error*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_Authenticator (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	Authenticator */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_ETYPE_INFO (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	ETYPE_INFO */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_ETYPE_INFO2 (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	ETYPE_INFO2 */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncAPRepPart (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	EncAPRepPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncASRepPart (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	EncASRepPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncKrbCredPart (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	EncKrbCredPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncTGSRepPart (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	EncTGSRepPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncTicketPart (
	krb5_context /*context*/,
	void */*data*/,
	size_t /*length*/,
	EncTicketPart */*t*/,
	size_t */*len*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encrypt (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	const void */*data*/,
	size_t /*len*/,
	krb5_data */*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encrypt_EncryptedData (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	void */*data*/,
	size_t /*len*/,
	int /*kvno*/,
	EncryptedData */*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encrypt_iov_ivec (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	krb5_crypto_iov */*data*/,
	int /*num_data*/,
	void */*ivec*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encrypt_ivec (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	const void */*data*/,
	size_t /*len*/,
	krb5_data */*result*/,
	void */*ivec*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_enctype_disable (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_enctype_enable (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_enctype_keybits (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	size_t */*keybits*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_enctype_keysize (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	size_t */*keysize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_enctype_to_keytype (
	krb5_context /*context*/,
	krb5_enctype /*etype*/,
	krb5_keytype */*keytype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_enctype_to_string (
	krb5_context /*context*/,
	krb5_enctype /*etype*/,
	char **/*string*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_enctype_valid (
	krb5_context /*context*/,
	krb5_enctype /*etype*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_enctypes_compatible_keys (
	krb5_context /*context*/,
	krb5_enctype /*etype1*/,
	krb5_enctype /*etype2*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

krb5_error_code
krb5_enomem (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_err (
	krb5_context /*context*/,
	int /*eval*/,
	krb5_error_code /*code*/,
	const char */*fmt*/,
	...)
     __attribute__ ((noreturn, format (printf, 4, 5)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_error_from_rd_error (
	krb5_context /*context*/,
	const krb5_error */*error*/,
	const krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_errx (
	krb5_context /*context*/,
	int /*eval*/,
	const char */*fmt*/,
	...)
     __attribute__ ((noreturn, format (printf, 3, 4)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_expand_hostname (
	krb5_context /*context*/,
	const char */*orig_hostname*/,
	char **/*new_hostname*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_expand_hostname_realms (
	krb5_context /*context*/,
	const char */*orig_hostname*/,
	char **/*new_hostname*/,
	char ***/*realms*/);

KRB5_LIB_FUNCTION PA_DATA * KRB5_LIB_CALL
krb5_find_padata (
	PA_DATA */*val*/,
	unsigned /*len*/,
	int /*type*/,
	int */*idx*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_format_time (
	krb5_context /*context*/,
	time_t /*t*/,
	char */*s*/,
	size_t /*len*/,
	krb5_boolean /*include_time*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_address (
	krb5_context /*context*/,
	krb5_address */*address*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_addresses (
	krb5_context /*context*/,
	krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_ap_rep_enc_part (
	krb5_context /*context*/,
	krb5_ap_rep_enc_part */*val*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_authenticator (
	krb5_context /*context*/,
	krb5_authenticator */*authenticator*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_checksum (
	krb5_context /*context*/,
	krb5_checksum */*cksum*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_checksum_contents (
	krb5_context /*context*/,
	krb5_checksum */*cksum*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_config_files (char **/*filenames*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_context (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_cred_contents (
	krb5_context /*context*/,
	krb5_creds */*c*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_creds (
	krb5_context /*context*/,
	krb5_creds */*c*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_creds_contents (
	krb5_context /*context*/,
	krb5_creds */*c*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_data (
	krb5_context /*context*/,
	krb5_data */*p*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_data_contents (
	krb5_context /*context*/,
	krb5_data */*data*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_default_realm (
	krb5_context /*context*/,
	krb5_realm /*realm*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_error (
	krb5_context /*context*/,
	krb5_error */*error*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_error_contents (
	krb5_context /*context*/,
	krb5_error */*error*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_error_message (
	krb5_context /*context*/,
	const char */*msg*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_error_string (
	krb5_context /*context*/,
	char */*str*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_host_realm (
	krb5_context /*context*/,
	krb5_realm */*realmlist*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_kdc_rep (
	krb5_context /*context*/,
	krb5_kdc_rep */*rep*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_keyblock (
	krb5_context /*context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_keyblock_contents (
	krb5_context /*context*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_krbhst (
	krb5_context /*context*/,
	char **/*hostlist*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_principal (
	krb5_context /*context*/,
	krb5_principal /*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_salt (
	krb5_context /*context*/,
	krb5_salt /*salt*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_ticket (
	krb5_context /*context*/,
	krb5_ticket */*ticket*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_unparsed_name (
	krb5_context /*context*/,
	char */*str*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_fwd_tgt_creds (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	const char */*hostname*/,
	krb5_principal /*client*/,
	krb5_principal /*server*/,
	krb5_ccache /*ccache*/,
	int /*forwardable*/,
	krb5_data */*out_data*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_generate_random_block (
	void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_generate_random_keyblock (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_generate_seq_number (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	uint32_t */*seqno*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_generate_subkey (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	krb5_keyblock **/*subkey*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_generate_subkey_extended (
	krb5_context /*context*/,
	const krb5_keyblock */*key*/,
	krb5_enctype /*etype*/,
	krb5_keyblock **/*subkey*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_all_client_addrs (
	krb5_context /*context*/,
	krb5_addresses */*res*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_all_server_addrs (
	krb5_context /*context*/,
	krb5_addresses */*res*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_cred_from_kdc (
	krb5_context /*context*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*in_creds*/,
	krb5_creds **/*out_creds*/,
	krb5_creds ***/*ret_tgts*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_cred_from_kdc_opt (
	krb5_context /*context*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*in_creds*/,
	krb5_creds **/*out_creds*/,
	krb5_creds ***/*ret_tgts*/,
	krb5_flags /*flags*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_credentials (
	krb5_context /*context*/,
	krb5_flags /*options*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*in_creds*/,
	krb5_creds **/*out_creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_credentials_with_flags (
	krb5_context /*context*/,
	krb5_flags /*options*/,
	krb5_kdc_flags /*flags*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*in_creds*/,
	krb5_creds **/*out_creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_creds (
	krb5_context /*context*/,
	krb5_get_creds_opt /*opt*/,
	krb5_ccache /*ccache*/,
	krb5_const_principal /*inprinc*/,
	krb5_creds **/*out_creds*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_creds_opt_add_options (
	krb5_context /*context*/,
	krb5_get_creds_opt /*opt*/,
	krb5_flags /*options*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_creds_opt_alloc (
	krb5_context /*context*/,
	krb5_get_creds_opt */*opt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_creds_opt_free (
	krb5_context /*context*/,
	krb5_get_creds_opt /*opt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_creds_opt_set_enctype (
	krb5_context /*context*/,
	krb5_get_creds_opt /*opt*/,
	krb5_enctype /*enctype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_creds_opt_set_impersonate (
	krb5_context /*context*/,
	krb5_get_creds_opt /*opt*/,
	krb5_const_principal /*self*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_creds_opt_set_options (
	krb5_context /*context*/,
	krb5_get_creds_opt /*opt*/,
	krb5_flags /*options*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_creds_opt_set_ticket (
	krb5_context /*context*/,
	krb5_get_creds_opt /*opt*/,
	const Ticket */*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_default_config_files (char ***/*pfilenames*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_default_in_tkt_etypes (
	krb5_context /*context*/,
	krb5_pdu /*pdu_type*/,
	krb5_enctype **/*etypes*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_default_principal (
	krb5_context /*context*/,
	krb5_principal */*princ*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_default_realm (
	krb5_context /*context*/,
	krb5_realm */*realm*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_default_realms (
	krb5_context /*context*/,
	krb5_realm **/*realms*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_get_dns_canonicalize_hostname (krb5_context /*context*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_get_err_text (
	krb5_context /*context*/,
	krb5_error_code /*code*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
krb5_get_error_message (
	krb5_context /*context*/,
	krb5_error_code /*code*/);

KRB5_LIB_FUNCTION char * KRB5_LIB_CALL
krb5_get_error_string (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_extra_addresses (
	krb5_context /*context*/,
	krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_fcache_version (
	krb5_context /*context*/,
	int */*version*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_forwarded_creds (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_ccache /*ccache*/,
	krb5_flags /*flags*/,
	const char */*hostname*/,
	krb5_creds */*in_creds*/,
	krb5_data */*out_data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_host_realm (
	krb5_context /*context*/,
	const char */*targethost*/,
	krb5_realm **/*realms*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_ignore_addresses (
	krb5_context /*context*/,
	krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_cred (
	krb5_context /*context*/,
	krb5_flags /*options*/,
	const krb5_addresses */*addrs*/,
	const krb5_enctype */*etypes*/,
	const krb5_preauthtype */*ptypes*/,
	const krb5_preauthdata */*preauth*/,
	krb5_key_proc /*key_proc*/,
	krb5_const_pointer /*keyseed*/,
	krb5_decrypt_proc /*decrypt_proc*/,
	krb5_const_pointer /*decryptarg*/,
	krb5_creds */*creds*/,
	krb5_kdc_rep */*ret_as_reply*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt (
	krb5_context /*context*/,
	krb5_flags /*options*/,
	const krb5_addresses */*addrs*/,
	const krb5_enctype */*etypes*/,
	const krb5_preauthtype */*ptypes*/,
	krb5_key_proc /*key_proc*/,
	krb5_const_pointer /*keyseed*/,
	krb5_decrypt_proc /*decrypt_proc*/,
	krb5_const_pointer /*decryptarg*/,
	krb5_creds */*creds*/,
	krb5_ccache /*ccache*/,
	krb5_kdc_rep */*ret_as_reply*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt_with_keytab (
	krb5_context /*context*/,
	krb5_flags /*options*/,
	krb5_addresses */*addrs*/,
	const krb5_enctype */*etypes*/,
	const krb5_preauthtype */*pre_auth_types*/,
	krb5_keytab /*keytab*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*creds*/,
	krb5_kdc_rep */*ret_as_reply*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt_with_password (
	krb5_context /*context*/,
	krb5_flags /*options*/,
	krb5_addresses */*addrs*/,
	const krb5_enctype */*etypes*/,
	const krb5_preauthtype */*pre_auth_types*/,
	const char */*password*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*creds*/,
	krb5_kdc_rep */*ret_as_reply*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt_with_skey (
	krb5_context /*context*/,
	krb5_flags /*options*/,
	krb5_addresses */*addrs*/,
	const krb5_enctype */*etypes*/,
	const krb5_preauthtype */*pre_auth_types*/,
	const krb5_keyblock */*key*/,
	krb5_ccache /*ccache*/,
	krb5_creds */*creds*/,
	krb5_kdc_rep */*ret_as_reply*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_keyblock (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	krb5_principal /*client*/,
	krb5_keyblock */*keyblock*/,
	krb5_deltat /*start_time*/,
	const char */*in_tkt_service*/,
	krb5_get_init_creds_opt */*options*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_keytab (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	krb5_principal /*client*/,
	krb5_keytab /*keytab*/,
	krb5_deltat /*start_time*/,
	const char */*in_tkt_service*/,
	krb5_get_init_creds_opt */*options*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_alloc (
	krb5_context /*context*/,
	krb5_get_init_creds_opt **/*opt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_free (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_get_error (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	KRB_ERROR **/*error*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_init (krb5_get_init_creds_opt */*opt*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_address_list (
	krb5_get_init_creds_opt */*opt*/,
	krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_set_addressless (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	krb5_boolean /*addressless*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_anonymous (
	krb5_get_init_creds_opt */*opt*/,
	int /*anonymous*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_set_canonicalize (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	krb5_boolean /*req*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_default_flags (
	krb5_context /*context*/,
	const char */*appname*/,
	krb5_const_realm /*realm*/,
	krb5_get_init_creds_opt */*opt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_etype_list (
	krb5_get_init_creds_opt */*opt*/,
	krb5_enctype */*etype_list*/,
	int /*etype_list_length*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_forwardable (
	krb5_get_init_creds_opt */*opt*/,
	int /*forwardable*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_set_pa_password (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	const char */*password*/,
	krb5_s2k_proc /*key_proc*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_set_pac_request (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	krb5_boolean /*req_pac*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_set_pkinit (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	krb5_principal /*principal*/,
	const char */*user_id*/,
	const char */*x509_anchors*/,
	char * const * /*pool*/,
	char * const * /*pki_revoke*/,
	int /*flags*/,
	krb5_prompter_fct /*prompter*/,
	void */*prompter_data*/,
	char */*password*/);

krb5_error_code KRB5_LIB_FUNCTION
krb5_get_init_creds_opt_set_pkinit_user_certs (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	struct hx509_certs_data */*certs*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_preauth_list (
	krb5_get_init_creds_opt */*opt*/,
	krb5_preauthtype */*preauth_list*/,
	int /*preauth_list_length*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_set_process_last_req (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	krb5_gic_process_last_req /*func*/,
	void */*ctx*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_proxiable (
	krb5_get_init_creds_opt */*opt*/,
	int /*proxiable*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_renew_life (
	krb5_get_init_creds_opt */*opt*/,
	krb5_deltat /*renew_life*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_salt (
	krb5_get_init_creds_opt */*opt*/,
	krb5_data */*salt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_get_init_creds_opt_set_tkt_life (
	krb5_get_init_creds_opt */*opt*/,
	krb5_deltat /*tkt_life*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_opt_set_win2k (
	krb5_context /*context*/,
	krb5_get_init_creds_opt */*opt*/,
	krb5_boolean /*req*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_init_creds_password (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	krb5_principal /*client*/,
	const char */*password*/,
	krb5_prompter_fct /*prompter*/,
	void */*data*/,
	krb5_deltat /*start_time*/,
	const char */*in_tkt_service*/,
	krb5_get_init_creds_opt */*options*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_kdc_cred (
	krb5_context /*context*/,
	krb5_ccache /*id*/,
	krb5_kdc_flags /*flags*/,
	krb5_addresses */*addresses*/,
	Ticket */*second_ticket*/,
	krb5_creds */*in_creds*/,
	krb5_creds **out_creds );

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_kdc_sec_offset (
	krb5_context /*context*/,
	int32_t */*sec*/,
	int32_t */*usec*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krb524hst (
	krb5_context /*context*/,
	const krb5_realm */*realm*/,
	char ***/*hostlist*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krb_admin_hst (
	krb5_context /*context*/,
	const krb5_realm */*realm*/,
	char ***/*hostlist*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krb_changepw_hst (
	krb5_context /*context*/,
	const krb5_realm */*realm*/,
	char ***/*hostlist*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_krbhst (
	krb5_context /*context*/,
	const krb5_realm */*realm*/,
	char ***/*hostlist*/);

KRB5_LIB_FUNCTION time_t KRB5_LIB_CALL
krb5_get_max_time_skew (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_permitted_enctypes (
	krb5_context /*context*/,
	krb5_enctype **/*etypes*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_pw_salt (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	krb5_salt */*salt*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_renewed_creds (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	krb5_const_principal /*client*/,
	krb5_ccache /*ccache*/,
	const char */*in_tkt_service*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_server_rcache (
	krb5_context /*context*/,
	const krb5_data */*piece*/,
	krb5_rcache */*id*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_get_use_admin_kdc (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_validated_creds (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	krb5_principal /*client*/,
	krb5_ccache /*ccache*/,
	char */*service*/);

KRB5_LIB_FUNCTION krb5_log_facility * KRB5_LIB_CALL
krb5_get_warn_dest (krb5_context /*context*/);

KRB5_LIB_FUNCTION size_t KRB5_LIB_CALL
krb5_get_wrapped_length (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	size_t /*data_len*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_getportbyname (
	krb5_context /*context*/,
	const char */*service*/,
	const char */*proto*/,
	int /*default_port*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_h_addr2addr (
	krb5_context /*context*/,
	int /*af*/,
	const char */*haddr*/,
	krb5_address */*addr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_h_addr2sockaddr (
	krb5_context /*context*/,
	int /*af*/,
	const char */*addr*/,
	struct sockaddr */*sa*/,
	krb5_socklen_t */*sa_size*/,
	int /*port*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_h_errno_to_heim_errno (int /*eai_errno*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_have_error_string (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_hmac (
	krb5_context /*context*/,
	krb5_cksumtype /*cktype*/,
	const void */*data*/,
	size_t /*len*/,
	unsigned /*usage*/,
	krb5_keyblock */*key*/,
	Checksum */*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_context (krb5_context */*context*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_init_creds_free (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_get (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_get_creds (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/,
	krb5_creds */*cred*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_get_error (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/,
	KRB_ERROR */*error*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_init (
	krb5_context /*context*/,
	krb5_principal /*client*/,
	krb5_prompter_fct /*prompter*/,
	void */*prompter_data*/,
	krb5_deltat /*start_time*/,
	krb5_get_init_creds_opt */*options*/,
	krb5_init_creds_context */*rctx*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_keyblock (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_keytab (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/,
	krb5_keytab /*keytab*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_password (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/,
	const char */*password*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_set_service (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/,
	const char */*service*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_creds_step (
	krb5_context /*context*/,
	krb5_init_creds_context /*ctx*/,
	krb5_data */*in*/,
	krb5_data */*out*/,
	krb5_krbhst_info */*hostinfo*/,
	unsigned int */*flags*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_init_ets (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_initlog (
	krb5_context /*context*/,
	const char */*program*/,
	krb5_log_facility **/*fac*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_is_config_principal (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_is_thread_safe (void);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kcm_call (
	krb5_context /*context*/,
	krb5_storage */*request*/,
	krb5_storage **/*response_p*/,
	krb5_data */*response_data_p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kcm_storage_request (
	krb5_context /*context*/,
	uint16_t /*opcode*/,
	krb5_storage **/*storage_p*/);

KRB5_LIB_FUNCTION const krb5_enctype * KRB5_LIB_CALL
krb5_kerberos_enctypes (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_enctype KRB5_LIB_CALL
krb5_keyblock_get_enctype (const krb5_keyblock */*block*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keyblock_init (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	const void */*data*/,
	size_t /*size*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keyblock_key_proc (
	krb5_context /*context*/,
	krb5_keytype /*type*/,
	krb5_data */*salt*/,
	krb5_const_pointer /*keyseed*/,
	krb5_keyblock **/*key*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_keyblock_zero (krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_CALLCONV
krb5_keytab_key_proc (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	krb5_salt /*salt*/,
	krb5_const_pointer /*keyseed*/,
	krb5_keyblock **/*key*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keytype_to_enctypes (
	krb5_context /*context*/,
	krb5_keytype /*keytype*/,
	unsigned */*len*/,
	krb5_enctype **/*val*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keytype_to_enctypes_default (
	krb5_context /*context*/,
	krb5_keytype /*keytype*/,
	unsigned */*len*/,
	krb5_enctype **/*val*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keytype_to_string (
	krb5_context /*context*/,
	krb5_keytype /*keytype*/,
	char **/*string*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_format_string (
	krb5_context /*context*/,
	const krb5_krbhst_info */*host*/,
	char */*hostname*/,
	size_t /*hostlen*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_krbhst_free (
	krb5_context /*context*/,
	krb5_krbhst_handle /*handle*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_get_addrinfo (
	krb5_context /*context*/,
	krb5_krbhst_info */*host*/,
	struct addrinfo **/*ai*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_init (
	krb5_context /*context*/,
	const char */*realm*/,
	unsigned int /*type*/,
	krb5_krbhst_handle */*handle*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_init_flags (
	krb5_context /*context*/,
	const char */*realm*/,
	unsigned int /*type*/,
	int /*flags*/,
	krb5_krbhst_handle */*handle*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_next (
	krb5_context /*context*/,
	krb5_krbhst_handle /*handle*/,
	krb5_krbhst_info **/*host*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_krbhst_next_as_string (
	krb5_context /*context*/,
	krb5_krbhst_handle /*handle*/,
	char */*hostname*/,
	size_t /*hostlen*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_krbhst_reset (
	krb5_context /*context*/,
	krb5_krbhst_handle /*handle*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_add_entry (
	krb5_context /*context*/,
	krb5_keytab /*id*/,
	krb5_keytab_entry */*entry*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_close (
	krb5_context /*context*/,
	krb5_keytab /*id*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_kt_compare (
	krb5_context /*context*/,
	krb5_keytab_entry */*entry*/,
	krb5_const_principal /*principal*/,
	krb5_kvno /*vno*/,
	krb5_enctype /*enctype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_copy_entry_contents (
	krb5_context /*context*/,
	const krb5_keytab_entry */*in*/,
	krb5_keytab_entry */*out*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_default (
	krb5_context /*context*/,
	krb5_keytab */*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_default_modify_name (
	krb5_context /*context*/,
	char */*name*/,
	size_t /*namesize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_default_name (
	krb5_context /*context*/,
	char */*name*/,
	size_t /*namesize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_destroy (
	krb5_context /*context*/,
	krb5_keytab /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_end_seq_get (
	krb5_context /*context*/,
	krb5_keytab /*id*/,
	krb5_kt_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_free_entry (
	krb5_context /*context*/,
	krb5_keytab_entry */*entry*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_entry (
	krb5_context /*context*/,
	krb5_keytab /*id*/,
	krb5_const_principal /*principal*/,
	krb5_kvno /*kvno*/,
	krb5_enctype /*enctype*/,
	krb5_keytab_entry */*entry*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_full_name (
	krb5_context /*context*/,
	krb5_keytab /*keytab*/,
	char **/*str*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_name (
	krb5_context /*context*/,
	krb5_keytab /*keytab*/,
	char */*name*/,
	size_t /*namesize*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_get_type (
	krb5_context /*context*/,
	krb5_keytab /*keytab*/,
	char */*prefix*/,
	size_t /*prefixsize*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_kt_have_content (
	krb5_context /*context*/,
	krb5_keytab /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_next_entry (
	krb5_context /*context*/,
	krb5_keytab /*id*/,
	krb5_keytab_entry */*entry*/,
	krb5_kt_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_read_service_key (
	krb5_context /*context*/,
	krb5_pointer /*keyprocarg*/,
	krb5_principal /*principal*/,
	krb5_kvno /*vno*/,
	krb5_enctype /*enctype*/,
	krb5_keyblock **/*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_register (
	krb5_context /*context*/,
	const krb5_kt_ops */*ops*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_remove_entry (
	krb5_context /*context*/,
	krb5_keytab /*id*/,
	krb5_keytab_entry */*entry*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_resolve (
	krb5_context /*context*/,
	const char */*name*/,
	krb5_keytab */*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_kt_start_seq_get (
	krb5_context /*context*/,
	krb5_keytab /*id*/,
	krb5_kt_cursor */*cursor*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_kuserok (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	const char */*luser*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_log (
	krb5_context /*context*/,
	krb5_log_facility */*fac*/,
	int /*level*/,
	const char */*fmt*/,
	...)
     __attribute__((format (printf, 4, 5)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_log_msg (
	krb5_context /*context*/,
	krb5_log_facility */*fac*/,
	int /*level*/,
	char **/*reply*/,
	const char */*fmt*/,
	...)
     __attribute__((format (printf, 5, 6)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_make_addrport (
	krb5_context /*context*/,
	krb5_address **/*res*/,
	const krb5_address */*addr*/,
	int16_t /*port*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_make_principal (
	krb5_context /*context*/,
	krb5_principal */*principal*/,
	krb5_const_realm /*realm*/,
	...);

KRB5_LIB_FUNCTION size_t KRB5_LIB_CALL
krb5_max_sockaddr_size (void);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_error (
	krb5_context /*context*/,
	krb5_error_code /*error_code*/,
	const char */*e_text*/,
	const krb5_data */*e_data*/,
	const krb5_principal /*client*/,
	const krb5_principal /*server*/,
	time_t */*client_time*/,
	int */*client_usec*/,
	krb5_data */*reply*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_priv (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	const krb5_data */*userdata*/,
	krb5_data */*outbuf*/,
	krb5_replay_data */*outdata*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_rep (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_data */*outbuf*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_req (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	const krb5_flags /*ap_req_options*/,
	const char */*service*/,
	const char */*hostname*/,
	krb5_data */*in_data*/,
	krb5_ccache /*ccache*/,
	krb5_data */*outbuf*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_req_exact (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	const krb5_flags /*ap_req_options*/,
	const krb5_principal /*server*/,
	krb5_data */*in_data*/,
	krb5_ccache /*ccache*/,
	krb5_data */*outbuf*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_req_extended (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	const krb5_flags /*ap_req_options*/,
	krb5_data */*in_data*/,
	krb5_creds */*in_creds*/,
	krb5_data */*outbuf*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_safe (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	const krb5_data */*userdata*/,
	krb5_data */*outbuf*/,
	krb5_replay_data */*outdata*/);

KRB5_LIB_FUNCTION krb5_ssize_t KRB5_LIB_CALL
krb5_net_read (
	krb5_context /*context*/,
	void */*p_fd*/,
	void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_ssize_t KRB5_LIB_CALL
krb5_net_write (
	krb5_context /*context*/,
	void */*p_fd*/,
	const void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_ssize_t KRB5_LIB_CALL
krb5_net_write_block (
	krb5_context /*context*/,
	void */*p_fd*/,
	const void */*buf*/,
	size_t /*len*/,
	time_t /*timeout*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_alloc (
	krb5_context /*context*/,
	krb5_ntlm */*ntlm*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_free (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_challange (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	krb5_data */*challange*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_flags (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	uint32_t */*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_opaque (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	krb5_data */*opaque*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_targetinfo (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_get_targetname (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	char **/*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_init_request (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	krb5_realm /*realm*/,
	krb5_ccache /*ccache*/,
	uint32_t /*flags*/,
	const char */*hostname*/,
	const char */*domainname*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_rep_get_sessionkey (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_ntlm_rep_get_status (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_flags (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	uint32_t /*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_lm (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	void */*hash*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_ntlm (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	void */*hash*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_opaque (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	krb5_data */*opaque*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_session (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	void */*sessionkey*/,
	size_t /*length*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_targetname (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	const char */*targetname*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_req_set_username (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	const char */*username*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ntlm_request (
	krb5_context /*context*/,
	krb5_ntlm /*ntlm*/,
	krb5_realm /*realm*/,
	krb5_ccache /*ccache*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_openlog (
	krb5_context /*context*/,
	const char */*program*/,
	krb5_log_facility **/*fac*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_add_buffer (
	krb5_context /*context*/,
	krb5_pac /*p*/,
	uint32_t /*type*/,
	const krb5_data */*data*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_pac_free (
	krb5_context /*context*/,
	krb5_pac /*pac*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_get_buffer (
	krb5_context /*context*/,
	krb5_pac /*p*/,
	uint32_t /*type*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_get_types (
	krb5_context /*context*/,
	krb5_pac /*p*/,
	size_t */*len*/,
	uint32_t **/*types*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_init (
	krb5_context /*context*/,
	krb5_pac */*pac*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_parse (
	krb5_context /*context*/,
	const void */*ptr*/,
	size_t /*len*/,
	krb5_pac */*pac*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_verify (
	krb5_context /*context*/,
	const krb5_pac /*pac*/,
	time_t /*authtime*/,
	krb5_const_principal /*principal*/,
	const krb5_keyblock */*server*/,
	const krb5_keyblock */*privsvr*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_padata_add (
	krb5_context /*context*/,
	METHOD_DATA */*md*/,
	int /*type*/,
	void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_address (
	krb5_context /*context*/,
	const char */*string*/,
	krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_name (
	krb5_context /*context*/,
	const char */*name*/,
	krb5_principal */*principal*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_name_flags (
	krb5_context /*context*/,
	const char */*name*/,
	int /*flags*/,
	krb5_principal */*principal*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_nametype (
	krb5_context /*context*/,
	const char */*str*/,
	int32_t */*nametype*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_passwd_result_to_string (
	krb5_context /*context*/,
	int /*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_CALLCONV
krb5_password_key_proc (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	krb5_salt /*salt*/,
	krb5_const_pointer /*keyseed*/,
	krb5_keyblock **/*key*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pk_enterprise_cert (
	krb5_context /*context*/,
	const char */*user_id*/,
	krb5_const_realm /*realm*/,
	krb5_principal */*principal*/,
	struct hx509_certs_data **/*res*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_plugin_register (
	krb5_context /*context*/,
	enum krb5_plugin_type /*type*/,
	const char */*name*/,
	void */*symbol*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_prepend_config_files (
	const char */*filelist*/,
	char **/*pq*/,
	char ***/*ret_pp*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_prepend_config_files_default (
	const char */*filelist*/,
	char ***/*pfilenames*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_prepend_error_message (
	krb5_context /*context*/,
	krb5_error_code /*ret*/,
	const char */*fmt*/,
	...)
     __attribute__ ((format (printf, 3, 4)));

KRB5_LIB_FUNCTION krb5_realm * KRB5_LIB_CALL
krb5_princ_realm (
	krb5_context /*context*/,
	krb5_principal /*principal*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_princ_set_realm (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	krb5_realm */*realm*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_compare (
	krb5_context /*context*/,
	krb5_const_principal /*princ1*/,
	krb5_const_principal /*princ2*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_compare_any_realm (
	krb5_context /*context*/,
	krb5_const_principal /*princ1*/,
	krb5_const_principal /*princ2*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_principal_get_comp_string (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	unsigned int /*component*/);

KRB5_LIB_FUNCTION unsigned int KRB5_LIB_CALL
krb5_principal_get_num_comp (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_principal_get_realm (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_principal_get_type (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_is_krbtgt (
	krb5_context /*context*/,
	krb5_const_principal /*p*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_principal_match (
	krb5_context /*context*/,
	krb5_const_principal /*princ*/,
	krb5_const_principal /*pattern*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_principal_set_realm (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	krb5_const_realm /*realm*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_principal_set_type (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	int /*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_print_address (
	const krb5_address */*addr*/,
	char */*str*/,
	size_t /*len*/,
	size_t */*ret_len*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_program_setup (
	krb5_context */*context*/,
	int /*argc*/,
	char **/*argv*/,
	struct getargs */*args*/,
	int /*num_args*/,
	void (KRB5_LIB_CALL *usage)(int, struct getargs*, int));

KRB5_LIB_FUNCTION int KRB5_CALLCONV
krb5_prompter_posix (
	krb5_context /*context*/,
	void */*data*/,
	const char */*name*/,
	const char */*banner*/,
	int /*num_prompts*/,
	krb5_prompt prompts[]);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_random_to_key (
	krb5_context /*context*/,
	krb5_enctype /*type*/,
	const void */*data*/,
	size_t /*size*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_close (
	krb5_context /*context*/,
	krb5_rcache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_default (
	krb5_context /*context*/,
	krb5_rcache */*id*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_default_name (krb5_context /*context*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_default_type (krb5_context /*context*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_destroy (
	krb5_context /*context*/,
	krb5_rcache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_expunge (
	krb5_context /*context*/,
	krb5_rcache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_get_lifespan (
	krb5_context /*context*/,
	krb5_rcache /*id*/,
	krb5_deltat */*auth_lifespan*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_get_name (
	krb5_context /*context*/,
	krb5_rcache /*id*/);

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_get_type (
	krb5_context /*context*/,
	krb5_rcache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_initialize (
	krb5_context /*context*/,
	krb5_rcache /*id*/,
	krb5_deltat /*auth_lifespan*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_recover (
	krb5_context /*context*/,
	krb5_rcache /*id*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_resolve (
	krb5_context /*context*/,
	krb5_rcache /*id*/,
	const char */*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_resolve_full (
	krb5_context /*context*/,
	krb5_rcache */*id*/,
	const char */*string_name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_resolve_type (
	krb5_context /*context*/,
	krb5_rcache */*id*/,
	const char */*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_store (
	krb5_context /*context*/,
	krb5_rcache /*id*/,
	krb5_donot_replay */*rep*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_cred (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_data */*in_data*/,
	krb5_creds ***/*ret_creds*/,
	krb5_replay_data */*outdata*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_cred2 (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	krb5_ccache /*ccache*/,
	krb5_data */*in_data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_error (
	krb5_context /*context*/,
	const krb5_data */*msg*/,
	KRB_ERROR */*result*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_priv (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	const krb5_data */*inbuf*/,
	krb5_data */*outbuf*/,
	krb5_replay_data */*outdata*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_rep (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	const krb5_data */*inbuf*/,
	krb5_ap_rep_enc_part **/*repl*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	const krb5_data */*inbuf*/,
	krb5_const_principal /*server*/,
	krb5_keytab /*keytab*/,
	krb5_flags */*ap_req_options*/,
	krb5_ticket **/*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_ctx (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	const krb5_data */*inbuf*/,
	krb5_const_principal /*server*/,
	krb5_rd_req_in_ctx /*inctx*/,
	krb5_rd_req_out_ctx */*outctx*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_ctx_alloc (
	krb5_context /*context*/,
	krb5_rd_req_in_ctx */*ctx*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_rd_req_in_ctx_free (
	krb5_context /*context*/,
	krb5_rd_req_in_ctx /*ctx*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_set_keyblock (
	krb5_context /*context*/,
	krb5_rd_req_in_ctx /*in*/,
	krb5_keyblock */*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_set_keytab (
	krb5_context /*context*/,
	krb5_rd_req_in_ctx /*in*/,
	krb5_keytab /*keytab*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_set_pac_check (
	krb5_context /*context*/,
	krb5_rd_req_in_ctx /*in*/,
	krb5_boolean /*flag*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_rd_req_out_ctx_free (
	krb5_context /*context*/,
	krb5_rd_req_out_ctx /*ctx*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_ap_req_options (
	krb5_context /*context*/,
	krb5_rd_req_out_ctx /*out*/,
	krb5_flags */*ap_req_options*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_keyblock (
	krb5_context /*context*/,
	krb5_rd_req_out_ctx /*out*/,
	krb5_keyblock **/*keyblock*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_server (
	krb5_context /*context*/,
	krb5_rd_req_out_ctx /*out*/,
	krb5_principal */*principal*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_ticket (
	krb5_context /*context*/,
	krb5_rd_req_out_ctx /*out*/,
	krb5_ticket **/*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_with_keyblock (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	const krb5_data */*inbuf*/,
	krb5_const_principal /*server*/,
	krb5_keyblock */*keyblock*/,
	krb5_flags */*ap_req_options*/,
	krb5_ticket **/*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_safe (
	krb5_context /*context*/,
	krb5_auth_context /*auth_context*/,
	const krb5_data */*inbuf*/,
	krb5_data */*outbuf*/,
	krb5_replay_data */*outdata*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_read_message (
	krb5_context /*context*/,
	krb5_pointer /*p_fd*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_read_priv_message (
	krb5_context /*context*/,
	krb5_auth_context /*ac*/,
	krb5_pointer /*p_fd*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_read_safe_message (
	krb5_context /*context*/,
	krb5_auth_context /*ac*/,
	krb5_pointer /*p_fd*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_realm_compare (
	krb5_context /*context*/,
	krb5_const_principal /*princ1*/,
	krb5_const_principal /*princ2*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_recvauth (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	krb5_pointer /*p_fd*/,
	const char */*appl_version*/,
	krb5_principal /*server*/,
	int32_t /*flags*/,
	krb5_keytab /*keytab*/,
	krb5_ticket **/*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_recvauth_match_version (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	krb5_pointer /*p_fd*/,
	krb5_boolean (*/*match_appl_version*/)(const void *, const char*),
	const void */*match_data*/,
	krb5_principal /*server*/,
	int32_t /*flags*/,
	krb5_keytab /*keytab*/,
	krb5_ticket **/*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_address (
	krb5_storage */*sp*/,
	krb5_address */*adr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_addrs (
	krb5_storage */*sp*/,
	krb5_addresses */*adr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_authdata (
	krb5_storage */*sp*/,
	krb5_authdata */*auth*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_creds (
	krb5_storage */*sp*/,
	krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_creds_tag (
	krb5_storage */*sp*/,
	krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_data (
	krb5_storage */*sp*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_int16 (
	krb5_storage */*sp*/,
	int16_t */*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_int32 (
	krb5_storage */*sp*/,
	int32_t */*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_int8 (
	krb5_storage */*sp*/,
	int8_t */*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_keyblock (
	krb5_storage */*sp*/,
	krb5_keyblock */*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_principal (
	krb5_storage */*sp*/,
	krb5_principal */*princ*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_string (
	krb5_storage */*sp*/,
	char **/*string*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_stringnl (
	krb5_storage */*sp*/,
	char **/*string*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_stringz (
	krb5_storage */*sp*/,
	char **/*string*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_times (
	krb5_storage */*sp*/,
	krb5_times */*times*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_uint16 (
	krb5_storage */*sp*/,
	uint16_t */*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_uint32 (
	krb5_storage */*sp*/,
	uint32_t */*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ret_uint8 (
	krb5_storage */*sp*/,
	uint8_t */*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_salttype_to_string (
	krb5_context /*context*/,
	krb5_enctype /*etype*/,
	krb5_salttype /*stype*/,
	char **/*string*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sendauth (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	krb5_pointer /*p_fd*/,
	const char */*appl_version*/,
	krb5_principal /*client*/,
	krb5_principal /*server*/,
	krb5_flags /*ap_req_options*/,
	krb5_data */*in_data*/,
	krb5_creds */*in_creds*/,
	krb5_ccache /*ccache*/,
	krb5_error **/*ret_error*/,
	krb5_ap_rep_enc_part **/*rep_result*/,
	krb5_creds **/*out_creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sendto (
	krb5_context /*context*/,
	const krb5_data */*send_data*/,
	krb5_krbhst_handle /*handle*/,
	krb5_data */*receive*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sendto_context (
	krb5_context /*context*/,
	krb5_sendto_ctx /*ctx*/,
	const krb5_data */*send_data*/,
	const krb5_realm /*realm*/,
	krb5_data */*receive*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_sendto_ctx_add_flags (
	krb5_sendto_ctx /*ctx*/,
	int /*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sendto_ctx_alloc (
	krb5_context /*context*/,
	krb5_sendto_ctx */*ctx*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_sendto_ctx_free (
	krb5_context /*context*/,
	krb5_sendto_ctx /*ctx*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_sendto_ctx_get_flags (krb5_sendto_ctx /*ctx*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_sendto_ctx_set_func (
	krb5_sendto_ctx /*ctx*/,
	krb5_sendto_ctx_func /*func*/,
	void */*data*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_sendto_ctx_set_type (
	krb5_sendto_ctx /*ctx*/,
	int /*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sendto_kdc (
	krb5_context /*context*/,
	const krb5_data */*send_data*/,
	const krb5_realm */*realm*/,
	krb5_data */*receive*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sendto_kdc_flags (
	krb5_context /*context*/,
	const krb5_data */*send_data*/,
	const krb5_realm */*realm*/,
	krb5_data */*receive*/,
	int /*flags*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_config_files (
	krb5_context /*context*/,
	char **/*filenames*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_default_in_tkt_etypes (
	krb5_context /*context*/,
	const krb5_enctype */*etypes*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_default_realm (
	krb5_context /*context*/,
	const char */*realm*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_set_dns_canonicalize_hostname (
	krb5_context /*context*/,
	krb5_boolean /*flag*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_set_error_message (
	krb5_context /*context*/,
	krb5_error_code /*ret*/,
	const char */*fmt*/,
	...)
     __attribute__ ((format (printf, 3, 4)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_error_string (
	krb5_context /*context*/,
	const char */*fmt*/,
	...)
     __attribute__((format (printf, 2, 3))) KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_extra_addresses (
	krb5_context /*context*/,
	const krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_fcache_version (
	krb5_context /*context*/,
	int /*version*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_set_home_dir_access (
	krb5_context /*context*/,
	krb5_boolean /*allow*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_ignore_addresses (
	krb5_context /*context*/,
	const krb5_addresses */*addresses*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_kdc_sec_offset (
	krb5_context /*context*/,
	int32_t /*sec*/,
	int32_t /*usec*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_set_max_time_skew (
	krb5_context /*context*/,
	time_t /*t*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_password (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	const char */*newpw*/,
	krb5_principal /*targprinc*/,
	int */*result_code*/,
	krb5_data */*result_code_string*/,
	krb5_data */*result_string*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_password_using_ccache (
	krb5_context /*context*/,
	krb5_ccache /*ccache*/,
	const char */*newpw*/,
	krb5_principal /*targprinc*/,
	int */*result_code*/,
	krb5_data */*result_code_string*/,
	krb5_data */*result_string*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_real_time (
	krb5_context /*context*/,
	krb5_timestamp /*sec*/,
	int32_t /*usec*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_send_to_kdc_func (
	krb5_context /*context*/,
	krb5_send_to_kdc_func /*func*/,
	void */*data*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_set_use_admin_kdc (
	krb5_context /*context*/,
	krb5_boolean /*flag*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_warn_dest (
	krb5_context /*context*/,
	krb5_log_facility */*fac*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sname_to_principal (
	krb5_context /*context*/,
	const char */*hostname*/,
	const char */*sname*/,
	int32_t /*type*/,
	krb5_principal */*ret_princ*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sock_to_principal (
	krb5_context /*context*/,
	int /*sock*/,
	const char */*sname*/,
	int32_t /*type*/,
	krb5_principal */*ret_princ*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sockaddr2address (
	krb5_context /*context*/,
	const struct sockaddr */*sa*/,
	krb5_address */*addr*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sockaddr2port (
	krb5_context /*context*/,
	const struct sockaddr */*sa*/,
	int16_t */*port*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_sockaddr_is_loopback (const struct sockaddr */*sa*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_sockaddr_uninteresting (const struct sockaddr */*sa*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_std_usage (
	int /*code*/,
	struct getargs */*args*/,
	int /*num_args*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_storage_clear_flags (
	krb5_storage */*sp*/,
	krb5_flags /*flags*/);

KRB5_LIB_FUNCTION krb5_storage * KRB5_LIB_CALL
krb5_storage_emem (void);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_storage_free (krb5_storage */*sp*/);

KRB5_LIB_FUNCTION krb5_storage * KRB5_LIB_CALL
krb5_storage_from_data (krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_storage * KRB5_LIB_CALL
krb5_storage_from_fd (krb5_socket_t /*fd_in*/);

KRB5_LIB_FUNCTION krb5_storage * KRB5_LIB_CALL
krb5_storage_from_mem (
	void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_storage * KRB5_LIB_CALL
krb5_storage_from_readonly_mem (
	const void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_flags KRB5_LIB_CALL
krb5_storage_get_byteorder (krb5_storage */*sp*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_storage_get_eof_code (krb5_storage */*sp*/);

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_storage_is_flags (
	krb5_storage */*sp*/,
	krb5_flags /*flags*/);

KRB5_LIB_FUNCTION krb5_ssize_t KRB5_LIB_CALL
krb5_storage_read (
	krb5_storage */*sp*/,
	void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION off_t KRB5_LIB_CALL
krb5_storage_seek (
	krb5_storage */*sp*/,
	off_t /*offset*/,
	int /*whence*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_storage_set_byteorder (
	krb5_storage */*sp*/,
	krb5_flags /*byteorder*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_storage_set_eof_code (
	krb5_storage */*sp*/,
	int /*code*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_storage_set_flags (
	krb5_storage */*sp*/,
	krb5_flags /*flags*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_storage_set_max_alloc (
	krb5_storage */*sp*/,
	size_t /*size*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_storage_to_data (
	krb5_storage */*sp*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_storage_truncate (
	krb5_storage */*sp*/,
	off_t /*offset*/);

KRB5_LIB_FUNCTION krb5_ssize_t KRB5_LIB_CALL
krb5_storage_write (
	krb5_storage */*sp*/,
	const void */*buf*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_address (
	krb5_storage */*sp*/,
	krb5_address /*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_addrs (
	krb5_storage */*sp*/,
	krb5_addresses /*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_authdata (
	krb5_storage */*sp*/,
	krb5_authdata /*auth*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_creds (
	krb5_storage */*sp*/,
	krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_creds_tag (
	krb5_storage */*sp*/,
	krb5_creds */*creds*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_data (
	krb5_storage */*sp*/,
	krb5_data /*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_int16 (
	krb5_storage */*sp*/,
	int16_t /*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_int32 (
	krb5_storage */*sp*/,
	int32_t /*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_int8 (
	krb5_storage */*sp*/,
	int8_t /*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_keyblock (
	krb5_storage */*sp*/,
	krb5_keyblock /*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_principal (
	krb5_storage */*sp*/,
	krb5_const_principal /*p*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_string (
	krb5_storage */*sp*/,
	const char */*s*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_stringnl (
	krb5_storage */*sp*/,
	const char */*s*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_stringz (
	krb5_storage */*sp*/,
	const char */*s*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_times (
	krb5_storage */*sp*/,
	krb5_times /*times*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_uint16 (
	krb5_storage */*sp*/,
	uint16_t /*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_uint32 (
	krb5_storage */*sp*/,
	uint32_t /*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_store_uint8 (
	krb5_storage */*sp*/,
	uint8_t /*value*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_deltat (
	const char */*string*/,
	krb5_deltat */*deltat*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_enctype (
	krb5_context /*context*/,
	const char */*string*/,
	krb5_enctype */*etype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_key (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	const char */*password*/,
	krb5_principal /*principal*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_key_data (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	krb5_data /*password*/,
	krb5_principal /*principal*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_key_data_salt (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	krb5_data /*password*/,
	krb5_salt /*salt*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_key_data_salt_opaque (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	krb5_data /*password*/,
	krb5_salt /*salt*/,
	krb5_data /*opaque*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_key_derived (
	krb5_context /*context*/,
	const void */*str*/,
	size_t /*len*/,
	krb5_enctype /*etype*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_key_salt (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	const char */*password*/,
	krb5_salt /*salt*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_key_salt_opaque (
	krb5_context /*context*/,
	krb5_enctype /*enctype*/,
	const char */*password*/,
	krb5_salt /*salt*/,
	krb5_data /*opaque*/,
	krb5_keyblock */*key*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_keytype (
	krb5_context /*context*/,
	const char */*string*/,
	krb5_keytype */*keytype*/)
     KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_salttype (
	krb5_context /*context*/,
	krb5_enctype /*etype*/,
	const char */*string*/,
	krb5_salttype */*salttype*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ticket_get_authorization_data_type (
	krb5_context /*context*/,
	krb5_ticket */*ticket*/,
	int /*type*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ticket_get_client (
	krb5_context /*context*/,
	const krb5_ticket */*ticket*/,
	krb5_principal */*client*/);

KRB5_LIB_FUNCTION time_t KRB5_LIB_CALL
krb5_ticket_get_endtime (
	krb5_context /*context*/,
	const krb5_ticket */*ticket*/);

KRB5_LIB_FUNCTION unsigned long KRB5_LIB_CALL
krb5_ticket_get_flags (
	krb5_context /*context*/,
	const krb5_ticket */*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ticket_get_server (
	krb5_context /*context*/,
	const krb5_ticket */*ticket*/,
	krb5_principal */*server*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_timeofday (
	krb5_context /*context*/,
	krb5_timestamp */*timeret*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	char **/*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_fixed (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	char */*name*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_fixed_flags (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	int /*flags*/,
	char */*name*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_fixed_short (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	char */*name*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_flags (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	int /*flags*/,
	char **/*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_unparse_name_short (
	krb5_context /*context*/,
	krb5_const_principal /*principal*/,
	char **/*name*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_us_timeofday (
	krb5_context /*context*/,
	krb5_timestamp */*sec*/,
	int32_t */*usec*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vabort (
	krb5_context /*context*/,
	krb5_error_code /*code*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__ ((noreturn, format (printf, 3, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vabortx (
	krb5_context /*context*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__ ((noreturn, format (printf, 2, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_ap_req (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	krb5_ap_req */*ap_req*/,
	krb5_const_principal /*server*/,
	krb5_keyblock */*keyblock*/,
	krb5_flags /*flags*/,
	krb5_flags */*ap_req_options*/,
	krb5_ticket **/*ticket*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_ap_req2 (
	krb5_context /*context*/,
	krb5_auth_context */*auth_context*/,
	krb5_ap_req */*ap_req*/,
	krb5_const_principal /*server*/,
	krb5_keyblock */*keyblock*/,
	krb5_flags /*flags*/,
	krb5_flags */*ap_req_options*/,
	krb5_ticket **/*ticket*/,
	krb5_key_usage /*usage*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_authenticator_checksum (
	krb5_context /*context*/,
	krb5_auth_context /*ac*/,
	void */*data*/,
	size_t /*len*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_checksum (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	krb5_key_usage /*usage*/,
	void */*data*/,
	size_t /*len*/,
	Checksum */*cksum*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_checksum_iov (
	krb5_context /*context*/,
	krb5_crypto /*crypto*/,
	unsigned /*usage*/,
	krb5_crypto_iov */*data*/,
	unsigned int /*num_data*/,
	krb5_cksumtype */*type*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_init_creds (
	krb5_context /*context*/,
	krb5_creds */*creds*/,
	krb5_principal /*ap_req_server*/,
	krb5_keytab /*ap_req_keytab*/,
	krb5_ccache */*ccache*/,
	krb5_verify_init_creds_opt */*options*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_init_creds_opt_init (krb5_verify_init_creds_opt */*options*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_init_creds_opt_set_ap_req_nofail (
	krb5_verify_init_creds_opt */*options*/,
	int /*ap_req_nofail*/);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_verify_opt_alloc (
	krb5_context /*context*/,
	krb5_verify_opt **/*opt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_opt_free (krb5_verify_opt */*opt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_opt_init (krb5_verify_opt */*opt*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_opt_set_ccache (
	krb5_verify_opt */*opt*/,
	krb5_ccache /*ccache*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_opt_set_flags (
	krb5_verify_opt */*opt*/,
	unsigned int /*flags*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_opt_set_keytab (
	krb5_verify_opt */*opt*/,
	krb5_keytab /*keytab*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_opt_set_secure (
	krb5_verify_opt */*opt*/,
	krb5_boolean /*secure*/);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_opt_set_service (
	krb5_verify_opt */*opt*/,
	const char */*service*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_user (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	krb5_ccache /*ccache*/,
	const char */*password*/,
	krb5_boolean /*secure*/,
	const char */*service*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_user_lrealm (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	krb5_ccache /*ccache*/,
	const char */*password*/,
	krb5_boolean /*secure*/,
	const char */*service*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_user_opt (
	krb5_context /*context*/,
	krb5_principal /*principal*/,
	const char */*password*/,
	krb5_verify_opt */*opt*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verr (
	krb5_context /*context*/,
	int /*eval*/,
	krb5_error_code /*code*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__ ((noreturn, format (printf, 4, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verrx (
	krb5_context /*context*/,
	int /*eval*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__ ((noreturn, format (printf, 3, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vlog (
	krb5_context /*context*/,
	krb5_log_facility */*fac*/,
	int /*level*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__((format (printf, 4, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vlog_msg (
	krb5_context /*context*/,
	krb5_log_facility */*fac*/,
	char **/*reply*/,
	int /*level*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__((format (printf, 5, 0)));

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_vprepend_error_message (
	krb5_context /*context*/,
	krb5_error_code /*ret*/,
	const char */*fmt*/,
	va_list /*args*/)
     __attribute__ ((format (printf, 3, 0)));

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_vset_error_message (
	krb5_context /*context*/,
	krb5_error_code /*ret*/,
	const char */*fmt*/,
	va_list /*args*/)
     __attribute__ ((format (printf, 3, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vset_error_string (
	krb5_context /*context*/,
	const char */*fmt*/,
	va_list /*args*/)
     __attribute__ ((format (printf, 2, 0))) KRB5_DEPRECATED_FUNCTION("Use X instead");

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vwarn (
	krb5_context /*context*/,
	krb5_error_code /*code*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__ ((format (printf, 3, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vwarnx (
	krb5_context /*context*/,
	const char */*fmt*/,
	va_list /*ap*/)
     __attribute__ ((format (printf, 2, 0)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_warn (
	krb5_context /*context*/,
	krb5_error_code /*code*/,
	const char */*fmt*/,
	...)
     __attribute__ ((format (printf, 3, 4)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_warnx (
	krb5_context /*context*/,
	const char */*fmt*/,
	...)
     __attribute__ ((format (printf, 2, 3)));

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_write_message (
	krb5_context /*context*/,
	krb5_pointer /*p_fd*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_write_priv_message (
	krb5_context /*context*/,
	krb5_auth_context /*ac*/,
	krb5_pointer /*p_fd*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_write_safe_message (
	krb5_context /*context*/,
	krb5_auth_context /*ac*/,
	krb5_pointer /*p_fd*/,
	krb5_data */*data*/);

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_xfree (void */*ptr*/);

#ifdef __cplusplus
}
#endif

#undef KRB5_DEPRECATED_FUNCTION

#endif /* __krb5_protos_h__ */
