/* This is a generated file */
#ifndef __hx509_protos_h__
#define __hx509_protos_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HX509_LIB
#ifndef HX509_LIB_FUNCTION
#if defined(_WIN32)
#define HX509_LIB_FUNCTION __declspec(dllimport)
#define HX509_LIB_CALL __stdcall
#define HX509_LIB_VARIABLE __declspec(dllimport)
#else
#define HX509_LIB_FUNCTION
#define HX509_LIB_CALL
#define HX509_LIB_VARIABLE
#endif
#endif
#endif
void
hx509_bitstring_print (
	const heim_bit_string */*b*/,
	hx509_vprint_func /*func*/,
	void */*ctx*/);

int
hx509_ca_sign (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	hx509_cert /*signer*/,
	hx509_cert */*certificate*/);

int
hx509_ca_sign_self (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	hx509_private_key /*signer*/,
	hx509_cert */*certificate*/);

int
hx509_ca_tbs_add_crl_dp_uri (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const char */*uri*/,
	hx509_name /*issuername*/);

int
hx509_ca_tbs_add_eku (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const heim_oid */*oid*/);

int
hx509_ca_tbs_add_san_hostname (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const char */*dnsname*/);

int
hx509_ca_tbs_add_san_jid (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const char */*jid*/);

int
hx509_ca_tbs_add_san_ms_upn (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const char */*principal*/);

int
hx509_ca_tbs_add_san_otherName (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const heim_oid */*oid*/,
	const heim_octet_string */*os*/);

int
hx509_ca_tbs_add_san_pkinit (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const char */*principal*/);

int
hx509_ca_tbs_add_san_rfc822name (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const char */*rfc822Name*/);

void
hx509_ca_tbs_free (hx509_ca_tbs */*tbs*/);

int
hx509_ca_tbs_init (
	hx509_context /*context*/,
	hx509_ca_tbs */*tbs*/);

int
hx509_ca_tbs_set_ca (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	int /*pathLenConstraint*/);

int
hx509_ca_tbs_set_domaincontroller (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/);

int
hx509_ca_tbs_set_notAfter (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	time_t /*t*/);

int
hx509_ca_tbs_set_notAfter_lifetime (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	time_t /*delta*/);

int
hx509_ca_tbs_set_notBefore (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	time_t /*t*/);

int
hx509_ca_tbs_set_proxy (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	int /*pathLenConstraint*/);

int
hx509_ca_tbs_set_serialnumber (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const heim_integer */*serialNumber*/);

int
hx509_ca_tbs_set_spki (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const SubjectPublicKeyInfo */*spki*/);

int
hx509_ca_tbs_set_subject (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	hx509_name /*subject*/);

int
hx509_ca_tbs_set_template (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	int /*flags*/,
	hx509_cert /*cert*/);

int
hx509_ca_tbs_set_unique (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	const heim_bit_string */*subjectUniqueID*/,
	const heim_bit_string */*issuerUniqueID*/);

int
hx509_ca_tbs_subject_expand (
	hx509_context /*context*/,
	hx509_ca_tbs /*tbs*/,
	hx509_env /*env*/);

const struct units *
hx509_ca_tbs_template_units (void);

int
hx509_cert_binary (
	hx509_context /*context*/,
	hx509_cert /*c*/,
	heim_octet_string */*os*/);

int
hx509_cert_check_eku (
	hx509_context /*context*/,
	hx509_cert /*cert*/,
	const heim_oid */*eku*/,
	int /*allow_any_eku*/);

int
hx509_cert_cmp (
	hx509_cert /*p*/,
	hx509_cert /*q*/);

int
hx509_cert_find_subjectAltName_otherName (
	hx509_context /*context*/,
	hx509_cert /*cert*/,
	const heim_oid */*oid*/,
	hx509_octet_string_list */*list*/);

void
hx509_cert_free (hx509_cert /*cert*/);

int
hx509_cert_get_SPKI (
	hx509_context /*context*/,
	hx509_cert /*p*/,
	SubjectPublicKeyInfo */*spki*/);

int
hx509_cert_get_SPKI_AlgorithmIdentifier (
	hx509_context /*context*/,
	hx509_cert /*p*/,
	AlgorithmIdentifier */*alg*/);

hx509_cert_attribute
hx509_cert_get_attribute (
	hx509_cert /*cert*/,
	const heim_oid */*oid*/);

int
hx509_cert_get_base_subject (
	hx509_context /*context*/,
	hx509_cert /*c*/,
	hx509_name */*name*/);

const char *
hx509_cert_get_friendly_name (hx509_cert /*cert*/);

int
hx509_cert_get_issuer (
	hx509_cert /*p*/,
	hx509_name */*name*/);

int
hx509_cert_get_issuer_unique_id (
	hx509_context /*context*/,
	hx509_cert /*p*/,
	heim_bit_string */*issuer*/);

time_t
hx509_cert_get_notAfter (hx509_cert /*p*/);

time_t
hx509_cert_get_notBefore (hx509_cert /*p*/);

int
hx509_cert_get_serialnumber (
	hx509_cert /*p*/,
	heim_integer */*i*/);

int
hx509_cert_get_subject (
	hx509_cert /*p*/,
	hx509_name */*name*/);

int
hx509_cert_get_subject_unique_id (
	hx509_context /*context*/,
	hx509_cert /*p*/,
	heim_bit_string */*subject*/);

int
hx509_cert_have_private_key (hx509_cert /*p*/);

int
hx509_cert_init (
	hx509_context /*context*/,
	const Certificate */*c*/,
	hx509_cert */*cert*/);

int
hx509_cert_init_data (
	hx509_context /*context*/,
	const void */*ptr*/,
	size_t /*len*/,
	hx509_cert */*cert*/);

int
hx509_cert_keyusage_print (
	hx509_context /*context*/,
	hx509_cert /*c*/,
	char **/*s*/);

int
hx509_cert_public_encrypt (
	hx509_context /*context*/,
	const heim_octet_string */*cleartext*/,
	const hx509_cert /*p*/,
	heim_oid */*encryption_oid*/,
	heim_octet_string */*ciphertext*/);

hx509_cert
hx509_cert_ref (hx509_cert /*cert*/);

int
hx509_cert_set_friendly_name (
	hx509_cert /*cert*/,
	const char */*name*/);

int
hx509_certs_add (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	hx509_cert /*cert*/);

int
hx509_certs_append (
	hx509_context /*context*/,
	hx509_certs /*to*/,
	hx509_lock /*lock*/,
	const char */*name*/);

int
hx509_certs_end_seq (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	hx509_cursor /*cursor*/);

int
hx509_certs_filter (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	const hx509_query */*q*/,
	hx509_certs */*result*/);

int
hx509_certs_find (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	const hx509_query */*q*/,
	hx509_cert */*r*/);

void
hx509_certs_free (hx509_certs */*certs*/);

int
hx509_certs_info (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	int (*/*func*/)(void *, const char *),
	void */*ctx*/);

int
hx509_certs_init (
	hx509_context /*context*/,
	const char */*name*/,
	int /*flags*/,
	hx509_lock /*lock*/,
	hx509_certs */*certs*/);

#ifdef __BLOCKS__
int
hx509_certs_iter (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	int (^func)(hx509_cert));
#endif /* __BLOCKS__ */

int
hx509_certs_iter_f (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	int (*/*func*/)(hx509_context, void *, hx509_cert),
	void */*ctx*/);

int
hx509_certs_merge (
	hx509_context /*context*/,
	hx509_certs /*to*/,
	hx509_certs /*from*/);

int
hx509_certs_next_cert (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	hx509_cursor /*cursor*/,
	hx509_cert */*cert*/);

hx509_certs
hx509_certs_ref (hx509_certs /*certs*/);

int
hx509_certs_start_seq (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	hx509_cursor */*cursor*/);

int
hx509_certs_store (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	int /*flags*/,
	hx509_lock /*lock*/);

int
hx509_ci_print_names (
	hx509_context /*context*/,
	void */*ctx*/,
	hx509_cert /*c*/);

void
hx509_clear_error_string (hx509_context /*context*/);

int
hx509_cms_create_signed (
	hx509_context /*context*/,
	int /*flags*/,
	const heim_oid */*eContentType*/,
	const void */*data*/,
	size_t /*length*/,
	const AlgorithmIdentifier */*digest_alg*/,
	hx509_certs /*certs*/,
	hx509_peer_info /*peer*/,
	hx509_certs /*anchors*/,
	hx509_certs /*pool*/,
	heim_octet_string */*signed_data*/);

int
hx509_cms_create_signed_1 (
	hx509_context /*context*/,
	int /*flags*/,
	const heim_oid */*eContentType*/,
	const void */*data*/,
	size_t /*length*/,
	const AlgorithmIdentifier */*digest_alg*/,
	hx509_cert /*cert*/,
	hx509_peer_info /*peer*/,
	hx509_certs /*anchors*/,
	hx509_certs /*pool*/,
	heim_octet_string */*signed_data*/);

int
hx509_cms_decrypt_encrypted (
	hx509_context /*context*/,
	hx509_lock /*lock*/,
	const void */*data*/,
	size_t /*length*/,
	heim_oid */*contentType*/,
	heim_octet_string */*content*/);

int
hx509_cms_envelope_1 (
	hx509_context /*context*/,
	int /*flags*/,
	hx509_cert /*cert*/,
	const void */*data*/,
	size_t /*length*/,
	const heim_oid */*encryption_type*/,
	const heim_oid */*contentType*/,
	heim_octet_string */*content*/);

int
hx509_cms_unenvelope (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	int /*flags*/,
	const void */*data*/,
	size_t /*length*/,
	const heim_octet_string */*encryptedContent*/,
	time_t /*time_now*/,
	heim_oid */*contentType*/,
	heim_octet_string */*content*/);

int
hx509_cms_unwrap_ContentInfo (
	const heim_octet_string */*in*/,
	heim_oid */*oid*/,
	heim_octet_string */*out*/,
	int */*have_data*/);

int
hx509_cms_verify_signed (
	hx509_context /*context*/,
	hx509_verify_ctx /*ctx*/,
	unsigned int /*flags*/,
	const void */*data*/,
	size_t /*length*/,
	const heim_octet_string */*signedContent*/,
	hx509_certs /*pool*/,
	heim_oid */*contentType*/,
	heim_octet_string */*content*/,
	hx509_certs */*signer_certs*/);

int
hx509_cms_wrap_ContentInfo (
	const heim_oid */*oid*/,
	const heim_octet_string */*buf*/,
	heim_octet_string */*res*/);

void
hx509_context_free (hx509_context */*context*/);

int
hx509_context_init (hx509_context */*context*/);

void
hx509_context_set_missing_revoke (
	hx509_context /*context*/,
	int /*flag*/);

int
hx509_crl_add_revoked_certs (
	hx509_context /*context*/,
	hx509_crl /*crl*/,
	hx509_certs /*certs*/);

int
hx509_crl_alloc (
	hx509_context /*context*/,
	hx509_crl */*crl*/);

void
hx509_crl_free (
	hx509_context /*context*/,
	hx509_crl */*crl*/);

int
hx509_crl_lifetime (
	hx509_context /*context*/,
	hx509_crl /*crl*/,
	int /*delta*/);

int
hx509_crl_sign (
	hx509_context /*context*/,
	hx509_cert /*signer*/,
	hx509_crl /*crl*/,
	heim_octet_string */*os*/);

const AlgorithmIdentifier *
hx509_crypto_aes128_cbc (void);

const AlgorithmIdentifier *
hx509_crypto_aes256_cbc (void);

void
hx509_crypto_allow_weak (hx509_crypto /*crypto*/);

int
hx509_crypto_available (
	hx509_context /*context*/,
	int /*type*/,
	hx509_cert /*source*/,
	AlgorithmIdentifier **/*val*/,
	unsigned int */*plen*/);

int
hx509_crypto_decrypt (
	hx509_crypto /*crypto*/,
	const void */*data*/,
	const size_t /*length*/,
	heim_octet_string */*ivec*/,
	heim_octet_string */*clear*/);

const AlgorithmIdentifier *
hx509_crypto_des_rsdi_ede3_cbc (void);

void
hx509_crypto_destroy (hx509_crypto /*crypto*/);

int
hx509_crypto_encrypt (
	hx509_crypto /*crypto*/,
	const void */*data*/,
	const size_t /*length*/,
	const heim_octet_string */*ivec*/,
	heim_octet_string **/*ciphertext*/);

const heim_oid *
hx509_crypto_enctype_by_name (const char */*name*/);

void
hx509_crypto_free_algs (
	AlgorithmIdentifier */*val*/,
	unsigned int /*len*/);

int
hx509_crypto_get_params (
	hx509_context /*context*/,
	hx509_crypto /*crypto*/,
	const heim_octet_string */*ivec*/,
	heim_octet_string */*param*/);

int
hx509_crypto_init (
	hx509_context /*context*/,
	const char */*provider*/,
	const heim_oid */*enctype*/,
	hx509_crypto */*crypto*/);

const char *
hx509_crypto_provider (hx509_crypto /*crypto*/);

int
hx509_crypto_random_iv (
	hx509_crypto /*crypto*/,
	heim_octet_string */*ivec*/);

int
hx509_crypto_select (
	const hx509_context /*context*/,
	int /*type*/,
	const hx509_private_key /*source*/,
	hx509_peer_info /*peer*/,
	AlgorithmIdentifier */*selected*/);

int
hx509_crypto_set_key_data (
	hx509_crypto /*crypto*/,
	const void */*data*/,
	size_t /*length*/);

int
hx509_crypto_set_key_name (
	hx509_crypto /*crypto*/,
	const char */*name*/);

void
hx509_crypto_set_padding (
	hx509_crypto /*crypto*/,
	int /*padding_type*/);

int
hx509_crypto_set_params (
	hx509_context /*context*/,
	hx509_crypto /*crypto*/,
	const heim_octet_string */*param*/,
	heim_octet_string */*ivec*/);

int
hx509_crypto_set_random_key (
	hx509_crypto /*crypto*/,
	heim_octet_string */*key*/);

int
hx509_env_add (
	hx509_context /*context*/,
	hx509_env */*env*/,
	const char */*key*/,
	const char */*value*/);

int
hx509_env_add_binding (
	hx509_context /*context*/,
	hx509_env */*env*/,
	const char */*key*/,
	hx509_env /*list*/);

const char *
hx509_env_find (
	hx509_context /*context*/,
	hx509_env /*env*/,
	const char */*key*/);

hx509_env
hx509_env_find_binding (
	hx509_context /*context*/,
	hx509_env /*env*/,
	const char */*key*/);

void
hx509_env_free (hx509_env */*env*/);

const char *
hx509_env_lfind (
	hx509_context /*context*/,
	hx509_env /*env*/,
	const char */*key*/,
	size_t /*len*/);

void
hx509_err (
	hx509_context /*context*/,
	int /*exit_code*/,
	int /*error_code*/,
	const char */*fmt*/,
	...);

hx509_private_key_ops *
hx509_find_private_alg (const heim_oid */*oid*/);

void
hx509_free_error_string (char */*str*/);

void
hx509_free_octet_string_list (hx509_octet_string_list */*list*/);

int
hx509_general_name_unparse (
	GeneralName */*name*/,
	char **/*str*/);

char *
hx509_get_error_string (
	hx509_context /*context*/,
	int /*error_code*/);

int
hx509_get_one_cert (
	hx509_context /*context*/,
	hx509_certs /*certs*/,
	hx509_cert */*c*/);

int
hx509_lock_add_cert (
	hx509_context /*context*/,
	hx509_lock /*lock*/,
	hx509_cert /*cert*/);

int
hx509_lock_add_certs (
	hx509_context /*context*/,
	hx509_lock /*lock*/,
	hx509_certs /*certs*/);

int
hx509_lock_add_password (
	hx509_lock /*lock*/,
	const char */*password*/);

int
hx509_lock_command_string (
	hx509_lock /*lock*/,
	const char */*string*/);

void
hx509_lock_free (hx509_lock /*lock*/);

int
hx509_lock_init (
	hx509_context /*context*/,
	hx509_lock */*lock*/);

int
hx509_lock_prompt (
	hx509_lock /*lock*/,
	hx509_prompt */*prompt*/);

void
hx509_lock_reset_certs (
	hx509_context /*context*/,
	hx509_lock /*lock*/);

void
hx509_lock_reset_passwords (hx509_lock /*lock*/);

void
hx509_lock_reset_promper (hx509_lock /*lock*/);

int
hx509_lock_set_prompter (
	hx509_lock /*lock*/,
	hx509_prompter_fct /*prompt*/,
	void */*data*/);

int
hx509_name_binary (
	const hx509_name /*name*/,
	heim_octet_string */*os*/);

int
hx509_name_cmp (
	hx509_name /*n1*/,
	hx509_name /*n2*/);

int
hx509_name_copy (
	hx509_context /*context*/,
	const hx509_name /*from*/,
	hx509_name */*to*/);

int
hx509_name_expand (
	hx509_context /*context*/,
	hx509_name /*name*/,
	hx509_env /*env*/);

void
hx509_name_free (hx509_name */*name*/);

int
hx509_name_is_null_p (const hx509_name /*name*/);

int
hx509_name_normalize (
	hx509_context /*context*/,
	hx509_name /*name*/);

int
hx509_name_to_Name (
	const hx509_name /*from*/,
	Name */*to*/);

int
hx509_name_to_string (
	const hx509_name /*name*/,
	char **/*str*/);

int
hx509_ocsp_request (
	hx509_context /*context*/,
	hx509_certs /*reqcerts*/,
	hx509_certs /*pool*/,
	hx509_cert /*signer*/,
	const AlgorithmIdentifier */*digest*/,
	heim_octet_string */*request*/,
	heim_octet_string */*nonce*/);

int
hx509_ocsp_verify (
	hx509_context /*context*/,
	time_t /*now*/,
	hx509_cert /*cert*/,
	int /*flags*/,
	const void */*data*/,
	size_t /*length*/,
	time_t */*expiration*/);

void
hx509_oid_print (
	const heim_oid */*oid*/,
	hx509_vprint_func /*func*/,
	void */*ctx*/);

int
hx509_oid_sprint (
	const heim_oid */*oid*/,
	char **/*str*/);

int
hx509_parse_name (
	hx509_context /*context*/,
	const char */*str*/,
	hx509_name */*name*/);

int
hx509_parse_private_key (
	hx509_context /*context*/,
	const AlgorithmIdentifier */*keyai*/,
	const void */*data*/,
	size_t /*len*/,
	hx509_key_format_t /*format*/,
	hx509_private_key */*private_key*/);

int
hx509_peer_info_add_cms_alg (
	hx509_context /*context*/,
	hx509_peer_info /*peer*/,
	const AlgorithmIdentifier */*val*/);

int
hx509_peer_info_alloc (
	hx509_context /*context*/,
	hx509_peer_info */*peer*/);

void
hx509_peer_info_free (hx509_peer_info /*peer*/);

int
hx509_peer_info_set_cert (
	hx509_peer_info /*peer*/,
	hx509_cert /*cert*/);

int
hx509_peer_info_set_cms_algs (
	hx509_context /*context*/,
	hx509_peer_info /*peer*/,
	const AlgorithmIdentifier */*val*/,
	size_t /*len*/);

int
hx509_pem_add_header (
	hx509_pem_header **/*headers*/,
	const char */*header*/,
	const char */*value*/);

const char *
hx509_pem_find_header (
	const hx509_pem_header */*h*/,
	const char */*header*/);

void
hx509_pem_free_header (hx509_pem_header */*headers*/);

int
hx509_pem_read (
	hx509_context /*context*/,
	FILE */*f*/,
	hx509_pem_read_func /*func*/,
	void */*ctx*/);

int
hx509_pem_write (
	hx509_context /*context*/,
	const char */*type*/,
	hx509_pem_header */*headers*/,
	FILE */*f*/,
	const void */*data*/,
	size_t /*size*/);

int
hx509_print_cert (
	hx509_context /*context*/,
	hx509_cert /*cert*/,
	FILE */*out*/);

void
hx509_print_stdout (
	void */*ctx*/,
	const char */*fmt*/,
	va_list /*va*/);

int
hx509_private_key2SPKI (
	hx509_context /*context*/,
	hx509_private_key /*private_key*/,
	SubjectPublicKeyInfo */*spki*/);

void
hx509_private_key_assign_rsa (
	hx509_private_key /*key*/,
	void */*ptr*/);

int
hx509_private_key_free (hx509_private_key */*key*/);

int
hx509_private_key_init (
	hx509_private_key */*key*/,
	hx509_private_key_ops */*ops*/,
	void */*keydata*/);

int
hx509_private_key_private_decrypt (
	hx509_context /*context*/,
	const heim_octet_string */*ciphertext*/,
	const heim_oid */*encryption_oid*/,
	hx509_private_key /*p*/,
	heim_octet_string */*cleartext*/);

int
hx509_prompt_hidden (hx509_prompt_type /*type*/);

int
hx509_query_alloc (
	hx509_context /*context*/,
	hx509_query **/*q*/);

void
hx509_query_free (
	hx509_context /*context*/,
	hx509_query */*q*/);

int
hx509_query_match_cmp_func (
	hx509_query */*q*/,
	int (*/*func*/)(hx509_context, hx509_cert, void *),
	void */*ctx*/);

int
hx509_query_match_eku (
	hx509_query */*q*/,
	const heim_oid */*eku*/);

int
hx509_query_match_expr (
	hx509_context /*context*/,
	hx509_query */*q*/,
	const char */*expr*/);

int
hx509_query_match_friendly_name (
	hx509_query */*q*/,
	const char */*name*/);

int
hx509_query_match_issuer_serial (
	hx509_query */*q*/,
	const Name */*issuer*/,
	const heim_integer */*serialNumber*/);

void
hx509_query_match_option (
	hx509_query */*q*/,
	hx509_query_option /*option*/);

void
hx509_query_statistic_file (
	hx509_context /*context*/,
	const char */*fn*/);

void
hx509_query_unparse_stats (
	hx509_context /*context*/,
	int /*printtype*/,
	FILE */*out*/);

void
hx509_request_free (hx509_request */*req*/);

int
hx509_request_get_SubjectPublicKeyInfo (
	hx509_context /*context*/,
	hx509_request /*req*/,
	SubjectPublicKeyInfo */*key*/);

int
hx509_request_get_name (
	hx509_context /*context*/,
	hx509_request /*req*/,
	hx509_name */*name*/);

int
hx509_request_init (
	hx509_context /*context*/,
	hx509_request */*req*/);

int
hx509_request_set_SubjectPublicKeyInfo (
	hx509_context /*context*/,
	hx509_request /*req*/,
	const SubjectPublicKeyInfo */*key*/);

int
hx509_request_set_name (
	hx509_context /*context*/,
	hx509_request /*req*/,
	hx509_name /*name*/);

int
hx509_revoke_add_crl (
	hx509_context /*context*/,
	hx509_revoke_ctx /*ctx*/,
	const char */*path*/);

int
hx509_revoke_add_ocsp (
	hx509_context /*context*/,
	hx509_revoke_ctx /*ctx*/,
	const char */*path*/);

void
hx509_revoke_free (hx509_revoke_ctx */*ctx*/);

int
hx509_revoke_init (
	hx509_context /*context*/,
	hx509_revoke_ctx */*ctx*/);

int
hx509_revoke_ocsp_print (
	hx509_context /*context*/,
	const char */*path*/,
	FILE */*out*/);

int
hx509_revoke_verify (
	hx509_context /*context*/,
	hx509_revoke_ctx /*ctx*/,
	hx509_certs /*certs*/,
	time_t /*now*/,
	hx509_cert /*cert*/,
	hx509_cert /*parent_cert*/);

void
hx509_set_error_string (
	hx509_context /*context*/,
	int /*flags*/,
	int /*code*/,
	const char */*fmt*/,
	...);

void
hx509_set_error_stringv (
	hx509_context /*context*/,
	int /*flags*/,
	int /*code*/,
	const char */*fmt*/,
	va_list /*ap*/);

const AlgorithmIdentifier *
hx509_signature_ecPublicKey (void);

const AlgorithmIdentifier *
hx509_signature_ecdsa_with_sha1 (void);

const AlgorithmIdentifier *
hx509_signature_ecdsa_with_sha256 (void);

const AlgorithmIdentifier *
hx509_signature_md5 (void);

const AlgorithmIdentifier *
hx509_signature_rsa (void);

const AlgorithmIdentifier *
hx509_signature_rsa_pkcs1_x509 (void);

const AlgorithmIdentifier *
hx509_signature_rsa_with_md5 (void);

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha1 (void);

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha256 (void);

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha384 (void);

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha512 (void);

const AlgorithmIdentifier *
hx509_signature_sha1 (void);

const AlgorithmIdentifier *
hx509_signature_sha256 (void);

const AlgorithmIdentifier *
hx509_signature_sha384 (void);

const AlgorithmIdentifier *
hx509_signature_sha512 (void);

int
hx509_unparse_der_name (
	const void */*data*/,
	size_t /*length*/,
	char **/*str*/);

int
hx509_validate_cert (
	hx509_context /*context*/,
	hx509_validate_ctx /*ctx*/,
	hx509_cert /*cert*/);

void
hx509_validate_ctx_add_flags (
	hx509_validate_ctx /*ctx*/,
	int /*flags*/);

void
hx509_validate_ctx_free (hx509_validate_ctx /*ctx*/);

int
hx509_validate_ctx_init (
	hx509_context /*context*/,
	hx509_validate_ctx */*ctx*/);

void
hx509_validate_ctx_set_print (
	hx509_validate_ctx /*ctx*/,
	hx509_vprint_func /*func*/,
	void */*c*/);

void
hx509_verify_attach_anchors (
	hx509_verify_ctx /*ctx*/,
	hx509_certs /*set*/);

void
hx509_verify_attach_revoke (
	hx509_verify_ctx /*ctx*/,
	hx509_revoke_ctx /*revoke_ctx*/);

void
hx509_verify_ctx_f_allow_best_before_signature_algs (
	hx509_context /*ctx*/,
	int /*boolean*/);

void
hx509_verify_ctx_f_allow_default_trustanchors (
	hx509_verify_ctx /*ctx*/,
	int /*boolean*/);

void
hx509_verify_destroy_ctx (hx509_verify_ctx /*ctx*/);

int
hx509_verify_hostname (
	hx509_context /*context*/,
	const hx509_cert /*cert*/,
	int /*flags*/,
	hx509_hostname_type /*type*/,
	const char */*hostname*/,
	const struct sockaddr */*sa*/,
	int /*sa_size*/);

int
hx509_verify_init_ctx (
	hx509_context /*context*/,
	hx509_verify_ctx */*ctx*/);

int
hx509_verify_path (
	hx509_context /*context*/,
	hx509_verify_ctx /*ctx*/,
	hx509_cert /*cert*/,
	hx509_certs /*pool*/);

void
hx509_verify_set_max_depth (
	hx509_verify_ctx /*ctx*/,
	unsigned int /*max_depth*/);

void
hx509_verify_set_proxy_certificate (
	hx509_verify_ctx /*ctx*/,
	int /*boolean*/);

void
hx509_verify_set_strict_rfc3280_verification (
	hx509_verify_ctx /*ctx*/,
	int /*boolean*/);

void
hx509_verify_set_time (
	hx509_verify_ctx /*ctx*/,
	time_t /*t*/);

int
hx509_verify_signature (
	hx509_context /*context*/,
	const hx509_cert /*signer*/,
	const AlgorithmIdentifier */*alg*/,
	const heim_octet_string */*data*/,
	const heim_octet_string */*sig*/);

void
hx509_xfree (void */*ptr*/);

int
yywrap (void);

#ifdef __cplusplus
}
#endif

#endif /* __hx509_protos_h__ */
